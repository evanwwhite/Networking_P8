#include "libc.h"

/*
 * ChatGPT was used to help brainstorm this ELF .bss testcase.
 * The test was reviewed and adapted for this project.
 */

#define SAY(msg) write(1, msg, sizeof(msg) - 1)

static unsigned char bss_bytes[8192 + 137];
static unsigned long long bss_words[1024];
static volatile unsigned long long initialized_word = 0x44414d49414e0001ULL;
static char initialized_text[] = "damian-data-ok";

static void fail(const char *msg) {
    write(1, msg, 31);
    __builtin_trap();
}

static int text_ok(void) {
    const char expected[] = "damian-data-ok";
    for (unsigned long i = 0; i < sizeof(expected); i++) {
        if (initialized_text[i] != expected[i]) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    SAY("*** damian: start elf bss test\n");

    if (initialized_word != 0x44414d49414e0001ULL || !text_ok()) {
        fail("*** damian: initialized data bad\n");
    }
    SAY("*** damian: initialized data ok\n");

    for (unsigned long i = 0; i < sizeof(bss_bytes); i++) {
        if (bss_bytes[i] != 0) {
            fail("*** damian: byte bss not zero\n");
        }
    }

    for (unsigned long i = 0; i < sizeof(bss_words) / sizeof(bss_words[0]); i++) {
        if (bss_words[i] != 0) {
            fail("*** damian: word bss not zero\n");
        }
    }
    SAY("*** damian: bss zeroed ok\n");

    bss_bytes[0] = 0x11;
    bss_bytes[4096] = 0x22;
    bss_bytes[sizeof(bss_bytes) - 1] = 0x33;
    bss_words[0] = 0x1111222233334444ULL;
    bss_words[1023] = 0x5555666677778888ULL;

    if (bss_bytes[0] != 0x11 ||
        bss_bytes[4096] != 0x22 ||
        bss_bytes[sizeof(bss_bytes) - 1] != 0x33 ||
        bss_words[0] != 0x1111222233334444ULL ||
        bss_words[1023] != 0x5555666677778888ULL) {
        fail("*** damian: bss writes failed\n");
    }
    SAY("*** damian: bss writable ok\n");
    SAY("*** damian: done\n");

    return 0;
}
