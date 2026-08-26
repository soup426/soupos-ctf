#pragma once
/* wad.h — WAD file parser for the soupOS Doom port.
 *
 * Supports both IWAD and PWAD.  The directory is loaded into heap memory
 * once at init time; individual lumps are read on demand via the FAT
 * seekable-handle layer so the entire WAD never has to fit in RAM.
 */

#include <stdint.h>

#define WAD_NAME_LEN  8   /* lump names are exactly 8 bytes, zero-padded */

typedef struct {
    uint32_t filepos;              /* byte offset of lump data in the file  */
    uint32_t size;                 /* size in bytes                          */
    char     name[WAD_NAME_LEN];   /* NOT null-terminated; zero-padded       */
} wad_lump_t;

/* Open a WAD file and read its directory.
 * Returns 0 on success, -1 if the file is missing or not a valid WAD. */
int  wad_init    (const char *filename);
void wad_shutdown(void);          /* close file handle, free directory      */

int               wad_num_lumps(void);
const wad_lump_t *wad_get_lump (int idx);   /* NULL if out of range          */
uint32_t          wad_lump_size(int idx);   /* 0 if out of range             */

/* Find a lump by name (case-insensitive, up to 8 chars).
 * Returns lump index, or -1 if not found.
 * start_after: begin search after this index (for duplicate-name lumps);
 * pass -1 to search from the beginning. */
int wad_find_lump      (const char *name);
int wad_find_lump_after(const char *name, int start_after);

/* Read lump data into buf (at most bufsize bytes).
 * Returns the number of bytes actually read, or -1 on error / not found. */
int wad_read_lump     (int idx,           void *buf, uint32_t bufsize);
int wad_read_lump_name(const char *name,  void *buf, uint32_t bufsize);

/* Convenience: read the first palette from the PLAYPAL lump.
 * Writes 768 bytes (256 × R,G,B, 8-bit each) into out_rgb.
 * Returns 0=ok, -1=PLAYPAL not found. */
int wad_read_palette(uint8_t out_rgb[768]);
