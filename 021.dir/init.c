#include "libc.h"

int main() {
    write(1, "*** [1] write: ok\n", 18);

    int fd = open("/hello", 0);
    if (fd < 0) {
        write(1, "*** [2] open: FAILED\n", 21);
        exit(1);
    }
    write(1, "*** [2] open: ok\n", 17);

    char buf[32];
    int n = read(fd, buf, 5);
    if (n <= 0) {
        write(1, "*** [3] read: FAILED\n", 21);
        exit(1);
    }
    write(1, "*** [3] read: ok\n", 17);

    close(fd);
    write(1, "*** [4] close: ok\n", 18);

    // Attack: pass kernel address as read buffer
    // a correct kernel rejects this, buggy kernel writes to kernel memory
    int fd2 = open("/hello", 0);
    int bad_result = read(fd2, (char*)0xffffffff80000000, 5);
    if (bad_result == -1) {
        write(1, "*** [5] attack: rejected\n", 25);
    } else {
        write(1, "*** [5] attack: vulnerability\n", 30);
    }

    exit(0);
}