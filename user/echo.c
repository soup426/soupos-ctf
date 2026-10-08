/* echo.c - print the arguments. Usage: cook echo.elf hello there */
#include "ulib.h"

int main(void) {
    /* The arguments joined by one space, as sh's echo prints them: quoted
     * spaces are kept, the quotes are not (v0.57.2). */
    char *argv[16];
    int argc = uargv(argv, 16);
    for (int i = 0; i < argc; i++) { if (i) print(" "); print(argv[i]); }
    print("\n");
    return 0;
}
