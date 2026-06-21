// ws — SDK を端から端まで示す最小のフリースタンディング echo サーバ。
// libc に依存しない: 独自 _start、syscall のみ。epoll で多重化し多数の同時接続を扱う。
// データメッセージをエコーし、ping には pong を返し、クローズハンドシェイクを完了する。
#include "ws/ws.h"

#include "platform/net.h"
#include "platform/sys.h"

#include "core/frame.h"
#include "core/mask.h"
#include "core/utf8.h"
#include "platform/mem.h"
#include "protocol/handshake.h"

#define PORT     9001
#define MAX_CONN 64
#define RXBUF    (1u << 16)
// 接続ごとのデモ用集約上限。SDK の既定 1 MiB より小さくし、MAX_CONN 個のスロットの
// .bss を ~64 MiB ではなく ~8 MiB に抑える (E2E のペイロードには十分収まる)。
#define MSGBUF (128u << 10)
#define TXBUF  (MSGBUF + 16)
#define HSBUF  2048 // アップグレードリクエストのステージング領域

// 接続ごとの状態。集約バッファは接続ごとに持つ必要がある。さもないと、
// 交互に処理されるクライアントが互いの処理中メッセージを破壊してしまう。
typedef struct {
    int fd; // -1 はスロット空き
    bool hs_done;
    size_t hs_len;
    u8 hs[HSBUF];
    ws_conn conn;
    u8 msg[MSGBUF];
} client;

static client g_clients[MAX_CONN];
static u8 g_rx[RXBUF]; // 共有の一時領域: 一度に 1 接続ずつ処理する
static u8 g_tx[TXBUF];

static client *slot_alloc(int fd) {
    for (int i = 0; i < MAX_CONN; i++) {
        if (g_clients[i].fd < 0) {
            client *c = &g_clients[i];
            ws_memset(c, 0, sizeof *c);
            c->fd = fd;
            return c;
        }
    }
    return NULL; // テーブル満杯
}

static client *slot_find(int fd) {
    for (int i = 0; i < MAX_CONN; i++)
        if (g_clients[i].fd == fd)
            return &g_clients[i];
    return NULL;
}

static bool write_all(int fd, const u8 *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        i64 n = sys_write(fd, buf + off, len - off);
        if (n <= 0)
            return false;
        off += (size_t) n;
    }
    return true;
}

static bool hs_complete(const client *c) {
    return c->hs_len >= 4 && ws_memcmp(c->hs + c->hs_len - 4, "\r\n\r\n", 4) == 0;
}

// ステージ済みリクエストに対する 101 レスポンスを組み立てて送る。成功で true を返す。
// リクエストが不正、または書き込み失敗のとき *fail を立てる。
static bool hs_respond(client *c, bool *fail) {
    const char *key;
    size_t klen = ws_handshake_find_key((const char *) c->hs, c->hs_len, &key);
    if (klen == 0) {
        *fail = true;
        return false;
    }
    char accept[WS_ACCEPT_KEY_LEN + 1];
    ws_handshake_accept(key, klen, accept);
    char resp[256];
    size_t rlen = ws_handshake_response(accept, resp, sizeof resp);
    *fail = !write_all(c->fd, (const u8 *) resp, rlen);
    return !*fail;
}

// \r\n\r\n がステージされたら HTTP アップグレードの完了を試みる。101 レスポンスを
// 送信できたら true (hs_done を設定)、まだ不完全なら false を返し、リクエストが
// 不正なら *fail を立てる。
static bool try_handshake(client *c, bool *fail) {
    *fail = false;
    if (!hs_complete(c))
        return false; // ヘッダバイトがまだ足りない
    if (!hs_respond(c, fail))
        return false;
    c->hs_done = true;
    ws_conn_init(&c->conn, WS_ROLE_SERVER, c->msg, MSGBUF);
    return true;
}

// ステージした n バイト (あれば) を送り、接続は開いたままにする。
static bool reply_open(client *c, size_t n) {
    return n && write_all(c->fd, g_tx, n);
}

// ステージした n バイト (あれば) を送り、接続を閉じる。
static bool reply_close(client *c, size_t n) {
    if (n)
        write_all(c->fd, g_tx, n);
    return false;
}

// データを伴うイベント (message/ping): エコーまたは pong を返し、接続を保つ。
// *handled は ev->type がこれらのいずれかだったかを示す。
static bool handle_data_event(client *c, const ws_event *ev, bool *handled) {
    *handled = true;
    if (ev->type == WS_EV_MESSAGE)
        return reply_open(c, ws_send_message(&c->conn, ev->opcode, ev->data, ev->len, g_tx, TXBUF));
    if (ev->type == WS_EV_PING)
        return reply_open(c, ws_send_pong(&c->conn, ev->data, ev->len, g_tx, TXBUF));
    *handled = false;
    return true;
}

