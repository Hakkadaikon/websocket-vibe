// ws sans-IO コネクション状態機械 (RFC6455 §5-6, §8.1)。
#include "ws/ws.h"

#include "core/frame.h"
#include "core/mask.h"
#include "core/utf8.h"
#include "platform/mem.h"
#include "platform/sys.h"

// 不透明な ws_conn ストレージ上に重ねた内部表現。
typedef struct {
    ws_role role;
    ws_conn_state state;

    u8 *msg; // 呼び出し側が用意する集約バッファ
    size_t msg_cap;
    size_t msg_len;  // 現在のメッセージに溜めたバイト数
    u8 msg_opcode;   // 最初のフラグメントの opcode (TEXT/BINARY)
    bool in_message; // フラグメント化されたデータメッセージが進行中
    bool close_sent; // すでに Close フレームを送出済み (S-03/S-05)

    ws_utf8_state utf8; // テキストメッセージ用の継続的な UTF-8 検査

    // 部分フレームのステージング: 1 フレームのヘッダ (最大 14) が揃うまで溜め、
    // 揃ったらペイロードを順に流す。rxhdr のうち `rxhdr_len` バイトが有効。
    u8 rxhdr[14];
    size_t rxhdr_len;
    bool hdr_done;          // ヘッダの解析が完了し、ペイロード収集中
    size_t rx_payload_got;  // これまでにステージしたペイロードのバイト数
    size_t rx_payload_need; // 期待されるペイロードの総バイト数

    // 直前に処理したフレームが生成した保留中のイベント。
    ws_event ev;
    ws_event_type ev_pending;
} conn_impl;

_Static_assert(sizeof(conn_impl) <= WS_CONN_SIZE, "conn_impl exceeds WS_CONN_SIZE");

static conn_impl *impl(ws_conn *c) {
    return (conn_impl *) c->_opaque;
}

bool ws_conn_init(ws_conn *c, ws_role role, u8 *msg_buf, size_t msg_cap) {
    if (sizeof(conn_impl) > WS_CONN_SIZE)
        return false;
    conn_impl *m = impl(c);
    ws_memset(m, 0, sizeof *m);
    m->role = role;
    m->state = WS_ST_OPEN;
    m->msg = msg_buf;
    m->msg_cap = msg_cap;
    return true;
}

ws_conn_state ws_conn_status(const ws_conn *c) {
    return ((const conn_impl *) c->_opaque)->state;
}

// --- 受信フレームの分類ヘルパー ---

static bool is_control(u8 op) {
    return (op & 0x08u) != 0;
}

static bool valid_data_opcode(u8 op) {
    return op == WS_OP_CONTINUATION || op == WS_OP_TEXT || op == WS_OP_BINARY;
}

static bool valid_control_opcode(u8 op) {
    return op == WS_OP_CLOSE || op == WS_OP_PING || op == WS_OP_PONG;
}

// 制御フレーム: FIN かつ 125 バイト以下、既知の制御 opcode でなければならない (§5.5)。
static bool control_ok(const ws_frame_header *h) {
    bool sized = h->fin && h->payload_len <= 125;
    return sized && valid_control_opcode(h->opcode);
}

// データフレーム: 有効な opcode で、かつフラグメント状態と整合していること。
static bool data_ok(const conn_impl *m, const ws_frame_header *h) {
    if (!valid_data_opcode(h->opcode))
        return false;
    bool is_cont = (h->opcode == WS_OP_CONTINUATION);
    if (is_cont != m->in_message)
        return false; // 継続フレームはメッセージ進行中のときに限り正当
    return true;
}

static bool rsv_set(const ws_frame_header *h) {
    return h->rsv1 || h->rsv2 || h->rsv3;
}

// マスク規則 (§5.1): サーバはクライアントからマスク済みフレームを受け取らねばならない。
static bool mask_ok(const conn_impl *m, const ws_frame_header *h) {
    return m->role != WS_ROLE_SERVER || h->masked;
}

