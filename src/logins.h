#pragma once
/* logins - a record of who clocked in, and who was refused, that survives a
 * reboot (v0.53.2). One text line per event in /etc/logins, newest last.
 * Read by `last`.
 *
 * Successes and refusals are kept apart (v0.55.4): the newest LOGINS_KEEP_OK
 * of one and LOGINS_KEEP_BAD of the other, in time order. Measured on
 * v0.55.3, with the newest 64 lines kept whatever they were: 64 wrong
 * logins typed at the console in 81 s pushed the headchef's real login out
 * of the record (through the pass, about 8 minutes). Now a refusal can only
 * push out a refusal. */
#define LOGINS_PATH      "/etc/logins"
#define LOGINS_KEEP_OK   48
#define LOGINS_KEEP_BAD  48
#define LOGINS_KEEP      (LOGINS_KEEP_OK + LOGINS_KEEP_BAD)
#define LOGINS_LINE  64

/* Record a login: `ok` 1 for clocked in, 0 for refused. `where` is
 * "console" or the client's address. */
void logins_record(const char *cook, const char *where, int ok);
