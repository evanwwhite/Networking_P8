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

#include "core_main.h"

#include "heap.h"
#include "idt.h"
#include "kernel_main.h"
#include "limine.h"
#include "machine.h"
#include "per_core.h"
#include "pit.h"
#include "print.h"
#include "system_main.h"
#include "thread.h"
#include "vmm.h"
#include "x86_64.h"
#include <cstdint>

Barrier *impl::start_barrier;

[[noreturn]] void impl::core_main(limine_mp_info *info) {

  // KPRINT("bootstrapping core ?\n", info->lapic_id);

  // Check supported CPU features and enable required ones
  Features features{};
  ASSERT(features.hasSSE3);
  enable_sse();
  ASSERT(features.hasFSGSBASE);
  CR4().setFSGSBASE();

  auto old_efer = Msr::EFER().read();
  KPRINT("old efer ?\n", old_efer);
  Msr::EFER().write(old_efer | 1);
  auto new_efer = Msr::EFER().read();
  KPRINT("new efer ?\n", new_efer);

  Msr::IA32_STAR().write(Sys::STAR);
  KPRINT("IA32_STAR  ?\n", Msr::IA32_STAR().read());

  Msr::IA32_LSTAR().write(uintptr_t(syscallHandler_));
  KPRINT("IA32_LSTAR ?\n", Msr::IA32_LSTAR().read());

  Msr::I32_FMASK().write(
      0x200); // disable interrupts on entry to syscall handler
  KPRINT("IA32_FMASK ?\n", Msr::I32_FMASK().read());

  // load the new GDT
  lgdt(Sys::gdtr);

  auto base = (uint64_t *)Sys::gdtr.base;
  for (int i = 0; i < 8; i++) {
    KPRINT("gdt[?] = ?\n", i, base[i]);
  }

  asm volatile("mov %0, %%ds" : : "r"(0x23));

  impl::bootstrap(info->lapic_id);

  auto tcb = impl::TCB::current();
  ASSERT(tcb != nullptr);
  ASSERT(tcb->self == tcb);
  ASSERT(tcb->is_idle);

  // PerCore data
  auto per_core = uint64_t(leak(new PerCore(info->lapic_id, tcb), true));
  wrgsbase(per_core);
  Msr::I32_KERNEL_GS_BASE().write(per_core);

  auto my_tss = &PerCore::get()->tss;
  auto tss_index = Sys::base_tss_index + 2 * PerCore::id();
  auto ptr = &((uint64_t *)Sys::gdtr.base)[tss_index];
  auto *descriptor = (impl::TSSDescriptor *)ptr;
  descriptor->init(my_tss, sizeof(impl::TSS) - 1);
  KPRINT("tss descriptor for ? is ?:?\n", info->lapic_id, ptr[0], ptr[1]);
  ltr(tss_index << 3);

  Sys::normal = true;
  restore(false);

  KPRINT("bootstrapped core ?\n", Dec(PerCore::id()));

  impl::start_barrier->sync();

  if (info->lapic_id == 0) {
    /* run kernel_main in the first real thread */
    Thread::create(kernel_main);
  }

  IDT::init_core();
  Pit::init_core();
  VMM::init_core();

  event_loop();
}
