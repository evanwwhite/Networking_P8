#pragma once
#include <cstdint>

constexpr uint8_t IPV4_PROTO_ICMP = 1;

struct [[gnu::packed]] Ipv4Header {
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t header_checksum;
    uint8_t  src_ip[4];
    uint8_t  dst_ip[4];
};