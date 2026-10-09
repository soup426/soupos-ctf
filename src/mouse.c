/* mouse.c - PS/2 mouse, the i8042's auxiliary device.
 *
 * The copy-and-paste work said selection would stay keyboard-driven "unless a
 * PS/2 mouse driver is built first". This is that driver.
 *
 * Three things make this fiddlier than the keyboard:
 *
 *   - It lives on IRQ 12, which is on the slave PIC, so the line needs opening
 *     AND the cascade on the master needs to be open too. irq_unmask does both;
 *     forgetting it is what made the network card look broken for an hour.
 *   - Commands go to the mouse through the controller with a 0xD4 prefix, and
 *     each one is answered with 0xFA. Not waiting for that acknowledgement
 *     leaves the next command talking to a device that is still replying to
 *     the last one.
 *   - Packets are three bytes with no framing. If a byte is ever dropped, the
 *     stream stays misaligned forever unless something resynchronises it,
 *     which is what bit 3 of the first byte is for: it is always set.
 */
#include "mouse.h"
#include "isr.h"
#include "timer.h"
#include "vga.h"
#include "klog.h"
#include "fb.h"

#define PS2_DATA   0x60
#define PS2_CMD    0x64
#define PS2_STATUS 0x64

#define ST_OUTPUT_FULL 0x01
#define ST_INPUT_FULL  0x02
#define ST_FROM_AUX    0x20

#define CMD_ENABLE_AUX   0xA8
#define CMD_READ_CONFIG  0x20
#define CMD_WRITE_CONFIG 0x60
#define CMD_TO_AUX       0xD4

#define MOUSE_SET_DEFAULTS 0xF6
#define MOUSE_ENABLE       0xF4
#define MOUSE_ACK          0xFA
#define MOUSE_SET_RATE     0xF3
#define MOUSE_GET_ID       0xF2

/* Text cells, so the pointer lines up with everything else on screen. The
 * console's size is asked for rather than assumed, because a framebuffer boot
 * is bigger than the hardware text mode and the pointer must reach the edge. */
#define SCREEN_W (vga_cols())
#define SCREEN_H (vga_rows())
/* Raw counts per cell. The mouse reports far finer movement than a character
 * grid can show, and moving a cell per count makes the pointer unusable. */
#define COUNTS_PER_CELL 4

static inline void outb_(uint16_t p, uint8_t v) { __asm__ volatile ("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline uint8_t inb_(uint16_t p) { uint8_t r; __asm__ volatile ("inb %1,%0" : "=a"(r) : "Nd"(p)); return r; }

static int      present;
static uint8_t  pkt[4];
static int      pkt_len = 3;         /* 4 once the mouse says it has a wheel (v0.60.151) */
static volatile int wheel_total;     /* notches, down positive, since boot */
static int      pkt_idx;
static int      cur_x, cur_y;        /* cell coordinates          */
static int      px_x, px_y;          /* pixels, a count each (v0.60.140) */
static int      acc_x, acc_y;        /* leftover raw counts       */
static int      buttons;
static volatile uint32_t press_last, press_prev; /* ticks of the last two left presses */
static volatile uint32_t press_count;             /* and how many there have been */
static uint32_t moves;               /* packets accepted, for `skewer` */
static uint32_t resyncs;             /* packets dropped out of frame   */

int  mouse_present(void) { return present; }
void mouse_get(int *x, int *y, int *btn) {
    if (x)   *x   = cur_x;
    if (y)   *y   = cur_y;
    if (btn) *btn = buttons;
}
/* The pointer in pixels (v0.60.140), for the desktop: one raw count a
 * pixel, clamped to the framebuffer (or to the text screen's 8x16 cells
 * when there is none). Kept beside the cells, not derived from them, so
 * everything that reads cells moves exactly as it always has. */
static int px_w(void) { return fb_present() ? (int)fb_width()  : SCREEN_W * 8; }
static int px_h(void) { return fb_present() ? (int)fb_height() : SCREEN_H * 16; }
void mouse_get_px(int *x, int *y, int *btn) {
    if (x)   *x   = px_x;
    if (y)   *y   = px_y;
    if (btn) *btn = buttons;
}
/* How many times the left button has gone down, and when the last two were
 * (v0.60.145). Counted in the interrupt, so a click shorter than a poller's
 * nap is not lost, and a double click is timed by the presses themselves. */
int mouse_wheel(void) { return wheel_total; }
uint32_t mouse_presses(uint32_t *last, uint32_t *prev) {
    uint32_t n, a, b;
    do { n = press_count; a = press_last; b = press_prev; } while (n != press_count);
    if (last) *last = a;
    if (prev) *prev = b;
    return n;
}
void mouse_stats(uint32_t *m, uint32_t *r) {
    if (m) *m = moves;
    if (r) *r = resyncs;
}

static void wait_write(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb_(PS2_STATUS) & ST_INPUT_FULL)) return;
}
static void wait_read(void) {
    for (int i = 0; i < 100000; i++)
        if (inb_(PS2_STATUS) & ST_OUTPUT_FULL) return;
}

