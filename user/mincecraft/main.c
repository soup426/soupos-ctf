/* main.c - Mincecraft (v0.60.192): a voxel sandbox in a food world.
 *
 *   cook mincecraft.elf                 play (from a terminal on the desktop)
 *   cook mincecraft.elf -seed N         world N (a new world otherwise)
 *   cook mincecraft.elf -flat           the test world: crust at y 10
 *   cook mincecraft.elf -gen -seed N    make world N, print its hash, end
 *   cook mincecraft.elf -wait ... &     (v0.60.193) wait up to a minute for
 *                                       the desktop, for tests that start it
 *                                       at the console before countertop
 *   cook mincecraft.elf -time T         (v0.60.196) start at time of day T (0
 *                                       midnight, 600 noon, of 1200) and hold it
 *   cook mincecraft.elf -script         (v0.60.194) the player after every tick
 *   cook mincecraft.elf -world PATH     (v0.60.197) the world file; HOME's
 *                                       mincecraft.wld otherwise
 *   cook mincecraft.elf -check          (v0.60.197) load the world file, say
 *                                       what came of it, end (for the tests)
 *
 * W A S D walk, the arrows look (or a right-button drag, v0.60.214), space jumps (all held, read through
 * SYS_WIN_KEYS at a fixed 20 ticks a second); F or the left button held
 * breaks what the crosshair is on (v0.60.195), r or q places the hotbar's
 * block on that face, 1 to 9 or the wheel choose the slot; c crafts from the
 * slot's block (v0.60.215: a stalk to four toast, two parmesan to a cheese
 * grater, a butter to four tealights); P saves, Esc or
 * the close box saves and ends it (a kill does not save); if that save
 * fails it says so and a second Esc ends it unsaved (v0.60.202).
 *
 * Sounds (v0.60.210, through SYS_TONE; none with no sound card): a tick
 * every fourth tick a block is held, its break lower the harder it is, a
 * plop on a place, a splash going into broth.
 *
 * Markers on stderr, [mincecraft] ...: window up, fps=N every 5 s, bye;
 * with -script "p x y z yaw pitch ground" each tick (16.16, angle units),
 * and "mark" on m, so tests can split the log into phases; "broke x y z id",
 * "placed x y z id" or "place refused", and "inv id=n ..." for each change;
 * "saved PATH bytes= hash= x= y= z=", "loaded PATH hash= x= y= z=",
 * "refused PATH" (a damaged file: a new world, the file left until a
 * save), "no world at PATH".
 */
#include "../ulib.h"
#include "world.h"
#include "render.h"
#include "player.h"
#include "hud.h"
#include "save.h"

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
/* Day and night (v0.60.196): 1200 steps a day, one every 20 ticks (a
 * 20-minute day). The light is 256 at noon and 77 (30%) at midnight;
 * the sky goes from 0x87BEEB to 0x0F142D with it. */
static int light_at(int tod) {
    int dn = tod > 600 ? tod - 600 : 600 - tod;
    return 256 - dn * 179 / 600;
}
static unsigned sky_at(int tod) {
    int k = (256 - light_at(tod)) * 256 / 179;                  /* 0 noon .. 256 midnight */
    int r = 0x87 + ((0x0F - 0x87) * k >> 8), g = 0xBE + ((0x14 - 0xBE) * k >> 8), b = 0xEB + ((0x2D - 0xEB) * k >> 8);
    return (unsigned)r << 16 | (unsigned)g << 8 | (unsigned)b;
}
static void pos_marker(const player_t *p) {
    eprint(" x="); eprint_int(p->x); eprint(" y="); eprint_int(p->y); eprint(" z="); eprint_int(p->z);
}
static int do_save(const char *path, const player_t *p, unsigned seed, int tod) {   /* 0, or -1 */
    unsigned n = 0;
    if (save_world(path, p, seed, tod, &n) < 0) { sys_note("Mincecraft: could not save"); eprint("[mincecraft] save failed\n"); return -1; }
    eprint("[mincecraft] saved "); eprint(path); eprint(" bytes="); eprint_u(n); eprint(" hash="); eprint_u(world_hash());
    pos_marker(p); eprint("\n");
    return 0;
}
/* A refused world file is kept as PATH.bad before anything is saved over
 * it (v0.60.202): the cook may want it back. */
