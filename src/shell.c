#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "rtc.h"
#include "pci.h"
#include "cpuid.h"
#include "str.h"
#include "fat.h"
#include "ata.h"
#include "soupyc.h"
#include "alphasoup.h"
#include "speaker.h"
#include "vga13h.h"
#include "task.h"
#include "vfs.h"
#include "klog.h"
#include "users.h"
#include "editor.h"
#include "ai.h"
#include "usermode.h"
#ifndef NO_CHALLENGE
#include "challenge.h"
#endif
#ifndef NO_DOOM
#include "doom.h"
#endif

#define INPUT_MAX   256
#define HIST_MAX     16

/* ---- string helpers ---- */
static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}
static int str_startswith(const char *s, const char *pfx) {
    while (*pfx) { if (*s++ != *pfx++) return 0; }
    return 1;
}
static int parse_num(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return n;
}

/* ---- I/O helper for reheat ---- */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* ---- current bowl (working directory) ---------------------------------- */
/* The shell tracks one absolute, normalised path. fat_* takes absolute
 * paths, so every file command resolves its argument against `cwd` first. */
static char cwd[FAT_PATH_MAX] = "/";

/* Normalise an absolute path: collapse "//", ".", and ".." into a clean
 * "/a/b/c" form (or "/"). `out` must be >= FAT_PATH_MAX bytes. */
static void normalize_path(const char *in, char *out) {
    char comp[FAT_NAME_MAX];
    out[0] = '/';
    out[1] = '\0';
    int len = 1;                        /* out[len] is always the NUL */
    const char *p = in;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        int i = 0;
        while (*p && *p != '/') {
            if (i < FAT_NAME_MAX - 1) comp[i++] = *p;
            p++;
        }
        comp[i] = '\0';
        if (i == 0) continue;
        if (str_eq(comp, ".")) continue;
        if (str_eq(comp, "..")) {
            while (len > 1 && out[len - 1] != '/') len--;
            if (len > 1) len--;          /* drop the separating '/' */
            if (len < 1) len = 1;
            out[len] = '\0';
            continue;
        }
        if (len > 1 && len + 1 < FAT_PATH_MAX) out[len++] = '/';
        for (int k = 0; k < i && len < FAT_PATH_MAX - 1; k++)
            out[len++] = comp[k];
        out[len] = '\0';
    }
}

/* Resolve `arg` (absolute or relative to cwd) into a clean absolute path. */
static void resolve_path(const char *arg, char *out) {
    char joined[FAT_PATH_MAX * 2];
    int j = 0;
    if (arg && arg[0] == '/') {
        joined[j++] = '/';
    } else {
        for (int i = 0; cwd[i] && j < (int)sizeof(joined) - 2; i++)
            joined[j++] = cwd[i];
        joined[j++] = '/';
    }
    if (arg)
        for (int i = 0; arg[i] && j < (int)sizeof(joined) - 1; i++)
            joined[j++] = arg[i];
    joined[j] = '\0';
    normalize_path(joined, out);
}

/* ---- accounts, login & permissions ------------------------------------- */

/* Read a line into buf (max bytes incl. NUL). If mask != 0, echo `mask` for
 * each typed character instead of the character itself (secret entry). */
static int read_line(char *buf, int max, char mask) {
    int len = 0;
    for (;;) {
        int c = keyboard_getchar();
        if (c == '\n') { vga_putchar('\n'); break; }
        if (c == '\b') {
            if (len > 0) { len--; vga_putchar('\b'); }
            continue;
        }
        if (c >= ' ' && c < 127 && len < max - 1) {
            buf[len++] = (char)c;
            vga_putchar(mask ? mask : (char)c);
        }
    }
    buf[len] = '\0';
    return len;
}

/* Switch the active session - both the user layer and the FAT owner stamp. */
static void set_session_uid(uint8_t uid) {
    users_set_current(uid);
    fat_set_creator(uid);
}

/* Write the parent bowl of an absolute path into out: "/a/b" -> "/a". */
static void parent_of(const char *path, char *out) {
    int n = (int)strlen(path);
    while (n > 1 && path[n - 1] != '/') n--;
    if (n > 1) n--;                       /* drop the trailing '/' */
    if (n < 1) n = 1;
    for (int i = 0; i < n; i++) out[i] = path[i];
    out[n] = '\0';
}

/* May the current cook do `need` ('r','w','x') to `path`? The headchef
 * always may; a missing path is allowed (the operation fails on its own). */
static int may(const char *path, char need) {
    if (users_is_headchef()) return 1;
    uint8_t owner, mode;
    if (fat_stat(path, &owner, &mode) < 0) return 1;     /* no such path */
    uint8_t bits;
    if (users_current_uid() == owner)
        bits = (need == 'r') ? FAT_PERM_OR :
               (need == 'w') ? FAT_PERM_OW : FAT_PERM_OX;
    else
        bits = (need == 'r') ? FAT_PERM_AR :
               (need == 'w') ? FAT_PERM_AW : FAT_PERM_AX;
    return (mode & bits) ? 1 : 0;
}

