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
/* A table, not a single block of state. The listener is kept SEPARATE from
 * the connections it accepts, which is the whole point: previously the
 * listener became the connection, so a second client knocking mid-request was
 * refused. Now a SYN takes a free slot and the listener stays listening.
 *
 * Four is plenty for a machine serving files one page at a time, and each slot
 * carries its own 4 KB of received data, so the table costs 16 KB. */

typedef struct {
    volatile tcp_state_t state;
    uint32_t  peer_ip;
    uint16_t  local_port, peer_port;
    uint32_t  snd_nxt;
    volatile uint32_t snd_una;      /* oldest unacknowledged          */
    volatile uint32_t rcv_nxt;      /* next sequence number we expect */
    volatile uint16_t peer_window;
    volatile int      refused;      /* peer sent RST                  */
    volatile int      peer_fin;     /* peer has finished sending      */
    int               fin_sent;     /* we have sent ours              */
    volatile int      accepted;     /* handed to a server loop already */

    /* Received data waiting for a reader. Written by the interrupt handler,
     * drained by tcp_recv, so the count is volatile and the drain disables
     * interrupts for the memmove rather than trying to be clever. */
    uint8_t           rx[TCP_RX_MAX];
    volatile uint16_t rx_len;
    uint32_t  tx_bytes, rx_bytes;   /* data carried, for kitchen and the close line */
} tcp_conn_t;

static tcp_conn_t conns[TCP_MAX_CONNS];

/* The outbound API (tcp_connect and friends) still talks about "the"
 * connection, because reserve, holler and takeout each make exactly one. This
 * is the slot they mean. */
static int client_h = -1;

/* The listener, which owns no connection of its own. */
static uint16_t listen_ports[TCP_MAX_LISTEN];   /* 0 = free slot */

static int port_listening(uint16_t port) {
    for (int i = 0; i < TCP_MAX_LISTEN; i++) if (listen_ports[i] == port) return 1;
    return 0;
}

static tcp_conn_t *conn_at(int h) {
    if (h < 0 || h >= TCP_MAX_CONNS) return 0;
    return &conns[h];
}

static int conn_alloc(void) {
    for (int i = 0; i < TCP_MAX_CONNS; i++)
        if (conns[i].state == TCP_CLOSED) { conns[i].tx_bytes = conns[i].rx_bytes = 0; return i; }
    return -1;
}

/* The four-tuple lookup that replaces "is this for our one connection?". */
static int conn_find(uint32_t src_ip, uint16_t sport, uint16_t dport) {
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        tcp_conn_t *c = &conns[i];
        if (c->state == TCP_CLOSED)     continue;
        if (c->peer_ip   != src_ip)     continue;
        if (c->local_port != dport)     continue;
        if (c->peer_port  != sport)     continue;
        return i;
    }
    return -1;
}

static uint16_t next_port = 49152;  /* ephemeral range */

tcp_state_t tcp_state_of(int h) { tcp_conn_t *c = conn_at(h); return c ? c->state : TCP_CLOSED; }
tcp_state_t tcp_state(void)     { return tcp_state_of(client_h); }

static inline uint16_t hton16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static inline uint16_t ntoh16(uint16_t v) { return hton16(v); }
static inline uint32_t hton32(uint32_t v) {
    return ((v & 0xFFu) << 24) | ((v & 0xFF00u) << 8) |
           ((v >> 8) & 0xFF00u) | ((v >> 24) & 0xFFu);
}
static inline uint32_t ntoh32(uint32_t v) { return hton32(v); }

