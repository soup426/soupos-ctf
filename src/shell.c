#include "shell.h"
#include "vga.h"
#include "term.h"
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
#include "proc.h"
#include "clip.h"
#include "net.h"
#include "rtl8139.h"
#include "tcp.h"
#include "selftest.h"
#include "mouse.h"
#include "ac97.h"
#include "mixer.h"
#include "remote.h"
#include "ssh.h"
#include "logins.h"
#include "swap.h"
#ifndef NO_DOOM
#include "music.h"
#endif
#ifndef NO_DOOM
#include "doomsnd.h"
#include "wad.h"
#endif
#include "fb.h"
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
#define VARS_MAX     24          /* was 16; PWD and OLDPWD take two now (v0.60.32) */

/* Everything that belongs to ONE shell rather than to the kernel: where it
 * is, what it typed before, where its prompt sits. Fourth queue, item 2a:
 * grouped here so a second shell (an SSH session's own) can have its own,
 * reached through cur_shell(). Today there is one. */
typedef struct {
    char cwd[FAT_PATH_MAX];
    char hist[HIST_MAX][INPUT_MAX];
    int  hist_head;              /* ring-buffer write position           */
    int  hist_size;              /* entries stored (0..HIST_MAX)          */
    int  prompt_row;             /* VGA row where the current prompt was printed */
    int  prompt_len;             /* width of the printed prompt (includes cwd)   */
    int  page_rows;              /* rows per page for the pager              */
    term_t *term;                /* where this shell reads and writes (v0.35.2) */
    /* v0.36.0: a shell of its own for every SSH session. */
    uint8_t uid;                 /* who is logged in to THIS shell               */
    int     last_status;         /* of the last command line: exec's exit status */
    int     exiting;             /* clockout in a session: leave the loop        */
    volatile int done;           /* the session task has finished                */
    char    exec_line[INPUT_MAX];/* a session that runs one line (ssh host cmd)  */
    char    where[20];           /* "console", or the client's address (v0.52.0) */
    char    vname[VARS_MAX][16]; /* shell variables (v0.58.0): NAME=value, $NAME */
    char    vval[VARS_MAX][96];
    uint8_t vhand[VARS_MAX];     /* handed to programs (`hand`, v0.60.26) */
    char    vpend[8][16];        /* handed while unset: handed once set    */
    int     nvars, npend;
    char    parg[9][64];         /* a script's $1..$9 (v0.60.2)            */
    int     npargs;              /* $#                                      */
    uint32_t started, last_input;/* ticks: for `who`                          */
    int     tabs;                /* Tabs in a row: the second lists (v0.60.10) */
    vfs_node_t *in_pipe;         /* `prog | while take ...`: take reads this (v0.60.34) */
    int     loops;               /* loops open now, for break and continue (v0.60.44) */
    int     brk, cont;           /* loops still to break out of / continue past */
    char    in_buf[512];
    int     in_pos, in_len;
    char    fname[16][16];       /* functions (v0.60.22): NAME() { BODY } */
    char    fbody[16][INPUT_MAX];
    int     nfuncs;
} shell_t;

/* The console's shell. A session's shell_t is kmalloc'd by
 * shell_session_start, and every task that belongs to it (the session task,
 * and through inheritance every program and script it starts) points at it,
 * so cur_shell() answers for whoever is asking. */
static shell_t the_shell = { .cwd = "/", .prompt_len = 8, .term = &term_vga, .where = "console" };

/* Every live shell, for `who`: the console's always, and each session's
 * from its start to its end (v0.52.0). */
static shell_t *shells[8] = { &the_shell };
static void shell_register(shell_t *sh, int on) {
    preempt_disable();
    for (int i = 1; i < 8; i++) {
        if (on && !shells[i])      { shells[i] = sh; break; }
        if (!on && shells[i] == sh) { shells[i] = 0; break; }
    }
    preempt_enable();
}
static inline shell_t *cur_shell(void) {
    task_t *me = task_current();
    return (me && me->shell) ? (shell_t *)me->shell : &the_shell;
}

/* Every console call the shell makes goes through its terminal. The local
 * console is term_vga, which forwards to vga_* and keyboard_*, so nothing
 * changes for it; a shell on another terminal (stage c) changes nothing here.
 * Left on vga_* deliberately: the screen-only features that poke cells or
 * the serial mirror directly (bgclock, the boot logo, kitchen's mirror). */
#define T (cur_shell()->term)
static int subst_depth;          /* inside a $(...) capture (v0.60.3) */
static inline void t_puts(const char *s)       { T->puts(T, s); }
static inline void t_putc(char c)              { T->putc(T, c); }
static inline void t_color(int fg, int bg)     { T->set_color(T, fg, bg); }
static inline void t_cursor(int row, int col)  { T->set_cursor(T, row, col); }
static inline int  t_row(void)                 { return T->row(T); }
static inline int  t_col(void)                 { return T->col(T); }
static inline int  t_cols(void)                { return T->cols(T); }
static inline int  t_rows(void)                { return T->rows(T); }
static inline void t_clear(void)               { T->clear(T); }
static inline int  t_getc(void)                { return T->getc(T); }
static inline int  t_avail(void)               { return T->available(T); }
static inline void t_flush_in(void)            { T->flush_in(T); }
#define t_printf(...) term_printf(T, __VA_ARGS__)

/* How a process records its terminal: NULL for the console. */
#define TERM_KEY ((void *)(T == &term_vga ? 0 : T))
static inline int on_console(void) { return T == &term_vga; }

/* A command that draws on the physical screen, reads the mouse or takes over
 * the display cannot run in a session on another terminal. */
static int needs_console(const char *what) {
    if (on_console()) return 0;
    t_printf("  %s needs the kitchen's own screen; it cannot run over the network.\n", what);
    cur_shell()->last_status = 1;
    return 1;
}

/* The foreground program of this shell's terminal: where Ctrl-C goes and who
 * may read stdin. The console also keeps proc.c's global, which the keyboard
 * IRQ reads. */
static void shell_set_fg(proc_t *p) {
    if (on_console()) proc_set_foreground(p);
    T->fg = p;
}

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
 * paths, so every file command resolves its argument against `cur_shell()->cwd` first. */

/* Normalise an absolute path: collapse "//", ".", and ".." into a clean
 * "/a/b/c" form (or "/"). `out` must be >= FAT_PATH_MAX bytes. */
