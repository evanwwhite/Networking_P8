#pragma once

#include <stddef.h>

// not standard
extern int say(const char* msg);


extern int printf(const char* fmt, ...);

extern int open(const char* path);
extern int read(int fd, char* buf, size_t len);
extern int close(int fd);
extern int lseek(int fd, size_t offset);
extern int sched_yield(void);

static inline int putchar(int ch) {
    char buf = ch;
    return write(1, &buf, 1);
}

static inline int isdigit(int ch) {
    return (ch >= '0') && (ch <= '9');
}

