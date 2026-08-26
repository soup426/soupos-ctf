#include "fat.h"
#include "task.h"

/* One lock for the whole FAT layer.
 *
 * fat.c keeps its cursor, sector cache, free-cluster hint and fd table in
 * file-scope state, and since v0.7.4 the timer can switch tasks anywhere.
 * preempt_disable would not be enough: these operations yield inside the
 * ATA PIO wait, so another task could still enter voluntarily. The lock is
 * recursive because these entry points call each other. */
static mutex_t fat_mtx;

#include "ata.h"
#include "str.h"

/* ---- On-disk structures (packed, byte-exact) ---- */

typedef struct __attribute__((packed)) {
    uint8_t  jump[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;   /* FAT16: 512, FAT32: 0 */
    uint16_t total_sectors16;
    uint8_t  media_type;
    uint16_t fat_size16;         /* FAT16: sectors/FAT, FAT32: 0 */
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;
} bpb_t;

typedef struct __attribute__((packed)) {
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} fat16_ebpb_t;

typedef struct __attribute__((packed)) {
    uint32_t fat_size32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} fat32_ebpb_t;

typedef struct __attribute__((packed)) {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t acc_date;
    uint16_t cluster_hi;   /* FAT32 high 16 bits; always 0 on FAT16 */
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t cluster_lo;
    uint32_t file_size;
} dirent_t;

/* ---- Module state ---- */

static int     initialized         = 0;
static int     fs_type_val         = 0;
static char    vol_label[12];

static uint16_t bps;                /* bytes per sector (always 512 for us) */
static uint8_t  spc;                /* sectors per cluster */
static uint32_t fat_start;          /* LBA of first FAT */
static uint32_t fat_sectors;        /* sectors per FAT copy */
static uint32_t cluster_count;      /* total data cluster count */
static uint32_t root_dir_start;     /* LBA of root dir  (FAT16 only) */
static uint32_t root_dir_sectors;   /* sectors in fixed root dir (FAT16) */
static uint32_t data_start;         /* LBA of cluster 2 */
static uint32_t root_cluster_32;    /* FAT32 root cluster number */
static uint8_t  num_fats;

/* Sector-sized I/O buffers */
static uint8_t sec_buf[512] __attribute__((aligned(4)));
static uint8_t fat_buf[512] __attribute__((aligned(4)));
static uint32_t fat_cache_lba   = (uint32_t)-1;
static int      fat_cache_dirty = 0;
/* Hint for fat_alloc_cluster - start the next probe from here instead of 2.
 * Reset to 2 if we wrap. Persisted across calls within a single fat_write. */
static uint32_t fat_alloc_hint  = 2;

/* uid stamped as owner on files/bowls created from here on (set via login). */
static uint8_t  creator_uid     = 0;

void fat_set_creator(uint8_t uid) { creator_uid = uid; }

/* The cluster number that represents "the root directory" in this module.
 * On FAT16 the root is a fixed sector range, not a cluster - we use 0 as a
 * sentinel for it. On FAT32 the root is a normal cluster chain. */
#define ROOT_DIR_CL  (fs_type_val == 32 ? root_cluster_32 : 0u)

/* ---- Internal helpers ---- */

static char to_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - ('a' - 'A')) : c;
}

/* Forward decl - defined further down with the other write-side helpers. */
static int fat_flush_cache(void);

static uint32_t fat_get_entry(uint32_t cluster) {
    uint32_t fat_offset = (fs_type_val == 32) ? cluster * 4 : cluster * 2;
    uint32_t lba = fat_start + fat_offset / bps;
    uint32_t off = fat_offset % bps;

    if (lba != fat_cache_lba) {
        /* If a different sector is dirty, flush before evicting it.
         * Otherwise queued writes get silently dropped on a chain walk. */
        if (fat_flush_cache() < 0) return 0x0FFFFFFF;
        if (ata_read_sector(lba, fat_buf) < 0) return 0x0FFFFFFF;
        fat_cache_lba = lba;
    }

    if (fs_type_val == 32)
        return (*(uint32_t *)(fat_buf + off)) & 0x0FFFFFFF;
    else
        return *(uint16_t *)(fat_buf + off);
}

static int is_eoc(uint32_t entry) {
    return (fs_type_val == 32) ? (entry >= 0x0FFFFFF8u) : (entry >= 0xFFF8u);
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return data_start + ((cluster - 2u) * spc);
}

/* Turn FAT 8.3 name (name[8], ext[3], space-padded) into "NAME.EXT\0". */
static void format_83(const char *n8, const char *e3, char *out) {
    int i, j = 0;
    int nlen = 0, elen = 0;

    for (i = 0; i < 8; i++) { if (n8[i] == ' ' || !n8[i]) break; nlen = i + 1; }
    for (i = 0; i < 3; i++) { if (e3[i] == ' ' || !e3[i]) break; elen = i + 1; }

    for (i = 0; i < nlen; i++) out[j++] = n8[i];
    if (elen) {
        out[j++] = '.';
        for (i = 0; i < elen; i++) out[j++] = e3[i];
    }
    out[j] = '\0';
}

