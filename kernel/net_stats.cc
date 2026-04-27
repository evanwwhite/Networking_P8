#include "net_stats.h"

#include "atomic.h"
#include "print.h"
#include "spin_lock.h"

namespace {

SpinLock g_stats_lock{};
NetStats g_stats{};

uint64_t &counter_ref(NetStats &stats, NetStatCounter counter) {
  switch (counter) {
  case NetStatCounter::RawRx:
    return stats.raw_rx;
  case NetStatCounter::RawTx:
    return stats.raw_tx;
  case NetStatCounter::ArpRx:
    return stats.arp_rx;
  case NetStatCounter::ArpTx:
    return stats.arp_tx;
  case NetStatCounter::Ipv4Rx:
    return stats.ipv4_rx;
  case NetStatCounter::Ipv4Tx:
    return stats.ipv4_tx;
  case NetStatCounter::IcmpRx:
    return stats.icmp_rx;
  case NetStatCounter::IcmpTx:
    return stats.icmp_tx;
  case NetStatCounter::UdpRx:
    return stats.udp_rx;
  case NetStatCounter::UdpTx:
    return stats.udp_tx;
  case NetStatCounter::DroppedShort:
    return stats.dropped_short;
  case NetStatCounter::DroppedBadChecksum:
    return stats.dropped_bad_checksum;
  case NetStatCounter::DroppedNotForMe:
    return stats.dropped_not_for_me;
  case NetStatCounter::DroppedUnknownEthertype:
    return stats.dropped_unknown_ethertype;
  case NetStatCounter::DroppedUnknownIpv4Protocol:
    return stats.dropped_unknown_ipv4_protocol;
  }

  return stats.dropped_unknown_ethertype;
}

} // namespace

void net_stats_reset() {
  LockGuard guard{g_stats_lock};
  g_stats = {};
}

void net_stats_increment(NetStatCounter counter) {
  LockGuard guard{g_stats_lock};
  ++counter_ref(g_stats, counter);
}

NetStats net_stats_snapshot() {
  LockGuard guard{g_stats_lock};
  return g_stats;
}

void net_stats_print() {
  NetStats stats = net_stats_snapshot();
  KPRINT("net: stats raw_rx=? raw_tx=? arp_rx=? arp_tx=? ipv4_rx=? ipv4_tx=? "
         "icmp_rx=? icmp_tx=? udp_rx=? udp_tx=?\n",
         Dec(stats.raw_rx), Dec(stats.raw_tx), Dec(stats.arp_rx),
         Dec(stats.arp_tx), Dec(stats.ipv4_rx), Dec(stats.ipv4_tx),
         Dec(stats.icmp_rx), Dec(stats.icmp_tx), Dec(stats.udp_rx),
         Dec(stats.udp_tx));
  KPRINT("net: drops short=? bad_checksum=? not_for_me=? unknown_ethertype=? "
         "unknown_ipv4_protocol=?\n",
         Dec(stats.dropped_short), Dec(stats.dropped_bad_checksum),
         Dec(stats.dropped_not_for_me), Dec(stats.dropped_unknown_ethertype),
         Dec(stats.dropped_unknown_ipv4_protocol));
}
