# net_stats

AI assistance note: AI was used as a development aid for planning, debugging,
review, and documentation. The team reviewed the output and is responsible for
the submitted work.

This test exercises the Phase 1 networking observability work.

It verifies that the kernel records packet statistics for:

- successful raw RX/TX through the fake backend
- successful ARP request/reply handling
- successful IPv4/ICMP echo request/reply handling
- short Ethernet-frame drops
- bad IPv4-checksum drops
- frames addressed to another MAC
- unknown Ethernet types
- unsupported IPv4 protocols

Run it with:

```sh
make -s net_stats.test
```
