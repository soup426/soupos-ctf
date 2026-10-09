#pragma once
#include <stdint.h>

/* Directory entry attribute bits */
#define FAT_ATTR_RDONLY  0x01
#define FAT_ATTR_HIDDEN  0x02
#define FAT_ATTR_SYSTEM  0x04
#define FAT_ATTR_VOLID   0x08
#define FAT_ATTR_DIR     0x10
#define FAT_ATTR_ARCHIVE 0x20
#define FAT_ATTR_LFN     0x0F   /* all four low bits set */

#define FAT_NAME_MAX  13        /* 8 + '.' + 3 + '\0' */
/* Long names, as VFAT stores them. 63 is a working limit rather than the
 * format's 255: an entry sits in arrays of FAT_LS_MAX, so every byte here
 * costs 128 of them, and nothing on this disk needs more. */
#define FAT_LFN_MAX   64
#define FAT_PATH_MAX  128       /* absolute path buffer size */
#define FAT_LS_MAX    128

typedef struct {
    char     name[FAT_NAME_MAX];   /* the 8.3 name, always present      */
    char     lfn[FAT_LFN_MAX];     /* the long name, or "" if it has none */
    uint8_t  attr;
    uint32_t cluster;
    uint32_t size;
} fat_entry_t;

/* What to show a person: the long name when there is one. */
static inline const char *fat_display_name(const fat_entry_t *e) {
    return e->lfn[0] ? e->lfn : e->name;
}

/* Returns 0=ok, -1=no drive / unrecognised filesystem */
int fat_init(void);

int         fat_get_type(void);   /* 16 or 32 */
const char *fat_label(void);      /* volume label, trimmed */

/* ── Paths ─────────────────────────────────────────────────────────────────
 * Every path-taking call below accepts an absolute path with '/'-separated
 * components, e.g. "/etc/recipe.txt" or "/bowls/inner". A leading '/' is
 * optional (a bare "README.TXT" is taken relative to the root). Directories
 * are called "bowls"; the FAT on-disk DIR attribute backs them.
 * ───────────────────────────────────────────────────────────────────────── */

/* List a directory. Returns entry count written (up to max). -1 on error.
   `.` and `..` are omitted. path "/" lists the root. */
int fat_ls(const char *path, fat_entry_t *out, int max);
/* The same, past the first `skip` entries (v0.60.125): a directory of more
 * than FAT_LS_MAX is listed a piece at a time. */
int  fat_ls_from(const char *path, fat_entry_t *out, int max, int skip);

/* Read a file into buf (up to bufsize bytes). *out_size = actual file size,
   WHICH MAY BE LARGER THAN bufsize: clamp before using it as a byte count.
   (Five callers did not, v0.39.1: an out-of-bounds write in three of them.)
   Returns 0=ok, -1=not found / is a directory / error. */
int fat_read(const char *path, uint8_t *buf, uint32_t bufsize, uint32_t *out_size);

/* Create or overwrite a file. FAT16 only. Returns 0=ok, -1=error. */
int fat_write(const char *path, const uint8_t *buf, uint32_t size);
/* The same whole-file write, pulling its bytes from `src` in order: src(ctx,
 * offset, dst, n) copies n bytes at offset into dst, 0 or -1 (v0.56.4). */
typedef int (*fat_source_fn)(void *ctx, uint32_t off, uint8_t *dst, uint32_t n);
int fat_write_from(const char *path, uint32_t size, fat_source_fn src, void *ctx);

/* Delete a file (not a directory - use fat_rmdir). FAT16 only. */
int fat_delete(const char *path);

/* Free and total data clusters, and the size of one in bytes. Any pointer may
 * be NULL. Walks the FAT, so it is a command-speed call, not a hot path. */
void fat_space(uint32_t *out_free, uint32_t *out_total, uint32_t *out_cluster_bytes);
/* Sectors the volume occupies from the start of the disk; what lies past it
 * belongs to nobody on the FAT side, which is where swap lives. 0 if unmounted. */
uint32_t fat_volume_sectors(void);
uint32_t fat_cluster_bytes(void);              /* bytes per cluster */
uint32_t fat_chain_clusters(uint32_t first);   /* clusters in a chain (du) */
uint32_t fat_first_cluster(const char *path);   /* 0 for the root or missing */

