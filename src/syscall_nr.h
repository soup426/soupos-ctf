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

/* Reserved file descriptors. fd >= 3 are returned by open(). */
#define FD_STDIN   0   /* keyboard                                          */
#define FD_STDOUT  1   /* screen (VGA)                                      */
#define FD_STDERR  2   /* serial (COM1)                                     */

/* open() modes */
#define O_READ     0
#define O_WRITE    1
