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

#define TCP_RX_MAX  4096        /* what we are willing to hold for the reader */
#define TCP_MSS     1024        /* conservative: never fragments an Ethernet frame */

/* The one connection. volatile where the interrupt handler writes it. */
static struct {
    volatile tcp_state_t state;
    uint32_t  peer_ip;
    uint16_t  local_port, peer_port;
    uint32_t  snd_nxt;              /* next sequence number we send   */
    volatile uint32_t snd_una;      /* oldest unacknowledged          */
    volatile uint32_t rcv_nxt;      /* next sequence number we expect */
    volatile uint16_t peer_window;
    volatile int      refused;      /* peer sent RST                  */
    volatile int      peer_fin;     /* peer has finished sending      */
    int               fin_sent;     /* we have sent ours              */
} tcb;

/* Received data waiting for a reader. Written by the interrupt handler, drained
 * by tcp_recv, so the count is volatile and the drain disables interrupts for
 * the memmove rather than trying to be clever with a lock-free ring. */
static uint8_t           rx_buf[TCP_RX_MAX];
static volatile uint16_t rx_len;

static uint16_t next_port = 49152;  /* ephemeral range */

tcp_state_t tcp_state(void) { return tcb.state; }

static inline uint16_t hton16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static inline uint16_t ntoh16(uint16_t v) { return hton16(v); }
static inline uint32_t hton32(uint32_t v) {
    return ((v & 0xFFu) << 24) | ((v & 0xFF00u) << 8) |
           ((v >> 8) & 0xFF00u) | ((v >> 24) & 0xFFu);
}
static inline uint32_t ntoh32(uint32_t v) { return hton32(v); }

/* Build and send one segment, with an optional payload. */
static void tcp_send_seg(uint8_t flags, uint32_t seq, uint32_t ack,
                         const uint8_t *data, uint16_t dlen) {
    uint8_t pkt[sizeof(tcp_hdr_t) + TCP_MSS];
    if (dlen > TCP_MSS) dlen = TCP_MSS;
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
    if (dlen && data) memcpy(pkt + sizeof(tcp_hdr_t), data, dlen);

    uint16_t total = (uint16_t)(sizeof(tcp_hdr_t) + dlen);
    t->csum = hton16(net_pseudo_checksum(net_ip(), tcb.peer_ip, 6, pkt, total));
    net_ip_send(tcb.peer_ip, 6, pkt, total);
}

static void tcp_send_flags(uint8_t flags, uint32_t seq, uint32_t ack) {
    tcp_send_seg(flags, seq, ack, 0, 0);
}

