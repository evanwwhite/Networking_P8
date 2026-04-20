#include "pcie.h"

#include "machine.h"
#include "print.h"

constexpr uint16_t VENDOR_NOT_PRESENT = 0xFFFF;

#define PCI_VENDOR_NOT_PRESENT 0xFFFF

// Following Macros were commented and annotated using AI

// Legacy PCI Configuration Space Access Ports (PCI 2.0 specification)
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

// PCI Bus Enumeration Limits
#define PCI_MAX_BUSES 256
#define PCI_MAX_DEVICES 32
#define PCI_MAX_FUNCTIONS 8

// PCI Configuration Space Offsets
#define PCI_REG_VENDOR_DEVICE 0x00 // Vendor ID (16-bit) | Device ID (16-bit)
#define PCI_REG_CLASS_REV 0x08     // Class Code, Subclass, Prog IF, Revision
#define PCI_REG_HEADER_TYPE 0x0C   // DWORD containing Header Type at byte 2

// PCI Register Constants
#define PCI_REG_COMMAND_STATUS 0x04
#define PCI_REG_CAP_PTR 0x34
#define PCI_REG_BAR0 0x10

#define PCI_COMMAND_IO_SPACE 0x1
#define PCI_COMMAND_MEMORY_SPACE 0x2
#define PCI_COMMAND_BUS_MASTER 0x4

// Bridge Configuration Space Offsets (Type 1 Header)
#define PCI_REG_IO_BASE_LIMIT 0x1C // I/O Base (byte 0) | I/O Limit (byte 1)
#define PCI_REG_BUS_NUMBERS 0x18 // Primary, Secondary, Subordinate bus numbers
#define PCI_REG_MEM_BASE_LIMIT                                                 \
  0x20 // Memory Base (16-bit) | Memory Limit (16-bit)
#define PCI_REG_PREFETCH_BASE                                                  \
  0x24 // Prefetchable Memory Base (16-bit) | Limit (16-bit)
#define PCI_REG_PREFETCH_BASE_HI 0x28  // Prefetchable Base Upper 32 bits
#define PCI_REG_PREFETCH_LIMIT_HI 0x2C // Prefetchable Limit Upper 32 bits
#define PCI_REG_SUBSYS 0x2C // Subsystem Vendor ID, Subsystem ID (Type 0 only)

// PCI Header Type Bits
#define PCI_HEADER_TYPE_MASK 0x7F
#define PCI_HEADER_TYPE_MULTIFUNCTION 0x80
#define PCI_HEADER_TYPE_ENDPOINT 0x00
#define PCI_HEADER_TYPE_BRIDGE 0x01

// PCI Class/Subclass for PCI-PCI Bridge
#define PCI_CLASS_BRIDGE 0x06
#define PCI_SUBCLASS_PCI_BRIDGE 0x04

// Bit manipulation constants
#define PCI_DEVICE_ID_SHIFT 16

// Class register field shifts (offset 0x08)
#define PCI_CLASS_REV_PROG_IF_SHIFT 8
#define PCI_CLASS_REV_SUBCLASS_SHIFT 16
#define PCI_CLASS_REV_CLASS_SHIFT 24

// Bitmap to track visited buses and prevent re-scanning
static uint8_t bus_visited[PCI_MAX_BUSES / 8];

static void mark_bus_visited(uint8_t bus) {
  bus_visited[bus / 8] |= (1 << (bus % 8));
}

static bool is_bus_visited(uint8_t bus) {
  return (bus_visited[bus / 8] & (1 << (bus % 8))) != 0;
}

static void clear_bus_visited() {
  for (int i = 0; i < PCI_MAX_BUSES / 8; i++) {
    bus_visited[i] = 0;
  }
}

/**
 * Returns the CONFIG_ADDRESS register value for legacy PCI config space access.
 *
 * @param bus      PCI Bus number (0-255)
 * @param device   PCI Device number (0-31)
 * @param function PCI Function number (0-7)
 * @param offset   Register offset (must be 4-byte aligned, 0-252)
 * @return The 32-bit value to write to CONFIG_ADDRESS port
 */
