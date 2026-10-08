#include "users.h"
#include "task.h"
#include "fat.h"
#include "alphasoup.h"
#include "str.h"
#include "sha256.h"
#include "random.h"
#include "klog.h"
#include "timer.h"

/* ---- roster ---- */

/* Two ways a secret is kept (v0.46.0).
 *   SECRET_SOUP32  the AlphaSOUP-32 hash, unsalted, any preimage accepted.
 *                  The CHALLENGE build keeps exactly this: it is challenge
 *                  stage 2. Line: name:uid:8hex
 *   SECRET_PBKDF2  PBKDF2-HMAC-SHA256, a random 16-byte salt, an iteration
 *                  count stored with it. The ordinary build makes these, and
 *                  rewrites a SOUP32 entry as one at its first good login.
 *                  Line: name:uid:$p$iterations$32hex-salt$64hex-key */
#define SECRET_SOUP32 0
#define SECRET_PBKDF2 1
#define PBKDF2_ITERS  20000      /* ~50 ms a login under KVM; stored per entry */

typedef struct {
    char     name[USER_NAME_MAX];
    uint8_t  uid;
    uint8_t  kind;
    uint32_t hash;          /* SOUP32 */
    uint32_t iters;         /* PBKDF2 */
    uint8_t  salt[16];
    uint8_t  key[32];
} user_t;

#ifdef NO_CHALLENGE
#define MAKE_PBKDF2 1        /* the ordinary build */
#else
#define MAKE_PBKDF2 0        /* the challenge build: stage 2 needs SOUP32 */
#endif

static void set_secret(user_t *u, const char *secret) {
    if (MAKE_PBKDF2) {
        u->kind  = SECRET_PBKDF2;
        u->iters = PBKDF2_ITERS;
        random_bytes(u->salt, sizeof(u->salt));
        pbkdf2_hmac_sha256(secret, strlen(secret), u->salt, sizeof(u->salt), u->iters, u->key, sizeof(u->key));
    } else {
        u->kind = SECRET_SOUP32;
        u->hash = alphasoup_hash(secret, (uint32_t)strlen(secret));
    }
}

static int secret_matches(const user_t *u, const char *secret) {
    if (u->kind == SECRET_PBKDF2) {
        uint8_t k[32];
        pbkdf2_hmac_sha256(secret, strlen(secret), u->salt, sizeof(u->salt), u->iters, k, sizeof(k));
        uint8_t diff = 0;                                  /* constant time */
        for (int i = 0; i < 32; i++) diff |= (uint8_t)(k[i] ^ u->key[i]);
        return diff == 0;
    }
    return u->hash == alphasoup_hash(secret, (uint32_t)strlen(secret));
}

static user_t  roster[USERS_MAX];
static int     n_users  = 0;
static uint8_t cur_uid  = 0;

#define KITCHEN_PATH  "/etc/kitchen"

/* ---- small string helpers ---- */


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
/* The roster holds every cook's secret, so in the ordinary build it is the
 * headchef's alone, rw---- (v0.54.0): before, it was rwxr-x and any cook
 * could read every PBKDF2 secret and attack it offline. The CHALLENGE build
 * keeps it readable on purpose - reading it is stage 2 ("Salt to Taste",
 * docs/challenge-solutions.md). The kernel reads and writes it directly,
 * so login, `secret` and `hire` do not need the cook to read it. */
static void seal_roster(void) {
#ifdef NO_CHALLENGE
    fat_chown(KITCHEN_PATH, 0);
    fat_chmod(KITCHEN_PATH, FAT_PERM_MARK | FAT_PERM_OR | FAT_PERM_OW);
#endif
}

/* Every system file the kernel writes or trusts, pinned to the headchef
 * with a known mode (v0.54.2), applied at boot and by the code that writes
 * each one. Before, each got whatever mode its writer's defaults gave: the
 * vault's private host key was rwxr-x, readable by every cook. /etc/rc is
 * NOT here on purpose: it runs as the headchef, so resetting its mode would
 * bless whatever a cook might have written into it; run_rc refuses an
 * unsafe one instead. Paths that do not exist are left alone. */
void users_sys_begin(void) { task_t *t = task_current(); if (t) t->sys_write++; }
void users_sys_end(void)   { task_t *t = task_current(); if (t && t->sys_write > 0) t->sys_write--; }
static int may_use_reserve(void) {
    task_t *t = task_current();
    return users_current_uid() == 0 || !t || t->sys_write > 0;
}

