#pragma once

// AI assistance note: AI was used to help plan, review, and document this
// networking code. The implementation was integrated, tested, and reviewed by
// the team.

#include <cstdint>

#include "net_stats.h"

// Parse one Ethernet frame and dispatch ARP/IPv4 handlers.
void net_handle_frame(const uint8_t* data, std::size_t len);
// Poll the raw NIC once and process one frame if available.
bool net_poll_once();
// Set this guest's network identity for tests and dual-QEMU roles.
void net_set_identity(const uint8_t mac[6], const uint8_t ip[4]);
bool net_send_arp_request(const uint8_t target_ip[4]);
// Send an IPv4 payload; returns false when ARP resolution is still pending.
bool net_send_ipv4(const uint8_t dst_ip[4], uint8_t protocol,
                   const uint8_t* payload, std::size_t payload_len);