/* Interrupt context: just enough to finish the handshake. */
void tcp_input(uint32_t src_ip, const uint8_t *seg, uint16_t len) {
    if (len < sizeof(tcp_hdr_t)) return;
    const tcp_hdr_t *t = (const tcp_hdr_t *)seg;

    if (tcb.state == TCP_CLOSED) return;

    /* A listener has no peer yet, so it can only match on the port it was
     * given; everything else matches the full four-tuple. */
    if (tcb.state == TCP_LISTEN) {
        if (ntoh16(t->dport) != tcb.local_port) return;
        if (!(t->flags & TCP_SYN)) return;

        tcb.peer_ip     = src_ip;
        tcb.peer_port   = ntoh16(t->sport);
        tcb.rcv_nxt     = ntoh32(t->seq) + 1;   /* their SYN takes one */
        tcb.peer_window = ntoh16(t->window);

        uint32_t iss = (timer_get_ticks() << 8) ^ 0x5000D00Du;
        tcb.snd_una  = iss;
        tcb.snd_nxt  = iss + 1;                 /* ours takes one too  */
        tcb.state    = TCP_SYN_RCVD;
        tcp_send_flags(TCP_SYN | TCP_ACK, iss, tcb.rcv_nxt);
        return;
    }

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

    if (tcb.state == TCP_SYN_RCVD) {
        if (t->flags & TCP_SYN) {        /* our SYN-ACK was lost: say it again */
            tcp_send_flags(TCP_SYN | TCP_ACK, tcb.snd_una, tcb.rcv_nxt);
            return;
        }
        if (t->flags & TCP_ACK) {
            tcb.snd_una     = ntoh32(t->ack);
            tcb.peer_window = ntoh16(t->window);
            tcb.state       = TCP_ESTABLISHED;
            /* Fall through: this ACK may carry the request's first bytes,
             * and SLIRP does exactly that often enough to matter. */
        } else {
            return;
        }
    }

    if (tcb.state == TCP_SYN_SENT && (t->flags & TCP_SYN) && (t->flags & TCP_ACK)) {
        /* Their SYN occupies one sequence number, so what we expect next is
         * their sequence plus one. */
        tcb.rcv_nxt     = ntoh32(t->seq) + 1;
        tcb.snd_una     = ntoh32(t->ack);
        tcb.peer_window = ntoh16(t->window);
        tcb.state       = TCP_ESTABLISHED;
        tcp_send_flags(TCP_ACK, tcb.snd_nxt, tcb.rcv_nxt);
        return;
    }

    /* Everything from here on applies to any open connection, not just an
     * established one: acknowledgements and even data keep arriving after a
     * FIN has been seen in either direction, and ignoring them leaves both
     * sides retransmitting at each other until they give up. */
    if (tcb.state == TCP_CLOSED || tcb.state == TCP_SYN_SENT) return;

    if (t->flags & TCP_ACK) {
        uint32_t a = ntoh32(t->ack);
        /* Signed comparison handles the sequence space wrapping; a plain > is
         * wrong once the numbers pass 2^31. */
        if ((int32_t)(a - tcb.snd_una) > 0) tcb.snd_una = a;
        tcb.peer_window = ntoh16(t->window);

        /* Our FIN consumed a sequence number, so it is acknowledged once
         * snd_una reaches snd_nxt. */
        if (tcb.fin_sent && (int32_t)(tcb.snd_una - tcb.snd_nxt) >= 0) {
            if (tcb.state == TCP_FIN_WAIT_1) tcb.state = TCP_FIN_WAIT_2;
            else if (tcb.state == TCP_LAST_ACK) tcb.state = TCP_CLOSED;
        }
    }

    uint16_t hlen  = (uint16_t)((t->off_rsv >> 4) * 4);
    if (hlen < sizeof(tcp_hdr_t) || hlen > len) return;
    uint16_t dlen  = (uint16_t)(len - hlen);
    uint32_t seq   = ntoh32(t->seq);

    if (dlen) {
        if (seq == tcb.rcv_nxt) {
            uint16_t room = (uint16_t)(TCP_RX_MAX - rx_len);
            uint16_t take = (dlen < room) ? dlen : room;
            memcpy(rx_buf + rx_len, seg + hlen, take);
            rx_len      = (uint16_t)(rx_len + take);
            tcb.rcv_nxt = seq + take;      /* only what we actually kept */
        }
        /* Either way, tell them where we are. An out-of-order segment gets the
         * same answer, which is this stack's way of asking for a resend. */
        tcp_send_flags(TCP_ACK, tcb.snd_nxt, tcb.rcv_nxt);
    }

    if ((t->flags & TCP_FIN) && seq == tcb.rcv_nxt) {
        /* Their FIN occupies a sequence number too, so acknowledge one past
         * it. Where that leaves us depends on whether we have already sent
         * ours: if we have, both sides are done; if not, they have stopped
         * talking but we may still have something to say. */
        tcb.peer_fin = 1;
        tcb.rcv_nxt  = seq + 1;
        tcp_send_flags(TCP_ACK, tcb.snd_nxt, tcb.rcv_nxt);

        if (tcb.state == TCP_ESTABLISHED)      tcb.state = TCP_CLOSE_WAIT;
        else if (tcb.state == TCP_FIN_WAIT_1)  tcb.state = TCP_LAST_ACK;
        else if (tcb.state == TCP_FIN_WAIT_2)  tcb.state = TCP_TIME_WAIT;
    }
}

int tcp_send(const void *data, uint16_t len) {
    if (tcb.state != TCP_ESTABLISHED || !data || !len) return -1;

    const uint8_t *p = (const uint8_t *)data;
    uint16_t sent = 0;

    while (sent < len) {
        uint16_t chunk = (uint16_t)(len - sent);
        if (chunk > TCP_MSS) chunk = TCP_MSS;

        uint32_t seq = tcb.snd_nxt;
        tcb.snd_nxt  = seq + chunk;

        /* One segment in flight: send it, wait for the acknowledgement to
         * cover it, resend if it does not come. Stop-and-wait, deliberately. */
        int acked = 0;
        for (int attempt = 0; attempt < 4 && !acked; attempt++) {
            tcp_send_seg(TCP_PSH | TCP_ACK, seq, tcb.rcv_nxt, p + sent, chunk);

            uint32_t until = timer_get_ticks() + 50;      /* 500 ms */
            while (timer_get_ticks() < until) {
                if ((int32_t)(tcb.snd_una - (seq + chunk)) >= 0) { acked = 1; break; }
                if (tcb.refused || tcb.state != TCP_ESTABLISHED) return -1;
                task_yield();
            }
        }
        if (!acked) {
            klog("[tcp] no ack for %u bytes at seq %u\n", chunk, seq);
            return (int)sent ? (int)sent : -1;
        }
        sent = (uint16_t)(sent + chunk);
    }

    klog("[tcp] sent %u bytes\n", sent);
    return (int)sent;
}

int tcp_peer_done(void) { return tcb.peer_fin; }

