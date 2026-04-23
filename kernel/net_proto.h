#pragma once
#include <cstdint>

void net_handle_frame(const uint8_t* data, std::size_t len);
bool net_poll_once();
void net_set_identity(const uint8_t mac[6], const uint8_t ip[4]);
