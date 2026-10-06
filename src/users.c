#include "users.h"
#include "fat.h"
#include "alphasoup.h"
#include "str.h"

/* ---- roster ---- */

typedef struct {
    char     name[USER_NAME_MAX];
    uint8_t  uid;
    uint32_t hash;          /* AlphaSOUP-32 hash of the secret */
} user_t;

static user_t  roster[USERS_MAX];
static int     n_users  = 0;
static uint8_t cur_uid  = 0;

#define KITCHEN_PATH  "/etc/kitchen"

/* ---- small string helpers ---- */

static uint32_t hash_secret(const char *s) {
    return alphasoup_hash(s, (uint32_t)strlen(s));
}

/* uint32 -> 8 lowercase hex digits (always 8, zero-padded). */
static void u32_to_hex(uint32_t v, char *out) {
    static const char d[] = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) { out[i] = d[v & 0xF]; v >>= 4; }
    out[8] = '\0';
}

/* Parse up to 8 hex digits. */
static uint32_t hex_to_u32(const char *s) {
    uint32_t v = 0;
    for (int i = 0; i < 8 && s[i]; i++) {
        char c = s[i];
        uint32_t d;
        if      (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else break;
        v = (v << 4) | d;
    }
    return v;
}

/* uint -> decimal text. Returns length written (no NUL). */
static int u_to_dec(uint32_t v, char *out) {
    char tmp[12];
    int  n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    return n;
}

static void copy_name(char *dst, const char *src) {
    int i = 0;
    while (src[i] && i < USER_NAME_MAX - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ---- persistence ---- */

/* Serialise the roster to /etc/kitchen. Returns 0=ok, -1=disk error. */
static int users_save(void) {
    static char buf[USERS_MAX * (USER_NAME_MAX + 16)];
    uint32_t len = 0;
    for (int i = 0; i < n_users; i++) {
        for (int k = 0; roster[i].name[k]; k++) buf[len++] = roster[i].name[k];
        buf[len++] = ':';
        len += (uint32_t)u_to_dec(roster[i].uid, buf + len);
        buf[len++] = ':';
        char hx[9];
        u32_to_hex(roster[i].hash, hx);
        for (int k = 0; k < 8; k++) buf[len++] = hx[k];
        buf[len++] = '\n';
    }
    fat_mkdir("/etc");                       /* harmless if it already exists */
    return fat_write(KITCHEN_PATH, (const uint8_t *)buf, len);
}

/* Parse one "name:uid:hexhash" line into the roster. */
static void parse_line(const char *line, int len) {
    char name[USER_NAME_MAX];
    int  i = 0, j = 0;
    while (i < len && line[i] != ':' && j < USER_NAME_MAX - 1) name[j++] = line[i++];
    name[j] = '\0';
    if (j == 0 || i >= len || line[i] != ':') return;
    i++;
    uint32_t uid = 0;
    while (i < len && line[i] >= '0' && line[i] <= '9')
        uid = uid * 10 + (uint32_t)(line[i++] - '0');
    if (i >= len || line[i] != ':') return;
    i++;
    char hx[9];
    int  h = 0;
    while (i < len && h < 8) hx[h++] = line[i++];
    hx[h] = '\0';
    if (n_users >= USERS_MAX) return;
    copy_name(roster[n_users].name, name);
    roster[n_users].uid  = (uint8_t)uid;
    roster[n_users].hash = hex_to_u32(hx);
    n_users++;
}

/* ---- public ---- */

void users_init(void) {
    n_users = 0;
    cur_uid = 0;

    static uint8_t buf[USERS_MAX * (USER_NAME_MAX + 16) + 16];
    uint32_t size = 0;
    if (fat_read(KITCHEN_PATH, buf, sizeof(buf) - 1, &size) == 0 && size > 0) {
        uint32_t s = 0;
        while (s < size) {
            uint32_t e = s;
            while (e < size && buf[e] != '\n' && buf[e] != '\r') e++;
            if (e > s) parse_line((const char *)buf + s, (int)(e - s));
            s = e + 1;
        }
    }

    /* No file, or it had no headchef -> seed the default headchef account. */
    if (users_uid_of("headchef") != 0) {
        n_users = 0;
        copy_name(roster[0].name, "headchef");
        roster[0].uid  = 0;
        /* Not guessable, and it does not need to be: users_check compares
         * hashes, so any preimage of this 32-bit hash is accepted. That is
         * the whole of challenge stage 2. */
        roster[0].hash = hash_secret("xxxxxxxx");
        /* The account players are given. Ordinary cook, no privileges. */
        copy_name(roster[1].name, "cook");
        roster[1].uid  = 1;
        roster[1].hash = hash_secret("soup");
        n_users = 2;
        users_save();
    }
}

int         users_count(void)        { return n_users; }
const char *users_name_at(int idx)   { return (idx >= 0 && idx < n_users) ? roster[idx].name : "?"; }
uint8_t     users_uid_at(int idx)    { return (idx >= 0 && idx < n_users) ? roster[idx].uid  : 0; }

int users_uid_of(const char *name) {
    for (int i = 0; i < n_users; i++)
        if (strcmp(roster[i].name, name) == 0) return (int)roster[i].uid;
    return -1;
}

const char *users_name_of(uint8_t uid) {
    for (int i = 0; i < n_users; i++)
        if (roster[i].uid == uid) return roster[i].name;
    return "?";
}

int users_check(const char *name, const char *secret) {
    for (int i = 0; i < n_users; i++) {
        if (strcmp(roster[i].name, name) == 0) {
            if (roster[i].hash == hash_secret(secret)) return (int)roster[i].uid;
            return -1;
        }
    }
    return -1;
}

int users_add(const char *name, const char *secret) {
    if (!name || !name[0])              return -1;
    if (n_users >= USERS_MAX)           return -1;
    if (users_uid_of(name) >= 0)        return -1;   /* duplicate */

    /* Smallest free uid in 1..254. */
    uint32_t uid = 1;
    for (; uid < 255; uid++) {
        int taken = 0;
        for (int i = 0; i < n_users; i++)
            if (roster[i].uid == uid) { taken = 1; break; }
        if (!taken) break;
    }
    if (uid >= 255) return -1;

    copy_name(roster[n_users].name, name);
    roster[n_users].uid  = (uint8_t)uid;
    roster[n_users].hash = hash_secret(secret ? secret : "");
    n_users++;
    return users_save();
}

int users_remove(const char *name) {
    int idx = -1;
    for (int i = 0; i < n_users; i++)
        if (strcmp(roster[i].name, name) == 0) { idx = i; break; }
    if (idx < 0)                 return -1;
    if (roster[idx].uid == 0)    return -1;          /* never fire headchef */

    for (int i = idx; i < n_users - 1; i++) roster[i] = roster[i + 1];
    n_users--;
    return users_save();
}

void        users_set_current(uint8_t uid) { cur_uid = uid; }
uint8_t     users_current_uid(void)        { return cur_uid; }
const char *users_current_name(void)       { return users_name_of(cur_uid); }
int         users_is_headchef(void)        { return cur_uid == 0; }
