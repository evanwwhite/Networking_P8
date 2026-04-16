#include "ext2.h"
#include "print.h"

Ext2::Ext2(StrongRef<BlockIO> device) : device(device), root() {

  SuperBlock sb;

  device->read(1024, sb);
  iNodeSize = sb.inode_size;
  iNodesPerGroup = sb.inodes_per_group;
  numberOfNodes = sb.inodes_count;
  numberOfBlocks = sb.blocks_count;

  // Debug::printf("inodes_count %d\n",sb.inodes_count);
  // Debug::printf("blocks_count %d\n",sb.blocks_count);
  // Debug::printf("blocks_per_group %d\n",sb.blocks_per_group);
  // Debug::printf("inode_size %d\n",sb.inode_size);
  // Debug::printf("block_group_nr %d\n",sb.block_group_nr);
  // Debug::printf("first_inode %d\n",sb.first_inode);

  blockSize = uint32_t(1) << (sb.log_block_size + 10);

  nGroups = (sb.blocks_count + sb.blocks_per_group - 1) / sb.blocks_per_group;
  // Debug::printf("nGroups = %d\n",nGroups);
  ASSERT(nGroups * sb.blocks_per_group >= sb.blocks_count);

  auto superBlockNumber = 1024 / blockSize;
  // Debug::printf("super block number %d\n",superBlockNumber);

  auto groupTableNumber = superBlockNumber + 1;
  // Debug::printf("group table number %d\n",groupTableNumber);

  auto groupTableSize = sizeof(BlockGroup) * nGroups;
  // Debug::printf("group table size %d\n",groupTableSize);

  groupTable = new BlockGroup[nGroups];
  auto cnt = device->read_all(groupTableNumber * blockSize, groupTableSize,
                              (char *)groupTable);
  ASSERT(uint64_t(cnt) == groupTableSize);

  iNodeTables = new uint32_t[nGroups];

  for (uint32_t i = 0; i < nGroups; i++) {
    auto g = &groupTable[i];

    iNodeTables[i] = g->inode_table;

    // Debug::printf("group #%d\n",i);
    // Debug::printf("    block_bitmap %d\n",g->block_bitmap);
    // Debug::printf("    inode_bitmap %d\n",g->inode_bitmap);
    // Debug::printf("    inode_table %d\n",g->inode_table);
  }

  // Debug::printf("========\n");
  // Debug::printf("iNodeSize %d\n",iNodeSize);
  // Debug::printf("nGroups %d\n",nGroups);
  // Debug::printf("iNodesPerGroup %d\n",iNodesPerGroup);
  // Debug::printf("numberOfNodes %d\n",numberOfNodes);
  // Debug::printf("numberOfBlocks %d\n",numberOfBlocks);
  // Debug::printf("blockSize %d\n",blockSize);
  // for (unsigned i = 0; i < nGroups; i++) {
  //     Debug::printf("iNodeTable[%d] %d\n",i,iNodeTables[i]);
  // }

  // root = new Node(ide,2,blockSize);

  root = get_node(2);

  // root->show("root");

  // root->entries([](uint32_t inode, char* name) {
  //     Debug::printf("%d %s\n",inode,name);
  // });

  // println(sb.uuid);
  // println(sb.volume_name);
}

Ext2::~Ext2() {
  delete[] iNodeTables;
  delete[] groupTable;
}

StrongRef<Node> Ext2::get_node(uint32_t number) {
  ASSERT(number > 0);
  ASSERT(number <= numberOfNodes);
  auto index = number - 1;

  auto groupIndex = index / iNodesPerGroup;
  // Debug::printf("groupIndex %d\n",groupIndex);
  ASSERT(groupIndex < nGroups);
  auto indexInGroup = index % iNodesPerGroup;
  auto iTableBase = iNodeTables[groupIndex];
  ASSERT(iTableBase <= numberOfBlocks);
  // Debug::printf("iTableBase %d\n",iTableBase);
  auto nodeOffset = iTableBase * blockSize + indexInGroup * iNodeSize;
  // Debug::printf("nodeOffset %d\n",nodeOffset);

  auto out = StrongRef<Node>::make(number, blockSize, device);
  device->read(nodeOffset, out->data);
  return out;
}

////////////// NodeData //////////////

void NodeData::show(const char *what) {
  KPRINT("?\n", what);
  KPRINT("    mode 0x?\n", mode);
  KPRINT("    uid ?\n", Dec(uid));
  KPRINT("    gif ?\n", Dec(gid));
  KPRINT("    n_links ?\n", Dec(n_links));
  KPRINT("    n_sectors ?\n", Dec(n_sectors));
}

///////////// Node /////////////

void Node::get_symbol(char *buffer) {
  ASSERT(is_symlink());
  auto sz = size_in_bytes();
  if (sz <= 60) {
    memcpy(buffer, &data.direct0, sz);
  } else {
    auto cnt = read_all(0, sz, buffer);
    ASSERT(cnt == sz);
  }
}

void Node::read_block(uint32_t index, char *buffer) {
  ASSERT(index < data.n_sectors / (block_size / 512));

  auto refs_per_block = block_size / 4;

  uint32_t block_index;

  if (index < 12) {
    uint32_t *direct = &data.direct0;
    block_index = direct[index];
  } else if (index < (12 + refs_per_block)) {
    device->read(data.indirect_1 * block_size + (index - 12) * 4, block_index);

  } else {
    block_index = 0;
    KPANIC("index = ?\n", index);
  }

  auto cnt = device->read_all(block_index * block_size, block_size, buffer);
  ASSERT(cnt == block_size);
}

uint32_t Node::find(const char *name) {
  uint32_t out = 0;

  entries([&out, name](uint32_t number, const char *nm) {
    if (streq(name, nm)) {
      out = number;
    }
  });

  return out;
}

uint32_t Node::entry_count() {
  ASSERT(is_dir());
  uint32_t count = 0;
  entries([&count](uint32_t, const char *) { count += 1; });
  return count;
}
