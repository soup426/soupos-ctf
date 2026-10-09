# Night queue

Sixth queue. The first five are in `docs/queue-archive-2026-10-07*.md`. Read
the fifth's notes first: its first item found a real bug the moment anything
looked (the permission byte was FAT's case flags), and its swap item is the
example of modelling a policy on the host before trusting a kernel number.

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
11. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. Passwords that survive the network — DONE v0.46.0 (and it found v0.45.1's FAT bug)

users.c stores each cook's secret as an unsalted 32-bit AlphaSOUP hash, and
any preimage logs in. In the CHALLENGE=1 build that is deliberate - it is the
whole of challenge stage 2 - and must stay byte for byte. In the ordinary
build it now sits behind `vault`, where a password crosses the network. Use
salted PBKDF2-HMAC-SHA256 there (HMAC-SHA256 has been verified since
v0.23.0): a random 16-byte salt from the entropy pool, a few thousand
iterations, stored in /etc/kitchen in a new line format the old one can be
told from. Verify PBKDF2 against Python's hashlib.pbkdf2_hmac before using
it. Old entries still log in and are rewritten in the new format on the
first successful login; time one login and keep it well under a second.

## 2. The machine comes up ready — DONE v0.47.0 (DHCP at boot already existed; /etc/rc added)

A fresh boot has no address until someone types `plumbing dhcp`, and nothing
is listening until someone types `vault 22`. Do DHCP at boot when a NIC is
present (with a short timeout, so a machine with no network still boots
promptly), and run /etc/rc.sc - a soupyc script, if present - as headchef
after it. A test boots an image whose rc.sc opens the vault and logs in over
SSH with nothing typed at the console.

## 3. Files get real timestamps — DONE v0.47.1

Everything soupOS creates is dated 1980 (mdir shows it), because the write
time and date fields are left zero. Stamp them from the RTC on create and on
every write, in the standard write-time/date fields (bytes 22-25; 13-14 now
hold the owner and mode). Check with mdir that a file written now is dated
now, and that fsck stays clean.

## 4. Programs can remove files and make directories — DONE v0.48.0 (and open() now checks permissions)

Ring-3 programs can create and read files and now list directories, but not
remove anything or make a directory. SYS_UNLINK and SYS_MKDIR, and rm.elf and
mkdir.elf on the disk, with mtools and fsck as the judges.

## 5. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)
