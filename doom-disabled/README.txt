Doom port — temporarily disabled (2026-05-20)
=============================================

The Doom port was pulled out of the build to focus on core OS work.
Nothing here is deleted — re-enable it once the OS is more developed.

Files parked here:
  doom.c / doom.h   the port itself (~2200 lines)
  wad.c  / wad.h    the WAD reader (only used by doom.c)

Still in src/ (do NOT move — used kernel-wide):
  doom_libc.c/.h    freestanding libc (memcpy, strlen, malloc shims, ...)
  vga13h.c/.h       mode 13h graphics — used by `bounce`, `mandelbrot`, `vgademo`
  errno/math/ctype/string/stdio/stdlib.h   hosted-C shims over doom_libc

To re-enable:
  1. mv doom.c doom.h wad.c wad.h ../src/
  2. shell.c: re-add `#include "doom.h"`, the cmd_doom() function,
     the `doom` dispatch branch, and "doom" in cmd_names[].
  3. The Makefile globs src/*.c, so no Makefile change is needed.
