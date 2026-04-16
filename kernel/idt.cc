#include "idt.h"
#include "machine.h"
#include "system_main.h"
#include "x86_64.h"
#include <cstdint>

namespace impl {

// Limine GDT settings
// https://codeberg.org/Limine/limine-protocol/src/branch/trunk/PROTOCOL.md
// constexpr uint16_t KERNEL_CS = 5 << 3;
// constexpr uint16_t dataCS = 6 << 3;

constexpr uint8_t spurious_index = 50;

class [[gnu::packed]] IdtEntry {
public:
  uint32_t data[4];

  void set(uint64_t offset, uint8_t type, uint8_t dpl) {
    data[0] = (uint32_t(Sys::KERNEL_CS) << 16) | uint16_t(offset);
    data[1] = uint32_t(uint16_t(offset >> 16)) << 16 | 0x8000 | (dpl << 13) |
              (type << 8);
    data[2] = uint32_t(offset >> 32);
    data[3] = 0;
  }
};

struct [[gnu::packed]] Idtr {
  uint16_t size;
  uint64_t base;
};

static_assert(sizeof(Idtr) == 10);

static_assert(sizeof(IdtEntry) == 16);
static_assert(sizeof(uintptr_t) == 8);

IdtEntry idt[256];
Idtr idtr{sizeof(idt) - 1, (uintptr_t)idt};
} // namespace impl

void IDT::init_system() {
  IDT::interrupt(Msr::SpuriousInterruptVector().read() & 0xff,
                 (uintptr_t)spuriousHandler_);
}
void IDT::init_core(void) {
  Msr::SpuriousInterruptVector().write((1 << 8) | impl::spurious_index);
  // KPRINT("suporious interrupt vector = ?\n",
  //        Msr::SpuriousInterruptVector().read());
  asm volatile("lidt %0" : : "m"(impl::idtr));
}

void IDT::interrupt(uint32_t index, uintptr_t handler) {
  using namespace impl;

  idt[index].set(handler, 0xe, 0);
}

void IDT::trap(uint32_t index, uintptr_t handler, uint32_t dpl) {
  using namespace impl;

  idt[index].set(handler, 0xf, dpl);
}
