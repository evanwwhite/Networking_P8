#include "net_proto.h"

// AI assistance note: AI was used to help plan, review, and document this
// networking code. The implementation was integrated, tested, and reviewed by
// the team.

#include "arp.h"
#include "arp_cache.h"
#include "ethernet.h"
#include "icmp.h"
#include "ipv4.h"
#include "net_stats.h"
#include "print.h"
#include "udp.h"
#include "virtio_net.h"

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

static bool payload_is_printable_ascii(const uint8_t* data, std::size_t len) {
    if (data == nullptr || len == 0) return false;

    for (std::size_t i = 0; i < len; i++) {
        const uint8_t ch = data[i];
        if (ch < 32 || ch > 126) return false;
    }
    return true;
}

static void log_ascii_payload(const char* prefix, const uint8_t* data, std::size_t len) {
    if (!payload_is_printable_ascii(data, len)) return;

    constexpr std::size_t k_max_logged_payload = 63;
    char text[k_max_logged_payload + 1] = {};
    const std::size_t to_copy = (len < k_max_logged_payload) ? len : k_max_logged_payload;
    for (std::size_t i = 0; i < to_copy; i++) {
        text[i] = char(data[i]);
    }
    text[to_copy] = 0;

    KPRINT("net: ? \"?\"\n", prefix, text);
}

static void trace_mac(const char* prefix, const uint8_t mac[6]) {
    KPRINT("net: ? ?:?:?:?:?:?\n", prefix, mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5]);
}

static void trace_drop(const char* reason) {
    KPRINT("net: DROP ?\n", reason);
}

static void log_ip_inline(const uint8_t ip[4]) {
    KPRINT("?.?.?.?", Dec(ip[0]), Dec(ip[1]), Dec(ip[2]), Dec(ip[3]));
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
    if (len < sizeof(EthernetHeader) + sizeof(ArpPacket)) {
        net_stats_increment(NetStatCounter::DroppedShort);
        trace_drop("short ARP frame");
        return;
    }

    auto eth = reinterpret_cast<const EthernetHeader*>(data);
    auto arp = reinterpret_cast<const ArpPacket*>(data + sizeof(EthernetHeader));
    uint16_t op = bswap16(arp->oper);

    if (arp->hlen == 6 && arp->plen == 4 &&
        bswap16(arp->htype) == ARP_HTYPE_ETHERNET &&
        bswap16(arp->ptype) == ARP_PTYPE_IPV4) {
        arp_cache_insert(arp->spa, arp->sha);
    }

    // Only answer ARP requests, not ARP replies
    if (op == ARP_OP_REPLY) {
        net_stats_increment(NetStatCounter::ArpRx);
        KPRINT("net: RX ARP reply ");
        log_ip_inline(arp->spa);
        KPRINT(" is-at ?:?:?:?:?:?\n", arp->sha[0], arp->sha[1], arp->sha[2],
               arp->sha[3], arp->sha[4], arp->sha[5]);
        return;
    }

    if (op != ARP_OP_REQUEST) {
        return;
    }

    // Only answer if the requested IP is our IP
    if (!ip_equals(arp->tpa, g_my_ip)) {
        net_stats_increment(NetStatCounter::DroppedNotForMe);
        trace_drop("ARP request for another IP");
        return;
    }

    net_stats_increment(NetStatCounter::ArpRx);
    KPRINT("net: RX ARP who-has ?.?.?.? from ?.?.?.?\n", Dec(arp->tpa[0]),
           Dec(arp->tpa[1]), Dec(arp->tpa[2]), Dec(arp->tpa[3]),
           Dec(arp->spa[0]), Dec(arp->spa[1]), Dec(arp->spa[2]),
           Dec(arp->spa[3]));
    trace_mac("RX ARP sender MAC", arp->sha);

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

    if (net_send_raw(reply, sizeof(reply))) {
        net_stats_increment(NetStatCounter::ArpTx);
        KPRINT("net: TX ARP reply ?.?.?.? is-at ?:?:?:?:?:?\n",
               Dec(g_my_ip[0]), Dec(g_my_ip[1]), Dec(g_my_ip[2]),
               Dec(g_my_ip[3]), g_my_mac[0], g_my_mac[1], g_my_mac[2],
               g_my_mac[3], g_my_mac[4], g_my_mac[5]);
    }
}

