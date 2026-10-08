#!/usr/bin/env bash
# The in-kernel self-test, the mouse, the clipboard and the editor.
source "$(dirname "$0")/lib.sh"
gate_init desk
gate_boot
# The pointer starts mid-console (40,12 text, 64,24 framebuffer), so it is
# driven hard into the top-left corner first, where it clamps, then moved a
# known amount: 40 raw is ten cells at four counts per cell, and a POSITIVE
# dy moves down. That lands on 10,5 whatever size the console is.
gate_drive \
    "sample" "WAIT:6" \
    "MON:mouse_move -120 -120" "MON:mouse_move -120 -120" "MON:mouse_move -120 -120" \
    "MON:mouse_move 40 20" "skewer" "WAIT:2" \
    "strain MOUSE.TXT" "jot MOUSE.TXT" "WAIT:2" "TYPE:abcdefghij" "WAIT:1" \
    "MON:mouse_move -120 -120" "MON:mouse_move -120 -120" "MON:mouse_move -120 -120" "WAIT:1" \
    "MON:mouse_button 1" "WAIT:1" "MON:mouse_move 20 0" "WAIT:1" "MON:mouse_button 0" "WAIT:1" \
    "KEY:ctrl-c" "WAIT:1" "KEY:ctrl-s" "WAIT:1" "KEY:esc" "WAIT:2" "scraps" "WAIT:2" \
    "strain CLIP.TXT" "WAIT:1" \
    "TYPE:soup is good" "KEY:ctrl-u" "scraps" "WAIT:2" \
    "jot CLIP.TXT" "WAIT:2" "TYPE:abc def" \
    "KEY:home" "KEY:ctrl-b" "KEY:end" "KEY:ctrl-c" "KEY:ctrl-v" \
    "KEY:ctrl-s" "WAIT:1" "KEY:esc" "WAIT:1" \
    "cook weigh.elf < CLIP.TXT" "WAIT:3" \
    "TYPE:scraps" "KEY:ctrl-u" "KEY:ctrl-v" "KEY:ret" "WAIT:2" \
    "kitchen" "WAIT:3" "KEY:esc" "WAIT:1"
check "in-kernel self-test passes" '\[sample\] [0-9]+ tests, 0 failed'
check "a service's cpu share is reported" '\[sample\] busiest service: '
check "the mixer came up" '\[mixer\] 4 voices'
check "mouse tracks injected movement" '\[mouse\] at 10,5 '
# Drag-select five cells and ^C: the content is asserted, not the length,
# because a mapping off by one still copies five bytes, just the wrong five.
check "mouse drag-select feeds the clipboard" '\[clip\] 5 bytes: abcde'
check "Ctrl+U copies the killed text" '\[clip\] 12 bytes: soup is good'
# "abc def" marked, copied and pasted at the end: 14 bytes, 3 words, no newline.
check "jot copies and pastes a marked span" 'WCOUT lines=0 words=3 bytes=14'
check "Ctrl+V pastes at the prompt" '\[clip\] 6 bytes: scraps'
# kitchen redraws in place once a second until a key. Its summary line
# carries its own task's share of the cpu: a viewer that spends its time
# redrawing itself is measuring itself, so that must stay near zero, and the
# machine it was watching was idle.
check "kitchen ran and left on a key" '\[kitchen\] [2-9][0-9]* frames'
kline=$(grep -oE '\[kitchen\] [0-9]+ frames, idle [0-9]+%, self [0-9]+%' "$LOG" | tail -1)
kidle=$(echo "$kline" | grep -oE 'idle [0-9]+' | grep -oE '[0-9]+'); kself=$(echo "$kline" | grep -oE 'self [0-9]+' | grep -oE '[0-9]+')
if [ -n "$kself" ] && [ "$kself" -le 5 ] && [ "$kidle" -ge 50 ]; then
    echo "  ok    kitchen costs nothing to watch (self ${kself}%, idle ${kidle}%)"
else
    echo "  FAIL  kitchen's own cost or idle figure is off (${kline:-no line})"; fail=1
fi
gate_finish
