/* call.c - print the arguments. Usage: cook call.elf hello there */
#include "ulib.h"

int main(void) {
    /* The arguments joined by one space, as sh's echo prints them: quoted
     * spaces are kept, the quotes are not (v0.57.2). */
    char *argv[64];                 /* 128 bytes of arguments hold up to 64 (v0.60.0: 16 dropped the 17th) */
    int argc = uargv(argv, 64);
    for (int i = 0; i < argc; i++) { if (i) print(" "); print(argv[i]); }
    print("\n");
    return 0;
}
