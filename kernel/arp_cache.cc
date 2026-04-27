#include "arp_cache.h"

// AI assistance note: AI was used to help plan, review, and document this
// networking code. The implementation was integrated, tested, and reviewed by
// the team.

#include "atomic.h"
#include "print.h"
#include "spin_lock.h"

namespace {

SpinLock g_arp_cache_lock{};
ArpEntry g_arp_cache[ARP_CACHE_CAPACITY]{};
uint64_t g_arp_cache_clock = 0;

bool bytes_equal(const uint8_t *lhs, const uint8_t *rhs, uint8_t len) {
  for (uint8_t i = 0; i < len; ++i) {
    if (lhs[i] != rhs[i]) {
      return false;
    }
  }
  return true;
}

void copy_bytes(uint8_t *dst, const uint8_t *src, uint8_t len) {
  for (uint8_t i = 0; i < len; ++i) {
    dst[i] = src[i];
  }
}

int find_entry_locked(const uint8_t ip[4]) {
  for (uint8_t i = 0; i < ARP_CACHE_CAPACITY; ++i) {
    if (g_arp_cache[i].valid && bytes_equal(g_arp_cache[i].ip, ip, 4)) {
      return i;
    }
  }
  return -1;
}

uint8_t choose_insert_slot_locked() {
  for (uint8_t i = 0; i < ARP_CACHE_CAPACITY; ++i) {
    if (!g_arp_cache[i].valid) {
      return i;
    }
  }

  uint8_t oldest = 0;
  for (uint8_t i = 1; i < ARP_CACHE_CAPACITY; ++i) {
    if (g_arp_cache[i].last_seen < g_arp_cache[oldest].last_seen) {
      oldest = i;
    }
  }
  return oldest;
}

void print_ip(const uint8_t ip[4]) {
  KPRINT("?.?.?.?", Dec(ip[0]), Dec(ip[1]), Dec(ip[2]), Dec(ip[3]));
}

void print_mac(const uint8_t mac[6]) {
  KPRINT("?:?:?:?:?:?", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

} // namespace

void arp_cache_reset() {
  LockGuard guard{g_arp_cache_lock};
  for (uint8_t i = 0; i < ARP_CACHE_CAPACITY; ++i) {
    g_arp_cache[i] = {};
  }
  g_arp_cache_clock = 0;
}

void arp_cache_insert(const uint8_t ip[4], const uint8_t mac[6]) {
  // The cache is intentionally fixed-size so the kernel networking path does
  // not depend on dynamic allocation.
  if (ip == nullptr || mac == nullptr) {
    return;
  }

  LockGuard guard{g_arp_cache_lock};
  int existing = find_entry_locked(ip);
  uint8_t slot = existing >= 0 ? uint8_t(existing) : choose_insert_slot_locked();
  bool replacing = g_arp_cache[slot].valid && existing < 0;
  uint8_t evicted_ip[4]{};

  if (replacing) {
    copy_bytes(evicted_ip, g_arp_cache[slot].ip, 4);
  }

  g_arp_cache[slot].valid = true;
  copy_bytes(g_arp_cache[slot].ip, ip, 4);
  copy_bytes(g_arp_cache[slot].mac, mac, 6);
  g_arp_cache[slot].last_seen = ++g_arp_cache_clock;

  if (replacing) {
    KPRINT("net: ARP cache evict ");
    print_ip(evicted_ip);
    KPRINT("\n");
  }
  KPRINT("net: ARP cache add ");
  print_ip(ip);
  KPRINT(" -> ");
  print_mac(mac);
  KPRINT("\n");
}

bool arp_cache_lookup(const uint8_t ip[4], uint8_t mac_out[6]) {
  if (ip == nullptr || mac_out == nullptr) {
    return false;
  }

  LockGuard guard{g_arp_cache_lock};
  int slot = find_entry_locked(ip);
  if (slot < 0) {
    KPRINT("net: ARP lookup ");
    print_ip(ip);
    KPRINT(" miss\n");
    return false;
  }

  copy_bytes(mac_out, g_arp_cache[slot].mac, 6);
  g_arp_cache[slot].last_seen = ++g_arp_cache_clock;
  KPRINT("net: ARP lookup ");
  print_ip(ip);
  KPRINT(" hit\n");
  return true;
}

bool arp_cache_snapshot(uint8_t index, ArpEntry *out) {
  if (out == nullptr || index >= ARP_CACHE_CAPACITY) {
    return false;
  }

  LockGuard guard{g_arp_cache_lock};
  *out = g_arp_cache[index];
  return true;
}

void arp_cache_print() {
  LockGuard guard{g_arp_cache_lock};
  KPRINT("net: ARP cache table\n");
  for (uint8_t i = 0; i < ARP_CACHE_CAPACITY; ++i) {
    if (!g_arp_cache[i].valid) {
      continue;
    }
    KPRINT("net:   ");
    print_ip(g_arp_cache[i].ip);
    KPRINT(" -> ");
    print_mac(g_arp_cache[i].mac);
    KPRINT(" seen=?\n", Dec(g_arp_cache[i].last_seen));
  }
}
