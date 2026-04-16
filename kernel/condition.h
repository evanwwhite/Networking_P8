#pragma once

#include "atomic.h"
#include "per_core.h"
#include "queue.h"
#include "thread.h"

class Condition {
private:
  Queue<impl::TCB, NoLock> waiting{};
  volatile uint64_t epoch = 0;

  friend class impl::TCB;

public:
  Condition() = default;
  Condition(const Condition &) = delete;
  Condition &operator=(const Condition &) = delete;
  ~Condition() = default;

  // Pre-condition: current thread holds the lock
  // Post-condition: current thread hold the lock
  [[gnu::noinline]]
  void wait(SpinLock &lock) {
    // assumes lock is held by the caller
    auto me = impl::TCB::current();
    if (me == nullptr || me->is_idle) {
      // not a real thread
      lock.unlock();
      stuckInALoop();
      lock.lock();
      return;
    }
    // capture the wait state so we can release the spin lock as
    // quickly as we can

    impl::TCB::current();
    me->cond_epoch = epoch;
    lock.unlock();

    auto next = impl::readyQueue.remove();
    if (next == nullptr) {
      // no next thread, grab the lock and return
      lock.lock();
      return;
    }

    me->cond_lock = &lock;
    me->action = impl::Action::Wait;
    me->cond = this;
    // switch, the post switch action we double-check the condition
    // and add the current thread to the appropriate queue
    context_switch_(next)->post_switch();

    // we're back, grab the lock again
    lock.lock();
  }

  void notify_one(SpinLock &lock) {
    // assumes lock is held by the caller, releases the lock
    auto tcb = waiting.remove();
    epoch += 1;
    lock.unlock();
    if (tcb != nullptr) {
      impl::readyQueue.add(tcb);
    }
  }
  void notify_all(SpinLock &lock) {
    // assumes lock is held by the caller, release the lock
    auto p = waiting.remove_all();
    epoch += 1;
    lock.unlock();
    while (p != nullptr) {
      auto next = p->next;
      impl::readyQueue.add(p);
      p = next;
    }
  }
};
