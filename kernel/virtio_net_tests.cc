#include "virtio_net_tests.h"

// AI assistance note: AI was used to help structure these networking tests and
// clarify expected packet behavior. The tests were adapted to this kernel and
// verified by the team.

#include "arp.h"
#include "arp_cache.h"
#include "ethernet.h"
#include "icmp.h"
#include "ipv4.h"
#include "net_chat.h"
#include "net_proto.h"
#include "net_stats.h"
#include "pit.h"
#include "print.h"
#include "thread.h"
#include "udp.h"
#include "virtio_net.h"

namespace {

constexpr size_t k_selector_buffer_size = 32;
constexpr size_t k_debug_frame_len = 80;
constexpr size_t k_tx_burst_count = VIRTIO_NET_QUEUE_SIZE * 3;
constexpr size_t k_rx_stress_loops = 32;

enum class NetTestCase : uint8_t {
  None,
  Demo,
  DualLive,
  Smoke,
  Tx,
  Rx,
  Queue,
  Debug,
  Stats,
  ArpCache,
  Udp,
  Chat,
  Proto,
  Live,
  RealTx,
  Unknown,
};

struct TestFrames {
  uint8_t frame_a[60];
  uint8_t frame_b[60];
  uint8_t frame_c[VIRTIO_NET_MAX_FRAME_SIZE];
  uint8_t oversized[VIRTIO_NET_MAX_FRAME_SIZE + 1];
};

constexpr uint8_t k_proto_my_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
constexpr uint8_t k_proto_my_ip[4] = {10, 0, 2, 15};
constexpr uint8_t k_proto_peer_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
constexpr uint8_t k_proto_peer_ip[4] = {10, 0, 2, 2};
constexpr uint8_t k_dual_sender_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x57};
constexpr uint8_t k_dual_sender_ip[4] = {10, 0, 2, 21};
constexpr uint8_t k_dual_responder_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
constexpr uint8_t k_dual_responder_ip[4] = {10, 0, 2, 15};
constexpr char k_dual_chat_sender_text[] = "hello from sender";
constexpr char k_dual_chat_responder_text[] = "hello from responder";

enum class DualRole : uint8_t {
  Unknown,
  Sender,
  Responder,
};

TestFrames g_frames{};
bool g_frames_ready = false;

struct UdpCapture {
  bool called;
  uint8_t src_ip[4];
  uint16_t src_port;
  uint8_t payload[64];
  size_t payload_len;
};

UdpCapture g_udp_capture{};

void init_frames() {
  if (g_frames_ready) {
    return;
  }

  for (size_t i = 0; i < sizeof(g_frames.frame_a); ++i) {
    g_frames.frame_a[i] = 0xaa;
  }

  for (size_t i = 0; i < sizeof(g_frames.frame_b); ++i) {
    g_frames.frame_b[i] = uint8_t(i);
  }

  for (size_t i = 0; i < sizeof(g_frames.frame_c); ++i) {
    g_frames.frame_c[i] = uint8_t(i & 0xffU);
  }

  for (size_t i = 0; i < sizeof(g_frames.oversized); ++i) {
    g_frames.oversized[i] = uint8_t((i + 1) & 0xffU);
  }

  g_frames_ready = true;
}

size_t min_size(size_t a, size_t b) { return a < b ? a : b; }

uint16_t be16(uint16_t x) { return uint16_t((x >> 8) | (x << 8)); }

void reset_fake_backend() {
  net_shutdown_backend();
  net_init_fake();
}

bool bytes_equal(const uint8_t *lhs, const uint8_t *rhs, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (lhs[i] != rhs[i]) {
      return false;
    }
  }
  return true;
}

void copy_bytes_local(uint8_t *dst, const uint8_t *src, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    dst[i] = src[i];
  }
}

uint16_t checksum16(const uint8_t *data, size_t len) {
  uint32_t sum = 0;
  for (size_t i = 0; i + 1 < len; i += 2) {
    sum += (uint16_t(data[i]) << 8) | data[i + 1];
  }
  if ((len & 1U) != 0) {
    sum += uint16_t(data[len - 1]) << 8;
  }
  while ((sum >> 16) != 0) {
    sum = (sum & 0xffffU) + (sum >> 16);
  }
  return uint16_t(~sum);
}

void build_arp_request(uint8_t *frame) {
  auto *eth = (EthernetHeader *)frame;
  auto *arp = (ArpPacket *)(frame + sizeof(EthernetHeader));
  uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

  copy_bytes_local(eth->dst, broadcast, 6);
  copy_bytes_local(eth->src, k_proto_peer_mac, 6);
  eth->ether_type = be16(ETH_TYPE_ARP);

  arp->htype = be16(ARP_HTYPE_ETHERNET);
  arp->ptype = be16(ARP_PTYPE_IPV4);
  arp->hlen = 6;
  arp->plen = 4;
  arp->oper = be16(ARP_OP_REQUEST);
  copy_bytes_local(arp->sha, k_proto_peer_mac, 6);
  copy_bytes_local(arp->spa, k_proto_peer_ip, 4);
  copy_bytes_local(arp->tha, k_proto_my_mac, 6);
  copy_bytes_local(arp->tpa, k_proto_my_ip, 4);
}

void build_icmp_echo_request_payload(uint8_t *frame, const uint8_t *payload_bytes,
                                     size_t payload_len) {
  auto *eth = (EthernetHeader *)frame;
  auto *ip = (Ipv4Header *)(frame + sizeof(EthernetHeader));
  auto *icmp =
      (IcmpEchoHeader *)(frame + sizeof(EthernetHeader) + sizeof(Ipv4Header));
  uint8_t *payload = frame + sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                     sizeof(IcmpEchoHeader);

  copy_bytes_local(eth->dst, k_proto_my_mac, 6);
  copy_bytes_local(eth->src, k_proto_peer_mac, 6);
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
  copy_bytes_local(ip->src_ip, k_proto_peer_ip, 4);
  copy_bytes_local(ip->dst_ip, k_proto_my_ip, 4);
  ip->header_checksum =
      be16(checksum16((const uint8_t *)ip, sizeof(Ipv4Header)));

  icmp->type = ICMP_ECHO_REQUEST;
  icmp->code = 0;
  icmp->checksum = 0;
  icmp->identifier = be16(0x4444);
  icmp->sequence = be16(7);
  for (size_t i = 0; i < payload_len; ++i) {
    payload[i] = payload_bytes[i];
  }
  icmp->checksum = be16(
      checksum16((const uint8_t *)icmp, sizeof(IcmpEchoHeader) + payload_len));
}

void build_udp_packet(uint8_t *frame, uint16_t src_port, uint16_t dst_port,
                      const uint8_t *payload_bytes, size_t payload_len) {
  auto *eth = (EthernetHeader *)frame;
  auto *ip = (Ipv4Header *)(frame + sizeof(EthernetHeader));
  auto *udp = (UdpHeader *)(frame + sizeof(EthernetHeader) + sizeof(Ipv4Header));
  uint8_t *payload = frame + sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                     sizeof(UdpHeader);

  copy_bytes_local(eth->dst, k_proto_my_mac, 6);
  copy_bytes_local(eth->src, k_proto_peer_mac, 6);
  eth->ether_type = be16(ETH_TYPE_IPV4);

  ip->version_ihl = 0x45;
  ip->tos = 0;
  ip->total_length =
      be16(uint16_t(sizeof(Ipv4Header) + sizeof(UdpHeader) + payload_len));
  ip->identification = be16(0x3333);
  ip->flags_fragment = 0;
  ip->ttl = 64;
  ip->protocol = IPV4_PROTO_UDP;
  ip->header_checksum = 0;
  copy_bytes_local(ip->src_ip, k_proto_peer_ip, 4);
  copy_bytes_local(ip->dst_ip, k_proto_my_ip, 4);
  ip->header_checksum =
      be16(checksum16((const uint8_t *)ip, sizeof(Ipv4Header)));

  udp->src_port = be16(src_port);
  udp->dst_port = be16(dst_port);
  udp->length = be16(uint16_t(sizeof(UdpHeader) + payload_len));
  udp->checksum = 0;
  for (size_t i = 0; i < payload_len; ++i) {
    payload[i] = payload_bytes[i];
  }
}

void build_arp_reply_frame(uint8_t *frame, const uint8_t src_mac[6],
                           const uint8_t src_ip[4], const uint8_t dst_mac[6],
                           const uint8_t dst_ip[4]) {
  auto *eth = (EthernetHeader *)frame;
  auto *arp = (ArpPacket *)(frame + sizeof(EthernetHeader));

  copy_bytes_local(eth->dst, dst_mac, 6);
  copy_bytes_local(eth->src, src_mac, 6);
  eth->ether_type = be16(ETH_TYPE_ARP);

  arp->htype = be16(ARP_HTYPE_ETHERNET);
  arp->ptype = be16(ARP_PTYPE_IPV4);
  arp->hlen = 6;
  arp->plen = 4;
  arp->oper = be16(ARP_OP_REPLY);
  copy_bytes_local(arp->sha, src_mac, 6);
  copy_bytes_local(arp->spa, src_ip, 4);
  copy_bytes_local(arp->tha, dst_mac, 6);
  copy_bytes_local(arp->tpa, dst_ip, 4);
}

