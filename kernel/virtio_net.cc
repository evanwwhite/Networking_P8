#include "virtio_net.h"

#include "atomic.h"
#include "debug.h"
#include "machine.h"
#include "pcie.h"
#include "physmem.h"
#include "print.h"
#include "spin_lock.h"
#include "vmm.h"

namespace {

// enum for turning on or off backend stuff
enum class BackendKind : uint8_t {
  None,
  Fake,
  Virtio,
};

// struct data if open/ closed of one packet
struct FrameSlot {
  uint8_t data[VIRTIO_NET_MAX_FRAME_SIZE];
  uint16_t len;
  bool occupied;
};

// Shared driver state for TX/RX descriptors, rings, and slot bookkeeping.
struct NetState {
  SpinLock lock{};
  BackendKind backend = BackendKind::None;

  VirtioNetDescriptor tx_desc[VIRTIO_NET_QUEUE_SIZE]{};
  VirtioNetDescriptor rx_desc[VIRTIO_NET_QUEUE_SIZE]{};
  // avail rings are published to the device; used rings are completed by it.
  VirtioNetAvailRing tx_avail{};
  VirtioNetAvailRing rx_avail{};
  VirtioNetUsedRing tx_used{};
  VirtioNetUsedRing rx_used{};

  FrameSlot tx_slots[VIRTIO_NET_QUEUE_SIZE]{};
  FrameSlot rx_slots[VIRTIO_NET_QUEUE_SIZE]{};

  uint16_t tx_next_probe = 0;
  // Next completed TX entry the driver still needs to reclaim.
  uint16_t tx_used_consume = 0;
  // Number of TX packets currently in flight.
  uint16_t tx_pending = 0;

