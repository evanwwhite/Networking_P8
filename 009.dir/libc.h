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

extern int open(const char* path);
extern int read(unsigned int fd, char* buf, size_t len);
extern int close(unsigned int fd);
extern int lseek(unsigned int fd, long long offset, unsigned int whence);
extern void exit(int code);