static uint32_t pci_config_address(uint8_t bus, uint8_t device,
                                   uint8_t function, uint8_t offset) {
  return (uint32_t)((1U << 31) | ((uint32_t)bus << 16) |
                    ((uint32_t)(device & 0x1F) << 11) |
                    ((uint32_t)(function & 0x07) << 8) |
                    ((uint32_t)(offset & 0xFC)));
}

/**
 * Reads a 32-bit value from PCI configuration space
 *
 * @param bus      PCI Bus number (0-255)
 * @param device   PCI Device number (0-31)
 * @param function PCI Function number (0-7)
 * @param offset   Register offset (must be 4-byte aligned)
 * @return The 32-bit value read from configuration space
 */
uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t offset) {
  outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, device, function, offset));
  return inl(PCI_CONFIG_DATA);
}
/**
 * Writes a 32-bit value from PCI configuration space
 *
 * @param bus      PCI Bus number (0-255)
 * @param device   PCI Device number (0-31)
 * @param function PCI Function number (0-7)
 * @param offset   Register offset (must be 4-byte aligned)
 */
void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t offset, uint32_t value) {
  outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, device, function, offset));
  outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t offset) {
  uint32_t value = pci_config_read32(bus, device, function, offset);
  return uint16_t(value >> ((offset & 2) * 8));
}

uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function,
                         uint8_t offset) {
  uint32_t value = pci_config_read32(bus, device, function, offset);
  return uint8_t(value >> ((offset & 3) * 8));
}

static bool read_function(uint8_t bus, uint8_t dev, uint8_t func,
                          PciFunction *out) {
  uint32_t id_reg = pci_config_read32(bus, dev, func, PCI_REG_VENDOR_DEVICE);
  uint16_t vendor_id = uint16_t(id_reg & 0xffff);

  if (vendor_id == PCI_VENDOR_NOT_PRESENT) {
    return false;
  }

  uint16_t device_id = uint16_t(id_reg >> PCI_DEVICE_ID_SHIFT);

  uint32_t class_reg = pci_config_read32(bus, dev, func, PCI_REG_CLASS_REV);
  uint8_t revision_id = uint8_t(class_reg & 0xff);
  uint8_t prog_if = uint8_t((class_reg >> PCI_CLASS_REV_PROG_IF_SHIFT) & 0xff);
  uint8_t subclass = uint8_t((class_reg >> PCI_CLASS_REV_SUBCLASS_SHIFT) & 0xff);
  uint8_t class_code =
      uint8_t((class_reg >> PCI_CLASS_REV_CLASS_SHIFT) & 0xff);

  uint32_t header_dword =
      pci_config_read32(bus, dev, func, PCI_REG_HEADER_TYPE);
  uint8_t header_type = uint8_t((header_dword >> 16) & PCI_HEADER_TYPE_MASK);

  if (out != nullptr) {
    out->bus = bus;
    out->device = dev;
    out->function = func;
    out->vendor_id = vendor_id;
    out->device_id = device_id;
    out->class_code = class_code;
    out->subclass = subclass;
    out->prog_if = prog_if;
    out->revision_id = revision_id;
    out->header_type = header_type;
  }

  return true;
}

/**
 * Probe one PCI bus/device/function slot using legacy config-space I/O.
 * Returns false when no device is present; otherwise prints the device identity
 * and class fields so enum_pcie() can dump what hardware QEMU exposed.
 */
static bool parse_device_legacy(uint8_t bus, uint8_t dev, uint8_t func) {
  PciFunction info{};
  if (!read_function(bus, dev, func, &info)) {
    return false;
  }

  auto is_bridge = (info.class_code == PCI_CLASS_BRIDGE &&
                    info.subclass == PCI_SUBCLASS_PCI_BRIDGE);

  SAY("Found device: bus ?, device ?, function ?: vendor ?, device ?, class ?, "
      "subclass ?, prog IF ?, revision ?, header type ?, is_bridge ?\n",
      info.bus, info.device, info.function, info.vendor_id, info.device_id,
      info.class_code, info.subclass, info.prog_if, info.revision_id,
      info.header_type, is_bridge);

  if ((info.class_code == 1) && (info.subclass == 6)) {
#if 0
    Ide::register_controller(bus, dev, func, class_code, subclass);
    auto abar_reg = pci_config_read32(bus, dev, func, 0x24);
    SAY("    is a SATA controller, abar = ?\n", abar_reg);
    auto abar = (uint32_t *)uint64_t(abar_reg & ~0xFFF);
    SAY("    abar[0] = ?\n", abar[0]);
    SAY("    abar[1] = ?\n", abar[1]);
#endif
  }

  return true;
}