  uint16_t rx_next_probe = 0;
  // Next completed RX entry the driver still needs to hand to callers.
  uint16_t rx_used_consume = 0;
  // Number of RX packets ready for net_recv_raw().
  uint16_t rx_ready = 0;
};

// Virtio-net does not place a raw Ethernet frame directly in the queue.
// Every TX/RX buffer starts with this small virtio header; our public API hides
// it so callers still send and receive plain Ethernet frames.
struct [[gnu::packed]] VirtioNetHdr {
  uint8_t flags;
  uint8_t gso_type;
  uint16_t hdr_len;
  uint16_t gso_size;
  uint16_t csum_start;
  uint16_t csum_offset;
};

// Modern virtio PCI exposes this common register block through a vendor
// specific PCI capability. The driver uses it for status, feature negotiation,
// queue selection, and telling the device where the queue memory lives.
struct [[gnu::packed]] VirtioPciCommonCfg {
  uint32_t device_feature_select;
  uint32_t device_feature;
  uint32_t driver_feature_select;
  uint32_t driver_feature;
  uint16_t config_msix_vector;
  uint16_t num_queues;
  uint8_t device_status;
  uint8_t config_generation;
  uint16_t queue_select;
  uint16_t queue_size;
  uint16_t queue_msix_vector;
  uint16_t queue_enable;
  uint16_t queue_notify_off;
  uint64_t queue_desc;
  uint64_t queue_driver;
  uint64_t queue_device;
};

// One virtio PCI capability says "structure X is in BAR N at offset Y".
// We keep the decoded values here, then use pci_read_bar() to build MMIO
// pointers after all capabilities have been discovered.
struct VirtioPciRegion {
  bool present = false;
  uint8_t bar = 0;
  uint32_t offset = 0;
  uint32_t length = 0;
};

struct VirtioPciLayout {
  VirtioPciRegion common{};
  VirtioPciRegion notify{};
  VirtioPciRegion isr{};
  VirtioPciRegion device{};
  uint32_t notify_multiplier = 0;
};

// The real hardware backend has separate queue metadata from the fake backend:
// the device needs physical addresses for DMA, while the kernel keeps virtual
// pointers so it can fill descriptors and packet buffers normally.
struct VirtioBuffer {
  uint8_t *data = nullptr;
  uint64_t pa = 0;
  bool in_use = false;
  uint16_t len = 0;
};

struct VirtioQueue {
  volatile VirtioNetDescriptor *desc = nullptr;
  volatile VirtioNetAvailRing *avail = nullptr;
  volatile VirtioNetUsedRing *used = nullptr;
  uint64_t desc_pa = 0;
  uint64_t avail_pa = 0;
  uint64_t used_pa = 0;
  volatile uint16_t *notify = nullptr;
  uint16_t used_consume = 0;
  uint16_t pending = 0;
};

struct VirtioHwState {
  bool ready = false;
  PciFunction pci{};
  VirtioPciLayout layout{};
  volatile VirtioPciCommonCfg *common = nullptr;
  volatile uint8_t *device_cfg = nullptr;
  VirtioQueue rx{};
  VirtioQueue tx{};
  VirtioBuffer rx_buffers[VIRTIO_NET_QUEUE_SIZE]{};
  VirtioBuffer tx_buffers[VIRTIO_NET_QUEUE_SIZE]{};
  uint8_t mac[6]{};
  uint64_t features = 0;
};

NetState g_net{};
VirtioHwState g_hw{};

constexpr size_t k_debug_dump_limit = 64;
constexpr size_t k_virtio_buffer_size =
    sizeof(VirtioNetHdr) + VIRTIO_NET_MAX_FRAME_SIZE;

constexpr uint8_t PCI_CAP_ID_VENDOR = 0x09;
constexpr uint8_t PCI_REG_CAP_PTR = 0x34;

constexpr uint8_t VIRTIO_PCI_CAP_COMMON_CFG = 1;
constexpr uint8_t VIRTIO_PCI_CAP_NOTIFY_CFG = 2;
constexpr uint8_t VIRTIO_PCI_CAP_ISR_CFG = 3;
constexpr uint8_t VIRTIO_PCI_CAP_DEVICE_CFG = 4;

constexpr uint8_t VIRTIO_STATUS_ACKNOWLEDGE = 1;
constexpr uint8_t VIRTIO_STATUS_DRIVER = 2;
constexpr uint8_t VIRTIO_STATUS_DRIVER_OK = 4;
constexpr uint8_t VIRTIO_STATUS_FEATURES_OK = 8;
constexpr uint8_t VIRTIO_STATUS_FAILED = 128;

constexpr uint64_t VIRTIO_F_VERSION_1 = 1ULL << 32;
constexpr uint64_t VIRTIO_NET_F_MAC = 1ULL << 5;
constexpr uint64_t VIRTIO_NET_F_STATUS = 1ULL << 16;

constexpr uint16_t VIRTIO_NET_RX_QUEUE = 0;
constexpr uint16_t VIRTIO_NET_TX_QUEUE = 1;
constexpr uint8_t VIRTIO_NET_HDR_GSO_NONE = 0;

// moves to next slot in circular queue 
uint16_t next_index(uint16_t idx) {
  return uint16_t((idx + 1) % VIRTIO_NET_QUEUE_SIZE);
}

// copies the bytes from one packet to another
void copy_bytes(uint8_t *dst, const uint8_t *src, size_t len) {
  if (len == 0) {
    return;
  }
  memcpy(dst, (void *)src, len);
}

uint64_t page_pa(PPN ppn) { return PA(ppn).pa(); }

template <typename T> T *page_va(PPN ppn) { return VA(ppn); }

// Convert a virtio PCI capability's BAR+offset pair into a kernel MMIO pointer.
// If this faults at runtime, the next project step is adding an explicit MMIO
// mapping helper instead of relying on the current higher-half direct map.
volatile uint8_t *map_pci_region(const PciFunction &fn,
                                 const VirtioPciRegion &region) {
  if (!region.present) {
    return nullptr;
  }

  uint64_t base = pci_read_bar(fn, region.bar);
  if (base == 0) {
    return nullptr;
  }

  return (volatile uint8_t *)VMM::map_mmio(base + region.offset, region.length);
}

void store_region(VirtioPciRegion &region, uint8_t bar, uint32_t offset,
                  uint32_t length) {
  region.present = true;
  region.bar = bar;
  region.offset = offset;
  region.length = length;
}

bool parse_virtio_pci_layout(const PciFunction &fn, VirtioPciLayout &layout) {
  layout = {};

  // Modern virtio PCI devices describe their register layout with vendor
  // specific PCI capabilities. This loop walks the linked capability list and
  // records the locations of the common config, notify area, ISR, and NIC config.
  uint8_t cap = uint8_t(pci_config_read8(fn.bus, fn.device, fn.function,
                                         PCI_REG_CAP_PTR) &
                        0xfc);

  for (uint8_t scanned = 0; cap != 0 && scanned < 48; ++scanned) {
    uint8_t cap_id = pci_config_read8(fn.bus, fn.device, fn.function, cap);
    uint8_t next = uint8_t(
        pci_config_read8(fn.bus, fn.device, fn.function, cap + 1) & 0xfc);

    if (cap_id == PCI_CAP_ID_VENDOR) {
      uint8_t cfg_type =
          pci_config_read8(fn.bus, fn.device, fn.function, cap + 3);
      uint8_t bar = pci_config_read8(fn.bus, fn.device, fn.function, cap + 4);
      uint32_t offset =
          pci_config_read32(fn.bus, fn.device, fn.function, cap + 8);
      uint32_t length =
          pci_config_read32(fn.bus, fn.device, fn.function, cap + 12);

      if (bar < 6) {
        if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
          store_region(layout.common, bar, offset, length);
        } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
          store_region(layout.notify, bar, offset, length);
          layout.notify_multiplier =
              pci_config_read32(fn.bus, fn.device, fn.function, cap + 16);
        } else if (cfg_type == VIRTIO_PCI_CAP_ISR_CFG) {
          store_region(layout.isr, bar, offset, length);
        } else if (cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
          store_region(layout.device, bar, offset, length);
        }
      }
    }

