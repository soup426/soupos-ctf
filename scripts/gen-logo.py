#!/usr/bin/env python3
"""Turn assets/logo.txt into src/logo.h.

The asset is UTF-8 block-drawing characters, which is the right way to store
it: it is editable, diffable, and the same bytes as the branding copy it came
from. It is the WRONG thing to compile into the kernel, because VGA text mode
prints one glyph per byte, so a three-byte UTF-8 sequence becomes three
garbage characters per block. This maps each character to its CP437 code -
the encoding the VGA hardware font and soupOS's embedded 8x16 font both use -
and emits both forms: CP437 for the screen, UTF-8 for the serial log.

Fails loudly on any character outside the four the logo is made of, because a
stray non-breaking space or a different block glyph would otherwise reach the
screen as whatever CP437 happens to have at that code point.
"""
import sys
from pathlib import Path

CP437 = {"█": 0xDB, "▄": 0xDC, "▀": 0xDF, " ": 0x20}

def main() -> int:
    root = Path(__file__).resolve().parent.parent
    src = root / "assets" / "logo.txt"
    dst = root / "src" / "logo.h"

    text = src.read_text(encoding="utf-8")
    rows = text.rstrip("\n").split("\n")

    bad = {}
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch not in CP437:
                bad.setdefault((ch, hex(ord(ch))), []).append((y + 1, x + 1))
    if bad:
        for (ch, code), where in bad.items():
            print(f"{src}: {code} ({ch!r}) is not one of "
                  f"{''.join(CP437)!r}; first at row {where[0][0]}, column {where[0][1]}",
                  file=sys.stderr)
        return 1
    if any(row != row.rstrip() for row in rows):
        print(f"{src}: trailing whitespace", file=sys.stderr)
        return 1

    width = max(len(r) for r in rows)

    out = ['/* Generated from assets/logo.txt by scripts/gen-logo.py - do not edit.',
           ' *',
           ' * Two encodings of the same picture. logo_cp437 is for the screen: one',
           ' * byte per glyph, in the encoding the VGA font uses (0xDB full block,',
           ' * 0xDC lower half, 0xDF upper half). logo_utf8 is for the serial log,',
           ' * where a terminal expects UTF-8 and would render the CP437 bytes as',
           ' * noise. Print the first with vga_puts_screen_only and send the second',
           ' * to klog; using vga_puts for the CP437 form would mirror those bytes',
           ' * straight down the serial line.',
           ' */',
           '#pragma once',
           '',
           f'#define LOGO_ROWS {len(rows)}',
           f'#define LOGO_COLS {width}',
           '',
           'static const char *const logo_cp437[LOGO_ROWS] = {']
    for row in rows:
        esc = "".join("\\x%02X" % CP437[ch] for ch in row)
        out.append(f'    "{esc}",')
    out += ['};', '', 'static const char *const logo_utf8[LOGO_ROWS] = {']
    for row in rows:
        out.append(f'    "{row}",')
    out += ['};', '']

    dst.write_text("\n".join(out), encoding="utf-8")
    print(f"{dst}: {len(rows)} rows, {width} columns")
    return 0

if __name__ == "__main__":
    sys.exit(main())
