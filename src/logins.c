/* logins.c - the login record behind `last` (v0.53.2).
 *
 * klog lines die with the machine; this file does not. Every console login
 * and every SSH login, and every refused password, appends one line:
 *
 *   2026-10-08 01:04  ok       saucier          console
 *   2026-10-08 01:05  refused  intern           10.0.2.2
 *
 * The file is rewritten whole each time (it is at most 64 lines of 64
 * bytes), keeping the newest LOGINS_KEEP. It is always the headchef's, mode
 * rw-r--: the write happens as whoever is logging in, so ownership is set
 * explicitly afterwards. A refused name is whatever was typed, so it is
 * cut to printable characters and 15 of them. A mutex serialises the four
 * SSH workers and the console. */
#include "logins.h"
#include "fat.h"
#include "rtc.h"
#include "task.h"
#include "heap.h"
#include "str.h"

static mutex_t logins_mtx;

static char *put2(char *p, unsigned v) { *p++ = (char)('0' + v / 10 % 10); *p++ = (char)('0' + v % 10); return p; }

static char *put_field(char *p, const char *s, int width) {
    int n = 0;
    for (; s && *s && n < 15; s++)
        if (*s > ' ' && *s < 127) { *p++ = *s; n++; }
    if (!n) { *p++ = '?'; n = 1; }
    while (n++ < width) *p++ = ' ';
    return p;
}

void logins_record(const char *cook, const char *where, int ok) {
    char line[LOGINS_LINE + 8], *p = line;
    rtc_time_t t; rtc_read(&t);
    unsigned y = t.year;
    *p++ = (char)('0' + y / 1000 % 10); *p++ = (char)('0' + y / 100 % 10); p = put2(p, y % 100);
    *p++ = '-'; p = put2(p, t.month); *p++ = '-'; p = put2(p, t.day); *p++ = ' ';
    p = put2(p, t.hour); *p++ = ':'; p = put2(p, t.minute); *p++ = ' '; *p++ = ' ';
    p = put_field(p, ok ? "ok" : "refused", 9);
    p = put_field(p, cook, 17);
    p = put_field(p, where, 1);
    *p++ = '\n'; *p = '\0';
    size_t ll = (size_t)(p - line);

    size_t cap = LOGINS_KEEP * (LOGINS_LINE + 8) + sizeof(line);
    uint8_t *buf = kmalloc(cap);
    if (!buf) return;
    mutex_lock(&logins_mtx);
    if (!fat_is_dir("/etc")) { fat_mkdir("/etc"); fat_chown("/etc", 0); }   /* never the cook's */
    uint32_t got = 0;
    if (fat_read(LOGINS_PATH, buf, (uint32_t)(cap - ll), &got) < 0) got = 0;
    if (got > cap - ll) got = (uint32_t)(cap - ll);       /* fat_read reports the file's size */
    memcpy(buf + got, line, ll);
    uint32_t len = got + (uint32_t)ll;
    /* Keep the newest LOGINS_KEEP lines. */
    uint32_t lines = 0;
    for (uint32_t i = 0; i < len; i++) if (buf[i] == '\n') lines++;
    uint32_t start = 0;
    while (lines > LOGINS_KEEP && start < len) { if (buf[start++] == '\n') lines--; }
    if (fat_write(LOGINS_PATH, buf + start, len - start) == 0) {
        fat_chown(LOGINS_PATH, 0);
        fat_chmod(LOGINS_PATH, FAT_PERM_MARK | FAT_PERM_OR | FAT_PERM_OW | FAT_PERM_AR);
    }
    mutex_unlock(&logins_mtx);
    kfree(buf);
}
