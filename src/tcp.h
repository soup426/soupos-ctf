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
 * Still to come: the graceful close (stage 2c). tcp_close() sends a RST, which
 * is a legitimate way to end a connection and an honest placeholder for the
 * FIN exchange that replaces it. */

typedef enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
} tcp_state_t;

/* Open a connection. Returns 0 once established, -1 on timeout or refusal.
 * Blocks: task context only. */
int         tcp_connect(uint32_t dst_ip, uint16_t dst_port);

/* Send data, blocking until the peer acknowledges it. Returns the number of
 * bytes sent, or -1 if the connection failed. Task context only. */
int         tcp_send(const void *data, uint16_t len);

/* Take up to `max` bytes already received, waiting up to `ticks` (100 Hz) for
 * some to arrive. Returns the byte count, 0 if nothing came. */
int         tcp_recv(uint8_t *out, uint16_t max, uint32_t ticks);

/* Tear the connection down (RST for now; see the note above). */
void        tcp_close(void);

tcp_state_t tcp_state(void);

/* Fed by the IP layer. Interrupt context. */
void        tcp_input(uint32_t src_ip, const uint8_t *seg, uint16_t len);
