# Networking Part 1: Virtio-Net Bring-Up

AI assistance note: AI was used as a development aid for planning, debugging,
review, and documentation. The team reviewed the output and is responsible for
the submitted work.

This change makes the kernel discover and initialize QEMU's modern virtio-net
PCI device. The goal of this stage is not full networking yet; it is to wake the
NIC, negotiate a minimal virtio contract, configure RX/TX queues, and mark the
device ready.

## What Changed

### PCI support

`kernel/pcie.h` and `kernel/pcie.cc` now provide a small PCI helper layer:

- `PciFunction` stores bus/device/function and device identity fields.
- `pci_config_read8`, `pci_config_read16`, and `pci_config_read32` read PCI
  config space.
- `pci_config_write32` writes PCI config space.
- `pci_find_device` scans PCI functions for a vendor/device pair.
- `pci_find_virtio_net` looks for modern virtio-net (`0x1AF4:0x1041`).
- `pci_read_bar` reads a PCI BAR base address.
- `pci_enable_mmio_and_bus_mastering` enables MMIO access and DMA.

The old PCI enumeration path still exists for debug output, but the decoding
logic is shared through `read_function`.

### QEMU device

The Makefile now exposes a modern virtio-net PCI device:

```make
-device virtio-net-pci,disable-legacy=on,mac=52:54:00:12:34:56
```

There is no host network backend attached yet. That is okay for Part 1 because
the kernel only needs the PCI device to exist and initialize cleanly.

### MMIO mapping

`kernel/vmm.h` and `kernel/vmm.cc` now include `VMM::map_mmio`.

Virtio PCI capabilities point to MMIO regions inside PCI BARs. Those physical
BAR ranges must be mapped before the kernel can safely touch virtio registers.
The first real boot proved this was necessary: PCI discovery worked, but the
kernel page-faulted on the BAR access until explicit MMIO mapping was added.

### Virtio-net initialization

`kernel/virtio_net.cc` now has a real virtio backend alongside the fake backend.
The real init path is `virtio_net_init_pci`.

It performs this sequence:

1. Find the virtio-net PCI function.
2. Enable PCI MMIO and bus mastering.
3. Parse vendor-specific virtio PCI capabilities.
4. Locate common config, notification, ISR, and device-specific config regions.
5. Map those PCI BAR regions into kernel space.
6. Reset the device and wait for reset completion.
7. Set `ACKNOWLEDGE`.
8. Set `DRIVER`.
9. Read device feature bits.
10. Choose the minimal supported feature subset:
    - `VIRTIO_F_VERSION_1`
    - `VIRTIO_NET_F_MAC` if offered
    - `VIRTIO_NET_F_STATUS` if offered
11. Write driver feature bits.
12. Set and verify `FEATURES_OK`.
13. Read the MAC address if available.
14. Allocate shared queue memory and packet buffers.
15. Configure RX queue `0`.
16. Configure TX queue `1`.
17. Publish queue physical addresses to the device.
18. Enable the queues.
19. Set `DRIVER_OK`.
20. Mark the real backend ready.

The public API remains the same:

```cpp
bool net_send_raw(const uint8_t *data, size_t len);
int net_recv_raw(uint8_t *out, size_t max_len);
bool net_ready();
```

Callers still deal with raw Ethernet frames. The real virtio backend adds and
removes the required virtio-net header internally.

### Boot wiring

`kernel/kernel_main.cc` now tries real virtio-net first:

```cpp
if (!virtio_net_init_pci()) {
  net_init_fake();
}
```

If the real NIC is unavailable or initialization fails, the fake backend remains
as a fallback for existing tests.

## Evidence It Works

Running:

```sh
PATH=/u/gheith/public/tools26/bin:$PATH make -s t0.test
```

produces:

```text
net: found virtio-net at 0:3:0
net: mac 5254:0012:3456:??:??:??
net: virtio-net ready queues=3 features=0000000100010020
*** hello, 666
*** hello, 42
```

The feature value means the driver negotiated:

- `VIRTIO_F_VERSION_1`
- `VIRTIO_NET_F_STATUS`
- `VIRTIO_NET_F_MAC`

That confirms PCI discovery, BAR/MMIO mapping, virtio feature negotiation, queue
setup, and `DRIVER_OK` all completed.

## Not Included Yet

This stage does not implement full networking. Remaining future work includes:

- ARP/IP/ICMP or other protocol layers.
- A real host network backend.
- Interrupt or MSI/MSI-X handling.
- Control queue support.
- Packet-level integration tests against real RX/TX traffic.