/* Case-insensitive match between user input and a FAT directory entry. */
static int name_matches(const char *input, const dirent_t *de) {
    char formatted[FAT_NAME_MAX];
    format_83(de->name, de->ext, formatted);

    const char *a = input, *b = formatted;
    while (*a && *b) {
        if (to_upper(*a) != to_upper(*b)) return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

static int is_valid(const dirent_t *de) {
    if ((uint8_t)de->name[0] == 0x00) return 0;  /* free / end */
    if ((uint8_t)de->name[0] == 0xE5) return 0;  /* deleted */
    if (de->attr == FAT_ATTR_LFN)     return 0;  /* LFN     */
    if (de->attr & FAT_ATTR_VOLID)    return 0;  /* vol id  */
    return 1;
}

static void fill_entry(const dirent_t *de, fat_entry_t *out) {
    format_83(de->name, de->ext, out->name);
    out->attr    = de->attr;
    out->cluster = ((uint32_t)de->cluster_hi << 16) | de->cluster_lo;
    out->size    = de->file_size;
}

/* Cluster number stored in a directory entry, normalised so that a stored
 * 0 ("points at the root", as in a "..") maps to ROOT_DIR_CL. */
static uint32_t entry_cluster(const dirent_t *de) {
    uint32_t cl = ((uint32_t)de->cluster_hi << 16) | de->cluster_lo;
    if (cl == 0) return ROOT_DIR_CL;
    return cl;
}

/* Parse a filename string (e.g. "hello.txt") into FAT 8.3 components.
   fname[8] and fext[3] are space-padded, uppercased. */
static void parse_83(const char *name, char fname[8], char fext[3]) {
    int i;
    for (i = 0; i < 8; i++) fname[i] = ' ';
    for (i = 0; i < 3; i++) fext[i]  = ' ';

    const char *dot = (void *)0;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;

    int nlen = dot ? (int)(dot - name) : (int)strlen(name);
    if (nlen > 8) nlen = 8;
    for (i = 0; i < nlen; i++) fname[i] = to_upper(name[i]);

    if (dot) {
        int elen = (int)strlen(dot + 1);
        if (elen > 3) elen = 3;
        for (i = 0; i < elen; i++) fext[i] = to_upper(dot[1 + i]);
    }
}

/* ======================================================================
 * Directory traversal - works for the FAT16 fixed root (cluster 0) and
 * for ordinary subdirectory cluster chains alike.
 * ====================================================================== */

/* Copy the next '/'-delimited component of *pp into comp (FAT_NAME_MAX).
 * Advances *pp. Returns 1 if a component was produced, 0 at end of path. */
static int next_component(const char **pp, char *comp) {
    const char *p = *pp;
    while (*p == '/') p++;
    if (!*p) { *pp = p; return 0; }
    int i = 0;
    while (*p && *p != '/') {
        if (i < FAT_NAME_MAX - 1) comp[i++] = *p;
        p++;
    }
    comp[i] = '\0';
    *pp = p;
    return 1;
}

/* LBA of logical sector `idx` within directory `dir_cl` (0 = FAT16 root).
 * Returns 1 ok (sets *lba), 0 past-end-of-directory, -1 on I/O error. */
static int dir_sector_lba(uint32_t dir_cl, uint32_t idx, uint32_t *lba) {
    if (dir_cl == 0) {
        if (idx >= root_dir_sectors) return 0;
        *lba = root_dir_start + idx;
        return 1;
    }
    uint32_t cl    = dir_cl;
    uint32_t steps = idx / spc;
    for (uint32_t i = 0; i < steps; i++) {
        cl = fat_get_entry(cl);
        if (is_eoc(cl) || cl < 2) return 0;
    }
    *lba = cluster_to_lba(cl) + (idx % spc);
    return 1;
}

/* Search directory `dir_cl` for `name`. On a hit, copies the entry into
 * *out (if non-NULL) and its on-disk location into *ent_lba / *ent_idx.
 * Returns 1 found, 0 not found, -1 on error. */
static int dir_find(uint32_t dir_cl, const char *name,
                    dirent_t *out, uint32_t *ent_lba, int *ent_idx) {
    int eps = bps / 32;
    for (uint32_t s = 0; ; s++) {
        uint32_t lba;
        int r = dir_sector_lba(dir_cl, s, &lba);
        if (r <= 0) return r;                 /* 0 = end, -1 = error */
        if (ata_read_sector(lba, sec_buf) < 0) return -1;
        dirent_t *dir = (dirent_t *)sec_buf;
        for (int i = 0; i < eps; i++) {
            if ((uint8_t)dir[i].name[0] == 0x00) return 0;   /* end of dir */
            if (!is_valid(&dir[i])) continue;
            if (name_matches(name, &dir[i])) {
                if (out)     *out     = dir[i];
                if (ent_lba) *ent_lba = lba;
                if (ent_idx) *ent_idx = i;
                return 1;
            }
        }
    }
}

/* Resolve `path` to a directory cluster (sets *out_cl; 0 = FAT16 root).
 * Returns 0 ok, -1 if a component is missing or is not a directory. */
static int dir_resolve(const char *path, uint32_t *out_cl) {
    uint32_t cur = ROOT_DIR_CL;
    const char *p = path ? path : "";
    char comp[FAT_NAME_MAX];
    while (next_component(&p, comp)) {
        dirent_t de;
        if (dir_find(cur, comp, &de, 0, 0) != 1) return -1;
        if (!(de.attr & FAT_ATTR_DIR))           return -1;
        cur = entry_cluster(&de);
    }
    *out_cl = cur;
    return 0;
}

/* Split `path` into the directory holding the final component (*parent_cl)
 * and that component's name (leaf, buffer >= FAT_NAME_MAX).
 * Returns 0 ok, 1 if `path` names the root itself (leaf is ""), -1 if an
 * intermediate component is missing or is not a directory. */
static int path_split(const char *path, uint32_t *parent_cl, char *leaf) {
    const char *p = path ? path : "";
    char comp[FAT_NAME_MAX];
    char prev[FAT_NAME_MAX];
    uint32_t cur = ROOT_DIR_CL;
    int have = 0;
    leaf[0] = '\0';

    while (next_component(&p, comp)) {
        if (have) {
            dirent_t de;
            if (dir_find(cur, prev, &de, 0, 0) != 1) return -1;
            if (!(de.attr & FAT_ATTR_DIR))           return -1;
            cur = entry_cluster(&de);
        }
        strcpy(prev, comp);
        have = 1;
    }
    if (!have) { *parent_cl = cur; return 1; }   /* path was the root */
    strcpy(leaf, prev);
    *parent_cl = cur;
    return 0;
}

/* ---- Public: fat_init ---- */

int fat_init(void) {
    initialized = 0;

    if (ata_read_sector(0, sec_buf) < 0) return -1;

    /* Validate boot signature */
    if (sec_buf[510] != 0x55 || sec_buf[511] != 0xAA) return -1;

    bpb_t *b = (bpb_t *)sec_buf;

    bps = b->bytes_per_sector;
    spc = b->sectors_per_cluster;

    if (bps != 512 || spc == 0) return -1;

    uint32_t fat_size  = b->fat_size16;
    uint32_t total_sec = b->total_sectors16 ? b->total_sectors16 : b->total_sectors32;
    uint32_t root_sec  = ((uint32_t)b->root_entry_count * 32 + bps - 1) / bps;

    if (fat_size == 0) {
        /* FAT32 extended BPB at offset 36 */
        fat32_ebpb_t *e = (fat32_ebpb_t *)(sec_buf + 36);
        fat_size        = e->fat_size32;
        root_cluster_32 = e->root_cluster;
        for (int i = 0; i < 11; i++) vol_label[i] = e->volume_label[i];
    } else {
        fat16_ebpb_t *e = (fat16_ebpb_t *)(sec_buf + 36);
        root_cluster_32 = 0;
        for (int i = 0; i < 11; i++) vol_label[i] = e->volume_label[i];
    }

    /* Trim trailing spaces from label */
    vol_label[11] = '\0';
    for (int i = 10; i >= 0 && vol_label[i] == ' '; i--) vol_label[i] = '\0';

    num_fats         = b->num_fats;
    fat_start        = b->reserved_sectors;
    fat_sectors      = fat_size;
    root_dir_start   = fat_start + (uint32_t)num_fats * fat_size;
    root_dir_sectors = root_sec;
    data_start       = root_dir_start + root_sec;

    /* Determine FAT type by cluster count */
    uint32_t data_sec = total_sec - (b->reserved_sectors + (uint32_t)num_fats * fat_size + root_sec);
    cluster_count     = data_sec / spc;

    if      (cluster_count < 4085)  return -1;    /* FAT12 - unsupported */
    else if (cluster_count < 65525) fs_type_val = 16;
    else                            fs_type_val = 32;

    initialized = 1;
    return 0;
}

int         fat_get_type(void)  { return fs_type_val; }
const char *fat_label(void)     { return vol_label;   }

/* ---- Public: fat_ls ---- */

static int fat_ls_impl(const char *path, fat_entry_t *out, int max) {
    if (!initialized) return -1;

    uint32_t dir_cl;
    if (dir_resolve(path ? path : "/", &dir_cl) < 0) return -1;

    int count = 0;
    int eps   = bps / 32;
    for (uint32_t s = 0; count < max; s++) {
        uint32_t lba;
        int r = dir_sector_lba(dir_cl, s, &lba);
        if (r <= 0) break;
        if (ata_read_sector(lba, sec_buf) < 0) break;
        dirent_t *dir = (dirent_t *)sec_buf;
        for (int i = 0; i < eps && count < max; i++) {
            if ((uint8_t)dir[i].name[0] == 0x00) return count;  /* end of dir */
            if (!is_valid(&dir[i]))    continue;
            if (dir[i].name[0] == '.') continue;   /* hide '.' and '..' */
            fill_entry(&dir[i], &out[count++]);
        }
    }
    return count;
}

/* ---- Public: fat_read ---- */

static int fat_read_impl(const char *path, uint8_t *buf, uint32_t bufsize, uint32_t *out_size) {
    if (!initialized || !path || !buf) return -1;

    uint32_t parent_cl;
    char     leaf[FAT_NAME_MAX];
    if (path_split(path, &parent_cl, leaf) != 0 || !leaf[0]) return -1;

    dirent_t de;
    if (dir_find(parent_cl, leaf, &de, 0, 0) != 1) return -1;
    if (de.attr & FAT_ATTR_DIR) return -1;          /* it's a bowl */

    uint32_t file_cluster = ((uint32_t)de.cluster_hi << 16) | de.cluster_lo;
    uint32_t file_size    = de.file_size;
    if (out_size) *out_size = file_size;
    if (file_size == 0 || file_cluster < 2) return 0;

    /* Read the cluster chain into buf */
    uint32_t written = 0;
    uint32_t cluster = file_cluster;
    while (!is_eoc(cluster) && cluster >= 2 && written < bufsize && written < file_size) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint32_t s = 0; s < spc; s++) {
            if (written >= file_size || written >= bufsize) break;
            if (ata_read_sector(lba + s, sec_buf) < 0) return -1;

            uint32_t rem_file = file_size - written;
            uint32_t rem_buf  = bufsize   - written;
            uint32_t to_copy  = bps;
            if (to_copy > rem_file) to_copy = rem_file;
            if (to_copy > rem_buf)  to_copy = rem_buf;

            memcpy(buf + written, sec_buf, to_copy);
            written += to_copy;
        }
        cluster = fat_get_entry(cluster);
    }
    return 0;
}

