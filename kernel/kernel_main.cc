#include "elf.h"
#include "ext2.h"
#include "print.h"
#include "ramdisk.h"

void kernel_main() {
  StrongRef<BlockIO> ide{new RamDisk("/boot/ramdisk", 0)};
  auto fs = StrongRef<Ext2>::make(ide);

  KPRINT("block size is ?\n", Dec(fs->get_block_size()));
  KPRINT("inode size is ?\n", Dec(fs->get_inode_size()));

  auto init = fs->find(fs->root, "init");

  ELF::exec(init);
}
