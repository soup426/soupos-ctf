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
18. **A review every 10 commits** (the user, 2026-10-12): once 10 commits that
    change more than this queue file have landed since the last review, stop
    taking items and review the whole project: six reviewers in parallel, one
    an area (kernel and shell; the desktop; Mincecraft; neosim and rx.c;
    Doom; tests, build and docs), read-only, each claim checked before it is
    queued. Their findings go into this queue's review section, most severe
    first, and are worked from the top. Count with
    `git log --oneline LAST..HEAD -- . ':!docs/NIGHT-QUEUE.md' ':!docs/queue-archive-*' | wc -l`.
    **Last review began at 7647649** (2026-10-12); move this line to the
    commit each new review begins at.

---

## 0. A text file's double click opens neosim (the user's call, 2026-10-12)

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

## Review findings (2026-10-12)

(Added as the six reviewers report: kernel and shell; the desktop;
Mincecraft; neosim and rx.c; Doom; tests, build and docs.)

### Mincecraft (reviewer reproduced 1-3 on the host with the game's own C)

- **R-M1 (high).** Flying has no ceiling (player.c fly branch); a save with
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
- **R-M4 (medium).** A failed save over an existing world destroys it: fat
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