static uint8_t aux_command(uint8_t cmd) {
    wait_write(); outb_(PS2_CMD,  CMD_TO_AUX);
    wait_write(); outb_(PS2_DATA, cmd);
    wait_read();
    return inb_(PS2_DATA);              /* expected: 0xFA */
}

static void mouse_irq(registers_t *regs) {
    (void)regs;
    /* Only take bytes the controller says came from the auxiliary device;
     * anything else belongs to the keyboard. */
    uint8_t st = inb_(PS2_STATUS);
    if (!(st & ST_OUTPUT_FULL) || !(st & ST_FROM_AUX)) return;
    uint8_t b = inb_(PS2_DATA);

    /* Bit 3 of the first byte is always set. If it is not, the stream has
     * slipped and this byte is the middle of someone else's packet. */
    if (pkt_idx == 0 && !(b & 0x08)) { resyncs++; return; }

    pkt[pkt_idx++] = b;
    if (pkt_idx < pkt_len) return;
    pkt_idx = 0;

    uint8_t flags = pkt[0];
    if (flags & 0xC0) return;           /* overflow: the deltas are nonsense */

    int dx = (int)pkt[1];
    int dy = (int)pkt[2];
    if (flags & 0x10) dx |= ~0xFF;      /* sign-extend from the flags byte */
    if (flags & 0x20) dy |= ~0xFF;

    if (pkt_len == 4) {                 /* the wheel: the low nibble, signed */
        int dz = pkt[3] & 0x0F;
        if (dz & 0x08) dz -= 16;
        wheel_total += dz;
    }
    if ((flags & 1) && !(buttons & 1)) { press_prev = press_last; press_last = timer_get_ticks(); press_count++; }
    buttons = flags & 0x07;

    px_x += dx;
    px_y -= dy;
    if (px_x < 0) px_x = 0;
    if (px_y < 0) px_y = 0;
    if (px_x >= px_w()) px_x = px_w() - 1;
    if (px_y >= px_h()) px_y = px_h() - 1;

    acc_x += dx;
    acc_y += dy;                        /* positive is up, screen y grows down */

    cur_x += acc_x / COUNTS_PER_CELL;
    cur_y -= acc_y / COUNTS_PER_CELL;
    acc_x %= COUNTS_PER_CELL;
    acc_y %= COUNTS_PER_CELL;

    if (cur_x < 0) cur_x = 0;
    if (cur_y < 0) cur_y = 0;
    if (cur_x >= SCREEN_W) cur_x = SCREEN_W - 1;
    if (cur_y >= SCREEN_H) cur_y = SCREEN_H - 1;

    moves++;
}

int mouse_init(void) {
    /* Turn the auxiliary port on, then set the controller's config byte to
     * raise IRQ 12 and keep the mouse clock running. */
    wait_write(); outb_(PS2_CMD, CMD_ENABLE_AUX);

    wait_write(); outb_(PS2_CMD, CMD_READ_CONFIG);
    wait_read();
    uint8_t cfg = inb_(PS2_DATA);
    cfg |=  (1u << 1);                  /* enable the aux interrupt  */
    cfg &= ~(1u << 5);                  /* un-gate the aux clock     */
    wait_write(); outb_(PS2_CMD,  CMD_WRITE_CONFIG);
    wait_write(); outb_(PS2_DATA, cfg);

    if (aux_command(MOUSE_SET_DEFAULTS) != MOUSE_ACK) {
        klog("[mouse] no acknowledgement for set-defaults\n");
        return -1;
    }
    /* The IntelliMouse knock (v0.60.151): sample rates 200, 100, 80, then
     * ask the ID. 3 means a wheel and a fourth byte in every packet; a
     * plain mouse stays 0 and its three bytes. */
    static const uint8_t knock[3] = { 200, 100, 80 };
    for (int i = 0; i < 3; i++) { aux_command(MOUSE_SET_RATE); aux_command(knock[i]); }
    uint8_t id = 0;
    if (aux_command(MOUSE_GET_ID) == MOUSE_ACK) { wait_read(); id = inb_(PS2_DATA); }
    pkt_len = id == 3 ? 4 : 3;
    klog("[mouse] id %u, %s\n", id, id == 3 ? "a wheel" : "no wheel");
    if (aux_command(MOUSE_ENABLE) != MOUSE_ACK) {
        klog("[mouse] no acknowledgement for enable\n");
        return -1;
    }

    cur_x = SCREEN_W / 2;
    cur_y = SCREEN_H / 2;
    px_x = px_w() / 2;
    px_y = px_h() / 2;
    pkt_idx = 0;

    irq_register(12, mouse_irq);
    irq_unmask(12);                     /* slave line: opens the cascade too */

    present = 1;
    klog("[mouse] ps/2 mouse ready at %d,%d\n", cur_x, cur_y);
    return 0;
}
