# Night queue

Forty-first queue. The first forty are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). The shell's no-op and its two answers, then programs
the kitchen still lacks: a name's last part and the rest, base64, a
SHA-256, and one that says the same thing forever.

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
8. **Never pipe into `grep -q` in a test**: pipefail turns the writer's
   SIGPIPE into a failed check. lint-test.sh enforces it.
9. **Kitchen names** for every new command, builtins and programs alike
   (sift.elf, not grep.elf); compare with the host tool under its real name.
10. **Gitea only.** Never GitHub, never the `cdctf-release` branch.
11. **`make clean` leaves disk.img alone**: `rm disk.img && make disk` after
    changing anything the Makefile embeds.
12. Do not edit the tree while check.sh runs: it builds once at its start
    and reads each test script when that test starts.
13. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. :, fresh and spoiled (true and false) — DONE v0.60.104

`:` is POSIX's special builtin, so it keeps its name: it does nothing,
status 0, its words expanded (so `: ${x:=5}` sets x). true and false
under kitchen names, fresh (0) and spoiled (1). Compare with bash.

## 2. peel and stalk (basename and dirname) — DONE v0.60.105

Programs: peel.elf NAME [SUFFIX] the last part (the suffix off), stalk.elf
NAME what leads to it, as the host's basename and dirname do with their
edge cases (trailing /, no /, /, //). Compare with the host's.

## 3. brine.elf (base64) — DONE v0.60.106

Encode stdin or a file to base64, wrapped at 76 as GNU's; -d decodes.
Compare with the host's base64, both ways, on text and on binary.

## 4. brand.elf (sha256sum) — DONE v0.60.107

The SHA-256 of a file or stdin, as `HASH  NAME` lines; several files.
Compare with the host's sha256sum, the empty file and a large one too.

## 5. encore.elf (yes) — DONE v0.60.108

Says y (or its words) forever, until what reads it stops: `cook encore.elf
| skim.elf -n 3` ends. Compare with yes | head -n 3.

## 6. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

- v0.60.105: "$(cmd)" in double quotes turns cmd's inner newlines into
  spaces; bash keeps them ("$(dirname a/b c d/)" is three lines). Worth an
  item in the next queue (only unquoted $( ) should be split).
