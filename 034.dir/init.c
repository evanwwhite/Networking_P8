/*
 * vayani - P7 Test Case
 * Tests: sched_yield (syscall 24), brk (syscall 12)
 *
 * [1] sched_yield returns 0 on success
 * [2] global variables survive 10 consecutive yields
 * [3] stack variable survives a yield (callee-saved register / stack frame intact)
 * [4] brk-allocated heap memory is intact after a yield
 * 
 * claude helped create this testcase
 */

#include "libc.h"

static volatile int  counter = 0;
static volatile unsigned int magic = 0xdeadbeef;

int main() {
    printf("*** vayani: sched_yield test\n");

    /* [1] return value */
    printf("*** [1] return value\n");
    int ret = sched_yield();
    if (ret != 0) {
        printf("*** FAIL [1]: returned %d\n", ret);
        return 1;
    }
    printf("*** [1] ok\n");

    /* [2] global state survives many yields */
    printf("*** [2] global state\n");
    counter = 0;
    for (int i = 0; i < 10; i++) {
        counter++;
        sched_yield();
    }
    if (counter != 10) {
        printf("*** FAIL [2]: counter=%d\n", counter);
        return 1;
    }
    printf("*** [2] ok\n");

    /* [3] stack variable survives a yield */
    printf("*** [3] stack state\n");
    int local = 0x1234;
    sched_yield();
    if (local != 0x1234) {
        printf("*** FAIL [3]: local=%d\n", local);
        return 1;
    }
    printf("*** [3] ok\n");

    /* [4] brk heap survives a yield */
    printf("*** [4] heap across yield\n");
    void *pbrk     = brk((void *)0);
    void *new_brk  = brk((char *)pbrk + 0x1000);
    if (new_brk != (char *)pbrk + 0x1000) {
        printf("*** FAIL [4]: brk did not expand\n");
        return 1;
    }
    volatile unsigned int *hp = (volatile unsigned int *)pbrk;
    *hp = 0x5a5a5a5a;
    sched_yield();
    if (*hp != 0x5a5a5a5a) {
        printf("*** FAIL [4]: heap corrupted across yield\n");
        return 1;
    }
    printf("*** [4] ok\n");

    printf("*** all done\n");
    return 0;
}
