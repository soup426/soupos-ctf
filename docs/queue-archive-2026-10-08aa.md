# Night queue

Thirty-fourth queue. The first thirty-three are in `docs/queue-archive-2026-10-0*.md`.
Items wait on a person in three archives (the seventh, the thirteenth and
the twenty-fourth). The gaps the last queues wrote down: quoted pattern
characters, $( ) in here-documents and 64-bit arithmetic; and two new
builtins, inspect (type) and dish -v.

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

## 1. A quoted glob character is itself, in patterns — DONE v0.60.69

${NAME#"*"}, ${NAME/"?"/x} and case patterns ("*") take a quoted * ? [
as glob syntax today (v0.60.64 wrote it down); sh takes it as the
character. Compare with bash.

## 2. $( ) in here-documents — DONE v0.60.70

heredoc_expand knows $NAME, ${NAME}, $1, $? and $(( )), not $( ).
Compare with bash, in a script and at the prompt.

## 3. 64-bit arithmetic — DONE v0.60.71 (with 0x, 017 and BASE#N)

$(( )) is 32 bits (a long on i386); bash's is 64. Make it 64, which needs
64-bit division and remainder in the kernel (libgcc's __divdi3 and
friends, or our own). Compare with bash past 2**31.

## 4. inspect NAME (sh's type) — DONE v0.60.72

Says what each NAME is: a keyword, a builtin, a function (with its body),
or a program (with its path), or not found (status 1). A kitchen name,
the health inspector's. Compare the kinds with bash's type -t.

## 5. dish -v NAME — DONE v0.60.73

dish (printf) into a variable instead of the terminal, as bash's
printf -v. Compare with bash.

## 6. Doom at the framebuffer's own resolution — DEFERRED

Carried; the user asked to keep to OS work.

---

## Notes left by unattended runs

(none yet)