void build_icmp_echo_request(uint8_t *frame, size_t payload_len) {
  auto *payload = frame + sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                  sizeof(IcmpEchoHeader);
  for (size_t i = 0; i < payload_len; ++i) {
    payload[i] = uint8_t(0xa0 + i);
  }
  build_icmp_echo_request_payload(frame, payload, payload_len);
}

void demo_payload_line(const char *label, const uint8_t *payload, size_t len) {
  char text[64]{};
  const size_t to_copy = (len < sizeof(text) - 1) ? len : sizeof(text) - 1;
  for (size_t i = 0; i < to_copy; ++i) {
    const uint8_t ch = payload[i];
    text[i] = (ch >= 32 && ch <= 126) ? char(ch) : '.';
  }
  text[to_copy] = 0;
  KPRINT("***   ? : \"?\"\n", label, text);
}

bool strings_equal(const char *lhs, const char *rhs) {
  size_t i = 0;
  while (lhs[i] != 0 && rhs[i] != 0) {
    if (lhs[i] != rhs[i]) {
      return false;
    }
    ++i;
  }
  return lhs[i] == rhs[i];
}

void trim_selector(char *buffer) {
  size_t len = 0;
  while (buffer[len] != 0) {
    ++len;
  }

  while (len > 0) {
    const char ch = buffer[len - 1];
    if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t') {
      buffer[len - 1] = 0;
      --len;
      continue;
    }
    break;
  }
}

NetTestCase parse_test_case(const char *name) {
  if (strings_equal(name, "demo")) {
    return NetTestCase::Demo;
  }
  if (strings_equal(name, "dual_live")) {
    return NetTestCase::DualLive;
  }
  if (strings_equal(name, "smoke")) {
    return NetTestCase::Smoke;
  }
  if (strings_equal(name, "tx")) {
    return NetTestCase::Tx;
  }
  if (strings_equal(name, "rx")) {
    return NetTestCase::Rx;
  }
  if (strings_equal(name, "queue")) {
    return NetTestCase::Queue;
  }
  if (strings_equal(name, "debug")) {
    return NetTestCase::Debug;
  }
  if (strings_equal(name, "stats")) {
    return NetTestCase::Stats;
  }
  if (strings_equal(name, "arp_cache")) {
    return NetTestCase::ArpCache;
  }
  if (strings_equal(name, "udp")) {
    return NetTestCase::Udp;
  }
  if (strings_equal(name, "chat")) {
    return NetTestCase::Chat;
  }
  if (strings_equal(name, "proto")) {
    return NetTestCase::Proto;
  }
  if (strings_equal(name, "live")) {
    return NetTestCase::Live;
  }
  if (strings_equal(name, "real_tx")) {
    return NetTestCase::RealTx;
  }
  return NetTestCase::Unknown;
}

NetTestCase read_test_case(StrongRef<Ext2> fs, char *buffer, size_t buffer_size) {
  auto selector = fs->find(fs->root, "net_test_case");
  if (selector == nullptr || buffer_size == 0) {
    return NetTestCase::None;
  }

  const size_t to_read = min_size(size_t(selector->size_in_bytes()), buffer_size - 1);
  if (to_read == 0) {
    return NetTestCase::None;
  }

  const int64_t count = selector->read_all(0, uint32_t(to_read), buffer);
  if (count <= 0) {
    return NetTestCase::None;
  }

  buffer[count] = 0;
  trim_selector(buffer);
  return parse_test_case(buffer);
}

bool read_named_file(StrongRef<Ext2> fs, const char *name, char *buffer,
                     size_t buffer_size) {
  if (buffer_size == 0) {
    return false;
  }

  auto node = fs->find(fs->root, name);
  if (node == nullptr) {
    buffer[0] = 0;
    return false;
  }

  const size_t to_read =
      min_size(size_t(node->size_in_bytes()), buffer_size - 1);
  const int64_t count = node->read_all(0, uint32_t(to_read), buffer);
  if (count <= 0) {
    buffer[0] = 0;
    return false;
  }

  buffer[count] = 0;
  trim_selector(buffer);
  return true;
}

DualRole read_dual_role(StrongRef<Ext2> fs) {
  char buffer[k_selector_buffer_size]{};
  if (!read_named_file(fs, "net_role", buffer, sizeof(buffer))) {
    return DualRole::Unknown;
  }
  if (strings_equal(buffer, "sender")) {
    return DualRole::Sender;
  }
  if (strings_equal(buffer, "responder")) {
    return DualRole::Responder;
  }
  return DualRole::Unknown;
}

struct Reporter {
  int failures = 0;

  void pass(const char *name) { KPRINT("*** PASS ?\n", name); }

  void fail(const char *name) {
    ++failures;
    KPRINT("*** FAIL ?\n", name);
  }

  void fail_eq(const char *name, int got, int want) {
    ++failures;
    KPRINT("*** FAIL ? got=? want=?\n", name, Dec(got), Dec(want));
  }

  void fail_bytes(const char *name, size_t len) {
    ++failures;
    KPRINT("*** FAIL ? byte-mismatch len=?\n", name, Dec(len));
  }

  void check(const char *name, bool ok) {
    if (ok) {
      pass(name);
    } else {
      fail(name);
    }
  }

  void check_eq(const char *name, int got, int want) {
    if (got == want) {
      pass(name);
    } else {
      fail_eq(name, got, want);
    }
  }

  void check_bytes(const char *name, const uint8_t *got, const uint8_t *want,
                   size_t len) {
    if (bytes_equal(got, want, len)) {
      pass(name);
    } else {
      fail_bytes(name, len);
    }
  }

  bool quiet_check(const char *name, bool ok) {
    if (!ok) {
      fail(name);
      return false;
    }
    return true;
  }

  bool quiet_check_eq(const char *name, int got, int want) {
    if (got != want) {
      fail_eq(name, got, want);
      return false;
    }
    return true;
  }

  bool quiet_check_bytes(const char *name, const uint8_t *got,
                         const uint8_t *want, size_t len) {
    if (!bytes_equal(got, want, len)) {
      fail_bytes(name, len);
      return false;
    }
    return true;
  }

  void note(const char *text) { KPRINT("*** NOTE ?\n", text); }

  void summary() { KPRINT("*** SUMMARY failures=?\n", Dec(failures)); }
};

void demo_hr() {
  KPRINT("*** ============================================================\n");
}

void demo_section(const char *title) {
  KPRINT("***\n");
  demo_hr();
  KPRINT("***   ?\n", title);
  demo_hr();
}

void demo_status(const char *label, bool ok) {
  KPRINT("***   status   : ?\n", ok ? "OK" : "FAIL");
  if (!ok) {
    KPRINT("***   detail   : ?\n", label);
  }
}

void demo_mac_line(const char *label, const uint8_t mac[6]) {
  KPRINT("***   ? : ?:?:?:?:?:?\n", label, mac[0], mac[1], mac[2], mac[3],
         mac[4], mac[5]);
}

void demo_ip_line(const char *label, const uint8_t ip[4]) {
  KPRINT("***   ? : ?.?.?.?\n", label, Dec(ip[0]), Dec(ip[1]), Dec(ip[2]),
         Dec(ip[3]));
}

void udp_test_handler(const uint8_t src_ip[4], uint16_t src_port,
                      const uint8_t *payload, size_t payload_len) {
  g_udp_capture.called = true;
  copy_bytes_local(g_udp_capture.src_ip, src_ip, 4);
  g_udp_capture.src_port = src_port;
  g_udp_capture.payload_len =
      min_size(payload_len, sizeof(g_udp_capture.payload));
  copy_bytes_local(g_udp_capture.payload, payload, g_udp_capture.payload_len);
}

void demo_intro() {
  KPRINT("***\n");
  demo_hr();
  KPRINT("***   NETWORKING PROJECT DEMO\n");
  demo_hr();
  KPRINT("***   mode     : deterministic fake NIC backend\n");
  KPRINT("***   goal     : show request -> reply behavior clearly\n");
  KPRINT("***   guest MAC: 52:54:00:12:34:56\n");
  KPRINT("***   guest IP : 10.0.2.15\n");
  demo_hr();
}

void run_smoke_tests(Reporter &reporter) {
  init_frames();

  uint8_t out[64]{};

  reporter.check("smoke.ready_fake_boot", net_ready());

  net_shutdown_backend();
  reporter.check("smoke.ready_after_shutdown", !net_ready());

  net_init_fake();
  reporter.check("smoke.ready_after_reinit", net_ready());

  reporter.check("smoke.reject_null_tx", !net_send_raw(nullptr, 10));
  reporter.check("smoke.reject_zero_tx", !net_send_raw(g_frames.frame_a, 0));
  reporter.check("smoke.reject_oversized_tx",
                 !net_send_raw(g_frames.oversized,
                               VIRTIO_NET_MAX_FRAME_SIZE + 1));
  reporter.check_eq("smoke.reject_null_rx_buffer", net_recv_raw(nullptr, 64),
                    -1);
  reporter.check_eq("smoke.reject_zero_rx_max", net_recv_raw(out, 0), -1);

  reset_fake_backend();
}

