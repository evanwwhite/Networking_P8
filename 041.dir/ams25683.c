#include "libc.h"

static long slen(const char *s) {
    long n = 0;
    while (s[n] != 0) {
        n += 1;
    }
    return n;
}

static void put(const char *s) {
    write(1, s, slen(s));
}

static void putnum(long n) {
    char buf[24];
    int i = 0;
    if (n == 0) {
        put("0");
        return;
    }
    if (n < 0) {
        put("-");
        n = -n;
    }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) {
        write(1, &buf[--i], 1);
    }
}

int main() {
    char* heap = (char*)brk(0);
    brk(heap + 8192);
    heap[0] = 'A';
    heap[4096] = 'B';
    put("*** brk ");
    putnum(heap[0]);
    put(" ");
    putnum(heap[4096]);
    put("\n");

    int fd = open("/data/data.txt", 0, 0);
    char first[9];
    long n = read(fd, first, 8);
    first[n] = 0;
    put("*** file ");
    putnum(fd);
    put(" ");
    putnum(n);
    put(" ");
    put(first);
    put("\n");

    long pos = lseek(fd, 5, 0);
    char second[5];
    long m = read(fd, second, 4);
    second[m] = 0;
    put("*** seek ");
    putnum(pos);
    put(" ");
    put(second);
    put("\n");
    close(fd);

    int status = 0;
    int child = fork();
    if (child == 0) {
        put("*** child 0\n");
        return 7;
    }
    int waited = waitpid(child, &status, 0);
    put("*** wait ");
    putnum(waited);
    put(" ");
    putnum(status);
    put("\n");

    return 0;
}