bool pci_find_device(uint16_t vendor_id, uint16_t device_id,
                     PciFunction *out) {
  for (uint16_t bus = 0; bus < PCI_MAX_BUSES; ++bus) {
    for (uint8_t device = 0; device < PCI_MAX_DEVICES; ++device) {
      PciFunction info{};

      if (!read_function(uint8_t(bus), device, 0, &info)) {
        continue;
      }

      if (info.vendor_id == vendor_id && info.device_id == device_id) {
        if (out != nullptr) {
          *out = info;
        }
        return true;
      }

      uint32_t header_dword =
          pci_config_read32(uint8_t(bus), device, 0, PCI_REG_HEADER_TYPE);
      uint8_t raw_header_type = uint8_t((header_dword >> 16) & 0xff);

      if (raw_header_type & PCI_HEADER_TYPE_MULTIFUNCTION) {
        for (uint8_t function = 1; function < PCI_MAX_FUNCTIONS; ++function) {
          if (!read_function(uint8_t(bus), device, function, &info)) {
            continue;
          }

          if (info.vendor_id == vendor_id && info.device_id == device_id) {
            if (out != nullptr) {
              *out = info;
            }
            return true;
          }
        }
      }
    }
  }

  return false;
}

bool pci_find_virtio_net(PciFunction *out) {
  return pci_find_device(0x1AF4, 0x1041, out);
}

uint64_t pci_read_bar(const PciFunction &fn, uint8_t bar_index) {
  if (bar_index >= 6) {
    return 0;
  }

  uint8_t offset = PCI_REG_BAR0 + bar_index * 4;
  uint32_t bar = pci_config_read32(fn.bus, fn.device, fn.function, offset);

  if (bar & 0x1) {
    return uint64_t(bar & ~0x3U);
  }

  uint64_t base = uint64_t(bar & ~0xFU);
  uint32_t type = (bar >> 1) & 0x3;

  if (type == 0x2 && bar_index < 5) {
    uint32_t high =
        pci_config_read32(fn.bus, fn.device, fn.function, offset + 4);
    base |= uint64_t(high) << 32;
  }

  return base;
}

void pci_enable_mmio_and_bus_mastering(const PciFunction &fn) {
  uint32_t command_status =
      pci_config_read32(fn.bus, fn.device, fn.function, PCI_REG_COMMAND_STATUS);

  uint16_t command = uint16_t(command_status & 0xffff);
  command |= PCI_COMMAND_MEMORY_SPACE;
  command |= PCI_COMMAND_BUS_MASTER;

  command_status = (command_status & 0xffff0000U) | command;
  pci_config_write32(fn.bus, fn.device, fn.function, PCI_REG_COMMAND_STATUS,
                     command_status);
}

/**
 * Enumerate all devices on a specific PCI bus via legacy I/O port access.
 * For bridges, recursively enumerates secondary buses.
 */
static void enumerate_bus(uint8_t bus) {
  if (is_bus_visited(bus))
    return;
  mark_bus_visited(bus);

  for (uint8_t device = 0; device < PCI_MAX_DEVICES; device++) {

    if (!parse_device_legacy(bus, device, 0)) {
      continue;
    }

    // Check if multifunction device
    uint32_t header_dword =
        pci_config_read32(bus, device, 0, PCI_REG_HEADER_TYPE);
    uint8_t raw_header_type = (uint8_t)((header_dword >> 16) & 0xFF);

    if (raw_header_type & PCI_HEADER_TYPE_MULTIFUNCTION) {
      for (uint8_t function = 1; function < PCI_MAX_FUNCTIONS; function++) {

        if (!parse_device_legacy(bus, device, function)) {
          continue;
        }
      }
    }
  }
}

void enum_pcie() {
  clear_bus_visited();

  // recursively scan buses
  enumerate_bus(0);
}
