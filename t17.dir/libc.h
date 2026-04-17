#pragma once

#include <stddef.h>


extern int printf(const char* fmt, ...);
extern size_t write(int fd, const char* buf, size_t len);
extern long read(int fd, char* buf, size_t len);
extern int open(const char* path, int flags, int mode);
extern int close(int fd);
extern long lseek(int fd, long offset, int whence);
extern long brk(void* addr);
extern long sched_yield(void);
extern int fork(void);
extern long waitpid(long pid, int* status, int options);
extern int semget(int key, int nsems, int semflg);
extern int semop(int semid, void* sops, size_t nsops);
extern int semctl(int semid, int semnum, int cmd);
extern void exit(int status);

static inline int putchar(int ch) {
    char buf = ch;
    return write(1, &buf, 1);
}

static inline int isdigit(int ch) {
    return (ch >= '0') && (ch <= '9');
}
