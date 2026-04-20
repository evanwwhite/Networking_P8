#pragma once
#include <cstdint>

constexpr uint8_t ICMP_ECHO_REPLY   = 0;
constexpr uint8_t ICMP_ECHO_REQUEST = 8;

struct [[gnu::packed]] IcmpEchoHeader {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
};