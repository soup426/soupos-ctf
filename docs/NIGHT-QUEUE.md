# Night queue

Ninth queue. The first eight are in `docs/queue-archive-2026-10-0*.md`. The
seventh's notes still hold the set-aside syscall item (item 4) for a person
to decide on. The eighth made the system multi-user in practice; this one
looks at what that leaves loose.

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

## 1. A fired cook's files go to the next hire

`hire` takes the smallest free uid, and `fire` frees it. Files record their
owner as a uid, so the next cook hired is likely to own everything the fired
one left, home included. Prove it first: hire, write a file, fire, hire
someone else, and see who owns the file. Then decide between never reusing a
uid and handing a fired cook's files to the headchef, and write down why.
Also find the real upper bound on uids, given how the owner is stored on disk.

## 2. last

Logins leave only klog lines, which do not survive a reboot. Record each
login and each refused one (cook, console or address, time) in a file that
does, and add `last` to show them, newest first. Check the record survives a
reboot and that mtools can read it.

## 3. Idle sessions end

An SSH session left open holds one of four slots forever. End sessions with
no input for a set time (a setting in /etc/rc, off by default on the
console), say why to the client, and log it. Test with a short timeout.

## 4. du

Homes make it worth knowing what takes the space. `du [bowl]`: the bytes and
clusters under each entry, with a total, checked against mtools.

## 5. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)