bool net_send_arp_request(const uint8_t target_ip[4]) {
    if (target_ip == nullptr) {
        return false;
    }

    uint8_t request[sizeof(EthernetHeader) + sizeof(ArpPacket)] = {};
    auto out_eth = reinterpret_cast<EthernetHeader*>(request);
    auto out_arp = reinterpret_cast<ArpPacket*>(request + sizeof(EthernetHeader));
    uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    uint8_t zero_mac[6] = {};

    copy_bytes(out_eth->dst, broadcast, 6);
    copy_bytes(out_eth->src, g_my_mac, 6);
    out_eth->ether_type = bswap16(ETH_TYPE_ARP);

    out_arp->htype = bswap16(ARP_HTYPE_ETHERNET);
    out_arp->ptype = bswap16(ARP_PTYPE_IPV4);
    out_arp->hlen = 6;
    out_arp->plen = 4;
    out_arp->oper = bswap16(ARP_OP_REQUEST);
    copy_bytes(out_arp->sha, g_my_mac, 6);
    copy_bytes(out_arp->spa, g_my_ip, 4);
    copy_bytes(out_arp->tha, zero_mac, 6);
    copy_bytes(out_arp->tpa, target_ip, 4);

    if (!net_send_raw(request, sizeof(request))) {
        return false;
    }

    net_stats_increment(NetStatCounter::ArpTx);
    KPRINT("net: TX ARP who-has ");
    log_ip_inline(target_ip);
    KPRINT(" tell ");
    log_ip_inline(g_my_ip);
    KPRINT("\n");
    return true;
}

// Handle IPv4 packets.
// For this project, we only care about ICMP ping requests.
static void handle_ipv4(const uint8_t* data, std::size_t len) {
    if (len < sizeof(EthernetHeader) + sizeof(Ipv4Header)) {
        net_stats_increment(NetStatCounter::DroppedShort);
        trace_drop("short IPv4 frame");
        return;
    }

    auto eth = reinterpret_cast<const EthernetHeader*>(data);
    auto ip  = reinterpret_cast<const Ipv4Header*>(data + sizeof(EthernetHeader));

    // Find real IPv4 header size
    std::size_t ip_hdr_len = ipv4_header_length(ip);
    if (ip_hdr_len < sizeof(Ipv4Header)) {
        net_stats_increment(NetStatCounter::DroppedShort);
        trace_drop("bad IPv4 header length");
        return;
    }

    if (len < sizeof(EthernetHeader) + ip_hdr_len) {
        net_stats_increment(NetStatCounter::DroppedShort);
        trace_drop("truncated IPv4 header");
        return;
    }

    if (checksum16(reinterpret_cast<const uint8_t*>(ip), ip_hdr_len) != 0) {
        net_stats_increment(NetStatCounter::DroppedBadChecksum);
        trace_drop("bad IPv4 checksum");
        return;
    }

    // Ignore packets not addressed to our IP
    if (!ip_is_for_me(ip)) {
        net_stats_increment(NetStatCounter::DroppedNotForMe);
        trace_drop("IPv4 packet for another IP");
        return;
    }

    net_stats_increment(NetStatCounter::Ipv4Rx);
    KPRINT("net: RX IPv4 proto=? ?.?.?.? -> ?.?.?.?\n", Dec(ip->protocol),
           Dec(ip->src_ip[0]), Dec(ip->src_ip[1]), Dec(ip->src_ip[2]),
           Dec(ip->src_ip[3]), Dec(ip->dst_ip[0]), Dec(ip->dst_ip[1]),
           Dec(ip->dst_ip[2]), Dec(ip->dst_ip[3]));

    // total_length is the IPv4 packet size: [IPv4 header + ICMP + payload]
    std::size_t ip_total_len = bswap16(ip->total_length);
    if (ip_total_len < ip_hdr_len) {
        net_stats_increment(NetStatCounter::DroppedShort);
        trace_drop("bad IPv4 total length");
        return;
    }

    // Make sure the whole IPv4 packet is actually present in the frame
    if (sizeof(EthernetHeader) + ip_total_len > len) {
        net_stats_increment(NetStatCounter::DroppedShort);
        trace_drop("truncated IPv4 payload");
        return;
    }

    if (ip->protocol == IPV4_PROTO_UDP) {
        const uint8_t* udp_data = data + sizeof(EthernetHeader) + ip_hdr_len;
        const std::size_t udp_len = ip_total_len - ip_hdr_len;
        udp_handle_packet(ip->src_ip, udp_data, udp_len);
        return;
    }

    // Ignore non-ICMP IPv4 packets until another protocol is registered.
    if (ip->protocol != IPV4_PROTO_ICMP) {
        net_stats_increment(NetStatCounter::DroppedUnknownIpv4Protocol);
        trace_drop("unsupported IPv4 protocol");
        return;
    }

    // Need enough bytes for Ethernet + IPv4 + ICMP header
    if (ip_total_len < ip_hdr_len + sizeof(IcmpEchoHeader)) {
        net_stats_increment(NetStatCounter::DroppedShort);
        trace_drop("short ICMP frame");
        return;
    }

    std::size_t icmp_off = icmp_offset(ip);
    auto icmp = reinterpret_cast<const IcmpEchoHeader*>(data + icmp_off);

    // Only answer ping requests
    if (icmp->type != ICMP_ECHO_REQUEST) return;

    std::size_t icmp_len = ip_total_len - ip_hdr_len;
    if (checksum16(reinterpret_cast<const uint8_t*>(icmp), icmp_len) != 0) {
        net_stats_increment(NetStatCounter::DroppedBadChecksum);
        trace_drop("bad ICMP checksum");
        return;
    }

    net_stats_increment(NetStatCounter::IcmpRx);
    std::size_t payload_len = icmp_len - sizeof(IcmpEchoHeader);
    const uint8_t* in_payload = data + icmp_off + sizeof(IcmpEchoHeader);

    log_ascii_payload("icmp echo payload", in_payload, payload_len);
    KPRINT("net: RX ICMP echo id=? seq=? payload_len=?\n",
           Dec(bswap16(icmp->identifier)), Dec(bswap16(icmp->sequence)),
           Dec(payload_len));

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
    if (net_send_raw(reply, reply_len)) {
        net_stats_increment(NetStatCounter::Ipv4Tx);
        net_stats_increment(NetStatCounter::IcmpTx);
        KPRINT("net: TX ICMP echo reply id=? seq=? payload_len=?\n",
               Dec(bswap16(out_icmp->identifier)),
               Dec(bswap16(out_icmp->sequence)), Dec(payload_len));
    }
}

