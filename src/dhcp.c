/* dhcp.c - take an address by DHCP.
 *
 * Four packets: DISCOVER, OFFER, REQUEST, ACK. We ask for an address, are
 * offered one, ask for that specific one, and are told it is ours. The address,
 * netmask, gateway and resolver all come out of the ACK's options.
 *
 * Deliberately not implemented: lease renewal and rebinding. The lease time is
 * read and logged so the number is visible, and then ignored, because soupOS
 * has no timer-driven background work and a hobby OS that reboots more often
 * than its lease expires loses nothing by it.
 *
 * Two things make DHCP awkward for a stack that otherwise assumes it has an
 * address: every packet goes out with a source of 0.0.0.0 to the broadcast
 * address (hence net_udp_send_from), and the replies are addressed to an
 * address we do not hold yet (hence net_accept_any_dst around the exchange).
 */
#include "net.h"
#include "rtl8139.h"
#include "timer.h"
#include "klog.h"
#include "str.h"

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_MAGIC       0x63825363u

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

/* Options we care about. */
#define OPT_SUBNET      1
#define OPT_ROUTER      3
#define OPT_DNS         6
#define OPT_REQUESTED  50
#define OPT_LEASE      51
#define OPT_MSGTYPE    53
#define OPT_SERVERID   54
#define OPT_PARAMLIST  55
#define OPT_END       255

typedef struct {
    uint8_t  op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint8_t  ciaddr[4], yiaddr[4], siaddr[4], giaddr[4];
    uint8_t  chaddr[16];
    uint8_t  sname[64], file[128];
    uint32_t magic;
} __attribute__((packed)) dhcp_hdr_t;

static uint8_t  pkt[sizeof(dhcp_hdr_t) + 64];
static uint8_t  reply[576];

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}
static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/* Find an option in a reply. Returns its length and sets *val, or 0. */
static uint8_t opt_find(const uint8_t *msg, uint16_t len, uint8_t want,
                        const uint8_t **val) {
    uint16_t o = (uint16_t)sizeof(dhcp_hdr_t);
    while (o + 1 < len) {
        uint8_t code = msg[o];
        if (code == OPT_END) break;
        if (code == 0) { o++; continue; }          /* pad */
        uint8_t l = msg[o + 1];
        if (o + 2 + l > len) break;
        if (code == want) { *val = msg + o + 2; return l; }
        o = (uint16_t)(o + 2 + l);
    }
    return 0;
}

/* Build DISCOVER or REQUEST into pkt[]; returns its length. */
static uint16_t build(uint8_t type, uint32_t xid, uint32_t req_ip, uint32_t server)
{
    memset(pkt, 0, sizeof(pkt));
    dhcp_hdr_t *h = (dhcp_hdr_t *)pkt;
    h->op    = 1;                   /* BOOTREQUEST */
    h->htype = 1;                   /* Ethernet    */
    h->hlen  = 6;
    put_be32((uint8_t *)&h->xid, xid);
    /* Ask for broadcast replies. A unicast reply would be addressed to an
     * address we do not hold yet, and the card would be the only part of the
     * machine willing to accept it. */
    h->flags = (uint16_t)((0x8000 >> 8) | (0x8000 << 8));   /* htons(0x8000) */
    memcpy(h->chaddr, rtl8139_mac(), 6);
    put_be32((uint8_t *)&h->magic, DHCP_MAGIC);

    uint16_t o = (uint16_t)sizeof(dhcp_hdr_t);
    pkt[o++] = OPT_MSGTYPE; pkt[o++] = 1; pkt[o++] = type;

    if (type == DHCP_REQUEST) {
        pkt[o++] = OPT_REQUESTED; pkt[o++] = 4; put_be32(pkt + o, req_ip); o += 4;
        pkt[o++] = OPT_SERVERID;  pkt[o++] = 4; put_be32(pkt + o, server); o += 4;
    }

    pkt[o++] = OPT_PARAMLIST; pkt[o++] = 3;
    pkt[o++] = OPT_SUBNET; pkt[o++] = OPT_ROUTER; pkt[o++] = OPT_DNS;
    pkt[o++] = OPT_END;
    return o;
}