void run_demo_tests(Reporter &reporter) {
  reset_fake_backend();

  uint8_t frame[128]{};
  uint8_t tx[VIRTIO_NET_MAX_FRAME_SIZE]{};

  demo_intro();
  demo_section("STEP 1: ARP RESOLUTION");
  KPRINT("***   incoming : host asks \"who has 10.0.2.15?\"\n");
  demo_mac_line("src MAC  ", k_proto_peer_mac);
  demo_ip_line("src IP   ", k_proto_peer_ip);
  demo_ip_line("target IP", k_proto_my_ip);

  build_arp_request(frame);
  const size_t arp_len = sizeof(EthernetHeader) + sizeof(ArpPacket);
  bool arp_ok = true;
  arp_ok &= reporter.quiet_check("demo.arp.inject",
                                 net_fake_inject_rx(frame, arp_len));
  arp_ok &= reporter.quiet_check("demo.arp.poll_once", net_poll_once());
  {
    size_t tx_len = sizeof(tx);
    arp_ok &= reporter.quiet_check("demo.arp.reply_captured",
                                   net_copy_last_tx_for_test(tx, &tx_len));
    arp_ok &= reporter.quiet_check_eq("demo.arp.reply_len", int(tx_len),
                                      int(arp_len));
    if (tx_len == arp_len) {
      auto *eth = (EthernetHeader *)tx;
      auto *arp = (ArpPacket *)(tx + sizeof(EthernetHeader));
      arp_ok &= reporter.quiet_check_bytes("demo.arp.reply_dst", eth->dst,
                                           k_proto_peer_mac, 6);
      arp_ok &= reporter.quiet_check_bytes("demo.arp.reply_src", eth->src,
                                           k_proto_my_mac, 6);
      arp_ok &= reporter.quiet_check("demo.arp.reply_op",
                                     be16(arp->oper) == ARP_OP_REPLY);
      arp_ok &= reporter.quiet_check_bytes("demo.arp.reply_spa", arp->spa,
                                           k_proto_my_ip, 4);
      arp_ok &= reporter.quiet_check_bytes("demo.arp.reply_tpa", arp->tpa,
                                           k_proto_peer_ip, 4);

      KPRINT("***   outgoing : guest sends ARP reply\n");
      demo_mac_line("dst MAC  ", eth->dst);
      demo_mac_line("src MAC  ", eth->src);
      demo_ip_line("sender IP", arp->spa);
      demo_ip_line("target IP", arp->tpa);
      KPRINT("***   message  : \"10.0.2.15 is at 52:54:00:12:34:56\"\n");
    }
  }
  demo_status("ARP transformation checks", arp_ok);

  reset_fake_backend();

  demo_section("STEP 2: ICMP ECHO / PING");
  KPRINT("***   incoming : host sends ICMP echo request\n");
  demo_ip_line("src IP   ", k_proto_peer_ip);
  demo_ip_line("dst IP   ", k_proto_my_ip);
  constexpr uint8_t k_demo_payload[] = {'h', 'e', 'l', 'l', 'o'};
  demo_payload_line("payload  ", k_demo_payload, sizeof(k_demo_payload));

  constexpr size_t k_demo_payload_len = sizeof(k_demo_payload);
  build_icmp_echo_request_payload(frame, k_demo_payload, k_demo_payload_len);
  const size_t request_len = sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                             sizeof(IcmpEchoHeader) + k_demo_payload_len;
  bool icmp_ok = true;
  icmp_ok &= reporter.quiet_check("demo.icmp.inject",
                                  net_fake_inject_rx(frame, request_len));
  icmp_ok &= reporter.quiet_check("demo.icmp.poll_once", net_poll_once());
  {
    size_t tx_len = sizeof(tx);
    icmp_ok &= reporter.quiet_check("demo.icmp.reply_captured",
                                    net_copy_last_tx_for_test(tx, &tx_len));
    icmp_ok &= reporter.quiet_check_eq("demo.icmp.reply_len", int(tx_len),
                                       int(request_len));
    if (tx_len == request_len) {
      auto *eth = (EthernetHeader *)tx;
      auto *ip = (Ipv4Header *)(tx + sizeof(EthernetHeader));
      auto *icmp = (IcmpEchoHeader *)(tx + sizeof(EthernetHeader) +
                                      sizeof(Ipv4Header));
      const uint8_t *payload = tx + sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                               sizeof(IcmpEchoHeader);
      icmp_ok &= reporter.quiet_check_bytes("demo.icmp.reply_dst", eth->dst,
                                            k_proto_peer_mac, 6);
      icmp_ok &= reporter.quiet_check_bytes("demo.icmp.reply_src", eth->src,
                                            k_proto_my_mac, 6);
      icmp_ok &= reporter.quiet_check_bytes("demo.icmp.reply_ip_src", ip->src_ip,
                                            k_proto_my_ip, 4);
      icmp_ok &= reporter.quiet_check_bytes("demo.icmp.reply_ip_dst", ip->dst_ip,
                                            k_proto_peer_ip, 4);
      icmp_ok &= reporter.quiet_check("demo.icmp.reply_type",
                                      icmp->type == ICMP_ECHO_REPLY);
      icmp_ok &= reporter.quiet_check("demo.icmp.reply_identifier",
                                      icmp->identifier == be16(0x4444));
      icmp_ok &= reporter.quiet_check("demo.icmp.reply_sequence",
                                      icmp->sequence == be16(7));
      icmp_ok &= reporter.quiet_check_bytes("demo.icmp.reply_payload", payload,
                                            k_demo_payload, k_demo_payload_len);
      icmp_ok &= reporter.quiet_check(
          "demo.icmp.reply_ip_checksum",
          checksum16((const uint8_t *)ip, sizeof(Ipv4Header)) == 0);
      icmp_ok &= reporter.quiet_check(
          "demo.icmp.reply_checksum",
          checksum16((const uint8_t *)icmp,
                     sizeof(IcmpEchoHeader) + k_demo_payload_len) == 0);

      KPRINT("***   outgoing : guest sends ICMP echo reply\n");
      demo_mac_line("dst MAC  ", eth->dst);
      demo_mac_line("src MAC  ", eth->src);
      demo_ip_line("src IP   ", ip->src_ip);
      demo_ip_line("dst IP   ", ip->dst_ip);
      KPRINT("***   icmp     : type=0 reply, id=0x4444, seq=7\n");
      demo_payload_line("payload  ", payload, k_demo_payload_len);
      KPRINT("***   message  : receiver saw the text and echoed it back\n");
    }
  }
  demo_status("ICMP transformation checks", icmp_ok);

  demo_section("DEMO RESULT");
  KPRINT("***   result   : ?\n", (arp_ok && icmp_ok) ? "request/reply pipeline works"
                                                 : "demo detected an error");
  KPRINT("***   next step: use net_live for a real host-backed NIC demo\n");

  reset_fake_backend();
}

void run_tx_tests(Reporter &reporter) {
  init_frames();
  reset_fake_backend();

  reporter.check("tx.send_one_valid_frame",
                 net_send_raw(g_frames.frame_a, sizeof(g_frames.frame_a)));

  bool multi_send_ok = true;
  for (size_t i = 0; i < 5; ++i) {
    const uint8_t *frame = (i % 2 == 0) ? g_frames.frame_a : g_frames.frame_b;
    if (!net_send_raw(frame, sizeof(g_frames.frame_a))) {
      multi_send_ok = false;
    }
  }
  reporter.check("tx.send_multiple_back_to_back", multi_send_ok);

  reporter.check("tx.send_max_frame",
                 net_send_raw(g_frames.frame_c, VIRTIO_NET_MAX_FRAME_SIZE));

  net_shutdown_backend();
  reporter.check("tx.reject_send_when_backend_disabled",
                 !net_send_raw(g_frames.frame_a, sizeof(g_frames.frame_a)));

  net_init_fake();

  bool burst_ok = true;
  for (size_t i = 0; i < k_tx_burst_count; ++i) {
    const uint8_t *frame = (i % 2 == 0) ? g_frames.frame_a : g_frames.frame_b;
    if (!net_send_raw(frame, sizeof(g_frames.frame_a))) {
      burst_ok = false;
    }
  }
  reporter.check("tx.reclaims_after_many_sends", burst_ok);

  reset_fake_backend();
}

