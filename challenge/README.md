# soupOS as a CTFd-ployer challenge

Packages soupOS as a `tcp` challenge: players connect with `nc` and land in
the shell over the COM1 serial console (see `src/console.c`).

## Build

From the repo root — the ISO and disk image must exist first:

```bash
make && make disk                                    # placeholder flag
make distclean && make disk FLAG='flag{the_real_one}' && make   # real flag
docker build -f challenge/Dockerfile -t soupos-chal .
```

The flag is baked into `disk.img` as `FLAG.TXT` (Makefile `FLAG` variable), so
it is identical for every instance. This is deliberate — it is not a
CTFd-ployer dynamic per-team flag.

## Deploy

The image must exist on the deployer host's Docker daemon (the launcher runs
containers there, it does not pull from a registry):

```bash
tar czf - soupOS.iso disk.img challenge/ | ssh soup@<deployer> \
    'mkdir -p ~/soupos-chal && cd ~/soupos-chal && tar xzf -'
ssh soup@<deployer> 'cd ~/soupos-chal && docker build -f challenge/Dockerfile -t soupos-chal .'
```

Then map it in **Admin → Deployer → Add Challenge**:

| field | value |
|---|---|
| image | `soupos-chal` |
| port | `9999` |
| type | `tcp` |
| timeout | `3600` |

## How an instance behaves

`socat` forks one QEMU per TCP connection, so **every connection gets a fresh
VM**. Disk writes go to a throwaway overlay (`snapshot=on`), so nothing a
player does survives their session or leaks into anyone else's — verified by
writing a file, reconnecting, and finding it gone.

`-monitor none` is load-bearing: without it QEMU can expose its monitor on
stdio, and a player reaching the monitor owns the host process and bypasses
the challenge entirely.

`max-children=8` caps concurrent VMs (128 MB each) so one player cannot
exhaust the host by looping connections.

## Ingress caveat

The launcher hands players `nc $BASE_DOMAIN <port>`. That only works if
`BASE_DOMAIN` resolves to something that will carry **raw TCP on an arbitrary
high port**. A Cloudflare-proxied record will not — Cloudflare's free proxy
carries HTTP/HTTPS only, so the `nc` line resolves to Cloudflare and the
connection dies. Web challenges are unaffected (they go through nginx on
443). A tcp challenge needs a non-proxied ("grey cloud") A record, a
dedicated port range through the firewall, or Cloudflare Spectrum.
