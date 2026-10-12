#pragma once
#include <stdint.h>

/* Special key codes returned by keyboard_getchar() - values above ASCII */
#define KEY_UP    256
#define KEY_DOWN  257
#define KEY_LEFT  258
#define KEY_RIGHT 259
#define KEY_DEL   260
#define KEY_HOME  261
#define KEY_END   262
#define KEY_PGUP  263
#define KEY_PGDN  264
#define KEY_F1    265           /* F1-F12: KEY_F1 + n - 1 (v0.60.171) */
#define KEY_F4    268
#define KEY_F12   276
/* Alt combinations, decided in the IRQ when the key goes down (v0.60.171):
 * read later, Alt may already be up (under load an Alt+F4 was lost). */
#define KEY_ALT_TAB     277
#define KEY_ALT_F4      278
#define KEY_CTRL_ALT_T  279
#define KEY_PRINT       280   /* Print Screen, E0 37 (v0.60.185) */
#define KEY_ALT_UP      281   /* Alt let go after an Alt+Tab (v0.60.203): the desktop's panel */

/* Set-1 scancode constants for keyboard_key_pressed().
 * Extended (0xE0-prefix) keys are stored with bit 7 set (sc | 0x80). */
#define KEY_SC_TAB      0x0F
#define KEY_SC_ESC      0x01
#define KEY_SC_ENTER    0x1C
#define KEY_SC_SPACE    0x39
#define KEY_SC_LSHIFT   0x2A
#define KEY_SC_RSHIFT   0x36
#define KEY_SC_LCTRL    0x1D
#define KEY_SC_LALT     0x38
#define KEY_SC_UP       (0x48 | 0x80)
#define KEY_SC_DOWN     (0x50 | 0x80)
#define KEY_SC_LEFT     (0x4B | 0x80)
#define KEY_SC_RIGHT    (0x4D | 0x80)
#define KEY_SC_A   0x1E
#define KEY_SC_B   0x30
#define KEY_SC_C   0x2E
#define KEY_SC_D   0x20
#define KEY_SC_E   0x12
#define KEY_SC_F   0x21
#define KEY_SC_G   0x22
#define KEY_SC_H   0x23
#define KEY_SC_S   0x1F
#define KEY_SC_W   0x11
#define KEY_SC_X   0x2D
#define KEY_SC_Y   0x15
#define KEY_SC_Z   0x2C
#define KEY_SC_COMMA 0x33
#define KEY_SC_DOT   0x34

void keyboard_init(void);
int  keyboard_getchar(void);    /* blocks; returns char value or KEY_* */
int  keyboard_available(void);
int  keyboard_key_pressed(uint8_t scancode); /* 1 if held, 0 if not */
void keyboard_key_bitmap(uint8_t out[32]);   /* key_state as bits (v0.60.190) */
/* Inject a character/KEY_* code into the input queue as if it had been
 * typed. Used by the serial console so a socket-attached client feeds the
 * same queue as the PS/2 driver. */
void keyboard_inject(int c);

void keyboard_flush(void);                   /* drain ASCII queue    */
