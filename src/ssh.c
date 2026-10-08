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
#include "logins.h"
#include "heap.h"
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
#include "term.h"
#include "shell.h"
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
#define MSG_USERAUTH_PK_OK   60
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

static const char VERSION[] = "SSH-2.0-soupOS_0.34";
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
    int      close_sent;
    uint8_t  uid;                   /* who authenticated */
    uint32_t peer_ip;               /* for the failed-login count (v0.50.0) */
    uint32_t pw_fails;              /* failed passwords on this connection */
    uint32_t last_data;             /* ticks of the client's last keystrokes (v0.53.3) */
    int      pty_cols, pty_rows;    /* from pty-req, for the terminal */
    term_stream_t term;             /* the channel, as a terminal (v0.36.0) */
    void    *shell;                 /* the session shell on it, once started */
    /* v0.42.0: what used to be file- or function-static, so connections
     * cannot trample each other. */
    task_t  *task;                  /* the worker task serving this slot */
    int      used;
    uint8_t  oring[16384];          /* session output -> this worker */
    volatile uint32_t ohead, otail;
    uint8_t  kexbuf[2 * SSH_MAX_PACKET + 1024];
    uint8_t  authkeys[4096];
    uint8_t  signed_mem[512];
    uint32_t chan_peer;             /* the client's channel number */
    uint32_t win_out;               /* bytes the client will still take */
    uint32_t win_in_used;           /* bytes we have taken since the last adjust */
    uint8_t  key_c2s[64], key_s2c[64];
    uint8_t  session_id[32];
    int      have_session_id;       /* set by the first exchange, kept by every rekey */
    int      kexinit_sent;          /* we have sent KEXINIT and await theirs */
    uint32_t kex_seq_out;           /* seq_out at the last exchange */
    uint32_t kex_tick;              /* when the last exchange finished */
    uint32_t rekeys;
    uint8_t  client_version[256]; uint32_t client_version_len;
    uint8_t  client_kexinit[SSH_MAX_PACKET]; uint32_t client_kexinit_len;
    uint8_t  server_kexinit[512]; uint32_t server_kexinit_len;
    uint8_t  in[SSH_MAX_PACKET + 64];
    uint8_t  out[SSH_MAX_PACKET + 64];
    uint8_t  payload[SSH_MAX_PACKET];
} ssh_t;

/* One slot per connection, as many as TCP has (v0.42.0). Each is served by
 * its own worker task, and S is whichever slot the calling task serves, so
 * the protocol code below reads exactly as it did when there was one. The
 * session's own tasks never use S: they reach their slot through the
 * terminal's write context. */
#define SSH_MAX_CONN 4              /* of TCP's 8: room left for refusals and other services */
static ssh_t conn_slots[SSH_MAX_CONN];

static ssh_t *cur_ssh(void) {
    task_t *me = task_current();
    for (int i = 0; i < SSH_MAX_CONN; i++)
        if (conn_slots[i].used && conn_slots[i].task == me) return &conn_slots[i];
    return &conn_slots[0];          /* not reached: only workers use S */
}
#define S (*cur_ssh())

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

/* One session at a time, and a second client should be told so rather than
 * left to time out. Lines before the version string are allowed by RFC 4253
 * and OpenSSH prints them, so the reason reaches the person's terminal. */
static void refuse_with(int other, const char *why);
static void refuse_busy(int other) {
    klog("[ssh] refused a client: all %u sessions busy\n", (unsigned)SSH_MAX_CONN);
    refuse_with(other, "the vault is busy: every session slot is in use, try again later");
}

