# soupOS — CTFd-ployer `tcp` challenge image.
#
# Players connect with `nc <host> <port>` and land in soupOS's shell over its
# COM1 serial console. socat forks a fresh QEMU per connection.
#
# Build from the repo root (the ISO and disk image must exist):
#   make && make disk
#   docker build -f challenge/Dockerfile -t soupos-chal .
FROM debian:bookworm-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends qemu-system-x86 socat \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/soupos
COPY soupOS.iso disk.img /opt/soupos/
COPY challenge/run-instance.sh /usr/local/bin/run-instance
RUN chmod +x /usr/local/bin/run-instance

# Unprivileged: nothing here needs root, and the launcher runs untrusted
# images by design.
RUN useradd -m -u 1000 soupos
USER soupos

EXPOSE 9999

# max-children caps concurrent VMs so one player cannot exhaust the host by
# opening connections in a loop (each instance is 128 MB).
CMD ["socat", "TCP-LISTEN:9999,reuseaddr,fork,max-children=8", "EXEC:/usr/local/bin/run-instance"]
