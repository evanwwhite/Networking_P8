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
extern uint64_t common_cr3;
} // namespace impl

namespace VMM {
enum flags { MAP_SHARED, MAP_ANONYMOUS };

extern void init_system();
extern void init_core();
extern void init_thread();
extern void fini_thread();

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
