/* ssh.c - the SSH transport over soupOS TCP. See ssh.h.
 *
 * Layout, top to bottom: byte-stream reading on a TCP handle; a little
 * builder for SSH's wire types (uint32, string, mpint, name-list); the packet
 * layer, plain and then chacha20-poly1305@openssh.com; the key exchange; and
 * the session loop a task runs per client.
 *
 * The OpenSSH AEAD is not the RFC 8439 construction. It uses two keys, K_2
 * (first half) for the payload and K_1 (second half) for the four length
 * bytes alone, so a receiver can learn the length before authenticating; the
 * nonce is the 64-bit packet sequence number; the Poly1305 key is block zero
 * of K_2's keystream and the payload starts at block one; and the tag covers
 * the encrypted length and the encrypted payload. That is why chacha.h exposes
 * the pieces rather than only the RFC's combined call.
 */
#include "ssh.h"
#include "tcp.h"
#include "net.h"
#include "task.h"
#include "timer.h"
#include "fat.h"
#include "random.h"
#include "sha256.h"
#include "chacha.h"
#include "curve25519.h"
#include "ed25519.h"
#include "users.h"
#include "console.h"
#include "str.h"
#include "klog.h"

#define MSG_DISCONNECT        1
#define MSG_IGNORE            2
#define MSG_UNIMPLEMENTED     3
#define MSG_DEBUG             4
#define MSG_SERVICE_REQUEST   5
#define MSG_SERVICE_ACCEPT    6
#define MSG_KEXINIT          20
#define MSG_NEWKEYS          21
#define MSG_KEX_ECDH_INIT    30
#define MSG_KEX_ECDH_REPLY   31
#define MSG_USERAUTH_REQUEST 50
#define MSG_USERAUTH_FAILURE 51
#define MSG_USERAUTH_SUCCESS 52
#define MSG_GLOBAL_REQUEST   80
#define MSG_REQUEST_FAILURE  82
#define MSG_CHANNEL_OPEN     90
#define MSG_CHANNEL_OPEN_OK  91
#define MSG_CHANNEL_OPEN_FAIL 92
#define MSG_CHANNEL_WINDOW   93
#define MSG_CHANNEL_DATA     94
#define MSG_CHANNEL_EOF      96
#define MSG_CHANNEL_CLOSE    97
#define MSG_CHANNEL_REQUEST  98
#define MSG_CHANNEL_SUCCESS  99
#define MSG_CHANNEL_FAILURE 100

static const char VERSION[] = "SSH-2.0-soupOS_0.29";
static const char HOSTKEY_PATH[] = "/HOSTKEY.ED";

static volatile int running, want_stop;
static uint16_t     port_in_use;
static uint8_t      host_seed[32], host_pk[32];
static char         fingerprint[64];

/* ── the one session ────────────────────────────────────────────────────── */
typedef struct {
    int      h;
    uint32_t seq_in, seq_out;
    int      crypt;                 /* after NEWKEYS both ways */
    uint32_t hdr_have;              /* length bytes read so far (poll-safe) */
    int      chan_open;
    uint32_t chan_peer;             /* the client's channel number */
    uint32_t win_out;               /* bytes the client will still take */
    uint32_t win_in_used;           /* bytes we have taken since the last adjust */
    uint8_t  key_c2s[64], key_s2c[64];
    uint8_t  session_id[32];
    uint8_t  client_version[256]; uint32_t client_version_len;
    uint8_t  client_kexinit[SSH_MAX_PACKET]; uint32_t client_kexinit_len;
    uint8_t  server_kexinit[512]; uint32_t server_kexinit_len;
    uint8_t  in[SSH_MAX_PACKET + 64];
    uint8_t  out[SSH_MAX_PACKET + 64];
    uint8_t  payload[SSH_MAX_PACKET];
} ssh_t;

static ssh_t S;

