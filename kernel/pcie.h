#pragma once

#include <stdint.h>

extern uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function,
                                  uint8_t offset);

void enum_pcie();
