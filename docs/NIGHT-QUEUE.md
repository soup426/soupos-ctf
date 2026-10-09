# Night queue

Forty-fifth queue. The first forty-four are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). Two the forty-fourth turned up (a directory listing
that reads the directory once per entry, and what happens past 128
entries), bash's $'...' quoting, and options the text programs lack.

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
13. **Check it is not already there**: grep the menu for "(sh's NAME)"
    and read the first lines of user/*.c before queuing an sh feature or
    a Unix tool (queue 42 queued wait and getopts, already rest and pluck).
14. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. SYS_READDIR: one pass, not one per entry — CLOSED, measured cheap

Measured on the 79-entry root: `cook peek.elf /` takes 4 to 9 ticks
(40 to 90 ms) into a file and 6 to 7 to the screen (eggtimer: 0.08 s),
the 79 passes included. Nothing worth a cache. The ls-test flake was the
loaded full check against a flat two-second wait, not the listing; the
v0.60.124 notes that blamed SYS_READDIR are corrected.


Measured in v0.60.124: SYS_READDIR runs fat_ls over the whole directory
for each index it is asked for, so `cook peek.elf /` on the 79-entry root
reads it 79 times. Measure peek.elf's ticks on the root first, keep the
last listing per process (the path, and a generation the FAT bumps on any
change, so a stale one is never handed out), and measure again. ls-test
and the rest must agree with mtools as before.

## 2. Past 128 entries — DONE v0.60.125

fat_ls fills arrays of FAT_LS_MAX (128). Measure what a directory of 200
files does to peek.elf, serve, forage.elf, globs and Tab: if entries are
lost silently, make them all reachable (a larger or growing array, or
listing in pieces) and test with 200 and 300 files against mtools.

## 3. $'...' (ANSI-C quoting) — DONE v0.60.126

bash's $'...': \n \t \r \\ \' \" \a \b \e \f \v \0NNN \xHH, the
result one word as '...' is, and nothing else expanded inside. In
commands, assignments, case patterns and [[ ]]. Test against bash.

## 4. slice -c LIST and -s (cut -c, cut -s) — DONE v0.60.127

-c LIST picks characters (bytes) by position, with N, N-M, N- and -M
ranges as -f's; -s drops lines with no delimiter when -f is given.
Compare with GNU cut.

## 5. skim -c N, dregs -c N, dregs -n +N, skim -n -N — DONE v0.60.128

head and tail's byte counts, tail's "from line N on", and head's "all but
the last N". Compare with GNU head and tail, binary input included.

## 6. cull -u and -i (uniq -u, uniq -i) — DONE v0.60.129

-u only the lines that are not repeated, -i comparing without case
(printing the first of each run as GNU's does). With -c and -d as they
are. Compare with GNU uniq.

## 7. Doom at the framebuffer's own resolution — DEFERRED

Skipped at the user's request: OS work only.

## Notes left by unattended runs

- v0.60.125: `slurp /big/*` over 302 names prints `/big/*` as typed: the
  glob's words do not fit the 256-byte line, and a builtin's argument glob
  falls back to the pattern instead of saying the line is too long (bash
  prints them all). A for loop's glob has its own 32 KB and is fine.