void run_rx_tests(Reporter &reporter) {
  init_frames();
  reset_fake_backend();

  uint8_t out[VIRTIO_NET_MAX_FRAME_SIZE]{};

  reporter.check_eq("rx.empty_returns_zero", net_recv_raw(out, sizeof(out)), 0);

  reporter.check("rx.inject_one_frame",
                 net_fake_inject_rx(g_frames.frame_b, sizeof(g_frames.frame_b)));
  int recv_len = net_recv_raw(out, 128);
  reporter.check_eq("rx.receive_one_frame_len", recv_len,
                    int(sizeof(g_frames.frame_b)));
  if (recv_len == int(sizeof(g_frames.frame_b))) {
    reporter.check_bytes("rx.receive_one_frame_bytes", out, g_frames.frame_b,
                         sizeof(g_frames.frame_b));
  }

  reset_fake_backend();

  reporter.check("rx.inject_exact_size_frame",
                 net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a)));
  recv_len = net_recv_raw(out, sizeof(g_frames.frame_a));
  reporter.check_eq("rx.receive_exact_size_len", recv_len,
                    int(sizeof(g_frames.frame_a)));
  if (recv_len == int(sizeof(g_frames.frame_a))) {
    reporter.check_bytes("rx.receive_exact_size_bytes", out, g_frames.frame_a,
                         sizeof(g_frames.frame_a));
  }

  reset_fake_backend();

  reporter.check("rx.inject_small_buffer_frame",
                 net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a)));
  reporter.check_eq("rx.reject_small_output_buffer", net_recv_raw(out, 20), -1);

  reset_fake_backend();

  reporter.check("rx.inject_max_frame",
                 net_fake_inject_rx(g_frames.frame_c, VIRTIO_NET_MAX_FRAME_SIZE));
  recv_len = net_recv_raw(out, VIRTIO_NET_MAX_FRAME_SIZE);
  reporter.check_eq("rx.receive_max_frame_len", recv_len,
                    int(VIRTIO_NET_MAX_FRAME_SIZE));
  if (recv_len == int(VIRTIO_NET_MAX_FRAME_SIZE)) {
    reporter.check_bytes("rx.receive_max_frame_bytes", out, g_frames.frame_c,
                         VIRTIO_NET_MAX_FRAME_SIZE);
  }

  reporter.check("rx.reject_null_inject", !net_fake_inject_rx(nullptr, 10));
  reporter.check("rx.reject_zero_inject",
                 !net_fake_inject_rx(g_frames.frame_a, 0));
  reporter.check("rx.reject_oversized_inject",
                 !net_fake_inject_rx(g_frames.oversized,
                                     VIRTIO_NET_MAX_FRAME_SIZE + 1));

  reset_fake_backend();
}

void run_queue_tests(Reporter &reporter) {
  init_frames();
  uint8_t out[VIRTIO_NET_MAX_FRAME_SIZE]{};

  reset_fake_backend();

  reporter.check("queue.recycle_inject_first",
                 net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a)));
  int recv_len = net_recv_raw(out, sizeof(out));
  reporter.check_eq("queue.recycle_receive_first_len", recv_len,
                    int(sizeof(g_frames.frame_a)));
  if (recv_len == int(sizeof(g_frames.frame_a))) {
    reporter.check_bytes("queue.recycle_receive_first_bytes", out,
                         g_frames.frame_a, sizeof(g_frames.frame_a));
  }

  reporter.check("queue.recycle_inject_second",
                 net_fake_inject_rx(g_frames.frame_b, sizeof(g_frames.frame_b)));
  recv_len = net_recv_raw(out, sizeof(out));
  reporter.check_eq("queue.recycle_receive_second_len", recv_len,
                    int(sizeof(g_frames.frame_b)));
  if (recv_len == int(sizeof(g_frames.frame_b))) {
    reporter.check_bytes("queue.recycle_receive_second_bytes", out,
                         g_frames.frame_b, sizeof(g_frames.frame_b));
  }

  reset_fake_backend();

  reporter.check("queue.order_inject_a",
                 net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a)));
  reporter.check("queue.order_inject_b",
                 net_fake_inject_rx(g_frames.frame_b, sizeof(g_frames.frame_b)));

  recv_len = net_recv_raw(out, sizeof(out));
  reporter.check_eq("queue.order_receive_a_len", recv_len,
                    int(sizeof(g_frames.frame_a)));
  if (recv_len == int(sizeof(g_frames.frame_a))) {
    reporter.check_bytes("queue.order_receive_a_bytes", out, g_frames.frame_a,
                         sizeof(g_frames.frame_a));
  }

  recv_len = net_recv_raw(out, sizeof(out));
  reporter.check_eq("queue.order_receive_b_len", recv_len,
                    int(sizeof(g_frames.frame_b)));
  if (recv_len == int(sizeof(g_frames.frame_b))) {
    reporter.check_bytes("queue.order_receive_b_bytes", out, g_frames.frame_b,
                         sizeof(g_frames.frame_b));
  }

  reporter.check_eq("queue.empty_after_all_consumed", net_recv_raw(out, sizeof(out)),
                    0);

  reset_fake_backend();

  bool fill_ok = true;
  for (size_t i = 0; i < VIRTIO_NET_QUEUE_SIZE; ++i) {
    const uint8_t *frame = (i % 2 == 0) ? g_frames.frame_a : g_frames.frame_b;
    if (!net_fake_inject_rx(frame, sizeof(g_frames.frame_a))) {
      fill_ok = false;
    }
  }
  reporter.check("queue.fill_ring_to_capacity", fill_ok);
  reporter.check("queue.reject_extra_when_full",
                 !net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a)));

  recv_len = net_recv_raw(out, sizeof(out));
  reporter.check_eq("queue.receive_after_full_len", recv_len,
                    int(sizeof(g_frames.frame_a)));
  if (recv_len == int(sizeof(g_frames.frame_a))) {
    reporter.check_bytes("queue.receive_after_full_bytes", out,
                         g_frames.frame_a, sizeof(g_frames.frame_a));
  }

  reporter.check("queue.recovers_after_full",
                 net_fake_inject_rx(g_frames.frame_b, sizeof(g_frames.frame_b)));

  reset_fake_backend();

  bool stress_ok = true;
  for (size_t i = 0; i < k_rx_stress_loops; ++i) {
    if (!net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a))) {
      stress_ok = false;
      break;
    }
    recv_len = net_recv_raw(out, sizeof(out));
    if (recv_len != int(sizeof(g_frames.frame_a)) ||
        !bytes_equal(out, g_frames.frame_a, sizeof(g_frames.frame_a))) {
      stress_ok = false;
      break;
    }
  }
  reporter.check("queue.stress_reuse", stress_ok);

  reset_fake_backend();
}

void run_debug_tests(Reporter &reporter) {
  init_frames();
  reset_fake_backend();

  uint8_t out[VIRTIO_NET_MAX_FRAME_SIZE]{};

  reporter.check("debug.send_large_frame",
                 net_send_raw(g_frames.frame_c, k_debug_frame_len));
  reporter.check("debug.inject_large_frame",
                 net_fake_inject_rx(g_frames.frame_c, k_debug_frame_len));

  const int recv_len = net_recv_raw(out, sizeof(out));
  reporter.check_eq("debug.receive_large_frame_len", recv_len,
                    int(k_debug_frame_len));
  if (recv_len == int(k_debug_frame_len)) {
    reporter.check_bytes("debug.receive_large_frame_bytes", out,
                         g_frames.frame_c, k_debug_frame_len);
  }

  reporter.note(
      "inspect raw for net: tx enqueue, net: fake tx, net: fake rx inject, "
      "net: rx dequeue, and ... 16 more bytes");

  reset_fake_backend();
}

