# Night queue

Sixty-seventh queue. The first sixty-six are in `docs/queue-archive-2026-10-*.md`;
queue 66 (`-12j`) made the full check 418 s to 292 s, gave neosim ranges,
:g :v :normal :sort and Ctrl-A Ctrl-X, and Mincecraft flying. On
2026-10-12 the user asked for a review of the whole project (six reviewers,
one an area) and then for the queue to be worked with what needs doing
added: this queue holds the known loose ends first, and the review's
findings are added below as they come in, most severe first, each fixed
under the standing rules (a test that fails first, then the fix).

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
18. **Reviews: by area every 10 commits, the whole project every 30** (the
    user, 2026-10-12; every 10 in full until then, the first one costing
    about 1.4M tokens). Count commits that change more than the queue files:
    `git log --oneline LAST..HEAD -- . ':!docs/NIGHT-QUEUE.md' ':!docs/queue-archive-*' | wc -l`.
    - **Every 10 since the last review of either kind:** stop taking items
      and review only the areas those commits touched, one read-only
      reviewer each. Which areas: `git diff --stat LAST..HEAD` against
      these paths (ROADMAP.txt and CLAUDE.md's version line touch none):
      kernel and shell = src/ but the four below, user/*.c but rx.c;
      the desktop = src/countertop.c; Mincecraft = user/mincecraft/;
      neosim and rx.c = user/neosim/, user/rx.c; Doom = src/doom*.c,
      src/wad*, src/doomsnd*; tests, build and docs = scripts/, Makefile,
      CLAUDE.md past its version line.
    - **Every 30 since the last full review:** all six areas, as the first.
      An area review that finds anything critical brings the full one
      forward.
    - Reviewers work on disk.img copies, at most 4 QEMUs each, never
      check.sh; every claim checked before it is queued; findings into the
      review section, most severe first, worked by the order there. The
      tree is not rebuilt while they run.
    **Last review began at 3c49bd7** (2026-10-13, by area: kernel and shell, neosim, Doom, tests build and docs; the one before at 8eebe4e).
    **Last full review began at c6c8d32** (2026-10-12; the first at 7647649).
    Move the first line at each review, both at a full one.
---

## 0. A text file's double click opens neosim (the user's call, 2026-10-12) — DONE v0.60.250

Done: the double click (and Enter) types cook neosim.elf PATH as the menu's
entry does; ctfiles-test reads neosim's window and quits it with :q. jot
was never in the right-click menu (new bowl, remove, open in neosim); it
stays a shell command.


The files window opens a text file in jot on a double click; the user
asked for neosim. Measure first where the double click decides (the files
window's open, ctx_text's "open in neosim" beside it) and which tests
expect jot there (ctjot-test, ctfiles*). Then neosim for text files, jot
still in the right-click menu; those tests changed to say neosim. Done
first, once the review's six reviewers are finished with the tree.

## 0b. Doom: make it play right (the user, 2026-10-12)

The user played it and reports "tons of problems": the graphics, the
enemies, more. This item gathers them and fixes them, one commit each,
most visible first. Measure first, from the play itself:
- A scripted walk through E1M1 (the start, the zigzag room, the nukage,
  the exit), screendumps every few steps, looked at for what is wrong:
  walls, floors and ceilings, sprites (in place, clipped, sorted, lit),
  the sky, the weapon, the HUD.
- Enemies against Doom's own rules (p_enemy.c, p_mobj.c): waking on sight
  and sound, chasing, attacking (hitscan and missiles), pain, death and
  their frames, damage they take and deal, line of sight.
- The player: movement and collision against walls and things, steps up
  and down, doors and lifts, damage floors, pickups, the level's exit.
- The Doom review's findings (below, when it reports) go here too.
Each problem found becomes a line here: what is wrong, Doom's rule, the
fix, the test (a screendump or the log against the rule). Ask the user for
the ones they saw first if they are around.

The Doom reviewer's findings (2026-10-12, confirmed against the WAD and
linuxdoom; most visible in play first):
- **R-D3 (high).** The game runs at the frame rate: thinkers, doors, lifts
  and projectiles advance once a frame (50 Hz idle, up to 100 while a key
  is held, more with the mouse), so monsters speed up as you move; the
  automap freezes the world. Fix: game logic on a fixed 35 Hz tick
  accumulator, apart from drawing.
- **R-D2 (high).** The skill is ignored and every thing spawns: E1M1 has
  29 monsters where Hurt Me Plenty has 6 (E1M3: 131 for 74), multiplayer-
  only things too. Fix: P_SpawnMapThing's skill bits and the 0x10 flag.
- **R-D4 (medium-high).** Monster sprites use only rotations 1 and 5: the
  WAD's side views are paired lumps (TROOA2A8) that an exact 6-letter
  lookup never finds, so anything not facing you or away is a frozen front
  view. Fix: R_InitSprites's frame/rotation table with the flip flag, and
  R_ProjectSprite's half-sector offset.
- **R-D1 (high).** A door used from the front opens the sector you stand
  in (the side test is backwards); doors open only by luck through their
  back face, and switch doors (29 61 63 103) never open. Fix: side < 0 is
  the front, use only from the front, switches through a tag search.
- **R-D7 (medium).** Fireballs and monsters go through walls: projectiles
  never test lines, chasing monsters move straight with no collision.
  Fix: lines block projectiles; monsters move through a try_move.
- **R-D6 (medium).** The player shoots monsters through walls (no line of
  sight in fire_weapon). Fix: los_clear, as P_AimLineAttack.
- **R-D8 (medium).** Walls clipped at the screen's left edge take depth
  and texture from the off-screen end: smeared, stretched near walls
  (about 13% of left-clipped walls, edges up to 188 px out). Fix:
  interpolate from the unclamped ends; move u1/u2 with the near clip.
- **R-D5 (medium).** Death frames are the gib frames, and the demon's
  stand upright for 40 frames; corpses vanish. Fix: a death table from
  info.c, the last frame held, corpses kept.
- **R-D9 (medium).** Wrong sprites and pickups: green armor drawn as the
  helmet bonus and gives +1; armor bonuses invisible and unpickable (25
  in E1M1); ammo boxes and torches mixed up; lamps missing; ammo taken at
  the cap; the shotgun gives 4, not 8. Fix: the table and
  P_TouchSpecialThing's rules.
- **R-D10 (medium-low).** Doors and lifts wait in Doom's 35 Hz tics at
  100 Hz (1.5 s, not 4.3); a closing door shuts on the player instead of
  going back up, boxing them in. Fix: the waits in ticks; reverse on the
  player.
- **R-D11 (medium-low).** No step height: the player walks up any ledge.
  Fix: P_TryMove's 24-unit step and the head room.
- **R-D12 (low).** The secret exit (51) does nothing, E1M8 cannot be
  finished (no tag-666 lower on the barons' death), the map order after
  E1M8 is wrong. Fix: G_DoCompleted's order and A_BossDeath.
- **R-D13 (low).** The main menu draws M_NEWG, not M_NGAME.
- Suspected (to measure): lost soul and cacodemon HP default to 50; the
  64-sprite cap fills in thing order, not by distance; patch and texture
  decoders trust the WAD's offsets; an int32 overflow in the near clip.

## 1. Mincecraft: the moon's top rows

Looking straight up at midnight, the moon's top rows are two pixels wider
than the rest of its square (v0.60.244 left it). Measure first: which rows,
and the pu/pv values there; the sun's square the same way. Fix the
projection so both are squares. Test: the moon's and the sun's widths
row by row in a dump, all alike.

## 2. Doom: the keys on the status bar

v0.60.230 draws the keys held (STKEYS0-2 at 239, 171 + 10 a key) by
st_stuff.c's rule, and no test sees one. Measure first how the test could
hold a key (a level with a key near the start, a cheat the port has, or a
debug flag); then doom-places-test with a key held, its pixels against the
WAD.

## 3. Doom: one ammo pool

Every pickup adds to bullets (shells, rockets and cells too), so the panel
shows 0 for the others and a shotgun-guy's shells feed the pistol. Doom
has four (P_GiveAmmo, clipammo[], maxammo[]). Measure first which ammo
things E1 has; then four pools as Doom's, the pistol using bullets; the
panel reads them. Test: the counts after picking up a clip and shells.

## 4. The desktop icons' second column

v0.60.245's icon_place wraps the icons into another column on a short
screen; every test machine is 1024x768, so nothing sees it. Measure first
whether QEMU can give soupOS a shorter framebuffer (the FB request, a
fw_cfg). Then a test that reads the second column, or the numbers for why
none can.

## 5. The full check at 48: a flake count

v0.60.246 runs three times the cores at once; three checks passed, but a
heavier pace can bring out timing flakes the old one hid. Run the full
check five times; every test that ever fails is a finding: its log read,
the cause found (a fixed WAIT a window outran, a race), fixed with an
UNTIL or what it needs, soaked six wide. The numbers in the notes.

Seen 2026-10-12 (v0.60.267's run): doom-mouse-test, "a shot each 0.15 s":
gaps [14, 15, ...], one a tick under its 15 floor; three runs alone passed.
Seen 2026-10-12 (v0.60.278's first run): ctslots-test "the hog held none";
two runs alone passed. Probably a kernel line landing mid-line in "prod: hog
holds N" (prodwin-test strips them for that reason); fixed v0.60.279.
- while-test W6 failed once in a full check (2026-10-13, v0.60.300; its Ctrl-C after a fixed WAIT:4), passed twice alone and in the rerun.

## 6. Doom: the face

The status bar's face is always STFST00. Doom's (st_stuff.c,
ST_updateFaceWidget): the row by health (ST_calcPainOffset, five rows),
looking left, ahead and right in turn at random (ST_FACESTRIDE), the
ouch face on a big hit, the grin on a new weapon, the dead face at 0.
Measure first which of those the port can trigger (damage: a level's
nukage or a monster). Test: the face's pixels against the lump the rule
picks, at 100 health and after damage.

## 7. Doom: the shotgun

The port has only the pistol (and the fist?). E1M1 has a shotgun (thing
2001) and shotgun guys drop one. Doom's shotgun: 7 pellets, 1 shell a
shot, its own frames (SHTG A-D, SHTF A-B flash), weapon 3 on the ARMS
panel lit when owned, the 3 key to switch. Measure first what the port
does with thing 2001 and with weapon switching. Test: pick up the
shotgun, 3 switches, a shot uses a shell, the gun's pixels against SHTGA0.
(Needs item 3's ammo pools.)

## 8. neosim: visual block

Ctrl-V selects a block (columns from both corners), d y c I A > < on it,
$ to the lines' ends, o to the other corner. Test: cases against
neovim's.

## 9. Mincecraft: broth flows

Broth placed (or a block beside broth broken) spreads: down first, then
sideways a few blocks on the level, thinning, as Minecraft's water but
shallow; a source block stays. Through the falling-sugar queue's way of
working (a queue of cells, a step a few ticks). Test: a world from the
host, broth placed on a ledge: the cells it reaches in the markers, the
save on the host.

## 10. SinkedIn, and every lone modifier opens it (the user, 2026-10-12)

A desktop program, sinkedin.elf (the user's name: it stays, as neosim
did), a LinkedIn parody that runs offline: a feed of posts ("I'm humbled
to announce I have been promoted to Senior Sous Chef"), endorsements
for kitchen skills (Whisking, Mise en place), a profile from the
logged-in user's name, connection requests from the other accounts on
the box. The posts come from templates and the entropy pool, so the
feed differs each boot.

Then the keyboard: Shift, Ctrl or Alt (either side) pressed and released
with no other key between opens SinkedIn. Measure first where the
release is seen (keyboard.c's break codes already track alt_tabbed the
same way: a flag set on press, cleared by any other key, read on
release) and what the text console does with it (the desktop only, or
the shell too: the user's words are "every modifier key", so both, the
console running it as a cook). A mouse click while the modifier is
held counts as another key (Shift-click selects). Tests: a scancode
stream through the driver with lone and chorded modifiers, the launch
marker only for the lone ones; Alt-Tab, Ctrl-C and shifted typing
untouched (the existing tests stay green).

## 11. Roux: a web engine of soupOS's own, and a browser (the user, 2026-10-12)

"Completely independent from Gecko, V8, everything", and the finish
line is the user's: navigate to Anthropic's website and go full circle.
Everything below is from scratch, as the TCP stack and the SSH client
were. Engine: roux (the base the sauce is built on); the program:
tureen.elf. Split into commits as the stages land:

- **11a. Sockets for cooks.** There are no TCP syscalls; the SSH client
  lives in the kernel. Add connect/send/recv/close on tcp.c's handles,
  and DNS through the existing resolver. Test: a cook fetches from a
  host-side server, bytes checked.
- **11b. HTTP/1.1 GET** over 11a: Host, Content-Length and chunked
  bodies, redirects (3 at most). Test: a host python server with
  chunked and redirected pages.
- **11c. TLS 1.3 client** (RFC 8446): x25519 and
  TLS_CHACHA20_POLY1305_SHA256 from the SSH client's chacha.c,
  curve25519.c and sha256.c, with HKDF on top; SNI. Rule 7: RFC 8448's
  trace as the published vectors, then a handshake against a host
  `openssl s_server`. Certificates are parsed but not yet verified, and
  the address bar says so in red until 11d.
- **11d. Certificate checks**: X.509 chains to a handful of bundled roots,
  ECDSA P-256 and RSA-PSS/PKCS#1 signatures, names and dates. Vectors
  from Wycheproof. Measure first which roots and algorithms
  anthropic.com's chain actually uses.
- **11e. Parser**: an HTML tokenizer (the spec's states that real pages
  hit), a tree with implied tags, entities. Test: pages on the host,
  the tree dumped to serial.
- **11f. Layout and paint**: block and inline flow, the font8x16
  glyphs at a couple of sizes, headings, lists, links, a little CSS
  (display, color, margin, font-weight from style blocks and
  attributes; the rest ignored), scrolling, clickable links, Back.
  Images are a later item. Test: a page rendered in QEMU, screenshot
  compared with a stored one.
- **11g. Full circle**: tureen opens https://www.anthropic.com through
  QEMU's user network. There is no JavaScript engine, so what shows is
  the server-rendered HTML; measure how much of the page that is before
  promising more. A JS engine of its own is a queue of its own if the
  user wants one. The test cannot depend on the internet: a saved copy
  of the page served locally over 11c; the live fetch is a manual check
  noted in ROADMAP.

## 12-18. More desktop programs (the user asked for some, 2026-10-12)

Each a ring-3 program with a window of its own (SYS_WIN_*, as frost,
oven and scale are), on the taskbar launcher's list, one commit each.
Rule 14 first: the desktop already has paint (frost), a timer (oven), a
calculator (scale), Mandelbrot (swirl), processes (expo), files, a
picture viewer, jot and neosim, so none of those again. Each gets a test
that drives it with synthetic events and checks serial markers, and a
host twin of its logic where there is logic to check (rule 4's way).

- **12. popcorn.elf: Minesweeper.** The mines are unpopped kernels; a
  click on one pops the lot. 9x9, 16x16 and 30x16; left click opens,
  right click flags, a number's both-buttons chord opens around it; the
  first click is never a kernel. Test: a seeded board from the host,
  clicks replayed, the opened cells and win/loss in the markers.
- **13. layercake.elf: Tetris.** Seven layers of cake falling, a row
  baked whole clears. The 7-bag randomizer and SRS rotation with wall
  kicks (the guideline's tables); levels speed up; hold and a next
  preview. Test: the kick table against the guideline's, a scripted
  game's line clears and score.
- **14. noodle.elf: Snake.** A noodle that grows on meatballs and
  ends on its own tail or the bowl's rim. The keys held through
  SYS_WIN_KEYS, so a quick double turn is not lost. Test: a scripted
  run, its length and the end it meets.
- **15. cuttingboard.elf: chess.** Two players at one board, or against
  the machine: legal moves only (castling, en passant, promotion), check
  and mate and the draws (stalemate, fifty moves, threefold). The
  machine is a small alpha-beta search. Rule 7's spirit: perft counts
  from the start position and Kiwipete to depth 4 against the published
  numbers, before any engine work.
- **16. waffle.elf: a spreadsheet.** A grid of cells (the waffle's),
  A1 references, + - * / and parentheses, SUM/AVG/MIN/MAX over ranges,
  recalculation in dependency order, a cycle shown as #LOOP. Saves to and
  opens CSV, so the text tools read it. Test: the formula engine on the
  host against cases with known answers written in the test, a sheet saved and read back in QEMU.
- **17. kettle.elf: a sound player.** WAV (PCM, 8/16-bit, mono/stereo,
  resampled to the mixer's rate) with a level meter and a seek bar.
  Measure first: there is no audio syscall for cooks (whistle and hum
  are the kernel's), so this begins with one that hands the mixer PCM,
  bounded, in its own commit. Test: a generated WAV through QEMU's
  wav audiodev, the captured file compared with the source.
- **18. grapevine.elf: an IRC client.** After 11a (cooks have no
  sockets until then). Connect, NICK/USER, JOIN, PRIVMSG, PING/PONG, a
  buffer per channel, the topic and names list. Test: a small IRC server
  on the host (a python script in scripts/), a conversation replayed and
  both sides checked. TLS (port 6697) once 11c is in.

## 19-21. Three more programs (the user, 2026-10-12)

Rule 14 closed two of the five suggested unbuilt: a screenshot is
already Print Screen's plateN.bmp (v0.60.185), and the settings panel is
the spice rack (v0.60.188, clock 12/24 included). What was left of the
settings idea is a spice rack row for item 10's trap (on, off), in item
10's commit.

- **19. cookbook.elf: a recipe box.** Recipes as plain text files in
  ~/recipes (a title, servings, ingredient lines "2 cup flour", steps),
  a list on the left, the card on the right, a servings - + that scales
  the amounts (fractions kept as fractions: 3/4 cup, not 0.75), and a
  few recipes on the disk to start with. Test: the scaling on the host
  against written answers (units, fractions, "a pinch" left alone), a
  card opened and scaled in QEMU.
- **20. Doom in a window.** Doom takes the whole screen; on the desktop
  it opens in a window (320x200 at 2x, the status bar included), its
  keys and mouse through SYS_WIN_* while focused, paused when
  minimised. Measure first how Doom draws today (doom.c in the kernel,
  not a cook) and what moving its output into a window costs per frame;
  if it must become a cook first, that is its own commit. Test: a timedemo
  in a window, frames in the markers, the console's full-screen Doom
  unchanged.
- **21. flapjack.elf: Pong over the network.** Two soupOS machines, a
  pancake between two pans. After 11a; UDP is simpler and enough (the
  host's ball, the guest's paddle, a frame number on every packet, late
  ones dropped); one machine alone plays the computer. Test: two QEMUs on
  a QEMU socket network, a scripted rally, both sides' scores agreeing.

## Review findings (2026-10-12)

(Added as the six reviewers report: kernel and shell; the desktop;
Mincecraft; neosim and rx.c; Doom; tests, build and docs.)

### Tests, build and docs (do these first: the other fixes lean on them)

- **R-T2 (high) — DONE v0.60.252 (disk-fresh.sh, disk-fresh-test).** check.sh never rebuilds the user programs or disk.img
  (`all: iso`, `$(DISK):` has no prerequisites), so a change under user/
  is tested only if someone ran `rm disk.img && make disk` by hand. Fix:
  check.sh builds the programs and fails if any differs from the copy
  inside disk.img (or builds its own disk in a copy).
- **R-T1 (high) — DONE v0.60.251 (gate-patterns-test).** The gate's "preempt_disable works" matches 'PASS'
  anywhere ([vmtest] PASS satisfies it, [pt] FAIL would not stop it). Fix:
  '^\[pt\] PASS'.
- **R-T3 (high) — DONE v0.60.253.** doom-fb-test passes when Doom never runs (a cleared
  screen agrees 99.8%). Fix: a Doom serial marker, a share of non-black
  pixels, a serial log for the login wait.
- **R-T4 (medium-high) — DONE v0.60.254.** Gate checks that accept failure: drop.elf's
  "exited with code" takes code 0 ("STILL ALIVE"); 'TESTOUT heap=' takes
  "heap=FAIL". Fix: exact patterns.
- **R-T5 (medium) — DONE v0.60.255 (keys-until-test).** qemu_keys' UNTIL after TYPE:/KEY: items anchors on the
  last plain line, so a prompt already printed satisfies it at once
  (bigdir-test's Tab, cont-test's up-arrow). Fix: anchor on each item.
- **R-T6 (medium) — DONE v0.60.256.** Mincecraft checks skipped when their markers are
  missing: fly-test's after-load check, play-test's walk/jump/focus. Fix:
  check the stretches exist before reading them.
- **R-T7 (medium).** gate/vga.sh's long-name check matches the typed echo;
  its interleave check passes when vgastress never ran. Fix: anchor both.
- **R-T8 (medium).** lint-test's "every script is run" uses grep -w, which
  "-" defeats (sound-test hides inside mincecraft-sound-test), and misses
  indented run lines. Fix: compare against the parsed TESTS list.
- **R-T9 (medium, suspected).** The 2 GB /tmp guard is below what 48 jobs
  hold (about 4 GB), and /tmp is RAM the memory cap does not count. Fix:
  JOBS x 80 MB + 1 GB, and /tmp in the memory budget.
- **R-T10 (medium).** challenge-test (run by hand) and make run/run-ai/
  run-console/run-tcp boot the tree's own disk.img (rule 16). Fix: boot a
  copy unless asked otherwise.
- **R-T11 (low-medium).** Cases expecting no output pass when the program
  crashes (headtail case 9, seq's empty cases). Fix: check the exit codes.
- **R-T12 (low).** A missing DOOM1.WAD shows eight tests as "ok". Fix: a
  skip code and SKIP in the summary.
- **R-T13 (low).** No time limit on a hung test or monitor socket. Fix:
  timeout per job, a socket timeout.
- **R-T14 (low).** gate.sh prints "no kernel panic" before it checks.
- **R-T15 (low).** qemu_keys drops QEMU's error replies (a mistyped key is
  silently lost). Fix: read the reply.
- **R-T16 (low).** random-test has no EXIT trap.
- **R-T17 (medium, docs).** CLAUDE.md's check section: remote-check called
  THE pre-commit check (rule 2 says local), "twice the cores", "exits on
  the first missing marker": all stale. **R-T18 (low, docs).** Five
  Mincecraft tests listed where there are 14; save.c's header comment says
  version 4.

### Kernel and shell (reviewer reproduced 1-6 in QEMU on copies)

- **R-K1 (critical, security) — DONE v0.60.257 (devkbd-test).** /dev/kbd: any cook's read freezes the
  machine (keyboard_getchar's hlt with interrupts off inside the syscall),
  and it has no foreground check, so a background or SSH program would
  read the console's keys, secrets typed at login and at chef included
  (src/vfs.c:318). Fix: through user_stdin_getchar (sti, foreground, kills)
  or refuse it from ring 3.
- **R-K2 (critical, security) — DONE v0.60.258 (clockout-rights-test; the owner rule, programs not killed at clockout).** A cook's background program takes the next
  user's rights after clockout: syscall checks use the shell's current user,
  not the process's owner (users.c eff_uid; usermode.c SYS_OPEN UNLINK
  MKDIR READDIR STAT KILL). Reproduced: the probe read /etc/kitchen as the
  headchef. Fix: every check from pr->owner; clockout and the desktop's
  shift end kill or hang up the user's jobs.
- **R-K3 (high) — DONE v0.60.269 (kfill-test).** A syscall copying into a program's promised but unmapped
  heap panics the kernel when the page cap and swap are full (a ring-0
  fault the pager declines). Fix: fail safely in ring 0 (pre-fault, or an
  exception-table return of -1).
- **R-K4 (high) — first half DONE v0.60.268 (the map to the highest frame; topram-test). R-K4b, the write buffer's cap, DONE v0.60.275 (writecap-test).** File writes buffer in RAM with no cap, and pmm hands out
  ~97 top frames the identity map never covered (it maps a count, not the
  highest frame): writing 113 MB panics (paging.c:90, vfs.c:215). Fix: map
  to the highest usable frame; cap or charge write buffers.
- **R-K5 (high) — SET ASIDE 2026-10-12: left for the user; the loop skips it.** Hostile ELF headers panic the kernel: e_phoff/e_phnum not
  checked against the size, p_filesz > p_memsz not refused (usermode.c:303,
  247). Fix: both checks, e_phentsize 32.
- **R-K6 (medium).** A pipeline whose later stage cannot start leaks zombie
  slots forever (shell.c:2262), shutting a cook out. Fix: proc_wait the
  stages killed.
- **R-K7 (medium, headchef only).** kill of a task blocked on a wait queue
  frees it while still linked (use-after-free); a killed mutex owner holds
  fat_mtx forever (task.c:343). Fix: unlink first, refuse mutex owners.
- **R-K9 (low-medium).** An open file keeps reading the freed clusters of a
  file deleted or overwritten under it; once reused, another user's data
  (fat.c delete and overwrite never look at fat_fds). Fix: refuse, or
  invalidate those fds.
- **R-K8 (low).** sbrk wraps on a huge increment and says yes. Fix: refuse
  newbrk < old.
- **R-K10 (low).** An IPv4 total length under the header length wraps the
  payload length; tcp/udp copy past the frame (net.c:426). Fix: refuse it.
- **R-K11 (low).** 16 open files machine-wide, 13 for one process: one
  program starves every redirect. Fix: a per-cook share or a bigger table.

### The desktop (reviewer reproduced 1-3 in QEMU on copies)

- **R-C1 (high, security) — DONE v0.60.259 (ctforeign-test; the focus-on-open half not taken).** Any cook's program, from anywhere (an SSH
  session too), can open a window on another cook's desktop: it goes on
  top, takes the focus and gets their keys (WEV_KEY and SYS_WIN_KEYS);
  under the clock-in screen it waits and has the focus once they clock in.
  SYS_NOTE writes on their bar the same way. No check compares the
  caller with ct_uid (countertop_win_open :526, countertop_note :2023).
  Fix: refuse a caller not owned by the desktop's cook (or not started
  from its sessions); no focus for a window not yet clicked.
- **R-C2 (medium, security) — DONE v0.60.263 (refused; ctcompostread-test).** Removing a file from a files window copies
  it into the cook's own compost with only 'w' checked: a write-only file
  becomes readable (ctx_remove :1460, compost_add :1590). Fix: 'r' too, or
  delete it outright when it cannot be read.
- **R-C3 (medium).** Leave leaks every open picture and files window's
  memory (3 MB a full-screen picture, a shift each time in clock-in mode;
  :2761). Fix: close every window through close_window on the way out.
- **R-C4 (medium).** A secret of 32 characters or more never lifts the
  cloche or clocks in (lock_buf[32]; hire takes 63): the only way out is a
  reboot. Fix: 64 bytes, as the login buffer.
- **R-C5 (test).** ctcloche-test cannot tell the 3-second wait from none.
  Fix: a screendump inside the wait, or the gap between its lines.
- **R-C6 (low).** The wheel during a terminal's drag select copies the wrong
  text, reading one byte out of bounds (sel_a -1). Fix: end the selection
  on scroll; sel_copy refuses sel_a < 0.
- **R-C7 (low).** The cloche comes down the moment a long Doom session ends
  (last_input not reset in ct_doom). Fix: reset it.
- **R-C8 (low).** The wheel still reaches windows under the cloche. Fix:
  && !locked.
- **R-C9 (low, suspected).** A title-bar drag outlives its window's close
  and can move the next window in that slot. Fix: clear drag in
  close_window.

### Mincecraft (reviewer reproduced 1-3 on the host with the game's own C)

- **R-M1 (high) — DONE v0.60.276 (mincecraft-ceiling-test).** Flying has no ceiling (player.c fly branch); a save with
  the feet above y 68 is refused on load (save.c:90), so the next start
  makes a new world and its Esc saves over the real one. Fix: cap flight
  (feet at most (WY+2)<<16) inside the load bound. Test: fly past 64, save,
  reload.
- **R-M2 (high).** Falling sugar lands in the cook's own cells (world.c
  fall step); the cook is stuck, cannot aim at the sugar it is inside
  (ray.c never tests the eye's cell), and jumping climbs up through solid
  ceilings (move_y/x snap as if the hit came from the leading edge). Fix:
  a fall stops where it would meet the player's box; moves leave the
  position alone when the box already overlaps.
- **R-M4 (medium) — DONE v0.60.277 (in fat_write; overwrite-safe-test; writecap still counts the old file as room, a fail there is clean).** A failed save over an existing world destroys it: fat
  frees the old chain before writing and the fail path leaves the entry on
  freed clusters (src/fat.c:1021, 1117). Fix: write PATH.tmp then replace,
  or fat_write allocates before freeing.
- **R-M5 (medium).** Only the first 9 kinds held can be chosen; 11 can be
  held, so the grater and tealights can become unusable (player.c hotbar,
  main.c). Fix: page the slots, or pin a slot to an id.
- **R-M6 (low).** "The 16 nearest tealights" are the first 16 in scan order
  (main.c:143); past 256 they are dropped silently. Fix: keep the 16
  smallest distances.
- **R-M7 (low).** Sugar sinking through broth leaves air holes for good
  (world.c:99 writes air, not what was below). Fix: swap the two cells.
- **R-M3 (low).** Space held under a 2-high ceiling hovers at +0.2 (move_y
  leaves ground set on a blocked upward move). Fix: ground = 0 there.
- **R-M10 (low).** The cloud drift is a signed int that overflows after
  about 18 h of play (main.c:288). Fix: wrap it at a multiple of the
  16-block region.
- **R-M8 (test).** The health test's broth case passes if the boot never
  loads the world. Fix: require the load and a y in the pool.
- **R-M9 (test).** The save test checks only "loaded" for version 3 and 4
  files, not the hearts and flying they should load with. Fix: -check
  prints hearts= fly=; check them.

### neosim and rx.c (reviewer: 401 existing cases replayed faithfully on a host harness with ASan; 1-2 also in QEMU)

- **R-N1 (high) — DONE v0.60.287 (neosim-longcmd-test).** A 79-character : or / command crashes neosim (the
  cursor drawn at column 80, past the body; screen.c:114, :67). Fix: clamp.
- **R-N2 (high) — DONE v0.60.288 (neosim-fullpaste-test; the other insert sites checked).** A linewise p on the last line of a full 2048-line buffer
  crashes it (paste_from after a failed insert). Fix: return when nothing
  was put in; audit the cy++ after failed inserts.
- **R-N3 (high, data) — DONE v0.60.278 (neosim-writeas-test).** :w OTHER renames the buffer and clears [+]: the
  original is never written, :q then quits losing it. Fix: rename only a
  nameless buffer; saved/changed only for its own path.
- **R-N4 (high, data) — DONE v0.60.285 (neosim-bytes-test; a tab is one blank cell, not expanded).** Tabs become spaces and CRs vanish on load, so a
  saved Makefile is broken (text.c:41). Fix: keep tabs (expand when drawn),
  keep or report CRLF.
- **R-N5 (high, data) — DONE v0.60.286 (neosim-longjoin-test).** Joins over 255 columns drop text (J, multi-line
  deletes, Backspace at column 0). Fix: refuse before changing anything.
- **R-N6 (high).** cw on a one-letter word or a word's last letter changes
  the next word too, across lines. Fix: vim's stay-at-end rule.
- **R-N7 (high).** Undo and redo wreck every mark (pop rebuilds through the
  lines hook). Fix: suspend the hook in pop; marks in the snapshot.
- **R-N8 (medium-high).** At 1000 lines the number column writes before the
  body and the numbers wrap. Fix: numberwidth max(4, digits+1); clamp put.
- **R-N9 (medium).** A failing motion or command does not stop a macro or
  :normal (10@a past the end, :%normal fzx deletes where there is no z).
  Fix: a failure flag the players check.
- **R-N10 (medium).** Counted motions that cannot be met still act (5}x,
  G2dd, 3>ap); the para test's counts are weak (a count-blind findpar
  passes all 32). Fix: fail as vim; a text where each count lands apart.
- **R-N11..N13 (medium).** op_delete's linewise rule (2D 3d$ leave a blank
  line); nostartofline (dd >> << :N keep the column); autoindent edges
  (Enter keeps the moved text's blanks; cc S lose the indent).
- **R-N14..N18 (medium).** . repeat gaps (a register prefix lost; an insert
  over 256 keys repeats the change before it; N. keeps the old count;
  visual . ends at the cursor's column); macro recording (:normal keys
  recorded twice, arrows dropped, linewise registers played without their
  newline); :normal reads its keys from E.cmd, which a nested : overwrites;
  visual mode (f t F T ' ` do not take their character, text objects
  replace the selection, " registers, v$ without the line break); z and g
  pending state survive Esc.
- **R-N19..N27 (low-medium).** 2"a3dd deletes 23; r<CR> puts a \n inside a
  line; Ctrl-A on 0b binary, 64-bit hex, leading zeros and the hex case;
  :s///g matches its own output, :s/a with no replacement, :99; vim
  patterns (\{n} literal, [\]], ^*, n onto overlapping matches); block
  objects do not search past the line; "1-"9 for named and } ` deletes;
  an empty buffer saved as one newline; undo steps with arrows, and the
  cursor after undo.
- **R-N28 (low, rx.c).** Over 64 top-level alternatives turn into literal
  | silently; {,n}, a{1,2}{3}, a{256}, ^*a, [z-a], [[:foo:]], empty groups'
  places. Fix: grow or refuse; GNU's readings.
- **R-N29 (low).** Caps that lose data quietly: the 64 KB unnamed
  register, a file over 2048 lines written back cut, a redo arena smaller
  than a full snapshot. Fix: refuse or warn.

## Area review findings (2026-10-12, began at e8bc3b7)

### Tests, build and docs (reviewer confirmed 1-3 on copies, with mutations)

- **R-T19 (high) — DONE v0.60.260 (fails with the R-K2 fix reverted).** clockout-rights-test passes on the unfixed kernel when
  the login is slow: infuse waits a fixed 12 s, and woken before the
  headchef's Welcome it is refused by the login screen, not by the fix
  (shown: WAIT:3 before rosemary, the resolver fix reverted, PASS). Fix:
  infuse waits for a file the headchef makes after clocking in, or the
  refusal must come after the Welcome.
- **R-T20 (medium) — DONE v0.60.261 (fails with the foreground check removed).** devkbd-test's background check cannot fail: a spoon
  that reads the console's keys never closes /KEYS.TXT, so the file is
  missing and "nothing in it" passes (shown with the foreground check
  removed: keys were stolen, the test blamed a hang). Fix: the background
  spoon must have exited before the word is typed; /KEYS.TXT must exist and
  be empty; the shell's echo whole.
- **R-T21 (low-medium).** disk-fresh.sh compares only the programs, not the
  recipe's text files, bowls and /etc/kitchen; CLAUDE.md says more. Fix:
  compare with a disk built into a temp file, or say "programs".
- **R-T22 (low).** ctforeign-test never checks that no key reached
  saucier's prod, nor a foreign SYS_NOTE. Fix: both.
- **R-T23 (low, docs).** UNTIL's anchoring is undocumented: the last plain
  line's echo, KEY:/TYPE: since v0.60.255, not MON:, and a line with no
  echo (a secret) waits 10 s before anything counts.

### Kernel and shell (reviewer confirmed K12 in QEMU on a copy)

- **R-K12 (critical, security) — DONE v0.60.262 (soupyc-perms-test).** soupyc's open(), read(), write() and its
  file loads call vfs_open with no users_may in the ordinary build
  (soupyc.c :738 :1634 :1674 :1704). soupyc runs in ring 0 on the shell's
  task, so `soup -c` as any cook reads /etc/kitchen (reproduced: the
  headchef's hash printed where `pour` said Permission denied) and writes
  anywhere (a file made in /). The unchecked open is the CHALLENGE build's
  taught stage 1 (challenge.c), not the ordinary build's. Fix: users_may
  ('r' or 'w', as SYS_OPEN asks) on every soupyc open under NO_CHALLENGE.
- **R-K13 (high, security, suspected) — DONE v0.60.264 (kbdtap-test).** soupyc can still read /dev/kbd
  through dev_kbd_read: no freeze (interrupts are on there) but no
  foreground check, and a spawn()ed soupyc task outlives clockout, so it
  could read keys typed at the next login. Fix: refuse /dev/kbd to all but
  the foreground, or to soupyc entirely.
- **R-K14 (high, once K12 made soupyc ask) — DONE v0.60.266 (soupyc-spawn-rights-test).** A soupyc spawn() task has no proc, so the resolver
  gives it the console shell's current cook: one outliving clockout makes
  files owned by the next cook. Fix: an owner on soupyc tasks (or their end
  at clockout), with K12.
- (Its F4 is R-C10 below.)

### The desktop (reviewer confirmed 1 and the apostrophe paths in QEMU on copies)

- **R-C10 (medium-high) — DONE v0.60.265 (gone slots freed at the start; no per-program cap yet; ctslots-test).** A program left running from an earlier shift
  keeps its window slots and their kernel-heap pixels into later shifts:
  the desktop's start frees only slots of dead programs (:2606), Leave
  only marks them gone, and APP_MAX is 8 for the whole machine with no cap
  per program. One program's 8 small windows shut every later cook out of
  program windows (neosim, frost, Mincecraft) until a reboot; 7 at 640x480
  held 7.2 MB of heap. Fix: free every gone slot (or every slot not of the
  new ct_uid) at the start; a cap per pid and per cook; px against a budget.
- **R-C11 (low-medium, regression of v0.60.250).** The double click passes
  the quoted path as a program's arguments, capped at PROC_ARGS_MAX 128,
  where jot had a whole input line: a path near FAT_PATH_MAX, or shorter
  with apostrophes (4 bytes each quoted), is refused. Fix: arguments past
  FAT_PATH_MAX plus quoting.
- **R-C12 (low) — DONE v0.60.267 (no test can make the race; ctforeign-test still passes).** ct_admits is read before app_mtx is taken (:545,
  :2041): a call between ct_running 0 and 1 gets a slot unchecked. Fix:
  the test inside the mutex.
- **R-C13 (low).** chef cook of a windowed program from the desktop's own
  terminal runs it as the headchef, and it is refused (owner 0 is not
  ct_uid). Fix: admit programs started from this desktop's sessions (a
  session tag on proc_t), or say so in the docs.
- **R-C14 (low).** The refusal is logged once for one pid only: two
  foreign pollers alternate and log every call. Fix: a told flag in proc_t.
- **R-C15 (nit).** `cook neosim.elf` finds ~/neosim.elf first (the cook's
  own, no gain); `static int ct_uid;` declared twice.

## Area review findings (2026-10-12, began at a6f1c1c)

### Tests, build and docs (reviewer reverted 11 fixes in copies; 9 tests failed as they should)

- **R-T24 (medium) — DONE v0.60.271 (the SSH half removed; the guard kept, said untested).** kbdtap-test's SSH half cannot fail: the shell's pour
  never reaches the device ("Not found: /dev/kbd"), so with v0.60.264's
  dev_kbd_read guard removed it still passed; no ring-0 reader on another
  terminal was found at all (cook spoon.elf < /dev/kbd over SSH hangs on
  both builds), so that guard and CLAUDE.md:297's line may be dead. A
  quick EOF also passes, an auth failure included. Fix: a real path or
  drop that half and its doc; a marker in the SSH output either way.
- **R-T25 (medium) / R-K20 (soupyc) — DONE v0.60.272 (soupyc-include-test).** include never finds a file in any
  build: it copies tok.sval, but string literals live in tok.str
  (soupyc.c:752); the disk's INCLUDE.SC fails, and no test runs it. So
  soupyc-perms-test's include check cannot fail. Fix: tok.str; an include
  test (INCLUDE.SC prints "cube(3)  = 27"); the perms case then refused by
  the rule, beside one saucier may read.
- **R-T26 (low, flaky toward FAIL).** UNTIL texts inside their own typed
  line ("OPEN ", "LINES ", "WOPEN ", "MINE ", "KBD ") are met by the echo;
  under load the output can come after the WAIT:1 that follows. Fix: split
  the marker ("MI" + "NE ") or wait on the result text.
- **R-T27 (low, docs).** CLAUDE.md:1110-1117 still says a program is killed
  at first touch past the cap, and says nothing of v0.60.269's pre-fault.

### Kernel and shell (reviewer confirmed K15 in QEMU on a copy)

- **R-K15 (high, my v0.60.269 incomplete) — DONE v0.60.273 (kfill-test's big read).** One syscall over a buffer
  larger than the page cap still panics: the pre-fault evicts the buffer's
  own first pages, can take the last swap slot doing it, and the copy's
  touch of page 0 then needs swap_in, which calls swap_out_one BEFORE it
  frees the incoming slot (swap.c:145), finds none, and declines in ring 0
  (reproduced: one sys_read of 5114 pages after kfill's measure). The
  comment's "swap having just made room" is wrong. Fix: swap_in reads into
  a fresh frame and frees its slot before evicting; and/or user_ok and the
  copy work in chunks under the cap; a kfill-test case for one big call.
- **R-K16 (medium, regression of v0.60.269, suspected) — DONE v0.60.273 (no pre-fault; a budget).** The pre-fault
  costs the whole buffer, not what is copied: a 20 MB read of a 47-byte
  file maps, zeroes and swaps out thousands of pages first, filling the
  machine's swap. Fix: clamp to what can be copied, or chunk (with K15).
- **R-K17 (medium, security; reproduced) — DONE v0.60.274 (soupyc-spawn-input-test).** A spawn()ed script's input()
  reads the console's keyboard with no foreground rule (soupyc.c:1160):
  the /dev/kbd hole K13 closed, by another door; it outlives clockout and
  races the next login for its keys. Fix: input() gives an empty line in a
  task with uid set (a spawned one), or soup: tasks end at clockout.
- **R-K18 (low, suspected).** users_may_open checks "/" as the parent of
  a new relative path ("home/rosemary/x"), too strict for soupyc's
  unresolved paths. Fix: keep the computed parent, or prefix "/".
- **R-K19 (low, suspected).** A killed soup: task never runs release_ctx:
  its vfs nodes (16 machine-wide) and buffered writes are lost.

### The desktop (reviewer confirmed 1 in QEMU on a copy)

- **R-C16 (medium-high, security) — DONE v0.60.270 (modes kept, compost rwx---; put back still takes the desktop cook as owner; ctcompostmode-test).** Compost, copy and put back drop the
  file's mode: stream_copy makes the new file FAT_PERM_DEFAULT (rwxr-x),
  and the headchef's compost is /compost at the root (made rwxr-x too),
  so a rw---- file the headchef composts is every cook's to read
  (reproduced: saucier poured /compost/priv.txt). Composting /etc/kitchen
  would hand out every hash; /compost/.whence names every origin. Put back
  and a drag copy lose the mode the same way. Fix: stream_copy keeps the
  source's mode (and the owner on put back); /compost and ~/compost
  rwx---, .whence rw----; perhaps refuse the pinned system files.
- **R-C17 (low, mine, v0.60.263).** The new refusal note is 79 characters;
  the bar shows about 43, so its advice is cut off. Fix: "not yours to
  read: strain it in a terminal".
- **R-C18 (low).** A file the cook may not write is refused with only a
  log line; the unreadable one gets a note. Fix: a note for both.
- **R-C19 (low, suspected).** A full .whence (about 2 KB) skips the new
  line silently: the file is composted but put back cannot find its home.
  Fix: refuse with a note, or a bigger index.
- **R-C20 (low, suspected).** ct_admits' log line now goes out under
  app_mtx (R-C12): with R-C14's alternating pollers every refusal holds
  the mutex for a serial line. Fix: R-C14's told flag, or log after the
  unlock.

## Full review findings (2026-10-12, began at c6c8d32)

### neosim and rx.c (reviewer: host harness with ASan against nvim, 14k fuzz runs, 1.2M rx patterns; 1-3 and 5 also in QEMU)

- **R-N30 (high, data, my R-N3 incomplete) — DONE v0.60.284 (neosim-writeas-test).** :wq OTHER and :x OTHER on a
  changed named buffer write OTHER and quit, the edit lost (main.c:518;
  reproduced). Fix: quit only after writing the buffer's own path or when
  nothing is changed; else E37 as :q.
- **R-N31 (high, crash) — DONE v0.60.289 (neosim-bighex-test).** Ctrl-A on a long hex number overflows out[40]
  (main.c:1000; 0x and 61 digits: neosim exits, unsaved work gone). Fix:
  refuse past 32 digits, or a T_COLS-sized buffer.
- **R-N32 (medium-high, data) — DONE v0.60.297 (neosim-searchend-test).** A search leaves the cursor one past the
  line's end (/$, /\>, /x*): p then writes a NUL into the file (reproduced,
  abcdef\0). Fix: clamp in search(); text_insert_char refuses col > len.
- **R-N33 (medium-high).** \n \t \r in patterns become the letters n t r
  (vim2ere, main.c:198): :%s/\n// deletes every n. Fix: \t a tab; refuse
  \n, \r and unknown escapes.
- **R-N34 (medium).** :normal leaves a half-typed command pending: :normal
  d then j deletes two lines (reproduced). Fix: clear P and reg_sel after.
- **R-N35 (medium).** :{range}normal walks fixed line numbers: :%norm dd
  deletes every other line. Fix: gmark the range as :g does.
- **R-N36 (medium).** :s replacement has no \r line break and no \u \U \l
  \L \e, ~ (inserts the letter). Fix: \r splits; refuse the rest.
- **R-N37 (medium).** Operators take no search motions (d/x, dn, d*).
- **R-N38 (medium-low).** Visual :, p, J, gJ, o are missing (:'<,'>s).
- **R-N39 (low-medium).** Keys silently ignored: s S Y W B E ZZ ZQ, Enter,
  Space, Backspace in normal mode; Del Home End PgUp PgDn; "_ falls
  through; dw on an empty line; it/at; \c; :s#a#b#; :s///gc's c; :w! :wq!
  :qa :qa!.
- **R-N40 (low).** Tab inserts four spaces, not to the next stop of 4.
- **R-N41 (low, rx.c).** RX_MAXCLS 32 counts every copy a count makes:
  [0-9a-f]{40} fails in sift -E and reckon. Fix: a slot per source [.
- **R-N42 (low).** Alternation is leftmost-longest, vim's leftmost-first;
  :s/b*/-/g allows an empty match after a match.
- **R-N43 (low).** u inside :normal or a macro breaks the one-step undo.
- **R-N44 (low).** Counted n N * # rescan the file each count (1000n:
  minutes under QEMU, no interrupt). Fix: stop when the match repeats.
