#pragma once

#include <stddef.h>

// not standard
extern int say(const char *msg);

extern int printf(const char *fmt, ...);

extern size_t write(int fd, const char *buf, size_t len);

static inline int puts(const char *s) {
  size_t n = 0;
  while (s[n] != '\0') {
    n++;
  }
  write(1, s, n);
  write(1, "\n", 1);
  return 1;
}

static inline int putchar(int ch) {
  char buf = ch;
  return write(1, &buf, 1);
}

static inline int isdigit(int ch) { return (ch >= '0') && (ch <= '9'); }
