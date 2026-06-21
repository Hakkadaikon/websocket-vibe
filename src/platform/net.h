// ws platform — ネットワーク ABI の型・定数 (Linux/x86-64、libc なし)。
// syscall ラッパ (sys.h) が運ぶデータのカーネル ABI レイアウトを定義する。
#ifndef WS_PLATFORM_NET_H
#define WS_PLATFORM_NET_H

#include "ws/types.h"

// ソケット定数 (Linux/x86-64)。
#define WS_AF_INET      2
#define WS_SOCK_STREAM  1
#define WS_SOL_SOCKET   1
#define WS_SO_REUSEADDR 2
#define WS_INADDR_ANY   0

// カーネル ABI に従ったレイアウトの sockaddr_in。
typedef struct {
    u16 sin_family;
    u16 sin_port; // ネットワークバイトオーダー
    u32 sin_addr; // ネットワークバイトオーダー
    u8 sin_zero[8];
} ws_sockaddr_in;

static inline u16 ws_htons(u16 x) {
    return (u16) ((x << 8) | (x >> 8));
}

// epoll 定数 (Linux/x86-64)。
#define WS_EPOLL_CTL_ADD 1
#define WS_EPOLL_CTL_DEL 2
#define WS_EPOLLIN       0x001u

// x86-64 では struct epoll_event はパックされている (events/data 間にパディングなし)。
typedef struct __attribute__((packed)) {
    u32 events;
    u64 data; // ここに fd を格納する
} ws_epoll_event;

#endif // WS_PLATFORM_NET_H のガード終端
