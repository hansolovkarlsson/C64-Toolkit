# Changelog

Notable changes to c64asm, newest first. This project doesn't use
version numbers or dated releases, so entries are grouped by feature
rather than by version — each one names what changed and points at
where it's documented in full (`c64asm-reference.md`, `README.md`, or
`lib-reference.md`).

Every entry below shipped identically across all three implementations
(the Python reference `c64asm.py`, the single-file C `c64asm.c`, and
the 14-module split-source C in `c64asm-split-src.zip`) and passed
this project's full regression suite before being marked done — that
discipline is this project's own standing practice, not something
worth repeating in every entry below.

## `maze.asm`: stage 6 (power pellet vulnerability)

Power pellets do something now: eating one starts a timed
vulnerability window where both enemies flee, turn a shared
frightened color, and move at half their own already-throttled
speed; touching a frightened enemy eats it (bonus score, resets just
that one enemy to its own start tile) instead of the player getting
caught. Touching a normal enemy still catches the player exactly as
before. Eating a second pellet mid-window refreshes the full duration
rather than stacking past it or being ignored.

Fleeing reuses the existing chase framework rather than needing new
logic: decide_enemy_direction and consider_enemy_candidate already
compute Manhattan distance to the player for every open neighboring
tile; a single flag (enemy_frightened) now decides whether the
*closest* one wins (chasing) or the *farthest* one does (fleeing),
the same distance calculation compared in the opposite direction. One
real edge case surfaced while designing this and was fixed before it
ever shipped, not found by testing after the fact: a distance
sentinel chosen to always lose when minimizing (a very large number)
would incorrectly reject a genuinely valid distance-0 candidate when
maximizing instead, since 0 isn't greater than a sentinel of 0.
Replaced with an explicit "found anything yet" flag instead of
relying on any specific starting value's own meaning surviving a
flipped comparison direction.

Built with one small extension point for whatever pellet types come
later: `activate_pellet_effect` is its own separate routine (currently
just calling `start_enemy_vulnerability`), not inlined into
`check_eat_dot`'s own pellet-eating logic — a future pellet type (a
different tile value from `TILE_PELLET`, dispatched to a different
effect routine here) won't need that existing logic restructured to
make room for it.

Testing this needed real care around two mini6502.py-specific timing
behaviors, both traced to their actual cause rather than patched
around blindly:

- Each frame here turns out to be only ~350-1500 instructions, far
  fewer than several existing tests already assumed when picking
  round-number instruction budgets like 200,000 for "let this
  happen." Against a 400-frame vulnerability window, a 200,000-
  instruction budget intended to mean "right after eating this
  pellet" could actually span 500+ frames -- long enough for the
  entire window to start *and fully expire* before the check ever
  ran, reading back a timer of exactly 0 and looking like the pellet
  was never eaten at all when it plainly had been (confirmed
  directly: the pellet was gone from the grid, just the window had
  already run its full course). Every new test that needs to check
  state right after a specific single frame now uses this project's
  own established step-to-loop-top technique for exactly one
  iteration, not a large fixed instruction count sized for a
  different, coarser purpose.
- mini6502.py doesn't simulate the VIC-II's own real behavior of
  clearing SPRITE_SPRITE_COLLISION the moment it's read -- a poked
  test value simply stays there across every subsequent frame until
  something explicitly clears it. For collision responses whose
  *own* effect changes what a repeated read of that same stale value
  means (eating a frightened enemy clears its own frightened flag, so
  a second frame reprocessing the same "collision" would treat the
  now-normal enemy as dangerous and incorrectly reset the player
  instead) this needed the collision register cleared by hand right
  after the one frame that's supposed to see it, matching what real
  hardware would have already done automatically. Existing collision
  tests from stage 5 happened not to need this: their own responses
  are idempotent (repeatedly "catching" the player already at its own
  start tile changes nothing further), which is exactly why this
  gap in mini6502.py's own fidelity never surfaced there.

A third, pre-existing test (from stage 2, well before enemies existed)
also needed a real update, not a workaround: it pre-poked
SPRITE_X_MSB's own bits 1-7 as "pretend other sprites" and expected
them frozen across a long run -- valid when nothing else used that
register, but bits 1 and 2 are real, actively-managed enemy sprites as
of stage 5, and correctly change based on their own real position now.
Poking the register there also needed moving to a clean loop boundary
first, the same reasoning already established for every other mid-run
poke in this file: landing mid-instruction risks the poke being
silently overwritten by an already in-flight read that captured the
old value first. Fixed to only check the genuinely still-unused bits
(3-7), landing the poke correctly, and confirmed this reflects a real
behavior change (real sprites managing real bits) rather than
papering over an actual regression.

## `maze.asm`: enemies slowed down (75% of the player's own speed)

Reported directly after playtesting stage 5's own chase AI: the
enemies felt a bit too fast at a straight 1:1 frame rate. Rather than
shrinking MOVE_SPEED itself (which would need sub-pixel/fractional
position tracking to express a speed between whole pixels per frame),
update_enemies now throttles how *often* a full MOVE_SPEED step
happens instead: a new frame counter, and two named, easy-to-retune
constants (ENEMY_THROTTLE_MASK/ENEMY_THROTTLE_SKIP_FRAME) saying "skip
movement on 1 out of every (MASK+1) frames." Only the actual AI/
movement step is skipped on a throttled frame -- sprite-position
update still runs every frame regardless, so whatever position an
enemy already has stays correctly drawn even when it didn't move that
frame.

Verified by stepping frame by frame along a fully-cleared,
unobstructed corridor and checking the *exact* per-frame movement
pattern, not an average over many frames that a partially-blocked
path could distort into looking approximately right for the wrong
reason. The first version of that test assumed a fixed starting
phase for the 4-frame skip cycle (checking for the literal sequence
1,1,1,0 from the very first sample) -- caught immediately when it
failed against a real, correctly-offset run (1,0,1,1,1,0,...), since
the throttle counter's own starting phase depends on exactly how many
frames already ran during setup, not guaranteed to align with any
particular sample window. Fixed to check the underlying cycle instead
(skips spaced exactly 4 apart), which is what actually matters and
holds regardless of phase. A second, separate test confirms the
player's own movement is completely unaffected by any of this --
still a full pixel every single frame.

## `maze.asm`: stage 5 (enemy AI)

Two ghost-shaped enemies now chase the player, using VIC-II sprites 1
and 2 alongside the player's own sprite 0. Deliberately not just the
player's own circle recolored: two visually identical chasers would
read as far less alive than something actually shaped like an
adversary, so this stage's own sprite is a distinct silhouette
(rounded top, scalloped bottom), anchored at the sprite's own (0,0)
origin the same careful way the player's own sprite already learned
it has to be.

The chase logic itself: at each tile-aligned point, an enemy checks
its own open neighboring tiles (walls ruled out via can_enemy_move_
direction, a careful mirror of the existing player-only can_move_
direction reading the enemy's own position instead -- kept as a
separate routine specifically to avoid any risk of disturbing that
already-proven one) and picks whichever is closest to the player's
own current tile by Manhattan distance (`|dcol|+|drow|`) rather than
true Euclidean distance -- no multiplication or square root needed on
a CPU that has neither built in, just subtraction and a sign flip.
Won't reverse its own current direction unless every other option is
genuinely blocked (a dead end), the same reasoning the player's own
existing turn logic already established for a different problem
(flickering indecision) applying here too.

Both enemies share a single set of AI/movement/sprite-position code,
rather than two independent copies that could quietly drift apart
over time: a "current enemy" working set gets loaded from whichever
enemy's own persistent state before the shared routines run, and
written back to it afterward. update_enemy_sprite_position itself
generalizes the player's own update_sprite_position to target any
sprite via indexed addressing off SPRITE0_X/SPRITE0_Y (the VIC-II's
own regular per-sprite register spacing) and a small lookup table for
the correct single bit of SPRITE_X_MSB, rather than the one hardcoded
bit the player-only version can afford to use directly.

Player-enemy contact uses the VIC-II's own hardware sprite-collision
register ($D01E) for pixel-accurate detection -- verified against
several independent sources given how significant getting a hardware
register's own address wrong would be, the same discipline this
file's own $01/CHAREN and CIA2_PRA work already established. On a
hit, the player resets to its own starting tile by simply calling
init_sprite again, rather than a second, parallel "reset position"
routine that could drift out of sync with it -- no lives or game-over
system yet, a deliberate, explicit scope decision, not an oversight.

Worth naming directly: mini6502.py is a CPU-level simulator and
doesn't model the VIC-II's own pixel-level sprite rendering or
collision detection at all. check_enemy_collision's own *response* to
a collision signal is fully tested (poking SPRITE_SPRITE_COLLISION
directly, including a dedicated check that two enemies colliding with
each other, without the player, correctly does nothing) -- but
whether two sprites' own visible pixels genuinely overlap on real
hardware is a claim this project's own test suite can't make, the
same category of limitation already documented for the character-ROM
borrowing (CHAREN) work. Worth confirming directly on real hardware
or VICE.

Several bugs surfaced and were fixed during this stage's own testing,
none in the underlying AI or collision logic itself: an existing
test's own expectation (`SPRITE_ENABLE == 0x01`) needed updating once
enemies legitimately enabled two more sprite bits alongside the
player's own; a synthetic dead-end test's own first version
accidentally created a *second*, unintended dead end by not
controlling every tile surrounding its own intended one; and new
setup-verification tests needed the same "step to main_loop's own
address from the true start, not after a fixed instruction budget
already ran past it" fix this project's own stage 3 work already
established for exactly this class of mistake -- calling it after,
rather than instead of, a fixed-budget run continues from wherever
execution already was, past the very first arrival this kind of check
actually needs.

## Fixed: `maze.asm`'s level load filling the screen with one tile

Reported directly from real hardware: the level file loaded (the disk
audibly spun, and the score display -- which only ever runs after a
successful load -- showed correctly), but the whole maze rendered as
a single repeated character instead of the actual level layout.

