#include <stdio.h>

volatile int x = 666;

int main() {
    printf("*** hello, %d\n", x);
    x = 42;
    printf("*** hello, %d\n", x);


    return 0;
}
