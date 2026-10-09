#include "fat.h"
#include "rtc.h"
#include "users.h"
#include "timer.h"
#include "klog.h"
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
static uint32_t volume_sectors;     /* sectors in the volume, from the BPB */
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

/* ── Where soupOS keeps an owner and a mode in a FAT entry (v0.41.0) ─────────
 * Until v0.41.0 the mode lived in byte 12, which FAT reserves for case flags
 * (bit 0 "no valid 8.3 name", bits 3-4 "lowercase"): fsck.fat called every
 * file soupOS made a bad name and renamed it, and Linux would have shown the
 * names in lowercase. Now:
 *   byte 13 (creation time, hundredths: valid 0-199) = 200 + owner uid, a
 *           value no other tool writes, so it is also the "soupOS metadata"
 *           mark;
 *   byte 14 (low byte of the creation time, written once at creation by
 *           everything and never validated) = the mode.
 * Byte 12 is always written as 0. The old layout is still READ, so a disk
 * made before this keeps its permissions until the entry is next written. */
#define META_OWNER_BASE 200
typedef char users_fit_the_owner_byte[(USERS_MAX < 256 - META_OWNER_BASE) ? 1 : -1];

static int meta_get(const dirent_t *de, uint8_t *mode, uint8_t *owner) {
    if (de->crt_time_tenth >= META_OWNER_BASE && (de->crt_time & FAT_PERM_MARK)) {
        *owner = (uint8_t)(de->crt_time_tenth - META_OWNER_BASE);
        *mode  = (uint8_t)(de->crt_time & 0xFF);
        return 1;
    }
    if (de->reserved & FAT_PERM_MARK) {                  /* the pre-v0.41 layout */
        *owner = de->crt_time_tenth;
        *mode  = de->reserved;
        return 1;
    }
    return 0;                                            /* another tool's file */
}

/* FAT's date and time encodings, from the RTC (v0.47.1). Until then every
 * file soupOS made was dated 1980-00-00 0:00, because these were left zero.
 * time: hours<<11 | minutes<<5 | seconds/2;  date: (year-1980)<<9 | month<<5 | day */
static void now_fat(uint16_t *date, uint16_t *time) {
    rtc_time_t t;
    rtc_read(&t);
    uint16_t y = t.year >= 1980 ? (uint16_t)(t.year - 1980) : 0;
    *date = (uint16_t)((y << 9) | ((t.month & 15) << 5) | (t.day & 31));
    *time = (uint16_t)(((t.hour & 31) << 11) | ((t.minute & 63) << 5) | ((t.second / 2) & 31));
}

/* A write stamps the write time and the access date; a creation also sets
 * the creation date. The creation TIME's low byte holds the mode (meta_put),
 * so only its date is set here. */
static void stamp(dirent_t *de, int created, uint16_t keep_crt_date) {
    uint16_t d, t;
    now_fat(&d, &t);
    de->wrt_date = d;
    de->wrt_time = t;
    de->acc_date = d;
    de->crt_date = created ? d : keep_crt_date;
}

static void meta_put(dirent_t *de, uint8_t mode, uint8_t owner) {
    de->reserved       = 0;
    de->crt_time_tenth = (uint8_t)(META_OWNER_BASE + owner);
    de->crt_time       = (uint16_t)((de->crt_time & 0xFF00) | (mode | FAT_PERM_MARK));
}
static uint32_t free_cached;        /* free clusters, kept by fat_set_entry */
static int      free_known;         /* ...once the first fat_space has counted */

/* uid stamped as owner on files/bowls created from here on (set via login). */
static uint8_t  creator_uid     = 0;

void fat_set_creator(uint8_t uid) { creator_uid = uid; }
static uint8_t (*creator_hook)(void);
void fat_set_creator_hook(uint8_t (*hook)(void)) { creator_hook = hook; }
static uint8_t creator_now(void) { return creator_hook ? creator_hook() : creator_uid; }
static int (*reserve_hook)(void);
void fat_set_reserve_hook(int (*hook)(void)) { reserve_hook = hook; }
static uint32_t fat_free_clusters_impl(void);

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
/* Case-insensitive compare, the same folding name_matches uses: FAT names are
 * case-preserving but not case-sensitive, long ones included. */