Genuinely couldn't be reproduced in simulation despite trying --
extended level-loading tests, deliberately different level data,
truncated files, all passed cleanly against `mini6502.py`. Rather
than keep guessing blindly at the cause with no way to confirm which
guess was right, built a dedicated diagnostic tool first: `hexdump.asm`
dumps `LEVEL1`'s own raw bytes from disk, unfiltered, plus the exact
total byte count read all the way to EOF -- reusing `dir_raw.asm`'s
own already-established pattern for exactly this kind of problem
(built for the same reason: a program's own *interpretation* of file
data can't show whether the data itself was ever actually correct).

That tool's own real value here: it can distinguish a file that's
missing, a file that's short, and a file that's correct, from each
other, in a way `maze.asm`'s own single red-border error signal
never could -- and it was the total byte count specifically that was
going to answer the actual question, whichever one it turned out to
be.

Independent of whatever that count turns out to show, the underlying
robustness gap in `load_level` was real and worth fixing either way:
`READST` was checked once, before the read loop started, and never
again during the 836 bytes of tile data (or the 20 bytes of title, or
even the player's own starting position) that follow. A file that
runs out early for any reason -- however it actually got onto the
disk -- would leave everything past that point as whatever `CHRIN`
happens to keep returning once nothing legitimate is left, silently,
with no signal anything went wrong. That's exactly consistent with a
screen full of one repeated tile: the same byte, over and over, is
what a KERNAL post-EOF `CHRIN` return being read into `MAZE_GRID`
many times over would produce. Fixed by checking `READST` before
*every* `CHRIN` in `load_level`, matching `dir_raw.asm`'s own
established, correct ordering exactly -- checked before, not after,
since checking after would incorrectly flag a correct file's own
final, valid byte as an error (`READST`'s own EOF bit is already set
by the time `CHRIN` returns that last legitimate byte).

A new test proves the fix directly: a level file deliberately cut off
partway through its own tile data now correctly triggers the same
red-border error a missing file already did, and confirms `MAZE_GRID`
was never written past the point the file actually ran out -- still
zero there, not leftover repeated-byte garbage.

`hexdump.asm` ships as its own tool, not folded into `maze.asm`,
since it's a diagnostic for verifying data on disk, not something the
game itself needs at runtime -- the same reasoning `dir_raw.asm`
already established for this project.

## `maze.asm`: stage 4 (level data loaded from disk)

The actual point of building this game on top of this project's own,
by now thoroughly tested disk I/O rather than another standalone
demo: the maze grid, the player's own starting tile, and the level's
own title now load from a real file on disk ("LEVEL1") at startup,
rather than being assembled directly into this program's own bytes
the way stages 1-3 shipped with first. A different level is now a
different 858-byte data file, not a reassembled program -- `LEVEL1.dat`
ships alongside `maze.prg` as this game's own first level, needing
only a rename to the plain filename `LEVEL1` once copied onto the
same C64 disk.

`load_level` reuses this project's own already-tested disk-read
pattern directly -- the same `SETLFS`/`SETNAM`/`OPEN`/`CHKIN` sequence
`editor.asm`'s own `do_load` already uses -- with one real difference:
this expects an exact-size binary file and reads precisely that many
bytes via `CHRIN`, rather than a variable-length text document padded
with spaces past EOF the way `editor.asm`'s own `read_file_to_screen`
does. If the file can't be opened at all, there's no safe way to
proceed -- `MAZE_GRID` would be left entirely uninitialized -- so this
turns the border red and halts rather than silently continuing with a
maze that was never actually loaded; this stage doesn't yet have any
of its own text rendering to show a real error message instead.

The player's own starting position stopped being a compile-time
constant as a direct consequence: `init_sprite` now computes the
player's own starting pixel position from `player_start_col`/
`player_start_row` at runtime instead, the same 16-bit-vs-8-bit split
`player_rel_x`/`player_rel_y` themselves already needed (a genuine
16-bit shift for X, since `player_start_col` can reach 37 and 37*8
doesn't fit in a single byte; a plain 8-bit one for Y, which never
needed a second byte here regardless of maze height). The level's own
title loads into RAM too, ready for a later stage to actually display
it (a level-select screen, say) -- this stage doesn't render it
anywhere itself yet, a deliberate scope decision, not an oversight.

Verifying the format actually generalizes, not just reloads the same
bytes from a new location, needed a second, genuinely different level
built specifically for one test: a different start tile, a different
title, and an actually-altered layout (two tiles deliberately changed
from dots to power pellets) -- confirmed the loader picks up every one
of those differences correctly, not just the ones the original level
happened to already have right.

Two of this project's own past testing lessons paid off directly
while building this: a test forgetting to set `joystick2 = 0` before
checking loaded grid state (this project's own established, recurring
category of test-harness mistake, not a new one) briefly produced a
spurious failure -- caught and fixed the same way each previous
instance was, by tracing to the actual cause rather than adjusting
the assertion. And `test_maze_data` (836 bytes of now-obsolete
assembled-in test maze bytes) came out of the program entirely once
nothing referenced it anymore, shrinking `maze.prg` from 2539 to 1808
bytes as a direct, visible result of the format switch actually
working.

## Fixed: `maze.asm` leaving stray characters in the maze's own margin

Reported directly from real hardware, with a screenshot: after
fixing the "SCORE"/"RBNQD" bug, a second, different artifact
remained -- letter and digit shapes stacked vertically in the
single-column margin left of the maze. Static from the very first
frame, present before any key was pressed, and unchanged for the
entire time the game was played.

That description -- fixed, present from startup, never changing --
pointed away from anything the running game loop does (already
checked at length: an extended, naturalistic simulation moving the
player through dozens of tiles and eating many dots found nothing
wrong anywhere on screen) and toward something the game simply never
touches in the first place. `maze.asm` never explicitly cleared the
screen at startup -- `render_maze` and `render_score` only ever write
to the specific cells they actually care about (the maze's own
bounds, and the HUD row), leaving every other screen position exactly
as it already was. On real hardware, "already was" means whatever
was on screen from actually typing `LOAD"MAZE",8` and `RUN` at a BASIC
prompt to start the program -- residual command text, left showing in
any cell this program's own code was never going to overwrite, like
the margin column. Fixed with a straightforward, direct clear of the
whole screen (4 pages of 256 bytes, covering all 1000 screen cells
and a few bytes past them harmlessly) as the very first thing `start:`
does, before anything else runs.

Worth naming why this one specific bug was invisible to every test
run against it before a real screenshot showed up: `mini6502.py`
always starts a test from an already-blank screen, the same way a
freshly reset emulator or a from-scratch test harness naturally would
-- there's never anything already on screen for a missing screen-clear
to fail to remove. The new regression test guarding against this
seeds the screen with realistic pseudo-random "residual" content
first, matching what a real BASIC prompt leaves, specifically so a
future regression here would actually be caught rather than silently
passing against a screen that was already blank to begin with.

The fix itself needed one correction along the way, caught by this
project's own test suite immediately rather than shipped: the first
version cleared the screen to the standard PETSCII space code ($20)
rather than `TILE_EMPTY` (0) -- but this program's own custom
character set only has bitmaps defined for codes 0-18. Character code
$20 has no defined bitmap here at all, so that first version would
have replaced one flavor of undefined-character-memory garbage with
another, not actually fixed anything. Caught because the existing
"nothing rendered below the maze" test checks for `TILE_EMPTY`
specifically, not blankness in general -- fixed to use the actual,
correctly-defined blank tile instead.

## Fixed: `maze.asm`'s score HUD reading "RBNQD" instead of "SCORE"

Reported directly after playing it: the score counted up correctly,
but the label above it read "RBNQD," and some garbled-looking
characters showed up in the maze's own left margin.

The label bug had an exact, findable cause: `init_text_characters`
computed each letter's own character-ROM source address from its
alphabet position directly -- A=0, B=1, C=2, and so on -- but that's
wrong. Screen code 0 is `@`, not `A`; the letters actually start at 1.
Confirmed against multiple independent sources (a directly labeled
byte dump of the character ROM, and an explicit statement from a C64
technical forum, not just one) before touching anything, given how
wrong a plausible-looking but incorrect assumption here already
proved to be. Every one of the five borrowed letters was reading from
exactly one screen code too low as a direct result -- S from R's own
slot, C from B's, and so on -- which is exactly "RBNQD": each letter
individually correct, just one position off. Digits were never
affected -- PETSCII 48-57 (`0`-`9`) maps to screen code unchanged,
a different rule for a different block of the character set,
confirmed separately rather than assumed safe by association with the
letter bug.

The most direct verification available for this class of bug: a new
test pokes the character ROM's own real, correct glyph bytes for S,
C, O, R, and E (not synthetic test patterns) and confirms the copied
result is the exact, correct letter shape -- not just that bytes
moved somewhere, but that they spell what they're supposed to. Two
addresses among the five needed excluding from that check for a
reason worth naming again: `mini6502.py` doesn't model CHAREN/`$01`
banking at all, so `$D012` and `$D018` (`VIC_RASTER` and
`VIC_MEMPTRS`) are always treated as their own VIC-II registers
regardless of this program's own CHAREN bit, and get overwritten by
this same program's own later setup code before the test can read
them back. Verified those two directly against the assembled listing
instead, the same approach this project's own earlier work on this
routine already established.

The left-margin report couldn't be reproduced in simulation: a full
sweep of the maze's own left margin column and the entire HUD row,
checked directly against every value actually expected there, found
nothing wrong in either place. Given how precisely the letter bug
matched what was described, and that nothing else turned up despite
looking specifically for it, the most likely explanation is that both
reports describe the same underlying mistake, now fixed -- worth
confirming on real hardware or in VICE now that this fix is in place,
since that's the one thing simulation alone couldn't settle either way
here.

## `maze.asm`: stage 3 (dot collection and scoring)

The first stage to give this game an actual objective: eating a dot
or power pellet clears it (in both `MAZE_GRID` and on screen) and adds
to a real, displayed score -- 10 points for a dot, 50 for a pellet,
shown as 5 decimal digits at the top of the screen, the row stage 1
reserved for exactly this from the start.

Displaying that score turned out to be the larger part of this stage,
and a genuinely new technique for this project: this program's own
custom character set (stage 1's own reason for existing) has nothing
in it but the four tile graphics, since redefining `VIC_MEMPTRS` means
the KERNAL's own font isn't visible to the VIC-II at any character
code anymore, not just the ones this program happens to reuse.
`init_text_characters` borrows digit and letter glyphs from the
character ROM at startup instead -- the standard technique of clearing
CHAREN (bit 2 of `$01`) to expose the ROM to the CPU at `$D000-$DFFF`
in place of I/O, copying what's needed, and restoring it -- confirmed
against several independent sources first, given how significant
getting this specific register wrong would be. Interrupts are disabled
for the whole copy window, not as a general precaution but because the
KERNAL's own IRQ handler (firing roughly 60 times a second for the
jiffy clock and keyboard scan) lives in exactly the memory this
temporarily replaces, and would crash or misbehave if it fired
mid-copy.

Verifying this leaned on a mix of static and dynamic checks, worth
naming since it's a new pattern: `mini6502.py` doesn't model `$01`/
CHAREN banking at all, so it always treats specific addresses within
`$D000-$DFFF` (`VIC_RASTER`, `VIC_BORDER`, `VIC_BG0`) as their own
VIC-II registers regardless of what this program's own CHAREN bit
says -- three of the fifteen glyphs this stage borrows happen to have
ROM source bytes at exactly those addresses, and can't be verified by
poking a test pattern and reading it back the way the other twelve
can. Verified those three directly against the assembled listing
instead, confirming the exact source and destination address in the
actual machine code, and confirmed the source/destination split
between the two check styles was based on how the code is genuinely
structured (digits 0-9 copy as a single contiguous 80-byte run, not
ten separate instructions -- an early version of this same listing
check wrongly assumed otherwise, checking for ten addresses that were
never going to individually appear).

Two correct, existing tests needed real updates, not workarounds:
adding a fourth call (`check_eat_dot`) to `main_loop`'s own per-frame
work meant a couple of stage-2 tests that poked player state
immediately after a fixed instruction budget could now land mid-
iteration, since each iteration takes measurably longer than before --
fixed by explicitly stepping to a clean loop boundary before poking,
not just sampling afterward. And two stage-1 tests needed to account
for a real, correct consequence of this stage's own new behavior: the
player's own start tile is itself a dot, and it's eaten within the
first loop iteration -- not left as a kind of implicit safe zone.

## Fixed: `maze.asm`'s player hit-box visibly shifted right and down

Reported directly after playing it: the player appeared to hit the
left and top walls with a visible gap still showing, while appearing
to overlap into the right and bottom walls (and into the maze's own
internal wall blocks) before actually stopping.

The collision system itself was never wrong -- `can_move_direction`
and `update_sprite_position` both correctly used the sprite's own real
(X,Y) coordinate the whole time. What was wrong was the player sprite
itself: its visible shape (a small circle, added specifically to fix
an earlier, different sizing problem) was centered in the middle of
the sprite's full 24x21 pixel canvas, rather than anchored at the
sprite's own (0,0) origin -- the same origin every collision check and
position update actually uses. Computed directly, not assumed: the
earlier bitmap's own visible pixels sat 6-7 pixels away from that
origin, on an 8x8 tile -- most of a whole tile's worth of visual
offset between where the collision boundary actually was and where
the circle appeared to be. Redrawn so the visible shape's own bounding
box starts exactly at (0,0), the same coordinate the rest of the game
already treats as "the player."

A new, permanent regression test computes the visible sprite's own
bounding box directly from its bitmap data and checks it starts at
(0,0) and stays within one tile's own bounds -- confirmed this
actually catches the class of bug it's meant to by running it against
the previous, unfixed bitmap first (it fails, reporting the same
(6,5) offset computed above) before confirming it passes against the
fix.

## `maze.asm`: the maze now fills nearly the whole screen

More direct feedback after trying the previous, already-larger maze:
even at 28x20 tiles, it still only covered about three-quarters of the
screen. That size wasn't arbitrary -- it was deliberately capped
specifically to keep the player's own absolute sprite X under 256, to
avoid a real second piece of work (`SPRITE_X_MSB` handling) that a
wider maze would require. This entry is that piece of work, done
properly rather than avoided: the maze grew to 38x22 tiles (1-column
margins on each side, using the full 40-column width exactly), and the
player's own X position became a genuine 16-bit value with real
`SPRITE_X_MSB` handling -- the same technique `bounce.asm`'s own wider
bounce area already uses -- since the maze's own width alone now
means the player's maximum possible absolute sprite X reaches 335.

This touched most of the player's own movement code, not just the
final sprite-position write: `update_player`'s tile-alignment check
(only the low byte's own low 3 bits ever mattered, unaffected by
adding a second byte), `can_move_direction`'s tile-column computation
(a genuine 16-bit-to-8-bit right shift on a scratch copy, not the
player's own actual position), the movement step itself (16-bit
increment/decrement with correct carry/borrow into the high byte), and
`update_sprite_position` (a genuine 16-bit addition, splitting the
result into `SPRITE0_X`'s own low byte and whichever single bit of
`SPRITE_X_MSB` belongs to sprite 0, without disturbing the other 7
sprites' own MSB bits sharing that same register). `compute_row_offset`
also needed a new decomposition for the new `TILE_COLS` (32+4+2 rather
than the previous maze's own 16+8+4).

Caught one real mistake before it ever reached testing, not after:
an early version of the MSB-clearing code used `and #(~PLAYER_SPRITE_
X_MSB_BIT) & $ff`, assuming bitwise NOT was available -- checked
`c64asm-reference.md`'s own expression table first, rather than
assuming, and confirmed this assembler has no bitwise operators at all
(only `+ - * /` and unary `< >`). Fixed with a plain, hardcoded
inverted mask before ever trying to assemble it.

Verifying the MSB crossing specifically needed one more new pattern
worth naming: sampling `SPRITE0_X`/`SPRITE_X_MSB` at a fixed
instruction-count interval (an earlier version of this same test) can
land mid-iteration, in between `update_player` moving the player's own
position and `update_sprite_position` catching up to that same new
value within the same loop pass -- producing several small, confusing,
entirely spurious mismatches that had nothing to do with the actual
code. Fixed by sampling only when the program counter returns to
`main_loop`'s own address (extracted from the assembled listing, not
hardcoded), the one point in the whole loop guaranteed to sit strictly
between iterations with both routines already finished.

## `maze.asm`: a bigger maze, and a player sprite actually sized to fit it

Prompted by direct feedback after trying the game: the 20x10 test maze
only filled a small corner of the screen, and the player sprite --
drawn edge-to-edge across its own full 24x21 pixel canvas -- was
roughly three tiles wide next to the 8x8 corridors it was actually
navigating. Neither had been deliberately tuned; both were simply
whatever stage 1 and 2 shipped with first.

The maze grew to 28x20 tiles (up from 20x10, roughly 3.7x the area),
sized specifically to stay just under the sprite-X=256 threshold --
checked precisely before picking dimensions, not guessed at -- so the
player never needs the second, high-bit X handling `bounce.asm`'s own
wider bounce area does (`SPRITE_X_MSB`). That constraint pushed the
maze's own left margin to a single column rather than centering it
symmetrically; the resulting free space on the right is a reasonable
place for a HUD (score/lives) a later stage could add. The player
sprite itself is now drawn as a much smaller shape (~12 pixels across)
well inside its own 24x21 canvas, rather than filling it -- reading as
roughly proportionate to one tile instead of three.

Growing the maze surfaced two real bugs, both caught by actually
running the result rather than trusting "assembles cleanly":

- `init_maze_grid`'s own copy loop used an 8-bit `X` register to count
  up to `TILE_COLS*TILE_ROWS` (560 bytes for this maze). An 8-bit
  counter can't reach 560 -- and doesn't fail loudly when asked to:
  `cpx` against a value above 255 simply never matches, so `X` wrapped
  through 0-255 repeatedly instead of ever completing, silently
  leaving everything past roughly the first 255 bytes of `MAZE_GRID`
  as whatever RAM already held. Fixed with a proper pointer-based copy
  and a genuine 16-bit byte counter.
- `get_tile`'s own address computation (`MAZE_GRID + row*TILE_COLS +
  col`) needed the same fix for the same reason: `row*TILE_COLS`
  reaches 532 at the maze's own last row, which doesn't fit in a
  single byte the smaller 20x10 maze's own addressing scheme relied
  on. Rewritten with a genuine 16-bit multiply (`compute_row_offset`,
  the same kind of bit-shift decomposition this project's other
  16-bit-offset routines already use) and a 16-bit pointer, rather
  than an 8-bit index into `MAZE_GRID`.

Verifying the new layout leaned on two things worth naming since
they're new patterns for this project: the maze's own connectivity
(every open tile reachable from every other) was checked by flood fill
*before* ever assembling it, not discovered by playing it -- a maze
generation bug that isolated one region from another would otherwise
be the kind of thing that's a lot more annoying to notice by hand.
And rather than hand-transcribing expected collision-stop coordinates
into the test suite, `test_maze.py` now regenerates the exact same
reference layout `maze.asm`'s own test data comes from and computes
where the player should stop moving directly from that reference,
independent of the game's own logic -- catching two real test-design
mistakes of exactly the kind that discipline is meant to prevent (see
below).

Two mistakes came from the test suite itself, not the game, both worth
naming plainly rather than glossing over: an early version of a
`run_fixed_budget` test helper set `joystick2 = 0` unconditionally
inside itself "for safety," which silently overwrote a value a test
had deliberately set immediately beforehand, wanting it to apply for
the whole run -- several tests briefly, incorrectly appeared to show
the player not moving at all as a result. And one test's own
assumption that "left" would be open from the player's start tile was
simply wrong -- that tile sits flush against a wall block's own right
edge -- caught by the reference-grid check above rather than shipped
as a false failure or, worse, quietly weakened to pass.

## `maze.asm`: WASD keyboard movement, alongside the joystick

Added so this game is playable without a physical joystick attached
(a real limitation of testing in VICE or on hardware without one, not
just a convenience) -- W/A/S/D via `keyboard.inc`'s own named,
verified constants, not hand-transcribed binary literals (that file's
own header comment documents a real bug an earlier version of this
project once shipped from exactly that: a hand-written column/row
pair that actually matched a different key than intended). Combined
with the joystick via OR into the same `joy_state` bitmask the rest of
`update_player` already works from, rather than replacing it -- either
input method works, interchangeably, and neither can mask the other.
Confirmed all four directions individually, that the joystick still
works exactly as it did before this change, and that both together
don't conflict.

## `maze.asm`: stage 2 (player movement and wall collision)

A real, moving player on top of stage 1's own tile rendering: a
hardware sprite (not a character-cell player -- smooth, pixel-by-pixel
movement independent of the tile grid underneath it, the same reason a
later stage's enemies will also be sprites), joystick input via this
project's own already-tested `read_joy2`, and wall collision checked
directly against the same maze grid stage 1's own rendering already
treats as the source of truth.

Movement is continuous, not one-tile-per-keypress, with turns only
committed when the player is exactly tile-aligned -- the classic
arcade convention this genre is built around, deliberately including
the part that's easy to leave out by accident: releasing the joystick
doesn't stop the player, it keeps moving in its current direction
until blocked by a wall, the same way the original arcade game
actually behaves (the joystick sets the *next* direction to turn, not
"move only while held"). The player's own position is tracked relative
to the maze's own top-left pixel rather than the whole screen's,
specifically so tile alignment stays a trivial low-3-bits check --
worth calling out since the maze's own screen offset isn't itself a
multiple of 8 on the Y axis, which would have broken a shortcut
version of that same check.

Two real bugs came up during this stage's own development, both
caught by actually running the result rather than trusting that
"assembles cleanly" was enough:

- A genuine logic inversion in `update_player`: `can_move_direction`
  returns with the zero flag set when a move is *blocked*, and several
  of the branches checking that result had `bne`/`beq` backwards,
  which meant the player could never move at all. Traced precisely
  (inspected `player_direction` and the joystick state mid-run rather
  than guessing) before fixing all four affected checks.
- A bug in the test harness itself, not the game: an early version of
  the test helper reset the whole program back to its own entry point
  on every call, which silently discarded manual pokes a test had set
  up for a specific scenario (a starting tile position, say) the
  moment that same helper ran again. Fixed by separating "start fresh"
  from "continue running" into two distinct helpers, now used
  correctly throughout `test_maze.py`.

Also worth naming since it shaped several tests along the way: this
project's own zero-page poisoning simulation (`simulate_zp_poisoning`)
doesn't simulate `$D012` (`VIC_RASTER`) at all -- it's ordinary memory
that never advances on its own, so `wait_frame`'s own raster poll
would spin forever inside `mini6502.py` unless that address is poked
once, up front, to the value it's waiting for. With it poked,
`wait_frame` always passes instantly, which is exactly what a test
wants -- many game-loop iterations within a fixed instruction budget,
not real-time throttling that has no meaning inside a CPU-only
simulation.

## `maze.asm`: a new game, stage 1 (tile rendering foundation)

The start of an original maze-chase game -- not a clone of anything
specific -- built as the next real stretch for this project now that
disk I/O is thoroughly tested: level data loaded from disk (a planned
later stage) is the actual point, and a tile-based maze grid is a
genuinely natural fit for that, unlike this project's earlier games,
whose state was never really shaped like a file. This first stage
lays the foundation everything else sits on: a custom VIC-II character
set for maze tiles, and a maze grid held in its own RAM (`MAZE_GRID`),
rendered onto the screen from that grid rather than drawn once and
left stale -- the same architectural lesson `editor.asm` already
learned the hard way from its own screen-is-the-document history,
applied here from the start instead of after the fact.

`hardware.inc` gained two things worth calling out on their own,
verified against multiple independent sources given how easy this
specific register is to get wrong: `CIA2_PRA`/`CIA2_DDRA` (VIC bank
selection), and a much fuller comment on `VIC_MEMPTRS` explaining its
exact bit layout. The bank-select register's other 6 bits are the
serial/IEC bus lines this project's own disk I/O depends on -- a
careless full-byte store there doesn't just risk picking the wrong VIC
bank, it can silently break loading a level from disk in exactly the
later stage this whole game exists to build toward. `hardware.inc` now
documents, and `maze.asm` uses, the careful read-modify-write pattern
instead. Character memory lives at `$3000`, not the more obvious
`$1000` -- VIC banks 0 and 2 both shadow that address with the
character ROM regardless of what's actually written to the RAM there,
a real, easy-to-lose-an-afternoon-to gotcha now spelled out directly
in `VIC_MEMPTRS`'s own comment rather than left to be rediscovered.
Confirmed the additions don't affect any of this project's other
programs that already `.include` this same file -- rebuilt all twelve
and compared byte-for-byte against what was already shipped.

Verification for a VIC-II/graphics-touching program looks a little
different from this project's other tests, worth noting since it's a
new pattern: `mini6502.py` is a CPU-level emulator, not a visual one,
so there's no "does it look right on screen" check available the way
there would be in VICE. What's actually verifiable, and what
determines correctness on real hardware regardless of whether a human
is looking at a screen: the exact register values written to
`CIA2_PRA`/`CIA2_DDRA`/`VIC_MEMPTRS`, the exact bitmap bytes landing in
character memory, and the exact tile values landing in both the maze
grid and rendered screen memory, checked row by row against the
intended test maze -- all confirmed directly.

## `editor.asm`: row number status, and HOME/CLR page up/down

Two additions, both aimed at getting around in the larger, scrollable
document more easily. Typing, RETURN, DEL, or any cursor key now shows
"ROW n" (1-based) on the status line -- DOC_ROWS is 200, so n is
always 1-3 digits, extracted the same way `print_decimal16` extracts
the (potentially larger) values it prints, via repeated subtraction,
just simpler here since there's no need for more than 3 digits.
Deliberately not shown after F-key operations, whose own result
messages ("SAVED.", "CANCELLED.", and so on) are more useful to leave
in place.

Page up/down uses HOME and CLR (SHIFT+HOME) -- not a cursor-key
combination, confirmed directly rather than assumed: CTRL+cursor
produces nothing at all in the keyboard buffer (the KERNAL doesn't
decode that combination for cursor keys), and both SHIFT+cursor and
C=+cursor produce the exact same code as the *opposite* plain cursor
key (SHIFT+down is indistinguishable from plain up), so neither can
mean anything new. HOME/CLR are a genuinely distinct, otherwise unused
key pair -- the same kind of repurposing RUN/STOP already gets for
"cancel" elsewhere in this file, not their usual KERNAL meaning. Each
page jumps the viewport a full 24 rows at once, capped at the same
[0, DOC_ROWS-24] range `try_scroll_down`/`try_scroll_up` already
enforce one row at a time.

A real priority conflict came up building the row status display,
caught by the test suite rather than shipped by accident: showing the
row number unconditionally after every keypress meant it immediately
overwrote `insert_char`'s "DOCUMENT FULL" message and `handle_return`'s
equivalent the instant either fired, since both are dispatched the
same way ordinary typing and RETURN are. Fixed by having those two
routines signal via the carry flag whether their own warning should be
left in place (carry set) or a row number is fine to show instead
(carry clear) -- the dispatcher in `main_loop` checks that flag before
deciding which one wins. Several existing tests had encoded the old
assumption that ordinary typing and RETURN leave the default help text
or nothing in particular on the status line -- updated to reflect what
the status line is actually showing now (a row number), not just
patched until green.

## Fixed: `editor.asm` corrupting the screen on real hardware after scrolling

Reported directly from real hardware, not caught by this project's own
testing first: reaching the bottom of the screen and pressing RETURN
produced "random characters and graphic symbols" on the lower half of
the screen -- the program kept running, not hanging, but with garbage
left behind.

Root cause: `copy_ptr`, reused throughout `DOC_BUF`'s own access
(scrolling's own feature, added recently) sat at `$F5`, inside `$F3-
$F6` -- a range this project's own past experience had already
identified as actively clobbered by the KERNAL's keyboard-scan IRQ on
real hardware (see `c64-memory-reference.md`'s zero-page notes). That
was a latent risk from the moment `copy_ptr` was first declared there,
but scrolling is what turned it into a real, reproducible bug: `copy_ptr`
is now used by loops (`blank_doc_buf`, `render_viewport`) long enough
to run across several keyboard-scan IRQs in a row, each one a fresh
chance for the pointer to actually get hit mid-loop and sent somewhere
else in memory -- corrupting whatever it read from or wrote to next.
Moved to `$03` instead, well clear of that range and not colliding
with the one other thing already using nearby zero page (`kw_ptr`/
`handler_vec` at `$02`).

The more important fix is in how this project verifies `editor.asm` at
all: `mini6502.py` has had a zero-page poisoning simulation
(`simulate_zp_poisoning`, on by default) since early in this project,
built specifically to catch exactly this class of bug by periodically
writing realistic garbage into `$F3-$F6` the way real hardware's own
IRQ handler does -- but every test in `test_editor.py` had been
calling it with `simulate_zp_poisoning=False` instead, silently
switching that protection off for the one file in this whole project
that actually needed it. Confirmed directly, not just reasoned about:
reverting the fix and running the suite with poisoning re-enabled
reliably reproduces the hang this bug actually caused in simulation;
with the fix in place and poisoning on, it doesn't. Every test in
`test_editor.py` now runs with `simulate_zp_poisoning=True`, and a new
one exists specifically to name this exact bug and check the two
operations the original report described: reaching the bottom of the
screen and pressing RETURN, with nothing left behind afterward. Swept
every other file in this project for the same class of collision (any
zero-page pointer landing in `$F3-$F6`) -- `editor.asm`'s `copy_ptr`
was the only one.

## `editor.asm`: F8 (help)

A small addition: F8 shows the bottom-row F-key assignments on the
status line, as a quick reference without needing to restart the
program to see the intro screen again. The exact wording asked for
("1:Q 2:NEW 3:SAV 4:DEL 5:LD 6:AS 7:DIR 8:?") comes to 41 characters,
one over the 40-column status line -- "DIR" became "DR" to fit, the
one abbreviation that could give up a character without shortening
any of the other, harder-to-trim words.

## `editor.asm`: the document scrolls well beyond one screen

The most substantial change this editor has had: the document is no
longer screen memory itself. A separate buffer, `DOC_BUF` -- 200 rows
of 40 columns, ~8x a single screen, placed well clear of this
program's own code in otherwise-free RAM -- is now the single source
of truth, and the visible 24 rows are only ever a rendered window onto
some contiguous slice of it, tracked by a new `doc_top_row`. Typing,
RETURN, and all four cursor keys now scroll the viewport when they'd
otherwise run off the visible top or bottom edge, rather than
stopping there the way the previous, screen-memory-is-the-document
version had to.

This was a deliberate architectural change, not a small addition, and
touched nearly every editing routine: `insert_char`, `handle_return`,
`handle_delete`, and all four `move_cursor_*` routines now read and
write `DOC_BUF` (via a new `compute_doc_ptr`, reusing `copy_ptr` for
it rather than claiming a new zero-page address, since the two uses
never overlap in time) and call a shared `try_scroll_down`/
`try_scroll_up` pair when they reach a visible edge with more document
beyond it. The "document full" message a recent, smaller change added
still fires, now correctly at `DOC_BUF`'s own true last cell rather
than the old single screen's -- confirmed precisely at that boundary,
not one row early or late.

Save now scans `DOC_BUF` backward first, trimming trailing blank rows,
so a short document doesn't take anywhere near as long to write to a
real drive (roughly 40-50 bytes/second) as writing the full, fixed
8000-byte buffer unconditionally every time would; an empty document
still saves as one blank row, not a zero-byte file, so it round-trips
the same way any other one does. Load fills the entire buffer
(blanking anything past the file's own end) and resets the viewport to
the top. F7 (directory) and the F4/F5 picker no longer need their own
literal screen backup to restore what was on screen before -- removed
entirely, along with the 1024 bytes it used, since `render_viewport`
can always correctly reconstruct any prior view from `DOC_BUF` and
`doc_top_row` on its own, which a byte-for-byte copy existed only to
approximate in the first place.

Confirmed the property that actually matters most, not just that
scrolling looks right on screen: content typed, scrolled far away
from, and scrolled back to is genuinely still there, and a saved file
correctly captures everything typed across many rows, not just
whatever happened to be visible at save time -- both checked directly
via a full save/load round trip spanning well beyond one screen.
Several existing tests had encoded the old, now-incorrect assumption
that reaching screen row 23 meant "no more room" -- updated to reflect
what's actually true now (there's 200 rows of real capacity, not 24),
not just patched to pass.

## `editor.asm`: feedback when there's no room left to type more

A small polish item, but confirmed as a real, reproducible rough edge
first, not assumed: filling the entire 24x40 document and continuing
to type past that point silently kept overwriting the single last
cell (row 23, column 39) forever, discarding whatever was typed there
with zero indication anything unusual was happening -- checked
directly: typing "!?#" after filling the document left only "#"
behind, the other two silently gone. Ordinary character wrap within
the document (typing past column 39 on any row before the last one)
was never the issue -- `move_cursor_right` already handles that
correctly, advancing to the next row's own column 0. The problem was
specific to the one cell where there's nowhere left to advance to at
all.

Fixed in the two places that share that same root cause -- no room
left to move to, not "wrapped to a new row": `insert_char` now shows
"DOCUMENT FULL -- NO ROOM TO ADD MORE." on the status line when typing
lands on that exact last cell (the character itself still gets
written -- only the further advance has nowhere to go), and
`handle_return` shows the same message when RETURN is pressed on the
last row, since it genuinely can't create a next line to move the
cursor to either. Both distinguish this precisely from ordinary
mid-document wrapping, which continues to show no message at all, and
the condition clears itself naturally the moment the cursor moves
away (there's no separate flag to reset, F2's own cursor reset back to
(0,0) is sufficient on its own).

## `editor.asm`: F3 remembers the document's filename, F6 is "save as"

F3 (save) now behaves the way most editors' plain save does: if the
document already has a name -- already loaded via F5, or already
saved once this session -- it saves straight back to that name without
asking again. It only prompts when there truly isn't one yet: a brand
new (F2) or never-saved document. F6 is the deliberate escape hatch --
always prompts for a filename regardless of whether one already
exists, and whatever name gets typed there becomes the document's new
current one, so a later plain F3 follows it rather than the original
name.

Shared the actual disk-writing logic between the two (scratch the
existing file, then write fresh, then remember the name used) as a
single `perform_save` routine, called either after copying the
remembered name into place (F3 with one already set) or after
`prompt_filename` returns one (F3 with none set yet, or F6
unconditionally) -- rather than duplicating that logic per entry
point.

One correctness detail worth naming plainly, not left implicit: the
new `current_filename_len` byte needed explicit initialization at
startup. Real hardware doesn't guarantee that RAM is zero at
power-on, and a stray nonzero value there would have made a brand
new document's very first F3 wrongly believe it already had a name --
a plausible, easy-to-miss bug specific to introducing a brand new
piece of persistent state, checked directly here (an explicit test
covers a fresh session's first F3) rather than left as an assumption.

## `editor.asm`: F4/F5 now show a selectable file list instead of typing a filename blind

Load and delete now parse the actual disk directory into a list --
cursor up/down to move, RETURN to pick, RUN/STOP to cancel -- rather
than asking for a filename typed from memory. Save keeps its existing
typing prompt, since a new filename can't be picked from a list of
what already exists. Reuses `lib/math.inc`'s `MULT_16` (a plain
power-of-two shift, no zero page needed, unlike `MULT_6`) to index the
15-entry filename buffer; capped at 15 files deliberately, not a
rounder-looking number -- the highest index that multiply stays
correct for in an 8-bit register (15*16=240; 16*16=256 would silently
wrap to 0).

Two real, non-obvious bugs came up building this, both caught only by
testing the complete F5 flow end to end rather than each piece in
isolation, and both worth naming plainly:

**Leading padding spaces.** A real directory entry's filename text is
padded with leading spaces to align the filename column against the
block-count number's own variable width -- `"NOTES"` in the listing
stream actually looks like `   "NOTES"`, not `"NOTES"` directly. The
first version of the filename parser checked only the very first
character for the opening quote, so every real file was mistaken for
"not a file" (the same bucket "BLOCKS FREE." falls into) and silently
skipped -- the picker would show nothing, or "NO FILES ON DISK." even
when there clearly were some. Fixed by skipping leading spaces before
checking for the quote.

**A stale `READST` flag leaking across an unrelated `OPEN`.** After
the first bug was fixed, files appeared in the picker correctly, but
selecting one and pressing RETURN still silently failed to load it.
The actual cause: real `READST`'s status persists until it's
explicitly read again, regardless of what happens in between -- it is
not tied to, or reset by, the specific file that was open when it was
last set. Parsing the directory listing's own final `CHRIN` call (the
closing `$00` terminator) happens to also be the very last byte of the
whole listing buffer, which sets the EOF flag -- and since nothing
read `READST` again before `parse_directory_filenames` returned, that
flag was still sitting there when `do_load`'s own, completely
unrelated `OPEN` (for the actual file the person picked) checked
`READST` immediately afterward, and wrongly concluded that file didn't
exist. This is not a simulator quirk; it is exactly how real `READST`
behaves, and it would have failed identically on real hardware. Fixed
by having `parse_directory_filenames` read (and so clear) `READST` one
last time before returning. `do_directory` (F7) and
`scratch_current_file` (used by F4 and by save's own overwrite logic)
had the same latent structural issue -- reading a stream through to
its own natural end without a final clearing read afterward -- and got
the same fix, even though it's less likely to actually trigger for
`scratch_current_file` specifically, which normally stops reading well
before the true end of its own short response.

The existing test suite needed real updates alongside this, not just
additions: three tests exercised save/load/delete scenarios ("load a
nonexistent file," "delete with an empty filename typed") that
described the *previous* typing-based F4/F5 flow and can no longer
happen at all now that both are picker-based -- removed or replaced
with the equivalent picker scenario (an empty disk, RUN/STOP before
selecting anything) rather than left in place describing behavior that
no longer exists.

## `adventure.asm`: combined room description and exits into one `Room` struct

A loose end closed, sitting unaddressed since `MULT_6` first made it
viable: `room_desc_table` (a plain word array, one description pointer
per room, indexed by `room*2`) and `room_exits` (its own 4-byte
`Exits`-struct-per-room table, indexed via `MULT_4`) are now one
6-byte `Room` struct per room (`desc_ptr` plus the four exits),
indexed by a single `compute_room_offset` using `MULT_6`. This was
deliberately deferred back when `.struct`/`.tag` were first added to
this file, specifically because `Exits` (4 bytes) had a matching
power-of-two macro and a combined 6-byte record didn't -- adding
`MULT_6` closed that gap.

Needed one new thing this file hadn't needed before: `MULT_6` (unlike
`MULT_4`) needs one byte of zero page (`mult_scratch`), which this
file didn't have declared. Rather than expand this program's own
already-documented zero-page footprint, used a single free byte that
had already been reserved but was previously unused within it (`$04`,
inside the existing `$02-$06` range) -- see this file's own header
comment for the full reasoning. `c64asm-reference.md` §10's own
worked example was also corrected: it had been claiming the *old*
`Exits`/`MULT_4` version was "this project's own `adventure.asm`,
close to verbatim," which stopped being true the moment this shipped;
that claim now correctly points at the `Room`/`MULT_6` version instead,
with the `Exits`/`MULT_4` example kept only as a generic illustration
of the power-of-two case, not tied to any specific file.

Confirmed behaviorally identical via the full existing regression
suite, including the complete win-condition playthrough -- this was a
pure internal data-layout refactor, not a gameplay change, and the
full suite passing unmodified is exactly what should be expected of
one.

## `editor.asm`: RUN/STOP cancels F3/F4/F5's own prompts

F3 (save), F4 (delete), and F5 (load) can now all be backed out of
mid-prompt without side effects, not just by pressing RETURN with
nothing typed. The C64 keyboard has no key labeled ESC; RUN/STOP
(PETSCII $03, also reachable as Ctrl+C) is the conventional C64
equivalent for "abort this," so that's what cancels here too --
during the filename prompt itself, and during F4's own Y/N
confirmation. Implemented by forcing the typed-length counter to zero
and falling into the same exit point an empty RETURN already uses, so
every caller's existing cancellation check handles it without any
separate code path. Confirmed RUN/STOP remains harmless during
ordinary typing in the main editing loop, where it isn't checked for
at all and simply falls through to being ignored like any other
unhandled control code, the same as before this change.

## `editor.asm`: F4 (delete), and fixed saving over an existing file

Two related changes, both built on the same mechanism. First, the
fix: loading a file, editing it, and saving under the same name
wasn't actually updating the file on disk at all -- real, well-
documented CBM DOS behavior, not a bug in the KERNAL calls themselves.
The drive refuses to write to a filename that already exists (a real
`63, FILE EXISTS` error) unless told otherwise. CBM DOS offers a
shortcut for this (`@0:name,S,W`, "save and replace"), but it has a
well-documented data-corruption bug on original 1541 firmware, fixed
only in later revisions (the 1541-II and 1571) -- not something a
program can detect or route around at the KERNAL call level, and
serious enough that CBM DOS references consistently recommend against
it. Fixed instead by `SCRATCH`ing the existing file first, then
writing fresh -- two plain, well-understood operations instead of the
buggy shortcut.

Second, F4: a delete command, built directly on that same `SCRATCH`
mechanism now that it exists. Unlike F2 (new) and F5 (load), which
overwrite the in-memory document without confirmation, F4 asks first
(Y/N) -- deleting a file from disk has no "just reload it" recovery
path the way an unsaved screen does, so the difference in stakes is
real, not just an inconsistency for its own sake.

A real, exact-value parsing bug came up while building this, caught
by testing against a genuinely wrong case rather than only the happy
path: the command channel's response to a SCRATCH command reports the
count of files actually deleted as a zero-padded two-digit field
(`01,FILES SCRATCHED,00,00` for none, `...,04,00` for four), and an
early version only checked for a bare `0` immediately followed by a
comma -- which never actually appears, since the field is always two
digits, so `"00"` was being misread as a nonzero count. The practical
effect: deleting a file that didn't exist was reported as `DELETED.`
instead of `FILE NOT FOUND.` Fixed by checking that both digits are
zero, which is what the field format actually guarantees. Testing this
meant extending `mini6502.py`'s command-channel simulation to actually
process a SCRATCH command against `self.disk_files` (removing the
matching entry and reporting a real count) rather than just returning
a fixed status string, which is also what made the original
overwrite bug possible to reproduce and verify fixed in the first
place, rather than assumed.

See `editor.asm`'s own header comment for the complete reasoning,
including why deleting a file briefly interrupted mid-operation can
still lose data (an inherent tradeoff of avoiding the buggier "@0:"
shortcut, not a flaw specific to this approach).

## `editor.asm`: F2 (new file)

The last of the five functions planned for this editor: F2 clears the
document back to blank and resets the cursor to (0,0), reusing the
same 4×240-byte loop shape `write_screen_to_file`/`read_file_to_screen`
already established for touching exactly the 960-byte editable area
(rows 0-23) and nothing else, so it can't disturb the status line the
same way an early version of the save code once did. No confirmation
prompt before clearing -- deliberately consistent with F5 (load),
which already overwrites the current document without asking first;
adding a confirmation only here would be a second, inconsistent rule
rather than a genuinely safer one. The status line's help text is
abbreviated to `F7=DIR` (from `F7=DIRECTORY`) to fit all five commands
in the 40-column row; the intro screen, which wraps naturally through
ordinary `PRINT`/`CHROUT`, still spells it out in full.

## Fixed: directory listing needs secondary address 0, not an arbitrary data channel

Root cause found and fixed, confirmed against real hardware via
`dir_sa_test.asm`: the special "$" directory request only produces a
well-formed listing when opened with secondary address 0 (matching
BASIC's own `LOAD"$",8`) -- unlike an ordinary file, where any value
2-14 works equally well as a data channel. `dir_demo.asm`,
`dir_raw.asm`, and `editor.asm`'s own directory code all used
secondary address 4, based on the general data-channel rule, which
was never actually correct for this specific request. Real hardware
showed exactly this: secondary address 0 returned the expected
`01 04 01 01 00 00 12 22...`; both 2 and 4 returned the same garbage
(`41 00` then a repeating 4-byte pattern), with `READST` never once
flagging an error the entire time -- which is what made this
genuinely hard to isolate rather than obviously wrong, and why it took
three rounds of narrowing (a raw byte dump, a drive-health check over
the command channel, then a direct secondary-address comparison) to
actually pin down instead of guessing.

Fixed the secondary address in all three files, and while there,
ported `dir_demo.asm`/`dir_raw.asm`'s other two fixes back into
`editor.asm`'s own directory code, which had never received them: it
now checks `OPEN`'s carry flag instead of assuming success, stops the
text-reading loop cleanly on `READST` rather than potentially hanging
on a missing terminator, and sends an explicit reverse-off after every
line.

The more important fix is in `mini6502.py` itself, not just this
project's own three `.asm` files: its virtual disk simulation didn't
distinguish directory reads by secondary address at all before this,
which is exactly why the original bug passed every test here and only
surfaced on real hardware. `_do_open` now only generates the
well-formed listing for secondary address 0; any other secondary
address with a "$" filename is modeled as a file that isn't there
(immediate EOF), not by reproducing the exact garbage bytes seen on
one real drive, since those specific bytes aren't something to treat
as a general, portable guarantee -- what matters for testing purposes
is that the wrong secondary address reliably does NOT produce a
well-formed listing, which this now enforces. Confirmed this actually
closes the gap by deliberately reverting the secondary address back to
4 and checking that the existing test suite now fails clearly instead
of passing -- it did, which is what "a test that would have caught
this" actually means, not just adding assertions and hoping.

## `dir_sa_test.asm`

A third diagnostic tool, testing a specific assumption that was never
actually verified: `dir_demo.asm`/`dir_raw.asm` always opened the "$"
directory request with secondary address 4, based on the general rule
that any value 2-14 is a valid data channel for an ordinary file --
but that rule describes reading a normal file, and this project never
actually confirmed the special "$" directory request behaves the same
way regardless of which secondary address is used. It was assumed,
not verified -- worth naming plainly, since `dir_status.asm` just
confirmed the drive itself is healthy ("00, OK,00,00"), which rules
out "no drive" as the explanation for `dir_raw.asm`'s garbage output
and points at exactly this kind of narrower, previously-unchecked
assumption instead. Notably, BASIC's own `LOAD"$",8` uses secondary
address 0 specifically, not an arbitrary data channel number.

Opens the directory with secondary addresses 0, 2, and 4 in turn and
prints the first 8 bytes each one returns, side by side, so they can
be compared directly rather than tested one at a time across separate
program runs.

## `dir_status.asm`

Another diagnostic tool: reads and prints the drive's own status
message off the command channel (secondary address 15), independent
of any directory-specific logic. Built directly in response to
`dir_raw.asm`'s real-hardware output -- "41 00" instead of the
expected "01 04" load address, then a tight repeating 4-byte pattern
for a full 128-byte dump, with `READST` never once flagging an error
the whole time. That combination -- the KERNAL apparently believing
the read is going fine while returning nonsense -- points away from a
parsing bug in this project's own code and toward the drive itself, or
how it's responding, which is exactly what the command channel exists
to report directly: every real disk operation writes its own result
there, whether or not a program asks for it, and a healthy drive
answers with a recognizable status string with no filename needed to
read it at all.

Testing this needed mini6502.py to grow actual command-channel
simulation (`drive_status`) -- secondary address 15 didn't do anything
special before this file needed it to.

Still an open, unresolved question: what this tool reports on the
actual hardware in question, and what that says about where the real
problem is.

## `dir_raw.asm`

A diagnostic tool, not a feature: dumps the raw bytes `OPEN`/`CHRIN`
return for a directory listing, in hex, with no interpretation at all.
Built because `dir_demo.asm`'s actual behavior on real hardware/VICE
("8192" printed, then two pi characters, then a hang) matched neither
its expected error message nor a well-formed listing, and `dir_demo`'s
own output -- which only ever shows its *interpretation* of the
bytes -- can't distinguish "the KERNAL returned something unexpected"
from "the parsing logic is misreading otherwise-correct data." This
shows the unfiltered truth instead: whether `OPEN`'s carry flag came
back set or clear, `READST` before any read, and every byte actually
returned, so the point where reality diverges from expectation is
visible directly rather than guessed at.

A real bug turned up while building this diagnostic tool itself, which
is worth calling out precisely because a diagnostic tool with its own
undetected bug is actively worse than no tool at all: an early version
read `READST`'s value, then called `PRINT` (which clobbers `A`
internally) before actually printing that value, producing
self-contradictory output ("STOPPED -- READST NONZERO: 00" -- nonzero
and 00 in the same line) that would have pointed straight at a wrong
conclusion. Fixed by saving the value across the `PRINT` call, the
same technique already used correctly for the `OPEN` error code right
next to it; the new regression test specifically checks the reported
value is real, not just present.

Still an open question, not a resolved one: whether what this tool
shows will actually explain `dir_demo.asm`'s behavior on real
hardware. That's the next thing to find out.

## `dir_demo.asm`

`editor.asm`'s disk directory listing, pulled out into its own
standalone program -- reported issues with `editor.asm` on real
hardware/VICE prompted breaking the problem down into smaller pieces
that are easier to test and debug in isolation, starting with this
one. Not a new feature so much as a diagnostic step: the smallest
program that can show whether something's actually wrong with how
this project reads a disk directory, separate from everything else
`editor.asm` does.

Re-reading the directory code for this surfaced two real gaps, both
invisible against mini6502.py's own virtual disk specifically because
that simulation's `OPEN` always succeeds and its generated listing is
always well-formed: the code never checked whether `OPEN` actually
succeeded (real `OPEN` signals failure via the carry flag -- no drive
present, no disk in it, device not responding -- and skipping that
check is exactly how a failed `OPEN` turns into reading garbage or
hanging instead of a clear error), and the text-reading loop had no
way to stop early if a stream ended before a `$00` terminator ever
arrived. Testing either gap needed mini6502.py to grow the ability to
simulate a missing drive (`device_present`) and a deliberately
malformed listing, neither of which existed before -- see
`mini6502.py`'s own `_do_open` comment. Also added an explicit
"reverse off" after every line, so the disk name line's own
reverse-video code can't visually carry into the rest of the listing
regardless of whether a real drive already sends one itself, which
public documentation on this specific point turned out to be
ambiguous about.

Whether these two fixes were the actual cause of what was seen on real
hardware isn't confirmed yet -- this is a step in an ongoing
back-and-forth, not a closed loop. If they turn out to help,
`editor.asm`'s own directory code should get the same fixes next.

## `editor.asm`: fixed F5 and F7 key codes

F5 did nothing, and F7 triggered load instead of the directory
listing. The actual bug: the four unshifted C64 function keys are
sequential PETSCII codes with no gaps -- F1=$85, F3=$86, F5=$87,
F7=$88 -- but the code used $88 for F5 (which is really F7) and $8A
for F7 (which is really shifted-F3/F4, not a key this editor even
listens for). Found from real use on hardware/VICE, not by this
project's own testing: the regression test used the exact same wrong
values the implementation did, so both agreed with each other and
neither caught the mismatch against the real keyboard. Re-verified the
correct codes against the same authoritative source used originally,
fixed both the code and the test's key constants, and added a new
check that presses each function key's real code in isolation and
confirms only its own intended action fires -- specifically so a
mistake shaped like this one can't pass silently again. See
`editor.asm`'s own header comment for the corrected key list.

## `editor.asm`: load, save, and directory listing

Real KERNAL disk I/O (`SETLFS`/`SETNAM`/`OPEN`/`CHKIN`/`CHKOUT`/
`CLRCHN`/`CLOSE`/`READST`, plus `CHRIN`/`CHROUT`'s file-redirected
behavior) added to the one-screen text editor, saving and loading the
document as a SEQ file and parsing a real disk directory listing's
byte format for F7. New status line (row 24; the editable area is now
rows 0-23, down from all 25) shows help text by default, a filename
prompt for F3/F5, and a one-line result ("SAVED.", "FILE NOT FOUND.",
"CANCELLED.") after each operation.

This is genuinely new ground for the project: every exact register
convention (`SETNAM`'s X=pointer-low/Y=pointer-high in particular,
easy to get backwards) and the directory listing's own byte structure
(a fake load address, then one BASIC-program-style "line" per file --
link pointer, a line number doubling as the block count, null-
terminated text) were verified against multiple independent,
authoritative sources before any 6502 code was written, not assumed
from memory.

**On testing**: this project has no way to test against a real IEC bus
or drive — mini6502.py's own emulation has never simulated disk
hardware. Rather than ship untested disk I/O, mini6502.py gained a
virtual file system trapping every KERNAL call this needed (see that
file's own header comment on `disk_files`), which is what the editor's
61-check regression test (up from 31) runs against. That's a real,
useful check — it confirms this program's own KERNAL call sequence,
register usage, and byte-for-byte file/directory-listing contents
match documented KERNAL conventions — but it is **not** the same as
testing against a real 1541 or VICE, which is still the right final
check before trusting this with anything you'd mind losing. That
boundary is documented directly in both `editor.asm`'s and
`mini6502.py`'s own header comments, not just here.

One real, meaningful bug came up during testing, not a cosmetic one: an
early version of `write_screen_to_file` copied the entire 1000-byte
screen, which meant saving a document also saved whatever was on the
status line at that exact moment — including, in one actual test run,
the tail end of the "SAVE AS:" prompt and the filename just typed into
it, baked directly into the saved file's own content. Fixed by
narrowing both the save and load copy loops to the 960-byte editable
area (rows 0-23) only, matching the row-24-is-not-part-of-the-document
design the status line itself introduced. `mini6502.py`'s own `_do_open`
was also adjusted after an initial design flaw: the first version only
flagged a missing file once something tried to read it, which left an
awkward ambiguity between "empty file" and "not found" for a caller
checking status right after `OPEN`; it now flags this immediately,
which is both simpler to model and simpler for `editor.asm`'s own load
logic to check.

See `README.md`'s file table and `editor.asm`'s own header comment for
the complete picture, including the exact SEQ filename-suffix
convention (`,S,W`/`,S,R`) and what F7's directory listing shows.

## `editor.asm`

A simple, one-screen text editor — the first step toward a planned
load/save/directory-listing follow-up, not a finished editor on its
own: it writes directly to screen memory, which is exactly what a
future "save" needs to read from (and a future "load" write back to),
so there's no separate document buffer that would need its own
conversion step added later.

Reads input via `GETIN` ($FFE4), the KERNAL's own keyboard-buffer
poll, rather than `lib/keyboard.inc`'s matrix-scanning `READ_KEY` --
right for a full-screen editor that needs Return/Delete/cursor keys as
ordinary keystrokes, not the single fixed key `READ_KEY` is built to
check. This needed real verification against an authoritative source
before writing any 6502 code, not assumed from memory: PETSCII and
screen memory use genuinely different byte values for the same
characters, and getting the conversion wrong would have meant garbage
displayed for whatever was actually typed. Cursor position is tracked
by a small row-address lookup table rather than a runtime multiply by
40 (not a power of two, and not one of `lib/math.inc`'s own small
non-power-of-two multipliers either) -- with only 25 possible rows,
precomputing was both simpler and faster than deriving a new multiply
routine for a number this specific to one screen layout.

Testing this needed a real extension to `mini6502.py` itself: only
`CHROUT`/`CHRIN` were emulated before, and `GETIN` -- essential for
this editor's whole design -- wasn't. Added a `getin_queue` mechanism
mirroring the existing `chrin_queue` one, and, while in there, fixed a
related pre-existing gap where neither trap actually set the CPU's
zero/negative flags after loading a value into `A`, which happened to
never matter before since nothing previously branched on the result of
a `CHRIN`/`GETIN` call the way `jsr GETIN` / `beq ...` (this editor's
own busy-wait) depends on. Purely additive to `mini6502.py` -- nothing
before this used `GETIN`, so there was no regression risk, confirmed
by the full existing test suite (292 checks across all eight demos)
passing unchanged. The editor's own regression test (31 checks) caught
one real mistake along the way, in the test itself rather than the
editor: an early version used $9D (cursor LEFT) where $1D (cursor
RIGHT) was intended, which looked at first like an editor bug before
the actual cause turned up.

## `lib/music.inc` and `music_demo.asm`

A two-voice SID music player, and a real demo built on it — "Twinkle
Twinkle Little Star" (public domain; the melody is the 18th-century
French folk tune "Ah! vous dirai-je, maman"), a sawtooth melody voice
over a triangle bass line, border color pulsing on the beat. The
library provides the sequencer (`MUSIC_INIT`, `music_tick` called once
per frame, `music_stop`); the actual note data — frequency and
duration tables for each voice — is the caller's own, the same way
`sound.inc` doesn't provide its own sound effects, only the mechanism
to play one. Frequencies were computed directly from the equal-
temperament/PAL-SID-clock formula rather than transcribed from a
reference table, and the demo's own regression test (103 checks)
recomputes every expected frequency independently, rather than
checking the assembled program's SID register writes against numbers
copied from the same place the `.asm` file's own data came from — a
transcription mistake in either the source data or the test would
actually be caught this way, not hidden by both sides agreeing by
construction. Confirmed correct frequency and gate-on/off state for
all 48 melody notes and all 6 bass notes, in order, including the
melody correctly looping back to its first note after a full cycle.
`wait_frame` is written fresh in the demo itself rather than pulling
in `graphics.inc` for it, which would otherwise mean satisfying nine
zero-page/RAM requirements this demo has no other use for. Also added
the previously-missing voice 2 and voice 3 SID register constants
(`VOICE2_FREQ_LO` and friends) to `hardware.inc`, matching voice 1's
own existing naming — only voice 1 had been needed by anything before
now. See `lib-reference.md` and `README.md`'s file table.

## `c64disasm.py`

A disassembler — the other direction from everything else in this
project, turning a `.prg` back into `c64asm.py`-compatible source. A
genuinely different problem from assembling: a raw binary doesn't say
which of its bytes are meant to be executed as instructions and which
are data that just happens to sit in the same address space, so this
follows actual code flow from the program's entry point (branches,
jumps, and calls, recursively) rather than blindly decoding byte by
byte, and shows anything never reached that way as plain `.byte` data
— the honest answer for a data byte, even when it isn't the most
informative one. Also detects and reconstructs printable PETSCII text
runs as readable `.text "..."` lines, self-verified against `c64asm.py`'s
own encoding function before ever being used, so a wrong guess simply
falls back to `.byte` instead of risking incorrect output.

Deliberately a single Python tool, not matched across three
implementations the way `c64asm.py` itself is — see `c64disasm.py`'s
own header comment for the full reasoning. Its correctness test is
disassembling and reassembling every `.prg` this project ships — seven
real demos plus the illegal-opcode showcase — and confirming
byte-for-byte identical output to the original across all three
`c64asm` builds, not a synthetic test written to be easy to pass. One
real bug came up during that testing and was fixed before shipping:
an early version generated a `jsr $FFD2`-style KERNAL call as a
reference to a *generated* label that was never actually defined
anywhere in the output, since the real target address falls well
outside the disassembled file's own range — now any jump/call/branch
target outside the file becomes a plain hex address instead of an
undefined symbol. See `README.md`'s "Disassembler" section.

## `GETTING-STARTED.md`

A short, example-driven walkthrough distinct from `c64asm-
reference.md`'s exhaustive syntax reference — three verified,
runnable examples (a minimal border-color program, a deliberate error
to show the error-reporting format, and a standard-library "hello
world" using `lib/text.inc`) building from nothing to a working
program, then a map pointing at the rest of the project's
documentation. Every example and command in it was actually run
before being written down, including the multi-file build commands
for all three implementations — one of which surfaced a real bug in
the example itself (a bare `.basic` with no explicit label, placed
right before a `.include`, silently jumped into the library's own
code instead of the program's), which became the guide's own
explanation of that exact gotcha rather than a mistake quietly fixed
and forgotten.

## `lib/math.inc`: `MULT_3`/`MULT_5`/`MULT_6`/`MULT_7`/`MULT_9`/`MULT_10`/`MULT_12`

Closes a real gap `MULT_2`/`MULT_4`/`MULT_8`/`MULT_16` left open: a
`.struct` that comes out to a non-power-of-two size — 6 bytes, like
`c64asm-reference.md`'s own `Room` example — had no library support at
all until now. Each is a shift-and-add (or, for `MULT_7`,
shift-and-subtract) built from the same power-of-two shifts the
existing macros already use — `MULT_6` is `(A*4) + (A*2)`, for
instance. Verified against 77 real, runtime-executed test cases (7
macros × 11 input values each, checked via `mini6502.py`) before
shipping, not just checked for correct assembly.

Unlike the power-of-two macros, these need one byte of zero page
(`mult_scratch`) — the 6502 has no register-to-register arithmetic at
all, so combining two shifted copies of `A` needs somewhere
addressable to hold one of them while the other stays in `A`. There's
deliberately no non-power-of-two `DIV_N`: dividing by an arbitrary
small constant needs real division logic, not a short shift-and-add,
which is a meaningfully bigger, slower thing than anything else in
this file — seemed better to leave it out and say so than ship
something half-baked. `c64asm-reference.md` §10's own `Room` example
now uses `MULT_6` directly instead of noting that nothing in the
library applied to it. See `lib/math.inc`'s own header comment for the
complete reasoning, and `c64asm-reference.md` §10.

## Cycle counts in `--listing`

Every assembled instruction in the listing file now shows how many
cycles it takes, cross-checked entry-by-entry against 6502.org's
published NMOS 6502/6510 timing table rather than reconstructed from
memory, given how easy this is to get subtly wrong. Handles the two
well-known variable cases explicitly instead of guessing: `4/5` for a
*read* instruction (`lda`, `cmp`, and similar) using indexed
addressing that might cross a page boundary at runtime, and `2/3/4`
for a conditional branch (not taken / taken / taken across a page).
Correctly excludes the page-crossing bonus for writes (`sta`) and
read-modify-write instructions (`inc`, `asl`, and similar) using
indexed addressing, which always take a fixed number of cycles
regardless — confirmed directly in test output that `lda $1000,x`
shows `4/5` while `sta $2000,x` shows a plain `5`, not `4/5`, despite
using the same addressing mode. Illegal/undocumented opcodes are
deliberately left blank rather than guessed. Along the way, found and
fixed a small pre-existing inconsistency between the Python and C
implementations' listing output (C wasn't trimming all trailing
whitespace from the source column, only newlines) that had never
surfaced before since nothing previously compared listing file
*content* between implementations directly. See `c64asm-reference.md`
§19.

## `adventure.asm`: each `room_exits` row individually `.tag`'d

`.tag` checks one struct instance, not a whole array — but nothing
stops tagging each *element* of an array on its own, which turned out
to be a real, meaningful check `.struct` alone never provided:
`.struct` guarantees `Exits` itself is 4 fields, but says nothing
about whether any particular row in `room_exits` actually has 4
values written out. Before this, a mistyped row (three values instead
of four) assembled cleanly with no warning at all, silently shifting
every room after it by one byte — confirmed by deliberately
introducing exactly that mistake and watching it assemble without
complaint. Tagging each of the five rows individually closes that gap
— the same mistake now fails immediately, naming which row and by how
much it's off — while assembling to *exactly* the same bytes as
before, confirmed byte-for-byte identical to the prior shipped
`adventure.prg`, and re-verified against the full game-playing test
suite including the win-condition playthrough. See
`c64asm-reference.md` §12, "Tagging each element of an array".

## `.tag`/`.endtag`

Binds a data block to a `.struct`, automatically checking at
`.endtag` that the block is really that struct's size:

```asm
room_data: .tag Room
        .word room0_desc
        .byte FOREST, $ff, COTTAGE, $ff
.endtag
```

Replaces the manual pattern of an `_end` label plus `.assert
end_label - start_label == Name.size` with something automatic. Pure
observation, not transformation — unlike `.repeat`/`.struct`, `.tag`
doesn't capture or reshape the lines between `.tag` and `.endtag` at
all, it just records `pc` at each end and compares the difference, so
whatever's actually in between (`.byte`, `.word`, `.text`, `.incbin`,
ordinary instructions, anything) assembles completely normally on its
own terms. Recoverable, not fatal, the same as `.assert` — a wrong
`.tag` doesn't corrupt anything about the rest of the file the way a
malformed `.repeat`/`.struct`/`.macro` would, so there's no reason to
stop assembly over it. Checks a single struct instance's size only,
not an array of them — `adventure.asm`'s array-of-records `room_exits`
table still uses the `.assert`-based pattern for that case, documented
alongside `.tag` itself. See `c64asm-reference.md` §12.

## `lib/math.inc`: `DIV_2`/`DIV_4`/`DIV_8`/`DIV_16`

Truncating, unsigned division of the A register by a small power of
two, via right shifts (`LSR`) — the mirror of `MULT_N` below, for the
reverse operation: recovering a record index from a byte offset. See
`c64asm-reference.md` §10, "Indexing an array of records".

## `lib/math.inc`: `MULT_2`/`MULT_4`/`MULT_8`/`MULT_16`

Multiply the A register by a small power of two via left shifts
(`ASL`), since the 6502 has no multiply instruction — mainly for
indexing into an array of `.struct` records. `adventure.asm`'s
room-navigation code now uses `MULT_4` in place of two hand-written
`asl a` lines. See `c64asm-reference.md` §10.

## `--warn-unused-all`, and `--warn-unused` scoped to the main file

`--warn-unused` now only reports symbols defined in the file named on
the command line, not anything it `.include`s, with a one-line count
of how many more were suppressed. `--warn-unused-all` restores the
original, unscoped behavior for anyone who wants the full picture. The
scoping exists because the original, unscoped version reported so much
routine library noise — 184 warnings against `demo.asm`, almost
entirely unused `keyboard.inc` constants for keys that particular demo
never checks — that it was hard to use in practice on any program
built on the standard library. See `c64asm-reference.md` §21.

## `--warn-unused`

Warns, after assembly finishes, about every label or `=`/`.equ`
constant (including `.struct` fields) defined but never referenced
anywhere in the program. Opt-in, never fails the build. See
`c64asm-reference.md` §21.

## `.assert`, and `==`/`!=` expression operators

`.assert condition[, "message"]` fails assembly (recoverably, the same
as `.error`) if `condition` evaluates to false — a compile-time
sanity check, most usefully paired with a `.struct`'s `Name.size`
(catching a struct that gained or lost a field out from under code
that assumed a specific size, say). Needed a real equality operator to
be useful for that, so `==` and `!=` were added to the expression
grammar at the same time — deliberately not `<`/`>`/`<=`/`>=` as
binary comparisons, since `<`/`>` are already unary low/high-byte
operators here, and overloading them for both meanings would be
genuinely ambiguous to parse. `adventure.asm`'s
`compute_room_exits_offset` now carries an `.assert` guarding its
hand-rolled multiply against exactly that kind of drift. See
`c64asm-reference.md` §11 and §4.

## `sprites.asm`: a real demo using `.incbin`

A 4-frame sprite animation loaded from `star_anim.bin`, an external
binary asset, via `.incbin`'s offset/length slicing — instead of the
hand-transcribed `.byte` sprite data every other demo uses. Same
W/A/S/D movement and border stop as `demo.asm`'s own star, plus
continuous frame-cycling independent of movement. See `README.md`'s
file table.

## `.struct`

Named byte offsets into a data record (`Room.north` instead of a bare
offset number) — `.byte`/`.word`/`.res` field declarations inside a
`.struct`/`.endstruct` block, producing `Name.field` symbols plus an
automatic `Name.size`. `adventure.asm`'s `room_exits` table was
converted from four separate parallel arrays
(`exit_north`/`exit_south`/`exit_east`/`exit_west`, one per direction)
into a single `.struct`-based combined table, indexed per room.
Required widening identifier parsing to allow a dot mid-symbol-name,
in exactly the two places that actually needed it, confirmed not to
change behavior for any previously-valid program (a dotted identifier
was always a hard parse error before). See `c64asm-reference.md` §10.

## `.incbin`

Imports raw bytes from an external binary file directly into the
assembled output, with optional `offset`/`length` arguments to slice
out part of a larger file — for sprite/font/music data made in an
external tool, instead of hand-transcribing it as `.byte` lists.
Reuses `.include`'s own path resolution rules (relative to the
including file first, `--lib-dir` as a fallback). Every error
`.incbin` can produce is fatal, not recoverable, deliberately unlike
`.byte`'s undefined-symbol handling: an `.incbin` problem means the
assembler doesn't know the emitted byte *count*, which would corrupt
every address computed after it if assembly tried to continue. See
`c64asm-reference.md` §14.

## `.repeat`/`.dup`

Assembles a block of code `count` times at assembly time, with an
optional index available inside the block via the same `\param`-style
substitution macro parameters already use. `.dup`/`.enddup` are exact
synonyms for `.repeat`/`.endrepeat`. Implemented as an anonymous macro
immediately invoked `count` times in a row, reusing the macro system's
own per-invocation local-label scoping rather than building a parallel
mechanism. `count` must be a plain integer literal, not a symbol or
expression, since `.repeat` is expanded during the same preprocessing
pass as `.macro`/`.include`, entirely before pass 1 builds a symbol
table. See `c64asm-reference.md` §9.

## `.error`/`.warning`

`.error "message"` records a recoverable error with a custom message;
`.warning "message"` prints a message without affecting whether
assembly succeeds. Typically paired with `.ifdef`/`.ifndef` to check a
precondition — every file in the standard library now checks its own
required zero-page symbols this way, right at the top, turning a
confusing `Undefined symbol` error surfacing three files deep into one
clear message naming exactly what's missing. Building this out
surfaced a real, pre-existing bug: an earlier `demo.asm` checked for a
"Y (restart)" keypress using a keyboard-matrix value that was actually
the **5** key's position, not Y's — found while cross-checking against
`keyboard.inc`'s new named key constants, which made the mismatch
visible for the first time; fixed in both `demo.asm` and its test.
See `c64asm-reference.md` §15.

## `--vice-labels`

Writes a VICE monitor label file (one `add_label` command per symbol),
for debugging by name in the VICE monitor — `break .main_loop` instead
of `break $0a60`, and disassembly showing names instead of bare
addresses. See `c64asm-reference.md` §20.

## `keyboard.inc`

Named `KEY_<name>_COL`/`KEY_<name>_ROW`/`KEY_<name>_CODE` constants for
every key on the C64 keyboard matrix, verified against the standard
published matrix reference, plus `wait_any_key` for a blocking "wait
for any key, return which one" read. Writing this out surfaced the
`demo.asm` Y-key bug described above.

## `demo.asm`: WASD movement, Q to exit

The star sprite now moves with W/A/S/D, stopping at the screen edges,
with a short sound on each successful move; the exit key changed from
Y to Q, with updated on-screen instructions.

## `demo.asm`: a visible sprite

The demo's sprite data was previously all zero bytes — invisible, even
though sprite hardware setup was otherwise correct. Replaced with a
small hand-drawn star.

## `demo.asm`: wait for a keypress before switching to bitmap mode

Previously, `demo.asm` switched to bitmap mode immediately after
printing its welcome text, wiping the screen before there was any real
chance to read it. Now waits for a keypress first, with on-screen
instructions explaining the controls.

## `.charset upper`/`.charset lower`

Controls how `.text`/`.asc`/`.byte` string literals encode letters:
forced-uppercase (`upper`, the default, matching every C64 program's
default display charset) or case-preserving (`lower`, once a program
has switched to the C64's alternate character set at runtime).
`adventure.asm`'s room descriptions were converted to natural mixed
case using this. See `c64asm-reference.md` §6.

## `.text`/`.byte` mixed strings and numeric bytes

`.text`/`.asc` (and quoted-string arguments to `.byte`) accept
comma-separated numeric bytes alongside quoted strings on the same
line — `.text "HELLO", 13, 0` instead of a separate `.text "HELLO"`
followed by `.byte 13, 0`. See `c64asm-reference.md` §7.

## Illegal/undocumented 6502/6510 opcode support

`.cpu 6510x` enables the well-known undocumented NMOS 6502/6510
opcodes (`LAX`, `SAX`, `DCP`, and others); `.cpu 6510` (the default)
or `.cpu 6502` turns support back off. Off by default, since these
aren't part of the documented instruction set. See
`c64asm-reference.md` §17 and `c64asm-opcode-reference.md`.

## Multi-error reporting

Most kinds of mistake — an undefined symbol, a malformed expression,
an unsupported addressing mode, a branch out of range, a redefined
symbol — no longer stop assembly at the first one found. Independent
problems are collected (up to 20 per run) and reported together in one
pass, closer to how a modern compiler behaves. A smaller category of
whole-file structural problems (a missing `.include`d file, a
malformed `.macro`, and similar) still stops assembly immediately,
since the shape of the rest of the file becomes genuinely ambiguous
once one of those is true. See `c64asm-reference.md` §22.

## Foundational feature set

Everything else the assembler could already do by the time the entries
above began: the full NMOS 6502/6510 instruction set (all 56
documented mnemonics, every addressing mode), automatic zero-page vs.
absolute addressing selection, two-pass assembly with forward
references, a real expression evaluator (`+ - * /`, parentheses, unary
`<`/`>` for low/high byte, `$hex`/`%binary`/decimal/`'char'` literals,
`*` for the current program counter), macros (`.macro`/`.endmacro`)
with named parameter substitution and recursive invocation, local
labels (`@label`) automatically scoped per macro invocation, `.include`
with automatic include-once semantics and circular-include detection,
conditional assembly (`.if`/`.elif`/`.else`/`.endif`,
`.ifdef`/`.ifndef`), the `.prg` output format, `--listing` output, and
the original standard library files (`hardware.inc`, `text.inc`,
`input.inc`, `graphics.inc`, `sound.inc`) extracted from and
cross-checked against this project's own demo programs
(`hello.asm`, `bounce.asm`, `pong.asm`, `adventure.asm`, `lander.asm`).
See `c64asm-reference.md` and `README.md` for the full, current
picture of all of it.