/* ── byte stream ────────────────────────────────────────────────────────── */
static int read_exact(uint8_t *dst, uint32_t n, uint32_t deadline_ticks) {
    uint32_t got = 0, start = timer_get_ticks();
    while (got < n) {
        if (want_stop) return -1;
        int r = tcp_recv_on(S.h, dst + got, (uint16_t)(n - got), 20);
        if (r < 0) return -1;
        if (r == 0) {
            if (tcp_peer_done_on(S.h)) return -1;
            if (timer_get_ticks() - start > deadline_ticks) return -1;
            continue;
        }
        got += (uint32_t)r;
    }
    return 0;
}

static int send_all(const uint8_t *src, uint32_t n) {
    while (n) {
        uint16_t chunk = n > 1024 ? 1024 : (uint16_t)n;
        if (tcp_send_on(S.h, src, chunk) < 0) return -1;
        src += chunk; n -= chunk;
    }
    return 0;
}

/* ── wire types ─────────────────────────────────────────────────────────── */
typedef struct { uint8_t *p; uint32_t len, cap; } wbuf_t;

static void w_init(wbuf_t *b, uint8_t *mem, uint32_t cap) { b->p = mem; b->len = 0; b->cap = cap; }
static void w_u8 (wbuf_t *b, uint8_t v)  { if (b->len < b->cap) b->p[b->len++] = v; }
static void w_u32(wbuf_t *b, uint32_t v) { w_u8(b, (uint8_t)(v >> 24)); w_u8(b, (uint8_t)(v >> 16)); w_u8(b, (uint8_t)(v >> 8)); w_u8(b, (uint8_t)v); }
static void w_raw(wbuf_t *b, const void *d, uint32_t n) { for (uint32_t i = 0; i < n; i++) w_u8(b, ((const uint8_t *)d)[i]); }
static void w_str(wbuf_t *b, const void *d, uint32_t n) { w_u32(b, n); w_raw(b, d, n); }
static void w_cstr(wbuf_t *b, const char *s) { w_str(b, s, (uint32_t)strlen(s)); }
/* mpint: big-endian, no leading zeros, a zero byte prepended if the top bit
 * is set so it reads as positive. */
static void w_mpint(wbuf_t *b, const uint8_t *d, uint32_t n) {
    uint32_t i = 0;
    while (i < n && d[i] == 0) i++;
    uint32_t len = n - i, pad = (len && (d[i] & 0x80)) ? 1 : 0;
    w_u32(b, len + pad);
    if (pad) w_u8(b, 0);
    w_raw(b, d + i, len);
}

typedef struct { const uint8_t *p; uint32_t len, pos; int bad; } rbuf_t;
static void     r_init(rbuf_t *r, const uint8_t *p, uint32_t len) { r->p = p; r->len = len; r->pos = 0; r->bad = 0; }
static uint8_t  r_u8 (rbuf_t *r) { if (r->pos + 1 > r->len) { r->bad = 1; return 0; } return r->p[r->pos++]; }
static uint32_t r_u32(rbuf_t *r) { uint32_t v = 0; for (int i = 0; i < 4; i++) v = (v << 8) | r_u8(r); return v; }
static const uint8_t *r_str(rbuf_t *r, uint32_t *n) {
    *n = r_u32(r);
    if (r->bad || r->pos + *n > r->len) { r->bad = 1; *n = 0; return r->p; }
    const uint8_t *s = r->p + r->pos;
    r->pos += *n;
    return s;
}

static int namelist_has(const uint8_t *list, uint32_t len, const char *name) {
    uint32_t nl = (uint32_t)strlen(name), i = 0;
    while (i <= len) {
        uint32_t j = i;
        while (j < len && list[j] != ',') j++;
        if (j - i == nl && memcmp(list + i, name, nl) == 0) return 1;
        if (j >= len) break;
        i = j + 1;
    }
    return 0;
}

/* ── packets ────────────────────────────────────────────────────────────── */

