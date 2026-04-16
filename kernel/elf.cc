#include "elf.h"
#include "debug.h"
#include "per_core.h"
#include "shared.h"
#include "thread.h"

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
      uint32_t memsz = phdr.memsz;
      uint32_t filesz = phdr.filesz;

      KPRINT("vaddr:? memsz:? filesz:? fileoff:?\n", uint64_t(p), memsz, filesz,
             phdr.offset);
      uint32_t n = file->read_all(phdr.offset, filesz, p);
      ASSERT(n == filesz);

      auto end = uint64_t(p) + memsz;
      if (end > me->brk) {
        me->brk = uint64_t(end);
      }
    }
  }

  me->min_brk = me->brk;

  return hdr.entry;
}

void ELF::exec(StrongRef<Node> file) {

  // load the file and get the entry point
  const auto entry = load(file);

  // pick a location for the user stack
  const auto rsp = UINT64_C(0x7fff'ffff'f000);

  // switch to user mode
  switch_to_user(entry, rsp);
}
