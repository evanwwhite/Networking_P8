#include "elf.h"
#include "debug.h"
#include "machine.h"
#include "per_core.h"
#include "shared.h"
#include "thread.h"
#include "vmm.h"

namespace {

constexpr uint64_t PAGE_SIZE = FRAME_SIZE;
constexpr uint64_t USER_STACK_TOP = UINT64_C(0x7ffffff0000);
constexpr uint64_t USER_STACK_SIZE = 64 * 1024;
constexpr uint64_t AT_NULL = 0;
constexpr uint64_t AT_PHDR = 3;
constexpr uint64_t AT_PHENT = 4;
constexpr uint64_t AT_PHNUM = 5;
constexpr uint64_t AT_PAGESZ = 6;
constexpr uint64_t AT_BASE = 7;
constexpr uint64_t AT_ENTRY = 9;
constexpr uint64_t AT_UID = 11;
constexpr uint64_t AT_EUID = 12;
constexpr uint64_t AT_GID = 13;
constexpr uint64_t AT_EGID = 14;
constexpr uint64_t AT_SECURE = 23;
constexpr uint64_t AT_RANDOM = 25;

inline uint64_t align_down(uint64_t value) { return value & ~(PAGE_SIZE - 1); }

inline uint64_t align_up(uint64_t value) {
  return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

void zero_user_range(uint64_t start, uint64_t length) {
  auto *bytes = reinterpret_cast<unsigned char *>(start);
  for (uint64_t i = 0; i < length; i++) {
    bytes[i] = 0;
  }
}

void map_user_range(uint64_t start, uint64_t length) {
  if (length == 0) {
    return;
  }

  uint64_t map_start = align_down(start);
  uint64_t map_end = align_up(start + length);
  impl::map_range(VA(map_start), map_end - map_start, true, true);
}

void copy_to_user(uint64_t dest, const void *src, uint64_t length) {
  auto *out = reinterpret_cast<unsigned char *>(dest);
  auto *in = reinterpret_cast<const unsigned char *>(src);
  for (uint64_t i = 0; i < length; i++) {
    out[i] = in[i];
  }
}

uint64_t find_program_headers_va(StrongRef<Node> file, const ElfHeader &hdr) {
  uint64_t hoff = hdr.phoff;
  for (uint16_t i = 0; i < hdr.phnum; i++) {
    ProgramHeader phdr{};
    file->read(hoff, phdr);
    hoff += hdr.phentsize;
    if (phdr.type != 1) {
      continue;
    }
    if (hdr.phoff >= phdr.offset && hdr.phoff < phdr.offset + phdr.filesz) {
      return phdr.vaddr + (hdr.phoff - phdr.offset);
    }
  }
  return 0;
}

uint64_t build_initial_stack(StrongRef<Node> file, const ElfHeader &hdr,
                             uint64_t entry) {
  struct AuxPair {
    uint64_t tag;
    uint64_t value;
  };

  uint64_t cursor = USER_STACK_TOP;

  const char arg0[] = "init";
  cursor -= sizeof(arg0);
  uint64_t arg0_addr = cursor;
  copy_to_user(arg0_addr, arg0, sizeof(arg0));

  cursor -= 16;
  uint64_t random_addr = cursor;
  zero_user_range(random_addr, 16);

  cursor &= ~UINT64_C(0xf);

  AuxPair auxv[] = {
      {AT_PHDR, find_program_headers_va(file, hdr)},
      {AT_PHENT, hdr.phentsize},
      {AT_PHNUM, hdr.phnum},
      {AT_PAGESZ, PAGE_SIZE},
      {AT_BASE, 0},
      {AT_ENTRY, entry},
      {AT_UID, 0},
      {AT_EUID, 0},
      {AT_GID, 0},
      {AT_EGID, 0},
      {AT_SECURE, 0},
      {AT_RANDOM, random_addr},
      {AT_NULL, 0},
  };

  auto push_u64 = [&](uint64_t value) {
    cursor -= sizeof(uint64_t);
    *reinterpret_cast<uint64_t *>(cursor) = value;
  };

  for (int i = int(sizeof(auxv) / sizeof(auxv[0])) - 1; i >= 0; i--) {
    push_u64(auxv[i].value);
    push_u64(auxv[i].tag);
  }

  push_u64(0);         // envp[0]
  push_u64(0);         // argv[1]
  push_u64(arg0_addr); // argv[0]
  push_u64(1);         // argc

  return cursor;
}

} // namespace

uint64_t ELF::load(StrongRef<Node> file) {
  ElfHeader hdr;

  auto me = impl::TCB::current();

  file->read(0, hdr);

  uint32_t hoff = hdr.phoff;

  KPRINT("phnum = ?\n", hdr.phnum);

  for (uint32_t i = 0; i < hdr.phnum; i++) {
    ProgramHeader phdr;
    file->read(hoff, phdr);
    hoff += hdr.phentsize;

    if (phdr.type == 1) {
      char *p = (char *)phdr.vaddr;
      uint64_t memsz = phdr.memsz;
      uint64_t filesz = phdr.filesz;

      KPRINT("vaddr:? memsz:? filesz:? fileoff:?\n", uint64_t(p), memsz, filesz,
             phdr.offset);
      ASSERT(memsz >= filesz);
      map_user_range(uint64_t(p), memsz);
      zero_user_range(uint64_t(p), memsz);
      if (filesz != 0) {
        auto n = file->read_all(uint32_t(phdr.offset), uint32_t(filesz), p);
        ASSERT(n == int64_t(filesz));
      }

      auto end = uint64_t(p) + memsz;
      if (end > me->brk) {
        me->brk = uint64_t(end);
      }
    }
  }

  me->min_brk = me->brk;

  uint64_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
  map_user_range(stack_base, USER_STACK_SIZE);
  zero_user_range(stack_base, USER_STACK_SIZE);

  return hdr.entry;
}

void ELF::exec(StrongRef<Node> file) {
  ElfHeader hdr{};
  file->read(0, hdr);

  // load the file and get the entry point
  const auto entry = load(file);

  // Build the Linux-style process entry stack expected by the user runtime.
  const auto rsp = build_initial_stack(file, hdr, entry);

  // switch to user mode
  switch_to_user(entry, rsp);
}
