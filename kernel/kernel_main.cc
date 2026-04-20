#include "elf.h"
#include "ext2.h"
#include "print.h"
#include "ramdisk.h"
#include "virtio_net.h"
#include "virtio_net_tests.h"

void kernel_main() {
  net_init_fake();

  StrongRef<BlockIO> ide{new RamDisk("/boot/ramdisk", 0)};
  auto fs = StrongRef<Ext2>::make(ide);

  KPRINT("block size is ?\n", Dec(fs->get_block_size()));
  KPRINT("inode size is ?\n", Dec(fs->get_inode_size()));

  net_run_selected_tests(fs);

  auto init = fs->find(fs->root, "init");

  ELF::exec(init);
}
