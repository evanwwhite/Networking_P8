#pragma once

#include <stddef.h>

// not standard
extern int say(const char* msg);


extern int printf(const char* fmt, ...);

extern size_t write(int fd, const char* buf, size_t len);

static inline int putchar(int ch) {
    char buf = ch;
    return write(1, &buf, 1);
}

static inline int isdigit(int ch) {
    return (ch >= '0') && (ch <= '9');
}

extern int open(const char* path, int flags);
extern int read(int fd, char* buf, int len);
extern int close(int fd);
extern void exit(int code);