/* Build and send one segment, with an optional payload. */
static void tcp_send_seg(tcp_conn_t *c, uint8_t flags, uint32_t seq, uint32_t ack,
                         const uint8_t *data, uint16_t dlen) {
    uint8_t pkt[sizeof(tcp_hdr_t) + TCP_MSS];
    if (dlen > TCP_MSS) dlen = TCP_MSS;
    tcp_hdr_t *t = (tcp_hdr_t *)pkt;

    t->sport   = hton16(c->local_port);
    t->dport   = hton16(c->peer_port);
    t->seq     = hton32(seq);
    t->ack     = hton32(ack);
    t->off_rsv = (uint8_t)((sizeof(tcp_hdr_t) / 4) << 4);
    t->flags   = flags;
    t->window  = hton16(4096);      /* what we are willing to receive */
    t->csum    = 0;
    t->urg     = 0;
    if (dlen && data) memcpy(pkt + sizeof(tcp_hdr_t), data, dlen);

    uint16_t total = (uint16_t)(sizeof(tcp_hdr_t) + dlen);
    t->csum = hton16(net_pseudo_checksum(net_ip(), c->peer_ip, 6, pkt, total));
    net_ip_send(c->peer_ip, 6, pkt, total);
}

static void tcp_send_flags(tcp_conn_t *c, uint8_t flags, uint32_t seq, uint32_t ack) {
    tcp_send_seg(c, flags, seq, ack, 0, 0);
}

/* Interrupt context: just enough to finish the handshake. */
void tcp_input(uint32_t src_ip, const uint8_t *seg, uint16_t len) {
    if (len < sizeof(tcp_hdr_t)) return;
    const tcp_hdr_t *t = (const tcp_hdr_t *)seg;

    uint16_t sport = ntoh16(t->sport), dport = ntoh16(t->dport);

    int h = conn_find(src_ip, sport, dport);
    if (h < 0) {
        /* Nothing matches the four-tuple. A SYN for the listening port starts
         * a new connection in a free slot; the listener itself is untouched,
         * so the next client is not turned away. */
        if (!port_listening(dport) || !(t->flags & TCP_SYN)) return;

        h = conn_alloc();
        if (h < 0) return;          /* table full: let them retransmit */

        tcp_conn_t *nc  = &conns[h];
        nc->peer_ip     = src_ip;
        nc->peer_port   = sport;
        nc->local_port  = dport;
        nc->rcv_nxt     = ntoh32(t->seq) + 1;   /* their SYN takes one */
        nc->peer_window = ntoh16(t->window);
        nc->refused     = 0;
        nc->peer_fin    = 0;
        nc->fin_sent    = 0;
        nc->accepted    = 0;
        nc->rx_len      = 0;

        uint32_t iss = ((timer_get_ticks() << 8) ^ 0x5000D00Du) + (uint32_t)h;
        nc->snd_una  = iss;
        nc->snd_nxt  = iss + 1;                 /* ours takes one too  */
        nc->state    = TCP_SYN_RCVD;
        tcp_send_flags(nc, TCP_SYN | TCP_ACK, iss, nc->rcv_nxt);
        return;
    }

    tcp_conn_t *c = &conns[h];

    if (t->flags & TCP_RST) {
        c->refused = 1;
        c->state   = TCP_CLOSED;
        return;
    }

    /* A duplicate SYN/ACK means our ACK was lost or was slower than their
     * retransmit timer. Answering it again is both correct and cheap; ignoring
     * it leaves the peer retransmitting until it gives up. */
    if (c->state == TCP_ESTABLISHED && (t->flags & TCP_SYN) && (t->flags & TCP_ACK)) {
        tcp_send_flags(c, TCP_ACK, c->snd_nxt, c->rcv_nxt);
        return;
    }

    if (c->state == TCP_SYN_RCVD) {
        if (t->flags & TCP_SYN) {        /* our SYN-ACK was lost: say it again */
            tcp_send_flags(c, TCP_SYN | TCP_ACK, c->snd_una, c->rcv_nxt);
            return;
        }
        if (t->flags & TCP_ACK) {
            c->snd_una     = ntoh32(t->ack);
            c->peer_window = ntoh16(t->window);
            c->state       = TCP_ESTABLISHED;
            /* Fall through: this ACK may carry the request's first bytes,
             * and SLIRP does exactly that often enough to matter. */
        } else {
            return;
        }
    }

    if (c->state == TCP_SYN_SENT && (t->flags & TCP_SYN) && (t->flags & TCP_ACK)) {
        /* Their SYN occupies one sequence number, so what we expect next is
         * their sequence plus one. */
        c->rcv_nxt     = ntoh32(t->seq) + 1;
        c->snd_una     = ntoh32(t->ack);
        c->peer_window = ntoh16(t->window);
        c->state       = TCP_ESTABLISHED;
        tcp_send_flags(c, TCP_ACK, c->snd_nxt, c->rcv_nxt);
        return;
    }

    /* Everything from here on applies to any open connection, not just an
     * established one: acknowledgements and even data keep arriving after a
     * FIN has been seen in either direction, and ignoring them leaves both
     * sides retransmitting at each other until they give up. */
    if (c->state == TCP_CLOSED || c->state == TCP_SYN_SENT) return;

    if (t->flags & TCP_ACK) {
        uint32_t a = ntoh32(t->ack);
        /* Signed comparison handles the sequence space wrapping; a plain > is
         * wrong once the numbers pass 2^31. */
        if ((int32_t)(a - c->snd_una) > 0) c->snd_una = a;
        c->peer_window = ntoh16(t->window);

        /* Our FIN consumed a sequence number, so it is acknowledged once
         * snd_una reaches snd_nxt. */
        if (c->fin_sent && (int32_t)(c->snd_una - c->snd_nxt) >= 0) {
            if (c->state == TCP_FIN_WAIT_1) c->state = TCP_FIN_WAIT_2;
            else if (c->state == TCP_LAST_ACK) c->state = TCP_CLOSED;
        }
    }

    uint16_t hlen  = (uint16_t)((t->off_rsv >> 4) * 4);
    if (hlen < sizeof(tcp_hdr_t) || hlen > len) return;
    uint16_t dlen  = (uint16_t)(len - hlen);
    uint32_t seq   = ntoh32(t->seq);

    if (dlen) {
        if (seq == c->rcv_nxt) {
            uint16_t room = (uint16_t)(TCP_RX_MAX - c->rx_len);
            uint16_t take = (dlen < room) ? dlen : room;
            memcpy(c->rx + c->rx_len, seg + hlen, take);
            c->rx_len      = (uint16_t)(c->rx_len + take);
            c->rx_bytes   += take;
            c->rcv_nxt = seq + take;      /* only what we actually kept */
        }
        /* Either way, tell them where we are. An out-of-order segment gets the
         * same answer, which is this stack's way of asking for a resend. */
        tcp_send_flags(c, TCP_ACK, c->snd_nxt, c->rcv_nxt);
    }

    if ((t->flags & TCP_FIN) && seq == c->rcv_nxt) {
        /* Their FIN occupies a sequence number too, so acknowledge one past
         * it. Where that leaves us depends on whether we have already sent
         * ours: if we have, both sides are done; if not, they have stopped
         * talking but we may still have something to say. */
        c->peer_fin = 1;
        c->rcv_nxt  = seq + 1;
        tcp_send_flags(c, TCP_ACK, c->snd_nxt, c->rcv_nxt);

        if (c->state == TCP_ESTABLISHED)      c->state = TCP_CLOSE_WAIT;
        else if (c->state == TCP_FIN_WAIT_1)  c->state = TCP_LAST_ACK;
        else if (c->state == TCP_FIN_WAIT_2)  c->state = TCP_TIME_WAIT;
    }
}

