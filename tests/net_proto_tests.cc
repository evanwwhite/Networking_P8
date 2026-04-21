#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "arp.h"
#include "ethernet.h"
#include "icmp.h"
#include "ipv4.h"
#include "net_proto.h"

namespace {

constexpr uint8_t kMyMac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
constexpr uint8_t kMyIp[4] = {10, 0, 2, 15};
constexpr uint8_t kPeerMac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
constexpr uint8_t kOtherMac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
constexpr uint8_t kPeerIp[4] = {10, 0, 2, 2};
constexpr uint8_t kOtherIp[4] = {10, 0, 2, 99};

uint8_t g_sent_frame[2048];
size_t g_sent_len = 0;
int g_send_count = 0;

uint16_t be16(uint16_t x) {
  return uint16_t((x >> 8) | (x << 8));
}

void copy_bytes(uint8_t *dst, const uint8_t *src, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    dst[i] = src[i];
  }
}

bool bytes_equal(const uint8_t *a, const uint8_t *b, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

uint16_t checksum16(const uint8_t *data, size_t len) {
  uint32_t sum = 0;
  for (size_t i = 0; i + 1 < len; i += 2) {
    sum += (uint16_t(data[i]) << 8) | data[i + 1];
  }
  if ((len & 1) != 0) {
    sum += uint16_t(data[len - 1]) << 8;
  }
  while ((sum >> 16) != 0) {
    sum = (sum & 0xffff) + (sum >> 16);
  }
  return uint16_t(~sum);
}

void reset_send_capture() {
  for (size_t i = 0; i < sizeof(g_sent_frame); ++i) {
    g_sent_frame[i] = 0;
  }
  g_sent_len = 0;
  g_send_count = 0;
}

bool expect(bool condition, const char *message) {
  if (!condition) {
    std::printf("    FAIL: %s\n", message);
    return false;
  }
  return true;
}

bool expect_bytes(const uint8_t *actual, const uint8_t *expected, size_t len,
                  const char *message) {
  return expect(bytes_equal(actual, expected, len), message);
}

void build_arp_request(uint8_t *frame, const uint8_t dst_mac[6],
                       const uint8_t target_ip[4]) {
  auto *eth = reinterpret_cast<EthernetHeader *>(frame);
  auto *arp = reinterpret_cast<ArpPacket *>(frame + sizeof(EthernetHeader));

  copy_bytes(eth->dst, dst_mac, 6);
  copy_bytes(eth->src, kPeerMac, 6);
  eth->ether_type = be16(ETH_TYPE_ARP);

  arp->htype = be16(ARP_HTYPE_ETHERNET);
  arp->ptype = be16(ARP_PTYPE_IPV4);
  arp->hlen = 6;
  arp->plen = 4;
  arp->oper = be16(ARP_OP_REQUEST);
  copy_bytes(arp->sha, kPeerMac, 6);
  copy_bytes(arp->spa, kPeerIp, 4);
  copy_bytes(arp->tha, kOtherMac, 6);
  copy_bytes(arp->tpa, target_ip, 4);
}

void build_icmp_echo_request(uint8_t *frame, size_t payload_len) {
  auto *eth = reinterpret_cast<EthernetHeader *>(frame);
  auto *ip = reinterpret_cast<Ipv4Header *>(frame + sizeof(EthernetHeader));
  auto *icmp = reinterpret_cast<IcmpEchoHeader *>(
      frame + sizeof(EthernetHeader) + sizeof(Ipv4Header));
  uint8_t *payload =
      frame + sizeof(EthernetHeader) + sizeof(Ipv4Header) + sizeof(IcmpEchoHeader);

  copy_bytes(eth->dst, kMyMac, 6);
  copy_bytes(eth->src, kPeerMac, 6);
  eth->ether_type = be16(ETH_TYPE_IPV4);

  ip->version_ihl = 0x45;
  ip->tos = 0;
  ip->total_length =
      be16(uint16_t(sizeof(Ipv4Header) + sizeof(IcmpEchoHeader) + payload_len));
  ip->identification = be16(0x1234);
  ip->flags_fragment = 0;
  ip->ttl = 64;
  ip->protocol = IPV4_PROTO_ICMP;
  ip->header_checksum = 0;
  copy_bytes(ip->src_ip, kPeerIp, 4);
  copy_bytes(ip->dst_ip, kMyIp, 4);
  ip->header_checksum = be16(checksum16(reinterpret_cast<uint8_t *>(ip),
                                        sizeof(Ipv4Header)));

  icmp->type = ICMP_ECHO_REQUEST;
  icmp->code = 0;
  icmp->checksum = 0;
  icmp->identifier = be16(0x4444);
  icmp->sequence = be16(7);

  for (size_t i = 0; i < payload_len; ++i) {
    payload[i] = uint8_t(0xa0 + i);
  }

  icmp->checksum = be16(checksum16(
      reinterpret_cast<uint8_t *>(icmp), sizeof(IcmpEchoHeader) + payload_len));
}

bool test_arp_request_for_my_ip_sends_reply() {
  reset_send_capture();

  uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  uint8_t frame[sizeof(EthernetHeader) + sizeof(ArpPacket)] = {};
  build_arp_request(frame, broadcast, kMyIp);

  net_handle_frame(frame, sizeof(frame));

  if (!expect(g_send_count == 1, "ARP request for my IP should send once")) {
    return false;
  }
  if (!expect(g_sent_len == sizeof(frame), "ARP reply should have ARP frame size")) {
    return false;
  }

  auto *eth = reinterpret_cast<const EthernetHeader *>(g_sent_frame);
  auto *arp = reinterpret_cast<const ArpPacket *>(g_sent_frame + sizeof(EthernetHeader));

  if (!expect_bytes(eth->dst, kPeerMac, 6, "reply Ethernet dst should be requester MAC")) {
    return false;
  }
  if (!expect_bytes(eth->src, kMyMac, 6, "reply Ethernet src should be my MAC")) {
    return false;
  }
  if (!expect(be16(eth->ether_type) == ETH_TYPE_ARP, "reply EtherType should be ARP")) {
    return false;
  }
  if (!expect(be16(arp->oper) == ARP_OP_REPLY, "reply ARP op should be reply")) {
    return false;
  }
  if (!expect_bytes(arp->sha, kMyMac, 6, "reply ARP sender MAC should be my MAC")) {
    return false;
  }
  if (!expect_bytes(arp->spa, kMyIp, 4, "reply ARP sender IP should be my IP")) {
    return false;
  }
  if (!expect_bytes(arp->tha, kPeerMac, 6, "reply ARP target MAC should be requester MAC")) {
    return false;
  }
  if (!expect_bytes(arp->tpa, kPeerIp, 4, "reply ARP target IP should be requester IP")) {
    return false;
  }

  return true;
}

bool test_arp_request_for_other_ip_sends_nothing() {
  reset_send_capture();

  uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  uint8_t frame[sizeof(EthernetHeader) + sizeof(ArpPacket)] = {};
  build_arp_request(frame, broadcast, kOtherIp);

  net_handle_frame(frame, sizeof(frame));

  return expect(g_send_count == 0, "ARP request for another IP should not send");
}

bool test_frame_for_other_mac_sends_nothing() {
  reset_send_capture();

  uint8_t frame[sizeof(EthernetHeader) + sizeof(ArpPacket)] = {};
  build_arp_request(frame, kOtherMac, kMyIp);

  net_handle_frame(frame, sizeof(frame));

  return expect(g_send_count == 0, "frame for another MAC should not send");
}

bool test_icmp_echo_request_for_my_ip_sends_reply() {
  reset_send_capture();

  constexpr size_t payload_len = 6;
  uint8_t frame[sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                sizeof(IcmpEchoHeader) + payload_len] = {};
  build_icmp_echo_request(frame, payload_len);

  net_handle_frame(frame, sizeof(frame));

  if (!expect(g_send_count == 1, "ICMP echo request for my IP should send once")) {
    return false;
  }
  if (!expect(g_sent_len == sizeof(frame), "ICMP echo reply should preserve frame length")) {
    return false;
  }

  auto *eth = reinterpret_cast<const EthernetHeader *>(g_sent_frame);
  auto *ip = reinterpret_cast<const Ipv4Header *>(g_sent_frame + sizeof(EthernetHeader));
  auto *icmp = reinterpret_cast<const IcmpEchoHeader *>(
      g_sent_frame + sizeof(EthernetHeader) + sizeof(Ipv4Header));
  const uint8_t *payload = g_sent_frame + sizeof(EthernetHeader) +
                           sizeof(Ipv4Header) + sizeof(IcmpEchoHeader);
  const uint8_t *input_payload = frame + sizeof(EthernetHeader) +
                                 sizeof(Ipv4Header) + sizeof(IcmpEchoHeader);

  if (!expect_bytes(eth->dst, kPeerMac, 6, "reply Ethernet dst should be requester MAC")) {
    return false;
  }
  if (!expect_bytes(eth->src, kMyMac, 6, "reply Ethernet src should be my MAC")) {
    return false;
  }
  if (!expect_bytes(ip->src_ip, kMyIp, 4, "reply IPv4 src should be my IP")) {
    return false;
  }
  if (!expect_bytes(ip->dst_ip, kPeerIp, 4, "reply IPv4 dst should be requester IP")) {
    return false;
  }
  if (!expect(icmp->type == ICMP_ECHO_REPLY, "reply ICMP type should be echo reply")) {
    return false;
  }
  if (!expect_bytes(payload, input_payload, payload_len, "reply ICMP payload should match request")) {
    return false;
  }
  if (!expect(checksum16(reinterpret_cast<const uint8_t *>(ip), sizeof(Ipv4Header)) == 0,
              "reply IPv4 checksum should be valid")) {
    return false;
  }
  if (!expect(checksum16(reinterpret_cast<const uint8_t *>(icmp),
                         sizeof(IcmpEchoHeader) + payload_len) == 0,
              "reply ICMP checksum should be valid")) {
    return false;
  }

  return true;
}

bool run_test(const char *name, bool (*test)()) {
  std::printf("net_proto: %s\n", name);
  bool passed = test();
  std::printf("net_proto: %s %s\n", passed ? "PASS" : "FAIL", name);
  return passed;
}

} // namespace

bool net_send_raw(const uint8_t *data, size_t len) {
  ++g_send_count;
  g_sent_len = len;
  if (data == nullptr || len > sizeof(g_sent_frame)) {
    return false;
  }
  copy_bytes(g_sent_frame, data, len);
  return true;
}

int main() {
  int failed = 0;

  if (!run_test("ARP request for my IP sends reply",
                test_arp_request_for_my_ip_sends_reply)) {
    ++failed;
  }
  if (!run_test("ARP request for other IP sends nothing",
                test_arp_request_for_other_ip_sends_nothing)) {
    ++failed;
  }
  if (!run_test("Ethernet frame for other MAC sends nothing",
                test_frame_for_other_mac_sends_nothing)) {
    ++failed;
  }
  if (!run_test("ICMP echo request for my IP sends reply",
                test_icmp_echo_request_for_my_ip_sends_reply)) {
    ++failed;
  }

  if (failed != 0) {
    std::printf("net_proto: %d test(s) failed\n", failed);
    return 1;
  }

  std::printf("net_proto: all tests passed\n");
  return 0;
}
