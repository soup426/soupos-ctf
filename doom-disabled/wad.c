/* wad.c — WAD file parser for soupOS.
 *
 * The WAD directory is malloc'd once during wad_init() and kept in RAM.
 * Individual lump reads seek the open FAT file handle each time — the WAD
 * itself is never fully buffered, so a 4+ MB IWAD works fine with 8 MB heap.
 */

#include "wad.h"
#include "fat.h"
#include "heap.h"
#include "str.h"
#include <stdint.h>

/* ── On-disk structures (all little-endian, same as x86) ─────────────────── */

typedef struct __attribute__((packed)) {
    char     magic[4];       /* "IWAD" or "PWAD" */
    uint32_t numlumps;
    uint32_t infotableofs;
} wad_hdr_t;

typedef struct __attribute__((packed)) {
    uint32_t filepos;
    uint32_t size;
    char     name[WAD_NAME_LEN];
} wad_dirent_t;

/* ── Module state ─────────────────────────────────────────────────────────── */

static int          wad_fd     = -1;
static int          wad_nlumps = 0;
static wad_lump_t  *wad_dir    = NULL;

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static int wad_upcase(int c) {
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

/* Case-insensitive comparison of a C string against a WAD lump name
 * (which is NOT null-terminated if it uses all 8 bytes). */
static int lump_name_eq(const char *query, const char lname[WAD_NAME_LEN]) {
    for (int i = 0; i < WAD_NAME_LEN; i++) {
        char a = (char)wad_upcase((unsigned char)query[i]);
        char b = (char)wad_upcase((unsigned char)lname[i]);
        if (a != b) return 0;
        if (!a) return 1;  /* both ended at same position */
    }
    /* Reached 8 chars — query must also end here */
    return !query[WAD_NAME_LEN];
}

/* ── Public API ───────────────────────────────────────────────────────────── */

int wad_init(const char *filename) {
    /* Tear down any previous state */
    wad_shutdown();

    wad_fd = fat_fopen(filename);
    if (wad_fd < 0) return -1;

    /* Read and validate header */
    wad_hdr_t hdr;
    if (fat_fread(wad_fd, &hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
        fat_fclose(wad_fd); wad_fd = -1; return -1;
    }
    if (memcmp(hdr.magic, "IWAD", 4) != 0 &&
        memcmp(hdr.magic, "PWAD", 4) != 0) {
        fat_fclose(wad_fd); wad_fd = -1; return -1;
    }

    uint32_t n = hdr.numlumps;
    if (n == 0 || n > 65536u) {
        fat_fclose(wad_fd); wad_fd = -1; return -1;
    }

    /* Allocate directory */
    wad_dir = (wad_lump_t *)kmalloc(n * sizeof(wad_lump_t));
    if (!wad_dir) { fat_fclose(wad_fd); wad_fd = -1; return -1; }

    /* Read directory entries */
    fat_fseek(wad_fd, (int32_t)hdr.infotableofs, FAT_SEEK_SET);
    uint32_t i;
    for (i = 0; i < n; i++) {
        wad_dirent_t de;
        if (fat_fread(wad_fd, &de, sizeof(de)) != (int)sizeof(de)) break;
        wad_dir[i].filepos = de.filepos;
        wad_dir[i].size    = de.size;
        memcpy(wad_dir[i].name, de.name, WAD_NAME_LEN);
    }
    wad_nlumps = (int)i;
    return 0;
}

void wad_shutdown(void) {
    if (wad_fd >= 0) { fat_fclose(wad_fd); wad_fd = -1; }
    if (wad_dir)     { kfree(wad_dir);     wad_dir = NULL; }
    wad_nlumps = 0;
}

int wad_num_lumps(void) { return wad_nlumps; }

const wad_lump_t *wad_get_lump(int idx) {
    if (idx < 0 || idx >= wad_nlumps) return NULL;
    return &wad_dir[idx];
}

uint32_t wad_lump_size(int idx) {
    if (idx < 0 || idx >= wad_nlumps) return 0;
    return wad_dir[idx].size;
}

int wad_find_lump_after(const char *name, int start_after) {
    for (int i = start_after + 1; i < wad_nlumps; i++) {
        if (lump_name_eq(name, wad_dir[i].name)) return i;
    }
    return -1;
}

int wad_find_lump(const char *name) {
    return wad_find_lump_after(name, -1);
}

int wad_read_lump(int idx, void *buf, uint32_t bufsize) {
    if (idx < 0 || idx >= wad_nlumps || wad_fd < 0) return -1;
    uint32_t sz = wad_dir[idx].size;
    if (sz == 0) return 0;
    uint32_t to_read = (bufsize < sz) ? bufsize : sz;
    fat_fseek(wad_fd, (int32_t)wad_dir[idx].filepos, FAT_SEEK_SET);
    return fat_fread(wad_fd, buf, to_read);
}

int wad_read_lump_name(const char *name, void *buf, uint32_t bufsize) {
    int idx = wad_find_lump(name);
    if (idx < 0) return -1;
    return wad_read_lump(idx, buf, bufsize);
}

int wad_read_palette(uint8_t out_rgb[768]) {
    return (wad_read_lump_name("PLAYPAL", out_rgb, 768) == 768) ? 0 : -1;
}
