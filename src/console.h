#pragma once

/* console.h - serial console.
 *
 * Lets soupOS be driven over COM1 with no PS/2 keyboard and no VGA window,
 * which is what makes it hostable behind a raw TCP socket (`nc host port`).
 *
 * Two halves, independently switchable:
 *   input  - bytes arriving on COM1 are translated from terminal conventions
 *            (CR, DEL, ANSI arrow/Home/End escapes) into the same codes the
 *            PS/2 driver produces, then injected into the keyboard queue.
 *            Both input paths stay live, so the QEMU window still works.
 *   output - every character written through vga_putchar is mirrored to COM1,
 *            with LF expanded to CRLF and backspace expanded to "\b \b" so a
 *            real terminal erases the way the VGA text buffer does.
 *
 * Both default to ON at boot. The mirror is what a remote player actually
 * sees; without it a socket-attached client gets a prompt it cannot read.
 *
 * Not mirrored: anything that writes the 0xB8000 cell buffer directly rather
 * than going through vga_putchar - `jot` and the mode-13h demos (bounce,
 * cat). Those remain VGA-only by nature.
 */

void console_init(void);

/* Drain COM1's receive FIFO, translate, and inject into the keyboard queue.
 * Non-blocking; safe to call from a poll loop. Called by keyboard_available()
 * and keyboard_getchar() so every existing input site picks serial up for
 * free. */
void console_rx_poll(void);

/* Output mirror, called from vga_putchar. */
void console_out_char(char c);

/* Runtime switches (both start enabled). */
void console_set_input(int on);
void console_set_output(int on);
int  console_input_enabled(void);
int  console_output_enabled(void);
