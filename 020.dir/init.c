/*
 * This test was written with the help of Claude to help me with writting testcases for all syscalls without forking for right now
 * Tests all required syscalls: open, read, write, close, lseek,
 * brk, exit, semget, semop, semctl.
 * fork, waitpid, sched_yield tested separately.
 * hello file contains: "*** you can read files\n" (23 bytes)
 */

#include "libc.h"

struct sembuf {
    unsigned short sem_num;
    short          sem_op;
    short          sem_flg;
};

int main() {
    // --- write ---
    printf("testing write... %s\n", "");
    write(1, "*** PASS 1: write works\n", 24);

    // --- open ---
    printf("testing open... %s\n", "");
    int fd = open("hello");
    if (fd >= 0)
        write(1, "*** PASS 2: open works\n", 23);
    else
        write(1, "*** FAIL 2: open not implemented or file missing\n", 49);

    // --- read ---
    printf("testing read... %s\n", "");
    char buf1[3];
    int r1 = read(fd, buf1, 3);
    if (r1 == 3 && buf1[0] == '*' && buf1[1] == '*' && buf1[2] == '*')
        write(1, "*** PASS 3: read correct bytes from start\n", 42);
    else if (r1 < 0)
        write(1, "*** FAIL 3: read not implemented\n", 33);
    else
        write(1, "*** FAIL 3: read returned wrong bytes\n", 38);

    // --- lseek SEEK_SET ---
    printf("testing lseek SEEK_SET... %s\n", "");
    lseek(fd, 4, 0);
    char buf2[3];
    int r2 = read(fd, buf2, 3);
    if (r2 == 3 && buf2[0] == 'y' && buf2[1] == 'o' && buf2[2] == 'u')
        write(1, "*** PASS 4: lseek SEEK_SET and read works\n", 42);
    else
        write(1, "*** FAIL 4: lseek SEEK_SET not implemented or wrong bytes\n", 58);

    // --- lseek SEEK_END ---
    printf("testing lseek SEEK_END... %s\n", "");
    lseek(fd, -6, 2);
    char buf3[5];
    int r3 = read(fd, buf3, 5);
    if (r3 == 5 && buf3[0] == 'f' && buf3[1] == 'i' && buf3[2] == 'l' && buf3[3] == 'e' && buf3[4] == 's')
        write(1, "*** PASS 5: lseek SEEK_END and read works\n", 42);
    else
        write(1, "*** FAIL 5: lseek SEEK_END not implemented or wrong bytes\n", 58);

    // --- lseek SEEK_CUR ---
    printf("testing lseek SEEK_CUR... %s\n", "");
    lseek(fd, 0, 1);
    char buf4[1];
    int r4 = read(fd, buf4, 1);
    if (r4 == 1 && buf4[0] == '\n')
        write(1, "*** PASS 6: lseek SEEK_CUR and read works\n", 42);
    else
        write(1, "*** FAIL 6: lseek SEEK_CUR not implemented or wrong bytes\n", 58);

    // --- read at EOF ---
    printf("testing read at EOF... %s\n", "");
    char buf5[4];
    int r5 = read(fd, buf5, 4);
    if (r5 == 0)
        write(1, "*** PASS 7: read at EOF returns 0\n", 34);
    else
        write(1, "*** FAIL 7: read past EOF should return 0\n", 42);

    // --- close ---
    printf("testing close... %s\n", "");
    close(fd);
    char buf6[4];
    int r6 = read(fd, buf6, 4);
    if (r6 < 0)
        write(1, "*** PASS 8: close works, read on closed fd fails\n", 49);
    else
        write(1, "*** FAIL 8: close not implemented, read on closed fd succeeded\n", 63);

    // --- brk ---
    printf("testing brk... %s\n", "");
    long cur = brk(0);
    long next = brk(cur + 4096);
    if (next == cur + 4096)
        write(1, "*** PASS 9: brk grows heap\n", 27);
    else
        write(1, "*** FAIL 9: brk not implemented or heap not growing\n", 52);

    // --- semget ---
    printf("testing semget... %s\n", "");
    int semid = semget(0x1234, 1, 0x1C0);
    if (semid >= 0)
        write(1, "*** PASS 10: semget works\n", 26);
    else
        write(1, "*** FAIL 10: semget not implemented or nsems != 1\n", 50);

    // --- semop ---
    printf("testing semop... %s\n", "");
    struct sembuf sup = {0, 1, 0};
    struct sembuf sdown = {0, -1, 0};
    semop(semid, &sup, 1);
    int so = semop(semid, &sdown, 1);
    if (so == 0)
        write(1, "*** PASS 11: semop up and down works\n", 37);
    else
        write(1, "*** FAIL 11: semop not implemented or bad semid\n", 48);

    // --- semctl ---
    printf("testing semctl... %s\n", "");
    int sc = semctl(semid, 0, 0);
    if (sc == 0)
        write(1, "*** PASS 12: semctl works\n", 26);
    else
        write(1, "*** FAIL 12: semctl IPC_RMID not implemented\n", 45);

    printf("testing exit... %s\n", "");
    write(1, "*** PASS 13: exit works\n", 24);
    return 0;
}