/* ======================================================================
 * Write support (FAT16 only)
 * ====================================================================== */

/* Flush the cached FAT sector to every FAT copy if it has been modified. */
static int fat_flush_cache(void) {
    if (!fat_cache_dirty || fat_cache_lba == (uint32_t)-1) {
        fat_cache_dirty = 0;
        return 0;
    }
    for (uint8_t i = 0; i < num_fats; i++) {
        uint32_t copy_lba = fat_start + (uint32_t)i * fat_sectors
                          + (fat_cache_lba - fat_start);
        if (ata_write_sector(copy_lba, fat_buf) < 0) return -1;
    }
    fat_cache_dirty = 0;
    return 0;
}

/* Update a FAT entry in the cached sector. Marks the cache dirty - caller
 * MUST eventually call fat_flush_cache() before returning to the user. */
static int fat_set_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = (fs_type_val == 32) ? cluster * 4 : cluster * 2;
    uint32_t lba = fat_start + fat_offset / bps;
    uint32_t off = fat_offset % bps;

    if (lba != fat_cache_lba) {
        if (fat_flush_cache() < 0) return -1;
        if (ata_read_sector(lba, fat_buf) < 0) return -1;
        fat_cache_lba = lba;
    }

    if (fs_type_val == 32) {
        uint32_t existing = *(uint32_t *)(fat_buf + off);
        *(uint32_t *)(fat_buf + off) = (existing & 0xF0000000u) | (value & 0x0FFFFFFFu);
    } else {
        *(uint16_t *)(fat_buf + off) = (uint16_t)value;
    }
    fat_cache_dirty = 1;
    return 0;
}

