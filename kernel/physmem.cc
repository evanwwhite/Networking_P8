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

#include "physmem.h"

#include "heap.h"
#include "limine++.h"
#include "limine.h"
#include "print.h"
#include "vmm.h"
#include <cstdint>

using namespace limine;

namespace impl {

struct Range {
  Range *next;
  uint64_t base;
  uint64_t length;

  Range(uint64_t base, uint64_t length)
      : next(nullptr), base(base), length(length) {}
};

[[gnu::section(
    ".limine_requests")]] static constexpr volatile MemMap limine_memmap{};

static bool pa_in_usable_range(uint64_t pa) {
  if (limine_memmap.response == nullptr) {
    return false;
  }

  for (uint64_t i = 0; i < limine_memmap.response->entry_count; i++) {
    auto const *entry = limine_memmap.response->entries[i];
    if (entry->type != LIMINE_MEMMAP_USABLE) {
      continue;
    }
    uint64_t start = entry->base;
    uint64_t len = entry->length;
    if (len < FRAME_SIZE) {
      continue;
    }
    uint64_t end = start + len;
    if (end < start) {
      continue;
    }
    if (pa >= start && pa <= (end - FRAME_SIZE)) {
      return true;
    }
  }
  return false;
}

constexpr uint64_t MAX_TRACKED_PPN = (256ULL * 1024 * 1024) / FRAME_SIZE;
static uint8_t frame_state[MAX_TRACKED_PPN]{};
// frame_state values:
//   0 = unknown/not tracked as allocated
//   1 = allocated (in use)
//   2 = on freelist

static bool ppn_is_tracked(uint64_t ppn) { return ppn < MAX_TRACKED_PPN; }

#if 0
static char const *memmap_type_names[] = {"Usable",
                                          "Reserved",
                                          "AcpiReclaimable",
                                          "AcpiNvs",
                                          "BadMemory",
                                          "BootloaderReclaimable",
                                          "ExecutableAndModules",
                                          "Framebuffer",
                                          "AcpiTables"};
#endif

} // namespace impl

void do_print(const PPN &ppn) {
  do_print("PPN(");
  do_print(ppn.ppn());
  do_print(")");
}

void do_print(const PA &pa) {
  do_print("PA(");
  do_print(pa.pa());
  do_print(")");
}

void do_print(const Offset &offset) {
  do_print("Offset(");
  do_print(offset.offset());
  do_print(")");
}

PhysMem::PhysMem() {
  using namespace impl;

  free_ranges = nullptr;

  if (limine_memmap.response) {
    for (uint64_t i = 0; i < limine_memmap.response->entry_count; i++) {
      auto const *entry = limine_memmap.response->entries[i];

      if (entry->type == LIMINE_MEMMAP_USABLE) {
        auto range = leak(new Range(entry->base, entry->length), true);
        range->next = free_ranges;
        free_ranges = range;
        total_memory += range->length;
      }
    }
  }

  KPRINT("avaialble memory: ?\n", Dec(total_memory));
  auto p = free_ranges;
  while (p) {
    KPRINT("    0x? ?\n", p->base, Dec(p->length / FRAME_SIZE));
    p = p->next;
  }
}

PPN PhysMem::alloc() {
  lock.lock();

  auto frame = avail;

  if (frame != nullptr) {
    avail = frame->next;
    uint64_t ppn = frame->ppn().ppn();
    if (impl::ppn_is_tracked(ppn)) {
      impl::frame_state[ppn] = 1;
    }
    lock.unlock();
    return frame->ppn();
  }

  if (free_ranges == nullptr) {
    lock.unlock();
    KPANIC("?\n", "out of frames");
  }

  PA pa{free_ranges->base};
  uint64_t ppn = pa.pa() >> LOG_FRAME_SIZE;
  if (impl::ppn_is_tracked(ppn)) {
    impl::frame_state[ppn] = 1;
  }

  free_ranges->base += FRAME_SIZE;
  free_ranges->length -= FRAME_SIZE;
  if (free_ranges->length == 0) {
    free_ranges = free_ranges->next;
  }

  lock.unlock();

  uint64_t *const ptr = VA(pa);

  for (uint64_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
    ptr[i] = 0;
  }
  return PPN{pa};
}

void PhysMem::free(PPN ppn) {
  if (ppn.ppn() == 0) {
    return;
  }

  uint64_t raw_ppn = ppn.ppn();
  uint64_t pa = PA(ppn).pa();
  if (!impl::pa_in_usable_range(pa)) {
    return;
  }

  // SAY("freeing ?\n", ppn);
  impl::AvailFrame *frame = VA(PA(pa));
  lock.lock();
  if (impl::ppn_is_tracked(raw_ppn)) {
    if (impl::frame_state[raw_ppn] != 1) {
      lock.unlock();
      return;
    }
    impl::frame_state[raw_ppn] = 2;
  }
  frame->next = avail;
  avail = frame;
  lock.unlock();
}

PhysMem physMem{};
