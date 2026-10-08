# Night queue

Twenty-sixth queue. The first twenty-five are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). Three more tools, and the shell and its history grow up
a little.

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
8. **Kitchen names** for every new command, builtins and programs alike
   (sift.elf, not grep.elf); compare with the host tool under its real name.
9. **Gitea only.** Never GitHub, never the `cdctf-release` branch.
10. **`make clean` leaves disk.img alone**: `rm disk.img && make disk` after
    changing anything the Makefile embeds.
11. Do not edit the tree while check.sh runs: it builds once at its start
    and reads each test script when that test starts.
12. If an item is blocked or needs a decision that is not written down, leave
    a note at the bottom and move on.

---

## 1. mince.elf — DONE v0.60.14

A hex dump in `hexdump -C` form: offset, sixteen bytes in two groups of
eight, the printable ones between bars, a `*` for repeated lines and the
final offset. Compare with the host's hexdump -C byte for byte.

## 2. knead.elf — DONE v0.60.15

Lines folded at a width, as `fold -w N` does (default 80), and with -s at
the last space. Compare with the host's fold.

## 3. layer.elf — DONE v0.60.16

Lines of two or more files side by side, as `paste` does (tab between,
-d to choose), files of different lengths included. Compare with the
host's paste.

## 4. A cook's leftovers outlive the shift — DONE v0.60.17

History is per shell and wiped at clockout (v0.60.12). Keep each cook's
in their home (a file only they can read), load it at login, so the up
arrow and `leftovers` survive a clockout and a reboot. Test across both.

## 5. Ctrl+R finds an old line — DONE v0.60.18

A reverse search through the history at the prompt, as in bash: type and
the newest match is shown; Ctrl+R again for the one before; Enter runs it,
Ctrl+C gives the line back. Test by typing.

## 6. case ... esac — DONE v0.60.19

`case WORD in pat) ... ;; pat|pat) ... ;; *) ... ;; esac`, with the glob
patterns the shell already has. Compare with bash's results for the same
script.

## 8. A gate failure that its own log says passed — DONE v0.60.20 (grep -q under pipefail; desk did not recur)

2026-10-08 10:34, one full check (the run before v0.60.16's commit) failed
two gate checks; the rerun passed. Both logs are in docs/flakes/.
- desk: the in-kernel self-test's "services are not busy" failed with
  "busiest service: fbcon at 55%". A share of CPU measured while ~55 QEMUs
  run is a load measurement; decide whether the self-test should judge it
  under check.sh at all.
- jobs: "no ring-3 output after the kill" failed, yet run against the kept
  log the same check passes (first `killed (irq)` at line 93, TESTOUT at
  108). Find how the check saw something else: the log at check time, the
  grep on it, or a second writer.

## 7. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)
