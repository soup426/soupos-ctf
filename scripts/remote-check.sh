#!/usr/bin/env bash
# remote-check.sh - run check.sh on the test container instead of here
# (2026-10-09: the full check boots ~200 QEMUs and made the laptop unusable).
#
#   scripts/remote-check.sh              the full check, on CT 123
#   scripts/remote-check.sh quick        configs + gate only
#   KEEP=1 CHECK_JOBS=16 scripts/remote-check.sh   passed through
#
# CT 123 `soupos-test` on atlas: Arch (so bash, coreutils and the rest are
# the versions the tests compare against, as here), 16 cores, 8 GB,
# /dev/kvm passed through, onboot 0 (`pct start 123` on atlas when it is
# down). It is reached through atlas with `pct exec`, so the only ssh is
# the shared one to atlas: one YubiKey touch every four idle hours, and
# nothing on the container's own sshd. rsync rides the same way; `env -u
# X` swallows the host name rsync puts before its command.
#
# The tree goes without objects, .git, ISOs or ELFs (check.sh builds them
# there from scratch); DOOM1.WAD is followed through its symlink. disk.img
# goes as it is here, with the user programs built here inside it: rebuild
# it here first when anything the Makefile embeds changed (`rm disk.img &&
# make disk`). Only the copy and the ssh run on this machine.
#
# The container needs what the tests use from the host, as this machine has
# them (Arch packages): base-devel gcc nasm mtools dosfstools libisoburn grub
# qemu-system-x86 python python-pexpect rsync psmisc openbsd-netcat; and
# DisableSandbox in pacman.conf (an unprivileged container has no Landlock).
set -uo pipefail
cd "$(dirname "$0")/.."
VIA=${SOUPOS_TEST_VIA:-atlas}
CT=${SOUPOS_TEST_CT:-123}
DIR=/root/soupOS
X() { ssh "$VIA" "pct exec $CT -- $*"; }
X mkdir -p $DIR || { echo "  FAIL  cannot reach CT $CT through $VIA (is it running? pct start $CT)"; exit 2; }
rsync -a --delete --exclude .git --exclude '*.o' --exclude '*.d' --exclude '*.iso' --exclude '*.elf' \
      --exclude DOOM1.WAD -e "ssh $VIA pct exec $CT -- env -u" ./ X:$DIR/ || exit 2
[ -e DOOM1.WAD ] && { rsync -aL -e "ssh $VIA pct exec $CT -- env -u" DOOM1.WAD X:$DIR/DOOM1.WAD || exit 2; }
# pct exec starts its command with SIGPIPE ignored, and a shell cannot undo
# an ignore it was born with: bash then shows `trap -- '' SIGPIPE` and
# dot-test and trap-test, which compare with bash, differ. env puts it back.
X env --default-signal=PIPE bash -c "'cd $DIR && KEEP=${KEEP:-0} ${CHECK_JOBS:+CHECK_JOBS=$CHECK_JOBS} scripts/check.sh ${1:-full}'"