static void refuse_with(int other, const char *why) {
    /* A line before the version string is only shown by `ssh -v`. What the
     * client prints unprompted is a DISCONNECT, so: our version line, then a
     * plain (pre-key-exchange) DISCONNECT with reason 12, "too many
     * connections", and the words. The client shows "Received disconnect
     * from ...: 12: the vault is busy ...". */
    static const char ver[] = "SSH-2.0-soupOS_0.34\r\n";
    uint8_t pkt[128]; wbuf_t b; w_init(&b, pkt + 5, sizeof(pkt) - 5);
    w_u8(&b, MSG_DISCONNECT); w_u32(&b, 12); w_cstr(&b, why); w_cstr(&b, "");
    uint32_t len = b.len, pad = 8 - ((5 + len) % 8);
    if (pad < 4) pad += 8;
    uint32_t plen = 1 + len + pad;
    pkt[0] = (uint8_t)(plen >> 24); pkt[1] = (uint8_t)(plen >> 16); pkt[2] = (uint8_t)(plen >> 8); pkt[3] = (uint8_t)plen;
    pkt[4] = (uint8_t)pad;
    memset(pkt + 5 + len, 0, pad);
    tcp_send_on(other, ver, sizeof(ver) - 1);
    tcp_send_on(other, pkt, (uint16_t)(4 + plen));
    tcp_close_on(other);
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
    uint8_t *mem = S.kexbuf;
    wbuf_t b; w_init(&b, mem, sizeof(S.kexbuf));
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
    /* The first exchange's H is the session id for the life of the
     * connection; a rekey derives new keys from it, never replaces it. */
    if (!S.have_session_id) { memcpy(S.session_id, H, 32); S.have_session_id = 1; }

    uint8_t sig[64];
    ed25519_sign(sig, H, 32, host_seed, host_pk);

    w_init(&b, mem, sizeof(S.kexbuf));
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
    S.kexinit_sent = 0;
    S.kex_seq_out  = S.seq_out;
    S.kex_tick     = timer_get_ticks();
    klog("[ssh] keys in: curve25519-sha256 / ssh-ed25519 / chacha20-poly1305\n");
    return 0;
}

/* RFC 4253 section 9: either side may start a new exchange at any time, and
 * a long connection must. The keys change; the session id does not. Called
 * with the peer's KEXINIT in S.payload. Nothing but kex packets may be sent
 * from our KEXINIT to our NEWKEYS, which holds because this runs to the end
 * on the SSH task before the loop flushes channel output again. */
static int rekey(uint32_t n, const char *who) {
    if (check_client_kexinit(S.payload, n) < 0) return -1;
    memcpy(S.client_kexinit, S.payload, n);
    S.client_kexinit_len = n;
    if (!S.kexinit_sent) {
        build_kexinit();
        if (write_packet(S.server_kexinit, S.server_kexinit_len) < 0) return -1;
    }
    int m;
    do { m = read_packet(); } while (m > 0 && (S.payload[0] == MSG_IGNORE || S.payload[0] == MSG_DEBUG));
    if (m < 1 || do_kex(S.payload, (uint32_t)m) < 0) { klog("[ssh] rekey failed\n"); return -1; }
    S.rekeys++;
    klog("[ssh] rekeyed (%s), %u so far\n", who, S.rekeys);
    return 0;
}

/* Our side of it: after an hour, or after rekey_packets packets if that has
 * been set (the test sets it small; 0 means only the hour). */
static uint32_t rekey_packets;
void ssh_set_rekey_packets(uint32_t n) { rekey_packets = n; }

/* A session with no keystrokes from its client for this long is ended
 * (v0.53.3); 0, the default, is never. Four slots are all there are, and a
 * forgotten session would hold one forever. */
static uint32_t idle_limit_s;
void ssh_set_idle(uint32_t seconds) { idle_limit_s = seconds; }
uint32_t ssh_idle(void) { return idle_limit_s; }