/* Send one message and wait for a reply of the expected type. Returns the
 * reply length, or -1. */
static int exchange(uint8_t send_type, uint8_t want_type, uint32_t xid,
                    uint32_t req_ip, uint32_t server) {
    uint16_t len = build(send_type, xid, req_ip, server);

    for (int attempt = 0; attempt < 3; attempt++) {
        /* Armed first: the server answers from inside QEMU, fast enough that
         * the reply can be handled before the send call returns. */
        net_udp_listen(DHCP_CLIENT_PORT);

        if (net_udp_send_from(0, 0xFFFFFFFFu, DHCP_CLIENT_PORT,
                              DHCP_SERVER_PORT, pkt, len) < 0)
            return -1;

        int n = net_udp_wait(DHCP_CLIENT_PORT, reply, sizeof(reply), 100);
        if (n < (int)sizeof(dhcp_hdr_t)) continue;

        const dhcp_hdr_t *h = (const dhcp_hdr_t *)reply;
        if (be32((const uint8_t *)&h->xid) != xid) continue;
        if (be32((const uint8_t *)&h->magic) != DHCP_MAGIC) continue;

        const uint8_t *v;
        if (opt_find(reply, (uint16_t)n, OPT_MSGTYPE, &v) != 1) continue;
        if (*v != want_type) continue;
        return n;
    }
    return -1;
}

int net_dhcp(void) {
    if (!rtl8139_present()) return -1;

    const uint8_t *mac = rtl8139_mac();
    uint32_t xid = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
                   ((uint32_t)mac[4] << 8)  | (uint32_t)mac[5];
    xid ^= timer_get_ticks() * 2654435761u;

    net_accept_any_dst(1);          /* the replies are not addressed to us yet */

    int n = exchange(DHCP_DISCOVER, DHCP_OFFER, xid, 0, 0);
    if (n < 0) { net_accept_any_dst(0); klog("[dhcp] no offer\n"); return -1; }

    const dhcp_hdr_t *h = (const dhcp_hdr_t *)reply;
    uint32_t offered = be32(h->yiaddr);
    const uint8_t *v;
    uint32_t server = (opt_find(reply, (uint16_t)n, OPT_SERVERID, &v) == 4)
                          ? be32(v) : 0;

    n = exchange(DHCP_REQUEST, DHCP_ACK, xid, offered, server);
    net_accept_any_dst(0);
    if (n < 0) { klog("[dhcp] no ack\n"); return -1; }

    h = (const dhcp_hdr_t *)reply;
    uint32_t ip = be32(h->yiaddr);
    if (!ip) return -1;
    net_set_ip(ip);
    net_set_leased(1);      /* after set_ip, which clears it */

    if (opt_find(reply, (uint16_t)n, OPT_SUBNET, &v) == 4) net_set_mask(be32(v));
    if (opt_find(reply, (uint16_t)n, OPT_ROUTER, &v) >= 4) net_set_gw(be32(v));
    if (opt_find(reply, (uint16_t)n, OPT_DNS,    &v) >= 4) net_set_dns(be32(v));

    uint32_t lease = 0;
    if (opt_find(reply, (uint16_t)n, OPT_LEASE, &v) == 4) lease = be32(v);

    /* Logged, then ignored: see the note at the top about renewal. */
    klog("[dhcp] lease %u.%u.%u.%u mask %u.%u.%u.%u gw %u.%u.%u.%u dns %u.%u.%u.%u for %us\n",
         (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
         (net_mask() >> 24) & 0xFF, (net_mask() >> 16) & 0xFF,
         (net_mask() >> 8) & 0xFF, net_mask() & 0xFF,
         (net_gw() >> 24) & 0xFF, (net_gw() >> 16) & 0xFF,
         (net_gw() >> 8) & 0xFF, net_gw() & 0xFF,
         (net_dns() >> 24) & 0xFF, (net_dns() >> 16) & 0xFF,
         (net_dns() >> 8) & 0xFF, net_dns() & 0xFF,
         lease);
    return 0;
}
