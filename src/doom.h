#pragma once
/* doom.h — high-level Doom interface for the soupOS shell.
 *
 * All functions open DOOM1.WAD on demand and close it when done (or keep a
 * cached handle via the wad module across calls within one session).
 */

/* Full interactive session: title screen + main menu (episode/skill nav).
 * Stays in VGA mode 13h until the user selects Quit Game. */
void doom_menu_run(void);

/* Show just the TITLEPIC title screen, wait for a key, return to text mode. */
void doom_titlescreen(void);

/* Load map ExMy (ep=0-based episode) and run the interactive automap.
 * Must be called while already in VGA mode 13h with the Doom palette set.
 * Controls: W/S/↑↓ move, A/D strafe, ←/→ turn, ,/. zoom, ESC quit. */
void doom_play_level(int ep, int skill);

/* Print WAD summary to the text console (type, numlumps, label). */
void doom_wad_info(void);

/* Print a formatted table of all lump names + sizes to the text console.
 * Pauses every screenful and waits for a key. */
void doom_lump_list(void);