static int maybe_start_rekey(void) {
    if (S.kexinit_sent) return 0;
    int due = (timer_get_ticks() - S.kex_tick > 360000u) ||
              (rekey_packets && S.seq_out - S.kex_seq_out >= rekey_packets);
    if (!due) return 0;
    build_kexinit();
    if (write_packet(S.server_kexinit, S.server_kexinit_len) < 0) return -1;
    S.kexinit_sent = 1;
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

/* ── output ring: the session's terminal (its tasks) -> the SSH task ─────── */
static void oring_push(ssh_t *c, char ch) {
    uint32_t next = (c->ohead + 1) % sizeof(c->oring);
    if (next == c->otail) return;           /* full: drop, the terminal survives */
    c->oring[c->ohead] = (uint8_t)ch;
    c->ohead = next;
}

/* The session terminal's write: whatever task prints in the session pushes
 * here and the SSH task drains it. A full ring makes the WRITER wait, which
 * is what a slow terminal should do to a program, rather than losing output;
 * a writer that cannot wait (interrupts off) or a hung-up channel drops. */
static void stream_write(const char *z, uint32_t n, void *ctx) {
    ssh_t *c = ctx;                         /* the writer is a session task, not the worker */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t start = timer_get_ticks();
        while (((c->ohead + 1) % sizeof(c->oring)) == c->otail) {
            uint32_t fl;
            __asm__ volatile ("pushf; pop %0" : "=r"(fl));
            if (c->term.closed || !(fl & 0x200) || timer_get_ticks() - start > 500) return;
            task_yield();
            if (((c->ohead + 1) % sizeof(c->oring)) == c->otail) cpu_halt();
        }
        preempt_disable();
        oring_push(c, z[i]);
        preempt_enable();
    }
}

static int flush_output(void) {
    while (S.ohead != S.otail && S.win_out > 0) {
        uint8_t chunk[1024];
        uint32_t n = 0;
        preempt_disable();
        while (S.ohead != S.otail && n < sizeof(chunk) && n < S.win_out) {
            chunk[n++] = S.oring[S.otail];
            S.otail = (S.otail + 1) % sizeof(S.oring);
        }
        preempt_enable();
        uint8_t mem[1024 + 16]; wbuf_t b; w_init(&b, mem, sizeof(mem));
        w_u8(&b, MSG_CHANNEL_DATA); w_u32(&b, S.chan_peer); w_str(&b, chunk, n);
        if (write_packet(b.p, b.len) < 0) return -1;
        S.win_out -= n;
    }
    return 0;
}

/* ── public keys: /AUTHKEYS on the disk, one "ssh-ed25519 <base64> ..." a line ── */
static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
static int b64dec(const char *in, uint32_t n, uint8_t *out, uint32_t cap) {
    uint32_t acc = 0, bits = 0, o = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (in[i] == '=') break;
        int v = b64val(in[i]);
        if (v < 0) return -1;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; if (o >= cap) return -1; out[o++] = (uint8_t)(acc >> bits); }
    }
    return (int)o;
}

/* Does /AUTHKEYS let this exact key log in as `user`? (v0.49.0)
 *
 * A line is "cook ssh-ed25519 <base64> [comment]": the key opens that cook's
 * account and no other. Until v0.49.0 lines were bare keys and any of them
 * opened ANY account, the headchef's included. A bare line - the old form,
 * and what a .pub file is - is still read, as the headchef's key only: the
 * safe reading of a file written before keys had names. */
static int token(const uint8_t *f, uint32_t *a, uint32_t e, uint32_t *b) {
    while (*a < e && (f[*a] == ' ' || f[*a] == '\t')) (*a)++;
    *b = *a;
    while (*b < e && f[*b] != ' ' && f[*b] != '\t' && f[*b] != '\r') (*b)++;
    return *b > *a;
}

static int key_authorized(const char *user, const uint8_t *blob, uint32_t blob_len) {
    uint8_t *file = S.authkeys;
    uint32_t got = 0;
    if (fat_read("/AUTHKEYS", file, sizeof(S.authkeys) - 1, &got) < 0 || got == 0) return 0;
    if (got > sizeof(S.authkeys) - 1) got = sizeof(S.authkeys) - 1;   /* fat_read fills at most bufsize but reports the FILE's size */
    file[got] = 0;
    uint32_t i = 0;
    while (i < got) {
        uint32_t e = i;
        while (e < got && file[e] != '\n') e++;
        uint32_t a = i, b;
        const char *owner = 0; uint32_t owner_len = 0;
        if (token(file, &a, e, &b)) {
            if (b - a == 11 && memcmp(file + a, "ssh-ed25519", 11) == 0) {
                owner = "headchef"; owner_len = 8;          /* a bare line */
            } else {
                owner = (const char *)file + a; owner_len = b - a;
                a = b;
                if (!token(file, &a, e, &b) || b - a != 11 || memcmp(file + a, "ssh-ed25519", 11) != 0) owner = 0;
            }
        }
        if (owner && strlen(user) == owner_len && memcmp(user, owner, owner_len) == 0) {
            a = b;
            if (token(file, &a, e, &b)) {
                uint8_t dec[128];
                int n = b64dec((const char *)file + a, b - a, dec, sizeof(dec));
                if (n == (int)blob_len && memcmp(dec, blob, blob_len) == 0) return 1;
            }
        }
        i = e + 1;
    }
    return 0;
}