/* Print the standard "access denied" line. */
static void deny(const char *path) {
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_printf("  Permission denied: %s\n", path);
    vga_puts("  (ask the headchef, or retry with: chef <command>)\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* Login gate - loops until a valid cook clocks in. Used at boot and by
 * the `clockout` command. */
static void do_login(void) {
    char name[USER_NAME_MAX];
    char secret[64];
    for (;;) {
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_puts("\n  soupOS kitchen -- clock in to start your shift.\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vga_puts("  cook:   ");
        read_line(name, sizeof(name), 0);
        vga_puts("  secret: ");
        read_line(secret, sizeof(secret), '*');

        int uid = users_check(name, secret);
        if (uid >= 0) {
            set_session_uid((uint8_t)uid);
            vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
            vga_printf("\n  Welcome to the kitchen, %s.%s\n\n",
                       users_current_name(),
                       users_is_headchef() ? " (headchef -- run of the kitchen)" : "");
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            return;
        }
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("  The kitchen door stays locked -- wrong cook or secret.\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

/* ---- command history ---- */
static char hist[HIST_MAX][INPUT_MAX];
static int  hist_head = 0;   /* ring-buffer write position */
static int  hist_size = 0;   /* entries stored (0..HIST_MAX) */

static void hist_push(const char *cmd) {
    if (!cmd || !cmd[0]) return;
    strncpy(hist[hist_head % HIST_MAX], cmd, INPUT_MAX - 1);
    hist[hist_head % HIST_MAX][INPUT_MAX - 1] = '\0';
    hist_head++;
    if (hist_size < HIST_MAX) hist_size++;
}

/* offset=1 -> most recent, offset=2 -> one before that, etc. */
static const char *hist_get(int offset) {
    if (offset <= 0 || offset > hist_size) return (void *)0;
    int idx = ((hist_head - offset) % HIST_MAX + HIST_MAX) % HIST_MAX;
    return hist[idx];
}

/* ---- Line editing with proper cursor positioning ---- */
#define VGA_W      80

static int prompt_row = 0;   /* VGA row where current prompt was printed */
static int prompt_len = 8;   /* width of the printed prompt (includes cwd) */

/* Move the hardware cursor to buffer position buf_pos */
static void cursor_to(int buf_pos) {
    int abs = prompt_len + buf_pos;
    vga_set_cursor(prompt_row + abs / VGA_W, abs % VGA_W);
}

/* Reprint input[from..len-1], then optionally one trailing space.
   Does NOT reposition the cursor afterwards - caller must call cursor_to(). */
static void redraw_tail(const char *input, int from, int len, int trail_space) {
    cursor_to(from);
    for (int i = from; i < len; i++) vga_putchar(input[i]);
    if (trail_space) vga_putchar(' ');
}

/* Insert character c at pos, updating buffer and screen */
static void shell_insert(char *input, int *pos, int *len, char c) {
    if (*len >= INPUT_MAX - 1) return;
    for (int i = *len; i > *pos; i--) input[i] = input[i - 1];
    input[*pos] = c;
    (*len)++;
    redraw_tail(input, *pos, *len, 0);
    (*pos)++;
    cursor_to(*pos);
}

/* Delete character before cursor (backspace) */
static void shell_backspace(char *input, int *pos, int *len) {
    if (*pos == 0) return;
    for (int i = *pos - 1; i < *len - 1; i++) input[i] = input[i + 1];
    (*pos)--;
    (*len)--;
    redraw_tail(input, *pos, *len, 1);
    cursor_to(*pos);
}

/* Delete character at cursor (DEL key) */
static void shell_delkey(char *input, int *pos, int *len) {
    if (*pos == *len) return;
    for (int i = *pos; i < *len - 1; i++) input[i] = input[i + 1];
    (*len)--;
    redraw_tail(input, *pos, *len, 1);
    cursor_to(*pos);
}

/* Delete from start-of-line up to cursor (Ctrl+U) */
static void shell_kill_to_start(char *input, int *pos, int *len) {
    if (*pos == 0) return;
    int n = *pos;
    for (int i = 0; i + n < *len; i++) input[i] = input[i + n];
    *len -= n;
    *pos = 0;
    redraw_tail(input, 0, *len, 0);
    /* Erase the n trailing chars left over from the old longer line */
    for (int i = 0; i < n; i++) vga_putchar(' ');
    cursor_to(0);
}

/* Delete the word immediately before the cursor (Ctrl+W) */
static void shell_kill_word(char *input, int *pos, int *len) {
    if (*pos == 0) return;
    int end = *pos;
    int start = end;
    /* Skip trailing spaces, then the word itself */
    while (start > 0 && input[start - 1] == ' ') start--;
    while (start > 0 && input[start - 1] != ' ') start--;
    int n = end - start;
    if (n == 0) return;
    for (int i = start; i + n < *len; i++) input[i] = input[i + n];
    *len -= n;
    *pos = start;
    redraw_tail(input, start, *len, 0);
    for (int i = 0; i < n; i++) vga_putchar(' ');
    cursor_to(*pos);
}

/* Replace the whole input line with new_str (used by history navigation) */
static void line_replace(char *input, int *pos, int *len, const char *new_str) {
    int old_len = *len;
    cursor_to(0);
    *len = 0;
    while (new_str[*len] && *len < INPUT_MAX - 1) {
        vga_putchar(new_str[*len]);
        input[*len] = new_str[*len];
        (*len)++;
    }
    input[*len] = '\0';
    /* Erase any leftover characters from the old longer line */
    for (int i = *len; i < old_len; i++) vga_putchar(' ');
    *pos = *len;
    cursor_to(*pos);
}

/* ===========================================================================
 * Commands
 *
 *   menu        - show this help
 *   wash        - clear the screen
 *   slurp       - echo text
 *   season      - set VGA fg/bg colors
 *   simmer      - uptime since boot
 *   broth       - physical memory stats
 *   ladle       - heap allocator stats
 *   expiry      - current date/time (RTC)
 *   chef        - CPU info (CPUID)
 *   pantry      - PCI device list
 *   recipe      - kernel version info
 *   leftovers   - command history
 *   spill       - trigger divide-by-zero
 *   freeze      - halt CPU
 *   reheat      - reboot
 * =========================================================================== */

/* ---- simple pager for long listings (the text screen has no scrollback) ----
 * Print lines through page_line(); once a screenful has scrolled past, it shows
 * a "-- more --" footer and blocks for a key. 'q'/ESC stop the listing (the
 * caller breaks on a 0 return), any other key clears the screen and continues
 * with the next page. */
#define PAGE_ROWS 22
static int g_page_rows;
static void page_begin(void) { g_page_rows = 0; }
static int page_line(int header, const char *text) {
    vga_set_color(header ? VGA_YELLOW : VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts(text);
    vga_putchar('\n');
    if (++g_page_rows < PAGE_ROWS) return 1;
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("  -- more -- (q to stop, any other key for next page)");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    int c = keyboard_getchar();
    if (c == 'q' || c == 'Q' || c == 27) { vga_putchar('\n'); return 0; }
    vga_clear();
    g_page_rows = 0;
    return 1;
}

static void cmd_menu(void) {
    static const struct { uint8_t hdr; const char *text; } lines[] = {
        {1, "  Today's Menu:"},
        {0, "    menu               - show this message"},
        {0, "    wash               - clear the screen"},
        {0, "    slurp <text>       - print text"},
        {0, "    season <fg> <bg>   - set colors (0-15)"},
        {0, "    simmer             - time since boot"},
        {0, "    broth              - physical memory stats"},
        {0, "    ladle              - heap allocator stats"},
        {0, "    expiry             - current date/time"},
        {0, "    chef               - CPU info (bare 'chef'; see Kitchen Staff)"},
        {0, "    pantry             - PCI devices"},
        {0, "    serve              - list the current bowl's contents"},
        {0, "    pour <file>        - print file contents"},
        {0, "    stir <file> <text> - write text to a file"},
        {0, "    strain <file>      - delete a file"},
        {0, "    decant <src> <dst> - copy a file"},
        {0, "    relabel <src> <dst>- move / rename a file"},
        {0, "    jot <file>         - edit a file (full-screen text editor)"},
        {0, "    pwd                - print the current bowl (directory)"},
        {0, "    cd <bowl>          - change to another bowl"},
        {0, "    mkbowl <name>      - make a new bowl (directory)"},
        {0, "    rmbowl <name>      - remove an empty bowl"},
        {0, "    bowltest           - self-test the bowl/filesystem layer"},
        {1, "  The Kitchen Staff:"},
        {0, "    whoami             - show the current cook"},
        {0, "    roster             - list every cook in the kitchen"},
        {0, "    perms <file> [spec]- show / set file permissions"},
        {0, "    chef <command>     - run one command as the headchef"},
        {0, "    hire <name>        - headchef: add a cook"},
        {0, "    fire <name>        - headchef: remove a cook"},
        {0, "    clockout           - log out, return to the login prompt"},
        {0, "    ai <prompt>        - ask the host LLM (needs: make run-ai)"},
        {0, "    cook <prog.elf>    - run a ring-3 user-mode program"},
        {0, "    soup <file.sc>     - run a soupyc script"},
        {0, "    soup -c \"code\"     - run inline soupyc"},
        {0, "    hash <text>        - AlphaSOUP-32 hash"},
        {0, "    beep <freq> [ms]   - play PC speaker tone"},
        {0, "    mandelbrot         - ASCII Mandelbrot set"},
        {0, "    vgademo            - mode 13h graphics demo"},
        {0, "    uname [-a]         - OS / arch / version"},
        {0, "    cal                - calendar for the current month"},
        {0, "    clock              - live digital clock"},
        {0, "    wipe               - animated screen clear"},
        {0, "    fortune            - random line from FORTUNE.TXT"},
        {0, "    bounce             - VGA bouncing-ball screensaver"},
#ifndef NO_DOOM
        {0, "    doom               - it runs Doom (needs DOOM1.WAD, no bg tasks)"},
#endif
        {0, "    recipe             - kernel info"},
        {0, "    leftovers          - command history"},
        {0, "    spill              - divide-by-zero crash"},
        {0, "    freeze             - halt the CPU"},
        {0, "    reheat             - reboot"},
        {0, "    ps                 - list running tasks"},
        {0, "    kill <id>          - stop a background task"},
        {0, "    bgclock            - spawn a clock in the top-right corner"},
        {0, "    yieldbg            - bg task logging [bg] to serial (pairs with cook spin.elf)"},
        {0, "    spinbg             - CPU-bound bg task that never yields (preemption demo)"},
        {0, "    sleeptest          - test task_sleep and wait-queue (500 ms sleep)"},
        {0, "    vfstest            - test the VFS shim (/dev nodes + file round-trip)"},
        {0, "    dmesg              - dump the kernel log ring buffer"},
        {0, "  (up/down arrows, Tab to complete filenames)"},
        {0, "  (Ctrl+A/E home/end, Ctrl+U clear line, Ctrl+W kill word, Ctrl+C cancel)"},
    };
    page_begin();
    for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
        if (!page_line(lines[i].hdr, lines[i].text)) break;
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void cmd_slurp(const char *args) {
    vga_puts(args);
    vga_putchar('\n');
}

static void cmd_season(const char *args) {
    int fg = parse_num(args);
    while (*args && *args != ' ') args++;
    while (*args == ' ') args++;
    int bg = parse_num(args);
    if (fg < 0 || fg > 15 || bg < 0 || bg > 15) {
        vga_puts("Usage: season <fg 0-15> <bg 0-15>\n"); return;
    }
    vga_set_color((vga_color_t)fg, (vga_color_t)bg);
    vga_printf("Seasoned: fg=%d bg=%d\n", fg, bg);
}

static void cmd_simmer(void) {
    uint32_t s = timer_get_seconds();
    vga_printf("Simmering for: %u:%02u:%02u\n", s / 3600, (s % 3600) / 60, s % 60);
}

static void cmd_broth(void) {
    uint32_t total = pmm_total_kb(), used = pmm_used_kb(), fr = pmm_free_kb();
    vga_set_color(VGA_YELLOW, VGA_BLACK); vga_puts("  Broth (physical memory):\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_printf("    Total:  %u KB (%u MB)\n", total, total / 1024);
    vga_printf("    Used:   %u KB (%u MB)\n", used,  used  / 1024);
    vga_printf("    Free:   %u KB (%u MB)\n", fr,    fr    / 1024);
    vga_printf("    Pages:  %u used / %u total (4 KB each)\n",
               pmm_used_pages(), pmm_total_pages());
}

static void cmd_ladle(void) {
    uint32_t used = heap_used_bytes(), fr = heap_free_bytes(), tot = heap_total_bytes();
    vga_set_color(VGA_YELLOW, VGA_BLACK); vga_puts("  Ladle (heap):\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_printf("    Total:  %u KB\n", tot / 1024);
    vga_printf("    Used:   %u bytes\n", used);
    vga_printf("    Free:   %u bytes (%u KB)\n", fr, fr / 1024);
}

static void cmd_expiry(void) {
    rtc_time_t t;
    rtc_read(&t);
    vga_printf("  %u-%02u-%02u  %02u:%02u:%02u\n",
               t.year, t.month, t.day, t.hour, t.minute, t.second);
}

static void cmd_chef(void) {
    cpuid_info_t c;
    cpuid_read(&c);
    vga_set_color(VGA_YELLOW, VGA_BLACK); vga_puts("  Chef (CPU):\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_printf("    Vendor:  %s\n", c.vendor);
    if (c.brand[0])
        vga_printf("    Brand:   %s\n", c.brand);
    vga_printf("    Family:  %u  Model: %u  Stepping: %u\n",
               c.family, c.model, c.stepping);
    vga_printf("    Logical cores: %u\n", c.logical_cores);
    vga_puts("    Features:");
    if (c.has_fpu)   vga_puts(" FPU");
    if (c.has_apic)  vga_puts(" APIC");
    if (c.has_sse)   vga_puts(" SSE");
    if (c.has_sse2)  vga_puts(" SSE2");
    if (c.has_sse3)  vga_puts(" SSE3");
    if (c.has_ssse3) vga_puts(" SSSE3");
    if (c.has_sse41) vga_puts(" SSE4.1");
    if (c.has_sse42) vga_puts(" SSE4.2");
    if (c.has_aes)   vga_puts(" AES");
    if (c.has_avx)   vga_puts(" AVX");
    vga_putchar('\n');
}

static void cmd_pantry(void) {
    static pci_dev_t devs[PCI_MAX_DEVS];
    int n = pci_scan(devs, PCI_MAX_DEVS);
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_printf("  Pantry (PCI, %d device%s):\n", n, n == 1 ? "" : "s");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int i = 0; i < n; i++) {
        vga_printf("  [%02x:%02x.%u] %04x:%04x  %-10s  %s\n",
                   devs[i].bus, devs[i].dev, devs[i].fn,
                   devs[i].vendor_id, devs[i].device_id,
                   pci_vendor_str(devs[i].vendor_id),
                   pci_class_str(devs[i].class_code));
    }
    if (n == 0) vga_puts("  (empty pantry - no PCI devices found)\n");
}


/* Left-align a string in a field of `width` characters */
static void print_padded(const char *s, int width) {
    int n = 0;
    while (s[n]) { vga_putchar(s[n++]); }
    while (n++ < width) vga_putchar(' ');
}

/* Render a permission mode byte into a 6-char "rwxr-x" string. */
static void perm_str(uint8_t mode, char *s) {
    s[0] = (mode & FAT_PERM_OR) ? 'r' : '-';
    s[1] = (mode & FAT_PERM_OW) ? 'w' : '-';
    s[2] = (mode & FAT_PERM_OX) ? 'x' : '-';
    s[3] = (mode & FAT_PERM_AR) ? 'r' : '-';
    s[4] = (mode & FAT_PERM_AW) ? 'w' : '-';
    s[5] = (mode & FAT_PERM_AX) ? 'x' : '-';
    s[6] = '\0';
}

static void cmd_serve(void) {
    if (!may(cwd, 'r')) { deny(cwd); return; }

    static fat_entry_t entries[FAT_LS_MAX];
    int n = fat_ls(cwd, entries, FAT_LS_MAX);
    if (n < 0) {
        vga_puts("  No filesystem mounted. (need FAT16/32 disk on primary master)\n");
        return;
    }
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_printf("  Serving %s  -  %d item%s  [FAT%d  %s]:\n",
               cwd, n, n == 1 ? "" : "s", fat_get_type(), fat_label());
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("  PERMS   OWNER     NAME             SIZE\n");
    vga_puts("  ------------------------------------------------\n");
    for (int i = 0; i < n; i++) {
        char full[FAT_PATH_MAX];
        resolve_path(entries[i].name, full);
        uint8_t owner = 0, mode = FAT_PERM_DEFAULT;
        fat_stat(full, &owner, &mode);
        char ps[7];
        perm_str(mode, ps);

        vga_puts("  ");
        vga_set_color(VGA_DARK_GREY, VGA_BLACK);
        vga_printf("%s  ", ps);
        print_padded(users_name_of(owner), 10);
        if (entries[i].attr & FAT_ATTR_DIR) {
            vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
            print_padded(entries[i].name, 16);
            vga_puts("<bowl>\n");
        } else {
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            print_padded(entries[i].name, 16);
            vga_printf("%u B\n", entries[i].size);
        }
    }
    if (n == 0) vga_puts("  (empty pot)\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void cmd_pour(const char *args) {
    if (!args || !args[0]) {
        vga_puts("Usage: pour <filename>\n");
        return;
    }
    static uint8_t file_buf[8192];
    uint32_t file_size = 0;
    char path[FAT_PATH_MAX];
    resolve_path(args, path);
    if (!may(path, 'r')) { deny(path); return; }
    if (fat_read(path, file_buf, sizeof(file_buf), &file_size) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("  Not found: %s\n", args);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    uint32_t display = file_size < sizeof(file_buf) ? file_size : sizeof(file_buf);
    for (uint32_t i = 0; i < display; i++) {
        char c = (char)file_buf[i];
        if (c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c < 0x7F))
            vga_putchar(c);
    }
    if (file_size > sizeof(file_buf)) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_printf("  (truncated - showing %u of %u bytes)\n",
                   (uint32_t)sizeof(file_buf), file_size);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    } else {
        vga_putchar('\n');
    }
}

static void cmd_soup(const char *args) {
    if (!args || !args[0]) {
        vga_puts("Usage: soup <file.sc>\n");
        vga_puts("       soup -c \"code\"\n");
        return;
    }

    /* soup -c "inline code" */
    if (args[0]=='-' && args[1]=='c') {
        const char *code = args + 2;
        while (*code == ' ') code++;
        soupyc_run(code);
        return;
    }

    /* soup filename - load from FAT volume */
    static uint8_t fbuf[8192];
    uint32_t fsize = 0;
    char path[FAT_PATH_MAX];
    resolve_path(args, path);
    if (!may(path, 'r')) { deny(path); return; }
    if (fat_read(path, fbuf, sizeof(fbuf) - 1, &fsize) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("  File not found: %s\n", args);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    fbuf[fsize] = '\0';
    soupyc_run((const char *)fbuf);
}

static void cmd_stir(const char *args) {
    if (!args || !args[0]) {
        vga_puts("Usage: stir <file> <content>\n");
        return;
    }
    /* Split "filename rest of line" */
    const char *p = args;
    while (*p && *p != ' ') p++;
    int flen = (int)(p - args);
    char arg_name[FAT_PATH_MAX];
    if (flen >= FAT_PATH_MAX) flen = FAT_PATH_MAX - 1;
    memcpy(arg_name, args, (uint32_t)flen);
    arg_name[flen] = '\0';
    while (*p == ' ') p++;

    char path[FAT_PATH_MAX];
    resolve_path(arg_name, path);

    /* Writing needs write on the file, or on the bowl if it is new. */
    if (fat_exists(path)) {
        if (!may(path, 'w')) { deny(path); return; }
    } else {
        char par[FAT_PATH_MAX];
        parent_of(path, par);
        if (!may(par, 'w')) { deny(par); return; }
    }

    /* Build content with trailing newline */
    static uint8_t wbuf[INPUT_MAX + 1];
    uint32_t clen = (uint32_t)strlen(p);
    memcpy(wbuf, p, clen);
    wbuf[clen++] = '\n';

    if (fat_write(path, wbuf, clen) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("  Write failed (no disk, FAT32 volume, or missing bowl)\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    } else {
        vga_printf("  Stirred: %s (%u bytes)\n", path, clen);
    }
}

static void cmd_strain(const char *args) {
    if (!args || !args[0]) {
        vga_puts("Usage: strain <file>\n");
        return;
    }
    char path[FAT_PATH_MAX];
    resolve_path(args, path);
    if (!may(path, 'w')) { deny(path); return; }
    if (fat_delete(path) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("  Cannot strain: %s\n", args);
        vga_puts("  (not found, or it is a bowl - use rmbowl)\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    } else {
        vga_printf("  Strained out: %s\n", path);
    }
}

/* ---- decant (copy) & relabel (move/rename) ----------------------------- */

/* One shared staging buffer for a whole-file copy. fat_write has no
 * streaming form, so a file must fit here to be copied. 64 KB covers every
 * text file and script on the disk; larger blobs are refused outright. */
#define COPY_BUF_MAX 65536
static uint8_t copy_buf[COPY_BUF_MAX];

/* Split "a b" (leading/space-separated) into two tokens. 0 if both present. */
static int split_two(const char *args, char *first, char *second) {
    while (*args == ' ') args++;
    int i = 0;
    while (*args && *args != ' ' && i < FAT_PATH_MAX - 1) first[i++] = *args++;
    first[i] = '\0';
    while (*args == ' ') args++;
    i = 0;
    while (*args && *args != ' ' && i < FAT_PATH_MAX - 1) second[i++] = *args++;
    second[i] = '\0';
    return (first[0] && second[0]) ? 0 : -1;
}

/* Last path component: "/a/b/c.txt" -> "c.txt". */
static const char *path_basename(const char *path) {
    const char *base = path;
    for (const char *q = path; *q; q++) if (*q == '/') base = q + 1;
    return base;
}

/* Effective destination: if `dst_abs` names an existing bowl, the file keeps
 * its own basename inside that bowl (cp/mv-into-directory semantics). */
static void copy_dest(const char *src_abs, const char *dst_abs, char *out) {
    if (fat_is_dir(dst_abs)) {
        char joined[FAT_PATH_MAX * 2];
        int j = 0;
        for (const char *q = dst_abs; *q && j < (int)sizeof(joined) - 2; q++)
            joined[j++] = *q;
        joined[j++] = '/';
        for (const char *q = path_basename(src_abs);
             *q && j < (int)sizeof(joined) - 1; q++)
            joined[j++] = *q;
        joined[j] = '\0';
        normalize_path(joined, out);
    } else {
        strncpy(out, dst_abs, FAT_PATH_MAX - 1);
        out[FAT_PATH_MAX - 1] = '\0';
    }
}

/* Copy core. 0=ok (*out_size set); -1=read, -2=too big, -3=write. */
static int copy_file_core(const char *src, const char *dst, uint32_t *out_size) {
    uint32_t size = 0;
    if (fat_read(src, copy_buf, COPY_BUF_MAX, &size) < 0) return -1;
    if (size > COPY_BUF_MAX) return -2;
    if (fat_write(dst, copy_buf, size) < 0) return -3;
    *out_size = size;
    return 0;
}

static void print_copy_err(int rc) {
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    if      (rc == -1) vga_puts("  Could not read the source file.\n");
    else if (rc == -2) vga_printf("  Too big: limit is %u bytes.\n",
                                  (uint32_t)COPY_BUF_MAX);
    else               vga_puts("  Write failed (no disk, FAT32 volume, "
                                "missing bowl, or disk full).\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* Resolve + validate a two-file operation. Fills src/dst with clean absolute
 * paths and checks permissions. `need_w_src` also requires write on the
 * source (for relabel, which deletes it). -1 on any error (message printed). */
static int prep_file_op(const char *args, const char *usage,
                        char *src, char *dst, int need_w_src) {
    char a1[FAT_PATH_MAX], a2[FAT_PATH_MAX];
    if (split_two(args, a1, a2) < 0) { vga_printf("Usage: %s\n", usage); return -1; }

    char dstarg[FAT_PATH_MAX];
    resolve_path(a1, src);
    resolve_path(a2, dstarg);

    if (!fat_exists(src) || fat_is_dir(src)) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("  Not a file: %s\n", a1);
        vga_puts("  (these commands move files, not bowls)\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return -1;
    }
    copy_dest(src, dstarg, dst);
    if (fat_is_dir(dst)) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("  Destination is a bowl: %s\n", dst);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return -1;
    }
    if (str_eq(src, dst)) {
        vga_puts("  Source and destination are the same.\n");
        return -1;
    }
    if (!may(src, 'r')) { deny(src); return -1; }
    if (need_w_src && !may(src, 'w')) { deny(src); return -1; }
    if (fat_exists(dst)) {
        if (!may(dst, 'w')) { deny(dst); return -1; }
    } else {
        char par[FAT_PATH_MAX];
        parent_of(dst, par);
        if (!may(par, 'w')) { deny(par); return -1; }
    }
    return 0;
}

static void cmd_decant(const char *args) {
    char src[FAT_PATH_MAX], dst[FAT_PATH_MAX];
    if (prep_file_op(args, "decant <src> <dst>", src, dst, 0) < 0) return;
    uint32_t size = 0;
    int rc = copy_file_core(src, dst, &size);
    if (rc < 0) { print_copy_err(rc); return; }
    vga_printf("  Decanted %s -> %s (%u bytes)\n", src, dst, size);
}

static void cmd_relabel(const char *args) {
    char src[FAT_PATH_MAX], dst[FAT_PATH_MAX];
    if (prep_file_op(args, "relabel <src> <dst>", src, dst, 1) < 0) return;
    uint32_t size = 0;
    int rc = copy_file_core(src, dst, &size);
    if (rc < 0) { print_copy_err(rc); return; }
    if (fat_delete(src) < 0) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_printf("  Copied to %s, but could not clear out %s.\n", dst, src);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    vga_printf("  Relabelled %s -> %s (%u bytes)\n", src, dst, size);
}

/* jot <file> - open the full-screen text editor on a file (new or existing).
 * Editing implies the right to save, so an existing file needs read+write;
 * a new file needs write on its parent bowl. */
static void cmd_jot(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { vga_puts("Usage: jot <file>\n"); return; }
    char path[FAT_PATH_MAX];
    resolve_path(args, path);
    if (fat_is_dir(path)) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("  That's a bowl, not a file: %s\n", path);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    if (fat_exists(path)) {
        if (!may(path, 'r') || !may(path, 'w')) { deny(path); return; }
    } else {
        char par[FAT_PATH_MAX];
        parent_of(path, par);
        if (!may(par, 'w')) { deny(par); return; }
    }
    editor_run(path);
}

/* ai <prompt> - ask the host-side LLM over the COM2 serial bridge. Streams
 * the reply to the screen as bytes arrive; times out gracefully if no bridge
 * daemon is answering. The assembled reply is also mirrored to the kernel log
 * (and thus COM1) so the round-trip is visible in dmesg / the serial capture. */
static void cmd_ai(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { vga_puts("Usage: ai <prompt>\n"); return; }
    if (!ai_available()) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_puts("  The kitchen oracle is asleep (no COM2 bridge).\n");
        vga_puts("  Boot with the bridge attached:  make run-ai\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_puts("  consulting the kitchen oracle...\n");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);

    ai_send(args);

    char reply[256];
    int  rlen = 0, got = 0;
    for (;;) {
        int c = ai_getc(got ? 600 : 1500);   /* 6 s between bytes, 15 s for the first */
        if (c < 0) {
            if (!got) {
                vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
                vga_puts("  (no reply - is the bridge daemon running?)\n");
            }
            break;
        }
        if (c == AI_EOT) break;
        if (c == '\r') continue;
        vga_putchar((char)c);
        if (rlen < (int)sizeof(reply) - 1) reply[rlen++] = (char)c;
        got = 1;
    }
    reply[rlen] = '\0';
    if (got) {
        vga_putchar('\n');
        klog("[ai] reply: %s\n", reply);
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* cook <program.elf> - load and run an ELF32 binary as a real ring-3
 * user-mode process. Returns to the shell when the program exits. */
static void cmd_cook(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { vga_puts("Usage: cook <program.elf> [args]\n"); return; }
    /* Split the program name from the rest of the line (passed to the program). */
    char name[FAT_PATH_MAX];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < FAT_PATH_MAX - 1) { name[i] = args[i]; i++; }
    name[i] = '\0';
    const char *prog_args = args + i;
    while (*prog_args == ' ') prog_args++;

    char path[FAT_PATH_MAX];
    resolve_path(name, path);
    if (!fat_exists(path) || fat_is_dir(path)) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("  No such program: %s\n", name);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    if (!may(path, 'r') || !may(path, 'x')) { deny(path); return; }

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_printf("  cooking %s in ring 3...\n", path);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    int rc = exec_elf(path, prog_args);
    if (rc < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("  Not a runnable soupOS program (need a 32-bit i386 ET_EXEC ELF).\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    } else {
        vga_set_color(VGA_DARK_GREY, VGA_BLACK);
        vga_printf("  [%s exited with code %d]\n", path, rc);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

/* ---- bowls (directories) ---------------------------------------------- */

static void cmd_cd(const char *args) {
    while (*args == ' ') args++;
    char target[FAT_PATH_MAX];
    if (!*args) { strcpy(cwd, "/"); return; }    /* cd with no arg -> root */
    resolve_path(args, target);
    if (!fat_is_dir(target)) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("  No such bowl: %s\n", args);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    if (!may(target, 'x')) { deny(target); return; }
    strncpy(cwd, target, FAT_PATH_MAX - 1);
    cwd[FAT_PATH_MAX - 1] = '\0';
}

static void cmd_pwd(void) {
    vga_printf("  %s\n", cwd);
}

static void cmd_mkbowl(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { vga_puts("Usage: mkbowl <name>\n"); return; }
    char path[FAT_PATH_MAX];
    resolve_path(args, path);
    char par[FAT_PATH_MAX];
    parent_of(path, par);
    if (!may(par, 'w')) { deny(par); return; }
    if (fat_mkdir(path) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("  Could not make bowl: %s\n", args);
        vga_puts("  (already exists, missing parent bowl, disk full, or FAT32)\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    } else {
        vga_printf("  Bowl ready: %s\n", path);
    }
}

static void cmd_rmbowl(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { vga_puts("Usage: rmbowl <name>\n"); return; }
    char path[FAT_PATH_MAX];
    resolve_path(args, path);
    if (str_eq(path, cwd)) {
        vga_puts("  Cannot rmbowl the bowl you are standing in.\n");
        return;
    }
    char par[FAT_PATH_MAX];
    parent_of(path, par);
    if (!may(par, 'w')) { deny(par); return; }
    if (fat_rmdir(path) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("  Could not remove bowl: %s\n", args);
        vga_puts("  (not a bowl, not empty, or does not exist)\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    } else {
        vga_printf("  Bowl removed: %s\n", path);
    }
}

/* bowltest - exercises the bowl (directory) layer end to end and cleans up
 * after itself, so it can be run repeatedly. */
static void cmd_bowltest(void) {
    int pass = 0, fail = 0;
    #define CHECK(label, cond) do {                       \
        if (cond) { vga_set_color(VGA_LIGHT_GREEN,VGA_BLACK);\
                    vga_printf("  [pass] %s\n", label); pass++; }\
        else      { vga_set_color(VGA_LIGHT_RED,VGA_BLACK);  \
                    vga_printf("  [FAIL] %s\n", label); fail++; }\
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);          \
    } while (0)

    /* Clean any leftovers from a previous run. */
    fat_delete("/BOWLTEST/HI.TXT");
    fat_rmdir ("/BOWLTEST/INNER");
    fat_rmdir ("/BOWLTEST");

    CHECK("mkbowl /BOWLTEST",            fat_mkdir("/BOWLTEST") == 0);
    CHECK("/BOWLTEST is a bowl",         fat_is_dir("/BOWLTEST") == 1);
    CHECK("mkbowl /BOWLTEST/INNER",      fat_mkdir("/BOWLTEST/INNER") == 0);
    CHECK("nested bowl is a bowl",       fat_is_dir("/BOWLTEST/INNER") == 1);

    const char *msg = "soup keeps well in a bowl\n";
    CHECK("write /BOWLTEST/HI.TXT",
          fat_write("/BOWLTEST/HI.TXT", (const uint8_t *)msg,
                    (uint32_t)strlen(msg)) == 0);

    static uint8_t rb[64];
    uint32_t rs = 0;
    int rok = (fat_read("/BOWLTEST/HI.TXT", rb, sizeof(rb), &rs) == 0)
              && rs == strlen(msg);
    CHECK("read it back, size matches",  rok);

    fat_entry_t le[FAT_LS_MAX];
    int ln = fat_ls("/BOWLTEST", le, FAT_LS_MAX);
    CHECK("ls /BOWLTEST shows 2 items",  ln == 2);

    CHECK("cannot rmbowl non-empty",     fat_rmdir("/BOWLTEST") < 0);
    CHECK("strain the file",             fat_delete("/BOWLTEST/HI.TXT") == 0);
    CHECK("rmbowl /BOWLTEST/INNER",      fat_rmdir("/BOWLTEST/INNER") == 0);
    CHECK("rmbowl /BOWLTEST (now empty)",fat_rmdir("/BOWLTEST") == 0);
    CHECK("/BOWLTEST is gone",           fat_exists("/BOWLTEST") == 0);

    if (fail == 0) vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    else           vga_set_color(VGA_LIGHT_RED,   VGA_BLACK);
    vga_printf("  bowltest: %d passed, %d failed\n", pass, fail);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    #undef CHECK
}

/* ---- kitchen staff: users, access control, elevation ------------------- */

static void dispatch(const char *buf);   /* forward decl for chef elevation */

static void cmd_whoami(void) {
    vga_printf("  %s  (uid %u)  -  %s\n",
               users_current_name(), users_current_uid(),
               users_is_headchef() ? "headchef" : "cook");
}

static void cmd_roster(void) {
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_printf("  Kitchen roster (%d):\n", users_count());
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int i = 0; i < users_count(); i++) {
        uint8_t uid = users_uid_at(i);
        vga_printf("    uid %3u  %s%s\n", uid, users_name_at(i),
                   uid == 0 ? "  (headchef)" : "");
    }
}

static void cmd_clockout(void) {
    vga_printf("  %s clocks out. The kitchen is yours, next cook.\n",
               users_current_name());
    do_login();
}

static void cmd_hire(const char *args) {
    while (*args == ' ') args++;
    if (!users_is_headchef()) {
        vga_puts("  Only the headchef hires cooks. Try: chef hire <name>\n");
        return;
    }
    if (!*args) { vga_puts("Usage: hire <name>\n"); return; }
    char name[USER_NAME_MAX];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < USER_NAME_MAX - 1) { name[i] = args[i]; i++; }
    name[i] = '\0';

    char secret[64], again[64];
    vga_printf("  Set a secret for %s: ", name);
    read_line(secret, sizeof(secret), '*');
    vga_puts("  Confirm secret:         ");
    read_line(again, sizeof(again), '*');
    if (!str_eq(secret, again)) {
        vga_puts("  Secrets do not match -- nobody hired.\n");
        return;
    }
    if (users_add(name, secret) == 0)
        vga_printf("  Hired: %s now has a place in the kitchen.\n", name);
    else
        vga_puts("  Could not hire (name taken, roster full, or disk error).\n");
}

static void cmd_fire(const char *args) {
    while (*args == ' ') args++;
    if (!users_is_headchef()) {
        vga_puts("  Only the headchef can fire cooks.\n");
        return;
    }
    if (!*args) { vga_puts("Usage: fire <name>\n"); return; }
    if (str_eq(args, "headchef")) {
        vga_puts("  The headchef cannot be fired.\n");
        return;
    }
    if (users_remove(args) == 0) vga_printf("  Fired: %s has left the kitchen.\n", args);
    else                         vga_puts("  No such cook (or disk error).\n");
}

/* perms <file>            - show owner + rwx
 * perms <file> <rwxrwx>   - set permissions (owner or headchef only) */
static void cmd_perms(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { vga_puts("Usage: perms <file> [rwxrwx]\n"); return; }

    char arg_name[FAT_PATH_MAX];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < FAT_PATH_MAX - 1) { arg_name[i] = args[i]; i++; }
    arg_name[i] = '\0';
    const char *spec = args + i;
    while (*spec == ' ') spec++;

    char path[FAT_PATH_MAX];
    resolve_path(arg_name, path);
    uint8_t owner, mode;
    if (fat_stat(path, &owner, &mode) < 0) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("  Not found: %s\n", arg_name);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    if (*spec) {
        if (strlen(spec) < 6) {
            vga_puts("  Spec must be 6 chars, e.g. rwxr-x (owner then cooks)\n");
            return;
        }
        if (users_current_uid() != owner && !users_is_headchef()) {
            vga_puts("  Only the owner or headchef may re-plate permissions.\n");
            return;
        }
        uint8_t m = FAT_PERM_MARK;
        if (spec[0] == 'r') m |= FAT_PERM_OR;
        if (spec[1] == 'w') m |= FAT_PERM_OW;
        if (spec[2] == 'x') m |= FAT_PERM_OX;
        if (spec[3] == 'r') m |= FAT_PERM_AR;
        if (spec[4] == 'w') m |= FAT_PERM_AW;
        if (spec[5] == 'x') m |= FAT_PERM_AX;
        if (fat_chmod(path, m) < 0) {
            vga_puts("  Could not change permissions (FAT32 or disk error).\n");
            return;
        }
        mode = m | FAT_PERM_MARK;
        vga_puts("  Re-plated.\n");
    }

    char s[7];
    s[0] = (mode & FAT_PERM_OR) ? 'r' : '-';
    s[1] = (mode & FAT_PERM_OW) ? 'w' : '-';
    s[2] = (mode & FAT_PERM_OX) ? 'x' : '-';
    s[3] = (mode & FAT_PERM_AR) ? 'r' : '-';
    s[4] = (mode & FAT_PERM_AW) ? 'w' : '-';
    s[5] = (mode & FAT_PERM_AX) ? 'x' : '-';
    s[6] = '\0';
    vga_printf("  %s\n", path);
    vga_printf("    owner: %s   perms: %s  (owner | cooks)\n",
               users_name_of(owner), s);
}

/* `chef`           - CPU info (the original command)
 * `chef <command>` - run <command> as the headchef (asks for the secret) */
static void cmd_chef(void);   /* defined earlier - CPU info */

static void cmd_chef_elevate(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { cmd_chef(); return; }            /* bare "chef" -> CPU info */

    uint8_t saved = users_current_uid();
    if (!users_is_headchef()) {
        char secret[64];
        vga_puts("  headchef's secret: ");
        read_line(secret, sizeof(secret), '*');
        if (users_check("headchef", secret) != 0) {
            vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
            vga_puts("  The headchef shakes their head. Denied.\n");
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            return;
        }
    }
    set_session_uid(0);                            /* elevate for one command */
    dispatch(args);
    set_session_uid(saved);                        /* drop back down */
}

static void cmd_hash(const char *args) {
    if (!args || !args[0]) {
        vga_puts("Usage: hash <text>\n"); return;
    }
    uint32_t h = alphasoup_hash(args, (uint32_t)strlen(args));
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_printf("  AlphaSOUP-32:  0x%08x  (%u)\n", h, h);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void cmd_beep(const char *args) {
    if (!args || !args[0]) {
        vga_puts("Usage: beep <freq> [ms]\n"); return;
    }
    uint32_t freq = (uint32_t)parse_num(args);
    while (*args && *args != ' ') args++;
    while (*args == ' ') args++;
    uint32_t ms = *args ? (uint32_t)parse_num(args) : 200;
    if (freq == 0) { vga_puts("  freq must be > 0\n"); return; }
    speaker_beep(freq, ms);
}

static void cmd_mandelbrot(void) {
/* 78×22 ASCII Mandelbrot - fixed-point arithmetic (SCALE = 1024)
 * Viewport: Re [-2.5, 1.0], Im [-1.0, 1.0]
 * At each pixel we check z -> z²+c: if it escapes within MAX_ITER
 * iterations, pick a char from the palette; otherwise draw '@'.
 * All arithmetic is 32-bit (values bounded by 2×SCALE before check). */
#define MB_W     78
#define MB_H     22
#define MB_ITER  48
#define MB_SC   1024
    /* Viewport bounds in fixed-point (×MB_SC): Re -2.5->1.0, Im -1.0->1.0 */
    static const char pal[] = " .`-,:;=+*i#%@";
    int plen = 14;   /* usable palette entries (not counting final '@') */

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int row = 0; row < MB_H; row++) {
        int ci = -MB_SC + 2*MB_SC * row / (MB_H - 1);          /* Im axis */
        for (int col = 0; col < MB_W; col++) {
            int cr = -5*MB_SC/2 + 7*MB_SC/2 * col / (MB_W - 1); /* Re axis */
            int zr = 0, zi = 0, it = 0;
            while (it < MB_ITER) {
                /* Escape check - bounds are safe: |zr|,|zi| ≤ 2×MB_SC here */
                if (zr*zr + zi*zi > 4 * MB_SC * MB_SC) break;
                int nzr = (zr*zr - zi*zi) / MB_SC + cr;
                int nzi = 2 * zr * zi / MB_SC + ci;
                zr = nzr; zi = nzi;
                it++;
            }
            if (it == MB_ITER) {
                vga_set_color(VGA_BLUE, VGA_BLACK);
                vga_putchar('@');
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            } else {
                /* Shade by depth - map 0..MB_ITER-1 to palette */
                vga_putchar(pal[it * plen / MB_ITER]);
            }
        }
        vga_putchar('\n');
    }
#undef MB_W
#undef MB_H
#undef MB_ITER
#undef MB_SC
}

static void cmd_vgademo(void) {
    vga13h_enter();
    vga13h_defpal();

    uint8_t *fb = vga13h_fb();

    /* ── plasma background ───────────────────────────────────── */
    /* Triangle-wave on x XOR triangle-wave on y -> diamond tiling */
    for (int y = 0; y < VGA13_H; y++) {
        int ty = y & 0x7F; if (ty > 63) ty = 127 - ty;   /* 0..63 */
        for (int x = 0; x < VGA13_W; x++) {
            int tx = x & 0x7F; if (tx > 63) tx = 127 - tx;
            uint8_t c = (uint8_t)(((tx + ty) * 2 + 1) & 0xFF);
            fb[y * VGA13_W + x] = c;
        }
    }

    /* ── soup bowl: stack of filled circles, dark to bright ───── */
    /* Outer shadow */
    vga13h_fill_circle(162, 102, 72, 20);
    /* Bowl layers - each a little smaller and brighter */
    for (int i = 0; i < 12; i++) {
        int r = 70 - i * 5;
        uint8_t c = (uint8_t)(30 + i * 18);
        vga13h_fill_circle(160, 100, r, c);
    }
    /* Highlight ring */
    vga13h_circle(160, 100, 71, 255);
    vga13h_circle(160, 100, 70, 200);

    /* ── noodle swirls: arcs using line segments ──────────────── */
    for (int a = 0; a < 360; a += 30) {
        /* Approximate arc: draw a short chord at angle a */
        int rad1 = 40, rad2 = 55;
        /* Use a tiny lookup: cos/sin via quarter-wave table */
        static const int ct[13] = {63,62,59,54,48,41,33,24,15,7,2,0,0};
        static const int st[13] = {0, 7,15,24,33,41,48,54,59,62,63,63,59};
        int qi = (a / 30) % 12;
        int cosv = ct[qi], sinv = st[qi];
        int quadrant = (a / 90) & 3;
        int cx1, cy1, cx2, cy2;
        /* Apply quadrant signs */
        int sx = (quadrant==1||quadrant==2) ? -1 : 1;
        int sy = (quadrant==2||quadrant==3) ? -1 : 1;
        cx1 = 160 + sx * cosv * rad1 / 63;
        cy1 = 100 + sy * sinv * rad1 / 63;
        cx2 = 160 + sx * cosv * rad2 / 63;
        cy2 = 100 + sy * sinv * rad2 / 63;
        uint8_t nc = (uint8_t)(128 + a);
        vga13h_line(cx1, cy1, cx2, cy2, nc);
    }

    /* ── border ───────────────────────────────────────────────── */
    vga13h_rect(0, 0, VGA13_W, VGA13_H, 255);
    vga13h_rect(2, 2, VGA13_W-4, VGA13_H-4, 200);

    /* Wait for any key, then back to text */
    keyboard_getchar();

    vga13h_exit();
    /* Resync hardware cursor with where the text driver left it */
    vga_set_cursor(vga_get_row(), vga_get_col());
}

/* Busy-wait for n timer ticks (10 ms each at 100 Hz). */
static void tick_delay(uint32_t ticks) {
    /* Cooperative delay: one PIT tick is 10 ms. task_sleep marks this
     * task BLOCKED so background tasks run while wipe/bounce animate. */
    task_sleep(ticks * 10);
}

static void cmd_wipe(void) {
    /* Sweep a colored bar top->bottom, then clear. */
    vga_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
    for (int row = 0; row < 25; row++) {
        vga_fill_row(row, ' ');
        tick_delay(2);
    }
    vga_set_color(VGA_BLACK, VGA_CYAN);
    for (int row = 0; row < 25; row++) {
        vga_fill_row(row, ' ');
        tick_delay(1);
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

/* Simple LCG PRNG - seeded from timer ticks on first use. */
static uint32_t rng_state = 0;
static uint32_t rng_next(void) {
    if (rng_state == 0) rng_state = timer_get_ticks() | 0xA5A5A5A5u;
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state;
}

static void cmd_fortune(void) {
    static uint8_t fbuf[4096];
    uint32_t fsize = 0;
    if (fat_read("FORTUNE.TXT", fbuf, sizeof(fbuf) - 1, &fsize) < 0) {
        vga_set_color(VGA_DARK_GREY, VGA_BLACK);
        vga_puts("  No FORTUNE.TXT on disk. Create one with:\n");
        vga_puts("    stir FORTUNE.TXT A watched pot never boils.\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    fbuf[fsize] = '\0';

    /* Count non-empty lines */
    int count = 0;
    for (uint32_t i = 0; i < fsize;) {
        uint32_t s = i;
        while (i < fsize && fbuf[i] != '\n') i++;
        if (i > s) count++;
        if (i < fsize) i++;
    }
    if (count == 0) {
        vga_puts("  (FORTUNE.TXT is empty)\n");
        return;
    }

    int pick = (int)(rng_next() % (uint32_t)count);
    int seen = 0;
    for (uint32_t i = 0; i < fsize;) {
        uint32_t s = i;
        while (i < fsize && fbuf[i] != '\n') i++;
        if (i > s) {
            if (seen == pick) {
                vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
                vga_puts("  ");
                for (uint32_t k = s; k < i; k++) {
                    char c = (char)fbuf[k];
                    if (c == '\r') continue;
                    vga_putchar(c);
                }
                vga_putchar('\n');
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                return;
            }
            seen++;
        }
        if (i < fsize) i++;
    }
}

static void cmd_clock(void) {
    /* Drain any pending input so ENTER doesn't exit immediately */
    keyboard_flush();
    vga_clear();

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_set_cursor(6, 33); vga_puts("soupOS clock");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_set_cursor(7, 33); vga_puts("============");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_set_cursor(16, 28); vga_puts("Press any key to exit.");

    rtc_time_t prev = {0};
    while (!keyboard_available()) {
        rtc_time_t t;
        rtc_read(&t);
        if (t.second != prev.second || t.minute != prev.minute ||
            t.hour   != prev.hour   || t.day    != prev.day) {
            vga_set_color(VGA_WHITE, VGA_BLACK);
            vga_set_cursor(10, 35);
            vga_printf("%02u:%02u:%02u", t.hour, t.minute, t.second);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_set_cursor(12, 34);
            vga_printf("%u-%02u-%02u", (uint32_t)t.year, t.month, t.day);
            prev = t;
        }
        /* Yield so background tasks run, then park until the next IRQ. */
        task_yield();
        if (!keyboard_available())
            __asm__ volatile ("hlt");
    }
    keyboard_flush();
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

/* ---- task/scheduler commands ---- */

static void cmd_ps(void) {
    task_t *head = task_list_head();
    if (!head) { vga_puts("(no tasks)\n"); return; }
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("  ID  ST  STACK   NAME\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    task_t *t = head;
    do {
        vga_printf("  %2u   %c  %5u   %s%s\n",
                   t->id,
                   task_state_letter(t->state),
                   t->stack_size,
                   t->name,
                   (t == task_current()) ? " *" : "");
        t = t->next;
    } while (t != head);
}

static void cmd_kill(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { vga_puts("Usage: kill <id>\n"); return; }
    int id = parse_num(args);
    int r  = task_kill((uint32_t)id);
    if (r == 0) vga_printf("killed task %d\n", id);
    else        vga_printf("kill: task %d not killable (unknown, self, or kernel)\n", id);
}

/* Background clock - writes HH:MM:SS directly to the VGA text buffer
 * at row 0, cols 72-79. Doesn't touch vga.c's tracked cursor/color so
 * the shell keeps working underneath. Exits when the task is killed. */
static void bgclock_task(void *arg) {
    (void)arg;
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    const int base = 0 * 80 + 72;
    const uint16_t attr = (uint16_t)(0x0E) << 8;   /* yellow on black */
    for (;;) {
        rtc_time_t t;
        rtc_read(&t);
        char s[8];
        s[0] = '0' + (t.hour   / 10) % 10;
        s[1] = '0' +  t.hour   % 10;
        s[2] = ':';
        s[3] = '0' + (t.minute / 10) % 10;
        s[4] = '0' +  t.minute % 10;
        s[5] = ':';
        s[6] = '0' + (t.second / 10) % 10;
        s[7] = '0' +  t.second % 10;
        for (int i = 0; i < 8; i++)
            vga[base + i] = attr | (uint8_t)s[i];

        /* Yield for ~250 ms between redraws. Co-op: we burn one yield
         * per scheduler round, not CPU cycles. */
        uint32_t start = timer_get_ticks();
        while (timer_get_ticks() - start < 25) task_yield();
    }
}

static void cmd_bgclock(void) {
    task_t *t = task_spawn("bgclock", bgclock_task, 0);
    if (!t) { vga_puts("bgclock: out of memory\n"); return; }
    vga_printf("bgclock running as task %u - stop with: kill %u\n", t->id, t->id);
}

/* yieldbg - spawn a background task that logs a [bg] marker to the serial
 * port every ~300 ms for ~6 s, then exits. It only makes progress when some
 * other task yields the CPU. Run it, then `cook spin.elf`: the [bg] and [u]
 * markers interleave in the serial log, proving a ring-3 program's sys_yield()
 * reaches the scheduler. (klog mirrors every byte to COM1 -> the serial log.) */
static void yieldbg_task(void *arg) {
    (void)arg;
    for (int i = 0; i < 20; i++) {
        klog("[bg] tick %u t=%u\n", (uint32_t)i, timer_get_ticks());
        task_sleep(300);
    }
    klog("[bg] done\n");
}

static void cmd_yieldbg(void) {
    task_t *t = task_spawn("yieldbg", yieldbg_task, 0);
    if (!t) { vga_puts("yieldbg: out of memory\n"); return; }
    vga_printf("yieldbg running as task %u -- markers go to serial (dmesg)\n", t->id);
}

/* spinbg - a CPU-bound task that NEVER calls task_yield(): it just busy-loops
 * for ~4 s, logging a [spin] marker to serial each ~0.5 s. Under the old
 * cooperative scheduler this would freeze the whole system until it finished;
 * with timer-driven preemption the shell stays responsive and other tasks
 * (e.g. a yieldbg sleeper) keep running. Proof of preemption. */
static void spinbg_task(void *arg) {
    (void)arg;
    uint32_t start = timer_get_ticks();
    uint32_t next  = 0;
    volatile uint32_t sink = 0;
    for (;;) {
        uint32_t elapsed = timer_get_ticks() - start;
        if (elapsed >= 400) break;          /* ~4 s at 100 Hz */
        if (elapsed >= next) {
            klog("[spin] busy tick t=%u (no yield)\n", timer_get_ticks());
            next += 50;
        }
        for (int i = 0; i < 200000; i++) sink += (uint32_t)i;  /* burn CPU, never yield */
    }
    klog("[spin] done\n");
}

static void cmd_spinbg(void) {
    task_t *t = task_spawn("spinbg", spinbg_task, 0);
    if (!t) { vga_puts("spinbg: out of memory\n"); return; }
    vga_printf("spinbg running as task %u -- busy-loops ~4s WITHOUT yielding\n", t->id);
}

/* sleeptest - exercises task_sleep and proves other tasks run while we sleep.
 *
 * Sequence:
 *   1. Print start tick.
 *   2. Spawn a bg task that prints "bg tick=N" twice with a 200 ms sleep
 *      between prints - proves the scheduler keeps running during main sleep.
 *   3. Main task sleeps 500 ms.
 *   4. Print woke tick + delta (expected ~50 ticks at 100 Hz).
 */
static void sleeptest_bg(void *arg) {
    (void)arg;
    vga_printf("sleeptest bg: tick=%u\n", timer_get_ticks());
    task_sleep(200);
    vga_printf("sleeptest bg: tick=%u (after 200 ms sleep)\n", timer_get_ticks());
    /* task exits naturally */
}

static void cmd_sleeptest(void) {
    uint32_t t0 = timer_get_ticks();
    vga_printf("sleeptest: start tick=%u\n", t0);

    task_t *bg = task_spawn("slptest_bg", sleeptest_bg, 0);
    if (!bg) vga_puts("sleeptest: warning: could not spawn bg task\n");

    task_sleep(500);

    uint32_t t1    = timer_get_ticks();
    uint32_t delta = t1 - t0;
    vga_printf("sleeptest: woke  tick=%u delta=%u (expect ~50)\n", t1, delta);
}

/* vfstest - exercises the VFS shim: a /dev char device, a file write/read
 * round-trip, and sizing an existing file - all through one vfs_* API. */
static void cmd_vfstest(void) {
    /* 1. character device - write a line to /dev/serial. */
    vfs_node_t *s = vfs_open("/dev/serial", VFS_WRONLY);
    if (s) {
        const char *msg = "[vfstest] hello from the VFS via /dev/serial\n";
        vfs_write(s, msg, strlen(msg));
        vfs_close(s);
        vga_puts("  /dev/serial : wrote a line (check the serial log)\n");
    } else {
        vga_puts("  /dev/serial : open FAILED\n");
    }

    /* 2. /dev/zero - read 16 bytes, confirm they all came back zero. */
    vfs_node_t *z = vfs_open("/dev/zero", VFS_RDONLY);
    if (z) {
        uint8_t b[16];
        memset(b, 0xFF, sizeof(b));
        int r = vfs_read(z, b, sizeof(b));
        int allzero = (r == 16);
        for (int i = 0; i < 16; i++) if (b[i]) allzero = 0;
        vfs_close(z);
        vga_printf("  /dev/zero   : read %d bytes, all-zero=%s\n",
                   r, allzero ? "yes" : "no");
    }

    /* 3. round-trip a FAT file through the VFS. */
    const char *payload = "soup is best served hot\n";
    vfs_node_t *w = vfs_open("/VFSTEST.TXT", VFS_WRONLY | VFS_CREATE);
    if (w) {
        vfs_write(w, payload, strlen(payload));
        vfs_close(w);                       /* flushes via fat_write */
        vfs_node_t *rd = vfs_open("VFSTEST.TXT", VFS_RDONLY);
        if (rd) {
            char buf[64];
            int r = vfs_read(rd, buf, sizeof(buf) - 1);
            if (r < 0) r = 0;
            buf[r] = 0;
            vga_printf("  VFSTEST.TXT : wrote %u, read back %d bytes: %s",
                       (uint32_t)strlen(payload), r, buf);
            vfs_close(rd);
        } else {
            vga_puts("  VFSTEST.TXT : reopen for read FAILED\n");
        }
    } else {
        vga_puts("  VFSTEST.TXT : open for write FAILED\n");
    }

    /* 4. size an existing file via the VFS. */
    vfs_node_t *f = vfs_open("README.TXT", VFS_RDONLY);
    if (f) {
        vga_printf("  README.TXT  : vfs_size() reports %u bytes\n",
                   vfs_size(f));
        vfs_close(f);
    } else {
        vga_puts("  README.TXT  : not found (skipped)\n");
    }
}

/* dmesg - dump the kernel log ring buffer (boot messages, panics). */
static void cmd_dmesg(void) {
    static char buf[KLOG_SIZE + 1];
    uint32_t n = klog_copy(buf, KLOG_SIZE);
    buf[n] = 0;
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_printf("  --- kernel log: %u bytes ---\n", n);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts(buf);
    if (n == 0 || buf[n - 1] != '\n') vga_putchar('\n');
}

static int is_leap_year(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int year, int month) {
    static const int dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && is_leap_year(year)) return 29;
    return dm[month - 1];
}

/* Zeller's congruence - returns 0=Sun, 1=Mon, ..., 6=Sat */
static int day_of_week(int year, int month, int day) {
    if (month < 3) { month += 12; year--; }
    int K = year % 100;
    int J = year / 100;
    int h = (day + 13 * (month + 1) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
    /* Convert Zeller (0=Sat) -> (0=Sun) */
    return (h + 6) % 7;
}

static void cmd_cal(void) {
    static const char *const month_name[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    rtc_time_t t;
    rtc_read(&t);
    int year  = t.year;
    int month = t.month;
    int today = t.day;
    if (month < 1 || month > 12) { vga_puts("  cal: bad month\n"); return; }

    int dim = days_in_month(year, month);
    int first_dow = day_of_week(year, month, 1);

    /* Header: centered "Month Year" in a 20-char-wide calendar body */
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    const char *mn = month_name[month - 1];
    int mn_len = (int)strlen(mn);
    /* "Month YYYY" length = mn_len + 5. Center in 20 cols. */
    int total_len = mn_len + 5;
    int pad = (20 - total_len) / 2;
    if (pad < 0) pad = 0;
    for (int i = 0; i < pad; i++) vga_putchar(' ');
    vga_printf("%s %u\n", mn, (uint32_t)year);

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("Su Mo Tu We Th Fr Sa\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    /* Leading blanks for days before the 1st */
    for (int i = 0; i < first_dow; i++) vga_puts("   ");

    int col = first_dow;
    for (int d = 1; d <= dim; d++) {
        if (d == today) {
            vga_set_color(VGA_BLACK, VGA_WHITE);
            vga_printf("%2d", d);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            vga_putchar(' ');
        } else {
            vga_printf("%2d ", d);
        }
        col++;
        if (col == 7) {
            vga_putchar('\n');
            col = 0;
        }
    }
    if (col != 0) vga_putchar('\n');
}

static void cmd_uname(const char *args) {
    int show_all = args && (args[0] == '-' && args[1] == 'a');
    if (show_all) {
        vga_puts("soupOS 0.7.6 i686 soup-kernel protected-mode\n");
    } else {
        vga_puts("soupOS\n");
    }
}

/* --- cat command (hidden) --------------------------------------------------
 * Not listed in cmd_menu() or cmd_names[] (no tab-completion). Plays an
 * infinite spinning-cat animation. Keyboard input is continually flushed so
 * the user cannot leave. Only a reboot ends it. Enjoy. */
static void cmd_cat(void) {
    /* Eight frames of a cat head rotating through 360°. */
    static const char *const frames[][6] = {
        {   /* 0° - front */
            "    /\\_/\\    ",
            "   ( o.o )   ",
            "    > ^ <    ",
            "   /     \\   ",
            "  ( |   | )  ",
            "   \\__|__/   ",
        },
        {   /* 45° */
            "    /\\_/\\_   ",
            "   ( o.-  \\  ",
            "    \\ ^ >    ",
            "    /    \\_  ",
            "   ( |    | )",
            "    \\__|__/  ",
        },
        {   /* 90° - right profile */
            "    /\\_/\\    ",
            "   ( o  . > ",
            "    \\___/    ",
            "    /   \\    ",
            "   (_|   |_) ",
            "     \\_|_/   ",
        },
        {   /* 135° */
            "    _/\\_/\\   ",
            "   /  -.-  ) ",
            "    < v /    ",
            "   _/    \\   ",
            "  ( |    | ) ",
            "   \\__|__/   ",
        },
        {   /* 180° - back */
            "    /\\_/\\    ",
            "   (  -  )   ",
            "    \\___/    ",
            "    /   \\    ",
            "   ( |   | ) ",
            "    \\__|__/  ",
        },
        {   /* 225° */
            "    /\\_/\\_   ",
            "   (  -.- \\  ",
            "    \\ v >    ",
            "    /    \\_  ",
            "   ( |    | )",
            "    \\__|__/  ",
        },
        {   /* 270° - left profile */
            "    /\\_/\\    ",
            "   < .  o )  ",
            "    \\___/    ",
            "    /   \\    ",
            "   (_|   |_) ",
            "     \\_|_/   ",
        },
        {   /* 315° */
            "   _/\\_/\\    ",
            "   /  -.o  ) ",
            "    < ^ >    ",
            "   /    \\_   ",
            "  ( |    | ) ",
            "    \\__|__/  ",
        },
    };
    #define N_CAT_FRAMES ((int)(sizeof(frames) / sizeof(frames[0])))
    #define CAT_LINES    6
    #define CAT_WIDTH    13

    keyboard_flush();
    vga_clear();

    /* Static decorations */
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_set_cursor(2, 32); vga_puts("* MAXWELL *");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_set_cursor(3, 30); vga_puts("~~~~~~~~~~~~~~~");

    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_set_cursor(19, 28); vga_puts("you cannot escape him");

    int frame = 0;
    int top_row = 7;
    int left_col = (80 - CAT_WIDTH) / 2;
    int escaped = 0;
    uint32_t last = timer_get_ticks();

    while (1) {
        uint32_t now = timer_get_ticks();
        if (now - last >= 12) {  /* ~120ms per frame */
            vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
            for (int row = 0; row < CAT_LINES; row++) {
                vga_set_cursor(top_row + row, left_col);
                vga_puts(frames[frame][row]);
            }

            /* Flash the taunt every half-rotation */
            if (frame == 0) {
                vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
                vga_set_cursor(19, 28); vga_puts("you cannot escape him");
            } else if (frame == 4) {
                vga_set_color(VGA_BLACK, VGA_LIGHT_RED);
                vga_set_cursor(19, 28); vga_puts("YOU CANNOT ESCAPE HIM");
            }

            frame = (frame + 1) % N_CAT_FRAMES;
            last = now;
        }

        /* Swallow anything the user types - except ESC. The gag is that he
         * ignores you; an unescapable command is a different thing. On a
         * hosted CTF instance it costs the player their box, and `cat` is
         * exactly what someone types expecting Unix cat (soupOS spells that
         * `pour`). */
        while (keyboard_available()) {
            if (keyboard_getchar() == 27) { escaped = 1; break; }
        }
        if (escaped) break;
        /* Yield so background tasks keep ticking, then park for an IRQ. */
        task_yield();
        __asm__ volatile ("hlt");
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
    vga_puts("  ...he lets you go, this time.\n");
    #undef N_CAT_FRAMES
    #undef CAT_LINES
    #undef CAT_WIDTH
}

static void cmd_bounce(void) {
    /* Screensaver: four colored balls ricochet around mode 13h. */
    #define BALLS 4
    int bx[BALLS], by[BALLS], vx[BALLS], vy[BALLS];
    uint8_t bc[BALLS];
    int radius = 10;

    /* Seed positions/velocities from the PRNG */
    for (int i = 0; i < BALLS; i++) {
        bx[i] = radius + (int)(rng_next() % (VGA13_W - 2 * radius));
        by[i] = radius + (int)(rng_next() % (VGA13_H - 2 * radius));
        vx[i] = (rng_next() & 1) ? 2 + (int)(rng_next() % 2) : -(2 + (int)(rng_next() % 2));
        vy[i] = (rng_next() & 1) ? 1 + (int)(rng_next() % 2) : -(1 + (int)(rng_next() % 2));
        bc[i] = (uint8_t)(32 + i * 40);
    }

    keyboard_flush();
    vga13h_enter();
    vga13h_defpal();

    while (!keyboard_available()) {
        /* Fade background: dim everything one shade toward 0 every few frames */
        uint8_t *fb = vga13h_fb();
        for (int i = 0; i < VGA13_W * VGA13_H; i++) {
            uint8_t v = fb[i];
            if (v > 0) fb[i] = (uint8_t)(v - 1);
        }

        for (int i = 0; i < BALLS; i++) {
            bx[i] += vx[i];
            by[i] += vy[i];
            if (bx[i] < radius)              { bx[i] = radius;              vx[i] = -vx[i]; }
            if (bx[i] > VGA13_W - radius - 1){ bx[i] = VGA13_W - radius - 1; vx[i] = -vx[i]; }
            if (by[i] < radius)              { by[i] = radius;              vy[i] = -vy[i]; }
            if (by[i] > VGA13_H - radius - 1){ by[i] = VGA13_H - radius - 1; vy[i] = -vy[i]; }
            vga13h_fill_circle(bx[i], by[i], radius, bc[i]);
            vga13h_circle(bx[i], by[i], radius, 255);
        }

        tick_delay(3);
    }

    keyboard_flush();
    vga13h_exit();
    vga_set_cursor(vga_get_row(), vga_get_col());
    #undef BALLS
}

static void cmd_recipe(void) {
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("  soupOS  v0.7.6\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("  Architecture:  i686 (IA-32, protected mode)\n");
    vga_puts("  Kernel base:   0x00100000\n");
    vga_puts("  Features:\n");
    vga_puts("    GDT  IDT  PIC  PIT  PS/2  VGA\n");
    vga_puts("    PMM  Paging (identity-map)  Heap\n");
    vga_puts("    RTC  PCI scan  CPUID\n");
    vga_puts("    ATA PIO  FAT16 read+write + bowls (subdirs)  PC speaker\n");
    vga_puts("    Users + access control (headchef/cooks)  chef elevation\n");
    vga_puts("    Cooperative scheduler  VFS + /dev nodes\n");
    vga_puts("    Kernel log ring (dmesg)  register-dump panic\n");
    vga_puts("    soupyc (arrays/for/file-I/O/stdlib)  Tab completion\n");
    vga_puts("    AlphaSOUP-32 hash  ASCII Mandelbrot\n");
    vga_printf("  Free RAM:      %u KB\n", pmm_free_kb());
    vga_printf("  Uptime:        %u seconds\n", timer_get_seconds());
    if (fat_get_type()) {
        vga_printf("  Disk volume:   FAT%d [%s]\n", fat_get_type(), fat_label());
        vga_printf("  Disk size:     %u MB\n", ata_total_sectors() / 2048);
    }
}

static void cmd_leftovers(void) {
    if (hist_size == 0) { vga_puts("  No leftovers yet.\n"); return; }
    vga_set_color(VGA_YELLOW, VGA_BLACK); vga_puts("  Leftovers (history):\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int i = hist_size; i >= 1; i--) {
        const char *h = hist_get(i);
        if (h) vga_printf("    %2d  %s\n", hist_size - i + 1, h);
    }
}

static void cmd_spill(void) {
    vga_puts("Spilling the soup...\n");
    volatile int zero = 0;
    volatile int x = 1 / zero;
    (void)x;
}

static void cmd_freeze(void) {
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    vga_puts("Soup frozen. Reset to reheat.\n");
    __asm__ volatile ("cli; hlt");
    while (1) {}
}

static void cmd_reheat(void) {
    vga_puts("Reheating...\n");
    uint8_t s;
    do { __asm__ volatile ("inb $0x64, %0" : "=a"(s)); } while (s & 0x02);
    outb(0x64, 0xFE);
    __asm__ volatile (
        "subl $6, %%esp\n"
        "movw $0, (%%esp)\n"
        "movl $0, 2(%%esp)\n"
        "lidt (%%esp)\n"
        "int $0x00\n"
        : : : "memory"
    );
    while (1) {}
}

#ifndef NO_DOOM
/* doom - the port, un-parked 2026-08-26.
 *
 * Refuses to run while any other task is alive. doom.c has no task_yield() in
 * its gameplay path: it was written for the cooperative scheduler, where
 * monopolising the CPU was correct. Since v0.7.4 the timer preempts, and only
 * heap.c was made preemption-safe, so a background task can be scheduled
 * mid-frame and write to the VGA text buffer while Doom owns mode 13h and the
 * DAC, or race fat.c's file-scope state while Doom streams lumps from the WAD.
 *
 * Refusing is deliberately chosen over preempt_disable() around the session:
 * sched_locked is a single global that unbalances across a context switch, and
 * Doom's menus call keyboard_getchar(), which yields. Wrapping the session
 * would leak the lock into an unrelated task the first time a player paused on
 * a menu. See docs/doom-reenable-plan.md. */
static void cmd_doom(void) {
    if (task_count() > 1) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_puts("  Doom needs the machine to itself.\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vga_puts("  Stop your background tasks first:  ps, then kill <id>\n");
        return;
    }
    if (!fat_exists("DOOM1.WAD")) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        vga_puts("  No DOOM1.WAD on disk.\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vga_puts("  Put the shareware WAD next to the Makefile and run: make wad\n");
        return;
    }
    doom_menu_run();
    vga_clear();
}
#endif /* NO_DOOM */

/* preempttest - prove preempt_disable() actually suppresses preemption, and
 * that it balances.
 *
 * The background task never yields, so it only ever runs if the timer
 * preempts into it. Phase A leaves preemption on and the counter should move;
 * phase B holds preempt_disable and it must not; phase C releases and it
 * should move again, which is what shows the release balanced.
 *
 * Before preempt_disable became per-task this could not be tested at all: it
 * routed through the scheduler's own lock, which is designed to unbalance
 * across a context switch. */
static volatile uint32_t pt_counter;
static volatile int      pt_stop;

static void pt_bg(void *arg) {
    (void)arg;
    while (!pt_stop) pt_counter++;      /* deliberately never yields */
}

static uint32_t pt_spin(uint32_t ticks) {
    uint32_t start = timer_get_ticks();
    uint32_t before = pt_counter;
    while (timer_get_ticks() - start < ticks) { /* burn, never yield */ }
    return pt_counter - before;
}

static void cmd_preempttest(void) {
    pt_counter = 0; pt_stop = 0;
    task_t *bg = task_spawn("pt_bg", pt_bg, 0);
    if (!bg) { vga_puts("preempttest: out of memory\n"); return; }

    uint32_t a = pt_spin(30);                 /* A: preemption on   */
    preempt_disable();
    uint32_t b = pt_spin(30);                 /* B: must be frozen  */
    preempt_enable();
    uint32_t c = pt_spin(30);                 /* C: moving again    */

    pt_stop = 1;
    task_yield();                             /* let bg observe the flag */

    vga_printf("  A preempt on   : %u\n", a);
    vga_printf("  B disabled     : %u\n", b);
    vga_printf("  C re-enabled   : %u\n", c);
    vga_printf("  depth after    : %d\n", task_current()->preempt_depth);
    klog("[pt] A=%u B=%u C=%u depth=%d\n", a, b, c,
         task_current()->preempt_depth);

    int ok = (a > 0) && (b == 0) && (c > 0) &&
             (task_current()->preempt_depth == 0);
    vga_set_color(ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    vga_puts(ok ? "  [pass] preempt_disable suppresses and balances\n"
                : "  [FAIL] preempt_disable is not behaving\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    klog(ok ? "[pt] PASS\n" : "[pt] FAIL\n");
}

/* fatstress - two tasks hammering the filesystem at once.
 *
 * fat.c keeps its cursor, sector cache, free-cluster hint and fd table in
 * file-scope state, and the timer can switch tasks anywhere. Each task writes
 * its own file with a known pattern and reads it straight back; if the layers
 * interleave, a read comes back as the other task's data or as garbage. */
#define FS_ITERS 12
static volatile int fs_bg_errors, fs_bg_done;

static void fs_fill(char *buf, char tag, int i) {
    for (int k = 0; k < 32; k++) buf[k] = tag;
    buf[0]  = tag;
    buf[1]  = (char)('0' + (i % 10));
    buf[32] = '\0';
}

static int fs_cycle(const char *path, char tag) {
    char w[33], r[64];
    uint32_t got = 0;
    int errors = 0;
    for (int i = 0; i < FS_ITERS; i++) {
        fs_fill(w, tag, i);
        if (fat_write(path, (const uint8_t *)w, 32) < 0) { errors++; continue; }
        for (int k = 0; k < (int)sizeof(r); k++) r[k] = 0;
        if (fat_read(path, (uint8_t *)r, sizeof(r) - 1, &got) < 0) { errors++; continue; }
        if (got != 32) { errors++; continue; }
        for (int k = 0; k < 32; k++)
            if (r[k] != w[k]) { errors++; break; }
    }
    return errors;
}

static void fs_bg(void *arg) {
    (void)arg;
    fs_bg_errors = fs_cycle("/FSB.TXT", 'B');
    fs_bg_done   = 1;
}

static void cmd_fatstress(void) {
    fs_bg_errors = 0; fs_bg_done = 0;
    vga_printf("  two tasks, %d write+verify cycles each...\n", FS_ITERS);
    task_t *bg = task_spawn("fatstress", fs_bg, 0);
    if (!bg) { vga_puts("fatstress: out of memory\n"); return; }

    int fg = fs_cycle("/FSA.TXT", 'A');
    while (!fs_bg_done) { task_yield(); __asm__ volatile ("hlt"); }

    vga_printf("  foreground mismatches: %d\n", fg);
    vga_printf("  background mismatches: %d\n", fs_bg_errors);
    int ok = (fg == 0 && fs_bg_errors == 0);
    vga_set_color(ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    vga_puts(ok ? "  [pass] concurrent FAT access stayed consistent\n"
                : "  [FAIL] FAT state was corrupted by interleaving\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    klog(ok ? "[fatstress] PASS fg=%d bg=%d\n" : "[fatstress] FAIL fg=%d bg=%d\n",
         fg, fs_bg_errors);
}

/* vgastress - two tasks printing whole lines at once.
 *
 * Each task prints a line of a single repeated character. vga_puts is
 * serialised, so every line must come out pure; if the guard is missing the
 * timer lands mid-line and the two characters interleave. The mirror to COM1
 * means the host can check the transcript for mixed lines. */
#define VS_LINES 40
static volatile int vs_done;

static void vs_emit(char tag, int n) {
    char line[42];
    for (int k = 0; k < 40; k++) line[k] = tag;
    line[40] = '\n'; line[41] = '\0';
    for (int i = 0; i < n; i++) { vga_puts(line); task_yield(); }
}

static void vs_bg(void *arg) { (void)arg; vs_emit('B', VS_LINES); vs_done = 1; }

static void cmd_vgastress(void) {
    vs_done = 0;
    klog("[vgastress] begin\n");
    task_t *bg = task_spawn("vgastress", vs_bg, 0);
    if (!bg) { vga_puts("vgastress: out of memory\n"); return; }
    vs_emit('A', VS_LINES);
    while (!vs_done) { task_yield(); __asm__ volatile ("hlt"); }
    klog("[vgastress] end\n");
    vga_puts("  vgastress done - check the transcript for mixed lines\n");
}

/* vmtest - prove a second address space is real and isolated.
 *
 * The kernel half of every directory is shared by pointer, so switching CR3
 * mid-execution has to keep working: the code, stack and heap we are running
 * from are all identity-mapped low. If that sharing were wrong this would
 * triple-fault instantly rather than print anything.
 *
 * The isolation check deliberately asks paging_is_mapped rather than
 * dereferencing, because reading an unmapped user address would fault. */
#define VM_PROBE  0xC0000000u

static void cmd_vmtest(void) {
    uint32_t *kdir = paging_kernel_dir();
    int fail = 0;

    uint32_t *pdir = paging_new_dir();
    if (!pdir) { vga_puts("vmtest: could not allocate a directory\n"); return; }
    vga_printf("  kernel dir %p   new dir %p\n", (void *)kdir, (void *)pdir);

    int mapped_before = paging_is_mapped(VM_PROBE);

    paging_switch(pdir);
    vga_puts("  switched CR3 to the new space (kernel still running)\n");

    /* Map a user page here and leave a marker in it. */
    void *frame = pmm_alloc_page();
    if (!frame) { paging_switch(kdir); paging_free_dir(pdir);
                  vga_puts("vmtest: out of frames\n"); return; }
    paging_map(VM_PROBE, (uint32_t)frame, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    *(volatile uint32_t *)VM_PROBE = 0x50555000u;
    int in_new = paging_is_mapped(VM_PROBE);
    uint32_t readback = *(volatile uint32_t *)VM_PROBE;

    paging_switch(kdir);
    int in_kernel = paging_is_mapped(VM_PROBE);

    paging_switch(pdir);
    uint32_t still = *(volatile uint32_t *)VM_PROBE;
    paging_switch(kdir);

    paging_free_dir(pdir);
    int after_free = paging_is_mapped(VM_PROBE);

    vga_printf("  probe mapped in kernel dir before : %d (want 0)\n", mapped_before);
    vga_printf("  probe mapped in new dir           : %d (want 1)\n", in_new);
    vga_printf("  marker read back in new dir       : 0x%x\n", readback);
    vga_printf("  probe mapped in kernel dir after  : %d (want 0)\n", in_kernel);
    vga_printf("  marker survives a round trip      : 0x%x\n", still);
    vga_printf("  probe mapped after free           : %d (want 0)\n", after_free);

    fail = (mapped_before != 0) || (in_new != 1) || (in_kernel != 0) ||
           (readback != 0x50555000u) || (still != 0x50555000u) || (after_free != 0);
    vga_set_color(fail ? VGA_LIGHT_RED : VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts(fail ? "  [FAIL] address spaces are not isolated\n"
                  : "  [pass] second address space is real and isolated\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    klog(fail ? "[vmtest] FAIL\n" : "[vmtest] PASS\n");
}

/* ---- tab completion ---- */

static void prompt(void);  /* forward declaration */

/* Master list of shell command names. Keep in sync with dispatch(). */
static const char *const cmd_names[] = {
#ifndef NO_DOOM
    "doom",
#endif
    "menu", "wash", "slurp", "season", "simmer", "broth", "ladle", "expiry",
    "chef", "pantry", "serve", "pour", "stir", "strain", "decant", "relabel",
    "jot", "ai", "cook", "soup", "hash",
    "beep", "mandelbrot", "vgademo", "uname", "cal",
    "clock", "wipe", "fortune", "bounce", "recipe", "leftovers", "spill",
    "freeze", "reheat", "ps", "kill", "bgclock", "yieldbg", "spinbg", "sleeptest", "vfstest",
    "dmesg", "pwd", "cd", "mkbowl", "rmbowl", "bowltest",
    "whoami", "roster", "clockout", "hire", "fire", "perms",
};
#define CMD_COUNT  (int)(sizeof(cmd_names) / sizeof(cmd_names[0]))

/* Case-insensitive: does `name` start with `pfx` (length pfx_len)? */
static int prefix_match_ci(const char *name, const char *pfx, int pfx_len) {
    for (int j = 0; j < pfx_len; j++) {
        char a = pfx[j], b = name[j];
        if (!b) return 0;
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
    }
    return 1;
}

static void shell_tab_complete(char *input, int *pos, int *len) {
    /* Find the start of the last word before cursor */
    int word_start = *pos;
    while (word_start > 0 && input[word_start - 1] != ' ') word_start--;
    int word_len = *pos - word_start;

    /* First word on the line? Skip leading spaces to decide. */
    int line_start = 0;
    while (line_start < word_start && input[line_start] == ' ') line_start++;
    int complete_commands = (word_start == line_start);

    /* Collect candidates: commands (if first word) + filenames */
    static char matches[FAT_LS_MAX + 32][FAT_NAME_MAX + 1];
    int max_matches = (int)(sizeof(matches) / sizeof(matches[0]));
    int nmatches = 0;

    if (complete_commands) {
        for (int i = 0; i < CMD_COUNT && nmatches < max_matches; i++) {
            if (prefix_match_ci(cmd_names[i], input + word_start, word_len)) {
                strncpy(matches[nmatches], cmd_names[i], FAT_NAME_MAX);
                matches[nmatches][FAT_NAME_MAX] = '\0';
                nmatches++;
            }
        }
    }

    static fat_entry_t entries[FAT_LS_MAX];
    int n = fat_ls(cwd, entries, FAT_LS_MAX);
    for (int i = 0; i < n && nmatches < max_matches; i++) {
        if (prefix_match_ci(entries[i].name, input + word_start, word_len)) {
            strncpy(matches[nmatches], entries[i].name, FAT_NAME_MAX);
            matches[nmatches][FAT_NAME_MAX] = '\0';
            nmatches++;
        }
    }
    if (nmatches == 0) return;

    if (nmatches == 1) {
        /* Single match - insert the remaining characters */
        const char *completion = matches[0] + word_len;
        while (*completion) shell_insert(input, pos, len, *completion++);
    } else {
        /* Multiple matches - list them below, then redraw the prompt */
        cursor_to(*len);
        vga_putchar('\n');
        for (int i = 0; i < nmatches; i++) {
            vga_puts(matches[i]);
            vga_putchar(' ');
        }
        vga_putchar('\n');
        prompt();   /* updates prompt_row */
        for (int i = 0; i < *len; i++) vga_putchar(input[i]);
        cursor_to(*pos);
    }
}

/* ---- prompt & dispatch ---- */

static void prompt(void) {
    const char *u = users_current_name();
    prompt_row = vga_get_row();
    vga_set_color(users_is_headchef() ? VGA_LIGHT_RED : VGA_LIGHT_GREEN, VGA_BLACK);
    vga_puts(u);
    vga_set_color(VGA_DARK_GREY,   VGA_BLACK); vga_putchar('@');
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK); vga_puts("soupOS");
    vga_set_color(VGA_LIGHT_CYAN,  VGA_BLACK); vga_putchar(':'); vga_puts(cwd);
    vga_set_color(VGA_WHITE, VGA_BLACK);       vga_puts("> ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    /* name + "@" + "soupOS" + ":" + cwd + "> " */
    prompt_len = (int)strlen(u) + 1 + 6 + 1 + (int)strlen(cwd) + 2;
}

static void dispatch(const char *buf) {
    const char *p = buf;
    while (*p == ' ') p++;
    if (!*p) return;

    if      (str_eq(p, "menu"))       cmd_menu();
    else if (str_eq(p, "wash"))       vga_clear();
    else if (str_eq(p, "simmer"))     cmd_simmer();
    else if (str_eq(p, "broth"))      cmd_broth();
    else if (str_eq(p, "ladle"))      cmd_ladle();
    else if (str_eq(p, "expiry"))     cmd_expiry();
    else if (str_startswith(p, "chef")) {
        const char *a = p + 4; while (*a == ' ') a++;
        cmd_chef_elevate(a);
    }
    else if (str_eq(p, "pantry"))     cmd_pantry();
    else if (str_eq(p, "recipe"))     cmd_recipe();
    else if (str_eq(p, "leftovers"))  cmd_leftovers();
    else if (str_eq(p, "spill"))      cmd_spill();
    else if (str_eq(p, "freeze"))     cmd_freeze();
    else if (str_eq(p, "reheat"))     cmd_reheat();
    else if (str_startswith(p, "slurp")) {
        const char *a = p + 5; while (*a == ' ') a++;
        cmd_slurp(a);
    }
    else if (str_startswith(p, "season")) {
        const char *a = p + 6; while (*a == ' ') a++;
        cmd_season(a);
    }
    else if (str_eq(p, "serve"))       cmd_serve();
    else if (str_startswith(p, "pour")) {
        const char *a = p + 4; while (*a == ' ') a++;
        cmd_pour(a);
    }
    else if (str_startswith(p, "stir")) {
        const char *a = p + 4; while (*a == ' ') a++;
        cmd_stir(a);
    }
    else if (str_startswith(p, "strain")) {
        const char *a = p + 6; while (*a == ' ') a++;
        cmd_strain(a);
    }
    else if (str_startswith(p, "decant")) {
        const char *a = p + 6; while (*a == ' ') a++;
        cmd_decant(a);
    }
    else if (str_startswith(p, "relabel")) {
        const char *a = p + 7; while (*a == ' ') a++;
        cmd_relabel(a);
    }
    else if (str_startswith(p, "jot")) {
        const char *a = p + 3; while (*a == ' ') a++;
        cmd_jot(a);
    }
    else if (str_startswith(p, "ai")) {
        const char *a = p + 2; while (*a == ' ') a++;
        cmd_ai(a);
    }
    else if (str_startswith(p, "cook")) {
        const char *a = p + 4; while (*a == ' ') a++;
        cmd_cook(a);
    }
    else if (str_eq(p, "pwd"))       cmd_pwd();
    else if (str_eq(p, "bowltest"))  cmd_bowltest();
    else if (str_startswith(p, "mkbowl")) {
        const char *a = p + 6; while (*a == ' ') a++;
        cmd_mkbowl(a);
    }
    else if (str_startswith(p, "rmbowl")) {
        const char *a = p + 6; while (*a == ' ') a++;
        cmd_rmbowl(a);
    }
    else if (str_startswith(p, "cd")) {
        const char *a = p + 2; while (*a == ' ') a++;
        cmd_cd(a);
    }
    else if (str_eq(p, "preempttest")) cmd_preempttest();
    else if (str_eq(p, "fatstress"))  cmd_fatstress();
    else if (str_eq(p, "vgastress"))  cmd_vgastress();
    else if (str_eq(p, "vmtest"))     cmd_vmtest();
    else if (str_eq(p, "whoami"))    cmd_whoami();
#ifndef NO_CHALLENGE
    else if (str_eq(p, "special"))   challenge_special();
#endif
    else if (str_eq(p, "roster"))    cmd_roster();
    else if (str_eq(p, "clockout"))  cmd_clockout();
    else if (str_startswith(p, "hire")) {
        const char *a = p + 4; while (*a == ' ') a++;
        cmd_hire(a);
    }
    else if (str_startswith(p, "fire")) {
        const char *a = p + 4; while (*a == ' ') a++;
        cmd_fire(a);
    }
    else if (str_startswith(p, "perms")) {
        const char *a = p + 5; while (*a == ' ') a++;
        cmd_perms(a);
    }
    else if (str_startswith(p, "soup")) {
        const char *a = p + 4; while (*a == ' ') a++;
        cmd_soup(a);
    }
    else if (str_startswith(p, "hash")) {
        const char *a = p + 4; while (*a == ' ') a++;
        cmd_hash(a);
    }
    else if (str_startswith(p, "beep")) {
        const char *a = p + 4; while (*a == ' ') a++;
        cmd_beep(a);
    }
    else if (str_eq(p, "mandelbrot")) cmd_mandelbrot();
    else if (str_eq(p, "vgademo"))   cmd_vgademo();
    else if (str_startswith(p, "uname")) {
        const char *a = p + 5; while (*a == ' ') a++;
        cmd_uname(a);
    }
    else if (str_eq(p, "cal"))       cmd_cal();
    else if (str_eq(p, "clock"))     cmd_clock();
    else if (str_eq(p, "wipe"))      cmd_wipe();
    else if (str_eq(p, "fortune"))   cmd_fortune();
    else if (str_eq(p, "bounce"))    cmd_bounce();
#ifndef NO_DOOM
    else if (str_eq(p, "doom"))      cmd_doom();
#endif
    else if (str_eq(p, "cat"))       cmd_cat();  /* hidden */
    else if (str_eq(p, "ps"))          cmd_ps();
    else if (str_eq(p, "bgclock"))     cmd_bgclock();
    else if (str_eq(p, "yieldbg"))     cmd_yieldbg();
    else if (str_eq(p, "spinbg"))      cmd_spinbg();
    else if (str_eq(p, "sleeptest"))   cmd_sleeptest();
    else if (str_eq(p, "vfstest"))     cmd_vfstest();
    else if (str_eq(p, "dmesg"))       cmd_dmesg();
    else if (str_startswith(p, "kill")) {
        const char *a = p + 4; while (*a == ' ') a++;
        cmd_kill(a);
    }
    else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_printf("No soup for you: '%s'\n", p);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

/* ---- main shell loop ---- */

void shell_run(void) {
    char input[INPUT_MAX];
    char saved[INPUT_MAX];
    int  pos = 0, len = 0;
    int  hist_browse = 0;

    input[0] = saved[0] = '\0';
    do_login();                 /* clock in before the kitchen opens */
    prompt();

    while (1) {
        int c = keyboard_getchar();

        if (c == '\n') {
            cursor_to(len);
            vga_putchar('\n');
            input[len] = '\0';
            hist_push(input);
            dispatch(input);
            pos = len = hist_browse = 0;
            input[0] = saved[0] = '\0';
            prompt();

        } else if (c == '\b') {
            shell_backspace(input, &pos, &len);

        } else if (c == KEY_DEL) {
            shell_delkey(input, &pos, &len);

        } else if (c == KEY_LEFT) {
            if (pos > 0) { pos--; cursor_to(pos); }

        } else if (c == KEY_RIGHT) {
            if (pos < len) { pos++; cursor_to(pos); }

        } else if (c == KEY_HOME || c == 0x01) {  /* Ctrl+A */
            pos = 0; cursor_to(0);

        } else if (c == KEY_END || c == 0x05) {   /* Ctrl+E */
            pos = len; cursor_to(len);

        } else if (c == 0x03) {   /* Ctrl+C - cancel current line */
            cursor_to(len);
            vga_set_color(VGA_DARK_GREY, VGA_BLACK);
            vga_puts("^C\n");
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            pos = len = hist_browse = 0;
            input[0] = saved[0] = '\0';
            prompt();

        } else if (c == 0x15) {   /* Ctrl+U - clear from cursor to start */
            shell_kill_to_start(input, &pos, &len);

        } else if (c == 0x17) {   /* Ctrl+W - delete word backwards */
            shell_kill_word(input, &pos, &len);

        } else if (c == '\t') {
            shell_tab_complete(input, &pos, &len);

        } else if (c == KEY_UP) {
            if (hist_browse == 0) strncpy(saved, input, INPUT_MAX);
            if (hist_browse < hist_size) {
                hist_browse++;
                const char *h = hist_get(hist_browse);
                if (h) line_replace(input, &pos, &len, h);
            }

        } else if (c == KEY_DOWN) {
            if (hist_browse > 0) {
                hist_browse--;
                const char *h = (hist_browse == 0) ? saved : hist_get(hist_browse);
                if (h) line_replace(input, &pos, &len, h);
            }

        } else if (c >= ' ' && c < 127) {
            shell_insert(input, &pos, &len, (char)c);
        }
    }
}
