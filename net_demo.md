# net_demo

This demo is meant for a short class presentation.

It exercises the protocol layer on top of the fake NIC backend, so the run is
fully deterministic and prints a presentation-style walkthrough instead of a
long unit-test checklist.

What it demonstrates:

- ARP request in, ARP reply out
- ICMP echo request in, ICMP echo reply out
- Correct source and destination MAC/IP rewriting
- Valid IPv4 and ICMP checksums on the generated reply

Run it with:

```sh
make -s net_demo.test
```

If you want to watch the serial output directly during the presentation, run:

```sh
./run_qemu net_demo
```

If you want a follow-up live demo against a real virtio NIC path, use:

```sh
make -s net_live.test
```

and boot QEMU with an attached host backend through `QEMU_NETDEV` and
`QEMU_NET_DEVICE`.
