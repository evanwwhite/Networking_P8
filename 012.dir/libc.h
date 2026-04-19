#pragma once

#include <stddef.h>

// libc.h is a wrapper around the system calls
// we must implement our own subset of these system calls

// not standard
extern int say(const char* msg);


extern int printf(const char* fmt, ...);

/**
 * wrapper around of system calls
 */
extern size_t write(int fd, const char* buf, size_t len);

extern size_t read(int fd, const char* buf, size_t len);

extern void exit(int code);
extern int fork(void);
extern int waitpid(int pid, int* status, int options);
extern int sched_yield(void);

static inline int putchar(int ch) {
    char buf = ch;
    return write(1, &buf, 1);
}

static inline int isdigit(int ch) {
    return (ch >= '0') && (ch <= '9');
}