// opcode 固有のフレーミング規則: 制御フレームかデータフレームかで分ける。
static bool body_ok(const conn_impl *m, const ws_frame_header *h) {
    return is_control(h->opcode) ? control_ok(h) : data_ok(m, h);
}

// 解析したばかりのヘッダを RFC6455 のフレーミング規則に照らして検証する。
static bool header_ok(conn_impl *m, const ws_frame_header *h) {
    return !rsv_set(h) && mask_ok(m, h) && body_ok(m, h);
}

// --- メッセージバッファへのペイロード組み立て ---

static void set_error(conn_impl *m) {
    m->ev_pending = WS_EV_ERROR;
    m->ev.type = WS_EV_ERROR;
    m->state = WS_ST_CLOSED;
}

// 最初の (継続でない) フラグメントで、新しい集約メッセージを開始する。
static void begin_message(conn_impl *m, const ws_frame_header *h) {
    m->msg_len = 0;
    m->msg_opcode = h->opcode;
    m->in_message = true;
    if (h->opcode == WS_OP_TEXT)
        ws_memset(&m->utf8, 0, sizeof m->utf8);
}

// 1 フラグメント分のペイロードを追加し、継続的な UTF-8 検査を進める。
// pl は recv バッファ上の位置 (旧 msg_len 起点) を指す。begin_message が
// msg_len を 0 に戻すと dst (msg+msg_len) と pl が重なり前方コピーになるため、
// ws_memmove で安全にずらし、検査は確定後の dst 側から読む。
static bool append_payload(conn_impl *m, const ws_frame_header *h, const u8 *pl) {
    if (m->msg_len + h->payload_len > m->msg_cap)
        return false;
    u8 *dst = m->msg + m->msg_len;
    ws_memmove(dst, pl, h->payload_len);
    m->msg_len += h->payload_len;
    bool is_text = (m->msg_opcode == WS_OP_TEXT);
    return !is_text || ws_utf8_feed(&m->utf8, dst, h->payload_len);
}

// 必要ならメッセージを開始し、このフラグメントのペイロードを追加する。
static bool stage_data(conn_impl *m, const ws_frame_header *h, const u8 *pl) {
    if (!m->in_message)
        begin_message(m, h);
    return append_payload(m, h, pl);
}

// FIN で集約メッセージを確定する。MESSAGE を発行、不正な UTF-8 ならエラーにする。
static void finish_message(conn_impl *m) {
    bool bad_text = (m->msg_opcode == WS_OP_TEXT) && !ws_utf8_complete(&m->utf8);
    if (bad_text) {
        set_error(m); // メッセージ末尾で UTF-8 が不完全
        return;
    }
    m->in_message = false;
    m->ev.type = WS_EV_MESSAGE;
    m->ev.opcode = m->msg_opcode;
    m->ev.data = m->msg;
    m->ev.len = m->msg_len;
    m->ev_pending = WS_EV_MESSAGE;
}

// データフレームのペイロードを追加し、フラグメント状態を更新する。FIN ならイベントを設定。
static void accept_data(conn_impl *m, const ws_frame_header *h, const u8 *pl) {
    if (!stage_data(m, h, pl)) {
        set_error(m);
        return;
    }
    if (h->fin)
        finish_message(m);
}

static bool in_range(u16 c, u16 lo, u16 hi) {
    return c >= lo && c <= hi;
}

// 受信時に有効なクローズコード (RFC6455 §7.4.1): 1000-1003, 1007-1011, 3000-4999。
static bool valid_close_code(u16 c) {
    return in_range(c, 1000, 1003) || in_range(c, 1007, 1011) || in_range(c, 3000, 4999);
}

// ワイヤ上に現れてはならない予約コード (§7.4.1)。
static bool reserved_close_code(u16 c) {
    return c == WS_CLOSE_NO_STATUS || c == WS_CLOSE_ABNORMAL || c == WS_CLOSE_TLS;
}

