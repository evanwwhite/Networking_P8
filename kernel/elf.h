#pragma once

#include "ext2.h"
#include "shared.h"
#include <stdint.h>

class ELF {
public:
  static uint64_t load(StrongRef<Node> file);
  static void exec(StrongRef<Node> file);
};

struct ElfHeader {
  unsigned char maigc0;   // should be 0x7f
  unsigned char magic1;   // should be E
  unsigned char magic2;   // should be L
  unsigned char magic3;   // should be F
  uint8_t cls;            // 1 -> 32 bit, 2 -> 64 bit
  uint8_t encoding;       // 1 -> LE, 2 -> BE
  uint8_t header_version; // 1
  uint8_t abi;            // 0 -> Unix System V, 1 -> HP-UX
  uint8_t abi_version;    //
  uint8_t padding[7];

  uint16_t type;      // 1 -> relocatable, 2 -> executable
  uint16_t machine;   // 3 -> intel i386
  uint32_t version;   // 1 -> current
  uint64_t entry;     // program entry point
  uint64_t phoff;     // offset in file for program headers
  uint64_t shoff;     // offset in file for section headers
  uint32_t flags;     //
  uint16_t ehsize;    // how many bytes in this header
  uint16_t phentsize; // bytes per program header entry
  uint16_t phnum;     // number of program header entries
  uint16_t shentsize;
  uint16_t shnum;
  uint16_t shstrndx;

} __attribute__((packed));

struct ProgramHeader {
  uint32_t type; /* 1 -> load, ... */
  uint32_t flags;
  uint64_t offset; /* data offset in the file */
  uint64_t vaddr;  /* Where should it be loaded in virtual memory */
  uint64_t paddr;  /* ignore */
  uint64_t filesz; /* how many bytes in the file */
  uint64_t memsz;  /* how many bytes in memory, result should be 0 */
  uint64_t align;  /* alignment */
} __attribute__((packed));
