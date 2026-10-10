/* scale.c - a calculator in a window (v0.60.173): the kitchen scale.
 *
 *   cook scale.elf          (from a terminal on the desktop)
 *
 * Buttons clicked or keys typed: digits, ., + - x (or *) /, = (or Enter),
 * C (or c). Left to right, as a pocket calculator: 2 + 3 x 4 = 20. Ring 3
 * has no FPU, so numbers are 64-bit integers in millionths: six decimals,
 * trailing zeros dropped (7 / 2 shows 3.5); past 10^12, or a division by
 * 0, the display says error until C. The display is the kernel's 8x16 font
 * drawn twice the size.
 */
#include "ulib.h"
#include "../src/font8x16.h"

#define SW 240
#define SH 300
#define ONE 1000000LL
#define LIMIT (1000000000000LL * ONE)
static unsigned px[SW * SH];
static const char *const keys[5][4] = {
    { "7", "8", "9", "/" }, { "4", "5", "6", "x" }, { "1", "2", "3", "-" }, { "0", ".", "=", "+" }, { "C", 0, 0, 0 },
};
static long long acc, entry;
static int have_entry, frac, op, err;            /* frac: digits after the point typed, -1 none */
static char shown[24];

static void rect(int x, int y, int w, int h, unsigned c) {
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) if (i >= 0 && j >= 0 && i < SW && j < SH) px[j * SW + i] = c;
}
static void text(int x, int y, const char *s, unsigned fg, int k) {   /* k: 1 or 2 times the font */
    for (; *s; s++, x += 8 * k)
        for (int j = 0; j < 16 * k; j++)
            for (int i = 0; i < 8 * k; i++)
                if ((font8x16[(unsigned char)*s][j / k] >> (7 - i / k)) & 1 && x + i < SW && y + j < SH) px[(y + j) * SW + x + i] = fg;
}
static void fmt(long long v, char *o) {           /* millionths, as text: trailing zeros dropped */
    int n = 0;
    if (v < 0) { o[n++] = '-'; v = -v; }
    long long ip = v / ONE, fp = v % ONE;
    char d[24]; int k = 0;
    do { d[k++] = (char)('0' + ip % 10); ip /= 10; } while (ip);
    while (k) o[n++] = d[--k];
    if (fp) {
        o[n++] = '.';
        for (long long p = ONE / 10; p && fp; p /= 10) { o[n++] = (char)('0' + fp / p); fp %= p; }
    }
    o[n] = 0;
}
static void draw(int id) {
    rect(0, 0, SW, SH, 0x00D8DCE2);
    rect(4, 4, SW - 8, 44, 0x00202428);
    int l = 0; while (shown[l]) l++;
    text(SW - 8 - 16 * l, 10, shown, err ? 0x00F05050 : 0x00F0F0F0, 2);
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 4; c++) {
            if (!keys[r][c]) continue;
            int x = 4 + c * 59, y = 56 + r * 48, w = r == 4 ? SW - 8 : 55;
            rect(x, y, w, 44, r == 4 ? 0x00C0504D : c == 3 || (r == 3 && c == 2) ? 0x003A6EA5 : 0x00F4F1EA);
            text(x + w / 2 - 8, y + 6, keys[r][c], r == 4 || c == 3 || (r == 3 && c == 2) ? 0x00F0F0F0 : 0x00202020, 2);
        }
    sys_win_put(id, px);
}
static void set_error(void) {
    const char *e = "error";
    int i = 0;
    while ((shown[i] = e[i])) i++;
    err = 1;
}
/* |a| x |b| / ONE and |a| / |b| x ONE on magnitudes, without passing 64
 * bits: -1 past LIMIT (or a division by 0). */
