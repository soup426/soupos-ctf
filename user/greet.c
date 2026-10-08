/* greet.c - a ring-3 soupOS program built on ulib. */
#include "ulib.h"

int main(void) {
    print("hello from ring 3! (a real user-mode process)\n");  /* -> screen */
    eprint("USERELF: ring3 syscall write ok\n");               /* -> serial */
    return 42;
}
