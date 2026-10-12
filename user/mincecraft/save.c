/* save.c - the world file (v0.60.197), little-endian:
 *   "MINC", version 1, seed, time of day, x y z yaw pitch, B_COUNT counts
 *   (32 bits each), the grid as (count 1..255, id) pairs, then the sum of
 *   every byte before it. Written and read through 4 KB, never whole: the
 *   worst run-length encoding is 2 MB. A file that is short, long, of
 *   another version, or whose sum is wrong is refused. */
#include "../ulib.h"
#include "save.h"
#include "world.h"
#define VERSION 1
static unsigned char io[4096];
static int ion, iofd, ioerr, iolen, iopos;
static unsigned iosum, iototal;
static void flush(void) { if (ion && sys_write(iofd, (const char *)io, ion) != ion) ioerr = 1; ion = 0; }
static void put(unsigned c) { iosum += c & 255; iototal++; io[ion++] = (unsigned char)c; if (ion == (int)sizeof(io)) flush(); }
static void put32(unsigned v) { put(v); put(v >> 8); put(v >> 16); put(v >> 24); }
int save_world(const char *path, const player_t *p, unsigned seed, int tod, unsigned *bytes) {
    iofd = sys_open(path, O_WRITE);
    if (iofd < 0) return -1;
    ion = 0; ioerr = 0; iosum = 0; iototal = 0;
    put('M'); put('I'); put('N'); put('C');
    put32(VERSION); put32(seed); put32((unsigned)tod);
    put32((unsigned)p->x); put32((unsigned)p->y); put32((unsigned)p->z); put32((unsigned)p->yaw); put32((unsigned)p->pitch);
    for (int i = 0; i < B_COUNT; i++) put32(p->inv[i]);
    for (int i = 0; i < WCELLS; ) {
        int b = world[i], n = 1;
        while (i + n < WCELLS && n < 255 && world[i + n] == b) n++;
        put((unsigned)n); put((unsigned)b);
        i += n;
    }
    put32(iosum);
    flush();
    if (sys_close(iofd) < 0) ioerr = 1;                          /* the write happens at the close (v0.60.202) */
    if (bytes) *bytes = iototal;
    return ioerr ? -1 : 0;
}
static int get(void) {
    if (iopos == iolen) { iolen = sys_read(iofd, (char *)io, sizeof(io)); iopos = 0; if (iolen <= 0) { iolen = 0; return -1; } }
    unsigned char c = io[iopos++];
    iosum += c;
    return c;
}
static int get32(unsigned *v) {
    unsigned r = 0;
    for (int i = 0; i < 4; i++) { int c = get(); if (c < 0) return -1; r |= (unsigned)c << (8 * i); }
    *v = r;
    return 0;
}
int load_world(const char *path, player_t *p, unsigned *seed, int *tod) {
    unsigned v[8], inv[B_COUNT], want, got;
    iofd = sys_open(path, O_READ);
    if (iofd < 0) return -1;
    iolen = iopos = 0; iosum = 0;
    int bad = !(get() == 'M' && get() == 'I' && get() == 'N' && get() == 'C');
    for (int i = 0; i < 8 && !bad; i++) if (get32(&v[i]) < 0) bad = 1;
    for (int i = 0; i < B_COUNT && !bad; i++) if (get32(&inv[i]) < 0 || inv[i] > 999) bad = 1;
    if (!bad && (v[0] != VERSION || v[2] >= 1200)) bad = 1;
    for (int i = 0; i < WCELLS && !bad; ) {
        int n = get(), b = get();
        if (n <= 0 || b < 0 || b >= B_COUNT || i + n > WCELLS) { bad = 1; break; }
        for (int k = 0; k < n; k++) world[i + k] = (unsigned char)b;
        i += n;
    }
    if (!bad) { want = iosum; if (get32(&got) < 0 || got != want || get() >= 0) bad = 1; }
    sys_close(iofd);
    if (bad) return -2;
    /* Everything is checked before anything is handed back (v0.60.202).
     * The feet may be above the top row: on the ceiling's top block, or
     * jumping off it (up to 1.3 more); the walls keep x and z inside. */
    fx x = (fx)v[3], y = (fx)v[4], z = (fx)v[5];
    if (x < 0 || x >= WX << 16 || z < 0 || z >= WZ << 16 || y < 0 || y >= (WY + 4) << 16) return -2;
    *seed = v[1]; *tod = (int)v[2];
    player_spawn(p);
    p->x = x; p->y = y; p->z = z; p->yaw = (int)v[6] & 1023; p->pitch = (int)v[7];
    if (p->pitch > 227 || p->pitch < -227) p->pitch = 0;
    for (int i = 0; i < B_COUNT; i++) p->inv[i] = (unsigned short)inv[i];
    p->ground = 0;
    return 0;
}
