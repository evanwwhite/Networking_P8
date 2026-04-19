#pragma once

#include <stddef.h>

extern int printf(const char* fmt, ...);
extern size_t write(int fd, const char* buf, size_t len);

static inline int putchar(int ch) {
    char buf = ch;
    return write(1, &buf, 1);
}

static inline int isdigit(int ch) {
    return (ch >= '0') && (ch <= '9');
}