/* Remove every /AUTHKEYS line that names `cook` (v0.54.1). Called by
 * `fire`: a line names a cook, not a person, so a cook later hired under the
 * same name would otherwise log in with the fired one's key. Bare lines (the
 * headchef's) and other cooks' lines are kept as they are. Returns the
 * number of lines removed, or -1 if the file could not be rewritten. */
int ssh_forget_keys(const char *cook) {
    uint32_t got = 0, cap = 16384;
    uint8_t *f = kmalloc(cap + 1);
    if (!f) return -1;
    if (fat_read("/AUTHKEYS", f, cap, &got) < 0 || got == 0) { kfree(f); return 0; }
    if (got > cap) got = cap;
    uint32_t w = 0, i = 0, cl = (uint32_t)strlen(cook);
    int removed = 0;
    while (i < got) {
        uint32_t e = i;
        while (e < got && f[e] != '\n') e++;
        uint32_t a = i, b;
        int named = token(f, &a, e, &b) && b - a == cl && memcmp(f + a, cook, cl) == 0;
        if (named) {
            uint32_t a2 = b, b2;                /* only "cook ssh-ed25519 ..." */
            named = token(f, &a2, e, &b2) && b2 - a2 == 11 && memcmp(f + a2, "ssh-ed25519", 11) == 0;
        }
        uint32_t next = e < got ? e + 1 : e;
        if (named) removed++;
        else { memmove(f + w, f + i, next - i); w += next - i; }
        i = next;
    }
    users_sys_begin();
    int rc = removed ? (fat_write("/AUTHKEYS", f, w) == 0 ? removed : -1) : 0;
    users_sys_end();
    if (removed) users_seal_system();
    kfree(f);
    return rc;
}

/* ── guessing costs time (v0.50.0) ─────────────────────────────────────────
 * Measured before: the real client got 124 wrong guesses a minute through
 * one address, bounded only by the key exchange and PBKDF2. Now each failed
 * password costs its connection a pause (1 s, doubling, capped at 8), and an
 * address with FAIL_LIMIT failures inside FAIL_WINDOW is refused at the door
 * until it has been quiet that long. A successful login clears its count. */
#define FAIL_LIMIT  10
#define FAIL_WINDOW (5 * 60 * 100)                 /* ticks: five minutes */
static struct { uint32_t ip, fails, last; } fail_table[8];

static int fail_slot(uint32_t ip, int make) {
    int oldest = 0;
    for (int i = 0; i < 8; i++) {
        if (fail_table[i].ip == ip && fail_table[i].fails) return i;
        if (fail_table[i].last < fail_table[oldest].last) oldest = i;
    }
    if (!make) return -1;
    fail_table[oldest].ip = ip; fail_table[oldest].fails = 0;
    return oldest;
}

static int address_locked(uint32_t ip) {
    int i = fail_slot(ip, 0);
    if (i < 0) return 0;
    if (timer_get_ticks() - fail_table[i].last > FAIL_WINDOW) { fail_table[i].fails = 0; return 0; }
    return fail_table[i].fails >= FAIL_LIMIT;
}

/* A dotted quad, by hand; out holds at least 16 bytes. */
static void ip_text(uint32_t a, char *out) {
    int k = 0;
    for (int part = 3; part >= 0; part--) {
        uint32_t v = (a >> (part * 8)) & 255;
        if (v >= 100) out[k++] = (char)('0' + v / 100);
        if (v >= 10)  out[k++] = (char)('0' + (v / 10) % 10);
        out[k++] = (char)('0' + v % 10);
        if (part) out[k++] = '.';
    }
    out[k] = '\0';
}

