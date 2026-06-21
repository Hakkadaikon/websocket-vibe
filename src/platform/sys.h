// ws platform — x86-64 Linux システムコール (フリースタンディング、libc なし)。
#ifndef WS_PLATFORM_SYS_H
#define WS_PLATFORM_SYS_H

#include "ws/types.h"

// 薄いシステムコールラッパー。戻り値はカーネル ABI に従い、負値は -errno。
// サーバループに必要な部分集合のみを提供する。
i64 sys_read(int fd, void *buf, size_t n);
i64 sys_write(int fd, const void *buf, size_t n);
int sys_close(int fd);
int sys_socket(int domain, int type, int protocol);
int sys_setsockopt(int fd, int level, int optname, const void *optval, u32 optlen);
int sys_bind(int fd, const void *addr, u32 addrlen);
int sys_listen(int fd, int backlog);
int sys_accept(int fd, void *addr, u32 *addrlen);
// buf を暗号論的に強い乱数 n バイトで埋める。書き込んだバイト数を返す
// (負値は -errno)。クライアントフレームのマスクキーに使う (RFC6455 §5.3)。
i64 sys_getrandom(void *buf, size_t n);

// 並行デモサーバ向けの epoll ベースの readiness 多重化。
int sys_epoll_create1(int flags);
int sys_epoll_ctl(int epfd, int op, int fd, void *ev);
int sys_epoll_wait(int epfd, void *evs, int maxevents, int timeout);
int sys_set_nonblock(int fd);

void sys_exit(int code) __attribute__((noreturn));

// ネットワーク ABI の型・定数 (sockaddr_in/htons/epoll) は net.h を参照。

#endif // WS_PLATFORM_SYS_H のガード終端
