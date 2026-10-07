/* net.c - Ethernet, ARP, IPv4 and ICMP echo.
 *
 * Structure: rtl8139.c hands every received frame to net_rx(), which runs in
 * the card's interrupt handler. That is not what a grown-up kernel does, but
 * this kernel's handlers already do real work, the protocol handling here is
 * tens of microseconds, and inventing a deferred-work mechanism for one driver
 * would be the wrong order to build things in.
 *
 * Byte order: everything on the wire is big-endian and everything in this
 * file's interfaces is host order. The conversion happens in the header
 * builders and parsers, nowhere else, so there is one place to get it wrong.
 */
#include "net.h"
#include "rtl8139.h"
#include "task.h"
#include "timer.h"
#include "vga.h"
#include "klog.h"
#include "str.h"
#include "tcp.h"

/* ── Wire formats ──────────────────────────────────────────────────────── */
#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806
#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP  17
#define IP_PROTO_TCP  6

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

typedef struct { uint8_t dst[6], src[6]; uint16_t type; } __attribute__((packed)) eth_hdr_t;

typedef struct {
    uint16_t htype, ptype;
    uint8_t  hlen, plen;
    uint16_t op;
    uint8_t  sha[6]; uint8_t spa[4];
    uint8_t  tha[6]; uint8_t tpa[4];
} __attribute__((packed)) arp_pkt_t;

typedef struct {
    uint8_t  ver_ihl, tos;
    uint16_t total_len, id, frag;
    uint8_t  ttl, proto;
    uint16_t csum;
    uint8_t  src[4], dst[4];
} __attribute__((packed)) ip_hdr_t;

typedef struct {
    uint8_t  type, code;
    uint16_t csum, id, seq;
} __attribute__((packed)) icmp_hdr_t;

typedef struct {
    uint16_t sport, dport, len, csum;
} __attribute__((packed)) udp_hdr_t;

/* DNS header. Counts are big-endian on the wire like everything else. */
typedef struct {
    uint16_t id, flags, qdcount, ancount, nscount, arcount;
} __attribute__((packed)) dns_hdr_t;

/* ── Byte order and checksums ──────────────────────────────────────────── */
static inline uint16_t hton16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static inline uint16_t ntoh16(uint16_t v) { return hton16(v); }

