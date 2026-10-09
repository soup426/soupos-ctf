# Night queue

Tenth queue. The first nine are in `docs/queue-archive-2026-10-0*.md`. The
seventh's notes still hold the set-aside syscall item for a person to decide
on. The ninth closed holes in how cooks come and go; this one looks at the
files that hold the kitchen's secrets.

## Standing rules

1. **One item, one commit**, and split when an item splits naturally.
2. **`scripts/check.sh` must pass before every commit** (five configurations
   in parallel copies, the gate's eight segments, every test script; about
   two minutes). Commit with explicit paths when other work is in the tree.
3. **Measure before building, and measure the fix.** An item whose premise
   measures false is closed with the numbers, not built anyway.
4. **Prove a bug before fixing it**, with the smallest reproduction that
   shows it, and turn that reproduction into the test.
5. **A test script per subsystem; the gate asserts on serial markers.** New
   scripts go in check.sh's list; a script waits on the PIDs it started,
   never a bare `wait` (QEMU is a child too).
6. **Reproduce under load**: six-wide soaks for anything timing-shaped.
7. **Crypto gets published vectors from an independent implementation.**
8. **Kitchen names** for new shell commands.
9. **Gitea only.** Never GitHub, never the `cdctf-release` branch.
10. **`make clean` leaves disk.img alone**: `rm disk.img && make disk` after
    changing anything the Makefile embeds.
11. Do not edit the tree while check.sh runs: it builds once at its start
    and reads each test script when that test starts.
12. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. The roster is readable by every cook — DONE v0.54.0

/etc/kitchen is rwxr-x (checked 2026-10-08), so any cook can read every
cook's PBKDF2 secret and attack it offline. Close it to the headchef in the
ordinary build. FIRST read what the CHALLENGE build's stages need: stage 2
is the AlphaSOUP-32 hash, and if reading the roster is part of it, that
build must keep it readable. Prove the read on the old kernel, and check
that login, `secret` and `hire` still work for a cook who cannot read it.

## 2. fire leaves the cook's SSH keys — DONE v0.54.1

/AUTHKEYS lines name a cook. Firing removes the cook but not the lines, so a
cook later hired under the same name logs in with the fired one's key.
Prove it, then remove a fired cook's lines (and say how many).

## 3. Every system file has a known owner and mode — DONE v0.54.2

/etc/kitchen, /etc/logins, /etc/rc, /AUTHKEYS, the host key, /home: list
every file the kernel writes or trusts, pin each one's owner and mode, set
them where the kernel writes them, and add a test that checks all of them
after a session of cooks doing ordinary things.

## 4. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)