- **R-N45 (low, nits).** A 2048-line file says "too long"; :sort n overflows
  past 10 digits; cut lines written back cut; msg_num drops a quote.

### Tests, build and docs (reviewer reverted each new fix in copies)

- **R-T28 (medium-high, mine) — DONE v0.60.281 (fails with the map fix reverted).** topram-test can no longer fail: v0.60.275's
  write cap stops /dev/zero > /z at the disk's room (28 MB), far below the
  top frames at -m 128M (reproduced: paging.c and pmm.c reverted, it still
  passes). Fix (the reviewer's, confirmed): boot it -m 32M, where free RAM
  is under the disk's room; require "out of memory at" so it cannot go
  dead again; fix its header and ROADMAP's line.
- **R-T29 (low-medium, mine) — DONE v0.60.282 (fails with the two chmods removed).** ctcompostmode-test guards only stream_copy's
  mode: with the bowl's chmod or .whence's removed it passes, though saucier
  can then list /compost (names, owner, sizes). Fix: serve /compost as
  saucier must be refused, no priv.txt in it.
- **R-T30 (low, suspected).** keys-until-test's margin is thin: the broken
  driver returned in 1.2-1.6 s against a 2600 ms floor. Fix: time from the
  fake monitor's receipt of the key.
- **R-T31 (low, suspected).** mincecraft-ceiling-test needs 220 ticks in a
  14 s hold; under load (under 5 fps, 4 catch-up ticks a frame) "reached
  it" can fail. Fix: hold until a p line shows y 66, with a limit.
- (Its note on write_limit's comment is R-K25.)

### Doom (reviewer's host harness matched a QEMU frame on 100% of pixels; item 0b)

- **R-D20 (high, looks) — DONE v0.60.299 (doom-sky-test; u by xtoviewangle's arctangent, not the linear one proposed).** The sky is black: sky_ensure decodes SKY1 with
  patch_blit, which subtracts its offsets (127, 123), leaving a 129x5
  strip (doom.c:899). It also turns the wrong way, at a quarter of Doom's
  scale, and squeezes 128 rows into 84. Fix: patch_composite; u from
  pl_angle*8 + (SCR_CX - sx)*256/dw, v = y/ds + 16.
- **R-D21 (high, looks) — DONE v0.60.307 (doom-walls-test; the pegged upper and lower are not in its view).** Wall textures stretch over the sector's height
  instead of a texel a map unit (v_step = th/wall_h); the upper and lower
  pegging offsets have their signs wrong; lower-unpegged mids swim with
  distance (doom.c:1365, 1573-1624). Fix: V in map units, offsets from the
  front ceiling (mid 0; unpegged lower th-(fc-ff); upper bc+th-fc; lower
  bf-fc; plus yoff).
- **R-D22 (high, looks).** Sprites draw through solid walls: those close
  col_top/col_bot only, never spr_top/spr_bot (doom.c:1589, 2701). Fix:
  solid walls close the sprite clip too; CLAUDE.md pitfall 8 is stale.
- **R-D23 (medium-high, looks).** Floors and ceilings are all at the
  player's own sector heights (doom.c:978): steps and pits slide. Fix:
  visplanes, a span per sector height.
- **R-D24 (medium).** Imp fireballs are a 3x3 dot at screen centre, seen
  through walls. Fix: the BAL1 sprite, clipped.
- **R-D25 (medium).** Armor does nothing: all damage comes off health.
  Fix: a third (green) or half (blue) absorbed, as P_DamageMobj.
- **R-D26 (medium).** Things are not solid (try_move tests lines only);
  barrels can be neither shot nor exploded.
- **R-D27 (medium).** Chasing monsters never turn: the rotation is from the
  spawn angle, so one facing away runs at you back first. Fix: the angle
  from the move direction.
- **R-D28 (medium).** Only the first sector with a tag moves (both loops
  break at it): E1M4's 4-sector door, E1M6's 5, lifts with 2.
- **R-D29 (medium).** Many E1 line specials do nothing (5 7 8 9 16 18 20
  22 23 35 36 48 70 76 82 90 91 97 98: teleports, stairs, floor lowers,
  the donut, scrollers); sector lights 2 8 12 13 do nothing, 17 is really
  8's glow, strobes dim to 0, flicker is a fixed blink. Whether any blocks
  a map is a play-through each.
- **R-D30 (medium).** Monster attacks are one rule: every monster melees
  10 within 72 units, through thin walls; hitscans never miss; no pain
  states; barons have no fireball.
- **R-D31 (medium-low).** Waking: all 360 degrees within 1024 units, no
  gunfire alert, ambush flag ignored, no shots past 768.
- **R-D32 (medium).** The player moves 800 u/s (Doom 280 walking, 560
  running; no run key); the pistol fires every 0.15 s (Doom 0.54;
  doom-mouse-test asserts 0.15); fixed 10 damage; a 20-degree autoaim cone.
- **R-D33 (medium-low).** Keys carry over to the next map.
- **R-D34 (low-medium, looks).** Lighting: bright sectors fade to black
  with distance; rows by the horizon are black; no wall contrast. Fix:
  scalelight/zlight.
- **R-D35 (low-medium, looks).** Masked middle textures (window bars) never
  drawn; upper textures between two sky ceilings drawn where Doom shows sky.
- **R-D36 (low).** Bloody mess (10, 12) draws a standing marine; dead
  player, backpack, candle, candelabra missing; items never animate; the
  spectre is opaque.
- **R-D37 (low).** Use reaches through walls (player_use does not stop at
  the first closed line).
- **R-D38 (low, looks).** Flats mirrored north-south (v = wy & 63).
- **R-D39 (low).** vy*PROJ_DIST overflows at 960x600 past ~4370 units
  (doom.c:2643, 1479). Fix: int64.
- **R-D40 (low).** Pickups that do nothing: radiation suit, light amp,
  computer map, invisibility, rocket launcher, chainsaw; no fist; the
  automap shows everything and secret lines.

### Kernel and shell (reviewer confirmed K21-K24 and K27 in QEMU on copies)

- **R-K21 (critical, security) — DONE v0.60.280 (soupyc-perms-test).** soupyc's remove(path) calls fat_delete
  with no rights check in any build (soupyc.c:1585), missed by K12:
  reproduced, saucier removed /etc/kitchen, and the next boot reseeds the
  headchef with the stock secret and drops every hired cook. Fix: under
  NO_CHALLENGE users_may(path, 'w') as strain asks, bowls refused; a
  soupyc-perms-test case.
- **R-K22 (high, panic) — SET ASIDE 2026-10-12: left for the user; the loop skips it.** user_ok's budget counts every page past the cap
  as one eviction can buy, but only resident heap pages in the fifo can be
  evicted: the image and the stack cannot. Reproduced: a 1010-page .bss,
  then a 200-page read into the stack: panic at cr2 0xc7f28000. Also, after
  swap_in's own eviction fails, upages stays at CAP+1. Fix: count past the
  cap only up to fifo_len (heap pages); better, a ring-0 copy that can
  return -1 (an exception table).
- **R-K23 (high, panic) — SET ASIDE 2026-10-12 with R-K22: left for the user.** A page on swap is not counted at all, but
  swap_in needs a free frame first: with every frame taken (write buffers,
  K24), a read into a swapped page panicked (reproduced, cr2 0xc4000000).
  Fix: at least one free frame when any page of the range is on swap.
- **R-K24 (high, my R-K4b incomplete) — DONE v0.60.283 (one budget, the disk's room; writebudget-test).** The write limit is per handle:
  each may buffer the whole free space, and a program has 13 (reproduced:
  four at 28 MB each, then out of memory, ~120 MB of buffers). Fix: one
  budget over all open write nodes (or a share per cook), charged in
  mem_ensure, released at close.
- **R-K25 (low-medium, suspected).** v0.60.277's overwrite needs size <=
  free, but write_limit still allows room + old: such a rewrite is buffered
  then fails at close (a big Mincecraft save on a nearly full disk). Fix:
  limit = room; say an overwrite needs room for both. Room also counts the
  reserve a cook cannot use.
- **R-K26 (low).** swap_out_one drops a page from the fifo before its
  ata_write: a failed write leaves it unevictable for good. Fix: out of
  the fifo after the write.
- **R-K27 (low, rights).** vfstest is not headchef-only: saucier made
  /VFSTEST.TXT in / with it (shell.c:5667). Fix: headchef_only.
- **R-K28 (low, suspected).** Ctrl-Z on a program blocked in a stdin read
  waits for the next key, which the program eats. Fix: break on
  stop_pending too.

### The desktop (reviewer confirmed 1-3 in QEMU on copies)

- **R-C21 (medium).** Pastes and typed command lines over 128 keys are cut:
  wt_key pushes into a 128-entry ring with no yield (countertop.c:254,
  :348), and the shell does not drain it meanwhile (reproduced: "pasted
  206 bytes", 128 arrived). The double click's `cook neosim.elf '...'`
  loses its quote and Enter past a ~110-character path: a second limit on
  top of R-C11. Fix: a ring of INPUT_MAX x 2 and a paste that waits for
  room (or a pending buffer the loop drains).
- **R-C22 (low-medium).** open_terminal leaks the 76 KB scrollback on both
  failure paths (:1370, :1373; reproduced: 20 failed opens, +1.5 MB of
  heap); ~100 would empty the kernel heap. Fix: free t->sb (a wt_free).
- **R-C23 (low-medium).** The taskbar draws 5 buttons but its click test
  goes on: slot 5 answers clicks where nothing is drawn, slot 6 and up are
  off the screen, and a minimised window there can never come back
  (Alt+Tab skips minimised ones). Fix: the same cut in taskbar_hit,
  narrower buttons, or Alt+Tab listing minimised windows.
- **R-C24 (low, suspected).** stream_copy sets the copy's mode after the
  data: in a public bowl another cook's poller can open it in the gap.
  Fix: a 0-byte file, chmod, then the data (an overwrite keeps the mode).
- **R-C25 (low).** R-C16 repairs a /compost made before it only at the next
  compost; its files keep the rwxr-x they were given. Fix: chmod the bowl
  and .whence when the desktop starts or the compost window opens.

### Mincecraft (reviewer built the game's C on the host)

- **R-M11 (medium).** Landing never ends flying: move_y zeroes vy on any
  hit, so the fly branch's p->vy < 0 (player.c:86) is never true after a
  landing; "fly off (landed)" can never print and CLAUDE.md's "landing
  ends it" is false (host: Shift to the ground, y 11.000 fly=1 ground=1).
  Fix: test in->down (or the downward hit) before move_y; a fly-test step
  that sinks to the ground.
- **R-M12 (low-medium).** The chosen slot is a bare index into kinds packed
  by id: one more kind or one run out shifts it (slot 2 butter becomes
  crumb), so r and e act on another block. With R-M5: keep the chosen id.
- **R-M13 (low, looks).** The cast-iron edge walls go on above the world
  in the picture (world_get gives cast iron outside the grid at any y).
  Fix: out-of-grid cells at cy >= WY are air to the ray.
- **R-M14 (low, looks).** Clouds vanish with the eye at or above y 52
  (render.c:104), which flying now reaches. Fix: draw them below too.
- **R-M15 (low, suspected).** The left button can stick if the full event
  queue drops its WEV_UP (countertop.c:513): breaking goes on. Fix: a
  move with the button up counts as up; the desktop never drops an UP.
- **R-M16 (low, suspected).** A refused world whose .bad copy fails (the
  path plus ".bad" past 128) is then saved over with a new world. Fix:
  save elsewhere or warn when keep_aside fails.

## Area review findings (2026-10-13, began at 8eebe4e)

### Tests, build and docs (reviewer reverted every fix in copies; all but one test failed as it should)

- **R-T32 (medium, mine) — DONE v0.60.290 (catches the decrement removed).** writebudget-test cannot catch a count never
  given back: with `wbuf_pages -= n->npg` deleted from mem_drop it passes,
  though every later write in that boot then fails. The append path's
  mem_drop is untested too. Fix: after the writers stop (Ctrl-C or kill;
  wash only clears), a small redirect must reach the disk. With R-K29.
- **R-T33 (low, docs).** CLAUDE.md says write buffers are a doubling heap
  buffer (they are pmm pages) and says nothing of the per-handle cap, the
  shared budget, or the log lines tests grep for.
- **R-T34 (low, docs).** CLAUDE.md's remove() clause sits mid-sentence as if
  remove() went through users_may_open/sc_open (it asks users_may itself);
  its fat_is_dir is redundant with fat_delete's own refusal.
- **R-T35 (low, flaky toward FAIL).** ctcompostmode-test types its perms
  commands 2 s after a re-login; a slow login loses one. Fix: slurp/UNTIL
  first (the saucier login too).
- **R-T36 (nits).** topram-test on a panic also says "memory never ran out"
  pointing the wrong way; ctcompostmode-test's header is stale;
  neosim-bytes-test misses a lone CR, a CR as the last byte, a CRLF split
  across a read (the cr carry).

### Kernel and shell (reviewer confirmed 1-3 in QEMU on copies)

- **R-K29 (high, my v0.60.283) — DONE v0.60.290 (writebudget-test).** A killed soup: task never runs
  release_ctx, so its write nodes are never closed and their pages stay in
  wbuf_pages for good: after `kill` of a script that had buffered 28 MB,
  every buffered write on the machine is refused until a reboot (reproduced:
  "stops at 0 bytes"). Fix: close the task's soupyc files when it is killed
  or reaped (a kill hook), or a count the reaper can give back per node.
- **R-K30 (medium-high, my v0.60.283) — DONE v0.60.292 (a cook half, cooks three quarters; writeshare-test).** One budget for all: one cook's
  background writer (it outlives clockout) holds the machine's, and every
  other cook's redirect, soupyc write, neosim and Mincecraft save fails
  (reproduced). Fix: a share per cook, a reserve for the headchef; or say it.
- **R-K31 (medium, my v0.60.283) — DONE v0.60.291 (fullbudget-test).** wbuf_room 0 means "not measured" too, so
  on a full disk the shared cap switches itself off and each overwrite
  handle may buffer its file again (reproduced: three at 28 MB, no
  "together"). The first append after boot is unchecked the same way.
  Fix: a separate measured flag; room 0 refuses past the old size.
- **R-K32 (low, suspected).** wbuf_room is measured only at a handle's first
  write: stale-low an append open fails as "cannot open"; stale-high a
  started handle buffers past what now fits. Fix: re-measure before
  refusing and at the append load.
- (Note: the budget is the disk's room, not memory; on a disk with more
  room than RAM it guards nothing. min(room, a share of free frames) is
  the reviewer's suggestion.)

### neosim (reviewer: host harness, ASan, 7-byte reads, 300 random runs against nvim)

- **R-N46 (high, data, my v0.60.285) — DONE v0.60.293 (neosim-bytes-test).** Undo or redo drops the CRLF flag:
  undo.c's pop() rebuilds through text_clear, which now clears crlf; x u
  :w on a CRLF file writes it LF. Fix: crlf kept across pop (reset only in
  text_load).
- **R-N47 (medium-high, data, my v0.60.285) — DONE v0.60.294 (neosim-bytes-test).** One CRLF line marks the whole
  file CRLF, so every LF line gains a CR on :w; nvim reads such a file as
  unix and keeps the CR as text. Fix: crlf only when every newline had a
  CR; else the CRs stay in their lines.
- **R-N48 (medium, my v0.60.286) — DONE v0.60.295 (neosim-longjoin-test).** A refused join still takes the register,
  starts an undo step and sets [+] (op_apply asks too_long_join only in
  delete_chars, after reg_take_chars and change_begin); a refused c goes on
  into insert; J starts its step before its check. Fix: ask before any of
  that.
- **R-N49 (medium, my v0.60.288) — DONE v0.60.296 (neosim-fullpaste-test).** A refused paste or o/O into a full buffer
  leaves an empty undo step and [+] (change_begin first). Fix: room first.
- **R-N50 (low-medium) — DONE v0.60.298 (neosim-fullpaste-test).** A multi-line paste into a nearly full buffer joins
  the register's lines (insert_text drops the \n when text_split fails),
  cut at 255, silently. Fix: stop and say the file is full.
- **R-N51 (low-medium, my v0.60.285).** Code that knew only spaces meets
  tabs: J skips only spaces and adds one before a tab; << removes no tab;
  >> puts spaces before it; autoindent ignores a tab indent; cw on a tab
  becomes ce; the highlighter misses \t#define. (j/k keep the byte column:
  the one-cell design, noted.)
- **R-N52 (low).** :wq ./file on buffer file writes its own file but the
  string compare says OTHER, so E37. Fix: normalise the path.

## Area review findings (2026-10-13, began at 3c49bd7)

Four areas changed since 8eebe4e: kernel and shell, neosim, Doom, and
tests, build and docs. Nothing critical, so the full review stays at 30.

### Kernel and shell (reviewer confirmed K33 and K34 in QEMU on copies)

- **R-K33 (high, my v0.60.290) — DONE v0.60.300 (killclose-test).** kill can hang the shell for good.
  task_kill only marks the task DEAD wherever it stopped, and
  soupyc_release_killed then runs release_ctx on the killer from the dead
  task's half-done state. Killed inside close(f): fat_write_from holds
  fat_mtx for the whole flush (fat.c:1572), the dead task owns it, and
  m_sc_files[i] still points at the node (soupyc.c:1780 clears it after
  vfs_close), so the killer's vfs_close blocks on the mutex forever: no
  "killed task", no prompt. Narrower windows (traced): killed in
  mem_drop's free loop (pages freed twice), after the flush before
  n->used=0 (junk written), after vfs_close before sc_files[i]=0 (another
  task's handle closed), between n->npg++ and the wbuf counts (wbuf_pages
  wraps, every buffered write refused). Fix: a deferred kill, as
  proc_kill_pending does for programs: kill of a soup: task sets a flag,
  the interpreter checks it at statement boundaries and in sleep/input,
  and soupyc_child's own release_ctx unwinds. Test: a script writes 24 MB,
  prints CLOSING, closes; kill right after CLOSING; the shell answers in
  10 s and the file is 24 MB.
- **R-K34 (medium, rights, my v0.60.290) — DONE v0.60.300 (killclose-test).** A killed cook's file is
  created as the headchef's: the flush runs in the headchef's shell task,
  so creator_now() and may_use_reserve() give uid 0 (perms said owner:
  headchef for /home/saucier/own.txt). R-K33's deferred kill fixes it;
  test: owner: saucier after the kill.
- **R-K35 (medium-low, my v0.60.292) — DONE v0.60.306 (appendshare-test).** The append load (vfs.c:466) goes
  through mem_ensure, so the per-cook check counts the old contents: with
  1 MB free a cook cannot >> to their own 600 KB log, a plain "cannot
  open" and nothing logged. Fix: count the append load only beyond the
  old size (or not against the share); log the share line on a refusal.
- **R-K36 (low).** doom at: see R-D43 and R-D44 (one fix): the spawn
  stays pending after a quit at the menu; no range check (doom at 30000
  30000 0 is the void; past 8388607 the << 8 overflows; a bare - is 0;
  trailing junk ignored).

### neosim (reviewer confirmed N53-N55 in QEMU on copies, against nvim)

- **R-N53 (medium, data, my v0.60.296) — DONE v0.60.304 (neosim-fullpaste-test).** o/O check room for one line; a
  counted o repeats insert_key('\n') at Esc and a failed split is silent,
  so on a 2046-line file G3ofoo<Esc>:w gives foo and foofoo (nvim: three
  foo). Enter on a full buffer fails silently too. Fix: text_room_for(cnt)
  before change_begin, and say the file is full when a split fails.
- **R-N54 (medium, data) — DONE v0.60.305 (neosim-widepaste-test).** A charwise paste past 255 drops the rest
  silently (insert_text skips text_insert_char's -1): 200 a, y$$p:w writes
  255 (nvim 400). R-N50 counted lines, never width. Fix: work out the
  widest line the paste makes before change_begin and refuse with E1240
  as joins do.
- **R-N55 (low-medium).** gu/gU/g~, ~ and :sort begin an undo step though
  nothing changes: guu on abc then :q says E37 (nvim: not modified). Fix:
  change_begin only when something will change; :sort discards the step
  when nothing moved. (shift_lines is right: nvim marks << modified.)
- **R-N56 (low).** Visual mode sets cur_change before op_apply, so a
  refused visual d or c becomes what . repeats. Fix: record it only once
  op_apply began a change.

### Doom (reviewer checked against r_main.c's tables in Python, one QEMU run)

- **R-D41 (low, test).** doom-sky-test passes a sky shifted by -11 to +9
  columns (more than a whole pl_angle step). Fix: nearest palette index,
  an exact match (or one column either side) on over 98%, and a shift of
  2 must score clearly worse.
- **R-D42 (low, looks).** g_sky_xu is up to 2 columns off Doom's
  xtoviewangle at 320 wide (168 of 320 differ; 1 at 960): the pixel middle
  and the arctangent approximation, where Doom takes viewangletox's
  rounding. Fix: build the table as Doom does, from a fine tangent.
- **R-D43 (low).** g_at_set is cleared only by level_load: quit at the
  menu and the next plain doom spawns there. Clear it when doom_menu_run
  returns.
- **R-D44 (low).** doom at checks nothing: clamp X and Y to int16, refuse
  a lone - or trailing junk, and a point outside the map's sectors.

### Tests, build and docs (reviewer reverted fixes in a copy; two tests passed)

- **R-T39 (medium, my v0.60.296/298) — DONE v0.60.301.** neosim-fullpaste-test and
  neosim-longjoin-test end with :q, so nothing is written and their cmp
  compares the file with itself: a refusal that changes the buffer without
  marking it passes. Fix: after the :q check, a second neosim (or :e!)
  writes the buffer, or the cmp lines go.
- **R-T37 (medium) — DONE v0.60.302.** writeshare-test never frees and has one cook: with
  mem_drop's two share decrements and the three-quarters cap deleted it
  passed. Fix: kill the writer, strain the file, saucier redirects again
  and it reaches the disk; a second cook's writer alongside, the two
  stopped near three quarters together.
- **R-T38 (low-medium) — DONE v0.60.303.** neosim-longjoin-test never pastes after a
  refusal: with the check moved back after reg_take_chars it passed. Fix:
  yl first, then the refused d or c, then p and :w: the file holds that
  character.
- **R-T40 (low).** writebudget-test and writeshare-test compare with
  mcopy ::/HELLO.TXT unchecked: two empty reads match. Fix: -n on it.
- **R-T41 (low, flaky toward FAIL).** fullbudget-test's WAIT:12 before
  the kill; fix: UNTIL:/fill: stops at.

## The order to work the review in

0. Item 0 first (the user asked for it directly: a text file's double
   click opens neosim).
1. The test harness (R-T1, R-T2, R-T3, R-T4, R-T5, R-T6), so the fixes
   after it are measured by tests that can fail.
2. Security: R-K1, R-K2, R-C1, then R-T19 and R-T20 (the tests of
   K2 and K1, which can pass broken), R-K12 (critical), R-C2, R-K13,
   R-C10, R-K14, R-C12 (the area review's, 2026-10-12), then R-C16
   (the second area review's).
2a. R-K21 first (critical, the second full review's); then K24 with the kernel crashes (K22 and K23 set aside for the user).
2c. R-T28 and R-T29 (my tests that cannot fail) right after R-K21.
2b. R-T24 and R-T25 with R-K20 (tests that cannot fail, the second area review's).
3. Kernel crashes any cook can cause: R-K15 (my K3, incomplete), R-K16, then R-K17 (security), R-K4b (R-K5 set aside for the user).
3b. R-K29 with R-T32, R-K31, R-K30 (my K24's regressions) before the rest of data loss.
4. Data loss: R-M1, R-M4, R-N3, R-N30 (my N3, incomplete), R-N4, R-N5, R-N1, R-N2, R-N31, then R-N46, R-N47 (my N4's regressions), R-N48, R-N49, R-N32, R-N50.
4b. The area review at 3c49bd7 (my own regressions first): R-K33 with R-K34, R-T39, R-T37, R-T38, R-N53, R-N54, R-K35; then back to item 0b at R-D21. Its lows (R-N55, R-N56, R-D41, R-D42, R-D43 with R-D44 and R-K36, R-T40, R-T41) go with the rest by severity.
5. Item 0b, Doom: R-D20, R-D21, R-D22 (what the user sees first), then R-D3, R-D2, R-D4, R-D1 and the rest by severity.
6. The rest by severity, the area's mediums before the lows; items 1-9
   where they fit.
7. Items 10 and 11 (the user's ideas, 2026-10-12) after the review;
   11 is a long one, its stages one commit each.
8. Items 12-18 between Roux's stages, smallest first (popcorn,
   noodle, layercake); 18 waits on 11a.
9. Items 19-21 with them; 21 waits on 11a, 20 is measured first.

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