static void note_failure(uint32_t ip) {
    int i = fail_slot(ip, 1);
    if (timer_get_ticks() - fail_table[i].last > FAIL_WINDOW) fail_table[i].fails = 0;
    fail_table[i].fails++;
    fail_table[i].last = timer_get_ticks();
}

static void note_success(uint32_t ip) {
    int i = fail_slot(ip, 0);
    if (i >= 0) fail_table[i].fails = 0;
}

/* ── userauth: password against the roster, or an ed25519 key on the disk ── */
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
        if ((strcmp(m, "publickey") == 0)) {
            /* RFC 4252 section 7: without a signature it is a question
             * ("would you take this key?"), with one it is the login. */
            int has_sig = r_u8(&r) != 0;
            uint32_t al, bl; const uint8_t *alg = r_str(&r, &al); const uint8_t *blob = r_str(&r, &bl);
            if (!r.bad && al == 11 && memcmp(alg, "ssh-ed25519", 11) == 0 && key_authorized(u, blob, bl)) {
                rbuf_t kb; r_init(&kb, blob, bl);
                uint32_t kl, pl2; r_str(&kb, &kl); const uint8_t *pk = r_str(&kb, &pl2);
                if (!has_sig) {
                    uint8_t mem[128]; wbuf_t b; w_init(&b, mem, sizeof(mem));
                    w_u8(&b, MSG_USERAUTH_PK_OK); w_str(&b, alg, al); w_str(&b, blob, bl);
                    if (write_packet(b.p, b.len) < 0) return -1;
                    attempts--;
                    continue;
                }
                uint32_t sl2; const uint8_t *sigblob = r_str(&r, &sl2);
                rbuf_t sb; r_init(&sb, sigblob, sl2);
                uint32_t xl, sgl; r_str(&sb, &xl); const uint8_t *sig = r_str(&sb, &sgl);
                if (!r.bad && !kb.bad && !sb.bad && pl2 == 32 && sgl == 64) {
                    /* The signed data: the session id, then the request as sent. */
                    wbuf_t sd; w_init(&sd, S.signed_mem, sizeof(S.signed_mem));
                    w_str(&sd, S.session_id, 32);
                    w_u8(&sd, MSG_USERAUTH_REQUEST);
                    w_str(&sd, user, ul); w_str(&sd, "ssh-connection", 14); w_cstr(&sd, "publickey");
                    w_u8(&sd, 1); w_str(&sd, alg, al); w_str(&sd, blob, bl);
                    /* A key in /AUTHKEYS may log in as any cook that exists. */
                    int uid = users_uid_of(u);
                    ok = uid >= 0 && ed25519_verify(sig, sd.p, sd.len, pk) == 0;
                    if (ok) S.uid = (uint8_t)uid;
                }
            }
            if (ok) klog("[ssh] %s authenticated by key\n", u);
        } else if ((strcmp(m, "password") == 0)) {
            r_u8(&r);                                /* FALSE: not a change request */
            const uint8_t *pass = r_str(&r, &pl);
            if (!r.bad && pl < sizeof(pw)) {
                memcpy(pw, pass, pl); pw[pl] = '\0';
                int uid = users_check(u, pw);
                ok = uid >= 0;
                if (ok) S.uid = (uint8_t)uid;
                memset(pw, 0, sizeof(pw));
            }
        }
        if (ok) {
            note_success(S.peer_ip);
            { char ip[16]; ip_text(S.peer_ip, ip); logins_record(u, ip, 1); }
            if (strcmp(m, "password") == 0) klog("[ssh] %s authenticated by password\n", u);
            uint8_t ok_msg = MSG_USERAUTH_SUCCESS;
            return write_packet(&ok_msg, 1);
        }
        klog("[ssh] userauth %s by %s: refused\n", u, m);
        if (strcmp(m, "password") == 0) {
            /* A wrong password, not the client's "none" probe or a key
             * question: it costs this connection a pause, and the address
             * a mark. */
            note_failure(S.peer_ip);
            { char ip[16]; ip_text(S.peer_ip, ip); logins_record(u, ip, 0); }
            uint32_t pause = 1000u << (S.pw_fails < 3 ? S.pw_fails : 3);
            S.pw_fails++;
            task_sleep(pause);
        }
        uint8_t mem[64]; wbuf_t b; w_init(&b, mem, sizeof(mem));
        w_u8(&b, MSG_USERAUTH_FAILURE); w_cstr(&b, "publickey,password"); w_u8(&b, 0);
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

