/* doom.c — Doom title screen + interactive menu for soupOS.
 *
 * Stage 3: PLAYPAL palette + TITLEPIC title screen
 * Stage 4: Main menu / episode / skill selection with animated skull cursor
 *
 * Doom patch_t on-disk format (all little-endian):
 *   int16  width, height, leftoffset, topoffset
 *   uint32 columnofs[width]    -- byte offsets from start of lump data
 * Each column: posts until topdelta == 0xFF
 *   uint8  topdelta, length, _pad, pixels[length], _pad
 */

#include "doom.h"
#include "wad.h"
#include "doomsnd.h"
#include "music.h"
#include "fb.h"
#include "vga.h"
#include "vga13h.h"
#include "keyboard.h"
#include "timer.h"
#include "klog.h"
#include "speaker.h"
#include "heap.h"
#include "str.h"
#include <stdint.h>

/* ── WAD cache ────────────────────────────────────────────────────────────── */

static int     wad_ready  = 0;
static uint8_t playpal[768];          /* first Doom palette, 8-bit RGB */

static int ensure_wad(void) {
    if (wad_ready) return 0;
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  Loading DOOM1.WAD...\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    if (wad_init("DOOM1.WAD") < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("  DOOM1.WAD not found on disk.\n");
        vga_puts("  Copy the shareware WAD to the FAT image and try again.\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return -1;
    }
    if (wad_read_palette(playpal) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("  PLAYPAL lump not found.\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        wad_shutdown(); return -1;
    }
    wad_ready = 1;
    vga_printf("  WAD loaded: %d lumps\n", wad_num_lumps());
    return 0;
}

/* Program the VGA DAC from playpal[] (8-bit → 6-bit >>2) */
static void set_doom_palette(void) {
    for (int i = 0; i < 256; i++)
        vga13h_setpal((uint8_t)i,
                      playpal[i*3+0] >> 2,
                      playpal[i*3+1] >> 2,
                      playpal[i*3+2] >> 2);
}

/* ── Frame profiling (build with PROFILE=1) ───────────────────────────────
 * Diagnostic only: where does a frame's time actually go? Reports to the
 * serial log every PF_EVERY frames, so it can be read headless. */
#ifdef DOOM_PROFILE
static inline uint64_t pf_tsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#define PF_EVERY 50
static uint64_t pf_flats, pf_bsp, pf_things, pf_sprites, pf_bar;
static uint64_t pf_vbl, pf_copy, pf_total;
static uint32_t pf_frames, pf_tick0;
#define PF_T0(v)        uint64_t v = pf_tsc()
#define PF_ACC(acc, v)  do { (acc) += pf_tsc() - (v); } while (0)

/* kilocycles per frame */
static uint32_t pf_kc(uint64_t acc, uint32_t frames) {
    if (!frames) return 0;
    return (uint32_t)(acc / frames / 1000);
}

static void pf_maybe_report(void) {
    if (++pf_frames < PF_EVERY) return;
    uint32_t ticks = timer_get_ticks() - pf_tick0;
    if (ticks == 0) ticks = 1;
    uint32_t f = pf_frames;
    klog("[doomprof] fps=%u frame=%uk flats=%uk bsp=%uk things=%uk "
         "spr=%uk bar=%uk vbl=%uk copy=%uk\n",
         (f * 100u) / ticks,
         pf_kc(pf_total, f), pf_kc(pf_flats, f), pf_kc(pf_bsp, f),
         pf_kc(pf_things, f), pf_kc(pf_sprites, f), pf_kc(pf_bar, f),
         pf_kc(pf_vbl, f), pf_kc(pf_copy, f));
    pf_flats = pf_bsp = pf_things = pf_sprites = pf_bar = 0;
    pf_vbl = pf_copy = pf_total = 0;
    pf_frames = 0;
    pf_tick0  = timer_get_ticks();
}
#else
#define PF_T0(v)        do { } while (0)
#define PF_ACC(acc, v)  do { } while (0)
#define pf_maybe_report()  do { } while (0)
#endif

/* ── Double-buffer ────────────────────────────────────────────────────────── */
static uint8_t g_backbuf[VGA13_W * VGA13_H];

static inline uint8_t vga_inb(uint16_t port) {
    uint8_t r; __asm__ volatile ("inb %1,%0" : "=a"(r) : "Nd"(port)); return r;
}

static void present_frame(void) {
    /* Wait for vertical blank then copy — eliminates tearing/flicker.
     * On a framebuffer boot there is no CRT to chase: 0x3DA's status bits
     * belong to a VGA mode that is not running, and polling them would spin
     * forever. The scaler writes a whole frame at once instead. */
    if (!fb_present()) {
        PF_T0(t_vbl);
        while ( vga_inb(0x3DA) & 8) {}  /* end of any current vblank */
        while (!(vga_inb(0x3DA) & 8)) {}  /* start of the next       */
        PF_ACC(pf_vbl, t_vbl);
    }

    PF_T0(t_copy);
    uint8_t *fb = vga13h_fb();
    for (int i = 0; i < VGA13_W * VGA13_H; i++) fb[i] = g_backbuf[i];
    vga13h_present();
    PF_ACC(pf_copy, t_copy);
}

static void bb_fill_rect(int x, int y, int w, int h, uint8_t c) {
    for (int row = y; row < y + h && row < VGA13_H; row++) {
        uint8_t *p = g_backbuf + row * VGA13_W + x;
        for (int col = 0; col < w && x + col < VGA13_W; col++) p[col] = c;
    }
}

/* ── Core patch renderer ──────────────────────────────────────────────────── */
/*
 * Decode a Doom patch_t into a raw pixel buffer 'out' (out_w × out_h).
 * Transparent pixels (gaps between posts) are left unchanged in 'out'.
 */
static void patch_blit(const uint8_t *data,
                        uint8_t *out, int out_w, int out_h,
                        int ox, int oy) {
    int16_t width  = *(const int16_t *)(data + 0);
    int16_t loffs  = *(const int16_t *)(data + 4);
    int16_t toffs  = *(const int16_t *)(data + 6);
    const uint32_t *colofs = (const uint32_t *)(data + 8);

    int x0 = ox - (int)loffs;
    int y0 = oy - (int)toffs;

    for (int col = 0; col < (int)width; col++) {
        int sx = x0 + col;
        if (sx < 0 || sx >= out_w) continue;
        const uint8_t *post = data + colofs[col];
        while (*post != 0xFF) {
            int top   = *post++;
            int count = *post++;
            post++;                        /* top padding byte */
            for (int i = 0; i < count; i++) {
                int sy = y0 + top + i;
                if ((unsigned)sy < (unsigned)out_h)
                    out[sy * out_w + sx] = *post;
                post++;
            }
            post++;                        /* bottom padding byte */
        }
    }
}

/* Draw to back buffer */
static void draw_patch(const uint8_t *data, int ox, int oy) {
    patch_blit(data, g_backbuf, VGA13_W, VGA13_H, ox, oy);
}

/* ── Background buffer ────────────────────────────────────────────────────── */
/*
 * bg_pixels holds the decoded TITLEPIC (320×200).  It is decoded once and
 * then blitted to the framebuffer at the start of every menu frame so menu
 * patches can be overlaid cleanly without re-reading the WAD.
 */
static uint8_t bg_pixels[VGA13_W * VGA13_H];  /* BSS — zero at start */
static int     bg_loaded = 0;

static void load_bg(void) {
    if (bg_loaded) return;
    int idx = wad_find_lump("TITLEPIC");
    if (idx < 0) return;
    uint32_t sz = wad_lump_size(idx);
    uint8_t *tmp = (uint8_t *)kmalloc(sz);
    if (!tmp) return;
    if (wad_read_lump(idx, tmp, sz) == (int)sz) {
        memset(bg_pixels, 0, sizeof(bg_pixels));
        patch_blit(tmp, bg_pixels, VGA13_W, VGA13_H, 0, 0);
        bg_loaded = 1;
    }
    kfree(tmp);
}

static void blit_bg(void) {
    memcpy(g_backbuf, bg_pixels, VGA13_W * VGA13_H);
}

/* ── Lump-patch helper ────────────────────────────────────────────────────── */
/* Load a named lump and draw it as a patch at (ox, oy). No-op if missing. */
static void draw_lump(const char *name, int ox, int oy) {
    int idx = wad_find_lump(name);
    if (idx < 0) return;
    uint32_t sz = wad_lump_size(idx);
    if (sz == 0 || sz > 256u*1024u) return;
    uint8_t *buf = (uint8_t *)kmalloc(sz);
    if (!buf) return;
    if (wad_read_lump(idx, buf, sz) == (int)sz)
        draw_patch(buf, ox, oy);
    kfree(buf);
}

/* ── Menu definitions ─────────────────────────────────────────────────────── */
/*
 * Positions match the original Doom source (m_menu.c):
 *   MainDef.x = 97, MainDef.y = 64, LINEHEIGHT = 16
 *   EpiDef.x  = 48, EpiDef.y  = 63
 *   NewDef.x  = 48, NewDef.y  = 63
 *   Skull x = menu_x - 43
 */

typedef struct { const char *lump; int x, y; } mitem_t;

#define MAIN_X   97
#define SUB_X    48
#define LH       16   /* line height */

#define N_MAIN    6
static const mitem_t main_items[N_MAIN] = {
    {"M_NEWG",   MAIN_X,  64},
    {"M_OPTION", MAIN_X,  64 + LH},
    {"M_LOADG",  MAIN_X,  64 + LH*2},
    {"M_SAVEG",  MAIN_X,  64 + LH*3},
    {"M_RDTHIS", MAIN_X,  64 + LH*4},
    {"M_QUITG",  MAIN_X,  64 + LH*5},
};

#define N_EP      4
static const mitem_t ep_items[N_EP] = {
    {"M_EPI1", SUB_X, 63},
    {"M_EPI2", SUB_X, 63 + LH},
    {"M_EPI3", SUB_X, 63 + LH*2},
    {"M_EPI4", SUB_X, 63 + LH*3},
};

#define N_SKILL   5
static const mitem_t skill_items[N_SKILL] = {
    {"M_JKILL", SUB_X, 63},
    {"M_ROUGH",  SUB_X, 63 + LH},
    {"M_HURT",   SUB_X, 63 + LH*2},
    {"M_ULTRA",  SUB_X, 63 + LH*3},
    {"M_NMARE",  SUB_X, 63 + LH*4},
};

/* ── Menu state ───────────────────────────────────────────────────────────── */

typedef enum { MS_MAIN = 0, MS_EPISODE, MS_SKILL } menu_state_t;

static menu_state_t ms      = MS_MAIN;
static int          cursor  = 0;
static int          episode = 0;     /* 0-based episode chosen in MS_EPISODE */
static int          skull_f = 0;     /* 0 or 1 — animation frame */
static uint32_t     skull_t = 0;
#define SKULL_TICKS  8               /* flip skull every 8 ticks (80 ms @100Hz) */

/* ── Frame renderer ───────────────────────────────────────────────────────── */

static void draw_frame(void) {
    blit_bg();                          /* TITLEPIC background */
    draw_lump("M_DOOM", 94, 2);         /* always-visible logo  */

    const mitem_t *items;
    int nitems, skull_x;

    switch (ms) {
    default:
    case MS_MAIN:
        items = main_items; nitems = N_MAIN;
        skull_x = MAIN_X - 43;         /* 54 */
        break;
    case MS_EPISODE:
        draw_lump("M_EPISOD", 54, 38);
        items = ep_items; nitems = N_EP;
        skull_x = SUB_X - 43;          /* 5  */
        break;
    case MS_SKILL:
        draw_lump("M_NEWG",  96, 14);
        draw_lump("M_SKILL", 54, 38);
        items = skill_items; nitems = N_SKILL;
        skull_x = SUB_X - 43;
        break;
    }

    for (int i = 0; i < nitems; i++)
        draw_lump(items[i].lump, items[i].x, items[i].y);

    draw_lump(skull_f ? "M_SKULL2" : "M_SKULL1",
              skull_x, items[cursor].y - 5);
    present_frame();
}

/* ── Action helpers ───────────────────────────────────────────────────────── */

/* Show a full-screen patch lump and wait for a key (for help screens). */
static void show_fullscreen(const char *lump_name) {
    int idx = wad_find_lump(lump_name);
    if (idx < 0) return;
    uint32_t sz = wad_lump_size(idx);
    uint8_t *buf = (uint8_t *)kmalloc(sz);
    if (!buf) return;
    if (wad_read_lump(idx, buf, sz) == (int)sz) {
        blit_bg();
        draw_patch(buf, 0, 0);
        present_frame();
        keyboard_flush();
        keyboard_getchar();
    }
    kfree(buf);
}

/* Brief "invalid" feedback: low-pitched beep */
static void beep_invalid(void) {
    speaker_beep(180, 80);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Stage 5 — Level loading + automap renderer
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── On-disk level structures (all little-endian = x86 native) ────────────── */

typedef struct { int16_t x, y; } dvertex_t;     /* VERTEXES: 4 B each  */

typedef struct {                                  /* LINEDEFS: 14 B each */
    uint16_t v1, v2;
    uint16_t flags;
    uint16_t special;
    uint16_t tag;
    uint16_t right_sdef;
    uint16_t left_sdef;
} dlinedef_t;

typedef struct {                                  /* THINGS: 10 B each   */
    int16_t  x, y;
    uint16_t angle;
    uint16_t type;
    uint16_t flags;
} dthing_t;

typedef struct {                                  /* SECTORS: 26 B each  */
    int16_t  floor_h, ceil_h;
    char     floor_flat[8], ceil_flat[8];
    int16_t  light;
    uint16_t special, tag;
} dsector_t;

typedef struct {                                  /* SIDEDEFS: 30 B each */
    int16_t  xoff, yoff;
    char     upper[8], lower[8], mid[8];
    uint16_t sector;
} dsidedef_t;

typedef struct {                                  /* SEGS: 12 B each     */
    uint16_t v1, v2;
    int16_t  angle;    /* BAM angle (unused in renderer) */
    uint16_t linedef, side;
    int16_t  offset;   /* texture U offset from linedef start */
} dseg_t;

typedef struct {                                  /* SSECTORS: 4 B each  */
    uint16_t numsegs, firstseg;
} dssector_t;

typedef struct {                                  /* NODES: 28 B each    */
    int16_t  x, y, dx, dy;
    int16_t  bbox[2][4];   /* [0]=right child bbox, [1]=left child bbox */
    uint16_t child[2];     /* bit 15 set → leaf; low 15 bits = ssector idx */
} dnode_t;

/* Linedef flags */
#define LD_TWOSIDED  0x0004
#define LD_SECRET    0x0020

/* Map lump offsets from the ExMy / MAPxx marker */
#define ML_THINGS    1
#define ML_LINEDEFS  2
#define ML_SIDEDEFS  3
#define ML_VERTEXES  4
#define ML_SEGS      5
#define ML_SSECTORS  6
#define ML_NODES     7
#define ML_SECTORS   8

/* ── Level state ──────────────────────────────────────────────────────────── */

static dvertex_t  *lv_verts   = NULL; static int lv_nverts   = 0;
static dlinedef_t *lv_lines   = NULL; static int lv_nlines   = 0;
static dsector_t  *lv_sectors = NULL; static int lv_nsectors = 0;
static int16_t    *g_sec_base_light = NULL;  /* original light per sector for animation */
static uint32_t    g_dmg_next = 0;           /* timer tick for next floor damage */
static dsidedef_t *lv_sdefs   = NULL; static int lv_nsdefs   = 0;
static dseg_t     *lv_segs    = NULL; static int lv_nsegs    = 0;
static dssector_t *lv_ssects  = NULL; static int lv_nssects  = 0;
static dnode_t    *lv_nodes   = NULL; static int lv_nnodes   = 0;
static dthing_t   *lv_things  = NULL; static int lv_nthings  = 0;
/* Parallel AI state for each thing (allocated when lv_things is). */
typedef struct { uint8_t state; uint8_t atk_cd; int16_t hp; } thing_ai_t;
static thing_ai_t *lv_thing_ai = NULL;

/* Player position × 256 (fixed-point sub-unit).
 * pl_angle: fine view angle 0-127 (128 steps, 2.8125° each; 0=East).
 * pl_dir: coarse 8-direction (= pl_angle / ANG_PER_DIR), for sprite rotation. */
static int32_t pl_x = 0, pl_y = 0;
static int     pl_angle = 0;
static int     pl_dir   = 0;

/* Automap view */
static int32_t am_cx = 0, am_cy = 0;   /* centre in map units */
static int32_t am_scale = 0;            /* pixels per map unit × 256 */

/* ── Coarse direction tables: cos/sin scaled ×128, 8 steps of 45° (movement) */
static const int cos128[8] = { 128,  91,   0, -91, -128, -91,   0,  91 };
static const int sin128[8] = {   0,  91, 128,  91,    0, -91, -128, -91 };

/* ── Fine view angle tables: cos/sin scaled ×1024, 32 steps of 11.25° ──────
 * Index 0 = East (0°), 8 = North (90°), 16 = West, 24 = South.
 * Derived from cos(2πk/32) and sin(2πk/32). */
/* View angles. 128 steps of 2.8125 degrees.
 *
 * This was 32 steps (11.25 degrees each), which made turning visibly jump:
 * the whole world rotated an eighth of a right angle per keypress. The step
 * count is the only thing that changed; everything derived from it goes
 * through ANG_* below, so the turn RATE is unchanged (TURN_DELAY drops from 4
 * ticks to 1, giving 4x as many steps that are each a quarter of the size).
 *
 * Tables are cosine/sine x1024, index 0 = East, increasing counter-clockwise. */
#define ANG_N        128            /* angles in a full turn            */
#define ANG_MASK     (ANG_N - 1)
#define ANG_90       (ANG_N / 4)    /* quarter turn, for strafing       */
#define ANG_270      (ANG_N * 3 / 4)
#define ANG_PER_DIR  (ANG_N / 8)    /* angles per 8-way sprite rotation */
#define ANG_SKY_U    (256 / ANG_N)  /* sky texels per angle step        */

static const int16_t view_cos[ANG_N] = {
     1024,  1023,  1019,  1013,  1004,   993,   980,   964,
      946,   926,   903,   878,   851,   822,   792,   759,
      724,   688,   650,   610,   569,   526,   483,   438,
      392,   345,   297,   249,   200,   150,   100,    50,
        0,   -50,  -100,  -150,  -200,  -249,  -297,  -345,
     -392,  -438,  -483,  -526,  -569,  -610,  -650,  -688,
     -724,  -759,  -792,  -822,  -851,  -878,  -903,  -926,
     -946,  -964,  -980,  -993, -1004, -1013, -1019, -1023,
    -1024, -1023, -1019, -1013, -1004,  -993,  -980,  -964,
     -946,  -926,  -903,  -878,  -851,  -822,  -792,  -759,
     -724,  -688,  -650,  -610,  -569,  -526,  -483,  -438,
     -392,  -345,  -297,  -249,  -200,  -150,  -100,   -50,
        0,    50,   100,   150,   200,   249,   297,   345,
      392,   438,   483,   526,   569,   610,   650,   688,
      724,   759,   792,   822,   851,   878,   903,   926,
      946,   964,   980,   993,  1004,  1013,  1019,  1023
};
static const int16_t view_sin[ANG_N] = {
        0,    50,   100,   150,   200,   249,   297,   345,
      392,   438,   483,   526,   569,   610,   650,   688,
      724,   759,   792,   822,   851,   878,   903,   926,
      946,   964,   980,   993,  1004,  1013,  1019,  1023,
     1024,  1023,  1019,  1013,  1004,   993,   980,   964,
      946,   926,   903,   878,   851,   822,   792,   759,
      724,   688,   650,   610,   569,   526,   483,   438,
      392,   345,   297,   249,   200,   150,   100,    50,
        0,   -50,  -100,  -150,  -200,  -249,  -297,  -345,
     -392,  -438,  -483,  -526,  -569,  -610,  -650,  -688,
     -724,  -759,  -792,  -822,  -851,  -878,  -903,  -926,
     -946,  -964,  -980,  -993, -1004, -1013, -1019, -1023,
    -1024, -1023, -1019, -1013, -1004,  -993,  -980,  -964,
     -946,  -926,  -903,  -878,  -851,  -822,  -792,  -759,
     -724,  -688,  -650,  -610,  -569,  -526,  -483,  -438,
     -392,  -345,  -297,  -249,  -200,  -150,  -100,   -50
};
/* Forward declarations — texture/sprite systems are defined in the Stage 6/7 block below */
static void tex_shutdown(void);
static void tex_init(void);
static void spr_shutdown(void);
static void st_cache_free(void);
static void colormap_free(void);
static void colormap_load(void);
static void flats_free(void);
static void doors_reset(void);
static void walk_trigger(int32_t ox, int32_t oy, int32_t nx, int32_t ny);
static int  find_sector_at(int32_t mx, int32_t my);
static void weapon_free(void);
static void weapon_init(void);
static void lifts_reset(void);
static void lift_activate(int sec, int repeatable);
static void projs_reset(void);

/* Player eye height in map units — defined below, needed early for draw_flats. */
static int32_t pl_eye_z;

/* ── Level load / free ────────────────────────────────────────────────────── */

static void level_free(void) {
    tex_shutdown();
    spr_shutdown();
    st_cache_free();
    colormap_free();
    flats_free();
    if (lv_verts)   { kfree(lv_verts);   lv_verts   = NULL; } lv_nverts   = 0;
    if (lv_lines)   { kfree(lv_lines);   lv_lines   = NULL; } lv_nlines   = 0;
    if (lv_sectors) { kfree(lv_sectors); lv_sectors = NULL; } lv_nsectors = 0;
    if (g_sec_base_light) { kfree(g_sec_base_light); g_sec_base_light = NULL; }
    if (lv_sdefs)   { kfree(lv_sdefs);   lv_sdefs   = NULL; } lv_nsdefs   = 0;
    if (lv_segs)    { kfree(lv_segs);    lv_segs    = NULL; } lv_nsegs    = 0;
    if (lv_ssects)  { kfree(lv_ssects);  lv_ssects  = NULL; } lv_nssects  = 0;
    if (lv_nodes)   { kfree(lv_nodes);   lv_nodes   = NULL; } lv_nnodes   = 0;
    if (lv_things)   { kfree(lv_things);   lv_things   = NULL; } lv_nthings  = 0;
    if (lv_thing_ai) { kfree(lv_thing_ai); lv_thing_ai = NULL; }
    weapon_free();
    lifts_reset();
    doors_reset();
    projs_reset();
}

static int level_load(const char *name) {
    level_free();
    tex_init();       /* load PNAMES + TEXTURE1/2 for this session */
    colormap_load();  /* 32 × 256-byte lighting tables */
    weapon_init();

    int mk = wad_find_lump(name);
    if (mk < 0) return -1;

    /* VERTEXES */
    const wad_lump_t *vl = wad_get_lump(mk + ML_VERTEXES);
    if (!vl || vl->size < 4) return -1;
    lv_nverts = (int)(vl->size / 4);
    lv_verts  = (dvertex_t *)kmalloc(vl->size);
    if (!lv_verts) return -1;
    wad_read_lump(mk + ML_VERTEXES, lv_verts, vl->size);

    /* LINEDEFS */
    const wad_lump_t *ll = wad_get_lump(mk + ML_LINEDEFS);
    if (!ll || ll->size < 14) { level_free(); return -1; }
    lv_nlines = (int)(ll->size / 14);
    lv_lines  = (dlinedef_t *)kmalloc(ll->size);
    if (!lv_lines) { level_free(); return -1; }
    wad_read_lump(mk + ML_LINEDEFS, lv_lines, ll->size);

    /* SIDEDEFS */
    const wad_lump_t *sdl = wad_get_lump(mk + ML_SIDEDEFS);
    if (sdl && sdl->size >= 30) {
        lv_nsdefs = (int)(sdl->size / 30);
        lv_sdefs  = (dsidedef_t *)kmalloc(sdl->size);
        if (lv_sdefs) wad_read_lump(mk + ML_SIDEDEFS, lv_sdefs, sdl->size);
    }

    /* SEGS */
    const wad_lump_t *sgl = wad_get_lump(mk + ML_SEGS);
    if (sgl && sgl->size >= 12) {
        lv_nsegs = (int)(sgl->size / 12);
        lv_segs  = (dseg_t *)kmalloc(sgl->size);
        if (lv_segs) wad_read_lump(mk + ML_SEGS, lv_segs, sgl->size);
    }

    /* SSECTORS */
    const wad_lump_t *ssl = wad_get_lump(mk + ML_SSECTORS);
    if (ssl && ssl->size >= 4) {
        lv_nssects = (int)(ssl->size / 4);
        lv_ssects  = (dssector_t *)kmalloc(ssl->size);
        if (lv_ssects) wad_read_lump(mk + ML_SSECTORS, lv_ssects, ssl->size);
    }

    /* NODES */
    const wad_lump_t *ndl = wad_get_lump(mk + ML_NODES);
    if (ndl && ndl->size >= 28) {
        lv_nnodes = (int)(ndl->size / 28);
        lv_nodes  = (dnode_t *)kmalloc(ndl->size);
        if (lv_nodes) wad_read_lump(mk + ML_NODES, lv_nodes, ndl->size);
    }

    /* SECTORS */
    const wad_lump_t *secl = wad_get_lump(mk + ML_SECTORS);
    if (secl && secl->size >= 26) {
        lv_nsectors = (int)(secl->size / 26);
        lv_sectors  = (dsector_t *)kmalloc(secl->size);
        if (lv_sectors) {
            wad_read_lump(mk + ML_SECTORS, lv_sectors, secl->size);
            g_sec_base_light = (int16_t *)kmalloc((uint32_t)lv_nsectors * sizeof(int16_t));
            if (g_sec_base_light) {
                for (int i = 0; i < lv_nsectors; i++)
                    g_sec_base_light[i] = lv_sectors[i].light;
            }
            g_dmg_next = timer_get_ticks() + 100;
        }
    }

    /* THINGS — keep full array for sprite rendering; find Player 1 start */
    pl_x = pl_y = 0; pl_angle = 0; pl_dir = 0;
    const wad_lump_t *tl = wad_get_lump(mk + ML_THINGS);
    if (tl && tl->size >= 10) {
        lv_nthings = (int)(tl->size / 10);
        lv_things  = (dthing_t *)kmalloc(tl->size);
        if (lv_things) {
            wad_read_lump(mk + ML_THINGS, lv_things, tl->size);
            for (int i = 0; i < lv_nthings; i++) {
                if (lv_things[i].type == 1) {   /* player 1 start */
                    pl_x = (int32_t)lv_things[i].x << 8;
                    pl_y = (int32_t)lv_things[i].y << 8;
                    /* Doom angle (degrees, 0=East) → fine angle (0-31, 32=full) */
                    pl_angle = ((int)lv_things[i].angle * ANG_N / 360) & ANG_MASK;
                    pl_dir   = pl_angle / ANG_PER_DIR;
                    break;
                }
            }
        }
    }
    /* AI state array — parallel to lv_things */
    if (lv_nthings > 0) {
        lv_thing_ai = (thing_ai_t *)kmalloc(
                          (uint32_t)lv_nthings * sizeof(thing_ai_t));
        if (lv_thing_ai) {
            memset(lv_thing_ai, 0, (uint32_t)lv_nthings * sizeof(thing_ai_t));
            /* Assign HP by monster type */
            for (int i = 0; i < lv_nthings; i++) {
                int16_t hp;
                switch (lv_things[i].type) {
                case 3004: hp = 20;   break; /* Zombieman */
                case    9: hp = 30;   break; /* Shotgun Guy */
                case 3001: hp = 60;   break; /* Imp */
                case 3002: case 58: hp = 150; break; /* Demon/Spectre */
                case 3003: hp = 1000; break; /* Baron of Hell */
                case   16: hp = 4000; break; /* Cyberdemon */
                case    7: hp = 3000; break; /* Spider Mastermind */
                default:   hp = 50;   break;
                }
                lv_thing_ai[i].hp = hp;
            }
        }
    }
    return 0;
}

/* ── Automap view ─────────────────────────────────────────────────────────── */

static void am_fit(void) {
    if (!lv_nverts) { am_scale = 256; return; }
    int16_t x0 = lv_verts[0].x, x1 = x0, y0 = lv_verts[0].y, y1 = y0;
    for (int i = 1; i < lv_nverts; i++) {
        if (lv_verts[i].x < x0) x0 = lv_verts[i].x;
        if (lv_verts[i].x > x1) x1 = lv_verts[i].x;
        if (lv_verts[i].y < y0) y0 = lv_verts[i].y;
        if (lv_verts[i].y > y1) y1 = lv_verts[i].y;
    }
    int sx = (x1>x0) ? (280<<8)/(x1-x0) : 256;
    int sy = (y1>y0) ? (180<<8)/(y1-y0) : 256;
    am_scale = (sx < sy) ? sx : sy;
    if (am_scale < 4) am_scale = 4;
}

/* Map coord → screen pixel (Y-flipped: map north = screen up) */
static int amx(int32_t mx) { return VGA13_W/2 + (int)(((mx - am_cx)*am_scale)>>8); }
static int amy(int32_t my) { return VGA13_H/2 - (int)(((my - am_cy)*am_scale)>>8); }

/* ── Automap frame ────────────────────────────────────────────────────────── */
/*
 * Doom PLAYPAL colour indices (approximate but reliable):
 *   0   = black            80  = medium grey
 *   104 = dark grey-green  176 = bright red
 *   168 = olive/yellow     248 = near-white
 */
#define AM_BG      0
#define AM_SOLID  80    /* one-sided (solid) wall      */
#define AM_PASS  104    /* two-sided (passable) line   */
#define AM_SPEC  176    /* special / trigger line      */
#define AM_SECR  168    /* secret sector boundary      */
#define AM_PLAY  248    /* player dot                  */
#define AM_ARRL  176    /* player direction arrow      */

static void draw_automap(void) {
    vga13h_clear(AM_BG);

    for (int i = 0; i < lv_nlines; i++) {
        dlinedef_t *l = &lv_lines[i];
        if (l->v1 >= (uint16_t)lv_nverts || l->v2 >= (uint16_t)lv_nverts) continue;

        uint8_t c;
        if      (l->flags  & LD_SECRET)      c = AM_SECR;
        else if (l->special)                 c = AM_SPEC;
        else if (l->left_sdef == 0xFFFFu)    c = AM_SOLID;
        else                                  c = AM_PASS;

        vga13h_line(amx(lv_verts[l->v1].x), amy(lv_verts[l->v1].y),
                    amx(lv_verts[l->v2].x), amy(lv_verts[l->v2].y), c);
    }

    /* Player: white dot + red direction arrow */
    int px = amx(pl_x >> 8), py = amy(pl_y >> 8);
    int ax = px + (cos128[pl_dir] * 6 >> 7);
    int ay = py - (sin128[pl_dir] * 6 >> 7);  /* screen Y is flipped */
    vga13h_line(px, py, ax, ay, AM_ARRL);
    vga13h_fill_circle(px, py, 2, AM_PLAY);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Stage 6/7 — BSP first-person renderer with portal walls
 *
 * Projection (FOV 90°, PROJ_DIST = 160):
 *   vx = forward distance (×1024),  vy = rightward offset (×1024)
 *   screen_x  = 160 + vy * 160 / vx
 *   screen_y  = VIEW_HALF_H - h_rel * PROJ_DIST * 1024 / vx
 *
 * Occlusion: per-column open span [col_top[x] .. col_bot[x]].
 *   Solid segs close the span; portal segs narrow it (upper/lower steps).
 *   Toggle automap ↔ 3D with TAB.
 * ══════════════════════════════════════════════════════════════════════════ */

#define PROJ_DIST    160
#define VIEW_H       168   /* 3D viewport height; bottom 32 rows = status bar */
#define VIEW_HALF_H   84   /* VIEW_H / 2 — horizon line */
#define CEIL_COL      25   /* palette index for ceiling */
#define FLOOR_COL     55   /* palette index for floor   */

/* Per-column open span (wall occlusion) */
static int16_t col_top[VGA13_W];
static int16_t col_bot[VGA13_W];
static int     cols_open;

/* Per-column sprite clip — updated by portal steps only, NOT by solid wall
 * closures. Solid walls behind sprites must not hide them. */
static int16_t spr_top[VGA13_W];
static int16_t spr_bot[VGA13_W];

/* ── COLORMAP-based diminished lighting ───────────────────────────────────
 * COLORMAP is 34 × 256 bytes: 32 shade levels (0 = full-bright, 31 = black),
 * plus invulnerability (32) and all-black (33) which we ignore.
 * Shade = base (from sector light) + distance attenuation. */
static uint8_t *g_colormap = NULL;   /* 32*256 = 8192 bytes when loaded */

static void colormap_free(void) {
    if (g_colormap) { kfree(g_colormap); g_colormap = NULL; }
}

static void colormap_load(void) {
    colormap_free();
    int idx = wad_find_lump("COLORMAP");
    if (idx < 0) return;
    uint32_t sz = wad_lump_size(idx);
    if (sz < 32u*256u) return;
    g_colormap = (uint8_t *)kmalloc(32u * 256u);
    if (!g_colormap) return;
    wad_read_lump(idx, g_colormap, 32u * 256u);
}

/* Pick shade level (0 = brightest, 31 = darkest) from sector light + distance.
 * vx is forward distance ×1024, sector_light is 0..255. */
static int shade_for(int sector_light, int32_t vx) {
    /* Base shade from sector light: light=255 → base=0, light=0 → base=24 */
    int base = 24 - sector_light * 24 / 255;
    if (base < 0) base = 0;
    /* Distance component: vx/1024 = map units. One shade step every 64 units. */
    int dist = (int)(vx >> (10 + 6));   /* vx / (1024*64) */
    int s = base + dist;
    if (s < 0) s = 0;
    if (s > 31) s = 31;
    return s;
}

static inline uint8_t shade_pix(int shade, uint8_t raw) {
    if (!g_colormap) return raw;
    return g_colormap[shade * 256 + raw];
}

/* ── Flat (floor/ceiling) system ─────────────────────────────────────────
 * Per-sector flat cache: up to MAX_FLAT_CACHE lumps kept in heap.
 * Sky: sectors with F_SKY1 ceiling draw the SKY1 patch scrolled by view angle.
 * draw_flats samples find_sector_at once per row (center column) to pick the
 * correct flat for each band — fixes floor/ceiling disappearing on height
 * transitions without full visplane tracking. */

#define MAX_FLAT_CACHE 32
typedef struct { char name[8]; uint8_t *pixels; } flat_t;
static flat_t  g_flats[MAX_FLAT_CACHE];
static int     g_nflats = 0;

static uint8_t g_sky_pixels[256 * 128];
static int     g_sky_loaded = 0;   /* 0=untried, 1=ok, -1=missing */

static void flat_cache_free(void) {
    for (int i = 0; i < g_nflats; i++) {
        if (g_flats[i].pixels) { kfree(g_flats[i].pixels); g_flats[i].pixels = NULL; }
    }
    g_nflats = 0;
}

static void flats_free(void) {
    flat_cache_free();
    g_sky_loaded = 0;
}

static int flat_is_sky(const char *n) {
    return n[0]=='F' && n[1]=='_' && n[2]=='S' && n[3]=='K' && n[4]=='Y';
}

static uint8_t *flat_get(const char n[8]) {
    for (int i = 0; i < g_nflats; i++)
        if (memcmp(g_flats[i].name, n, 8) == 0) return g_flats[i].pixels;
    if (g_nflats >= MAX_FLAT_CACHE) return NULL;
    char safe[9]; memcpy(safe, n, 8); safe[8] = 0;
    int idx = wad_find_lump(safe);
    if (idx < 0 || wad_lump_size(idx) < 64*64) return NULL;
    uint8_t *p = (uint8_t *)kmalloc(64 * 64);
    if (!p) return NULL;
    if (wad_read_lump(idx, p, 64*64) != 64*64) { kfree(p); return NULL; }
    memcpy(g_flats[g_nflats].name, n, 8);
    g_flats[g_nflats].pixels = p;
    g_nflats++;
    return p;
}

static void sky_ensure(void) {
    if (g_sky_loaded) return;
    int idx = wad_find_lump("SKY1");
    if (idx < 0) { g_sky_loaded = -1; return; }
    uint32_t sz = wad_lump_size(idx);
    uint8_t *tmp = (uint8_t *)kmalloc(sz);
    if (!tmp) { g_sky_loaded = -1; return; }
    if (wad_read_lump(idx, tmp, sz) == (int)sz) {
        memset(g_sky_pixels, 0, sizeof(g_sky_pixels));
        patch_blit(tmp, g_sky_pixels, 256, 128, 0, 0);
        g_sky_loaded = 1;
    }
    kfree(tmp);
}

/* Per-row raycaster. Fills floor/ceiling rows before the BSP wall pass.
 * Samples the sector per-column (with a last-sector cache so BSP walks happen
 * only at sector transitions) so flats and sky are correct across boundaries. */
/* ── Per-row sector runs ──────────────────────────────────────────────────
 *
 * find_sector_at() is a BSP descent followed by four dependent lookups
 * (subsector -> seg -> linedef -> sidedef), roughly 100 cycles. draw_flats
 * used to call it once per floor/ceiling pixel: 53,760 descents per frame,
 * which measured at ~87% of the entire frame.
 *
 * It does not need to. Along one screen row the visible floor lies on a single
 * straight line in world space, so the sector can only change where that line
 * crosses a sector boundary: a handful of times per row at most. Sample every
 * FLAT_STEP pixels, and when two samples disagree, bisect to find the exact
 * pixel where it changed.
 *
 * Exact at every boundary it finds. The one thing it can miss is a sector
 * that both starts and ends inside a single FLAT_STEP window, which for floors
 * means a sliver under 16 pixels wide; that is rare, and the cost of being
 * wrong is one run of flat drawn with a neighbour's texture.
 */
#define FLAT_STEP      16
#define FLAT_MAX_RUNS  48

static int flat_sec_px(int32_t wx0, int32_t wy0,
                       int32_t step_x, int32_t step_y, int sx, int psec) {
    int s = find_sector_at((wx0 + step_x * sx) >> 8,
                           (wy0 + step_y * sx) >> 8);
    if (s < 0 || s >= lv_nsectors)
        s = (psec >= 0 && psec < lv_nsectors) ? psec : -1;
    return s;
}

/* Split one row into runs of constant sector. Run i covers pixels up to and
 * including end[i] and has sector sec[i]. Returns the number of runs. */
static int flat_row_runs(int32_t wx0, int32_t wy0,
                         int32_t step_x, int32_t step_y, int psec,
                         int *end, int *sec) {
    int n    = 0;
    int xa   = 0;
    int seca = flat_sec_px(wx0, wy0, step_x, step_y, 0, psec);

    while (xa < VGA13_W - 1) {
        int xb = xa + FLAT_STEP;
        if (xb > VGA13_W - 1) xb = VGA13_W - 1;
        int secb = flat_sec_px(wx0, wy0, step_x, step_y, xb, psec);
        if (secb == seca) { xa = xb; continue; }   /* same run, keep going */

        /* A boundary lies in (xa, xb]. Narrow it to one pixel: lo always has
         * sector seca, hi always has something else. */
        int lo = xa, hi = xb;
        while (hi - lo > 1) {
            int mid  = (lo + hi) / 2;
            int secm = flat_sec_px(wx0, wy0, step_x, step_y, mid, psec);
            if (secm == seca) lo = mid;
            else            { hi = mid; secb = secm; }
        }
        if (n < FLAT_MAX_RUNS - 1) { end[n] = lo; sec[n] = seca; n++; }
        xa = hi; seca = secb;
    }

    end[n] = VGA13_W - 1; sec[n] = seca; n++;
    return n;
}

static void draw_flats(int psec) {
    sky_ensure();

    int32_t plx_mu = pl_x >> 8;
    int32_t ply_mu = pl_y >> 8;
    int32_t cos_a  = view_cos[pl_angle];
    int32_t sin_a  = view_sin[pl_angle];

    int32_t psec_floor = (psec >= 0 && psec < lv_nsectors) ? lv_sectors[psec].floor_h : (pl_eye_z - 41);
    int32_t psec_ceil  = (psec >= 0 && psec < lv_nsectors) ? lv_sectors[psec].ceil_h  : (pl_eye_z + 40);

    for (int y = 0; y < VIEW_H; y++) {
        int     is_floor;
        int32_t dy_px, h_diff_ref;
        if (y > VIEW_HALF_H) {
            is_floor   = 1; dy_px = y - VIEW_HALF_H;
            h_diff_ref = pl_eye_z - psec_floor;
        } else if (y < VIEW_HALF_H) {
            is_floor   = 0; dy_px = VIEW_HALF_H - y;
            h_diff_ref = psec_ceil - pl_eye_z;
        } else continue;

        if (dy_px <= 0) continue;
        if (h_diff_ref <= 0) h_diff_ref = 1;

        int32_t d = h_diff_ref * PROJ_DIST / dy_px;
        if (d <= 0) continue;

        int32_t dx0    = d * (cos_a - sin_a) / 4;
        int32_t dy0    = d * (sin_a + cos_a) / 4;
        int32_t wx_fp  = (plx_mu << 8) + dx0;
        int32_t wy_fp  = (ply_mu << 8) + dy0;
        int32_t step_x = d *   sin_a  / (PROJ_DIST * 4);
        int32_t step_y = d * (-cos_a) / (PROJ_DIST * 4);

        uint8_t *row = g_backbuf + y * VGA13_W;

        int sky_u_base = ((int)pl_angle * ANG_SKY_U) & 255;
        int sky_v = y * 128 / VIEW_HALF_H;
        if (sky_v > 127) sky_v = 127;

        int last_sec = -999;
        uint8_t *flat = NULL;
        const uint8_t *cmap = NULL;
        int is_sky_here = 0;

        /* Where this row changes sector, found with a few dozen BSP descents
         * instead of one per pixel. */
        int run_end[FLAT_MAX_RUNS], run_sec[FLAT_MAX_RUNS];
        flat_row_runs(wx_fp, wy_fp, step_x, step_y, psec, run_end, run_sec);
        int ri  = 0;
        int sec = run_sec[0];

        for (int sx = 0; sx < VGA13_W; sx++, wx_fp += step_x, wy_fp += step_y) {
            if (sx > run_end[ri]) { ri++; sec = run_sec[ri]; }
            if (sec < 0 || sec >= lv_nsectors) continue;

            if (sec != last_sec) {
                last_sec = sec;
                const dsector_t *s = &lv_sectors[sec];
                const char *fname = is_floor ? s->floor_flat : s->ceil_flat;
                is_sky_here = !is_floor && flat_is_sky(fname);
                if (is_sky_here) {
                    flat = NULL; cmap = NULL;
                } else {
                    flat = flat_get(fname);
                    int shade = shade_for(s->light, d << 10);
                    cmap = g_colormap ? (g_colormap + shade * 256) : NULL;
                }
            }

            if (is_sky_here) {
                if (g_sky_loaded == 1) {
                    int sky_u = (sky_u_base + sx * 64 / VGA13_W) & 255;
                    row[sx] = g_sky_pixels[sky_v * 256 + sky_u];
                }
                continue;
            }

            if (!flat) continue;
            int u = (wx_fp >> 8) & 63;
            int v = (wy_fp >> 8) & 63;
            if (u < 0) u += 64;
            if (v < 0) v += 64;
            uint8_t raw = flat[v * 64 + u];
            row[sx] = cmap ? cmap[raw] : raw;
        }
    }
}

static void spans_reset(void) {
    for (int i = 0; i < VGA13_W; i++) {
        col_top[i] = 0;
        col_bot[i] = VIEW_H - 1;   /* clip to viewport, not full screen */
        spr_top[i] = 0;
        spr_bot[i] = VIEW_H - 1;
    }
    cols_open = VGA13_W;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Texture system
 *
 * Doom textures are composites of one or more patches laid out by TEXTURE1.
 * PNAMES maps patch indices to WAD lump names.
 * We decode each texture into a flat width×height pixel buffer on first use
 * and keep it cached for the session.
 *
 * Layout of TEXTURE1/2:
 *   uint32  numtextures
 *   uint32  offsets[numtextures]      <- from start of lump
 *   At each offset (maptexture_t):
 *     char     name[8]
 *     uint32   masked   (ignored)
 *     int16    width, height
 *     uint32   columndirectory (ignored)
 *     uint16   patchcount
 *     patchcount × mappatch_t (10 bytes each):
 *       int16   originx, originy
 *       uint16  patch    <- index into PNAMES
 *       int16   stepdir, colormap  (ignored)
 * ══════════════════════════════════════════════════════════════════════════ */

/* Integer square root (Babylonian) */
static int32_t isqrt32(int32_t n) {
    if (n <= 0) return 0;
    int32_t x = n, y = 1;
    while (x > y) { x = (x + y) >> 1; y = n / x; }
    return x;
}

/* Case-insensitive 8-byte WAD name comparison */
static int tex_name_eq(const char *a, const char *b) {
    for (int i = 0; i < 8; i++) {
        char ca = (a[i] >= 'a' && a[i] <= 'z') ? (char)(a[i]-32) : a[i];
        char cb = (b[i] >= 'a' && b[i] <= 'z') ? (char)(b[i]-32) : b[i];
        if (ca != cb) return 0;
        if (!ca)      return 1;
    }
    return 1;
}

/* Composite a WAD patch into a texture pixel buffer.
 * Uses originx/originy directly — does NOT subtract the patch's own
 * leftoffset/topoffset (correct for texture compositing per Doom source). */
static void patch_composite(const uint8_t *data,
                             uint8_t *out, int out_w, int out_h,
                             int ox, int oy) {
    int16_t width = *(const int16_t *)(data + 0);
    const uint32_t *colofs = (const uint32_t *)(data + 8);
    for (int col = 0; col < (int)width; col++) {
        int sx = ox + col;
        if (sx < 0 || sx >= out_w) continue;
        const uint8_t *post = data + colofs[col];
        while (*post != 0xFF) {
            int top   = *post++;
            int count = *post++;
            post++;
            for (int i = 0; i < count; i++) {
                int sy = oy + top + i;
                if ((unsigned)sy < (unsigned)out_h)
                    out[sy * out_w + sx] = *post;
                post++;
            }
            post++;
        }
    }
}

#define MAX_TEX  64
typedef struct {
    char     name[8];
    uint8_t *pixels;   /* width × height, row-major */
    int      width, height;
} tex_t;

static tex_t    tex_cache[MAX_TEX];
static int      tex_n      = 0;
static uint8_t *pnames_raw = NULL;
static int      pnames_cnt = 0;
static uint8_t *tex1_raw   = NULL;
static int      tex1_cnt   = 0;
static uint8_t *tex2_raw   = NULL;
static int      tex2_cnt   = 0;

static void tex_shutdown(void) {
    for (int i = 0; i < tex_n; i++)
        if (tex_cache[i].pixels) { kfree(tex_cache[i].pixels); tex_cache[i].pixels = NULL; }
    tex_n = 0;
    if (pnames_raw) { kfree(pnames_raw); pnames_raw = NULL; } pnames_cnt = 0;
    if (tex1_raw)   { kfree(tex1_raw);   tex1_raw   = NULL; } tex1_cnt   = 0;
    if (tex2_raw)   { kfree(tex2_raw);   tex2_raw   = NULL; } tex2_cnt   = 0;
}

static void tex_init(void) {
    tex_shutdown();
    int idx; uint32_t sz;

    idx = wad_find_lump("PNAMES");
    if (idx >= 0 && (sz = wad_lump_size(idx)) >= 4) {
        pnames_raw = (uint8_t *)kmalloc(sz);
        if (pnames_raw && wad_read_lump(idx, pnames_raw, sz) == (int)sz)
            pnames_cnt = (int)*(const uint32_t *)pnames_raw;
        else { kfree(pnames_raw); pnames_raw = NULL; }
    }
    idx = wad_find_lump("TEXTURE1");
    if (idx >= 0 && (sz = wad_lump_size(idx)) >= 4) {
        tex1_raw = (uint8_t *)kmalloc(sz);
        if (tex1_raw && wad_read_lump(idx, tex1_raw, sz) == (int)sz)
            tex1_cnt = (int)*(const uint32_t *)tex1_raw;
        else { kfree(tex1_raw); tex1_raw = NULL; }
    }
    idx = wad_find_lump("TEXTURE2");
    if (idx >= 0 && (sz = wad_lump_size(idx)) >= 4) {
        tex2_raw = (uint8_t *)kmalloc(sz);
        if (tex2_raw && wad_read_lump(idx, tex2_raw, sz) == (int)sz)
            tex2_cnt = (int)*(const uint32_t *)tex2_raw;
        else { kfree(tex2_raw); tex2_raw = NULL; }
    }
}

/* Find or decode texture by 8-byte name.  Returns NULL if "-" or not found. */
static tex_t *tex_get(const char name[8]) {
    if (!name || name[0] == '-') return NULL;

    for (int i = 0; i < tex_n; i++)
        if (tex_name_eq(name, tex_cache[i].name)) return &tex_cache[i];

    if (tex_n >= MAX_TEX || !pnames_raw) return NULL;

    /* Search TEXTURE1 then TEXTURE2 */
    const uint8_t *entry = NULL;
    for (int pass = 0; pass < 2 && !entry; pass++) {
        uint8_t *lump = pass ? tex2_raw  : tex1_raw;
        int      n    = pass ? tex2_cnt  : tex1_cnt;
        if (!lump) continue;
        const uint32_t *offs = (const uint32_t *)(lump + 4);
        for (int i = 0; i < n; i++) {
            const uint8_t *e = lump + offs[i];
            if (tex_name_eq(name, (const char *)e)) { entry = e; break; }
        }
    }
    if (!entry) return NULL;

    int16_t  tw    = *(const int16_t  *)(entry + 12);
    int16_t  th    = *(const int16_t  *)(entry + 14);
    uint16_t nptch = *(const uint16_t *)(entry + 20);
    if (tw <= 0 || th <= 0 || tw > 1024 || th > 1024) return NULL;

    uint8_t *pixels = (uint8_t *)kmalloc((uint32_t)tw * (uint32_t)th);
    if (!pixels) return NULL;
    memset(pixels, 0, (uint32_t)tw * (uint32_t)th);

    const uint8_t *pp = entry + 22;
    for (int pi = 0; pi < (int)nptch; pi++, pp += 10) {
        int16_t  ox   = *(const int16_t  *)(pp + 0);
        int16_t  oy   = *(const int16_t  *)(pp + 2);
        uint16_t pidx = *(const uint16_t *)(pp + 4);
        if (pidx >= (uint16_t)pnames_cnt) continue;

        char pname[9] = {0};
        memcpy(pname, pnames_raw + 4 + (uint32_t)pidx * 8, 8);

        int lidx = wad_find_lump(pname);
        if (lidx < 0) continue;
        uint32_t psz = wad_lump_size(lidx);
        if (psz < 8 || psz > 128u*1024u) continue;
        uint8_t *pbuf = (uint8_t *)kmalloc(psz);
        if (!pbuf) continue;
        if (wad_read_lump(lidx, pbuf, psz) == (int)psz)
            patch_composite(pbuf, pixels, (int)tw, (int)th, (int)ox, (int)oy);
        kfree(pbuf);
    }

    tex_t *t = &tex_cache[tex_n++];
    memcpy(t->name, name, 8);
    t->pixels = pixels;
    t->width  = (int)tw;
    t->height = (int)th;
    return t;
}

/* ══════════════════════════════════════════════════════════════════════════
 * Sprite system
 *
 * Sprites are WAD patches between S_START/S_END.  Lump names follow the
 * pattern XXXXFY where XXXX is the 4-char sprite name, F is the frame
 * letter (A=first), and Y is the rotation (0=any-angle).
 * Each sprite is decoded into a flat W×H pixel buffer; palette index 0
 * is transparent.
 * ══════════════════════════════════════════════════════════════════════════ */

#define MAX_SPRITES 128
typedef struct {
    char     name[8];
    uint8_t *pixels;
    int      width, height;
    int      loffset, toffset;
} sprite_t;

static sprite_t spr_cache[MAX_SPRITES];
static int      spr_n = 0;

static void spr_shutdown(void) {
    for (int i = 0; i < spr_n; i++)
        if (spr_cache[i].pixels) { kfree(spr_cache[i].pixels); spr_cache[i].pixels = NULL; }
    spr_n = 0;
}

static sprite_t *spr_get(const char *name) {
    for (int i = 0; i < spr_n; i++)
        if (tex_name_eq(name, spr_cache[i].name)) return &spr_cache[i];
    if (spr_n >= MAX_SPRITES) return NULL;

    int idx = wad_find_lump(name);
    if (idx < 0) return NULL;
    uint32_t sz = wad_lump_size(idx);
    if (sz < 8 || sz > 64u*1024u) return NULL;
    uint8_t *raw = (uint8_t *)kmalloc(sz);
    if (!raw) return NULL;
    if (wad_read_lump(idx, raw, sz) != (int)sz) { kfree(raw); return NULL; }

    int16_t w  = *(const int16_t *)(raw + 0);
    int16_t h  = *(const int16_t *)(raw + 2);
    int16_t lo = *(const int16_t *)(raw + 4);
    int16_t to = *(const int16_t *)(raw + 6);
    if (w <= 0 || h <= 0 || w > 256 || h > 256) { kfree(raw); return NULL; }

    uint8_t *pixels = (uint8_t *)kmalloc((uint32_t)w * (uint32_t)h);
    if (!pixels) { kfree(raw); return NULL; }
    /* Fill with SPR_TRANSPARENT sentinel (0xFF); patch_blit overwrites only
     * the pixels covered by posts. draw_sprites skips the sentinel so real
     * palette-index-0 pixels still render (previously `if (pix)` dropped them). */
    memset(pixels, 0xFF, (uint32_t)w * (uint32_t)h);
    /* Decode: pass (lo, to) so patch top-left lands at pixel (0,0) */
    patch_blit(raw, pixels, (int)w, (int)h, (int)lo, (int)to);
    kfree(raw);

    sprite_t *s  = &spr_cache[spr_n++];
    memcpy(s->name, name, 8);
    s->pixels  = pixels;
    s->width   = (int)w;
    s->height  = (int)h;
    s->loffset = (int)lo;
    s->toffset = (int)to;
    return s;
}

/* thing type → 4-char sprite prefix */
typedef struct { uint16_t type; const char spr[5]; } thing_info_t;
static const thing_info_t thing_table[] = {
    /* Monsters */
    {3004, "POSS"}, {   9, "SPOS"}, {3001, "TROO"}, {3002, "SARG"},
    {  58, "SARG"}, {3003, "BOSS"}, {  16, "CYBR"}, {   7, "SPID"},
    {  65, "CPOS"}, {  69, "BOS2"}, {  68, "BSPI"}, {  71, "PAIN"},
    {  66, "SKEL"}, {  67, "FATT"}, {  64, "VILE"}, {  63, "ARCH"},
    /* Health / armor */
    {2014, "BON1"}, {2018, "BON2"}, {2011, "STIM"}, {2012, "MEDI"},
    {2013, "SOUL"}, {2019, "ARM2"}, {   5, "BKEY"}, {  40, "BSKU"},
    {  13, "RKEY"}, {  38, "RSKU"}, {   6, "YKEY"}, {  39, "YSKU"},
    /* Weapons */
    {2001, "SHOT"}, {2002, "MGUN"}, {2003, "LAUN"}, {2004, "PLAS"},
    {2005, "CSAW"}, {2006, "BFUG"},
    /* Ammo */
    {2007, "CLIP"}, {2008, "SHEL"}, {2010, "ROCK"}, {2047, "CELL"},
    {2048, "BROK"}, {2049, "SBOX"}, {2046, "BEXP"},
    /* Power-ups */
    {2022, "PINV"}, {2023, "PSTR"}, {2024, "PINS"}, {2025, "SUIT"},
    {2026, "PMAP"}, {2045, "PVIS"}, {  83, "MEGA"},
    /* Decorations */
    {2035, "BAR1"}, {  48, "ELEC"}, {  30, "COL1"}, {  31, "COL2"},
    {  32, "COL3"}, {  33, "COL4"}, {  37, "COL6"}, {  36, "COL5"},
    {  44, "TBLU"}, {  45, "TGRN"}, {  46, "TRED"}, {  55, "SMRT"},
    {  56, "SMBT"}, {  57, "SMGT"}, {  70, "FCAN"}, {  41, "CEYE"},
    {  42, "FSKU"}, {  47, "SMIT"}, {  54, "TRE2"}, {  43, "TRE1"},
    {  25, "POL1"}, {  26, "POL6"}, {  27, "POL4"}, {  28, "POL2"},
    {  29, "POL3"}, {  24, "POL5"}, {  10, "PLAY"}, {  12, "PLAY"},
    {   0, ""    },
};

/* Draw a textured column strip sx from y0..y1 (clamped to open span).
 * u     : texture X already wrapped to [0, tex->width).
 * ytop  : screen Y of wall top (V=0 maps here, including yoff shift).
 * wall_h: screen height of the full wall strip for V mapping. */
static void col_fill_tex(int sx, int y0, int y1,
                          const tex_t *tex, int u,
                          int ytop, int wall_h, int yoff, int shade) {
    if (!tex || wall_h <= 0) return;
    if (y0 < (int)col_top[sx]) y0 = (int)col_top[sx];
    if (y1 > (int)col_bot[sx]) y1 = (int)col_bot[sx];
    if (y0 > y1) return;

    u = u % tex->width; if (u < 0) u += tex->width;

    /* Fixed-point V stepping (×256) to avoid per-pixel division */
    int32_t v_step  = ((int32_t)tex->height << 8) / wall_h;
    int32_t v_fixed = ((int32_t)(y0 - ytop) * (int32_t)tex->height << 8) / wall_h
                    + ((int32_t)yoff << 8);

    const int tw = tex->width;
    const int th = tex->height;
    const int th_mask = th - 1;
    const int th_pow2 = (th > 0) && ((th & th_mask) == 0);
    const uint8_t *cmap = g_colormap ? (g_colormap + shade * 256) : NULL;
    const uint8_t *texcol = tex->pixels + u;   /* column stride = tw */
    uint8_t *fb = g_backbuf + (unsigned)(y0 * VGA13_W + sx);
    if (th_pow2) {
        for (int y = y0; y <= y1; y++, v_fixed += v_step, fb += VGA13_W) {
            int v = (v_fixed >> 8) & th_mask;
            uint8_t raw = texcol[v * tw];
            *fb = cmap ? cmap[raw] : raw;
        }
    } else {
        for (int y = y0; y <= y1; y++, v_fixed += v_step, fb += VGA13_W) {
            int v = (v_fixed >> 8) % th;
            if (v < 0) v += th;
            uint8_t raw = texcol[v * tw];
            *fb = cmap ? cmap[raw] : raw;
        }
    }
}

/* pl_eye_z: forward-declared near top; floor of current sector + 41. */

/* Find the subsector index that contains map point (mx, my) via BSP walk.
 * Returns -1 if the point falls outside mapped geometry. */
static int find_subsector_at(int32_t mx, int32_t my) {
    if (!lv_nnodes && !lv_nssects) return -1;
    int nid = (lv_nnodes > 0) ? (lv_nnodes - 1) : 0x8000;
    while (!(nid & 0x8000)) {
        const dnode_t *nd = &lv_nodes[nid];
        int32_t s = (int32_t)nd->dy * (mx - nd->x)
                  - (int32_t)nd->dx * (my - nd->y);
        /* s == 0 → point on splitter; treat as front (right) side */
        nid = nd->child[(s >= 0) ? 0 : 1];
    }
    int ss = nid & 0x7FFF;
    return (ss >= lv_nssects) ? -1 : ss;
}

/* Find the sector index that contains map point (mx, my) via BSP walk. */
static int find_sector_at(int32_t mx, int32_t my) {
    int ss = find_subsector_at(mx, my);
    if (ss < 0) return -1;
    const dssector_t *ssc = &lv_ssects[ss];
    if (!ssc->numsegs || (int)ssc->firstseg >= lv_nsegs) return -1;
    const dseg_t *sg = &lv_segs[ssc->firstseg];
    if (sg->linedef >= (uint16_t)lv_nlines) return -1;
    const dlinedef_t *ld = &lv_lines[sg->linedef];
    uint16_t sdi = sg->side ? ld->left_sdef : ld->right_sdef;
    if (sdi == 0xFFFFu || sdi >= (uint16_t)lv_nsdefs) return -1;
    return (int)lv_sdefs[sdi].sector;
}

/* ── Projection helpers ──────────────────────────────────────────────────── */

/* Project world point (wx,wy) into view space.
 * Returns vx (forward, ×1024); writes vy (rightward, ×1024) via pointer. */
static int32_t view_project(int32_t wx, int32_t wy, int32_t *vy_out) {
    int32_t dx = wx - (pl_x >> 8);
    int32_t dy = wy - (pl_y >> 8);
    *vy_out = (int32_t)view_sin[pl_angle] * dx - (int32_t)view_cos[pl_angle] * dy;
    return   (int32_t)view_cos[pl_angle] * dx + (int32_t)view_sin[pl_angle] * dy;
}

/* Project a height offset (relative to eye, in map units) to screen Y.
 * h_rel positive = above eye level = higher on screen (lower Y value). */
static int proj_y(int32_t h_rel, int32_t vx) {
    int32_t py = (int32_t)VIEW_HALF_H - h_rel * (int32_t)PROJ_DIST * 1024 / vx;
    if (py < -9999) return -9999;
    if (py >  9999) return  9999;
    return (int)py;
}

/* Map sector light (0..255) to a palette index in the range [40..120]. */
static uint8_t light_to_col(int light) {
    return (uint8_t)(40 + light * 80 / 255);
}

/* ── Per-column fill helpers (clamp to current open span) ───────────────── */

static void col_fill(int sx, int y0, int y1, uint8_t c) {
    /* Clamp to open span */
    if (y0 < (int)col_top[sx]) y0 = (int)col_top[sx];
    if (y1 > (int)col_bot[sx]) y1 = (int)col_bot[sx];
    if (y0 > y1) return;
    uint8_t *fb = g_backbuf + (unsigned)sx;
    for (int y = y0; y <= y1; y++)
        fb[(unsigned)y * VGA13_W] = c;
}

/* ── Seg renderer ────────────────────────────────────────────────────────── */

#define LD_UPPER_UNPEGGED  0x0008
#define LD_LOWER_UNPEGGED  0x0010

static void render_seg_3d(const dseg_t *seg) {
    if (seg->v1 >= (uint16_t)lv_nverts || seg->v2 >= (uint16_t)lv_nverts) return;

    int32_t vy1, vy2;
    int32_t vx1 = view_project(lv_verts[seg->v1].x, lv_verts[seg->v1].y, &vy1);
    int32_t vx2 = view_project(lv_verts[seg->v2].x, lv_verts[seg->v2].y, &vy2);

    #define NP 256
    if (vx1 <= NP && vx2 <= NP) return;
    if (vx1 < NP) { int32_t d=vx2-vx1; vy1=vy1+(vy2-vy1)*(NP-vx1)/d; vx1=NP; }
    if (vx2 < NP) { int32_t d=vx1-vx2; vy2=vy2+(vy1-vy2)*(NP-vx2)/d; vx2=NP; }
    #undef NP

    int sx1 = 160 + (int)(vy1 * PROJ_DIST / vx1);
    int sx2 = 160 + (int)(vy2 * PROJ_DIST / vx2);

    /* Seg U coordinates: offset at v1, offset+length at v2 */
    int32_t sdx = lv_verts[seg->v2].x - lv_verts[seg->v1].x;
    int32_t sdy = lv_verts[seg->v2].y - lv_verts[seg->v1].y;
    int32_t seg_len = isqrt32((int32_t)((uint32_t)(sdx*sdx) + (uint32_t)(sdy*sdy)));
    int32_t u1 = (int32_t)(int16_t)seg->offset;
    int32_t u2 = u1 + seg_len;

    /* Ensure left-to-right; swap U ends to match */
    if (sx1 > sx2) {
        int t=sx1; sx1=sx2; sx2=t;
        int32_t tv=vx1; vx1=vx2; vx2=tv;
        int32_t tu=u1;  u1=u2;   u2=tu;
    }
    if (sx2 < 0 || sx1 >= VGA13_W) return;
    if (sx1 < 0)        sx1 = 0;
    if (sx2 >= VGA13_W) sx2 = VGA13_W - 1;

    /* Look up front/back sectors and sidedefs */
    int front_sec = -1, back_sec = -1, two_sided = 0;
    const dsidedef_t *fsd = NULL, *bsd = NULL;
    uint16_t ld_flags = 0;
    if (seg->linedef < (uint16_t)lv_nlines) {
        const dlinedef_t *ld = &lv_lines[seg->linedef];
        ld_flags = ld->flags;
        uint16_t fsdi = seg->side ? ld->left_sdef  : ld->right_sdef;
        uint16_t bsdi = seg->side ? ld->right_sdef : ld->left_sdef;
        if (fsdi != 0xFFFFu && fsdi < (uint16_t)lv_nsdefs) {
            fsd = &lv_sdefs[fsdi];
            front_sec = (int)fsd->sector;
        }
        if (bsdi != 0xFFFFu && bsdi < (uint16_t)lv_nsdefs) {
            bsd = &lv_sdefs[bsdi];
            back_sec = (int)bsd->sector;
            two_sided = 1;
        }
    }

    /* Sector heights */
    int32_t ff = (front_sec>=0&&front_sec<lv_nsectors) ? lv_sectors[front_sec].floor_h : -64;
    int32_t fc = (front_sec>=0&&front_sec<lv_nsectors) ? lv_sectors[front_sec].ceil_h  :  64;
    int32_t bf = (back_sec >=0&&back_sec <lv_nsectors) ? lv_sectors[back_sec ].floor_h : ff;
    int32_t bc = (back_sec >=0&&back_sec <lv_nsectors) ? lv_sectors[back_sec ].ceil_h  : fc;

    /* Fallback flat colour (used when texture is missing) */
    uint8_t flat_col = (front_sec>=0&&front_sec<lv_nsectors)
                       ? light_to_col(lv_sectors[front_sec].light) : 80;
    int sec_light = (front_sec>=0&&front_sec<lv_nsectors)
                    ? lv_sectors[front_sec].light : 192;

    /* Fetch textures */
    tex_t *mid_tex   = fsd ? tex_get(fsd->mid)   : NULL;
    tex_t *upper_tex = fsd ? tex_get(fsd->upper) : NULL;
    tex_t *lower_tex = fsd ? tex_get(fsd->lower) : NULL;

    int16_t xoff = fsd ? fsd->xoff : 0;
    int16_t yoff = fsd ? fsd->yoff : 0;

    int span = sx2 - sx1 + 1;
    int s    = span - 1;   /* denominator for perspective-correct lerp */

    for (int sx = sx1; sx <= sx2; sx++) {
        if (col_top[sx] > col_bot[sx]) continue;

        /* Perspective-correct interpolation of depth and texture U.
         * Linearly interpolating 1/vx in screen space gives correct results:
         *   vx(t)  = vx1*vx2*s / (vx2*(s-t) + vx1*t)
         *   u(t)   = (u1*vx2*(s-t) + u2*vx1*t) / (vx2*(s-t) + vx1*t)
         * where t = sx - sx1. */
        int32_t vx, u_raw;
        if (s > 0) {
            int      t   = sx - sx1;
            int64_t  den = (int64_t)vx2 * (s - t) + (int64_t)vx1 * t;
            vx    = (den > 0) ? (int32_t)((int64_t)vx1 * vx2 * s / den) : vx1;
            u_raw = (den > 0) ? (int32_t)(((int64_t)u1 * vx2 * (s - t)
                                           + (int64_t)u2 * vx1 * t) / den) : u1;
        } else {
            vx    = vx1;
            u_raw = u1;
        }
        if (vx < 1) vx = 1;

        /* Heights relative to player eye */
        int yfc = proj_y(fc - pl_eye_z, vx);
        int yff = proj_y(ff - pl_eye_z, vx);
        int ybc = proj_y(bc - pl_eye_z, vx);
        int ybf = proj_y(bf - pl_eye_z, vx);

        /* Texture U for this column (perspective-correct + sidedef x-offset) */
        int u_coord = (int)(u_raw + xoff);

        /* Full wall height for V-coordinate mapping */
        int wall_h = yff - yfc;

        int shade = shade_for(sec_light, vx);

        if (!two_sided) {
            /* ── Solid wall ─────────────────────────────────────────── */
            if (mid_tex) {
                /* Pegging: default = top-pegged (V=0 at ceiling).
                 * Lower-unpegged = V=0 at bottom → shift yoff up by surplus. */
                int v_yoff = (int)yoff;
                if ((ld_flags & LD_LOWER_UNPEGGED) && mid_tex && wall_h > 0)
                    v_yoff += mid_tex->height - wall_h;
                col_fill_tex(sx, yfc, yff, mid_tex, u_coord, yfc, wall_h, v_yoff, shade);
            } else {
                col_fill(sx, yfc, yff, flat_col);
            }
            col_top[sx] = (int16_t)(col_bot[sx] + 1);
            cols_open--;

        } else {
            /* ── Portal wall ────────────────────────────────────────── */
            int ceil_clip  = (ybc > yfc) ? ybc : yfc;
            int floor_clip = (ybf < yff) ? ybf : yff;

            /* Upper step */
            if (ybc > yfc) {
                if (upper_tex) {
                    /* Default: texture bottom aligned with back ceiling (step bottom).
                     * UPPER_UNPEGGED: V=0 at front ceiling (natural for doors). */
                    int v_yoff_up = (int)yoff;
                    if (!(ld_flags & LD_UPPER_UNPEGGED))
                        v_yoff_up += (fc - bc) - upper_tex->height;
                    col_fill_tex(sx, yfc, ybc-1, upper_tex, u_coord,
                                 yfc, wall_h, v_yoff_up, shade);
                } else {
                    col_fill(sx, yfc, ybc-1,
                             flat_col > 20 ? flat_col-20 : 0);
                }
            }
            if (col_top[sx] < ceil_clip) col_top[sx] = (int16_t)ceil_clip;
            if (spr_top[sx] < ceil_clip) spr_top[sx] = (int16_t)ceil_clip;

            /* Lower step */
            if (ybf < yff) {
                if (lower_tex) {
                    /* Default: V=0 at back floor (top of step — Doom's natural pegging).
                     * Our V-math has V=0 at front ceiling, so shift by (fc - bf) tex px.
                     * LOWER_UNPEGGED: V=0 at front ceiling — no shift (texture flows
                     * continuously with the ceiling texture). */
                    int v_yoff = (int)yoff;
                    if (!(ld_flags & LD_LOWER_UNPEGGED) && wall_h > 0)
                        v_yoff += (fc - bf);
                    col_fill_tex(sx, ybf+1, yff, lower_tex, u_coord,
                                 yfc, wall_h, v_yoff, shade);
                } else {
                    col_fill(sx, ybf+1, yff,
                             flat_col > 20 ? flat_col-20 : 0);
                }
            }
            if (col_bot[sx] > floor_clip - 1)
                col_bot[sx] = (int16_t)(floor_clip - 1);
            if (spr_bot[sx] > floor_clip - 1)
                spr_bot[sx] = (int16_t)(floor_clip - 1);

            if (col_top[sx] > col_bot[sx]) cols_open--;
        }
    }
}

/* ── BSP traversal ───────────────────────────────────────────────────────── */

static void snap_vissprites(int ss);   /* forward decl */

static void bsp_traverse(int node_id) {
    if (cols_open <= 0) return;

    if (node_id & 0x8000) {
        int ss = node_id & 0x7FFF;
        if (ss >= lv_nssects) return;
        /* Snapshot before rendering this subsector's segs so sprites living
         * here clip against the spans as they were when we arrived (not after
         * this subsector's portals narrow them further). */
        snap_vissprites(ss);
        const dssector_t *ssc = &lv_ssects[ss];
        for (int i = 0; i < (int)ssc->numsegs; i++) {
            int si = (int)ssc->firstseg + i;
            if (si < lv_nsegs) render_seg_3d(&lv_segs[si]);
        }
        return;
    }

    const dnode_t *nd = &lv_nodes[node_id];
    int32_t px = pl_x >> 8, py = pl_y >> 8;
    int32_t s = (int32_t)nd->dy * (px - nd->x)
              - (int32_t)nd->dx * (py - nd->y);
    int front = (s >= 0) ? 0 : 1;   /* on-splitter → front for consistency */

    bsp_traverse(nd->child[front]);
    bsp_traverse(nd->child[front ^ 1]);
}

/* ── Player game state ───────────────────────────────────────────────────── */
static int pl_health  = 100;
static int pl_armor   = 0;
static int pl_ammo    = 50;
static int pl_fire_cd = 0;   /* weapon fire cooldown in ticks */
static int pl_keys    = 0;   /* bitmask: KEY_BLUE=1, KEY_YELLOW=2, KEY_RED=4 */
static int g_level_exit = 0; /* set when player crosses/uses an exit linedef */

#define KEY_BLUE   1
#define KEY_YELLOW 2
#define KEY_RED    4

/* ── Enemy projectiles ────────────────────────────────────────────────────── */
#define MAX_PROJS    32
#define PROJ_SPD      8    /* map units per frame */
#define PROJ_HIT_R   20    /* hit radius in map units */
#define PROJ_COL    176    /* palette index — orange/red in Doom's PLAYPAL */

typedef struct {
    int32_t  x, y;   /* map units */
    int32_t  vx, vy; /* map units per frame */
    uint8_t  dmg;
    uint8_t  active;
    uint16_t life;   /* frames remaining */
} proj_t;

static proj_t g_projs[MAX_PROJS];

static void projs_reset(void) {
    for (int i = 0; i < MAX_PROJS; i++) g_projs[i].active = 0;
}

static void spawn_proj(int32_t sx, int32_t sy, uint8_t dmg) {
    int32_t px = pl_x >> 8, py = pl_y >> 8;
    int32_t dx = px - sx, dy = py - sy;
    int32_t dist = dx*dx + dy*dy;
    if (dist == 0) return;
    /* isqrt32 is defined later; approximate with shift for speed */
    int32_t d = 1;
    int32_t tmp = dist;
    while (tmp > 1) { tmp >>= 2; d <<= 1; }
    /* Newton step to refine */
    d = (d + dist/d) >> 1;
    d = (d + dist/d) >> 1;
    if (d == 0) return;
    for (int i = 0; i < MAX_PROJS; i++) {
        if (g_projs[i].active) continue;
        g_projs[i].x      = sx;
        g_projs[i].y      = sy;
        g_projs[i].vx     = dx * PROJ_SPD / d;
        g_projs[i].vy     = dy * PROJ_SPD / d;
        g_projs[i].dmg    = dmg;
        g_projs[i].active = 1;
        g_projs[i].life   = 250;
        return;
    }
}

static void projs_tick(void) {
    int32_t px = pl_x >> 8, py = pl_y >> 8;
    for (int i = 0; i < MAX_PROJS; i++) {
        if (!g_projs[i].active) continue;
        g_projs[i].x += g_projs[i].vx;
        g_projs[i].y += g_projs[i].vy;
        if (g_projs[i].life-- == 0) { g_projs[i].active = 0; continue; }
        int32_t dx = g_projs[i].x - px;
        int32_t dy = g_projs[i].y - py;
        if (dx*dx + dy*dy <= PROJ_HIT_R * PROJ_HIT_R) {
            pl_health -= g_projs[i].dmg;
            if (pl_health < 0) pl_health = 0;
            g_projs[i].active = 0;
        }
    }
}

/* Draw active projectiles into g_backbuf using 3D projection */
static void projs_draw(void) {
    int32_t px = pl_x >> 8, py = pl_y >> 8;
    for (int i = 0; i < MAX_PROJS; i++) {
        if (!g_projs[i].active) continue;
        int32_t wx = g_projs[i].x - px;
        int32_t wy = g_projs[i].y - py;
        /* Project into view space (×1024 fixed) */
        int32_t vx = (int32_t)view_cos[pl_angle] * wx + (int32_t)view_sin[pl_angle] * wy;
        int32_t vy = (int32_t)view_sin[pl_angle] * wx - (int32_t)view_cos[pl_angle] * wy;
        if (vx <= 256) continue; /* behind near plane */
        int sx = 160 + (int)((int64_t)vy * PROJ_DIST / vx);
        if (sx < 1 || sx >= VGA13_W - 1) continue;
        /* Fireball flies at player eye height → screen center */
        int sy = VIEW_HALF_H;
        /* 3×3 dot */
        for (int dy2 = -1; dy2 <= 1; dy2++) {
            int row = sy + dy2;
            if (row < 0 || row >= VIEW_H) continue;
            g_backbuf[row * VGA13_W + sx - 1] = PROJ_COL;
            g_backbuf[row * VGA13_W + sx    ] = PROJ_COL;
            g_backbuf[row * VGA13_W + sx + 1] = PROJ_COL;
        }
    }
}

/* ── Status bar ──────────────────────────────────────────────────────────── */
/* Coordinates from Doom source (st_stuff.h).
 * Numbers are right-justified: the given x is the RIGHT edge of the field. */
#define ST_Y        168
#define ST_AMMOX     44
#define ST_AMMOY    171
#define ST_HEALTHX   90
#define ST_HEALTHY  171
#define ST_ARMORX   221
#define ST_ARMORY   171
#define ST_FACESX   143
#define ST_FACESY   168

/* Cached status-bar patches — avoids re-reading from disk every frame. */
static uint8_t *st_digit[10] = {0};
static uint32_t st_digit_sz[10] = {0};
static uint8_t *st_percent   = NULL;
static uint8_t *st_bar       = NULL;
static uint8_t *st_face      = NULL;

static uint8_t *load_patch_cached(const char *name, uint32_t *out_sz) {
    int idx = wad_find_lump(name);
    if (idx < 0) return NULL;
    uint32_t sz = wad_lump_size(idx);
    if (sz < 8 || sz > 64u*1024u) return NULL;
    uint8_t *buf = (uint8_t *)kmalloc(sz);
    if (!buf) return NULL;
    if (wad_read_lump(idx, buf, sz) != (int)sz) { kfree(buf); return NULL; }
    if (out_sz) *out_sz = sz;
    return buf;
}

static void st_cache_init(void) {
    if (st_bar) return;   /* already cached */
    char name[8] = "STTNUM0";
    for (int d = 0; d < 10; d++) {
        name[6] = (char)('0' + d);
        st_digit[d] = load_patch_cached(name, &st_digit_sz[d]);
    }
    st_percent = load_patch_cached("STTPRCNT", NULL);
    st_bar     = load_patch_cached("STBAR",    NULL);
    st_face    = load_patch_cached("STFST00",  NULL);
}

static void st_cache_free(void) {
    for (int d = 0; d < 10; d++) {
        if (st_digit[d]) { kfree(st_digit[d]); st_digit[d] = NULL; }
        st_digit_sz[d] = 0;
    }
    if (st_percent) { kfree(st_percent); st_percent = NULL; }
    if (st_bar)     { kfree(st_bar);     st_bar     = NULL; }
    if (st_face)    { kfree(st_face);    st_face    = NULL; }
}

/* Draw integer n right-justified in a 'digits'-wide field ending at (rx,y). */
static void st_number(int n, int rx, int y, int digits) {
    if (n < 0)   n = 0;
    if (n > 999) n = 999;
    int x = rx;
    for (int d = 0; d < digits; d++) {
        uint8_t *buf = st_digit[n % 10];
        n /= 10;
        if (!buf) break;
        int16_t w = *(const int16_t *)buf;
        if (w > 0 && w < 64) x -= (int)w;
        draw_patch(buf, x, y);
    }
}

static void draw_statusbar(void) {
    st_cache_init();
    if (st_bar)  draw_patch(st_bar,  0,         ST_Y);
    if (st_face) draw_patch(st_face, ST_FACESX, ST_FACESY);
    st_number(pl_health, ST_HEALTHX, ST_HEALTHY, 3);
    st_number(pl_armor,  ST_ARMORX,  ST_ARMORY,  3);
    st_number(pl_ammo,   ST_AMMOX,   ST_AMMOY,   3);
    if (st_percent) {
        draw_patch(st_percent, ST_HEALTHX+1, ST_HEALTHY);
        draw_patch(st_percent, ST_ARMORX+1,  ST_ARMORY);
    }
}

/* ── Collision detection ─────────────────────────────────────────────────── */
#define PLAYER_RADIUS  16
#define LD_BLOCKING  0x0001

static int ld_blocks(int i) {
    const dlinedef_t *l = &lv_lines[i];
    if (l->left_sdef == 0xFFFFu) return 1;   /* one-sided wall */
    if (l->flags & LD_BLOCKING)  return 1;
    if (!lv_sdefs || !lv_sectors) return 0;
    /* Block if the portal opening is too narrow for the player to fit. This
     * prevents walking through closed doors (ceil_h ≈ floor_h + 4). */
    int sr = lv_sdefs[l->right_sdef].sector;
    int sl = lv_sdefs[l->left_sdef ].sector;
    if (sr < 0 || sr >= lv_nsectors || sl < 0 || sl >= lv_nsectors) return 0;
    int16_t open_top = lv_sectors[sr].ceil_h  < lv_sectors[sl].ceil_h
                       ? lv_sectors[sr].ceil_h  : lv_sectors[sl].ceil_h;
    int16_t open_bot = lv_sectors[sr].floor_h > lv_sectors[sl].floor_h
                       ? lv_sectors[sr].floor_h : lv_sectors[sl].floor_h;
    return (open_top - open_bot) < 56;
}

/* Squared distance from point (px,py) to segment (A,B). 1-unit rounding in
 * the projection is fine against PLAYER_RADIUS=16. */
static int64_t point_seg_dist2(int32_t px, int32_t py,
                                int32_t ax, int32_t ay,
                                int32_t bx, int32_t by) {
    int64_t dx = bx - ax, dy = by - ay;
    int64_t len2 = dx*dx + dy*dy;
    int64_t ex, ey;
    if (len2 == 0) {
        ex = px - ax; ey = py - ay;
    } else {
        int64_t t = (int64_t)(px - ax) * dx + (int64_t)(py - ay) * dy;
        if (t < 0)       t = 0;
        else if (t > len2) t = len2;
        int32_t cx = (int32_t)(ax + t * dx / len2);
        int32_t cy = (int32_t)(ay + t * dy / len2);
        ex = px - cx; ey = py - cy;
    }
    return ex*ex + ey*ey;
}

/* 1 if placing the player at (px,py) would clip into blocking linedef i. */
static int hits_ld(int i, int32_t px, int32_t py) {
    if (!ld_blocks(i)) return 0;
    const dlinedef_t *l = &lv_lines[i];
    int32_t ax = lv_verts[l->v1].x, ay = lv_verts[l->v1].y;
    int32_t bx = lv_verts[l->v2].x, by = lv_verts[l->v2].y;

    /* Bbox reject expanded by PLAYER_RADIUS */
    int32_t R = PLAYER_RADIUS;
    int32_t xmin = (ax < bx ? ax : bx) - R;
    int32_t xmax = (ax > bx ? ax : bx) + R;
    int32_t ymin = (ay < by ? ay : by) - R;
    int32_t ymax = (ay > by ? ay : by) + R;
    if (px < xmin || px > xmax || py < ymin || py > ymax) return 0;

    int64_t d2 = point_seg_dist2(px, py, ax, ay, bx, by);
    return d2 < (int64_t)R * R;
}

static int hits_any_ld(int32_t px, int32_t py) {
    for (int i = 0; i < lv_nlines; i++)
        if (hits_ld(i, px, py)) return 1;
    return 0;
}

/* Attempt movement (dx,dy) in fixed-point (×256 map units).
 * Falls back to wall-slide (X-only then Y-only) on collision.
 * Returns 1 if any movement happened. */
static int try_move(int32_t dx, int32_t dy) {
    int32_t ox = pl_x >> 8, oy = pl_y >> 8;
    int32_t nx = (pl_x + dx) >> 8;
    int32_t ny = (pl_y + dy) >> 8;

    if (!hits_any_ld(nx, ny)) {
        pl_x += dx; pl_y += dy;
        walk_trigger(ox, oy, pl_x >> 8, pl_y >> 8);
        return 1;
    }

    /* Slide along wall: try each axis independently */
    int moved = 0;
    if (!hits_any_ld(nx, oy)) {
        pl_x += dx; moved = 1;
        walk_trigger(ox, oy, pl_x >> 8, oy);
    }
    if (!hits_any_ld(ox, ny)) {
        pl_y += dy; moved = 1;
        walk_trigger(ox, oy, ox, pl_y >> 8);
    }
    return moved;
}

/* ── Pickup system ───────────────────────────────────────────────────────── */

static void try_pickups(void) {
    if (!lv_things) return;
    int32_t px = pl_x >> 8, py = pl_y >> 8;
    for (int i = 0; i < lv_nthings; i++) {
        dthing_t *th = &lv_things[i];
        if (!th->type) continue;
        int32_t dx = (int32_t)th->x - px;
        int32_t dy = (int32_t)th->y - py;
        if (dx*dx + dy*dy > 32*32) continue;
        int took = 1;
        switch (th->type) {
        case 2014: if (pl_health < 200) pl_health++;                                     else took=0; break;
        case 2011: if (pl_health < 100) { pl_health += 10; if (pl_health>100) pl_health=100; } else took=0; break;
        case 2012: if (pl_health < 100) { pl_health += 25; if (pl_health>100) pl_health=100; } else took=0; break;
        case 2013: pl_health += 100; if (pl_health > 200) pl_health = 200;               break;
        case 2018: if (pl_armor  < 200) pl_armor++;                                      else took=0; break;
        case 2019: pl_armor = 200;                                                        break;
        case 2007: pl_ammo += 10; if (pl_ammo > 200) pl_ammo = 200;                     break;
        case 2048: pl_ammo += 50; if (pl_ammo > 200) pl_ammo = 200;                     break;
        case 2008: pl_ammo += 4;  if (pl_ammo > 200) pl_ammo = 200;                     break;
        case 2049: pl_ammo += 20; if (pl_ammo > 200) pl_ammo = 200;                     break;
        case 2001: pl_ammo += 4;  if (pl_ammo > 200) pl_ammo = 200;                     break;
        case 2002: pl_ammo += 20; if (pl_ammo > 200) pl_ammo = 200;                     break;
        /* Keys */
        case  5: case 40: pl_keys |= KEY_BLUE;   break;  /* blue keycard / blue skull */
        case 13: case 38: pl_keys |= KEY_RED;    break;  /* red keycard  / red skull  */
        case  6: case 39: pl_keys |= KEY_YELLOW; break;  /* yellow keycard / yellow skull */
        /* Other weapons/powerups: collect silently */
        case 2003: case 2004: case 2005: case 2006:
        case 2022: case 2023: case 2024: case 2025: case 2026: case 2045: case 83:
            break;
        default: took = 0; break;
        }
        if (took) th->type = 0;
    }
}

/* ── Enemy AI ────────────────────────────────────────────────────────────── */
#define AI_IDLE  0
#define AI_CHASE 1
#define AI_DEAD  2
/* Squared activation radius. Was 512*512, which is why the AI looked dead:
 * in E1M1 the nearest monster spawns ~816 units from the player start, so
 * nothing ever woke unless you walked right up to it. That is the whole of
 * the "enemies never move toward player" report. */
#define ENEMY_SEE_D2  (1024*1024)
/* Max range for the ranged hitscan. Without this an awake monster shoots the
 * player from anywhere on the map, through walls, because there is no
 * line-of-sight test: widening SEE_D2 alone killed the player in ~1 second.
 * Chasing still happens at any distance; only shooting is bounded. */
#define ENEMY_RNG_D2  (768*768)
#define ENEMY_MEL_D2  (72*72)     /* squared melee attack range */
#define ENEMY_SPD     4           /* map units moved per think call */
#define ENEMY_ATK_CD  50          /* think calls between attacks */

static int is_monster_type(uint16_t t) {
    switch (t) {
    case 3004: case 9:  case 3001: case 3002: case 58:
    case 3003: case 16: case 7:   case 65:   case 69:
    case 68:   case 71: case 66:  case 67:   case 64: case 63:
        return 1;
    }
    return 0;
}

/* ── Line of sight ────────────────────────────────────────────────────────
 * Without this, monsters shot the player through solid walls: nothing ever
 * tested visibility, so every woken monster in range had a clear shot from
 * anywhere. Combined with waking on proximity alone, the whole map converged
 * on the player and killed them in about a second.
 *
 * Only one-sided linedefs block. A linedef with a back sidedef is an opening
 * between two sectors, so we see through it. That ignores closed doors, which
 * is a known simplification: a monster can see through a shut door, but it
 * cannot shoot through a wall, which is the part that mattered. */
static int seg_crosses(int32_t ax, int32_t ay, int32_t bx, int32_t by,
                       int32_t cx, int32_t cy, int32_t dx, int32_t dy) {
    /* int64 throughout: map coordinates are int16 but the cross products of
     * their differences overflow int32 (see CLAUDE.md pitfall 6). */
    int64_t d1 = (int64_t)(bx-ax)*(cy-ay) - (int64_t)(by-ay)*(cx-ax);
    int64_t d2 = (int64_t)(bx-ax)*(dy-ay) - (int64_t)(by-ay)*(dx-ax);
    int64_t d3 = (int64_t)(dx-cx)*(ay-cy) - (int64_t)(dy-cy)*(ax-cx);
    int64_t d4 = (int64_t)(dx-cx)*(by-cy) - (int64_t)(dy-cy)*(bx-cx);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

static int los_clear(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    if (!lv_lines || !lv_verts) return 1;
    for (int i = 0; i < lv_nlines; i++) {
        const dlinedef_t *ld = &lv_lines[i];
        if (ld->v1 >= lv_nverts || ld->v2 >= lv_nverts) continue;
        if (!seg_crosses(x1, y1, x2, y2,
                         lv_verts[ld->v1].x, lv_verts[ld->v1].y,
                         lv_verts[ld->v2].x, lv_verts[ld->v2].y))
            continue;

        /* One-sided line: solid wall, always blocks. */
        if (ld->left_sdef == 0xFFFF) return 0;

        /* Two-sided: this is a gap between sectors, so it blocks only when
         * the gap is shut. Doors are animated by moving the sector ceiling,
         * so comparing live heights makes a closed door opaque and an open
         * one transparent for free. Same test the renderer uses to decide
         * whether a portal is passable. */
        if (!lv_sdefs || !lv_sectors) return 0;
        if (ld->right_sdef >= lv_nsdefs || ld->left_sdef >= lv_nsdefs) return 0;
        uint16_t fs = lv_sdefs[ld->right_sdef].sector;
        uint16_t bs = lv_sdefs[ld->left_sdef].sector;
        if (fs >= lv_nsectors || bs >= lv_nsectors) return 0;

        int32_t fc = lv_sectors[fs].ceil_h,  bc = lv_sectors[bs].ceil_h;
        int32_t ff = lv_sectors[fs].floor_h, bf = lv_sectors[bs].floor_h;
        int32_t top = (fc < bc) ? fc : bc;      /* lowest ceiling  */
        int32_t bot = (ff > bf) ? ff : bf;      /* highest floor   */
        if (top - bot <= 0) return 0;           /* shut: no sight through */
    }
    return 1;
}

static void thing_think(void) {
    if (!lv_things || !lv_thing_ai) return;
    int32_t px = pl_x >> 8, py = pl_y >> 8;
    for (int i = 0; i < lv_nthings; i++) {
        dthing_t   *th = &lv_things[i];
        thing_ai_t *ai = &lv_thing_ai[i];
        if (!th->type || !is_monster_type(th->type)) continue;

        /* Tick death animation; remove sprite when sequence finishes */
        if (ai->state == AI_DEAD) {
            if (ai->atk_cd++ >= 48) th->type = 0;
            continue;
        }

        int32_t dx = px - (int32_t)th->x;
        int32_t dy = py - (int32_t)th->y;
        /* int64 like fire_weapon: dx and dy span the map, so dx*dx + dy*dy
         * overflows int32 for distant things and can come out negative,
         * which would make the "is the player near?" test pass. */
        int64_t d2 = (int64_t)dx*dx + (int64_t)dy*dy;

        /* Wake up when player is near */
        if (ai->state == AI_IDLE && d2 <= ENEMY_SEE_D2 &&
            los_clear((int32_t)th->x, (int32_t)th->y, px, py))
            ai->state = AI_CHASE;
        if (ai->state != AI_CHASE) continue;

        /* Cooldown tick */
        if (ai->atk_cd > 0) ai->atk_cd--;

        /* Melee attack */
        if (d2 <= ENEMY_MEL_D2 && ai->atk_cd == 0) {
            pl_health -= 10;
            if (pl_health < 0) pl_health = 0;
            ai->atk_cd = ENEMY_ATK_CD;
        }

        /* Ranged attack: Imp fires fireball, zombies hitscan */
        if (d2 > ENEMY_MEL_D2 && d2 <= ENEMY_RNG_D2 && ai->atk_cd == 0 &&
            los_clear((int32_t)th->x, (int32_t)th->y, px, py)) {
            if (th->type == 3001) {
                spawn_proj((int32_t)th->x, (int32_t)th->y, 8);
            } else if (th->type == 3004) {
                /* Zombieman hitscan */
                pl_health -= 10;
                if (pl_health < 0) pl_health = 0;
            } else if (th->type == 9) {
                /* Shotgun Guy hitscan */
                pl_health -= 15;
                if (pl_health < 0) pl_health = 0;
            }
            if (th->type == 3001 || th->type == 3004 || th->type == 9)
                ai->atk_cd = ENEMY_ATK_CD;
        }

        /* Chase: move toward player if not in melee range */
        if (d2 > ENEMY_MEL_D2) {
            int32_t dist = isqrt32((uint32_t)(d2 > 0x7FFFFFFF ? 0x7FFFFFFF : d2));
            if (dist > 0) {
                th->x = (int16_t)((int32_t)th->x + dx * ENEMY_SPD / dist);
                th->y = (int16_t)((int32_t)th->y + dy * ENEMY_SPD / dist);
            }
        }
    }
}

/* ── Hitscan weapon (simplified) ─────────────────────────────────────────── */
#define FIRE_CD_TICKS  15   /* 0.15s between shots */
#define PISTOL_DMG     10
#define PISTOL_RANGE   (1024*1024)   /* squared max range in map units */

static void fire_weapon(void) {
    if (pl_ammo <= 0 || pl_fire_cd > 0) return;
    /* Check the level is loaded BEFORE spending the round. The old order
     * decremented ammo and then bailed, so a failed level load looked like
     * "ammo goes down but the gun does nothing". */
    if (!lv_things || !lv_thing_ai) return;
    pl_ammo--;
    pl_fire_cd = FIRE_CD_TICKS;
    doomsnd_play("DSPISTOL");       /* non-blocking: the frame carries on */
    int32_t px   = pl_x >> 8, py = pl_y >> 8;
    int32_t fw_x = view_cos[pl_angle];   /* forward vector ×1024 */
    int32_t fw_y = view_sin[pl_angle];

    /* Find nearest monster inside a ~20° forward cone */
    int     best_i  = -1;
    int64_t best_d2 = PISTOL_RANGE;

    for (int i = 0; i < lv_nthings; i++) {
        dthing_t *th = &lv_things[i];
        if (!th->type || !is_monster_type(th->type)) continue;
        if (lv_thing_ai[i].state == AI_DEAD) continue;

        int32_t dx = (int32_t)th->x - px;
        int32_t dy = (int32_t)th->y - py;
        int64_t d2 = (int64_t)dx*dx + (int64_t)dy*dy;
        if (d2 >= best_d2) continue;

        /* Forward dot-product: must be in front and within ~20° */
        int64_t dot  = (int64_t)fw_x * dx + (int64_t)fw_y * dy;
        if (dot <= 0) continue;
        int32_t dist = isqrt32((int32_t)(d2 > 0x7FFFFFFF ? 0x7FFFFFFF : d2));
        /* cos(20°)≈0.940 → threshold = dist * 962 / 1024 */
        if (dot < (int64_t)dist * 962) continue;

        best_i  = i;
        best_d2 = d2;
    }

    if (best_i >= 0) {
        thing_ai_t *ai = &lv_thing_ai[best_i];
        ai->state = AI_CHASE;
        ai->hp   -= PISTOL_DMG;
        if (ai->hp <= 0) { ai->state = AI_DEAD; ai->atk_cd = 0; }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * Door / ceiling animation
 * ══════════════════════════════════════════════════════════════════════════ */

#define DOOR_SPEED    2    /* map units per tick (100 Hz) */
#define DOOR_WAIT   150    /* ticks open before DR auto-close (~1.5 s) */
#define MAX_DOORS    16

typedef struct {
    int      sec;
    int16_t  open_h;     /* target ceil when fully open */
    int16_t  close_h;    /* original ceil (= closed position) */
    int8_t   dir;        /* +1 opening, 0 waiting, -1 closing */
    int8_t   stays_open; /* 1 = D1 style: remove when fully open */
    uint32_t wait_end;
} door_t;

static door_t g_doors[MAX_DOORS];
static int    g_ndoors = 0;

static void doors_reset(void) { g_ndoors = 0; }

static int16_t door_min_surround_ceil(int sec) {
    int16_t mn = 30000;
    if (!lv_lines || !lv_sdefs || !lv_sectors) return lv_sectors[sec].ceil_h;
    for (int i = 0; i < lv_nlines; i++) {
        const dlinedef_t *ld = &lv_lines[i];
        int sr = (ld->right_sdef != 0xFFFF) ? lv_sdefs[ld->right_sdef].sector : -1;
        int sl = (ld->left_sdef  != 0xFFFF) ? lv_sdefs[ld->left_sdef ].sector : -1;
        int other = (sr == sec) ? sl : (sl == sec) ? sr : -1;
        if (other >= 0 && other < lv_nsectors) {
            int16_t h = lv_sectors[other].ceil_h;
            if (h < mn) mn = h;
        }
    }
    return (mn == 30000) ? lv_sectors[sec].ceil_h : mn;
}

static door_t *door_find_sector(int sec) {
    for (int i = 0; i < g_ndoors; i++)
        if (g_doors[i].sec == sec) return &g_doors[i];
    return NULL;
}

static void door_activate(int sec, int stays_open) {
    if (!lv_sectors || sec < 0 || sec >= lv_nsectors) return;
    door_t *d = door_find_sector(sec);
    if (d) {
        if (d->dir <= 0) { d->dir = 1; d->wait_end = 0; }
        return;
    }
    if (g_ndoors >= MAX_DOORS) return;
    doomsnd_play("DSDOROPN");
    d = &g_doors[g_ndoors++];
    d->sec        = sec;
    d->close_h    = lv_sectors[sec].ceil_h;
    d->open_h     = door_min_surround_ceil(sec) - 4;
    if (d->open_h <= d->close_h) d->open_h = d->close_h + 4;
    d->dir        = 1;
    d->stays_open = (int8_t)stays_open;
    d->wait_end   = 0;
}

static void doors_tick(void) {
    uint32_t now = timer_get_ticks();
    for (int i = 0; i < g_ndoors; ) {
        door_t    *d = &g_doors[i];
        dsector_t *s = &lv_sectors[d->sec];
        if (d->dir == 1) {
            s->ceil_h = (int16_t)(s->ceil_h + DOOR_SPEED);
            if (s->ceil_h >= d->open_h) {
                s->ceil_h = d->open_h;
                if (d->stays_open) { g_doors[i] = g_doors[--g_ndoors]; continue; }
                d->dir = 0; d->wait_end = now + DOOR_WAIT;
            }
        } else if (d->dir == 0) {
            if (now >= d->wait_end) d->dir = -1;
        } else {
            s->ceil_h = (int16_t)(s->ceil_h - DOOR_SPEED);
            if (s->ceil_h <= d->close_h) {
                s->ceil_h = d->close_h;
                g_doors[i] = g_doors[--g_ndoors]; continue;
            }
        }
        i++;
    }
}

/* 1 if segment (px,py)→(ux,uy) crosses linedef i */
static int seg_crosses_ld(int i,
                           int32_t px, int32_t py,
                           int32_t ux, int32_t uy) {
    const dlinedef_t *l = &lv_lines[i];
    int32_t ax = lv_verts[l->v1].x, ay = lv_verts[l->v1].y;
    int32_t bx = lv_verts[l->v2].x, by = lv_verts[l->v2].y;
    int64_t d1 = (int64_t)(bx-ax)*(py-ay) - (int64_t)(by-ay)*(px-ax);
    int64_t d2 = (int64_t)(bx-ax)*(uy-ay) - (int64_t)(by-ay)*(ux-ax);
    if (!((d1>0&&d2<0)||(d1<0&&d2>0))) return 0;
    int64_t d3 = (int64_t)(ux-px)*(ay-py) - (int64_t)(uy-py)*(ax-px);
    int64_t d4 = (int64_t)(ux-px)*(by-py) - (int64_t)(uy-py)*(bx-px);
    return (d3>0&&d4<0)||(d3<0&&d4>0);
}

static int door_is_use_special(int spec) {
    switch (spec) {
    case  1: case 26: case 27: case 28:   /* DR open/close */
    case 31: case 32: case 33: case 34:   /* D1 open-stay */
    case 46: case 29: case 63: case 61:   /* misc manual doors */
    case 103: case 117: case 118:
        return 1;
    }
    return 0;
}

/* Try to activate a USE-triggered door linedef. Returns 1 on success. */
static int door_try_use(int li) {
    if (!lv_sdefs) return 0;
    const dlinedef_t *ld = &lv_lines[li];
    if (!door_is_use_special(ld->special)) return 0;
    /* Door sector = back side of linedef from player's perspective.
     * Determine which side the player is on via cross product. */
    int32_t px = pl_x >> 8, py = pl_y >> 8;
    int32_t ax = lv_verts[ld->v1].x, ay = lv_verts[ld->v1].y;
    int32_t bx = lv_verts[ld->v2].x, by = lv_verts[ld->v2].y;
    int64_t side = (int64_t)(bx-ax)*(py-ay) - (int64_t)(by-ay)*(px-ax);
    int sec = -1;
    if (side >= 0) {
        /* Player on right side → door sector is left */
        if (ld->left_sdef  != 0xFFFF) sec = lv_sdefs[ld->left_sdef ].sector;
    } else {
        /* Player on left side → door sector is right */
        if (ld->right_sdef != 0xFFFF) sec = lv_sdefs[ld->right_sdef].sector;
    }
    if (sec < 0) return 0;

    /* Key-locked doors: check player has the required key */
    int need = 0;
    switch (ld->special) {
    case 26: case 32: need = KEY_BLUE;   break;
    case 27: case 34: need = KEY_YELLOW; break;
    case 28: case 33: need = KEY_RED;    break;
    }
    if (need && !(pl_keys & need)) return 1;  /* consume USE, but don't open */

    int stays = (ld->special == 31 || ld->special == 32 || ld->special == 33 ||
                 ld->special == 34 || ld->special == 46 || ld->special == 29 ||
                 ld->special == 61 || ld->special == 103 || ld->special == 118);
    door_activate(sec, stays);
    return 1;
}

/* Player presses Space: scan linedefs along 64-unit forward ray */
static void player_use(void) {
    int32_t px = pl_x >> 8, py = pl_y >> 8;
    int32_t ux = px + (int32_t)view_cos[pl_angle] * 64 / 1024;
    int32_t uy = py + (int32_t)view_sin[pl_angle] * 64 / 1024;
    for (int i = 0; i < lv_nlines; i++) {
        if (!seg_crosses_ld(i, px, py, ux, uy)) continue;
        if (door_try_use(i)) continue;
        /* Special 11: S1 Exit Level (USE once) */
        int spec = lv_lines[i].special;
        if (spec == 11) {
            g_level_exit = 1;
            lv_lines[i].special = 0;
            return;
        }
        /* Lift USE specials: 21=S1, 62=SR */
        if (spec == 21 || spec == 62) {
            int sec = -1;
            uint16_t tag = lv_lines[i].tag;
            if (tag) {
                for (int s = 0; s < lv_nsectors; s++)
                    if (lv_sectors[s].tag == tag) { sec = s; break; }
            }
            if (sec < 0 && lv_sdefs) {
                int32_t ax = lv_verts[lv_lines[i].v1].x, ay = lv_verts[lv_lines[i].v1].y;
                int32_t bx = lv_verts[lv_lines[i].v2].x, by = lv_verts[lv_lines[i].v2].y;
                int64_t side = (int64_t)(bx-ax)*(py-ay) - (int64_t)(by-ay)*(px-ax);
                if (side >= 0 && lv_lines[i].left_sdef  != 0xFFFF)
                    sec = lv_sdefs[lv_lines[i].left_sdef].sector;
                else if (lv_lines[i].right_sdef != 0xFFFF)
                    sec = lv_sdefs[lv_lines[i].right_sdef].sector;
            }
            if (sec >= 0) {
                lift_activate(sec, (spec == 62));
                if (spec == 21) lv_lines[i].special = 0;
            }
        }
    }
}

/* Walk-trigger: call after a move from (ox,oy) to (nx,ny) in map units */
static void walk_trigger(int32_t ox, int32_t oy, int32_t nx, int32_t ny) {
    if (!lv_lines || !lv_sdefs) return;
    for (int i = 0; i < lv_nlines; i++) {
        int spec = lv_lines[i].special;
        if (spec != 2 && spec != 86 && spec != 10 && spec != 88 && spec != 52) continue;
        if (!seg_crosses_ld(i, ox, oy, nx, ny)) continue;
        /* Special 52: W1 Exit Level */
        if (spec == 52) {
            g_level_exit = 1;
            lv_lines[i].special = 0;
            return;
        }
        int sec = -1;
        uint16_t tag = lv_lines[i].tag;
        if (tag) {
            for (int s = 0; s < lv_nsectors; s++)
                if (lv_sectors[s].tag == tag) { sec = s; break; }
        }
        if (sec < 0 && lv_lines[i].left_sdef  != 0xFFFF)
            sec = lv_sdefs[lv_lines[i].left_sdef ].sector;
        if (sec < 0 && lv_lines[i].right_sdef != 0xFFFF)
            sec = lv_sdefs[lv_lines[i].right_sdef].sector;
        if (sec < 0) continue;
        if (spec == 10 || spec == 88) {
            lift_activate(sec, (spec == 88));
            if (spec == 10) lv_lines[i].special = 0;
        } else {
            door_activate(sec, 1);
            if (spec == 2) lv_lines[i].special = 0;
        }
    }
}

/* ── Lifts ────────────────────────────────────────────────────────────────── */
#define LIFT_SPEED    4    /* map units per tick */
#define LIFT_WAIT   105    /* ticks at bottom before raising (~1.0 s) */
#define MAX_LIFTS    16

typedef struct {
    int      sec;
    int16_t  low_h;
    int16_t  high_h;
    int8_t   dir;        /* -1 lowering, 0 waiting at bottom, +1 raising */
    int8_t   repeatable;
    uint32_t wait_end;
} lift_t;

static lift_t g_lifts[MAX_LIFTS];
static int    g_nlifts = 0;

static void lifts_reset(void) { g_nlifts = 0; }

static int16_t lift_low_h(int sec) {
    int16_t mn = lv_sectors[sec].floor_h;
    if (!lv_lines || !lv_sdefs || !lv_sectors) return mn;
    for (int i = 0; i < lv_nlines; i++) {
        const dlinedef_t *ld = &lv_lines[i];
        int sr = (ld->right_sdef != 0xFFFF) ? lv_sdefs[ld->right_sdef].sector : -1;
        int sl = (ld->left_sdef  != 0xFFFF) ? lv_sdefs[ld->left_sdef ].sector : -1;
        int other = (sr == sec) ? sl : (sl == sec) ? sr : -1;
        if (other >= 0 && other < lv_nsectors && lv_sectors[other].floor_h < mn)
            mn = lv_sectors[other].floor_h;
    }
    return mn;
}

static void lift_activate(int sec, int repeatable) {
    if (!lv_sectors || sec < 0 || sec >= lv_nsectors) return;
    for (int i = 0; i < g_nlifts; i++)
        if (g_lifts[i].sec == sec) return;
    if (g_nlifts >= MAX_LIFTS) return;
    lift_t *lt   = &g_lifts[g_nlifts++];
    lt->sec        = sec;
    lt->high_h     = lv_sectors[sec].floor_h;
    lt->low_h      = lift_low_h(sec);
    if (lt->low_h >= lt->high_h) lt->low_h = (int16_t)(lt->high_h - 8);
    lt->dir        = -1;
    lt->repeatable = (int8_t)repeatable;
    lt->wait_end   = 0;
}

static void lifts_tick(void) {
    uint32_t now = timer_get_ticks();
    for (int i = 0; i < g_nlifts; ) {
        lift_t    *lt = &g_lifts[i];
        dsector_t *s  = &lv_sectors[lt->sec];
        if (lt->dir == -1) {
            s->floor_h = (int16_t)(s->floor_h - LIFT_SPEED);
            if (s->floor_h <= lt->low_h) {
                s->floor_h = lt->low_h;
                lt->dir = 0; lt->wait_end = now + LIFT_WAIT;
            }
        } else if (lt->dir == 0) {
            if (now >= lt->wait_end) lt->dir = 1;
        } else {
            s->floor_h = (int16_t)(s->floor_h + LIFT_SPEED);
            if (s->floor_h >= lt->high_h) {
                s->floor_h = lt->high_h;
                g_lifts[i] = g_lifts[--g_nlifts]; continue;
            }
        }
        i++;
    }
}

/* Return the view angle (0..ANG_N-1) from (mx,my) toward the player. */
static int angle_to_player_32(int32_t mx, int32_t my) {
    int32_t dx = (pl_x >> 8) - mx;
    int32_t dy = (pl_y >> 8) - my;
    int best = 0;
    int64_t best_dot = (int64_t)view_cos[0]*dx + (int64_t)view_sin[0]*dy;
    for (int a = 1; a < ANG_N; a++) {
        int64_t dot = (int64_t)view_cos[a]*dx + (int64_t)view_sin[a]*dy;
        if (dot > best_dot) { best_dot = dot; best = a; }
    }
    return best;
}

/* ── Sprite billboard renderer ─────────────────────────────────────────────
 *
 * Three-phase pipeline that fixes sprite over-clipping across subsector
 * boundaries:
 *   build_vissprites() — called before BSP. Projects each thing, resolves
 *                        frame+rotation, stores screen-space parameters and
 *                        subsector id. No snapshot yet.
 *   snap_vissprites(ss) — called at each BSP leaf. For any vissprite whose
 *                        ssec_id matches and hasn't been snapshotted, copies
 *                        spr_top/spr_bot into per-vissprite arrays. This
 *                        captures the span as it was when the sprite's
 *                        subsector was rendered, not the final BSP state.
 *   draw_vissprites() — called after BSP. Sorts far-to-near and renders
 *                        using each sprite's captured span.
 *
 * A vissprite whose subsector was never visited (cols_open hit 0 first) has
 * snapshot_valid == 0 and is skipped — correct, it's fully occluded.
 * ──────────────────────────────────────────────────────────────────────── */

#define MAX_VIS_SPR 64
typedef struct {
    int             ti;          /* thing index — for reference/debug */
    int             ssec_id;
    int             snapshot_valid;
    int32_t         vx;          /* depth for sort */
    int             sx_left, sx_right;
    int             sty;
    int             screen_h;
    const sprite_t *spr;
    const uint8_t  *cmap;
    int16_t         snap_top[VGA13_W];
    int16_t         snap_bot[VGA13_W];
} vissprite_t;

static vissprite_t g_vislist[MAX_VIS_SPR];
static int         g_nvis = 0;

static void build_vissprites(void) {
    g_nvis = 0;
    if (!lv_things || !lv_nthings) return;

    for (int i = 0; i < lv_nthings && g_nvis < MAX_VIS_SPR; i++) {
        dthing_t *th = &lv_things[i];
        if (th->type == 1) continue;

        int32_t vy;
        int32_t vx = view_project((int32_t)th->x, (int32_t)th->y, &vy);
        if (vx <= 256) continue;

        const char *sname4 = NULL;
        for (int si = 0; thing_table[si].type; si++)
            if (thing_table[si].type == th->type) { sname4 = thing_table[si].spr; break; }
        if (!sname4 || !sname4[0]) continue;

        char lname[9];
        lname[0]=sname4[0]; lname[1]=sname4[1];
        lname[2]=sname4[2]; lname[3]=sname4[3];
        lname[6]=lname[7]=lname[8]=0;

        char frame = 'A';
        if (lv_thing_ai && is_monster_type(th->type)) {
            thing_ai_t *ai = &lv_thing_ai[i];
            if (ai->state == AI_DEAD) {
                int df = ai->atk_cd / 8;
                if (df > 5) df = 5;
                frame = (char)('N' + df);
            } else if (ai->atk_cd > ENEMY_ATK_CD - 15) {
                frame = 'E';
            } else if (ai->state == AI_CHASE) {
                static const char wf[4] = {'A','B','C','D'};
                frame = wf[(timer_get_ticks() / 10) % 4];
            }
        }

        char rot_char;
        if (is_monster_type(th->type) && lv_thing_ai
            && lv_thing_ai[i].state != AI_DEAD) {
            int dir = angle_to_player_32((int32_t)th->x, (int32_t)th->y);
            int mon_facing = (int)(th->angle) * ANG_N / 360;
            int delta = (dir - mon_facing + ANG_N) & ANG_MASK;
            rot_char = (char)('1' + delta / ANG_PER_DIR);
        } else {
            rot_char = '0';
        }

        lname[4] = frame; lname[5] = rot_char;
        sprite_t *spr = spr_get(lname);
        if (!spr && frame != 'A') {
            lname[4] = 'A';
            spr = spr_get(lname);
        }
        if (!spr) {
            for (char r = '0'; r <= '8' && !spr; r++) {
                lname[5] = r;
                spr = spr_get(lname);
            }
        }
        if (!spr) continue;

        int scx = 160 + (int)(vy * PROJ_DIST / vx);
        int sx_left  = scx - (int)((int64_t)spr->loffset * PROJ_DIST * 1024 / vx);
        int sx_right = sx_left + (int)((int64_t)spr->width  * PROJ_DIST * 1024 / vx) - 1;
        if (sx_right < 0 || sx_left >= VGA13_W) continue;

        int tsec = find_sector_at((int32_t)th->x, (int32_t)th->y);
        int32_t thing_floor = (tsec >= 0 && tsec < lv_nsectors)
                              ? lv_sectors[tsec].floor_h : (pl_eye_z - 41);
        int spr_light = (tsec >= 0 && tsec < lv_nsectors)
                        ? lv_sectors[tsec].light : 192;
        int spr_shade = shade_for(spr_light, vx);

        int sty      = proj_y(thing_floor + spr->toffset - pl_eye_z, vx);
        int screen_h = (int)((int64_t)spr->height * PROJ_DIST * 1024 / vx);
        if (screen_h <= 0) continue;

        int ssec = find_subsector_at((int32_t)th->x, (int32_t)th->y);

        vissprite_t *v = &g_vislist[g_nvis++];
        v->ti              = i;
        v->ssec_id         = ssec;
        v->snapshot_valid  = 0;
        v->vx              = vx;
        v->sx_left         = sx_left;
        v->sx_right        = sx_right;
        v->sty             = sty;
        v->screen_h        = screen_h;
        v->spr             = spr;
        v->cmap            = g_colormap ? (g_colormap + spr_shade * 256) : NULL;
    }
}

/* Called from bsp_traverse() at each leaf. Snapshots current spr_top/spr_bot
 * for every vissprite living in this subsector. */
static void snap_vissprites(int ss) {
    for (int k = 0; k < g_nvis; k++) {
        vissprite_t *v = &g_vislist[k];
        if (v->snapshot_valid || v->ssec_id != ss) continue;
        int lo = v->sx_left  < 0       ? 0           : v->sx_left;
        int hi = v->sx_right >= VGA13_W ? VGA13_W - 1 : v->sx_right;
        for (int sx = lo; sx <= hi; sx++) {
            v->snap_top[sx] = spr_top[sx];
            v->snap_bot[sx] = spr_bot[sx];
        }
        v->snapshot_valid = 1;
    }
}

static void draw_vissprites(void) {
    /* Insertion sort: far-to-near so closer sprites overdraw farther ones */
    for (int i = 1; i < g_nvis; i++) {
        vissprite_t tmp = g_vislist[i]; int j = i;
        while (j > 0 && g_vislist[j-1].vx < tmp.vx) {
            g_vislist[j] = g_vislist[j-1]; j--;
        }
        g_vislist[j] = tmp;
    }

    for (int k = 0; k < g_nvis; k++) {
        vissprite_t    *v   = &g_vislist[k];
        if (!v->snapshot_valid) continue;   /* subsector never reached */
        const sprite_t *spr = v->spr;
        const uint8_t  *cmap = v->cmap;
        int sx_left = v->sx_left, sx_right = v->sx_right;
        int sty = v->sty, screen_h = v->screen_h;

        int sw = sx_right - sx_left + 1;
        int32_t u_step = (sw > 0) ? ((int32_t)spr->width  << 8) / sw : 0;
        int32_t v_step =             ((int32_t)spr->height << 8) / screen_h;

        int32_t u_fixed = 0;
        for (int sx = sx_left; sx <= sx_right; sx++, u_fixed += u_step) {
            if (sx < 0) continue;
            if (sx >= VGA13_W) break;
            int stop = v->snap_top[sx], sbot = v->snap_bot[sx];
            if (stop > sbot) continue;

            int patch_col = u_fixed >> 8;
            if (patch_col < 0) patch_col = 0;
            if (patch_col >= spr->width) patch_col = spr->width - 1;

            int y0 = sty, y1 = sty + screen_h - 1;
            if (y0 < stop) y0 = stop;
            if (y1 > sbot) y1 = sbot;
            if (y0 < 0)       y0 = 0;
            if (y1 >= VIEW_H) y1 = VIEW_H - 1;
            if (y0 > y1) continue;

            int32_t v_fixed = ((int32_t)(y0 - sty) * (int32_t)spr->height << 8) / screen_h;
            for (int y = y0; y <= y1; y++, v_fixed += v_step) {
                int vp = v_fixed >> 8;
                if (vp < 0) vp = 0;
                if (vp >= spr->height) vp = spr->height - 1;
                uint8_t pix = spr->pixels[vp * spr->width + patch_col];
                if (pix != 0xFF)
                    g_backbuf[(unsigned)y * VGA13_W + (unsigned)sx]
                        = cmap ? cmap[pix] : pix;
            }
        }
    }
}

/* ── Weapon overlay ───────────────────────────────────────────────────────── */
static uint8_t  *g_pistol_raw   = NULL;
static uint32_t  g_pistol_sz    = 0;
static uint8_t  *g_pistol_flash = NULL;
static uint32_t  g_pistol_fsz   = 0;

#define FLASH_TICKS 5   /* frames to show muzzle flash after firing */

static void weapon_load_lump(const char *name, uint8_t **buf, uint32_t *sz) {
    int idx = wad_find_lump(name);
    if (idx < 0) return;
    *sz = wad_lump_size(idx);
    if (!*sz) return;
    *buf = (uint8_t *)kmalloc(*sz);
    if (!*buf) { *sz = 0; return; }
    if (wad_read_lump(idx, *buf, *sz) != (int)*sz) {
        kfree(*buf); *buf = NULL; *sz = 0;
    }
}

static void weapon_init(void) {
    if (!g_pistol_raw)   weapon_load_lump("PISGA0", &g_pistol_raw,   &g_pistol_sz);
    if (!g_pistol_flash) weapon_load_lump("PISFB0", &g_pistol_flash, &g_pistol_fsz);
}

static void weapon_free(void) {
    if (g_pistol_raw)   { kfree(g_pistol_raw);   g_pistol_raw   = NULL; g_pistol_sz  = 0; }
    if (g_pistol_flash) { kfree(g_pistol_flash); g_pistol_flash = NULL; g_pistol_fsz = 0; }
}

static void draw_weapon(void) {
    /* Show muzzle flash for the first FLASH_TICKS frames of cooldown */
    if (g_pistol_flash && pl_fire_cd > FIRE_CD_TICKS - FLASH_TICKS)
        draw_patch(g_pistol_flash, 160, VIEW_H);
    else if (g_pistol_raw)
        draw_patch(g_pistol_raw, 160, VIEW_H);
}

/* ── Sector specials (damage floors + light effects) ─────────────────────── */

static void sectors_tick(int psec) {
    if (!lv_sectors || !g_sec_base_light) return;
    uint32_t t = timer_get_ticks();

    /* Damage floors: apply once per second (~100 ticks) */
    if (t >= g_dmg_next) {
        g_dmg_next = t + 100;
        if (psec >= 0 && psec < lv_nsectors) {
            switch (lv_sectors[psec].special) {
            case  5: pl_health -= 10; break;  /* nukage */
            case  7: pl_health -=  5; break;  /* minor acid */
            case 16: pl_health -= 20; break;  /* lava */
            }
            if (pl_health < 0) pl_health = 0;
        }
    }

    /* Light animations: recompute from base each frame */
    for (int i = 0; i < lv_nsectors; i++) {
        int16_t base = g_sec_base_light[i];
        switch (lv_sectors[i].special) {
        case 1: {
            /* Random flicker: pseudo-random period 3-6 ticks per sector */
            uint32_t h   = (uint32_t)((i + 1) * 1234567U);
            int      per = 3 + (int)(h & 3);
            lv_sectors[i].light = (int16_t)((t / (uint32_t)per & 1)
                                  ? base : (base > 96 ? base - 64 : 32));
            break;
        }
        case 3: {
            /* Strobe slow: flash off for 5 ticks every 30 */
            lv_sectors[i].light = (int16_t)((t % 30 < 5) ? 0 : base);
            break;
        }
        case 17: {
            /* Oscillating: triangle wave over 60 ticks */
            int32_t phase = (int32_t)(t % 60);
            int32_t delta = (phase < 30) ? phase : 60 - phase;
            lv_sectors[i].light = (int16_t)((int32_t)base * delta / 30);
            break;
        }
        }
    }
}

/* ── Full 3D frame ───────────────────────────────────────────────────────── */

static void draw_3d(void) {
    int psec = find_sector_at(pl_x >> 8, pl_y >> 8);
    if (psec >= 0 && psec < lv_nsectors)
        pl_eye_z = lv_sectors[psec].floor_h + 41;

    PF_T0(t_total);

    bb_fill_rect(0,           0, VGA13_W, VIEW_HALF_H,          CEIL_COL);
    bb_fill_rect(0, VIEW_HALF_H, VGA13_W, VIEW_H - VIEW_HALF_H, FLOOR_COL);

    PF_T0(t_flats);
    draw_flats(psec);
    PF_ACC(pf_flats, t_flats);

    spans_reset();
    build_vissprites();

    PF_T0(t_bsp);
    if (lv_nnodes > 0)
        bsp_traverse(lv_nnodes - 1);
    else if (lv_nssects > 0)
        bsp_traverse(0x8000);
    PF_ACC(pf_bsp, t_bsp);

    PF_T0(t_things);
    doors_tick();
    lifts_tick();
    sectors_tick(psec);
    try_pickups();
    thing_think();
    projs_tick();
    PF_ACC(pf_things, t_things);

    PF_T0(t_spr);
    draw_vissprites();
    projs_draw();
    draw_weapon();
    PF_ACC(pf_sprites, t_spr);

    PF_T0(t_bar);
    draw_statusbar();
    PF_ACC(pf_bar, t_bar);

    present_frame();

    PF_ACC(pf_total, t_total);
    pf_maybe_report();
}

/* ── doom_play_level ──────────────────────────────────────────────────────── */
/*
 * W/↑  forward    S/↓  back    A  strafe-left    D  strafe-right
 * ←/→  turn       TAB  toggle 3D ↔ automap
 * ,/.  zoom (automap)           ESC  back to menu
 */
void doom_play_level(int ep, int skill) {
    (void)skill;

#define MOVE_SPEED    8
#define MOVE_DELAY    1
#define TURN_DELAY    1
#define ZOOM_DELAY    3
#define ZOOM_STEP    32
#define FRAME_TICKS   2
#define USE_DELAY    20

    int map      = 1;
    int new_game = 1;  /* reset player stats only on first map */

    while (1) {
        char mapname[6] = {'E','0','M','0','\0','\0'};
        mapname[1] = (char)('1' + ep);
        mapname[3] = (char)('0' + map);

        g_level_exit = 0;

        if (level_load(mapname) < 0) {
            music_stop();
    vga13h_exit();
            vga_set_cursor(vga_get_row(), vga_get_col());
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_printf("  %s not found in WAD.\n", mapname);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_puts("  Press any key.\n");
            keyboard_flush(); keyboard_getchar();
            vga13h_enter(); set_doom_palette();
            break;
        }

        /* The level's own music: D_ plus the map name, looping. Started after
         * the load succeeds, so a missing map does not leave a tune playing
         * over the error. */
        char mustune[8] = {'D','_',0,0,0,0,0,0};
        for (int mi = 0; mi < 4; mi++) mustune[2 + mi] = mapname[mi];
        music_play(mustune, 1);

        am_fit();
        am_cx = pl_x >> 8;
        am_cy = pl_y >> 8;

        if (new_game) {
            pl_health = 100; pl_armor = 0; pl_ammo = 50; pl_keys = 0;
            new_game = 0;
        }
        pl_fire_cd = 0;

        int view3d = 1;
        if (view3d) draw_3d(); else draw_automap();
        keyboard_flush();

        uint32_t last_draw = timer_get_ticks();
        uint32_t turn_last = last_draw;
        uint32_t zoom_last = last_draw;
        uint32_t move_last = last_draw;
        uint32_t use_last  = last_draw;

        while (1) {
            uint32_t now = timer_get_ticks();
            int moved = 0;
            int do_move = (now - move_last >= MOVE_DELAY);

            /* ── Forward / backward ──────────────────────────────────────── */
            /* view_cos/sin ×1024; /8 → ×128; ×MOVE_SPEED×2 → ×256 fp scale */
            if (do_move) {
            if (keyboard_key_pressed(KEY_SC_W) || keyboard_key_pressed(KEY_SC_UP)) {
                int32_t dx = (int32_t)view_cos[pl_angle] / 8 * MOVE_SPEED * 2;
                int32_t dy = (int32_t)view_sin[pl_angle] / 8 * MOVE_SPEED * 2;
                if (try_move(dx, dy)) moved = 1;
            }
            if (keyboard_key_pressed(KEY_SC_S) || keyboard_key_pressed(KEY_SC_DOWN)) {
                int32_t dx = -(int32_t)view_cos[pl_angle] / 8 * MOVE_SPEED * 2;
                int32_t dy = -(int32_t)view_sin[pl_angle] / 8 * MOVE_SPEED * 2;
                if (try_move(dx, dy)) moved = 1;
            }

            /* ── Strafe ───────────────────────────────────────────────────── */
            if (keyboard_key_pressed(KEY_SC_A)) {
                int sa = (pl_angle + ANG_90) & ANG_MASK;
                int32_t dx = (int32_t)view_cos[sa] / 8 * MOVE_SPEED * 2;
                int32_t dy = (int32_t)view_sin[sa] / 8 * MOVE_SPEED * 2;
                if (try_move(dx, dy)) moved = 1;
            }
            if (keyboard_key_pressed(KEY_SC_D)) {
                int sa = (pl_angle + ANG_270) & ANG_MASK;
                int32_t dx = (int32_t)view_cos[sa] / 8 * MOVE_SPEED * 2;
                int32_t dy = (int32_t)view_sin[sa] / 8 * MOVE_SPEED * 2;
                if (try_move(dx, dy)) moved = 1;
            }
            if (moved) move_last = now;
            }

            /* ── Turn (rate-limited, fine angle) ─────────────────────────── */
            if (now - turn_last >= TURN_DELAY) {
                if (keyboard_key_pressed(KEY_SC_LEFT)) {
                    pl_angle = (pl_angle + 1) & ANG_MASK;
                    pl_dir   = pl_angle / ANG_PER_DIR;
                    turn_last = now; moved = 1;
                } else if (keyboard_key_pressed(KEY_SC_RIGHT)) {
                    pl_angle = (pl_angle + ANG_MASK) & ANG_MASK;
                    pl_dir   = pl_angle / ANG_PER_DIR;
                    turn_last = now; moved = 1;
                }
            }

            /* ── Automap zoom ─────────────────────────────────────────────── */
            if (!view3d && now - zoom_last >= ZOOM_DELAY) {
                if (keyboard_key_pressed(KEY_SC_COMMA)) {
                    am_scale -= ZOOM_STEP; if (am_scale < 8) am_scale = 8;
                    zoom_last = now; moved = 1;
                } else if (keyboard_key_pressed(KEY_SC_DOT)) {
                    am_scale += ZOOM_STEP; if (am_scale > 8192) am_scale = 8192;
                    zoom_last = now; moved = 1;
                }
            }

            /* ── TAB: toggle view ─────────────────────────────────────────── */
            if (keyboard_key_pressed(KEY_SC_TAB)) {
                view3d ^= 1;
                moved = 1;
                /* debounce: wait for key release */
                while (keyboard_key_pressed(KEY_SC_TAB)) __asm__ volatile ("pause");
            }

            /* ── Use / open door (Space) ─────────────────────────────────── */
            if (view3d && keyboard_key_pressed(KEY_SC_SPACE)) {
                if (now - use_last >= USE_DELAY) { player_use(); use_last = now; moved = 1; }
            }

            /* ── Fire (Ctrl) ─────────────────────────────────────────────── */
            if (view3d && keyboard_key_pressed(KEY_SC_LCTRL)) {
                if (pl_fire_cd == 0) { fire_weapon(); moved = 1; }
            }
            if (pl_fire_cd > 0) pl_fire_cd--;

            /* ── Level exit ───────────────────────────────────────────────── */
            if (g_level_exit) break;

            /* ── Death check ──────────────────────────────────────────────── */
            if (pl_health <= 0) break;

            /* ── ESC: quit ────────────────────────────────────────────────── */
            if (keyboard_key_pressed(KEY_SC_ESC)) break;

            if (moved) { am_cx = pl_x >> 8; am_cy = pl_y >> 8; }

            if (moved || now - last_draw >= FRAME_TICKS) {
                if (view3d) draw_3d(); else draw_automap();
                last_draw = now;
            }

            __asm__ volatile ("pause");
        }

        level_free();
        keyboard_flush();

        if (g_level_exit) {
            /* Brief level-complete screen in text mode, then load next map */
            music_stop();
    vga13h_exit();
            vga_set_cursor(vga_get_row(), vga_get_col());
            vga_set_color(VGA_YELLOW, VGA_BLACK);
            vga_printf("\n  ** E%dM%d COMPLETE! **\n", ep + 1, map);
            map++;
            if (map > 9) {
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                vga_puts("  No more maps in this episode.\n");
                vga_puts("  Press any key.\n");
                keyboard_flush(); keyboard_getchar();
                vga13h_enter(); set_doom_palette();
                break;
            }
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_printf("  Entering E%dM%d...\n", ep + 1, map);
            /* ~1.5 second pause */
            uint32_t tt = timer_get_ticks();
            while (timer_get_ticks() - tt < 150) __asm__ volatile ("pause");
            vga13h_enter();
            set_doom_palette();
            continue;
        }

        if (pl_health <= 0) {
            /* Death screen: red text, 2.5-second pause, then back to menu */
            music_stop();
    vga13h_exit();
            vga_set_cursor(vga_get_row(), vga_get_col());
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_puts("\n  YOU DIED.\n");
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_puts("  Press any key.\n");
            keyboard_flush(); keyboard_getchar();
            vga13h_enter(); set_doom_palette();
        }

        break;  /* ESC or death — return to menu */
    }

#undef MOVE_SPEED
#undef MOVE_DELAY
#undef TURN_DELAY
#undef ZOOM_DELAY
#undef ZOOM_STEP
#undef FRAME_TICKS
#undef USE_DELAY
}

/* ── doom_menu_run ────────────────────────────────────────────────────────── */

void doom_menu_run(void) {
    if (ensure_wad() < 0) return;

    load_bg();   /* decode TITLEPIC into bg_pixels once */

    vga13h_enter();
    set_doom_palette();

    /* Reset state */
    ms = MS_MAIN; cursor = 0; skull_f = 0;
    skull_t = timer_get_ticks();

    draw_frame();
    keyboard_flush();

    int running = 1;
    while (running) {

        /* Skull animation (non-blocking) */
        uint32_t t = timer_get_ticks();
        if (t - skull_t >= SKULL_TICKS) {
            skull_f ^= 1;
            skull_t = t;
            draw_frame();
        }

        if (!keyboard_available()) {
            __asm__ volatile ("pause");
            continue;
        }

        int key = keyboard_getchar();

        /* How many items in the current menu? */
        int nitems = (ms == MS_MAIN) ? N_MAIN :
                     (ms == MS_EPISODE) ? N_EP : N_SKILL;

        /* ── Navigation ───────────────────────────────────────────────── */
        if (key == KEY_UP) {
            cursor = (cursor > 0) ? cursor - 1 : nitems - 1;

        } else if (key == KEY_DOWN) {
            cursor = (cursor + 1) % nitems;

        /* ── ESC — go back one level ───────────────────────────────── */
        } else if (key == 27) {
            switch (ms) {
            case MS_MAIN:
                /* ESC at top level: do nothing (like original Doom) */
                break;
            case MS_EPISODE:
                ms = MS_MAIN; cursor = 0;
                break;
            case MS_SKILL:
                ms = MS_EPISODE; cursor = episode;
                break;
            }

        /* ── Enter — select current item ──────────────────────────── */
        } else if (key == '\n' || key == '\r' || key == ' ') {
            switch (ms) {

            /* ── Main menu selections ─────────────────────────────── */
            case MS_MAIN:
                switch (cursor) {
                case 0:  /* New Game */
                    ms = MS_EPISODE; cursor = 0;
                    break;
                case 1:  /* Options */
                case 2:  /* Load Game */
                case 3:  /* Save Game */
                    beep_invalid();   /* not yet implemented */
                    break;
                case 4:  /* Read This! */
                    /* Try help screen lumps in order */
                    if (wad_find_lump("HELP1") >= 0) {
                        show_fullscreen("HELP1");
                        show_fullscreen("HELP2");
                    } else {
                        show_fullscreen("HELP");
                    }
                    /* Restore palette + redraw after returning from fullscreen */
                    set_doom_palette();
                    break;
                case 5:  /* Quit Game */
                    running = 0;
                    break;
                }
                break;

            /* ── Episode selections ───────────────────────────────── */
            case MS_EPISODE:
                if (cursor == 0) {
                    /* Episode 1 — available in shareware */
                    episode = 0;
                    ms = MS_SKILL;
                    cursor = 2;   /* default: Hurt Me Plenty */
                } else {
                    /* Episodes 2-4 not in shareware */
                    beep_invalid();
                }
                break;

            /* ── Skill selections ─────────────────────────────────── */
            case MS_SKILL:
                /* Launch automap — stays in mode 13h throughout.
                 * doom_play_level re-sets the palette before returning. */
                doom_play_level(episode, cursor);
                set_doom_palette();   /* restore after level */
                ms = MS_MAIN; cursor = 0;
                break;
            }
        }

        if (running) draw_frame();
    }

    music_stop();
    vga13h_exit();
    vga_set_cursor(vga_get_row(), vga_get_col());
}

/* ── doom_titlescreen ─────────────────────────────────────────────────────── */
/* Standalone: show TITLEPIC with no menu overlay, wait for key. */

void doom_titlescreen(void) {
    if (ensure_wad() < 0) return;

    int idx = wad_find_lump("TITLEPIC");
    if (idx < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("  TITLEPIC lump not found.\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    uint32_t sz = wad_lump_size(idx);
    uint8_t *patch = (uint8_t *)kmalloc(sz);
    if (!patch) return;
    if (wad_read_lump(idx, patch, sz) != (int)sz) { kfree(patch); return; }

    vga13h_enter();
    set_doom_palette();
    memset(g_backbuf, 0, sizeof(g_backbuf));
    draw_patch(patch, 0, 0);
    present_frame();
    kfree(patch);

    keyboard_flush();
    keyboard_getchar();

    music_stop();
    vga13h_exit();
    vga_set_cursor(vga_get_row(), vga_get_col());
}

/* ── doom_wad_info ────────────────────────────────────────────────────────── */

void doom_wad_info(void) {
    if (ensure_wad() < 0) return;

    int n = wad_num_lumps();
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("  WAD info\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_printf("  Lumps:  %d\n", n);

    int markers = 0, data_lumps = 0;
    uint32_t total_bytes = 0;
    for (int i = 0; i < n; i++) {
        const wad_lump_t *l = wad_get_lump(i);
        if (l->size == 0) markers++;
        else { data_lumps++; total_bytes += l->size; }
    }
    vga_printf("  Data:   %d lumps (%u KB)\n", data_lumps,
               (total_bytes + 1023) / 1024);
    vga_printf("  Markers:%d\n", markers);

    static const char *interesting[] = {
        "PLAYPAL","COLORMAP","ENDOOM","TITLEPIC","CREDIT",
        "HELP","HELP1","HELP2","E1M1","E1M2","MAP01",NULL
    };
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  Key lumps:\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int k = 0; interesting[k]; k++) {
        int i = wad_find_lump(interesting[k]);
        if (i >= 0)
            vga_printf("    %-12s %6u B\n", interesting[k], wad_lump_size(i));
    }
}

/* ── doom_lump_list ───────────────────────────────────────────────────────── */

void doom_lump_list(void) {
    if (ensure_wad() < 0) return;

    int n = wad_num_lumps();
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_printf("  %d lumps (any key = next page, ESC = quit)\n", n);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

#define COLS 4
#define ROWS 20
    for (int i = 0; i < n; i++) {
        const wad_lump_t *l = wad_get_lump(i);
        char name[WAD_NAME_LEN + 1];
        memcpy(name, l->name, WAD_NAME_LEN); name[WAD_NAME_LEN] = '\0';
        if (l->size == 0)
            vga_printf("  [%-8s]         ", name);
        else
            vga_printf("  %-8s %6u B ", name, l->size);
        if ((i + 1) % COLS == 0) vga_putchar('\n');
        if ((i + 1) % (COLS * ROWS) == 0 && (i + 1) < n) {
            vga_set_color(VGA_YELLOW, VGA_BLACK);
            vga_printf("  -- page %d -- key to continue --\n", (i+1)/(COLS*ROWS));
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            if (keyboard_getchar() == 27) break;
        }
    }
    if (n % COLS != 0) vga_putchar('\n');
#undef COLS
#undef ROWS
}
