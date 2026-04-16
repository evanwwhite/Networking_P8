#pragma once

#include "atomic.h"
#include "fun.h"
#include "queue.h"
#include "spin_lock.h"
#include <cstdint>

class Condition;

namespace impl {

enum class Action { Ready, Stop, Wait, None };

struct TCB {
  TCB *self; // Required by the thread-local storage specification
  // Thread Control Block structure
  // Placeholder for thread-specific data

  // shared with assembly, needs to be at offset 8
  std::uintptr_t saved_rsp;

  // shared with assembly, needs to be at offset 16
  volatile uint64_t saved_rip;

  // brk management
  uint64_t min_brk = 4096;
  uint64_t brk = 4096;

  // deferred work
  Action action{Action::None};
  Condition *cond = nullptr;
  uint64_t cond_epoch = 0;
  SpinLock *cond_lock = nullptr;

  // idle thread?
  bool is_idle;

  // the stack area, nullptr for the idle thread
  uintptr_t *the_stack;
  uintptr_t *stack_bottom;

  // used for linking TCBs in the ready queue, etc.
  TCB *next;

  // thread work, nullptr for the idle thread
  Fun *fun;

  static inline TCB *current() { return (TCB *)rdfsbase(); }

  TCB() = delete;
  ~TCB() = delete;

  static TCB *alloc();
  static void dealloc(TCB *);
  inline bool can_block() const {
    auto tcb = current();
    return tcb != nullptr && !tcb->is_idle;
  }

  void post_switch();
};

void bootstrap(std::uint64_t lapic_id);
[[noreturn]] void event_loop();

extern Queue<TCB, SpinLock> readyQueue;

void make_thread(Fun *);

void reap();

} // namespace impl

extern void do_print(impl::Action a);

namespace Thread {
/* Work can either be:
 *   - a function pointer
 *   - a lambda
 *   - an instance of an object object that implements operator()()
 * Any callable object would do
 */
template <typename Work> inline void create(Work work) {
  /* wrap it in a heap allocated Fun object. Why?
   *   - we only need to pass a pointer around
   *   - allow the implementation to manage the work's lifetime
   *
   * Assumes (checked by the compiler) that work is copyable.
   *   - trivially true for function pointers
   *   - requires that lamdbas only capture copyiable objects
   *   - requires that constructor only accepts copyiable objects
   */
  impl::make_thread(new FunImpl(work));
}
[[noreturn]] void stop();
void yield();
void sleep(uint64_t ms);
} // namespace Thread
