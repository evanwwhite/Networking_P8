/* Copyright (C) 2025 Ahmed Gheith and contributors.
 *
 * Use restricted to classroom projects.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "pit.h"
#include "debug.h"
#include "idt.h"
#include "machine.h"
#include "per_core.h"
#include "print.h"
#include "system_main.h"
#include "thread.h"
#include "x86_64.h"

/*
 * The old PIT runs at a fixed frequency of 1193182Hz but doesn't support
 * multiple processors
 *
 * The APIT supports multiple processors but runs at a frequency we don't
 * know
 *
 * Here is plan:
 *    we're going to use the PIT to count how many cycles it takes
 *    for the APIT to run at the frequency we want then we're going
 *    to switch over to the APIT and abandon our old trusty friend
 *
 *    Running on an emulator complicates things because the emulator
 *    will never get timing exactly right so we try to do the calibration
 *    in a loop and hope for the best
 */

/* The standard frequency of the PIT */
constexpr uint32_t PIT_FREQ = 1193182;

/* Were we want the APIT to iunterrupt us */
constexpr uint32_t APIT_vector = 40;

volatile uint64_t Pit::jiffies = 0;
uint64_t Pit::jiffiesPerSecond = 0;

static uint64_t apitCounter = 0;

namespace impl {
Atomic<uint64_t> n_preempt{0};
}

/* Do what you need to do in order to run the APIT at the given
 * frequency. Should be called by the bootstrap CPI
 */
void Pit::init_system(uint32_t hz) {

  // Our objective is to count how many APIT tickes happen in
  // a second. To do that we're going to program the PIT at
  // 20Hz and wait for it to signal us 20 times, giving us
  // a delay if 1 second.
  //
  // We will set the APIT counter to 0xffffffff at the begining
  // of the second and see how far it went at the end and this
  // will help us determine its frequency
  //
  // Why 20Hz? becasue the PIT has a fixed frequency of 1193182Hz
  // and a 16 bit divider, 20Hz will require a divider of 59658 which
  // we can fit in 16 bits
#if 1
  Msr::LVTTimerRegister().write(0x00010000);            // oneshot, masked, ...
  Msr::DivideConfigurationRegister().write(0x0000000B); // divide by 1

  // Now program the PIT to compute the frequency
  KPRINT("| pitInit freq ?Hz\n", Dec(hz));
  uint32_t d = PIT_FREQ / 20;

  if ((d & 0xffff) != d) {
    KPRINT("| pitInit invalid divider ?\n", Dec(d));
    d = 0xffff;
  }
  KPRINT("| pitInit divider ?\n", Dec(d));

  uint32_t initial = 0xffffffff;
  Msr::InitialCountRegister().write(initial);

  outb(0x61, 1); // speaker off, gate on

  outb(0x43, 0b10110110); //  10 -> channel#2
                          //  11 -> lobyte/hibyte
                          // 011 -> square wave generator
                          //   0 -> count in binary

  // write the divider to the PIT
  outb(0x42, d);
  outb(0x42, d >> 8);

  uint32_t last = inb(0x61) & 0x20;
  uint32_t changes = 0;
  // The PIT counts twice as fast when it runs in the
  // square-wave generator mode. So, the state is
  // really changing at 40Hz and we should loop
  // for 40 iterations if we want to wait for a second
  //
  while (changes < 40) {
    uint32_t t = inb(0x61) & 0x20;
    if (t != last) {
      changes++;
      last = t;
    }
  }

  uint32_t diff = initial - Msr::CurrentCountRegister().read();

  // stop the PIT
  outb(0x61, 0);

  KPRINT("| APIT running at ?Hz\n", Dec(diff));
#endif
  apitCounter = diff / hz;
  jiffiesPerSecond = hz;
  KPRINT("| APIT counter=? for ?Hz\n", Dec(apitCounter), Dec(hz));

  // Register the APIT interrupt handler
  IDT::interrupt(APIT_vector, uintptr_t(apitHandler_));
}

// Called by each CPU in order to initialize its own PIT
void Pit::init_core() {

  if (apitCounter == 0) {
    KPANIC("apiCounter == ?, did you call Pit::calibrate\n", apitCounter);
  }

  Msr::DivideConfigurationRegister().write(0x0000000B); // divide by 1

  // The following line will enable timer interrupts for this CPU
  // You better be prepared for it
  Msr::LVTTimerRegister().write((1 << 17) | // Timer mode: 1 -> Periodic
                                0 << 16 |   // mask: 0 -> interrupts not masked
                                APIT_vector // the interrupt vector
  );

  // Let's go
  Msr::InitialCountRegister().write(apitCounter);
}

struct ApitInterruptFrame {
  std::uint64_t rax;
  std::uint64_t rbx;
  std::uint64_t rcx;
  std::uint64_t rdx;
  std::uint64_t rdi;
  std::uint64_t rsi;
  std::uint64_t rbp;
  std::uint64_t r8;
  std::uint64_t r9;
  std::uint64_t r10;
  std::uint64_t r11;
  std::uint64_t r12;
  std::uint64_t r13;
  std::uint64_t r14;
  std::uint64_t r15;
  std::uint64_t rip;
  std::uint64_t cs;
  std::uint64_t rflags;
  std::uint64_t rsp;
  std::uint64_t ss;
};

extern "C" [[gnu::force_align_arg_pointer]] void
apitHandler(ApitInterruptFrame *frame) {
  // Interrupts are disabled on entry and should remain disabled
  // while we're running on the current stack.

  auto tcb = impl::TCB::current();
  if (tcb == nullptr)
    return;

  ASSERT(tcb != nullptr);
  // interrupts are disabled.

  auto id = PerCore::id();

  if (id == 0) {
    // One core updates the global time
    Pit::jiffies = Pit::jiffies + 1;
  }

  // Acknowledge the interrupt, interrupts are still disabled
  Msr::EOI().write(0);

  if (frame->cs == Sys::KERNEL_CS) {
    /* The kernel is non pre-emptible */
    ASSERT(frame->rip >= UINT64_MAX / 2);
    return;
  }

  ASSERT(!tcb->is_idle);

  const auto cs = frame->cs;
  ASSERT(cs == Sys::USER_CS);
  const auto rip = frame->rip;
  ASSERT(rip < UINT64_MAX / 2);

  impl::n_preempt.add_fetch(1);

  // why is it safe it enable interrupt here?
  //     - we only get to this point if we were running in user mode with
  //       interrupts enabled
  //     - we are running in the kernel, itself not preemptible
  //     - the TSS was setup to point to a thread dedicated kernel stack
  //     - other interrupt handlers will use an IST stack; never this one

  sti();
  Thread::yield(); // Thread::yield doesn't block
}