int tcp_recv(uint8_t *out, uint16_t max, uint32_t ticks) {
    if (!out || !max) return 0;

    uint32_t until = timer_get_ticks() + ticks;
    while (rx_len == 0 && timer_get_ticks() < until) {
        if (tcb.refused || tcb.peer_fin) break;
        task_yield();
    }
    if (rx_len == 0) return 0;

    /* The handler appends to the same buffer, so take a consistent snapshot
     * rather than racing it mid-memmove. */
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    uint16_t n = (rx_len < max) ? rx_len : max;
    memcpy(out, rx_buf, n);
    if (n < rx_len) memmove(rx_buf, rx_buf + n, (uint32_t)(rx_len - n));
    rx_len = (uint16_t)(rx_len - n);
    __asm__ volatile ("push %0; popf" :: "r"(flags) : "memory", "cc");

    klog("[tcp] rx %u bytes\n", n);
    return (int)n;
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
    tcb.peer_fin    = 0;
    tcb.fin_sent    = 0;
    rx_len          = 0;
    tcb.state       = TCP_SYN_SENT;

    uint32_t iss = tcb.snd_nxt;
    tcb.snd_una = iss;
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

void tcp_abort(void) {
    if (tcb.state == TCP_CLOSED) return;
    tcp_send_flags(TCP_RST | TCP_ACK, tcb.snd_nxt, tcb.rcv_nxt);
    tcb.state = TCP_CLOSED;
    klog("[tcp] aborted (rst)\n");
}

void tcp_close(void) {
    if (tcb.state == TCP_CLOSED) return;

    /* Send our FIN. It occupies a sequence number, like the SYN did, so
     * snd_nxt moves past it and the peer's acknowledgement of that number is
     * what retires it. */
    if (!tcb.fin_sent) {
        uint32_t seq = tcb.snd_nxt;
        tcb.snd_nxt  = seq + 1;
        tcb.fin_sent = 1;
        tcb.state    = (tcb.state == TCP_CLOSE_WAIT) ? TCP_LAST_ACK : TCP_FIN_WAIT_1;
        tcp_send_seg(TCP_FIN | TCP_ACK, seq, tcb.rcv_nxt, 0, 0);
    }

    /* Wait for the exchange to finish, but not forever: a peer that vanishes
     * must not wedge the shell. */
    uint32_t until = timer_get_ticks() + 200;        /* 2 s */
    while (timer_get_ticks() < until) {
        if (tcb.state == TCP_CLOSED || tcb.state == TCP_TIME_WAIT) break;
        if (tcb.refused) break;
        task_yield();
    }

    /* TIME_WAIT exists so a late duplicate of the last segment cannot be
     * mistaken for part of a new connection on the same port pair. A real
     * stack lingers for twice the maximum segment lifetime, two minutes or so.
     * soupOS lingers for 200 ms, because it uses a fresh ephemeral port for
     * every connection and a shell that froze for two minutes after every
     * close would be unusable. The trade is written down rather than hidden:
     * reusing a port pair within that window could confuse a peer. */
    if (tcb.state == TCP_TIME_WAIT) {
        uint32_t linger = timer_get_ticks() + 20;
        while (timer_get_ticks() < linger) task_yield();
        tcb.state = TCP_CLOSED;
        klog("[tcp] closed cleanly\n");
        return;
    }

    /* The peer never sent its FIN. Forgetting the connection here would leave
     * it half-open at the other end, still holding a socket and retransmitting
     * a FIN at a TCB that no longer exists - which is exactly what made a
     * third connection to the same listener hang: the first one was still
     * lingering on the host side. A RST says so explicitly. */
    if (tcb.state != TCP_CLOSED) {
        tcp_send_flags(TCP_RST | TCP_ACK, tcb.snd_nxt, tcb.rcv_nxt);
        tcb.state = TCP_CLOSED;
        klog("[tcp] closed (peer never finished; reset)\n");
        return;
    }

    klog("[tcp] closed cleanly\n");
}

int tcp_listen(uint16_t port) {
    tcb.peer_ip     = 0;
    tcb.peer_port   = 0;
    tcb.local_port  = port;
    tcb.snd_nxt     = 0;
    tcb.snd_una     = 0;
    tcb.rcv_nxt     = 0;
    tcb.peer_window = 0;
    tcb.refused     = 0;
    tcb.peer_fin    = 0;
    tcb.fin_sent    = 0;
    rx_len          = 0;
    tcb.state       = TCP_LISTEN;
    klog("[tcp] listening on port %u\n", port);
    return 0;
}

int tcp_accept(uint32_t ticks) {
    uint32_t start = timer_get_ticks();
    while (timer_get_ticks() - start < ticks) {
        if (tcb.state == TCP_ESTABLISHED) {
            klog("[tcp] accepted %u.%u.%u.%u:%u on port %u\n",
                 (tcb.peer_ip >> 24) & 0xFF, (tcb.peer_ip >> 16) & 0xFF,
                 (tcb.peer_ip >> 8) & 0xFF, tcb.peer_ip & 0xFF,
                 tcb.peer_port, tcb.local_port);
            return 0;
        }
        if (tcb.state == TCP_CLOSED) return -1;   /* reset while handshaking */
        task_yield();
        __asm__ volatile ("hlt");
    }
    return -1;
}
