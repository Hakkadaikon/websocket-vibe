// sans-IO 接続の振る舞い仕様: フレーミング、フラグメント、制御フレームの差し込み、
// マスキング規則、プロトコル違反の検出(RFC6455 §5.4-5.6, §6)を検証する。
// 実装をホワイトボックスで include する。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/core/frame.c"
#include "../../src/core/mask.c"
#include "../../src/core/utf8.c"
#include "../../src/platform/mem.c"
#include "../../src/platform/sys.c"
#include "../../src/sdk/conn.c"

// クライアント→サーバのフレーム(マスク済み)を buf に組み立てる。全体長を返す。
static size_t mk_frame(u8 *buf, bool fin, u8 opcode, const u8 *payload, size_t plen) {
    u8 key[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    size_t n = ws_frame_build_header(buf, 14, fin, opcode, true, key, plen);
    for (size_t i = 0; i < plen; i++)
        buf[n + i] = payload[i];
    ws_mask(buf + n, plen, key);
    return n + plen;
}

static ws_conn C;
static u8 MSG[WS_MAX_MESSAGE];

static void reset(void) {
    assert(ws_conn_init(&C, WS_ROLE_SERVER, MSG, sizeof MSG));
}

static void feed_all(const u8 *buf, size_t len) {
    size_t off = 0;
    while (off < len)
        off += ws_conn_recv(&C, buf + off, len - off);
}

static void test_single_text(void) {
    reset();
    u8 f[64];
    size_t n = mk_frame(f, true, WS_OP_TEXT, (const u8 *) "hello", 5);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_MESSAGE);
    assert(ev.opcode == WS_OP_TEXT && ev.len == 5 && memcmp(ev.data, "hello", 5) == 0);
    assert(ws_conn_poll(&C, &ev) == WS_EV_NONE);
}

static void test_fragmented(void) {
    reset();
    u8 f[64];
    size_t n = mk_frame(f, false, WS_OP_TEXT, (const u8 *) "Hel", 3);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_NONE); // まだ未完
    n = mk_frame(f, true, WS_OP_CONTINUATION, (const u8 *) "lo", 2);
    feed_all(f, n);
    assert(ws_conn_poll(&C, &ev) == WS_EV_MESSAGE);
    assert(ev.len == 5 && memcmp(ev.data, "Hello", 5) == 0);
}

static void test_control_interleaved(void) {
    // ping はフラグメントの間に届くことがあり、処理中のメッセージを壊さず
    // 別個に通知されなければならない(RFC6455 §5.4)。
    reset();
    u8 f[64];
    size_t n = mk_frame(f, false, WS_OP_TEXT, (const u8 *) "ab", 2);
    feed_all(f, n);
    n = mk_frame(f, true, WS_OP_PING, (const u8 *) "pi", 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_PING);
    assert(ev.len == 2 && memcmp(ev.data, "pi", 2) == 0);
    n = mk_frame(f, true, WS_OP_CONTINUATION, (const u8 *) "cd", 2);
    feed_all(f, n);
    assert(ws_conn_poll(&C, &ev) == WS_EV_MESSAGE);
    assert(ev.len == 4 && memcmp(ev.data, "abcd", 4) == 0);
}

static void test_reject_unmasked_client_frame(void) {
    // サーバはクライアントからの非マスクフレームを拒否しなければならない(RFC6455 §5.1)。
    reset();
    u8 f[16];
    size_t n = ws_frame_build_header(f, 16, true, WS_OP_TEXT, false, NULL, 1);
    f[n] = 'x';
    feed_all(f, n + 1);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_ERROR);
}

static void test_reject_fragmented_control(void) {
    // 制御フレームはフラグメント化してはならない(RFC6455 §5.5)。
    reset();
    u8 f[16];
    size_t n = mk_frame(f, false, WS_OP_PING, (const u8 *) "x", 1);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_ERROR);
}

static void test_reject_oversize_control(void) {
    // 制御フレームのペイロード > 125 は不正(RFC6455 §5.5)。
    reset();
    u8 big[126];
    memset(big, 'z', sizeof big);
    u8 f[140];
    size_t n = mk_frame(f, true, WS_OP_PING, big, 126);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_ERROR);
}

static void test_reject_bad_utf8_text(void) {
    // 不正な UTF-8 を含むテキストフレームはプロトコルエラー(RFC6455 §8.1)。
    reset();
    u8 bad[] = {0xC0, 0x80}; // 冗長符号化
    u8 f[16];
    size_t n = mk_frame(f, true, WS_OP_TEXT, bad, 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_ERROR);
}

static void test_close_handshake(void) {
    reset();
    u8 payload[2] = {0x03, 0xE8}; // コード 1000
    u8 f[16];
    size_t n = mk_frame(f, true, WS_OP_CLOSE, payload, 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_CLOSE);
    assert(ev.close_code == 1000);
    assert(ws_conn_status(&C) == WS_ST_CLOSING || ws_conn_status(&C) == WS_ST_CLOSED);
}

