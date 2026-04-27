#pragma once

// AI assistance note: AI was used to help plan, review, and document this
// networking code. The implementation was integrated, tested, and reviewed by
// the team.

#include <cstdint>

constexpr uint8_t ARP_CACHE_CAPACITY = 8;

struct ArpEntry {
  bool valid;
  uint8_t ip[4];
  uint8_t mac[6];
  uint64_t last_seen;
};

void arp_cache_reset();
void arp_cache_insert(const uint8_t ip[4], const uint8_t mac[6]);
bool arp_cache_lookup(const uint8_t ip[4], uint8_t mac_out[6]);
bool arp_cache_snapshot(uint8_t index, ArpEntry *out);
void arp_cache_print();
