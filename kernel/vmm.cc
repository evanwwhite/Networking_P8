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

#include "vmm.h"

#include "ext2.h"
#include "idt.h"
#include "machine.h"
#include "physmem.h"
#include "print.h"
#include "shared.h"
#include "system_main.h"
#include <cstdint>

uint64_t impl::common_cr3 = 0;

uint64_t VA::check_canonical(uint64_t va) {
  int64_t sa = int64_t(va);
  ASSERT(((sa << 16) >> 16) == sa);
  return va;
}

namespace impl {

// This helper function should insert the requested mapping into this thread's
// page table structure. It may be helpful to make it recursive, using the
// level variable (though it is not required!)
void map(const PPN table_ppn, uint32_t level, VPN vpn, PPN ppn, bool user,
         bool write) {

  // TODO: handle XD
  uint64_t index = (vpn.vpn() >> (9 * level)) & 0x1FF;
  auto table_pa = PA(table_ppn);
  uint64_t *table = VA(table_pa);

  auto flags = 1 | (write ? 2 : 0) | (user ? 4 : 0);

  if (level == 0) {
    table[index] = PA(ppn).pa() | flags;
    return;
  }
  auto entry = table[index];
  PPN next_ppn{entry >> 12}; // TODO: beware XD

  if (entry & 1) {
    table[index] |= flags;
  } else {
    next_ppn = physMem.alloc();
    table[index] = PA(next_ppn).pa() | flags;
  }

  map(next_ppn, level - 1, vpn, ppn, user, write);
}

void map(VPN vpn, PPN ppn, bool user, bool write) {
  map(PA(get_cr3()), 3, vpn, ppn, user, write);
}

struct PageFaultTrapFrame {
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
  std::uint64_t error_code;
  std::uint64_t rip;
  std::uint64_t cs;
  std::uint64_t rflags;
  std::uint64_t rsp;
  std::uint64_t ss;
};

struct VME {
  uint64_t start; // page aligned
  uint64_t end;   // page alinged
  StrongRef<Node> node;
  uint64_t offset; // page aligned
  VME *next = nullptr;
};

thread_local VME *vmes = nullptr;

} // namespace impl

/*
 * A simplified mmap implementation
 */

void *VMM::mmap(void *addr, size_t length, int prot, int flags,
                StrongRef<Node> file, uint64_t offset) {
  uint64_t candidate = 0x1000;
  using namespace impl;
  auto next = vmes;
  VME **pprev = &vmes;
  while (next != nullptr) {
    if (candidate + length <= next->start) {
      break;
    }
    candidate = next->end;
    pprev = &next->next;
    next = next->next;
  }

  auto vme = new VME();
  vme->start = candidate;
  vme->end = candidate + length;
  vme->node = file;
  vme->offset = offset;
  vme->next = next;
  *pprev = vme;
  return (void *)candidate;
}

int VMM::munmap(void *addr, size_t length) {
  // Do the unmap of addr
  // return 0 if successful, -1 otherwise
  return -1;
}

extern "C" [[gnu::force_align_arg_pointer]] void
pageFaultHandler(uintptr_t cr2, impl::PageFaultTrapFrame *trap_frame) {
  auto frame = physMem.alloc();
  if (cr2 >= UINT64_C(0x8000'0000'0000'0000)) {
    KPANIC("va = ?\n", cr2);
  }
  impl::map(VPN(VA(cr2)), frame, true, true);
}

void VMM::init_system() {
  impl::common_cr3 = get_cr3();
  IDT::trap(14, uintptr_t(pageFaultHandler_), 3);
}

void VMM::init_core() {}

void VMM::init_thread() {
  PPN new_cr3_ppn = physMem.alloc();
  PA new_cr3_pa = new_cr3_ppn;

  uint64_t *new_table = VA(new_cr3_ppn);
  uint64_t *old_table = VA(PPN(impl::common_cr3 >> 12));

  for (uint64_t i = 512 / 2; i < 512; i++) {
    new_table[i] = old_table[i];
  }

  set_cr3(new_cr3_pa.pa());
}

void VMM::fini_thread() {
  using namespace impl;
  auto p = vmes;
  vmes = nullptr;
  while (p != nullptr) {
    auto next = p->next;
    delete p;
    p = next;
  }
}