static int mul(unsigned long long a, unsigned long long b, unsigned long long *out) {
    unsigned long long ai = a / ONE, af = a % ONE, bi = b / ONE, bf = b % ONE, L = (unsigned long long)LIMIT;
    if (ai && bi > (L / ONE) / ai) return -1;
    unsigned long long r = ai * bi * ONE;              /* < LIMIT */
    if (ai && bf > (L - r) / ai) return -1;
    r += ai * bf;
    if (bi && af > (L - r) / bi) return -1;
    r += af * bi;
    r += af * bf / ONE;                                /* both < ONE: no overflow */
    if (r >= L) return -1;
    *out = r;
    return 0;
}
static int divide(unsigned long long a, unsigned long long b, unsigned long long *out) {
    if (!b) return -1;
    unsigned long long q = a / b, r = a % b, L = (unsigned long long)LIMIT;
    if (q >= L / ONE) return -1;
    q *= ONE;
    for (unsigned long long place = ONE / 10; place; place /= 10) {   /* a decimal at a time: r < b < 10^17 */
        r *= 10;
        q += (r / b) * place;
        r %= b;
    }
    *out = q;
    return 0;
}
static int apply(void) {                          /* acc = acc op entry; 0, or -1 an error */
    long long a = acc, b = entry;
    if (op == '+') a += b;
    else if (op == '-') a -= b;
    else if (op == 'x' || op == '/') {
        int neg = (a < 0) != (b < 0);
        unsigned long long m;
        if ((op == 'x' ? mul : divide)((unsigned long long)(a < 0 ? -a : a), (unsigned long long)(b < 0 ? -b : b), &m) < 0) return -1;
        a = neg ? -(long long)m : (long long)m;
    }
    else a = b;
    if (a >= LIMIT || a <= -LIMIT) return -1;
    acc = a;
    return 0;
}
static void press(int k) {
    if (k == 'c' || k == 'C') { acc = entry = 0; have_entry = 0; frac = -1; op = 0; err = 0; shown[0] = '0'; shown[1] = 0; return; }
    if (err) return;
    if (k >= '0' && k <= '9') {
        if (!have_entry) { entry = 0; have_entry = 1; frac = -1; }
        if (entry >= LIMIT / 100 || frac >= 6) return;   /* r x 10 in divide stays inside 64 bits */
        if (frac < 0) entry = entry * 10 + (k - '0') * ONE;
        else { long long p = ONE; for (int i = 0; i <= frac; i++) p /= 10; entry += (k - '0') * p; frac++; }
        fmt(entry, shown);
        if (frac == 0) { int l = 0; while (shown[l]) l++; shown[l] = '.'; shown[l + 1] = 0; }
        return;
    }
    if (k == '.') {
        if (!have_entry) { entry = 0; have_entry = 1; }
        if (frac < 0) { frac = 0; fmt(entry, shown); int l = 0; while (shown[l]) l++; shown[l] = '.'; shown[l + 1] = 0; }
        return;
    }
    if (k == '*') k = 'x';
    if (k == '\n') k = '=';
    if (k == '+' || k == '-' || k == 'x' || k == '/' || k == '=') {
        if (have_entry && apply() < 0) { set_error(); return; }
        have_entry = 0; frac = -1;
        op = k == '=' ? 0 : k;
        fmt(acc, shown);
    }
}

int main(void) {
    int id = sys_win_open(SW, SH, "scale");
    if (id < 0) { print("scale: it needs the desktop (run countertop, then scale in one of its terminals)\n"); return 1; }
    press('c');
    draw(id);
    for (;;) {
        uwinev_t e;
        int r = sys_win_event(id, &e, 5000);
        if (r < 0) return 0;
        if (r == 0) continue;
        if (e.type == WEV_CLOSE || (e.type == WEV_KEY && e.key == 'q')) break;
        if (e.type == WEV_KEY && e.key < 256) press((int)e.key);
        if (e.type == WEV_DOWN) {
            int c = (e.x - 4) / 59, row = (e.y - 56) / 48;
            if (e.y >= 56 && row >= 0 && row < 5 && e.x >= 4) {
                if (row == 4) press('C');
                else if (c >= 0 && c < 4) press(keys[row][c][0]);
            }
        }
        draw(id);
    }
    sys_win_close(id);
    return 0;
}
