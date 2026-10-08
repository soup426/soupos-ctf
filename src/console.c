/* console.c - serial console: drive soupOS over COM1 with no keyboard.
 *
 * See console.h for the contract. The interesting part is the input side:
 * a terminal on the far end of a socket does not speak PS/2, so the bytes
 * it sends have to be translated into the codes the rest of the OS already
 * understands (plain ASCII plus the KEY_* values from keyboard.h).
 *
 * Three conventions have to be reconciled:
 *   - Enter arrives as CR (0x0D), or CRLF from some clients. The shell wants
 *     '\n', and must not see two newlines for one keypress.
 *   - Backspace arrives as DEL (0x7F) from virtually every terminal. The
 *     shell's line editor wants '\b'.
 *   - Arrows/Home/End/Del/PgUp/PgDn arrive as multi-byte ANSI escape
 *     sequences ("\033[A"), which have to be reassembled into one KEY_* code.
 *
 * The escape reassembly is a small state machine rather than a blocking read,
 * because console_rx_poll() is called from the scheduler's idle path and must
 * never block. A lone Escape keypress is indistinguishable from the start of
 * a sequence until either more bytes arrive or enough time passes, so a
 * pending ESC is flushed as a literal 27 after ESC_TIMEOUT_TICKS.
 */

#include "console.h"
#include <stdint.h>
#include "serial.h"
#include "keyboard.h"
#include "timer.h"

/* A lone ESC is only a lone ESC if nothing follows it promptly. At 100 Hz
 * this is a 20-30 ms grace period - far longer than the sub-millisecond gap
 * between bytes of a real escape sequence, and short enough that pressing
 * Escape feels instant. */
#define ESC_TIMEOUT_TICKS 3

enum { S_NORMAL, S_ESC, S_CSI, S_CSI_NUM };

static int      in_enabled  = 1;
static int      out_enabled = 1;
static int      ready       = 0;   /* set once serial_init() has run */


void console_init(void) {
    ready = 1;          /* the translator starts zeroed: S_NORMAL, no pending CR */
}

void console_set_input(int on)     { in_enabled  = on ? 1 : 0; }
void console_set_output(int on)    { out_enabled = on ? 1 : 0; }
int  console_input_enabled(void)   { return in_enabled; }
int  console_output_enabled(void)  { return out_enabled; }

/* ── output mirror ─────────────────────────────────────────────────────── */

/* A second place for output to go, so a network session sees the same stream
 * a serial one does. One sink is enough: soupOS has one shell, and two remote
 * viewers of one shell is a different feature. */
static void (*extra_sink)(char c);

void console_set_sink(void (*sink)(char c)) { extra_sink = sink; }

void console_out_char(char c) {
    /* The sink gets the raw character and does its own CR/backspace handling,
     * because a socket and a UART want the same expansion but the code below
     * is tangled with `ready`, which is about the UART only. */
    if (extra_sink) extra_sink(c);

    if (!ready || !out_enabled) return;

    if (c == '\n') {
        /* The VGA layer treats '\n' as "column 0, next row". A terminal needs
         * both halves spelled out or the next line starts under the cursor. */
        serial_putc('\r');
        serial_putc('\n');
    } else if (c == '\b') {
        /* vga_putchar erases the cell it backs over; "\b \b" is the terminal
         * equivalent (step back, overwrite with a space, step back again). */
        serial_putc('\b');
        serial_putc(' ');
        serial_putc('\b');
    } else {
        serial_putc(c);
    }
}

/* ── input translation ─────────────────────────────────────────────────── */

/* Map the final byte of a "\033[<letter>" sequence. */
static int csi_letter_to_key(int c) {
    switch (c) {
        case 'A': return KEY_UP;
        case 'B': return KEY_DOWN;
        case 'C': return KEY_RIGHT;
        case 'D': return KEY_LEFT;
        case 'H': return KEY_HOME;   /* xterm */
        case 'F': return KEY_END;    /* xterm */
        default:  return -1;
    }
}

/* Map the numeric form, "\033[<n>~" (linux console / putty / vt220). */
static int csi_num_to_key(int n) {
    switch (n) {
        case 1: case 7: return KEY_HOME;
        case 3:         return KEY_DEL;
        case 4: case 8: return KEY_END;
        case 5:         return KEY_PGUP;
        case 6:         return KEY_PGDN;
        default:        return -1;
    }
}

#define KEYOUT(k) x->out((k), x->ctx)

/* Translate one byte in the NORMAL state and inject it. */
static void emit_normal(keyxlate_t *x, int c) {
    if (c == '\r') {
        x->last_was_cr = 1;
        KEYOUT('\n');
        return;
    }
    if (c == '\n') {
        /* Swallow the LF of a CRLF pair - one keypress, one newline. */
        if (x->last_was_cr) { x->last_was_cr = 0; return; }
        KEYOUT('\n');
        return;
    }
    x->last_was_cr = 0;

    if (c == 0x7F) { KEYOUT('\b'); return; }   /* DEL -> backspace */
    KEYOUT(c);
}

/* One received byte, through the terminal-convention state machine. Split out
 * from the serial poll so a network session can feed it the same way: the
 * translation - CR, DEL, ANSI arrows - is identical whatever carried the byte. */
void keyxlate_byte(keyxlate_t *x, int c) {
    {
        switch (x->state) {
        case S_NORMAL:
            if (c == 0x1B) { x->state = S_ESC; x->esc_tick = timer_get_ticks(); }
            else            emit_normal(x, c);
            break;

        case S_ESC:
            if (c == '[') {
                x->state   = S_CSI;
                x->csi_num = 0;
            } else {
                /* Not a sequence after all: the ESC was a real keypress and
                 * this byte is the next one. */
                KEYOUT(27);
                x->state = S_NORMAL;
                if (c == 0x1B) { x->state = S_ESC; x->esc_tick = timer_get_ticks(); }
                else            emit_normal(x, c);
            }
            break;

        case S_CSI:
            if (c >= '0' && c <= '9') {
                x->csi_num = c - '0';
                x->state   = S_CSI_NUM;
            } else {
                int k = csi_letter_to_key(c);
                if (k >= 0) KEYOUT(k);
                /* Unknown sequences are dropped rather than injected as
                 * garbage into the line editor. */
                x->state = S_NORMAL;
            }
            break;

        case S_CSI_NUM:
            if (c >= '0' && c <= '9') {
                x->csi_num = x->csi_num * 10 + (c - '0');
                if (x->csi_num > 9999) x->csi_num = 9999;   /* don't overflow */
            } else {
                if (c == '~') {
                    int k = csi_num_to_key(x->csi_num);
                    if (k >= 0) KEYOUT(k);
                }
                x->state = S_NORMAL;
            }
            break;
        }
    }

    /* No more bytes. If an ESC has been pending long enough that a sequence
     * would have completed by now, treat it as the Escape key. */
    if (x->state == S_ESC &&
        (uint32_t)(timer_get_ticks() - x->esc_tick) >= ESC_TIMEOUT_TICKS) {
        KEYOUT(27);
        x->state = S_NORMAL;
    }
}

/* The serial console's own translator: keys go to the keyboard queue. */
static void console_key(int key, void *ctx) { (void)ctx; keyboard_inject(key); }
static keyxlate_t console_x = { 0, 0, 0, 0, console_key, 0 };

void console_rx_byte(int c) { keyxlate_byte(&console_x, c); }

void console_rx_poll(void) {
    if (!ready || !in_enabled) return;
    int c;
    while ((c = serial_getc_nb()) >= 0) console_rx_byte(c);
}

