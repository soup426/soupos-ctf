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
/* Inject a character/KEY_* code into the input queue as if it had been
 * typed. Used by the serial console so a socket-attached client feeds the
 * same queue as the PS/2 driver. */
void keyboard_inject(int c);

void keyboard_flush(void);                   /* drain ASCII queue    */