/* OpenSSH's chacha20-poly1305: see the file comment. `seq` is the nonce. */
static void aead_nonce(uint8_t nonce[12], uint32_t seq) {
    memset(nonce, 0, 12);
    nonce[8]  = (uint8_t)(seq >> 24); nonce[9]  = (uint8_t)(seq >> 16);
    nonce[10] = (uint8_t)(seq >> 8);  nonce[11] = (uint8_t)seq;
}
static void aead_tag(const uint8_t key[64], uint32_t seq, const uint8_t *enc, uint32_t len, uint8_t tag[16]) {
    uint8_t nonce[12], otk[32];
    aead_nonce(nonce, seq);
    memset(otk, 0, sizeof(otk));
    chacha20_xor(key, nonce, 0, otk, sizeof(otk));      /* K_2, block 0 */
    poly1305_t p;
    poly1305_init(&p, otk);
    poly1305_update(&p, enc, len);
    poly1305_final(&p, tag);
}

static int write_packet(const uint8_t *payload, uint32_t len) {
    /* Padding: at least four bytes, and the padded body a multiple of eight.
     * Plain packets count the length field in that; the AEAD does not. */
    uint32_t base = (S.crypt ? 1 : 5) + len;
    uint32_t pad  = 8 - (base % 8);
    if (pad < 4) pad += 8;
    uint32_t plen = 1 + len + pad;
    if (4 + plen + 16 > sizeof(S.out)) return -1;

    uint8_t *o = S.out;
    o[0] = (uint8_t)(plen >> 24); o[1] = (uint8_t)(plen >> 16); o[2] = (uint8_t)(plen >> 8); o[3] = (uint8_t)plen;
    o[4] = (uint8_t)pad;
    memcpy(o + 5, payload, len);
    random_bytes(o + 5 + len, pad);
    uint32_t total = 4 + plen;

    if (S.crypt) {
        uint8_t nonce[12];
        aead_nonce(nonce, S.seq_out);
        chacha20_xor(S.key_s2c + 32, nonce, 0, o, 4);           /* K_1: length */
        chacha20_xor(S.key_s2c,      nonce, 1, o + 4, plen);    /* K_2: body   */
        aead_tag(S.key_s2c, S.seq_out, o, total, o + total);
        total += 16;
    }
    S.seq_out++;
    return send_all(o, total);
}

/* Returns the payload length into S.payload, or -1. */
/* Returns the payload length into S.payload; -1 on error; -2 if no packet
 * started within `wait` ticks (nothing consumed that is not kept for the
 * next call, so the session loop can poll between output flushes). */
static int read_packet_wait(uint32_t wait) {
    uint8_t *i = S.in;
    uint32_t start = timer_get_ticks();
    while (S.hdr_have < 4) {
        if (want_stop) return -1;
        int r = tcp_recv_on(S.h, i + S.hdr_have, (uint16_t)(4 - S.hdr_have), 2);
        if (r < 0) return -1;
        if (r == 0) {
            if (tcp_peer_done_on(S.h)) return -1;
            if (timer_get_ticks() - start >= wait) return -2;
            continue;
        }
        S.hdr_have += (uint32_t)r;
    }
    S.hdr_have = 0;
    uint8_t lenbytes[4];
    memcpy(lenbytes, i, 4);
    if (S.crypt) {
        uint8_t nonce[12];
        aead_nonce(nonce, S.seq_in);
        chacha20_xor(S.key_c2s + 32, nonce, 0, lenbytes, 4);
    }
    uint32_t plen = ((uint32_t)lenbytes[0] << 24) | ((uint32_t)lenbytes[1] << 16) |
                    ((uint32_t)lenbytes[2] << 8) | lenbytes[3];
    if (plen < 5 || plen > SSH_MAX_PACKET) { klog("[ssh] bad packet length %u\n", plen); return -1; }
    if (read_exact(i + 4, plen + (S.crypt ? 16 : 0), 3000) < 0) return -1;

    if (S.crypt) {
        uint8_t want[16];
        aead_tag(S.key_c2s, S.seq_in, i, 4 + plen, want);
        uint8_t diff = 0;
        for (int k = 0; k < 16; k++) diff |= want[k] ^ i[4 + plen + k];
        if (diff) { klog("[ssh] bad MAC on packet %u\n", S.seq_in); return -1; }
        uint8_t nonce[12];
        aead_nonce(nonce, S.seq_in);
        chacha20_xor(S.key_c2s, nonce, 1, i + 4, plen);
    }
    S.seq_in++;
    uint8_t pad = i[4];
    if (pad + 1u > plen) return -1;
    uint32_t len = plen - 1 - pad;
    memcpy(S.payload, i + 5, len);
    return (int)len;
}

