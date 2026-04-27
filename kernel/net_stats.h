#pragma once

// AI assistance note: AI was used to help plan, review, and document this
// networking code. The implementation was integrated, tested, and reviewed by
// the team.

#include <cstdint>

// Counters used by tests and debug logs to prove each network path ran.
struct NetStats {
  uint64_t raw_rx;
  uint64_t raw_tx;
  uint64_t arp_rx;
  uint64_t arp_tx;
  uint64_t ipv4_rx;
  uint64_t ipv4_tx;
  uint64_t icmp_rx;
  uint64_t icmp_tx;
  uint64_t udp_rx;
  uint64_t udp_tx;
  uint64_t dropped_short;
  uint64_t dropped_bad_checksum;
  uint64_t dropped_not_for_me;
  uint64_t dropped_unknown_ethertype;
  uint64_t dropped_unknown_ipv4_protocol;
};

enum class NetStatCounter : uint8_t {
  RawRx,
  RawTx,
  ArpRx,
  ArpTx,
  Ipv4Rx,
  Ipv4Tx,
  IcmpRx,
  IcmpTx,
  UdpRx,
  UdpTx,
  DroppedShort,
  DroppedBadChecksum,
  DroppedNotForMe,
  DroppedUnknownEthertype,
  DroppedUnknownIpv4Protocol,
};

void net_stats_reset();
void net_stats_increment(NetStatCounter counter);
NetStats net_stats_snapshot();
void net_stats_print();
