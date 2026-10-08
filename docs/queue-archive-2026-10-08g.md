# Night queue

Fourteenth queue. The first thirteen are in `docs/queue-archive-2026-10-0*.md`.
Two items wait on a person, with notes in their archives: the seventh's
syscall-pointer item and the thirteenth's swap-shares item. After several
queues of hardening, this one is ordinary features: the small tools that
make pipes worth having. Each is checked against the same tool on the host.

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

## 1. >> appends — DONE v0.56.0

`cook echo.elf x > F` overwrites; nothing appends. Add `>>` to the shell's
redirection (the cook's write permission rules as for `>`), and check the
result against the host shell doing the same with the same input.

## 2. grep.elf — DONE v0.56.1

A fixed-string filter for pipes and files: `grep.elf [-v] [-c] [-i] word
[file]`. Compare its output with `grep -F` on the host over the same files,
long lines and empty ones included.

## 3. head.elf and tail.elf — DONE v0.56.2

`-n N`, default 10, from a file or a pipe. Compare with the host's head and
tail, including files shorter than N and files with no final newline.

## 4. sort.elf — DONE v0.56.3

Lines in byte order, with -r and -n. Compare with `LC_ALL=C sort` on the
host. Find the largest input it handles and say so.

## 5. > loses output past about 2 MB, and says nothing — DONE v0.56.4

Found measuring sort.elf: `cook sort.elf /B3.TXT > /O3.TXT` left exactly
2097152 bytes of a 3 MB result, and 5 and 7 MB results left empty files,
every time with exit code 0. A redirect buffers the whole file in the 8 MB
kernel heap and writes it on close; when the buffer cannot grow, the write
fails and nobody hears. Make the output reach the disk in pieces (or, at
the least, make a failed write fail loudly: the program sees it, the shell
says the file was cut, and nothing pretends it worked). Test with a 5 MB
result checked against the host.

## 6. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)
