
#include "thread.h"

#include "debug.h"
#include "heap.h"
#include "per_core.h"
#include "pit.h"
#include "print.h"
#include "queue.h"
#include "vmm.h"

extern uint64_t __tls_start__;
extern uint64_t __tls_end__;

extern "C" [[gnu::force_align_arg_pointer]]
void impl_bounce_to_yield() {
  Thread::yield();
}

namespace impl {

Atomic<uint64_t> n_active{0};

Queue<TCB, SpinLock> readyQueue{};
Queue<TCB, SpinLock> reaperQueue{};

void reap() {
  while (true) {
    auto tcb = reaperQueue.remove();
    if (tcb == nullptr)
      return;

    TCB::dealloc(tcb);
    if (n_active.sub_fetch(1) == 0) {
      shutdown(true);
    }
  }
}

TCB *TCB::alloc() {
  auto tls_count = (&__tls_end__ - &__tls_start__);
  auto tls_bytes = tls_count * sizeof(uint64_t);
  auto size = tls_bytes + sizeof(TCB);
  auto mem = (uint64_t *)malloc(size);
  ASSERT(mem != nullptr);

  for (int i = 0; i < tls_count; i++) {
    mem[i] = (&__tls_start__)[i];
  }

  TCB *tcb = (TCB *)(mem + tls_count);
  tcb->self = tcb;
  tcb->is_idle = false;
  tcb->fun = nullptr;
  tcb->the_stack = nullptr;
  tcb->saved_rsp = 0;
  return tcb;
}

void TCB::dealloc(TCB *tcb) {
  if (tcb->the_stack) {
    delete[] tcb->the_stack;
  }
  if (tcb->fun) {
    delete tcb->fun;
  }

  auto tls_count = (&__tls_end__ - &__tls_start__);
  auto mem = (uint64_t *)tcb - tls_count;
  free(mem);
}

void TCB::post_switch() {
  auto me = impl::TCB::current();
  auto per_core = PerCore::get();
  per_core->current_thread = me;
  per_core->saved_fsbase = uint64_t(me);
  per_core->saved_rsp = uint64_t(me->stack_bottom);
  per_core->tss.rsp0 = uint64_t(me->stack_bottom);
  switch (action) {
  case Action::Ready:
    ASSERT(!is_idle);
    readyQueue.add(this);
    break;
  case Action::Stop:
    ASSERT(!is_idle);
    reaperQueue.add(this);
    break;
  case Action::Wait:
    ASSERT(!is_idle);
    ASSERT(cond != nullptr);
    ASSERT(cond_lock != nullptr);
    cond_lock->lock();
    if (cond->epoch == cond_epoch) {
      cond->waiting.add(this);
      cond_lock->unlock();
    } else {
      cond_lock->unlock();
      readyQueue.add(this);
    }
    break;
  case Action::None:
    break;
  default:
    KPANIC("unknown action ?\n", action);
  }
}

[[gnu::force_align_arg_pointer]] [[noreturn]]
void entry(TCB *prev) {
  prev->post_switch();
  auto me = TCB::current();

  reap();
  VMM::init_thread();

  auto fun = me->fun;
  if (fun != nullptr) {
    fun->doit();
    delete fun;
    me->fun = nullptr;
  }
  Thread::stop();
}

} // namespace impl

void Thread::yield() {
  using namespace impl;

  ASSERT(!is_disabled());

  auto me = TCB::current();

  auto next = readyQueue.remove();
  if (next == nullptr) {
    return;
  }

  ASSERT(me != next);

  me->action = Action::Ready;

  context_switch_(next)->post_switch();
  ASSERT(me == TCB::current());
  ASSERT(!is_disabled());
  // ASSERT(me->disable_preemption_count == 0);
}

static constexpr size_t STACK_QUADS = 2048;

void impl::make_thread(Fun *fun) {

  n_active.add_fetch(1);

  reap();

  auto tcb = TCB::alloc();
  tcb->fun = fun;

  // initialize stack
  tcb->the_stack = new uintptr_t[STACK_QUADS];
  tcb->stack_bottom = tcb->the_stack + STACK_QUADS;
  tcb->the_stack[STACK_QUADS - 1] = (uintptr_t)entry; // return address
  tcb->the_stack[STACK_QUADS - 2] = 0;                // rbx
  tcb->the_stack[STACK_QUADS - 3] = 0;                // rbp
  tcb->the_stack[STACK_QUADS - 4] = 0;                // r12
  tcb->the_stack[STACK_QUADS - 5] = 0;                // r13
  tcb->the_stack[STACK_QUADS - 6] = 0;                // r14
  tcb->the_stack[STACK_QUADS - 7] = 0;                // r15
  tcb->the_stack[STACK_QUADS - 8] = 0x200;            // rflags (IF=1)
  tcb->the_stack[STACK_QUADS - 9] = 0;                // cr2
  tcb->the_stack[STACK_QUADS - 10] = impl::common_cr3;
  tcb->saved_rsp = (uintptr_t)&tcb->the_stack[STACK_QUADS - 10];

  readyQueue.add(tcb);
}

void Thread::sleep(uint64_t ms) {
  // TODO: do better
  auto me = impl::TCB::current();
  ASSERT(me != nullptr);
  ASSERT(me->self == me);
  ASSERT(!me->is_idle);

  auto when = ms + Pit::jiffies;
  while (Pit::jiffies < when) {
    ASSERT(!is_disabled());
    yield();
    ASSERT(me == impl::TCB::current());
    ASSERT(!is_disabled());
  }
}

[[noreturn]] void Thread::stop() {
  using namespace impl;

  VMM::fini_thread();

  auto me = TCB::current();
  ASSERT(me != nullptr);
  ASSERT(me->self == me);
  ASSERT(!me->is_idle);

  auto next = readyQueue.remove();
  if (next == nullptr) {
    next = PerCore::get()->idle_thread;
  } else {
    ASSERT(!next->is_idle);
  }
  me->action = Action::Stop;
  context_switch_(next);
  KPANIC("internal error, block returned for ?\n", me);
}

void do_print(impl::Action a) {
  switch (a) {
  case impl::Action::Ready:
    do_print("Ready");
    break;
  case impl::Action::None:
    do_print("None");
    break;
  case impl::Action::Stop:
    do_print("Stop");
    break;
  case impl::Action::Wait:
    do_print("Wait");
    break;
  default:
    do_print("?");
  }
}

[[noreturn]] void impl::event_loop() {
  // no need to disable preemption, we're the idle thread
  // we'll do it anyway in order to please the accountants
  auto tcb = TCB::current();
  ASSERT(tcb != nullptr);
  ASSERT(tcb->is_idle);

  while (true) {
    ASSERT(!is_disabled());
    impl::reap();

    auto next = impl::readyQueue.remove();
    if (next != nullptr) {
      tcb->action = Action::None;
      ASSERT(!next->is_idle);
      context_switch_(next)->post_switch(); // post_switch re-enables preemption
    } else {
      stuckInALoop();
    }
  }
}

void impl::bootstrap(std::uint64_t lapic_id) {
  KPRINT("bootstrapping core ?\n", lapic_id);

  ASSERT(lapic_id == rdmsr(0x802));
  KPRINT("Local APIC Version Register ?\n", rdmsr(0x803));

  wrfsbase(0); // not running in a thread yet

  // wrap in a TCB, will eventually become the idle thread for this core
  auto tcb = leak(TCB::alloc(), true);
  tcb->is_idle = true;

  // now we're officially running in a thread
  wrfsbase(uintptr_t(tcb));
}
