/* tcp.c - TCP connection setup. See tcp.h for the scope of this stage.
 *
 * The handshake is the easy part of TCP; what makes it worth writing carefully
 * is that the sequence numbers set up here are what every later stage depends
 * on. The convention used throughout: snd_nxt is the next sequence number we
 * will send, rcv_nxt is the next one we expect, and a SYN counts as one byte
 * for both. Getting that "SYN is a byte" rule wrong is the classic way to end
 * up acknowledging one less than the peer expects and stalling forever.
 */
#include "tcp.h"
#include "net.h"
#include "timer.h"
#include "task.h"
#include "klog.h"
#include "str.h"

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

typedef struct {
    uint16_t sport, dport;
    uint32_t seq, ack;
    uint8_t  off_rsv;        /* data offset in the high nibble, in 32-bit words */
    uint8_t  flags;
    uint16_t window, csum, urg;
} __attribute__((packed)) tcp_hdr_t;

/* The one connection. volatile where the interrupt handler writes it. */
static struct {
    volatile tcp_state_t state;
    uint32_t  peer_ip;
    uint16_t  local_port, peer_port;
    uint32_t  snd_nxt;              /* next sequence number we send   */
    volatile uint32_t rcv_nxt;      /* next sequence number we expect */
    volatile uint16_t peer_window;
    volatile int      refused;      /* peer sent RST                  */
} tcb;

static uint16_t next_port = 49152;  /* ephemeral range */

tcp_state_t tcp_state(void) { return tcb.state; }

static inline uint16_t hton16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static inline uint16_t ntoh16(uint16_t v) { return hton16(v); }
static inline uint32_t hton32(uint32_t v) {
    return ((v & 0xFFu) << 24) | ((v & 0xFF00u) << 8) |
           ((v >> 8) & 0xFF00u) | ((v >> 24) & 0xFFu);
}
static inline uint32_t ntoh32(uint32_t v) { return hton32(v); }

/* Build and send one segment. No payload at this stage. */
static void tcp_send_flags(uint8_t flags, uint32_t seq, uint32_t ack) {
    uint8_t pkt[sizeof(tcp_hdr_t)];
    tcp_hdr_t *t = (tcp_hdr_t *)pkt;

    t->sport   = hton16(tcb.local_port);
    t->dport   = hton16(tcb.peer_port);
    t->seq     = hton32(seq);
    t->ack     = hton32(ack);
    t->off_rsv = (uint8_t)((sizeof(tcp_hdr_t) / 4) << 4);
    t->flags   = flags;
    t->window  = hton16(4096);      /* what we are willing to receive */
    t->csum    = 0;
    t->urg     = 0;
    t->csum = hton16(net_pseudo_checksum(net_ip(), tcb.peer_ip, 6,
                                         pkt, sizeof(pkt)));

    net_ip_send(tcb.peer_ip, 6, pkt, sizeof(pkt));
}

/* Interrupt context: just enough to finish the handshake. */
void tcp_input(uint32_t src_ip, const uint8_t *seg, uint16_t len) {
    if (len < sizeof(tcp_hdr_t)) return;
    const tcp_hdr_t *t = (const tcp_hdr_t *)seg;

    if (tcb.state == TCP_CLOSED) return;
    if (src_ip != tcb.peer_ip) return;
    if (ntoh16(t->dport) != tcb.local_port) return;
    if (ntoh16(t->sport) != tcb.peer_port) return;

    if (t->flags & TCP_RST) {
        tcb.refused = 1;
        tcb.state   = TCP_CLOSED;
        return;
    }

    /* A duplicate SYN/ACK means our ACK was lost or was slower than their
     * retransmit timer. Answering it again is both correct and cheap; ignoring
     * it leaves the peer retransmitting until it gives up. */
    if (tcb.state == TCP_ESTABLISHED && (t->flags & TCP_SYN) && (t->flags & TCP_ACK)) {
        tcp_send_flags(TCP_ACK, tcb.snd_nxt, tcb.rcv_nxt);
        return;
    }

    if (tcb.state == TCP_SYN_SENT && (t->flags & TCP_SYN) && (t->flags & TCP_ACK)) {
        /* Their SYN occupies one sequence number, so what we expect next is
         * their sequence plus one. */
        tcb.rcv_nxt     = ntoh32(t->seq) + 1;
        tcb.peer_window = ntoh16(t->window);
        tcb.state       = TCP_ESTABLISHED;
        tcp_send_flags(TCP_ACK, tcb.snd_nxt, tcb.rcv_nxt);
    }
}

int tcp_connect(uint32_t dst_ip, uint16_t dst_port) {
    uint8_t mac[6];
    /* Task context, so resolving here is allowed; net_ip_send will not. */
    if (net_arp_resolve(((dst_ip & net_mask()) == (net_ip() & net_mask()))
                            ? dst_ip : net_gw(), mac) < 0) {
        klog("[tcp] no route to %u.%u.%u.%u\n",
             (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
             (dst_ip >> 8) & 0xFF, dst_ip & 0xFF);
        return -1;
    }

    tcb.peer_ip     = dst_ip;
    tcb.peer_port   = dst_port;
    tcb.local_port  = next_port++;
    if (next_port == 0) next_port = 49152;
    /* An initial sequence number that at least varies between connections.
     * A real stack derives it from a clock for security reasons that do not
     * apply to a machine with one connection and no adversary. */
    tcb.snd_nxt     = (timer_get_ticks() << 8) ^ 0x5000C0DEu;
    tcb.rcv_nxt     = 0;
    tcb.refused     = 0;
    tcb.state       = TCP_SYN_SENT;

    uint32_t iss = tcb.snd_nxt;
    /* Our SYN occupies one sequence number too, so everything after it is
     * numbered from iss + 1. */
    tcb.snd_nxt = iss + 1;

    for (int attempt = 0; attempt < 3; attempt++) {
        tcp_send_flags(TCP_SYN, iss, 0);
        klog("[tcp] syn to %u.%u.%u.%u:%u from port %u\n",
             (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
             (dst_ip >> 8) & 0xFF, dst_ip & 0xFF, dst_port, tcb.local_port);

        uint32_t until = timer_get_ticks() + 100;      /* 1 second */
        while (timer_get_ticks() < until) {
            if (tcb.state == TCP_ESTABLISHED) {
                klog("[tcp] established with %u.%u.%u.%u:%u, peer window %u\n",
                     (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
                     (dst_ip >> 8) & 0xFF, dst_ip & 0xFF, dst_port,
                     tcb.peer_window);
                return 0;
            }
            if (tcb.refused) {
                klog("[tcp] refused by %u.%u.%u.%u:%u\n",
                     (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
                     (dst_ip >> 8) & 0xFF, dst_ip & 0xFF, dst_port);
                return -1;
            }
            task_yield();
        }
    }

    tcb.state = TCP_CLOSED;
    klog("[tcp] no answer from %u.%u.%u.%u:%u\n",
         (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
         (dst_ip >> 8) & 0xFF, dst_ip & 0xFF, dst_port);
    return -1;
}

void tcp_close(void) {
    if (tcb.state == TCP_CLOSED) return;
    /* A RST, not a FIN: the graceful close belongs to stage 2c, and pretending
     * otherwise here would leave a half-closed connection nobody handles. */
    tcp_send_flags(TCP_RST | TCP_ACK, tcb.snd_nxt, tcb.rcv_nxt);
    tcb.state = TCP_CLOSED;
    klog("[tcp] closed (rst)\n");
}
