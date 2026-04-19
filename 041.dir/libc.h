#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern size_t write(int fd, const char* buf, size_t len);
extern long read(int fd, char* buf, size_t len);
extern int open(const char* path, int flags, int mode);
extern int close(int fd);
extern long lseek(int fd, long off, int whence);
extern void* brk(void* addr);
extern int fork(void);
extern int waitpid(int pid, int* status, int options);

#ifdef __cplusplus
}
#endif
