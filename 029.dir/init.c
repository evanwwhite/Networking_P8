#include "libc.h"
#define SEEK_SET 0
#define IPC_RMID 0
 
// Comprehensive syscall test suite
// Tests memory management (brk), file I/O, process control (fork/waitpid), and scheduling (yield)
// The only thing not tested is semaphore operations (semop, semget)
// Again, thanks Claude. And I promise I was here too to reason through this code and edit it to make sense
 
int main() {
    printf("*** Starting syscall test\n");
    
    // Test 1: brk - expand heap
    printf("*** Test 1: brk\n");
    void* initial_brk = brk(0); // get current program break (end of heap)
    void* new_brk = brk(4096); // atempt to grow heap by one page (4096 bytes)
    if (new_brk != (void*)-1) {
        printf("*** brk: heap expanded successfully\n");
    }
    
    // Test 2: File operations (open, read, lseek, close)
    printf("*** Test 2: file operations\n");
    int fd = open("/sbin/init", 0);
    if (fd >= 0) {
        printf("*** open: fd=%d\n", fd);
        
        char buffer[32];
        int bytes_read = read(fd, buffer, 4);
        if (bytes_read == 4) {
            // ELF files start with 0x7F 'E' 'L' 'F' :D
            printf("*** read: %d bytes (ELF magic: %c%c%c)\n", 
                   bytes_read, buffer[1], buffer[2], buffer[3]);
        }
        
        // Test lseek, reset file offset back to beginning
        int pos = lseek(fd, 0, SEEK_SET);
        printf("*** lseek: pos=%d\n", pos);
        
        // Close file descriptor
        close(fd);
        printf("*** close: fd closed\n");
    }
    
    // Test 3: fork and waitpid
    printf("*** Test 3: fork/waitpid\n");
    int pid = fork();
    
    if (pid == 0) {
        // Child process
        printf("*** child: pid=%d\n", getpid());
        sched_yield();  // test sched_yield
        printf("*** child: after yield\n");
        exit(42);
    } else if (pid > 0) {
        // Parent process
        printf("*** parent: child_pid=%d\n", pid);
        int status;
        int waited_pid = waitpid(pid, &status, 0);
        printf("*** parent: child %d exited with status %d\n", waited_pid, status);
    }
    
    printf("*** All tests completed\n");
    return 0;
}