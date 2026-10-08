#pragma once
#include <stdint.h>

/* The pass: a shell session over TCP, sharing the one local shell the way the
 * serial console does. Runs as a task, so the shell keeps running while it
 * pumps bytes.
 *
 * Unencrypted and unauthenticated on the wire: whoever reaches the port gets
 * the keyboard, and the login prompt is typed in clear. Local networks only;
 * SSH is the item after this one.
 *
 * Returns 0, -1 if already running, -2 with no IP address, -3 if the task
 * could not be spawned. */
int      remote_start(uint16_t port);
void     remote_stop(void);
int      remote_running(void);
uint16_t remote_port(void);
/* The pass caller's address while one is connected, else 0 (v0.55.1). */
uint32_t remote_peer(void);