static int read_packet(void) {
    int n;
    do { n = read_packet_wait(3000); } while (n == -2 && !want_stop && timer_get_ticks());
    return n;
}

static int send_disconnect(uint32_t reason, const char *why) {
    uint8_t mem[256]; wbuf_t b; w_init(&b, mem, sizeof(mem));
    w_u8(&b, MSG_DISCONNECT); w_u32(&b, reason); w_cstr(&b, why); w_cstr(&b, "");
    return write_packet(b.p, b.len);
}

/* ── key exchange ───────────────────────────────────────────────────────── */
static void build_kexinit(void) {
    wbuf_t b; w_init(&b, S.server_kexinit, sizeof(S.server_kexinit));
    uint8_t cookie[16];
    random_bytes(cookie, 16);
    w_u8(&b, MSG_KEXINIT);
    w_raw(&b, cookie, 16);
    w_cstr(&b, "curve25519-sha256,curve25519-sha256@libssh.org");
    w_cstr(&b, "ssh-ed25519");
    w_cstr(&b, "chacha20-poly1305@openssh.com");
    w_cstr(&b, "chacha20-poly1305@openssh.com");
    w_cstr(&b, "hmac-sha2-256");     /* ignored for an AEAD, but must be a list */
    w_cstr(&b, "hmac-sha2-256");
    w_cstr(&b, "none");
    w_cstr(&b, "none");
    w_cstr(&b, "");
    w_cstr(&b, "");
    w_u8(&b, 0);                     /* first_kex_packet_follows */
    w_u32(&b, 0);
    S.server_kexinit_len = b.len;
}

/* The client's KEXINIT: just confirm it can speak what we offer. */
static int check_client_kexinit(const uint8_t *p, uint32_t len) {
    rbuf_t r; r_init(&r, p, len);
    if (r_u8(&r) != MSG_KEXINIT) return -1;
    r.pos += 16;
    uint32_t n; const uint8_t *l;
    l = r_str(&r, &n); if (!namelist_has(l, n, "curve25519-sha256") && !namelist_has(l, n, "curve25519-sha256@libssh.org")) return -1;
    l = r_str(&r, &n); if (!namelist_has(l, n, "ssh-ed25519")) return -1;
    l = r_str(&r, &n); if (!namelist_has(l, n, "chacha20-poly1305@openssh.com")) return -1;
    l = r_str(&r, &n); if (!namelist_has(l, n, "chacha20-poly1305@openssh.com")) return -1;
    return r.bad ? -1 : 0;
}

static void hostkey_blob(wbuf_t *b) { w_cstr(b, "ssh-ed25519"); w_str(b, host_pk, 32); }

/* key = HASH(K || H || letter || session_id), extended once to 64 bytes. */
static void derive_key(uint8_t out[64], const uint8_t *k_mpint, uint32_t k_len,
                       const uint8_t H[32], char letter) {
    sha256_t s;
    sha256_init(&s);
    sha256_update(&s, k_mpint, k_len);
    sha256_update(&s, H, 32);
    sha256_update(&s, &letter, 1);
    sha256_update(&s, S.session_id, 32);
    sha256_final(&s, out);
    sha256_init(&s);
    sha256_update(&s, k_mpint, k_len);
    sha256_update(&s, H, 32);
    sha256_update(&s, out, 32);
    sha256_final(&s, out + 32);
}

