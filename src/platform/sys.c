// インラインアセンブリ経由の x86-64 Linux システムコール。番号はカーネル ABI に従う。
#include "platform/sys.h"

#define SYS_read          0
#define SYS_write         1
#define SYS_close         3
#define SYS_socket        41
#define SYS_accept        43
#define SYS_bind          49
#define SYS_listen        50
#define SYS_setsockopt    54
#define SYS_exit          60
#define SYS_getrandom     318
#define SYS_epoll_create1 291
#define SYS_epoll_ctl     233
#define SYS_epoll_wait    232
#define SYS_fcntl         72

static i64 syscall3(i64 n, i64 a, i64 b, i64 c) {
    i64 ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}

static i64 syscall4(i64 n, i64 a, i64 b, i64 c, i64 d) {
    i64 ret;
    register i64 r10 __asm__("r10") = d;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

static i64 syscall5(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e) {
    i64 ret;
    register i64 r10 __asm__("r10") = d;
    register i64 r8 __asm__("r8") = e;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8)
                     : "rcx", "r11", "memory");
    return ret;
}

i64 sys_read(int fd, void *buf, size_t n) {
    return syscall3(SYS_read, fd, (i64) buf, (i64) n);
}
i64 sys_write(int fd, const void *buf, size_t n) {
    return syscall3(SYS_write, fd, (i64) buf, (i64) n);
}
int sys_close(int fd) {
    return (int) syscall3(SYS_close, fd, 0, 0);
}

int sys_socket(int domain, int type, int protocol) {
    return (int) syscall3(SYS_socket, domain, type, protocol);
}
int sys_setsockopt(int fd, int level, int optname, const void *optval, u32 optlen) {
    return (int) syscall5(SYS_setsockopt, fd, level, optname, (i64) optval, optlen);
}
int sys_bind(int fd, const void *addr, u32 addrlen) {
    return (int) syscall3(SYS_bind, fd, (i64) addr, addrlen);
}
int sys_listen(int fd, int backlog) {
    return (int) syscall3(SYS_listen, fd, backlog, 0);
}
int sys_accept(int fd, void *addr, u32 *addrlen) {
    return (int) syscall3(SYS_accept, fd, (i64) addr, (i64) addrlen);
}
i64 sys_getrandom(void *buf, size_t n) {
    return syscall3(SYS_getrandom, (i64) buf, (i64) n, 0); // flags=0: シード完了までブロック
}

int sys_epoll_create1(int flags) {
    return (int) syscall3(SYS_epoll_create1, flags, 0, 0);
}
int sys_epoll_ctl(int epfd, int op, int fd, void *ev) {
    return (int) syscall4(SYS_epoll_ctl, epfd, op, fd, (i64) ev);
}
int sys_epoll_wait(int epfd, void *evs, int maxevents, int timeout) {
    return (int) syscall4(SYS_epoll_wait, epfd, (i64) evs, maxevents, timeout);
}
int sys_set_nonblock(int fd) {
    i64 fl = syscall3(SYS_fcntl, fd, 3 /*F_GETFL*/, 0);
    if (fl < 0)
        return (int) fl;
    return (int) syscall3(SYS_fcntl, fd, 4 /*F_SETFL*/, fl | 04000 /*O_NONBLOCK*/);
}
void sys_exit(int code) {
    syscall3(SYS_exit, code, 0, 0);
    __builtin_unreachable();
}
