#pragma once

#include <stddef.h>

extern int printf(const char* fmt, ...);

static inline int isdigit(int c) { return c >= '0' && c <= '9'; }

/* syscall 1 */
extern size_t write(int fd, const char* buf, size_t len);

/* syscall 3 */
extern int close(int fd);

/* syscall 24 */
extern int sched_yield(void);

/* syscall 60 */
extern void exit(int code);

static inline int putchar(int ch) {
    char buf = ch;
    return write(1, &buf, 1);
}
