#include "virtio_net.h"

#include "atomic.h"
#include "debug.h"
#include "machine.h"
#include "print.h"
#include "spin_lock.h"

namespace {

enum class BackendKind : uint8_t {
  None,
  Fake,
};

struct FrameSlot {
  uint8_t data[VIRTIO_NET_MAX_FRAME_SIZE];
  uint16_t len;
  bool occupied;
};

struct NetState {
  SpinLock lock{};
  BackendKind backend = BackendKind::None;

  VirtioNetDescriptor tx_desc[VIRTIO_NET_QUEUE_SIZE]{};
  VirtioNetDescriptor rx_desc[VIRTIO_NET_QUEUE_SIZE]{};
  VirtioNetAvailRing tx_avail{};
  VirtioNetAvailRing rx_avail{};
  VirtioNetUsedRing tx_used{};
  VirtioNetUsedRing rx_used{};

  FrameSlot tx_slots[VIRTIO_NET_QUEUE_SIZE]{};
  FrameSlot rx_slots[VIRTIO_NET_QUEUE_SIZE]{};

  uint16_t tx_next_probe = 0;
  uint16_t tx_used_consume = 0;
  uint16_t tx_pending = 0;

  uint16_t rx_next_probe = 0;
  uint16_t rx_used_consume = 0;
  uint16_t rx_ready = 0;
};

NetState g_net{};

constexpr size_t k_debug_dump_limit = 64;

uint16_t next_index(uint16_t idx) {
  return uint16_t((idx + 1) % VIRTIO_NET_QUEUE_SIZE);
}

void copy_bytes(uint8_t *dst, const uint8_t *src, size_t len) {
  if (len == 0) {
    return;
  }
  memcpy(dst, (void *)src, len);
}

void reset_slot(FrameSlot &slot) {
  slot.len = 0;
  slot.occupied = false;
}

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

int find_free_tx_slot_locked() {
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

void complete_tx_locked(uint16_t slot, size_t len) {
  uint16_t used_idx = g_net.tx_used.idx % VIRTIO_NET_QUEUE_SIZE;
  g_net.tx_used.ring[used_idx].id = slot;
  g_net.tx_used.ring[used_idx].len = len;
  ++g_net.tx_used.idx;
}

bool backend_ready_locked() {
  if (g_net.backend == BackendKind::Fake) {
    return true;
  }
  return nic_ready();
}

} // namespace

bool nic_ready() __attribute__((weak));
void nic_kick_tx() __attribute__((weak));

bool nic_ready() { return false; }

void nic_kick_tx() {}

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
  g_net.tx_avail.ring[avail_idx] = slot;
  ++g_net.tx_avail.idx;
  ++g_net.tx_pending;

  KPRINT("net: tx enqueue slot=? avail=? pending=? len=?\n", Dec(slot),
         Dec(g_net.tx_avail.idx), Dec(g_net.tx_pending), Dec(len));

  if (g_net.backend == BackendKind::Fake) {
    KPRINT("net: fake tx len=?\n", Dec(len));
    net_debug_dump_frame(data, len);
    complete_tx_locked(slot, len);
    reclaim_tx_locked();
    return true;
  }

  nic_kick_tx();
  return true;
}

int net_recv_raw(uint8_t *out, size_t max_len) {
  if (out == nullptr || max_len == 0) {
    return -1;
  }

  LockGuard guard{g_net.lock};
  if (!backend_ready_locked()) {
    return 0;
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
