#include "libc.h"

volatile int x = 67;

int main() {
    printf("*** hello, %d\n", x);
    x = 42;
    printf("*** hello, %d\n", x);


    return 0;
}
