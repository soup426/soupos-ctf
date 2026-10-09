/* expo.c - the processes, in a window (v0.60.168): named for the
 * expediter, who runs the pass and calls the orders.
 *
 *   cook expo.elf           (from a terminal on the desktop)
 *
 * A row a process (SYS_PROCS, by pid, every second): its pid, program,
 * cook, state and pages. A click or the arrows choose one, k asks the
 * kernel to end it (SYS_KILL), and the line at the foot says what the
 * kernel answered: ended, not yours (another cook's, unless you are the
 * headchef: the kernel decides, as the shell's kill does), or gone. q or
 * the close box ends expo. Drawn with the kernel's own 8x16 font.
 */
#include "ulib.h"
#include "../src/font8x16.h"

#define EW 420
#define EH 300
#define ROW 18
#define TOP 28
#define NROW 13
static unsigned px[EW * EH];
static uproc_t procs[32];
static int n, sel;
static char status[64];

static void rect(int x, int y, int w, int h, unsigned c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) if (i >= 0 && j >= 0 && i < EW && j < EH) px[j * EW + i] = c;
}
static void text(int x, int y, const char *s, unsigned fg) {
    for (; *s; s++, x += 8)
        for (int j = 0; j < 16; j++)
            for (int i = 0; i < 8; i++)
                if ((font8x16[(unsigned char)*s][j] >> (7 - i)) & 1 && x + i < EW && y + j < EH) px[(y + j) * EW + x + i] = fg;
}
static void num(char *o, unsigned v) {
    char d[12]; int k = 0;
    do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    int m = 0; while (k) o[m++] = d[--k];
    o[m] = 0;
}
static void draw(int id) {
    rect(0, 0, EW, EH, 0x00F4F1EA);
    rect(0, 0, EW, 24, 0x00D8DCE2);
    text(8, 4, "PID  PROGRAM          COOK       ST  PAGES", 0x00202020);
    for (int i = 0; i < n && i < NROW; i++) {
        int y = TOP + i * ROW;
        unsigned fg = 0x00202020;
        if (i == sel) { rect(2, y, EW - 4, ROW, 0x003A6EA5); fg = 0x00F0F0F0; }
        char b[16]; num(b, procs[i].pid); text(8, y + 1, b, fg);
        text(48, y + 1, procs[i].name, fg);
        text(184, y + 1, procs[i].owner_name, fg);
        char st[2] = { (char)procs[i].state, 0 }; text(272, y + 1, st, fg);
        num(b, procs[i].pages); text(304, y + 1, b, fg);
    }
    rect(0, EH - 24, EW, 24, 0x00D8DCE2);
    text(8, EH - 20, status[0] ? status : "click or arrows to choose, k to end it, q to quit", 0x00202020);
    sys_win_put(id, px);
}
static void refresh(void) {
    n = sys_procs(procs, 32);
    if (n < 0) n = 0;
    if (sel >= n) sel = n ? n - 1 : 0;
}
static void say(const char *a, unsigned pid, const char *b) {
    int k = 0;
    for (; *a; a++) status[k++] = *a;
    char d[12]; num(d, pid);
    for (char *p = d; *p; p++) status[k++] = *p;
    for (; *b && k < 60; b++) status[k++] = *b;
    status[k] = 0;
    print("expo: "); print(status); print("\n");
}

int main(void) {
    int id = sys_win_open(EW, EH, "expo");
    if (id < 0) { print("expo: it needs the desktop (run countertop, then expo in one of its terminals)\n"); return 1; }
    refresh(); draw(id);
    unsigned next = sys_ticks() + 100;
    for (;;) {
        uwinev_t e;
        int r = sys_win_event(id, &e, 100), redraw = 0;
        if (r < 0) return 0;
        if (r == 1) {
            if (e.type == WEV_CLOSE || (e.type == WEV_KEY && e.key == 'q')) break;
            if (e.type == WEV_DOWN && e.y >= TOP && (e.y - TOP) / ROW < n) { sel = (e.y - TOP) / ROW; redraw = 1; }
            if (e.type == WEV_KEY && e.key == 257 && sel < n - 1) { sel++; redraw = 1; }   /* the arrows */
            if (e.type == WEV_KEY && e.key == 256 && sel > 0) { sel--; redraw = 1; }
            if (e.type == WEV_KEY && e.key == 'k' && n) {
                unsigned pid = procs[sel].pid;
                int k = sys_kill(pid);
                if (k == 0) say("ended ", pid, "");
                else if (k == -2) say("", pid, " is not yours to end");
                else say("", pid, " is gone");
                refresh(); redraw = 1;
            }
        }
        if (sys_ticks() >= next) { next = sys_ticks() + 100; refresh(); redraw = 1; }
        if (redraw) draw(id);
    }
    sys_win_close(id);
    return 0;
}
