#pragma once

#include <stddef.h>

extern int puts(const char *s);
// not standard
extern int say(const char* msg);


extern int printf(const char* fmt, ...);

extern size_t write(int fd, const char* buf, size_t len);
// below got added for my test case
extern size_t read(int fd, char* buf, size_t len);
extern int open(const char* path, int flags);
extern int close(int fd);
extern long lseek(int fd, long offset, int whence);

static inline int putchar(int ch) {
    char buf = ch;
    return write(1, &buf, 1);
}

static inline int isdigit(int ch) {
    return (ch >= '0') && (ch <= '9');
}

