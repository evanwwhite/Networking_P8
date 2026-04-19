#include "libc.h"
/*
 * Tests sched_yield by printing before and after a yield call, and tests
 * file I/O by opening "hello", reading the contents, then seeking back
 * to the start and reading only the first 10 bytes.
*/
int main() {
// est 1: sched_yield
    printf("*** before yield\n");
    sched_yield();
    printf("*** after yield\n");

    // test 2, tesitng open, lseek, read, write, close
    int fd = open("hello");
    char buf[256];
    
    //read from beginning
    int n = read(fd, buf, 256);
    write(1, buf, n);
    
    //seek back to start and read again
    lseek(fd, 0);
    n = read(fd, buf, 10);  // read just first 10 bytes
    write(1, buf, 10);
    printf("\n");
    
    close(fd);
    return 0;
}
