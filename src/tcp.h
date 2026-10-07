#pragma once
#include <stdint.h>

/* TCP, stage one: connection setup.
 *
 * One connection at a time, the same single-slot model the resolver uses. That
 * is not a limitation anyone will notice until soupOS has something that wants
 * two sockets, and it keeps the whole state machine in one visible struct
 * instead of a table.
 *
 * This stage does the three-way handshake and nothing above it: no data
 * transfer (stage 2b) and no graceful close (stage 2c). tcp_close() sends a
 * RST, which is a legitimate way to end a connection and an honest placeholder
 * for the FIN exchange that replaces it later. */

typedef enum {
    TCP_CLOSED = 0,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
} tcp_state_t;

/* Open a connection. Returns 0 once established, -1 on timeout or refusal.
 * Blocks: task context only. */
int         tcp_connect(uint32_t dst_ip, uint16_t dst_port);

/* Tear the connection down (RST for now; see the note above). */
void        tcp_close(void);

tcp_state_t tcp_state(void);

/* Fed by the IP layer. Interrupt context. */
void        tcp_input(uint32_t src_ip, const uint8_t *seg, uint16_t len);
