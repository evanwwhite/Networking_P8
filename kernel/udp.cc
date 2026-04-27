#include "udp.h"

#include "atomic.h"
#include "ipv4.h"
#include "net_proto.h"
#include "net_stats.h"
#include "print.h"
#include "spin_lock.h"

namespace {

constexpr uint8_t UDP_HANDLER_CAPACITY = 8;

struct UdpHandlerEntry {
  bool valid;
  uint16_t port;
  UdpHandler handler;
};

SpinLock g_udp_lock{};
UdpHandlerEntry g_udp_handlers[UDP_HANDLER_CAPACITY]{};

uint16_t bswap16(uint16_t x) { return uint16_t((x >> 8) | (x << 8)); }

void copy_bytes(uint8_t *dst, const uint8_t *src, std::size_t len) {
  for (std::size_t i = 0; i < len; ++i) {
    dst[i] = src[i];
  }
}

void print_ip(const uint8_t ip[4]) {
  KPRINT("?.?.?.?", Dec(ip[0]), Dec(ip[1]), Dec(ip[2]), Dec(ip[3]));
}

} // namespace

bool udp_register_handler(uint16_t port, UdpHandler handler) {
  if (port == 0 || handler == nullptr) {
    return false;
  }

  LockGuard guard{g_udp_lock};
  for (uint8_t i = 0; i < UDP_HANDLER_CAPACITY; ++i) {
    if (g_udp_handlers[i].valid && g_udp_handlers[i].port == port) {
      g_udp_handlers[i].handler = handler;
      return true;
    }
  }

  for (uint8_t i = 0; i < UDP_HANDLER_CAPACITY; ++i) {
    if (!g_udp_handlers[i].valid) {
      g_udp_handlers[i].valid = true;
      g_udp_handlers[i].port = port;
      g_udp_handlers[i].handler = handler;
      KPRINT("net: UDP handler registered port=?\n", Dec(port));
      return true;
    }
  }

  return false;
}

void udp_clear_handlers() {
  LockGuard guard{g_udp_lock};
  for (uint8_t i = 0; i < UDP_HANDLER_CAPACITY; ++i) {
    g_udp_handlers[i] = {};
  }
}

bool udp_handle_packet(const uint8_t src_ip[4], const uint8_t *data,
                       std::size_t len) {
  if (src_ip == nullptr || data == nullptr || len < sizeof(UdpHeader)) {
    net_stats_increment(NetStatCounter::DroppedShort);
    KPRINT("net: DROP short UDP packet\n");
    return false;
  }

  auto udp = reinterpret_cast<const UdpHeader *>(data);
  const uint16_t src_port = bswap16(udp->src_port);
  const uint16_t dst_port = bswap16(udp->dst_port);
  const std::size_t udp_len = bswap16(udp->length);
  if (udp_len < sizeof(UdpHeader) || udp_len > len) {
    net_stats_increment(NetStatCounter::DroppedShort);
    KPRINT("net: DROP bad UDP length\n");
    return false;
  }

  UdpHandler handler = nullptr;
  {
    LockGuard guard{g_udp_lock};
    for (uint8_t i = 0; i < UDP_HANDLER_CAPACITY; ++i) {
      if (g_udp_handlers[i].valid && g_udp_handlers[i].port == dst_port) {
        handler = g_udp_handlers[i].handler;
        break;
      }
    }
  }

  if (handler == nullptr) {
    KPRINT("net: UDP no handler port=?\n", Dec(dst_port));
    return false;
  }

  const uint8_t *payload = data + sizeof(UdpHeader);
  const std::size_t payload_len = udp_len - sizeof(UdpHeader);
  net_stats_increment(NetStatCounter::UdpRx);
  KPRINT("net: RX UDP ");
  print_ip(src_ip);
  KPRINT(":? -> port=? payload_len=?\n", Dec(src_port), Dec(dst_port),
         Dec(payload_len));
  handler(src_ip, src_port, payload, payload_len);
  return true;
}

bool udp_send_to(const uint8_t dst_ip[4], uint16_t src_port, uint16_t dst_port,
                 const uint8_t *payload, std::size_t payload_len) {
  if (dst_ip == nullptr || src_port == 0 || dst_port == 0 ||
      (payload == nullptr && payload_len != 0) ||
      payload_len > 0xffffU - sizeof(UdpHeader)) {
    return false;
  }

  uint8_t packet[sizeof(UdpHeader) + 1472] = {};
  if (payload_len > sizeof(packet) - sizeof(UdpHeader)) {
    return false;
  }

  auto udp = reinterpret_cast<UdpHeader *>(packet);
  udp->src_port = bswap16(src_port);
  udp->dst_port = bswap16(dst_port);
  udp->length = bswap16(uint16_t(sizeof(UdpHeader) + payload_len));
  udp->checksum = 0;
  if (payload_len != 0) {
    copy_bytes(packet + sizeof(UdpHeader), payload, payload_len);
  }

  if (!net_send_ipv4(dst_ip, IPV4_PROTO_UDP, packet,
                     sizeof(UdpHeader) + payload_len)) {
    return false;
  }

  net_stats_increment(NetStatCounter::UdpTx);
  KPRINT("net: TX UDP port=? -> port=? payload_len=?\n", Dec(src_port),
         Dec(dst_port), Dec(payload_len));
  return true;
}
