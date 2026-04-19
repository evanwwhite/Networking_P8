#include "libc.h"

void r();

int main() {
    
    int fd = fork();
    if (fd == 0) { // child
        r();
    } else { // parent
        int buf;
        waitpid(fd, &buf, 0);
        printf("*** made it\n");
    }

    return 0;
}

void r() {
    r();
}