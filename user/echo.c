/* echo.c - print the arguments. Usage: cook echo.elf hello there */
#include "ulib.h"

int main(void) {
    char args[128];
    sys_args(args, sizeof(args));
    print(args);
    print("\n");
    return 0;
}