void run_stats_tests(Reporter &reporter) {
  reset_fake_backend();
  net_stats_reset();

  uint8_t frame[128]{};
  uint8_t tx[VIRTIO_NET_MAX_FRAME_SIZE]{};

  NetStats stats = net_stats_snapshot();
  reporter.check_eq("stats.initial_raw_rx", int(stats.raw_rx), 0);
  reporter.check_eq("stats.initial_raw_tx", int(stats.raw_tx), 0);
  reporter.check_eq("stats.initial_drops", int(stats.dropped_short), 0);

  build_arp_request(frame);
  const size_t arp_len = sizeof(EthernetHeader) + sizeof(ArpPacket);
  reporter.check("stats.arp.inject", net_fake_inject_rx(frame, arp_len));
  reporter.check("stats.arp.poll_once", net_poll_once());
  {
    size_t tx_len = sizeof(tx);
    reporter.check("stats.arp.reply_captured",
                   net_copy_last_tx_for_test(tx, &tx_len));
  }

  stats = net_stats_snapshot();
  reporter.check_eq("stats.after_arp_raw_rx", int(stats.raw_rx), 1);
  reporter.check_eq("stats.after_arp_raw_tx", int(stats.raw_tx), 1);
  reporter.check_eq("stats.after_arp_arp_rx", int(stats.arp_rx), 1);
  reporter.check_eq("stats.after_arp_arp_tx", int(stats.arp_tx), 1);
  reporter.check_eq("stats.after_arp_ipv4_rx", int(stats.ipv4_rx), 0);

  constexpr size_t k_payload_len = 5;
  build_icmp_echo_request(frame, k_payload_len);
  const size_t request_len = sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                             sizeof(IcmpEchoHeader) + k_payload_len;
  reporter.check("stats.icmp.inject", net_fake_inject_rx(frame, request_len));
  reporter.check("stats.icmp.poll_once", net_poll_once());
  {
    size_t tx_len = sizeof(tx);
    reporter.check("stats.icmp.reply_captured",
                   net_copy_last_tx_for_test(tx, &tx_len));
  }

  stats = net_stats_snapshot();
  reporter.check_eq("stats.after_icmp_raw_rx", int(stats.raw_rx), 2);
  reporter.check_eq("stats.after_icmp_raw_tx", int(stats.raw_tx), 2);
  reporter.check_eq("stats.after_icmp_ipv4_rx", int(stats.ipv4_rx), 1);
  reporter.check_eq("stats.after_icmp_ipv4_tx", int(stats.ipv4_tx), 1);
  reporter.check_eq("stats.after_icmp_icmp_rx", int(stats.icmp_rx), 1);
  reporter.check_eq("stats.after_icmp_icmp_tx", int(stats.icmp_tx), 1);

  reporter.check("stats.short.inject", net_fake_inject_rx(frame, 6));
  reporter.check("stats.short.poll_once", net_poll_once());
  stats = net_stats_snapshot();
  reporter.check_eq("stats.after_short_raw_rx", int(stats.raw_rx), 3);
  reporter.check_eq("stats.after_short_drop", int(stats.dropped_short), 1);

  build_icmp_echo_request(frame, k_payload_len);
  auto *ip = (Ipv4Header *)(frame + sizeof(EthernetHeader));
  ip->header_checksum = uint16_t(ip->header_checksum ^ 0xffffU);
  reporter.check("stats.bad_checksum.inject",
                 net_fake_inject_rx(frame, request_len));
  reporter.check("stats.bad_checksum.poll_once", net_poll_once());
  stats = net_stats_snapshot();
  reporter.check_eq("stats.after_bad_checksum_raw_rx", int(stats.raw_rx), 4);
  reporter.check_eq("stats.after_bad_checksum_drop",
                    int(stats.dropped_bad_checksum), 1);
  reporter.check_eq("stats.after_bad_checksum_icmp_rx", int(stats.icmp_rx), 1);

  build_icmp_echo_request(frame, k_payload_len);
  auto *eth = (EthernetHeader *)frame;
  eth->dst[5] ^= 0x55;
  reporter.check("stats.not_for_me.inject",
                 net_fake_inject_rx(frame, request_len));
  reporter.check("stats.not_for_me.poll_once", net_poll_once());
  stats = net_stats_snapshot();
  reporter.check_eq("stats.after_not_for_me_raw_rx", int(stats.raw_rx), 5);
  reporter.check_eq("stats.after_not_for_me_drop",
                    int(stats.dropped_not_for_me), 1);

  build_icmp_echo_request(frame, k_payload_len);
  eth = (EthernetHeader *)frame;
  eth->ether_type = be16(0x88b5);
  reporter.check("stats.unknown_ethertype.inject",
                 net_fake_inject_rx(frame, request_len));
  reporter.check("stats.unknown_ethertype.poll_once", net_poll_once());
  stats = net_stats_snapshot();
  reporter.check_eq("stats.after_unknown_ethertype_raw_rx", int(stats.raw_rx),
                    6);
  reporter.check_eq("stats.after_unknown_ethertype_drop",
                    int(stats.dropped_unknown_ethertype), 1);

  build_icmp_echo_request(frame, k_payload_len);
  ip = (Ipv4Header *)(frame + sizeof(EthernetHeader));
  ip->protocol = 99;
  ip->header_checksum = 0;
  ip->header_checksum =
      be16(checksum16((const uint8_t *)ip, sizeof(Ipv4Header)));
  reporter.check("stats.unknown_ipv4.inject",
                 net_fake_inject_rx(frame, request_len));
  reporter.check("stats.unknown_ipv4.poll_once", net_poll_once());
  stats = net_stats_snapshot();
  reporter.check_eq("stats.after_unknown_ipv4_raw_rx", int(stats.raw_rx), 7);
  reporter.check_eq("stats.after_unknown_ipv4_rx", int(stats.ipv4_rx), 2);
  reporter.check_eq("stats.after_unknown_ipv4_drop",
                    int(stats.dropped_unknown_ipv4_protocol), 1);
  reporter.check_eq("stats.final_udp_rx", int(stats.udp_rx), 0);
  reporter.check_eq("stats.final_udp_tx", int(stats.udp_tx), 0);

  net_stats_print();
  reset_fake_backend();
}

void run_arp_cache_tests(Reporter &reporter) {
  reset_fake_backend();
  net_stats_reset();
  arp_cache_reset();
  net_set_identity(k_proto_my_mac, k_proto_my_ip);

  uint8_t frame[128]{};
  uint8_t tx[VIRTIO_NET_MAX_FRAME_SIZE]{};
  uint8_t mac[6]{};

  reporter.check("arp_cache.initial_miss",
                 !arp_cache_lookup(k_proto_peer_ip, mac));

  build_arp_request(frame);
  const size_t arp_len = sizeof(EthernetHeader) + sizeof(ArpPacket);
  reporter.check("arp_cache.learn_request.inject",
                 net_fake_inject_rx(frame, arp_len));
  reporter.check("arp_cache.learn_request.poll_once", net_poll_once());
  reporter.check("arp_cache.learn_request.lookup",
                 arp_cache_lookup(k_proto_peer_ip, mac));
  reporter.check_bytes("arp_cache.learn_request.mac", mac, k_proto_peer_mac, 6);

  arp_cache_reset();
  build_arp_reply_frame(frame, k_proto_peer_mac, k_proto_peer_ip, k_proto_my_mac,
                        k_proto_my_ip);
  reporter.check("arp_cache.learn_reply.inject",
                 net_fake_inject_rx(frame, arp_len));
  reporter.check("arp_cache.learn_reply.poll_once", net_poll_once());
  reporter.check("arp_cache.learn_reply.lookup",
                 arp_cache_lookup(k_proto_peer_ip, mac));
  reporter.check_bytes("arp_cache.learn_reply.mac", mac, k_proto_peer_mac, 6);

  arp_cache_reset();
  for (uint8_t i = 0; i < ARP_CACHE_CAPACITY; ++i) {
    uint8_t ip[4] = {10, 0, 2, uint8_t(30 + i)};
    uint8_t entry_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x10, i};
    arp_cache_insert(ip, entry_mac);
  }
  {
    uint8_t overflow_ip[4] = {10, 0, 2, 99};
    uint8_t overflow_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x20, 0x99};
    uint8_t first_ip[4] = {10, 0, 2, 30};
    uint8_t second_ip[4] = {10, 0, 2, 31};

    arp_cache_insert(overflow_ip, overflow_mac);
    reporter.check("arp_cache.evict_oldest", !arp_cache_lookup(first_ip, mac));
    reporter.check("arp_cache.keep_newer", arp_cache_lookup(second_ip, mac));
    reporter.check("arp_cache.insert_after_evict",
                   arp_cache_lookup(overflow_ip, mac));
    reporter.check_bytes("arp_cache.evicted_slot_mac", mac, overflow_mac, 6);
  }

  reset_fake_backend();
  arp_cache_reset();
  uint8_t payload[4] = {'p', 'i', 'n', 'g'};
  reporter.check("arp_cache.ipv4_miss_returns_pending",
                 !net_send_ipv4(k_proto_peer_ip, 99, payload, sizeof(payload)));
  {
    size_t tx_len = sizeof(tx);
    reporter.check("arp_cache.ipv4_miss_arp_tx",
                   net_copy_last_tx_for_test(tx, &tx_len));
    reporter.check_eq("arp_cache.ipv4_miss_arp_len", int(tx_len),
                      int(arp_len));
    if (tx_len == arp_len) {
      auto *eth = (EthernetHeader *)tx;
      auto *arp = (ArpPacket *)(tx + sizeof(EthernetHeader));
      uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
      uint8_t zero_mac[6] = {};
      reporter.check_bytes("arp_cache.ipv4_miss_eth_dst", eth->dst,
                           broadcast, 6);
      reporter.check_bytes("arp_cache.ipv4_miss_eth_src", eth->src,
                           k_proto_my_mac, 6);
      reporter.check("arp_cache.ipv4_miss_op",
                     be16(arp->oper) == ARP_OP_REQUEST);
      reporter.check_bytes("arp_cache.ipv4_miss_sha", arp->sha,
                           k_proto_my_mac, 6);
      reporter.check_bytes("arp_cache.ipv4_miss_spa", arp->spa,
                           k_proto_my_ip, 4);
      reporter.check_bytes("arp_cache.ipv4_miss_tha", arp->tha, zero_mac, 6);
      reporter.check_bytes("arp_cache.ipv4_miss_tpa", arp->tpa,
                           k_proto_peer_ip, 4);
    }
  }

  arp_cache_insert(k_proto_peer_ip, k_proto_peer_mac);
  reporter.check("arp_cache.ipv4_hit_sends",
                 net_send_ipv4(k_proto_peer_ip, 99, payload, sizeof(payload)));
  {
    size_t tx_len = sizeof(tx);
    reporter.check("arp_cache.ipv4_hit_tx",
                   net_copy_last_tx_for_test(tx, &tx_len));
    reporter.check_eq("arp_cache.ipv4_hit_len", int(tx_len),
                      int(sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                          sizeof(payload)));
    if (tx_len == sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                      sizeof(payload)) {
      auto *eth = (EthernetHeader *)tx;
      auto *ip = (Ipv4Header *)(tx + sizeof(EthernetHeader));
      const uint8_t *out_payload =
          tx + sizeof(EthernetHeader) + sizeof(Ipv4Header);
      reporter.check_bytes("arp_cache.ipv4_hit_eth_dst", eth->dst,
                           k_proto_peer_mac, 6);
      reporter.check_bytes("arp_cache.ipv4_hit_eth_src", eth->src,
                           k_proto_my_mac, 6);
      reporter.check("arp_cache.ipv4_hit_eth_type",
                     be16(eth->ether_type) == ETH_TYPE_IPV4);
      reporter.check_bytes("arp_cache.ipv4_hit_src_ip", ip->src_ip,
                           k_proto_my_ip, 4);
      reporter.check_bytes("arp_cache.ipv4_hit_dst_ip", ip->dst_ip,
                           k_proto_peer_ip, 4);
      reporter.check("arp_cache.ipv4_hit_protocol", ip->protocol == 99);
      reporter.check("arp_cache.ipv4_hit_checksum",
                     checksum16((const uint8_t *)ip, sizeof(Ipv4Header)) == 0);
      reporter.check_bytes("arp_cache.ipv4_hit_payload", out_payload, payload,
                           sizeof(payload));
    }
  }

  NetStats stats = net_stats_snapshot();
  reporter.check_eq("arp_cache.stats_arp_tx", int(stats.arp_tx), 2);
  reporter.check_eq("arp_cache.stats_ipv4_tx", int(stats.ipv4_tx), 1);

  arp_cache_print();
  reset_fake_backend();
  arp_cache_reset();
}

