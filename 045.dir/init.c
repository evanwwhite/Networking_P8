#include "libc.h"

static int fib(int n) {
    if (n <= 1) return n;
    return fib(n-1) + fib(n-2);
}

int main() {
    printf("*** start\n");
    for (int i = 0; i < 8; i++) {
        printf("*** fib(%d) = %d\n", i, fib(i));
    }
    printf("*** done\n");
    return 0;
}
