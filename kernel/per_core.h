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

#pragma once

#include "limine.h"
#include "machine.h"

namespace impl {
class TCB;

class [[gnu::packed]] TSS {
public:
  uint32_t _0 = 0;
  uint64_t rsp0 = 0;
  uint64_t rsp1 = 0;
  uint64_t rsp2 = 0;
  uint64_t _1 = 0;
  uint64_t ist[7] = {0};
  uint64_t _2 = 0;
  uint16_t _3 = 0;
  uint16_t iopb_offset = 0;
};

struct [[gnu::packed]] TSSDescriptor {
  uint16_t limit_15_0;
  uint16_t base_15_0;
  uint8_t base_23_16;
  uint8_t type : 4;
  uint8_t _1 : 1;
  uint8_t dpl : 2;
  uint8_t P : 1;
  uint8_t limit_19_16 : 4;
  uint8_t avl : 1;
  uint8_t _2 : 2;
  uint8_t G : 1;
  uint8_t base_31_24;
  uint32_t base_63_32;
  uint32_t _3 = 0;

  void init(TSS *tss, uint64_t limit) {
    uint64_t base = (uint64_t)tss;
    limit_15_0 = limit & 0xFFFF;
    limit_19_16 = (limit >> 16) & 0xF;
    base_15_0 = base & 0xFFFF;
    base_23_16 = (base >> 16) & 0xFF;
    base_31_24 = (base >> 24) & 0xFF;
    base_63_32 = (base >> 32) & 0xFFFFFFFF;
    type = 0x9;
    _1 = 0;
    dpl = 0;
    P = 1;
    avl = 0;
    _2 = 0;
    G = 1;
    _3 = 0;
  }
};

static_assert(sizeof(TSSDescriptor) == 16);

static_assert(sizeof(TSS) == 104);

}; // namespace impl

class PerCore {
public:
  static uint64_t core_count;

  // needs to be at offset 0
  const uint64_t lapic_id;
  // needs to be at offset 8
  impl::TCB *const idle_thread;
  // needs to be at offset 16
  uint64_t saved_fsbase;
  // needs to be at offset 24
  uint64_t saved_rsp;

  impl::TCB *current_thread = nullptr;

  impl::TSS tss{};

  PerCore(uint64_t lapic_id, impl::TCB *idle_thread)
      : lapic_id(lapic_id), idle_thread(idle_thread) {}

  // Challenges:
  //      (1) find the race risk and fix it
  //      (2) do it without disabling interrupts, remember that a SpinLock
  //          disables interrupts

  // TODO: preemption related race condition
  static inline uint64_t id() { return PerCore::get()->lapic_id; }
  // TODO: preemption related race risk
  static inline PerCore *get() { return (PerCore *)(rdgsbase()); }
};