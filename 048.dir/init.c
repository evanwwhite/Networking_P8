#include "libc.h"

/**
 * A series of test on the waitpid syscall. I hope it works, I couldn't finish
 * my own functionality in time to test it
 *
 * 1. The Baseline Execution
 * Confirms the fundamental waitpid functionality: a parent process can
 * successfully pause its execution to reap the exit status of its direct,
 * terminated child.
 * 2. The Sibling Anomaly
 * Asserts that sibling processes are strictly isolated and cannot reap each
 * other, enforcing the hierarchical permission structure of the process tree.
 * 3. The Ascendant Violation
 * Verifies that a process cannot invert the hierarchical process tree by
 * attempting to wait on its own creator.
 * 4. The Ghost Retrieval (Double Wait)
 * Ensures that once a child process is successfully reaped, its Thread Control
 * Block (TCB) is permanently destroyed, preventing subsequent waitpid calls
 * from retrieving a stale or recycled state.
 * 5. The Fabricated Entity
 * Tests the kernel's bounds-checking and validation logic by requesting the
 * status of an arbitrary, non-existent Process ID.
 */

int main() {
  int status;
  int pid1;
  int pid2;
  int wait_result;

  /* 1. Baseline waitpid case */
  pid1 = fork();
  if (pid1 == 0) {
    exit(0);
  } else if (pid1 > 0) {
    wait_result = waitpid(pid1, &status, 0);

    if (wait_result == pid1) {
      printf("*** Successfully waited for child\n");
    }
  }

  /* 2. Sibling anomaly testcase */
  pid1 = fork();
  if (pid1 == 0) {
    /* yield to parent */
    sched_yield();
    sched_yield();
    sched_yield();
    sched_yield();
    sched_yield();
    sched_yield();
    exit(0);
  } else if (pid1 > 0) {
    pid2 = fork();

    if (pid2 == 0) {
      wait_result = waitpid(pid1, &status, 0);

      if (wait_result == -1) {
        printf("*** Successfully denied sibling calling waitpid\n");
      } else {
        printf(
            "*** FAILED - sibling processes should not be able to call waitpid "
            "on each other\n");
      }
      exit(0);
    }

    waitpid(pid1, &status, 0);
    waitpid(pid2, &status, 0);
  }

  /* 3. Ascendant testcase */
  pid1 = fork();
  if (pid1 == 0) {
    wait_result = waitpid(1, &status, 0);

    if (wait_result == -1) {
      printf("*** Successfully denied ascendant calling waitpid\n");
    } else {
      printf(
          "*** FAILED - child processes cannot call waitpid on their parent\n");
    }
    exit(0);
  } else if (pid1 > 0) {
    waitpid(pid1, &status, 0);
  }

  /* 4. Ghost retrieval case */
  pid1 = fork();
  if (pid1 == 0) {
    exit(0);
  } else if (pid1 > 0) {
    waitpid(pid1, &status, 0);
    wait_result = waitpid(pid1, &status, 0);

    if (wait_result == -1) {
      printf("*** Successfully denied ghost retrieval\n");
    } else {
      printf("*** FAILED - waitpid should not work on dead processes\n");
    }
  }

  /* 5. Fabricated entity test */
  wait_result = waitpid(99999, &status, 0);
  if (wait_result == -1) {
    printf("*** Successfully denied fabricated entity\n");
  } else {
    printf("*** FAILED - waitpid should not work on an invalid PID\n");
  }

  return 0;
}
