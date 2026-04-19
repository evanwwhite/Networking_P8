#include "libc.h"

volatile int x = 5;

//This testcases checks fork, waitpid, and exit 
// X starts at 5 before fork. The child then changes its copy 
//of x to 10 and exits. The parents waits for the child, then checks 
// that its own copy of x is still 5. The test tests process splitting, seperate memory
// after fork, child termination, and parent waiting behavior
// AI helped me with the idea 

int main() {
    int pid;
    int status = -1;
    printf("*** before fork occurs x=%d\n", x);
    pid = fork();

    if (pid < 0) {
        printf("*** fork failed :(\n");
        return 1;
    }

    if (pid == 0) {
        x = 10;
        printf("*** child x=%d\n", x);
        exit(7);
        printf("*** ERROR child continued x=%d\n", x);
        return 2;
    }

    printf("*** parent childpid worked\n");

    if (waitpid(pid, &status, 0) < 0) {
        printf("*** waitpid failed\n");
        return 3;
    }
    printf("*** parent after waitpid x=%d\n", x);
    printf("*** finished status worked\n");

    return 0;
}