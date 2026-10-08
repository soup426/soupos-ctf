/* raise.c - stdin to stdout, uppercased. A pipeline stage with no purpose
 * other than being obviously a pipeline stage:
 *
 *   cook call.elf hello | raise.elf      -> HELLO
 */
#include "ulib.h"

int main(void) {
    char buf[256];
    int got;
    while ((got = sys_read(FD_STDIN, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < got; i++)
            if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] = (char)(buf[i] - 32);
        sys_write(FD_STDOUT, buf, got);
    }
    return 0;
}
