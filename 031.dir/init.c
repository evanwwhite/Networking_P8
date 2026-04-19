#include "libc.h"
/**
 * This is based on a thing that our TAs said in
 * discussion today/ quiz question. The kernel
 * address space is now what stout used to me
 * basically the user is screwed cause they might
 * print and kill their operating system. This is
 * a test to see if the kernel address space is I
 * got the address from
 * `script.ld:19`>.limine_requests
 * 0xffffffff80000000 : ALIGN(4096)`
 * you can't fix the kernel address space issue,
 * but you can protect from writting over stout
 */
int main() {

    size_t ret = write(1, (char*)0xffffffff80000000UL, 8);
    if ((long)ret < 0)
        write(1, "*** kernel address rejected\n", 28);
    else
        write(1, "*** kernel address accessible\n", 30);
    return 0;
}
