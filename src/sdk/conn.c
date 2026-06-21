// ws sans-IO connection state machine (RFC6455 §5-6, §8.1).
#include "ws/ws.h"

#include "core/frame.h"
#include "core/mask.h"
#include "core/utf8.h"
#include "platform/mem.h"
#include "platform/sys.h"

// Internal representation laid over the opaque ws_conn storage.
typedef struct {
    ws_role role;
    ws_conn_state state;

    u8 *msg; // caller-provided aggregation buffer
    size_t msg_cap;
    size_t msg_len;  // bytes accumulated for the current message
    u8 msg_opcode;   // opcode of the first fragment (TEXT/BINARY)
    bool in_message; // a fragmented data message is in progress

    ws_utf8_state utf8; // running UTF-8 check for text messages

    // Partial-frame staging: we buffer one frame's header (max 14) until full,
    // then stream its payload through. `rxhdr_len` bytes valid in rxhdr.
    u8 rxhdr[14];
    size_t rxhdr_len;
    bool hdr_done;          // header fully parsed; now collecting payload
    size_t rx_payload_got;  // payload bytes staged so far
    size_t rx_payload_need; // total payload bytes expected

    // Pending event produced by the last consumed frame.
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

// --- inbound classification helpers ---

static bool is_control(u8 op) {
    return (op & 0x08u) != 0;
}

static bool valid_data_opcode(u8 op) {
    return op == WS_OP_CONTINUATION || op == WS_OP_TEXT || op == WS_OP_BINARY;
}

// Control frames: must be FIN, <=125 bytes, and a known control opcode (§5.5).
static bool control_ok(const ws_frame_header *h) {
    if (!h->fin || h->payload_len > 125)
        return false;
    return h->opcode == WS_OP_CLOSE || h->opcode == WS_OP_PING || h->opcode == WS_OP_PONG;
}

// Data frames: valid opcode and consistent with fragmentation state.
static bool data_ok(const conn_impl *m, const ws_frame_header *h) {
    if (!valid_data_opcode(h->opcode))
        return false;
    bool is_cont = (h->opcode == WS_OP_CONTINUATION);
    if (is_cont != m->in_message)
        return false; // continuation iff a message is in progress
    return true;
}

// Validate a freshly parsed header against RFC6455 framing rules.
static bool header_ok(conn_impl *m, const ws_frame_header *h) {
    if (h->rsv1 || h->rsv2 || h->rsv3)
        return false;
    if (m->role == WS_ROLE_SERVER && !h->masked)
        return false; // client must mask (§5.1)
    return is_control(h->opcode) ? control_ok(h) : data_ok(m, h);
}

// --- payload assembly into the message buffer ---

static void set_error(conn_impl *m) {
    m->ev_pending = WS_EV_ERROR;
    m->ev.type = WS_EV_ERROR;
    m->state = WS_ST_CLOSED;
}

// Append data-frame payload; updates fragmentation state. Sets event when FIN.
static void accept_data(conn_impl *m, const ws_frame_header *h, const u8 *pl) {
    if (!m->in_message) {
        m->msg_len = 0;
        m->msg_opcode = h->opcode;
        m->in_message = true;
        if (h->opcode == WS_OP_TEXT)
            ws_memset(&m->utf8, 0, sizeof m->utf8);
    }
    if (m->msg_len + h->payload_len > m->msg_cap) {
        set_error(m);
        return;
    }
    ws_memcpy(m->msg + m->msg_len, pl, h->payload_len);
    m->msg_len += h->payload_len;

    if (m->msg_opcode == WS_OP_TEXT && !ws_utf8_feed(&m->utf8, pl, h->payload_len)) {
        set_error(m);
        return;
    }
    if (!h->fin)
        return; // wait for more fragments

    if (m->msg_opcode == WS_OP_TEXT && !ws_utf8_complete(&m->utf8)) {
        set_error(m); // incomplete UTF-8 at message end
        return;
    }
    m->in_message = false;
    m->ev.type = WS_EV_MESSAGE;
    m->ev.opcode = m->msg_opcode;
    m->ev.data = m->msg;
    m->ev.len = m->msg_len;
    m->ev_pending = WS_EV_MESSAGE;
}

// Valid received close codes (RFC6455 §7.4.1): 1000-1003, 1007-1011, 3000-4999.
static bool valid_close_code(u16 c) {
    return (c >= 1000 && c <= 1003) || (c >= 1007 && c <= 1011) || (c >= 3000 && c <= 4999);
}

// Reserved codes that MUST NOT appear on the wire (§7.4.1).
static bool reserved_close_code(u16 c) {
    return c == 1005 || c == 1006 || c == 1015;
}

// Control frames carry their own (un-aggregated) payload via the staging area.
static void accept_control(conn_impl *m, const ws_frame_header *h, const u8 *pl) {
    if (h->opcode == WS_OP_CLOSE) {
        u16 code = 1005; // "no status" default when no body is present
        if (h->payload_len >= 2) {
            code = (u16) ((pl[0] << 8) | pl[1]);
            if (!valid_close_code(code))
                code = 1002; // M-07: out-of-range code -> Protocol Error
        }
        m->ev.type = WS_EV_CLOSE;
        m->ev.close_code = code;
        m->ev_pending = WS_EV_CLOSE;
        m->state = (m->state == WS_ST_OPEN) ? WS_ST_CLOSING : WS_ST_CLOSED;
        return;
    }
    m->ev.type = (h->opcode == WS_OP_PING) ? WS_EV_PING : WS_EV_PONG;
    m->ev.opcode = h->opcode;
    m->ev.data = pl;
    m->ev.len = (size_t) h->payload_len;
    m->ev_pending = m->ev.type;
}

// Process one fully-staged frame (header in rxhdr, payload `pl`).
static void consume_frame(conn_impl *m, const ws_frame_header *h, u8 *pl) {
    if (!header_ok(m, h)) {
        set_error(m);
        return;
    }
    if (h->masked)
        ws_mask(pl, (size_t) h->payload_len, h->mask_key);
    if (is_control(h->opcode))
        accept_control(m, h, pl);
    else if (m->state == WS_ST_OPEN)
        accept_data(m, h, pl);
    // else S-04: a CLOSE was already received; further data is discarded (§5.5.1).
}

// Phase 1: accumulate header bytes until parseable. Advances *off.
// Returns OK (header in *h, m->hdr_done set), NEED_MORE, or ERROR.
static ws_parse_status recv_header(conn_impl *m, const u8 *buf, size_t len, size_t *off,
                                   ws_frame_header *h) {
    ws_parse_status st = WS_PARSE_NEED_MORE;
    while (*off < len && m->rxhdr_len < sizeof m->rxhdr) {
        m->rxhdr[m->rxhdr_len++] = buf[(*off)++];
        st = ws_frame_parse_header(m->rxhdr, m->rxhdr_len, h);
        if (st != WS_PARSE_NEED_MORE)
            break;
    }
    if (st == WS_PARSE_OK) {
        size_t plen = (size_t) h->payload_len;
        if (m->msg_len + plen > m->msg_cap)
            return WS_PARSE_ERROR; // would overflow aggregation buffer
        m->hdr_done = true;
        m->rx_payload_got = 0;
        m->rx_payload_need = plen;
    }
    return st;
}

// recv: returns bytes consumed from buf this call. Buffers across calls so a
// frame may be delivered in arbitrarily small chunks.
size_t ws_conn_recv(ws_conn *c, const u8 *buf, size_t len) {
    conn_impl *m = impl(c);
    if (m->ev_pending != WS_EV_NONE || m->state == WS_ST_CLOSED)
        return 0; // drain the pending event first

    size_t off = 0;
    ws_frame_header h;
    if (!m->hdr_done) {
        ws_parse_status st = recv_header(m, buf, len, &off, &h);
        if (st == WS_PARSE_ERROR) {
            set_error(m);
            return off;
        }
        if (st != WS_PARSE_OK)
            return off; // need more header bytes
    } else {
        // Re-derive the parsed header from the staged bytes.
        ws_frame_parse_header(m->rxhdr, m->rxhdr_len, &h);
    }

    // Phase 2: stage payload at the message-buffer tail (scratch; committed only
    // when accept_data advances msg_len). Persist progress across calls.
    u8 *stage = m->msg + m->msg_len;
    while (m->rx_payload_got < m->rx_payload_need && off < len)
        stage[m->rx_payload_got++] = buf[off++];
    if (m->rx_payload_got < m->rx_payload_need)
        return off; // payload incomplete; resume next call

    // Full frame staged.
    m->rxhdr_len = 0;
    m->hdr_done = false;
    m->rx_payload_got = 0;
    consume_frame(m, &h, stage);
    return off;
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

// --- outbound ---

static size_t build_frame(ws_conn *c, u8 opcode, const u8 *data, size_t len, u8 *out, size_t cap) {
    conn_impl *m = impl(c);
    bool masked = (m->role == WS_ROLE_CLIENT);
    u8 key[4];
    if (masked) {
        // RFC6455 §5.3: each masking key MUST be fresh and from a strong RNG.
        // Refuse to send rather than emit a predictable key.
        if (sys_getrandom(key, sizeof key) != (i64) sizeof key)
            return 0;
    }
    size_t hn = ws_frame_build_header(out, cap, true, opcode, masked, masked ? key : NULL, len);
    if (hn == 0 || hn + len > cap)
        return 0;
    ws_memcpy(out + hn, data, len);
    if (masked)
        ws_mask(out + hn, len, key);
    return hn + len;
}

size_t ws_send_message(ws_conn *c, u8 opcode, const u8 *data, size_t len, u8 *out, size_t cap) {
    return build_frame(c, opcode, data, len, out, cap);
}
size_t ws_send_ping(ws_conn *c, const u8 *data, size_t len, u8 *out, size_t cap) {
    return build_frame(c, WS_OP_PING, data, len, out, cap);
}
size_t ws_send_pong(ws_conn *c, const u8 *data, size_t len, u8 *out, size_t cap) {
    return build_frame(c, WS_OP_PONG, data, len, out, cap);
}
size_t ws_send_close(ws_conn *c, u16 code, u8 *out, size_t cap) {
    if (reserved_close_code(code))
        code = 1000; // M-06: never emit 1005/1006/1015 on the wire
    u8 payload[2] = {(u8) (code >> 8), (u8) (code & 0xFF)};
    impl(c)->state = WS_ST_CLOSING;
    return build_frame(c, WS_OP_CLOSE, payload, 2, out, cap);
}
