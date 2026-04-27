# net_chat

AI assistance note: AI was used as a development aid for planning, debugging,
review, and documentation. The team reviewed the output and is responsible for
the submitted work.

This test exercises the Phase 4 UDP chat layer.

It verifies that:

- the chat layer registers a UDP handler on `NET_CHAT_PORT`
- UDP payloads on the chat port are stored and can be received
- history printing keeps received messages after `net_chat_recv`
- non-printable payload bytes are sanitized
- long payloads are truncated safely
- `net_chat_send` returns pending/false and sends ARP when the peer MAC is unknown
- `net_chat_send` emits a valid Ethernet+IPv4+UDP chat packet when ARP is cached
- UDP, ARP, and IPv4 stats update correctly

Run it with:

```sh
make -s net_chat.test
```
