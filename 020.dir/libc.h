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

extern long read(int fd, char* buf, size_t len);
extern int  open(const char* path);
extern int  close(int fd);
extern long lseek(int fd, long offset, int whence);
extern long brk(long addr);
extern int  sched_yield(void);
extern int  fork(void);
extern long waitpid(long pid, int* status, int options);
extern long semget(int key, int nsems, int flags);
extern long semop(int semid, void* sops, size_t nsops);
extern long semctl(int semid, int semnum, int cmd);
extern void exit(int status);
extern size_t strlen(const char* s);