static int do_kex(const uint8_t *init, uint32_t init_len) {
    rbuf_t r; r_init(&r, init, init_len);
    if (r_u8(&r) != MSG_KEX_ECDH_INIT) return -1;
    uint32_t qlen; const uint8_t *q_c = r_str(&r, &qlen);
    if (r.bad || qlen != 32) { klog("[ssh] ECDH_INIT with a %u-byte point\n", qlen); return -1; }

    uint8_t eph[32], q_s[32], k[32];
    random_bytes(eph, 32);
    x25519_base(q_s, eph);
    x25519(k, eph, q_c);

    /* H = HASH(V_C || V_S || I_C || I_S || K_S || Q_C || Q_S || K). */
    static uint8_t mem[2 * SSH_MAX_PACKET + 1024];
    wbuf_t b; w_init(&b, mem, sizeof(mem));
    w_str(&b, S.client_version, S.client_version_len);
    w_cstr(&b, VERSION);
    w_str(&b, S.client_kexinit, S.client_kexinit_len);
    w_str(&b, S.server_kexinit, S.server_kexinit_len);
    uint8_t ksmem[80]; wbuf_t ks; w_init(&ks, ksmem, sizeof(ksmem)); hostkey_blob(&ks);
    w_str(&b, ks.p, ks.len);
    w_str(&b, q_c, 32);
    w_str(&b, q_s, 32);
    uint8_t kmem[40]; wbuf_t kb; w_init(&kb, kmem, sizeof(kmem)); w_mpint(&kb, k, 32);
    w_raw(&b, kb.p, kb.len);
    uint8_t H[32];
    sha256(b.p, b.len, H);
    memcpy(S.session_id, H, 32);           /* first exchange: this IS the id */

    uint8_t sig[64];
    ed25519_sign(sig, H, 32, host_seed, host_pk);

    w_init(&b, mem, sizeof(mem));
    w_u8(&b, MSG_KEX_ECDH_REPLY);
    w_str(&b, ks.p, ks.len);
    w_str(&b, q_s, 32);
    uint8_t sgmem[96]; wbuf_t sg; w_init(&sg, sgmem, sizeof(sgmem));
    w_cstr(&sg, "ssh-ed25519"); w_str(&sg, sig, 64);
    w_str(&b, sg.p, sg.len);
    if (write_packet(b.p, b.len) < 0) return -1;

    uint8_t nk = MSG_NEWKEYS;
    if (write_packet(&nk, 1) < 0) return -1;

    int n = read_packet();
    if (n < 1 || S.payload[0] != MSG_NEWKEYS) { klog("[ssh] expected NEWKEYS\n"); return -1; }

    derive_key(S.key_c2s, kb.p, kb.len, H, 'C');
    derive_key(S.key_s2c, kb.p, kb.len, H, 'D');
    S.crypt = 1;
    klog("[ssh] keys in: curve25519-sha256 / ssh-ed25519 / chacha20-poly1305\n");
    return 0;
}

/* ── the session ────────────────────────────────────────────────────────── */
static int read_client_version(void) {
    /* Lines before the SSH- one are allowed and ignored. */
    for (int lines = 0; lines < 8; lines++) {
        uint32_t n = 0;
        while (n < sizeof(S.client_version)) {
            if (read_exact(S.client_version + n, 1, 1000) < 0) return -1;
            if (S.client_version[n] == '\n') break;
            n++;
        }
        while (n && (S.client_version[n - 1] == '\r' || S.client_version[n - 1] == '\n')) n--;
        if (n >= 4 && memcmp(S.client_version, "SSH-", 4) == 0) {
            S.client_version_len = n;
            return 0;
        }
    }
    return -1;
}

/* ── output ring: console sink (any task) -> SSH task ────────────────────── */
static uint8_t  oring[4096];
static volatile uint32_t ohead, otail;

static void oring_push(char c) {
    uint32_t next = (ohead + 1) % sizeof(oring);
    if (next == otail) return;              /* full: drop, the terminal survives */
    oring[ohead] = (uint8_t)c;
    ohead = next;
}

static void ssh_sink(char c) {
    if (!S.chan_open) return;
    preempt_disable();
    if (c == '\n')      { oring_push('\r'); oring_push('\n'); }
    else if (c == '\b') { oring_push('\b'); oring_push(' '); oring_push('\b'); }
    else                  oring_push(c);
    preempt_enable();
}