void run_udp_tests(Reporter &reporter) {
  reset_fake_backend();
  net_stats_reset();
  arp_cache_reset();
  udp_clear_handlers();
  net_set_identity(k_proto_my_mac, k_proto_my_ip);
  g_udp_capture = {};

  constexpr uint16_t k_listen_port = 4390;
  constexpr uint16_t k_peer_port = 12000;
  constexpr uint8_t k_payload[] = {'h', 'e', 'l', 'l', 'o'};
  uint8_t frame[128]{};
  uint8_t tx[VIRTIO_NET_MAX_FRAME_SIZE]{};

  reporter.check("udp.register_handler",
                 udp_register_handler(k_listen_port, udp_test_handler));

  build_udp_packet(frame, k_peer_port, k_listen_port, k_payload,
                   sizeof(k_payload));
  const size_t udp_frame_len = sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                               sizeof(UdpHeader) + sizeof(k_payload);
  reporter.check("udp.rx_valid.inject",
                 net_fake_inject_rx(frame, udp_frame_len));
  reporter.check("udp.rx_valid.poll_once", net_poll_once());
  reporter.check("udp.rx_valid.handler_called", g_udp_capture.called);
  reporter.check_bytes("udp.rx_valid.src_ip", g_udp_capture.src_ip,
                       k_proto_peer_ip, 4);
  reporter.check_eq("udp.rx_valid.src_port", int(g_udp_capture.src_port),
                    int(k_peer_port));
  reporter.check_eq("udp.rx_valid.payload_len", int(g_udp_capture.payload_len),
                    int(sizeof(k_payload)));
  reporter.check_bytes("udp.rx_valid.payload", g_udp_capture.payload,
                       k_payload, sizeof(k_payload));

  g_udp_capture = {};
  build_udp_packet(frame, k_peer_port, uint16_t(k_listen_port + 1), k_payload,
                   sizeof(k_payload));
  reporter.check("udp.wrong_port.inject",
                 net_fake_inject_rx(frame, udp_frame_len));
  reporter.check("udp.wrong_port.poll_once", net_poll_once());
  reporter.check("udp.wrong_port.ignored", !g_udp_capture.called);

  build_udp_packet(frame, k_peer_port, k_listen_port, k_payload,
                   sizeof(k_payload));
  auto *ip = (Ipv4Header *)(frame + sizeof(EthernetHeader));
  ip->total_length = be16(uint16_t(sizeof(Ipv4Header) + 4));
  ip->header_checksum = 0;
  ip->header_checksum =
      be16(checksum16((const uint8_t *)ip, sizeof(Ipv4Header)));
  reporter.check("udp.short.inject",
                 net_fake_inject_rx(frame, sizeof(EthernetHeader) +
                                               sizeof(Ipv4Header) + 4));
  reporter.check("udp.short.poll_once", net_poll_once());

  arp_cache_reset();
  reporter.check("udp.tx_miss_returns_pending",
                 !udp_send_to(k_proto_peer_ip, k_listen_port, k_peer_port,
                              k_payload, sizeof(k_payload)));
  {
    size_t tx_len = sizeof(tx);
    reporter.check("udp.tx_miss_arp_tx",
                   net_copy_last_tx_for_test(tx, &tx_len));
    reporter.check_eq("udp.tx_miss_arp_len", int(tx_len),
                      int(sizeof(EthernetHeader) + sizeof(ArpPacket)));
    if (tx_len == sizeof(EthernetHeader) + sizeof(ArpPacket)) {
      auto *eth = (EthernetHeader *)tx;
      auto *arp = (ArpPacket *)(tx + sizeof(EthernetHeader));
      uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
      reporter.check_bytes("udp.tx_miss_eth_dst", eth->dst, broadcast, 6);
      reporter.check("udp.tx_miss_arp_op", be16(arp->oper) == ARP_OP_REQUEST);
      reporter.check_bytes("udp.tx_miss_tpa", arp->tpa, k_proto_peer_ip, 4);
    }
  }

  arp_cache_insert(k_proto_peer_ip, k_proto_peer_mac);
  reporter.check("udp.tx_hit_sends",
                 udp_send_to(k_proto_peer_ip, k_listen_port, k_peer_port,
                             k_payload, sizeof(k_payload)));
  {
    size_t tx_len = sizeof(tx);
    reporter.check("udp.tx_hit_frame", net_copy_last_tx_for_test(tx, &tx_len));
    reporter.check_eq("udp.tx_hit_len", int(tx_len), int(udp_frame_len));
    if (tx_len == udp_frame_len) {
      auto *eth = (EthernetHeader *)tx;
      auto *out_ip = (Ipv4Header *)(tx + sizeof(EthernetHeader));
      auto *udp =
          (UdpHeader *)(tx + sizeof(EthernetHeader) + sizeof(Ipv4Header));
      const uint8_t *out_payload =
          tx + sizeof(EthernetHeader) + sizeof(Ipv4Header) + sizeof(UdpHeader);
      reporter.check_bytes("udp.tx_hit_eth_dst", eth->dst, k_proto_peer_mac,
                           6);
      reporter.check_bytes("udp.tx_hit_eth_src", eth->src, k_proto_my_mac, 6);
      reporter.check("udp.tx_hit_ip_protocol",
                     out_ip->protocol == IPV4_PROTO_UDP);
      reporter.check("udp.tx_hit_ip_checksum",
                     checksum16((const uint8_t *)out_ip,
                                sizeof(Ipv4Header)) == 0);
      reporter.check_eq("udp.tx_hit_src_port", int(be16(udp->src_port)),
                        int(k_listen_port));
      reporter.check_eq("udp.tx_hit_dst_port", int(be16(udp->dst_port)),
                        int(k_peer_port));
      reporter.check_eq("udp.tx_hit_udp_len", int(be16(udp->length)),
                        int(sizeof(UdpHeader) + sizeof(k_payload)));
      reporter.check_eq("udp.tx_hit_checksum_zero", int(udp->checksum), 0);
      reporter.check_bytes("udp.tx_hit_payload", out_payload, k_payload,
                           sizeof(k_payload));
    }
  }

  NetStats stats = net_stats_snapshot();
  reporter.check_eq("udp.stats_udp_rx", int(stats.udp_rx), 1);
  reporter.check_eq("udp.stats_udp_tx", int(stats.udp_tx), 1);
  reporter.check_eq("udp.stats_arp_tx", int(stats.arp_tx), 1);
  reporter.check_eq("udp.stats_ipv4_tx", int(stats.ipv4_tx), 1);
  reporter.check_eq("udp.stats_short_drop", int(stats.dropped_short), 1);

  udp_clear_handlers();
  reset_fake_backend();
  arp_cache_reset();
}

