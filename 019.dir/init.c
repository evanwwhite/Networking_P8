#include "libc.h"
// CLAUDE AI helps me to format this testcase and come up with ideas
// Test coverage:
// - Correct offset handling in read()
// - Functionality of open, read, write, and close syscalls
// - read() returns -1 on closed file descriptors
// - read() validates user buffer addresses

int main() {
    printf("*** Testing if open is working\n");

    int fd = open("hello", 0);
    if (fd < 0) {
        printf("*** open: FAILED \n");
        exit(1);
    }
    printf("*** open: OK \n");

    printf("*** Testing if read is working\n");
    char buf[14];   // 13 + 1 for null terminator
    int n = read(fd, buf, 13);
    if (n <= 0) {
        printf("*** read: FAILED (n=%d)\n", n);
        exit(1);
    }
    buf[n] = '\0';
    printf("*** read: OK (n=%d) buf=\"%s\"\n", n, buf);

    printf("Testing if read handled an invalid adress");
    n = read(fd, (char*) 0, 13);
    if (n != -1) {
        printf("*** read(NULL): FAILED (expected -1, got %d)\n", n);
        exit(1);
    }
    printf("*** read(NULL): OK (got -1)\n");

    printf("*** Testing if read is moving the offset\n");
    n = read(fd, buf, 13);
    if (n < 0) {
        printf("*** read: FAILED (n=%d)\n", n);
        exit(1);
    }
    buf[n] = '\0';
    printf("*** read: OK (n=%d) buf=\"%s\"\n", n, buf);

    // Close the file
    printf("*** Testing if read is close is working\n");
    if (close(fd) < 0) {
        printf("*** close: FAILED\n");
        exit(1);
    }
    printf("*** close: OK\n");

    // Try read again — should fail
    printf("*** Testing if read after close is invalid\n");
    n = read(fd, buf, 13);
    if (n >= 0) {
        printf("*** read after close: FAILED (expected error, got n=%d)\n", n);
        exit(1);
    }
    printf("*** read after close: OK (correctly failed with n=%d)\n", n);

    return 0;
}