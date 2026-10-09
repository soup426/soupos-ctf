/* selftest.c - in-kernel assertions, run by the `sample` shell command.
 *
 * The smoke gate drives the whole machine from outside and checks what comes
 * out of the serial port. That catches a great deal, but it can only see what
 * a subsystem does through the shell, so a subsystem with no command exercising
 * its edges is untested. These are the edges: allocator patterns, the handle
 * table at exhaustion, the pipe ring across its wrap, a checksum with a known
 * answer, a filesystem round trip, and the hand-written string routines that
 * everything else is built on.
 *
 * Every test is self-contained and leaves nothing behind, because `sample` is
 * meant to be runnable on a live machine rather than on a special boot. The
 * last line is machine-readable so the gate can assert on one grep.
 */
#include "selftest.h"
#include "heap.h"
#include "vfs.h"
#include "fat.h"
#include "net.h"
#include "str.h"
#include "vga.h"
#include "term.h"
#include "fb.h"
#include "task.h"
#include "klog.h"
#include "sha256.h"
#include "random.h"
#include "chacha.h"
#include "curve25519.h"
#include "sha512.h"
#include "ed25519.h"
#include "timer.h"

static int t_run, t_fail, t_slow;

static void check(const char *name, int ok) {
    t_run++;
    if (!ok) t_fail++;
    term_color(term_current(), ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    term_printf(term_current(), "  %-28s %s\n", name, ok ? "ok" : "FAIL");
    term_color(term_current(), VGA_LIGHT_GREY, VGA_BLACK);
    klog("[sample] %s %s\n", name, ok ? "ok" : "FAIL");
}

/* ── Heap ─────────────────────────────────────────────────────────────────
 * The interesting property is not that a single allocation works, it is that
 * the arena recovers. Allocate a run of blocks, free them all, and a block
 * larger than any one of them must then fit: that only holds if free blocks
 * are being merged back together. */
static void test_heap(void) {
    void *p = kmalloc(64);
    int ok = (p != 0);
    if (ok) { memset(p, 0xA5, 64); ok = (((uint8_t *)p)[63] == 0xA5); kfree(p); }
    check("heap alloc/write/free", ok);

    void *blocks[16];
    int all = 1;
    for (int i = 0; i < 16; i++) {
        blocks[i] = kmalloc(1024);
        if (!blocks[i]) all = 0;
    }
    check("heap 16x1KB allocation", all);

    for (int i = 0; i < 16; i++) kfree(blocks[i]);

    void *big = kmalloc(12 * 1024);
    check("heap recovers freed space", big != 0);
    if (big) kfree(big);

    /* The same thing, freed in reverse. An allocator that only merges a freed
     * block with the one AFTER it copes with the in-order case above and
     * leaves this one as sixteen separate holes, so a block spanning them
     * fails. This is the case worth testing. */
    for (int i = 0; i < 16; i++) blocks[i] = kmalloc(1024);
    for (int i = 15; i >= 0; i--) kfree(blocks[i]);
    void *big2 = kmalloc(12 * 1024);
    check("heap recovers after reverse free", big2 != 0);
    if (big2) kfree(big2);

    /* An impossible request must fail rather than return something. */
    check("heap refuses the absurd", kmalloc(64u * 1024u * 1024u) == 0);
}

/* ── VFS handle table ─────────────────────────────────────────────────────
 * Exhaust it, confirm it says no rather than handing out a seventeenth, then
 * confirm closing gives the slots back. */
static void test_vfs_table(void) {
    vfs_node_t *n[VFS_MAX_OPEN];
    int opened = 0;
    for (int i = 0; i < VFS_MAX_OPEN; i++) {
        n[i] = vfs_open("/dev/null", VFS_RDONLY);
        if (n[i]) opened++;
    }
    check("vfs table fills", opened > 0);

    vfs_node_t *extra = vfs_open("/dev/null", VFS_RDONLY);
    check("vfs table refuses when full", extra == 0 || opened < VFS_MAX_OPEN);
    if (extra) vfs_close(extra);

    for (int i = 0; i < VFS_MAX_OPEN; i++) if (n[i]) vfs_close(n[i]);

    vfs_node_t *again = vfs_open("/dev/null", VFS_RDONLY);
    check("vfs table frees on close", again != 0);
    if (again) vfs_close(again);
}

/* ── Pipe ring ────────────────────────────────────────────────────────────
 * Push more bytes through than the ring holds, in chunks small enough never to
 * block, so the head and tail both wrap. Content is checked, not just counts:
 * an off-by-one in the wrap shows up as the wrong byte, not a short read. */
static void test_pipe_wrap(void) {
    vfs_node_t *rd = 0, *wr = 0;
    if (vfs_pipe(&rd, &wr) < 0) { check("pipe create", 0); return; }
    check("pipe create", 1);

    static uint8_t out[1000], in[1000];
    int ok = 1;
    for (int round = 0; round < 6 && ok; round++) {
        for (int i = 0; i < 1000; i++) out[i] = (uint8_t)(i + round);
        if (vfs_write(wr, out, 1000) != 1000) { ok = 0; break; }
        if (vfs_read(rd, in, 1000) != 1000)   { ok = 0; break; }
        for (int i = 0; i < 1000; i++)
            if (in[i] != out[i]) { ok = 0; break; }
    }
    check("pipe survives ring wrap", ok);

    vfs_close(wr);
    /* With the writer gone and the ring drained, a read must report EOF
     * rather than blocking forever. */
    check("pipe reads EOF after close", vfs_read(rd, in, 16) == 0);
    vfs_close(rd);
}

/* ── Checksum ─────────────────────────────────────────────────────────────
 * A known answer, computed independently, so a change to the arithmetic is
 * caught here rather than by a peer silently dropping our packets. */
static void test_checksum(void) {
    static const uint8_t udp[8] = { 0x00,0x35, 0x00,0x35, 0x00,0x08, 0x00,0x00 };
    uint16_t c = net_pseudo_checksum(IPV4(10,0,2,15), IPV4(10,0,2,2), 17, udp, 8);
    check("checksum known answer", c == 0xE763);
}

/* ── Filesystem ───────────────────────────────────────────────────────────
 * Write, read back, compare, remove. Uses the real disk, because a filesystem
 * test against a mock proves nothing about this one. */
static void test_fat_roundtrip(void) {
    static const char *path = "/SAMPLE.TMP";
    static const char  body[] = "soup, stock, simmer, serve";
    uint32_t len = (uint32_t)strlen(body);

    int wrote = fat_write(path, (const uint8_t *)body, len);
    check("fat write", wrote >= 0);

    static uint8_t back[64];
    uint32_t got = 0;
    int r = fat_read(path, back, sizeof(back), &got);
    int ok = (r >= 0 && got == len && memcmp(back, body, len) == 0);
    check("fat read matches", ok);

    check("fat delete", fat_delete(path) >= 0);

    /* Free space must come back exactly. A write that takes clusters and
     * gives back fewer is the shape of the leak fixed in the write path. */
    uint32_t before = 0, after = 0;
    fat_space(&before, 0, 0);
    if (fat_write(path, (const uint8_t *)body, len) >= 0 && fat_delete(path) >= 0) {
        fat_space(&after, 0, 0);
        check("fat gives back what it took", before == after);
    } else {
        check("fat gives back what it took", 0);
    }
}

/* ── VGA ──────────────────────────────────────────────────────────────────
 * The scroll is a hand-written word copy over the frame buffer. */
static void test_vga(void) {
    /* Runs in both modes now: the cell store is real memory either way (the
     * hardware buffer on a text boot, the RAM backing on a framebuffer one). */
    check("vga scroll moves 24 rows", vga_scroll_selftest());
}

/* ── Background services ──────────────────────────────────────────────────
 * The console renderer repaints thirty times a second and the mixer wakes a
 * hundred times a second, so it is worth knowing they are not costing real
 * time. Measured on an idle machine the renderer is 0%; the threshold is
 * generous because `sample` itself is printing while this runs, which gives
 * the renderer actual work to do. */
static int busiest_service(const char **name) {
    task_t *head = task_list_head();
    int worst = 0;
    *name = "none";
    if (head) {
        task_t *t = head;
        do {
            if (t->service && (int)t->cpu_last > worst) {
                worst = (int)t->cpu_last;
                *name = t->name;
            }
            t = t->next;
        } while (t != head);
    }
    return worst;
}

/* One busy second is measured again a second later (v0.60.108): on a loaded
 * host the guest loses time in lumps and a renderer catching up can read
 * over half for one second (2026-10-09, a full check). A service that spins
 * is busy in the next second too. */
static void test_services(void) {
    const char *worst_name;
    int worst = busiest_service(&worst_name);
    klog("[sample] busiest service: %s at %d%%\n", worst_name, worst);
    if (worst > 50) {
        task_sleep(1100);
        worst = busiest_service(&worst_name);
        klog("[sample] busiest service a second later: %s at %d%%\n", worst_name, worst);
    }
    check("services are not busy", worst <= 50);
}

/* ── The hand-written libc ────────────────────────────────────────────────
 * Everything above is built on these, and memmove's overlap behaviour is the
 * one that goes unnoticed until it corrupts something far away. */
static void test_str(void) {
    char buf[32];
    memset(buf, 'x', sizeof(buf));
    check("memset fills", buf[0] == 'x' && buf[31] == 'x');

    strcpy(buf, "soupOS");
    check("strcpy/strlen", strlen(buf) == 6 && buf[6] == '\0');
    check("strcmp orders", strcmp("a", "b") < 0 && strcmp("b", "a") > 0 &&
                           strcmp("eq", "eq") == 0);

    /* Overlapping, destination after source: a naive forward copy smears the
     * first byte through the range. */
    char ov[16];
    strcpy(ov, "abcdef");
    memmove(ov + 2, ov, 6);
    ov[8] = '\0';
    check("memmove handles overlap", ov[2] == 'a' && ov[3] == 'b' && ov[7] == 'f');
}

/* ── SHA-256 ──────────────────────────────────────────────────────────────
 * The FIPS 180-4 vectors and the RFC 4231 HMAC cases. Each one probes a
 * different path: the 56-byte message is the exact length whose padding
 * spills into a second block, the million 'a's exercise streaming across
 * thousands of blocks, and the 131-byte HMAC key is longer than a block, so
 * it has to be hashed first. Expected values were produced by a separate
 * implementation, never by this one. */
static int hex_eq(const uint8_t *d, const char *hex) {
    for (int i = 0; i < SHA256_DIGEST; i++) {
        unsigned hi = hex[2*i], lo = hex[2*i+1];
        hi = hi >= 'a' ? hi - 'a' + 10 : hi - '0';
        lo = lo >= 'a' ? lo - 'a' + 10 : lo - '0';
        if (d[i] != (uint8_t)((hi << 4) | lo)) return 0;
    }
    return 1;
}
static void test_sha256(void) {
    uint8_t d[SHA256_DIGEST];
    sha256("", 0, d);
    check("sha256 empty", hex_eq(d, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    sha256("abc", 3, d);
    check("sha256 abc", hex_eq(d, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    static const char m448[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256(m448, 56, d);
    check("sha256 448-bit (pad spills)", hex_eq(d, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    static uint8_t chunk[1000];
    memset(chunk, 'a', sizeof(chunk));
    sha256_t s;
    sha256_init(&s);
    for (int i = 0; i < 1000; i++) sha256_update(&s, chunk, sizeof(chunk));
    sha256_final(&s, d);
    check("sha256 million a's", hex_eq(d, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));

    uint8_t key[131];
    memset(key, 0x0b, 20);
    hmac_sha256(key, 20, "Hi There", 8, d);
    check("hmac rfc4231 #1", hex_eq(d, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
    hmac_sha256("Jefe", 4, "what do ya want for nothing?", 28, d);
    check("hmac rfc4231 #2", hex_eq(d, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
    memset(key, 0xaa, sizeof(key));
    hmac_sha256(key, sizeof(key), "Test Using Larger Than Block-Size Key - Hash Key First", 54, d);
    check("hmac key longer than block", hex_eq(d, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
    /* PBKDF2 (v0.46.0), for stored passwords. 1 and 4096 iterations, the
     * second timed: it is what a login costs. Expected values from hashlib. */
    pbkdf2_hmac_sha256("password", 8, "salt", 4, 1, d, 32);
    check("pbkdf2-sha256 1 iteration", hex_eq(d, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
    uint32_t pt0 = timer_get_ticks();
    pbkdf2_hmac_sha256("password", 8, "salt", 4, 4096, d, 32);
    klog("[sample] pbkdf2 4096 iterations took %u ticks\n", timer_get_ticks() - pt0);
    check("pbkdf2-sha256 4096 iterations", hex_eq(d, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"));
}

/* ── Entropy ──────────────────────────────────────────────────────────────
 * No vector exists for randomness. Two draws must differ, and 64 KB of
 * output must look balanced: ones within 3 sigma of half (sigma = sqrt(n)/2
 * = 362 bits for 524288), and bit runs within 3 sigma of n/2 likewise. A
 * pool that was somehow outputting its counter would pass the first check
 * and fail both the others. The cross-boot check lives in random-test.sh. */
static void test_random(void) {
    uint8_t a[32], b[32];
    random_bytes(a, 32);
    random_bytes(b, 32);
    check("random draws differ", memcmp(a, b, 32) != 0);
    char hex[17];
    for (int i = 0; i < 8; i++) {
        hex[2*i]   = "0123456789abcdef"[a[i] >> 4];
        hex[2*i+1] = "0123456789abcdef"[a[i] & 15];
    }
    hex[16] = '\0';
    klog("[sample] random draw: %s\n", hex);

    static uint8_t big[65536];
    random_bytes(big, sizeof(big));
    uint32_t ones = 0, runs = 1;
    int prev = big[0] & 1;
    for (uint32_t i = 0; i < sizeof(big) * 8; i++) {
        int bit = (big[i >> 3] >> (i & 7)) & 1;
        ones += (uint32_t)bit;
        if (i && bit != prev) runs++;
        prev = bit;
    }
    klog("[sample] random 64KB: %u ones, %u runs\n", ones, runs);
    /* Bounds at 5 sigma (sigma = sqrt(524288)/2 = 362). They were 3 sigma
     * (1086), which a perfect generator fails 0.27% of the time per check,
     * about one boot in 185 for the pair - and the gate boots many times: it
     * failed once, 2026-10-08, at 3.14 sigma. 36 boots measured then: ones
     * mean 262167 sd 374, runs mean 262142 sd 379, none past 3 sigma. 5 sigma
     * fails a fair source about once in two million and still catches any
     * bias above about 0.35%. */
    check("random monobit", ones > 262144 - 1810 && ones < 262144 + 1810);
    check("random runs",    runs > 262144 - 1810 && runs < 262144 + 1810);
}

/* ── ChaCha20-Poly1305 ────────────────────────────────────────────────────
 * The RFC 8439 vectors: section 2.4.2 (the cipher alone), 2.5.2 (the MAC
 * alone) and 2.8.2 (the AEAD, whose tag covers the padding and length
 * trailer that are easiest to get wrong). Then a tampered byte must be
 * refused, because an AEAD that decrypts anything is just a stream cipher. */
static int bytes_eq(const uint8_t *d, const char *hex, int n) {
    for (int i = 0; i < n; i++) {
        unsigned hi = hex[2*i], lo = hex[2*i+1];
        hi = hi >= 'a' ? hi - 'a' + 10 : hi - '0';
        lo = lo >= 'a' ? lo - 'a' + 10 : lo - '0';
        if (d[i] != (uint8_t)((hi << 4) | lo)) return 0;
    }
    return 1;
}
static void test_chacha(void) {
    static const char sunscreen[] =
        "Ladies and Gentlemen of the class of '99: If I could offer you "
        "only one tip for the future, sunscreen would be it.";
    uint8_t buf[114], key[32], nonce[12], tag[16];

    for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
    memset(nonce, 0, 12); nonce[7] = 0x4a;
    memcpy(buf, sunscreen, 114);
    chacha20_xor(key, nonce, 1, buf, 114);
    check("chacha20 rfc8439 2.4.2", bytes_eq(buf,
        "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b"
        "f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d8"
        "07ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab7793736"
        "5af90bbf74a35be6b40b8eedf2785e42874d", 114));

    static const uint8_t pk[32] = {
        0x85,0xd6,0xbe,0x78,0x57,0x55,0x6d,0x33,0x7f,0x44,0x52,0xfe,0x42,0xd5,0x06,0xa8,
        0x01,0x03,0x80,0x8a,0xfb,0x0d,0xb2,0xfd,0x4a,0xbf,0xf6,0xaf,0x41,0x49,0xf5,0x1b };
    poly1305_t p;
    poly1305_init(&p, pk);
    poly1305_update(&p, (const uint8_t *)"Cryptographic Forum ", 20);   /* split across blocks */
    poly1305_update(&p, (const uint8_t *)"Research Group", 14);
    poly1305_final(&p, tag);
    check("poly1305 rfc8439 2.5.2", bytes_eq(tag, "a8061dc1305136c6c22b8baf0c0127a9", 16));

    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(0x80 + i);
    static const uint8_t n2[12] = { 0x07,0,0,0, 0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47 };
    static const uint8_t aad[12] = { 0x50,0x51,0x52,0x53,0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7 };
    memcpy(buf, sunscreen, 114);
    chacha20poly1305_encrypt(key, n2, aad, 12, buf, 114, tag);
    check("aead rfc8439 2.8.2 ciphertext", bytes_eq(buf,
        "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
        "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
        "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
        "3ff4def08e4b7a9de576d26586cec64b6116", 114));
    check("aead rfc8439 2.8.2 tag", bytes_eq(tag, "1ae10b594f09e26a7e902ecbd0600691", 16));

    int ok = chacha20poly1305_decrypt(key, n2, aad, 12, buf, 114, tag);
    check("aead round trip", ok == 0 && memcmp(buf, sunscreen, 114) == 0);

    chacha20poly1305_encrypt(key, n2, aad, 12, buf, 114, tag);
    buf[50] ^= 1;
    check("aead refuses a flipped bit", chacha20poly1305_decrypt(key, n2, aad, 12, buf, 114, tag) == -1);
}

/* ── X25519 ───────────────────────────────────────────────────────────────
 * RFC 7748 section 5.2: two scalar multiplications and the iterated test,
 * in which k and u start at 9 and each round feeds (k*u, k) back in. The
 * single vectors can pass by luck with a carry bug that only bites for some
 * inputs; a thousand chained rounds give such a bug every chance to show.
 * The time is logged because this arithmetic is deliberately the simple,
 * slow kind, and a key exchange should stay well under a second. The
 * thousand rounds take 1.5 s on a quiet machine and far longer on a busy
 * one, so they run only under `sample slow` (scripts/x25519-test.sh). */
static void hex32(uint8_t *out, const char *hex) {
    for (int i = 0; i < 32; i++) {
        unsigned hi = hex[2*i], lo = hex[2*i+1];
        hi = hi >= 'a' ? hi - 'a' + 10 : hi - '0';
        lo = lo >= 'a' ? lo - 'a' + 10 : lo - '0';
        out[i] = (uint8_t)((hi << 4) | lo);
    }
}
static void test_x25519(void) {
    uint8_t k[32], u[32], q[32];
    hex32(k, "a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4");
    hex32(u, "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c");
    uint32_t t0 = timer_get_ticks();
    x25519(q, k, u);
    uint32_t t1 = timer_get_ticks();
    check("x25519 rfc7748 #1", bytes_eq(q, "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552", 32));
    klog("[sample] x25519 took %u ticks\n", t1 - t0);

    hex32(k, "4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d");
    hex32(u, "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493");
    x25519(q, k, u);
    check("x25519 rfc7748 #2", bytes_eq(q, "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957", 32));

    memset(k, 0, 32); k[0] = 9;
    memcpy(u, k, 32);
    x25519(q, k, u);
    memcpy(u, k, 32);
    memcpy(k, q, 32);
    check("x25519 iterated once", bytes_eq(k, "422c8e7a6227d7bca1350b3e2bb7279f7897b87bb6854b783c60e80311ae3079", 32));
    if (!t_slow) return;

    t0 = timer_get_ticks();
    for (int i = 1; i < 1000; i++) {
        x25519(q, k, u);
        memcpy(u, k, 32);
        memcpy(k, q, 32);
    }
    klog("[sample] x25519 1000 rounds took %u ticks\n", timer_get_ticks() - t0);
    check("x25519 iterated 1000x", bytes_eq(k, "684cf59ba83309552800ef566f2f4d3c1c3887c49360e3875f2eb94d99532c51", 32));
}

/* ── SHA-512 and Ed25519 ──────────────────────────────────────────────────
 * FIPS 180-4's "abc" and 896-bit SHA-512 vectors, then RFC 8032's first
 * three Ed25519 cases: each checks the public key derived from the seed and
 * the signature over the message, and the signature must verify. Then one
 * byte of a signature is changed and it must not. Expected values from the
 * cryptography library, not from this code. */
static void test_ed25519(void) {
    uint8_t d[64];
    sha512("abc", 3, d);
    check("sha512 abc", bytes_eq(d, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                                   "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", 64));
    sha512("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
           "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 112, d);
    check("sha512 896-bit", bytes_eq(d, "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
                                       "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909", 64));

    static const struct { const char *seed, *pk, *sig; const char *msg; int len; } v[3] = {
        { "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
          "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
          "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
          "", 0 },
        { "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
          "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
          "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
          "\x72", 1 },
        { "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
          "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
          "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a",
          "\xaf\x82", 2 },
    };
    uint8_t seed[32], pk[32], sig[64];
    int pk_ok = 1, sig_ok = 1, ver_ok = 1;
    uint32_t t0 = timer_get_ticks();
    for (int i = 0; i < 3; i++) {
        hex32(seed, v[i].seed);
        ed25519_public_key(pk, seed);
        if (!bytes_eq(pk, v[i].pk, 32)) pk_ok = 0;
        ed25519_sign(sig, (const uint8_t *)v[i].msg, (size_t)v[i].len, seed, pk);
        if (!bytes_eq(sig, v[i].sig, 64)) sig_ok = 0;
        if (ed25519_verify(sig, (const uint8_t *)v[i].msg, (size_t)v[i].len, pk) != 0) ver_ok = 0;
    }
    klog("[sample] ed25519: three keygen+sign+verify took %u ticks\n", timer_get_ticks() - t0);
    check("ed25519 rfc8032 public keys", pk_ok);
    check("ed25519 rfc8032 signatures", sig_ok);
    check("ed25519 signatures verify", ver_ok);
    sig[40] ^= 1;
    check("ed25519 refuses a changed byte",
          ed25519_verify(sig, (const uint8_t *)v[2].msg, 2, pk) == -1);
}

int selftest_run(int slow) {
    t_run = t_fail = 0;
    t_slow = slow;

    term_color(term_current(), VGA_YELLOW, VGA_BLACK);
    term_puts(term_current(), "  tasting the soup...\n");
    term_color(term_current(), VGA_LIGHT_GREY, VGA_BLACK);

    test_heap();
    test_vfs_table();
    test_pipe_wrap();
    test_checksum();
    test_fat_roundtrip();
    test_vga();
    test_services();
    test_str();
    test_sha256();
    test_random();
    test_chacha();
    test_x25519();
    test_ed25519();

    term_color(term_current(), t_fail ? VGA_LIGHT_RED : VGA_LIGHT_GREEN, VGA_BLACK);
    term_printf(term_current(), "  %d tests, %d failed\n", t_run, t_fail);
    term_color(term_current(), VGA_LIGHT_GREY, VGA_BLACK);

    /* One line, fixed shape, for the gate. */
    klog("[sample] %d tests, %d failed\n", t_run, t_fail);
    return t_fail;
}
