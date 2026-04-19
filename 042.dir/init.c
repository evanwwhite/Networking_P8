//used claude to brainstorm test ideas and outline

//syscalls tested: write, fork, waitpid, open, read, close, lseek, brk, sched_yield, exit

// tests the core process lifecycle and file i/o syscalls
// forks a child that opens and reads a file and exits with code 42
// parents waits to check exist status
// tests sched_yield, brk heap expansion, and lseek after
#include "libc.h"

int main() {
    //write 
    printf("*** write: hello from init\n");
    //fork and waitpid
    int pid = fork();
    if (pid == 0) {
        printf("*** child: I am the child\n");
        //open, read, close - child
        int fd = open("comics", 0);
        if (fd >= 0) {
            char buf[128];
            int n = read(fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = 0;
                printf("*** child read: %s\n", buf);
            } else {
                printf("*** child read: failed\n");
            }
            close(fd);
        } else {
            printf("*** child open: failed\n");
        }

        exit(42);
    } else if (pid > 0) {
        //parent
        printf("*** parent: forked child\n");
        int status = 0;
        waitpid(pid, &status, 0);
        printf("*** parent: child exited with %d\n", status);
    } else {
        printf("*** fork: failed\n");
    }
    //sched_yield
    sched_yield();
    printf("*** yield: ok\n");

    //brk
    void *cur = (void *)brk(0);
    printf("*** brk: got current break\n");
    void *next = (void *)brk((unsigned long)cur + 4096);
    if ((unsigned long)next >= (unsigned long)cur + 4096) {
        volatile int *p = (volatile int *)cur;
        *p = 1234;
        if (*p == 1234) {
            printf("*** brk: heap works\n");
        } else {
            printf("*** brk: heap read mismatch\n");
        }
    } else {
        printf("*** brk: expand failed\n");
    }

    //lseek
    int fd2 = open("comics", 0);
    if (fd2 >= 0) {
        char buf[16];
        read(fd2, buf, 5);
        buf[5] = 0;
        printf("*** lseek: first5=%s\n", buf);

        lseek(fd2, 0, 0); /* SEEK_SET */
        read(fd2, buf, 5);
        buf[5] = 0;
        printf("*** lseek: again=%s\n", buf);

        close(fd2);
    } else {
        printf("*** lseek: open failed\n");
    }

    printf("*** done\n");
    return 0;
}
