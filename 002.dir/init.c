// Simple brk Test
/**
 * - This testcase verifies that the brk syscall functions as intended
 *   > Checks that syscall 12: brk has been implemented [SECTION 1]
 *     ~ Takes arguments: args[0] = (void*) addr
 *   > Checks that the implementation matches the brk syscall, not brk libc wrapper
 *     ~ Returns: (int) program break
 *       * program break = first invalid addr after the data segment (heap)
 *   > Checks that brk only accepts valid addr [SECTION 2]
 *     ~ Does not move program break if addr is...
 *       > before the data segment (in loaded code)
 *       > outside valid process memory (in shared address space)
 *   > Checks that brk moves the program break as intended [SECTION 3]
 *     ~ Moves program break...
 *       > forward, if addr is valid && greater than current program break
 *       > backward, if addr is valid && less than current program break
 *   > Checks that brk allocates memory as intended [SECTION 4]
 *     ~ brk allows for the use of memory with init program break <= addr < new program break
 *     ~ brk does not free pages if program break is in middle of page
 */

// Test Notes
/**
 * - To pass this test (and t0), you can implement a trivial exit that just calls Thread::stop()
 * - brk(void* addr) is defined in libc.h and brk is defined in machine.S 
 */

#include "libc.h"

#define TEST_NUM 2
#define MAX_PRIVATE 0x0000800000000000
#define MAX_SHARED 0xFFFFFFFF80000000

void toFile() { printf("*** "); }

int main() {
    printf("*** Test 00%d start\n", TEST_NUM);

    printf("*** Section %d start\n", 1); // [SECTION 1]
    // Checks that syscall 12: brk has been implemented
    void* pbrk_init = brk((void*) 0); // fail (0 addr always invalid), ret val is initial program break
    // Checks that the implementation matches the brk syscall, not brk libc wrapper
    if (pbrk_init < (void*) 0x1000 || pbrk_init >= (void*) 0x0000800000000000) toFile();
    printf("initial program break @ %p\n", pbrk_init); // syscall returns addr, NOT 0/-1 && addr is in valid range
    printf("*** Section %d end\n", 1);

    printf("*** Section %d start\n", 2);
    // Checks that brk only accepts valid addr
    void* pbrk = brk((void*) 0x4000); // fail, in program data
    if (pbrk != pbrk_init) toFile();
    printf("program break @ %p\n", pbrk);
    pbrk = brk((void*) (MAX_SHARED - 0x80000000)); // fail, in shared mapping range
    if (pbrk != pbrk_init) toFile();
    printf("program break @ %p\n", pbrk);
    printf("*** Section %d end\n", 2);

    // NOTE: assumes standard heap/stack placement for validity (heap at min addr, stack at max addr avail)
    printf("*** Section %d start\n", 3);
    // Checks that brk moves the program break as intended
    // Forward moves
    pbrk = brk((void*) (pbrk + 0x1000));
    if (pbrk != pbrk_init + 0x1000) toFile();
    printf("program break @ %p\n", pbrk);
    pbrk = brk((void*) (pbrk + 0x3894)); // program break should be exact
    if (pbrk != pbrk_init + 0x3894 + 0x1000) toFile();
    printf("program break @ %p\n", pbrk);
    // Backward moves
    pbrk = brk((void*) (pbrk - 0x2000));
    if (pbrk != pbrk_init + 0x1000 + 0x3894 - 0x2000) toFile();
    printf("program break @ %p\n", pbrk);
    pbrk = brk((void*) (pbrk - 0x0375));
    if (pbrk != pbrk_init + 0x1000 + 0x3894 - 0x2000 - 0x0375) toFile();
    printf("program break @ %p\n", pbrk);
    printf("*** Section %d end\n", 3);

    printf("*** Section %d start\n", 4);
    // Checks that brk allocates memory as intended
    unsigned long long* ptr = (unsigned long long*) (pbrk_init + (((unsigned long long) (pbrk - pbrk_init)) / 2));
    *ptr = 0x002; // should succeed if brk actually allocated heap
    if (*ptr != 0x002) toFile();
    printf("wrote %d to %p\n", *ptr, ptr);
    pbrk = brk((void*) pbrk_init); // reset program break
    if (pbrk != pbrk_init) toFile();
    printf("program break @ %p\n", pbrk);
    pbrk = brk((void*) pbrk_init + 0x1000);
    if (pbrk != pbrk_init + 0x1000) toFile();
    printf("program break @ %p\n", pbrk);
    pbrk = brk((void*) pbrk - 0x0649); // middle of same page (assuming page-aligned, non-arbitrary pbrk_init)
    if (pbrk != pbrk_init + 0x1000 - 0x0649) toFile();
    printf("program break @ %p\n", pbrk);
    ptr = (unsigned long long*) (pbrk - sizeof(unsigned long long));
    *ptr = 0x002; // should succeed, page should not have been freed
    printf("wrote %d to %p\n", *ptr, ptr);
    printf("*** Section %d end\n", 4);

    printf("*** Test 00%d end\n", TEST_NUM);

    return 0;
}