bool net_send_ipv4(const uint8_t dst_ip[4], uint8_t protocol,
                   const uint8_t* payload, std::size_t payload_len) {
    if (dst_ip == nullptr || (payload == nullptr && payload_len != 0)) {
        return false;
    }

    const std::size_t ip_total_len = sizeof(Ipv4Header) + payload_len;
    const std::size_t frame_len = sizeof(EthernetHeader) + ip_total_len;
    if (payload_len > VIRTIO_NET_MAX_FRAME_SIZE ||
        frame_len > VIRTIO_NET_MAX_FRAME_SIZE ||
        ip_total_len > 0xffffU) {
        return false;
    }

    uint8_t dst_mac[6]{};
    // ARP cache lookup keeps IPv4 sending independent from Ethernet address
    // resolution details.
    if (!arp_cache_lookup(dst_ip, dst_mac)) {
        KPRINT("net: ARP lookup ");
        log_ip_inline(dst_ip);
        KPRINT(" miss, sending request\n");
        net_send_arp_request(dst_ip);
        return false;
    }

    uint8_t frame[VIRTIO_NET_MAX_FRAME_SIZE] = {};
    auto eth = reinterpret_cast<EthernetHeader*>(frame);
    auto ip = reinterpret_cast<Ipv4Header*>(frame + sizeof(EthernetHeader));
    uint8_t* out_payload = frame + sizeof(EthernetHeader) + sizeof(Ipv4Header);

    copy_bytes(eth->dst, dst_mac, 6);
    copy_bytes(eth->src, g_my_mac, 6);
    eth->ether_type = bswap16(ETH_TYPE_IPV4);

    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_length = bswap16(uint16_t(ip_total_len));
    ip->identification = bswap16(0x2222);
    ip->flags_fragment = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->header_checksum = 0;
    copy_bytes(ip->src_ip, g_my_ip, 4);
    copy_bytes(ip->dst_ip, dst_ip, 4);
    if (payload_len != 0) {
        copy_bytes(out_payload, payload, payload_len);
    }
    ip->header_checksum =
        bswap16(checksum16(reinterpret_cast<const uint8_t*>(ip),
                           sizeof(Ipv4Header)));

    if (!net_send_raw(frame, frame_len)) {
        return false;
    }

    net_stats_increment(NetStatCounter::Ipv4Tx);
    KPRINT("net: TX IPv4 proto=? ", Dec(protocol));
    log_ip_inline(g_my_ip);
    KPRINT(" -> ");
    log_ip_inline(dst_ip);
    KPRINT(" payload_len=?\n", Dec(payload_len));
    return true;
}

// Main entry point for the protocol layer.
// Person 2 gives us a raw Ethernet frame here.
// We inspect the outer Ethernet header and choose what to do.
void net_handle_frame(const uint8_t* data, std::size_t len) {
    if (len < sizeof(EthernetHeader)) {
        net_stats_increment(NetStatCounter::DroppedShort);
        trace_drop("short Ethernet frame");
        return;
    }

    auto eth = reinterpret_cast<const EthernetHeader*>(data);

    // First check whether this frame is even meant for us
    if (!mac_is_for_me(eth->dst)) {
        net_stats_increment(NetStatCounter::DroppedNotForMe);
        trace_mac("drop dst MAC", eth->dst);
        trace_drop("Ethernet frame for another MAC");
        return;
    }

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
    net_stats_increment(NetStatCounter::DroppedUnknownEthertype);
    trace_drop("unknown Ethernet type");
}

bool net_poll_once() {
    uint8_t frame[VIRTIO_NET_MAX_FRAME_SIZE] = {};
    int recv_len = net_recv_raw(frame, sizeof(frame));
    if (recv_len <= 0) return false;

    net_handle_frame(frame, std::size_t(recv_len));
    return true;
}

void net_set_identity(const uint8_t mac[6], const uint8_t ip[4]) {
    if (mac != nullptr) {
        copy_bytes(g_my_mac, mac, 6);
    }
    if (ip != nullptr) {
        copy_bytes(g_my_ip, ip, 4);
    }
}
