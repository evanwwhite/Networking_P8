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

#include "system_main.h"

#include "core_main.h"
#include "debug.h"
#include "heap.h"
#include "idt.h"
#include "limine++.h"
#include "pit.h"
#include "print.h"
#include "vmm.h"
#include "x86_64.h"
#include <cstdint>

using namespace limine;

extern void (*_init_array_start)();
extern void (*_init_array_end)();

char the_heap[6 * 1024 * 1024];

[[gnu::section(
    ".limine_requests")]] constexpr volatile BaseRevision base_revision{};
[[gnu::section(".limine_requests")]] constexpr volatile MultiProcessor mp{};
[[gnu::section(".limine_requests")]] constexpr volatile HHDM limine_hhdm{};

uint64_t Sys::core_count = 0;
volatile bool Sys::bootstraping = true;
uint64_t Sys::start_tsc = 0;
bool Sys::normal = false;
uint64_t Sys::hhdm_offset = 0;
GDTR Sys::gdtr{};
uint64_t Sys::base_tss_index = 0;

extern "C" [[noreturn]] void system_main(void) {

  using namespace Sys;

  // Make sure we were loaded by a limine-compliant bootloader that supports the
  // base revision we require
  ASSERT(base_revision.supported());

  ///////////////////////////////////////////////////////////
  // Check supported CPU features and enable required ones //
  ///////////////////////////////////////////////////////////

  Features features{};
  features.dump();
  ASSERT(features.hasSSE3);
  enable_sse();
  ASSERT(features.hasFSGSBASE);
  CR4().setFSGSBASE();
  CR0().dump();
  CR4().dump();

  start_tsc = rdtsc();

  Sys::hhdm_offset = limine_hhdm.response->offset;

  ///////////////////////////
  // Initialize Global IDT //
  ///////////////////////////

  IDT::init_system();

  /////////////////
  // Frequencies //
  /////////////////

  Pit::init_system(1000);

  ////////////////////
  // Initialize VMM //
  ////////////////////

  VMM::init_system();

  ///////////////////////////
  // Initialize core count //
  ///////////////////////////

  ASSERT(mp.response);

  core_count = mp.response->cpu_count;
  // Need x2apic support to be available and enabled. This allows access
  // to the local APIC registers using MSRs instead of MMIO.
  ASSERT(mp.response->flags & LIMINE_MP_RESPONSE_X86_64_X2APIC);

  ////////////////////////////////
  // Initialize the kernel heap //
  ////////////////////////////////

  heap::init(uintptr_t(the_heap), sizeof(the_heap));

  ///////////////////////////////////////////////////////////////////////////
  // Run global initializers -- has to be done after initializing the heap //
  ///////////////////////////////////////////////////////////////////////////

  KPRINT("_init_array_start:? _init_array_end:?\n", &_init_array_start,
         &_init_array_end);

  std::size_t count = &_init_array_end - &_init_array_start;
  for (std::size_t i = 0; i < count; i++) {
    (*((&_init_array_start)[i]))();
  }

  /////////
  // GDT //
  /////////

  /* Limine's GDT is not big enough, need to extend it in order
     to add per-core TSS descriptors. */

  gdtr = sgdt();
  KPRINT("Limine GDT limit: ? base: 0x?\n", Dec(gdtr.limit), gdtr.base);

  base_tss_index = (gdtr.limit + 1) / sizeof(uint64_t);
  uint64_t new_gdt_entries = base_tss_index + 2 * core_count;
  uint64_t *new_gdt = leak(new uint64_t[new_gdt_entries], true);
  uint64_t *old_gdt = (uint64_t *)gdtr.base;

  // old_gdt is setup by limine
  // https://codeberg.org/Limine/limine-protocol/src/branch/trunk/PROTOCOL.md

  new_gdt[0] = old_gdt[0];
  new_gdt[1] = old_gdt[5] | UINT64_C(0x0000'6000'0000'0000); // DPL=3
  new_gdt[2] = old_gdt[6] | UINT64_C(0x0000'6000'0000'0000); // DPL=3
  new_gdt[3] = old_gdt[5] | UINT64_C(0x0000'6000'0000'0000); // DPL=3
  new_gdt[4] = old_gdt[6] | UINT64_C(0x0000'6000'0000'0000); // DPL=3
  new_gdt[5] = old_gdt[5];
  new_gdt[6] = old_gdt[6];

  gdtr.limit = new_gdt_entries * sizeof(uint64_t) - 1;
  gdtr.base = (uint64_t)new_gdt;

  /////////////////////////
  // Wake up other cores //
  /////////////////////////

  KPRINT("has ? cores, flags=?, bsp_lapic_id=?\n", Dec(core_count),
         mp.response->flags, mp.response->bsp_lapic_id);

  impl::start_barrier = leak(new Barrier(core_count), true);

  for (uint64_t i = 0; i < core_count; i++) {
    auto info = mp.response->cpus[i];
    if (mp.response->bsp_lapic_id != info->lapic_id) {
      info->extra_argument = 0;
      info->goto_address = impl::core_main;
    }
  }

  impl::core_main(mp.response->cpus[mp.response->bsp_lapic_id]);
}
