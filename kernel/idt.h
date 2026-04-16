#pragma once

#include <cstdint>

class IDT {
public:
  static void init_system(void);
  static void init_core(void);
  static void interrupt(std::uint32_t index, std::uintptr_t handler);
  static void trap(std::uint32_t index, std::uintptr_t handler,
                   std::uint32_t dpl);
};
