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
 * Parse a single device/function from legacy I/O config space and populate
 * info. Returns true if a valid device was found, false otherwise.
 */
static bool parse_device_legacy(uint8_t bus, uint8_t dev, uint8_t func) {
  uint32_t id_reg = pci_config_read32(bus, dev, func, PCI_REG_VENDOR_DEVICE);
  uint16_t vendor_id = (uint16_t)(id_reg & 0xFFFF);

  if (vendor_id == PCI_VENDOR_NOT_PRESENT) {
    return false;
  }

  auto device_id = (uint16_t)(id_reg >> PCI_DEVICE_ID_SHIFT);

  uint32_t class_reg = pci_config_read32(bus, dev, func, PCI_REG_CLASS_REV);
  auto revision_id = (uint8_t)(class_reg & 0xFF);
  auto prog_if = (uint8_t)((class_reg >> PCI_CLASS_REV_PROG_IF_SHIFT) & 0xFF);
  auto subclass = (uint8_t)((class_reg >> PCI_CLASS_REV_SUBCLASS_SHIFT) & 0xFF);
  auto class_code = (uint8_t)((class_reg >> PCI_CLASS_REV_CLASS_SHIFT) & 0xFF);

  uint32_t header_dword =
      pci_config_read32(bus, dev, func, PCI_REG_HEADER_TYPE);
  auto header_type = (uint8_t)((header_dword >> 16) & PCI_HEADER_TYPE_MASK);

  auto is_bridge =
      (class_code == PCI_CLASS_BRIDGE && subclass == PCI_SUBCLASS_PCI_BRIDGE);

  SAY("Found device: bus ?, device ?, function ?: vendor ?, device ?, class ?, "
      "subclass ?, prog IF ?, revision ?, header type ?, is_bridge ?\n",
      bus, dev, func, vendor_id, device_id, class_code, subclass, prog_if,
      revision_id, header_type, is_bridge);

  if ((class_code == 1) && (subclass == 6)) {
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
