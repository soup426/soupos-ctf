/* main.c - Mincecraft (v0.60.192): a voxel sandbox in a food world.
 *
 *   cook mincecraft.elf                 play (from a terminal on the desktop)
 *   cook mincecraft.elf -seed N         world N (a new world otherwise)
 *   cook mincecraft.elf -flat           the test world: crust at y 10
 *   cook mincecraft.elf -gen -seed N    make world N, print its hash, end
 *   cook mincecraft.elf -wait ... &     (v0.60.193) wait up to a minute for
 *                                       the desktop, for tests that start it
 *                                       at the console before countertop
 *   cook mincecraft.elf -time T         (v0.60.193) taken, used from v0.60.196
 *
 * Markers on stderr, [mincecraft] ...: window up, fps=N every 5 s, bye.
 */
#include "../ulib.h"
#include "world.h"
#include "render.h"

static void eprint_u(unsigned v) {
    char b[12]; int n = 11; b[n] = 0;
    do { b[--n] = (char)('0' + v % 10); v /= 10; } while (v);
    eprint(b + n);
}
static unsigned parse_u(const char *s) { unsigned v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (unsigned)(*s++ - '0'); return v; }
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

int main(void) {
    char *argv[12];
    int argc = uargv(argv, 12), gen_only = 0, flat = 0, seeded = 0, wait = 0;
    unsigned seed = 0;
    for (int i = 0; i < argc; i++) {
        if (same(argv[i], "-gen")) gen_only = 1;
        else if (same(argv[i], "-flat")) flat = 1;
        else if (same(argv[i], "-wait")) wait = 1;
        else if (same(argv[i], "-seed") && i + 1 < argc) { seed = parse_u(argv[++i]); seeded = 1; }
        else if (same(argv[i], "-time") && i + 1 < argc) i++;
    }
    if (world_alloc() < 0) { print("Mincecraft: no memory for the world\n"); return 1; }
    if (!seeded) sys_random(&seed, 4);
    if (flat) world_flat(); else world_gen(seed);
    if (gen_only) {
        eprint("[mincecraft] world seed="); eprint_u(seed);
        eprint(" hash="); eprint_u(world_hash());
        eprint(" top="); eprint_u((unsigned)world_top(64, 64));
        eprint("\n");
        return 0;
    }

    int win = sys_win_open(BODY_W, BODY_H, "Mincecraft");
    for (int i = 0; wait && win < 0 && i < 600; i++) { sys_sleep(100); win = sys_win_open(BODY_W, BODY_H, "Mincecraft"); }
    if (win < 0) { print("Mincecraft could not open its window (is the countertop up?)\n"); return 1; }
    unsigned *body = (unsigned *)sys_sbrk(BODY_W * BODY_H * 4);
    if (body == (unsigned *)-1 || render_alloc() < 0) { print("Mincecraft: no memory for the screen\n"); return 1; }
    eprint("[mincecraft] window up\n");
    cam_t cam = { (64 << 16) + FX_ONE / 2, ((world_top(64, 64) + 1) << 16) + 104858, (64 << 16) + FX_ONE / 2, 0, 0 };
    unsigned fps_t = sys_ticks();
    int frames = 0;
    for (;;) {
        uwinev_t ev;
        int g, quit = 0;
        while ((g = sys_win_event(win, &ev, 0)) == 1)
            if (ev.type == WEV_CLOSE || (ev.type == WEV_KEY && ev.key == 27)) quit = 1;
        if (g < 0 || quit) break;
        render(&cam, body, 256, 0x87BEEB);
        sys_win_put(win, body);
        frames++;
        unsigned now = sys_ticks();
        if (now - fps_t >= 500) { eprint("[mincecraft] fps="); eprint_u((unsigned)(frames * 100 / (int)(now - fps_t))); eprint("\n"); frames = 0; fps_t = now; }
    }
    eprint("[mincecraft] bye\n");
    sys_win_close(win);
    return 0;
}
