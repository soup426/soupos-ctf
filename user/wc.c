/* wc.c - count lines, words and bytes on stdin.
 *
 *   cook echo.elf hello pipes | wc.elf    ->  1 2 12
 *
 * Also emits the counts to serial as a WCOUT marker, so the headless smoke
 * test can read the result of a pipeline rather than infer it.
 */
#include "ulib.h"

int main(void) {
    char buf[256];
    int got, lines = 0, words = 0, bytes = 0, in_word = 0;

    while ((got = sys_read(FD_STDIN, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < got; i++) {
            char c = buf[i];
            bytes++;
            if (c == '\n') lines++;
            if (c == ' ' || c == '\t' || c == '\n') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }

    print_int(lines); print(" ");
    print_int(words); print(" ");
    print_int(bytes); print("\n");

    eprint("WCOUT lines="); eprint_int(lines);
    eprint(" words=");      eprint_int(words);
    eprint(" bytes=");      eprint_int(bytes);
    eprint("\n");
    return 0;
}