void users_seal_system(void) {
    static const struct { const char *path; uint8_t mode; } pins[] = {
        { "/etc",        FAT_PERM_OR | FAT_PERM_OW | FAT_PERM_OX | FAT_PERM_AR | FAT_PERM_AX },
        { "/home",       FAT_PERM_OR | FAT_PERM_OW | FAT_PERM_OX | FAT_PERM_AR | FAT_PERM_AX },
        { "/etc/logins", FAT_PERM_OR | FAT_PERM_OW | FAT_PERM_AR },
        { "/etc/motd",   FAT_PERM_OR | FAT_PERM_OW | FAT_PERM_AR },
        { "/AUTHKEYS",   FAT_PERM_OR | FAT_PERM_OW | FAT_PERM_AR },
        { "/HOSTKEY.ED", FAT_PERM_OR | FAT_PERM_OW },
    };
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++)
        if (fat_exists(pins[i].path)) {
            fat_chown(pins[i].path, 0);
            fat_chmod(pins[i].path, FAT_PERM_MARK | pins[i].mode);
        }
    seal_roster();
}

static int users_save(void) {
    static char buf[USERS_MAX * (USER_NAME_MAX + 128)];
    uint32_t len = 0;
    for (int i = 0; i < n_users; i++) {
        for (int k = 0; roster[i].name[k]; k++) buf[len++] = roster[i].name[k];
        buf[len++] = ':';
        len += (uint32_t)u_to_dec(roster[i].uid, buf + len);
        buf[len++] = ':';
        if (roster[i].kind == SECRET_PBKDF2) {
            static const char d[] = "0123456789abcdef";
            buf[len++] = '$'; buf[len++] = 'p'; buf[len++] = '$';
            len += (uint32_t)u_to_dec(roster[i].iters, buf + len);
            buf[len++] = '$';
            for (int k = 0; k < 16; k++) { buf[len++] = d[roster[i].salt[k] >> 4]; buf[len++] = d[roster[i].salt[k] & 15]; }
            buf[len++] = '$';
            for (int k = 0; k < 32; k++) { buf[len++] = d[roster[i].key[k] >> 4];  buf[len++] = d[roster[i].key[k] & 15]; }
        } else {
            char hx[9];
            u32_to_hex(roster[i].hash, hx);
            for (int k = 0; k < 8; k++) buf[len++] = hx[k];
        }
        buf[len++] = '\n';
    }
    users_sys_begin();
    fat_mkdir("/etc");                       /* harmless if it already exists */
    int rc = fat_write(KITCHEN_PATH, (const uint8_t *)buf, len);
    if (rc == 0) seal_roster();
    users_sys_end();
    return rc;
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
    if (n_users >= USERS_MAX) return;
    user_t *u = &roster[n_users];
    memset(u, 0, sizeof(*u));
    if (len - i > 3 && line[i] == '$' && line[i + 1] == 'p' && line[i + 2] == '$') {
        i += 3;
        uint32_t it = 0;
        while (i < len && line[i] >= '0' && line[i] <= '9') it = it * 10 + (uint32_t)(line[i++] - '0');
        if (i >= len || line[i] != '$' || it == 0) return;
        i++;
        for (int k = 0; k < 16; k++, i += 2) { if (i + 1 >= len) return; char t[3] = { line[i], line[i + 1], 0 }; u->salt[k] = (uint8_t)hex_to_u32(t); }
        if (i >= len || line[i] != '$') return;
        i++;
        for (int k = 0; k < 32; k++, i += 2) { if (i + 1 >= len) return; char t[3] = { line[i], line[i + 1], 0 }; u->key[k] = (uint8_t)hex_to_u32(t); }
        u->kind  = SECRET_PBKDF2;
        u->iters = it;
    } else {
        char hx[9];
        int  h = 0;
        while (i < len && h < 8) hx[h++] = line[i++];
        hx[h] = '\0';
        u->kind = SECRET_SOUP32;
        u->hash = hex_to_u32(hx);
    }
    copy_name(u->name, name);
    u->uid = (uint8_t)uid;
    n_users++;
}

/* ---- public ---- */

