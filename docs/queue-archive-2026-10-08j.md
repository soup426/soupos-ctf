# Night queue

Seventeenth queue. The first sixteen are in `docs/queue-archive-2026-10-0*.md`.
Two items wait on a person, with notes in their archives: the seventh's
syscall-pointer item and the thirteenth's swap-shares item. The sixteenth
added tr and taught the shell ; && || and $?; this one gives the shell
quotes and ~, and makes every test wait for what it checks.

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

## 1. Quotes in the shell — DONE v0.57.2

Arguments are split on every space, so `grep.elf "two words"` and
`tr.elf ' ' '_'` cannot be written. '...' keeps everything literally and
"..." does too (there is nothing to expand inside but $?); either kind
groups its contents into one argument with the spaces kept, and the
quotes are dropped, as sh does. Programs receive their arguments as one
string today: decide how a quoted space reaches them (and write it down),
then compare echo, grep and tr results with sh on the same lines.

## 2. ~ is the cook's home — DONE v0.57.3

`cd ~`, `pour ~/NOTES.TXT`, `cook cat.elf ~/X`: a word that is ~ or starts
with ~/ means the current cook's home, as sh does. Not inside quotes.
Test as the headchef (whose home is /) and as a cook.

## 3. Every test waits for what it checks — DONE (scripts only)

rc-test slept 8 s and then read the log, and failed once under the full
check's load (fixed in 1d5d174). Find every script that sleeps a fixed
time and then checks something it could have waited for, make each wait
for its event, and show each change under load (the full check twice).

## 4. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)
