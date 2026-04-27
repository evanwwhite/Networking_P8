# net_dual_demo

This boots two QEMU guests on one internal QEMU socket link. It does not use
TAP, `/dev/net/tun`, host forwarding, or privileged host networking.

- `net_dual_sender` uses MAC `52:54:00:12:34:57` and IP `10.0.2.21`
- `net_dual_responder` uses MAC `52:54:00:12:34:56` and IP `10.0.2.15`
- the sender first tries to send a UDP chat message and triggers ARP resolution
- the responder answers ARP through the normal protocol path
- the sender sends `hello from sender` over UDP port `4390`
- the responder receives it through the chat layer and replies with
  `hello from responder`
- the sender receives the reply and prints `PASS dual chat`

Run it with:

```sh
chmod +x ./run_dual_qemu
./run_dual_qemu
```

The script verifies both raw logs and exits nonzero if any expected chat line is
missing.

To run the same socket-only demo inside Docker:

```sh
chmod +x ./run_dual_qemu_docker
./run_dual_qemu_docker
```

The Docker runner builds `Dockerfile.dual`, mounts this repo at `/work`, and
runs `./run_dual_qemu` inside the container. The two QEMU processes communicate
through `127.0.0.1:${DUAL_QEMU_PORT:-12345}` inside the container, so TAP is not
required.

After it exits, inspect:

- `net_dual_sender.raw`
- `net_dual_responder.raw`