    cap = next;
  }

  return layout.common.present && layout.notify.present;
}

uint64_t read_device_features(volatile VirtioPciCommonCfg *common) {
  // Feature bits are split into 32-bit windows selected by *_feature_select.
  // Read both halves so we can negotiate 64-bit features like VERSION_1.
  common->device_feature_select = 0;
  uint64_t low = common->device_feature;
  common->device_feature_select = 1;
  uint64_t high = common->device_feature;
  return low | (high << 32);
}

void write_driver_features(volatile VirtioPciCommonCfg *common,
                           uint64_t features) {
  // The driver writes back only the subset it understands. Anything not written
  // here is treated as unsupported by this driver.
  common->driver_feature_select = 0;
  common->driver_feature = uint32_t(features);
  common->driver_feature_select = 1;
  common->driver_feature = uint32_t(features >> 32);
}

void add_device_status(volatile VirtioPciCommonCfg *common, uint8_t status) {
  common->device_status = uint8_t(common->device_status | status);
}

bool wait_for_reset(volatile VirtioPciCommonCfg *common) {
  for (uint32_t i = 0; i < 1000000; ++i) {
    if (common->device_status == 0) {
      return true;
    }
    pause();
  }
  return false;
}

bool alloc_queue_pages(VirtioQueue &queue) {
  // Virtqueues are shared memory. QEMU reads/writes them using guest physical
  // addresses, while the kernel touches the same pages through HHDM VAs.
  PPN desc_ppn = physMem.alloc();
  PPN avail_ppn = physMem.alloc();
  PPN used_ppn = physMem.alloc();

  queue.desc = page_va<VirtioNetDescriptor>(desc_ppn);
  queue.avail = page_va<VirtioNetAvailRing>(avail_ppn);
  queue.used = page_va<VirtioNetUsedRing>(used_ppn);
  queue.desc_pa = page_pa(desc_ppn);
  queue.avail_pa = page_pa(avail_ppn);
  queue.used_pa = page_pa(used_ppn);
  queue.used_consume = 0;
  queue.pending = 0;
  return true;
}

bool alloc_packet_buffers(VirtioBuffer *buffers) {
  for (uint16_t i = 0; i < VIRTIO_NET_QUEUE_SIZE; ++i) {
    PPN ppn = physMem.alloc();
    buffers[i].data = page_va<uint8_t>(ppn);
    buffers[i].pa = page_pa(ppn);
    buffers[i].in_use = false;
    buffers[i].len = 0;
  }
  return true;
}

bool setup_virtqueue(volatile VirtioPciCommonCfg *common,
                     volatile uint8_t *notify_base,
                     uint32_t notify_multiplier, uint16_t queue_index,
                     VirtioQueue &queue) {
  // The required order is: select queue, choose size, publish descriptor/avail/
  // used physical addresses, compute notification address, then enable queue.
  common->queue_select = queue_index;
  uint16_t max_size = common->queue_size;
  if (max_size < VIRTIO_NET_QUEUE_SIZE) {
    return false;
  }

  if (!alloc_queue_pages(queue)) {
    return false;
  }

  common->queue_size = VIRTIO_NET_QUEUE_SIZE;
  common->queue_desc = queue.desc_pa;
  common->queue_driver = queue.avail_pa;
  common->queue_device = queue.used_pa;

  uint16_t notify_off = common->queue_notify_off;
  queue.notify =
      (volatile uint16_t *)(notify_base + notify_off * notify_multiplier);
  common->queue_enable = 1;
  return true;
}

