#!/usr/bin/env bash
# Text output from two tasks at once, and VFAT long names read and written.
source "$(dirname "$0")/lib.sh"
gate_init vga
gate_boot
gate_drive \
    "vgastress" "WAIT:25" \
    "serve" "WAIT:2" \
    "pour A Long Recipe Name.txt" "WAIT:2" \
    "pour ALONGR~1.TXT" "WAIT:2" \
    "soup LONGWR.SC" "WAIT:5"
# vgastress prints whole lines of a single repeated character from two tasks.
# Any line containing both means vga_puts was interrupted mid-string.
mixed=$(sed -n '/\[vgastress\] begin/,/\[vgastress\] end/p' "$LOG" | grep -c 'A.*B\|B.*A' || true)
if [ "$mixed" = "0" ]; then echo "  ok    vga output not interleaved"
else echo "  FAIL  vga output interleaved on $mixed lines"; fail=1; fi
# VFAT long names. The listing must show the name a person typed, and BOTH
# names must open the file: the long one and the mangled 8.3 alias mtools
# generated beside it.
check "a long name is listed"            'A Long Recipe Name.txt'
check "a long name opens the file"       'A file whose name does not fit in 8.3'
check "the 8.3 alias opens it as well"   -c2 'A file whose name does not fit in 8.3'
# Writing them: two files whose aliases collide on the same stem, read back.
check "a long name was written and read back" 'read back written by soupOS'
check "a colliding alias got its own name"    'read two the second one'
gate_finish
