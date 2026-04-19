// this is based on a true story
// if my testcase is picked i will publish this story
#include "libc.h"


/**
 * a true story
 * 
 * abstract: this test case tests basic fork and waitpid implementation
 * It does this by birthing a child process via fork
 * implicitly testing that the return value of fork for a child process is 0
 * if child process yields, parent should still block
 * parent will resume when child exits into the real world 
 */
int main() {

    printf("*** I am about to give birth\n");
    int id = fork(); // the process will undergo mitosis to produce a child

    // fork returns twice, one for the parent process... and one for the its newborn child process 🥹

    if (id == 0) { // the child process returns with pid 0. What a cute baby process!
        printf("*** hehe I am a child. goo goo ga ga\n");
        // now the child will start growing up
        for (int i = 0; i < 3; i++) {
            printf("*** [child is growing up: age %d]\n", i);
            sched_yield(); // child process yields but will come back. Parent, you better wait on your child! do not unblock.
        }
        exit(67);

    } else { // the parent process returns with pid > 0. what an unc.
        // the parent process is raising the child process by waiting on it
        int status;
        waitpid(id, &status, 0); // the parent will block on the child, when the child exits the parent will return from waitpid
        printf("*** [parent finished raising child, child exited to the heap world with status %d 🥹]\n", status);
        printf("*** hehe I am a parent, [sobs]my child is all grown up :')\n");
        exit(439);

    }
    // the end. based on a true story
}
