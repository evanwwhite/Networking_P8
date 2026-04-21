#include "net_proto.h"

#include "arp.h"
#include "ethernet.h"
#include "icmp.h"
#include "ipv4.h"

// Convert a 16-bit value between byte orders.
// Network packets store multi-byte numbers in big-endian order.
// Our CPU is little-endian, so we swap when reading/writing packet fields.
static uint16_t bswap16(uint16_t x) {
    return uint16_t((x >> 8) | (x << 8));
}

extern bool net_send_raw(const uint8_t* data, std::size_t len);

// Temporary identity for our OS on the network.
// Later this should probably come from Person 1 / dev ice setup.
static uint8_t g_my_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static uint8_t g_my_ip[4]  = {10, 0, 2, 15};

// Compare two IPv4 addresses.
// Returns true only if all 4 bytes match.
static bool ip_equals(const uint8_t a[4], const uint8_t b[4]) {
    for (int i = 0; i < 4; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

// Copy raw bytes from src into dst.
// Read this as: copy_bytes(to, from, how_many).
static void copy_bytes(uint8_t* dst, const uint8_t* src, std::size_t n) {
    for (std::size_t i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

// Check whether a MAC address is the broadcast address FF:FF:FF:FF:FF:FF.
// Broadcast means "send this frame to everyone on the local network".
static bool mac_is_broadcast(const uint8_t mac[6]) {
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0xFF) return false;
    }
    return true;
}

// Check whether an Ethernet frame is relevant to us.
// We accept frames sent either:
// 1. directly to our MAC address
// 2. to the broadcast MAC address
static bool mac_is_for_me(const uint8_t mac[6]) {
    if (mac_is_broadcast(mac)) return true;

    for (int i = 0; i < 6; i++) {
        if (mac[i] != g_my_mac[i]) return false;
    }
    return true;
}

// Compute the standard Internet checksum.
// IPv4 headers use this.
// ICMP messages also use this.
// Main idea: after building/modifying a packet, recompute checksum so it is valid.
static uint16_t checksum16(const uint8_t* data, std::size_t len) {
    uint32_t sum = 0;

    // Add 16-bit words
    for (std::size_t i = 0; i + 1 < len; i += 2) {
        uint16_t word = (uint16_t(data[i]) << 8) | uint16_t(data[i + 1]);
        sum += word;
    }

    // If there is one leftover byte, pad it on the right
    if (len & 1) {
        sum += uint16_t(data[len - 1]) << 8;
    }

    // Fold carries back into 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // Final checksum is the one's complement
    return uint16_t(~sum);
}

// The IPv4 header is not always exactly 20 bytes.
// The low 4 bits of version_ihl tell us the header length in 32-bit words.
// Multiply by 4 to get bytes.
static std::size_t ipv4_header_length(const Ipv4Header* ip) {
    return std::size_t(ip->version_ihl & 0x0F) * 4;
}

// Check whether this IPv4 packet is addressed to our OS.
static bool ip_is_for_me(const Ipv4Header* ip) {
    return ip_equals(ip->dst_ip, g_my_ip);
}

// Build the IPv4 part of a reply packet.
// Main idea:
// - keep it as a valid IPv4 packet
// - make the reply come FROM me
// - send the reply back TO whoever sent the request
static void copy_ip_header_basic(Ipv4Header* dst, const Ipv4Header* src) {
    dst->version_ihl     = src->version_ihl;
    dst->tos             = 0;
    dst->total_length    = src->total_length;
    dst->identification  = src->identification;
    dst->flags_fragment  = src->flags_fragment;
    dst->ttl             = 64;
    dst->protocol        = src->protocol;
    dst->header_checksum = 0;   // must be recomputed later

    // Reply source IP = my IP
    copy_bytes(dst->src_ip, g_my_ip, 4);

    // Reply destination IP = original sender's IP
    copy_bytes(dst->dst_ip, src->src_ip, 4);
}

// Build the Ethernet part of a reply frame.
// Main idea:
// - send the frame back to whoever sent the original frame
// - mark my MAC as the sender
static void copy_eth_header_reply(EthernetHeader* dst, const EthernetHeader* src) {
    // Reply destination MAC = original sender MAC
    copy_bytes(dst->dst, src->src, 6);

    // Reply source MAC = my MAC
    copy_bytes(dst->src, g_my_mac, 6);

    // Keep the same Ethernet type (ARP stays ARP, IPv4 stays IPv4)
    dst->ether_type = src->ether_type;
}

// Build the ICMP header for a ping reply.
// Main idea:
// - change request into reply
// - keep identifier/sequence so the sender can match the reply
static void copy_icmp_echo_reply(IcmpEchoHeader* dst, const IcmpEchoHeader* src) {
    dst->type = ICMP_ECHO_REPLY;   // request(8) -> reply(0)
    dst->code = 0;
    dst->checksum = 0;             // must be recomputed later
    dst->identifier = src->identifier;
    dst->sequence = src->sequence;
}

// Copy the ping payload unchanged.
// Ping replies usually send back the same extra data they received.
static void copy_payload(uint8_t* dst, const uint8_t* src, std::size_t len) {
    copy_bytes(dst, src, len);
}

// Compute where the ICMP message starts inside the frame.
// Layout is:
// [ Ethernet header ][ IPv4 header ][ ICMP header ][ payload ]
static std::size_t icmp_offset(const Ipv4Header* ip) {
    return sizeof(EthernetHeader) + ipv4_header_length(ip);
}

// Handle ARP packets.
// ARP asks: "Who has this IP address?"
// If the question is asking for our IP, we send back our MAC address.
static void handle_arp(const uint8_t* data, std::size_t len) {
    if (len < sizeof(EthernetHeader) + sizeof(ArpPacket)) return;

    auto eth = reinterpret_cast<const EthernetHeader*>(data);
    auto arp = reinterpret_cast<const ArpPacket*>(data + sizeof(EthernetHeader));

    // Only answer ARP requests, not ARP replies
    if (bswap16(arp->oper) != ARP_OP_REQUEST) return;

    // Only answer if the requested IP is our IP
    if (!ip_equals(arp->tpa, g_my_ip)) return;

    // Build a full Ethernet+ARP reply packet
    uint8_t reply[sizeof(EthernetHeader) + sizeof(ArpPacket)] = {};

    auto out_eth = reinterpret_cast<EthernetHeader*>(reply);
    auto out_arp = reinterpret_cast<ArpPacket*>(reply + sizeof(EthernetHeader));

    // Ethernet reply: back to the sender
    copy_bytes(out_eth->dst, eth->src, 6);
    copy_bytes(out_eth->src, g_my_mac, 6);
    out_eth->ether_type = bswap16(ETH_TYPE_ARP);

    // ARP reply fields
    out_arp->htype = bswap16(ARP_HTYPE_ETHERNET);
    out_arp->ptype = bswap16(ARP_PTYPE_IPV4);
    out_arp->hlen = 6;                   // MAC address length
    out_arp->plen = 4;                   // IPv4 address length
    out_arp->oper = bswap16(ARP_OP_REPLY);

    // "I am the owner of this IP"
    copy_bytes(out_arp->sha, g_my_mac, 6);  // sender hardware address = my MAC
    copy_bytes(out_arp->spa, g_my_ip, 4);   // sender protocol address = my IP

    // Send reply back to original requester
    copy_bytes(out_arp->tha, arp->sha, 6);  // target hardware address = their MAC
    copy_bytes(out_arp->tpa, arp->spa, 4);  // target protocol address = their IP

    net_send_raw(reply, sizeof(reply));
}

// Handle IPv4 packets.
// For this project, we only care about ICMP ping requests.
static void handle_ipv4(const uint8_t* data, std::size_t len) {
    if (len < sizeof(EthernetHeader) + sizeof(Ipv4Header)) return;

    auto eth = reinterpret_cast<const EthernetHeader*>(data);
    auto ip  = reinterpret_cast<const Ipv4Header*>(data + sizeof(EthernetHeader));

    // Ignore packets not addressed to our IP
    if (!ip_is_for_me(ip)) return;

    // Ignore non-ICMP IPv4 packets
    if (ip->protocol != IPV4_PROTO_ICMP) return;

    // Find real IPv4 header size
    std::size_t ip_hdr_len = ipv4_header_length(ip);
    if (ip_hdr_len < sizeof(Ipv4Header)) return;

    // Need enough bytes for Ethernet + IPv4 + ICMP header
    if (len < sizeof(EthernetHeader) + ip_hdr_len + sizeof(IcmpEchoHeader)) return;

    std::size_t icmp_off = icmp_offset(ip);
    auto icmp = reinterpret_cast<const IcmpEchoHeader*>(data + icmp_off);

    // Only answer ping requests
    if (icmp->type != ICMP_ECHO_REQUEST) return;

    // total_length is the IPv4 packet size: [IPv4 header + ICMP + payload]
    std::size_t ip_total_len = bswap16(ip->total_length);
    if (ip_total_len < ip_hdr_len + sizeof(IcmpEchoHeader)) return;

    // Make sure the whole IPv4 packet is actually present in the frame
    if (sizeof(EthernetHeader) + ip_total_len > len) return;

    std::size_t icmp_len = ip_total_len - ip_hdr_len;
    std::size_t payload_len = icmp_len - sizeof(IcmpEchoHeader);

    // Build reply in a temporary buffer.
    // 1514 is a normal Ethernet frame-sized buffer.
    uint8_t reply[1514] = {};
    std::size_t reply_len = sizeof(EthernetHeader) + ip_hdr_len + icmp_len;

    auto out_eth  = reinterpret_cast<EthernetHeader*>(reply);
    auto out_ip   = reinterpret_cast<Ipv4Header*>(reply + sizeof(EthernetHeader));
    auto out_icmp = reinterpret_cast<IcmpEchoHeader*>(reply + sizeof(EthernetHeader) + ip_hdr_len);

    // Build each layer of the reply
    copy_eth_header_reply(out_eth, eth);
    copy_ip_header_basic(out_ip, ip);
    copy_icmp_echo_reply(out_icmp, icmp);

    // Copy the ping payload unchanged
    const uint8_t* in_payload = data + icmp_off + sizeof(IcmpEchoHeader);
    uint8_t* out_payload = reply + sizeof(EthernetHeader) + ip_hdr_len + sizeof(IcmpEchoHeader);
    copy_payload(out_payload, in_payload, payload_len);

    // Recompute IPv4 checksum after building the reply header
    out_ip->header_checksum = 0;
    out_ip->header_checksum =
        bswap16(checksum16(reinterpret_cast<const uint8_t*>(out_ip), ip_hdr_len));

    // Recompute ICMP checksum after building the reply ICMP message
    out_icmp->checksum = 0;
    out_icmp->checksum =
        bswap16(checksum16(reinterpret_cast<const uint8_t*>(out_icmp), icmp_len));

    // Send the finished reply frame
    net_send_raw(reply, reply_len);
}

// Main entry point for the protocol layer.
// Person 2 gives us a raw Ethernet frame here.
// We inspect the outer Ethernet header and choose what to do.
void net_handle_frame(const uint8_t* data, std::size_t len) {
    if (len < sizeof(EthernetHeader)) return;

    auto eth = reinterpret_cast<const EthernetHeader*>(data);

    // First check whether this frame is even meant for us
    if (!mac_is_for_me(eth->dst)) return;

    // Then decide what packet type is inside Ethernet
    uint16_t ether_type = bswap16(eth->ether_type);

    if (ether_type == ETH_TYPE_ARP) {
        handle_arp(data, len);
        return;
    }

    if (ether_type == ETH_TYPE_IPV4) {
        handle_ipv4(data, len);
        return;
    }

    // Ignore anything else for now
}
