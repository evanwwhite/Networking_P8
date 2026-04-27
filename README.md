# Networking OS Project

AI assistance note: AI was used as a development aid for planning, debugging, review, and documentation. The team reviewed the output and is responsible for the submitted work.

## Overview

This repository contains a small teaching OS extended with virtio-net networking support. The final networking project builds a raw Ethernet path, adds ARP/IPv4/ICMP/UDP protocol support, and demonstrates UDP chat between two QEMU guests connected through QEMU socket networking.

The main demo boots two copies of the OS:

- sender: MAC `52:54:00:12:34:57`, IP `10.0.2.21`
- responder: MAC `52:54:00:12:34:56`, IP `10.0.2.15`

The sender resolves the responder with ARP, sends `hello from sender` over UDP port `4390`, the responder replies with `hello from responder`, and the sender verifies the exchange with `PASS dual chat`.

## Major Components

- `kernel/virtio_net.*`: virtio-net driver, raw Ethernet send/receive API, fake backend, and live PCI virtio backend.
- `kernel/net_proto.*`: Ethernet dispatch, ARP, IPv4, and ICMP handling.
- `kernel/arp_cache.*`: fixed-size ARP cache used by IPv4 sending.
- `kernel/udp.*`: UDP parsing, handler registration, dispatch, and transmit support.
- `kernel/net_chat.*`: small UDP chat layer used by the two-machine demo.
- `kernel/net_stats.*`: packet and drop counters for debugging/tests.
- `kernel/virtio_net_tests.*`: focused networking test harness.
- `run_dual_qemu`: local two-QEMU socket-network demo runner.
- `run_dual_qemu_docker`: Docker/Podman wrapper for the same demo.

## Review Guide

For a quick review, start with:

1. `REPORT.txt` for the final project summary, design notes, limitations, and verification notes.
2. `README.md` for the current run/test instructions.
3. `kernel/virtio_net.cc` and `kernel/virtio_net.h` for the raw NIC interface and virtio-net backend.
4. `kernel/net_proto.cc`, `kernel/arp_cache.cc`, `kernel/udp.cc`, and `kernel/net_chat.cc` for the protocol stack and chat demo.
5. `kernel/virtio_net_tests.cc` for the networking test harness.
6. `net_dual_demo.md` and `run_dual_qemu_docker` for the two-machine demo.

## Requirements

For local builds, use the course toolchain/environment expected by the original OS project. The Docker demo also requires Docker or Podman on the host.

The dual-QEMU demo intentionally uses QEMU socket networking instead of TAP, so it does not require `/dev/net/tun`, host bridge setup, or privileged host networking.

## Run the Main Demo

Preferred containerized run:

```sh
chmod +x ./run_dual_qemu_docker
./run_dual_qemu_docker
```

Expected final output:

```text
dual chat verification: pass
```

After the run, inspect these logs if needed:

```text
net_dual_sender.raw
net_dual_responder.raw
```

The sender log should include `PASS dual chat` and `SUMMARY failures=0`. The responder log should include receiving `hello from sender`, sending `hello from responder`, and `SUMMARY failures=0`.

For a local run without Docker:

```sh
chmod +x ./run_dual_qemu
./run_dual_qemu
```

## Focused Tests

Useful networking tests:

```sh
make -s net_smoke.test
make -s net_tx.test
make -s net_rx.test
make -s net_queue.test
make -s net_debug.test
make -s net_stats.test
make -s net_arp_cache.test
make -s net_udp.test
make -s net_chat.test
make -s net_proto.test
make -s net_demo.test
```

Live/demo-oriented tests and runners:

```sh
make -s net_real_tx.test
make -s net_live.test
./run_dual_qemu_docker
```

If test directories or generated disk images were changed, run a clean rebuild first:

```sh
make -s clean net_chat.test
```

## Demo Message Files

The two-QEMU chat demo reads simple command files from each guest directory:

- `net_dual_sender.dir/chat_send`
- `net_dual_sender.dir/chat_expect`
- `net_dual_responder.dir/chat_send`
- `net_dual_responder.dir/chat_expect`

Edit `chat_send` to change what a VM sends. Keep the peer `chat_expect` file in sync so the automated verification still passes.

## Documentation

- `REPORT.txt`: final project report and implementation summary.
- `VIRTIO_NET_BRINGUP.md`: virtio-net PCI/device bring-up notes.
- `net_demo.md`: deterministic fake-backend protocol demo.
- `net_dual_demo.md`: two-QEMU chat demo details.
- `net_stats.md`, `net_arp_cache.md`, `net_udp.md`, `net_chat.md`: focused test descriptions.

`REPORT.txt` is intentionally kept even though this README summarizes the project. The README is the repo entry point; `REPORT.txt` is the fuller final-project writeup.

## Known Limitations

- UDP checksums are intentionally set to zero for this project stage.
- The chat demo is scripted rather than interactive.
- ARP behavior can be noisy while waiting for resolution; a future version should track pending ARP requests or rate-limit repeated requests.
- The dual-machine regression verifies one request/reply exchange rather than a long conversation.

## Repository Hygiene

Generated files should not be committed. The `.gitignore` excludes build products and run outputs such as `build/`, `*.img`, `*.data`, `*.raw`, `*.out`, `*.failure`, `*.cycles`, and `*.result`.
