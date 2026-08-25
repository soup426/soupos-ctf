#pragma once

/* jot - a tiny full-screen text editor for soupOS.
 *
 * editor_run() takes a clean absolute path (the shell resolves it first),
 * loads the file if it exists (else starts an empty buffer), and runs a
 * modal editor until the user quits. Saving writes the buffer back to the
 * FAT volume via fat_write(). The caller is responsible for permission
 * checks before launching and for redrawing its own UI afterward - the
 * editor clears the screen on exit.
 *
 * Keys: arrows/Home/End move, Backspace/Del erase, Enter splits a line,
 * Ctrl+S saves, Esc or Ctrl+Q quits (with a discard prompt if modified).
 */
void editor_run(const char *path);
