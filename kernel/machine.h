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

#include <cstdint>

namespace impl {
class TCB;
}

extern "C" std::uint64_t get_cr0();

extern "C" std::uint64_t get_cr2();

static inline std::uint64_t get_cr3() {
  uint64_t cr3;
  asm volatile("mov %%cr3, %0" : "=r"(cr3));
  return cr3;
}

static inline void set_cr3(uint64_t cr3) {
  asm volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

extern "C" std::uint64_t get_cr4();
extern "C" std::uint64_t set_cr4(std::uint64_t val);

extern "C" std::uint8_t inb(std::uint16_t port);
extern "C" std::uint32_t inl(std::uint16_t port);
extern "C" void outb(std::uint16_t port, std::uint8_t val);
extern "C" void outl(std::uint16_t port, std::uint32_t val);

extern "C" void enable_sse();

extern "C" std::uintptr_t rdfsbase();
extern "C" void wrfsbase(std::uintptr_t val);
extern "C" std::uintptr_t rdgsbase();
extern "C" void wrgsbase(std::uintptr_t val);

extern "C" void pause();

static inline std::uint64_t rdmsr(std::uint32_t msr) {
  std::uint32_t a = 0;
  std::uint32_t d = 0;
  __asm__ volatile("rdmsr" : [a] "=a"(a), [d] "=d"(d) : [c] "c"(msr));

  return (std::uint64_t(d) << 32) | a;
}

static inline void wrmsr(std::uint32_t msr, std::uint64_t val) {
  std::uint32_t eax = val;
  std::uint32_t edx = val >> 32;
  __asm__ volatile("wrmsr" : : "c"(msr), "a"(eax), "d"(edx));
}

static inline void cpuid(std::uint32_t eax, std::uint32_t ecx,
                         std::uint32_t *ap, std::uint32_t *bp,
                         std::uint32_t *cp, std::uint32_t *dp) {
  __asm__ volatile("cpuid"
                   : [a] "=a"(*ap), [b] "=b"(*bp), [c] "=c"(*cp), [d] "=d"(*dp)
                   : [e] "0"(eax), [f] "2"(ecx));
}

static inline void mem_acquire() {
  asm volatile("lfence" ::: "memory"); // Is this really needed?
}

static inline void mem_release() {
  asm volatile("sfence" ::: "memory"); // Is this really needed?
}

extern "C" uint64_t get_flags();

static inline bool is_disabled() { return (get_flags() & 0x200) == 0; }

static inline bool disable() {
  auto was = is_disabled();
  asm volatile("cli" ::: "memory");
  return was;
}

static inline void restore(bool was_disabled) {
  if (!was_disabled) {
    asm volatile("sti" ::: "memory");
  }
}

static inline void sti() { asm volatile("sti" ::: "memory"); }

extern "C" impl::TCB *context_switch_(void *next);
extern "C" void bounce_to_yield_();

extern "C" void pageFaultHandler_();
extern "C" void apitHandler_();
extern "C" void spuriousHandler_();
extern "C" void syscallHandler_();

static inline uint64_t rdtsc() {
  uint32_t eax, edx;
  __asm__ volatile("rdtsc" : "=a"(eax), "=d"(edx));
  return ((uint64_t)edx << 32) | eax;
}

struct [[gnu::packed]] GDTR {
  uint16_t limit;
  uint64_t base;
};

static inline void lgdt(const GDTR &gdt) {
  asm volatile("lgdt %0" : : "m"(gdt));
}
static inline GDTR sgdt() {
  GDTR gdt;
  asm volatile("sgdt %0" : : "m"(gdt));
  return gdt;
}

static inline void ltr(uint16_t index) {
  asm volatile("ltr %0" : : "r"(index));
}

extern "C" size_t strlen(const char *);
extern "C" bool streq(const char *a, const char *b);
extern "C" void memcpy(void *dest, void *src, size_t n);

extern "C" [[noreturn]] void switch_to_user(uint64_t entry, uint64_t sp);
