#include "libc.h"

/*
  This test case is a nested fork and multi child waitpid test. 

  We firstly test fork creation at 2 levels (parent -> child -> grandchild)
  We then do waitpid (blocks calling process until child with that pid exits) and collect the correct child
  We then exit and write from different proceeses

  We print in each level and check for a proper sequence
  
  ai helped w this test case
 */

int main() {
    printf("*** parent start\n");

    int pidA = fork();

    if (pidA == 0) {
        // child a
        printf("*** child A\n");
        exit(10);
    }

    int pidB = fork();

    if (pidB == 0) {
        // child b
        int pidG = fork();

        if (pidG == 0) {
            // grand child
            printf("*** grandchild\n");
            exit(30);
        }

        //child b waiting for grandchild
        int gstatus = 0;
        waitpid(pidG, &gstatus, 0);
        printf("*** child B got grandchild status %d\n", gstatus);
        exit(20);
    }

    //parent waiting for child a 
    int statusA = 0;
    waitpid(pidA, &statusA, 0); 
    printf("*** parent got child A status %d\n", statusA);

    //parent waiting for child b
    int statusB = 0;
    waitpid(pidB, &statusB, 0);
    printf("*** parent got child B status %d\n", statusB);

    printf("*** parent done\n");
    return 0;
}
