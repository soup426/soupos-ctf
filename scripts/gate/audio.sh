#!/usr/bin/env bash
# A tone, a Doom sound effect and a MUS score through the mixer. The fine
# measurements (frequency, length, the right notes) live in sound-test.sh,
# mixer-test.sh and music-test.sh, which each need a capture with nothing
# else in it; this is what the serial log can answer on its own.
source "$(dirname "$0")/lib.sh"
gate_init audio
gate_boot
# Each waits for its own line (v0.60.150): fixed waits ran out under
# check.sh's load, twice in a row, with the tone logged and the rest not.
gate_drive "whistle 440 300" "UNTIL:[mixer] tone 440" "WAIT:3" "sizzle DSPISTOL" "UNTIL:[doomsnd] DSPISTOL" "WAIT:3" \
           "hum D_E1M1" "UNTIL:[music] D_E1M1" "WAIT:2" "hum" "WAIT:2"
check "the mixer accepted a tone" '\[mixer\] tone 440 Hz'
check "a Doom sound lump was decoded" '\[doomsnd\] DSPISTOL: 5661 samples at 11025 Hz'
check "a MUS score was parsed" '\[music\] D_E1M1: 17237 bytes of score, 3 channels'
gate_finish
