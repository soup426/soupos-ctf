#pragma once
/* logins - a record of who clocked in, and who was refused, that survives a
 * reboot (v0.53.2). One text line per event in /etc/logins, newest last,
 * the newest LOGINS_KEEP kept. Read by `last`. */
#define LOGINS_PATH  "/etc/logins"
#define LOGINS_KEEP  64
#define LOGINS_LINE  64

/* Record a login: `ok` 1 for clocked in, 0 for refused. `where` is
 * "console" or the client's address. */
void logins_record(const char *cook, const char *where, int ok);
