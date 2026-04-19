#pragma once
#include <stddef.h>

extern size_t write(int fd, const char* buf, size_t len);

extern int open(const char* path);

extern int read(int fd, char* buf, size_t len);

extern int close(int fd);

extern long lseek(int fd, long offset, int whence);

extern void exit(int status);

extern int putchar(int c);

extern size_t strlen(const char* s);