#pragma once

// AI assistance note: AI was used to help plan, review, and document this
// networking code. The implementation was integrated, tested, and reviewed by
// the team.

#include <cstdint>

constexpr uint16_t ARP_HTYPE_ETHERNET = 1;
constexpr uint16_t ARP_PTYPE_IPV4     = 0x0800;
constexpr uint16_t ARP_OP_REQUEST     = 1;
constexpr uint16_t ARP_OP_REPLY       = 2;

struct [[gnu::packed]] ArpPacket {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[6];   // sender MAC
    uint8_t  spa[4];   // sender IP
    uint8_t  tha[6];   // target MAC
    uint8_t  tpa[4];   // target IP
};
