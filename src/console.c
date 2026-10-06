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

static int      state       = S_NORMAL;
static uint32_t esc_tick    = 0;
static int      csi_num     = 0;
static int      last_was_cr = 0;

void console_init(void) {
    state       = S_NORMAL;
    csi_num     = 0;
    last_was_cr = 0;
    ready       = 1;
}

void console_set_input(int on)     { in_enabled  = on ? 1 : 0; }
void console_set_output(int on)    { out_enabled = on ? 1 : 0; }
int  console_input_enabled(void)   { return in_enabled; }
int  console_output_enabled(void)  { return out_enabled; }

/* ── output mirror ─────────────────────────────────────────────────────── */

void console_out_char(char c) {
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

/* Translate one byte in the NORMAL state and inject it. */
static void emit_normal(int c) {
    if (c == '\r') {
        last_was_cr = 1;
        keyboard_inject('\n');
        return;
    }
    if (c == '\n') {
        /* Swallow the LF of a CRLF pair - one keypress, one newline. */
        if (last_was_cr) { last_was_cr = 0; return; }
        keyboard_inject('\n');
        return;
    }
    last_was_cr = 0;

    if (c == 0x7F) { keyboard_inject('\b'); return; }   /* DEL -> backspace */
    keyboard_inject(c);
}

void console_rx_poll(void) {
    if (!ready || !in_enabled) return;

    int c;
    while ((c = serial_getc_nb()) >= 0) {
        switch (state) {
        case S_NORMAL:
            if (c == 0x1B) { state = S_ESC; esc_tick = timer_get_ticks(); }
            else            emit_normal(c);
            break;

        case S_ESC:
            if (c == '[') {
                state   = S_CSI;
                csi_num = 0;
            } else {
                /* Not a sequence after all: the ESC was a real keypress and
                 * this byte is the next one. */
                keyboard_inject(27);
                state = S_NORMAL;
                if (c == 0x1B) { state = S_ESC; esc_tick = timer_get_ticks(); }
                else            emit_normal(c);
            }
            break;

        case S_CSI:
            if (c >= '0' && c <= '9') {
                csi_num = c - '0';
                state   = S_CSI_NUM;
            } else {
                int k = csi_letter_to_key(c);
                if (k >= 0) keyboard_inject(k);
                /* Unknown sequences are dropped rather than injected as
                 * garbage into the line editor. */
                state = S_NORMAL;
            }
            break;

        case S_CSI_NUM:
            if (c >= '0' && c <= '9') {
                csi_num = csi_num * 10 + (c - '0');
                if (csi_num > 9999) csi_num = 9999;   /* don't overflow */
            } else {
                if (c == '~') {
                    int k = csi_num_to_key(csi_num);
                    if (k >= 0) keyboard_inject(k);
                }
                state = S_NORMAL;
            }
            break;
        }
    }

    /* No more bytes. If an ESC has been pending long enough that a sequence
     * would have completed by now, treat it as the Escape key. */
    if (state == S_ESC &&
        (uint32_t)(timer_get_ticks() - esc_tick) >= ESC_TIMEOUT_TICKS) {
        keyboard_inject(27);
        state = S_NORMAL;
    }
}