/* Free every cluster in a chain (set each entry to 0 = free) */
static int fat_free_chain(uint32_t cluster) {
    while (!is_eoc(cluster) && cluster >= 2) {
        uint32_t next = fat_get_entry(cluster);
        if (fat_set_entry(cluster, 0) < 0) return -1;
        cluster = next;
    }
    return 0;
}

/* Find the first free cluster, mark it EOC, return it. 0 on failure. */
static uint32_t fat_alloc_cluster(void) {
    uint32_t eoc  = (fs_type_val == 32) ? 0x0FFFFFFFu : 0xFFFFu;
    uint32_t last = cluster_count + 1;
    if (fat_alloc_hint < 2 || fat_alloc_hint > last) fat_alloc_hint = 2;

    for (uint32_t c = fat_alloc_hint; c <= last; c++) {
        if (fat_get_entry(c) == 0) {
            if (fat_set_entry(c, eoc) < 0) return 0;
            fat_alloc_hint = c + 1;
            return c;
        }
    }
    for (uint32_t c = 2; c < fat_alloc_hint; c++) {
        if (fat_get_entry(c) == 0) {
            if (fat_set_entry(c, eoc) < 0) return 0;
            fat_alloc_hint = c + 1;
            return c;
        }
    }
    return 0;
}

/* Zero every sector of cluster `cl`. */
static int cluster_zero(uint32_t cl) {
    uint32_t base = cluster_to_lba(cl);
    memset(sec_buf, 0, bps);
    for (uint32_t s = 0; s < spc; s++)
        if (ata_write_sector(base + s, sec_buf) < 0) return -1;
    return 0;
}

/* Find a free directory-entry slot in `dir_cl`, growing a subdirectory by
 * one cluster if it is full. The FAT16 root cannot grow. On success sets
 * *out_lba / *out_idx and returns 0; returns -1 on failure. */
static int dir_alloc_slot(uint32_t dir_cl, uint32_t *out_lba, int *out_idx) {
    int eps = bps / 32;
    for (uint32_t s = 0; ; s++) {
        uint32_t lba;
        int r = dir_sector_lba(dir_cl, s, &lba);
        if (r < 0) return -1;
        if (r == 0) break;                     /* ran off the end */
        if (ata_read_sector(lba, sec_buf) < 0) return -1;
        dirent_t *dir = (dirent_t *)sec_buf;
        for (int i = 0; i < eps; i++) {
            uint8_t f = (uint8_t)dir[i].name[0];
            if (f == 0x00 || f == 0xE5) {
                *out_lba = lba;
                *out_idx = i;
                return 0;
            }
        }
    }
    /* Directory full - the fixed FAT16 root cannot be extended. */
    if (dir_cl == 0) return -1;

    /* Walk to the last cluster of the chain. */
    uint32_t cl = dir_cl;
    for (;;) {
        uint32_t next = fat_get_entry(cl);
        if (is_eoc(next) || next < 2) break;
        cl = next;
    }
    uint32_t newcl = fat_alloc_cluster();
    if (newcl == 0) return -1;
    if (fat_set_entry(cl, newcl) < 0) return -1;
    if (fat_flush_cache() < 0)        return -1;
    if (cluster_zero(newcl) < 0)      return -1;

    *out_lba = cluster_to_lba(newcl);
    *out_idx = 0;
    return 0;
}