// CLOSE 本体からクローズコードをデコードする (§7.4.1)。不正なコードは 1002 に写像する。
static u16 close_code_from(const ws_frame_header *h, const u8 *pl) {
    if (h->payload_len < 2)
        return WS_CLOSE_NO_STATUS; // 本体がないときの「ステータスなし」既定値
    u16 code = (u16) ((pl[0] << 8) | pl[1]);
    return valid_close_code(code) ? code
                                  : WS_CLOSE_PROTOCOL; // M-07: 範囲外は Protocol Error にする
}

// 受信した CLOSE を処理する: CLOSE イベントを発行し、ハンドシェイク状態を進める。
static void accept_close(conn_impl *m, const ws_frame_header *h, const u8 *pl) {
    m->ev.type = WS_EV_CLOSE;
    m->ev.close_code = close_code_from(h, pl);
    m->ev_pending = WS_EV_CLOSE;
    m->state = (m->state == WS_ST_OPEN) ? WS_ST_CLOSING : WS_ST_CLOSED;
}

// 受信した PING/PONG を処理する: その (集約しない) ペイロードをそのまま渡す。
static void accept_ping_pong(conn_impl *m, const ws_frame_header *h, const u8 *pl) {
    m->ev.type = (h->opcode == WS_OP_PING) ? WS_EV_PING : WS_EV_PONG;
    m->ev.opcode = h->opcode;
    m->ev.data = pl;
    m->ev.len = (size_t) h->payload_len;
    m->ev_pending = m->ev.type;
}

// 制御フレームは自身の (集約しない) ペイロードをステージング領域経由で運ぶ。
static void accept_control(conn_impl *m, const ws_frame_header *h, const u8 *pl) {
    if (h->opcode == WS_OP_CLOSE)
        accept_close(m, h, pl);
    else
        accept_ping_pong(m, h, pl);
}

// 検証済みでマスク解除されたフレームを制御またはデータのハンドラへ振り分ける。
static void dispatch_frame(conn_impl *m, const ws_frame_header *h, u8 *pl) {
    if (is_control(h->opcode))
        accept_control(m, h, pl);
    else if (m->state == WS_ST_OPEN)
        accept_data(m, h, pl);
    // それ以外 S-04: すでに CLOSE を受信済み。以降のデータは破棄する (§5.5.1)。
}

// 完全にステージされた 1 フレームを処理する (ヘッダは rxhdr、ペイロードは `pl`)。
static void consume_frame(conn_impl *m, const ws_frame_header *h, u8 *pl) {
    if (!header_ok(m, h)) {
        set_error(m);
        return;
    }
    if (h->masked)
        ws_mask(pl, (size_t) h->payload_len, h->mask_key);
    dispatch_frame(m, h, pl);
}

// ステージできるヘッダバイトがまだある (入力が残り、かつ空き領域もある)。
static bool header_wants_byte(const conn_impl *m, size_t len, size_t off) {
    return off < len && m->rxhdr_len < sizeof m->rxhdr;
}

// ヘッダが解析できる (または入力が尽きる) まで、溜めたバイトを 1 つずつ送り込む。
static ws_parse_status accumulate_header(conn_impl *m, const u8 *buf, size_t len, size_t *off,
                                         ws_frame_header *h) {
    ws_parse_status st = WS_PARSE_NEED_MORE;
    while (st == WS_PARSE_NEED_MORE && header_wants_byte(m, len, *off)) {
        m->rxhdr[m->rxhdr_len++] = buf[(*off)++];
        st = ws_frame_parse_header(m->rxhdr, m->rxhdr_len, h);
    }
    return st;
}

