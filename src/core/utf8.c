#include "core/utf8.h"

// 証明済みの分岐境界 (WsProof.Utf8) に一致する先頭バイトのディスパッチ:
// シーケンス長と、最初の継続バイトに許される範囲を設定する
// (以降の継続バイトは常に 0x80..0xBF)。
// `b` が有効な先頭/ASCII バイトでなければ false を返す。
static void set_seq(ws_utf8_state *s, u8 remaining, u8 lo, u8 hi) {
    s->remaining = remaining;
    s->lo = lo;
    s->hi = hi;
}

// 3 バイト先頭: E0 は冗長符号化を禁止 (最初の継続 >= A0)、ED はサロゲートを禁止
// (最初の継続 <= 9F)、それ以外は汎用の 80..BF 範囲。
static void lead_3byte(ws_utf8_state *s, u8 b) {
    u8 lo = (b == 0xE0) ? 0xA0 : 0x80;
    u8 hi = (b == 0xED) ? 0x9F : 0xBF;
    set_seq(s, 2, lo, hi);
}

// 4 バイト先頭: F0 は冗長符号化を禁止 (最初の継続 >= 90)、F4 は U+10FFFF で上限
// (最初の継続 <= 8F)、それ以外は汎用範囲。
static void lead_4byte(ws_utf8_state *s, u8 b) {
    u8 lo = (b == 0xF0) ? 0x90 : 0x80;
    u8 hi = (b == 0xF4) ? 0x8F : 0xBF;
    set_seq(s, 3, lo, hi);
}

// ASCII (1 バイト)。すでに完結したシーケンス。
static void lead_ascii(ws_utf8_state *s) {
    s->remaining = 0;
}

// 2 バイト先頭 C2..DF: 汎用の継続範囲。
static void lead_2byte(ws_utf8_state *s) {
    set_seq(s, 1, 0x80, 0xBF);
}

// 各テスターは先頭バイトの追加バイト数 (0=ASCII,1..3) を返す。該当しなければ -1。
static int try_ascii(ws_utf8_state *s, u8 b) {
    if (b > 0x7F)
        return -1;
    lead_ascii(s);
    return 0;
}

static int try_2byte(ws_utf8_state *s, u8 b) {
    if (b < 0xC2 || b > 0xDF)
        return -1;
    lead_2byte(s);
    return 1;
}

static int try_3byte(ws_utf8_state *s, u8 b) {
    if (b < 0xE0 || b > 0xEF)
        return -1;
    lead_3byte(s, b);
    return 2;
}

static int try_4byte(ws_utf8_state *s, u8 b) {
    if (b < 0xF0 || b > 0xF4)
        return -1;
    lead_4byte(s, b);
    return 3;
}

typedef int (*lead_tester)(ws_utf8_state *, u8);

static bool classify_lead(ws_utf8_state *s, u8 b) {
    // 0x80..0xC1 (冗長な C0/C1 を含む) と 0xF5..0xFF はどれにも該当しない。
    static const lead_tester testers[] = {try_ascii, try_2byte, try_3byte, try_4byte};
    for (size_t k = 0; k < sizeof(testers) / sizeof(testers[0]); k++) {
        if (testers[k](s, b) >= 0)
            return true;
    }
    return false;
}

// 継続バイト: 現在許容されている範囲に収まらなければならない。
static bool cont_in_range(const ws_utf8_state *s, u8 b) {
    return b >= s->lo && b <= s->hi;
}

static bool feed_cont(ws_utf8_state *s, u8 b) {
    if (!cont_in_range(s, b))
        return false;
    s->remaining--;
    // 最初の継続バイト以降は範囲が汎用のものに緩和される。
    s->lo = 0x80;
    s->hi = 0xBF;
    return true;
}

static bool feed_byte(ws_utf8_state *s, u8 b) {
    if (s->remaining == 0)
        return classify_lead(s, b);
    return feed_cont(s, b);
}

bool ws_utf8_feed(ws_utf8_state *s, const u8 *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (!feed_byte(s, buf[i]))
            return false;
    }
    return true;
}

bool ws_utf8_complete(const ws_utf8_state *s) {
    return s->remaining == 0;
}

bool ws_utf8_valid(const u8 *buf, size_t len) {
    ws_utf8_state s = {0};
    return ws_utf8_feed(&s, buf, len) && ws_utf8_complete(&s);
}
