#!/bin/sh
# One soupOS instance, wired to stdin/stdout.
#
# socat runs this once per incoming TCP connection, so every player (and
# every reconnect) gets a brand new VM. `snapshot=on` sends disk writes to a
# throwaway overlay, so nothing a player does to the filesystem survives
# their session or leaks into anyone else's.
#
# -monitor none matters: without it QEMU can put its monitor on stdio, and
# a player who reaches the monitor owns the host process, which is a trivial
# bypass of the entire challenge.
exec qemu-system-i386 \
    -drive file=/opt/soupos/disk.img,format=raw,if=ide,index=0,snapshot=on \
    -cdrom /opt/soupos/soupOS.iso -boot d \
    -m 128M \
    -no-reboot -no-shutdown \
    -display none \
    -monitor none \
    -serial stdio
