#include "net_chat.h"

// AI assistance note: AI was used to help plan, review, and document this
// networking code. The implementation was integrated, tested, and reviewed by
// the team.

#include "atomic.h"
#include "net_proto.h"
#include "print.h"
#include "spin_lock.h"
#include "udp.h"

namespace {

SpinLock g_chat_lock{};
ChatMessage g_history[NET_CHAT_HISTORY_SIZE]{};
ChatMessage g_inbox[NET_CHAT_HISTORY_SIZE]{};
uint8_t g_history_next = 0;
uint8_t g_history_count = 0;
uint8_t g_inbox_read = 0;
uint8_t g_inbox_write = 0;
uint8_t g_inbox_count = 0;

void copy_bytes(uint8_t *dst, const uint8_t *src, std::size_t len) {
  for (std::size_t i = 0; i < len; ++i) {
    dst[i] = src[i];
  }
}

bool is_printable(uint8_t ch) { return ch >= 32 && ch <= 126; }

std::size_t sanitize_payload(char *out, const uint8_t *payload,
                             std::size_t payload_len) {
  if (out == nullptr) {
    return 0;
  }

  const std::size_t limit = NET_CHAT_TEXT_SIZE - 1;
  const std::size_t to_copy = payload_len < limit ? payload_len : limit;
  for (std::size_t i = 0; i < to_copy; ++i) {
    out[i] = is_printable(payload[i]) ? char(payload[i]) : '.';
  }
  out[to_copy] = 0;
  return to_copy;
}

std::size_t sanitize_cstr(char *out, const char *text) {
  if (out == nullptr || text == nullptr) {
    return 0;
  }

  const std::size_t limit = NET_CHAT_TEXT_SIZE - 1;
  std::size_t len = 0;
  while (len < limit && text[len] != 0) {
    const uint8_t ch = uint8_t(text[len]);
    out[len] = is_printable(ch) ? char(ch) : '.';
    ++len;
  }
  out[len] = 0;
  return len;
}

void store_message_locked(const ChatMessage &message) {
  g_history[g_history_next] = message;
  g_history_next = uint8_t((g_history_next + 1) % NET_CHAT_HISTORY_SIZE);
  if (g_history_count < NET_CHAT_HISTORY_SIZE) {
    ++g_history_count;
  }

  if (g_inbox_count == NET_CHAT_HISTORY_SIZE) {
    g_inbox_read = uint8_t((g_inbox_read + 1) % NET_CHAT_HISTORY_SIZE);
    --g_inbox_count;
  }

  g_inbox[g_inbox_write] = message;
  g_inbox_write = uint8_t((g_inbox_write + 1) % NET_CHAT_HISTORY_SIZE);
  ++g_inbox_count;
}

void print_ip(const uint8_t ip[4]) {
  KPRINT("?.?.?.?", Dec(ip[0]), Dec(ip[1]), Dec(ip[2]), Dec(ip[3]));
}

void chat_udp_handler(const uint8_t src_ip[4], uint16_t src_port,
                      const uint8_t *payload, std::size_t payload_len) {
  // Chat payloads are sanitized before logging so packet data cannot corrupt
  // the serial output used by automated tests.
  ChatMessage message{};
  message.valid = true;
  copy_bytes(message.src_ip, src_ip, 4);
  message.src_port = src_port;
  sanitize_payload(message.text, payload, payload_len);

  {
    LockGuard guard{g_chat_lock};
    store_message_locked(message);
  }

  KPRINT("chat: ");
  print_ip(src_ip);
  KPRINT(" > ?\n", message.text);
}

} // namespace

bool net_chat_init() {
  return udp_register_handler(NET_CHAT_PORT, chat_udp_handler);
}

void net_chat_reset() {
  LockGuard guard{g_chat_lock};
  for (uint8_t i = 0; i < NET_CHAT_HISTORY_SIZE; ++i) {
    g_history[i] = {};
    g_inbox[i] = {};
  }
  g_history_next = 0;
  g_history_count = 0;
  g_inbox_read = 0;
  g_inbox_write = 0;
  g_inbox_count = 0;
}

bool net_chat_send(const uint8_t dst_ip[4], const char *text) {
  if (dst_ip == nullptr || text == nullptr || !net_chat_init()) {
    return false;
  }

  char sanitized[NET_CHAT_TEXT_SIZE]{};
  std::size_t len = sanitize_cstr(sanitized, text);
  if (len == 0) {
    return false;
  }

  return udp_send_to(dst_ip, NET_CHAT_PORT, NET_CHAT_PORT,
                     reinterpret_cast<const uint8_t *>(sanitized), len);
}

bool net_chat_recv(char *out, std::size_t max_len) {
  if (out == nullptr || max_len == 0) {
    return false;
  }

  LockGuard guard{g_chat_lock};
  if (g_inbox_count == 0) {
    out[0] = 0;
    return false;
  }

  ChatMessage message = g_inbox[g_inbox_read];
  g_inbox[g_inbox_read] = {};
  g_inbox_read = uint8_t((g_inbox_read + 1) % NET_CHAT_HISTORY_SIZE);
  --g_inbox_count;

  std::size_t len = 0;
  while (len + 1 < max_len && message.text[len] != 0) {
    out[len] = message.text[len];
    ++len;
  }
  out[len] = 0;
  return true;
}

bool net_chat_poll() {
  if (!net_chat_init()) {
    return false;
  }
  return net_poll_once();
}

void net_chat_print_history() {
  LockGuard guard{g_chat_lock};
  KPRINT("chat: history\n");

  uint8_t start = 0;
  if (g_history_count == NET_CHAT_HISTORY_SIZE) {
    start = g_history_next;
  }

  for (uint8_t i = 0; i < g_history_count; ++i) {
    uint8_t idx = uint8_t((start + i) % NET_CHAT_HISTORY_SIZE);
    if (!g_history[idx].valid) {
      continue;
    }
    KPRINT("chat:   ");
    print_ip(g_history[idx].src_ip);
    KPRINT(":? > ?\n", Dec(g_history[idx].src_port), g_history[idx].text);
  }
}