/* Create a directory ("bowl"). FAT16 only. Fails if it already exists,
   the parent is missing, or the disk is full. Returns 0=ok, -1=error. */
int fat_mkdir(const char *path);

/* Remove an empty directory ("bowl"). FAT16 only. Fails if the bowl is
   not empty, is not a directory, or does not exist. Returns 0=ok, -1=err. */
int fat_rmdir(const char *path);

/* 1 if path exists and is a directory, else 0. The root is a directory. */
int fat_is_dir(const char *path);

/* 1 if path exists (file or directory), else 0. */
int fat_exists(const char *path);

/* ── Permissions ───────────────────────────────────────────────────────────
 * soupOS stamps an owner uid and an rwx mode into the creation-time bytes of
 * every FAT directory entry (bytes 13-14; see meta_get in fat.c, v0.41.0). The mode byte's high bit (FAT_PERM_MARK) flags the
 * metadata as present; entries without it (files written by other tools)
 * read back as owned by headchef (uid 0) with default permissions.
 * Enforcement is advisory - done by the shell, since soupOS runs in ring 0.
 * ───────────────────────────────────────────────────────────────────────── */
#define FAT_PERM_MARK  0x80   /* mode byte: soupOS metadata present  */
#define FAT_PERM_OR    0x20   /* owner read                          */
#define FAT_PERM_OW    0x10   /* owner write                         */
#define FAT_PERM_OX    0x08   /* owner execute / enter-bowl          */
#define FAT_PERM_AR    0x04   /* all-cooks read                      */
#define FAT_PERM_AW    0x02   /* all-cooks write                     */
#define FAT_PERM_AX    0x01   /* all-cooks execute                   */
/* default for new files/bowls: owner rwx, other cooks r-x */
#define FAT_PERM_DEFAULT \
    (FAT_PERM_MARK|FAT_PERM_OR|FAT_PERM_OW|FAT_PERM_OX|FAT_PERM_AR|FAT_PERM_AX)

/* uid stamped as owner on files/bowls created from here on. */
void fat_set_creator(uint8_t uid);
/* If set, consulted instead of the fat_set_creator value: the uid of the
 * task creating the file, so a file made from an SSH session is that cook's. */
void fat_set_creator_hook(uint8_t (*hook)(void));
/* The last FAT_RESERVE_CLUSTERS free clusters are kept for writes the hook
 * allows (the headchef's, and the kernel's own on anyone's behalf): one
 * cook filling the disk used to stop the headchef opening the vault, which
 * writes its host key (v0.55.5). No hook: no reserve. */
#define FAT_RESERVE_CLUSTERS 128
void fat_set_reserve_hook(int (*hook)(void));

/* Read a path's owner uid and permission mode. Returns 0=ok, -1=missing. */
int  fat_stat (const char *path, uint8_t *owner, uint8_t *mode);

/* Change a path's permission mode / owner. FAT16 only. 0=ok, -1=error. */
int  fat_chmod(const char *path, uint8_t mode);
int  fat_chown(const char *path, uint8_t owner);

/* ── Seekable file handles ─────────────────────────────────────────────────
 * Up to FAT_MAX_FD files may be open simultaneously.
 * fd values are 0..FAT_MAX_FD-1; -1 = error.
 *
 * Usage mirrors POSIX:
 *   int fd = fat_fopen("/DOOM1.WAD");
 *   fat_fseek(fd, offset, SEEK_SET);
 *   fat_fread(fd, buf, count);
 *   fat_fclose(fd);
 * ───────────────────────────────────────────────────────────────────────── */
#define FAT_MAX_FD   8
#define FAT_SEEK_SET 0
#define FAT_SEEK_CUR 1
#define FAT_SEEK_END 2

int      fat_fopen (const char *path);       /* returns fd or -1          */
void     fat_fclose(int fd);
int      fat_fread (int fd, void *buf, uint32_t count); /* returns bytes read */
int      fat_fseek (int fd, int32_t offset, int whence);/* 0=ok, -1=err      */
int32_t  fat_ftell (int fd);                 /* current byte position     */
uint32_t fat_fsize (int fd);                 /* file size in bytes        */
int      fat_feof  (int fd);                 /* 1 if at/past end-of-file  */