// 解析済みヘッダをペイロード段階に向けて確定する。オーバーフローするなら拒否する。
static ws_parse_status begin_payload(conn_impl *m, const ws_frame_header *h) {
    size_t plen = (size_t) h->payload_len;
    if (m->msg_len + plen > m->msg_cap)
        return WS_PARSE_ERROR; // 集約バッファをオーバーフローさせてしまう
    m->hdr_done = true;
    m->rx_payload_got = 0;
    m->rx_payload_need = plen;
    return WS_PARSE_OK;
}

// フェーズ 1: 解析可能になるまでヘッダバイトを溜める。*off を進める。
// OK (ヘッダを *h に格納し m->hdr_done を設定)、NEED_MORE、または ERROR を返す。
static ws_parse_status recv_header(conn_impl *m, const u8 *buf, size_t len, size_t *off,
                                   ws_frame_header *h) {
    ws_parse_status st = accumulate_header(m, buf, len, off, h);
    if (st == WS_PARSE_OK)
        return begin_payload(m, h);
    return st;
}

// m->hdr_done が成立し h が埋まった状態を保証する。解析ステータスを返す:
// OK -> ヘッダ準備完了、NEED_MORE -> 溜めた (後で再開)、ERROR -> set_error 済み。
static ws_parse_status ensure_header(conn_impl *m, const u8 *buf, size_t len, size_t *off,
                                     ws_frame_header *h) {
    if (m->hdr_done) {
        ws_frame_parse_header(m->rxhdr, m->rxhdr_len, h); // ステージ済みバイトから再導出
        return WS_PARSE_OK;
    }
    ws_parse_status st = recv_header(m, buf, len, off, h);
    if (st == WS_PARSE_ERROR)
        set_error(m);
    return st;
}

// フェーズ 2: ペイロードをメッセージバッファ末尾にステージする (一時領域。
// accept_data が msg_len を進めて初めて確定)。進捗は呼び出しをまたいで保持する。
// ペイロードが全てステージされたら true を返す。
static bool stage_payload(conn_impl *m, const u8 *buf, size_t len, size_t *off) {
    u8 *stage = m->msg + m->msg_len;
    while (m->rx_payload_got < m->rx_payload_need && *off < len)
        stage[m->rx_payload_got++] = buf[(*off)++];
    return m->rx_payload_got >= m->rx_payload_need;
}

// イベントが保留中 (先にドレインが必要) か、クローズ済みの間は recv をブロックする。
static bool recv_blocked(const conn_impl *m) {
    return m->ev_pending != WS_EV_NONE || m->state == WS_ST_CLOSED;
}

// recv: この呼び出しで buf から消費したバイト数を返す。呼び出しをまたいでバッファ
// するため、フレームは任意の小さなチャンクに分割されて届いてもよい。
// 1 フレーム分のヘッダ + ペイロードのステージングを駆動し、buf から消費する。
// 消費バイト数を返す。完全にステージされたらフレームを処理し、未完なら後で再開する。
static size_t recv_frame(conn_impl *m, const u8 *buf, size_t len) {
    size_t off = 0;
    ws_frame_header h;
    if (ensure_header(m, buf, len, &off, &h) != WS_PARSE_OK)
        return off; // ヘッダバイトが足りない (またはすでにエラー設定済み)
    if (!stage_payload(m, buf, len, &off))
        return off; // ペイロード未完。次の呼び出しで再開する

    // フレーム全体がステージされた。
    m->rxhdr_len = 0;
    m->hdr_done = false;
    m->rx_payload_got = 0;
    consume_frame(m, &h, m->msg + m->msg_len);
    return off;
}

size_t ws_conn_recv(ws_conn *c, const u8 *buf, size_t len) {
    conn_impl *m = impl(c);
    return recv_blocked(m) ? 0 : recv_frame(m, buf, len);
}

ws_event_type ws_conn_poll(ws_conn *c, ws_event *ev) {
    conn_impl *m = impl(c);
    if (m->ev_pending == WS_EV_NONE)
        return WS_EV_NONE;
    *ev = m->ev;
    ws_event_type t = m->ev_pending;
    m->ev_pending = WS_EV_NONE;
    return t;
}

