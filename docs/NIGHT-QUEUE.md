# Night queue

Fifty-fourth queue. The first fifty-three are in `docs/queue-archive-2026-10-0*.md`;
queue 53 (`-09s`) added Ctrl+Alt+T and Alt+F4 (Alt decided in the IRQ),
new bowls and removing files from the files window, scale.elf, icons on
the background and a calendar under the clock. This one: locking the
desktop, copying files by dragging, CPU use in expo, the network on the
bar, and a picture for the background. Written by an unattended run when
queue 53 ran out, as the loop it runs in asks; the user has not seen it.

## Standing rules

1. **One item, one commit**, and split when an item splits naturally.
2. **`scripts/check.sh` must pass before every commit** (five configurations
   in parallel copies, the gate's eight segments, every test script), run
   with `scripts/check.sh` here (the user allowed the laptop again later on
   2026-10-09; it runs at most CHECK_JOBS at once) or on the test container
   with `scripts/remote-check.sh` if the laptop is wanted for other things.
   Keep soaks six wide at most. Commit with
   explicit paths when other work is in the tree.
3. **Measure before building, and measure the fix.** An item whose premise
   measures false is closed with the numbers, not built anyway.
4. **Prove a bug before fixing it**, with the smallest reproduction that
   shows it, and turn that reproduction into the test.
5. **A test script per subsystem; the gate asserts on serial markers.** New
   scripts go in check.sh's list; a script waits on the PIDs it started,
   never a bare `wait` (QEMU is a child too).
6. **Reproduce under load**: six-wide soaks for anything timing-shaped.
7. **Crypto gets published vectors from an independent implementation.**
8. **Never pipe into `grep -q` in a test**: pipefail turns the writer's
   SIGPIPE into a failed check. lint-test.sh enforces it.
9. **Kitchen names** for every new command, builtins and programs alike
   (sift.elf, not grep.elf); compare with the host tool under its real name.
10. **Gitea only.** Never GitHub, never the `cdctf-release` branch.
11. **`make clean` leaves disk.img alone**: `rm disk.img && make disk` after
    changing anything the Makefile embeds.
12. Do not edit the tree while check.sh runs: it builds once at its start
    and reads each test script when that test starts.
13. **Guard every new file**: `[ -e scripts/X-test.sh ] && { echo EXISTS;
    exit 1; }` before writing it (v0.60.128 wrote over headtail-test.sh
    after an `ls` printed that it was there).
14. **Check it is not already there**: grep the menu for "(sh's NAME)"
    and read the first lines of user/*.c before queuing an sh feature or
    a Unix tool (queue 42 queued wait and getopts, already rest and pluck).
15. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. cloche: locking the desktop — DONE v0.60.176

The Kitchen menu's "cloche" (the dome over a plate), and ten minutes with
no key or mouse, cover the screen with a lock showing the cook's name;
their secret, typed there and checked by users_check, lifts it. Programs
go on running under it. Wrong secrets are logged, and after three the
lock waits a few seconds before the next try. Test: lock from the menu,
a wrong secret refused (logged), the right one unlocks; the idle lock
with the timeout shortened for the test (a build switch; built as an
"idle N" line in the cook's settings instead, which the test writes).

## 2. Copy a file by dragging between files windows — DONE v0.60.177

A file row dragged from one files window onto another copies it into the
bowl that one shows: read permission on the file, write on the bowl (the
shell's rules); a name taken there gets "copy" put in front. Test: as
saucier, a file copied from / into the home (the bytes on the disk the
same), and refused into /.

## 3. CPU use in expo

The scheduler counts each task's ticks a second (cpu_last). SYS_PROCS
passes it on and expo shows a CPU column, with c sorting by it. Test:
glutton.elf near 100, an idle process near 0, the sort order.

## 4. The network on the bar

Next to the clock: the link (up or down) and the address once DHCP has
given one ("10.0.2.15"), refreshed every few seconds, from what plumbing
already knows. Test: with QEMU's user network, the address read off the
bar after `plumbing dhcp`; without a network card, "no network".

## 5. A picture for the background

A BMP (24-bit, any size up to the screen) named in the cook's
`countertop` settings ("picture /home/saucier/sky.bmp") drawn centred on
the background instead of the gradient. Measure first how long a full
1024x768 picture takes to read and draw. Test: a small BMP made on the
host, its pixels on the screendump where expected.

## 6. Doom at the framebuffer's own resolution — DEFERRED

Skipped at the user's request (queue 48): OS work only.

## Notes left by unattended runs

- From queue 48 (v0.60.143), still open: a terminal window shows stderr,
  as SSH sessions do, so peek.elf's "LS f SIZE NAME" serial markers
  (written for ls-test) appear under its listing there, and frost's
  [frost] lines too. Cosmetic; worth deciding whether such markers
  should go to the serial log only.
- From queue 48 (v0.60.145): the gate's boot segment failed once in
  three check.sh runs under load ("could not drive the QEMU monitor");
  if it comes back, look at qemu_keys' connect and login waits first.
- v0.60.149: cmp-test failed once under check.sh's load: it typed each
  line then waited a fixed 2 s, the guest fell behind after four, and
  QEMU was killed with four lines unread. Now it waits for each prompt.
  find, cut, glob, flip, quote, uniq, wc and tr tests have the same
  fixed wait (grep '"WAIT:2"); done'); item 8 should take them too.
- v0.60.150: the gate failed again only inside check.sh, this time its
  audio segment (no "[doomsnd] DSPISTOL" or "[music] D_E1M1" line, 98
  checks run, not 107); the gate alone passed (107 ok) and so did the
  rerun. With the boot segment's failure (v0.60.145) that is two of
  about ten check.sh runs today. Worth an item: run check.sh several
  times, count, and give the gate's segments what they wait for rather
  than fixed time under load.