static int start_session(const char *exec_line) {
    if (S.shell) return -1;                          /* one per channel */
    term_stream_init(&S.term, S.pty_cols, S.pty_rows, stream_write, &S);
    char where[20];
    ip_text(S.peer_ip, where);
    S.shell = shell_session_start(&S.term.t, S.uid, exec_line, where);
    if (!S.shell) return -1;
    klog("[ssh] %s: a shell of its own for uid %u\n", exec_line ? "exec" : "shell", S.uid);
    return 0;
}

static int handle_channel_request(rbuf_t *r) {
    uint32_t tl; const uint8_t *type = r_str(r, &tl);
    int want_reply = r_u8(r) != 0;
    if (r->bad) return -1;
    char t[32]; uint32_t tn = tl < 31 ? tl : 31; memcpy(t, type, tn); t[tn] = '\0';

    int ok = 1;
    if (strcmp(t, "pty-req") == 0) {
        uint32_t el; r_str(r, &el);                  /* TERM: we speak VT100 to all */
        S.pty_cols = (int)r_u32(r);
        S.pty_rows = (int)r_u32(r);
    } else if (strcmp(t, "window-change") == 0) {
        int c = (int)r_u32(r), rw = (int)r_u32(r);
        if (S.shell) term_stream_resize(&S.term, c, rw);
        else { S.pty_cols = c; S.pty_rows = rw; }
    } else if (strcmp(t, "shell") == 0) {
        ok = start_session(0) == 0;
    } else if (strcmp(t, "exec") == 0) {
        uint32_t cl; const uint8_t *cmd = r_str(r, &cl);
        if (r->bad) return -1;
        char line[256];
        uint32_t n = cl < sizeof(line) - 1 ? cl : sizeof(line) - 1;
        memcpy(line, cmd, n); line[n] = '\0';
        ok = start_session(line) == 0;
    } else if (strcmp(t, "env") != 0) {
        ok = 0;                                      /* subsystems, x11, agent: no */
    }
    if (want_reply) return reply_status(ok ? MSG_CHANNEL_SUCCESS : MSG_CHANNEL_FAILURE);
    return 0;
}

static int finish_session(int status) {
    /* EOF, the session's exit status, close. */
    uint8_t mem[64]; wbuf_t b;
    if (reply_status(MSG_CHANNEL_EOF) < 0) return -1;
    w_init(&b, mem, sizeof(mem));
    w_u8(&b, MSG_CHANNEL_REQUEST); w_u32(&b, S.chan_peer); w_cstr(&b, "exit-status"); w_u8(&b, 0);
    w_u32(&b, (uint32_t)status);
    if (write_packet(b.p, b.len) < 0) return -1;
    S.close_sent = 1;
    return reply_status(MSG_CHANNEL_CLOSE);
}

