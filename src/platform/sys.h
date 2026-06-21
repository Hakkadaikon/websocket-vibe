// ws platform — x86-64 Linux syscalls (freestanding, no libc).
#ifndef WS_PLATFORM_SYS_H
#define WS_PLATFORM_SYS_H

#include "ws/types.h"

// Thin syscall wrappers. Return value follows the kernel ABI: negative is
// -errno. Only the subset the server loop needs.
i64 sys_read(int fd, void *buf, size_t n);
i64 sys_write(int fd, const void *buf, size_t n);
int sys_close(int fd);
int sys_socket(int domain, int type, int protocol);
int sys_setsockopt(int fd, int level, int optname, const void *optval, u32 optlen);
int sys_bind(int fd, const void *addr, u32 addrlen);
int sys_listen(int fd, int backlog);
int sys_accept(int fd, void *addr, u32 *addrlen);
// Fill buf with n cryptographically-strong random bytes. Returns bytes written
// (negative is -errno). Used for client frame masking keys (RFC6455 §5.3).
i64 sys_getrandom(void *buf, size_t n);
void sys_exit(int code) __attribute__((noreturn));

// Constants we need (Linux/x86-64).
#define WS_AF_INET      2
#define WS_SOCK_STREAM  1
#define WS_SOL_SOCKET   1
#define WS_SO_REUSEADDR 2
#define WS_INADDR_ANY   0

// sockaddr_in laid out per the kernel ABI.
typedef struct {
    u16 sin_family;
    u16 sin_port; // network byte order
    u32 sin_addr; // network byte order
    u8 sin_zero[8];
} ws_sockaddr_in;

static inline u16 ws_htons(u16 x) {
    return (u16) ((x << 8) | (x >> 8));
}

#endif // WS_PLATFORM_SYS_H