static void put_ip(uint8_t *p, uint32_t ip) {
    p[0] = (uint8_t)(ip >> 24); p[1] = (uint8_t)(ip >> 16);
    p[2] = (uint8_t)(ip >> 8);  p[3] = (uint8_t)ip;
}
static uint32_t get_ip(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

/* Standard 16-bit one's complement sum, used for both IP and ICMP. */
static uint16_t checksum(const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    while (len > 1) { sum += (uint32_t)((p[0] << 8) | p[1]); p += 2; len -= 2; }
    if (len) sum += (uint32_t)(p[0] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* ── State ─────────────────────────────────────────────────────────────── */
static uint32_t my_ip = NET_DEFAULT_IP, my_gw = NET_DEFAULT_GW, my_mask = NET_DEFAULT_MASK;
static uint32_t my_dns = NET_DEFAULT_DNS;
static uint32_t n_arp, n_ip, n_icmp, n_udp, n_other;
static int      leased;          /* did the address come from DHCP? */

/* One outstanding UDP request at a time, which is all a resolver needs and
 * keeps this free of a socket table. The interrupt handler drops the reply
 * here and the waiting task picks it up. */
#define UDP_RX_MAX 512
static volatile uint16_t udp_want_port;
static volatile int      udp_got;
static volatile uint16_t udp_rx_len;
static uint8_t           udp_rx[UDP_RX_MAX];

#define ARP_MAX 8
static struct { uint32_t ip; uint8_t mac[6]; int valid; } arp_cache[ARP_MAX];

/* Set by the interrupt handler when an echo reply we are waiting for lands. */
static volatile uint16_t ping_want_id, ping_want_seq;
static volatile int      ping_got;

uint32_t net_ip(void)   { return my_ip; }
uint32_t net_gw(void)   { return my_gw; }
uint32_t net_mask(void) { return my_mask; }
void     net_set_ip(uint32_t ip)     { my_ip = ip; leased = 0; }
void     net_set_leased(int on)       { leased = on ? 1 : 0; }
int      net_leased(void)             { return leased; }
void     net_set_mask(uint32_t mask)  { my_mask = mask; }
void     net_set_gw(uint32_t gw)      { my_gw = gw; }

uint32_t net_dns(void) { return my_dns; }
void     net_set_dns(uint32_t ip) { my_dns = ip; }

void net_stats(uint32_t *arp, uint32_t *ip, uint32_t *icmp, uint32_t *udp,
               uint32_t *other) {
    if (arp)   *arp   = n_arp;
    if (ip)    *ip    = n_ip;
    if (icmp)  *icmp  = n_icmp;
    if (udp)   *udp   = n_udp;
    if (other) *other = n_other;
}

static void arp_insert(uint32_t ip, const uint8_t *mac) {
    for (int i = 0; i < ARP_MAX; i++)
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    for (int i = 0; i < ARP_MAX; i++)
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = 1;
            return;
        }
    /* Full: overwrite the first entry. A proper cache would age them out, but
     * eight hosts is already more than this stack talks to. */
    arp_cache[0].ip = ip;
    memcpy(arp_cache[0].mac, mac, 6);
}

static int arp_find(uint32_t ip, uint8_t *out) {
    for (int i = 0; i < ARP_MAX; i++)
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            if (out) memcpy(out, arp_cache[i].mac, 6);
            return 0;
        }
    return -1;
}

/* ── Transmit helpers ──────────────────────────────────────────────────────
 *
 * The staging buffers below are file-scope, and transmits happen from BOTH
 * task context (a ping, a DNS query, tcp_send) and interrupt context (an ICMP
 * echo reply, an ARP reply, a TCP ACK). A task halfway through filling one of
 * them, interrupted by the card, would have its frame overwritten before it
 * reached the hardware.
 *
 * Interrupts off for the build-and-hand-over rather than a mutex: the work is
 * two memcpys of at most 1500 bytes, it never yields, and an interrupt handler
 * cannot wait on a lock anyway. */
static inline uint32_t irq_save(void) {
    uint32_t f;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void irq_restore(uint32_t f) {
    __asm__ volatile ("push %0; popf" :: "r"(f) : "memory", "cc");
}

static const uint8_t eth_bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint8_t txf[ETH_FRAME_MAX];

static void eth_send(const uint8_t *dst_mac, uint16_t type,
                     const void *payload, uint16_t plen) {
    if (plen > ETH_FRAME_MAX - (uint16_t)sizeof(eth_hdr_t)) return;
    uint32_t flags = irq_save();
    eth_hdr_t *e = (eth_hdr_t *)txf;
    memcpy(e->dst, dst_mac, 6);
    memcpy(e->src, rtl8139_mac(), 6);
    e->type = hton16(type);
    memcpy(txf + sizeof(eth_hdr_t), payload, plen);
    rtl8139_send(txf, (uint16_t)(sizeof(eth_hdr_t) + plen));
    irq_restore(flags);
}

static void arp_send(uint16_t op, const uint8_t *target_mac, uint32_t target_ip) {
    arp_pkt_t a;
    a.htype = hton16(1);
    a.ptype = hton16(ETH_TYPE_IP);
    a.hlen  = 6;
    a.plen  = 4;
    a.op    = hton16(op);
    memcpy(a.sha, rtl8139_mac(), 6);
    put_ip(a.spa, my_ip);
    memcpy(a.tha, target_mac, 6);
    put_ip(a.tpa, target_ip);

    eth_send((op == 1) ? eth_bcast : target_mac, ETH_TYPE_ARP, &a, sizeof(a));
}

/* The general form. DHCP needs both of the things it adds over ip_send: a
 * source address of 0.0.0.0, because we do not have one yet when we ask for
 * one, and a broadcast destination, which goes to the Ethernet broadcast
 * address and must not try to ARP for 255.255.255.255. */
static void ip_send_from(uint32_t src, uint32_t dst, uint8_t proto,
                         const void *payload, uint16_t plen) {
    uint8_t mac[6];
    const uint8_t *dmac;

    if (dst == 0xFFFFFFFFu) {
        dmac = eth_bcast;
    } else {
        /* Anything off our subnet goes via the gateway, which is the whole of
         * the routing table. */
        uint32_t next_hop = ((dst & my_mask) == (my_ip & my_mask)) ? dst : my_gw;
        if (arp_find(next_hop, mac) < 0) {
        /* Callers in task context resolve first; this path is also reached
         * from the interrupt handler (an ICMP reply), where blocking to ARP
         * would be wrong. Say so rather than dropping the packet in silence,
         * which is exactly how the DNS query went missing the first time. */
            klog("[net] drop: no arp entry for %u.%u.%u.%u\n",
                 (next_hop >> 24) & 0xFF, (next_hop >> 16) & 0xFF,
                 (next_hop >> 8) & 0xFF, next_hop & 0xFF);
            return;
        }
        dmac = mac;
    }

    static uint8_t buf[ETH_FRAME_MAX];
    static uint16_t ip_id;
    uint32_t flags = irq_save();          /* buf and ip_id are shared too */
    ip_hdr_t *ih = (ip_hdr_t *)buf;

    ih->ver_ihl   = 0x45;
    ih->tos       = 0;
    ih->total_len = hton16((uint16_t)(sizeof(ip_hdr_t) + plen));
    ih->id        = hton16(++ip_id);
    ih->frag      = 0;
    ih->ttl       = 64;
    ih->proto     = proto;
    ih->csum      = 0;
    put_ip(ih->src, src);
    put_ip(ih->dst, dst);
    ih->csum = hton16(checksum(ih, sizeof(ip_hdr_t)));

    memcpy(buf + sizeof(ip_hdr_t), payload, plen);
    eth_send(dmac, ETH_TYPE_IP, buf, (uint16_t)(sizeof(ip_hdr_t) + plen));
    irq_restore(flags);
}

static void ip_send(uint32_t dst, uint8_t proto, const void *payload, uint16_t plen) {
    ip_send_from(my_ip, dst, proto, payload, plen);
}

/* For protocol modules in their own files (tcp.c). Resolves nothing: the
 * caller is in task context and resolves its own next hop first. */
void net_ip_send(uint32_t dst, uint8_t proto, const void *payload, uint16_t plen) {
    ip_send(dst, proto, payload, plen);
}

/* UDP and TCP both checksum a pseudo-header of addresses, protocol and length
 * as well as their own bytes, which is why neither can use checksum() alone. */
uint16_t net_pseudo_checksum(uint32_t src, uint32_t dst, uint8_t proto,
                             const uint8_t *udp, uint16_t len) {
    uint32_t sum = 0;
    sum += (src >> 16) & 0xFFFF; sum += src & 0xFFFF;
    sum += (dst >> 16) & 0xFFFF; sum += dst & 0xFFFF;
    sum += proto;
    sum += len;
    for (uint16_t i = 0; i + 1 < len; i += 2)
        sum += (uint32_t)((udp[i] << 8) | udp[i + 1]);
    if (len & 1) sum += (uint32_t)(udp[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t c = (uint16_t)~sum;
    return c ? c : 0xFFFF;      /* 0 means "no checksum", so never send it */
}

static uint16_t udp_checksum(uint32_t src, uint32_t dst,
                             const uint8_t *udp, uint16_t len) {
    return net_pseudo_checksum(src, dst, IP_PROTO_UDP, udp, len);
}

int net_udp_send_from(uint32_t src, uint32_t dst, uint16_t sport, uint16_t dport,
                      const void *data, uint16_t len) {
    static uint8_t pkt[ETH_FRAME_MAX];
    if (len > sizeof(pkt) - sizeof(udp_hdr_t)) return -1;

    /* A broadcast has no next hop to resolve. Anything else does, and this is
     * task context, so it is allowed to block: ip_send itself will not ARP. */
    if (dst != 0xFFFFFFFFu) {
        uint32_t next_hop = ((dst & my_mask) == (my_ip & my_mask)) ? dst : my_gw;
        uint8_t  hop_mac[6];
        if (net_arp_resolve(next_hop, hop_mac) < 0) return -1;
    }

    uint32_t uflags = irq_save();         /* pkt is shared with the IRQ path */
    udp_hdr_t *u = (udp_hdr_t *)pkt;
    uint16_t total = (uint16_t)(sizeof(udp_hdr_t) + len);
    u->sport = hton16(sport);
    u->dport = hton16(dport);
    u->len   = hton16(total);
    u->csum  = 0;
    memcpy(pkt + sizeof(udp_hdr_t), data, len);
    u->csum = hton16(udp_checksum(src, dst, pkt, total));

    ip_send_from(src, dst, IP_PROTO_UDP, pkt, total);
    irq_restore(uflags);
    return 0;
}

int net_udp_send(uint32_t dst, uint16_t sport, uint16_t dport,
                 const void *data, uint16_t len) {
    return net_udp_send_from(my_ip, dst, sport, dport, data, len);
}

/* Arm the receiver BEFORE sending the request it answers. SLIRP replies from
 * inside the same process, so a reply can land in the interrupt handler before
 * a send call has even returned; anything arriving before the port is armed is
 * dropped, which looks exactly like the server ignoring us. */
void net_udp_listen(uint16_t port) {
    udp_want_port = port;
    udp_got       = 0;
    udp_rx_len    = 0;
}

/* Wait for one datagram on `port`. Returns its length, or -1 on timeout. The
 * single-slot model from the resolver applies: one outstanding request. */
int net_udp_wait(uint16_t port, uint8_t *out, uint16_t max, uint32_t ticks) {
    if (udp_want_port != port) net_udp_listen(port);    /* not armed: arm now */

    uint32_t until = timer_get_ticks() + ticks;
    while (!udp_got && timer_get_ticks() < until) task_yield();

    if (!udp_got) { udp_want_port = 0; return -1; }
    uint16_t n = udp_rx_len;
    if (n > max) n = max;
    memcpy(out, (const void *)udp_rx, n);
    udp_want_port = 0;
    return (int)n;
}

/* DHCP replies arrive before we have an address, so the "is it for us?" test
 * in handle_ip would throw them away. This opens that gate while a lease is
 * being taken. */
static volatile int accept_any_dst;
void net_accept_any_dst(int on) { accept_any_dst = on ? 1 : 0; }

/* ── Receive ───────────────────────────────────────────────────────────── */
static void handle_arp(const uint8_t *p, uint16_t len) {
    if (len < sizeof(arp_pkt_t)) return;
    const arp_pkt_t *a = (const arp_pkt_t *)p;
    if (ntoh16(a->ptype) != ETH_TYPE_IP || a->plen != 4) return;

    uint32_t spa = get_ip(a->spa), tpa = get_ip(a->tpa);
    arp_insert(spa, a->sha);            /* learn from anything we see */
    n_arp++;

    if (ntoh16(a->op) == 1 && tpa == my_ip)
        arp_send(2, a->sha, spa);       /* someone is asking for us */
}

static void handle_icmp(uint32_t src, const uint8_t *p, uint16_t len) {
    if (len < sizeof(icmp_hdr_t)) return;
    const icmp_hdr_t *ic = (const icmp_hdr_t *)p;
    n_icmp++;

    if (ic->type == ICMP_ECHO_REPLY) {
        if (ntoh16(ic->id) == ping_want_id && ntoh16(ic->seq) == ping_want_seq)
            ping_got = 1;
        return;
    }

    if (ic->type == ICMP_ECHO_REQUEST) {
        /* Reply with the same id, sequence and payload, which is what makes
         * soupOS answer a ping from outside. */
        static uint8_t rep[ETH_FRAME_MAX];
        if (len > sizeof(rep)) return;
        memcpy(rep, p, len);
        icmp_hdr_t *r = (icmp_hdr_t *)rep;
        r->type = ICMP_ECHO_REPLY;
        r->code = 0;
        r->csum = 0;
        r->csum = hton16(checksum(rep, len));
        ip_send(src, IP_PROTO_ICMP, rep, len);
    }
}

static void handle_udp(const uint8_t *p, uint16_t len) {
    if (len < sizeof(udp_hdr_t)) return;
    const udp_hdr_t *u = (const udp_hdr_t *)p;
    n_udp++;

    uint16_t dport = ntoh16(u->dport);
    if (!udp_want_port || dport != udp_want_port || udp_got) return;

    uint16_t plen = (uint16_t)(ntoh16(u->len) - sizeof(udp_hdr_t));
    if (plen > len - sizeof(udp_hdr_t)) plen = (uint16_t)(len - sizeof(udp_hdr_t));
    if (plen > UDP_RX_MAX) plen = UDP_RX_MAX;

    memcpy(udp_rx, p + sizeof(udp_hdr_t), plen);
    udp_rx_len = plen;
    udp_got    = 1;
}

static void handle_ip(const uint8_t *p, uint16_t len) {
    if (len < sizeof(ip_hdr_t)) return;
    const ip_hdr_t *ih = (const ip_hdr_t *)p;
    if ((ih->ver_ihl >> 4) != 4) return;

    uint32_t hlen = (uint32_t)(ih->ver_ihl & 0x0F) * 4;
    if (hlen < sizeof(ip_hdr_t) || hlen > len) return;
    if (checksum(ih, hlen) != 0) return;            /* corrupt header */

    uint32_t dst = get_ip(ih->dst);
    if (dst != my_ip && dst != 0xFFFFFFFFu && !accept_any_dst) return; /* not ours */
    n_ip++;

    uint16_t total = ntoh16(ih->total_len);
    if (total > len) total = len;
    if (ih->proto == IP_PROTO_ICMP)
        handle_icmp(get_ip(ih->src), p + hlen, (uint16_t)(total - hlen));
    else if (ih->proto == IP_PROTO_UDP)
        handle_udp(p + hlen, (uint16_t)(total - hlen));
    else if (ih->proto == IP_PROTO_TCP)
        tcp_input(get_ip(ih->src), p + hlen, (uint16_t)(total - hlen));
}

static void net_rx(const uint8_t *frame, uint16_t len) {
    if (len < sizeof(eth_hdr_t)) return;
    const eth_hdr_t *e = (const eth_hdr_t *)frame;
    const uint8_t *payload = frame + sizeof(eth_hdr_t);
    uint16_t plen = (uint16_t)(len - sizeof(eth_hdr_t));

    switch (ntoh16(e->type)) {
        case ETH_TYPE_ARP: handle_arp(payload, plen); break;
        case ETH_TYPE_IP:
            /* Learn the sender's hardware address from the frame itself, not
             * only from ARP traffic.
             *
             * Without this, soupOS cannot ANSWER a connection it did not
             * start. Replies are built in interrupt context, where ip_send
             * refuses to resolve - and a peer does not have to ARP us first:
             * QEMU's SLIRP learned our MAC from the DHCP exchange, so an
             * inbound SYN arrived with nothing in the cache to address the
             * SYN-ACK to, and the handshake died with "no arp entry". Every
             * IP frame already carries both halves of the mapping, so this
             * is a line of bookkeeping rather than a round trip.
             *
             * handle_arp already learns from anything it sees, so this is the
             * same policy applied to the other half of the traffic. */
            if (plen >= sizeof(ip_hdr_t)) {
                const ip_hdr_t *ih = (const ip_hdr_t *)payload;
                uint32_t src = get_ip(ih->src);
                if (src && src != my_ip) arp_insert(src, e->src);
            }
            handle_ip(payload, plen);
            break;
        default:           n_other++;                 break;
    }
}

/* ── Public operations ─────────────────────────────────────────────────── */
void net_init(void) {
    if (!rtl8139_present()) return;
    rtl8139_set_rx_handler(net_rx);
    klog("[net] ip %u.%u.%u.%u gw %u.%u.%u.%u\n",
         (my_ip >> 24) & 0xFF, (my_ip >> 16) & 0xFF, (my_ip >> 8) & 0xFF, my_ip & 0xFF,
         (my_gw >> 24) & 0xFF, (my_gw >> 16) & 0xFF, (my_gw >> 8) & 0xFF, my_gw & 0xFF);
}

int net_arp_resolve(uint32_t ip, uint8_t *mac_out) {
    if (!rtl8139_present()) return -1;
    if (arp_find(ip, mac_out) == 0) return 0;

    static const uint8_t zero[6] = { 0, 0, 0, 0, 0, 0 };
    for (int attempt = 0; attempt < 3; attempt++) {
        arp_send(1, zero, ip);
        /* The reply arrives in the interrupt handler and lands in the cache,
         * so all we do is wait for it to show up there. */
        uint32_t until = timer_get_ticks() + 50;    /* 500 ms */
        while (timer_get_ticks() < until) {
            if (arp_find(ip, mac_out) == 0) return 0;
            task_yield();
        }
    }
    return -1;
}

void net_arp_dump(void) {
    int n = 0;
    for (int i = 0; i < ARP_MAX; i++) {
        if (!arp_cache[i].valid) continue;
        uint32_t ip = arp_cache[i].ip;
        const uint8_t *m = arp_cache[i].mac;
        vga_printf("  %u.%u.%u.%u  %x:%x:%x:%x:%x:%x\n",
                   (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
                   m[0], m[1], m[2], m[3], m[4], m[5]);
        n++;
    }
    if (!n) vga_puts("  (empty)\n");
}


/* ── DNS ───────────────────────────────────────────────────────────────────
 * One question, A records only, and no cache: a resolver for `nslookup` and
 * for `ping <hostname>`, not a library. The fiddly part of parsing a reply is
 * that names are compressed: a length byte of 0xC0 or more means the rest is a
 * pointer into the message, so walking records means skipping names without
 * following them. */

/* Write "www.example.com" as 3www7example3com0. Returns bytes written, or 0
 * if the name does not fit or has an empty label. */
static uint16_t dns_encode(const char *name, uint8_t *out, uint16_t max) {
    uint16_t o = 0;
    const char *p = name;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        uint32_t lab = (uint32_t)(dot - p);
        if (lab == 0 || lab > 63 || o + lab + 1 >= max) return 0;
        out[o++] = (uint8_t)lab;
        for (uint32_t i = 0; i < lab; i++) out[o++] = (uint8_t)p[i];
        p = (*dot == '.') ? dot + 1 : dot;
    }
    if (o + 1 >= max) return 0;
    out[o++] = 0;
    return o;
}

/* Step over a name at `off`, following nothing. Returns the offset just past
 * it, or 0 if the message is malformed. */
static uint16_t dns_skip_name(const uint8_t *msg, uint16_t len, uint16_t off) {
    while (off < len) {
        uint8_t l = msg[off];
        if (l == 0) return (uint16_t)(off + 1);
        if ((l & 0xC0) == 0xC0) return (uint16_t)(off + 2);   /* pointer ends it */
        off = (uint16_t)(off + 1 + l);
    }
    return 0;
}

int net_dns_resolve(const char *name, uint32_t *out) {
    if (!rtl8139_present() || !name || !*name) return -1;

    /* A dotted quad is already an address; save the round trip. */
    {
        uint32_t v = 0; int parts = 0; const char *p = name; int ok = 1;
        while (*p && parts < 4) {
            if (*p < '0' || *p > '9') { ok = 0; break; }
            uint32_t oct = 0;
            while (*p >= '0' && *p <= '9') oct = oct * 10 + (uint32_t)(*p++ - '0');
            if (oct > 255) { ok = 0; break; }
            v = (v << 8) | oct; parts++;
            if (*p == '.') p++; else break;
        }
        if (ok && parts == 4 && *p == '\0') { *out = v; return 0; }
    }

    static uint8_t q[256];
    dns_hdr_t *h = (dns_hdr_t *)q;
    static uint16_t dns_id;
    uint16_t id = (uint16_t)(0x1000 + (++dns_id));

    h->id      = hton16(id);
    h->flags   = hton16(0x0100);        /* standard query, recursion desired */
    h->qdcount = hton16(1);
    h->ancount = h->nscount = h->arcount = 0;

    uint16_t o = (uint16_t)sizeof(dns_hdr_t);
    uint16_t n = dns_encode(name, q + o, (uint16_t)(sizeof(q) - o));
    if (!n) return -1;
    o = (uint16_t)(o + n);
    q[o++] = 0; q[o++] = 1;             /* QTYPE  A     */
    q[o++] = 0; q[o++] = 1;             /* QCLASS IN    */

    uint16_t sport = (uint16_t)(40000 + (id & 0x0FFF));

    for (int attempt = 0; attempt < 2; attempt++) {
        udp_want_port = sport;
        udp_got       = 0;
        udp_rx_len    = 0;

        if (net_udp_send(my_dns, sport, 53, q, o) < 0) return -1;
        klog("[net] dns query id=%u for %s\n", id, name);

        uint32_t until = timer_get_ticks() + 200;        /* 2 seconds */
        while (!udp_got && timer_get_ticks() < until) task_yield();
        if (!udp_got) continue;

        uint16_t rlen = udp_rx_len;
        udp_want_port = 0;
        if (rlen < sizeof(dns_hdr_t)) return -1;

        const dns_hdr_t *rh = (const dns_hdr_t *)udp_rx;
        if (ntoh16(rh->id) != id) return -1;
        if ((ntoh16(rh->flags) & 0x000F) != 0) {         /* RCODE != 0 */
            klog("[net] dns rcode %u\n", ntoh16(rh->flags) & 0x0F);
            return -1;
        }

        uint16_t qd = ntoh16(rh->qdcount), an = ntoh16(rh->ancount);
        uint16_t off = (uint16_t)sizeof(dns_hdr_t);
        for (uint16_t i = 0; i < qd; i++) {
            off = dns_skip_name(udp_rx, rlen, off);
            if (!off || off + 4 > rlen) return -1;
            off = (uint16_t)(off + 4);                   /* QTYPE + QCLASS */
        }
        for (uint16_t i = 0; i < an; i++) {
            off = dns_skip_name(udp_rx, rlen, off);
            if (!off || off + 10 > rlen) return -1;
            uint16_t type  = (uint16_t)((udp_rx[off] << 8) | udp_rx[off + 1]);
            uint16_t rdlen = (uint16_t)((udp_rx[off + 8] << 8) | udp_rx[off + 9]);
            off = (uint16_t)(off + 10);
            if (off + rdlen > rlen) return -1;
            if (type == 1 && rdlen == 4) {               /* an A record */
                *out = get_ip(udp_rx + off);
                klog("[net] dns %s -> %u.%u.%u.%u\n", name,
                     (*out >> 24) & 0xFF, (*out >> 16) & 0xFF,
                     (*out >> 8) & 0xFF, *out & 0xFF);
                return 0;
            }
            off = (uint16_t)(off + rdlen);               /* CNAME etc: keep going */
        }
        return -1;                                        /* answered, no address */
    }
    udp_want_port = 0;
    klog("[net] dns timeout for %s\n", name);
    return -1;
}

int net_ping(uint32_t dst, int count) {
    if (!rtl8139_present()) { vga_puts("  No network card.\n"); return 0; }

    uint32_t next_hop = ((dst & my_mask) == (my_ip & my_mask)) ? dst : my_gw;
    uint8_t mac[6];
    if (net_arp_resolve(next_hop, mac) < 0) {
        vga_printf("  No ARP reply from %u.%u.%u.%u\n",
                   (next_hop >> 24) & 0xFF, (next_hop >> 16) & 0xFF,
                   (next_hop >> 8) & 0xFF, next_hop & 0xFF);
        klog("[net] ping: arp timeout\n");
        return 0;
    }

    int got = 0;
    for (int i = 0; i < count; i++) {
        uint8_t pkt[sizeof(icmp_hdr_t) + 32];
        icmp_hdr_t *ic = (icmp_hdr_t *)pkt;
        ic->type = ICMP_ECHO_REQUEST;
        ic->code = 0;
        ic->csum = 0;
        ic->id   = hton16(0x50C0);
        ic->seq  = hton16((uint16_t)(i + 1));
        for (uint32_t k = 0; k < 32; k++) pkt[sizeof(icmp_hdr_t) + k] = (uint8_t)('a' + (k % 26));
        ic->csum = hton16(checksum(pkt, sizeof(pkt)));

        ping_want_id  = 0x50C0;
        ping_want_seq = (uint16_t)(i + 1);
        ping_got      = 0;

        uint32_t t0 = timer_get_ticks();
        ip_send(dst, IP_PROTO_ICMP, pkt, sizeof(pkt));

        uint32_t until = t0 + 100;                  /* 1 second */
        while (!ping_got && timer_get_ticks() < until) task_yield();

        if (ping_got) {
            /* The clock is the 100 Hz PIT, so a reply that beats the next tick
             * can only be reported as "under one tick" rather than as 0 ms. */
            uint32_t ms = (timer_get_ticks() - t0) * 10;
            vga_printf("  reply from %u.%u.%u.%u  seq=%d  time ",
                       (dst >> 24) & 0xFF, (dst >> 16) & 0xFF,
                       (dst >> 8) & 0xFF, dst & 0xFF, i + 1);
            if (ms == 0) vga_puts("<10ms\n");
            else         vga_printf("<=%ums\n", ms);
            klog("[net] ping reply seq=%d\n", i + 1);
            got++;
        } else {
            vga_printf("  no reply  seq=%d\n", i + 1);
            klog("[net] ping timeout seq=%d\n", i + 1);
        }

        uint32_t gap = timer_get_ticks() + 10;      /* 100 ms between pings */
        while (timer_get_ticks() < gap) task_yield();
    }

    vga_printf("  %d/%d replies\n", got, count);
    klog("[net] ping done %d/%d\n", got, count);
    return got;
}
