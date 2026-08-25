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
#define FAT_PATH_MAX  128       /* absolute path buffer size */
#define FAT_LS_MAX    128

typedef struct {
    char     name[FAT_NAME_MAX];
    uint8_t  attr;
    uint32_t cluster;
    uint32_t size;
} fat_entry_t;

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

/* Read a file into buf (up to bufsize bytes). *out_size = actual file size.
   Returns 0=ok, -1=not found / is a directory / error. */
int fat_read(const char *path, uint8_t *buf, uint32_t bufsize, uint32_t *out_size);

/* Create or overwrite a file. FAT16 only. Returns 0=ok, -1=error. */
int fat_write(const char *path, const uint8_t *buf, uint32_t size);

/* Delete a file (not a directory - use fat_rmdir). FAT16 only. */
int fat_delete(const char *path);

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
 * soupOS stamps an owner uid and an rwx mode into two spare bytes of every
 * FAT directory entry. The mode byte's high bit (FAT_PERM_MARK) flags the
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
