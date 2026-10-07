#!/usr/bin/env bash
# The script language: long strings, scoping, the runaway budget, spawn.
source "$(dirname "$0")/lib.sh"
gate_init soupyc
gate_boot
gate_drive \
    "soup LONGSTR.SC" "WAIT:6" \
    "soup SCOPE.SC" "WAIT:4" \
    "soup RUNAWAY.SC" "WAIT:22" "pwd" "WAIT:2" \
    "soup SPAWN.SC" "WAIT:1" "ps" "WAIT:5"
# Exact lengths, so a truncation anywhere shows up as the wrong number.
check "soupyc concatenates past 47 chars" 'built 100'
check "soupyc takes a long literal"       'literal 97'
check "soupyc reads 100 bytes of a file"  'file 100'
check "soupyc can delete a file"          'removed 1'
# The budget has to fail cleanly rather than taking the kernel with it.
check "runaway strings fail cleanly" 'out of string memory'
# A `let` inside a while body used to accumulate a binding per pass.
check "a let inside a loop survives 200 passes" 'SCOPE total 39800'
# spawn: a script function running as its own task.
check "spawn returns a task id"            '\[parent\] spawned [0-9]+'
check "the spawned script is a live task"  'soup:ticker'
check "parent keeps its arrays"            '\[parent\] array still 2 2'
check "child gets arrays of its own"       '\[child\] array 3 8'
check "the spawned script finishes"        '\[child\] done'
gate_finish