static int flush_output(void) {
    while (ohead != otail && S.win_out > 0) {
        uint8_t chunk[1024];
        uint32_t n = 0;
        preempt_disable();
        while (ohead != otail && n < sizeof(chunk) && n < S.win_out) {
            chunk[n++] = oring[otail];
            otail = (otail + 1) % sizeof(oring);
        }
        preempt_enable();
        uint8_t mem[1024 + 16]; wbuf_t b; w_init(&b, mem, sizeof(mem));
        w_u8(&b, MSG_CHANNEL_DATA); w_u32(&b, S.chan_peer); w_str(&b, chunk, n);
        if (write_packet(b.p, b.len) < 0) return -1;
        S.win_out -= n;
    }
    return 0;
}

/* ── userauth: password against the roster ──────────────────────────────── */
static int userauth(void) {
    for (int attempts = 0; attempts < 6; attempts++) {
        int n = read_packet();
        if (n < 1) return -1;
        if (S.payload[0] == MSG_IGNORE || S.payload[0] == MSG_DEBUG) { attempts--; continue; }
        if (S.payload[0] != MSG_USERAUTH_REQUEST) return -1;
        rbuf_t r; r_init(&r, S.payload + 1, (uint32_t)n - 1);
        uint32_t ul, sl, ml, pl = 0;
        const uint8_t *user = r_str(&r, &ul); r_str(&r, &sl); const uint8_t *method = r_str(&r, &ml);
        if (r.bad) return -1;
        char u[32], m[32], pw[64];
        uint32_t un = ul < 31 ? ul : 31, mn = ml < 31 ? ml : 31;
        memcpy(u, user, un); u[un] = '\0';
        memcpy(m, method, mn); m[mn] = '\0';

        int ok = 0;
        if ((strcmp(m, "password") == 0)) {
            r_u8(&r);                                /* FALSE: not a change request */
            const uint8_t *pass = r_str(&r, &pl);
            if (!r.bad && pl < sizeof(pw)) {
                memcpy(pw, pass, pl); pw[pl] = '\0';
                ok = users_check(u, pw) >= 0;
                memset(pw, 0, sizeof(pw));
            }
        }
        if (ok) {
            klog("[ssh] %s authenticated by password\n", u);
            uint8_t ok_msg = MSG_USERAUTH_SUCCESS;
            return write_packet(&ok_msg, 1);
        }
        klog("[ssh] userauth %s by %s: refused\n", u, m);
        uint8_t mem[64]; wbuf_t b; w_init(&b, mem, sizeof(mem));
        w_u8(&b, MSG_USERAUTH_FAILURE); w_cstr(&b, "password"); w_u8(&b, 0);
        if (write_packet(b.p, b.len) < 0) return -1;
    }
    return -1;
}

/* ── the connection layer: one session channel on the console ───────────── */
static int reply_status(uint8_t msg) {
    uint8_t mem[8]; wbuf_t b; w_init(&b, mem, sizeof(mem));
    w_u8(&b, msg); w_u32(&b, S.chan_peer);
    return write_packet(b.p, b.len);
}

static int handle_channel_request(rbuf_t *r) {
    uint32_t tl; const uint8_t *type = r_str(r, &tl);
    int want_reply = r_u8(r) != 0;
    if (r->bad) return -1;
    char t[32]; uint32_t tn = tl < 31 ? tl : 31; memcpy(t, type, tn); t[tn] = '\0';

    int ok = (strcmp(t, "pty-req") == 0) || (strcmp(t, "shell") == 0) || (strcmp(t, "env") == 0) || (strcmp(t, "window-change") == 0);
    if ((strcmp(t, "shell") == 0)) {
        console_set_sink(ssh_sink);
        klog("[ssh] shell attached to the console\n");
        static const char hello[] = "\r\nsoupOS over SSH. You share the local keyboard.\r\n";
        for (uint32_t i = 0; i < sizeof(hello) - 1; i++) oring_push(hello[i]);
        console_rx_byte('\r');               /* a fresh prompt for the new arrival */
    }
    if (want_reply) return reply_status(ok ? MSG_CHANNEL_SUCCESS : MSG_CHANNEL_FAILURE);
    return 0;
}