// --- 送信 ---

// ヘッダビルダに渡すマスキングキーを決める。クライアントの場合は新しいキーを
// 生成して (RFC6455 §5.3) *kp 経由で返す。マスクしないフレームでは *kp=NULL。
// RNG 失敗時は false を返し、呼び出し側が送信を拒否する (弱いキーを使わせない)。
static bool resolve_mask_key(bool masked, u8 key[4], const u8 **kp) {
    *kp = NULL;
    if (!masked)
        return true;
    *kp = key;
    return sys_getrandom(key, 4) == (i64) 4;
}

// ヘッダ長が 0 (組み立て失敗)、または `cap` を超えるものは使えない。
static bool frame_fits(size_t hn, size_t len, size_t cap) {
    return hn != 0 && hn + len <= cap;
}

// `hn` バイトのヘッダの後ろに `len` バイトのペイロードを追加し、必要ならマスクする。
static size_t finish_frame(u8 *out, size_t cap, size_t hn, const u8 *data, size_t len, bool masked,
                           const u8 key[4]) {
    if (!frame_fits(hn, len, cap))
        return 0;
    ws_memcpy(out + hn, data, len);
    if (masked)
        ws_mask(out + hn, len, key);
    return hn + len;
}

static size_t build_frame(ws_conn *c, u8 opcode, const u8 *data, size_t len, u8 *out, size_t cap) {
    bool masked = (impl(c)->role == WS_ROLE_CLIENT);
    u8 key[4];
    const u8 *kp;
    if (!resolve_mask_key(masked, key, &kp))
        return 0; // RNG 失敗: 予測可能なキーを出すより送信を拒否する
    size_t hn = ws_frame_build_header(out, cap, true, opcode, masked, kp, len);
    return finish_frame(out, cap, hn, data, len, masked, key);
}

size_t ws_send_message(ws_conn *c, u8 opcode, const u8 *data, size_t len, u8 *out, size_t cap) {
    // S-03: 一度 Close を送ったら、以降データフレームを送ってはならない。
    if (impl(c)->close_sent)
        return 0;
    return build_frame(c, opcode, data, len, out, cap);
}
size_t ws_send_ping(ws_conn *c, const u8 *data, size_t len, u8 *out, size_t cap) {
    return build_frame(c, WS_OP_PING, data, len, out, cap);
}
size_t ws_send_pong(ws_conn *c, const u8 *data, size_t len, u8 *out, size_t cap) {
    return build_frame(c, WS_OP_PONG, data, len, out, cap);
}
// 自分の Close を送ったことを記録し、クローズハンドシェイク状態を進める。
// 相手がすでにクローズ済み (こちらが CLOSING だった) なら、ハンドシェイク完了で
// CLOSED。そうでなければこちらがクローズを開始し CLOSING。
static void mark_close_sent(conn_impl *m) {
    m->close_sent = true;
    m->state = (m->state == WS_ST_CLOSING) ? WS_ST_CLOSED : WS_ST_CLOSING;
}

// M-06: 予約ステータス (1005/1006/1015) はワイヤ上に決して送出しない。
static u16 sanitize_close_code(u16 code) {
    return reserved_close_code(code) ? WS_CLOSE_NORMAL : code;
}

size_t ws_send_close(ws_conn *c, u16 code, u8 *out, size_t cap) {
    conn_impl *m = impl(c);
    // S-05: Close は高々 1 回だけ送る。すでに応答・開始済みなら何もしない。
    if (m->close_sent)
        return 0;
    code = sanitize_close_code(code);
    u8 payload[2] = {(u8) (code >> 8), (u8) (code & 0xFF)};
    size_t n = build_frame(c, WS_OP_CLOSE, payload, 2, out, cap);
    if (n != 0)
        mark_close_sent(m);
    return n;
}