static void connection_loop(void) {
    for (;;) {
        if (maybe_start_rekey() < 0) return;
        /* Between our KEXINIT and the new keys, no channel data may go out. */
        if (!S.kexinit_sent && flush_output() < 0) return;
        int status;
        if (S.shell && !S.close_sent && S.ohead == S.otail && shell_session_done(S.shell, &status)) {
            if (finish_session(status) < 0) return;
        }
        int n = read_packet_wait(2);
        if (n == -2) {
            uint32_t lim = idle_limit_s;
            if (lim && S.chan_open && timer_get_ticks() - S.last_data >= lim * 100u) {
                char why[96], num[11]; int k = 0;
                uint32_t v = lim; do { num[k++] = (char)('0' + v % 10); v /= 10; } while (v);
                strcpy(why, "idle for ");
                size_t w = strlen(why);
                while (k) why[w++] = num[--k];
                why[w] = '\0';
                strcat(why, " seconds: the vault ended this session");
                klog("[ssh] %s idle for %u s: session ended\n", users_name_of(S.uid), lim);
                send_disconnect(11, why);           /* 11: by application */
                return;
            }
            continue;
        }
        if (n < 1) return;
        rbuf_t r; r_init(&r, S.payload + 1, (uint32_t)n - 1);
        switch (S.payload[0]) {
        case MSG_IGNORE: case MSG_DEBUG: break;
        case MSG_DISCONNECT: return;
        case MSG_KEXINIT:
            if (rekey((uint32_t)n, S.kexinit_sent ? "server" : "client") < 0) return;
            break;
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
            if (S.shell) term_stream_feed(&S.term, d, dl);
            S.last_data = timer_get_ticks();
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
            if (!S.close_sent) reply_status(MSG_CHANNEL_CLOSE);
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
    ssh_t *me = &S;
    task_t *t = me->task;
    uint32_t ip = me->peer_ip;              /* set by the listener before the wipe */
    memset(me, 0, sizeof(*me));
    me->task = t;
    me->used = 1;
    me->peer_ip = ip;
    S.h = -1;
    S.ohead = S.otail = 0;
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
    S.ohead = S.otail = 0;
    S.last_data = timer_get_ticks();
    connection_loop();
    S.chan_open = 0;
    if (S.shell) {
        /* Hang up: the terminal reads as gone, the session's programs are
         * killed, and the session task notices and ends. Wait for it, so the
         * terminal it points at (inside S) is not reused under it. */
        term_stream_close(&S.term);
        shell_session_hangup(S.shell);
        uint32_t start = timer_get_ticks();
        while (!shell_session_done(S.shell, 0) && timer_get_ticks() - start < 500) {
            task_yield();
            cpu_halt();
        }
        if (shell_session_done(S.shell, 0)) shell_session_free(S.shell);
        else klog("[ssh] session did not end within 5 s; leaving it\n");
        S.shell = 0;
    }
    klog("[ssh] session over\n");
}

/* A worker: one connection, start to finish, in its own slot. */
static void ssh_conn_task(void *arg) {
    ssh_t *c = arg;
    task_set_service();
    int h = c->h;
    c->task = task_current();               /* from here, S is this slot */
    klog("[ssh] connection on port %u (slot %d)\n", port_in_use, (int)(c - conn_slots));
    run_session(h);
    tcp_close_on(h);
    klog("[ssh] connection closed\n");
    preempt_disable();
    c->used = 0; c->task = 0;
    preempt_enable();
    task_exit();
}

static void ssh_task(void *arg) {
    (void)arg;
    task_set_service();
    running = 1;
    while (!want_stop) {
        if (tcp_listen(port_in_use) < 0) break;
        int h = tcp_accept(port_in_use, 100);
        if (h < 0) continue;
        ssh_t *slot = 0;
        preempt_disable();
        for (int i = 0; i < SSH_MAX_CONN; i++)
            if (!conn_slots[i].used) { slot = &conn_slots[i]; slot->used = 1; slot->task = 0; break; }
        preempt_enable();
        if (!slot) { refuse_busy(h); continue; }
        uint32_t pip = 0;
        tcp_conn_at(h, 0, &pip, 0, 0);
        if (address_locked(pip)) {
            /* The busy refusal's shape, with its own reason. */
            /* Logged first: the refusal ends with a graceful close that
             * waits for the peer, and the log line should not wait on it. */
            klog("[ssh] refused %u.%u.%u.%u: too many failed logins\n",
                 (pip >> 24) & 255, (pip >> 16) & 255, (pip >> 8) & 255, pip & 255);
            refuse_with(h, "too many failed logins from your address; try again in a few minutes");
            preempt_disable(); slot->used = 0; preempt_enable();
            continue;
        }
        slot->h = h;
        slot->peer_ip = pip;
        if (!task_spawn("ssh:conn", ssh_conn_task, slot)) {
            slot->used = 0;
            tcp_close_on(h);
        }
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
        users_sys_begin();
        int wr = fat_write(HOSTKEY_PATH, host_seed, 32);
        users_sys_end();
        if (wr < 0) return -1;
        users_seal_system();             /* the private key: rw----, headchef (v0.54.2) */
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