static void connection_loop(void) {
    for (;;) {
        if (flush_output() < 0) return;
        int n = read_packet_wait(2);
        if (n == -2) continue;
        if (n < 1) return;
        rbuf_t r; r_init(&r, S.payload + 1, (uint32_t)n - 1);
        switch (S.payload[0]) {
        case MSG_IGNORE: case MSG_DEBUG: break;
        case MSG_DISCONNECT: return;
        case MSG_GLOBAL_REQUEST: {
            uint32_t nl; r_str(&r, &nl);
            if (r_u8(&r)) { uint8_t f = MSG_REQUEST_FAILURE; if (write_packet(&f, 1) < 0) return; }
            break;
        }
        case MSG_CHANNEL_OPEN: {
            uint32_t tl; const uint8_t *type = r_str(&r, &tl);
            uint32_t peer = r_u32(&r), win = r_u32(&r); r_u32(&r);
            if (r.bad) return;
            uint8_t mem[64]; wbuf_t b; w_init(&b, mem, sizeof(mem));
            if (S.chan_open || tl != 7 || memcmp(type, "session", 7) != 0) {
                w_u8(&b, MSG_CHANNEL_OPEN_FAIL); w_u32(&b, peer); w_u32(&b, 1);
                w_cstr(&b, "one session, and only a session"); w_cstr(&b, "");
            } else {
                S.chan_open = 1; S.chan_peer = peer; S.win_out = win; S.win_in_used = 0;
                w_u8(&b, MSG_CHANNEL_OPEN_OK); w_u32(&b, peer); w_u32(&b, 0);
                w_u32(&b, 65536); w_u32(&b, 2048);
            }
            if (write_packet(b.p, b.len) < 0) return;
            break;
        }
        case MSG_CHANNEL_WINDOW: r_u32(&r); S.win_out += r_u32(&r); break;
        case MSG_CHANNEL_DATA: {
            r_u32(&r);
            uint32_t dl; const uint8_t *d = r_str(&r, &dl);
            if (r.bad) return;
            for (uint32_t i = 0; i < dl; i++) console_rx_byte(d[i]);
            S.win_in_used += dl;
            if (S.win_in_used > 32768) {
                uint8_t mem[16]; wbuf_t b; w_init(&b, mem, sizeof(mem));
                w_u8(&b, MSG_CHANNEL_WINDOW); w_u32(&b, S.chan_peer); w_u32(&b, S.win_in_used);
                if (write_packet(b.p, b.len) < 0) return;
                S.win_in_used = 0;
            }
            break;
        }
        case MSG_CHANNEL_REQUEST: r_u32(&r); if (handle_channel_request(&r) < 0) return; break;
        case MSG_CHANNEL_EOF: break;
        case MSG_CHANNEL_CLOSE:
            flush_output();
            reply_status(MSG_CHANNEL_CLOSE);
            return;
        default: {
            uint8_t mem[8]; wbuf_t b; w_init(&b, mem, sizeof(mem));
            w_u8(&b, MSG_UNIMPLEMENTED); w_u32(&b, S.seq_in - 1);
            if (write_packet(b.p, b.len) < 0) return;
        }
        }
    }
}

static void session(void) {
    console_set_sink(0);
    memset(&S, 0, sizeof(S));
    S.h = -1;
    ohead = otail = 0;
}

