#pragma once

#include <stddef.h>

// not standard
extern int say(const char *msg);

extern int printf(const char *fmt, ...);

extern size_t write(int fd, const char *buf, size_t len);
extern size_t strlen(const char *s);

static inline int putchar(int ch)
{
    char buf = ch;
    return write(1, &buf, 1);
}

static inline int isdigit(int ch)
{
    return (ch >= '0') && (ch <= '9');
}

static inline int puts(const char *s)
{
    size_t len = strlen(s);
    write(1, s, len);
    write(1, "\n", 1);
    return 0;
}
extern int printf(const char *fmt, ...);
extern size_t write(int fd, const char *buf, size_t len);
extern long read(int fd, void *buf, size_t len);
extern int open(const char *path);
extern int close(int fd);
extern int fork(void);
extern int waitpid(int pid, int *status, int options);
extern void exit(int status);
