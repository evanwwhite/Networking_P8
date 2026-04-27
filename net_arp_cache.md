# net_arp_cache

This test exercises the Phase 2 ARP cache work.

It verifies that:

- ARP cache misses report false
- ARP requests teach the cache from sender IP/MAC
- ARP replies teach the cache from sender IP/MAC
- the fixed-size cache evicts the oldest entry when full
- `net_send_ipv4` returns pending/false on an ARP miss
- the miss path sends a valid ARP request for the target IP
- the hit path sends a valid Ethernet+IPv4 frame to the cached MAC
- ARP TX and IPv4 TX stats are updated

Run it with:

```sh
make -s net_arp_cache.test
```
