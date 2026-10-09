# Night queue

Forty-sixth queue. The first forty-five are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). A glob the forty-fifth found falling back silently,
then what the text programs and expiry still lack against their GNU
counterparts (each checked first).

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
13. **Guard every new file**: `[ -e scripts/X-test.sh ] && { echo EXISTS;
    exit 1; }` before writing it (v0.60.128 wrote over headtail-test.sh
    after an `ls` printed that it was there).
14. **Check it is not already there**: grep the menu for "(sh's NAME)"
    and read the first lines of user/*.c before queuing an sh feature or
    a Unix tool (queue 42 queued wait and getopts, already rest and pluck).
15. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. A builtin's glob too big for the line — DONE v0.60.130 (builtins did not glob at all)

Found in v0.60.125: `slurp /big/*` over 302 names prints `/big/*`, the
pattern as typed, because the words do not fit the 256-byte line. bash
prints them all. Measure where it falls back (builtins, cook, for), then
either fit them (a builtin's arguments in a larger buffer) or say the
line is too long with status 1, as a cut expansion already does; never
the pattern as if nothing matched. Test with 302 names.

## 2. expiry +FORMAT (date +FORMAT) — DONE v0.60.131 (with -d @SECONDS)

`expiry +FORMAT`: %Y %m %d %H %M %S %y %e %j %a %A %b %B %p %I %s %F %T
%D %R %n %t %%, as GNU date's in the C locale, for the RTC's time. Test:
print %s beside each format and compare with the host's `date -u -d @S`.

## 3. swap [:class:] and -c (tr) — DONE v0.60.132

Classes in a set ([:alpha:] [:digit:] [:alnum:] [:upper:] [:lower:]
[:space:] [:punct:] [:blank:] [:xdigit:] [:cntrl:] [:print:] [:graph:]),
[:lower:] to [:upper:] and back in order, and -c (the complement of the
first set) with -d and -s. Compare with GNU tr.

## 4. rack -f and -b (sort) — DONE v0.60.133

-f folds lower case to upper for comparing, -b ignores leading blanks
(in keys with -k too); with -r -n -u -k -t as they are. Compare with GNU
sort, LC_ALL=C.

## 5. sift over several files, -l -H -h -w -x (grep) — DONE v0.60.134

Several FILEs: each line prefixed NAME: when more than one (-H always,
-h never), -l the names of files with a match, -w whole words, -x whole
lines; with -E, -c (NAME:N), -n (NAME:N:line), -v -i -o. Compare with
GNU grep (by its path, not the ugrep wrapper).

## 6. weigh over several files, and -L (wc) — DONE v0.60.135

Several FILEs, a line each and a `total` line, widths as GNU wc's; -L
the longest line. Compare with GNU wc.

## 7. Doom at the framebuffer's own resolution — DEFERRED

Skipped at the user's request: OS work only.

## Notes left by unattended runs

(none yet)
