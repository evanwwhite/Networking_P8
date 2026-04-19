/*
 * Tests the write and exit syscalls, correct ELF loading of initialized (.data)
 * and zero-initialized (.bss) segments, and basic arithmetic/control flow.
 * Prints squares of 1 through n (a global volatile) to verify .data loads
 * correctly, then sums a BSS array to confirm it was zeroed on load.
 */

#include "libc.h"

volatile int n = 5;
int bss_buf[8];

int main() {
    for (int i = 1; i <= n; i++) {
        printf("*** %d squared is %d\n", i, i * i);
    }

    int sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += bss_buf[i];
    }
    printf("*** bss sum is %d\n", sum);

    return 0;
}
