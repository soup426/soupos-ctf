# Night queue

Fifty-seventh queue. The first fifty-six are in `docs/queue-archive-2026-10-*.md`;
queue 56 (`-11a`) did the clock in the cook's own time, the spice rack,
compost and a visible Alt+Tab, and Mincecraft was built beside it
(v0.60.190 to v0.60.202, its own spec and plan in docs/superpowers/). This
one: neosim, Mincecraft's review leftovers and sound, Doom at the
framebuffer's resolution, and check.sh's flakes under load. Written by an
unattended run when queue 56 ran out, at the user's request ("if the queue
is empty, add stuff to the queue, like a word editor called neosim (based
off of neovim)"; Doom allowed again after Mincecraft); the user has not
seen it.

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

## 1. neosim: a modal editor, the first part

The user's name, kept as asked (it simmers). A ring-3 program in a
desktop window, `user/neosim/` with its own Makefile rule as Mincecraft
has: `cook neosim.elf FILE`. 640x480, the kernel's 8x16 font: 80 columns,
28 rows of text, a status line (mode, file name, line:column, [+] when
changed) and a command line. Normal mode: h j k l and the arrows, i a o O
A I into insert mode, Esc out, x, :w :q :wq :q! (q with changes refused,
said on the command line), :e FILE. Lines numbered down the left. The
file is read whole into a gap buffer per line or an array of lines; a
missing file opens empty and is made at :w. Markers ([neosim] ...) for
the tests, which start it at the console with -wait before the desktop
(as Mincecraft's do). Test: a scripted edit of a copy of RECIPE.TXT
(insert, append, open lines, x, :wq) against the expected file; :q with
changes refused; a screendump with the status line read against the font.

## 2. neosim: counts, operators, undo

w b e 0 ^ $ gg G and counts (3j, 5x, 2dd); d c y with a motion (dw, d$,
cw, db), dd yy cc, p P (line-wise and character-wise registers), u and
Ctrl-R through a list of changes (each insert-mode visit one change), J.
Test: each against vim's behaviour on the same text, the expected files
written by hand from vim on the host and kept in the test.

## 3. neosim: search, substitute, visual, repeat

/ and ? with n N (plain text, wraps with a note), :s/a/b/ and :%s/a/b/g
(plain text, no regex), v and V with d y c > <, and . repeating the last
change. Test: each, as item 2.

## 4. neosim from the files window

The files window's menu gains "open in neosim" for a file (as "use as
background" is for a picture), and a double click on a .txt, .md, .sc or
.c opens it there. Test: the menu entry and the double click, the window
up with the file's first line read back.

## 5. Mincecraft's review leftovers

From the branch review (minors, deferred): Esc first closes a desktop menu
that is up (close desk_open when the focus goes to a program window);
mouse_init could read a key byte pending at boot as the config byte
(drain the buffer after cli); no test holds W across the focus change
itself (one long hold spanning an Alt+Tab, z stops within two ticks); the
save's byte sum becomes a CRC-32 (version 2 files, version 1 still read);
a HOME over 140 characters is refused with a message, not cut.

## 6. Mincecraft: sounds

SYS_TONE (the mixer): a short low crunch on a break (lower for harder
blocks), a plop on a place, a soft tick while breaking, splash entering
broth. Nothing with no sound card. Test: the mixer's markers in the log
for each, as ctoven-test reads the oven's.

## 7. Doom at the framebuffer's own resolution

Deferred in queue 48 at the user's request ("OS work only"); allowed again
2026-10-11. Doom draws 320x200 into mode 13h-style memory and the console
scales it; at the framebuffer's resolution (1024x768 under QEMU) it should
draw scaled to fit, centred, aspect kept, not stretched. Measure first
(rule 3): the frame time now and after. Test: a screendump in a level,
the picture centred and the borders black.

## 8. check.sh under load

Two of today's full runs each had one test fail that passed six of six
in a soak (ctclockin, ctfiles "could not drive QEMU"), as the notes below
have it for the gate's boot and audio segments. Run check.sh five times,
count each test's failures, and give the flakers what they wait for (a
marker, a prompt) instead of a fixed time. Then five more runs, none
failing. The numbers in the changelog either way.

## 9. CLAUDE.md: the Doom note

CLAUDE.md's Doom section says "the enemy AI regression noted in ROADMAP.txt
is still open"; ROADMAP has it RESOLVED in v0.7.6. Read the code and the
roadmap, fix whichever is wrong.

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
