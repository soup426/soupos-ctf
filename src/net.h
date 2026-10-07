#pragma once
#include <stdint.h>

/* A small IPv4 stack: Ethernet, ARP, IP and ICMP echo. Enough to be pinged and
 * to ping, which is the whole of this milestone.
 *
 * Addresses are held in host byte order everywhere in this interface and
 * converted at the packet boundary, so nothing above has to think about
 * endianness. */

#define IPV4(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

/* QEMU's user-mode network is fixed and well known, so these are compiled-in
 * defaults rather than something DHCP has to discover. */
#define NET_DEFAULT_IP   IPV4(10, 0, 2, 15)
#define NET_DEFAULT_GW   IPV4(10, 0, 2, 2)
#define NET_DEFAULT_MASK IPV4(255, 255, 255, 0)
#define NET_DEFAULT_DNS  IPV4(10, 0, 2, 3)     /* SLIRP's resolver */

void     net_init(void);

uint32_t net_ip(void);
uint32_t net_gw(void);
uint32_t net_mask(void);
void     net_set_ip(uint32_t ip);
void     net_set_mask(uint32_t mask);
/* Whether the current address came from DHCP (set by the client) or is the
 * compiled-in fallback / something typed into `plumbing`. */
int      net_leased(void);
void     net_set_leased(int on);
void     net_set_gw(uint32_t gw);

/* Send `count` echo requests to `dst`, one every 100 ms, and return how many
 * replies came back. Writes per-reply lines to the console as it goes. */
int      net_ping(uint32_t dst, int count);

/* ARP cache: resolve (sending a request and waiting if needed), and dump. */
int      net_arp_resolve(uint32_t ip, uint8_t *mac_out);
void     net_arp_dump(void);

/* Send one UDP datagram. Returns 0 if it went to the card. */
int      net_udp_send(uint32_t dst, uint16_t sport, uint16_t dport,
                      const void *data, uint16_t len);

/* The same, with an explicit source address, for the case where we do not have
 * one yet (DHCP). A destination of 255.255.255.255 goes to the Ethernet
 * broadcast address without trying to ARP for it. */
int      net_udp_send_from(uint32_t src, uint32_t dst, uint16_t sport,
                           uint16_t dport, const void *data, uint16_t len);

/* Arm the receiver for `port`. Call this BEFORE sending the request it
 * answers: a reply can arrive in the interrupt handler before the send returns,
 * and anything received before the port is armed is dropped. */
void     net_udp_listen(uint16_t port);

/* Block for one datagram on `port`, up to `ticks` (100 Hz). Returns its
 * length, or -1 on timeout. One outstanding request at a time. */
int      net_udp_wait(uint16_t port, uint8_t *out, uint16_t max, uint32_t ticks);

/* Accept IP datagrams not addressed to us. Needed only while taking a DHCP
 * lease, since the reply arrives before we have an address. */
void     net_accept_any_dst(int on);

/* Take an address by DHCP. Returns 0 on success, -1 if nothing answered. */
int      net_dhcp(void);

/* Hand a built payload to the IP layer. The next hop must already be resolved
 * (net_arp_resolve), because this does not block. */
void     net_ip_send(uint32_t dst, uint8_t proto, const void *payload, uint16_t plen);

/* The pseudo-header checksum UDP and TCP share. */
uint16_t net_pseudo_checksum(uint32_t src, uint32_t dst, uint8_t proto,
                             const uint8_t *data, uint16_t len);

/* Resolve a hostname to an IPv4 address through the configured resolver.
 * Returns 0 and writes *out on success, -1 on timeout or a failed lookup. */
int      net_dns_resolve(const char *name, uint32_t *out);
uint32_t net_dns(void);
void     net_set_dns(uint32_t ip);

/* Counters for `netinfo`: frames handled by each protocol. */
void     net_stats(uint32_t *arp, uint32_t *ip, uint32_t *icmp, uint32_t *udp,
                   uint32_t *other);
