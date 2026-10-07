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

/* ── Wire formats ──────────────────────────────────────────────────────── */
#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806
#define IP_PROTO_ICMP 1

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
static uint32_t n_arp, n_ip, n_icmp, n_other;

#define ARP_MAX 8
static struct { uint32_t ip; uint8_t mac[6]; int valid; } arp_cache[ARP_MAX];

/* Set by the interrupt handler when an echo reply we are waiting for lands. */
static volatile uint16_t ping_want_id, ping_want_seq;
static volatile int      ping_got;

uint32_t net_ip(void)   { return my_ip; }
uint32_t net_gw(void)   { return my_gw; }
uint32_t net_mask(void) { return my_mask; }
void     net_set_ip(uint32_t ip) { my_ip = ip; }

void net_stats(uint32_t *arp, uint32_t *ip, uint32_t *icmp, uint32_t *other) {
    if (arp)   *arp   = n_arp;
    if (ip)    *ip    = n_ip;
    if (icmp)  *icmp  = n_icmp;
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

/* ── Transmit helpers ──────────────────────────────────────────────────── */
static uint8_t txf[ETH_FRAME_MAX];

static void eth_send(const uint8_t *dst_mac, uint16_t type,
                     const void *payload, uint16_t plen) {
    if (plen > ETH_FRAME_MAX - (uint16_t)sizeof(eth_hdr_t)) return;
    eth_hdr_t *e = (eth_hdr_t *)txf;
    memcpy(e->dst, dst_mac, 6);
    memcpy(e->src, rtl8139_mac(), 6);
    e->type = hton16(type);
    memcpy(txf + sizeof(eth_hdr_t), payload, plen);
    rtl8139_send(txf, (uint16_t)(sizeof(eth_hdr_t) + plen));
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

    static const uint8_t bcast[6] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
    eth_send((op == 1) ? bcast : target_mac, ETH_TYPE_ARP, &a, sizeof(a));
}

static void ip_send(uint32_t dst, uint8_t proto, const void *payload, uint16_t plen) {
    uint8_t mac[6];
    /* Anything off our subnet goes via the gateway, which is the whole of the
     * routing table. */
    uint32_t next_hop = ((dst & my_mask) == (my_ip & my_mask)) ? dst : my_gw;
    if (arp_find(next_hop, mac) < 0) return;      /* caller resolves first */

    static uint8_t buf[ETH_FRAME_MAX];
    ip_hdr_t *ih = (ip_hdr_t *)buf;
    static uint16_t ip_id;

    ih->ver_ihl   = 0x45;
    ih->tos       = 0;
    ih->total_len = hton16((uint16_t)(sizeof(ip_hdr_t) + plen));
    ih->id        = hton16(++ip_id);
    ih->frag      = 0;
    ih->ttl       = 64;
    ih->proto     = proto;
    ih->csum      = 0;
    put_ip(ih->src, my_ip);
    put_ip(ih->dst, dst);
    ih->csum = hton16(checksum(ih, sizeof(ip_hdr_t)));

    memcpy(buf + sizeof(ip_hdr_t), payload, plen);
    eth_send(mac, ETH_TYPE_IP, buf, (uint16_t)(sizeof(ip_hdr_t) + plen));
}

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

static void handle_ip(const uint8_t *p, uint16_t len) {
    if (len < sizeof(ip_hdr_t)) return;
    const ip_hdr_t *ih = (const ip_hdr_t *)p;
    if ((ih->ver_ihl >> 4) != 4) return;

    uint32_t hlen = (uint32_t)(ih->ver_ihl & 0x0F) * 4;
    if (hlen < sizeof(ip_hdr_t) || hlen > len) return;
    if (checksum(ih, hlen) != 0) return;            /* corrupt header */

    uint32_t dst = get_ip(ih->dst);
    if (dst != my_ip && dst != 0xFFFFFFFFu) return; /* not ours */
    n_ip++;

    uint16_t total = ntoh16(ih->total_len);
    if (total > len) total = len;
    if (ih->proto == IP_PROTO_ICMP)
        handle_icmp(get_ip(ih->src), p + hlen, (uint16_t)(total - hlen));
}

static void net_rx(const uint8_t *frame, uint16_t len) {
    if (len < sizeof(eth_hdr_t)) return;
    const eth_hdr_t *e = (const eth_hdr_t *)frame;
    const uint8_t *payload = frame + sizeof(eth_hdr_t);
    uint16_t plen = (uint16_t)(len - sizeof(eth_hdr_t));

    switch (ntoh16(e->type)) {
        case ETH_TYPE_ARP: handle_arp(payload, plen); break;
        case ETH_TYPE_IP:  handle_ip(payload, plen);  break;
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
