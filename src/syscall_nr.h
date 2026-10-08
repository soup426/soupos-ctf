#pragma once
/* Syscall numbers - shared by the kernel dispatcher and user programs.
 * Convention (int 0x80): eax = number, ebx/ecx/edx = args, eax = return. */
#define SYS_EXIT   0   /* ebx = exit code                                   */
#define SYS_WRITE  1   /* ebx = fd, ecx = buf, edx = len; -> bytes written  */
#define SYS_READ   2   /* ebx = fd, ecx = buf, edx = max;  -> bytes read     */
#define SYS_YIELD  3   /* cooperative yield (currently a no-op)             */
#define SYS_OPEN   4   /* ebx = path, ecx = mode (0=read,1=write); -> fd/-1 */
#define SYS_CLOSE  5   /* ebx = fd; -> 0/-1                                  */
#define SYS_SBRK   6   /* ebx = increment (signed); -> old break, or -1     */
#define SYS_ARGS   7   /* ebx = buf, ecx = max; -> length of arg string     */
#define SYS_TICKS  8   /* (no args) -> uptime in 100 Hz PIT ticks           */
#define SYS_READDIR 9  /* ebx = path, ecx = index, edx = udirent_t*;
                        * -> 1 entry written, 0 past the end, -1 error     */
#define SYS_UNLINK 10  /* ebx = path; -> 0, or -1 (missing, a bowl, not yours) */
#define SYS_MKDIR  11  /* ebx = path; -> 0, or -1                               */

/* One directory entry for SYS_READDIR (v0.44.0): the long name when the
 * entry has one, else the 8.3 name. Shared by the kernel and ulib. */
typedef struct {
    char     name[64];
    unsigned size;
    unsigned is_dir;
} udirent_t;

/* Reserved file descriptors. fd >= 3 are returned by open(). */
#define FD_STDIN   0   /* keyboard                                          */
#define FD_STDOUT  1   /* screen (VGA)                                      */
#define FD_STDERR  2   /* serial (COM1)                                     */

/* open() modes */
#define O_READ     0
#define O_WRITE    1
