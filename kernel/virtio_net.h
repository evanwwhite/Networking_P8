#pragma once

#include <cstdint>

// Fixed queue size for the simplified virtio TX/RX rings.
constexpr size_t VIRTIO_NET_QUEUE_SIZE = 8;
constexpr size_t VIRTIO_NET_MAX_FRAME_SIZE = 2048;

constexpr uint16_t VIRTQ_DESC_F_NEXT = 1U;
constexpr uint16_t VIRTQ_DESC_F_WRITE = 2U;

// One buffer descriptor shared with the device.
struct [[gnu::packed]] VirtioNetDescriptor {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
};

struct [[gnu::packed]] VirtioNetAvailRing {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[VIRTIO_NET_QUEUE_SIZE];
};

struct [[gnu::packed]] VirtioNetUsedElem {
  uint32_t id;
  uint32_t len;
};

struct [[gnu::packed]] VirtioNetUsedRing {
  uint16_t flags;
  uint16_t idx;
  VirtioNetUsedElem ring[VIRTIO_NET_QUEUE_SIZE];
};

// Raw frame send/receive entry points used by the rest of the kernel.
bool net_send_raw(const uint8_t *data, size_t len);
int net_recv_raw(uint8_t *out, size_t max_len);
bool net_ready();
void net_debug_dump_frame(const uint8_t *data, size_t len);

// Fake backend helpers for bring-up and local testing.
void net_init_fake();
void net_shutdown_backend();
bool net_fake_inject_rx(const uint8_t *data, size_t len);

// Real virtio-net PCI bring-up. Returns false if QEMU did not expose the NIC or
// the device rejected our minimal feature/queue setup.
bool virtio_net_init_pci();

// Minimal boundary for the future real NIC implementation.
bool nic_ready();
void nic_kick_tx();
