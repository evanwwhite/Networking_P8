#pragma once

#include <cstdint>

constexpr uint16_t NET_CHAT_PORT = 4390;
constexpr std::size_t NET_CHAT_TEXT_SIZE = 128;
constexpr uint8_t NET_CHAT_HISTORY_SIZE = 8;

struct ChatMessage {
  bool valid;
  uint8_t src_ip[4];
  uint16_t src_port;
  char text[NET_CHAT_TEXT_SIZE];
};

bool net_chat_init();
void net_chat_reset();
bool net_chat_send(const uint8_t dst_ip[4], const char *text);
bool net_chat_recv(char *out, std::size_t max_len);
bool net_chat_poll();
void net_chat_print_history();
