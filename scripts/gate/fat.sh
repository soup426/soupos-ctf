#!/usr/bin/env bash
# The filesystem under two writers at once, and the free-space report.
source "$(dirname "$0")/lib.sh"
gate_init fat
gate_boot
gate_drive "fatstress" "WAIT:30" "larder" "WAIT:2"
check "concurrent FAT is safe"      '\[fatstress\] PASS'
# Free space had no way to be seen from inside soupOS before, which is part
# of why a cluster leak could sit in the write path unnoticed.
check "larder reports disk space" 'clusters used \([0-9]+ KB each\)'
gate_finish