static void test_send_message_unmasked_server(void) {
    reset();
    u8 out[64];
    size_t n = ws_send_message(&C, WS_OP_BINARY, (const u8 *) "hi", 2, out, sizeof out);
    assert(n == 4);               // ヘッダ 2 + ペイロード 2、非マスク
    assert((out[1] & 0x80) == 0); // サーバフレームはマスクしない
    assert(out[0] == (0x80 | WS_OP_BINARY));
    assert(out[2] == 'h' && out[3] == 'i');
}

// --- 証明済み Spec.Workflow 定理に橋渡しするテスト ---

static void test_unsolicited_pong(void) {
    // C-06: 自発的でない pong は受理され、状態は不変のまま PONG として通知される。
    reset();
    u8 f[64];
    size_t n = mk_frame(f, true, WS_OP_PONG, (const u8 *) "po", 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_PONG);
    assert(ev.len == 2 && memcmp(ev.data, "po", 2) == 0);
    assert(ws_conn_status(&C) == WS_ST_OPEN); // 状態は保たれる
}

static void test_pong_echoes_ping_payload(void) {
    // C-05: ping のペイロードを ws_send_pong に渡すと、その ping と一致する
    // ペイロードの pong が得られる(pongFor / pong_echoes_ping_payload)。
    reset();
    u8 f[64];
    size_t n = mk_frame(f, true, WS_OP_PING, (const u8 *) "data", 4);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_PING && ev.len == 4);
    u8 out[64];
    size_t m = ws_send_pong(&C, ev.data, ev.len, out, sizeof out);
    // サーバ pong: ヘッダ 2 バイト + ペイロード 4、非マスク、ペイロードは ping を反復。
    assert(m == 6 && (out[1] & 0x80) == 0);
    assert(memcmp(out + 2, "data", 4) == 0);
}

static void test_no_data_after_close_sent(void) {
    // S-03: ws_send_close の後、接続はもう OPEN ではなくデータフレームは
    // 拒否される(ws_send_message は 0 を返す)。
    reset();
    u8 out[64];
    ws_send_close(&C, 1000, out, sizeof out);
    assert(ws_conn_status(&C) != WS_ST_OPEN);
    assert(ws_send_message(&C, WS_OP_TEXT, (const u8 *) "x", 1, out, sizeof out) == 0);
}

static void test_close_handshake_roundtrip(void) {
    // S-05: ピアが先に閉じ(-> CLOSING)、こちらの返信でハンドシェイクが完了する
    // (-> CLOSED)。2 回目の ws_send_close は no-op(送信は高々 1 回)。
    reset();
    u8 f[16];
    u8 body[2] = {0x03, 0xE8}; // 1000
    size_t n = mk_frame(f, true, WS_OP_CLOSE, body, 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_CLOSE);
    assert(ws_conn_status(&C) == WS_ST_CLOSING); // ピアが閉じたがこちらは未返信
    u8 out[16];
    size_t r = ws_send_close(&C, ev.close_code, out, sizeof out);
    assert(r == 4);
    assert(ws_conn_status(&C) == WS_ST_CLOSED);            // ハンドシェイク完了
    assert(ws_send_close(&C, 1000, out, sizeof out) == 0); // べき等
}

static void test_data_discarded_after_close_recv(void) {
    // S-04: CLOSE を受信した(状態 CLOSING)後に来るデータフレームは破棄され、
    // MESSAGE イベントは生成されない。
    reset();
    u8 f[64];
    size_t n = mk_frame(f, true, WS_OP_CLOSE, (const u8 *) "\x03\xE8", 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_CLOSE);
    assert(ws_conn_status(&C) == WS_ST_CLOSING);
    // ここでテキストフレームが届く: メッセージとして通知されてはならない。
    n = mk_frame(f, true, WS_OP_TEXT, (const u8 *) "late", 4);
    feed_all(f, n);
    assert(ws_conn_poll(&C, &ev) == WS_EV_NONE); // 破棄される
}

static void test_client_mask_key_random(void) {
    // RFC6455 §5.3: クライアントフレームは毎回新しい強 RNG キーでマスクされる。
    // 検証: フレームがマスクされていること、unmask でデータが復元すること、連続する
    // 2 フレームが同じキーを使い回さないこと(= 旧来の固定キーではないこと)。
    ws_conn cc;
    u8 buf[WS_MAX_MESSAGE];
    assert(ws_conn_init(&cc, WS_ROLE_CLIENT, buf, sizeof buf));
    u8 a[32], b[32];
    size_t na = ws_send_message(&cc, WS_OP_BINARY, (const u8 *) "payload!", 8, a, sizeof a);
    size_t nb = ws_send_message(&cc, WS_OP_BINARY, (const u8 *) "payload!", 8, b, sizeof b);
    assert(na == 14 && nb == 14);           // ヘッダ 2 + マスクキー 4 + ペイロード 8
    assert((a[1] & 0x80) && (b[1] & 0x80)); // マスクビットが立つ
    // キーはバイト [2..5]。強 RNG なら 2 フレームはほぼ確実に異なる。
    assert(memcmp(a + 2, b + 2, 4) != 0);
    // unmask で元のペイロードが復元する。
    u8 key[4];
    memcpy(key, a + 2, 4);
    ws_mask(a + 6, 8, key);
    assert(memcmp(a + 6, "payload!", 8) == 0);
}