void notify_queue(const VirtioQueue &queue, uint16_t queue_index) {
  // A notify write is the "doorbell" that tells the device to re-check the
  // avail ring for newly published descriptors.
  if (queue.notify != nullptr) {
    *queue.notify = queue_index;
  }
}

void init_rx_buffers_locked() {
  // RX starts by giving every receive buffer to the device. The device owns
  // these descriptors until it writes completions into the used ring.
  g_hw.rx.avail->flags = 0;
  g_hw.rx.avail->idx = 0;
  g_hw.rx.used->flags = 0;
  g_hw.rx.used->idx = 0;
  g_hw.rx.used_consume = 0;

  for (uint16_t i = 0; i < VIRTIO_NET_QUEUE_SIZE; ++i) {
    g_hw.rx_buffers[i].in_use = true;
    g_hw.rx_buffers[i].len = 0;
    g_hw.rx.desc[i].addr = g_hw.rx_buffers[i].pa;
    g_hw.rx.desc[i].len = k_virtio_buffer_size;
    g_hw.rx.desc[i].flags = VIRTQ_DESC_F_WRITE;
    g_hw.rx.desc[i].next = 0;
    g_hw.rx.avail->ring[i] = i;
  }

  mem_release();
  g_hw.rx.avail->idx = VIRTIO_NET_QUEUE_SIZE;
  notify_queue(g_hw.rx, VIRTIO_NET_RX_QUEUE);
}

void init_tx_buffers_locked() {
  g_hw.tx.avail->flags = 0;
  g_hw.tx.avail->idx = 0;
  g_hw.tx.used->flags = 0;
  g_hw.tx.used->idx = 0;
  g_hw.tx.used_consume = 0;
  g_hw.tx.pending = 0;

  for (uint16_t i = 0; i < VIRTIO_NET_QUEUE_SIZE; ++i) {
    g_hw.tx_buffers[i].in_use = false;
    g_hw.tx_buffers[i].len = 0;
    g_hw.tx.desc[i].addr = g_hw.tx_buffers[i].pa;
    g_hw.tx.desc[i].len = 0;
    g_hw.tx.desc[i].flags = 0;
    g_hw.tx.desc[i].next = 0;
  }
}

void reclaim_virtio_tx_locked() {
  while (g_hw.tx.used_consume != g_hw.tx.used->idx) {
    uint16_t used_idx = g_hw.tx.used_consume % VIRTIO_NET_QUEUE_SIZE;
    uint16_t slot = uint16_t(g_hw.tx.used->ring[used_idx].id);
    if (slot < VIRTIO_NET_QUEUE_SIZE && g_hw.tx_buffers[slot].in_use) {
      g_hw.tx_buffers[slot].in_use = false;
      g_hw.tx_buffers[slot].len = 0;
      if (g_hw.tx.pending > 0) {
        --g_hw.tx.pending;
      }
      KPRINT("net: virtio tx reclaim slot=? pending=?\n", Dec(slot),
             Dec(g_hw.tx.pending));
    }
    ++g_hw.tx.used_consume;
  }
}

int find_free_virtio_tx_slot_locked() {
  if (g_hw.tx.pending >= VIRTIO_NET_QUEUE_SIZE) {
    return -1;
  }

  for (uint16_t i = 0; i < VIRTIO_NET_QUEUE_SIZE; ++i) {
    if (!g_hw.tx_buffers[i].in_use) {
      return i;
    }
  }

  return -1;
}

bool virtio_send_raw_locked(const uint8_t *data, size_t len) {
  reclaim_virtio_tx_locked();

  int slot = find_free_virtio_tx_slot_locked();
  if (slot < 0) {
    KPRINT("net: virtio tx ring full len=?\n", Dec(len));
    return false;
  }

  // Build the device-facing buffer: virtio header first, then Ethernet frame.
  auto *hdr = (VirtioNetHdr *)g_hw.tx_buffers[slot].data;
  *hdr = {};
  hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;
  copy_bytes(g_hw.tx_buffers[slot].data + sizeof(VirtioNetHdr), data, len);

  size_t total_len = sizeof(VirtioNetHdr) + len;
  g_hw.tx_buffers[slot].in_use = true;
  g_hw.tx_buffers[slot].len = uint16_t(total_len);

  g_hw.tx.desc[slot].addr = g_hw.tx_buffers[slot].pa;
  g_hw.tx.desc[slot].len = uint32_t(total_len);
  g_hw.tx.desc[slot].flags = 0;
  g_hw.tx.desc[slot].next = 0;

  uint16_t avail_idx = g_hw.tx.avail->idx % VIRTIO_NET_QUEUE_SIZE;
  g_hw.tx.avail->ring[avail_idx] = uint16_t(slot);
  mem_release();
  g_hw.tx.avail->idx = uint16_t(g_hw.tx.avail->idx + 1);
  ++g_hw.tx.pending;

  KPRINT("net: virtio tx enqueue slot=? avail=? pending=? len=?\n", Dec(slot),
         Dec(g_hw.tx.avail->idx), Dec(g_hw.tx.pending), Dec(len));
  notify_queue(g_hw.tx, VIRTIO_NET_TX_QUEUE);
  return true;
}

