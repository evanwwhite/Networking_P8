#include "libc.h"
#include <stdint.h>

/*

 *
 * what is being tested:
 *   -successful write returns exact byte count.
 *   -zero length writes are handled safely.
 *   -kernel space pointers are rejected.
 *
 */

static int failures = 0;

static size_t local_strlen(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static void validateLong(const char *name, long got, long expected, int line,
                                                    const char *likely_cause) {
    if (got != expected) {
        printf("*** FAIL line %d: %s (got=%ld expected=%ld)\n", line, name, got,
                     expected);
        printf("*** FAIL detail: likely because %s\n", likely_cause);
        failures++;
        return;
    }

    printf("*** PASS line %d: %s\n", line, name);
}

#define validate(name, got, expected, likely_cause)                              \
    validateLong((name), (long)(got), (long)(expected), __LINE__, (likely_cause))

int main() {
    const char *payload = "*** payload: write(fd=1) basic path\n";
    size_t payload_len = local_strlen(payload);

    // Test 1: happy path write to stdout
    long rc_ok = (long)write(1, payload, payload_len);
    validate("write(fd=1) returns full length", rc_ok, (long)payload_len,
                     "the kernel write path may not return the number of bytes written");

    //  Test 2: len=0 should succeed and must not dereference the pointe
    long rc_zero_len = (long)write(1, (const char *)0, 0);
    validate("write(len=0) returns 0", rc_zero_len, 0,
                     "zero-length writes are not handled as a no-op");

    //  Test 3: obvious kernel space pointer should be rejected, not read
    long rc_kernel_ptr = (long)write(1, (const char *)UINT64_C(0xFFFF800000000000),
                                                                     1);
    validate("write(kernel pointer) rejected", rc_kernel_ptr, -1,
                     "user/kernel pointer range checks are missing");

    if (failures == 0) {
        printf("*** PASS summary: %s\n", "all write() validation checks passed");
        return 0;
    }

    printf("*** FAIL summary: %d checks failed\n", failures);
    return 1;
}