void run_chat_tests(Reporter &reporter) {
  reset_fake_backend();
  net_stats_reset();
  arp_cache_reset();
  udp_clear_handlers();
  net_chat_reset();
  net_set_identity(k_proto_my_mac, k_proto_my_ip);

  uint8_t frame[256]{};
  uint8_t tx[VIRTIO_NET_MAX_FRAME_SIZE]{};
  char out[160]{};
  constexpr uint8_t k_hello[] = {'h', 'e', 'l', 'l', 'o'};

  reporter.check("chat.init", net_chat_init());

  build_udp_packet(frame, 12000, NET_CHAT_PORT, k_hello, sizeof(k_hello));
  const size_t hello_frame_len = sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                                 sizeof(UdpHeader) + sizeof(k_hello);
  reporter.check("chat.rx_hello.inject",
                 net_fake_inject_rx(frame, hello_frame_len));
  reporter.check("chat.rx_hello.poll", net_chat_poll());
  reporter.check("chat.rx_hello.recv", net_chat_recv(out, sizeof(out)));
  reporter.check("chat.rx_hello.text", strings_equal(out, "hello"));
  net_chat_print_history();

  constexpr uint8_t k_dirty[] = {'o', 'k', 1, '!'};
  build_udp_packet(frame, 12000, NET_CHAT_PORT, k_dirty, sizeof(k_dirty));
  reporter.check("chat.sanitize.inject",
                 net_fake_inject_rx(frame, sizeof(EthernetHeader) +
                                               sizeof(Ipv4Header) +
                                               sizeof(UdpHeader) +
                                               sizeof(k_dirty)));
  reporter.check("chat.sanitize.poll", net_chat_poll());
  reporter.check("chat.sanitize.recv", net_chat_recv(out, sizeof(out)));
  reporter.check("chat.sanitize.text", strings_equal(out, "ok.!"));

  uint8_t long_payload[NET_CHAT_TEXT_SIZE + 16]{};
  for (size_t i = 0; i < sizeof(long_payload); ++i) {
    long_payload[i] = 'A';
  }
  build_udp_packet(frame, 12000, NET_CHAT_PORT, long_payload,
                   sizeof(long_payload));
  reporter.check("chat.truncate.inject",
                 net_fake_inject_rx(frame, sizeof(EthernetHeader) +
                                               sizeof(Ipv4Header) +
                                               sizeof(UdpHeader) +
                                               sizeof(long_payload)));
  reporter.check("chat.truncate.poll", net_chat_poll());
  reporter.check("chat.truncate.recv", net_chat_recv(out, sizeof(out)));
  bool truncated_ok = out[NET_CHAT_TEXT_SIZE - 1] == 0;
  for (size_t i = 0; i < NET_CHAT_TEXT_SIZE - 1; ++i) {
    if (out[i] != 'A') {
      truncated_ok = false;
      break;
    }
  }
  reporter.check("chat.truncate.text", truncated_ok);

  arp_cache_reset();
  reporter.check("chat.send_miss_returns_pending",
                 !net_chat_send(k_proto_peer_ip, "hello"));
  {
    size_t tx_len = sizeof(tx);
    reporter.check("chat.send_miss_arp_tx",
                   net_copy_last_tx_for_test(tx, &tx_len));
    reporter.check_eq("chat.send_miss_arp_len", int(tx_len),
                      int(sizeof(EthernetHeader) + sizeof(ArpPacket)));
    if (tx_len == sizeof(EthernetHeader) + sizeof(ArpPacket)) {
      auto *eth = (EthernetHeader *)tx;
      auto *arp = (ArpPacket *)(tx + sizeof(EthernetHeader));
      uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
      reporter.check_bytes("chat.send_miss_eth_dst", eth->dst, broadcast, 6);
      reporter.check("chat.send_miss_arp_op",
                     be16(arp->oper) == ARP_OP_REQUEST);
      reporter.check_bytes("chat.send_miss_tpa", arp->tpa, k_proto_peer_ip, 4);
    }
  }

  arp_cache_insert(k_proto_peer_ip, k_proto_peer_mac);
  reporter.check("chat.send_hit_sends", net_chat_send(k_proto_peer_ip, "hello"));
  {
    size_t tx_len = sizeof(tx);
    reporter.check("chat.send_hit_frame",
                   net_copy_last_tx_for_test(tx, &tx_len));
    reporter.check_eq("chat.send_hit_len", int(tx_len),
                      int(hello_frame_len));
    if (tx_len == hello_frame_len) {
      auto *eth = (EthernetHeader *)tx;
      auto *ip = (Ipv4Header *)(tx + sizeof(EthernetHeader));
      auto *udp =
          (UdpHeader *)(tx + sizeof(EthernetHeader) + sizeof(Ipv4Header));
      const uint8_t *payload =
          tx + sizeof(EthernetHeader) + sizeof(Ipv4Header) + sizeof(UdpHeader);
      reporter.check_bytes("chat.send_hit_eth_dst", eth->dst, k_proto_peer_mac,
                           6);
      reporter.check("chat.send_hit_ip_protocol", ip->protocol == IPV4_PROTO_UDP);
      reporter.check_eq("chat.send_hit_src_port", int(be16(udp->src_port)),
                        int(NET_CHAT_PORT));
      reporter.check_eq("chat.send_hit_dst_port", int(be16(udp->dst_port)),
                        int(NET_CHAT_PORT));
      reporter.check_eq("chat.send_hit_udp_len", int(be16(udp->length)),
                        int(sizeof(UdpHeader) + sizeof(k_hello)));
      reporter.check_bytes("chat.send_hit_payload", payload, k_hello,
                           sizeof(k_hello));
    }
  }

  NetStats stats = net_stats_snapshot();
  reporter.check_eq("chat.stats_udp_rx", int(stats.udp_rx), 3);
  reporter.check_eq("chat.stats_udp_tx", int(stats.udp_tx), 1);
  reporter.check_eq("chat.stats_arp_tx", int(stats.arp_tx), 1);
  reporter.check_eq("chat.stats_ipv4_tx", int(stats.ipv4_tx), 1);

  net_chat_print_history();
  udp_clear_handlers();
  net_chat_reset();
  reset_fake_backend();
  arp_cache_reset();
}

void run_proto_tests(Reporter &reporter) {
  reset_fake_backend();

  uint8_t frame[128]{};
  uint8_t tx[VIRTIO_NET_MAX_FRAME_SIZE]{};

  build_arp_request(frame);
  reporter.check("proto.arp.inject",
                 net_fake_inject_rx(frame,
                                    sizeof(EthernetHeader) + sizeof(ArpPacket)));
  reporter.check("proto.arp.poll_once", net_poll_once());
  {
    size_t tx_len = sizeof(tx);
    reporter.check("proto.arp.reply_captured",
                   net_copy_last_tx_for_test(tx, &tx_len));
    reporter.check_eq("proto.arp.reply_len", int(tx_len),
                      int(sizeof(EthernetHeader) + sizeof(ArpPacket)));
    if (tx_len == sizeof(EthernetHeader) + sizeof(ArpPacket)) {
      auto *eth = (EthernetHeader *)tx;
      auto *arp = (ArpPacket *)(tx + sizeof(EthernetHeader));
      reporter.check_bytes("proto.arp.reply_dst", eth->dst, k_proto_peer_mac, 6);
      reporter.check_bytes("proto.arp.reply_src", eth->src, k_proto_my_mac, 6);
      reporter.check("proto.arp.reply_op",
                     be16(arp->oper) == ARP_OP_REPLY);
      reporter.check_bytes("proto.arp.reply_spa", arp->spa, k_proto_my_ip, 4);
      reporter.check_bytes("proto.arp.reply_tpa", arp->tpa, k_proto_peer_ip, 4);
    }
  }

  reset_fake_backend();

  constexpr size_t k_payload_len = 6;
  build_icmp_echo_request(frame, k_payload_len);
  size_t request_len = sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                       sizeof(IcmpEchoHeader) + k_payload_len;
  reporter.check("proto.icmp.inject",
                 net_fake_inject_rx(frame, request_len));
  {
    reporter.check("proto.icmp.poll_once", net_poll_once());
    size_t tx_len = sizeof(tx);
    reporter.check("proto.icmp.reply_captured",
                   net_copy_last_tx_for_test(tx, &tx_len));
    reporter.check_eq("proto.icmp.reply_len", int(tx_len), int(request_len));
    if (tx_len == request_len) {
      auto *eth = (EthernetHeader *)tx;
      auto *ip = (Ipv4Header *)(tx + sizeof(EthernetHeader));
      auto *icmp = (IcmpEchoHeader *)(tx + sizeof(EthernetHeader) +
                                      sizeof(Ipv4Header));
      const uint8_t *payload = tx + sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                               sizeof(IcmpEchoHeader);
      reporter.check_bytes("proto.icmp.reply_dst", eth->dst, k_proto_peer_mac,
                           6);
      reporter.check_bytes("proto.icmp.reply_src", eth->src, k_proto_my_mac, 6);
      reporter.check_bytes("proto.icmp.reply_ip_src", ip->src_ip,
                           k_proto_my_ip, 4);
      reporter.check_bytes("proto.icmp.reply_ip_dst", ip->dst_ip,
                           k_proto_peer_ip, 4);
      reporter.check("proto.icmp.reply_type", icmp->type == ICMP_ECHO_REPLY);
      reporter.check("proto.icmp.reply_identifier",
                     icmp->identifier == be16(0x4444));
      reporter.check("proto.icmp.reply_sequence",
                     icmp->sequence == be16(7));
      uint8_t expected_payload[k_payload_len];
      for (size_t i = 0; i < k_payload_len; ++i) {
        expected_payload[i] = uint8_t(0xa0 + i);
      }
      reporter.check_bytes("proto.icmp.reply_payload", payload,
                           expected_payload, k_payload_len);
      reporter.check("proto.icmp.reply_ip_checksum",
                     checksum16((const uint8_t *)ip, sizeof(Ipv4Header)) == 0);
      reporter.check("proto.icmp.reply_checksum",
                     checksum16((const uint8_t *)icmp,
                                sizeof(IcmpEchoHeader) + k_payload_len) == 0);
    }
  }

  reset_fake_backend();
}

