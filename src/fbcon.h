#pragma once

/* The 80x25 text console rendered onto the linear framebuffer, for FB=1
 * boots. Watches the cell buffer at 0xB8000 and repaints what changes, so
 * everything that writes text cells keeps working unmodified. Start it after
 * task_init; it is a task. */
int fbcon_start(void);

/* Stop and restart painting, for programs that take the whole framebuffer.
 * vga13h_enter/exit call these, so every mode 13h program gets it free. */
void fbcon_pause(void);
void fbcon_resume(void);
