/* challenge.c — CTF content. See challenge.h. */

#include "challenge.h"
#include "vga.h"
#include "fat.h"
#include "users.h"
#include "heap.h"
#include "str.h"
#include "klog.h"

/* ── Flags ────────────────────────────────────────────────────────────────
 * Kept together so there is one place to audit before an event. Each is under
 * soupyc's 47-character string cap, because stage 1 is read with a script.  */
#define FLAG1 "cdctf{mise_en_place_two_paths_one_check}"
#define FLAG2 "cdctf{salt_to_taste_any_preimage_works}"
#define FLAG3 "cdctf{bad_recipe_the_loader_reads_wide}"
#define FLAG4 "cdctf{too_many_cooks_ring0_from_a_script}"

/* ── Stage 3 target ───────────────────────────────────────────────────────
 * Pinned in the kernel heap at boot. The ELF loader validates where it writes
 * but not where it reads from, so a crafted p_offset walks off the end of the
 * file buffer and copies whatever follows into the program's own image. This
 * is what it finds. Deliberately kmalloc'd rather than static: the story is
 * "the loader read past its buffer", and the heap is where that lands.       */
static char *g_stage3;

void challenge_init(void) {
    /* Stage 3: pin the flag in the heap. */
    g_stage3 = (char *)kmalloc(64);
    if (g_stage3) {
        strcpy(g_stage3, FLAG3);
        klog("[chal] stage3 flag pinned at %p\n", (void *)g_stage3);
    }

    /* Players need somewhere to write. The root bowl is owner-write only, so
     * a cook cannot create a script there, and every stage from 1 onwards
     * needs to drop a file (a soupyc script, later a crafted ELF). Give them
     * a scratch bowl that cooks can write. */
    if (!fat_exists("/prep")) {
        if (fat_mkdir("/prep") < 0) klog("[chal] could not create /prep\n");
    }
    if (fat_chmod("/prep", FAT_PERM_MARK |
                           FAT_PERM_OR | FAT_PERM_OW | FAT_PERM_OX |
                           FAT_PERM_AR | FAT_PERM_AW | FAT_PERM_AX) < 0)
        klog("[chal] could not open up /prep\n");
    else
        klog("[chal] /prep is cook-writable\n");

    /* Stage 1: the flag file exists, is owned by headchef, and is readable
     * only by its owner. The shell's may() honours that, so a cook is
     * refused; soupyc's open() never asks, which is the bug being taught. */
    uint32_t sz = 0;
    uint8_t  probe[8];
    if (fat_read("FLAG1.TXT", probe, sizeof(probe), &sz) < 0) {
        char buf[64];
        int n = 0;
        const char *p = FLAG1 "\n";
        while (p[n] && n < (int)sizeof(buf) - 1) { buf[n] = p[n]; n++; }
        if (fat_write("FLAG1.TXT", (const uint8_t *)buf, (uint32_t)n) < 0)
            klog("[chal] could not create FLAG1.TXT\n");
    }
    /* Owner may read and write; other cooks get nothing. soupOS modes are a
     * 6-bit rwx pair plus a presence marker, not Unix octal. */
    if (fat_chown("FLAG1.TXT", 0) < 0 ||
        fat_chmod("FLAG1.TXT",
                  FAT_PERM_MARK | FAT_PERM_OR | FAT_PERM_OW) < 0)
        klog("[chal] could not stamp FLAG1.TXT ownership\n");
    else
        klog("[chal] FLAG1.TXT owned by headchef, mode 600\n");
}

/* ── Stage 2 ──────────────────────────────────────────────────────────────
 * Deliberately not a file. The stage 1 bypass reads any file on the volume,
 * so a FLAG2 file would be readable at stage 1 and the chain would collapse
 * into a single step. Gating on uid 0 is what makes stage 2 load-bearing.   */
void challenge_special(void) {
    if (!users_is_headchef()) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_puts("  Only the headchef knows today's special.\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("  Today's special: " FLAG2 "\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* ── Stage 4 ──────────────────────────────────────────────────────────────
 * Nothing in the tree calls this. Reaching it is the challenge: soupyc's
 * array assignment no longer rejects a negative index, so a script can write
 * below the array pool and redirect a kernel function pointer here.         */
void serve_the_special(void) {
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_puts("\n  The kitchen is yours: " FLAG4 "\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    klog("[chal] serve_the_special reached\n");
}
