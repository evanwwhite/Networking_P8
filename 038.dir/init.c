#include "libc.h"

int main() {
    int pid = fork();
    //child return
    if (pid == 0) {
        printf("%s", "*** child\n");
        exit(0);
    }
    if (pid < 0) {
        printf("%s", "*** bad fork\n");
        return 1;
    }
    //parent return
    int status = 0;
    waitpid(pid, &status, 0);
    printf("%s", "*** parent\n");
    return 0;
}