void users_init(void) {
    fat_set_reserve_hook(may_use_reserve);
    n_users = 0;
    cur_uid = 0;

    static uint8_t buf[USERS_MAX * (USER_NAME_MAX + 128) + 16];
    uint32_t size = 0;
    if (fat_read(KITCHEN_PATH, buf, sizeof(buf) - 1, &size) == 0 && size > 0) {
        if (size > sizeof(buf) - 1) size = sizeof(buf) - 1;   /* fat_read fills at most bufsize but reports the FILE's size */
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
        set_secret(&roster[0], "rosemary");
        /* The account players are given. Ordinary cook, no privileges. */
        copy_name(roster[1].name, "cook");
        roster[1].uid  = 1;
        set_secret(&roster[1], "soup");
        n_users = 2;
        users_save();
    }
    users_seal_system();         /* an older disk is closed at boot */
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
            uint32_t t0 = timer_get_ticks();
            if (!secret_matches(&roster[i], secret)) return -1;
            if (roster[i].kind == SECRET_PBKDF2)
                klog("[users] %s: pbkdf2 check took %u ticks\n", name, timer_get_ticks() - t0);
            /* A SOUP32 entry in the ordinary build is upgraded on the first
             * login that proves the secret: the only moment it is known. */
            if (MAKE_PBKDF2 && roster[i].kind == SECRET_SOUP32) {
                set_secret(&roster[i], secret);
                users_save();
                klog("[users] %s: secret upgraded to pbkdf2\n", name);
            }
            return (int)roster[i].uid;
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
    set_secret(&roster[n_users], secret ? secret : "");
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

static int (*resolver)(void);
void        users_set_resolver(int (*fn)(void)) { resolver = fn; }
static uint8_t eff_uid(void) {
    int r = resolver ? resolver() : -1;
    return r >= 0 ? (uint8_t)r : cur_uid;
}
void        users_set_current(uint8_t uid) { cur_uid = uid; }
uint8_t     users_current_uid(void)        { return eff_uid(); }
const char *users_current_name(void)       { return users_name_of(eff_uid()); }
int         users_is_headchef(void)        { return eff_uid() == 0; }

/* One entry's own mode against what the current cook needs. */
static int mode_allows(const char *path, char need) {
    uint8_t owner, mode;
    if (fat_stat(path, &owner, &mode) < 0) return 1;     /* no such path */
    uint8_t bits;
    if (users_current_uid() == owner)
        bits = (need == 'r') ? FAT_PERM_OR :
               (need == 'w') ? FAT_PERM_OW : FAT_PERM_OX;
    else
        bits = (need == 'r') ? FAT_PERM_AR :
               (need == 'w') ? FAT_PERM_AW : FAT_PERM_AX;
    return (mode & bits) ? 1 : 0;
}

/* `need` on the path itself, AND search (x) on every bowl above it
 * (v0.53.1). Without the second half a closed bowl closed only `cd`: a cook
 * refused at /home/saucier still read /home/saucier/RECIPE.TXT by its full
 * path, from the shell and through SYS_OPEN alike. Paths here are absolute
 * (resolved by the shell, or copied in by a syscall). */
/* May ANYONE read this path - every cook, whoever they are? The read bit
 * for all cooks on the path itself, and search for all cooks on every bowl
 * above it, whatever its owner (v0.55.0). For hatch: its HTTP clients are
 * anonymous, so it serves only what is public, and a headchef's hatch is no
 * different from a cook's. */
int users_public(const char *path) {
    char up[FAT_PATH_MAX];
    size_t n = strlen(path);
    if (n >= sizeof(up)) return 0;
    uint8_t owner, mode;
    for (size_t i = 1; i < n; i++) {
        if (path[i] != '/') continue;
        memcpy(up, path, i); up[i] = '\0';
        if (fat_stat(up, &owner, &mode) == 0 && !(mode & FAT_PERM_AX)) return 0;
    }
    return fat_stat(path, &owner, &mode) == 0 && (mode & FAT_PERM_AR);
}

/* May the cook reach this path - search (x) on every bowl above it - without
 * needing anything on the path itself? What stat asks (v0.59.0). */
int users_reach(const char *path) {
    if (users_is_headchef()) return 1;
    char up[FAT_PATH_MAX];
    size_t n = strlen(path);
    if (n >= sizeof(up)) return 0;
    for (size_t i = 1; i < n; i++) {
        if (path[i] != '/') continue;
        memcpy(up, path, i); up[i] = '\0';
        if (!mode_allows(up, 'x')) return 0;
    }
    return 1;
}

int users_may(const char *path, char need) {
    if (users_is_headchef()) return 1;
    char up[FAT_PATH_MAX];
    size_t n = strlen(path);
    if (n >= sizeof(up)) return 0;
    for (size_t i = 1; i < n; i++) {
        if (path[i] != '/') continue;
        memcpy(up, path, i); up[i] = '\0';               /* "/home", "/home/saucier", ... */
        if (!mode_allows(up, 'x')) return 0;
    }
    return mode_allows(path, need);
}

int users_set_secret(const char *name, const char *secret) {
    for (int i = 0; i < n_users; i++)
        if (strcmp(roster[i].name, name) == 0) {
            set_secret(&roster[i], secret ? secret : "");
            return users_save();
        }
    return -1;
}
