# net_dual_demo

AI assistance note: AI was used as a development aid for planning, debugging,
review, and documentation. The team reviewed the output and is responsible for
the submitted work.

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

The demo messages come from simple command files on each guest disk:

- `net_dual_sender.dir/chat_send`
- `net_dual_sender.dir/chat_expect`
- `net_dual_responder.dir/chat_send`
- `net_dual_responder.dir/chat_expect`

Edit the `chat_send` files to change what each VM sends. Keep each peer's
`chat_expect` file in sync with the message it should receive so the automated
demo can still verify the exchange.

The blessed entry point for the messaging demo is the Docker wrapper:

```sh
chmod +x ./run_dual_qemu_docker
./run_dual_qemu_docker
```

The Docker runner builds `Dockerfile.dual`, mounts this repo at `/work`, and
runs `./run_dual_qemu` inside the container. The two QEMU processes communicate
through `127.0.0.1:${DUAL_QEMU_PORT:-12345}` inside the container, so TAP is not
required. The wrapper uses Docker by default and can be pointed at Podman with
`DUAL_QEMU_CONTAINER_CMD=podman`.

Expected final terminal output:

```text
dual chat verification: pass
```

After it exits, inspect:

- `net_dual_sender.raw`
- `net_dual_responder.raw`

The sender log should include:

```text
*** DUAL sender : ARP resolved 10.0.2.15
*** DUAL sender : chat TX "hello from sender"
*** DUAL sender : chat RX from 10.0.2.15 "hello from responder"
*** DUAL sender : PASS dual chat
*** SUMMARY failures=0
```

The responder log should include:

```text
*** DUAL responder : chat RX from 10.0.2.21 "hello from sender"
*** DUAL responder : chat TX "hello from responder"
*** SUMMARY failures=0
```

For local runs without Docker:

```sh
chmod +x ./run_dual_qemu
./run_dual_qemu
```

The script verifies both raw logs and exits nonzero if any expected chat line is
missing.