int virtio_recv_raw_locked(uint8_t *out, size_t max_len) {
  if (g_hw.rx.used_consume == g_hw.rx.used->idx) {
    return 0;
  }

  uint16_t used_idx = g_hw.rx.used_consume % VIRTIO_NET_QUEUE_SIZE;
  uint16_t slot = uint16_t(g_hw.rx.used->ring[used_idx].id);
  size_t total_len = g_hw.rx.used->ring[used_idx].len;

  if (slot >= VIRTIO_NET_QUEUE_SIZE || total_len < sizeof(VirtioNetHdr)) {
    ++g_hw.rx.used_consume;
    return 0;
  }

  // Strip the virtio header before returning bytes to the raw Ethernet API.
  size_t frame_len = total_len - sizeof(VirtioNetHdr);
  if (frame_len > max_len) {
    return -1;
  }

  copy_bytes(out, g_hw.rx_buffers[slot].data + sizeof(VirtioNetHdr), frame_len);

  g_hw.rx.desc[slot].addr = g_hw.rx_buffers[slot].pa;
  g_hw.rx.desc[slot].len = k_virtio_buffer_size;
  g_hw.rx.desc[slot].flags = VIRTQ_DESC_F_WRITE;
  g_hw.rx.desc[slot].next = 0;

  ++g_hw.rx.used_consume;
  uint16_t avail_idx = g_hw.rx.avail->idx % VIRTIO_NET_QUEUE_SIZE;
  g_hw.rx.avail->ring[avail_idx] = slot;
  mem_release();
  g_hw.rx.avail->idx = uint16_t(g_hw.rx.avail->idx + 1);
  notify_queue(g_hw.rx, VIRTIO_NET_RX_QUEUE);

  KPRINT("net: virtio rx dequeue slot=? used_consume=? len=?\n", Dec(slot),
         Dec(g_hw.rx.used_consume), Dec(frame_len));

  return int(frame_len);
}

// re-initialization
void reset_slot(FrameSlot &slot) {
  slot.len = 0;
  slot.occupied = false;
}

// Reset ring state and republish every RX buffer so receive can start cleanly.
void reset_rings_locked() {
  g_net.tx_avail = {};
  g_net.rx_avail = {};
  g_net.tx_used = {};
  g_net.rx_used = {};
  g_net.tx_next_probe = 0;
  g_net.tx_used_consume = 0;
  g_net.tx_pending = 0;
  g_net.rx_next_probe = 0;
  g_net.rx_used_consume = 0;
  g_net.rx_ready = 0;

  for (uint16_t i = 0; i < VIRTIO_NET_QUEUE_SIZE; ++i) {
    reset_slot(g_net.tx_slots[i]);
    reset_slot(g_net.rx_slots[i]);

    g_net.tx_desc[i].addr = uint64_t(g_net.tx_slots[i].data);
    g_net.tx_desc[i].len = 0;
    g_net.tx_desc[i].flags = 0;
    g_net.tx_desc[i].next = 0;

    g_net.rx_desc[i].addr = uint64_t(g_net.rx_slots[i].data);
    g_net.rx_desc[i].len = VIRTIO_NET_MAX_FRAME_SIZE;
    g_net.rx_desc[i].flags = VIRTQ_DESC_F_WRITE;
    g_net.rx_desc[i].next = 0;

    g_net.rx_avail.ring[i] = i;
  }

  g_net.rx_avail.idx = VIRTIO_NET_QUEUE_SIZE;
}