/* Wait for the network, and do not burn the cpu doing it (queue item 6):
 * let any other task run, then halt until the next interrupt - the NIC's,
 * when an ACK or data arrives. Only when interrupts are on: the pass's
 * console sink can call tcp_send_on from inside a syscall, and halting with
 * interrupts off would never wake. Measured, though: this changed nothing.
 * hatch serving 1 MB files in a loop takes 95-98% of the machine before and
 * after, and a TSC profile shows why - 320 waits in 46 fetches, because
 * SLIRP's ACK has always arrived by the time the loop looks. The cost is
 * the emulated NIC (about 42,000 cycles a frame to transmit, plus the
 * receive interrupts), which is real work. Kept because every other wait
 * loop in the kernel halts and this one should not be the exception. */
static void net_wait(void) {
    task_yield();
    uint32_t f;
    __asm__ volatile ("pushf; pop %0" : "=r"(f));
    if (f & 0x200) cpu_halt();
}

int tcp_send_on(int h, const void *data, uint16_t len) {
    tcp_conn_t *c = conn_at(h);
    if (!c) return -1;
    if (c->state != TCP_ESTABLISHED || !data || !len) return -1;

    const uint8_t *p = (const uint8_t *)data;
    uint16_t sent = 0;

    while (sent < len) {
        uint16_t chunk = (uint16_t)(len - sent);
        if (chunk > TCP_MSS) chunk = TCP_MSS;

        uint32_t seq = c->snd_nxt;
        c->snd_nxt  = seq + chunk;

        /* One segment in flight: send it, wait for the acknowledgement to
         * cover it, resend if it does not come. Stop-and-wait, deliberately. */
        int acked = 0;
        for (int attempt = 0; attempt < 4 && !acked; attempt++) {
            tcp_send_seg(c, TCP_PSH | TCP_ACK, seq, c->rcv_nxt, p + sent, chunk);

            uint32_t until = timer_get_ticks() + 50;      /* 500 ms */
            while (timer_get_ticks() < until) {
                if ((int32_t)(c->snd_una - (seq + chunk)) >= 0) { acked = 1; break; }
                if (c->refused || c->state != TCP_ESTABLISHED) return -1;
                net_wait();
            }
        }
        if (!acked) {
            klog("[tcp] no ack for %u bytes at seq %u\n", chunk, seq);
            return (int)sent ? (int)sent : -1;
        }
        sent = (uint16_t)(sent + chunk);
        c->tx_bytes += chunk;
    }

    /* Only the outbound connection logs its bytes: the services carry a
     * terminal, and a line per keystroke was most of the log. */
    if (h == client_h) klog("[tcp] sent %u bytes\n", sent);
    return (int)sent;
}

