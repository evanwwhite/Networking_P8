#include "libc.h"

/*
 * Test that checks if accessing kernel memory won't be allowed
 * Base code from t0.dir/
 */
int main() {
    char hello[] = "*** Hello, World! Beginning test to access kernel\n";
    char end[] = "*** Made it to the end!\n";

    /*
     * 1. (1): Write to stdout
     * 2. (const char *)(0xffffffffffffff67): Address somewhere in the kernel
     * 3. (8): Attempt to write 8 bytes from the kernel address
     */
    write(1, hello, sizeof(hello) - 1);
    write(1, (const char *)(0xffffffffffffff67), 8);
    write(1, end, sizeof(end) - 1);

    return 0;
}
