# net_udp

This test exercises the Phase 3 UDP layer.

It verifies that:

- a handler can be registered for a UDP port
- a valid IPv4/UDP packet reaches the registered handler
- a UDP packet for an unregistered port is ignored
- a short UDP payload is dropped as malformed
- `udp_send_to` returns pending/false when ARP resolution is missing
- the miss path emits an ARP request
- the hit path emits a valid Ethernet+IPv4+UDP frame
- UDP checksum is zero, by design for this phase
- UDP, ARP, IPv4, and drop stats update correctly

Run it with:

```sh
make -s net_udp.test
```
