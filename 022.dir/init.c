/*
 * darrenzw P7 security testcase; AI was used to generate portions of the assembly & c code
 *
 * Inspired by CVE-2017-1000364(Stack CLash)
 *
 * Follows a bug family but does not reproduce it literally
 * Targets the same kind of privileged stack-boundary mistake in
 * this teaching kernel: syscall entry must not save kernel state on an
 * attacker-controlled user stack.
 *
 * Test idea:
 *   1. Fill a user buffer with a sentinel byte.
 *   2. Temporarily move %rsp so it points at that buffer.
 *   3. Make a zero-length write syscall.
 *   4. Check whether the sentinel buffer changed.
 */

#include "libc.h"

#define FAKE_STACK_SIZE 4096
#define SENTINEL 0xa5

static unsigned char fake_stack[FAKE_STACK_SIZE] __attribute__((aligned(16)));

static void print(const char *msg) {
    const char *p = msg;
    while (*p != 0) {
        p++;
    }
    write(1, msg, (size_t)(p - msg));
}

static void fill_fake_stack(void) {
    for (size_t i = 0; i < FAKE_STACK_SIZE; i++) {
        fake_stack[i] = SENTINEL;
    }
}

static int fake_stack_was_modified(void) {
    for (size_t i = 0; i < FAKE_STACK_SIZE; i++) {
        if (fake_stack[i] != SENTINEL) {
            return 1;
        }
    }
    return 0;
}

static void zero_length_write_with_fake_rsp(void) {
    void *top = fake_stack + FAKE_STACK_SIZE;
    const char *empty = "";

    /*
     * Some stuff gen by chat
     * Save the real user stack in %r12, redirect %rsp to the fake stack,
     * issue write(fd=1, buf="", len=0), then restore the original stack
     * immediately after returning to user mode.
     */
    asm volatile(
        "mov %[top], %%r13\n\t"
        "mov %[empty], %%r14\n\t"
        "mov %%rsp, %%r12\n\t"
        "mov %%r13, %%rsp\n\t"
        "mov $1, %%rax\n\t"
        "mov $1, %%rdi\n\t"
        "mov %%r14, %%rsi\n\t"
        "xor %%rdx, %%rdx\n\t"
        "syscall\n\t"
        "mov %%r12, %%rsp\n\t"
        :
        : [top] "r"(top), [empty] "r"(empty)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "r12", "r13", "r14",
          "memory");
}

int main(void) {
    print("*** [1] fill fake user stack with sentinel bytes\n");
    fill_fake_stack();

    if (fake_stack_was_modified()) {
        print("*** FAIL [1]: sentinel initialization check failed\n");
        return 1;
    }

    print("*** [2] invoke zero-length write with rsp redirected\n");
    zero_length_write_with_fake_rsp();

    print("*** [3] verify whether syscall entry modified fake stack\n");
    if (fake_stack_was_modified()) {
        print("*** FAIL [3]: syscall entry wrote to user-controlled stack\n");
    } else {
        print("*** PASS [3]: syscall entry did not use user-controlled stack\n");
    }

    return 0;
}
