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
 *   cook mincecraft.elf -script         (v0.60.194) the player after every tick
 *   cook mincecraft.elf -world PATH     (v0.60.194) taken, used from v0.60.197
 *
 * W A S D walk, the arrows look, space jumps (all held, read through
 * SYS_WIN_KEYS at a fixed 20 ticks a second); F or the left button held
 * breaks what the crosshair is on (v0.60.195), r or q places the hotbar's
 * block on that face, 1 to 9 or the wheel choose the slot; Esc or the
 * close box ends it.
 *
 * Markers on stderr, [mincecraft] ...: window up, fps=N every 5 s, bye;
 * with -script "p x y z yaw pitch ground" each tick (16.16, angle units),
 * and "mark" on m, so tests can split the log into phases; "broke x y z id",
 * "placed x y z id" or "place refused", and "inv id=n ..." for each change.
 */
#include "../ulib.h"
#include "world.h"
#include "render.h"
#include "player.h"
#include "hud.h"

static void eprint_u(unsigned v) {
    char b[12]; int n = 11; b[n] = 0;
    do { b[--n] = (char)('0' + v % 10); v /= 10; } while (v);
    eprint(b + n);
}
static unsigned parse_u(const char *s) { unsigned v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (unsigned)(*s++ - '0'); return v; }
/* "[mincecraft] inv id=n ..." for the ids whose count changed since last. */
static void inv_marker(const player_t *p, unsigned short *last) {
    int any = 0;
    for (int i = 1; i < B_COUNT; i++) if (p->inv[i] != last[i]) {
        if (!any) eprint("[mincecraft] inv");
        eprint(" "); eprint_u((unsigned)i); eprint("="); eprint_u(p->inv[i]);
        last[i] = p->inv[i]; any = 1;
    }
    if (any) eprint("\n");
}
static void cell_marker(const char *what, int x, int y, int z, int id) {
    eprint("[mincecraft] "); eprint(what); eprint(" "); eprint_u((unsigned)x); eprint(" "); eprint_u((unsigned)y);
    eprint(" "); eprint_u((unsigned)z); eprint(" "); eprint_u((unsigned)id); eprint("\n");
}
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

int main(void) {
    char *argv[12];
    int argc = uargv(argv, 12), gen_only = 0, flat = 0, seeded = 0, wait = 0, script = 0;
    unsigned seed = 0;
    for (int i = 0; i < argc; i++) {
        if (same(argv[i], "-gen")) gen_only = 1;
        else if (same(argv[i], "-flat")) flat = 1;
        else if (same(argv[i], "-wait")) wait = 1;
        else if (same(argv[i], "-seed") && i + 1 < argc) { seed = parse_u(argv[++i]); seeded = 1; }
        else if (same(argv[i], "-script")) script = 1;
        else if (same(argv[i], "-time") && i + 1 < argc) i++;
        else if (same(argv[i], "-world") && i + 1 < argc) i++;
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
    player_t pl;
    player_spawn(&pl);
    cam_t cam;
    ray_t tg = { 0 };
    fx dx, dy, dz;
    unsigned last = sys_ticks(), fps_t = last;
    int frames = 0, lbtn = 0;
    unsigned short inv_last[B_COUNT] = { 0 };
    for (;;) {
        uwinev_t ev;
        int g, quit = 0;
        while ((g = sys_win_event(win, &ev, 0)) == 1) {
            if (ev.type == WEV_CLOSE || (ev.type == WEV_KEY && ev.key == 27)) quit = 1;
            else if (ev.type == WEV_KEY && ev.key == 'm') eprint("[mincecraft] mark\n");
            else if (ev.type == WEV_KEY && ev.key >= '1' && ev.key <= '9') pl.slot = (int)ev.key - '1';
            else if (ev.type == WEV_WHEEL) pl.slot = ((pl.slot + (int)ev.key) % 9 + 9) % 9;
            else if (ev.type == WEV_KEY && (ev.key == 'r' || ev.key == 'q')) {
                int ids[9], n = hotbar(&pl, ids);
                if (pl.slot < n && player_place(&pl, &tg, ids[pl.slot])) cell_marker("placed", tg.px, tg.py, tg.pz, ids[pl.slot]);
                else eprint("[mincecraft] place refused\n");
                inv_marker(&pl, inv_last);
            }
            else if (ev.type == WEV_DOWN) lbtn = 1;
            else if (ev.type == WEV_UP) lbtn = 0;
        }
        if (g < 0 || quit) break;
        unsigned char k[32];
        if (sys_win_keys(win, k) < 0) for (int i = 0; i < 32; i++) k[i] = 0;
        input_t in = { key_held(k, SC_W) - key_held(k, SC_S), key_held(k, SC_D) - key_held(k, SC_A),
                       key_held(k, SC_RIGHT) - key_held(k, SC_LEFT), key_held(k, SC_UP) - key_held(k, SC_DOWN),
                       key_held(k, SC_SPACE), key_held(k, SC_F) | lbtn };
        unsigned now = sys_ticks();
        for (int n = 0; now - last >= 5 && n < 4; n++, last += 5) {
            player_eye(&pl, &cam);
            cam_forward(&cam, &dx, &dy, &dz);
            ray_cast(cam.x, cam.y, cam.z, dx, dy, dz, 5 << 16, &tg);
            int broke = player_tick(&pl, &in, &tg);
            if (broke) { cell_marker("broke", tg.x, tg.y, tg.z, broke); inv_marker(&pl, inv_last); }
            if (script) {
                char b[96]; int n2 = 0;
                const int v[6] = { pl.x, pl.y, pl.z, pl.yaw, pl.pitch, pl.ground };
                for (const char *t = "[mincecraft] p"; *t; t++) b[n2++] = *t;
                for (int i = 0; i < 6; i++) {
                    unsigned u = v[i] < 0 ? (unsigned)-v[i] : (unsigned)v[i];
                    char d[12]; int m = 0;
                    b[n2++] = ' ';
                    if (v[i] < 0) b[n2++] = '-';
                    do { d[m++] = (char)('0' + u % 10); u /= 10; } while (u);
                    while (m) b[n2++] = d[--m];
                }
                b[n2++] = '\n';
                sys_write(FD_STDERR, b, n2);                      /* one write a line */
            }
        }
        if (now - last >= 5) last = now;                         /* too far behind: let it go */
        player_eye(&pl, &cam);
        cam_forward(&cam, &dx, &dy, &dz);
        ray_cast(cam.x, cam.y, cam.z, dx, dy, dz, 5 << 16, &tg);   /* the crosshair, for r and the HUD */
        render(&cam, body, 256, 0x87BEEB);
        hud_draw(body, &pl, &tg, 600);
        sys_win_put(win, body);
        frames++;
        if (now - fps_t >= 500) { eprint("[mincecraft] fps="); eprint_u((unsigned)(frames * 100 / (int)(now - fps_t))); eprint("\n"); frames = 0; fps_t = now; }
    }
    eprint("[mincecraft] bye\n");
    sys_win_close(win);
    return 0;
}
