# Night queue

Sixty-fifth queue. The first sixty-four are in `docs/queue-archive-2026-10-*.md`;
queue 64 (`-12h`) made the full check faster, gave neosim registers, :reg
and macros, and Mincecraft F3 (and, asked for by the user between items,
Mincecraft and Doom icons on the desktop). This one: the keys lost during
a big write that its notes found; neosim patterns; a sun and a moon; a
neosim icon. Written by an unattended run when queue 64 ran out, as the
user asked on 2026-10-11; the user has not seen it.

## Standing rules

1. **One item, one commit**, and split when an item splits naturally.
2. **`scripts/check.sh` must pass before every commit** (five configurations
   in parallel copies, the gate's eight segments, every test script), run
   here on the laptop: the user asked on 2026-10-11 for everything to run
   locally, so not `scripts/remote-check.sh`.
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
16. **Never run an interactive QEMU on the tree's disk.img** (2026-10-11: a
    session's /etc and /countertop writes failed eight tests). Copy it.
17. **Test directories are kept on failure, 50 MB each**: clear
    /tmp/soupos-* after a run of failures, and never let a test compute a
    size from parsed tool output without checking it is a sane number
    (2026-10-11: `head -c` of a misparsed negative count wrote 11 GB).

---

## 1. Keys lost during a big write — CLOSED, measured (2026-10-12): not reproduced

Typed while a program wrote 3 MB, "countertop" arrived as "countert".
Measure first: a line typed at the console during a long write (a test
program writing a few MB, or the shell copying a big file), the keys that
arrive counted, under KVM; then whether the keyboard IRQ is held off
through the ATA writes (cli, polling), or the key buffer overflows, or the
echo is lost and the keys are not. Fix what it is; a test that types
during a big write and gets every key.

## 2. neosim: patterns — DONE v0.60.243 (and an rx.c bug)

/ ? n N * and :s with vim's basic patterns: . * ^ $ [abc] [^a-z] \< \>
\. (the magic ones, as 'magic' is on), plus \| ? Measure what neosim's
plain search would miss first. Test: searches and substitutes against
neovim's.

## 3. Mincecraft: a sun and a moon

A square sun crossing the sky from east to west through the day, a
square moon by night (as Minecraft's), drawn behind the clouds and the
terrain. Test: dumps at three times of day, the sun's square where its
angle puts it (low east in the morning, high at noon), the moon at night.

## 4. neosim on the desktop

An icon and a Kitchen entry: a double click opens neosim with no file
([No Name]), a :w NAME writes it. Test as ctgames-test: the icon named,
its window up with no terminal on show.

**Closed with the numbers (rule 3).** The game refusing a 3 MB world and
copying it to .bad, "countertop" typed at the console straight after
(scripts in the run's scratchpad, not kept): 3 runs alone, 12 six wide,
48 sixteen wide: all 63 had every key and the desktop up. Nothing in
ata.c or fat.c holds interrupts off; the key buffer is 256. The first
try at it was wrong, worth saying: "echo" is not a soupOS command ("No
soup for you"), so its line looked lost when it had run. The failure seen
once (v0.60.239's full check) showed only the echo cut by the game's own
output; the keys after it were never shown lost. mincecraft-save-test
types after the copy now anyway, so it is out of that race.

## Notes left by unattended runs









(Carried from queue 56, still open.)
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
- From queue 57 item 4 (v0.60.207): the double click on a text file still
  opens jot, as it has since v0.60.154; only the menu's "open in neosim" was
  added. Moving the double click to neosim is the user's call: one line in
  files_open (countertop.c), and ctfiles-test's double-click checks follow.
- v0.60.212: litexp-test failed once under check.sh ("expanded text differs
  from bash:" with nothing after it, so its output file was empty), passed
  three of three alone and the rerun passed. It already waits for each
  prompt; the empty file suggests the first line, typed one second after the
  secret, was lost while the login was checked. If it comes back, wait for
  the first prompt (UNTIL) before the first line, in every test that types
  straight after logging in.

- 2026-10-12 (queue 60 item 3): toast (crafted from a stalk) is not food
  yet; the queue named only sugar, butter and florets. Making it the best
  food (four hearts, say) would give crafting a reason; a later item.

- 2026-10-12 (queue 62 item 2): neosim does not repeat an insert by its
  count (3ihi<Esc>, 2Onew<Esc> insert once; neovim three and two times).
  Found by a marks case; a later item.

- 2026-10-12 (queue 62 item 3): neosim has no paragraphs: no { } motions
  and no ip ap objects. A later item.

- 2026-10-12 (queue 63 item 2): the full check is 420 s now (it was 373
  at queue 58's start), run under timeout 590; each new test adds 5-10 s.
  Before it nears the limit: find the slowest tests (check.sh prints each
  one's time) and share boots, or raise the timeout on purpose.

- 2026-10-12 (v0.60.239): keys typed while a program writes a large file
  can be lost. mincecraft-save-test typed "countertop" during the game's
  3 MB copy of a refused world; the console echoed "countert" and the rest
  never reached the shell. Measure: type a line during a big write
  (pour a 3 MB file) and count what arrives; then whether the keyboard IRQ
  is held off (cli) through the ATA writes, or the buffer overflows.