static void keep_aside(const char *path) {
    static char dst[176], buf[4096];
    int n = 0, in, out, got, okay = 1;
    while (path[n] && n < 160) { dst[n] = path[n]; n++; }
    for (const char *t = ".bad"; *t; t++) dst[n++] = *t;
    dst[n] = 0;
    if ((in = sys_open(path, O_READ)) < 0) return;
    if ((out = sys_open(dst, O_WRITE)) < 0) { sys_close(in); okay = 0; }
    else {
        while ((got = sys_read(in, buf, sizeof(buf))) > 0) if (sys_write(out, buf, got) != got) { okay = 0; break; }
        sys_close(in);
        if (sys_close(out) < 0) okay = 0;
    }
    eprint(okay ? "[mincecraft] kept " : "[mincecraft] could not keep "); eprint(path); eprint(" as "); eprint(dst); eprint("\n");
}
/* Crafting (v0.60.215): c on the hotbar's block. */
static const struct { int from, need, to, makes; } recipes[] = {
    { B_STALK, 1, B_TOAST, 4 }, { B_PARMESAN, 2, B_GRATER, 1 }, { B_BUTTER, 1, B_TEALIGHT, 4 },
};
static void craft(player_t *p, int id, unsigned short *inv_last) {
    if (id <= 0) { sys_note("Mincecraft: nothing in hand"); eprint("[mincecraft] craft: nothing in hand\n"); return; }
    for (unsigned r = 0; r < sizeof(recipes) / sizeof(recipes[0]); r++) {
        if (recipes[r].from != id) continue;
        if (p->inv[id] < recipes[r].need) {
            eprint("[mincecraft] craft: need "); eprint_u((unsigned)recipes[r].need); eprint(" "); eprint(blocks[id].name); eprint("\n");
            sys_note("Mincecraft: not enough for that");
            return;
        }
        if (p->inv[recipes[r].to] + recipes[r].makes > 999) { sys_note("Mincecraft: no room in the hand"); return; }
        p->inv[id] -= (unsigned short)recipes[r].need;
        p->inv[recipes[r].to] += (unsigned short)recipes[r].makes;
        eprint("[mincecraft] crafted "); eprint_u((unsigned)recipes[r].makes); eprint(" "); eprint(blocks[recipes[r].to].name);
        eprint(" from "); eprint_u((unsigned)recipes[r].need); eprint(" "); eprint(blocks[id].name); eprint("\n");
        sys_note("Mincecraft: crafted");
        sys_tone(660, 50);
        inv_marker(p, inv_last);
        return;
    }
    eprint("[mincecraft] craft: nothing to make from "); eprint(blocks[id].name); eprint("\n");
    sys_note("Mincecraft: nothing to make from that");
}
/* The tealights (v0.60.216): every one in the world, found by a scan when
 * a world comes and when one is placed or broken; the 16 nearest the eye,
 * within 30 blocks, go to render() each frame. */
#define MAX_LAMPS 256
static lamp_t lamps[MAX_LAMPS], near_lamps[16];
static int nlamps;
static void lamps_scan(void) {
    nlamps = 0;
    for (int y = 0; y < WY; y++) for (int z = 0; z < WZ; z++) for (int x = 0; x < WX; x++)
        if (world[(y * WZ + z) * WX + x] == B_TEALIGHT && nlamps < MAX_LAMPS) { lamps[nlamps].x = x; lamps[nlamps].y = y; lamps[nlamps].z = z; nlamps++; }
}
static int lamps_near(const cam_t *c) {
    int n = 0, ex = c->x >> 16, ey = c->y >> 16, ez = c->z >> 16;
    for (int i = 0; i < nlamps && n < 16; i++) {
        int dx = lamps[i].x - ex, dy = lamps[i].y - ey, dz = lamps[i].z - ez;
        if (dx * dx + dy * dy + dz * dz <= 30 * 30) near_lamps[n++] = lamps[i];
    }
    return n;
}
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

