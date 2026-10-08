#pragma once
#include <stdint.h>

/* users.h - the soupOS kitchen staff.
 *
 * Accounts live in /etc/kitchen on the FAT volume, one cook per line:
 *     name:uid:hexhash
 * uid 0 is "headchef" - the superuser. Every other account is a "cook".
 * Passwords ("secrets") are stored as an 8-hex-digit AlphaSOUP-32 hash.
 *
 * If /etc/kitchen is missing at boot it is seeded with a single account,
 * headchef, whose secret is "soup".
 */

#define USERS_MAX      16
#define USER_NAME_MAX  16

/* Load /etc/kitchen, seeding a default headchef if it does not exist. */
void users_init(void);

/* Roster queries. */
int         users_count(void);
const char *users_name_at(int idx);    /* 0..users_count()-1 */
uint8_t     users_uid_at(int idx);

/* Verify name + secret. Returns the uid (>=0) on success, -1 on failure. */
int  users_check(const char *name, const char *secret);

/* Name <-> uid lookups. */
int         users_uid_of(const char *name);   /* -1 if no such cook */
const char *users_name_of(uint8_t uid);       /* "?" if unknown     */

/* Hire a new cook / fire one. Auto-assigns a uid. Persists /etc/kitchen.
 * Returns 0=ok, -1=error (duplicate, roster full, headchef, disk error). */
int  users_add(const char *name, const char *secret);
/* Give a cook a new secret (a fresh salt, in the build's kind). 0, or -1. */
int  users_set_secret(const char *name, const char *secret);
/* Pin every system file's owner and mode (v0.54.2). */
void users_seal_system(void);
/* Bracket a kernel-internal write made on a cook's behalf, so it may use
 * the disk's reserve (v0.55.5). Nests. */
void users_sys_begin(void);
void users_sys_end(void);
/* Could every cook read this path? (hatch serves only these, v0.55.0) */
int  users_public(const char *path);
/* Search on every bowl above the path (stat's rule, v0.59.0). */
int  users_reach(const char *path);
int  users_remove(const char *name);

/* The logged-in session. */
void        users_set_current(uint8_t uid);
/* The current user is per shell since v0.36.0: an SSH session is logged in as
 * whoever authenticated, not as whoever sits at the console. The shell
 * registers this; it returns the asking task's shell uid, or -1 for "the
 * console's user" (users_set_current). */
void        users_set_resolver(int (*resolve)(void));
uint8_t     users_current_uid(void);
const char *users_current_name(void);
int         users_is_headchef(void);          /* 1 if current uid == 0 */
/* May the current cook do `need` ('r','w','x') to `path`? The headchef always
 * may; otherwise the owner or the all-cooks bits decide; a missing path is
 * allowed (the operation fails on its own). One rule for the shell and for
 * programs' syscalls (v0.48.0). */
int         users_may(const char *path, char need);