static void run_session(int h) {
    session();
    S.h = h;

    /* Our line first; the client may already have sent its own. */
    uint8_t line[64];
    uint32_t vl = (uint32_t)strlen(VERSION);
    memcpy(line, VERSION, vl); line[vl] = '\r'; line[vl + 1] = '\n';
    if (send_all(line, vl + 2) < 0) return;
    if (read_client_version() < 0) { klog("[ssh] no version line\n"); return; }
    {
        char v[64];
        uint32_t n = S.client_version_len < sizeof(v) - 1 ? S.client_version_len : sizeof(v) - 1;
        memcpy(v, S.client_version, n); v[n] = '\0';
        klog("[ssh] client: %s\n", v);
    }

    build_kexinit();
    if (write_packet(S.server_kexinit, S.server_kexinit_len) < 0) return;

    int n = read_packet();
    if (n < 0) return;
    if (check_client_kexinit(S.payload, (uint32_t)n) < 0) {
        klog("[ssh] client cannot speak our one of everything\n");
        send_disconnect(3, "no common algorithm");
        return;
    }
    memcpy(S.client_kexinit, S.payload, (uint32_t)n);
    S.client_kexinit_len = (uint32_t)n;

    n = read_packet();
    if (n < 0) return;
    if (do_kex(S.payload, (uint32_t)n) < 0) { klog("[ssh] key exchange failed\n"); return; }

    n = read_packet();
    if (n < 1) return;
    if (S.payload[0] == MSG_SERVICE_REQUEST) {
        rbuf_t r; r_init(&r, S.payload + 1, (uint32_t)n - 1);
        uint32_t sl; const uint8_t *svc = r_str(&r, &sl);
        if (r.bad || sl != 12 || memcmp(svc, "ssh-userauth", 12) != 0) { send_disconnect(7, "unknown service"); return; }
        uint8_t mem[32]; wbuf_t b; w_init(&b, mem, sizeof(mem));
        w_u8(&b, MSG_SERVICE_ACCEPT); w_cstr(&b, "ssh-userauth");
        if (write_packet(b.p, b.len) < 0) return;
    } else {
        send_disconnect(2, "expected service request");
        return;
    }

    if (userauth() < 0) return;
    ohead = otail = 0;
    connection_loop();
    console_set_sink(0);
    S.chan_open = 0;
    klog("[ssh] session over\n");
}

static void ssh_task(void *arg) {
    (void)arg;
    task_set_service();
    running = 1;
    while (!want_stop) {
        if (tcp_listen(port_in_use) < 0) break;
        int h = tcp_accept(port_in_use, 100);
        if (h < 0) continue;
        klog("[ssh] connection on port %u\n", port_in_use);
        run_session(h);
        tcp_close_on(h);
        klog("[ssh] connection closed\n");
    }
    running = 0;
    tcp_stop_listening(port_in_use);
    klog("[ssh] stopped\n");
    task_exit();
}

/* ── host key ───────────────────────────────────────────────────────────── */
static void b64(const uint8_t *in, uint32_t n, char *out) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t i = 0, o = 0;
    while (i < n) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        if (i + 1 < n) out[o++] = T[(v >> 6) & 63];
        if (i + 2 < n) out[o++] = T[v & 63];
        i += 3;
    }
    out[o] = '\0';                  /* OpenSSH prints fingerprints unpadded */
}

static int load_or_make_hostkey(void) {
    uint32_t got = 0;
    if (fat_read(HOSTKEY_PATH, host_seed, 32, &got) < 0 || got != 32) {
        random_bytes(host_seed, 32);
        if (fat_write(HOSTKEY_PATH, host_seed, 32) < 0) return -1;
        klog("[ssh] new host key written to %s\n", HOSTKEY_PATH);
    }
    ed25519_public_key(host_pk, host_seed);

    uint8_t mem[80]; wbuf_t b; w_init(&b, mem, sizeof(mem)); hostkey_blob(&b);
    uint8_t d[32];
    sha256(b.p, b.len, d);
    memcpy(fingerprint, "SHA256:", 7);
    b64(d, 32, fingerprint + 7);
    klog("[ssh] host key %s\n", fingerprint);
    return 0;
}

int ssh_start(uint16_t port) {
    if (running) return -1;
    if (!net_ip()) return -2;
    if (load_or_make_hostkey() < 0) return -4;
    want_stop   = 0;
    port_in_use = port;
    if (!task_spawn("ssh", ssh_task, 0)) return -3;
    return 0;
}

void        ssh_stop(void)        { want_stop = 1; }
int         ssh_running(void)     { return running; }
uint16_t    ssh_port(void)        { return port_in_use; }
const char *ssh_fingerprint(void) { return fingerprint; }