int tcp_peer_done_on(int h) { tcp_conn_t *c = conn_at(h); return c ? c->peer_fin : 1; }
int tcp_peer_done(void)     { return tcp_peer_done_on(client_h); }

int tcp_recv_on(int h, uint8_t *out, uint16_t max, uint32_t ticks) {
    tcp_conn_t *c = conn_at(h);
    if (!c) return -1;
    if (!out || !max) return 0;

    uint32_t until = timer_get_ticks() + ticks;
    while (c->rx_len == 0 && timer_get_ticks() < until) {
        if (c->refused || c->peer_fin) break;
        net_wait();
    }
    if (c->rx_len == 0) return 0;

    /* The handler appends to the same buffer, so take a consistent snapshot
     * rather than racing it mid-memmove. */
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) :: "memory");
    uint16_t n = (c->rx_len < max) ? c->rx_len : max;
    memcpy(out, c->rx, n);
    if (n < c->rx_len) memmove(c->rx, c->rx + n, (uint32_t)(c->rx_len - n));
    c->rx_len = (uint16_t)(c->rx_len - n);
    __asm__ volatile ("push %0; popf" :: "r"(flags) : "memory", "cc");

    if (h == client_h) klog("[tcp] rx %u bytes\n", n);
    return (int)n;
}

