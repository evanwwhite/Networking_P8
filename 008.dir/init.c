// This is a simple test program that verifies the basic functionality of the
// system calls read, open, close, and lseek. It should print a series of messages
// indicating the success or failure of each operation.
// Assisted by ChatGPT.
#include "libc.h"

static size_t my_strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static void print_line(const char* s) {
    write(1, s, my_strlen(s));
    write(1, "\n", 1);
}

static int eq_bytes(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

int main() {
    print_line("*** START");

    int fd = open("hello");
    if (fd < 0) {
        print_line("*** open failed");
        return 1;
    }
    print_line("*** open ok");

    char buf[16];

    int n1 = read(fd, buf, 5);
    if (n1 != 5 || !eq_bytes(buf, "abcde", 5)) {
        print_line("*** read1 failed");
        return 1;
    }
    print_line("*** read1 ok");

    long pos = lseek(fd, 2, 0); // SEEK_SET
    if (pos != 2) {
        print_line("*** seek failed");
        return 1;
    }
    print_line("*** seek ok");

    int n2 = read(fd, buf, 4);
    if (n2 != 4 || !eq_bytes(buf, "cdef", 4)) {
        print_line("*** read2 failed");
        return 1;
    }
    print_line("*** read2 ok");

    int rc = close(fd);
    if (rc != 0) {
        print_line("*** close failed");
        return 1;
    }
    print_line("*** close ok");

    print_line("*** DONE");
    return 0;
}