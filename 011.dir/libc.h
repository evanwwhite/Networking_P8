#pragma once

#include <stddef.h>

typedef long ssize_t;

// not standard
extern int say(const char* msg);

extern size_t strlen(const char* s);

extern int printf(const char* fmt, ...);

extern size_t write(int fd, const char* buf, size_t len);

extern int open(const char* path, int flags);

extern ssize_t read(int fd, char* buf, size_t len);

extern int close(int fd);

extern long lseek(int fd, long off, int whence);

extern long brk(void* addr);

extern long sched_yield(void);

extern int fork(void);

extern long waitpid(int pid, int *ustatus, int options);

extern void exit(int status);

extern int getpid(void);

extern int semget(int key, int nsems, int flags);

struct sembuf;
extern int semop(int semid, struct sembuf *sops, unsigned nsops);

extern int semctl(int semid, int semnum, int cmd, ...);

static inline int putchar(int ch) {
    char buf = ch;
    return write(1, &buf, 1);
}

extern int puts(const char* s);

static inline int isdigit(int ch) {
    return (ch >= '0') && (ch <= '9');
}