int tcp_connect(uint32_t dst_ip, uint16_t dst_port) {
    int h = conn_alloc();
    if (h < 0) return -1;
    client_h = h;
    tcp_conn_t *c = &conns[h];
    uint8_t mac[6];
    /* Task context, so resolving here is allowed; net_ip_send will not. */
    if (net_arp_resolve(((dst_ip & net_mask()) == (net_ip() & net_mask()))
                            ? dst_ip : net_gw(), mac) < 0) {
        klog("[tcp] no route to %u.%u.%u.%u\n",
             (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
             (dst_ip >> 8) & 0xFF, dst_ip & 0xFF);
        return -1;
    }

    c->peer_ip     = dst_ip;
    c->peer_port   = dst_port;
    c->local_port  = next_port++;
    if (next_port == 0) next_port = 49152;
    /* An initial sequence number that at least varies between connections.
     * A real stack derives it from a clock for security reasons that do not
     * apply to a machine with one connection and no adversary. */
    c->snd_nxt     = (timer_get_ticks() << 8) ^ 0x5000C0DEu;
    c->rcv_nxt     = 0;
    c->refused     = 0;
    c->peer_fin    = 0;
    c->fin_sent    = 0;
    c->rx_len          = 0;
    c->state       = TCP_SYN_SENT;

    uint32_t iss = c->snd_nxt;
    c->snd_una = iss;
    /* Our SYN occupies one sequence number too, so everything after it is
     * numbered from iss + 1. */
    c->snd_nxt = iss + 1;

    for (int attempt = 0; attempt < 3; attempt++) {
        tcp_send_flags(c, TCP_SYN, iss, 0);
        klog("[tcp] syn to %u.%u.%u.%u:%u from port %u\n",
             (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
             (dst_ip >> 8) & 0xFF, dst_ip & 0xFF, dst_port, c->local_port);

        uint32_t until = timer_get_ticks() + 100;      /* 1 second */
        while (timer_get_ticks() < until) {
            if (c->state == TCP_ESTABLISHED) {
                klog("[tcp] established with %u.%u.%u.%u:%u, peer window %u\n",
                     (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
                     (dst_ip >> 8) & 0xFF, dst_ip & 0xFF, dst_port,
                     c->peer_window);
                return 0;
            }
            if (c->refused) {
                klog("[tcp] refused by %u.%u.%u.%u:%u\n",
                     (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
                     (dst_ip >> 8) & 0xFF, dst_ip & 0xFF, dst_port);
                return -1;
            }
            net_wait();
        }
    }

    c->state = TCP_CLOSED;
    klog("[tcp] no answer from %u.%u.%u.%u:%u\n",
         (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
         (dst_ip >> 8) & 0xFF, dst_ip & 0xFF, dst_port);
    return -1;
}

void tcp_abort_on(int h) {
    tcp_conn_t *c = conn_at(h);
    if (!c) return;
    if (c->state == TCP_CLOSED) return;
    tcp_send_flags(c, TCP_RST | TCP_ACK, c->snd_nxt, c->rcv_nxt);
    c->state = TCP_CLOSED;
    klog("[tcp] aborted (rst)\n");
}

void tcp_close_on(int h) {
    tcp_conn_t *c = conn_at(h);
    if (!c) return;
    if (c->state == TCP_CLOSED) return;

    /* Send our FIN. It occupies a sequence number, like the SYN did, so
     * snd_nxt moves past it and the peer's acknowledgement of that number is
     * what retires it. */
    if (!c->fin_sent) {
        uint32_t seq = c->snd_nxt;
        c->snd_nxt  = seq + 1;
        c->fin_sent = 1;
        c->state    = (c->state == TCP_CLOSE_WAIT) ? TCP_LAST_ACK : TCP_FIN_WAIT_1;
        tcp_send_seg(c, TCP_FIN | TCP_ACK, seq, c->rcv_nxt, 0, 0);
    }

    /* Wait for the exchange to finish, but not forever: a peer that vanishes
     * must not wedge the shell. */
    uint32_t until = timer_get_ticks() + 200;        /* 2 s */
    while (timer_get_ticks() < until) {
        if (c->state == TCP_CLOSED || c->state == TCP_TIME_WAIT) break;
        if (c->refused) break;
        net_wait();
    }

    /* TIME_WAIT exists so a late duplicate of the last segment cannot be
     * mistaken for part of a new connection on the same port pair. A real
     * stack lingers for twice the maximum segment lifetime, two minutes or so.
     * soupOS lingers for 200 ms, because it uses a fresh ephemeral port for
     * every connection and a shell that froze for two minutes after every
     * close would be unusable. The trade is written down rather than hidden:
     * reusing a port pair within that window could confuse a peer. */
    if (c->state == TCP_TIME_WAIT) {
        uint32_t linger = timer_get_ticks() + 20;
        while (timer_get_ticks() < linger) net_wait();
        c->state = TCP_CLOSED;
        klog("[tcp] closed cleanly (sent %u, received %u)\n", c->tx_bytes, c->rx_bytes);
        return;
    }

    /* The peer never sent its FIN. Forgetting the connection here would leave
     * it half-open at the other end, still holding a socket and retransmitting
     * a FIN at a TCB that no longer exists - which is exactly what made a
     * third connection to the same listener hang: the first one was still
     * lingering on the host side. A RST says so explicitly. */
    if (c->state != TCP_CLOSED) {
        tcp_send_flags(c, TCP_RST | TCP_ACK, c->snd_nxt, c->rcv_nxt);
        c->state = TCP_CLOSED;
        klog("[tcp] closed (peer never finished; reset) (sent %u, received %u)\n", c->tx_bytes, c->rx_bytes);
        return;
    }

    klog("[tcp] closed cleanly (sent %u, received %u)\n", c->tx_bytes, c->rx_bytes);
}

void tcp_conn_bytes(int i, uint32_t *tx, uint32_t *rx) {
    if (i < 0 || i >= TCP_MAX_CONNS) { *tx = *rx = 0; return; }
    *tx = conns[i].tx_bytes; *rx = conns[i].rx_bytes;
}

int tcp_conn_at(int i, tcp_state_t *st, uint32_t *peer_ip, uint16_t *peer_port, uint16_t *local_port) {
    if (i < 0 || i >= TCP_MAX_CONNS || conns[i].state == TCP_CLOSED) return 0;
    if (st)         *st         = conns[i].state;
    if (peer_ip)    *peer_ip    = conns[i].peer_ip;
    if (peer_port)  *peer_port  = conns[i].peer_port;
    if (local_port) *local_port = conns[i].local_port;
    return 1;
}
uint16_t tcp_listener_at(int i) { return (i >= 0 && i < TCP_MAX_LISTEN) ? listen_ports[i] : 0; }
const char *tcp_state_name(tcp_state_t st) {
    static const char *names[] = { "closed", "listen", "syn-rcvd", "syn-sent", "established",
        "fin-wait-1", "fin-wait-2", "close-wait", "last-ack", "time-wait" };
    return (unsigned)st < sizeof(names) / sizeof(names[0]) ? names[st] : "?";
}

int tcp_listen(uint16_t port) {
    if (!port || port_listening(port)) return port ? 0 : -1;
    for (int i = 0; i < TCP_MAX_LISTEN; i++) {
        if (listen_ports[i] == 0) {
            listen_ports[i] = port;
            klog("[tcp] listening on port %u\n", port);
            return 0;
        }
    }
    klog("[tcp] no listener slot for port %u\n", port);
    return -1;
}

void tcp_stop_listening(uint16_t port) {
    for (int i = 0; i < TCP_MAX_LISTEN; i++)
        if (listen_ports[i] == port) listen_ports[i] = 0;
}

int tcp_accept(uint16_t port, uint32_t ticks) {
    uint32_t start = timer_get_ticks();
    for (;;) {                      /* at least one scan: ticks=0 is a poll */
        for (int i = 0; i < TCP_MAX_CONNS; i++) {
            tcp_conn_t *c = &conns[i];
            if (c->state == TCP_ESTABLISHED && !c->accepted &&
                c->local_port == port) {
                c->accepted = 1;
                klog("[tcp] accepted %u.%u.%u.%u:%u on port %u (slot %d)\n",
                     (c->peer_ip >> 24) & 0xFF, (c->peer_ip >> 16) & 0xFF,
                     (c->peer_ip >> 8) & 0xFF, c->peer_ip & 0xFF,
                     c->peer_port, c->local_port, i);
                return i;
            }
        }
        if (timer_get_ticks() - start >= ticks) return -1;
        net_wait();
    }
}

/* ── The single-connection API ──────────────────────────────────────────────
 *
 * reserve, holler and takeout each make exactly one outbound connection, so
 * they keep talking about "the" connection and these forward to whichever slot
 * tcp_connect took. Anything that serves - which may hold several at once -
 * uses the handle forms above. */
int  tcp_send(const void *data, uint16_t len)            { return tcp_send_on(client_h, data, len); }
int  tcp_recv(uint8_t *out, uint16_t max, uint32_t ticks){ return tcp_recv_on(client_h, out, max, ticks); }
void tcp_close(void)                                     { tcp_close_on(client_h); }
void tcp_abort(void)                                     { tcp_abort_on(client_h); }