int main(void) {
    char *argv[12];
    int argc = uargv(argv, 12), gen_only = 0, flat = 0, seeded = 0, wait = 0, script = 0, tod = 300, hold_time = 0, check = 0, tset = -1;
    const char *wpath = 0;
    unsigned seed = 0;
    for (int i = 0; i < argc; i++) {
        if (same(argv[i], "-gen")) gen_only = 1;
        else if (same(argv[i], "-flat")) flat = 1;
        else if (same(argv[i], "-wait")) wait = 1;
        else if (same(argv[i], "-seed") && i + 1 < argc) { seed = parse_u(argv[++i]); seeded = 1; }
        else if (same(argv[i], "-script")) script = 1;
        else if (same(argv[i], "-time") && i + 1 < argc) { tset = (int)(parse_u(argv[++i]) % 1200); hold_time = 1; }
        else if (same(argv[i], "-world") && i + 1 < argc) wpath = argv[++i];
        else if (same(argv[i], "-check")) check = 1;
    }
    if (world_alloc() < 0) { print("Mincecraft: no memory for the world\n"); return 1; }
    if (!seeded) sys_random(&seed, 4);
    if (gen_only) {
        if (flat) world_flat(); else world_gen(seed);
        eprint("[mincecraft] world seed="); eprint_u(seed);
        eprint(" hash="); eprint_u(world_hash());
        eprint(" top="); eprint_u((unsigned)world_top(64, 64));
        eprint("\n");
        return 0;
    }
    /* The world file: -world, or HOME's mincecraft.wld. */
    static char pbuf[160];
    if (!wpath) {
        const char *home = getenv("HOME");
        if (!home) { print("Mincecraft: no HOME; give -world PATH\n"); return 1; }
        int n = 0;
        while (home[n]) n++;
        if (n > 140) {                                /* (v0.60.209) refused, not cut to another path */
            print("Mincecraft: HOME is too long (140 characters at most); give -world PATH\n");
            eprint("[mincecraft] HOME is too long\n");
            return 1;
        }
        n = 0;
        while (home[n]) { pbuf[n] = home[n]; n++; }
        if (n == 0 || pbuf[n - 1] != '/') pbuf[n++] = '/';
        for (const char *t = "mincecraft.wld"; *t; t++) pbuf[n++] = *t;
        pbuf[n] = 0;
        wpath = pbuf;
    }
    player_t pl;
    int got = load_world(wpath, &pl, &seed, &tod);
    if (got == 0) { eprint("[mincecraft] loaded "); eprint(wpath); eprint(" hash="); eprint_u(world_hash()); pos_marker(&pl); eprint("\n"); }
    else {
        if (got == -2) {
            sys_note("Mincecraft: that world is damaged; a new one");
            eprint("[mincecraft] refused "); eprint(wpath); eprint("\n");
            if (!check) keep_aside(wpath);
        }
        else { eprint("[mincecraft] no world at "); eprint(wpath); eprint("\n"); }
        if (flat) world_flat(); else world_gen(seed);
        player_spawn(&pl);
        eprint("[mincecraft] world seed="); eprint_u(seed); eprint(" hash="); eprint_u(world_hash()); eprint("\n");
    }
    if (check) return got == 0 ? 0 : 1;
    lamps_scan();
    if (tset >= 0) tod = tset;

    int win = sys_win_open(BODY_W, BODY_H, "Mincecraft");
    for (int i = 0; wait && win < 0 && i < 600; i++) { sys_sleep(100); win = sys_win_open(BODY_W, BODY_H, "Mincecraft"); }
    if (win < 0) { print("Mincecraft could not open its window (is the countertop up?)\n"); return 1; }
    unsigned *body = (unsigned *)sys_sbrk(BODY_W * BODY_H * 4);
    if (body == (unsigned *)-1 || render_alloc() < 0) { print("Mincecraft: no memory for the screen\n"); return 1; }
    eprint("[mincecraft] window up\n");
    cam_t cam;
    ray_t tg = { 0 };
    fx dx, dy, dz;
    unsigned last = sys_ticks(), fps_t = last;
    int frames = 0, lbtn = 0, unsaved = 0, rx = 0, ry = 0;
    unsigned short inv_last[B_COUNT] = { 0 };
    inv_marker(&pl, inv_last);                                   /* what a loaded world has in hand */
    unsigned tt = 0;                                             /* game ticks, for the clock */
    for (;;) {
        uwinev_t ev;
        int g, quit = 0;
        while ((g = sys_win_event(win, &ev, 0)) == 1) {
            if (ev.type == WEV_KEY && ev.key != 27 && ev.key != 'm') unsaved = 0;   /* an unsaved warning holds until the next Esc */
            if (ev.type == WEV_CLOSE || (ev.type == WEV_KEY && ev.key == 27)) quit = 1;
            else if (ev.type == WEV_KEY && ev.key == 'm') eprint("[mincecraft] mark\n");
            else if (ev.type == WEV_KEY && ev.key == 'p') do_save(wpath, &pl, seed, tod);
            else if (ev.type == WEV_KEY && ev.key == 'c') { int ids[9], n = hotbar(&pl, ids); craft(&pl, pl.slot < n ? ids[pl.slot] : 0, inv_last); }
            else if (ev.type == WEV_KEY && ev.key >= '1' && ev.key <= '9') pl.slot = (int)ev.key - '1';
            else if (ev.type == WEV_WHEEL) pl.slot = ((pl.slot + (int)ev.key) % 9 + 9) % 9;
            else if (ev.type == WEV_KEY && (ev.key == 'r' || ev.key == 'q')) {
                int ids[9], n = hotbar(&pl, ids);
                if (pl.slot < n && player_place(&pl, &tg, ids[pl.slot])) { cell_marker("placed", tg.px, tg.py, tg.pz, ids[pl.slot]); sys_tone(523, 60); if (ids[pl.slot] == B_TEALIGHT) lamps_scan(); }
                else eprint("[mincecraft] place refused\n");
                inv_marker(&pl, inv_last);
            }
            else if (ev.type == WEV_DOWN) lbtn = 1;
            else if (ev.type == WEV_UP) lbtn = 0;
            else if (ev.type == WEV_RDOWN) { rx = ev.x; ry = ev.y; }       /* look with the mouse (v0.60.214): */
            else if (ev.type == WEV_RMOVE) {                              /* two pixels an angle step */
                pl.yaw = (pl.yaw + (ev.x - rx) / 2) & 1023;
                pl.pitch -= (ev.y - ry) / 2;
                if (pl.pitch > 227) pl.pitch = 227;
                if (pl.pitch < -227) pl.pitch = -227;
                rx += (ev.x - rx) / 2 * 2; ry += (ev.y - ry) / 2 * 2;    /* keep the odd pixel for next time */
            }
        }
        if (g < 0) { do_save(wpath, &pl, seed, tod); break; }   /* the desktop is gone: save if it can, and go */
        if (quit) {                                               /* Esc or the close box: save, then go */
            if (do_save(wpath, &pl, seed, tod) == 0 || unsaved) break;
            unsaved = 1;                                          /* (v0.60.202) not without saying so */
            sys_note("Mincecraft: could not save; Esc again to quit");
            eprint("[mincecraft] unsaved, Esc again to quit\n");
        }
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
            if (!hold_time && ++tt % 20 == 0) tod = (tod + 1) % 1200;
            int was_wet = pl.wet;
            int broke = player_tick(&pl, &in, &tg);
            if (broke) {
                cell_marker("broke", tg.x, tg.y, tg.z, broke); inv_marker(&pl, inv_last);
                if (broke == B_TEALIGHT) lamps_scan();
                int hz = 220 - 4 * blocks[broke].hardness;
                sys_tone((unsigned)(hz < 60 ? 60 : hz), 80);       /* the crunch: lower for harder */
            } else if (pl.bn && pl.bn % 4 == 0) sys_tone(330, 20);  /* breaking: a soft tick */
            if (pl.wet && !was_wet) sys_tone(300, 120);           /* into the broth */
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
        int crack = 0;
        if (tg.hit && pl.bn && tg.x == pl.bx && tg.y == pl.by && tg.z == pl.bz) {
            int hd = blocks[world_get(tg.x, tg.y, tg.z)].hardness;
            if (hd) crack = pl.bn * 256 / hd;
        }
        render(&cam, body, light_at(tod), sky_at(tod), &tg, crack, near_lamps, lamps_near(&cam));
        hud_draw(body, &pl, &tg, tod);
        sys_win_put(win, body);
        frames++;
        /* About 33 frames a second (v0.60.202): the desktop draws the
         * window about 30 times a second, so faster only burns the CPU. */
        unsigned spent = sys_ticks() - now;
        if (spent < 3) sys_sleep((3 - spent) * 10);
        if (now - fps_t >= 500) { eprint("[mincecraft] fps="); eprint_u((unsigned)(frames * 100 / (int)(now - fps_t))); eprint("\n"); frames = 0; fps_t = now; }
    }
    eprint("[mincecraft] bye\n");
    sys_win_close(win);
    return 0;
}
