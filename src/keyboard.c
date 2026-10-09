#include "keyboard.h"
#include "term.h"
#include "random.h"
#include "isr.h"
#include "task.h"
#include "proc.h"
#include "console.h"

/* PS/2 scancodes set 1 -> ASCII, unshifted */
static const char scancode_table[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0, '*', 0, ' ',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

/* Shifted versions */
static const char scancode_shift_table[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0, '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0, '*', 0, ' ',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

/* Ring buffer - stores int so KEY_* values (>= 256) fit cleanly */
#define KB_BUF_SIZE 256
static int      kb_buf[KB_BUF_SIZE];
static uint32_t kb_read  = 0;
static uint32_t kb_write = 0;

static int shift_held = 0;
static int ctrl_held  = 0;
static int e0_prefix  = 0;   /* set when 0xE0 escape byte is received */

/* Live key-state table - 1 = currently pressed, indexed by set-1 scancode.
 * Extended (0xE0-prefixed) keys use index 0x80+ (e0_sc | 0x80). */
#define KEY_STATE_SIZE 256
static uint8_t key_state[KEY_STATE_SIZE];

static inline uint8_t inb(uint16_t port) {
    uint8_t r; __asm__ volatile ("inb %1, %0" : "=a"(r) : "Nd"(port)); return r;
}
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void kb_push(int val) {
    uint32_t next = (kb_write + 1) % KB_BUF_SIZE;
    if (next != kb_read) {
        kb_buf[kb_write] = val;
        kb_write = next;
    }
}

/* Public face of kb_push, for the serial console. Serial input and PS/2
 * input share one queue, so both work at once and every existing reader
 * (shell, editor, soupyc `input`) picks up serial for free. */
void keyboard_inject(int c) { kb_push(c); }

static void keyboard_irq_handler(registers_t *regs) {
    (void)regs;
    uint8_t sc = inb(0x60);
    random_stir(sc);        /* keystroke timing feeds the pool */

    /* Extended key sequence prefix */
    if (sc == 0xE0) { e0_prefix = 1; return; }

    /* Key release: high bit set */
    if (sc & 0x80) {
        uint8_t code = sc & 0x7F;
        uint8_t idx  = e0_prefix ? (uint8_t)(code | 0x80) : code;
        key_state[idx] = 0;  /* idx is uint8_t, always < KEY_STATE_SIZE(256) */
        if (!e0_prefix && (code == 0x2A || code == 0x36))
            shift_held = 0;
        if (code == 0x1D)  /* left ctrl, or right ctrl (0xE0 0x1D) */
            ctrl_held = 0;
        e0_prefix = 0;
        return;
    }

    if (e0_prefix) {
        uint8_t idx = (uint8_t)(sc | 0x80);
        key_state[idx] = 1;  /* idx is uint8_t, always < KEY_STATE_SIZE(256) */
        e0_prefix = 0;
        if (sc == 0x1D) { ctrl_held = 1; return; }   /* right ctrl */
        /* Map extended scancodes to KEY_* values for the ASCII queue */
        switch (sc) {
            case 0x48: kb_push(KEY_UP);    return;
            case 0x50: kb_push(KEY_DOWN);  return;
            case 0x4B: kb_push(KEY_LEFT);  return;
            case 0x4D: kb_push(KEY_RIGHT); return;
            case 0x53: kb_push(KEY_DEL);   return;
            case 0x47: kb_push(KEY_HOME);  return;
            case 0x4F: kb_push(KEY_END);   return;
            case 0x49: kb_push(KEY_PGUP);  return;
            case 0x51: kb_push(KEY_PGDN);  return;
            default:   return;
        }
    }

    /* Normal key press */
    key_state[sc] = 1;  /* sc is uint8_t, always < KEY_STATE_SIZE(256) */
    if (sc == 0x2A || sc == 0x36) { shift_held = 1; return; }
    if (sc == 0x1D) { ctrl_held = 1; return; }
    if (sc >= 128) return;

    char c = shift_held ? scancode_shift_table[sc] : scancode_table[sc];
    if (c) {
        /* Ctrl+letter -> ASCII control char (Ctrl+A=1 .. Ctrl+Z=26).
         * Skip 0x08/0x09/0x0A/0x0D so backspace/tab/enter still work normally. */
        if (ctrl_held && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
            int ctl = c & 0x1F;
            if (ctl != '\b' && ctl != '\t' && ctl != '\n' && ctl != '\r')
                c = (char)ctl;
        }
        /* Ctrl-C is the terminal driver's job, not the shell's. While a
         * foreground program runs, the shell is blocked in proc_wait and
         * nobody is reading keys, so the interrupt has to be raised here, from
         * the IRQ. Flag the program and swallow the key. With no foreground
         * program it falls through to the shell, which uses 0x03 to cancel the
         * input line. */
        if (c == 0x03) term_vga.intr++;          /* for shell loops (v0.60.1) */
        if (c == 0x03 || c == 0x1A) {
            proc_t *fg = proc_foreground();
            if (fg) {
                if (c == 0x03) proc_flag_kill(fg);   /* Ctrl-C: terminate */
                else           proc_flag_stop(fg);   /* Ctrl-Z: park it   */
                return;
            }
        }
        kb_push((int)(unsigned char)c);
    }
}

/* Wait for i8042 input buffer empty */
static void i8042_wait_write(void) {
    int t = 100000;
    while ((inb(0x64) & 0x02) && --t) {}
}

/* Drain i8042 output buffer */
static void i8042_flush(void) {
    int t = 1000;
    while ((inb(0x64) & 0x01) && --t) inb(0x60);
}

void keyboard_init(void) {
    i8042_flush();

    i8042_wait_write(); outb(0x64, 0xAE);  /* enable first PS/2 port */

    i8042_wait_write(); outb(0x64, 0x20);  /* read config byte */
    int t = 100000;
    while (!(inb(0x64) & 0x01) && --t) {}
    uint8_t cfg = inb(0x60);
    cfg |=  0x01;   /* enable IRQ1 */
    cfg &= ~0x10;   /* clear disable bit */
    i8042_wait_write(); outb(0x64, 0x60);
    i8042_wait_write(); outb(0x60, cfg);

    i8042_wait_write(); outb(0x60, 0xF4);  /* enable scanning */
    i8042_flush();

    irq_register(1, keyboard_irq_handler);
}

int keyboard_available(void) {
    console_rx_poll();               /* drain COM1 into the same queue */
    return kb_read != kb_write;
}

int keyboard_getchar(void) {
    while (!keyboard_available()) {
        /* Let any other ready task run. task_yield is a safe no-op before
         * task_init() so pre-scheduler callers (none today) still work. */
        task_yield();
        /* If nothing else was runnable we just spun back here. Park the
         * CPU until the next IRQ (keyboard or 100 Hz timer) wakes us. */
        if (!keyboard_available())
            cpu_halt();
    }
    int c = kb_buf[kb_read];
    kb_read = (kb_read + 1) % KB_BUF_SIZE;
    return c;
}

/* Return 1 if the key with the given set-1 scancode is currently held.
 * Extended (0xE0-prefixed) keys: pass (sc | 0x80).
 * Scancode reference (partial):
 *   0x01=Esc 0x1C=Enter 0x39=Space 0x48=Up(ext) 0x50=Down(ext)
 *   0x4B=Left(ext) 0x4D=Right(ext) 0x1E=A 0x30=B 0x2E=C ...
 * KEY_SC_* constants in keyboard.h cover the Doom-relevant ones.     */
int keyboard_key_pressed(uint8_t sc) {
    return (int)key_state[sc];  /* sc is uint8_t, always < KEY_STATE_SIZE(256) */
}

/* Drain the ASCII queue without blocking */
void keyboard_flush(void) {
    kb_read = kb_write;
}
