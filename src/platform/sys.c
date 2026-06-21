// x86-64 Linux syscalls via inline asm. Syscall numbers from the kernel ABI.
#include "platform/sys.h"

#define SYS_read       0
#define SYS_write      1
#define SYS_close      3
#define SYS_socket     41
#define SYS_accept     43
#define SYS_bind       49
#define SYS_listen     50
#define SYS_setsockopt 54
#define SYS_exit       60

static i64 syscall3(i64 n, i64 a, i64 b, i64 c) {
    i64 ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}

static i64 syscall5(i64 n, i64 a, i64 b, i64 c, i64 d, i64 e) {
    i64                   ret;
    register i64 r10 __asm__("r10") = d;
    register i64 r8 __asm__("r8")   = e;
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
int sys_close(int fd) { return (int) syscall3(SYS_close, fd, 0, 0); }

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
void sys_exit(int code) {
    syscall3(SYS_exit, code, 0, 0);
    __builtin_unreachable();
}