/* ---- Public: fat_write ---- */

static int fat_write_impl(const char *path, const uint8_t *buf, uint32_t size) {
    if (!initialized || !path || fs_type_val == 32) return -1;

    uint32_t parent_cl;
    char     leaf[FAT_NAME_MAX];
    if (path_split(path, &parent_cl, leaf) != 0 || !leaf[0]) return -1;

    char fname[8], fext[3];
    parse_83(leaf, fname, fext);

    /* Existing entry -> free its chain and reuse the slot; else find a slot.
     * An overwrite keeps the file's existing owner/mode; a new file is
     * stamped with the current creator uid and default permissions. */
    uint32_t dir_lba    = 0;
    int      dir_idx    = -1;
    uint8_t  ent_mode   = FAT_PERM_DEFAULT;
    uint8_t  ent_owner  = creator_uid;
    dirent_t de;
    uint32_t e_lba;
    int      e_idx;
    if (dir_find(parent_cl, leaf, &de, &e_lba, &e_idx) == 1) {
        if (de.attr & FAT_ATTR_DIR) return -1;     /* can't overwrite a bowl */
        uint32_t old = ((uint32_t)de.cluster_hi << 16) | de.cluster_lo;
        if (old >= 2 && fat_free_chain(old) < 0) return -1;
        if (de.reserved & FAT_PERM_MARK) {         /* keep prior ownership */
            ent_mode  = de.reserved;
            ent_owner = de.crt_time_tenth;
        }
        dir_lba = e_lba;
        dir_idx = e_idx;
    } else {
        if (dir_alloc_slot(parent_cl, &dir_lba, &dir_idx) < 0) return -1;
    }

    /* Allocate clusters and write data. */
    uint32_t first_cluster = 0, prev_cluster = 0;
    uint32_t written       = 0;
    uint32_t cluster_bytes = (uint32_t)spc * bps;
    uint32_t clusters_need = size ? (size + cluster_bytes - 1) / cluster_bytes : 0;

    for (uint32_t c = 0; c < clusters_need; c++) {
        uint32_t cl = fat_alloc_cluster();
        if (cl == 0) return -1;

        if (!first_cluster) first_cluster = cl;
        if (prev_cluster && fat_set_entry(prev_cluster, cl) < 0) return -1;

        uint32_t lba = cluster_to_lba(cl);
        for (uint32_t s = 0; s < spc; s++) {
            memset(sec_buf, 0, bps);
            uint32_t to_copy = (written < size) ? (size - written) : 0;
            if (to_copy > bps) to_copy = bps;
            if (to_copy) memcpy(sec_buf, buf + written, to_copy);
            if (ata_write_sector(lba + s, sec_buf) < 0) return -1;
            written += to_copy;
        }
        prev_cluster = cl;
    }

    if (fat_flush_cache() < 0) return -1;

    /* Write the directory entry. */
    if (ata_read_sector(dir_lba, sec_buf) < 0) return -1;
    dirent_t *ent = (dirent_t *)sec_buf + dir_idx;
    memset(ent, 0, sizeof(*ent));
    memcpy(ent->name, fname, 8);
    memcpy(ent->ext,  fext,  3);
    ent->attr           = FAT_ATTR_ARCHIVE;
    ent->reserved       = ent_mode;            /* soupOS permission mode */
    ent->crt_time_tenth = ent_owner;           /* soupOS owner uid       */
    ent->cluster_lo     = (uint16_t)(first_cluster & 0xFFFF);
    ent->file_size      = size;
    return ata_write_sector(dir_lba, sec_buf);
}

/* ---- Public: fat_mkdir ---- */

static int fat_mkdir_impl(const char *path) {
    if (!initialized || !path || fs_type_val == 32) return -1;

    uint32_t parent_cl;
    char     leaf[FAT_NAME_MAX];
    if (path_split(path, &parent_cl, leaf) != 0 || !leaf[0]) return -1;

    /* Must not already exist. */
    if (dir_find(parent_cl, leaf, 0, 0, 0) == 1) return -1;

    char fname[8], fext[3];
    parse_83(leaf, fname, fext);

    /* Allocate and initialise the new directory's first cluster. */
    uint32_t newcl = fat_alloc_cluster();
    if (newcl == 0) return -1;
    if (fat_flush_cache() < 0) return -1;
    if (cluster_zero(newcl) < 0) return -1;

    /* First sector gets the '.' and '..' entries. A '..' that points at
     * the root is conventionally stored as cluster 0. */
    memset(sec_buf, 0, bps);
    dirent_t *d = (dirent_t *)sec_buf;
    memset(d[0].name, ' ', 11);
    d[0].name[0]    = '.';
    d[0].attr       = FAT_ATTR_DIR;
    d[0].cluster_lo = (uint16_t)(newcl & 0xFFFF);
    memset(d[1].name, ' ', 11);
    d[1].name[0]    = '.';
    d[1].name[1]    = '.';
    d[1].attr       = FAT_ATTR_DIR;
    d[1].cluster_lo = (uint16_t)(parent_cl & 0xFFFF);
    if (ata_write_sector(cluster_to_lba(newcl), sec_buf) < 0) return -1;

    /* Add the directory entry to the parent. */
    uint32_t slot_lba;
    int      slot_idx;
    if (dir_alloc_slot(parent_cl, &slot_lba, &slot_idx) < 0) {
        fat_free_chain(newcl);
        fat_flush_cache();
        return -1;
    }
    if (ata_read_sector(slot_lba, sec_buf) < 0) return -1;
    dirent_t *ent = (dirent_t *)sec_buf + slot_idx;
    memset(ent, 0, sizeof(*ent));
    memcpy(ent->name, fname, 8);
    memcpy(ent->ext,  fext,  3);
    ent->attr           = FAT_ATTR_DIR;
    ent->reserved       = FAT_PERM_DEFAULT;    /* soupOS permission mode */
    ent->crt_time_tenth = creator_uid;         /* soupOS owner uid       */
    ent->cluster_lo     = (uint16_t)(newcl & 0xFFFF);
    ent->file_size      = 0;
    return ata_write_sector(slot_lba, sec_buf);
}

