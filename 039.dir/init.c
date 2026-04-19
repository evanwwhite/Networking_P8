#include "libc.h"

int main() {
    printf("1. Parent process started.\n");
    
    int pid = fork();

    if (pid < 0) {
        printf("ERROR: Fork failed!\n");
        exit(1);
    } else if (pid == 0) {
        // --- CHILD PROCESS ---
        printf("2. Child process running.\n");
        
        // Yield the CPU to test if the scheduler handles multiple threads properly
        sched_yield(); 
        
        // Exit with a specific secret code
        exit(42); 
    } else {
        // --- PARENT PROCESS ---
        int status = 0;
        
        // Wait for the specific child PID to finish
        waitpid(pid, &status, 0);
        
        printf("3. Parent saw child exit with status %d.\n", status);
    }

    return 0;
}