void run_live_tests(Reporter &reporter) {
  reporter.check("live.ready_after_pci_init", net_ready());
  reporter.note("send host ARP/ping traffic to 10.0.2.15 during this window");
  reporter.note("inspect raw for net: virtio rx dequeue and reply tx logs");

  uint64_t until = Pit::jiffies + 7000;
  while (Pit::jiffies < until) {
    if (!net_poll_once()) {
      Thread::yield();
    }
  }
}

void run_real_tx_tests(Reporter &reporter) {
  init_frames();

  uint8_t out[VIRTIO_NET_MAX_FRAME_SIZE]{};

  reporter.check("real.ready_after_pci_init", net_ready());
  reporter.check("real.fake_inject_unavailable",
                 !net_fake_inject_rx(g_frames.frame_a, sizeof(g_frames.frame_a)));

  reporter.check("real.tx_one_valid_frame",
                 net_send_raw(g_frames.frame_a, sizeof(g_frames.frame_a)));

  bool multi_send_ok = true;
  for (size_t i = 0; i < 5; ++i) {
    const uint8_t *frame = (i % 2 == 0) ? g_frames.frame_a : g_frames.frame_b;
    if (!net_send_raw(frame, sizeof(g_frames.frame_a))) {
      multi_send_ok = false;
    }
  }
  reporter.check("real.tx_multiple_back_to_back", multi_send_ok);

  reporter.check("real.tx_max_frame",
                 net_send_raw(g_frames.frame_c, VIRTIO_NET_MAX_FRAME_SIZE));
  reporter.check_eq("real.rx_empty_without_host_backend",
                    net_recv_raw(out, sizeof(out)), 0);
}

void run_dual_sender(Reporter &reporter, const char *sender_text,
                     const char *responder_text) {
  KPRINT("*** DUAL role    : sender\n");
  demo_ip_line("sender IP", k_dual_sender_ip);
  demo_ip_line("peer IP  ", k_dual_responder_ip);
  reporter.check("dual.sender.ready_after_pci_init", net_ready());
  reporter.check("dual.sender.chat_init", net_chat_init());

  uint64_t start = Pit::jiffies;
  while (Pit::jiffies < start + 1000) {
    Thread::yield();
  }

  reporter.check("dual.sender.chat_first_send_pending",
                 !net_chat_send(k_dual_responder_ip, sender_text));

  uint8_t learned_mac[6]{};
  bool arp_resolved = false;
  uint64_t until = Pit::jiffies + 3000;
  while (Pit::jiffies < until) {
    if (arp_cache_lookup(k_dual_responder_ip, learned_mac)) {
      arp_resolved = true;
      break;
    }
    if (!net_poll_once()) {
      Thread::yield();
    }
  }
  reporter.check("dual.sender.arp_resolved", arp_resolved);
  if (arp_resolved) {
    KPRINT("*** DUAL sender : ARP resolved 10.0.2.15\n");
  }

  bool sent_chat = net_chat_send(k_dual_responder_ip, sender_text);
  reporter.check("dual.sender.chat_tx", sent_chat);
  if (sent_chat) {
    KPRINT("*** DUAL sender : chat TX \"?\"\n", sender_text);
  }

  char chat_reply[NET_CHAT_TEXT_SIZE]{};
  bool saw_chat_reply = false;
  until = Pit::jiffies + 5000;
  while (Pit::jiffies < until) {
    if (net_chat_poll() && net_chat_recv(chat_reply, sizeof(chat_reply)) &&
        strings_equal(chat_reply, responder_text)) {
      saw_chat_reply = true;
      break;
    }
    Thread::yield();
  }

  reporter.check("dual.sender.chat_reply_rx", saw_chat_reply);
  if (saw_chat_reply) {
    KPRINT("*** DUAL sender : chat RX from 10.0.2.15 \"?\"\n", chat_reply);
    KPRINT("*** DUAL sender : PASS dual chat\n");
  }

  net_stats_print();
}

void run_dual_responder(Reporter &reporter, const char *sender_text,
                        const char *responder_text) {
  KPRINT("*** DUAL role    : responder\n");
  demo_ip_line("responder", k_dual_responder_ip);
  demo_ip_line("peer IP  ", k_dual_sender_ip);
  reporter.check("dual.responder.ready_after_pci_init", net_ready());
  reporter.check("dual.responder.chat_init", net_chat_init());

  const uint64_t until = Pit::jiffies + 5000;
  bool saw_chat = false;
  bool sent_chat_reply = false;
  char text[NET_CHAT_TEXT_SIZE]{};
  while (Pit::jiffies < until) {
    if (net_chat_poll()) {
      if (!saw_chat && net_chat_recv(text, sizeof(text)) &&
          strings_equal(text, sender_text)) {
        saw_chat = true;
        KPRINT("*** DUAL responder : chat RX from 10.0.2.21 \"?\"\n", text);
        sent_chat_reply =
            net_chat_send(k_dual_sender_ip, responder_text);
        if (sent_chat_reply) {
          KPRINT("*** DUAL responder : chat TX \"?\"\n", responder_text);
        }
      }
      continue;
    }
    Thread::yield();
  }

  reporter.check("dual.responder.chat_rx", saw_chat);
  reporter.check("dual.responder.chat_reply_tx", sent_chat_reply);
  net_stats_print();
}

void run_dual_live_tests(Reporter &reporter, StrongRef<Ext2> fs) {
  const DualRole role = read_dual_role(fs);
  if (role == DualRole::Unknown) {
    reporter.fail("dual.role_missing_or_unknown");
    return;
  }

  char chat_send[NET_CHAT_TEXT_SIZE]{};
  char chat_expect[NET_CHAT_TEXT_SIZE]{};
  if (!read_named_file(fs, "chat_send", chat_send, sizeof(chat_send))) {
    if (role == DualRole::Sender) {
      copy_bytes_local(reinterpret_cast<uint8_t *>(chat_send),
                       reinterpret_cast<const uint8_t *>(
                           k_dual_chat_sender_text),
                       sizeof(k_dual_chat_sender_text));
    } else {
      copy_bytes_local(reinterpret_cast<uint8_t *>(chat_send),
                       reinterpret_cast<const uint8_t *>(
                           k_dual_chat_responder_text),
                       sizeof(k_dual_chat_responder_text));
    }
  }
  if (!read_named_file(fs, "chat_expect", chat_expect, sizeof(chat_expect))) {
    if (role == DualRole::Sender) {
      copy_bytes_local(reinterpret_cast<uint8_t *>(chat_expect),
                       reinterpret_cast<const uint8_t *>(
                           k_dual_chat_responder_text),
                       sizeof(k_dual_chat_responder_text));
    } else {
      copy_bytes_local(reinterpret_cast<uint8_t *>(chat_expect),
                       reinterpret_cast<const uint8_t *>(
                           k_dual_chat_sender_text),
                       sizeof(k_dual_chat_sender_text));
    }
  }

  net_stats_reset();
  arp_cache_reset();
  udp_clear_handlers();
  net_chat_reset();

  if (role == DualRole::Sender) {
    net_set_identity(k_dual_sender_mac, k_dual_sender_ip);
    run_dual_sender(reporter, chat_send, chat_expect);
    return;
  }

  net_set_identity(k_dual_responder_mac, k_dual_responder_ip);
  run_dual_responder(reporter, chat_expect, chat_send);
}

} // namespace

void net_run_selected_tests(StrongRef<Ext2> fs) {
  char selector[k_selector_buffer_size]{};
  const NetTestCase test_case = read_test_case(fs, selector, sizeof(selector));

  if (test_case == NetTestCase::None) {
    return;
  }

  Reporter reporter{};

  switch (test_case) {
  case NetTestCase::Demo:
    run_demo_tests(reporter);
    break;
  case NetTestCase::DualLive:
    run_dual_live_tests(reporter, fs);
    break;
  case NetTestCase::Smoke:
    run_smoke_tests(reporter);
    break;
  case NetTestCase::Tx:
    run_tx_tests(reporter);
    break;
  case NetTestCase::Rx:
    run_rx_tests(reporter);
    break;
  case NetTestCase::Queue:
    run_queue_tests(reporter);
    break;
  case NetTestCase::Debug:
    run_debug_tests(reporter);
    break;
  case NetTestCase::Stats:
    run_stats_tests(reporter);
    break;
  case NetTestCase::ArpCache:
    run_arp_cache_tests(reporter);
    break;
  case NetTestCase::Udp:
    run_udp_tests(reporter);
    break;
  case NetTestCase::Chat:
    run_chat_tests(reporter);
    break;
  case NetTestCase::Proto:
    run_proto_tests(reporter);
    break;
  case NetTestCase::Live:
    run_live_tests(reporter);
    break;
  case NetTestCase::RealTx:
    run_real_tx_tests(reporter);
    break;
  case NetTestCase::Unknown:
    reporter.fail("selector.unknown_test_case");
    reporter.note(selector);
    break;
  case NetTestCase::None:
    break;
  }

  reporter.summary();
}
