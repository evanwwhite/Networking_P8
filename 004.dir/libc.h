#pragma once
#include <stddef.h>

/*
    This entire test was inspired by Dr. Gheith's test and Claude also contributed a huge majority
    of this test case throughout the entire folder. 
*/

/*
    Used by semop to describe the semaphore for which the operation is
*/
struct sembuf {
    // which sema, incr/decr, and flags
    unsigned short sem_num;
    short sem_op;
    short sem_flg;
};

// a bunch of macros below for several diff syscalls
// semget + semctl
#define IPC_PRIVATE  0
#define IPC_RMID 0

// open
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2

// lseek
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// some function declarations - impls in machine.S
#ifdef __cplusplus
extern "C" {
#endif

// All of these are implemented in machine.S as needed - user side stubs of all of these calls

extern int say(const char *msg);
extern int printf(const char *fmt, ...);
extern size_t write(int fd, const char *buf, size_t len);
extern long read(int fd, char *buf, long count);
extern int open(const char *path, int flags, int mode);
extern int close(int fd);
extern long lseek(int fd, long offset, int whence);
extern void *brk(void *addr);
extern int sched_yield(void);
extern int fork(void);
extern int waitpid(int pid, int *status, int options);
extern int semget(int key, int nsems, int semflg);
extern int semop(int semid, struct sembuf *sops, long nsops);
extern int semctl(int semid, int semnum, int cmd, long arg);
extern void   exit(int status) __attribute__((noreturn));

#ifdef __cplusplus
} /* extern "C" */
#endif

/**
 * Writes a single character to stdout.
 *
 * @param ch the character to write (as an int)
 * @return the number of bytes written
*/
static inline int putchar(int ch) {
    char buf = (char)ch; 
    return (int)write(1, &buf, 1); // write the single character to stdout
}

/**
 * Checks whether a character is a decimal digit ('0'–'9').
 *
 * @param ch the character to check
 * @return non-zero if ch is a digit, zero otherwise
 */
static inline int isdigit(int ch) {
    return (ch >= '0') && (ch <= '9'); // 0 to 9 check
}
