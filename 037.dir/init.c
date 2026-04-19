// opens hello and reads from it to verify basic file I/O works

// AI Assistance (April 10, 2026) (Claude): Helped structure the test case
// and confirm the ok file.

#include "libc.h"

int main(void) {
    char buf[32];
    int i;
    for (i = 0; i < 32; i++) buf[i] = 0;

    write(1, "*** hello from nap2869\n", 23);

    int fd = open("hello");
    if (fd < 0) {
        write(1, "*** could not open file\n", 24);
        exit(1);
    }

    int n = read(fd, buf, 5);
    if (n != 5) {
        write(1, "*** read came back wrong\n", 25);
        exit(1);
    }
    write(1, "*** file read good\n", 19);

    close(fd);
    write(1, "*** goodbye from nap2869\n", 25);
    exit(0);
}
