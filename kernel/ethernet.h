#pragma once
#include <cstdint>

constexpr uint16_t ETH_TYPE_ARP  = 0x0806;
constexpr uint16_t ETH_TYPE_IPV4 = 0x0800;

struct [[gnu::packed]] EthernetHeader {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ether_type;
};