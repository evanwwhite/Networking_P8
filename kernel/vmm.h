#pragma once

#include "ext2.h"
#include "physmem.h"
#include "shared.h"
#include <cstdint>

class VA;

class VPN {
private:
  static constexpr uint64_t MASK = UINT64_C(0xFFFFFFFFF);
  uint64_t _val;

public:
  explicit inline VPN(const VA &va);
  inline VPN(const VPN &rhs) : _val(rhs._val) {}
  inline uint64_t vpn() const { return _val; }
};

extern void do_print(const VPN &);

class VA {
private:
  uint64_t _va;
  uint64_t check_canonical(uint64_t va);

public:
  explicit inline VA(uint64_t va) : _va(check_canonical(va)) {}
  explicit inline VA(const VPN &vpn)
      : _va(check_canonical(vpn.vpn() << LOG_FRAME_SIZE)) {}
  inline VA(const VA &rhs) : _va(rhs._va) {}
  explicit inline VA(const PA &pa)
      : _va(check_canonical(pa.pa() + Sys::hhdm_offset)) {}
  explicit inline VA(const PPN &ppn) : VA(PA(ppn)) {}
  inline uint64_t va() const { return _va; }

  template <typename T> operator T *() const { return (T *)_va; }

  inline VPN vpn() const { return VPN(*this); }
};

extern void do_print(const VA &);

inline VPN::VPN(const VA &va) : _val(va.va() >> LOG_FRAME_SIZE) {}

namespace impl {
void map(VPN vpn, PPN ppn, bool user, bool write);
static inline void map_range(VA start, uint64_t length, bool user, bool write) {
  for (uint64_t i = 0; i < length; i += FRAME_SIZE) {
    map(VPN(VA(start.va() + i)), physMem.alloc(), user, write);
  }
}
extern uint64_t common_cr3;
} // namespace impl

namespace VMM {
enum flags { MAP_SHARED = 1, MAP_ANONYMOUS = 2 }; // UPDATED the flags

extern void init_system();
extern void init_core();
extern void init_thread();
extern void fini_thread();

bool user_buffer_ok(const void *addr, size_t length);
void fork_from(uint64_t source_cr3);

// Map a physical MMIO range into the kernel half of the address space and
// return the direct-map-style virtual address for the first byte.
uintptr_t map_mmio(uint64_t pa, uint64_t length);

uintptr_t silly_mmap(uint64_t length, StrongRef<Node> file, uint64_t offset);

/* A subset of Linux mmap, read the mmap spec for details */
void *
mmap(void *addr,    // starting address (ignored)
     size_t length, // length of the mapping in bytes
     int prot, // desired memory protection of the mapping (ignored, always RWX)
     int flags, // various flags about the mapping. All you need to handle is
                // MAP_SHARED and MAP_ANONYMOUS
     StrongRef<Node> file, // the file to map
                           // (file == nullptr) iff MAP_ANONYMOUS
     uint64_t offset       // offset in the file
);

/* A subset of Linux munmap, read the munmap spec for details on corner cases */
int munmap(void *addr,   // the address to unmap
           size_t length // the length of it that you want to unmap
);

} // namespace VMM
