#!/bin/sh
# One soupOS instance, reachable from a browser.
#
#   browser --https--> nginx --> websockify :8080 --> QEMU VNC 127.0.0.1:5900
#
# websockify serves the noVNC client (static HTML/JS) *and* bridges its
# WebSocket to QEMU's VNC server, so one process covers both and the container
# exposes exactly one port.
#
# The VNC server binds 127.0.0.1 so it is reachable only from inside this
# container -- players get to it through websockify, never directly, so there
# is no unauthenticated VNC port on the host.
set -e

VNC_DISPLAY=0                # -> TCP 5900
WEB_PORT=${WEB_PORT:-8080}

qemu-system-i386 \
    -drive file=/opt/soupos/disk.img,format=raw,if=ide,index=0,snapshot=on \
    -cdrom /opt/soupos/soupOS.iso -boot d \
    -m 128M \
    -no-reboot -no-shutdown \
    -vnc 127.0.0.1:${VNC_DISPLAY} \
    -monitor unix:/tmp/qemu-monitor.sock,server,nowait \
    -serial file:/tmp/soupos-serial.log &

QEMU_PID=$!

# If QEMU dies the instance is useless -- take the container down with it so
# the launcher's reaper cleans up instead of leaving a page that never loads.
trap 'kill $QEMU_PID 2>/dev/null' TERM INT

# Wait for the VNC port rather than sleeping a fixed amount.
for _ in $(seq 1 50); do
    if (echo > /dev/tcp/127.0.0.1/5900) 2>/dev/null; then break; fi
    sleep 0.2
done

exec websockify --web=/usr/share/novnc "${WEB_PORT}" 127.0.0.1:5900
