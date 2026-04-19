#include "libc.h"  //user libc/syscall 

// Constants used by the syscalls below. I define them here so the test does
// not depend on any host system headers.
enum {
    SEEK_SET = 0,  // passed to lseek, its the start of file
    IPC_RMID = 0, // passed to semctl, removing a semaphore
};

// semop needs this
struct sembuf {
    unsigned short sem_num; // which semaphore inside the semaphore set to operate on; semget only needs one semaphore
    short sem_op;           // negative - wait, positive - post
    short sem_flg;          // ignore no wait flag, set to 0
};

// Print a failure line before exiting to point at bugs
static void fail(int line, const char *msg) {
    printf("*** FAILS at line %d: %s\n", line, msg);
    exit(1);
}

// Print successful checkpoints that should appear in the .ok file if the test passes
static void checkpoint(const char *msg) {
    printf("*** %s\n", msg);
}

// Helper that gives the source line that failed
#define CHECK(cond, msg) if (!(cond)) fail(__LINE__, msg)

// Compare a fixed number of bytes
static void check_bytes(const char *got, const char *want, int len, int line){
    for(int i = 0; i< len; i++) {
        if(got[i]!=want[i]) {
            printf("*** FAIL line %d: file contents mismatch at byte %d\n", line, i);
            exit(1);
        }
    }
}
// Compares file content byte by byte,  failures report the caller's line number
#define CHECK_BYTES(got, want, len) check_bytes((got), (want), (len), __LINE__)

// Test open, read, lseek, and close using a root level file only
static void test_file_offsets(void) {
    char buf[8]; 

    // Keep this file at the image root 
    int fd = open("check.txt", 0, 0);
    CHECK(fd >= 0, "open check.txt failed");
    checkpoint("opened root file, check");

    // file starts with the "root" in "root-file-offset-test".
    long n = read(fd, buf, 4);
    CHECK(n == 4, "first read count wrong");
    CHECK_BYTES(buf, "root", 4);
    checkpoint("check, first read");

    // seek forward to "offset" to make sure lseek changes the read position
    long pos = lseek(fd, 10, SEEK_SET);
    CHECK(pos == 10, "seek forward offset wrong");
    n = read(fd, buf, 6);
    CHECK(n == 6, "second read count wrong");
    CHECK_BYTES(buf, "offset", 6);
    checkpoint("check, seek forward");

    // seek backward to "file" 
    pos = lseek(fd, 5, SEEK_SET);
    CHECK(pos == 5, "seek backward offset wrong");
    n = read(fd, buf, 4);
    CHECK(n == 4, "third read count wrong");
    CHECK_BYTES(buf, "file", 4);
    checkpoint("check, seek backward");

    // reopening should reset offset
    CHECK(close(fd) == 0, "close failed");
    checkpoint("closed");

    // Reopen the same file and verify the new descriptor starts at byte 0
    fd = open("check.txt", 0, 0);
    CHECK(fd >= 0, "reopen check.txt failed");
    n = read(fd, buf, 4);
    CHECK(n == 4, "reopen read count wrong");
    CHECK_BYTES(buf, "root", 4);
    CHECK(close(fd) == 0, "close after reopen failed");
    checkpoint("reopen resets offset ok");
}

// Test that brk  returns the new break and maps usable memory
static void test_brk(void) {
    // current heap break 
    long base = brk(0);
    CHECK(base > 0, "brk(0) returned an invalid heap break");

    // Grow by one page
    long grown = brk((void *)(base + 4096));
    CHECK(grown == base + 4096, "brk failed growing by one page");

    // Make sure the new page is actually usable
    char *heap = (char*)base;
    heap[0] = 'a';
    heap[4095] = 'b';
    CHECK(heap[0] == 'a' && heap[4095] == 'b', "new brk page not writable");
    checkpoint("brk grow page ok");
}

// Test fork/waitpid plus semaphore blocking and wakeup
static void test_fork_wait_sem(void) {
    // New semaphores should start at zero so  child should block on -1
    int semid = semget(12345, 1, 0);
    CHECK(semid >= 0, "semget failed");
    checkpoint("check semget");

    // Fork creates a child that waits and parent wakes it
    long pid = fork();
    CHECK(pid >= 0, "fork failed");

    // Child path waits on the semaphore 
    if (pid == 0) {
        struct sembuf down = {0, -1, 0};
        checkpoint("child waiting on semaphore");
        CHECK(semop(semid, &down, 1) == 0, "child semop failed");
        checkpoint("child acquired semaphore");
        exit(2);
    }

    // Parent path yields so the child can block first
    CHECK(sched_yield() == 0, "sched_yield failed");
    checkpoint("check parent resumed after yield");

    // semaphore wakes the child
    struct sembuf up = {0, 1, 0};
    CHECK(semop(semid, &up, 1) == 0, "parent semop failed");
    checkpoint("parent released semaphore");

    // reap the child and check its exit status 
    int status = 0;
    long waited = waitpid(pid, &status, 0);
    CHECK(waited == pid, "waitpid pid wrong");
    CHECK(status == (2 << 8), "waitpid status wrong");
    checkpoint("check waitpid child status");

    // Remove the semaphore
    CHECK(semctl(semid, 0, IPC_RMID) == 0, "semctl failed");
    checkpoint("semctl remove ok");
}

int main() {
    // file descriptor & offset 
    test_file_offsets();

    // heap growth 
    test_brk();

    // Process and semaphore 
    test_fork_wait_sem();

    checkpoint("everything good");

    return 0;
}