static void normalize_path(const char *in, char *out) {
    /* FAT_LFN_MAX, not FAT_NAME_MAX: this is a path component, which can be a
     * long name. Capping at 12 characters here truncated the name before
     * fat_read ever saw it, so a file that LISTED correctly could not be
     * opened - the second place the same mistake was hiding. */
    char comp[FAT_LFN_MAX];
    out[0] = '/';
    out[1] = '\0';
    int len = 1;                        /* out[len] is always the NUL */
    const char *p = in;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        int i = 0;
        while (*p && *p != '/') {
            if (i < FAT_LFN_MAX - 1) comp[i++] = *p;
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

/* Resolve `arg` (absolute or relative to cur_shell()->cwd) into a clean absolute path. */
static void resolve_path(const char *arg, char *out) {
    char joined[FAT_PATH_MAX * 2];
    int j = 0;
    if (arg && arg[0] == '/') {
        joined[j++] = '/';
    } else {
        for (int i = 0; cur_shell()->cwd[i] && j < (int)sizeof(joined) - 2; i++)
            joined[j++] = cur_shell()->cwd[i];
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
        int c = t_getc();
        if (c < 0) break;                          /* the terminal hung up */
        if (c == '\n') { t_putc('\n'); break; }
        if (c == '\b') {
            if (len > 0) { len--; t_putc('\b'); }
            continue;
        }
        if (c >= ' ' && c < 127 && len < max - 1) {
            buf[len++] = (char)c;
            t_putc(mask ? mask : (char)c);
        }
    }
    buf[len] = '\0';
    return len;
}

/* Switch the active session - both the user layer and the FAT owner stamp. */
static void set_session_uid(uint8_t uid) {
    cur_shell()->uid = uid;
    if (cur_shell() == &the_shell) {      /* the console's user is the global one */
        users_set_current(uid);
        fat_set_creator(uid);
    }
}

/* Registered with users.c and fat.c: a session task answers with its own
 * shell's user; the console (and every task with no shell) with -1, meaning
 * the global. */
static int shell_uid_resolver(void) {
    task_t *me = task_current();
    return (me && me->shell) ? ((shell_t *)me->shell)->uid : -1;
}
static uint8_t shell_creator(void) { return users_current_uid(); }

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
static int may(const char *path, char need) { return users_may(path, need); }

/* Print the standard "access denied" line. */
static void deny(const char *path) {
    cur_shell()->last_status = 1;           /* a refusal is a failure (v0.57.0) */
    t_color(VGA_LIGHT_RED, VGA_BLACK);
    t_printf("  Permission denied: %s\n", path);
    t_puts("  (ask the headchef, or retry with: chef <command>)\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* Every ordinary cook has a home bowl, /home/<cook>, theirs and closed to
 * the other cooks, and their shells start in it (v0.52.2). The headchef
 * keeps the root, as root traditionally does. Made at `hire`, or at the
 * first login of a cook hired before homes existed. Built here with the
 * kernel's own hands, not the cook's, since a cook may not write in /home. */
static void ensure_home_inner(uint8_t uid, char *cwd_out);
static void ensure_home(uint8_t uid, char *cwd_out) {
    users_sys_begin();            /* a home may use the disk's reserve (v0.55.5) */
    ensure_home_inner(uid, cwd_out);
    users_sys_end();
}
static void ensure_home_inner(uint8_t uid, char *cwd_out) {
    if (uid == 0) { strcpy(cwd_out, "/"); return; }
    const char *name = users_name_of(uid);
    char home[FAT_PATH_MAX];
    strcpy(home, "/home/");
    strncpy(home + 6, name, sizeof(home) - 7); home[sizeof(home) - 1] = '\0';
    if (!fat_is_dir("/home")) {
        fat_mkdir("/home");
        fat_chown("/home", 0);
        fat_chmod("/home", FAT_PERM_OR | FAT_PERM_OW | FAT_PERM_OX | FAT_PERM_AR | FAT_PERM_AX);
    }
    if (!fat_is_dir(home)) {
        fat_mkdir(home);
        fat_chown(home, uid);
        fat_chmod(home, FAT_PERM_OR | FAT_PERM_OW | FAT_PERM_OX);      /* rwx--- */
        klog("[users] made %s for %s\n", home, name);
    }
    uint8_t owner, mode;
    if (fat_is_dir(home) && fat_stat(home, &owner, &mode) == 0 && owner != uid) {
        /* A fired cook's home, handed to the headchef: not this cook's to
         * inherit just because the name matches (v0.53.0). */
        klog("[users] %s belongs to uid %u, not %s; starting at /\n", home, owner, name);
        strcpy(cwd_out, "/");
        return;
    }
    strcpy(cwd_out, fat_is_dir(home) ? home : "/");
}

/* Login gate - loops until a valid cook clocks in. Used at boot and by
 * the `clockout` command. */
static volatile int console_at_login;   /* the console is in do_login (v0.55.1) */
static volatile int console_hangup;     /* clock the console out at the next key */
int  shell_console_at_login(void) { return console_at_login; }
void shell_console_hangup(void)   { console_hangup = 1; }

static void ip_dotted(uint32_t a, char *out) {          /* out: 16 bytes */
    int k = 0;
    for (int part = 3; part >= 0; part--) {
        uint32_t v = (a >> (part * 8)) & 255;
        if (v >= 100) out[k++] = (char)('0' + v / 100);
        if (v >= 10)  out[k++] = (char)('0' + (v / 10) % 10);
        out[k++] = (char)('0' + v % 10);
        if (part) out[k++] = '.';
    }
    out[k] = '\0';
}

/* /etc/motd, if there is one, after every interactive login: console, pass
 * and vault (v0.55.6). The headchef's to write (/etc is closed to cooks and
 * users_seal_system pins it rw-r--). Control characters other than newline
 * and tab are dropped, so the file cannot send a terminal escape sequences;
 * at most 2 KB is shown. */
static void show_motd(void) {
    static char motd[2049];
    uint32_t got = 0;
    if (fat_read("/etc/motd", (uint8_t *)motd, sizeof(motd) - 1, &got) < 0 || got == 0) return;
    if (got > sizeof(motd) - 1) got = sizeof(motd) - 1;     /* fat_read reports the file's size */
    t_color(VGA_YELLOW, VGA_BLACK);
    int at_start = 1;
    for (uint32_t i = 0; i < got; i++) {
        char c = motd[i];
        if (c == '\r') continue;
        if (c != '\n' && c != '\t' && (c < ' ' || c == 127)) continue;
        if (at_start && c != '\n') { t_puts("  "); at_start = 0; }
        t_putc(c);
        if (c == '\n') at_start = 1;
    }
    if (!at_start) t_putc('\n');
    t_putc('\n');
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* ~/profile (v0.60.5): if the cook's home holds a file named profile, it is
 * run - as `run` runs a script, with the cook's permissions - after every
 * interactive login (console, pass, vault; not `ssh host cmd`). Its
 * variables stay for the session; an error in it is said and the login
 * goes on. The headchef's home is /, so theirs is /profile. */
static void run_line(const char *line);
static void run_profile(void) {
    char path[FAT_PATH_MAX];
    shell_t *me = cur_shell();
    if (me->uid == 0) strcpy(path, "/profile");
    else { strcpy(path, "/home/"); strncpy(path + 6, users_name_of(me->uid), sizeof(path) - 15); path[sizeof(path) - 9] = '\0'; strcat(path, "/profile"); }
    if (!fat_exists(path) || fat_is_dir(path)) return;
    char line[FAT_PATH_MAX + 8];
    strcpy(line, "follow \"");
    strcat(line, path);
    strcat(line, "\"");
    run_line(line);
}

static void hist_load(void);     /* below, with the history */
static void set_var_quiet(const char *name, const char *value);   /* with cd (v0.60.32) */
static void do_login(void) {
    char name[USER_NAME_MAX];
    char secret[64];
    int  pass_fails = 0;
    if (cur_shell() == &the_shell) console_at_login = 1;
    for (;;) {
        t_color(VGA_LIGHT_CYAN, VGA_BLACK);
        t_puts("\n  soupOS kitchen -- clock in to start your shift.\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        t_puts("  cook:   ");
        read_line(name, sizeof(name), 0);
        if (!name[0]) continue;      /* a bare Enter (the pass sends one on arrival) asks again */
        t_puts("  secret: ");
        read_line(secret, sizeof(secret), '*');

        int uid = users_check(name, secret);
        /* Over the pass the "console" is a network caller: record their
         * address, and make guessing cost time as the vault does (v0.55.1;
         * measured on v0.55.0: ten wrong passwords in 8 s, the client's own
         * pace). */
        uint32_t caller = remote_peer();
        char where[16];
        if (caller) ip_dotted(caller, where); else strcpy(where, "console");
        if (uid >= 0) {
            set_session_uid((uint8_t)uid);
            ensure_home((uint8_t)uid, cur_shell()->cwd);
            set_var_quiet("PWD", cur_shell()->cwd);     /* v0.60.32 */
            hist_load();
            logins_record(users_current_name(), where, 1);
            console_at_login = 0;
            t_color(VGA_LIGHT_GREEN, VGA_BLACK);
            t_printf("\n  Welcome to the kitchen, %s.%s\n\n",
                       users_current_name(),
                       users_is_headchef() ? " (headchef -- run of the kitchen)" : "");
            t_color(VGA_LIGHT_GREY, VGA_BLACK);
            show_motd();
            run_profile();
            return;
        }
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_puts("  The kitchen door stays locked -- wrong cook or secret.\n");
        logins_record(name, where, 0);
        if (caller) {
            uint32_t pause = 1000u << (pass_fails < 3 ? pass_fails : 3);
            pass_fails++;
            klog("[pass] wrong secret from %s: %u ms pause\n", where, pause);
            task_sleep(pause);
        }
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

/* ---- command history ---- */

static const char *hist_get(int offset);

static void hist_add(const char *cmd) {
    strncpy(cur_shell()->hist[cur_shell()->hist_head % HIST_MAX], cmd, INPUT_MAX - 1);
    cur_shell()->hist[cur_shell()->hist_head % HIST_MAX][INPUT_MAX - 1] = '\0';
    cur_shell()->hist_head++;
    if (cur_shell()->hist_size < HIST_MAX) cur_shell()->hist_size++;
}

/* A cook's leftovers outlive the shift (v0.60.17). The shift's end writes
 * the cook's file, oldest line first: clockout, the end of an SSH session,
 * and reheat or freeze for the cook who gives it. Not every line: a file
 * that changed under every command would change under `portions` and
 * `peek -l` as they measured it (and bash, too, writes at the end). A
 * power cut loses the shift in progress. Logging in loads the file.
 * The headchef's home is the root, where every cook looks, so theirs is
 * /etc/LEFTOVER; anyone else's is LEFTOVER in their home, and only while
 * that home is still theirs. Owner read and write only.
 * Not in the CTF build: its ring-3 open is unchecked on purpose, and every
 * cook's history would become something to read. */
#ifdef NO_CHALLENGE
static int hist_file(char *out) {
    uint8_t uid = cur_shell()->uid, owner, mode;
    if (uid == 0) { strcpy(out, "/etc/LEFTOVER"); return fat_is_dir("/etc"); }
    char home[FAT_PATH_MAX];
    strcpy(home, "/home/");
    strncpy(home + 6, users_name_of(uid), sizeof(home) - 7); home[sizeof(home) - 1] = '\0';
    if (!fat_is_dir(home) || fat_stat(home, &owner, &mode) < 0 || owner != uid) return 0;
    strcpy(out, home); strcat(out, "/LEFTOVER");
    return 1;
}

static void hist_save(void) {
    char path[FAT_PATH_MAX];
    if (!hist_file(path)) return;
    shell_t *sh = cur_shell();
    static char buf[HIST_MAX * INPUT_MAX];
    uint32_t n = 0;
    for (int i = sh->hist_size; i >= 1; i--) {          /* oldest first */
        const char *h = hist_get(i);
        for (int k = 0; h && h[k] && n < sizeof(buf) - 1; k++) buf[n++] = h[k];
        if (n < sizeof(buf)) buf[n++] = '\n';
    }
    int fresh = !fat_exists(path);
    if (fat_write(path, (const uint8_t *)buf, n) < 0) return;    /* a full disk keeps the old one */
    if (fresh) { fat_chown(path, sh->uid); fat_chmod(path, FAT_PERM_MARK | FAT_PERM_OR | FAT_PERM_OW); }
}

static void hist_load(void) {
    char path[FAT_PATH_MAX];
    if (!hist_file(path) || !fat_exists(path)) return;
    static char buf[HIST_MAX * INPUT_MAX];
    uint32_t n = 0;
    if (fat_read(path, (uint8_t *)buf, sizeof(buf) - 1, &n) < 0) return;
    char line[INPUT_MAX];
    int l = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (buf[i] == '\n') { line[l] = '\0'; if (l) hist_add(line); l = 0; }
        else if (l < INPUT_MAX - 1 && buf[i] >= ' ' && buf[i] < 127) line[l++] = buf[i];
    }
}
#else
static void hist_save(void) {}
static void hist_load(void) {}
#endif

static void hist_push(const char *cmd) {
    if (!cmd || !cmd[0]) return;
    hist_add(cmd);
}

/* offset=1 -> most recent, offset=2 -> one before that, etc. */
static const char *hist_get(int offset) {
    if (offset <= 0 || offset > cur_shell()->hist_size) return (void *)0;
    int idx = ((cur_shell()->hist_head - offset) % HIST_MAX + HIST_MAX) % HIST_MAX;
    return cur_shell()->hist[idx];
}

/* ---- Line editing with proper cursor positioning ---- */
/* The console's real width, which is not 80 on a framebuffer boot: the line
 * editor wraps on it, so a stale 80 would wrap in the wrong place. */
#define VGA_W      (t_cols())


/* Move the hardware cursor to buffer position buf_pos */
static void cursor_to(int buf_pos) {
    int abs = cur_shell()->prompt_len + buf_pos;
    t_cursor(cur_shell()->prompt_row + abs / VGA_W, abs % VGA_W);
}

/* Reprint input[from..len-1], then optionally one trailing space.
   Does NOT reposition the cursor afterwards - caller must call cursor_to(). */
static void redraw_tail(const char *input, int from, int len, int trail_space) {
    cursor_to(from);
    for (int i = from; i < len; i++) t_putc(input[i]);
    if (trail_space) t_putc(' ');
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
    clip_set(input, (uint32_t)n);     /* killed text is the clipboard, so ^V restores it */
    for (int i = 0; i + n < *len; i++) input[i] = input[i + n];
    *len -= n;
    *pos = 0;
    redraw_tail(input, 0, *len, 0);
    /* Erase the n trailing chars left over from the old longer line */
    for (int i = 0; i < n; i++) t_putc(' ');
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
    clip_set(input + start, (uint32_t)n);
    for (int i = start; i + n < *len; i++) input[i] = input[i + n];
    *len -= n;
    *pos = start;
    redraw_tail(input, start, *len, 0);
    for (int i = 0; i < n; i++) t_putc(' ');
    cursor_to(*pos);
}

/* Replace the whole input line with new_str (used by history navigation) */
static void line_replace(char *input, int *pos, int *len, const char *new_str) {
    int old_len = *len;
    cursor_to(0);
    *len = 0;
    while (new_str[*len] && *len < INPUT_MAX - 1) {
        t_putc(new_str[*len]);
        input[*len] = new_str[*len];
        (*len)++;
    }
    input[*len] = '\0';
    /* Erase any leftover characters from the old longer line */
    for (int i = *len; i < old_len; i++) t_putc(' ');
    *pos = *len;
    cursor_to(*pos);
}

/* Ctrl+R: search the leftovers, newest first (v0.60.18). The line becomes
 *   (leftovers)`query': the newest line containing the query
 * Typing narrows it, Ctrl+R steps to an older match, Backspace widens it
 * again. Enter runs the match (returns 1, with it in input); Ctrl+C or
 * Ctrl+G gives back the line as it was; any other key (an arrow, Home, End,
 * Esc) keeps the match on the prompt to edit. "(no leftovers)" says the
 * query matches nothing, and the last match stays shown. */
static int hist_find(const char *q, int ql, int from, const char *skip) {
    for (int i = from; i <= cur_shell()->hist_size; i++) {
        const char *h = hist_get(i);
        if (!h || (skip && str_eq(h, skip))) continue;
        for (int k = 0; h[k]; k++)
            if (strncmp(h + k, q, (size_t)ql) == 0) return i;
    }
    return 0;
}

static void prompt(void);

/* Back to an ordinary prompt on the same row, showing input[0..len). */
static void search_leave(char *input, int *pos, int *len, int drawn) {
    t_cursor(cur_shell()->prompt_row, 0);
    prompt();
    for (int i = 0; i < *len; i++) t_putc(input[i]);
    for (int i = cur_shell()->prompt_len + *len; i < drawn; i++) t_putc(' ');
    cursor_to(*pos);
}

static int shell_search(char *input, int *pos, int *len) {
    shell_t *sh = cur_shell();
    char orig[INPUT_MAX];
    int olen = *len, opos = *pos;
    memcpy(orig, input, (size_t)olen);
    char q[48];
    int ql = 0, at = 0, failed = 0, drawn = 0;
    cursor_to(*len);
    t_putc('\n');
    sh->prompt_row = t_row();
    *len = *pos = 0;
    for (;;) {
        /* Draw: the label, then the match as the editable line. */
        const char *label = failed ? "(no leftovers)`" : "(leftovers)`";
        t_cursor(sh->prompt_row, 0);
        t_color(VGA_DARK_GREY, VGA_BLACK); t_puts(label);
        t_color(VGA_WHITE, VGA_BLACK);
        for (int i = 0; i < ql; i++) t_putc(q[i]);
        t_color(VGA_DARK_GREY, VGA_BLACK); t_puts("': ");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        sh->prompt_len = (int)strlen(label) + ql + 3;
        for (int i = 0; i < *len; i++) t_putc(input[i]);
        int now = sh->prompt_len + *len;
        for (int i = now; i < drawn; i++) t_putc(' ');
        drawn = now;
        cursor_to(*pos);

        int c = t_getc();
        if (c < 0) return 0;
        int found = -1;                  /* -1: nothing to look for this key */
        if (c == '\n') return *len > 0;
        if (c == 0x03 || c == 0x07) {    /* Ctrl+C, Ctrl+G: the line as it was */
            memcpy(input, orig, (size_t)olen);
            *len = olen; *pos = opos;
            search_leave(input, pos, len, drawn);
            return 0;
        }
        if (c == 0x12) {                 /* Ctrl+R: an older one */
            if (ql) { input[*len] = '\0'; found = hist_find(q, ql, at + 1, input); }
        } else if (c == '\b') {
            if (ql) { ql--; found = ql ? hist_find(q, ql, 1, 0) : 0; if (!ql) { *len = *pos = 0; at = 0; failed = 0; } }
        } else if (c >= ' ' && c < 127) {
            if (ql < (int)sizeof(q)) { q[ql++] = (char)c; found = hist_find(q, ql, at ? at : 1, 0); }
        } else {                         /* anything else: edit the match */
            *pos = *len;
            search_leave(input, pos, len, drawn);
            return 0;
        }
        if (found > 0) {
            const char *h = hist_get(found);
            at = found; failed = 0;
            *len = 0;
            while (h[*len] && *len < INPUT_MAX - 1) { input[*len] = h[*len]; (*len)++; }
            input[*len] = '\0';
            *pos = 0;
            for (int i = 0; i + ql <= *len; i++)             /* the cursor on the match, as bash */
                if (strncmp(input + i, q, (size_t)ql) == 0) { *pos = i; break; }
        } else if (found == 0 && ql) {
            failed = 1;
        }
    }
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
static void page_begin(void) { cur_shell()->page_rows = 0; }
static int page_line(int header, const char *text) {
    t_color(header ? VGA_YELLOW : VGA_LIGHT_GREY, VGA_BLACK);
    t_puts(text);
    t_putc('\n');
    if (++cur_shell()->page_rows < PAGE_ROWS) return 1;
    t_color(VGA_DARK_GREY, VGA_BLACK);
    t_puts("  -- more -- (q to stop, any other key for next page)");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    int c = t_getc();
    if (c < 0 || c == 'q' || c == 'Q' || c == 27) { t_putc('\n'); return 0; }
    t_clear();
    cur_shell()->page_rows = 0;
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
        {0, "    cd <bowl> / cd -   - change bowl; - goes back ($PWD, $OLDPWD)"},
        {0, "    mkbowl <name>      - make a new bowl (directory)"},
        {0, "    rmbowl <name>      - remove an empty bowl"},
        {0, "    bowltest           - self-test the bowl/filesystem layer"},
        {1, "  The Kitchen Staff:"},
        {0, "    whoami             - show the current cook"},
        {0, "    roster             - list every cook in the kitchen"},
        {0, "    perms <file> [spec]- show / set file permissions"},
        {0, "    chef <command>     - run one command as the headchef"},
        {0, "    hire <name>        - headchef: add a cook"},
        {0, "    secret [cook]      - change your secret (headchef: anyone's)"},
        {0, "    fire <name>        - headchef: remove a cook"},
        {0, "    clockout           - log out, return to the login prompt"},
        {0, "    ai <prompt>        - ask the host LLM (needs: make run-ai)"},
        {0, "    cook <prog.elf>    - run a ring-3 user-mode program"},
        {0, "    cook <prog.elf> &  - ... in the background (Ctrl+C kills the"},
        {0, "                         foreground one; kill <pid> any of them)"},
        {0, "    jobs               - list processes and their exit codes"},
        {0, "    clip               - show the clipboard (^U/^W fill it, ^V pastes)"},
        {1, "  Network"},
        {0, "    netinfo            - card, addresses and frame counters"},
        {0, "    ifconfig [ip]      - show or set this machine's address"},
        {0, "    arp                - the ARP cache"},
        {0, "    ping <host> [count]- ICMP echo (name or address)"},
        {0, "    nslookup <host>    - resolve a name over DNS"},
        {0, "    fg [pid] / bg [pid]- resume a job in the fore/background"},
        {0, "                         (Ctrl+Z stops the foreground program)"},
        {0, "    soup <file.sc>     - run a soupyc script"},
        {0, "    soup -c \"code\"     - run inline soupyc"},
        {0, "      spawn(\"fn\")      - in soupyc: run fn() as a background task"},
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
        {0, "    kitchen            - the whole room, live, until a key"},
        {0, "    brigade            - every shell: its cook, where, up, idle"},
        {0, "    logbook            - who clocked in, and who was refused, newest first"},
        {0, "    portions [bowl]    - clusters and bytes under each entry, and the total"},
        {0, "    follow <file> [..] - follow a recipe (a script); $1..$9, $#, $@ are its args"},
        {0, "    NAME=value, $NAME  - shell variables; discard NAME removes one"},
        {0, "    take NAME...       - take a line from the terminal into variables"},
        {0, "    hand NAME[=value]  - hand a variable to programs (their getenv)"},
        {0, "    kill <id>          - stop a background task"},
        {0, "    bgclock            - spawn a clock in the top-right corner"},
        {0, "    yieldbg            - bg task logging [bg] to serial (pairs with cook whisk.elf)"},
        {0, "    spinbg             - CPU-bound bg task that never yields (preemption demo)"},
        {0, "    sleeptest          - test task_sleep and wait-queue (500 ms sleep)"},
        {0, "    vfstest            - test the VFS shim (/dev nodes + file round-trip)"},
        {0, "    dmesg              - dump the kernel log ring buffer"},
        {0, "  (up/down arrows, Ctrl+R searches, Tab finishes words)"},
        {0, "  (Ctrl+A/E home/end, Ctrl+U clear line, Ctrl+W kill word, Ctrl+C cancel)"},
    };
    page_begin();
    for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
        if (!page_line(lines[i].hdr, lines[i].text)) break;
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
}


/* Quotes for builtins (v0.57.4), with the rules programs get from uargv:
 * outside '...' and "..." a space ends a word; inside them everything is
 * kept and the quotes are dropped. */
/* One word: skip spaces, read a word (quotes dropped) into out; returns
 * where the rest of the line starts. */
static const char *first_word(const char *s, char *out, int max) {
    while (*s == ' ') s++;
    int n = 0; char q = 0;
    while (*s && (q || *s != ' ')) {
        if (q) { if (*s == q) q = 0; else if (n < max - 1) out[n++] = *s; }
        else if (*s == '\'' || *s == '"') q = *s;
        else if (n < max - 1) out[n++] = *s;
        s++;
    }
    out[n] = '\0';
    while (*s == ' ') s++;
    return s;
}
/* The whole rest of the line, quotes dropped and spaces kept, for the
 * builtins that take one path or a text (pour "my file", slurp 'a  b'). */
static const char *uq_args(const char *raw) {
    static char out[INPUT_MAX];
    int n = 0; char q = 0;
    while (*raw == ' ') raw++;
    for (; *raw && n < INPUT_MAX - 1; raw++) {
        if (q) { if (*raw == q) q = 0; else out[n++] = *raw; }
        else if (*raw == '\'' || *raw == '"') q = *raw;
        else out[n++] = *raw;
    }
    while (n > 0 && out[n - 1] == ' ' && !q) n--;
    out[n] = '\0';
    return out;
}
/* slurp - echo (v0.60.38: as echo, word by word). The words are split at
 * blanks outside quotes and printed with one space between, so `slurp a
 * b` and `slurp $v` (v holding "x   y") print "a b" and "x y", as echo does;
 * quoted stretches keep their spaces, and '' is a word of its own. It
 * printed the rest of the line as typed before. */
static void cmd_slurp(const char *args) {
    int words = 0;
    const char *r = args;
    for (;;) {
        while (*r == ' ' || *r == '\t') r++;
        if (!*r) break;
        if (words++) t_putc(' ');
        char q = 0;
        while (*r && (q || (*r != ' ' && *r != '\t'))) {
            if (q) { if (*r == q) q = 0; else t_putc(*r); }
            else if (*r == '\'' || *r == '"') q = *r;
            else t_putc(*r);
            r++;
        }
    }
    t_putc('\n');
}

static void cmd_season(const char *args) {
    int fg = parse_num(args);
    while (*args && *args != ' ') args++;
    while (*args == ' ') args++;
    int bg = parse_num(args);
    if (fg < 0 || fg > 15 || bg < 0 || bg > 15) {
        t_puts("Usage: season <fg 0-15> <bg 0-15>\n"); return;
    }
    t_color((vga_color_t)fg, (vga_color_t)bg);
    t_printf("Seasoned: fg=%d bg=%d\n", fg, bg);
}

static void cmd_simmer(void) {
    uint32_t s = timer_get_seconds();
    t_printf("Simmering for: %u:%02u:%02u\n", s / 3600, (s % 3600) / 60, s % 60);
}

static void cmd_broth(void) {
    uint32_t total = pmm_total_kb(), used = pmm_used_kb(), fr = pmm_free_kb();
    t_color(VGA_YELLOW, VGA_BLACK); t_puts("  Broth (physical memory):\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    t_printf("    Total:  %u KB (%u MB)\n", total, total / 1024);
    t_printf("    Used:   %u KB (%u MB)\n", used,  used  / 1024);
    t_printf("    Free:   %u KB (%u MB)\n", fr,    fr    / 1024);
    t_printf("    Pages:  %u used / %u total (4 KB each)\n",
               pmm_used_pages(), pmm_total_pages());
}

static void cmd_ladle(void) {
    uint32_t used = heap_used_bytes(), fr = heap_free_bytes(), tot = heap_total_bytes();
    t_color(VGA_YELLOW, VGA_BLACK); t_puts("  Ladle (heap):\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    t_printf("    Total:  %u KB\n", tot / 1024);
    t_printf("    Used:   %u bytes\n", used);
    t_printf("    Free:   %u bytes (%u KB)\n", fr, fr / 1024);
}

static void cmd_expiry(void) {
    rtc_time_t t;
    rtc_read(&t);
    t_printf("  %u-%02u-%02u  %02u:%02u:%02u\n",
               t.year, t.month, t.day, t.hour, t.minute, t.second);
}

static void cmd_chef(void) {
    cpuid_info_t c;
    cpuid_read(&c);
    t_color(VGA_YELLOW, VGA_BLACK); t_puts("  Chef (CPU):\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    t_printf("    Vendor:  %s\n", c.vendor);
    if (c.brand[0])
        t_printf("    Brand:   %s\n", c.brand);
    t_printf("    Family:  %u  Model: %u  Stepping: %u\n",
               c.family, c.model, c.stepping);
    t_printf("    Logical cores: %u\n", c.logical_cores);
    t_puts("    Features:");
    if (c.has_fpu)   t_puts(" FPU");
    if (c.has_apic)  t_puts(" APIC");
    if (c.has_sse)   t_puts(" SSE");
    if (c.has_sse2)  t_puts(" SSE2");
    if (c.has_sse3)  t_puts(" SSE3");
    if (c.has_ssse3) t_puts(" SSSE3");
    if (c.has_sse41) t_puts(" SSE4.1");
    if (c.has_sse42) t_puts(" SSE4.2");
    if (c.has_aes)   t_puts(" AES");
    if (c.has_avx)   t_puts(" AVX");
    t_putc('\n');
}

static void cmd_pantry(void) {
    static pci_dev_t devs[PCI_MAX_DEVS];
    int n = pci_scan(devs, PCI_MAX_DEVS);
    t_color(VGA_YELLOW, VGA_BLACK);
    t_printf("  Pantry (PCI, %d device%s):\n", n, n == 1 ? "" : "s");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int i = 0; i < n; i++) {
        t_printf("  [%02x:%02x.%u] %04x:%04x  %-10s  %s\n",
                   devs[i].bus, devs[i].dev, devs[i].fn,
                   devs[i].vendor_id, devs[i].device_id,
                   pci_vendor_str(devs[i].vendor_id),
                   pci_class_str(devs[i].class_code));
    }
    if (n == 0) t_puts("  (empty pantry - no PCI devices found)\n");
}


/* Left-align a string in a field of `width` characters */
static void print_padded(const char *s, int width) {
    int n = 0;
    while (s[n]) { t_putc(s[n++]); }
    while (n++ < width) t_putc(' ');
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
    if (!may(cur_shell()->cwd, 'r')) { deny(cur_shell()->cwd); return; }

    static fat_entry_t entries[FAT_LS_MAX];
    int n = fat_ls(cur_shell()->cwd, entries, FAT_LS_MAX);
    if (n < 0) {
        t_puts("  No filesystem mounted. (need FAT16/32 disk on primary master)\n");
        return;
    }
    t_color(VGA_YELLOW, VGA_BLACK);
    t_printf("  Serving %s  -  %d item%s  [FAT%d  %s]:\n",
               cur_shell()->cwd, n, n == 1 ? "" : "s", fat_get_type(), fat_label());
    t_color(VGA_DARK_GREY, VGA_BLACK);
    t_puts("  PERMS   OWNER     NAME             SIZE\n");
    t_puts("  ------------------------------------------------\n");
    for (int i = 0; i < n; i++) {
        char full[FAT_PATH_MAX];
        resolve_path(entries[i].name, full);
        uint8_t owner = 0, mode = FAT_PERM_DEFAULT;
        fat_stat(full, &owner, &mode);
        char ps[7];
        perm_str(mode, ps);

        t_puts("  ");
        t_color(VGA_DARK_GREY, VGA_BLACK);
        t_printf("%s  ", ps);
        print_padded(users_name_of(owner), 10);
        /* The long name if the entry has one, which is what a person typed
         * when they made the file. The 8.3 name still opens it either way.
         *
         * print_padded pads but does not cut, so a name wider than the column
         * runs into the size and the table stops lining up. Cut it here. */
        #define NAME_COL 30
        char shownbuf[NAME_COL + 1];
        const char *full_name = fat_display_name(&entries[i]);
        strncpy(shownbuf, full_name, NAME_COL);
        shownbuf[NAME_COL] = '\0';
        if (strlen(full_name) > NAME_COL) shownbuf[NAME_COL - 1] = '~';
        const char *shown = shownbuf;
        if (entries[i].attr & FAT_ATTR_DIR) {
            t_color(VGA_LIGHT_CYAN, VGA_BLACK);
            print_padded(shown, NAME_COL + 1);
            t_puts("<bowl>\n");
        } else {
            t_color(VGA_LIGHT_GREY, VGA_BLACK);
            print_padded(shown, NAME_COL + 1);
            t_printf("%u B\n", entries[i].size);
        }
    }
    if (n == 0) t_puts("  (empty pot)\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* pour - a file (v0.60.29: cat, as far as anything reading it can tell).
 * The whole file, in 4 KB pieces (it used to stop at 8 KB with a note).
 * Into a pipe or a $(...) it goes exactly as it is; on a terminal (the
 * console, an SSH session) bytes outside printable ASCII, tab, CR and LF
 * are still dropped, so a file cannot send a cook's terminal escape
 * sequences, and a file that does not end in a newline gets one there, so
 * the prompt starts on a line of its own. */
static void cmd_pour(const char *args) {
    if (!args || !args[0]) {
        t_puts("Usage: pour <filename>\n");
        return;
    }
    char path[FAT_PATH_MAX];
    resolve_path(args, path);
    if (!may(path, 'r')) { deny(path); return; }
    vfs_node_t *n = (fat_exists(path) && !fat_is_dir(path)) ? vfs_open(path, VFS_RDONLY) : 0;
    if (!n) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  Not found: %s\n", args);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        cur_shell()->last_status = 1;
        return;
    }
    term_t *t = cur_shell()->term;
    int raw = t && t->name && strcmp(t->name, "subst") == 0;
    static char buf[4096];
    char last = '\n';
    int got;
    while ((got = vfs_read(n, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < got; i++) {
            char c = buf[i];
            if (raw || c == '\n' || c == '\r' || c == '\t' || (c >= 0x20 && c < 0x7F)) t_putc(c);
        }
        last = buf[got - 1];
    }
    vfs_close(n);
    if (!raw && last != '\n') t_putc('\n');
    cur_shell()->last_status = 0;
}

static void cmd_soup(const char *args) {
    if (!args || !args[0]) {
        t_puts("Usage: soup <file.sc>\n");
        t_puts("       soup -c \"code\"\n");
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
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  File not found: %s\n", args);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    /* fat_read fills at most bufsize but reports the FILE's size: clamp, or a big file writes past fbuf. */
    if (fsize > sizeof(fbuf) - 1) {
        /* fat_read reports the file's size, which can exceed the buffer:
         * running the first 8 KB of a longer script would be worse than
         * refusing it. */
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  %s is too large to run (%u bytes; a script may be %u)\n",
                 args, fsize, (uint32_t)(sizeof(fbuf) - 1));
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    fbuf[fsize] = '\0';
    soupyc_run((const char *)fbuf);
}

static void cmd_stir(const char *args) {
    if (!args || !args[0]) {
        t_puts("Usage: stir <file> <content>\n");
        return;
    }
    /* Split "filename rest of line"; either may be quoted (v0.57.4). */
    char arg_name[FAT_PATH_MAX];
    const char *p = first_word(args, arg_name, FAT_PATH_MAX);
    p = uq_args(p);
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
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_puts("  Write failed (no disk, FAT32 volume, or missing bowl)\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
    } else {
        t_printf("  Stirred: %s (%u bytes)\n", path, clen);
    }
}

static void cmd_strain(const char *args) {
    if (!args || !args[0]) {
        t_puts("Usage: strain <file>\n");
        return;
    }
    char path[FAT_PATH_MAX];
    resolve_path(args, path);
    if (!may(path, 'w')) { deny(path); return; }
    if (fat_delete(path) < 0) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  Cannot strain: %s\n", args);
        t_puts("  (not found, or it is a bowl - use rmbowl)\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
    } else {
        t_printf("  Strained out: %s\n", path);
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
    args = first_word(args, first, FAT_PATH_MAX);       /* quotes allowed (v0.57.4) */
    first_word(args, second, FAT_PATH_MAX);
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
    t_color(VGA_LIGHT_RED, VGA_BLACK);
    if      (rc == -1) t_puts("  Could not read the source file.\n");
    else if (rc == -2) t_printf("  Too big: limit is %u bytes.\n",
                                  (uint32_t)COPY_BUF_MAX);
    else               t_puts("  Write failed (no disk, FAT32 volume, "
                                "missing bowl, or disk full).\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* Resolve + validate a two-file operation. Fills src/dst with clean absolute
 * paths and checks permissions. `need_w_src` also requires write on the
 * source (for relabel, which deletes it). -1 on any error (message printed). */
static int prep_file_op(const char *args, const char *usage,
                        char *src, char *dst, int need_w_src) {
    char a1[FAT_PATH_MAX], a2[FAT_PATH_MAX];
    if (split_two(args, a1, a2) < 0) { t_printf("Usage: %s\n", usage); return -1; }

    char dstarg[FAT_PATH_MAX];
    resolve_path(a1, src);
    resolve_path(a2, dstarg);

    if (!fat_exists(src) || fat_is_dir(src)) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  Not a file: %s\n", a1);
        t_puts("  (these commands move files, not bowls)\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return -1;
    }
    copy_dest(src, dstarg, dst);
    if (fat_is_dir(dst)) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  Destination is a bowl: %s\n", dst);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return -1;
    }
    if (str_eq(src, dst)) {
        t_puts("  Source and destination are the same.\n");
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
    t_printf("  Decanted %s -> %s (%u bytes)\n", src, dst, size);
}

static void cmd_relabel(const char *args) {
    char src[FAT_PATH_MAX], dst[FAT_PATH_MAX];
    if (prep_file_op(args, "relabel <src> <dst>", src, dst, 1) < 0) return;
    uint32_t size = 0;
    int rc = copy_file_core(src, dst, &size);
    if (rc < 0) { print_copy_err(rc); return; }
    if (fat_delete(src) < 0) {
        t_color(VGA_YELLOW, VGA_BLACK);
        t_printf("  Copied to %s, but could not clear out %s.\n", dst, src);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    t_printf("  Relabelled %s -> %s (%u bytes)\n", src, dst, size);
}

/* jot <file> - open the full-screen text editor on a file (new or existing).
 * Editing implies the right to save, so an existing file needs read+write;
 * a new file needs write on its parent bowl. */
static void cmd_jot(const char *args) {
    if (needs_console("jot")) return;
    while (*args == ' ') args++;
    if (!*args) { t_puts("Usage: jot <file>\n"); return; }
    char path[FAT_PATH_MAX];
    resolve_path(args, path);
    if (fat_is_dir(path)) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  That's a bowl, not a file: %s\n", path);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
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
    if (!*args) { t_puts("Usage: ai <prompt>\n"); return; }
    if (!ai_available()) {
        t_color(VGA_YELLOW, VGA_BLACK);
        t_puts("  The kitchen oracle is asleep (no COM2 bridge).\n");
        t_puts("  Boot with the bridge attached:  make run-ai\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    t_color(VGA_DARK_GREY, VGA_BLACK);
    t_puts("  consulting the kitchen oracle...\n");
    t_color(VGA_LIGHT_CYAN, VGA_BLACK);

    ai_send(args);

    char reply[256];
    int  rlen = 0, got = 0;
    for (;;) {
        int c = ai_getc(got ? 600 : 1500);   /* 6 s between bytes, 15 s for the first */
        if (c < 0) {
            if (!got) {
                t_color(VGA_LIGHT_RED, VGA_BLACK);
                t_puts("  (no reply - is the bridge daemon running?)\n");
            }
            break;
        }
        if (c == AI_EOT) break;
        if (c == '\r') continue;
        t_putc((char)c);
        if (rlen < (int)sizeof(reply) - 1) reply[rlen++] = (char)c;
        got = 1;
    }
    reply[rlen] = '\0';
    if (got) {
        t_putc('\n');
        klog("[ai] reply: %s\n", reply);
    }
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* ── Pipelines ────────────────────────────────────────────────────────────
 *
 *   cook call.elf hello | raise.elf | weigh.elf
 *   cook spoon.elf < README.TXT > COPY.TXT
 *   cook ticket.elf A &
 *
 * Each stage is a process of its own (that is all a pipeline is here), wired
 * stdout-to-stdin with vfs_pipe. The last stage gets the terminal, because it
 * is the one whose output you are watching and the one Ctrl-C and Ctrl-Z
 * should act on.
 */
#define PIPE_MAX_STAGES  4

typedef struct {
    char prog[FAT_PATH_MAX];      /* as typed            */
    char path[FAT_PATH_MAX];      /* resolved            */
    char args[PROC_ARGS_MAX];
    char err[FAT_PATH_MAX];       /* 2> file, or ""      (v0.60.21) */
    uint8_t err_append, err_dup;  /* 2>>, 2>&1           */
    uint8_t dup_before_out;       /* 2>&1 came before the > (v0.60.31) */
} stage_t;

/* Next whitespace-delimited token, advancing *sp. The shell metacharacters
 * < > | & are tokens on their own, so `a.elf|b.elf` and `>OUT.TXT` work
 * without spaces around them. Returns 0 when the line is exhausted. */
static int shell_token(const char **sp, char *out, int max) {
    const char *s = *sp;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s) { *sp = s; return 0; }

    /* 2> 2>> 2>&1 (v0.60.21): only where a word would start, so `a2>x` is
     * still the word a2 and a redirection, as in sh. */
    if (s[0] == '2' && s[1] == '>') {
        int n = (s[2] == '>') ? 3 : (s[2] == '&' && s[3] == '1') ? 4 : 2;
        memcpy(out, s, (size_t)n); out[n] = '\0';
        *sp = s + n;
        return 1;
    }
    if (s[0] == '<' && s[1] == '<' && s[2] == '<') {   /* here-string (v0.60.45) */
        out[0] = out[1] = out[2] = '<'; out[3] = '\0';
        *sp = s + 3;
        return 1;
    }
    if (s[0] == '>' && s[1] == '>') {             /* append (v0.56.0) */
        out[0] = '>'; out[1] = '>'; out[2] = '\0';
        *sp = s + 2;
        return 1;
    }
    if (*s == '<' || *s == '>' || *s == '|' || *s == '&') {
        out[0] = *s++; out[1] = '\0';
        *sp = s;
        return 1;
    }
    /* A word runs to the next space or special character, except inside
     * '...' or "..." where everything is part of it (v0.57.2). The quotes
     * stay in the word: a program's uargv drops them as it splits, and the
     * shell drops them itself (unquote) from the words it uses as paths. */
    int n = 0;
    char q = 0;
    while (*s && n < max - 1) {
        if (q) { if (*s == q) q = 0; out[n++] = *s++; continue; }
        if (*s == '\'' || *s == '"') { q = *s; out[n++] = *s++; continue; }
        if (*s == ' ' || *s == '\t' || *s == '<' || *s == '>' || *s == '|' || *s == '&') break;
        out[n++] = *s++;
    }
    out[n] = '\0';
    *sp = s;
    return 1;
}

/* Drop the quotes from a word, keeping what they held (v0.57.2). */
static void unquote(char *w) {
    char *r = w, *o = w, q = 0;
    for (; *r; r++) {
        if (q) { if (*r == q) q = 0; else *o++ = *r; }
        else if (*r == '\'' || *r == '"') q = *r;
        else *o++ = *r;
    }
    *o = '\0';
}

/* Globs (v0.58.2). */
static int glob_match(const char *pat, const char *name) {
    if (*pat == '\0') return *name == '\0';
    if (*pat == '*') {
        for (const char *n = name; ; n++) {
            if (glob_match(pat + 1, n)) return 1;
            if (!*n) return 0;
        }
    }
    if (!*name) return 0;
    if (*pat == '?' || *pat == *name) return glob_match(pat + 1, name + 1);
    return 0;
}
static int byte_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Append one argument; 0, or -1 if it does not fit in PROC_ARGS_MAX
 * (v0.58.2: the arguments are no longer cut short silently). */
static int stage_add_arg(stage_t *st, const char *tok) {
    int len = (int)strlen(st->args), tl = (int)strlen(tok);
    if (len + (len ? 1 : 0) + tl > PROC_ARGS_MAX - 1) return -1;
    if (len) st->args[len++] = ' ';
    memcpy(st->args + len, tok, (uint32_t)tl + 1);
    return 0;
}

/* A program argument with an unquoted * or ? (and no quotes at all) is
 * matched against the names in the bowl it names, or the cwd: only its last
 * component may hold the pattern. Matches are sorted by byte, as sh sorts
 * in the C locale, and a leading * or ? does not match a name that starts
 * with '.'. No match: the word goes as it is, as sh does. -1 if the
 * arguments no longer fit. */
/* Expand a word's globs, calling emit for each result (v0.58.3; a callback
 * since v0.59.3 so `for` uses it too). A word with no unquoted * or ?, or
 * with any quote in it, is emitted exactly as written. */
static void glob_expand(const char *tok, void (*emit)(void *, const char *), void *ctx) {
    int globby = 0, quoted = 0;
    for (const char *c = tok; *c; c++) {
        if (*c == '*' || *c == '?') globby = 1;
        if (*c == '\'' || *c == '"') quoted = 1;
    }
    if (!globby || quoted) { emit(ctx, tok); return; }
    char dirpart[FAT_PATH_MAX], pat[FAT_PATH_MAX];
    const char *slash = 0;
    for (const char *c = tok; *c; c++) if (*c == '/') slash = c;
    if (slash) {
        int dl = (int)(slash - tok) + 1;
        if (dl >= FAT_PATH_MAX) { emit(ctx, tok); return; }
        memcpy(dirpart, tok, (uint32_t)dl); dirpart[dl] = '\0';
        strncpy(pat, slash + 1, sizeof(pat) - 1); pat[sizeof(pat) - 1] = '\0';
    } else { dirpart[0] = '\0'; strncpy(pat, tok, sizeof(pat) - 1); pat[sizeof(pat) - 1] = '\0'; }
    for (const char *c = dirpart; *c; c++) if (*c == '*' || *c == '?') { emit(ctx, tok); return; }
    char dir[FAT_PATH_MAX];
    if (dirpart[0]) resolve_path(dirpart, dir); else strcpy(dir, cur_shell()->cwd);
    if (!fat_is_dir(dir) || !may(dir, 'r')) { emit(ctx, tok); return; }
    fat_entry_t *e = kmalloc(sizeof(fat_entry_t) * FAT_LS_MAX);
    if (!e) { emit(ctx, tok); return; }
    int n = fat_ls(dir, e, FAT_LS_MAX);
    const char *hit[FAT_LS_MAX]; int nh = 0;
    for (int i = 0; i < n; i++) {
        const char *nm = fat_display_name(&e[i]);
        if (nm[0] == '.' && pat[0] != '.') continue;
        if (glob_match(pat, nm)) hit[nh++] = nm;
    }
    for (int i = 1; i < nh; i++)                         /* insertion sort, by byte */
        for (int j = i; j > 0 && byte_cmp(hit[j - 1], hit[j]) > 0; j--) { const char *t = hit[j]; hit[j] = hit[j - 1]; hit[j - 1] = t; }
    if (!nh) emit(ctx, tok);
    for (int i = 0; i < nh; i++) {
        char w[FAT_PATH_MAX];
        int dl = (int)strlen(dirpart);
        if (dl + (int)strlen(hit[i]) >= FAT_PATH_MAX) continue;
        strcpy(w, dirpart); strcpy(w + dl, hit[i]);
        emit(ctx, w);
    }
    kfree(e);
}

typedef struct { stage_t *st; int failed; } stage_ctx_t;
static void stage_emit(void *ctx, const char *w) {
    stage_ctx_t *c = (stage_ctx_t *)ctx;
    if (!c->failed && stage_add_arg(c->st, w) < 0) c->failed = 1;
}

/* A program argument, its globs expanded (v0.58.3). -1 if the arguments no
 * longer fit. */
static int stage_add_word(stage_t *st, const char *tok) {
    stage_ctx_t c = { st, 0 };
    glob_expand(tok, stage_emit, &c);
    return c.failed ? -1 : 0;
}

static void cook_err(const char *msg) {
    cur_shell()->last_status = 1;           /* nothing ran: a failure (v0.57.0) */
    t_color(VGA_LIGHT_RED, VGA_BLACK);
    t_printf("  %s\n", msg);
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* Feeds a captured builtin's output into a pipe for a shell loop that
 * reads it (v0.60.34), or a here-string into a program (v0.60.45), then
 * closes the pipe. */
typedef struct { vfs_node_t *wr; char *buf; int len; } loopfeed_t;
static void loopfeed_task(void *arg) {
    loopfeed_t *f = arg;
    for (int off = 0; off < f->len; ) {
        int w = vfs_write(f->wr, f->buf + off, (uint32_t)(f->len - off));
        if (w <= 0) break;
        off += w;
    }
    vfs_close(f->wr);
    kfree(f->buf); kfree(f);
    task_exit();
}

static void build_env(char *out, int max);   /* with hand (v0.60.26) */
static vfs_node_t *pending_in;   /* stdin for the next cook's first stage (v0.60.27, pipe_into) */
static void (*pending_feed)(void);   /* run once the pipeline is up (v0.60.33, pipe_into) */
static vfs_node_t *pending_out;        /* stdout for the next cook's last stage (v0.60.34) */
static int pending_nowait;             /* ... and do not wait: leave the pids here */
static uint32_t pending_pids[4];
static int pending_npids;

/* Open a file a pipeline writes to (> >> 2> 2>>), with the checks SYS_OPEN
 * makes: write on the file if it exists, write on its bowl if it does not
 * (v0.56.0: until then `> /etc/EVIL.TXT` from an ordinary cook made the
 * file in a bowl closed to cooks). Says why and returns 0 if not. */
static vfs_node_t *open_for_writing(const char *name, int append, const char *fail) {
    char path[FAT_PATH_MAX];
    resolve_path(name, path);
    if (fat_exists(path) && !may(path, 'w')) { deny(path); return 0; }
    if (!fat_exists(path)) {
        char parent[FAT_PATH_MAX];
        strcpy(parent, path);
        char *slash = parent + strlen(parent);
        while (slash > parent && *slash != '/') slash--;
        if (slash == parent) strcpy(parent, "/"); else *slash = '\0';
        if (!may(parent, 'w')) { deny(parent); return 0; }
    }
    vfs_node_t *n = vfs_open(path, VFS_WRONLY | (append ? VFS_APPEND : VFS_CREATE));
    if (!n) cook_err(fail);
    return n;
}

static void cmd_cook(const char *args) {
    while (*args == ' ') args++;
    if (!*args) {
        t_puts("Usage: cook <prog.elf> [args] [2> err] [| prog.elf ...] [< in | <<< word] [> out] [&]\n");
        return;
    }

    stage_t st[PIPE_MAX_STAGES];
    char infile[FAT_PATH_MAX];  infile[0]  = '\0';
    char outfile[FAT_PATH_MAX]; outfile[0] = '\0';
    char herestr[FAT_PATH_MAX]; int have_here = 0;      /* <<< (v0.60.45) */
    int  append = 0;                               /* >> (v0.56.0) */
    int  nst = 1, background = 0;
    st[0].prog[0] = st[0].args[0] = st[0].err[0] = '\0';
    st[0].err_append = st[0].err_dup = st[0].dup_before_out = 0;

    char tok[FAT_PATH_MAX];
    const char *sp = args;
    while (shell_token(&sp, tok, sizeof(tok))) {
        /* 2> 2>> 2>&1 belong to the stage they follow (v0.60.21). */
        if (str_eq(tok, "2>") || str_eq(tok, "2>>")) {
            stage_t *cs = &st[nst - 1];
            if (!shell_token(&sp, cs->err, sizeof(cs->err))) { cook_err("No file after 2>."); return; }
            unquote(cs->err);
            cs->err_append = (tok[2] == '>');
            cs->err_dup = 0;
            continue;
        }
        if (str_eq(tok, "<<<")) {
            /* cook prog <<< WORD (v0.60.45): WORD and a newline are the first
             * stage's stdin; the later of < and <<< wins, as in bash. */
            if (!shell_token(&sp, herestr, sizeof(herestr))) { cook_err("No word after <<<."); return; }
            unquote(herestr);
            have_here = 1; infile[0] = '\0';
            continue;
        }
        if (str_eq(tok, "2>&1")) {
            /* sh takes the copy where stdout points now: before a > it is
             * the terminal (or the pipe, if a | follows), after it the file. */
            st[nst - 1].err_dup = 1; st[nst - 1].err[0] = '\0';
            st[nst - 1].dup_before_out = (outfile[0] == '\0');
            continue;
        }
        if (str_eq(tok, ">>")) {
            if (!shell_token(&sp, outfile, sizeof(outfile))) { cook_err("No file after >>."); return; }
            unquote(outfile);
            append = 1;
            continue;
        }
        if (!tok[1] && (tok[0] == '|' || tok[0] == '<' || tok[0] == '>' || tok[0] == '&')) {
            switch (tok[0]) {
            case '|':
                if (!st[nst - 1].prog[0]) { cook_err("Nothing before the |."); return; }
                if (nst >= PIPE_MAX_STAGES) {
                    cook_err("Too many pipeline stages (max 4)."); return;
                }
                st[nst].prog[0] = st[nst].args[0] = st[nst].err[0] = '\0';
                st[nst].err_append = st[nst].err_dup = st[nst].dup_before_out = 0;
                nst++;
                break;
            case '<':
                if (!shell_token(&sp, infile, sizeof(infile))) {
                    cook_err("No file after <."); return;
                }
                unquote(infile);
                have_here = 0;
                break;
            case '>':
                if (!shell_token(&sp, outfile, sizeof(outfile))) {
                    cook_err("No file after >."); return;
                }
                unquote(outfile);
                break;
            case '&': background = 1; break;
            }
            continue;
        }
        if (!st[nst - 1].prog[0]) { strncpy(st[nst - 1].prog, tok, FAT_PATH_MAX - 1); unquote(st[nst - 1].prog); }
        else if (stage_add_word(&st[nst - 1], tok) < 0) { cook_err("The arguments are too long (128 bytes at most)."); return; }
    }
    if (!st[nst - 1].prog[0]) { cook_err("Nothing after the |."); return; }

    /* Check every stage before starting any of them: half a pipeline is worse
     * than none, and a typo in stage 3 should not leave stages 1 and 2 running. */
    for (int i = 0; i < nst; i++) {
        resolve_path(st[i].prog, st[i].path);
        /* Not here? Programs live in the root: look there too, the way a
         * PATH would (v0.52.2) - a cook starts in their home bowl now. */
        if ((!fat_exists(st[i].path) || fat_is_dir(st[i].path)) && st[i].prog[0] != '/') {
            char alt[FAT_PATH_MAX];
            alt[0] = '/'; strncpy(alt + 1, st[i].prog, sizeof(alt) - 2); alt[sizeof(alt) - 1] = '\0';
            if (fat_exists(alt) && !fat_is_dir(alt)) strcpy(st[i].path, alt);
        }
        if (!fat_exists(st[i].path) || fat_is_dir(st[i].path)) {
            t_color(VGA_LIGHT_RED, VGA_BLACK);
            t_printf("  No such program: %s\n", st[i].prog);
            t_color(VGA_LIGHT_GREY, VGA_BLACK);
            return;
        }
        if (!may(st[i].path, 'r') || !may(st[i].path, 'x')) { deny(st[i].path); return; }
    }

    /* Redirections. The process that receives one owns it and closes it on
     * exit, so nothing here is closed on the success path. */
    vfs_node_t *fin = 0, *fout = 0;
    if (have_here) {
        vfs_node_t *rd = 0, *wr = 0;
        loopfeed_t *f = kmalloc(sizeof(loopfeed_t));
        int hl = (int)strlen(herestr);
        char *b = kmalloc((uint32_t)hl + 2);
        if (!f || !b || vfs_pipe(&rd, &wr) < 0) { if (f) kfree(f); if (b) kfree(b); cook_err("Out of pipes."); return; }
        memcpy(b, herestr, (uint32_t)hl); b[hl] = '\n';
        f->wr = wr; f->buf = b; f->len = hl + 1;
        if (!task_spawn("herestr", loopfeed_task, f)) { vfs_close(wr); vfs_close(rd); kfree(b); kfree(f); cook_err("Out of memory."); return; }
        fin = rd;
    }
    if (infile[0]) {
        char path[FAT_PATH_MAX];
        resolve_path(infile, path);
        if (!may(path, 'r')) { deny(path); return; }
        fin = vfs_open(path, VFS_RDONLY);
        if (!fin) { cook_err("Cannot read the input file."); return; }
    }
    if (outfile[0]) {
        fout = open_for_writing(outfile, append, "Cannot write the output file.");
        if (!fout) { if (fin) vfs_close(fin); return; }
    }
    /* Each stage's 2> file, all opened before anything starts. */
    vfs_node_t *ferr[PIPE_MAX_STAGES] = { 0 };
    for (int i = 0; i < nst; i++) {
        if (!st[i].err[0]) continue;
        ferr[i] = open_for_writing(st[i].err, st[i].err_append, "Cannot write the 2> file.");
        if (!ferr[i]) {
            for (int k = 0; k < i; k++) if (ferr[k]) vfs_close(ferr[k]);
            if (fout) vfs_close(fout);
            if (fin) vfs_close(fin);
            return;
        }
    }

    uint32_t pids[PIPE_MAX_STAGES];
    int started = 0;
    if (!fin && pending_in) { fin = pending_in; pending_in = 0; }   /* a builtin's output (v0.60.27) */
    if (!fout && pending_out) { fout = pending_out; pending_out = 0; }   /* into a shell loop (v0.60.34) */
    int nowait = pending_nowait; pending_nowait = 0;
    static char env[PROC_ENV_MAX];          /* the same for every stage (v0.60.26) */
    build_env(env, sizeof(env));
    vfs_node_t *stage_in = fin;

    for (int i = 0; i < nst; i++) {
        vfs_node_t *rd = 0, *wr = 0;
        vfs_node_t *out = fout;                 /* last stage writes here */
        if (i + 1 < nst) {
            if (vfs_pipe(&rd, &wr) < 0) {
                cook_err("Out of pipes.");
                if (stage_in) vfs_close(stage_in);
                for (int k = i; k < nst; k++) if (ferr[k]) vfs_close(ferr[k]);
                if (fout) vfs_close(fout);
                break;
            }
            out = wr;
        }
        /* 2>&1 > f on the last stage: stderr to the terminal stdout had
         * before the > (2), not to f (1). */
        int dup = st[i].err_dup;
        if (dup && i + 1 == nst && fout && st[i].dup_before_out) dup = 2;
        int rc = proc_spawn(st[i].path, st[i].args, env, background, stage_in, out,
                            ferr[i], dup, &pids[i]);
        if (rc < 0) {
            cook_err(rc == PROC_ERR_FULL     ? "Too many orders (try: orders)" :
                     rc == PROC_ERR_COOK_CAP ? "You already have 4 orders cooking (try: orders, kill)" :
                     rc == PROC_ERR_RESERVED ? "The last orders are kept for the headchef (try: orders)" :
                                               "Out of memory starting the process.");
            if (rc == PROC_ERR_COOK_CAP || rc == PROC_ERR_RESERVED)
                klog("[proc] %s refused a process: %s\n", users_current_name(),
                     rc == PROC_ERR_COOK_CAP ? "their limit" : "the headchef's reserve");
            if (stage_in) vfs_close(stage_in);
            if (rd) vfs_close(rd);
            if (out) vfs_close(out);
            for (int k = i; k < nst; k++) if (ferr[k]) vfs_close(ferr[k]);   /* never handed over */
            if (fout && out != fout && i + 1 < nst) vfs_close(fout);
            break;
        }
        started++;
        stage_in = rd;                          /* next stage reads the pipe */
    }

    if (started == 0) return;
    if (started < nst) {                        /* partial pipeline: undo it */
        for (int i = 0; i < started; i++) proc_kill(pids[i]);
        return;
    }

    if (nowait) {                               /* `prog | while ...`: the loop reads it (v0.60.34) */
        pending_npids = nst;
        for (int i = 0; i < nst; i++) pending_pids[i] = pids[i];
        return;
    }
    if (pending_feed) {                         /* a builtin feeds the first stage now (v0.60.33) */
        void (*feed)(void) = pending_feed;
        pending_feed = 0;
        feed();
    }

    if (background) {
        t_color(VGA_DARK_GREY, VGA_BLACK);
        t_printf("  [%u] %s%s\n", pids[nst - 1], st[0].prog,
                   nst > 1 ? " | ..." : "");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    /* The last stage owns the terminal and is what we report on. Wait for it
     * FIRST: if it stops or is killed, waiting on an earlier stage would block
     * the shell against a pipe nobody is draining. */
    uint32_t last = pids[nst - 1];
    shell_set_fg(proc_by_pid(last));
    if (nst == 1 && !subst_depth) {            /* not into a $(...)'s output */
        t_color(VGA_DARK_GREY, VGA_BLACK);
        t_printf("  cooking %s in ring 3...\n", st[0].path);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
    }

    int code = 0;
    int done = proc_wait(last, &code);
    shell_set_fg(0);

    if (done == 0) {
        /* Stopped. Leave the rest of the pipeline alone; mark the earlier
         * stages as jobs so the prompt collects them when they finish. */
        for (int i = 0; i < nst - 1; i++) {
            proc_t *q = proc_by_pid(pids[i]);
            if (q) q->background = 1;
        }
        t_color(VGA_YELLOW, VGA_BLACK);
        t_printf("  [%u] set aside  %s   (plate / steep / kill %%)\n", last, st[nst - 1].path);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    /* Collect the earlier stages. They have finished, or will as soon as their
     * next write fails against a pipe with no reader left. */
    for (int i = 0; i < nst - 1; i++) {
        int c = 0;
        proc_wait(pids[i], &c);
    }

    /* The shell's own status, for `ssh host cmd`: the program's code, 126
     * for something that would not run, 128+9 for a kill, as sh does. */
    /* A kill is -(128+9) and a fault -(128+exception), so -code is sh's
     * 128+n either way: 137 for a kill, 142 for a page fault (v0.57.1; a
     * fault used to come out as code & 0xFF, 114, which meant nothing). */
    cur_shell()->last_status = (code == -1) ? 126 : (code < 0) ? ((-code) & 0xFF) : (code & 0xFF);
    if (code == PROC_EXIT_KILLED) {
        t_color(VGA_YELLOW, VGA_BLACK);
        t_printf("  [%s killed]\n", st[nst - 1].path);
    } else if (code == -1) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_puts("  Not a runnable soupOS program (need a 32-bit i386 ET_EXEC ELF).\n");
    } else if (!subst_depth) {                 /* not into a $(...)'s output */
        t_color(VGA_DARK_GREY, VGA_BLACK);
        t_printf("  [%s exited with code %d]\n", st[nst - 1].path, code);
    }
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* Resolve a job argument: a pid, or empty for the newest job. */
/* May the current cook signal this process (kill, stop, resume)? Their own,
 * or any if they are the headchef (v0.51.0). Until then any cook - or any SSH
 * session - could kill any other cook's programs. */
static int may_signal(const proc_t *p) {
    return users_is_headchef() || p->owner == users_current_uid();
}

static void not_yours(uint32_t pid) {
    t_color(VGA_LIGHT_RED, VGA_BLACK);
    t_printf("  %u is not yours to signal (ask the headchef)\n", pid);
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    klog("[proc] %s may not signal %u\n", users_current_name(), pid);
}

static proc_t *job_arg(const char *args) {
    while (*args == ' ') args++;
    proc_t *p = *args ? proc_by_pid((uint32_t)parse_num(args)) : proc_most_recent_on(TERM_KEY);
    if (p && !may_signal(p)) { not_yours(p->pid); return 0; }
    return p;
}

/* fg [pid] - put a job back in the foreground: give it the terminal, resume it
 * if it was stopped, and wait for it again. */
static void cmd_plate(const char *args) {
    proc_t *p = job_arg(args);
    if (!p || (p->state != PROC_RUNNING && p->state != PROC_STOPPED)) {
        t_puts("plate: no such order (try: orders)\n");
        return;
    }
    uint32_t pid = p->pid;
    p->background = 0;
    t_color(VGA_DARK_GREY, VGA_BLACK);
    t_printf("  %s\n", p->name);
    t_color(VGA_LIGHT_GREY, VGA_BLACK);

    shell_set_fg(p);
    proc_continue(p);

    int code = 0;
    int done = proc_wait(pid, &code);
    shell_set_fg(0);

    if (done == 0) {
        t_color(VGA_YELLOW, VGA_BLACK);
        t_printf("  [%u] stopped\n", pid);
    } else if (code == PROC_EXIT_KILLED) {
        t_color(VGA_YELLOW, VGA_BLACK);
        t_printf("  [%u] killed\n", pid);
    } else {
        t_color(VGA_DARK_GREY, VGA_BLACK);
        t_printf("  [%u] exited with code %d\n", pid, code);
    }
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* bg [pid] - resume a stopped job in the background. The shell keeps the
 * terminal, so the job's stdin reads see EOF from here on. */
static void cmd_steep(const char *args) {
    proc_t *p = job_arg(args);
    if (!p) { t_puts("steep: no such order (try: orders)\n"); return; }
    if (p->state != PROC_STOPPED) {
        t_printf("steep: order %u is not set aside\n", p->pid);
        return;
    }
    p->background = 1;
    proc_continue(p);
    t_color(VGA_DARK_GREY, VGA_BLACK);
    t_printf("  [%u] %s &\n", p->pid, p->name);
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* ── Networking ──────────────────────────────────────────────────────────── */

static void print_ip(uint32_t ip) {
    t_printf("%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
               (ip >> 8) & 0xFF, ip & 0xFF);
}

/* Parse dotted quad. Returns 0 on success. */
static int parse_ip(const char *s, uint32_t *out) {
    uint32_t v = 0;
    for (int part = 0; part < 4; part++) {
        if (*s < '0' || *s > '9') return -1;
        uint32_t oct = 0;
        while (*s >= '0' && *s <= '9') { oct = oct * 10 + (uint32_t)(*s - '0'); s++; }
        if (oct > 255) return -1;
        v = (v << 8) | oct;
        if (part < 3) { if (*s != '.') return -1; s++; }
    }
    /* Whatever follows the address belongs to the caller (ping's count, say).
     * Only a malformed address is rejected here: 10.0.2.2.5 or 10.0.2.2x. */
    if (*s != '\0' && *s != ' ') return -1;
    *out = v;
    return 0;
}

static void cmd_faucet(void) {
    if (!rtl8139_present()) {
        t_puts("  No network card. QEMU needs: -device rtl8139\n");
        return;
    }
    const uint8_t *m = rtl8139_mac();
    uint32_t tx, rx, drop, a, ip, icmp, udp, other;
    rtl8139_stats(&tx, &rx, &drop);
    net_stats(&a, &ip, &icmp, &udp, &other);

    t_color(VGA_YELLOW, VGA_BLACK);
    t_puts("  RTL8139\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    t_printf("    MAC      %x:%x:%x:%x:%x:%x\n", m[0], m[1], m[2], m[3], m[4], m[5]);
    t_printf("    address  %s\n", net_leased() ? "from DHCP" : "static");
    t_puts("    IP       "); print_ip(net_ip());
    t_puts("    mask ");     print_ip(net_mask()); t_putc('\n');
    t_puts("    gateway  "); print_ip(net_gw());   t_putc('\n');
    t_puts("    resolver "); print_ip(net_dns());  t_putc('\n');
    t_printf("    frames   tx %u  rx %u  dropped %u\n", tx, rx, drop);
    t_printf("    handled  arp %u  ip %u  icmp %u  udp %u  other %u\n",
               a, ip, icmp, udp, other);
}

static void cmd_plumbing(const char *args) {
    while (*args == ' ') args++;
    if (!*args) {
        t_puts("  ip "); print_ip(net_ip());
        t_puts("  mask "); print_ip(net_mask());
        t_puts("  gw ");   print_ip(net_gw());
        t_putc('\n');
        return;
    }
    if (str_eq(args, "dhcp")) {
        t_puts("  asking for a lease...\n");
        if (net_dhcp() == 0) {
            t_puts("  ip ");   print_ip(net_ip());
            t_puts("  mask "); print_ip(net_mask());
            t_puts("  gw ");   print_ip(net_gw());
            t_puts("  dns ");  print_ip(net_dns());
            t_putc('\n');
        } else {
            t_color(VGA_LIGHT_RED, VGA_BLACK);
            t_puts("  no answer from a DHCP server\n");
            t_color(VGA_LIGHT_GREY, VGA_BLACK);
        }
        return;
    }

    uint32_t ip;
    if (parse_ip(args, &ip) < 0) { t_puts("  Usage: plumbing [a.b.c.d | dhcp]\n"); return; }
    net_set_ip(ip);
    t_puts("  ip is now "); print_ip(ip); t_putc('\n');
}

static void cmd_table(void) {
    t_color(VGA_YELLOW, VGA_BLACK);
    t_puts("  ADDRESS          MAC\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    net_arp_dump();
}

/* reserve <host> <port> - open a TCP connection and hang up again. A table
 * booked and immediately released: it proves the handshake, which is all
 * stage one of TCP does. */
static void cmd_reserve(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { t_puts("  Usage: reserve <host> <port>\n"); return; }

    char host[64];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < (int)sizeof(host) - 1) { host[i] = args[i]; i++; }
    host[i] = '\0';

    const char *rest = args + i;
    while (*rest == ' ') rest++;
    int port = parse_num(rest);
    if (port <= 0 || port > 65535) { t_puts("  Usage: reserve <host> <port>\n"); return; }

    uint32_t ip;
    if (net_dns_resolve(host, &ip) < 0) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  could not resolve %s\n", host);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    t_puts("  booking "); print_ip(ip); t_printf(":%d...\n", port);
    if (tcp_connect(ip, (uint16_t)port) == 0) {
        t_color(VGA_LIGHT_GREEN, VGA_BLACK);
        t_puts("  table reserved (connection established)\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        tcp_close();
        t_puts("  and released\n");
    } else {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_puts("  no table (connection failed)\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

/* holler <host> <port> <text> - call across to another kitchen and listen for
 * the answer. Opens a connection, sends the text, prints whatever comes back.
 * This is what stage two of TCP is for; stage three turns it into `takeout`. */
static void cmd_holler(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { t_puts("  Usage: holler <host> <port> <text>\n"); return; }

    char host[64];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < (int)sizeof(host) - 1) { host[i] = args[i]; i++; }
    host[i] = '\0';

    const char *rest = args + i;
    while (*rest == ' ') rest++;
    int port = parse_num(rest);
    while (*rest && *rest != ' ') rest++;
    while (*rest == ' ') rest++;
    if (port <= 0 || port > 65535 || !*rest) {
        t_puts("  Usage: holler <host> <port> <text>\n");
        return;
    }

    uint32_t ip;
    if (net_dns_resolve(host, &ip) < 0) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  could not resolve %s\n", host);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    if (tcp_connect(ip, (uint16_t)port) < 0) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_puts("  no answer\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    int n = tcp_send(rest, (uint16_t)strlen(rest));
    if (n < 0) { t_puts("  nothing got through\n"); tcp_close(); return; }
    t_printf("  hollered %d bytes\n", n);

    uint8_t buf[256];
    int got = tcp_recv(buf, (uint16_t)(sizeof(buf) - 1), 200);   /* 2 s */
    if (got > 0) {
        buf[got] = '\0';
        t_color(VGA_LIGHT_GREEN, VGA_BLACK);
        t_printf("  heard back %d bytes: ", got);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        for (int k = 0; k < got; k++)
            t_putc((buf[k] >= ' ' && buf[k] < 127) ? (char)buf[k] : '.');
        t_putc('\n');
    } else {
        t_puts("  nothing came back\n");
    }
    tcp_close();
}

/* takeout <host>[:port] [path] - fetch a page over HTTP and print it.
 *
 * HTTP/1.0 with Connection: close, so the server hangs up when it is done and
 * the end of the body is simply the end of the connection. That avoids parsing
 * Content-Length or chunked encoding, which is a lot of machinery for a
 * command whose job is to prove TCP works. */
static void cmd_takeout(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { t_puts("  Usage: takeout <host>[:port] [path]\n"); return; }

    char host[64];
    int i = 0;
    while (args[i] && args[i] != ' ' && args[i] != ':' && i < (int)sizeof(host) - 1) {
        host[i] = args[i]; i++;
    }
    host[i] = '\0';

    const char *rest = args + i;
    int port = 80;
    if (*rest == ':') { rest++; port = parse_num(rest); while (*rest && *rest != ' ') rest++; }
    while (*rest == ' ') rest++;
    const char *path = *rest ? rest : "/";
    if (port <= 0 || port > 65535) { t_puts("  Usage: takeout <host>[:port] [path]\n"); return; }

    uint32_t ip;
    if (net_dns_resolve(host, &ip) < 0) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  could not resolve %s\n", host);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    t_puts("  collecting from "); print_ip(ip); t_printf(":%d%s\n", port, path);
    if (tcp_connect(ip, (uint16_t)port) < 0) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_puts("  nobody answered\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    char req[256];
    int n = 0;
    const char *p1 = "GET ";
    while (*p1) req[n++] = *p1++;
    for (const char *q = path; *q && n < 180; q++) req[n++] = *q;
    const char *p2 = " HTTP/1.0\r\nHost: ";
    while (*p2) req[n++] = *p2++;
    for (const char *q = host; *q && n < 240; q++) req[n++] = *q;
    const char *p3 = "\r\nConnection: close\r\n\r\n";
    while (*p3) req[n++] = *p3++;

    if (tcp_send(req, (uint16_t)n) < 0) {
        t_puts("  the request did not get through\n");
        tcp_abort();
        return;
    }

    /* Read until the server hangs up. Printed as it arrives rather than
     * buffered: a page can be larger than anything worth holding here. */
    uint32_t total = 0;
    uint8_t  buf[512];
    t_color(VGA_DARK_GREY, VGA_BLACK);
    for (;;) {
        int got = tcp_recv(buf, (uint16_t)(sizeof(buf) - 1), 200);
        if (got <= 0) {
            if (tcp_peer_done()) break;
            break;
        }
        total += (uint32_t)got;
        for (int k = 0; k < got; k++) {
            char c = (char)buf[k];
            if (c == '\n' || (c >= ' ' && c < 127)) t_putc(c);
        }
    }
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    t_printf("\n  %u bytes collected\n", total);
    klog("[tcp] takeout %u bytes\n", total);
    tcp_close();
}

static void cmd_sniff(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { t_puts("  Usage: sniff <hostname>\n"); return; }

    char name[64];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < (int)sizeof(name) - 1) { name[i] = args[i]; i++; }
    name[i] = '\0';

    uint32_t ip;
    t_printf("  resolving %s via ", name); print_ip(net_dns()); t_puts("...\n");
    if (net_dns_resolve(name, &ip) == 0) {
        t_printf("  %s is ", name); print_ip(ip); t_putc('\n');
    } else {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  could not resolve %s\n", name);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

static void cmd_sip(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { t_puts("  Usage: sip <host|a.b.c.d> [count]\n"); return; }

    /* A name is as good as an address here: net_dns_resolve passes a dotted
     * quad straight through, so one path covers both. */
    char host[64];
    int hi = 0;
    while (args[hi] && args[hi] != ' ' && hi < (int)sizeof(host) - 1) { host[hi] = args[hi]; hi++; }
    host[hi] = '\0';

    uint32_t ip;
    if (net_dns_resolve(host, &ip) < 0) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  could not resolve %s\n", host);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    /* Optional count after the host. */
    const char *rest = args;
    while (*rest && *rest != ' ') rest++;
    while (*rest == ' ') rest++;
    int count = *rest ? parse_num(rest) : 4;
    if (count < 1)  count = 1;
    if (count > 16) count = 16;

    t_puts("  PING "); print_ip(ip); t_printf("  %d packets\n", count);
    net_ping(ip, count);
}

/* pass [port] - let somebody drive the kitchen from the network.
 * Bare `pass` reports; `pass off` closes it. */
static void cmd_pass(const char *args) {
    while (*args == ' ') args++;

    if (!*args) {
        if (remote_running())
            t_printf("  the pass is open on port %u\n", remote_port());
        else
            t_puts("  the pass is closed. `pass <port>` opens it.\n");
        return;
    }
    if (str_eq(args, "off")) {
        if (!remote_running()) { t_puts("  the pass is already closed\n"); return; }
        remote_stop();
        t_puts("  closing the pass\n");
        return;
    }

    uint32_t port = 0;
    while (*args >= '0' && *args <= '9') port = port * 10 + (uint32_t)(*args++ - '0');
    if (!port || port > 65535) { t_puts("  Usage: pass <port> | pass off\n"); return; }

    int r = remote_start((uint16_t)port);
    if (r == -1) { t_printf("  already open on port %u\n", remote_port()); return; }
    if (r == -2) { t_puts("  No address yet. Try: plumbing dhcp\n"); return; }
    if (r < 0)   { t_puts("  could not open the pass\n"); return; }

    t_printf("  the pass is open on port %u\n", port);
    t_color(VGA_YELLOW, VGA_BLACK);
    t_puts("  Nothing is encrypted and nothing authenticates: whoever reaches\n"
             "  that port gets this keyboard. Local networks only.\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* hatch <port> - a serving hatch: answer HTTP GETs for files on the disk.
 *
 * One request at a time, because the TCP stack holds one connection and the
 * listener becomes that connection (see tcp.h). The loop is therefore:
 * listen, accept, read the request, answer it, close, listen again. A client
 * that knocks mid-request is refused rather than queued, which for serving
 * files off a FAT volume is a limitation nobody will notice.
 *
 * Any key stops it, checked between connections.
 */
static void cmd_hatch(const char *args) {
    while (*args == ' ') args++;
    uint32_t port = 0;
    while (*args >= '0' && *args <= '9') port = port * 10 + (uint32_t)(*args++ - '0');
    if (!port) port = 80;

    if (!net_ip()) {
        t_puts("  No address yet. Try: plumbing dhcp\n");
        return;
    }

    static uint8_t  body[32768];
    static char     req[512];
    static char     path[FAT_PATH_MAX];
    static char     head[256];

    t_printf("  hatch open on port %u - press a key to close it\n", port);
    t_flush_in();

    int served = 0;
    /* Listen ONCE: the listener is not a connection any more, so it stays
     * armed while a client is being served and the next SYN takes its own
     * slot instead of being refused. */
    if (tcp_listen((uint16_t)port) < 0) {
        t_puts("  could not listen\n");
        return;
    }
    while (!t_avail()) {
        int h = tcp_accept((uint16_t)port, 100);                    /* a second, then check the keyboard */
        if (h < 0) continue;

        int n = tcp_recv_on(h, (uint8_t *)req, sizeof(req) - 1, 200);
        if (n <= 0) { tcp_close_on(h); continue; }
        req[n] = '\0';

        /* "GET /NAME HTTP/1.0" - only the path matters here. */
        const char *pp = req;
        while (*pp && *pp != ' ') pp++;             /* past the method */
        while (*pp == ' ') pp++;
        int k = 0;
        path[k++] = '/';
        if (*pp == '/') pp++;
        while (*pp && *pp != ' ' && *pp != '\r' && *pp != '\n' && k < FAT_PATH_MAX - 1)
            path[k++] = *pp++;
        path[k] = '\0';
        if (k == 1) strcpy(path, "/README.TXT");    /* "/" gets something to read */

        /* Streamed through the VFS in body-sized pieces. Until v0.39.1 the
         * file was read into this 32 KB buffer whole, fat_read reported the
         * file's FULL size, and that many bytes were sent: a 40 KB file went
         * out with 7 KB of kernel memory after its first 32 KB, and anything
         * of 64 KB or more was cut to its size modulo 65536 by the
         * uint16_t length tcp_send_on takes. */
        /* Only what every cook may read (v0.55.0). Until then hatch served any
         * path to anyone on the network, whoever started it: an ordinary
         * cook's hatch gave out /etc/kitchen and the vault's private host
         * key. An HTTP client is anonymous, so the test is "public", not
         * "the cook who opened the hatch may read it". */
        if (fat_exists(path) && !users_public(path)) {
            static const char fb[] = "Forbidden.\n";
            strcpy(head, "HTTP/1.0 403 Forbidden\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: 11\r\n"
                         "Connection: close\r\n\r\n");
            tcp_send_on(h, head, (uint16_t)strlen(head));
            tcp_send_on(h, fb, (uint16_t)(sizeof(fb) - 1));
            t_printf("  403 %s\n", path);
            klog("[hatch] refused %s: not public\n", path);
            tcp_close_on(h);
            continue;
        }
        vfs_node_t *f = fat_is_dir(path) ? 0 : vfs_open(path, VFS_RDONLY);
        uint32_t got = f ? vfs_size(f) : 0;

        if (!f) {
            static const char nf[] = "Not found.\n";
            int hl = 0;
            strcpy(head, "HTTP/1.0 404 Not Found\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: 11\r\n"
                         "Connection: close\r\n\r\n");
            hl = (int)strlen(head);
            tcp_send_on(h, head, (uint16_t)hl);
            tcp_send_on(h, nf, (uint16_t)(sizeof(nf) - 1));
            t_printf("  404 %s\n", path);
        } else {
            /* Content-Length matters: without it a client waits for the close
             * and curl reports a truncated body if anything goes wrong. */
            /* No integer formatter in str.h, and vga_printf writes to the
             * screen rather than a buffer, so the length is built by hand. */
            char num[12];
            int ni = 0;
            uint32_t v = got;
            if (!v) num[ni++] = '0';
            char tmp[12]; int ti = 0;
            while (v) { tmp[ti++] = (char)('0' + v % 10); v /= 10; }
            while (ti) num[ni++] = tmp[--ti];
            num[ni] = '\0';
            strcpy(head, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: ");
            strcat(head, num);
            strcat(head, "\r\nConnection: close\r\n\r\n");
            tcp_send_on(h, head, (uint16_t)strlen(head));
            uint32_t sent = 0;
            while (sent < got) {
                int r = vfs_read(f, body, sizeof(body));
                if (r <= 0) break;
                if (tcp_send_on(h, body, (uint16_t)r) != r) break;   /* r <= 32768 */
                sent += (uint32_t)r;
            }
            vfs_close(f);
            if (sent != got) t_printf("  %s: sent %u of %u bytes\n", path, sent, got);
            t_printf("  200 %s (%u bytes)\n", path, got);
            served++;
        }
        tcp_close_on(h);
        klog("[hatch] served %s\n", path);
    }
    t_flush_in();
    tcp_stop_listening((uint16_t)port);
    t_printf("  hatch closed after %d file%s\n", served, served == 1 ? "" : "s");
    /* What serving cost the machine, measured over the second before the
     * key: queue item 6 asked whether TCP's waits spin. */
    klog("[hatch] closed after %d files; idle %u%%, hatch %u%% in the last second\n",
         served, task_idle_last(), task_current()->cpu_last);
}

#ifndef NO_DOOM
/* The WAD stays open once loaded, so repeated sizzles and hums are cheap.
 * Shared by both, because either can be the first to need it. */
static int wad_ready_for_shell(void) {
    static int opened = 0;
    if (!opened && wad_init("DOOM1.WAD") == 0) opened = 1;
    return opened;
}

/* hum <LUMP> - play one of Doom's MUS tunes; bare `hum` stops it. */
static void cmd_hum(const char *args) {
    if (!ac97_present()) { t_puts("  No sound card. QEMU needs -device AC97.\n"); return; }
    while (*args == ' ') args++;

    if (!*args) {
        if (music_playing()) { music_stop(); t_puts("  stopped humming\n"); }
        else t_puts("  Usage: hum <LUMP>   e.g. hum D_E1M1\n");
        return;
    }
    if (!wad_ready_for_shell()) { t_puts("  No DOOM1.WAD on the disk.\n"); return; }
    if (music_play(args, 1) < 0) {
        t_printf("  Could not play %s (is it a D_* lump?)\n", args);
        return;
    }
    t_printf("  humming %s - `hum` on its own stops it\n", args);
}

/* sizzle - play one of Doom's sound effects (a DS* lump from the WAD). */
static void cmd_sizzle(const char *args) {
    if (!ac97_present()) { t_puts("  No sound card. QEMU needs -device AC97.\n"); return; }
    while (*args == ' ') args++;
    if (!*args) { t_puts("  Usage: sizzle <LUMP>   e.g. sizzle DSPISTOL\n"); return; }

    if (!wad_ready_for_shell()) { t_puts("  No DOOM1.WAD on the disk.\n"); return; }
    /* Several lumps at once, because that is what the mixer is for - and
     * because typing two commands takes the better part of two seconds, so
     * overlap cannot be demonstrated any other way. */
    char name[16];
    int started = 0;
    while (*args) {
        while (*args == ' ') args++;
        int k = 0;
        while (*args && *args != ' ' && k < (int)sizeof(name) - 1)
            name[k++] = *args++;
        name[k] = '\0';
        if (!k) break;
        if (doomsnd_play(name) < 0) t_printf("  Could not play %s\n", name);
        else { t_printf("  sizzling %s\n", name); started++; }
    }
    if (started > 1) t_printf("  %d voices at once\n", started);
}
#endif /* NO_DOOM: sizzle reads DS* lumps, so it needs the WAD reader */

/* hush [0-100] - how loud the kitchen gets. Bare `hush` reports the level. */
static void cmd_hush(const char *args) {
    while (*args == ' ') args++;
    if (!*args) {
        t_printf("  the kitchen is at %d of 100\n", mixer_master());
        return;
    }
    int level = 0;
    while (*args >= '0' && *args <= '9') level = level * 10 + (*args++ - '0');
    mixer_set_master(level);
    t_printf("  hushed to %d of 100%s\n", mixer_master(),
               mixer_master() == 0 ? " (silent)" : "");
}

/* whistle - a tone through the sound card, as the kettle would.
 * `beep` is the PC speaker; this is the AC97. */
static void cmd_whistle(const char *args) {
    if (!ac97_present()) {
        t_puts("  No sound card. QEMU needs -device AC97.\n");
        return;
    }
    while (*args == ' ') args++;
    uint32_t hz = 0, ms = 0;
    while (*args >= '0' && *args <= '9') hz = hz * 10 + (uint32_t)(*args++ - '0');
    while (*args == ' ') args++;
    while (*args >= '0' && *args <= '9') ms = ms * 10 + (uint32_t)(*args++ - '0');
    if (!hz) hz = 440;
    if (!ms) ms = 300;
    t_printf("  whistling %u Hz for %u ms...\n", hz, ms);
    /* A mixer voice, so two whistles - or a whistle over a sizzle - overlap
     * instead of queueing. Returns at once; the mixer task does the rest. */
    if (mixer_tone(hz, ms) < 0) t_puts("  the kettle would not whistle\n");
}

/* larder - how much room is left on the disk. */
static void cmd_larder(void) {
    uint32_t freec = 0, total = 0, cbytes = 0;
    fat_space(&freec, &total, &cbytes);
    if (!total) { t_puts("  No filesystem mounted.\n"); return;  }
    uint32_t used = total - freec;
    uint32_t kb_per = cbytes / 1024;
    t_color(VGA_YELLOW, VGA_BLACK);
    t_puts("  Larder:\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    t_printf("  %u of %u clusters used (%u KB each)\n", used, total, kb_per);
    t_printf("  %u KB free, %u KB total\n", freec * kb_per, total * kb_per);
    klog("[larder] free=%u total=%u clusterkb=%u\n", freec, total, kb_per);
}

/* skewer - what the pointer is pointing at. */
static void cmd_skewer(void) {
    if (needs_console("skewer")) return;
    if (!mouse_present()) {
        t_puts("  No mouse. QEMU has one by default; check IRQ 12.\n");
        return;
    }
    int x, y, b;
    uint32_t moves, resyncs;
    mouse_get(&x, &y, &b);
    mouse_stats(&moves, &resyncs);
    t_printf("  pointing at %d,%d  buttons:%s%s%s\n", x, y,
               (b & 1) ? " left"   : "",
               (b & 2) ? " right"  : "",
               (b & 4) ? " middle" : "");
    t_printf("  %u packets, %u resyncs\n", moves, resyncs);
    klog("[mouse] at %d,%d buttons=%d packets=%u resyncs=%u\n",
         x, y, b, moves, resyncs);
}

/* vault - the SSH server: `vault <port>`, `vault off`, or bare for status.
 * The encrypted, host-authenticated door; `pass` is the clear-text one. */
static void cmd_vault(const char *args) {
    while (*args == ' ') args++;
    if (!*args) {
        if (ssh_running()) t_printf("  vault open on port %u, host key %s\n", ssh_port(), ssh_fingerprint());
        else               t_puts("  vault closed. Usage: vault <port> | vault off | vault idle <seconds> | vault rekey <packets>\n");
        return;
    }
    if (str_startswith(args, "idle")) {             /* vault idle <seconds>, 0 = never */
        const char *a = args + 4;
        while (*a == ' ') a++;
        if (*a) ssh_set_idle((uint32_t)(parse_num(a) > 0 ? parse_num(a) : 0));
        if (ssh_idle()) t_printf("  the vault ends a session after %u idle seconds.\n", ssh_idle());
        else            t_puts("  the vault never ends an idle session.\n");
        return;
    }
    if (str_startswith(args, "rekey")) {
        const char *a = args + 5;
        while (*a == ' ') a++;
        int n = parse_num(a);
        ssh_set_rekey_packets(n > 0 ? (uint32_t)n : 0);
        t_printf("  the vault rekeys every %d packets, and every hour.\n", n > 0 ? n : 0);
        return;
    }
    if (str_eq(args, "off")) {
        if (!ssh_running()) { t_puts("  vault is not open.\n"); return; }
        ssh_stop();
        t_puts("  closing the vault.\n");
        return;
    }
    int port = parse_num(args);
    if (port <= 0 || port > 65535) { t_puts("  vault: port must be 1-65535\n"); return; }
    int r = ssh_start((uint16_t)port);
    if      (r == -1) t_puts("  vault is already open.\n");
    else if (r == -2) t_puts("  no IP address yet; try `plumbing dhcp` first.\n");
    else if (r == -3) t_puts("  could not start the vault task.\n");
    else if (r == -4) t_puts("  could not read or write the host key on disk.\n");
    else {
        t_printf("  vault open on port %d.\n", port);
        t_color(VGA_LIGHT_CYAN, VGA_BLACK);
        t_printf("  host key %s\n", ssh_fingerprint());
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

/* du [bowl] - what takes the space (v0.53.4). Each entry of the bowl with
 * the clusters and bytes under it, then the total, which also counts the
 * bowl's own clusters - the way mtools' mdu counts, so the two can be
 * compared. Clusters are the chains' real lengths, walked through the FAT,
 * not sizes rounded up. A bowl the cook may not read is shown, not entered. */
typedef struct { uint32_t clusters, bytes; int hidden; } du_t;

static void du_walk(const char *dir, du_t *acc, int depth) {
    if (depth > 16) return;
    if (!may(dir, 'r')) { acc->hidden++; return; }
    fat_entry_t *e = kmalloc(sizeof(fat_entry_t) * FAT_LS_MAX);
    if (!e) return;
    int n = fat_ls(dir, e, FAT_LS_MAX);
    for (int i = 0; i < n; i++) {
        acc->clusters += fat_chain_clusters(e[i].cluster);
        if (e[i].attr & FAT_ATTR_DIR) {
            char path[FAT_PATH_MAX];
            const char *nm = fat_display_name(&e[i]);
            size_t dl = strlen(dir);
            if (dl + 1 + strlen(nm) + 1 > sizeof(path)) continue;
            strcpy(path, dir); if (dl > 1) path[dl++] = '/'; strcpy(path + dl, nm);
            du_walk(path, acc, depth + 1);
        } else {
            acc->bytes += e[i].size;
        }
    }
    kfree(e);
}

static void cmd_du(const char *args) {
    while (*args == ' ') args++;
    char dir[FAT_PATH_MAX];
    if (*args) resolve_path(args, dir); else strcpy(dir, cur_shell()->cwd);
    if (!fat_is_dir(dir)) { t_printf("  No such bowl: %s\n", *args ? args : dir); return; }
    if (!may(dir, 'r')) { deny(dir); return; }
    fat_entry_t *e = kmalloc(sizeof(fat_entry_t) * FAT_LS_MAX);
    if (!e) return;
    int n = fat_ls(dir, e, FAT_LS_MAX);
    uint32_t cb = fat_cluster_bytes();
    t_color(VGA_YELLOW, VGA_BLACK);
    t_printf("  CLUSTERS        BYTES  NAME            (a cluster is %u bytes)\n", cb);
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    du_t total = {0, 0, 0};
    for (int i = 0; i < n; i++) {
        du_t one = {fat_chain_clusters(e[i].cluster), 0, 0};
        const char *nm = fat_display_name(&e[i]);
        if (e[i].attr & FAT_ATTR_DIR) {
            char path[FAT_PATH_MAX];
            size_t dl = strlen(dir);
            if (dl + 1 + strlen(nm) + 1 > sizeof(path)) continue;
            strcpy(path, dir); if (dl > 1) path[dl++] = '/'; strcpy(path + dl, nm);
            du_walk(path, &one, 1);
        } else {
            one.bytes = e[i].size;
        }
        t_printf("  %8u  %11u  %s%s%s\n", one.clusters, one.bytes, nm,
                 (e[i].attr & FAT_ATTR_DIR) ? "/" : "", one.hidden ? "  (some not yours to read)" : "");
        total.clusters += one.clusters; total.bytes += one.bytes; total.hidden += one.hidden;
    }
    kfree(e);
    /* The bowl's own clusters (the root of a FAT16 volume has none). */
    uint32_t own = fat_chain_clusters(fat_first_cluster(dir));
    t_printf("  %8u  %11u  total for %s%s\n", total.clusters + own, total.bytes, dir,
             total.hidden ? " (some bowls not yours to read)" : "");
}

/* who - every shell on the machine: its cook, where it is, how long it has
 * been up and how long since its last keystroke (v0.52.0). */
static int u_to_str(uint32_t v, char *out) {      /* decimal, no NUL; returns length */
    char d[11]; int n = 0;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = 0; i < n; i++) out[i] = d[n - 1 - i];
    return n;
}

static void fmt_dur(char *out, uint32_t ticks) {
    uint32_t sec = ticks / 100, h = sec / 3600, m = (sec / 60) % 60, s = sec % 60;
    char *p = out;
    if (h) { p += u_to_str(h, p); *p++ = ':'; *p++ = (char)('0' + m / 10); *p++ = (char)('0' + m % 10); }
    else   { p += u_to_str(m, p); }
    *p++ = ':'; *p++ = (char)('0' + s / 10); *p++ = (char)('0' + s % 10); *p = '\0';
}

static void cmd_who(void) {
    t_color(VGA_YELLOW, VGA_BLACK);
    t_puts("  COOK        WHERE                UP        IDLE\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    uint32_t now = timer_get_ticks();
    preempt_disable();
    shell_t *list[8]; int n = 0;
    for (int i = 0; i < 8; i++) if (shells[i]) list[n++] = shells[i];
    preempt_enable();
    for (int i = 0; i < n; i++) {
        shell_t *sh = list[i];
        char up[16], idle[16];
        fmt_dur(up, now - sh->started); fmt_dur(idle, now - sh->last_input);
        t_printf("  %-10s  %-18s  %8s  %8s%s\n", users_name_of(sh->uid), sh->where, up, idle,
                 sh == cur_shell() ? "   (you)" : (sh->exec_line[0] ? "   exec" : ""));
    }
}

/* last - the login record, newest first (v0.53.2). /etc/logins is the
 * headchef's but readable by every cook, as a login record usually is. */
static void cmd_last(void) {
    if (!may(LOGINS_PATH, 'r')) { deny(LOGINS_PATH); return; }
    uint32_t cap = LOGINS_KEEP * (LOGINS_LINE + 8), got = 0;
    char *buf = kmalloc(cap + 1);
    if (!buf) return;
    if (fat_read(LOGINS_PATH, (uint8_t *)buf, cap, &got) < 0) {
        t_puts("  No logins recorded yet.\n"); kfree(buf); return;
    }
    if (got > cap) got = cap;
    buf[got] = '\0';
    t_color(VGA_YELLOW, VGA_BLACK);
    t_puts("  WHEN              RESULT   COOK             FROM\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    int end = (int)got;
    while (end > 0) {
        int start = end - 1;                       /* end is one past a '\n' */
        while (start > 0 && buf[start - 1] != '\n') start--;
        int len = end - start;
        if (len > 1) {
            buf[start + len - 1] = '\0';           /* drop the newline */
            if (len > 25 && buf[start + 18] == 'r') t_color(VGA_LIGHT_RED, VGA_BLACK);   /* the RESULT column */
            t_printf("  %s\n", buf + start);
            t_color(VGA_LIGHT_GREY, VGA_BLACK);
        }
        end = start;
    }
    kfree(buf);
}

/* sample - taste the soup: run the in-kernel subsystem checks. `sample slow`
 * adds the ones that take seconds, which the gate leaves to their own script. */
static void cmd_sample(const char *args) {
    while (*args == ' ') args++;
    int failed = selftest_run(str_eq(args, "slow"));
    if (failed) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_puts("  something is off in the kitchen.\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

/* clip - show what is on the clipboard. Mirrored to the kernel log so a
 * headless test can read the result of a copy or a cut. */
static void cmd_scraps(void) {
    uint32_t n = clip_len();
    if (n == 0) {
        t_puts("  (clipboard empty)\n");
        klog("[clip] empty\n");
        return;
    }
    const char *cb = clip_peek();
    t_color(VGA_DARK_GREY, VGA_BLACK);
    t_printf("  %u bytes:\n", n);
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    t_puts("  ");
    for (uint32_t i = 0; i < n; i++) {
        char c = cb[i];
        if (c == '\n')                 { t_puts("\n  "); }
        else if (c >= ' ' && c < 127)   { t_putc(c); }
        else                            { t_putc('.'); }
    }
    t_putc('\n');

    /* klog wants a NUL-terminated string, and control bytes would wreck the
     * log, so flatten into a bounded printable copy. */
    char line[120];
    uint32_t m = (n < sizeof(line) - 1) ? n : (uint32_t)(sizeof(line) - 1);
    for (uint32_t i = 0; i < m; i++)
        line[i] = (cb[i] >= ' ' && cb[i] < 127) ? cb[i] : '.';
    line[m] = '\0';
    klog("[clip] %u bytes: %s\n", n, line);
}

/* jobs - the process table. `ps` lists tasks, including the task each process
 * runs on; this lists processes, which is where background state and exit
 * codes live. A finished background job stays here until the next prompt
 * announces it. */
static void cmd_orders(void) {
    int idx = 0, n = 0;
    proc_t *p;
    t_color(VGA_YELLOW, VGA_BLACK);
    t_puts("  PID  ST  BG  CODE  OWNER     PROGRAM\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    while ((p = proc_next(&idx)) != 0) {
        n++;
        t_printf("  %3u   %c   %c", p->pid,
                   proc_state_letter(p->state), p->background ? 'y' : 'n');
        if (p->state == PROC_ZOMBIE) t_printf("  %4d", p->exit_code);
        else                         t_puts("     -");
        t_printf("  %-8s  %s%s\n", users_name_of(p->owner), p->name, p->killed ? " (killing)" : "");
    }
    if (!n) t_puts("  (no processes)\n");
}

/* ---- bowls (directories) ---------------------------------------------- */

/* PWD and OLDPWD (v0.60.32) are ordinary variables, as in sh: login sets
 * PWD, every cd that moves sets both, a cook may set or discard them, and
 * cd - goes to $OLDPWD. Set quietly: a cook with every slot full still
 * moves, the variables just do not follow. */
static const char *get_var(const char *name) {
    shell_t *me = cur_shell();
    for (int v = 0; v < me->nvars; v++) if (str_eq(me->vname[v], name)) return me->vval[v];
    return 0;
}
static void set_var(const char *name, const char *value);
static void set_var_quiet(const char *name, const char *value) {
    shell_t *me = cur_shell();
    int st = me->last_status;
    if (get_var(name) || me->nvars < VARS_MAX) set_var(name, value);
    me->last_status = st;
}

static void cmd_cd(const char *args) {
    shell_t *me = cur_shell();
    while (*args == ' ') args++;
    char target[FAT_PATH_MAX], was[FAT_PATH_MAX];
    const char *pv = get_var("PWD");                 /* OLDPWD is $PWD as it was, as sh has it */
    strncpy(was, pv ? pv : me->cwd, FAT_PATH_MAX - 1); was[FAT_PATH_MAX - 1] = '\0';
    me->last_status = 0;
    if (!*args) {                                   /* cd alone -> home (v0.52.3) */
        ensure_home(me->uid, me->cwd);
        set_var_quiet("OLDPWD", was); set_var_quiet("PWD", me->cwd);
        return;
    }
    int back = str_eq(args, "-");                   /* cd - (v0.60.32), as sh's */
    if (back) {
        const char *o = get_var("OLDPWD");
        if (!o || !*o) { t_puts("  cd: OLDPWD not set\n"); me->last_status = 1; return; }
        resolve_path(o, target);
    } else resolve_path(args, target);
    if (!fat_is_dir(target)) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  No such bowl: %s\n", back ? target : args);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        me->last_status = 1;
        return;
    }
    if (!may(target, 'x')) { deny(target); me->last_status = 1; return; }
    strncpy(me->cwd, target, FAT_PATH_MAX - 1);
    me->cwd[FAT_PATH_MAX - 1] = '\0';
    set_var_quiet("OLDPWD", was); set_var_quiet("PWD", me->cwd);
    if (back) t_printf("  %s\n", me->cwd);          /* sh prints where cd - went */
}

static void cmd_pwd(void) {
    t_printf("  %s\n", cur_shell()->cwd);
}

static void cmd_mkbowl(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { t_puts("Usage: mkbowl <name>\n"); return; }
    char path[FAT_PATH_MAX];
    resolve_path(args, path);
    char par[FAT_PATH_MAX];
    parent_of(path, par);
    if (!may(par, 'w')) { deny(par); return; }
    if (fat_mkdir(path) < 0) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  Could not make bowl: %s\n", args);
        t_puts("  (already exists, missing parent bowl, disk full, or FAT32)\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
    } else {
        t_printf("  Bowl ready: %s\n", path);
    }
}

static void cmd_rmbowl(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { t_puts("Usage: rmbowl <name>\n"); return; }
    char path[FAT_PATH_MAX];
    resolve_path(args, path);
    if (str_eq(path, cur_shell()->cwd)) {
        t_puts("  Cannot rmbowl the bowl you are standing in.\n");
        return;
    }
    char par[FAT_PATH_MAX];
    parent_of(path, par);
    if (!may(par, 'w')) { deny(par); return; }
    if (fat_rmdir(path) < 0) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  Could not remove bowl: %s\n", args);
        t_puts("  (not a bowl, not empty, or does not exist)\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
    } else {
        t_printf("  Bowl removed: %s\n", path);
    }
}

/* bowltest - exercises the bowl (directory) layer end to end and cleans up
 * after itself, so it can be run repeatedly. */
static void cmd_bowltest(void) {
    int pass = 0, fail = 0;
    #define CHECK(label, cond) do {                       \
        if (cond) { t_color(VGA_LIGHT_GREEN,VGA_BLACK);\
                    t_printf("  [pass] %s\n", label); pass++; }\
        else      { t_color(VGA_LIGHT_RED,VGA_BLACK);  \
                    t_printf("  [FAIL] %s\n", label); fail++; }\
        t_color(VGA_LIGHT_GREY, VGA_BLACK);          \
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

    if (fail == 0) t_color(VGA_LIGHT_GREEN, VGA_BLACK);
    else           t_color(VGA_LIGHT_RED,   VGA_BLACK);
    t_printf("  bowltest: %d passed, %d failed\n", pass, fail);
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    #undef CHECK
}

/* ---- kitchen staff: users, access control, elevation ------------------- */

static void dispatch(const char *buf);   /* forward decl for chef elevation */
static void cmd_run(const char *args);  /* defined beside run_rc (v0.57.5) */
static void cmd_unset(const char *args); /* beside run_line (v0.58.0) */
static void cmd_hand(const char *args);  /* beside it (v0.60.26) */
static void cmd_break(const char *args, int is_continue);   /* with the loops (v0.60.44) */
static void cmd_read(const char *args);  /* beside run_line (v0.60.4) */

/* The headchef's commands (v0.55.0). Until then any cook could close the
 * vault on everyone, open the clear-text pass door, change the vault's
 * settings, reboot, halt or crash the machine, and run the self-tests that
 * write the disk as the kernel - from an SSH session too. `chef <command>`
 * still runs one as the headchef, for a cook who knows the secret. */
static int headchef_only(const char *what) {
    if (users_is_headchef()) return 1;
    cur_shell()->last_status = 1;
    t_printf("  Only the headchef may %s.\n", what);
    klog("[shell] %s may not %s\n", users_current_name(), what);
    return 0;
}
static int has_args(const char *a) { while (*a == ' ') a++; return *a != 0; }

static void cmd_whoami(void) {
    t_printf("  %s  (uid %u)  -  %s\n",
               users_current_name(), users_current_uid(),
               users_is_headchef() ? "headchef" : "cook");
}

static void cmd_roster(void) {
    t_color(VGA_YELLOW, VGA_BLACK);
    t_printf("  Kitchen roster (%d):\n", users_count());
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int i = 0; i < users_count(); i++) {
        uint8_t uid = users_uid_at(i);
        t_printf("    uid %3u  %s%s\n", uid, users_name_at(i),
                   uid == 0 ? "  (headchef)" : "");
    }
}

static void hist_save(void);    /* with the history */
static void cmd_clockout(void) {
    hist_save();                               /* the shift's leftovers, kept */
    if (cur_shell() != &the_shell) {           /* a session: clocking out leaves */
        t_printf("  %s clocks out. Goodbye.\n", users_current_name());
        cur_shell()->exiting = 1;
        return;
    }
    t_printf("  %s clocks out. The kitchen is yours, next cook.\n",
               users_current_name());
    /* The next cook starts clean (v0.60.12): the console's shell outlives
     * the cook, so what they typed (leftovers and the up arrow), set
     * (variables, functions, $1..$9) and cut (Ctrl+U, Ctrl+W) would pass to whoever
     * clocks in next. Wiped, not just forgotten: a secret typed at the
     * wrong prompt sits in the history. */
    shell_t *sh = cur_shell();
    memset(sh->hist, 0, sizeof(sh->hist));
    sh->hist_head = sh->hist_size = 0;
    memset(sh->vname, 0, sizeof(sh->vname));
    memset(sh->vval, 0, sizeof(sh->vval));
    memset(sh->vhand, 0, sizeof(sh->vhand));
    memset(sh->vpend, 0, sizeof(sh->vpend));
    sh->nvars = sh->npend = 0;
    memset(sh->parg, 0, sizeof(sh->parg));
    sh->npargs = 0;
    memset(sh->fname, 0, sizeof(sh->fname));
    memset(sh->fbody, 0, sizeof(sh->fbody));
    sh->nfuncs = 0;
    clip_clear();
    do_login();
}

/* secret - change your own secret (the old one, then the new one twice), or,
 * as the headchef, `secret <cook>` sets anyone's without the old one
 * (v0.52.1). Stored in the build's kind: PBKDF2 with a fresh salt. */
static void cmd_secret(const char *args) {
    while (*args == ' ') args++;
    char name[USER_NAME_MAX];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < USER_NAME_MAX - 1) { name[i] = args[i]; i++; }
    name[i] = '\0';
    const char *who = name[0] ? name : users_current_name();
    int self = str_eq(who, users_current_name());
    if (!self && !users_is_headchef()) { t_puts("  Only the headchef sets another cook's secret.\n"); return; }
    if (users_uid_of(who) < 0) { t_printf("  No cook called %s.\n", who); return; }
    char old[64], fresh[64], again[64];
    /* Your own secret always needs the current one, the headchef's included:
     * otherwise an unattended session hands over the account. */
    if (self) {
        t_puts("  Current secret: ");
        read_line(old, sizeof(old), '*');
        if (users_check(who, old) < 0) {
            memset(old, 0, sizeof(old));
            t_color(VGA_LIGHT_RED, VGA_BLACK); t_puts("  That is not your secret.\n"); t_color(VGA_LIGHT_GREY, VGA_BLACK);
            klog("[users] %s: secret change refused (wrong current secret)\n", who);
            return;
        }
        memset(old, 0, sizeof(old));
    }
    t_printf("  New secret for %s: ", who);  read_line(fresh, sizeof(fresh), '*');
    t_puts("  Again:                 ");       read_line(again, sizeof(again), '*');
    int same = str_eq(fresh, again) && fresh[0];
    if (same && users_set_secret(who, fresh) == 0) {
        t_printf("  %s has a new secret.\n", who);
        klog("[users] %s: secret changed\n", who);
    } else {
        t_puts(same ? "  Could not save the roster.\n" : "  The two did not match (or were empty); nothing changed.\n");
    }
    memset(fresh, 0, sizeof(fresh)); memset(again, 0, sizeof(again));
}

static void cmd_hire(const char *args) {
    while (*args == ' ') args++;
    if (!users_is_headchef()) {
        t_puts("  Only the headchef hires cooks. Try: chef hire <name>\n");
        return;
    }
    if (!*args) { t_puts("Usage: hire <name>\n"); return; }
    char name[USER_NAME_MAX];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < USER_NAME_MAX - 1) { name[i] = args[i]; i++; }
    name[i] = '\0';

    char secret[64], again[64];
    t_printf("  Set a secret for %s: ", name);
    read_line(secret, sizeof(secret), '*');
    t_puts("  Confirm secret:         ");
    read_line(again, sizeof(again), '*');
    if (!str_eq(secret, again)) {
        t_puts("  Secrets do not match -- nobody hired.\n");
        return;
    }
    if (users_add(name, secret) == 0) {
        char home[FAT_PATH_MAX];
        ensure_home((uint8_t)users_uid_of(name), home);
        char want[FAT_PATH_MAX];
        strcpy(want, "/home/"); strncpy(want + 6, name, sizeof(want) - 7); want[sizeof(want) - 1] = '\0';
        if (str_eq(home, "/") && fat_is_dir(want))
            t_printf("  Hired: %s now has a place in the kitchen, but no home: /home/%s is not theirs.\n", name, name);
        else if (str_eq(home, "/"))
            t_printf("  Hired: %s now has a place in the kitchen, but /home/%s could not be made (disk full?).\n", name, name);
        else
            t_printf("  Hired: %s now has a place in the kitchen, and a home at %s.\n", name, home);
    }
    else
        t_puts("  Could not hire (name taken, roster full, or disk error).\n");
}

/* Hand everything `uid` owns under `dir` to the headchef (v0.53.0). uids
 * are reused - the owner byte holds only 0-55, so they must be - and a file
 * left owned by a fired cook's uid belongs to whoever is hired next. One
 * directory listing on the heap per level, since the shell's stack would not
 * hold many. Returns the number of entries handed over. */
static int hand_over(const char *dir, uint8_t uid, int depth) {
    if (depth > 16) return 0;
    fat_entry_t *e = kmalloc(sizeof(fat_entry_t) * FAT_LS_MAX);
    if (!e) return 0;
    int n = fat_ls(dir, e, FAT_LS_MAX), moved = 0;
    for (int i = 0; i < n; i++) {
        char path[FAT_PATH_MAX];
        const char *nm = fat_display_name(&e[i]);
        size_t dl = strlen(dir);
        if (dl + 1 + strlen(nm) + 1 > sizeof(path)) continue;
        strcpy(path, dir);
        if (dl > 1) path[dl++] = '/';
        strcpy(path + dl, nm);
        uint8_t owner, mode;
        if (fat_stat(path, &owner, &mode) == 0 && owner == uid && fat_chown(path, 0) == 0) moved++;
        if (e[i].attr & FAT_ATTR_DIR) moved += hand_over(path, uid, depth + 1);
    }
    kfree(e);
    return moved;
}

/* How many shells and processes a cook still has: a live one carries the
 * uid, and would pass to the next hire as surely as a file. */
static void still_at_work(uint8_t uid, int *nshells, int *nprocs) {
    *nshells = *nprocs = 0;
    preempt_disable();
    for (int i = 0; i < 8; i++) if (shells[i] && shells[i]->uid == uid) (*nshells)++;
    preempt_enable();
    int idx = 0; proc_t *p;
    while ((p = proc_next(&idx)) != 0) if (p->owner == uid && p->state != PROC_ZOMBIE) (*nprocs)++;
}

static void cmd_fire(const char *args) {
    while (*args == ' ') args++;
    if (!users_is_headchef()) {
        t_puts("  Only the headchef can fire cooks.\n");
        return;
    }
    if (!*args) { t_puts("Usage: fire <name>\n"); return; }
    if (str_eq(args, "headchef")) {
        t_puts("  The headchef cannot be fired.\n");
        return;
    }
    int uid = users_uid_of(args);
    if (uid < 0) { t_puts("  No such cook.\n"); return; }
    int nsh, npr;
    still_at_work((uint8_t)uid, &nsh, &npr);
    if (nsh || npr) {
        t_printf("  %s is still at work (%d shell%s, %d process%s): clock them out first.\n",
                 args, nsh, nsh == 1 ? "" : "s", npr, npr == 1 ? "" : "es");
        return;
    }
    if (users_remove(args) != 0) { t_puts("  Could not fire (disk error).\n"); return; }
    clip_forget((uint8_t)uid);          /* the next hire into this uid starts clean */
    int moved = hand_over("/", (uint8_t)uid, 0);
    int keys  = ssh_forget_keys(args);
    t_printf("  Fired: %s has left the kitchen; %d of their files and bowls are the headchef's now.\n", args, moved);
    if (keys > 0)  t_printf("  %d SSH key%s for %s removed from /AUTHKEYS.\n", keys, keys == 1 ? "" : "s", args);
    if (keys < 0)  t_printf("  Could not rewrite /AUTHKEYS: remove %s's keys by hand.\n", args);
    klog("[users] fired %s (uid %d): %d entries handed to the headchef, %d keys removed\n", args, uid, moved, keys);
}

/* perms <file>            - show owner + rwx
 * perms <file> <rwxrwx>   - set permissions (owner or headchef only) */
static void cmd_perms(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { t_puts("Usage: perms <file> [rwxrwx]\n"); return; }

    char arg_name[FAT_PATH_MAX];
    const char *spec = first_word(args, arg_name, FAT_PATH_MAX);   /* quotes allowed (v0.57.4) */

    char path[FAT_PATH_MAX];
    resolve_path(arg_name, path);
    uint8_t owner, mode;
    if (fat_stat(path, &owner, &mode) < 0) {
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("  Not found: %s\n", arg_name);
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    if (*spec) {
        if (strlen(spec) < 6) {
            t_puts("  Spec must be 6 chars, e.g. rwxr-x (owner then cooks)\n");
            return;
        }
        if (users_current_uid() != owner && !users_is_headchef()) {
            t_puts("  Only the owner or headchef may re-plate permissions.\n");
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
            t_puts("  Could not change permissions (FAT32 or disk error).\n");
            return;
        }
        mode = m | FAT_PERM_MARK;
        t_puts("  Re-plated.\n");
    }

    char s[7];
    s[0] = (mode & FAT_PERM_OR) ? 'r' : '-';
    s[1] = (mode & FAT_PERM_OW) ? 'w' : '-';
    s[2] = (mode & FAT_PERM_OX) ? 'x' : '-';
    s[3] = (mode & FAT_PERM_AR) ? 'r' : '-';
    s[4] = (mode & FAT_PERM_AW) ? 'w' : '-';
    s[5] = (mode & FAT_PERM_AX) ? 'x' : '-';
    s[6] = '\0';
    t_printf("  %s\n", path);
    t_printf("    owner: %s   perms: %s  (owner | cooks)\n",
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
        t_puts("  headchef's secret: ");
        read_line(secret, sizeof(secret), '*');
        int ok = users_check("headchef", secret) == 0;
        memset(secret, 0, sizeof(secret));
#ifdef NO_CHALLENGE
        /* chef asks for the headchef's secret, so it is a door like the
         * others (v0.55.2). Measured on v0.55.1: 137 wrong guesses a minute
         * from one SSH session, none recorded. Now each one is recorded as
         * "chef:<cook>", and costs 1, 2, 4, then 8 s, counted per cook - not
         * per session, so opening more sessions does not reset it. A right
         * secret clears the count. CHALLENGE keeps the old chef: its stage 2
         * is one chef with a cracked preimage, and that build stays as it
         * was. */
        static uint8_t chef_fails[256];
        char where[16];
        strcpy(where, "chef:");
        strncpy(where + 5, users_current_name(), sizeof(where) - 6); where[sizeof(where) - 1] = '\0';
        logins_record("headchef", where, ok);
        if (ok) chef_fails[saved] = 0;
        else {
            uint32_t pause = 1000u << (chef_fails[saved] < 3 ? chef_fails[saved] : 3);
            if (chef_fails[saved] < 255) chef_fails[saved]++;
            klog("[users] %s: wrong headchef secret at chef, %u ms pause\n", users_current_name(), pause);
            task_sleep(pause);
        }
#endif
        if (!ok) {
            t_color(VGA_LIGHT_RED, VGA_BLACK);
            t_puts("  The headchef shakes their head. Denied.\n");
            t_color(VGA_LIGHT_GREY, VGA_BLACK);
            return;
        }
    }
    set_session_uid(0);                            /* elevate for one command */
    dispatch(args);
    set_session_uid(saved);                        /* drop back down */
}

static void cmd_hash(const char *args) {
    if (!args || !args[0]) {
        t_puts("Usage: hash <text>\n"); return;
    }
    uint32_t h = alphasoup_hash(args, (uint32_t)strlen(args));
    t_color(VGA_LIGHT_CYAN, VGA_BLACK);
    t_printf("  AlphaSOUP-32:  0x%08x  (%u)\n", h, h);
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void cmd_beep(const char *args) {
    if (!args || !args[0]) {
        t_puts("Usage: beep <freq> [ms]\n"); return;
    }
    uint32_t freq = (uint32_t)parse_num(args);
    while (*args && *args != ' ') args++;
    while (*args == ' ') args++;
    uint32_t ms = *args ? (uint32_t)parse_num(args) : 200;
    if (freq == 0) { t_puts("  freq must be > 0\n"); return; }
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

    t_color(VGA_LIGHT_GREY, VGA_BLACK);
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
                t_color(VGA_BLUE, VGA_BLACK);
                t_putc('@');
                t_color(VGA_LIGHT_GREY, VGA_BLACK);
            } else {
                /* Shade by depth - map 0..MB_ITER-1 to palette */
                t_putc(pal[it * plen / MB_ITER]);
            }
        }
        t_putc('\n');
    }
#undef MB_W
#undef MB_H
#undef MB_ITER
#undef MB_SC
}

static void cmd_vgademo(void) {
    if (needs_console("vgademo")) return;
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
    t_getc();

    vga13h_exit();
    /* Resync hardware cursor with where the text driver left it */
    t_cursor(t_row(), t_col());
}

/* Busy-wait for n timer ticks (10 ms each at 100 Hz). */
static void tick_delay(uint32_t ticks) {
    /* Cooperative delay: one PIT tick is 10 ms. task_sleep marks this
     * task BLOCKED so background tasks run while wipe/bounce animate. */
    task_sleep(ticks * 10);
}

static void cmd_wipe(void) {
    if (needs_console("wipe")) return;
    /* Sweep a colored bar top->bottom, then clear. */
    t_color(VGA_BLACK, VGA_LIGHT_CYAN);
    for (int row = 0; row < 25; row++) {
        vga_fill_row(row, ' ');
        tick_delay(2);
    }
    t_color(VGA_BLACK, VGA_CYAN);
    for (int row = 0; row < 25; row++) {
        vga_fill_row(row, ' ');
        tick_delay(1);
    }
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    t_clear();
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
        t_color(VGA_DARK_GREY, VGA_BLACK);
        t_puts("  No FORTUNE.TXT on disk. Create one with:\n");
        t_puts("    stir FORTUNE.TXT A watched pot never boils.\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    if (fsize > sizeof(fbuf) - 1) fsize = sizeof(fbuf) - 1;   /* fat_read fills at most bufsize but reports the FILE's size */
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
        t_puts("  (FORTUNE.TXT is empty)\n");
        return;
    }

    int pick = (int)(rng_next() % (uint32_t)count);
    int seen = 0;
    for (uint32_t i = 0; i < fsize;) {
        uint32_t s = i;
        while (i < fsize && fbuf[i] != '\n') i++;
        if (i > s) {
            if (seen == pick) {
                t_color(VGA_LIGHT_CYAN, VGA_BLACK);
                t_puts("  ");
                for (uint32_t k = s; k < i; k++) {
                    char c = (char)fbuf[k];
                    if (c == '\r') continue;
                    t_putc(c);
                }
                t_putc('\n');
                t_color(VGA_LIGHT_GREY, VGA_BLACK);
                return;
            }
            seen++;
        }
        if (i < fsize) i++;
    }
}

static void cmd_clock(void) {
    /* Drain any pending input so ENTER doesn't exit immediately */
    t_flush_in();
    t_clear();

    t_color(VGA_LIGHT_CYAN, VGA_BLACK);
    t_cursor(6, 33); t_puts("soupOS clock");
    t_color(VGA_DARK_GREY, VGA_BLACK);
    t_cursor(7, 33); t_puts("============");
    t_color(VGA_DARK_GREY, VGA_BLACK);
    t_cursor(16, 28); t_puts("Press any key to exit.");

    rtc_time_t prev = {0};
    while (!t_avail()) {
        rtc_time_t t;
        rtc_read(&t);
        if (t.second != prev.second || t.minute != prev.minute ||
            t.hour   != prev.hour   || t.day    != prev.day) {
            t_color(VGA_WHITE, VGA_BLACK);
            t_cursor(10, 35);
            t_printf("%02u:%02u:%02u", t.hour, t.minute, t.second);
            t_color(VGA_LIGHT_GREY, VGA_BLACK);
            t_cursor(12, 34);
            t_printf("%u-%02u-%02u", (uint32_t)t.year, t.month, t.day);
            prev = t;
        }
        /* Yield so background tasks run, then park until the next IRQ. */
        task_yield();
        if (!t_avail())
            cpu_halt();
    }
    t_flush_in();
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    t_clear();
}

/* ---- task/scheduler commands ---- */

/* A right-aligned 5-wide number into a buffer, since t_printf's %5u would
 * do it but the dash for "no figure" has to line up with it too. */
static void fmt_u5(char out[8], uint32_t v) {
    char d[11]; int n = 0;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 10);
    int pad = 5 - n, k = 0;
    while (pad-- > 0) out[k++] = ' ';
    while (n) out[k++] = d[--n];
    out[k] = '\0';
}

static void cmd_ps(void) {
    task_t *head = task_list_head();
    if (!head) { t_puts("(no tasks)\n"); return; }
    t_color(VGA_YELLOW, VGA_BLACK);
    t_puts("  ID  ST  CPU%  STACK  KIND    RES  SWAP  OWNER     NAME\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    task_t *t = head;
    do {
        /* cpu_last is ticks out of a 100-tick second, so it is already a
         * percentage. It says who HELD the cpu: a task halted at a prompt
         * still counts, which is why the shell reads high when idle. */
        /* A program's memory in pages: resident (the cap is 1024) and on
         * disk now. Kernel tasks live in the kernel's memory: no figure. */
        char res[8] = "    -", sw[8] = "    -";
        if (t->proc) { fmt_u5(res, t->proc->upages); fmt_u5(sw, t->proc->swapped); }
        t_printf("  %2u   %c  %3u%%  %5u  %s  %s %s  %-8s  %s%s\n",
                   t->id,
                   task_state_letter(t->state),
                   t->cpu_last > 100 ? 100 : t->cpu_last,
                   t->stack_size,
                   t->proc ? "ring3" : "task ",
                   res, sw,
                   t->proc ? users_name_of(t->proc->owner) : "-",
                   t->name,
                   (t == task_current()) ? " *" : "");
        t = t->next;
    } while (t != head);
    t_printf("  idle %u%% of the last second\n", task_idle_last());
}

/* kitchen - the whole room, redrawn in place once a second until a key.
 *
 * Draws with the serial mirror off, so neither the log nor a remote session
 * is fed a frame per second; one summary line goes to klog on the way out,
 * with this task's own share in it. That number is the thing to watch: a
 * viewer that spends its time redrawing itself is measuring itself. It
 * sleeps a whole second between frames, so it should read 0%. */
static void kitchen_frame(uint32_t frames) {
    int w = t_cols(), rows = t_rows();
    uint32_t up = timer_get_seconds(), ip = net_ip();

    t_clear();
    t_color(VGA_YELLOW, VGA_BLACK);
    t_printf("  soupOS kitchen   up %u:%02u:%02u   ip %u.%u.%u.%u   console %dx%d   frame %u   (any key to leave)\n",
               up / 3600, (up / 60) % 60, up % 60,
               (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255, w, rows, frames);
    t_color(VGA_LIGHT_GREY, VGA_BLACK);

    uint32_t idle = task_idle_last();
    t_printf("\n  cpu     idle %u%%  busy %u%%        tasks %u alive, %u yours        processes %u of %d\n",
               idle > 100 ? 100 : idle, idle > 100 ? 0 : 100 - idle,
               task_count_alive(), task_count_users(), proc_count_live(), PROC_MAX);
    t_printf("  memory  %u of %u KB in pages      heap %u of %u KB      swap %u of %u pages\n",
               pmm_used_kb(), pmm_total_kb(), heap_used_bytes() / 1024, heap_total_bytes() / 1024,
               swap_slots_used(), swap_slots_total());
    uint32_t dfree = 0, dtotal = 0, dcl = 0;
    fat_space(&dfree, &dtotal, &dcl);
    t_printf("  disk    %u of %u KB free\n", (dfree * dcl) / 1024, (dtotal * dcl) / 1024);

    t_puts("  net     listening");
    int any = 0;
    for (int i = 0; i < TCP_MAX_LISTEN; i++)
        if (tcp_listener_at(i)) { t_printf(" %u", tcp_listener_at(i)); any = 1; }
    if (!any) t_puts(" (nothing)");
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        tcp_state_t st; uint32_t pip; uint16_t pport, lport;
        if (!tcp_conn_at(i, &st, &pip, &pport, &lport)) continue;
        uint32_t tx, rx;
        tcp_conn_bytes(i, &tx, &rx);
        t_printf("\n          %s %u.%u.%u.%u:%u -> :%u   sent %u, received %u", tcp_state_name(st),
                   (pip >> 24) & 255, (pip >> 16) & 255, (pip >> 8) & 255, pip & 255, pport, lport, tx, rx);
    }
    t_puts("\n\n");

    t_color(VGA_YELLOW, VGA_BLACK);
    t_puts("  ID  ST  CPU%  STACK  KIND    RES  SWAP  OWNER     NAME\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    task_t *head = task_list_head(), *t = head;
    if (head) do {
        if (t_row() >= rows - 1) break;              /* keep it on one screen */
        char res[8] = "    -", sw[8] = "    -";
        if (t->proc) { fmt_u5(res, t->proc->upages); fmt_u5(sw, t->proc->swapped); }
        t_printf("  %2u   %c  %3u%%  %5u  %s  %s %s  %-8s  %s%s\n", t->id, task_state_letter(t->state),
                   t->cpu_last > 100 ? 100 : t->cpu_last, t->stack_size,
                   t->proc ? "ring3" : "task ", res, sw,
                   t->proc ? users_name_of(t->proc->owner) : "-", t->name, t == task_current() ? " *" : "");
        t = t->next;
    } while (t != head);
}

static void cmd_kitchen(void) {
    uint32_t frames = 0;
    t_flush_in();
    while (!t_avail()) {
        vga_set_mirror(0);
        kitchen_frame(frames);
        vga_set_mirror(1);
        frames++;
        task_sleep(1000);
    }
    t_getc();
    t_clear();
    klog("[kitchen] %u frames, idle %u%%, self %u%%\n",
         frames, task_idle_last(), task_current()->cpu_last);
    t_printf("  left the kitchen after %u frames (idle %u%%, this shell %u%%).\n",
               frames, task_idle_last(), task_current()->cpu_last);
}

static void cmd_kill(const char *args) {
    while (*args == ' ') args++;
    if (!*args) { t_puts("Usage: kill <pid> | kill %  (newest process)\n"); return; }
    /* `kill %` targets the newest live process, the way a shell's %1 does.
     * Saves reading `jobs` for a pid, and lets a script kill what it just
     * started without knowing the number. */
    if (*args == '%') {
        proc_t *last = proc_most_recent_on(TERM_KEY);
        if (!last) { t_puts("kill: no live process\n"); return; }
        if (!may_signal(last)) { not_yours(last->pid); return; }
        proc_kill(last->pid);
        t_printf("killing process %u (%s)\n", last->pid, last->name);
        return;
    }

    int id = parse_num(args);
    proc_t *target = proc_by_pid((uint32_t)id);
    if (target && (target->state == PROC_RUNNING || target->state == PROC_STOPPED) && !may_signal(target)) {
        not_yours((uint32_t)id); return;
    }
    /* A pid IS a task id, so try the process path first: it flags the program
     * and lets it unwind through its own teardown. Marking the task DEAD
     * instead would leak the whole address space, because the loader would
     * never return to free it. */
    if (proc_kill((uint32_t)id) == 0) {
        t_printf("killing process %d\n", id);
        return;
    }
    /* Not a program: a kernel task (the vault's worker, a service). Only the
     * headchef may kill one of those. */
    if (!users_is_headchef()) {
        t_puts("  only the headchef may kill a kernel task\n");
        klog("[proc] %s may not kill task %d\n", users_current_name(), id);
        return;
    }
    int r = task_kill((uint32_t)id);
    if (r == 0) t_printf("killed task %d\n", id);
    else        t_printf("kill: %d not killable (unknown, self, or kernel)\n", id);
}

/* Background clock - writes HH:MM:SS directly to the VGA text buffer
 * at row 0, cols 72-79. Doesn't touch vga.c's tracked cursor/color so
 * the shell keeps working underneath. Exits when the task is killed. */
static void bgclock_task(void *arg) {
    (void)arg;
    volatile uint16_t *vga = vga_cells();   /* 0xB8000 is dead on FB boots */
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
    if (needs_console("bgclock")) return;
    task_t *t = task_spawn("bgclock", bgclock_task, 0);
    if (!t) { t_puts("bgclock: out of memory\n"); return; }
    t_printf("bgclock running as task %u - stop with: kill %u\n", t->id, t->id);
}

/* yieldbg - spawn a background task that logs a [bg] marker to the serial
 * port every ~300 ms for ~6 s, then exits. It only makes progress when some
 * other task yields the CPU. Run it, then `cook whisk.elf`: the [bg] and [u]
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
    if (!t) { t_puts("yieldbg: out of memory\n"); return; }
    t_printf("yieldbg running as task %u -- markers go to serial (dmesg)\n", t->id);
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
    /* Read the accumulator, so the compiler can see the loop mattered and
     * the log shows it actually ran rather than being elided. */
    klog("[spin] done (burn checksum %u)\n", sink);
}

static void cmd_spinbg(void) {
    task_t *t = task_spawn("spinbg", spinbg_task, 0);
    if (!t) { t_puts("spinbg: out of memory\n"); return; }
    t_printf("spinbg running as task %u -- busy-loops ~4s WITHOUT yielding\n", t->id);
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
    t_printf("sleeptest bg: tick=%u\n", timer_get_ticks());
    task_sleep(200);
    t_printf("sleeptest bg: tick=%u (after 200 ms sleep)\n", timer_get_ticks());
    /* task exits naturally */
}

static void cmd_sleeptest(void) {
    uint32_t t0 = timer_get_ticks();
    t_printf("sleeptest: start tick=%u\n", t0);

    task_t *bg = task_spawn("slptest_bg", sleeptest_bg, 0);
    if (!bg) t_puts("sleeptest: warning: could not spawn bg task\n");

    task_sleep(500);

    uint32_t t1    = timer_get_ticks();
    uint32_t delta = t1 - t0;
    t_printf("sleeptest: woke  tick=%u delta=%u (expect ~50)\n", t1, delta);
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
        t_puts("  /dev/serial : wrote a line (check the serial log)\n");
    } else {
        t_puts("  /dev/serial : open FAILED\n");
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
        t_printf("  /dev/zero   : read %d bytes, all-zero=%s\n",
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
            t_printf("  VFSTEST.TXT : wrote %u, read back %d bytes: %s",
                       (uint32_t)strlen(payload), r, buf);
            vfs_close(rd);
        } else {
            t_puts("  VFSTEST.TXT : reopen for read FAILED\n");
        }
    } else {
        t_puts("  VFSTEST.TXT : open for write FAILED\n");
    }

    /* 4. size an existing file via the VFS. */
    vfs_node_t *f = vfs_open("README.TXT", VFS_RDONLY);
    if (f) {
        t_printf("  README.TXT  : vfs_size() reports %u bytes\n",
                   vfs_size(f));
        vfs_close(f);
    } else {
        t_puts("  README.TXT  : not found (skipped)\n");
    }
}

/* dmesg - dump the kernel log ring buffer (boot messages, panics). */
static void cmd_dmesg(void) {
    static char buf[KLOG_SIZE + 1];
    uint32_t n = klog_copy(buf, KLOG_SIZE);
    buf[n] = 0;
    t_color(VGA_DARK_GREY, VGA_BLACK);
    t_printf("  --- kernel log: %u bytes ---\n", n);
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    t_puts(buf);
    if (n == 0 || buf[n - 1] != '\n') t_putc('\n');
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
    if (month < 1 || month > 12) { t_puts("  cal: bad month\n"); return; }

    int dim = days_in_month(year, month);
    int first_dow = day_of_week(year, month, 1);

    /* Header: centered "Month Year" in a 20-char-wide calendar body */
    t_color(VGA_YELLOW, VGA_BLACK);
    const char *mn = month_name[month - 1];
    int mn_len = (int)strlen(mn);
    /* "Month YYYY" length = mn_len + 5. Center in 20 cols. */
    int total_len = mn_len + 5;
    int pad = (20 - total_len) / 2;
    if (pad < 0) pad = 0;
    for (int i = 0; i < pad; i++) t_putc(' ');
    t_printf("%s %u\n", mn, (uint32_t)year);

    t_color(VGA_LIGHT_CYAN, VGA_BLACK);
    t_puts("Su Mo Tu We Th Fr Sa\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);

    /* Leading blanks for days before the 1st */
    for (int i = 0; i < first_dow; i++) t_puts("   ");

    int col = first_dow;
    for (int d = 1; d <= dim; d++) {
        if (d == today) {
            t_color(VGA_BLACK, VGA_WHITE);
            t_printf("%2d", d);
            t_color(VGA_LIGHT_GREY, VGA_BLACK);
            t_putc(' ');
        } else {
            t_printf("%2d ", d);
        }
        col++;
        if (col == 7) {
            t_putc('\n');
            col = 0;
        }
    }
    if (col != 0) t_putc('\n');
}

static void cmd_uname(const char *args) {
    int show_all = args && (args[0] == '-' && args[1] == 'a');
    if (show_all) {
        t_puts("soupOS 0.7.6 i686 soup-kernel protected-mode\n");
    } else {
        t_puts("soupOS\n");
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

    t_flush_in();
    t_clear();

    /* Static decorations */
    t_color(VGA_YELLOW, VGA_BLACK);
    t_cursor(2, 32); t_puts("* MAXWELL *");
    t_color(VGA_DARK_GREY, VGA_BLACK);
    t_cursor(3, 30); t_puts("~~~~~~~~~~~~~~~");

    t_color(VGA_LIGHT_RED, VGA_BLACK);
    t_cursor(19, 28); t_puts("you cannot escape him");

    int frame = 0;
    int top_row = 7;
    int left_col = (80 - CAT_WIDTH) / 2;
    int escaped = 0;
    uint32_t last = timer_get_ticks();

    while (1) {
        uint32_t now = timer_get_ticks();
        if (now - last >= 12) {  /* ~120ms per frame */
            t_color(VGA_LIGHT_CYAN, VGA_BLACK);
            for (int row = 0; row < CAT_LINES; row++) {
                t_cursor(top_row + row, left_col);
                t_puts(frames[frame][row]);
            }

            /* Flash the taunt every half-rotation */
            if (frame == 0) {
                t_color(VGA_LIGHT_RED, VGA_BLACK);
                t_cursor(19, 28); t_puts("you cannot escape him");
            } else if (frame == 4) {
                t_color(VGA_BLACK, VGA_LIGHT_RED);
                t_cursor(19, 28); t_puts("YOU CANNOT ESCAPE HIM");
            }

            frame = (frame + 1) % N_CAT_FRAMES;
            last = now;
        }

        /* Swallow anything the user types - except ESC. The gag is that he
         * ignores you; an unescapable command is a different thing. On a
         * hosted CTF instance it costs the player their box, and `cat` is
         * exactly what someone types expecting Unix cat (soupOS spells that
         * `pour`). */
        while (t_avail()) {
            if (t_getc() == 27) { escaped = 1; break; }
        }
        if (escaped) break;
        /* Yield so background tasks keep ticking, then park for an IRQ. */
        task_yield();
        cpu_halt();
    }
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    t_clear();
    t_puts("  ...he lets you go, this time.\n");
    #undef N_CAT_FRAMES
    #undef CAT_LINES
    #undef CAT_WIDTH
}

static void cmd_bounce(void) {
    if (needs_console("bounce")) return;
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

    t_flush_in();
    vga13h_enter();
    vga13h_defpal();

    while (!t_avail()) {
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

    t_flush_in();
    vga13h_exit();
    t_cursor(t_row(), t_col());
    #undef BALLS
}

static void cmd_recipe(void) {
    t_color(VGA_LIGHT_CYAN, VGA_BLACK);
    t_puts("  soupOS  v0.7.6\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    t_puts("  Architecture:  i686 (IA-32, protected mode)\n");
    t_puts("  Kernel base:   0x00100000\n");
    t_puts("  Features:\n");
    t_puts("    GDT  IDT  PIC  PIT  PS/2  VGA\n");
    t_puts("    PMM  Paging (identity-map)  Heap\n");
    t_puts("    RTC  PCI scan  CPUID\n");
    t_puts("    ATA PIO  FAT16 read+write + bowls (subdirs)  PC speaker\n");
    t_puts("    Users + access control (headchef/cooks)  chef elevation\n");
    t_puts("    Cooperative scheduler  VFS + /dev nodes\n");
    t_puts("    Kernel log ring (dmesg)  register-dump panic\n");
    t_puts("    soupyc (arrays/for/file-I/O/stdlib)  Tab completion\n");
    t_puts("    AlphaSOUP-32 hash  ASCII Mandelbrot\n");
    t_printf("  Free RAM:      %u KB\n", pmm_free_kb());
    t_printf("  Uptime:        %u seconds\n", timer_get_seconds());
    if (fat_get_type()) {
        t_printf("  Disk volume:   FAT%d [%s]\n", fat_get_type(), fat_label());
        t_printf("  Disk size:     %u MB\n", ata_total_sectors() / 2048);
    }
}

static void cmd_leftovers(void) {
    if (cur_shell()->hist_size == 0) { t_puts("  No leftovers yet.\n"); return; }
    t_color(VGA_YELLOW, VGA_BLACK); t_puts("  Leftovers (history):\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (int i = cur_shell()->hist_size; i >= 1; i--) {
        const char *h = hist_get(i);
        if (h) t_printf("    %2d  %s\n", cur_shell()->hist_size - i + 1, h);
    }
}

static void cmd_spill(void) {
    t_puts("Spilling the soup...\n");
    volatile int zero = 0;
    volatile int x = 1 / zero;
    (void)x;
}

static void cmd_freeze(void) {
    t_color(VGA_LIGHT_RED, VGA_BLACK);
    t_puts("Soup frozen. Reset to reheat.\n");
    __asm__ volatile ("cli; hlt");
    while (1) {}
}

static void cmd_reheat(void) {
    t_puts("Reheating...\n");
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
 * Doom's menus call t_getc(), which yields. Wrapping the session
 * would leak the lock into an unrelated task the first time a player paused on
 * a menu. See docs/doom-reenable-plan.md. */
static void cmd_doom(void) {
    if (needs_console("doom")) return;
    if (task_count_users() > 1) {
        t_color(VGA_YELLOW, VGA_BLACK);
        t_puts("  Doom needs the machine to itself.\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        t_puts("  Stop your background tasks first:  ps, then kill <id>\n");
        return;
    }
    if (!fat_exists("DOOM1.WAD")) {
        t_color(VGA_YELLOW, VGA_BLACK);
        t_puts("  No DOOM1.WAD on disk.\n");
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        t_puts("  Put the shareware WAD next to the Makefile and run: make wad\n");
        return;
    }
    doom_menu_run();
    t_clear();
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
    if (!bg) { t_puts("preempttest: out of memory\n"); return; }

    uint32_t a = pt_spin(30);                 /* A: preemption on   */
    preempt_disable();
    uint32_t b = pt_spin(30);                 /* B: must be frozen  */
    preempt_enable();
    uint32_t c = pt_spin(30);                 /* C: moving again    */

    pt_stop = 1;
    task_yield();                             /* let bg observe the flag */

    t_printf("  A preempt on   : %u\n", a);
    t_printf("  B disabled     : %u\n", b);
    t_printf("  C re-enabled   : %u\n", c);
    t_printf("  depth after    : %d\n", task_current()->preempt_depth);
    klog("[pt] A=%u B=%u C=%u depth=%d\n", a, b, c,
         task_current()->preempt_depth);

    int ok = (a > 0) && (b == 0) && (c > 0) &&
             (task_current()->preempt_depth == 0);
    t_color(ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    t_puts(ok ? "  [pass] preempt_disable suppresses and balances\n"
                : "  [FAIL] preempt_disable is not behaving\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
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
    t_printf("  two tasks, %d write+verify cycles each...\n", FS_ITERS);
    task_t *bg = task_spawn("fatstress", fs_bg, 0);
    if (!bg) { t_puts("fatstress: out of memory\n"); return; }

    int fg = fs_cycle("/FSA.TXT", 'A');
    while (!fs_bg_done) { task_yield(); cpu_halt(); }

    t_printf("  foreground mismatches: %d\n", fg);
    t_printf("  background mismatches: %d\n", fs_bg_errors);
    int ok = (fg == 0 && fs_bg_errors == 0);
    t_color(ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    t_puts(ok ? "  [pass] concurrent FAT access stayed consistent\n"
                : "  [FAIL] FAT state was corrupted by interleaving\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
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
    for (int i = 0; i < n; i++) { t_puts(line); task_yield(); }
}

static void vs_bg(void *arg) { (void)arg; vs_emit('B', VS_LINES); vs_done = 1; }

static void cmd_vgastress(void) {
    vs_done = 0;
    klog("[vgastress] begin\n");
    task_t *bg = task_spawn("vgastress", vs_bg, 0);
    if (!bg) { t_puts("vgastress: out of memory\n"); return; }
    vs_emit('A', VS_LINES);
    while (!vs_done) { task_yield(); cpu_halt(); }
    klog("[vgastress] end\n");
    t_puts("  vgastress done - check the transcript for mixed lines\n");
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
    if (!pdir) { t_puts("vmtest: could not allocate a directory\n"); return; }
    t_printf("  kernel dir %p   new dir %p\n", (void *)kdir, (void *)pdir);

    int mapped_before = paging_is_mapped(VM_PROBE);

    paging_switch(pdir);
    t_puts("  switched CR3 to the new space (kernel still running)\n");

    /* Map a user page here and leave a marker in it. */
    void *frame = pmm_alloc_page();
    if (!frame) { paging_switch(kdir); paging_free_dir(pdir);
                  t_puts("vmtest: out of frames\n"); return; }
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

    t_printf("  probe mapped in kernel dir before : %d (want 0)\n", mapped_before);
    t_printf("  probe mapped in new dir           : %d (want 1)\n", in_new);
    t_printf("  marker read back in new dir       : 0x%x\n", readback);
    t_printf("  probe mapped in kernel dir after  : %d (want 0)\n", in_kernel);
    t_printf("  marker survives a round trip      : 0x%x\n", still);
    t_printf("  probe mapped after free           : %d (want 0)\n", after_free);

    fail = (mapped_before != 0) || (in_new != 1) || (in_kernel != 0) ||
           (readback != 0x50555000u) || (still != 0x50555000u) || (after_free != 0);
    t_color(fail ? VGA_LIGHT_RED : VGA_LIGHT_GREEN, VGA_BLACK);
    t_puts(fail ? "  [FAIL] address spaces are not isolated\n"
                  : "  [pass] second address space is real and isolated\n");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
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
    "freeze", "reheat", "ps", "kitchen", "brigade", "logbook", "portions", "follow", "discard", "take", "orders", "plate", "steep", "hand", "kill", "scraps", "sample", "skewer", "larder", "whistle", "hatch", "hush", "pass", "vault",
#ifndef NO_DOOM
    "sizzle", "hum",
#endif
    "faucet", "plumbing", "table", "sip", "sniff", "reserve", "holler",
    "takeout", "bgclock", "yieldbg", "spinbg", "sleeptest", "vfstest",
    "dmesg", "pwd", "cd", "mkbowl", "rmbowl", "bowltest",
    "whoami", "roster", "clockout", "hire", "fire", "perms", "secret",
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

static char lower_c(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int str_ieq(const char *a, const char *b) {
    int l = (int)strlen(b);
    return (int)strlen(a) == l && prefix_match_ci(a, b, l);
}

/* Tab (v0.60.10). The word before the cursor is finished from what fits
 * where it stands:
 *   - a command's first word (the line's, or after ; && ||): the builtins;
 *   - the word after `cook`, or after a | : programs, the .ELF files in the
 *     root (where cook looks) and in the current bowl;
 *   - anything else, and any word with a / in it: a path, from its bowl,
 *     which is only listed if the cook may read it (a private home's names
 *     are not given away by Tab any more than by peek).
 * One fit: the rest of it, then '/' after a bowl or a space after anything
 * else. More than one: as much as they all share; a Tab that has nothing
 * more to add lists them. FAT ignores case, so what a person typed in
 * lowercase is finished in lowercase when the fit is an all-capitals 8.3
 * name (cook fl<Tab> gives flip.elf, not flIP.ELF). */
#define TAB_MAX (FAT_LS_MAX * 2 + 128)
typedef struct {
    char        name[TAB_MAX][FAT_LFN_MAX];
    uint8_t     is_dir[TAB_MAX];
    int         n;
    fat_entry_t ents[FAT_LS_MAX];
} tab_fits_t;

static void tab_add(tab_fits_t *t, const char *name, int is_dir) {
    if (t->n >= TAB_MAX) return;
    for (int i = 0; i < t->n; i++)              /* the root and the bowl can both hold it */
        if (str_ieq(t->name[i], name)) return;
    strncpy(t->name[t->n], name, FAT_LFN_MAX - 1);
    t->name[t->n][FAT_LFN_MAX - 1] = '\0';
    t->is_dir[t->n++] = (uint8_t)is_dir;
}

/* Every entry of the bowl at `dir` starting with leaf[0..leaf_len); only
 * .ELF files when `programs`. */
static void tab_from_bowl(tab_fits_t *t, const char *dir, const char *leaf, int leaf_len, int programs) {
    if (!may(dir, 'r')) return;
    int n = fat_ls(dir, t->ents, FAT_LS_MAX);
    for (int i = 0; i < n; i++) {
        const char *nm = fat_display_name(&t->ents[i]);
        int dirent = (t->ents[i].attr & FAT_ATTR_DIR) != 0;
        if (str_eq(nm, ".") || str_eq(nm, "..")) continue;
        if (programs) {
            int l = (int)strlen(nm);
            if (dirent || l < 4 || !str_ieq(nm + l - 4, ".elf")) continue;
        }
        if (prefix_match_ci(nm, leaf, leaf_len)) tab_add(t, nm, dirent);
    }
}

static void shell_tab_complete(char *input, int *pos, int *len) {
    shell_t *sh = cur_shell();
    int tabs = ++sh->tabs;

    /* The word: back to a space or a character that ends one. */
    int ws = *pos;
    while (ws > 0 && input[ws - 1] != ' ' && !strchr("|;&<>", input[ws - 1])) ws--;
    const char *word = input + ws;
    int word_len = *pos - ws;

    /* Where it stands. */
    enum { AT_CMD, AT_PROG, AT_PATH } at = AT_PATH;
    int j = ws;
    while (j > 0 && input[j - 1] == ' ') j--;
    if (j == 0) at = AT_CMD;
    else if (input[j - 1] == '<' || input[j - 1] == '>') at = AT_PATH;
    else if (input[j - 1] == '|') at = (j >= 2 && input[j - 2] == '|') ? AT_CMD : AT_PROG;
    else if (input[j - 1] == ';' || input[j - 1] == '&') at = AT_CMD;
    else {
        int k = j;
        while (k > 0 && input[k - 1] != ' ') k--;
        char prev[12];
        int pl = j - k < (int)sizeof(prev) - 1 ? j - k : (int)sizeof(prev) - 1;
        memcpy(prev, input + k, (size_t)pl); prev[pl] = '\0';
        if (str_eq(prev, "cook")) at = AT_PROG;
        else if (str_eq(prev, "then") || str_eq(prev, "do") || str_eq(prev, "else")) at = AT_CMD;
    }
    int slash = -1;
    for (int i = 0; i < word_len; i++) if (word[i] == '/') slash = i;
    if (slash >= 0) at = AT_PATH;

    /* The part being finished is what follows the word's last '/'. */
    const char *leaf = word + slash + 1;
    int leaf_len = word_len - (slash + 1);

    tab_fits_t *t = kmalloc(sizeof(tab_fits_t));
    if (!t) return;
    t->n = 0;
    if (at == AT_CMD) {
        for (int i = 0; i < CMD_COUNT; i++)
            if (prefix_match_ci(cmd_names[i], leaf, leaf_len)) tab_add(t, cmd_names[i], 0);
    } else if (at == AT_PROG) {
        tab_from_bowl(t, "/", leaf, leaf_len, 1);
        if (!str_eq(sh->cwd, "/")) tab_from_bowl(t, sh->cwd, leaf, leaf_len, 1);
    } else {
        char dir[FAT_PATH_MAX];
        if (slash < 0) strcpy(dir, sh->cwd);
        else {
            char part[FAT_PATH_MAX];
            int pl = slash + 1 < (int)sizeof(part) - 1 ? slash + 1 : (int)sizeof(part) - 1;
            memcpy(part, word, (size_t)pl); part[pl] = '\0';
            resolve_path(part, dir);
        }
        if (fat_is_dir(dir)) tab_from_bowl(t, dir, leaf, leaf_len, 0);
    }
    if (t->n == 0) { kfree(t); return; }

    /* As much as every fit shares. */
    int common = (int)strlen(t->name[0]);
    for (int i = 1; i < t->n; i++) {
        int c = 0;
        while (c < common && t->name[i][c] && lower_c(t->name[i][c]) == lower_c(t->name[0][c])) c++;
        common = c;
    }
    int typed_lower = 0, fit_lower = 0;
    for (int i = 0; i < leaf_len; i++) if (leaf[i] >= 'a' && leaf[i] <= 'z') typed_lower = 1;
    for (const char *c = t->name[0]; *c; c++) if (*c >= 'a' && *c <= 'z') fit_lower = 1;
    int fold = typed_lower && !fit_lower;

    if (common > leaf_len) {
        for (int i = leaf_len; i < common; i++) {
            char c = t->name[0][i];
            shell_insert(input, pos, len, fold ? lower_c(c) : c);
        }
        sh->tabs = 1;           /* it did what it could: the next Tab lists */
    }
    if (t->n == 1) {
        if (t->is_dir[0]) shell_insert(input, pos, len, '/');
        else if (*pos == *len || input[*pos] != ' ') shell_insert(input, pos, len, ' ');
        sh->tabs = 0;
    } else if (common <= leaf_len && tabs >= 2) {
        /* Nothing more to add, asked twice: show what fits, then the line again. */
        cursor_to(*len);
        t_putc('\n');
        for (int i = 0; i < t->n; i++) {
            t_puts(t->name[i]);
            if (t->is_dir[i]) t_putc('/');
            t_putc(' ');
        }
        t_putc('\n');
        prompt();   /* updates cur_shell()->prompt_row */
        for (int i = 0; i < *len; i++) t_putc(input[i]);
        cursor_to(*pos);
        sh->tabs = 0;
    }
    kfree(t);
}

/* ---- prompt & dispatch ---- */

/* How a finished background job is announced, just before the next prompt. */
static void report_finished_job(const proc_t *p) {
    t_color(VGA_DARK_GREY, VGA_BLACK);
    if (p->exit_code == PROC_EXIT_KILLED)
        t_printf("  [%u] killed  %s\n", p->pid, p->name);
    else
        t_printf("  [%u] done    %s (exit %d)\n", p->pid, p->name, p->exit_code);
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void prompt(void) {
    /* Collect background jobs that finished while we were away. Done before
     * cur_shell()->prompt_row is recorded, so the report cannot desync the line editor. */
    proc_report_finished_on(TERM_KEY, report_finished_job);

    const char *u = users_current_name();
    cur_shell()->prompt_row = t_row();
    t_color(users_is_headchef() ? VGA_LIGHT_RED : VGA_LIGHT_GREEN, VGA_BLACK);
    t_puts(u);
    t_color(VGA_DARK_GREY,   VGA_BLACK); t_putc('@');
    t_color(VGA_LIGHT_GREEN, VGA_BLACK); t_puts("soupOS");
    t_color(VGA_LIGHT_CYAN,  VGA_BLACK); t_putc(':'); t_puts(cur_shell()->cwd);
    t_color(VGA_WHITE, VGA_BLACK);       t_puts("> ");
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    /* name + "@" + "soupOS" + ":" + cur_shell()->cwd + "> " */
    cur_shell()->prompt_len = (int)strlen(u) + 1 + 6 + 1 + (int)strlen(cur_shell()->cwd) + 2;
}

static void dispatch(const char *buf) {
    const char *p = buf;
    while (*p == ' ') p++;
    if (!*p) return;
    cur_shell()->last_status = 0;

    if      (str_eq(p, "menu"))       cmd_menu();
    else if (str_eq(p, "wash"))       t_clear();
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
    else if (str_eq(p, "spill"))      { if (headchef_only("crash the kernel")) cmd_spill(); }
    else if (str_eq(p, "freeze"))     { if (headchef_only("halt the machine")) { hist_save(); cmd_freeze(); } }
    else if (str_eq(p, "reheat"))     { if (headchef_only("reboot the machine")) { hist_save(); cmd_reheat(); } }
    else if (str_startswith(p, "slurp")) {
        const char *a = p + 5; while (*a == ' ') a++;
        cmd_slurp(a);                           /* its own word splitting (v0.60.38) */
    }
    else if (str_startswith(p, "season")) {
        const char *a = p + 6; while (*a == ' ') a++;
        cmd_season(a);
    }
    else if (str_eq(p, "serve"))       cmd_serve();
    else if (str_eq(p, "follow") || str_startswith(p, "follow ")) cmd_run(p + 6);
    else if (str_startswith(p, "discard ")) cmd_unset(uq_args(p + 8));
    else if (str_eq(p, "hand") || str_startswith(p, "hand ")) cmd_hand(p + 4 + (p[4] == ' '));
    else if (str_eq(p, "break") || str_startswith(p, "break ")) cmd_break(p + 5, 0);
    else if (str_eq(p, "continue") || str_startswith(p, "continue ")) cmd_break(p + 8, 1);
    else if (str_eq(p, "take") || str_startswith(p, "take ")) cmd_read(p + 4);
    else if (str_startswith(p, "pour")) {
        const char *a = p + 4; while (*a == ' ') a++;
        cmd_pour(uq_args(a));
    }
    else if (str_startswith(p, "stir")) {
        const char *a = p + 4; while (*a == ' ') a++;
        cmd_stir(a);
    }
    else if (str_startswith(p, "strain")) {
        const char *a = p + 6; while (*a == ' ') a++;
        cmd_strain(uq_args(a));
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
        cmd_jot(uq_args(a));
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
    else if (str_eq(p, "bowltest"))  { if (headchef_only("run the disk self-tests")) cmd_bowltest(); }
    else if (str_startswith(p, "mkbowl")) {
        const char *a = p + 6; while (*a == ' ') a++;
        cmd_mkbowl(uq_args(a));
    }
    else if (str_startswith(p, "rmbowl")) {
        const char *a = p + 6; while (*a == ' ') a++;
        cmd_rmbowl(uq_args(a));
    }
    else if (str_startswith(p, "cd")) {
        const char *a = p + 2; while (*a == ' ') a++;
        cmd_cd(uq_args(a));
    }
    else if (str_eq(p, "preempttest")) cmd_preempttest();
    else if (str_eq(p, "fatstress"))  { if (headchef_only("run the disk self-tests")) cmd_fatstress(); }
    else if (str_eq(p, "vgastress"))  cmd_vgastress();
    else if (str_eq(p, "vmtest"))     cmd_vmtest();
    else if (str_eq(p, "whoami"))    cmd_whoami();
#ifndef NO_CHALLENGE
    else if (str_eq(p, "special"))   challenge_special();
#endif
    else if (str_eq(p, "roster"))    cmd_roster();
    else if (str_eq(p, "clockout"))  cmd_clockout();
    else if (str_eq(p, "secret") || str_startswith(p, "secret ")) cmd_secret(p + 6);
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
        cmd_hash(uq_args(a));
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
    else if (str_eq(p, "brigade"))     cmd_who();
    else if (str_eq(p, "logbook"))     cmd_last();
    else if (str_eq(p, "portions") || str_startswith(p, "portions ")) cmd_du(uq_args(p + 8));
    else if (str_eq(p, "kitchen"))     cmd_kitchen();
    else if (str_eq(p, "orders"))      cmd_orders();
    else if (str_eq(p, "scraps"))      cmd_scraps();
    else if (str_startswith(p, "sample")) cmd_sample(p + 6);
    else if (str_eq(p, "skewer"))      cmd_skewer();
    else if (str_eq(p, "larder"))      cmd_larder();
    else if (str_startswith(p, "whistle")) cmd_whistle(p + 7);
    else if (str_startswith(p, "hush"))    cmd_hush(p + 4);
#ifndef NO_DOOM
    else if (str_startswith(p, "sizzle"))  cmd_sizzle(p + 6);
    else if (str_startswith(p, "hum"))     cmd_hum(p + 3);
#endif
    else if (str_startswith(p, "hatch"))   cmd_hatch(p + 5);
    else if (str_startswith(p, "pass"))    { if (!has_args(p + 4) || headchef_only("open, close or change the pass")) cmd_pass(p + 4); }
    else if (str_startswith(p, "vault"))   { if (!has_args(p + 5) || headchef_only("open, close or change the vault")) cmd_vault(p + 5); }
    else if (str_eq(p, "faucet"))      cmd_faucet();
    else if (str_eq(p, "table"))       cmd_table();
    else if (str_startswith(p, "sniff"))    { const char *a = p + 5; cmd_sniff(a); }
    else if (str_startswith(p, "reserve")) { const char *a = p + 7; cmd_reserve(a); }
    else if (str_startswith(p, "holler"))  { const char *a = p + 6; cmd_holler(a); }
    else if (str_startswith(p, "takeout")) { const char *a = p + 7; cmd_takeout(a); }
    else if (str_startswith(p, "plumbing")) { const char *a = p + 8; cmd_plumbing(a); }
    else if (str_startswith(p, "sip"))      { const char *a = p + 3; cmd_sip(a); }
    else if (str_startswith(p, "plate")) { const char *a = p + 5; cmd_plate(a); }
    else if (str_startswith(p, "steep")) { const char *a = p + 5; cmd_steep(a); }
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
        t_color(VGA_LIGHT_RED, VGA_BLACK);
        t_printf("No soup for you: '%s'\n", p);
        cur_shell()->last_status = 127;
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

/* A command line may be several commands joined by ; && and || (v0.57.0):
 * `a ; b` runs both, `a && b` runs b only if a's status was 0, `a || b`
 * only if it was not; left to right, as sh groups them, so `a && b || c`
 * runs c when either a or b failed. A single & (background) and a single
 * | (pipe) belong to their command and are not split on. The status of a
 * skipped command is not taken: the last one that ran stands. */
/* NAME=value as the whole of a command (v0.58.0): sets a variable in this
 * shell; the value is unquoted (A="two  words" keeps both spaces). Returns
 * 1 if the text was an assignment. `A=1 cmd` (a variable for one command)
 * is prefix_assigns' (v0.60.41). */
static void unquote(char *w);
static void set_var(const char *name, const char *value);
static int try_assign(const char *text) {
    const char *t = text;
    while (*t == ' ') t++;
    int n = 0;
    if (!(t[0] == '_' || (t[0] >= 'A' && t[0] <= 'Z') || (t[0] >= 'a' && t[0] <= 'z'))) return 0;
    while (t[n] == '_' || (t[n] >= 'A' && t[n] <= 'Z') || (t[n] >= 'a' && t[n] <= 'z') || (t[n] >= '0' && t[n] <= '9')) n++;
    if (t[n] != '=' || n > 15) return 0;
    char value[INPUT_MAX];
    int k = 0; char q = 0;
    for (const char *v = t + n + 1; *v && k < INPUT_MAX - 1; v++) {
        if (!q && *v == ' ') { const char *w = v; while (*w == ' ') w++; if (*w) return 0; break; }
        if (q) { if (*v == q) q = 0; }
        else if (*v == '\'' || *v == '"') q = *v;
        value[k++] = *v;
    }
    value[k] = '\0';
    unquote(value);
    char name[16]; memcpy(name, t, (uint32_t)n); name[n] = '\0';
    set_var(name, value);
    return 1;
}

/* Set a shell variable (assignment, and each turn of a for loop). */
static void set_var(const char *name, const char *value) {
    shell_t *me = cur_shell();
    int slot = -1;
    for (int i = 0; i < me->nvars; i++) if (str_eq(me->vname[i], name)) slot = i;
    if (slot < 0) {
        if (me->nvars >= VARS_MAX) { t_printf("  Too many variables (%d).\n", VARS_MAX); me->last_status = 1; return; }
        slot = me->nvars++;
        strncpy(me->vname[slot], name, sizeof(me->vname[slot]) - 1);
        me->vname[slot][sizeof(me->vname[slot]) - 1] = '\0';
        me->vhand[slot] = 0;
        for (int i = 0; i < me->npend; i++)            /* `hand X` before X was set */
            if (str_eq(me->vpend[i], name)) {
                me->vhand[slot] = 1;
                strcpy(me->vpend[i], me->vpend[--me->npend]);
                break;
            }
    }
    strncpy(me->vval[slot], value, sizeof(me->vval[slot]) - 1);
    me->vval[slot][sizeof(me->vval[slot]) - 1] = '\0';
    me->last_status = 0;
}

/* read NAME... (v0.60.4): a line from the terminal into variables, as sh's
 * read does - one word each, the last NAME taking the rest, spaces around
 * trimmed. At end of input before anything was typed the names are set
 * empty and the status is 1. */
static void cmd_read(const char *args) {
    char names[8][16]; int nn = 0;
    const char *a = args;
    while (*a && nn < 8) { a = first_word(a, names[nn], 16); if (names[nn][0]) nn++; }
    if (!nn) { t_puts("Usage: take NAME...\n"); cur_shell()->last_status = 2; return; }
    char line[INPUT_MAX];
    int len = 0, eof = 0;
    shell_t *me = cur_shell();
    if (me->in_pipe) {
        /* From a pipe (v0.60.34): the line as written, no echo, as sh's read
         * from a pipe. Reaching the end is status 1 even when a last line with
         * no newline was read (its words are still set), as bash's read: so
         * `while take` does not run for that line. */
        for (;;) {
            if (me->in_pos == me->in_len) {
                int n = vfs_read(me->in_pipe, me->in_buf, sizeof(me->in_buf));
                if (n <= 0) { eof = 1; break; }
                me->in_pos = 0; me->in_len = n;
            }
            char c = me->in_buf[me->in_pos++];
            if (c == '\n') break;
            if (c != '\r' && len < INPUT_MAX - 1) line[len++] = c;
        }
    } else
    for (;;) {
        int c = t_getc();
        if (c < 0) { eof = 1; break; }
        if (c == '\n') { t_putc('\n'); break; }
        if (c == '\b') { if (len > 0) { len--; t_putc('\b'); } continue; }
        if (c >= ' ' && c < 127 && len < INPUT_MAX - 1) { line[len++] = (char)c; t_putc((char)c); }
    }
    line[len] = '\0';
    const char *r = line;
    for (int i = 0; i < nn; i++) {
        char word[INPUT_MAX];
        while (*r == ' ') r++;
        if (i == nn - 1) {                       /* the last takes the rest */
            int n = (int)strlen(r);
            while (n > 0 && r[n - 1] == ' ') n--;
            memcpy(word, r, (uint32_t)n); word[n] = '\0';
        } else {
            int n = 0;
            while (*r && *r != ' ' && n < INPUT_MAX - 1) word[n++] = *r++;
            word[n] = '\0';
        }
        set_var(names[i], word);
    }
    cur_shell()->last_status = me->in_pipe ? (eof ? 1 : 0) : ((eof && len == 0) ? 1 : 0);
}

/* unset NAME (v0.58.0). */
static void cmd_unset(const char *args) {
    shell_t *me = cur_shell();
    for (int i = 0; i < me->nvars; i++)
        if (str_eq(me->vname[i], args)) {
            me->nvars--;
            if (i != me->nvars) { strcpy(me->vname[i], me->vname[me->nvars]); strcpy(me->vval[i], me->vval[me->nvars]); me->vhand[i] = me->vhand[me->nvars]; }
            return;
        }
    for (int i = 0; i < me->npend; i++)               /* unset drops the mark, as sh's */
        if (str_eq(me->vpend[i], args)) { strcpy(me->vpend[i], me->vpend[--me->npend]); return; }
}

/* hand NAME[=value]... (v0.60.26): sh's export, under a kitchen name. The
 * variable goes to every program run after, as NAME=value in its
 * environment (ulib's getenv). A name not yet set is marked and handed once
 * it is, as sh does. `hand` alone lists what is handed. */
static void cmd_hand(const char *args) {
    shell_t *me = cur_shell();
    me->last_status = 0;
    if (!*args) {
        for (int i = 0; i < me->nvars; i++)
            if (me->vhand[i]) t_printf("  %s=%s\n", me->vname[i], me->vval[i]);
        return;
    }
    const char *a = args;
    while (*a) {
        char w[INPUT_MAX];
        a = first_word(a, w, sizeof(w));
        if (!w[0]) break;
        char *eq = strchr(w, '=');
        if (eq) { *eq = '\0'; }
        int bad = !w[0] || !((w[0] >= 'A' && w[0] <= 'Z') || (w[0] >= 'a' && w[0] <= 'z') || w[0] == '_') || strlen(w) > 15;
        for (const char *c = w; !bad && *c; c++)
            if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) bad = 1;
        if (bad) { t_printf("  hand: not a name: %s\n", w); me->last_status = 1; continue; }
        if (eq) set_var(w, eq + 1);
        int slot = -1;
        for (int i = 0; i < me->nvars; i++) if (str_eq(me->vname[i], w)) slot = i;
        if (slot >= 0) { me->vhand[slot] = 1; continue; }
        int known = 0;
        for (int i = 0; i < me->npend; i++) if (str_eq(me->vpend[i], w)) known = 1;
        if (!known) {
            if (me->npend >= 8) { t_puts("  hand: too many names waiting for a value (8).\n"); me->last_status = 1; continue; }
            strcpy(me->vpend[me->npend++], w);
        }
    }
}

/* The environment for a program: every handed variable as NAME=value, one
 * a line, as much as fits. */
static void build_env(char *out, int max) {
    shell_t *me = cur_shell();
    int n = 0;
    out[0] = '\0';
    for (int i = 0; i < me->nvars; i++) {
        if (!me->vhand[i]) continue;
        int need = (int)strlen(me->vname[i]) + 1 + (int)strlen(me->vval[i]) + 1;
        if (n + need >= max) break;
        n += (int)strlen(strcpy(out + n, me->vname[i]));
        out[n++] = '=';
        n += (int)strlen(strcpy(out + n, me->vval[i]));
        out[n++] = '\n';
        out[n] = '\0';
    }
}

/* $(( EXPR )) (v0.60.0): integers, + - * / % and parentheses, unary - and
 * +, the comparisons < <= > >= == != giving 1 or 0, variables as NAME or
 * $NAME (empty or not a number: 0). C's precedence, as sh's. Division by
 * zero sets arith_err: the command is not run and its status is 1. */
static int arith_err;
static const char *ap;               /* where the parser is */
static long a_eq(void);
static void a_sp(void) { while (*ap == ' ') ap++; }
static long a_var(const char *name) {
    shell_t *me = cur_shell();
    for (int v = 0; v < me->nvars; v++)
        if (str_eq(me->vname[v], name)) {
            const char *x = me->vval[v]; long n = 0; int neg = 0;
            while (*x == ' ') x++;
            if (*x == '-') { neg = 1; x++; }
            while (*x >= '0' && *x <= '9') n = n * 10 + (*x++ - '0');
            return neg ? -n : n;
        }
    return 0;
}
static long a_prim(void) {
    a_sp();
    if (*ap == '(') { ap++; long v = a_eq(); a_sp(); if (*ap == ')') ap++; else arith_err = 1; return v; }
    if (*ap == '-') { ap++; return -a_prim(); }
    if (*ap == '+') { ap++; return a_prim(); }
    if (*ap >= '0' && *ap <= '9') { long v = 0; while (*ap >= '0' && *ap <= '9') v = v * 10 + (*ap++ - '0'); return v; }
    if (*ap == '$') ap++;
    if (*ap == '_' || (*ap >= 'A' && *ap <= 'Z') || (*ap >= 'a' && *ap <= 'z')) {
        char name[16]; int n = 0;
        while ((*ap == '_' || (*ap >= 'A' && *ap <= 'Z') || (*ap >= 'a' && *ap <= 'z') || (*ap >= '0' && *ap <= '9')) && n < 15) name[n++] = *ap++;
        name[n] = '\0';
        return a_var(name);
    }
    arith_err = 1;
    return 0;
}
static long a_mul(void) {
    long v = a_prim();
    for (;;) {
        a_sp();
        char o = *ap;
        if (o != '*' && o != '/' && o != '%') return v;
        ap++;
        long r = a_prim();
        if (o == '*') v *= r;
        else if (r == 0) { arith_err = 2; return 0; }
        else if (o == '/') v /= r;
        else v %= r;
    }
}
static long a_add(void) {
    long v = a_mul();
    for (;;) { a_sp(); if (*ap == '+') { ap++; v += a_mul(); } else if (*ap == '-') { ap++; v -= a_mul(); } else return v; }
}
static long a_rel(void) {
    long v = a_add();
    for (;;) {
        a_sp();
        if (ap[0] == '<' && ap[1] == '=') { ap += 2; v = v <= a_add(); }
        else if (ap[0] == '>' && ap[1] == '=') { ap += 2; v = v >= a_add(); }
        else if (ap[0] == '<') { ap++; v = v < a_add(); }
        else if (ap[0] == '>') { ap++; v = v > a_add(); }
        else return v;
    }
}
static long a_eq(void) {
    long v = a_rel();
    for (;;) {
        a_sp();
        if (ap[0] == '=' && ap[1] == '=') { ap += 2; v = v == a_rel(); }
        else if (ap[0] == '!' && ap[1] == '=') { ap += 2; v = v != a_rel(); }
        else return v;
    }
}
static long arith(const char *expr) {
    ap = expr;
    long v = a_eq();
    a_sp();
    if (*ap && !arith_err) arith_err = 1;            /* something left over */
    return v;
}

/* $(COMMAND) (v0.60.3): run COMMAND with its output going to a capture
 * terminal - every builtin writes through the shell's terminal and every
 * program through its task's, which a spawned program inherits - then put
 * what it wrote in place of the $(...). Trailing newlines are dropped and
 * the rest become spaces, as sh's word splitting makes them of an unquoted
 * $(...). The status is COMMAND's. A Ctrl-C pressed while it runs is seen
 * when it ends, not during (written down). */
typedef struct { term_t t; char *buf; int len, cap, limit; } capterm_t;
static void cap_putc(term_t *t, char c) {
    capterm_t *k = (capterm_t *)t;
    if (k->len >= k->cap - 1 && k->cap < k->limit) {      /* a pipe's capture grows (v0.60.27) */
        int ncap = k->cap * 2 < k->limit ? k->cap * 2 : k->limit;
        char *nb = kmalloc((uint32_t)ncap);
        if (nb) { memcpy(nb, k->buf, (uint32_t)k->len); kfree(k->buf); k->buf = nb; k->cap = ncap; }
    }
    if (k->len < k->cap - 1) k->buf[k->len++] = c;
}
static void cap_puts(term_t *t, const char *s) { while (*s) cap_putc(t, *s++); }
static void cap_color(term_t *t, int f, int b) { (void)t; (void)f; (void)b; }
static void cap_cursor(term_t *t, int r, int c) { (void)t; (void)r; (void)c; }
static int  cap_zero(term_t *t) { (void)t; return 0; }
static int  cap_cols(term_t *t) { (void)t; return 80; }
static int  cap_rows(term_t *t) { (void)t; return 25; }
static void cap_clear(term_t *t) { (void)t; }
static int  cap_getc(term_t *t) { (void)t; return -1; }      /* no input: EOF */
static void cap_flush(term_t *t) { (void)t; }

static void run_line(const char *line);
static int subst_cut;     /* the last $(...) did not fit (v0.60.30) */
static int command_subst(const char *cmd, char *out, int max) {
    if (subst_depth >= 2) return 0;
    capterm_t cap;
    cap.t.name = "subst"; cap.t.puts = cap_puts; cap.t.putc = cap_putc; cap.t.set_color = cap_color;
    cap.t.set_cursor = cap_cursor; cap.t.row = cap_zero; cap.t.col = cap_zero; cap.t.cols = cap_cols;
    cap.t.rows = cap_rows; cap.t.clear = cap_clear; cap.t.getc = cap_getc; cap.t.available = cap_zero;
    cap.t.flush_in = cap_flush; cap.t.fg = 0;
    cap.buf = kmalloc(4096); cap.len = 0; cap.cap = 4096;
    cap.limit = max + 2 > 4096 ? (max + 2 < 65536 ? max + 2 : 65536) : 4096;   /* v0.60.30 */
    if (!cap.buf) return 0;
    shell_t *me = cur_shell();
    task_t *tk = task_current();
    term_t *was_sh = me->term, *was_tk = tk->term;
    cap.t.intr = was_sh ? was_sh->intr : 0;
    cap.t.ctx = was_tk ? was_tk : &term_vga;   /* stderr goes where it would have (usermode) */
    me->term = &cap.t; tk->term = &cap.t;
    subst_depth++;
    run_line(cmd);
    subst_depth--;
    me->term = was_sh; tk->term = was_tk;
    while (cap.len > 0 && (cap.buf[cap.len - 1] == '\n' || cap.buf[cap.len - 1] == '\r')) cap.len--;
    subst_cut = (cap.len > max || cap.len >= cap.limit - 1);
    int n = 0;
    for (int i = 0; i < cap.len && n < max; i++) {
        char c = cap.buf[i];
        if (c == '\r') continue;
        out[n++] = (c == '\n') ? ' ' : c;
    }
    kfree(cap.buf);
    return n;
}

/* Expand one command's text: ~, $NAME / ${NAME}, $?, $((...)) and $(...),
 * quotes left in for the parts that follow (v0.57.1-v0.60.3). */
/* Into ex, at most max bytes with the NUL (v0.60.30: was always INPUT_MAX).
 * 1 if the expansion did not fit and was cut, which callers must not run
 * as if it had: a command cut short can name a different file. */
/* One character that came out of an expansion, written so that what reads
 * the line afterwards (uargv, slurp, first_word, unquote, cook's tokens)
 * takes it as text (v0.60.39): sh never re-reads an expansion's quotes, and
 * a | or > in a value is not a pipe or a redirection. Outside quotes ' and
 * " are wrapped in the other quote and | < > & in single quotes; inside
 * "..." only " needs it (close, '"', reopen). 0 if there was no room. */
static int put_lit(char *ex, int *k, int max, char c, int in_dq) {
    char one[4] = { c, 0, 0, 0 };
    const char *rep = one;
    int keep_blanks = in_dq == 2;          /* an assignment's value (v0.60.40) */
    if (in_dq == 1) { if (c == '"') rep = "\"'\"'\""; }
    else if (c == '\'') rep = "\"'\"";
    else if (c == '"') rep = "'\"'";
    else if (c == '|' || c == '<' || c == '>' || c == '&' || (keep_blanks && (c == ' ' || c == '\t'))) {
        one[0] = '\''; one[1] = c; one[2] = '\'';
    }
    int n = (int)strlen(rep);
    if (*k + n >= max - 1) return 0;
    memcpy(ex + *k, rep, (uint32_t)n);
    *k += n;
    return 1;
}

/* Is ex[0..k) the start of an assignment's value: NAME= first in the
 * command, and no blank outside quotes since? Then an expansion here must
 * stay one value, blanks and all, as sh does not split an assignment
 * (v0.60.40: `w=$v` with v="a b" became `w=a b`, not an assignment). */
static int in_assign_value(const char *ex, int k) {
    int a = 0;
    while (a < k && ex[a] == ' ') a++;
    if (a >= k || !(ex[a] == '_' || (ex[a] >= 'A' && ex[a] <= 'Z') || (ex[a] >= 'a' && ex[a] <= 'z'))) return 0;
    while (a < k && (ex[a] == '_' || (ex[a] >= 'A' && ex[a] <= 'Z') || (ex[a] >= 'a' && ex[a] <= 'z') || (ex[a] >= '0' && ex[a] <= '9'))) a++;
    if (a >= k || ex[a] != '=') return 0;
    char q = 0;
    for (a++; a < k; a++) {
        if (q) { if (ex[a] == q) q = 0; }
        else if (ex[a] == '\'' || ex[a] == '"') q = ex[a];
        else if (ex[a] == ' ' || ex[a] == '\t') return 0;
    }
    return 1;
}

static int expand_into(const char *seg, char *ex, int max) {
    int cut = 0;
    int  k = 0;
    char sq = 0;                     /* inside '...', $? is left alone */
    /* ~ is the cook's home (v0.57.3): a word that is ~ or starts
     * with ~/, unquoted, at the start of a word (after a space, or
     * right after < >). The headchef's home is /, and ~/X gives /X
     * (sh would give //X). */
    char home[FAT_PATH_MAX];
    if (cur_shell()->uid == 0) strcpy(home, "/");
    else { strcpy(home, "/home/"); strncpy(home + 6, users_name_of(cur_shell()->uid), sizeof(home) - 7); home[sizeof(home) - 1] = '\0'; }
    for (int i = 0; seg[i] && k < max - 1; i++) {
        if (seg[i] == '\'' && sq != '"') sq = sq ? 0 : '\'';
        else if (seg[i] == '"' && sq != '\'') sq = sq ? 0 : '"';
        char prev = i ? seg[i - 1] : ' ';
        if (seg[i] == '~' && !sq && (prev == ' ' || prev == '<' || prev == '>')
            && (seg[i + 1] == '\0' || seg[i + 1] == ' ' || seg[i + 1] == '/')) {
            const char *h = home;
            if (seg[i + 1] == '/' && h[0] == '/' && h[1] == '\0') h = "";   /* ~/X with home / */
            while (*h && k < max - 1) ex[k++] = *h++;
            continue;
        }
        if (seg[i] == '$' && seg[i + 1] == '(' && seg[i + 2] == '(' && sq != '\'') {
            int j = i + 3, depth = 0;
            while (seg[j] && !(depth == 0 && seg[j] == ')' && seg[j + 1] == ')')) {
                if (seg[j] == '(') depth++; else if (seg[j] == ')') depth--;
                j++;
            }
            if (!seg[j]) { ex[k++] = seg[i]; continue; }       /* no )): leave it */
            char expr[INPUT_MAX]; int el = j - (i + 3);
            memcpy(expr, seg + i + 3, (uint32_t)el); expr[el] = '\0';
            long v = arith(expr);
            char d[24]; int nd = 0; unsigned long u = v < 0 ? (unsigned long)(-v) : (unsigned long)v;
            do { d[nd++] = (char)('0' + u % 10); u /= 10; } while (u);
            if (v < 0) d[nd++] = '-';
            while (nd && k < max - 1) ex[k++] = d[--nd];
            i = j + 1;
            continue;
        }
        if (seg[i] == '$' && seg[i + 1] == '(' && seg[i + 2] != '(' && sq != '\'') {
            int j = i + 2, depth = 0;
            char q2 = 0;
            while (seg[j] && !(depth == 0 && !q2 && seg[j] == ')')) {
                if (q2) { if (seg[j] == q2) q2 = 0; }
                else if (seg[j] == '\'' || seg[j] == '"') q2 = seg[j];
                else if (seg[j] == '(') depth++;
                else if (seg[j] == ')') depth--;
                j++;
            }
            if (!seg[j]) { ex[k++] = seg[i]; continue; }       /* no ): leave it */
            char inner[INPUT_MAX]; int il = j - (i + 2);
            memcpy(inner, seg + i + 2, (uint32_t)il); inner[il] = '\0';
            /* Right after NAME= the result is the value, kept whole as sh
             * keeps an assignment's (quoted here); elsewhere its spaces
             * separate words, as sh's splitting does. */
            int assign = 0, a = 0;
            while (a < k && ex[a] == ' ') a++;
            if (a < k && (ex[a] == '_' || (ex[a] >= 'A' && ex[a] <= 'Z') || (ex[a] >= 'a' && ex[a] <= 'z'))) {
                while (a < k && (ex[a] == '_' || (ex[a] >= 'A' && ex[a] <= 'Z') || (ex[a] >= 'a' && ex[a] <= 'z') || (ex[a] >= '0' && ex[a] <= '9'))) a++;
                assign = (a == k - 1 && ex[a] == '=');
            }
            if (!assign && sq != '"' && in_assign_value(ex, k)) assign = 1;   /* w=x$(...) too (v0.60.40) */
            if (assign && k < max - 1) ex[k++] = '"';
            char *out = kmalloc((uint32_t)max);
            if (out) {
                int n = command_subst(inner, out, max - 2 - k);
                if (subst_cut) cut = 1;
                for (int o = 0; o < n; o++)
                    if (!put_lit(ex, &k, max - 1, out[o], assign || sq == '"')) { cut = 1; break; }
                kfree(out);
            }
            if (assign && k < max - 1) ex[k++] = '"';
            i = j;
            continue;
        }
        /* $1..$9, $# and $@: a script's arguments (v0.60.2); not in '...'. */
        if (seg[i] == '$' && sq != '\'' && ((seg[i + 1] >= '1' && seg[i + 1] <= '9') || seg[i + 1] == '#' || seg[i + 1] == '@')) {
            shell_t *me = cur_shell();
            char c = seg[i + 1];
            if (c == '#') {
                char d[4]; int nd = 0; int v = me->npargs;
                do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v);
                while (nd && k < max - 1) ex[k++] = d[--nd];
            } else if (c == '@') {
                for (int a = 0; a < me->npargs; a++) {
                    if (a && k < max - 1) ex[k++] = ' ';
                    for (const char *v = me->parg[a]; *v; v++) if (!put_lit(ex, &k, max, *v, sq == '"')) { cut = 1; break; }
                }
            } else if (c - '1' < me->npargs) {
                int mode = sq == '"' ? 1 : in_assign_value(ex, k) ? 2 : 0;
                for (const char *v = me->parg[c - '1']; *v; v++) if (!put_lit(ex, &k, max, *v, mode)) { cut = 1; break; }
            }
            i++;
            continue;
        }
        /* ${NAME}, ${#NAME}, ${NAME:-word}, ${NAME:=word} (v0.60.42), with a
         * name or a script argument 1..9: the value, its length, the word
         * when it is unset or empty, and the same setting it. The word is
         * expanded first ($X, quotes), as sh does. Anything else in braces
         * is left as typed. */
        if (seg[i] == '$' && seg[i + 1] == '{' && sq != '\'') {
            int j = i + 2, len_of = 0;
            if (seg[j] == '#' && seg[j + 1] != '}') { len_of = 1; j++; }
            char name[16]; int nn = 0;
            if (seg[j] >= '1' && seg[j] <= '9') name[nn++] = seg[j++];
            else while ((seg[j] == '_' || (seg[j] >= 'A' && seg[j] <= 'Z') || (seg[j] >= 'a' && seg[j] <= 'z') ||
                         (nn && seg[j] >= '0' && seg[j] <= '9')) && nn < 15) name[nn++] = seg[j++];
            name[nn] = '\0';
            char op = 0;
            if (nn && !len_of && seg[j] == ':' && (seg[j + 1] == '-' || seg[j + 1] == '=')) { op = seg[j + 1]; j += 2; }
            int wstart = j;
            if (op) {                                    /* the word: to the matching } */
                int dep = 0; char q = 0;
                while (seg[j] && (q || dep || seg[j] != '}')) {
                    if (q) { if (seg[j] == q) q = 0; }
                    else if (seg[j] == '\'' || seg[j] == '"') q = seg[j];
                    else if (seg[j] == '$' && seg[j + 1] == '{') { dep++; j++; }
                    else if (seg[j] == '}') dep--;
                    j++;
                }
            }
            if (!nn || seg[j] != '}') { ex[k++] = seg[i]; continue; }
            shell_t *me = cur_shell();
            const char *val = 0;
            if (name[0] >= '1' && name[0] <= '9') { if (name[0] - '1' < me->npargs) val = me->parg[name[0] - '1']; }
            else for (int v = 0; v < me->nvars; v++) if (str_eq(me->vname[v], name)) { val = me->vval[v]; break; }
            int mode = sq == '"' ? 1 : in_assign_value(ex, k) ? 2 : 0;
            if (len_of) {
                char d[12]; int nd = 0; unsigned v = val ? (unsigned)strlen(val) : 0;
                do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v);
                while (nd && k < max - 1) ex[k++] = d[--nd];
            } else if (op && (!val || !*val)) {
                char word[INPUT_MAX], wex[INPUT_MAX];
                int wl = j - wstart < INPUT_MAX - 1 ? j - wstart : INPUT_MAX - 1;
                memcpy(word, seg + wstart, (uint32_t)wl); word[wl] = '\0';
                if (expand_into(word, wex, INPUT_MAX)) cut = 1;
                for (const char *w = wex; *w && k < max - 1; w++) ex[k++] = *w;   /* its own quotes stand */
                if (op == '=' && !(name[0] >= '1' && name[0] <= '9')) {
                    char v2[INPUT_MAX];
                    strcpy(v2, wex);
                    unquote(v2);
                    set_var(name, v2);
                }
            } else if (val) {
                for (; *val; val++) if (!put_lit(ex, &k, max, *val, mode)) { cut = 1; break; }
            }
            i = j;
            continue;
        }
        /* $NAME and ${NAME} (v0.58.0): a shell variable, empty if unset;
         * not inside '...'. */
        if (seg[i] == '$' && sq != '\'' &&
            (seg[i + 1] == '{' || seg[i + 1] == '_' ||
             (seg[i + 1] >= 'A' && seg[i + 1] <= 'Z') || (seg[i + 1] >= 'a' && seg[i + 1] <= 'z'))) {
            char name[16]; int nn = 0, j = i + 1, braced = (seg[j] == '{');
            if (braced) j++;
            while ((seg[j] == '_' || (seg[j] >= 'A' && seg[j] <= 'Z') || (seg[j] >= 'a' && seg[j] <= 'z') ||
                    (seg[j] >= '0' && seg[j] <= '9')) && nn < 15) name[nn++] = seg[j++];
            name[nn] = '\0';
            if (braced) { if (seg[j] != '}') { ex[k++] = seg[i]; continue; } j++; }
            shell_t *me = cur_shell();
            for (int v = 0; v < me->nvars; v++)
                if (str_eq(me->vname[v], name)) {
                    int mode = sq == '"' ? 1 : in_assign_value(ex, k) ? 2 : 0;
                    for (const char *val = me->vval[v]; *val; val++)
                        if (!put_lit(ex, &k, max, *val, mode)) { cut = 1; break; }
                    break;
                }
            i = j - 1;
            continue;
        }
        if (seg[i] == '$' && seg[i + 1] == '?' && sq != '\'') {
            char d[12]; int nd = 0; unsigned v = (unsigned)cur_shell()->last_status;
            do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v);
            while (nd && k < max - 1) ex[k++] = d[--nd];
            i++;
        } else ex[k++] = seg[i];
    }
    ex[k] = '\0';
    return cut || k >= max - 1;
}
static void expand_seg(const char *seg, char *ex) { expand_into(seg, ex, INPUT_MAX); }

/* A function's name as the first word runs its body (v0.60.22), with the
 * words after it as $1..$9 for the body and the caller's given back after,
 * as `follow` does for a script. Its status is the body's last. 1 if ex
 * named a function. Eight calls deep at most, so one that calls itself
 * forever stops. */
static void run_line(const char *line);
static int call_function(const char *ex) {
    shell_t *me = cur_shell();
    char name[16];
    const char *rest = first_word(ex, name, sizeof(name));
    int f = -1;
    for (int i = 0; i < me->nfuncs; i++) if (str_eq(me->fname[i], name)) f = i;
    if (f < 0) return 0;
    static int depth;
    if (depth >= 8) { t_printf("  %s: functions nested eight deep; not going further.\n", name); me->last_status = 1; return 1; }
    char (*saved)[64] = kmalloc(sizeof(me->parg));
    char *body = kmalloc(INPUT_MAX);
    if (!saved || !body) { if (saved) kfree(saved); if (body) kfree(body); return 1; }
    int saved_n = me->npargs;
    memcpy(saved, me->parg, sizeof(me->parg));
    strcpy(body, me->fbody[f]);                 /* it may redefine itself */
    me->npargs = 0;
    while (*rest && me->npargs < 9) { rest = first_word(rest, me->parg[me->npargs], 64); me->npargs++; }
    me->last_status = 0;
    depth++;
    run_line(body);
    depth--;
    memcpy(me->parg, saved, sizeof(me->parg));
    me->npargs = saved_n;
    kfree(saved); kfree(body);
    return 1;
}

/* A builtin or a function into a program (v0.60.27; streams since v0.60.33):
 * `slurp hi | raise.elf`, `myfunc | stack.elf`. Only `cook` lines made pipes
 * before. The right side starts first, as a cook line whose first stage
 * reads a pipe; once it is running, cook calls back here (pending_feed) and
 * the left side runs with a terminal that writes into that pipe as it
 * prints, then the pipe is closed and cook waits as usual. So a left side
 * that never ends no longer holds the line: when the reader is gone the
 * terminal stops writing and counts a Ctrl-C, which ends a loop on the
 * left the way sh's SIGPIPE would. A `<` on the right wins, as in sh; the
 * status is the right side's. The left side runs in this shell, not a
 * subshell, so what it sets stays set (sh would forget it). */
typedef struct { term_t t; vfs_node_t *wr; char buf[512]; int n, dead; } pipeterm_t;
static void pt_flush(pipeterm_t *k) {
    for (int off = 0; off < k->n && !k->dead; ) {
        int w = vfs_write(k->wr, k->buf + off, (uint32_t)(k->n - off));
        if (w <= 0) { k->dead = 1; k->t.intr++; break; }   /* the reader has gone */
        off += w;
    }
    k->n = 0;
}
static void pt_putc(term_t *t, char c) {
    pipeterm_t *k = (pipeterm_t *)t;
    if (k->dead) return;
    k->buf[k->n++] = c;
    if (k->n == (int)sizeof(k->buf)) pt_flush(k);
}
static void pt_puts(term_t *t, const char *s) { while (*s) pt_putc(t, *s++); }

static void run_one(const char *seg);
static int is_kw(const char *t, const char *kw);
static void run_line(const char *line);
/* The first '|' in seg that is a pipe: outside quotes and $(...), not ||. */
static const char *pipe_bar(const char *seg) {
    char q = 0; int dep = 0;
    for (const char *s = seg; *s; s++) {
        if (q) { if (*s == q) q = 0; continue; }
        if (*s == '\'' || *s == '"') { q = *s; continue; }
        if (s[0] == '$' && s[1] == '(') { dep++; s++; continue; }
        if (dep && *s == '(') { dep++; continue; }
        if (dep && *s == ')') { dep--; continue; }
        if (!dep && *s == '|' && s[1] != '|' && (s == seg || s[-1] != '|')) return s;
    }
    return 0;
}

static unsigned intr_mark;               /* see interrupted(), below */
static const char *feed_left;            /* what pending_feed runs */
static void (*feed_fn)(void *);          /* ... or this, for a whole loop (v0.60.37) */
static void *feed_ctx;
static vfs_node_t *feed_wr;

static void feed_now(void) {
    shell_t *me = cur_shell();
    pipeterm_t *pt = kmalloc(sizeof(pipeterm_t));
    if (!pt) { vfs_close(feed_wr); return; }
    memset(pt, 0, sizeof(*pt));
    pt->t.name = "subst"; pt->t.puts = pt_puts; pt->t.putc = pt_putc; pt->t.set_color = cap_color;
    pt->t.set_cursor = cap_cursor; pt->t.row = cap_zero; pt->t.col = cap_zero; pt->t.cols = cap_cols;
    pt->t.rows = cap_rows; pt->t.clear = cap_clear; pt->t.getc = cap_getc; pt->t.available = cap_zero;
    pt->t.flush_in = cap_flush; pt->t.fg = 0;
    pt->wr = feed_wr;
    task_t *tk = task_current();
    term_t *was_sh = me->term, *was_tk = tk->term;
    pt->t.intr = was_sh ? was_sh->intr : 0;
    pt->t.ctx = was_tk ? was_tk : &term_vga;   /* stderr goes where it would have */
    me->term = &pt->t; tk->term = &pt->t;
    unsigned mark = intr_mark;
    intr_mark = pt->t.intr;                    /* a dead reader is the Ctrl-C it watches for */
    subst_depth++;
    if (feed_fn) { void (*fn)(void *) = feed_fn; feed_fn = 0; fn(feed_ctx); }
    else run_line(feed_left);
    subst_depth--;
    intr_mark = mark;
    me->term = was_sh; tk->term = was_tk;
    pt_flush(pt);
    vfs_close(pt->wr);                         /* EOF for the reader */
    kfree(pt);
}

static void pipe_into(const char *left, const char *right) {
    vfs_node_t *rd = 0, *wr = 0;
    if (vfs_pipe(&rd, &wr) < 0) { cook_err("Out of pipes."); return; }
    char line[INPUT_MAX];
    int n = 0;
    const char *c = "cook ";
    while (*c && n < INPUT_MAX - 1) line[n++] = *c++;
    while (*right == ' ') right++;
    while (*right && n < INPUT_MAX - 1) line[n++] = *right++;
    line[n] = '\0';
    pending_in = rd;
    feed_left = left; feed_wr = wr;
    pending_feed = feed_now;
    run_one(line);
    if (pending_feed) {                         /* cook never started it: no left side */
        pending_feed = 0;
        feed_fn = 0;
        vfs_close(wr);
    }
    if (pending_in) { vfs_close(pending_in); pending_in = 0; }
}


/* Start `left` writing into wr, for a shell loop to read (v0.60.34). A cook
 * line runs alongside the loop (its pids are left in pending_pids); anything
 * else runs first with its output captured, and a task feeds it in. Either
 * way wr is owned from here on. */
static void start_into(const char *left) {
    vfs_node_t *wr = pending_out;
    pending_npids = 0;
    const char *q = left; while (*q == ' ') q++;
    if (is_kw(q, "cook")) {
        pending_nowait = 1;
        run_one(q);
        pending_nowait = 0;
        if (pending_out) { vfs_close(pending_out); pending_out = 0; }   /* cook did not take it */
        return;
    }
    pending_out = 0;
    shell_t *me = cur_shell();
    capterm_t cap;
    cap.t.name = "subst"; cap.t.puts = cap_puts; cap.t.putc = cap_putc; cap.t.set_color = cap_color;
    cap.t.set_cursor = cap_cursor; cap.t.row = cap_zero; cap.t.col = cap_zero; cap.t.cols = cap_cols;
    cap.t.rows = cap_rows; cap.t.clear = cap_clear; cap.t.getc = cap_getc; cap.t.available = cap_zero;
    cap.t.flush_in = cap_flush; cap.t.fg = 0;
    cap.buf = kmalloc(4096); cap.len = 0; cap.cap = 4096; cap.limit = 256 * 1024;
    loopfeed_t *f = kmalloc(sizeof(loopfeed_t));
    if (!cap.buf || !f) { if (cap.buf) kfree(cap.buf); if (f) kfree(f); vfs_close(wr); return; }
    task_t *tk = task_current();
    term_t *was_sh = me->term, *was_tk = tk->term;
    cap.t.intr = was_sh ? was_sh->intr : 0;
    cap.t.ctx = was_tk ? was_tk : &term_vga;
    me->term = &cap.t; tk->term = &cap.t;
    subst_depth++;
    run_line(q);
    subst_depth--;
    me->term = was_sh; tk->term = was_tk;
    f->wr = wr; f->buf = cap.buf; f->len = cap.len;
    if (!task_spawn("loopfeed", loopfeed_task, f)) { vfs_close(wr); kfree(cap.buf); kfree(f); }
}

/* NAME=value NAME=value... CMD (v0.60.41), as sh: with no CMD they are all
 * set; with one, each is set and handed to programs for CMD alone, then
 * put back as it was (unset, or its old value and whether it was handed).
 * 0 if ex does not start with two words of which the first is NAME=value
 * (try_assign has the single NAME=value). */
static void set_var(const char *name, const char *value);
static void cmd_unset(const char *args);
static void unquote(char *w);
static int call_function(const char *ex);
static void dispatch(const char *buf);
typedef struct { char name[16]; char val[96]; char old[96]; int had, handed; } pre_t;
static int prefix_assigns(const char *ex) {
    const char *t = ex;
    while (*t == ' ') t++;
    pre_t *as = 0;
    int n = 0;
    for (;;) {
        const char *w = t;
        int nl = 0;
        if (!(w[0] == '_' || (w[0] >= 'A' && w[0] <= 'Z') || (w[0] >= 'a' && w[0] <= 'z'))) break;
        while (w[nl] == '_' || (w[nl] >= 'A' && w[nl] <= 'Z') || (w[nl] >= 'a' && w[nl] <= 'z') || (w[nl] >= '0' && w[nl] <= '9')) nl++;
        if (w[nl] != '=' || nl > 15) break;
        const char *v = w + nl + 1;
        char q = 0;
        const char *e = v;
        while (*e && (q || *e != ' ')) { if (q) { if (*e == q) q = 0; } else if (*e == '\'' || *e == '"') q = *e; e++; }
        if (q) break;
        if (n == 0 && !*e) return 0;             /* one NAME=value alone: try_assign's */
        if (!as) { as = kmalloc(sizeof(pre_t) * 8); if (!as) return 0; }
        if (n == 8) break;
        memcpy(as[n].name, w, (uint32_t)nl); as[n].name[nl] = '\0';
        int vl = (int)(e - v) < 95 ? (int)(e - v) : 95;
        memcpy(as[n].val, v, (uint32_t)vl); as[n].val[vl] = '\0';
        unquote(as[n].val);
        n++;
        t = e;
        while (*t == ' ') t++;
        if (!*t) break;
    }
    if (n == 0) { if (as) kfree(as); return 0; }
    shell_t *me = cur_shell();
    if (!*t) {                                   /* A=1 B=2: plain assignments */
        for (int i = 0; i < n; i++) set_var(as[i].name, as[i].val);
        kfree(as);
        return 1;
    }
    for (int i = 0; i < n; i++) {                /* for CMD alone */
        int slot = -1;
        for (int v = 0; v < me->nvars; v++) if (str_eq(me->vname[v], as[i].name)) slot = v;
        as[i].had = slot >= 0;
        as[i].handed = slot >= 0 && me->vhand[slot];
        if (slot >= 0) strcpy(as[i].old, me->vval[slot]);
        set_var(as[i].name, as[i].val);
        for (int v = 0; v < me->nvars; v++) if (str_eq(me->vname[v], as[i].name)) me->vhand[v] = 1;
    }
    char cmd[INPUT_MAX];
    strncpy(cmd, t, sizeof(cmd) - 1); cmd[sizeof(cmd) - 1] = '\0';
    me->last_status = 0;
    if (!call_function(cmd)) dispatch(cmd);
    int st = me->last_status;
    for (int i = n - 1; i >= 0; i--) {           /* back as it was */
        if (!as[i].had) { cmd_unset(as[i].name); continue; }
        set_var(as[i].name, as[i].old);
        for (int v = 0; v < me->nvars; v++) if (str_eq(me->vname[v], as[i].name)) me->vhand[v] = (uint8_t)as[i].handed;
    }
    me->last_status = st;
    kfree(as);
    return 1;
}

/* Run one command: expand it, then it is an assignment or a dispatch. */
static void run_one(const char *seg) {
    const char *q = seg; while (*q == ' ') q++;
    if (!*q) return;
    const char *bar = pipe_bar(q);
    if (bar && !is_kw(q, "cook")) {
        char left[INPUT_MAX];
        int ln = (int)(bar - q) < INPUT_MAX - 1 ? (int)(bar - q) : INPUT_MAX - 1;
        memcpy(left, q, (uint32_t)ln); left[ln] = '\0';
        pipe_into(left, bar + 1);
        return;
    }
    char ex[INPUT_MAX];
    arith_err = 0;
    if (expand_into(seg, ex, INPUT_MAX) && !arith_err) {
        /* v0.60.30: it used to run cut short, which can name another file */
        t_printf("  The command is too long once expanded (%d bytes at most); not run.\n", INPUT_MAX - 1);
        cur_shell()->last_status = 1;
        return;
    }
    if (arith_err) {
        t_puts(arith_err == 2 ? "  arithmetic: division by zero\n" : "  arithmetic: not an expression\n");
        cur_shell()->last_status = 1;
        return;
    }
    if (prefix_assigns(ex)) return;
    if (try_assign(ex)) return;
    if (call_function(ex)) return;
    dispatch(ex);
}

/* ---- control flow: for and if (v0.59.3) ---------------------------------
 * A line is split into segments joined by ; && ||, as before, but a segment
 * that starts with do, then or else becomes the keyword and the command
 * after it, and `if X` becomes `if` and `X`, so the keywords stand alone.
 * exec_range then walks the segments: `for NAME in WORDS ; do ... ; done`
 * and `if ... ; then ... ; [else ... ;] fi` find their matching done or fi
 * by counting nesting, and run their bodies through exec_range again. Each
 * command is expanded just before it runs, so $NAME takes each value of a
 * loop in turn. `case W in P) ... ;; esac` (v0.60.19) splits further: the
 * header, each arm's pattern and a ;; are segments of their own. */
#define LSEG_MAX 64
typedef struct { const char *text; int op; int into; const char *out; int junk; } lseg_t;
/* op: ';' 'A' 'O'; into: | the next (v0.60.34); out: `done | PROG`, PROG (v0.60.37) */

static int is_kw(const char *t, const char *kw) {
    int n = (int)strlen(kw);
    return strncmp(t, kw, (uint32_t)n) == 0 && (t[n] == '\0' || t[n] == ' ');
}

/* Functions (v0.60.22). `NAME() { BODY ; }` (or `NAME () {`), one line:
 * if t starts with one, where it ends (just past its '}'), else 0. The
 * braces are counted outside quotes, and ${...} is not one. */
static int fn_name_len(const char *t) {
    int k = 0;
    if (!((t[0] >= 'a' && t[0] <= 'z') || (t[0] >= 'A' && t[0] <= 'Z') || t[0] == '_')) return 0;
    while ((t[k] >= 'a' && t[k] <= 'z') || (t[k] >= 'A' && t[k] <= 'Z') ||
           (t[k] >= '0' && t[k] <= '9') || t[k] == '_') k++;
    return k;
}
static char *fndef_end(char *t) {
    while (*t == ' ') t++;
    int k = fn_name_len(t);
    if (!k || k > 15) return 0;
    char *r = t + k;
    while (*r == ' ') r++;
    if (r[0] != '(' || r[1] != ')') return 0;
    r += 2;
    while (*r == ' ') r++;
    if (*r != '{' || (r[1] && r[1] != ' ')) return 0;
    int depth = 0; char q = 0;
    for (; *r; r++) {
        if (q) { if (*r == q) q = 0; continue; }
        if (*r == '\'' || *r == '"') { q = *r; continue; }
        if (r[0] == '$' && r[1] == '{') { while (*r && *r != '}') r++; if (!*r) return 0; continue; }
        if (*r == '{') depth++;
        else if (*r == '}' && --depth == 0) return r + 1;
    }
    return 0;
}

/* The first ')' outside quotes, or 0: where a case arm's pattern ends. */
static char *pattern_close(char *t) {
    char q = 0;
    for (; *t; t++) {
        if (q) { if (*t == q) q = 0; }
        else if (*t == '\'' || *t == '"') q = *t;
        else if (*t == ')') return t;
    }
    return 0;
}

/* `case WORD in` at t: where the header ends (after "in"), or 0. */
static char *case_header_end(char *t) {
    if (!is_kw(t, "case")) return 0;
    char *r = t + 4, q = 0;
    while (*r == ' ') r++;
    if (!*r) return 0;
    while (*r && (q || *r != ' ')) { if (q) { if (*r == q) q = 0; } else if (*r == '\'' || *r == '"') q = *r; r++; }
    while (*r == ' ') r++;
    if (r[0] != 'i' || r[1] != 'n' || (r[2] && r[2] != ' ')) return 0;
    return r + 2;
}

static const char *const DSEMI = ";;";

/* `X | while ...` (and for, if, case): the | before a compound (v0.60.34). */
static char *compound_bar(char *t) {
    for (const char *b = pipe_bar(t); b; b = pipe_bar(b + 1)) {
        const char *r = b + 1;
        while (*r == ' ') r++;
        if (is_kw(r, "while") || is_kw(r, "until") || is_kw(r, "for") || is_kw(r, "if") || is_kw(r, "case")) return (char *)b;
    }
    return 0;
}

static int split_segs(char *buf, lseg_t *sg, int max) {
    int n = 0, op = ';', arm_next = 0;
    memset(sg, 0, sizeof(lseg_t) * (uint32_t)max);
    char *s = buf;
    while (n < max) {
        char *start = s, inq = 0;
        int next_op = 0, pdepth = 0, dsemi = 0;   /* inside $( ... ): no splitting (v0.60.4) */
        char *defend = fndef_end(start);          /* NAME() { ... }: one segment (v0.60.22) */
        if (defend) s = defend;
        while (*s) {
            if (inq) { if (*s == inq) inq = 0; s++; continue; }
            if (*s == '\'' || *s == '"') { inq = *s; s++; continue; }
            if (s[0] == '$' && s[1] == '(') { pdepth++; s += 2; continue; }
            if (pdepth && *s == '(') { pdepth++; s++; continue; }
            if (pdepth && *s == ')') { pdepth--; s++; continue; }
            if (pdepth) { s++; continue; }
            if (s[0] == ';' && s[1] == ';') { next_op = ';'; dsemi = 1; *s = '\0'; s += 2; break; }
            if (s[0] == ';')                { next_op = ';'; *s = '\0'; s += 1; break; }
            if (s[0] == '&' && s[1] == '&') { next_op = 'A'; *s = '\0'; s += 2; break; }
            if (s[0] == '|' && s[1] == '|') { next_op = 'O'; *s = '\0'; s += 2; break; }
            s++;
        }
        char *t = start;
        while (*t == ' ') t++;
        char *end = t + strlen(t);
        while (end > t && end[-1] == ' ') *--end = '\0';
        {
            char *cb = compound_bar(t);               /* X | while ...: X pipes into what follows */
            if (cb && n < max - 1) {
                char *e = cb;
                *cb = '\0';
                while (e > t && e[-1] == ' ') *--e = '\0';
                sg[n].text = t; sg[n].op = op; sg[n].into = 1; n++;
                op = ';';
                t = cb + 1; while (*t == ' ') t++;
            }
        }
    again:
        /* do X / then X / else X / if X: the keyword, then X on its own -
         * repeatedly, since X may start with a keyword too (`do if ...`,
         * `then for ...`, `else if ...`). */
        {
            static const char *const kws[] = { "do", "then", "else", "if", "while", "until" };
            int more = 1;
            while (more && n < max - 1) {
                more = 0;
                for (int k = 0; k < 6; k++) {
                    int kl = (int)strlen(kws[k]);
                    if (strncmp(t, kws[k], (uint32_t)kl) == 0 && t[kl] == ' ') {
                        t[kl] = '\0';
                        sg[n].text = t; sg[n].op = op; n++;
                        op = ';';
                        t += kl + 1; while (*t == ' ') t++;
                        more = 1; break;
                    }
                }
            }
        }
        /* case (v0.60.19): `case W in` stands alone; each arm's pattern
         * (without its ')') is a segment of its own, then its command; ;;
         * is a segment of its own, and the one after it is an arm again. */
        if (arm_next && *t && n < max - 1) {
            if (is_kw(t, "esac")) arm_next = 0;
            else {
                char *cp = pattern_close(t);
                if (cp) {
                    *cp = '\0';
                    sg[n].text = t; sg[n].op = op; n++;
                    op = ';';
                    arm_next = 0;
                    t = cp + 1; while (*t == ' ') t++;
                    goto again;
                }
            }
        }
        {
            char *he = case_header_end(t);
            if (he && n < max - 1) {
                char *rest = he;
                while (*rest == ' ') rest++;
                *he = '\0';
                sg[n].text = t; sg[n].op = op; n++;
                op = ';';
                arm_next = 1;
                t = rest;
                goto again;
            }
        }
        if (*t && n < max) {
            sg[n].text = t; sg[n].op = op;
            /* done | PROG, fi | PROG, esac | PROG (v0.60.37): the compound's
             * output goes to PROG. The word stands alone; PROG is noted. */
            static const char *const closers[] = { "done", "fi", "esac" };
            for (int k = 0; k < 3; k++) {
                int kl = (int)strlen(closers[k]);
                if (strncmp(t, closers[k], (uint32_t)kl) == 0 && t[kl] == ' ') {
                    char *r = t + kl; while (*r == ' ') r++;
                    if (r[0] == '|' && r[1] != '|') {
                        t[kl] = '\0';
                        r++; while (*r == ' ') r++;
                        sg[n].out = r;
                    } else if (*r) sg[n].junk = 1;      /* done > F, done foo: said, not ignored */
                    break;
                }
            }
            n++;
        }
        if (dsemi && n < max) { sg[n].text = DSEMI; sg[n].op = ';'; n++; arm_next = 1; }
        if (!next_op) break;
        op = next_op;
    }
    return n;
}

static int match_end(lseg_t *sg, int from, int to, const char *open, const char *close) {
    int depth = 0;
    for (int i = from; i < to; i++) {
        if (is_kw(sg[i].text, open)) depth++;
        else if (is_kw(sg[i].text, close) && --depth == 0) return i;
    }
    return -1;
}

static void set_var(const char *name, const char *value);

/* A for loop's words (v0.60.30: 32 KB and 8192 words; were 512 bytes and 64,
 * after an expansion cut at 256, so `for i in $(cook tally.elf 6000)` ran
 * 64 times). */
#define FOR_TEXT   (32 * 1024)
#define FOR_WORDS  8192
typedef struct { char words[FOR_TEXT + 8192]; int used, n, cut; const char *list[FOR_WORDS]; } wordlist_t;
static void wl_add(void *ctx, const char *raw) {
    wordlist_t *wl = (wordlist_t *)ctx;
    char w[INPUT_MAX];
    strncpy(w, raw, sizeof(w) - 1); w[sizeof(w) - 1] = '\0';
    unquote(w);                                   /* a loop's values have no quotes */
    int len = (int)strlen(w);
    if (wl->n >= FOR_WORDS || wl->used + len + 1 > (int)sizeof(wl->words)) { wl->cut = 1; return; }
    memcpy(wl->words + wl->used, w, (uint32_t)len + 1);
    wl->list[wl->n++] = wl->words + wl->used;
    wl->used += len + 1;
}

/* Ctrl-C and shell loops (v0.60.1). The outermost run_line notes the
 * terminal's Ctrl-C count; a loop, and a script, stop between commands once
 * it has moved: the rest of the line is dropped and the status is 130, as
 * sh gives for SIGINT. A program running at the press is killed as before. */
static int run_depth;
static int interrupted(void) {
    term_t *t = cur_shell()->term;
    if (!t || t->intr == intr_mark) return 0;
    return 1;
}
static int match_done(lseg_t *sg, int from, int to) {      /* for and while share done */
    int depth = 0;
    for (int i = from; i < to; i++) {
        if (is_kw(sg[i].text, "for") || is_kw(sg[i].text, "while") || is_kw(sg[i].text, "until")) depth++;
        else if (is_kw(sg[i].text, "done") && --depth == 0) return i;
    }
    return -1;
}

/* break [N] and continue [N] (v0.60.44), as sh: N loops out (1 if not
 * given, all of them if more than are open). exec_range stops at once
 * while one is pending; each loop, after its body, takes its share. */
static int loop_after_body(shell_t *me) {
    if (me->brk) { me->brk--; return 1; }              /* this loop ends; the rest go on up */
    if (me->cont) {
        me->cont--;
        if (me->cont) return 1;                        /* an outer loop's continue */
        return 0;                                      /* ours: the next turn */
    }
    return 0;
}
static void cmd_break(const char *args, int is_continue) {
    shell_t *me = cur_shell();
    int n = 0;
    while (*args == ' ') args++;
    while (*args >= '0' && *args <= '9') n = n * 10 + (*args++ - '0');
    if (!n) n = 1;
    me->last_status = 0;
    if (!me->loops) {
        t_printf("  %s: only meaningful in a for, while or until loop\n", is_continue ? "continue" : "break");
        return;
    }
    if (n > me->loops) n = me->loops;
    if (is_continue) me->cont = n; else me->brk = n;
}

/* A compound fed into a pipe (v0.60.37): run its segments once more, from
 * inside the feed, with its first segment's && or || already decided. */
typedef struct { lseg_t *sg; int from, to, depth; } range_ctx_t;
static lseg_t *feeding_seg;
static void exec_range(lseg_t *sg, int from, int to, int depth);
static void range_feed(void *arg) {
    range_ctx_t *r = arg;
    lseg_t *was = feeding_seg;
    int op = r->sg[r->from].op;
    feeding_seg = &r->sg[r->from];
    r->sg[r->from].op = ';';
    exec_range(r->sg, r->from, r->to, r->depth + 1);
    r->sg[r->from].op = op;
    feeding_seg = was;
}

static void exec_range(lseg_t *sg, int from, int to, int depth) {
    if (depth > 8) { t_puts("  Nested eight deep; not going further.\n"); cur_shell()->last_status = 1; return; }
    int i = from;
    while (i < to && !cur_shell()->exiting) {
        if (cur_shell()->brk || cur_shell()->cont) return;   /* break/continue on its way out (v0.60.44) */
        if (interrupted()) { cur_shell()->last_status = 130; return; }
        int status = cur_shell()->last_status, op = sg[i].op;
        int run = (op == ';') || (op == 'A' && status == 0) || (op == 'O' && status != 0);
        {
            /* for/while/if/case ... done | PROG (v0.60.37): PROG starts, then
             * the whole compound runs feeding it, as `builtin | prog` does.
             * Not again for the compound the feed itself is running. */
            const char *c = sg[i].text;
            int loopish = is_kw(c, "for") || is_kw(c, "while") || is_kw(c, "until");
            if (run && &sg[i] != feeding_seg && (loopish || is_kw(c, "if") || is_kw(c, "case"))) {
                int e = loopish ? match_done(sg, i, to)
                      : is_kw(c, "if") ? match_end(sg, i, to, "if", "fi")
                      : match_end(sg, i, to, "case", "esac");
                if (e >= 0 && sg[e].junk) {
                    t_printf("  Only `| program` may follow done, fi or esac (here: %s).\n", sg[e].text);
                    cur_shell()->last_status = 2;
                    return;
                }
                if (e >= 0 && sg[e].out) {
                    range_ctx_t rc = { sg, i, e + 1, depth };
                    feed_fn = range_feed; feed_ctx = &rc;
                    pipe_into(0, sg[e].out);
                    feed_fn = 0;
                    i = e + 1;
                    continue;
                }
            }
        }
        if (sg[i].into && i + 1 < to) {
            /* X | while/for/if/case ... (v0.60.34): the compound after this
             * segment reads X's output through take. Find where it ends. */
            const char *c = sg[i + 1].text;
            int e = (is_kw(c, "while") || is_kw(c, "until") || is_kw(c, "for")) ? match_done(sg, i + 1, to)
                  : is_kw(c, "if") ? match_end(sg, i + 1, to, "if", "fi")
                  : match_end(sg, i + 1, to, "case", "esac");
            if (e < 0) { t_puts("  |: the loop after it never ends (no done, fi or esac).\n"); cur_shell()->last_status = 2; return; }
            if (run) {
                shell_t *me = cur_shell();
                vfs_node_t *rd = 0, *wr = 0;
                if (vfs_pipe(&rd, &wr) < 0) { cook_err("Out of pipes."); i = e + 1; continue; }
                pending_out = wr;
                start_into(sg[i].text);
                vfs_node_t *was_in = me->in_pipe;
                int was_pos = me->in_pos, was_len = me->in_len;
                me->in_pipe = rd; me->in_pos = me->in_len = 0;
                sg[i + 1].op = ';';                 /* the compound runs whatever X's status */
                exec_range(sg, i + 1, e + 1, depth + 1);
                me->in_pipe = was_in; me->in_pos = was_pos; me->in_len = was_len;
                vfs_close(rd);                       /* a writer still going gets -1 and ends */
                int st = me->last_status;
                for (int k = 0; k < pending_npids; k++) { int code; proc_wait(pending_pids[k], &code); }
                pending_npids = 0;
                me->last_status = st;                /* the loop's, as sh's */
            }
            i = e + 1;
            continue;
        }
        if (is_kw(sg[i].text, "for")) {
            int d = match_done(sg, i, to);
            if (d < 0 || i + 1 >= d || !is_kw(sg[i + 1].text, "do")) {
                t_puts("  for: expected `for NAME in WORDS ; do ... ; done`.\n");
                cur_shell()->last_status = 2; return;
            }
            if (run) {
                char *ex = kmalloc(FOR_TEXT);
                wordlist_t *wl = kmalloc(sizeof(wordlist_t));
                if (!ex || !wl) { if (ex) kfree(ex); if (wl) kfree(wl); t_puts("  for: out of memory.\n"); cur_shell()->last_status = 1; return; }
                wl->used = wl->n = wl->cut = 0;
                if (expand_into(sg[i].text + 3, ex, FOR_TEXT)) wl->cut = 1;
                /* NAME in WORDS */
                char name[16]; const char *r = first_word(ex, name, sizeof(name));
                char inw[8]; r = first_word(r, inw, sizeof(inw));
                if (!name[0] || !str_eq(inw, "in")) { kfree(ex); kfree(wl); t_puts("  for: expected `for NAME in WORDS`.\n"); cur_shell()->last_status = 2; return; }
                while (*r) {
                    /* a word as written (quotes kept), then globbed or unquoted */
                    char raw[INPUT_MAX]; int k = 0; char q = 0;
                    while (*r == ' ') r++;
                    while (*r && (q || *r != ' ') && k < INPUT_MAX - 1) {
                        if (q) { if (*r == q) q = 0; } else if (*r == '\'' || *r == '"') q = *r;
                        raw[k++] = *r++;
                    }
                    raw[k] = '\0';
                    if (k) glob_expand(raw, wl_add, wl);
                }
                kfree(ex);
                if (wl->cut) t_printf("  for: the word list was cut (%d bytes, %d words at most); %d words kept.\n", FOR_TEXT, FOR_WORDS, wl->n);
                cur_shell()->last_status = 0;
                shell_t *me = cur_shell();
                me->loops++;
                for (int w = 0; w < wl->n && !me->exiting && !interrupted(); w++) {
                    set_var(name, wl->list[w]);
                    exec_range(sg, i + 2, d, depth + 1);
                    if (loop_after_body(me)) break;
                }
                me->loops--;
                if (interrupted()) { kfree(wl); cur_shell()->last_status = 130; return; }
                kfree(wl);
            }
            i = d + 1;
            continue;
        }
        if (is_kw(sg[i].text, "while") || is_kw(sg[i].text, "until")) {
            /* while COMMANDS ; do COMMANDS ; done (v0.60.1): the condition is
             * the commands between while and do; the status is the body's
             * last, or 0 if the body never ran, as sh does. until (v0.60.43)
             * is the same loop, running while the condition fails. */
            int until = is_kw(sg[i].text, "until");
            int d = match_done(sg, i, to), dd = -1, dep = 0;
            for (int j = i; d >= 0 && j < d; j++) {
                if (is_kw(sg[j].text, "for") || is_kw(sg[j].text, "while") || is_kw(sg[j].text, "until")) dep++;
                else if (is_kw(sg[j].text, "done")) dep--;
                else if (dep == 1 && is_kw(sg[j].text, "do")) { dd = j; break; }
            }
            if (d < 0 || dd < 0) { t_printf("  %s: expected `%s ... ; do ... ; done`.\n", until ? "until" : "while", until ? "until" : "while"); cur_shell()->last_status = 2; return; }
            if (run) {
                int body_status = 0;
                shell_t *me = cur_shell();
                me->loops++;
                for (;;) {
                    if (interrupted() || me->exiting) break;
                    exec_range(sg, i + 1, dd, depth + 1);
                    if (loop_after_body(me)) break;
                    if (interrupted() || (me->last_status != 0) != until) break;
                    exec_range(sg, dd + 1, d, depth + 1);
                    body_status = me->last_status;
                    if (loop_after_body(me)) break;
                }
                me->loops--;
                if (interrupted()) { cur_shell()->last_status = 130; return; }
                cur_shell()->last_status = body_status;
            }
            i = d + 1;
            continue;
        }
        if (is_kw(sg[i].text, "case")) {
            /* case WORD in PAT|PAT) ... ;; ... esac (v0.60.19): the first arm
             * whose pattern matches runs, and no other; none matching is
             * status 0, as sh has it. A pattern is expanded like a word, then
             * matched as a glob, or as plain text where it was quoted. */
            int e = match_end(sg, i, to, "case", "esac");
            if (e < 0) { t_puts("  case: expected `case WORD in PATTERN) ... ;; esac`.\n"); cur_shell()->last_status = 2; return; }
            if (run) {
                char ex[INPUT_MAX], word[INPUT_MAX];
                expand_seg(sg[i].text + 4, ex);
                const char *w = ex; while (*w == ' ') w++;
                int k = 0; char q = 0;
                while (*w && (q || *w != ' ') && k < INPUT_MAX - 1) {
                    if (q) { if (*w == q) q = 0; } else if (*w == '\'' || *w == '"') q = *w;
                    word[k++] = *w++;
                }
                word[k] = '\0';
                unquote(word);
                cur_shell()->last_status = 0;
                int j = i + 1, matched = 0;
                while (j < e && !matched) {
                    /* the arm: its pattern at j, its commands up to ;; or esac */
                    int b = j + 1, dep = 0;
                    while (b < e) {
                        if (is_kw(sg[b].text, "case")) dep++;
                        else if (is_kw(sg[b].text, "esac")) dep--;
                        else if (dep == 0 && sg[b].text == DSEMI) break;
                        b++;
                    }
                    char pat[INPUT_MAX];
                    const char *pt = sg[j].text;
                    if (*pt == '(') pt++;                         /* (PAT) is sh too */
                    expand_seg(pt, pat);
                    char *alt = pat;
                    while (alt && !matched) {
                        char *bar = alt, qq = 0;
                        while (*bar && (qq || *bar != '|')) { if (qq) { if (*bar == qq) qq = 0; } else if (*bar == '\'' || *bar == '"') qq = *bar; bar++; }
                        char *next = *bar ? bar + 1 : 0;
                        *bar = '\0';
                        while (*alt == ' ') alt++;
                        char *ae = alt + strlen(alt);
                        while (ae > alt && ae[-1] == ' ') *--ae = '\0';
                        int quoted = strchr(alt, '\'') || strchr(alt, '"');
                        if (quoted) { unquote(alt); matched = str_eq(alt, word); }
                        else matched = glob_match(alt, word);
                        alt = next;
                    }
                    if (matched) exec_range(sg, j + 1, b, depth + 1);
                    j = b + 1;
                }
            }
            i = e + 1;
            continue;
        }
        if (sg[i].text == DSEMI) { i++; continue; }
        if (run && fndef_end((char *)sg[i].text)) {
            /* A definition: kept as written, expanded only when called. */
            shell_t *sh = cur_shell();
            const char *t = sg[i].text;
            int k = fn_name_len(t);
            char name[16];
            memcpy(name, t, (size_t)k); name[k] = '\0';
            const char *b = strchr(t, '{') + 1;
            const char *e = t + strlen(t) - 1;           /* the '}' */
            while (*b == ' ') b++;
            while (e > b && (e[-1] == ' ' || e[-1] == ';')) e--;
            int slot = -1;
            for (int f = 0; f < sh->nfuncs; f++) if (str_eq(sh->fname[f], name)) slot = f;
            if (slot < 0 && sh->nfuncs < 16) slot = sh->nfuncs++;
            if (slot < 0) { t_puts("  Sixteen functions already; not another.\n"); sh->last_status = 1; i++; continue; }
            int bl = (int)(e - b) < INPUT_MAX - 1 ? (int)(e - b) : INPUT_MAX - 1;
            strcpy(sh->fname[slot], name);
            memcpy(sh->fbody[slot], b, (size_t)bl); sh->fbody[slot][bl] = '\0';
            sh->last_status = 0;
            i++;
            continue;
        }
        if (is_kw(sg[i].text, "if")) {
            int f = match_end(sg, i, to, "if", "fi");
            int th = -1, el = -1, dep = 0;
            for (int j = i; f >= 0 && j < f; j++) {
                if (is_kw(sg[j].text, "if")) dep++;
                else if (is_kw(sg[j].text, "fi")) dep--;
                else if (dep == 1 && is_kw(sg[j].text, "then") && th < 0) th = j;
                else if (dep == 1 && is_kw(sg[j].text, "else") && th >= 0 && el < 0) el = j;
            }
            if (f < 0 || th < 0) { t_puts("  if: expected `if ... ; then ... ; fi`.\n"); cur_shell()->last_status = 2; return; }
            if (run) {
                exec_range(sg, i + 1, th, depth + 1);
                if (cur_shell()->last_status == 0) exec_range(sg, th + 1, el >= 0 ? el : f, depth + 1);
                else if (el >= 0)                  exec_range(sg, el + 1, f, depth + 1);
                else                               cur_shell()->last_status = 0;
            }
            i = f + 1;
            continue;
        }
        if (run) run_one(sg[i].text);
        i++;
    }
}

static void run_line(const char *line) {
    char *buf = kmalloc(INPUT_MAX * 2);
    lseg_t *sg = kmalloc(sizeof(lseg_t) * LSEG_MAX);
    if (!buf || !sg) { if (buf) kfree(buf); if (sg) kfree(sg); return; }
    strncpy(buf, line, INPUT_MAX * 2 - 1); buf[INPUT_MAX * 2 - 1] = '\0';
    int n = split_segs(buf, sg, LSEG_MAX);
    if (run_depth == 0) { cur_shell()->brk = cur_shell()->cont = 0; cur_shell()->loops = 0; }
    if (run_depth++ == 0 && cur_shell()->term) intr_mark = cur_shell()->term->intr;
    exec_range(sg, 0, n, 0);
    run_depth--;
    kfree(sg); kfree(buf);
}

/* ---- main shell loop ---- */

static void shell_loop(char *input, char *saved, int *pos_p, int *len_p, int *hb_p);

/* run FILE - a file of command lines, run as the cook who runs it (v0.57.5).
 * Each line goes through run_line, so ; && || $? ~ and quotes all work; a
 * line starting with '#' is a comment; nothing stops it but the end or a
 * clockout; its status is the last line's. The cook must be able to read
 * the file. A script may run another, eight deep at most, so one that runs
 * itself stops rather than running the kernel stack out. */
static void run_line(const char *line);
static void cmd_run(const char *args) {
    static int depth;
    /* run FILE [ARGS...] (v0.60.2): the words after the file are the
     * script's $1..$9; quotes group them, as everywhere. */
    char file[FAT_PATH_MAX];
    const char *rest = first_word(args, file, sizeof(file));
    if (!file[0]) { t_puts("Usage: follow <file> [args...]\n"); cur_shell()->last_status = 2; return; }
    char path[FAT_PATH_MAX];
    resolve_path(file, path);
    if (!fat_exists(path) || fat_is_dir(path)) { cook_err("No such script."); return; }
    if (!may(path, 'r')) { deny(path); return; }
    if (depth >= 8) {
        t_puts("  follow: recipes nested eight deep; not going further.\n");
        klog("[run] %s: nested eight deep, stopped\n", path);
        cur_shell()->last_status = 1;
        return;
    }
    char *text = kmalloc(8193);
    if (!text) return;
    uint32_t size = 0;
    if (fat_read(path, (uint8_t *)text, 8192, &size) < 0) { kfree(text); cook_err("Cannot read the script."); return; }
    if (size > 8192) size = 8192;                    /* fat_read reports the file's size */
    text[size] = '\0';
    /* The caller's arguments are kept and given back after: a script run
     * from a script does not take its parent's $1. */
    shell_t *me = cur_shell();
    char (*saved)[64] = kmalloc(sizeof(me->parg));
    int  saved_n = me->npargs;
    if (!saved) { kfree(text); return; }
    memcpy(saved, me->parg, sizeof(me->parg));
    me->npargs = 0;
    while (*rest && me->npargs < 9) { rest = first_word(rest, me->parg[me->npargs], 64); me->npargs++; }
    depth++;
    cur_shell()->last_status = 0;
    uint32_t i = 0;
    while (i < size && !cur_shell()->exiting) {
        uint32_t e = i;
        while (e < size && text[e] != '\n' && text[e] != '\r') e++;
        char line[INPUT_MAX];
        uint32_t n = e - i < INPUT_MAX - 1 ? e - i : INPUT_MAX - 1;
        memcpy(line, text + i, n); line[n] = '\0';
        const char *l = line;
        while (*l == ' ') l++;
        if (*l && *l != '#') run_line(l);
        if (interrupted()) { cur_shell()->last_status = 130; break; }   /* Ctrl-C ends a script (v0.60.1) */
        i = e + 1;
    }
    depth--;
    memcpy(me->parg, saved, sizeof(me->parg));
    me->npargs = saved_n;
    kfree(saved);
    kfree(text);
}

/* /etc/rc: shell commands, one a line, run as headchef at boot before the
 * login prompt (v0.47.0) - so a machine can come up with `vault 22` already
 * open. The network is already up: the kernel leases an address at boot. A
 * line starting with '#' is a comment. Each line is logged as it runs. Only
 * the headchef should be able to write /etc; permissions are advisory. */
static void run_rc(void) {
    static char rc[2048];
    uint32_t size = 0;
    if (fat_read("/etc/rc", (uint8_t *)rc, sizeof(rc) - 1, &size) < 0 || size == 0) return;
    /* It runs as the headchef, so it must be the headchef's and closed to
     * the other cooks, or it is not run (v0.54.2), the way sshd's
     * StrictModes refuses a key file others can write. A file copied in by
     * another tool carries no soupOS metadata and reads as the headchef's,
     * rwxr-x, so it passes. */
    uint8_t rc_owner, rc_mode;
    if (fat_stat("/etc/rc", &rc_owner, &rc_mode) == 0 && (rc_owner != 0 || (rc_mode & FAT_PERM_AW))) {
        klog("[rc] /etc/rc not run: owned by uid %u, mode 0x%x - only the headchef may write it\n", rc_owner, rc_mode);
        t_puts("  /etc/rc was not run: a cook other than the headchef can write it.\n");
        return;
    }
    if (size > sizeof(rc) - 1) size = sizeof(rc) - 1;     /* fat_read reports the file's size */
    rc[size] = '\0';
    set_session_uid(0);
    uint32_t i = 0;
    while (i < size) {
        uint32_t e = i;
        while (e < size && rc[e] != '\n' && rc[e] != '\r') e++;
        char line[INPUT_MAX];
        uint32_t n = e - i < INPUT_MAX - 1 ? e - i : INPUT_MAX - 1;
        memcpy(line, rc + i, n); line[n] = '\0';
        const char *l = line;
        while (*l == ' ') l++;
        if (*l && *l != '#') {
            klog("[rc] %s\n", l);
            run_line(l);
        }
        i = e + 1;
    }
}

void shell_run(void) {
    char input[INPUT_MAX];
    char saved[INPUT_MAX];
    int  pos = 0, len = 0;
    int  hist_browse = 0;

    input[0] = saved[0] = '\0';
    users_set_resolver(shell_uid_resolver);
    fat_set_creator_hook(shell_creator);
    the_shell.started = the_shell.last_input = timer_get_ticks();
    run_rc();                   /* /etc/rc, as headchef, before anyone logs in */
    do_login();                 /* clock in before the kitchen opens */
    prompt();
    shell_loop(input, saved, &pos, &len, &hist_browse);
}

/* The read-edit-dispatch loop, on cur_shell(). Returns when the terminal
 * hangs up (getc gives -1) or the shell clocks out of a session; the
 * console's never does either. */
static void shell_loop(char *input, char *saved, int *pos_p, int *len_p, int *hb_p) {
    #define pos (*pos_p)
    #define len (*len_p)
    #define hist_browse (*hb_p)
    while (1) {
        int c = t_getc();
        if (c < 0) break;
        cur_shell()->last_input = timer_get_ticks();
        if (c != '\t') cur_shell()->tabs = 0;
        if (c == 0x12) {                         /* Ctrl+R (v0.60.18) */
            if (!shell_search(input, &pos, &len)) { hist_browse = 0; continue; }
            c = '\n';                           /* Enter on a match runs it */
        }
        if (console_hangup && cur_shell() == &the_shell) {
            /* The pass caller left logged in (v0.55.1). */
            console_hangup = 0;
            klog("[pass] the caller left logged in as %s: clocking the console out\n", users_current_name());
            cmd_clockout();
            pos = len = hist_browse = 0;
            input[0] = saved[0] = '\0';
            prompt();
            continue;
        }

        if (c == '\n') {
            cursor_to(len);
            t_putc('\n');
            input[len] = '\0';
            hist_push(input);
            run_line(input);
            if (cur_shell()->exiting) break;
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
            t_color(VGA_DARK_GREY, VGA_BLACK);
            t_puts("^C\n");
            t_color(VGA_LIGHT_GREY, VGA_BLACK);
            pos = len = hist_browse = 0;
            input[0] = saved[0] = '\0';
            prompt();

        } else if (c == 0x16) {   /* Ctrl+V - paste the clipboard */
            uint32_t n = clip_len();
            const char *cb = clip_peek();
            for (uint32_t k = 0; k < n; k++) {
                /* The prompt is one line: a pasted newline becomes a space
                 * rather than submitting half a command by surprise. */
                char ch = cb[k];
                if (ch == '\n' || ch == '\t' || ch == '\r') ch = ' ';
                if (ch < ' ' || ch >= 127) continue;
                shell_insert(input, &pos, &len, ch);
            }

        } else if (c == 0x15) {   /* Ctrl+U - clear from cursor to start */
            shell_kill_to_start(input, &pos, &len);

        } else if (c == 0x17) {   /* Ctrl+W - delete word backwards */
            shell_kill_word(input, &pos, &len);

        } else if (c == '\t') {
            shell_tab_complete(input, &pos, &len);

        } else if (c == KEY_UP) {
            if (hist_browse == 0) strncpy(saved, input, INPUT_MAX);
            if (hist_browse < cur_shell()->hist_size) {
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
#undef pos
#undef len
#undef hist_browse

/* ── Sessions: a shell of its own on another terminal (v0.36.0) ───────────
 * The SSH server starts one per channel. The session task points its task at
 * the session's shell_t and terminal, so everything it runs inherits both:
 * output, input, the user, the foreground job and `kill %` all stay inside
 * the session. */
static void session_task(void *arg) {
    shell_t *sh = arg;
    task_t  *me = task_current();
    me->shell = sh;
    me->term  = sh->term;
    ensure_home(sh->uid, sh->cwd);            /* a session starts at home too */
    set_var_quiet("PWD", sh->cwd);
    if (!sh->exec_line[0]) hist_load();       /* and with its cook's leftovers */

    if (sh->exec_line[0]) {
        /* One command, no prompt and no echo: what `ssh host cmd` means. */
        run_line(sh->exec_line);
    } else {
        char input[INPUT_MAX], saved[INPUT_MAX];
        int  pos = 0, len = 0, hb = 0;
        input[0] = saved[0] = '\0';
        t_color(VGA_LIGHT_CYAN, VGA_BLACK);
        t_printf("\n  soupOS over SSH: a shell of your own. Welcome to the kitchen, %s.\n\n",
                 users_current_name());
        t_color(VGA_LIGHT_GREY, VGA_BLACK);
        show_motd();
        run_profile();
        prompt();
        shell_loop(input, saved, &pos, &len, &hb);
        if (!sh->exiting) hist_save();       /* hung up: clockout saved it otherwise */
    }
    t_color(VGA_LIGHT_GREY, VGA_BLACK);
    klog("[shell] session for %s ended, status %d\n", users_current_name(), sh->last_status);
    shell_register(sh, 0);
    sh->done = 1;
    task_exit();
}

void *shell_session_start(term_t *t, int uid, const char *exec_line, const char *where) {
    shell_t *sh = kmalloc(sizeof(shell_t));
    if (!sh) return 0;
    memset(sh, 0, sizeof(*sh));
    strcpy(sh->cwd, "/");
    sh->prompt_len = 8;
    sh->term = t;
    sh->uid  = (uint8_t)uid;
    strncpy(sh->where, where ? where : "?", sizeof(sh->where) - 1);
    sh->started = sh->last_input = timer_get_ticks();
    shell_register(sh, 1);
    if (exec_line) {
        strncpy(sh->exec_line, exec_line, INPUT_MAX - 1);
        sh->exec_line[INPUT_MAX - 1] = '\0';
    }
    task_t *tk = task_spawn(exec_line ? "sh:exec" : "sh:session", session_task, sh);
    if (!tk) { kfree(sh); return 0; }
    klog("[shell] session for uid %d started as task %u\n", uid, tk->id);
    return sh;
}

int shell_session_done(void *s, int *status) {
    shell_t *sh = s;
    if (!sh->done) return 0;
    if (status) *status = sh->last_status;
    return 1;
}

void shell_session_hangup(void *s) {
    shell_t *sh = s;
    proc_kill_by_term(sh->term);        /* its programs die with it, like SIGHUP */
}

void shell_session_free(void *s) { kfree(s); }
