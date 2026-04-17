#include "libc.h"

int main() {
  int x = 42;
  char *regptr = (char *)&x;
  const char *nullpointer = (const char *)0;
  const char *kernel_ptr = (const char *)0xffff800000000000ULL;
  const char *unmapped_user_ptr = (const char *)0x00007fffffffffffULL;
  // test 1
  printf("*** attack: write from NULL buffer\n");
  // If write does not validate user pointers, this triggers a kernel-mode page fault.
  write(1, nullpointer, 1);
  printf("*** survived NULL-buffer attack\n");
  // test 2
  printf("*** now poking kernel-space pointer\n");
  // If write only checks for NULL, this second attack should still crash an unpatched kernel.
  write(1, kernel_ptr, 1);
  printf("*** survived kernel-pointer attack\n");
  // test 3
  printf("*** now trying weird lengths\n");
  write(1, regptr, 0);  // should succeed and do nothing
  write(1, regptr, -1); // should fail and not crash
  printf("*** survived weird-length attack\n");
  // test 4
  printf("*** now poking unmapped user-space pointer\n");
  // This address is in user range but should be unmapped in a small test process.
  write(1, unmapped_user_ptr, 1);
  printf("*** survived unmapped-user-pointer attack\n");

  // test 5
  printf("*** now poking cross-page mapped-to-unmapped buffer\n");
  {
    const unsigned long PAGE_SIZE = 4096;
    char stack_byte = 'X';
    unsigned long page_start = ((unsigned long)&stack_byte) & ~(PAGE_SIZE - 1);
    const char *cross_page_ptr = (const char *)(page_start + PAGE_SIZE - 8);
    write(1, cross_page_ptr, 16);
  }
  printf("*** survived cross-page-buffer attack\n");

  return 0;
}
