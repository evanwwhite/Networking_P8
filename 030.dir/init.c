// Test case created with AI assistance
// Tests: multiple write syscall sizes and exit behavior

#include "libc.h"

int main() {
    write(1, "x", 1);
    write(1, "\n", 1);
    write(1, "*** arjunk\n", 11);
    return 0;
}
