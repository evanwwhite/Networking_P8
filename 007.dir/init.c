#include "libc.h"

/* this test verifies proper ELF loader handling in kernel_main; read jd57293.md for an explanation of the test and how to pass it */

char elf[] = "ELF";
int main() {
    printf("*** /init's %s file was successfully loaded without conflicting with stack region!\n", elf);

    return 0;
}
