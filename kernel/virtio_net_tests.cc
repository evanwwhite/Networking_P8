#include "virtio_net_tests.h"

#include "arp.h"
#include "ethernet.h"
#include "icmp.h"
#include "ipv4.h"
#include "net_proto.h"
#include "pit.h"
#include "print.h"
#include "thread.h"
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
constexpr char k_dual_payload_text[] = "hello";

enum class DualRole : uint8_t {
  Unknown,
  Sender,
  Responder,
};

TestFrames g_frames{};
bool g_frames_ready = false;

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

void build_arp_request_frame(uint8_t *frame, const uint8_t src_mac[6],
                             const uint8_t src_ip[4], const uint8_t dst_ip[4]) {
  auto *eth = (EthernetHeader *)frame;
  auto *arp = (ArpPacket *)(frame + sizeof(EthernetHeader));
  uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  uint8_t zero_mac[6] = {};

  copy_bytes_local(eth->dst, broadcast, 6);
  copy_bytes_local(eth->src, src_mac, 6);
  eth->ether_type = be16(ETH_TYPE_ARP);

  arp->htype = be16(ARP_HTYPE_ETHERNET);
  arp->ptype = be16(ARP_PTYPE_IPV4);
  arp->hlen = 6;
  arp->plen = 4;
  arp->oper = be16(ARP_OP_REQUEST);
  copy_bytes_local(arp->sha, src_mac, 6);
  copy_bytes_local(arp->spa, src_ip, 4);
  copy_bytes_local(arp->tha, zero_mac, 6);
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

bool wait_for_matching_frame(uint8_t *frame, size_t *frame_len,
                             uint64_t timeout_jiffies,
                             bool (*match)(const uint8_t *, size_t)) {
  uint8_t tmp[VIRTIO_NET_MAX_FRAME_SIZE]{};

  while (Pit::jiffies < timeout_jiffies) {
    int recv_len = net_recv_raw(tmp, sizeof(tmp));
    if (recv_len > 0) {
      if (match(tmp, size_t(recv_len))) {
        const size_t to_copy = min_size(*frame_len, size_t(recv_len));
        copy_bytes_local(frame, tmp, to_copy);
        *frame_len = size_t(recv_len);
        return true;
      }
      continue;
    }
    Thread::yield();
  }

  return false;
}

bool is_dual_arp_reply(const uint8_t *frame, size_t len) {
  if (len < sizeof(EthernetHeader) + sizeof(ArpPacket)) {
    return false;
  }
  auto *eth = (const EthernetHeader *)frame;
  auto *arp = (const ArpPacket *)(frame + sizeof(EthernetHeader));
  return be16(eth->ether_type) == ETH_TYPE_ARP &&
         be16(arp->oper) == ARP_OP_REPLY &&
         bytes_equal(eth->src, k_dual_responder_mac, 6) &&
         bytes_equal(arp->spa, k_dual_responder_ip, 4) &&
         bytes_equal(arp->tpa, k_dual_sender_ip, 4);
}

bool is_dual_icmp_reply(const uint8_t *frame, size_t len) {
  const size_t payload_len = sizeof(k_dual_payload_text) - 1;
  const size_t want_len = sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                          sizeof(IcmpEchoHeader) + payload_len;
  if (len != want_len) {
    return false;
  }

  auto *eth = (const EthernetHeader *)frame;
  auto *ip = (const Ipv4Header *)(frame + sizeof(EthernetHeader));
  auto *icmp =
      (const IcmpEchoHeader *)(frame + sizeof(EthernetHeader) + sizeof(Ipv4Header));
  const uint8_t *payload =
      frame + sizeof(EthernetHeader) + sizeof(Ipv4Header) + sizeof(IcmpEchoHeader);

  return be16(eth->ether_type) == ETH_TYPE_IPV4 &&
         bytes_equal(eth->src, k_dual_responder_mac, 6) &&
         bytes_equal(ip->src_ip, k_dual_responder_ip, 4) &&
         bytes_equal(ip->dst_ip, k_dual_sender_ip, 4) &&
         icmp->type == ICMP_ECHO_REPLY &&
         icmp->identifier == be16(0x5151) &&
         icmp->sequence == be16(1) &&
         bytes_equal(payload, (const uint8_t *)k_dual_payload_text, payload_len);
}

bool try_dual_send_arp_reply(const uint8_t *frame, size_t len) {
  if (len < sizeof(EthernetHeader) + sizeof(ArpPacket)) {
    return false;
  }

  auto *eth = (const EthernetHeader *)frame;
  auto *arp = (const ArpPacket *)(frame + sizeof(EthernetHeader));
  if (be16(eth->ether_type) != ETH_TYPE_ARP || be16(arp->oper) != ARP_OP_REQUEST ||
      !bytes_equal(arp->tpa, k_dual_responder_ip, 4)) {
    return false;
  }

  uint8_t reply[sizeof(EthernetHeader) + sizeof(ArpPacket)]{};
  auto *out_eth = (EthernetHeader *)reply;
  auto *out_arp = (ArpPacket *)(reply + sizeof(EthernetHeader));

  copy_bytes_local(out_eth->dst, eth->src, 6);
  copy_bytes_local(out_eth->src, k_dual_responder_mac, 6);
  out_eth->ether_type = be16(ETH_TYPE_ARP);

  out_arp->htype = be16(ARP_HTYPE_ETHERNET);
  out_arp->ptype = be16(ARP_PTYPE_IPV4);
  out_arp->hlen = 6;
  out_arp->plen = 4;
  out_arp->oper = be16(ARP_OP_REPLY);
  copy_bytes_local(out_arp->sha, k_dual_responder_mac, 6);
  copy_bytes_local(out_arp->spa, k_dual_responder_ip, 4);
  copy_bytes_local(out_arp->tha, arp->sha, 6);
  copy_bytes_local(out_arp->tpa, arp->spa, 4);

  KPRINT("*** DUAL responder : ARP reply to ?.?.?.?\n", Dec(arp->spa[0]),
         Dec(arp->spa[1]), Dec(arp->spa[2]), Dec(arp->spa[3]));
  return net_send_raw(reply, sizeof(reply));
}

bool try_dual_send_icmp_reply(const uint8_t *frame, size_t len) {
  const size_t min_len =
      sizeof(EthernetHeader) + sizeof(Ipv4Header) + sizeof(IcmpEchoHeader);
  if (len < min_len) {
    return false;
  }

  auto *eth = (const EthernetHeader *)frame;
  auto *ip = (const Ipv4Header *)(frame + sizeof(EthernetHeader));
  if (be16(eth->ether_type) != ETH_TYPE_IPV4 || ip->protocol != IPV4_PROTO_ICMP ||
      !bytes_equal(ip->dst_ip, k_dual_responder_ip, 4)) {
    return false;
  }

  const size_t ip_total_len = be16(ip->total_length);
  if (ip_total_len < sizeof(Ipv4Header) + sizeof(IcmpEchoHeader) ||
      sizeof(EthernetHeader) + ip_total_len > len) {
    return false;
  }

  auto *icmp =
      (const IcmpEchoHeader *)(frame + sizeof(EthernetHeader) + sizeof(Ipv4Header));
  if (icmp->type != ICMP_ECHO_REQUEST) {
    return false;
  }

  const size_t payload_len = ip_total_len - sizeof(Ipv4Header) - sizeof(IcmpEchoHeader);
  const uint8_t *payload = frame + sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                           sizeof(IcmpEchoHeader);

  uint8_t reply[VIRTIO_NET_MAX_FRAME_SIZE]{};
  copy_bytes_local(reply, frame, sizeof(EthernetHeader) + ip_total_len);

  auto *out_eth = (EthernetHeader *)reply;
  auto *out_ip = (Ipv4Header *)(reply + sizeof(EthernetHeader));
  auto *out_icmp =
      (IcmpEchoHeader *)(reply + sizeof(EthernetHeader) + sizeof(Ipv4Header));

  copy_bytes_local(out_eth->dst, eth->src, 6);
  copy_bytes_local(out_eth->src, k_dual_responder_mac, 6);
  copy_bytes_local(out_ip->src_ip, k_dual_responder_ip, 4);
  copy_bytes_local(out_ip->dst_ip, ip->src_ip, 4);
  out_ip->header_checksum = 0;
  out_ip->header_checksum =
      be16(checksum16((const uint8_t *)out_ip, sizeof(Ipv4Header)));

  out_icmp->type = ICMP_ECHO_REPLY;
  out_icmp->code = 0;
  out_icmp->checksum = 0;
  out_icmp->checksum = be16(
      checksum16((const uint8_t *)out_icmp, sizeof(IcmpEchoHeader) + payload_len));

  demo_payload_line("payload  ", payload, payload_len);
  KPRINT("*** DUAL responder : ICMP echo reply\n");
  return net_send_raw(reply, sizeof(EthernetHeader) + ip_total_len);
}

void run_dual_sender(Reporter &reporter) {
  uint8_t frame[128]{};
  uint8_t reply[VIRTIO_NET_MAX_FRAME_SIZE]{};

  KPRINT("*** DUAL role    : sender\n");
  demo_ip_line("sender IP", k_dual_sender_ip);
  demo_ip_line("peer IP  ", k_dual_responder_ip);
  reporter.check("dual.sender.ready_after_pci_init", net_ready());

  uint64_t start = Pit::jiffies;
  while (Pit::jiffies < start + 1000) {
    Thread::yield();
  }

  build_arp_request_frame(frame, k_dual_sender_mac, k_dual_sender_ip,
                          k_dual_responder_ip);
  KPRINT("*** DUAL sender : ARP who-has ?.?.?.?\n", Dec(k_dual_responder_ip[0]),
         Dec(k_dual_responder_ip[1]), Dec(k_dual_responder_ip[2]),
         Dec(k_dual_responder_ip[3]));
  reporter.check("dual.sender.arp_request_tx",
                 net_send_raw(frame, sizeof(EthernetHeader) + sizeof(ArpPacket)));

  size_t reply_len = sizeof(reply);
  reporter.check(
      "dual.sender.arp_reply_rx",
      wait_for_matching_frame(reply, &reply_len, Pit::jiffies + 3000,
                              is_dual_arp_reply));
  if (is_dual_arp_reply(reply, reply_len)) {
    KPRINT("*** DUAL sender : learned ?.?.?.? is at ?:?:?:?:?:?\n",
           Dec(k_dual_responder_ip[0]), Dec(k_dual_responder_ip[1]),
           Dec(k_dual_responder_ip[2]), Dec(k_dual_responder_ip[3]), reply[6],
           reply[7], reply[8], reply[9], reply[10], reply[11]);
  }

  build_icmp_echo_request_payload(
      frame, (const uint8_t *)k_dual_payload_text, sizeof(k_dual_payload_text) - 1);
  auto *eth = (EthernetHeader *)frame;
  auto *ip = (Ipv4Header *)(frame + sizeof(EthernetHeader));
  auto *icmp =
      (IcmpEchoHeader *)(frame + sizeof(EthernetHeader) + sizeof(Ipv4Header));
  copy_bytes_local(eth->dst, k_dual_responder_mac, 6);
  copy_bytes_local(eth->src, k_dual_sender_mac, 6);
  copy_bytes_local(ip->src_ip, k_dual_sender_ip, 4);
  copy_bytes_local(ip->dst_ip, k_dual_responder_ip, 4);
  ip->header_checksum = 0;
  ip->header_checksum =
      be16(checksum16((const uint8_t *)ip, sizeof(Ipv4Header)));
  icmp->identifier = be16(0x5151);
  icmp->sequence = be16(1);
  icmp->checksum = 0;
  icmp->checksum = be16(checksum16(
      (const uint8_t *)icmp,
      sizeof(IcmpEchoHeader) + (sizeof(k_dual_payload_text) - 1)));

  KPRINT("*** DUAL sender : ping text \"?\"\n", k_dual_payload_text);
  reporter.check(
      "dual.sender.icmp_request_tx",
      net_send_raw(frame, sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                              sizeof(IcmpEchoHeader) +
                              (sizeof(k_dual_payload_text) - 1)));

  reply_len = sizeof(reply);
  reporter.check(
      "dual.sender.icmp_reply_rx",
      wait_for_matching_frame(reply, &reply_len, Pit::jiffies + 3000,
                              is_dual_icmp_reply));
  if (is_dual_icmp_reply(reply, reply_len)) {
    KPRINT("*** DUAL sender : peer echoed \"?\"\n", k_dual_payload_text);
  }
}

void run_dual_responder(Reporter &reporter) {
  KPRINT("*** DUAL role    : responder\n");
  demo_ip_line("responder", k_dual_responder_ip);
  demo_ip_line("peer IP  ", k_dual_sender_ip);
  reporter.check("dual.responder.ready_after_pci_init", net_ready());

  const uint64_t until = Pit::jiffies + 5000;
  bool saw_traffic = false;
  bool sent_arp_reply = false;
  bool sent_icmp_reply = false;
  uint8_t frame[VIRTIO_NET_MAX_FRAME_SIZE]{};
  while (Pit::jiffies < until) {
    int recv_len = net_recv_raw(frame, sizeof(frame));
    if (recv_len > 0) {
      saw_traffic = true;
      sent_arp_reply =
          try_dual_send_arp_reply(frame, size_t(recv_len)) || sent_arp_reply;
      sent_icmp_reply =
          try_dual_send_icmp_reply(frame, size_t(recv_len)) || sent_icmp_reply;
      continue;
    }
    Thread::yield();
  }

  reporter.check("dual.responder.saw_live_traffic", saw_traffic);
  reporter.check("dual.responder.arp_reply_tx", sent_arp_reply);
  reporter.check("dual.responder.icmp_reply_tx", sent_icmp_reply);
}

void run_dual_live_tests(Reporter &reporter, StrongRef<Ext2> fs) {
  const DualRole role = read_dual_role(fs);
  if (role == DualRole::Unknown) {
    reporter.fail("dual.role_missing_or_unknown");
    return;
  }

  if (role == DualRole::Sender) {
    net_set_identity(k_dual_sender_mac, k_dual_sender_ip);
    run_dual_sender(reporter);
    return;
  }

  run_dual_responder(reporter);
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