// Scan from the last probe point so slot usage stays spread across the ring.
int find_free_tx_slot_locked() {
  // if already full
  if (g_net.tx_pending >= VIRTIO_NET_QUEUE_SIZE) {
    return -1;
  }

  uint16_t start = g_net.tx_next_probe;
  for (uint16_t scanned = 0; scanned < VIRTIO_NET_QUEUE_SIZE; ++scanned) {
    uint16_t slot = uint16_t((start + scanned) % VIRTIO_NET_QUEUE_SIZE);
    if (!g_net.tx_slots[slot].occupied) {
      g_net.tx_next_probe = next_index(slot);
      return slot;
    }
  }

  return -1;
}

// Scan from the last probe point so slot usage stays spread across the ring.
int find_free_rx_slot_locked() {
  if (g_net.rx_ready >= VIRTIO_NET_QUEUE_SIZE) {
    return -1;
  }

  uint16_t start = g_net.rx_next_probe;
  for (uint16_t scanned = 0; scanned < VIRTIO_NET_QUEUE_SIZE; ++scanned) {
    uint16_t slot = uint16_t((start + scanned) % VIRTIO_NET_QUEUE_SIZE);
    if (!g_net.rx_slots[slot].occupied) {
      g_net.rx_next_probe = next_index(slot);
      return slot;
    }
  }

  return -1;
}

// Consume completed TX entries from the used ring and make their slots reusable.
void reclaim_tx_locked() {
  while (g_net.tx_used_consume != g_net.tx_used.idx) {
    uint16_t used_idx = g_net.tx_used_consume % VIRTIO_NET_QUEUE_SIZE;
    uint16_t slot = uint16_t(g_net.tx_used.ring[used_idx].id);
    if (slot < VIRTIO_NET_QUEUE_SIZE && g_net.tx_slots[slot].occupied) {
      reset_slot(g_net.tx_slots[slot]);
      g_net.tx_desc[slot].len = 0;
      if (g_net.tx_pending > 0) {
        --g_net.tx_pending;
      }
      KPRINT("net: tx reclaim slot=? pending=?\n", Dec(slot),
             Dec(g_net.tx_pending));
    }
    ++g_net.tx_used_consume;
  }
}

// records completion of tx request by adding to used ring
void complete_tx_locked(uint16_t slot, size_t len) {
  uint16_t used_idx = g_net.tx_used.idx % VIRTIO_NET_QUEUE_SIZE;
  g_net.tx_used.ring[used_idx].id = slot;
  g_net.tx_used.ring[used_idx].len = len;
  ++g_net.tx_used.idx;
}

// checks if the backend is good to send/ recieve
bool backend_ready_locked() {
  if (g_net.backend == BackendKind::Fake) {
    return true;
  }
  if (g_net.backend == BackendKind::Virtio) {
    return nic_ready();
  }
  return false;
}

} // namespace

bool nic_ready() __attribute__((weak));
void nic_kick_tx() __attribute__((weak));

bool nic_ready() { return g_hw.ready; }

void nic_kick_tx() { notify_queue(g_hw.tx, VIRTIO_NET_TX_QUEUE); }

