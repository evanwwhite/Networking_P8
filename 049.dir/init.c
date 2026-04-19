#include "libc.h"

/*
    Basic testcase ensuring basic functionality works as intended.
*/

volatile int x = 777;

// Code based on t0, as well as some guidance from Gemini 3
int main() {
    printf("*** hello, %d\n", x);
    x = 316;
    printf("*** John, %d\n", x);


    return 0;
}
