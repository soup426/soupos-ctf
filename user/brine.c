/* brine.c - base64, under a kitchen name (v0.60.106): what keeps.
 *
 *   cook brine.elf FILE         FILE (or stdin) as base64, 76 to a line
 *   cook brine.elf -d FILE      back again
 *
 * As GNU's base64: lines of 76 and a newline after the last, nothing for
 * nothing; -d skips newlines and stops at anything else that is not
 * base64, saying so, status 1.
 */
#include "ulib.h"

static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char out[96];
static int col, on;

static void put(char c) {
    out[on++] = c;
    if (++col == 76) { out[on++] = '\n'; col = 0; }
    if (on >= 80) { sys_write(FD_STDOUT, out, on); on = 0; }
}

static void encode(int fd) {
    unsigned char in[3]; int n = 0, any = 0;
    char buf[192]; int got;
    while ((got = sys_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < got; i++) {
            in[n++] = (unsigned char)buf[i];
            if (n == 3) {
                put(A[in[0] >> 2]); put(A[((in[0] & 3) << 4) | (in[1] >> 4)]);
                put(A[((in[1] & 15) << 2) | (in[2] >> 6)]); put(A[in[2] & 63]);
                n = 0; any = 1;
            }
        }
    }
    if (n) {
        if (n == 1) in[1] = 0;
        put(A[in[0] >> 2]); put(A[((in[0] & 3) << 4) | (in[1] >> 4)]);
        put(n == 2 ? A[(in[1] & 15) << 2] : '='); put('=');
        any = 1;
    }
    if (any && col) out[on++] = '\n';
    if (on) sys_write(FD_STDOUT, out, on);
}

static int val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int decode(int fd) {
    int q[4], n = 0, pad = 0;
    char buf[192], o[192]; int got, on2 = 0;
    while ((got = sys_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < got; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\r') continue;
            if (c == '=') { if (pad || n >= 2) { q[n++] = 0; pad++; } else goto bad; }
            else { int v = val(c); if (v < 0 || pad) goto bad; q[n++] = v; }
            if (n == 4) {
                o[on2++] = (char)((q[0] << 2) | (q[1] >> 4));
                if (pad < 2) o[on2++] = (char)(((q[1] & 15) << 4) | (q[2] >> 2));
                if (pad < 1) o[on2++] = (char)(((q[2] & 3) << 6) | q[3]);
                n = 0;
                if (on2 > 180) { sys_write(FD_STDOUT, o, on2); on2 = 0; }
            }
        }
    }
    if (on2) sys_write(FD_STDOUT, o, on2);
    if (n) goto bad2;
    return 0;
bad:
    if (on2) sys_write(FD_STDOUT, o, on2);
bad2:
    eprint("brine.elf: invalid input\n");          /* stderr, as GNU */
    return 1;
}

int main(void) {
    char *tok[3];
    int nt = uargv(tok, 3), dec = 0, a = 0;
    if (nt > 0 && tok[0][0] == '-' && tok[0][1] == 'd' && !tok[0][2]) { dec = 1; a = 1; }
    if (nt - a > 1) { print("usage: brine.elf [-d] [FILE]\n"); return 2; }
    int fd = FD_STDIN;
    if (nt - a == 1) {
        fd = sys_open(tok[a], O_READ);
        if (fd < 0) { eprint("brine.elf: cannot open "); eprint(tok[a]); eprint("\n"); return 1; }
    }
    int st = 0;
    if (dec) st = decode(fd); else encode(fd);
    if (fd != FD_STDIN) sys_close(fd);
    return st;
}