bool virtio_net_init_pci() {
  LockGuard guard{g_net.lock};
  if (g_hw.ready) {
    g_net.backend = BackendKind::Virtio;
    return true;
  }

  g_hw = {};
  PciFunction fn{};
  if (!pci_find_virtio_net(&fn)) {
    KPRINT("net: virtio-net PCI device not found\n");
    return false;
  }

  KPRINT("net: found virtio-net at ?:?:?\n", Dec(fn.bus), Dec(fn.device),
         Dec(fn.function));
  pci_enable_mmio_and_bus_mastering(fn);

  VirtioPciLayout layout{};
  if (!parse_virtio_pci_layout(fn, layout)) {
    KPRINT("net: missing virtio PCI common/notify caps\n");
    return false;
  }

  auto *common = (volatile VirtioPciCommonCfg *)map_pci_region(fn, layout.common);
  auto *notify_base = map_pci_region(fn, layout.notify);
  auto *device_cfg = map_pci_region(fn, layout.device);
  if (common == nullptr || notify_base == nullptr) {
    KPRINT("net: failed to map virtio PCI regions\n");
    return false;
  }

  common->device_status = 0;
  if (!wait_for_reset(common)) {
    KPRINT("net: virtio reset did not complete\n");
    return false;
  }

  add_device_status(common, VIRTIO_STATUS_ACKNOWLEDGE);
  add_device_status(common, VIRTIO_STATUS_DRIVER);

  uint64_t offered = read_device_features(common);
  if ((offered & VIRTIO_F_VERSION_1) == 0) {
    KPRINT("net: virtio device does not offer VERSION_1\n");
    common->device_status = VIRTIO_STATUS_FAILED;
    return false;
  }

  uint64_t wanted = VIRTIO_F_VERSION_1;
  if (offered & VIRTIO_NET_F_MAC) {
    wanted |= VIRTIO_NET_F_MAC;
  }
  if (offered & VIRTIO_NET_F_STATUS) {
    wanted |= VIRTIO_NET_F_STATUS;
  }

  write_driver_features(common, wanted);
  add_device_status(common, VIRTIO_STATUS_FEATURES_OK);
  if ((common->device_status & VIRTIO_STATUS_FEATURES_OK) == 0) {
    KPRINT("net: virtio feature negotiation rejected\n");
    common->device_status = VIRTIO_STATUS_FAILED;
    return false;
  }

  if ((wanted & VIRTIO_NET_F_MAC) && device_cfg != nullptr) {
    for (uint8_t i = 0; i < sizeof(g_hw.mac); ++i) {
      g_hw.mac[i] = device_cfg[i];
    }
    KPRINT("net: mac ??:??:??:??:??:??\n", g_hw.mac[0], g_hw.mac[1],
           g_hw.mac[2], g_hw.mac[3], g_hw.mac[4], g_hw.mac[5]);
  }

  if (!alloc_packet_buffers(g_hw.rx_buffers) ||
      !alloc_packet_buffers(g_hw.tx_buffers)) {
    common->device_status = VIRTIO_STATUS_FAILED;
    return false;
  }

  if (!setup_virtqueue(common, notify_base, layout.notify_multiplier,
                       VIRTIO_NET_RX_QUEUE, g_hw.rx) ||
      !setup_virtqueue(common, notify_base, layout.notify_multiplier,
                       VIRTIO_NET_TX_QUEUE, g_hw.tx)) {
    KPRINT("net: virtio queue setup failed\n");
    common->device_status = VIRTIO_STATUS_FAILED;
    return false;
  }

  g_hw.pci = fn;
  g_hw.layout = layout;
  g_hw.common = common;
  g_hw.device_cfg = device_cfg;
  g_hw.features = wanted;

  init_tx_buffers_locked();
  init_rx_buffers_locked();

  add_device_status(common, VIRTIO_STATUS_DRIVER_OK);
  g_hw.ready = true;
  g_net.backend = BackendKind::Virtio;
  KPRINT("net: virtio-net ready queues=? features=?\n", Dec(common->num_queues),
         wanted);
  return true;
}

void net_init_fake() {
  LockGuard guard{g_net.lock};
  reset_rings_locked();
  g_net.backend = BackendKind::Fake;
}

void net_shutdown_backend() {
  LockGuard guard{g_net.lock};
  reset_rings_locked();
  g_net.backend = BackendKind::None;
}

bool net_ready() {
  LockGuard guard{g_net.lock};
  return backend_ready_locked();
}

bool net_send_raw(const uint8_t *data, size_t len) {
  if (data == nullptr || len == 0 || len > VIRTIO_NET_MAX_FRAME_SIZE) {
    return false;
  }

  LockGuard guard{g_net.lock};
  if (!backend_ready_locked()) {
    return false;
  }

  if (g_net.backend == BackendKind::Virtio) {
    return virtio_send_raw_locked(data, len);
  }

  reclaim_tx_locked();

  int slot = find_free_tx_slot_locked();
  if (slot < 0) {
    KPRINT("net: tx ring full len=?\n", Dec(len));
    return false;
  }

  copy_bytes(g_net.tx_slots[slot].data, data, len);
  g_net.tx_slots[slot].len = len;
  g_net.tx_slots[slot].occupied = true;

  g_net.tx_desc[slot].addr = uint64_t(g_net.tx_slots[slot].data);
  g_net.tx_desc[slot].len = len;
  g_net.tx_desc[slot].flags = 0;
  g_net.tx_desc[slot].next = 0;

  uint16_t avail_idx = g_net.tx_avail.idx % VIRTIO_NET_QUEUE_SIZE;
  // Publishing the slot in the avail ring hands this descriptor to the backend.
  g_net.tx_avail.ring[avail_idx] = slot;
  ++g_net.tx_avail.idx;
  ++g_net.tx_pending;

  KPRINT("net: tx enqueue slot=? avail=? pending=? len=?\n", Dec(slot),
         Dec(g_net.tx_avail.idx), Dec(g_net.tx_pending), Dec(len));

  if (g_net.backend == BackendKind::Fake) {
    // The fake backend completes transmission immediately without real hardware.
    KPRINT("net: fake tx len=?\n", Dec(len));
    net_debug_dump_frame(data, len);
    complete_tx_locked(slot, len);
    reclaim_tx_locked();
    return true;
  }

  nic_kick_tx();
  return true;
}

