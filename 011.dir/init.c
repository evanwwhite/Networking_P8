/*
 * "Semaphore Relay"
 *
 * Inter-process data transfer using files + semaphore synchronization (was inspired
 * by quiz question which I definitely did NOT get right, used Claude for help with structure
 * and machine.s)
 *
 * Two forked processes take turns reading from a shared file, using
 * semaphores to coordinate ordering. This demonstrates IPC without
 * relying on wait/exit for data exchange.
 *
 * Syscalls tested: fork, open, read, lseek, close, write, exit,
 *                  semget, semop, semctl, waitpid
 */

#include "libc.h"

#define SEEK_SET 0
#define IPC_CREAT 01000
#define IPC_RMID 0

struct sembuf {
    unsigned short sem_num;
    short sem_op;
    short sem_flg;
};

// block until semaphore value > 0, then decrement by 1
void sem_wait(int semid) {
    struct sembuf op;
    op.sem_num = 0;
    op.sem_op = -1;
    op.sem_flg = 0;
    semop(semid, &op, 1);
}

// increment semaphore value by 1, potentially waking a blocked process
void sem_signal(int semid) {
    struct sembuf op;
    op.sem_num = 0;
    op.sem_op = 1;
    op.sem_flg = 0;
    semop(semid, &op, 1);
}

int main() {
    printf("*** relay: begin\n");

    // two semaphores (initial value 0) for turn coordination
    int gate_a = semget(100, 1, IPC_CREAT);
    int gate_b = semget(200, 1, IPC_CREAT);

    int pid = fork();

    if (pid == 0) {
        // CHILD
        // read first part of message from shared file
        int fd = open("/relay.txt", 0);
        char buf[32];
        int n = read(fd, buf, 5);
        buf[n] = '\0';
        close(fd);

        printf("*** child: %s\n", buf);

        // signal parent: "my part is done"
        sem_signal(gate_a);

        // wait for parent to finish its part
        sem_wait(gate_b);

        printf("*** child: done\n");
        exit(0);
    } else {
        // PARENT
        // wait for child to read and print its part
        sem_wait(gate_a);

        // read second part from same file at different offset
        int fd = open("/relay.txt", 0);
        lseek(fd, 6, SEEK_SET);
        char buf[32];
        int n = read(fd, buf, 5);
        buf[n] = '\0';
        close(fd);

        printf("*** parent: %s\n", buf);

        // signal child to finish
        sem_signal(gate_b);

        // wait for child to exit, clean up semaphores
        int status;
        waitpid(pid, &status, 0);
        semctl(gate_a, 0, IPC_RMID);
        semctl(gate_b, 0, IPC_RMID);

        printf("*** relay: passed\n");
    }

    return 0;
}