// 相手のクローズコードをそのまま返す。「コードなし」のセンチネルは 1000 に写像する。
static u16 echo_close_code(u16 code) {
    return code == 1005 ? 1000 : code;
}

// 終了系イベント (close/error): 対応する close を送ってから接続を閉じる。
static bool handle_close_event(client *c, const ws_event *ev) {
    if (ev->type == WS_EV_ERROR)
        return reply_close(c, ws_send_close(&c->conn, 1002, g_tx, TXBUF));
    if (ev->type == WS_EV_CLOSE)
        return reply_close(c,
                           ws_send_close(&c->conn, echo_close_code(ev->close_code), g_tx, TXBUF));
    return true;
}

// ドレインした 1 イベントを処理する。接続を終了させるなら false を返す。
static bool handle_event(client *c, const ws_event *ev) {
    bool handled;
    bool keep = handle_data_event(c, ev, &handled);
    return handled ? keep : handle_close_event(c, ev);
}

// 接続に現在溜まっている全イベントをドレインして処理する。
// 接続を閉じるなら false を返す。
static bool drain_events(client *c) {
    ws_event ev;
    while (ws_conn_poll(&c->conn, &ev) != WS_EV_NONE)
        if (!handle_event(c, &ev))
            return false;
    return true;
}

// 接続が進展せず、もはや OPEN でない (詰まっている) とき真。
static bool feed_stalled(const client *c, size_t used) {
    return used == 0 && ws_conn_status(&c->conn) != WS_ST_OPEN;
}

// *off から始まる 1 スライスを消費する: そのイベントを処理し *off を進める。
// 接続を閉じるなら false を返す。
static bool feed_step(client *c, const u8 *buf, size_t len, size_t *off) {
    size_t used = ws_conn_recv(&c->conn, buf + *off, len - *off);
    if (!drain_events(c) || feed_stalled(c, used))
        return false;
    *off += used;
    return true;
}

// 読み込んだばかりのチャンクを接続に流し込む。接続を閉じるなら false を返す。
static bool feed_frames(client *c, const u8 *buf, size_t len) {
    size_t off = 0;
    while (off < len)
        if (!feed_step(c, buf, len, &off))
            return false;
    return true;
}

// 1 回の読み込み + 処理ループの結果。
typedef enum { STEP_MORE, STEP_KEEP, STEP_DROP } step;

// c->hs に読み込んだばかりの n バイト分、ハンドシェイクを進める。
static step hs_step(client *c, size_t n) {
    c->hs_len += n;
    bool fail = false;
    try_handshake(c, &fail); // ヘッダ未完なら追加バイトを待ってループするだけ
    return fail ? STEP_DROP : STEP_MORE;
}

// アクティブ段階で 1 回読む。sys_read のバイト数または <=0 を返す。
// step は返さず、n の分類は呼び出し側が行う。
static i64 service_read(client *c) {
    if (c->hs_done)
        return sys_read(c->fd, g_rx, RXBUF);
    if (c->hs_len >= HSBUF)
        return 0; // リクエストが大きすぎる: 相手切断扱いにして破棄する
    return sys_read(c->fd, c->hs + c->hs_len, HSBUF - c->hs_len);
}

// アクティブ段階で読み込んだ n>0 バイトを処理する。
static step service_consume(client *c, size_t n) {
    if (!c->hs_done)
        return hs_step(c, n);
    return feed_frames(c, g_rx, n) ? STEP_MORE : STEP_DROP;
}

// 現在の段階に応じて 1 回読み、その結果を処理する。
static step service_step(client *c) {
    i64 n = service_read(c);
    if (n == 0)
        return STEP_DROP; // 相手が切断 (またはリクエストが大きすぎる)
    if (n < 0)
        return STEP_KEEP; // EAGAIN: 今は読むものがない。接続は保つ
    return service_consume(c, (size_t) n);
}

// 1 クライアントから読めるものを EAGAIN まで全て読み切る。接続を閉じるなら false を返す。
static bool service_client(client *c) {
    for (;;) {
        step s = service_step(c);
        if (s == STEP_MORE)
            continue;
        return s == STEP_KEEP;
    }
}

