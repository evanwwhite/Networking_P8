#include "libc.h"
 
volatile int n = 4;
int bss_counter[4];
 
int main() {
    // print cubes
    for (int i = 1; i <= n; i++) {
        printf("*** %d cubed is %d\n", i, i * i * i);
    }
 
    // bss is zero-initialized — sum must be 0
    int sum = 0;
    for (int i = 0; i < 4; i++) {
        sum += bss_counter[i];
    }
    printf("*** bss sum is %d\n", sum);
 
    // basic arithmetic checks
    printf("*** 7 + 8 is %d\n", 7 + 8);
    printf("*** 10 - 3 is %d\n", 10 - 3);
    printf("*** 6 * 7 is %d\n", 6 * 7);
 
    return 0;
}