/* ---- Public: fat_rmdir ---- */

static int fat_rmdir_impl(const char *path) {
    if (!initialized || !path || fs_type_val == 32) return -1;

    uint32_t parent_cl;
    char     leaf[FAT_NAME_MAX];
    if (path_split(path, &parent_cl, leaf) != 0 || !leaf[0]) return -1;

    dirent_t de;
    uint32_t e_lba;
    int      e_idx;
    if (dir_find(parent_cl, leaf, &de, &e_lba, &e_idx) != 1) return -1;
    if (!(de.attr & FAT_ATTR_DIR)) return -1;       /* not a bowl */

    uint32_t dir_cl = ((uint32_t)de.cluster_hi << 16) | de.cluster_lo;
    if (dir_cl < 2) return -1;

    /* The bowl must be empty: only '.' and '..' may be present. */
    int eps = bps / 32;
    for (uint32_t s = 0; ; s++) {
        uint32_t lba;
        int r = dir_sector_lba(dir_cl, s, &lba);
        if (r < 0) return -1;
        if (r == 0) break;
        if (ata_read_sector(lba, sec_buf) < 0) return -1;
        dirent_t *dir = (dirent_t *)sec_buf;
        for (int i = 0; i < eps; i++) {
            uint8_t f = (uint8_t)dir[i].name[0];
            if (f == 0x00) goto empty_ok;
            if (f == 0xE5) continue;
            if (dir[i].attr == FAT_ATTR_LFN) continue;
            if (f == '.') {                          /* '.' or '..' */
                if (dir[i].name[1] == ' ' ||
                    (dir[i].name[1] == '.' && dir[i].name[2] == ' '))
                    continue;
            }
            return -1;                               /* bowl not empty */
        }
    }
empty_ok:
    /* Free the bowl's clusters, then drop the parent's entry. */
    if (fat_free_chain(dir_cl) < 0) return -1;
    if (fat_flush_cache() < 0)     return -1;
    if (ata_read_sector(e_lba, sec_buf) < 0) return -1;
    ((dirent_t *)sec_buf)[e_idx].name[0] = (char)0xE5;
    return ata_write_sector(e_lba, sec_buf);
}

/* ---- Public: fat_is_dir / fat_exists ---- */

static int fat_is_dir_impl(const char *path) {
    if (!initialized) return 0;
    uint32_t parent_cl;
    char     leaf[FAT_NAME_MAX];
    int sr = path_split(path, &parent_cl, leaf);
    if (sr == 1) return 1;                  /* the root is a directory */
    if (sr != 0 || !leaf[0]) return 0;
    dirent_t de;
    if (dir_find(parent_cl, leaf, &de, 0, 0) != 1) return 0;
    return (de.attr & FAT_ATTR_DIR) ? 1 : 0;
}

static int fat_exists_impl(const char *path) {
    if (!initialized) return 0;
    uint32_t parent_cl;
    char     leaf[FAT_NAME_MAX];
    int sr = path_split(path, &parent_cl, leaf);
    if (sr == 1) return 1;
    if (sr != 0 || !leaf[0]) return 0;
    return dir_find(parent_cl, leaf, 0, 0, 0) == 1;
}

/* ---- Public: fat_stat / fat_chmod / fat_chown ---- */

static int fat_stat_impl(const char *path, uint8_t *owner, uint8_t *mode) {
    if (!initialized) return -1;
    uint32_t parent_cl;
    char     leaf[FAT_NAME_MAX];
    int sr = path_split(path, &parent_cl, leaf);
    if (sr == 1) {                          /* the root */
        if (owner) *owner = 0;
        if (mode)  *mode  = FAT_PERM_DEFAULT;
        return 0;
    }
    if (sr != 0 || !leaf[0]) return -1;
    dirent_t de;
    if (dir_find(parent_cl, leaf, &de, 0, 0) != 1) return -1;

    uint8_t m = de.reserved;
    uint8_t o = de.crt_time_tenth;
    if (!(m & FAT_PERM_MARK)) {              /* foreign entry -> defaults */
        m = FAT_PERM_DEFAULT;
        o = 0;
    }
    if (owner) *owner = o;
    if (mode)  *mode  = m;
    return 0;
}

