# net_dual_demo

This boots two QEMU guests on one internal QEMU socket link.

- `net_dual_sender` uses MAC `52:54:00:12:34:57` and IP `10.0.2.21`
- `net_dual_responder` uses MAC `52:54:00:12:34:56` and IP `10.0.2.15`
- the sender emits an ARP request followed by an ICMP echo request carrying
  the text `hello`
- the responder uses the existing protocol layer to answer the ARP and ICMP
  traffic live

Run it with:

```sh
chmod +x ./run_dual_qemu
./run_dual_qemu
```

After it exits, inspect:

- `net_dual_sender.raw`
- `net_dual_responder.raw`
