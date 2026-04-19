/*
 * avika - P7 Test Case
 * Tests: write, fork, waitpid, exit
 * Attack: corrupting kernel memory via malicious %rsp on syscall entry
 */

#include "libc.h"

int fork();
int waitpid(int pid, int* status, int options);

int main() {

    /* Test 1: write
     * Basic sanity check. If this fails, nothing else works. */
    printf("*** [1] write: ok\n");

    /* Test 2: fork + waitpid
     * fork() returns twice: 0 to the child, child's pid to the parent.
     * Only the child prints during the fork section to avoid any
     * ambiguity about scheduling order between parent and child.
     * Parent blocks on waitpid() until child exits, then confirms. */
    int pid = fork();

    if (pid == 0) {
        /* child: runs independently in its own address space */
        printf("*** [2] fork: child running\n");
        exit(0);
    } else {
        /* parent: block until child is done, then confirm */
        waitpid(pid, 0, 0);
        printf("*** [2] fork: parent and child both done\n");
    }

    /* Attack: bad %rsp on syscall entry
     *
     * The syscall instruction does NOT switch stacks (unlike interrupts
     * which use the TSS). The kernel's syscallHandler_ in machine.S
     * immediately pushes all registers onto whatever %rsp is — if we
     * point %rsp into kernel memory first, those pushes corrupt it.
     *
     * We set %rsp = 0xffff800000000000 (kernel address space base)
     * then issue a write() syscall. The kernel will push saved registers
     * directly into its own memory, causing a crash. */
    printf("*** [3] attack: setting rsp to kernel address\n");

    asm volatile (
        "mov $0xffff800000000000, %%rsp\n\t"  /* point rsp into kernel memory */
        "mov $1, %%rax\n\t"                   /* write syscall number         */
        "syscall\n\t"                          /* kernel corrupts itself here  */
        ::: "rax"
    );

    /* should never reach here on the unpatched kernel */
    printf("*** ERROR: kernel did not crash\n");
    return 0;
}