static int name_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (to_upper(*a) != to_upper(*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

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

/* ---- Long names (VFAT) ----
 *
 * A long name is carried by the entries IMMEDIATELY BEFORE its 8.3 entry, in
 * reverse order: sequence n first, sequence 1 last. Each holds 13 UCS-2
 * characters in three runs, because the fields a real directory entry uses
 * for the cluster and the size had to be stepped around.
 *
 * Every one of them also carries a CHECKSUM OF THE 8.3 NAME, and validating it
 * is the step that gets skipped. Without it, a chain orphaned by a tool that
 * deleted the short entry gets attached to whatever entry follows it, and the
 * listing shows a file under a name that belongs to something else.
 */
static uint8_t lfn_checksum(const char *name83) {   /* 11 bytes, no dot */
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + (uint8_t)name83[i]);
    return sum;
}

/* State while walking one directory. Reset after each 8.3 entry, and kept
 * across sector boundaries because a chain can straddle one. */
typedef struct {
    char    buf[FAT_LFN_MAX];
    uint8_t sum;                /* checksum the chain claims     */
    int     have;               /* any pieces collected          */
    int     broken;             /* a gap or overflow: distrust it */
} lfn_acc_t;

static void lfn_reset(lfn_acc_t *a) {
    a->buf[0] = '\0'; a->sum = 0; a->have = 0; a->broken = 0;
}

/* Fold one LFN entry into the accumulator. */
static void lfn_add(lfn_acc_t *a, const dirent_t *de) {
    const uint8_t *raw = (const uint8_t *)de;
    uint8_t seq = (uint8_t)(raw[0] & 0x1F);
    if (seq == 0 || seq > (FAT_LFN_MAX / 13)) { a->broken = 1; return; }

    if (raw[0] & 0x40) {                 /* last in the chain: starts fresh */
        for (int i = 0; i < FAT_LFN_MAX; i++) a->buf[i] = '\0';
        a->sum    = raw[13];
        a->have   = 1;
        a->broken = 0;
    } else if (!a->have || raw[13] != a->sum) {
        a->broken = 1;                   /* a piece with no head, or a mixed chain */
        return;
    }

    /* The 13 characters, in the three runs VFAT splits them into. */
    static const int off[13] = { 1,3,5,7,9, 14,16,18,20,22,24, 28,30 };
    int base = (seq - 1) * 13;
    for (int k = 0; k < 13; k++) {
        int o = base + k;
        if (o >= FAT_LFN_MAX - 1) { a->broken = 1; return; }
        uint16_t ch = (uint16_t)(raw[off[k]] | (raw[off[k] + 1] << 8));
        if (ch == 0x0000 || ch == 0xFFFF) continue;        /* end or padding */
        a->buf[o] = (ch < 0x80) ? (char)ch : '?';          /* ASCII, or a stand-in */
    }
}

/* The finished name, or NULL if there is not a trustworthy one. */
static const char *lfn_name(const lfn_acc_t *a, const dirent_t *de) {
    if (!a->have || a->broken) return 0;
    if (a->sum != lfn_checksum(de->name)) return 0;   /* belongs to another entry */
    if (!a->buf[0]) return 0;
    return a->buf;
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
    /* Byte 12's case flags (v0.58.2): Linux and mtools store a lowercase
     * name that fits 8.3 (a.txt) as A.TXT with bit 3 "base is lowercase"
     * and bit 4 "extension is lowercase", and no long name. soupOS showed
     * them in capitals. A byte with FAT_PERM_MARK set is soupOS's own
     * pre-v0.41.0 mode, not case flags. */
    uint8_t cf = de->reserved;
    if (!(cf & FAT_PERM_MARK) && (cf & 0x18)) {
        int in_ext = 0;
        for (char *c = out->name; *c; c++) {
            if (*c == '.') { in_ext = 1; continue; }
            if (*c >= 'A' && *c <= 'Z' && (in_ext ? (cf & 0x10) : (cf & 0x08))) *c = (char)(*c + 32);
        }
    }
    out->lfn[0]  = '\0';
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
/* One path component. The limit is FAT_LFN_MAX, not FAT_NAME_MAX: a component
 * can be a long name now, and capping at 12 characters silently truncated
 * "A Long Recipe Name.txt" to "A Long Recip" so nothing ever matched it. */
static int next_component(const char **pp, char *comp) {
    const char *p = *pp;
    while (*p == '/') p++;
    if (!*p) { *pp = p; return 0; }
    int i = 0;
    while (*p && *p != '/') {
        if (i < FAT_LFN_MAX - 1) comp[i++] = *p;
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
    lfn_acc_t acc;
    lfn_reset(&acc);
    for (uint32_t s = 0; ; s++) {
        uint32_t lba;
        int r = dir_sector_lba(dir_cl, s, &lba);
        if (r <= 0) return r;                 /* 0 = end, -1 = error */
        if (ata_read_sector(lba, sec_buf) < 0) return -1;
        dirent_t *dir = (dirent_t *)sec_buf;
        for (int i = 0; i < eps; i++) {
            if ((uint8_t)dir[i].name[0] == 0x00) return 0;   /* end of dir */

            if (dir[i].attr == FAT_ATTR_LFN &&
                (uint8_t)dir[i].name[0] != 0xE5) { lfn_add(&acc, &dir[i]); continue; }
            if (!is_valid(&dir[i])) { lfn_reset(&acc); continue; }

            /* Either name opens the file: the 8.3 one it has always had, or
             * the long one a person would actually type. */
            const char *lng = lfn_name(&acc, &dir[i]);
            int hit = name_matches(name, &dir[i]) ||
                      (lng && name_eq_ci(name, lng));
            lfn_reset(&acc);
            if (hit) {
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
    char comp[FAT_LFN_MAX];
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
 * and that component's name (leaf, buffer >= FAT_LFN_MAX).
 * Returns 0 ok, 1 if `path` names the root itself (leaf is ""), -1 if an
 * intermediate component is missing or is not a directory. */
static int path_split(const char *path, uint32_t *parent_cl, char *leaf) {
    const char *p = path ? path : "";
    char comp[FAT_LFN_MAX];
    char prev[FAT_LFN_MAX];
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
    volume_sectors = total_sec;
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
    free_known  = 0;            /* a fresh mount: count again on first ask */
    return 0;
}

int         fat_get_type(void)  { return fs_type_val; }
const char *fat_label(void)     { return vol_label;   }

/* ---- Public: fat_ls ---- */

static int fat_ls_impl(const char *path, fat_entry_t *out, int max, int skip) {
    if (!initialized) return -1;

    uint32_t dir_cl;
    if (dir_resolve(path ? path : "/", &dir_cl) < 0) return -1;

    int count = 0;
    int eps   = bps / 32;
    lfn_acc_t acc;
    lfn_reset(&acc);                       /* outside the loop: chains span sectors */

    for (uint32_t s = 0; count < max; s++) {
        uint32_t lba;
        int r = dir_sector_lba(dir_cl, s, &lba);
        if (r <= 0) break;
        if (ata_read_sector(lba, sec_buf) < 0) break;
        dirent_t *dir = (dirent_t *)sec_buf;
        for (int i = 0; i < eps && count < max; i++) {
            if ((uint8_t)dir[i].name[0] == 0x00) return count;  /* end of dir */

            if (dir[i].attr == FAT_ATTR_LFN &&
                (uint8_t)dir[i].name[0] != 0xE5) { lfn_add(&acc, &dir[i]); continue; }

            if (!is_valid(&dir[i]))    { lfn_reset(&acc); continue; }
            if (dir[i].name[0] == '.') { lfn_reset(&acc); continue; }

            if (skip > 0) { skip--; lfn_reset(&acc); continue; }   /* fat_ls_from (v0.60.125) */
            fill_entry(&dir[i], &out[count]);
            const char *lng = lfn_name(&acc, &dir[i]);
            if (lng) strncpy(out[count].lfn, lng, FAT_LFN_MAX - 1);
            count++;
            lfn_reset(&acc);
        }
    }
    return count;
}

/* ---- Public: fat_read ---- */

static int fat_read_impl(const char *path, uint8_t *buf, uint32_t bufsize, uint32_t *out_size) {
    if (!initialized || !path || !buf) return -1;

    uint32_t parent_cl;
    char     leaf[FAT_LFN_MAX];
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
/* The free-cluster count, kept up to date here so fat_space does not have to
 * read the whole FAT from disk to answer. Counting 16K entries was 60-70 ms
 * of PIO per call, which made `larder` sluggish and `kitchen` cost 7% of the
 * cpu to watch. The first fat_space after a mount still scans once; every
 * cluster change after that goes through fat_set_entry and adjusts it. */

static int fat_set_entry(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = (fs_type_val == 32) ? cluster * 4 : cluster * 2;
    uint32_t lba = fat_start + fat_offset / bps;
    uint32_t off = fat_offset % bps;

    if (lba != fat_cache_lba) {
        if (fat_flush_cache() < 0) return -1;
        if (ata_read_sector(lba, fat_buf) < 0) return -1;
        fat_cache_lba = lba;
    }

    uint32_t old, now;
    if (fs_type_val == 32) {
        uint32_t existing = *(uint32_t *)(fat_buf + off);
        old = existing & 0x0FFFFFFFu;
        now = value & 0x0FFFFFFFu;
        *(uint32_t *)(fat_buf + off) = (existing & 0xF0000000u) | now;
    } else {
        old = *(uint16_t *)(fat_buf + off);
        now = (uint16_t)value;
        *(uint16_t *)(fat_buf + off) = (uint16_t)now;
    }
    if (free_known) {
        if (old == 0 && now != 0)      free_cached--;
        else if (old != 0 && now == 0) free_cached++;
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
    if (reserve_hook && fat_free_clusters_impl() <= FAT_RESERVE_CLUSTERS && !reserve_hook()) {
        static uint32_t said;
        if (timer_get_ticks() - said > 500 || !said) {          /* once in 5 s, not per cluster */
            said = timer_get_ticks() | 1;
            klog("[fat] only the reserve is left: refused a cluster to uid %u\n", creator_now());
        }
        return 0;
    }
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


/* ---- Writing long names ----
 *
 * A name that will not fit 8.3 needs two things on disk: an alias that DOES
 * fit, because every tool that cannot read long names still has to see the
 * file, and a chain of LFN entries immediately before it carrying the real
 * name. The chain is stored in reverse - highest sequence number first - and
 * every entry repeats the checksum of the alias, which is what binds the two
 * together.
 */

/* Does this name need a long entry at all? Anything that does not survive
 * being folded into 8.3 unchanged does: too long, lower case, spaces, or more
 * than one dot. */
static int needs_lfn(const char *name) {
    int dots = 0, base = 0, ext = 0, seen_dot = 0;
    for (const char *p = name; *p; p++) {
        if (*p == '.') { dots++; seen_dot = 1; continue; }
        if (*p == ' ') return 1;
        if (*p >= 'a' && *p <= 'z') return 1;
        if (seen_dot) ext++; else base++;
    }
    if (dots > 1) return 1;
    return base > 8 || ext > 3;
}

/* A character the 8.3 half can hold. */
static char alias_char(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    if (c == '-' || c == '_' || c == '~') return c;
    return '_';
}

/* Build an 8.3 alias for `name` that no entry in `dir_cl` already uses. The
 * tail is ~1, ~2, ... which is the convention every other implementation
 * follows, and the uniqueness check is a real directory lookup rather than a
 * guess. */
static int make_alias(uint32_t dir_cl, const char *name, char fname[8], char fext[3]) {
    const char *dot = 0;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;

    char stem[8];
    int  sn = 0;
    for (const char *p = name; *p && (!dot || p < dot) && sn < 6; p++) {
        if (*p == ' ' || *p == '.') continue;      /* dropped, not folded */
        stem[sn++] = alias_char(*p);
    }
    if (!sn) { stem[0] = '_'; sn = 1; }

    for (int i = 0; i < 3; i++) fext[i] = ' ';
    if (dot) {
        int e = 0;
        for (const char *p = dot + 1; *p && e < 3; p++) fext[e++] = alias_char(*p);
    }

    for (int n = 1; n <= 99; n++) {
        int i = 0;
        for (; i < sn && i < (n < 10 ? 6 : 5); i++) fname[i] = stem[i];
        fname[i++] = '~';
        if (n >= 10) fname[i++] = (char)('0' + n / 10);
        fname[i++] = (char)('0' + n % 10);
        while (i < 8) fname[i++] = ' ';

        char probe[FAT_NAME_MAX];
        format_83(fname, fext, probe);
        if (dir_find(dir_cl, probe, 0, 0, 0) != 1) return 0;   /* free */
    }
    return -1;
}

/* Logical slot number -> where it lives. Slots are numbered across the whole
 * directory, so a run can cross a sector boundary without the caller caring. */
static int dir_slot_at(uint32_t dir_cl, uint32_t slot, uint32_t *lba, int *idx) {
    int eps = bps / 32;
    int r = dir_sector_lba(dir_cl, slot / (uint32_t)eps, lba);
    if (r <= 0) return r;
    *idx = (int)(slot % (uint32_t)eps);
    return 1;
}

/* First run of `count` consecutive free slots. */
static int dir_alloc_run(uint32_t dir_cl, int count, uint32_t *out_slot) {
    uint32_t run_start = 0;
    int      run = 0;
    for (uint32_t slot = 0; slot < 65536; slot++) {
        uint32_t lba; int idx;
        int r = dir_slot_at(dir_cl, slot, &lba, &idx);
        if (r <= 0) break;                       /* end of directory */
        if (ata_read_sector(lba, sec_buf) < 0) return -1;
        uint8_t f = (uint8_t)((dirent_t *)sec_buf)[idx].name[0];
        if (f == 0x00 || f == 0xE5) {
            if (!run) run_start = slot;
            if (++run == count) { *out_slot = run_start; return 0; }
        } else {
            run = 0;
        }
    }
    return -1;      /* no run that long; the caller falls back to 8.3 only */
}

/* Erase the long-name entries that belong to the short entry at (e_lba,
 * e_idx) in directory dir_cl: the slots just before it that are LFN entries
 * carrying its checksum (v0.57.6). Until then a delete marked only the short
 * entry, and fsck found the long name orphaned: "Orphaned long file name
 * part". Call before the short entry is marked deleted. */
static int erase_lfn_before(uint32_t dir_cl, uint32_t e_lba, int e_idx) {
    if (ata_read_sector(e_lba, sec_buf) < 0) return -1;
    uint8_t sum = lfn_checksum(((dirent_t *)sec_buf)[e_idx].name);
    uint32_t slot = 0, l; int i;
    for (;; slot++) {
        if (slot >= 65536 || dir_slot_at(dir_cl, slot, &l, &i) <= 0) return 0;
        if (l == e_lba && i == e_idx) break;
    }
    while (slot > 0) {
        slot--;
        if (dir_slot_at(dir_cl, slot, &l, &i) <= 0) break;
        if (ata_read_sector(l, sec_buf) < 0) return -1;
        uint8_t *raw = (uint8_t *)&((dirent_t *)sec_buf)[i];
        if (raw[11] != FAT_ATTR_LFN || raw[0] == 0xE5 || raw[13] != sum) break;
        raw[0] = 0xE5;
        if (ata_write_sector(l, sec_buf) < 0) return -1;
    }
    return 0;
}

/* Write the LFN chain for `name` into the `n` slots before the short entry. */
static int write_lfn_chain(uint32_t dir_cl, uint32_t first_slot, int n,
                           const char *name, uint8_t sum) {
    int len = (int)strlen(name);
    static const int off[13] = { 1,3,5,7,9, 14,16,18,20,22,24, 28,30 };

    for (int e = 0; e < n; e++) {
        int seq = n - e;                         /* highest first, on disk */
        uint32_t lba; int idx;
        if (dir_slot_at(dir_cl, first_slot + (uint32_t)e, &lba, &idx) <= 0) return -1;
        if (ata_read_sector(lba, sec_buf) < 0) return -1;

        uint8_t *raw = (uint8_t *)&((dirent_t *)sec_buf)[idx];
        for (int i = 0; i < 32; i++) raw[i] = 0;
        raw[0]  = (uint8_t)(seq | (e == 0 ? 0x40 : 0));   /* first on disk ends it */
        raw[11] = FAT_ATTR_LFN;
        raw[13] = sum;

        int base = (seq - 1) * 13;
        for (int k = 0; k < 13; k++) {
            int o = base + k;
            uint16_t ch;
            if (o < len)       ch = (uint16_t)(uint8_t)name[o];
            else if (o == len) ch = 0x0000;                /* terminator */
            else               ch = 0xFFFF;                /* padding    */
            raw[off[k]]     = (uint8_t)(ch & 0xFF);
            raw[off[k] + 1] = (uint8_t)(ch >> 8);
        }
        if (ata_write_sector(lba, sec_buf) < 0) return -1;
    }
    return 0;
}

/* Where a whole-file write gets its bytes (v0.56.4): a plain buffer, or
 * anything that can hand them over in order - the VFS keeps a redirect's
 * output in a list of pages, not one block of the 8 MB kernel heap. */
static int mem_source(void *ctx, uint32_t off, uint8_t *dst, uint32_t n) {
    memcpy(dst, (const uint8_t *)ctx + off, n);
    return 0;
}

static int fat_write_src_impl(const char *path, uint32_t size, fat_source_fn src, void *ctx);
static int fat_write_impl(const char *path, const uint8_t *buf, uint32_t size) {
    return fat_write_src_impl(path, size, mem_source, (void *)buf);
}

static int fat_write_src_impl(const char *path, uint32_t size, fat_source_fn src, void *ctx) {
    if (!initialized || !path || fs_type_val == 32) return -1;

    uint32_t parent_cl;
    char     leaf[FAT_LFN_MAX];
    if (path_split(path, &parent_cl, leaf) != 0 || !leaf[0]) return -1;

    /* A name that does not survive 8.3 needs an alias plus a chain of long
     * entries in front of it. Both are decided here, before any cluster is
     * touched, so a directory with no room for the chain fails early rather
     * than leaving a file nothing can name. */
    char fname[8], fext[3];
    int  want_lfn = needs_lfn(leaf);
    int  lfn_ents = want_lfn ? (((int)strlen(leaf) + 12) / 13) : 0;

    /* Overwriting keeps the file's own short name. Until v0.46.0 the alias
     * was made first, and the existing file's alias (~1) made ~1 look taken,
     * so an overwrite renamed the short entry to ~2 while its long-name
     * entries kept ~1's checksum: the long name was orphaned, mtools could no
     * longer find the file by it, and fsck reported a wrong checksum. Found by
     * users-test reading /etc/kitchen back after `hire` rewrote it. */
    dirent_t existing;
    uint32_t ex_lba;
    int      ex_idx;
    int      exists = (dir_find(parent_cl, leaf, &existing, &ex_lba, &ex_idx) == 1);
    if (exists) {
        memcpy(fname, existing.name, 8);
        memcpy(fext,  existing.ext,  3);
        want_lfn = 0;                      /* its chain is already there and matches */
    } else if (want_lfn) {
        if (make_alias(parent_cl, leaf, fname, fext) < 0) return -1;
    } else {
        parse_83(leaf, fname, fext);
    }

    /* Existing entry -> free its chain and reuse the slot; else find a slot.
     * An overwrite keeps the file's existing owner/mode; a new file is
     * stamped with the current creator uid and default permissions. */
    uint32_t dir_lba    = 0;
    int      dir_idx    = -1;
    uint32_t lfn_slot   = 0;
    int      lfn_count  = 0;
    uint8_t  ent_mode   = FAT_PERM_DEFAULT;
    uint8_t  ent_owner  = creator_now();
    dirent_t de;
    uint32_t e_lba;
    int      e_idx;
    if (exists) {
        de = existing; e_lba = ex_lba; e_idx = ex_idx;
        if (de.attr & FAT_ATTR_DIR) return -1;     /* can't overwrite a bowl */
        uint32_t old = ((uint32_t)de.cluster_hi << 16) | de.cluster_lo;
        if (old >= 2 && fat_free_chain(old) < 0) return -1;
        uint8_t pm, po;
        if (meta_get(&de, &pm, &po)) {             /* keep prior ownership */
            ent_mode  = pm;
            ent_owner = po;
        }
        dir_lba = e_lba;
        dir_idx = e_idx;
    } else {
        if (want_lfn) {
            /* The chain and the short entry have to be consecutive, so the
             * run is reserved as one piece. If the directory cannot offer
             * one, fall back to an 8.3-only name rather than failing the
             * write: a file under an ugly name beats no file. */
            uint32_t first;
            if (dir_alloc_run(parent_cl, lfn_ents + 1, &first) == 0) {
                lfn_slot  = first;
                lfn_count = lfn_ents;
                if (dir_slot_at(parent_cl, first + (uint32_t)lfn_ents,
                                &dir_lba, &dir_idx) <= 0) return -1;
            } else {
                want_lfn = 0;
                parse_83(leaf, fname, fext);
                if (dir_alloc_slot(parent_cl, &dir_lba, &dir_idx) < 0) return -1;
            }
        } else if (dir_alloc_slot(parent_cl, &dir_lba, &dir_idx) < 0) {
            return -1;
        }
    }

    /* Allocate clusters and write data. */
    uint32_t first_cluster = 0, prev_cluster = 0;
    uint32_t written       = 0;
    uint32_t cluster_bytes = (uint32_t)spc * bps;
    uint32_t clusters_need = size ? (size + cluster_bytes - 1) / cluster_bytes : 0;

    /* Every failure below goes to `fail`, which gives the clusters back.
     * Returning early instead would strand them: they are marked in use in
     * the FAT, and the directory entry that would reference them is not
     * written until after this loop, so nothing on the disk points at them
     * and nothing ever will. That is space lost until a reformat, and the
     * easiest way to hit it is a write that runs out of room halfway. */
    for (uint32_t c = 0; c < clusters_need; c++) {
        uint32_t cl = fat_alloc_cluster();
        if (cl == 0) goto fail;                 /* out of space */

        if (!first_cluster) first_cluster = cl;
        if (prev_cluster && fat_set_entry(prev_cluster, cl) < 0) {
            /* Allocated but not yet linked, so the chain walk below cannot
             * reach it: hand this one back on its own. */
            fat_set_entry(cl, 0);
            goto fail;
        }

        uint32_t lba = cluster_to_lba(cl);
        for (uint32_t s = 0; s < spc; s++) {
            memset(sec_buf, 0, bps);
            uint32_t to_copy = (written < size) ? (size - written) : 0;
            if (to_copy > bps) to_copy = bps;
            if (to_copy && src(ctx, written, sec_buf, to_copy) < 0) goto fail;
            if (ata_write_sector(lba + s, sec_buf) < 0) goto fail;
            written += to_copy;
        }
        prev_cluster = cl;
    }

    if (fat_flush_cache() < 0) goto fail;

    /* The long-name chain first: it sits in the slots immediately before the
     * short entry, and every entry carries the checksum of the alias, which is
     * what a reader uses to decide the chain belongs to this file. */
    if (lfn_count > 0) {
        char alias[11];
        for (int i = 0; i < 8; i++) alias[i] = fname[i];
        for (int i = 0; i < 3; i++) alias[8 + i] = fext[i];
        if (write_lfn_chain(parent_cl, lfn_slot, lfn_count, leaf,
                            lfn_checksum(alias)) < 0) goto fail;
    }

    /* Write the directory entry. */
    if (ata_read_sector(dir_lba, sec_buf) < 0) goto fail;
    dirent_t *ent = (dirent_t *)sec_buf + dir_idx;
    memset(ent, 0, sizeof(*ent));
    memcpy(ent->name, fname, 8);
    memcpy(ent->ext,  fext,  3);
    ent->attr           = FAT_ATTR_ARCHIVE;
    meta_put(ent, ent_mode, ent_owner);        /* soupOS owner and mode   */
    stamp(ent, !exists, exists ? existing.crt_date : 0);
    ent->cluster_lo     = (uint16_t)(first_cluster & 0xFFFF);
    ent->file_size      = size;
    /* Past this point the entry references the chain, so a failure here must
     * NOT free it: that would be a dangling directory entry, which is worse
     * than lost space. */
    return ata_write_sector(dir_lba, sec_buf);

fail:
    if (first_cluster) {
        fat_free_chain(first_cluster);
        fat_flush_cache();
    }
    return -1;
}

/* ---- Public: fat_space ----
 *
 * Counting free clusters means walking the FAT, which the sector cache makes
 * cheap enough for a shell command. There was no way to see free space from
 * inside soupOS before, which is part of why a cluster leak could sit in the
 * write path unnoticed. */
static uint32_t fat_free_clusters_impl(void) {
    if (!initialized) return 0;
    if (!free_known) {
        uint32_t t0 = timer_get_ticks();
        uint32_t last = cluster_count + 1, n = 0;
        for (uint32_t c = 2; c <= last; c++)
            if (fat_get_entry(c) == 0) n++;
        free_cached = n;
        free_known  = 1;
        /* Once per mount, and it reads the whole FAT: a cheap yardstick for
         * the disk path (64 sectors here; 6-7 ticks with word-at-a-time PIO). */
        klog("[fat] counted %u free clusters in %u ticks\n", n, timer_get_ticks() - t0);
    }
    return free_cached;
}

uint32_t fat_volume_sectors(void) { return initialized ? volume_sectors : 0; }

/* For du (v0.53.4): the bytes in one cluster, and how many clusters a chain
 * starting at `first` holds, walked through the FAT as mdu does. A chain
 * longer than the volume is a loop; the count stops there. */
uint32_t fat_cluster_bytes(void) { return initialized ? (uint32_t)spc * bps : 0; }

uint32_t fat_chain_clusters(uint32_t first) {
    if (!initialized || first < 2) return 0;
    mutex_lock(&fat_mtx);
    uint32_t n = 0, c = first, cap = volume_sectors / (spc ? spc : 1) + 2;
    while (c >= 2 && !is_eoc(c) && n < cap) { n++; c = fat_get_entry(c); }
    mutex_unlock(&fat_mtx);
    return n;
}

void fat_space(uint32_t *out_free, uint32_t *out_total, uint32_t *out_cluster_bytes) {
    mutex_lock(&fat_mtx);
    if (out_free)  *out_free  = fat_free_clusters_impl();
    if (out_total) *out_total = initialized ? cluster_count : 0;
    if (out_cluster_bytes)
        *out_cluster_bytes = initialized ? (uint32_t)spc * bps : 0;
    mutex_unlock(&fat_mtx);
}

/* ---- Public: fat_mkdir ---- */

static int fat_mkdir_impl(const char *path) {
    if (!initialized || !path || fs_type_val == 32) return -1;

    uint32_t parent_cl;
    char     leaf[FAT_LFN_MAX];
    if (path_split(path, &parent_cl, leaf) != 0 || !leaf[0]) return -1;

    /* Must not already exist. */
    if (dir_find(parent_cl, leaf, 0, 0, 0) == 1) return -1;

    /* A name 8.3 cannot hold gets an alias and a long-name chain, as a file
     * does in fat_write (v0.57.6). Until then `mkbowl "/a bowl"` stored the
     * bowl as A BOWL - a space inside an 8.3 name - and nothing could find
     * it again. */
    char fname[8], fext[3];
    int  want_lfn = needs_lfn(leaf);
    int  lfn_ents = want_lfn ? (((int)strlen(leaf) + 12) / 13) : 0;
    if (want_lfn) { if (make_alias(parent_cl, leaf, fname, fext) < 0) return -1; }
    else parse_83(leaf, fname, fext);

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

    /* Add the directory entry to the parent: after its long-name chain if
     * it has one, which needs a run of consecutive slots. */
    uint32_t slot_lba, lfn_slot = 0;
    int      slot_idx, lfn_count = 0;
    if (want_lfn && dir_alloc_run(parent_cl, lfn_ents + 1, &lfn_slot) == 0) {
        lfn_count = lfn_ents;
        if (dir_slot_at(parent_cl, lfn_slot + (uint32_t)lfn_ents, &slot_lba, &slot_idx) <= 0) {
            fat_free_chain(newcl); fat_flush_cache(); return -1;
        }
    } else {
        if (want_lfn) parse_83(leaf, fname, fext);     /* no run: an 8.3 name, as fat_write does */
        if (dir_alloc_slot(parent_cl, &slot_lba, &slot_idx) < 0) {
            fat_free_chain(newcl);
            fat_flush_cache();
            return -1;
        }
    }
    if (lfn_count > 0) {
        char alias[11];
        for (int i = 0; i < 8; i++) alias[i] = fname[i];
        for (int i = 0; i < 3; i++) alias[8 + i] = fext[i];
        if (write_lfn_chain(parent_cl, lfn_slot, lfn_count, leaf, lfn_checksum(alias)) < 0) return -1;
    }
    if (ata_read_sector(slot_lba, sec_buf) < 0) return -1;
    dirent_t *ent = (dirent_t *)sec_buf + slot_idx;
    memset(ent, 0, sizeof(*ent));
    memcpy(ent->name, fname, 8);
    memcpy(ent->ext,  fext,  3);
    ent->attr           = FAT_ATTR_DIR;
    meta_put(ent, FAT_PERM_DEFAULT, creator_now());   /* soupOS owner and mode */
    stamp(ent, 1, 0);
    ent->cluster_lo     = (uint16_t)(newcl & 0xFFFF);
    ent->file_size      = 0;
    return ata_write_sector(slot_lba, sec_buf);
}

/* ---- Public: fat_rmdir ---- */

static int fat_rmdir_impl(const char *path) {
    if (!initialized || !path || fs_type_val == 32) return -1;

    uint32_t parent_cl;
    char     leaf[FAT_LFN_MAX];
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
    if (erase_lfn_before(parent_cl, e_lba, e_idx) < 0) return -1;   /* its long name too */
    if (ata_read_sector(e_lba, sec_buf) < 0) return -1;
    ((dirent_t *)sec_buf)[e_idx].name[0] = (char)0xE5;
    return ata_write_sector(e_lba, sec_buf);
}

/* ---- Public: fat_is_dir / fat_exists ---- */

static int fat_is_dir_impl(const char *path) {
    if (!initialized) return 0;
    uint32_t parent_cl;
    char     leaf[FAT_LFN_MAX];
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
    char     leaf[FAT_LFN_MAX];
    int sr = path_split(path, &parent_cl, leaf);
    if (sr == 1) return 1;
    if (sr != 0 || !leaf[0]) return 0;
    return dir_find(parent_cl, leaf, 0, 0, 0) == 1;
}

/* ---- Public: fat_stat / fat_chmod / fat_chown ---- */

static int fat_stat_impl(const char *path, uint8_t *owner, uint8_t *mode) {
    if (!initialized) return -1;
    uint32_t parent_cl;
    char     leaf[FAT_LFN_MAX];
    int sr = path_split(path, &parent_cl, leaf);
    if (sr == 1) {                          /* the root */
        if (owner) *owner = 0;
        if (mode)  *mode  = FAT_PERM_DEFAULT;
        return 0;
    }
    if (sr != 0 || !leaf[0]) return -1;
    dirent_t de;
    if (dir_find(parent_cl, leaf, &de, 0, 0) != 1) return -1;

    uint8_t m, o;
    if (!meta_get(&de, &m, &o)) {            /* foreign entry -> defaults */
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
    char     leaf[FAT_LFN_MAX];
    if (path_split(path, &parent_cl, leaf) != 0 || !leaf[0]) return -1;
    dirent_t de;
    uint32_t e_lba;
    int      e_idx;
    if (dir_find(parent_cl, leaf, &de, &e_lba, &e_idx) != 1) return -1;
    if (ata_read_sector(e_lba, sec_buf) < 0) return -1;
    dirent_t *ent = (dirent_t *)sec_buf + e_idx;
    meta_put(ent, mode, owner);
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
    char     leaf[FAT_LFN_MAX];
    if (path_split(path, &parent_cl, leaf) != 0 || !leaf[0]) return -1;

    dirent_t de;
    uint32_t e_lba;
    int      e_idx;
    if (dir_find(parent_cl, leaf, &de, &e_lba, &e_idx) != 1) return -1;
    if (de.attr & FAT_ATTR_DIR) return -1;          /* use fat_rmdir */

    uint32_t cl = ((uint32_t)de.cluster_hi << 16) | de.cluster_lo;
    if (cl >= 2 && fat_free_chain(cl) < 0) return -1;
    if (fat_flush_cache() < 0) return -1;
    if (erase_lfn_before(parent_cl, e_lba, e_idx) < 0) return -1;   /* its long name too */
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
    char     leaf[FAT_LFN_MAX];
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
    return fat_ls_from(path, out, max, 0);
}

int fat_ls_from(const char *path, fat_entry_t *out, int max, int skip) {
    mutex_lock(&fat_mtx);
    int _r = fat_ls_impl(path, out, max, skip);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_read(const char *path, uint8_t *buf, uint32_t bufsize, uint32_t *out_size) {
    mutex_lock(&fat_mtx);
    int _r = fat_read_impl(path, buf, bufsize, out_size);
    mutex_unlock(&fat_mtx);
    return _r;
}

int fat_write_from(const char *path, uint32_t size, fat_source_fn src, void *ctx) {
    mutex_lock(&fat_mtx);
    int _r = fat_write_src_impl(path, size, src, ctx);
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

/* The first cluster of a path's chain; 0 for the root (FAT16 keeps it in a
 * fixed region, not a chain) or a missing path. For du (v0.53.4). */
uint32_t fat_first_cluster(const char *path) {
    mutex_lock(&fat_mtx);
    uint32_t cl = 0, parent_cl;
    char leaf[FAT_LFN_MAX];
    dirent_t de;
    if (initialized && path_split(path, &parent_cl, leaf) == 0 && leaf[0]
        && dir_find(parent_cl, leaf, &de, 0, 0) == 1)
        cl = ((uint32_t)de.cluster_hi << 16) | de.cluster_lo;
    mutex_unlock(&fat_mtx);
    return cl;
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