static void test_recv_invalid_close_code(void) {
    // M-07: 範囲外の close コードを受信すると 1002(Protocol Error)が通知される。
    reset();
    u8 f[16];
    u8 bad[2] = {0x03, 0xED}; // 1005 — 予約済みで、ワイヤ上に現れてはならない
    size_t n = mk_frame(f, true, WS_OP_CLOSE, bad, 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_CLOSE);
    assert(ev.close_code == 1002);
}

static void test_recv_valid_close_code(void) {
    // M-07: 妥当な close コードはそのまま通過する。
    reset();
    u8 f[16];
    u8 ok[2] = {0x03, 0xE8}; // 1000 — 妥当
    size_t n = mk_frame(f, true, WS_OP_CLOSE, ok, 2);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_CLOSE);
    assert(ev.close_code == 1000);
}

static void test_recv_close_no_status(void) {
    // M-07: payload なしの close は 1005(No Status Rcvd)センチネルで通知される。
    // Lean recvCloseCode の「長さ<2 → 1005」分岐に対応。
    reset();
    u8 f[16];
    size_t n = mk_frame(f, true, WS_OP_CLOSE, NULL, 0);
    feed_all(f, n);
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_CLOSE);
    assert(ev.close_code == 1005);
}

static void test_send_close_sanitizes_code(void) {
    // M-06: 予約コード(1006)は決して送出されてはならず、1000 に丸められる。
    reset();
    u8 out[16];
    size_t n = ws_send_close(&C, 1006, out, sizeof out);
    assert(n == 4); // ヘッダ 2 + ペイロード 2
    u16 emitted = (u16) ((out[2] << 8) | out[3]);
    assert(emitted == 1000);
}

static void test_split_across_recv(void) {
    // 別々の recv 呼び出しで届くヘッダとペイロードは集約されなければならない。
    reset();
    u8 f[64];
    size_t n = mk_frame(f, true, WS_OP_TEXT, (const u8 *) "split", 5);
    for (size_t k = 0; k < n; k++)
        ws_conn_recv(&C, f + k, 1); // 1 バイトずつ
    ws_event ev;
    assert(ws_conn_poll(&C, &ev) == WS_EV_MESSAGE);
    assert(ev.len == 5 && memcmp(ev.data, "split", 5) == 0);
}

// 同一接続で複数メッセージを連続受信する: 各メッセージを別 recv で流し、
// 間で poll してドレインする (echo サーバの serve ループと同じ順序)。
// 後続メッセージのペイロードが集約バッファ内で前メッセージ長ぶんずれた位置に
// ステージされるため、begin_message のリセットと重なって前方オーバーラップ
// コピーになり、UTF-8 検査が壊れたバイトを読む退行があった。
static void test_consecutive_text_messages(void) {
    reset();
    const char *msgs[] = {"unko", "テストテストテスト"}; // 2 件目で退行が出ていた
    ws_event ev;
    for (size_t i = 0; i < 2; i++) {
        u8 f[64];
        size_t plen = strlen(msgs[i]);
        size_t n = mk_frame(f, true, WS_OP_TEXT, (const u8 *) msgs[i], plen);
        feed_all(f, n);
        assert(ws_conn_poll(&C, &ev) == WS_EV_MESSAGE);
        assert(ev.len == plen && memcmp(ev.data, msgs[i], plen) == 0);
        assert(ws_conn_poll(&C, &ev) == WS_EV_NONE);
    }
}

int main(void) {
    test_single_text();
    test_consecutive_text_messages();
    test_fragmented();
    test_control_interleaved();
    test_reject_unmasked_client_frame();
    test_reject_fragmented_control();
    test_reject_oversize_control();
    test_reject_bad_utf8_text();
    test_close_handshake();
    test_send_message_unmasked_server();
    test_unsolicited_pong();
    test_pong_echoes_ping_payload();
    test_no_data_after_close_sent();
    test_close_handshake_roundtrip();
    test_data_discarded_after_close_recv();
    test_client_mask_key_random();
    test_recv_invalid_close_code();
    test_recv_valid_close_code();
    test_recv_close_no_status();
    test_send_close_sanitizes_code();
    test_split_across_recv();
    printf("test_conn: all passed\n");
    return 0;
}
