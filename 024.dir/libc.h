#pragma once

#include <stddef.h>

// not standard
extern int say(const char* msg);

extern int printf(const char* fmt, ...);

extern size_t write(int fd, const char* buf, size_t len);

extern int fork(void);

extern int waitpid(int pid, int *status, int options);

extern void exit(int status);

static inline int putchar(int ch) {
    char buf = ch;
    return write(1, &buf, 1);
}

static inline int isdigit(int ch) {
    return (ch >= '0') && (ch <= '9');
}

//without it my files didn't want to make
extern int puts(const char* s);