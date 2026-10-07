#pragma once

/* The 80x25 text console rendered onto the linear framebuffer, for FB=1
 * boots. Watches the cell buffer at 0xB8000 and repaints what changes, so
 * everything that writes text cells keeps working unmodified. Start it after
 * task_init; it is a task. */
int fbcon_start(void);