/* Locate a path's directory entry and rewrite its two metadata bytes. */
static int fat_set_meta(const char *path, uint8_t mode, uint8_t owner) {
    if (!initialized || fs_type_val == 32) return -1;
    uint32_t parent_cl;
    char     leaf[FAT_NAME_MAX];
    if (path_split(path, &parent_cl, leaf) != 0 || !leaf[0]) return -1;
    dirent_t de;
    uint32_t e_lba;
    int      e_idx;
    if (dir_find(parent_cl, leaf, &de, &e_lba, &e_idx) != 1) return -1;
    if (ata_read_sector(e_lba, sec_buf) < 0) return -1;
    dirent_t *ent = (dirent_t *)sec_buf + e_idx;
    ent->reserved       = mode | FAT_PERM_MARK;
    ent->crt_time_tenth = owner;
    return ata_write_sector(e_lba, sec_buf);
}

static int fat_chmod_impl(const char *path, uint8_t mode) {
    uint8_t owner, oldmode;
    if (fat_stat(path, &owner, &oldmode) < 0) return -1;
    return fat_set_meta(path, mode, owner);
}

static int fat_chown_impl(const char *path, uint8_t owner) {
    uint8_t oldowner, mode;
    if (fat_stat(path, &oldowner, &mode) < 0) return -1;
    return fat_set_meta(path, mode, owner);
}

/* ---- Public: fat_delete ---- */

static int fat_delete_impl(const char *path) {
    if (!initialized || !path || fs_type_val == 32) return -1;

    uint32_t parent_cl;
    char     leaf[FAT_NAME_MAX];
    if (path_split(path, &parent_cl, leaf) != 0 || !leaf[0]) return -1;

    dirent_t de;
    uint32_t e_lba;
    int      e_idx;
    if (dir_find(parent_cl, leaf, &de, &e_lba, &e_idx) != 1) return -1;
    if (de.attr & FAT_ATTR_DIR) return -1;          /* use fat_rmdir */

    uint32_t cl = ((uint32_t)de.cluster_hi << 16) | de.cluster_lo;
    if (cl >= 2 && fat_free_chain(cl) < 0) return -1;
    if (fat_flush_cache() < 0) return -1;
    if (ata_read_sector(e_lba, sec_buf) < 0) return -1;
    ((dirent_t *)sec_buf)[e_idx].name[0] = (char)0xE5;
    return ata_write_sector(e_lba, sec_buf);
}

/* ======================================================================
 * Seekable file handles
 * ====================================================================== */

typedef struct {
    int      open;           /* 1 if slot is in use               */
    uint32_t file_size;      /* total file size in bytes          */
    uint32_t pos;            /* current read position (bytes)     */
    uint32_t start_cluster;  /* first cluster of the file         */
    uint32_t cur_cluster;    /* cluster that contains pos         */
    uint32_t cur_cl_idx;     /* index of cur_cluster in the chain */
} fat_fd_t;

static fat_fd_t fat_fds[FAT_MAX_FD];

/* Walk the cluster chain forward by `steps` clusters. */
static uint32_t chain_advance(uint32_t cl, uint32_t steps) {
    for (uint32_t i = 0; i < steps && !is_eoc(cl) && cl >= 2; i++)
        cl = fat_get_entry(cl);
    return cl;
}

static int fat_fopen_impl(const char *path) {
    if (!initialized || !path) return -1;

    int fd = -1;
    for (int i = 0; i < FAT_MAX_FD; i++)
        if (!fat_fds[i].open) { fd = i; break; }
    if (fd < 0) return -1;                  /* too many open files */

    uint32_t parent_cl;
    char     leaf[FAT_NAME_MAX];
    if (path_split(path, &parent_cl, leaf) != 0 || !leaf[0]) return -1;

    dirent_t de;
    if (dir_find(parent_cl, leaf, &de, 0, 0) != 1) return -1;
    if (de.attr & FAT_ATTR_DIR) return -1;          /* can't fopen a bowl */

    uint32_t file_cluster = ((uint32_t)de.cluster_hi << 16) | de.cluster_lo;

    fat_fds[fd].open          = 1;
    fat_fds[fd].file_size     = de.file_size;
    fat_fds[fd].pos           = 0;
    fat_fds[fd].start_cluster = file_cluster;
    fat_fds[fd].cur_cluster   = file_cluster;
    fat_fds[fd].cur_cl_idx    = 0;
    return fd;
}

static void fat_fclose_impl(int fd) {
    if (fd >= 0 && fd < FAT_MAX_FD)
        fat_fds[fd].open = 0;
}