// dequeues recieved packet from rx virtio queue and is copied into out
int net_recv_raw(uint8_t *out, size_t max_len) {
  if (out == nullptr || max_len == 0) {
    return -1;
  }

  LockGuard guard{g_net.lock};
  if (!backend_ready_locked()) {
    return 0;
  }

  if (g_net.backend == BackendKind::Virtio) {
    return virtio_recv_raw_locked(out, max_len);
  }

  if (g_net.rx_used_consume == g_net.rx_used.idx) {
    return 0;
  }

  uint16_t used_idx = g_net.rx_used_consume % VIRTIO_NET_QUEUE_SIZE;
  uint16_t slot = uint16_t(g_net.rx_used.ring[used_idx].id);
  size_t len = g_net.rx_used.ring[used_idx].len;

  if (slot >= VIRTIO_NET_QUEUE_SIZE || !g_net.rx_slots[slot].occupied) {
    ++g_net.rx_used_consume;
    return 0;
  }

  if (len > max_len) {
    return -1;
  }

  copy_bytes(out, g_net.rx_slots[slot].data, len);

  ++g_net.rx_used_consume;
  if (g_net.rx_ready > 0) {
    --g_net.rx_ready;
  }

  // Recycle the consumed RX buffer so a future packet can be written into it.
  g_net.rx_avail.ring[g_net.rx_avail.idx % VIRTIO_NET_QUEUE_SIZE] = slot;
  ++g_net.rx_avail.idx;
  reset_slot(g_net.rx_slots[slot]);

  g_net.rx_desc[slot].addr = uint64_t(g_net.rx_slots[slot].data);
  g_net.rx_desc[slot].len = VIRTIO_NET_MAX_FRAME_SIZE;
  g_net.rx_desc[slot].flags = VIRTQ_DESC_F_WRITE;
  g_net.rx_desc[slot].next = 0;

  KPRINT("net: rx dequeue slot=? used_consume=? ready=? len=?\n", Dec(slot),
         Dec(g_net.rx_used_consume), Dec(g_net.rx_ready), Dec(len));

  return int(len);
}

// Simulate a device receive completion by pushing a filled slot into rx_used.
bool net_fake_inject_rx(const uint8_t *data, size_t len) {
  if (data == nullptr || len == 0 || len > VIRTIO_NET_MAX_FRAME_SIZE) {
    return false;
  }

  LockGuard guard{g_net.lock};
  if (g_net.backend != BackendKind::Fake) {
    return false;
  }

  int slot = find_free_rx_slot_locked();
  if (slot < 0) {
    KPRINT("net: rx ring full len=?\n", Dec(len));
    return false;
  }

  copy_bytes(g_net.rx_slots[slot].data, data, len);
  g_net.rx_slots[slot].len = len;
  g_net.rx_slots[slot].occupied = true;

  g_net.rx_desc[slot].addr = uint64_t(g_net.rx_slots[slot].data);
  g_net.rx_desc[slot].len = len;
  g_net.rx_desc[slot].flags = VIRTQ_DESC_F_WRITE;
  g_net.rx_desc[slot].next = 0;

  uint16_t used_idx = g_net.rx_used.idx % VIRTIO_NET_QUEUE_SIZE;
  g_net.rx_used.ring[used_idx].id = slot;
  g_net.rx_used.ring[used_idx].len = len;
  ++g_net.rx_used.idx;
  ++g_net.rx_ready;

  KPRINT("net: fake rx inject slot=? ready=? len=?\n", Dec(slot),
         Dec(g_net.rx_ready), Dec(len));
  net_debug_dump_frame(data, len);
  return true;
}

// prints out the frame buffer stuff or packet essentially into good format
void net_debug_dump_frame(const uint8_t *data, size_t len) {
  if (data == nullptr || len == 0) {
    return;
  }

  size_t shown = len;
  if (shown > k_debug_dump_limit) {
    shown = k_debug_dump_limit;
  }

  for (size_t i = 0; i < shown; ++i) {
    KPRINT("? ", data[i]);
    if ((i % 16) == 15) {
      KPRINT("\n");
    }
  }

  if ((shown % 16) != 0) {
    KPRINT("\n");
  }

  if (shown != len) {
    KPRINT("... ? more bytes\n", Dec(len - shown));
  }
}