// fd を PORT にバインドして listen を開始する。成功で 0、失敗で <0 を返す。
static int bind_listen(int fd) {
    int one = 1;
    sys_setsockopt(fd, WS_SOL_SOCKET, WS_SO_REUSEADDR, &one, sizeof one);
    ws_sockaddr_in addr = {0};
    addr.sin_family = WS_AF_INET;
    addr.sin_port = ws_htons(PORT);
    addr.sin_addr = WS_INADDR_ANY;
    if (sys_bind(fd, &addr, sizeof addr) < 0)
        return -1;
    return sys_listen(fd, 16) < 0 ? -1 : 0;
}

static int listen_socket(void) {
    int fd = sys_socket(WS_AF_INET, WS_SOCK_STREAM, 0);
    if (fd < 0)
        return fd;
    return bind_listen(fd) < 0 ? -1 : fd;
}

static void ep_add(int epfd, int fd) {
    ws_epoll_event ev = {.events = WS_EPOLLIN, .data = (u64) fd};
    sys_epoll_ctl(epfd, WS_EPOLL_CTL_ADD, fd, &ev);
}

static void drop_client(int epfd, client *c) {
    sys_epoll_ctl(epfd, WS_EPOLL_CTL_DEL, c->fd, NULL);
    sys_close(c->fd);
    c->fd = -1;
}

// accept した fd を空きスロットに登録する。テーブルが満杯なら拒否する。
static void admit_client(int epfd, int cfd) {
    if (!slot_alloc(cfd)) {
        sys_close(cfd); // テーブル満杯: 拒否する
        return;
    }
    sys_set_nonblock(cfd);
    ep_add(epfd, cfd);
}

// listen ソケットで保留中の接続をすべて空きスロットへ accept する。
static void accept_all(int epfd, int lfd) {
    for (;;) {
        int cfd = sys_accept(lfd, NULL, NULL);
        if (cfd < 0)
            break;
        admit_client(epfd, cfd);
    }
}

// 既知のクライアント fd を処理し、接続を閉じるべきなら破棄する。
static void on_client(int epfd, int fd) {
    client *c = slot_find(fd);
    if (c && !service_client(c))
        drop_client(epfd, c);
}

// 準備できた fd を 1 つ処理する: リスナーなら accept、それ以外はクライアントを処理。
static void on_ready(int epfd, int lfd, int fd) {
    if (fd == lfd)
        accept_all(epfd, lfd);
    else
        on_client(epfd, fd);
}

static void clients_init(void) {
    for (int i = 0; i < MAX_CONN; i++)
        g_clients[i].fd = -1;
}

// リスナーと epoll を作り、リスナーを登録する。epoll fd を *epfd に、
// listen fd を *lfd に返す。失敗時は <0。
static int setup(int *epfd, int *lfd) {
    clients_init();
    *lfd = listen_socket();
    if (*lfd < 0)
        return -1;
    sys_set_nonblock(*lfd);
    *epfd = sys_epoll_create1(0);
    return *epfd < 0 ? -1 : (ep_add(*epfd, *lfd), 0);
}

// epoll でブロックし、準備できた fd をすべて処理する。決して return しない。
static void event_loop(int epfd, int lfd) {
    ws_epoll_event evs[MAX_CONN + 1];
    for (;;) {
        int nready = sys_epoll_wait(epfd, evs, MAX_CONN + 1, -1);
        for (int i = 0; i < nready; i++)
            on_ready(epfd, lfd, (int) evs[i].data);
    }
}

static int run(void) {
    int epfd, lfd;
    if (setup(&epfd, &lfd) < 0)
        return 1;
    event_loop(epfd, lfd);
    return 0; // 到達しない: event_loop は決して return しない
}

// フリースタンディングのエントリポイント。カーネルは RSP を 16 バイト境界に
// 揃えてここへ飛ぶが、SysV ABI は関数入口で RSP%16==8 を期待する (CALL 直後と同じ)。
// 揃えてから call することで、呼び出し先の SSE 命令 (movaps) がフォルトしないようにする。
// _start はプロセスのエントリシンボルとして必須で、予約名を使う必要がある。
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c)
__attribute__((naked, noreturn)) void _start(void) {
    __asm__ volatile("xor %rbp, %rbp\n\t" // 最外フレームであることを示す
                     "and $-16, %rsp\n\t" // 16 バイト境界に揃える
                     "call ws_main\n\t"); // 入口で RSP%16==8、ABI に適合
}

// 正しく整列したスタックで呼ばれる本来のエントリ。
__attribute__((used, noreturn)) void ws_main(void) {
    sys_exit(run());
}
