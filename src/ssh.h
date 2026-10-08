#pragma once
#include <stdint.h>

/* SSH, stage S6: the transport. RFC 4253 version exchange and binary packet
 * protocol, curve25519-sha256 key exchange (RFC 8731) with an ssh-ed25519
 * host key (RFC 8709), and chacha20-poly1305@openssh.com for everything after
 * NEWKEYS. After that, RFC 4252 userauth up to the point of saying "no":
 * the client is told `password` is available and refused, so a real `ssh`
 * reaches "Permission denied" over an encrypted, authenticated-server
 * connection and prints the host fingerprint on the way. Stage S7 accepts the
 * password and attaches the session channel to the console.
 *
 * The host key is a 32-byte Ed25519 seed kept at /HOSTKEY.ED on the FAT
 * volume, generated from the entropy pool on first start, so the fingerprint
 * is stable across reboots and a client's known_hosts entry keeps working.
 *
 * ONE SESSION AT A TIME, in static buffers, because the shell underneath is
 * one shell. Packets are capped at SSH_MAX_PACKET; a client's KEXINIT is
 * about 1.5 KB, so that is plenty for the protocol and small enough for the
 * kernel heap not to notice.
 *
 * This is hand-rolled crypto in a hobby kernel, verified against test
 * vectors but not against an adversary. It is for a private network. */

#define SSH_MAX_PACKET 4096

/* 0 ok, -1 already running, -2 no IP address, -3 no task, -4 no host key. */
int      ssh_start(uint16_t port);
void     ssh_stop(void);
int      ssh_running(void);
uint16_t ssh_port(void);

/* "SHA256:..." of the host key, as `ssh` prints it. Empty until started. */
const char *ssh_fingerprint(void);

/* Rekey after this many packets out as well as after an hour; 0 means only
 * the hour. `vault rekey <packets>`, which is for testing it. */
void ssh_set_rekey_packets(uint32_t n);
/* End sessions with no client input for `seconds` (0: never, the default). */
/* Remove a cook's lines from /AUTHKEYS (fire). Lines removed, or -1. */
int      ssh_forget_keys(const char *cook);
void     ssh_set_idle(uint32_t seconds);
uint32_t ssh_idle(void);
