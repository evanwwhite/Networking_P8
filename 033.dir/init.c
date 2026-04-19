#include "libc.h"

extern size_t write_on_tiny_stack(int fd, const char *buf, size_t len,
                                  void *stack_top);

static const char warmup[] = "*** warmup write on normal stack\n";
static const char mapped_edge_ok[] =
    "*** survived write on current-page edge stack\n";
static const char canary_ok[] =
    "*** canary survived current-page edge-stack syscall\n";
static const char canary_bad[] =
    "*** canary was corrupted by syscall entry\n";
static const char returned_ok[] =
    "*** returned from current-page edge-stack helper\n";

static unsigned long long pattern_for(int i) {
  // use a different recognizable pattern in each slot so corruption is obvious.
  return 0xfeed000000000000ULL | (unsigned long long)i;
}

static void reset_canary(volatile unsigned long long *edge_words) {
  // fill the first 128 bytes at the bottom of the current stack page.
  // a buggy kernel that uses the user stack for its save frame will overwrite
  // almost all of this region even if the syscall does not crash.
  for (int i = 0; i < 16; i++) {
    edge_words[i] = pattern_for(i);
  }
}

static int canary_intact(volatile unsigned long long *edge_words) {
  for (int i = 0; i < 16; i++) {
    if (edge_words[i] != pattern_for(i)) {
      return 0;
    }
  }
  return 1;
}

int main() {
  // first prove the ordinary syscall path still works.
  write(1, warmup, sizeof(warmup) - 1);

  /*
   * use a local variable to find the current user stack page at runtime.
   * that keeps the testcase independent of any particular hard-coded stack
   * address while still probing the same syscall-entry bug.
   */
  volatile unsigned long long anchor = 0;
  unsigned long long page_base = ((unsigned long long)&anchor) & ~0xfffULL;
  // keep the canary at the bottom of the same page we are about to stress.
  volatile unsigned long long *edge_words =
      (volatile unsigned long long *)page_base;

  /*
   * now do a syscall from a stack pointer that is still mapped, but lives
   * right on the edge of the current stack page.
   *
   * a buggy kernel that pushes its save frame onto user memory may still
   * return from this syscall, but it will corrupt the canary words below rsp.
   * a fixed kernel should leave that memory completely untouched.
   */
  reset_canary(edge_words);
  // 0x80 is still inside the page, but close enough to the edge to be useful.
  write_on_tiny_stack(1, mapped_edge_ok, sizeof(mapped_edge_ok) - 1,
                      (void *)(page_base + 0x80ULL));
  // report the corruption check as output so the testcase result is obvious.
  if (canary_intact(edge_words)) {
    write(1, canary_ok, sizeof(canary_ok) - 1);
  } else {
    write(1, canary_bad, sizeof(canary_bad) - 1);
  }

  // reaching this line means the syscall returned safely too.
  write(1, returned_ok, sizeof(returned_ok) - 1);

  return 0;
}
