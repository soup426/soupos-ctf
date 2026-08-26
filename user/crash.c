/* crash.c - deliberately faults, to prove a ring-3 fault kills only this
 * program and leaves the kernel (and the shell) alive.
 *   cook crash.elf        -> null dereference (page fault)
 *   cook crash.elf ud     -> invalid opcode
 */
#include "ulib.h"

int main(void) {
    char a[8];
    sys_args(a, sizeof(a));
    print("crash: about to fault on purpose\n");
    if (a[0] == 'u' && a[1] == 'd') {
        __asm__ volatile (".byte 0x0f, 0x0b");   /* ud2 */
    } else {
        volatile int *p = (volatile int *)0;     /* kernel page, not user */
        *p = 0x41414141;
    }
    print("crash: STILL ALIVE (this should not print)\n");
    return 0;
}
