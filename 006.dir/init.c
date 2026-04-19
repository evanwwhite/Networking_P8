#include <fcntl.h>
#include <unistd.h>

int main() {
    char buf[64];
    int n;

    write(1, "=== File I/O Test ===\n", 22);

    int fd = open("/message.txt", O_RDONLY);
    if (fd < 0) {
        write(1, "open failed\n", 12);
        exit(1);
    }
    write(1, "open: ok\n", 9);

    n = read(fd, buf, sizeof(buf));
    if (n < 0) {
        write(1, "read failed\n", 12);
        exit(1);
    }
    write(1, "first read: ", 12);
    write(1, buf, n);

    // seek back to start
    int pos = lseek(fd, 0, SEEK_SET);
    if (pos < 0) {
        write(1, "lseek failed\n", 13);
        exit(1);
    }
    write(1, "lseek: ok\n", 10);

    n = read(fd, buf, sizeof(buf));
    write(1, "second read: ", 13);
    write(1, buf, n);

    close(fd);
    write(1, "close: ok\n", 10);
    write(1, "=== PASSED ===\n", 15);

    exit(0);
}