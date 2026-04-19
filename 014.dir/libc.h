#pragma once

#include <stddef.h>

extern int printf(const char* fmt, ...);

extern size_t write(int fd, const char* buf, size_t len);
extern long open(const char* path);
extern long read(int fd, void* buf, size_t len);
extern long close(int fd);
extern long lseek(int fd, long off, int whence);
extern long brk(void* addr);
extern long sched_yield(void);
extern long waitpid(int pid, int *ustatus, int options);
extern int fork(void);
extern void exit(int status);
extern size_t strlen(const char* s);

static inline int putchar(int ch) {
    char buf = ch;
    return write(1, &buf, 1);
}

static inline int isdigit(int ch) {
    return (ch >= '0') && (ch <= '9');
}