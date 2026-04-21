#pragma once

#include <stdint.h>

struct PciFunction {
  uint8_t bus;
  uint8_t device;
  uint8_t function;
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t class_code;
  uint8_t subclass;
  uint8_t prog_if;
  uint8_t revision_id;
  uint8_t header_type;
};


extern uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function,
                                  uint8_t offset);


uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t offset);

uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function,
                         uint8_t offset);

void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t offset, uint32_t value);

bool pci_find_device(uint16_t vendor_id, uint16_t device_id,
                     PciFunction *out);

bool pci_find_virtio_net(PciFunction *out);

uint64_t pci_read_bar(const PciFunction &fn, uint8_t bar_index);

void pci_enable_mmio_and_bus_mastering(const PciFunction &fn);
void enum_pcie();
