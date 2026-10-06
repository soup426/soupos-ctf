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
int  users_remove(const char *name);

/* The logged-in session. */
void        users_set_current(uint8_t uid);
uint8_t     users_current_uid(void);
const char *users_current_name(void);
int         users_is_headchef(void);          /* 1 if current uid == 0 */
