#pragma once

#include <stddef.h>

extern int say(const char* msg);

extern int printf(const char* fmt, ...);

extern size_t write(int fd, const char* buf, size_t len);
extern size_t read(int fd, char* buf, size_t len);
extern int open(const char* path, int flags);
extern int close(int fd);
extern long lseek(int fd, long offset, int whence);
extern long brk(unsigned long addr);
extern int fork(void);
extern int waitpid(int pid, int* status, int options);
extern int sched_yield(void);
extern void exit(int status);

static inline int putchar(int ch) {
    char buf = ch;
    return write(1, &buf, 1);
}

static inline int isdigit(int ch) {
    return (ch >= '0') && (ch <= '9');
}
