#pragma once
#include <stdint.h>

/* TCP, stage one: connection setup.
 *
 * One connection at a time, the same single-slot model the resolver uses. That
 * is not a limitation anyone will notice until soupOS has something that wants
 * two sockets, and it keeps the whole state machine in one visible struct
 * instead of a table.
 *
 * Stage two adds data: one segment in flight at a time, acknowledged before
 * the next goes out. That is not a window, it is a stop-and-wait protocol
 * wearing TCP's header, and it is deliberate - there is no congestion control,
 * no window scaling and no selective acknowledgement here, and an out-of-order
 * segment is dropped so the peer resends it. On a virtual network with a
 * millisecond of latency that costs nothing; on a real one it would.
 *
 * Stage three closes properly: FIN in both directions, with the active close
 * (we hang up first) and the passive one (they do) both handled. TIME_WAIT is
 * deliberately short rather than the two-minute 2*MSL a real stack waits -
 * see the note in tcp.c for what that trades away. */

typedef enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,         /* waiting for somebody to connect to us          */
    TCP_SYN_RCVD,       /* their SYN answered, waiting for their ACK      */
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,     /* we sent FIN, waiting for it to be acknowledged */
    TCP_FIN_WAIT_2,     /* ours acknowledged, waiting for theirs          */
    TCP_CLOSE_WAIT,     /* theirs arrived first; ours still to send       */
    TCP_LAST_ACK,       /* we answered theirs, waiting for the last ack   */
    TCP_TIME_WAIT,      /* both sides done; linger briefly                */
} tcp_state_t;

/* Open a connection. Returns 0 once established, -1 on timeout or refusal.
 * Blocks: task context only. */
int         tcp_connect(uint32_t dst_ip, uint16_t dst_port);

/* Passive open: wait for somebody to connect to `port`.
 *
 * ONE CONNECTION AT A TIME, like the rest of this stack. The listener does not
 * survive the connection - the same single block of state becomes the
 * connection - so a server loop listens again after each client is done, and
 * a SYN arriving mid-request is refused rather than queued. For serving files
 * one request at a time that is the honest shape; a backlog needs a table of
 * connections, which is a bigger change than it sounds.
 *
 * tcp_listen arms it and returns at once; tcp_accept blocks until a client has
 * completed the handshake, returning 0, or -1 after `ticks` (100 Hz). */
/* Several connections at once, since v0.21.0. The listener owns no connection
 * of its own: a SYN takes a free slot and the listener keeps listening, so a
 * client knocking mid-request is served rather than refused.
 *
 * tcp_accept returns a HANDLE, and the handle forms below act on it. The
 * plain forms act on the one connection tcp_connect made, which is all the
 * outbound commands need. */
/* Several LISTENERS too, since v0.30.0: pass, vault and hatch each own a
 * port and run as their own task, and with one listener slot they re-armed it
 * to their own port in turn, so a client reached whichever had armed it last.
 * tcp_listen adds a port (idempotent; -1 if the table is full),
 * tcp_stop_listening removes it, and tcp_accept takes the port it is
 * accepting for, so each service only ever collects its own clients. */
#define TCP_MAX_LISTEN 4
#define TCP_MAX_CONNS 8
int         tcp_listen(uint16_t port);
void        tcp_stop_listening(uint16_t port);
int         tcp_accept(uint16_t port, uint32_t ticks);

int         tcp_send_on(int h, const void *data, uint16_t len);
int         tcp_recv_on(int h, uint8_t *out, uint16_t max, uint32_t ticks);
void        tcp_close_on(int h);
void        tcp_abort_on(int h);
int         tcp_peer_done_on(int h);
tcp_state_t tcp_state_of(int h);

/* Send data, blocking until the peer acknowledges it. Returns the number of
 * bytes sent, or -1 if the connection failed. Task context only. */
int         tcp_send(const void *data, uint16_t len);

/* Take up to `max` bytes already received, waiting up to `ticks` (100 Hz) for
 * some to arrive. Returns the byte count, 0 if nothing came. */
int         tcp_recv(uint8_t *out, uint16_t max, uint32_t ticks);

/* Close politely: FIN, wait for theirs, linger briefly. Returns when the
 * connection is fully closed or the wait times out. */
void        tcp_close(void);

/* Abort: RST and done. For when the peer is misbehaving or we gave up. */
void        tcp_abort(void);

/* Has the peer finished sending? True once its FIN has been seen. */
int         tcp_peer_done(void);

tcp_state_t tcp_state(void);

/* For a viewer: slot i of the connection table (0 if the slot is closed) and
 * of the listener table (0 if the slot is free). */
int         tcp_conn_at(int i, tcp_state_t *st, uint32_t *peer_ip, uint16_t *peer_port, uint16_t *local_port);
uint16_t    tcp_listener_at(int i);
/* Data bytes carried by connection slot i so far, each way. */
void        tcp_conn_bytes(int i, uint32_t *tx, uint32_t *rx);
const char *tcp_state_name(tcp_state_t st);

/* Fed by the IP layer. Interrupt context. */
void        tcp_input(uint32_t src_ip, const uint8_t *seg, uint16_t len);
