#pragma once

// AI assistance note: AI was used to help plan, review, and document this
// networking code. The implementation was integrated, tested, and reviewed by
// the team.

#include <cstdint>

struct [[gnu::packed]] UdpHeader {
  uint16_t src_port;
  uint16_t dst_port;
  uint16_t length;
  uint16_t checksum;
};

using UdpHandler = void (*)(const uint8_t src_ip[4], uint16_t src_port,
                            const uint8_t *payload, std::size_t payload_len);

// Register a callback for packets addressed to one UDP port.
bool udp_register_handler(uint16_t port, UdpHandler handler);
void udp_clear_handlers();
// Validate one UDP datagram and dispatch it to the matching port handler.
bool udp_handle_packet(const uint8_t src_ip[4], const uint8_t *data,
                       std::size_t len);
// Build and send a UDP datagram through the IPv4 layer.
bool udp_send_to(const uint8_t dst_ip[4], uint16_t src_port, uint16_t dst_port,
                 const uint8_t *payload, std::size_t payload_len);
