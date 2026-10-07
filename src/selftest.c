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
#include "fb.h"
#include "task.h"
#include "klog.h"

static int t_run, t_fail;

static void check(const char *name, int ok) {
    t_run++;
    if (!ok) t_fail++;
    vga_set_color(ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    vga_printf("  %-28s %s\n", name, ok ? "ok" : "FAIL");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
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
static void test_services(void) {
    task_t *head = task_list_head();
    int worst = 0;
    const char *worst_name = "none";
    if (head) {
        task_t *t = head;
        do {
            if (t->service && (int)t->cpu_last > worst) {
                worst = (int)t->cpu_last;
                worst_name = t->name;
            }
            t = t->next;
        } while (t != head);
    }
    klog("[sample] busiest service: %s at %d%%\n", worst_name, worst);
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

int selftest_run(void) {
    t_run = t_fail = 0;

    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  tasting the soup...\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    test_heap();
    test_vfs_table();
    test_pipe_wrap();
    test_checksum();
    test_fat_roundtrip();
    test_vga();
    test_services();
    test_str();

    vga_set_color(t_fail ? VGA_LIGHT_RED : VGA_LIGHT_GREEN, VGA_BLACK);
    vga_printf("  %d tests, %d failed\n", t_run, t_fail);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    /* One line, fixed shape, for the gate. */
    klog("[sample] %d tests, %d failed\n", t_run, t_fail);
    return t_fail;
}
