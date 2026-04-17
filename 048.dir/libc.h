#pragma once

#include <stddef.h>

extern int printf(const char* fmt, ...);
extern size_t write(int fd, const char* buf, size_t len);

extern void exit(int status);

extern int puts(const char* s);

extern int fork(void);
extern int sched_yield(void);
extern int waitpid(int pid, int* status, int options);

static inline int putchar(int ch) {
  char buf = ch;
  return write(1, &buf, 1);
}

static inline int isdigit(int ch) {
  return (ch >= '0') && (ch <= '9');
}