static int fat_fread_impl(int fd, void *buf, uint32_t count) {
    if (fd < 0 || fd >= FAT_MAX_FD || !fat_fds[fd].open) return -1;
    fat_fd_t *f = &fat_fds[fd];

    uint32_t remaining = f->file_size > f->pos ? f->file_size - f->pos : 0;
    if (count > remaining) count = remaining;
    if (count == 0) return 0;

    uint8_t  *out      = (uint8_t *)buf;
    uint32_t  done_r   = 0;
    uint32_t  cl_bytes = (uint32_t)spc * bps;

    while (done_r < count) {
        if (is_eoc(f->cur_cluster) || f->cur_cluster < 2) break;

        uint32_t cl_off      = f->pos % cl_bytes;
        uint32_t sec_in_cl   = cl_off / bps;
        uint32_t byte_in_sec = cl_off % bps;

        uint32_t lba = cluster_to_lba(f->cur_cluster) + sec_in_cl;
        if (ata_read_sector(lba, sec_buf) < 0) return (int)done_r;

        uint32_t avail = bps - byte_in_sec;
        uint32_t take  = count - done_r;
        if (take > avail) take = avail;

        memcpy(out + done_r, sec_buf + byte_in_sec, take);
        done_r  += take;
        f->pos  += take;

        if (f->pos % cl_bytes == 0 && f->pos < f->file_size) {
            f->cur_cluster = fat_get_entry(f->cur_cluster);
            f->cur_cl_idx++;
        }
    }
    return (int)done_r;
}

static int fat_fseek_impl(int fd, int32_t offset, int whence) {
    if (fd < 0 || fd >= FAT_MAX_FD || !fat_fds[fd].open) return -1;
    fat_fd_t *f = &fat_fds[fd];

    int32_t new_pos;
    if      (whence == FAT_SEEK_SET) new_pos = offset;
    else if (whence == FAT_SEEK_CUR) new_pos = (int32_t)f->pos + offset;
    else if (whence == FAT_SEEK_END) new_pos = (int32_t)f->file_size + offset;
    else return -1;

    if (new_pos < 0)                      new_pos = 0;
    if ((uint32_t)new_pos > f->file_size) new_pos = (int32_t)f->file_size;

    uint32_t cl_bytes   = (uint32_t)spc * bps;
    uint32_t target_idx = (uint32_t)new_pos / cl_bytes;

    if (target_idx < f->cur_cl_idx) {
        f->cur_cluster = f->start_cluster;
        f->cur_cl_idx  = 0;
    }
    uint32_t steps = target_idx - f->cur_cl_idx;
    if (steps > 0) {
        f->cur_cluster = chain_advance(f->cur_cluster, steps);
        f->cur_cl_idx  = target_idx;
    }
    f->pos = (uint32_t)new_pos;
    return 0;
}

int32_t fat_ftell(int fd) {
    if (fd < 0 || fd >= FAT_MAX_FD || !fat_fds[fd].open) return -1;
    return (int32_t)fat_fds[fd].pos;
}

uint32_t fat_fsize(int fd) {
    if (fd < 0 || fd >= FAT_MAX_FD || !fat_fds[fd].open) return 0;
    return fat_fds[fd].file_size;
}

int fat_feof(int fd) {
    if (fd < 0 || fd >= FAT_MAX_FD || !fat_fds[fd].open) return 1;
    return fat_fds[fd].pos >= fat_fds[fd].file_size;
}

/* ── Locked public entry points ───────────────────────────────────────────
 * Each real implementation is now static and named *_impl; these thin
 * wrappers take the FAT lock around it. Wrapping rather than editing every
 * return path keeps the lock impossible to leak on an error exit. */
int fat_ls(const char *path, fat_entry_t *out, int max) {
    mutex_lock(&fat_mtx);
    int _r = fat_ls_impl(path, out, max);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_read(const char *path, uint8_t *buf, uint32_t bufsize, uint32_t *out_size) {
    mutex_lock(&fat_mtx);
    int _r = fat_read_impl(path, buf, bufsize, out_size);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_write(const char *path, const uint8_t *buf, uint32_t size) {
    mutex_lock(&fat_mtx);
    int _r = fat_write_impl(path, buf, size);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_mkdir(const char *path) {
    mutex_lock(&fat_mtx);
    int _r = fat_mkdir_impl(path);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_rmdir(const char *path) {
    mutex_lock(&fat_mtx);
    int _r = fat_rmdir_impl(path);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_is_dir(const char *path) {
    mutex_lock(&fat_mtx);
    int _r = fat_is_dir_impl(path);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_exists(const char *path) {
    mutex_lock(&fat_mtx);
    int _r = fat_exists_impl(path);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_stat(const char *path, uint8_t *owner, uint8_t *mode) {
    mutex_lock(&fat_mtx);
    int _r = fat_stat_impl(path, owner, mode);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_chmod(const char *path, uint8_t mode) {
    mutex_lock(&fat_mtx);
    int _r = fat_chmod_impl(path, mode);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_chown(const char *path, uint8_t owner) {
    mutex_lock(&fat_mtx);
    int _r = fat_chown_impl(path, owner);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_delete(const char *path) {
    mutex_lock(&fat_mtx);
    int _r = fat_delete_impl(path);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_fopen(const char *path) {
    mutex_lock(&fat_mtx);
    int _r = fat_fopen_impl(path);
    mutex_unlock(&fat_mtx);
    return _r;
}

void fat_fclose(int fd) {
    mutex_lock(&fat_mtx);
    fat_fclose_impl(fd);
    mutex_unlock(&fat_mtx);
}

int fat_fread(int fd, void *buf, uint32_t count) {
    mutex_lock(&fat_mtx);
    int _r = fat_fread_impl(fd, buf, count);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_fseek(int fd, int32_t offset, int whence) {
    mutex_lock(&fat_mtx);
    int _r = fat_fseek_impl(fd, offset, whence);
    mutex_unlock(&fat_mtx);
    return _r;
}
