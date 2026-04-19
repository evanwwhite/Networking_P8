#pragma once

#include <stddef.h>

extern int printf(const char *fmt, ...);

extern size_t read(int fd, void *buf, size_t count);

extern size_t write(int fd, const char *buf, size_t len);

extern int open(const char *path, int flags, ... /* mode_t mode */);

extern int lseek(int fd, int offset, int whence);

extern int brk(void *addr);

extern int close(int fd);

static inline int putchar(int ch) {
    char buf = ch;
    return write(1, &buf, 1);
}

static inline int isdigit(int ch) {
    return (ch >= '0') && (ch <= '9');
}
