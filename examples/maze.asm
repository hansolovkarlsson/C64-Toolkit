; maze.asm - a maze-chase game. Stage 4: level data loaded from disk,
; on top of stages 1-3's own tile rendering, movement/collision, and
; dot collection/scoring.
;
; Stage 1 built the core mechanism everything else sits on: a custom
; VIC-II character set for maze tiles, and a maze grid held in its own
; RAM, rendered onto the screen from that grid rather than drawn once
; and forgotten. Stage 2 added a real, moving player: a hardware
; sprite (not a character-cell player -- a sprite gives smooth,
; pixel-by-pixel movement independent of the tile grid underneath it,
; the same reason later stages' enemies will also be sprites, not
; characters), joystick input (port 2, via this project's own already-
; tested read_joy2) OR WASD (this project's own established convention
; for keyboard movement elsewhere -- demo.asm/bounce.asm -- reusing
; the same verified column/row constants from keyboard.inc rather than
; risking a hand-written binary literal for the wrong key, which has
; already happened once elsewhere in this project; see that file's own
; header comment), and wall collision checked directly against the
; maze grid -- the same grid stage 1's own rendering already treats as
; the source of truth, not a separate copy that could drift out of
; sync with it. Stage 3 added dot collection and a real, displayed
; score.
;
; Movement model: continuous, not one-tile-per-keypress, with turns
; only committed when the player is exactly tile-aligned (matching the
; classic arcade convention this genre is built around) -- pressing a
; direction that isn't open yet just does nothing until the player
; reaches a cell where it is, rather than either ignoring it entirely
; or queuing it to happen automatically later; that queuing refinement
; is a reasonable future polish pass, not something this stage needs
; to get a working, correct maze game. Also matching that same
; convention deliberately, not by omission: releasing the joystick
; doesn't stop the player -- it keeps moving in its current direction
; until blocked by a wall, exactly the way the original arcade game
; this genre is built around behaves (the joystick sets the *next*
; direction to turn, not "move only while held").
;
; --- level file format ---
;
; This stage's own point: the maze grid, the player's own starting
; tile, and the level's own title now come from a real file on disk
; ("LEVEL1"), loaded once at startup by load_level, rather than being
; assembled directly into this program's own bytes the way stages 1-3
; shipped with first -- a different level means authoring a new file,
; not reassembling this program. 858 bytes total for this maze's own
; dimensions:
;   byte 0            player's own starting column
;   byte 1            player's own starting row
;   bytes 2-21        the level's own title, 20 bytes, PETSCII,
;                       space-padded (LEVEL_TITLE_LEN) -- loaded and
;                       held ready for a later stage to actually
;                       display (a level-select screen, say); this one
;                       doesn't render it anywhere itself yet
;   bytes 22-857      the maze grid itself, TILE_COLS*TILE_ROWS bytes,
;                       row-major, the exact same encoding MAZE_GRID
;                       already uses (0/1/2/3 for empty/wall/dot/
;                       pellet) -- copied straight in with no
;                       translation needed
;
; Planned for later stages, not yet here: enemy sprites with real
; chase/patrol AI, power pellets doing something, lives and game-over,
; sound.
;
; --- VIC-II setup ---
;
; Selects VIC bank 0 ($0000-$3FFF) -- already the default after reset,
; but asserted explicitly here via the careful read-modify-write
; hardware.inc's own CIA2_PRA comment documents, rather than assumed;
; the other 6 bits of that same register are the serial/IEC bus lines
; this project's own disk I/O depends on, and a careless full-byte
; write there is exactly the kind of thing that could silently break
; loading a level from disk in a later stage.
;
; Character memory lives at $3000 (VIC_MEMPTRS value 6 in bits 3-1) --
; not $1000, which VIC banks 0 and 2 both shadow with the character
; ROM regardless of what's actually in RAM there (see VIC_MEMPTRS's
; own comment in hardware.inc). Screen memory stays at $0400, this
; project's own usual place (VIC_MEMPTRS value 1 in bits 7-4).
;
; --- player sprite and coordinates ---
;
; Sprite (24, 50) is the standard, widely-documented VIC-II position
; for the top-left corner of the visible screen -- confirmed against
; several independent sources, not just one, and matching this
; project's own already-tested bounce.asm/pong.asm (their own XMIN/
; YMIN). Screen column N is sprite X = 24 + N*8; screen row N is
; sprite Y = 50 + N*8.
;
; The player's own position (player_rel_x/player_rel_y) is tracked
; relative to the MAZE's own top-left pixel, not the whole screen's --
; 0,0 is the maze's own first tile's own top-left corner. This makes
; tile alignment a trivial check (the low 3 bits of either coordinate
; being clear, with no need to first subtract the maze's own screen
; offset, which isn't itself a multiple of 8 on the Y axis --
; MAZE_SCREEN_ROW*8+50 = 58, and 58 isn't one). The maze's own screen
; offset is added back in only once, in update_sprite_position, to get
; the final absolute sprite coordinates actually written to
; SPRITE0_X/SPRITE0_Y.
;
; player_rel_x is a genuine 16-bit value (player_rel_x_lo/
; player_rel_x_hi), unlike player_rel_y (still a single byte -- sprite
; Y never approaches 255 regardless of maze height here, so it never
; needed this): this maze is wide enough (TILE_COLS*8-1 = 303) that
; its own range alone exceeds a single byte, and added to the maze's
; own screen offset (MAZE_ORIGIN_X = 32), the player's maximum
; possible absolute sprite X reaches 335 -- past 255, requiring the
; same second, high-bit X handling (SPRITE_X_MSB) bounce.asm's own
; wider bounce area already needs, not an oversight carried over from
; an earlier, narrower version of this same maze that deliberately
; stayed under that threshold instead. update_sprite_position sets or
; clears PLAYER_SPRITE_X_MSB_BIT (SPRITE_X_MSB's own bit for sprite 0)
; based on whether the computed absolute X actually crossed 256, the
; same way it's the only place the maze's own screen offset gets added
; back in at all.
;
; --- tile characters and the maze grid ---
;
; Four tile types, one character code each, chosen to equal their own
; screen code exactly (TILE_EMPTY=0, TILE_WALL=1, TILE_DOT=2,
; TILE_PELLET=3) -- render_maze copies the grid straight to screen
; memory with no translation step as a direct result. MAZE_GRID (at
; $3800, right after the 2KB of custom character memory, still
; comfortably inside VIC bank 0) holds one byte per tile, TILE_COLS
; across by TILE_ROWS down, row-major.
;
; The test maze below is this stage's own placeholder -- fixed,
; assembled-in data, not yet loaded from disk (that's stage 4's own
; job, once the level file format exists to load).

        .basic start
        .include "lib/hardware.inc"

; --- zero page ---
; screen_ptr/grid_ptr: render_maze's own working pointers. $02-$06 is
; documented elsewhere in this project as safe, ordinary zero page --
; deliberately not anywhere near $F3-$F6, which this project's own
; past experience (a real bug, caught on real hardware, in this same
; project's text editor) already identified as actively clobbered by
; the KERNAL's keyboard-scan IRQ.
screen_ptr = $02
grid_ptr   = $04

; input.inc requires this defined before its own .include, even though
; this file never actually calls extract_word (the only routine that
; ever references it) -- sharing grid_ptr's own address is safe
; specifically because the two are never active at the same time:
; render_maze (which owns grid_ptr) never runs while reading input.
word_dest_ptr = grid_ptr

; keyboard.inc requires this too, for the same reason and with the
; same safety guarantee as word_dest_ptr just above -- key_scratch is
; "never live across a call to anything else in this library" per
; that file's own header comment, so aliasing it onto grid_ptr's own
; address is safe for the identical reason.
key_scratch = grid_ptr

        .include "lib/input.inc"       ; needs word_dest_ptr, just
                                           ; defined above -- only
                                           ; read_joy2 is actually
                                           ; used here
        .include "lib/keyboard.inc"    ; needs key_scratch, just
                                           ; defined above -- named
                                           ; WASD constants for
                                           ; keyboard movement,
                                           ; alongside the joystick

; Ordinary RAM (no indirect addressing needed on any of these), in the
; same cassette-buffer region this project's other programs already
; use safely for exactly this kind of small working state.
player_rel_x_lo      = $033c   ; 0-303: player's X, relative to the
                                   ; maze's own top-left pixel -- see
                                   ; this file's own header comment for
                                   ; why this is a genuine 16-bit value
player_rel_x_hi      = $033d
player_rel_y         = $033e   ; 0-175, same idea for Y (still a
                                   ; single byte -- see the same
                                   ; comment for why Y never needed
                                   ; the second byte X does)
player_direction     = $033f   ; 0=stopped, 1=up, 2=down, 3=left, 4=right
joy_state            = $0340   ; this frame's own read_joy2 result,
                                   ; held here briefly so update_player
                                   ; doesn't need to re-read it per
                                   ; direction checked
tile_col             = $0341   ; get_tile's own input -- which grid
                                   ; column/row to look up
tile_row             = $0342
can_move_dir_scratch = $0343   ; can_move_direction's own saved copy
                                   ; of the direction it's checking

; can_move_direction's own scratch for converting player_rel_x (16-bit)
; to a single-byte tile column: a copy it can freely shift right 3
; times (dividing by 8) without disturbing the player's own actual
; position, since the result (0-37) always fits in the low byte alone
; once the shift's done, the high byte is only needed as scratch space
; during the shift itself.
x_shift_lo = $0344
x_shift_hi = $0345

; update_sprite_position's own scratch for computing the player's
; final absolute sprite X (MAZE_ORIGIN_X + player_rel_x, as a genuine
; 16-bit sum) before splitting it into SPRITE0_X's own low byte and
; whichever bit of SPRITE_X_MSB decides whether the 9th bit is set.
sprite_x_lo = $0346
sprite_x_hi = $0347

; compute_row_offset's own scratch: a genuine 16-bit result (see that
; routine's own comment for why -- TILE_ROWS*TILE_COLS is far past
; what a single byte can hold), plus the two partial products (row*2,
; row*4) it adds together to get row*38.
row_offset_lo  = $0348
row_offset_hi  = $0349
row_times2_lo  = $034a
row_times2_hi  = $034b
row_times4_lo  = $034c
row_times4_hi  = $034d

; init_maze_grid's own 16-bit byte counter -- needed because
; TILE_COLS*TILE_ROWS (836 for this maze) exceeds what an 8-bit
; counter can reach.
maze_copy_count_lo = $034e
maze_copy_count_hi = $034f

; The player's own score, and render_score's own working state for
; converting it to decimal digits. A genuine 16-bit score (max 65535)
; since this maze has hundreds of dots worth 10 points each, plus
; pellets worth more -- comfortably past what a single byte could
; hold well before the maze is half cleared.
score_lo = $0350
score_hi = $0351
score_scratch_lo = $0352      ; extract_digit's own working copy of
score_scratch_hi = $0353         ; the score, so the real score isn't
                                     ; disturbed while being displayed
subtract_amount_lo = $0354    ; extract_digit's own input: the power
subtract_amount_hi = $0355       ; of ten currently being extracted

; compute_tile_screen_addr's own scratch, for the same reason
; compute_row_offset needs row_times4/row_times2: a genuine 16-bit
; multiply (by 40, the screen's own row stride), since
; (MAZE_SCREEN_ROW+tile_row)*40 can reach 880, past a single byte.
screen_row_offset_lo = $0356
screen_row_offset_hi = $0357
screen_row_times8_lo = $0358
screen_row_times8_hi = $0359

; The player's own starting tile and the level's own name, both
; loaded from disk by load_level -- no longer compile-time constants,
; since a different level file could specify a different start tile
; or title without this program itself changing at all. LEVEL_TITLE_LEN
; matches the fixed-width title field load_level's own comment
; documents; the title is loaded and held here, ready for a later
; stage to actually display it (a level-select screen, say), but this
; stage doesn't render it anywhere itself yet.
player_start_col = $035a
player_start_row = $035b
LEVEL_TITLE_LEN = 20
level_title = $035c        ; 20 bytes: $035c-$036f

; --- enemy AI (2 enemies, VIC-II sprites 1 and 2 -- sprite 0 is the
; player's own) ---

; Each enemy's own persistent state between frames -- position and
; current direction, the same fields player_rel_x_lo/player_rel_x_hi/
; player_rel_y/player_direction hold for the player, just duplicated
; per enemy rather than shared, since both enemies need their own
; independent position at the same time.
enemy1_rel_x_lo  = $0370
enemy1_rel_x_hi  = $0371
enemy1_rel_y     = $0372
enemy1_direction = $0373

enemy2_rel_x_lo  = $0374
enemy2_rel_x_hi  = $0375
enemy2_rel_y     = $0376
enemy2_direction = $0377

; The "current enemy" working set: update_enemies loads whichever
; enemy's own stored state here before calling the shared, generic
; per-enemy routines (update_enemy_ai_and_move, update_enemy_sprite_
; position), then writes back whatever they changed -- one set of AI/
; movement/sprite-position code shared by both enemies, rather than
; duplicating it per enemy the way the persistent state above is
; duplicated (state needs to be separate; the logic operating on it
; doesn't).
enemy_rel_x_lo    = $0378
enemy_rel_x_hi    = $0379
enemy_rel_y       = $037a
enemy_direction   = $037b
enemy_sprite_num  = $037c   ; 1 or 2 -- which VIC-II sprite the
                                ; "current enemy" working set maps to

; decide_enemy_direction's own scratch for picking the best of up to
; 4 candidate directions, plus consider_enemy_candidate's own
; Manhattan-distance working byte.
enemy_best_direction = $037d
enemy_best_dist      = $037e
enemy_dist_scratch   = $037f

; The player's own current tile, computed once per frame by update_
; enemies (not by either can_move_direction or can_enemy_move_
; direction, whose own contract is about a checked *neighboring*
; tile, not "the player's tile" specifically) and shared by both
; enemies' own decide_enemy_direction calls, rather than recomputed
; twice.
player_tile_col = $0380
player_tile_row = $0381

; update_enemy_sprite_position's own scratch, mirroring update_
; sprite_position's own sprite_x_lo/sprite_x_hi but for whichever
; enemy is currently being updated.
enemy_sprite_x_lo = $0382
enemy_sprite_x_hi = $0383

; update_enemies' own frame counter for throttling enemy movement --
; see ENEMY_THROTTLE_MASK's own comment, just below, for the full
; reasoning.
enemy_move_throttle = $0384

; The power-pellet vulnerability window: a 16-bit countdown (0 =
; inactive), and each enemy's own "currently frightened" flag. Kept
; per-enemy, not one shared boolean, specifically because eating one
; frightened enemy ends *its own* vulnerability immediately without
; touching the other's -- matching the classic convention this
; mechanic is drawn from, where an eaten ghost doesn't stay edible
; after respawning even if the window is still running for the rest.
pellet_vulnerable_timer_lo = $0385
pellet_vulnerable_timer_hi = $0386
enemy1_frightened = $0387
enemy2_frightened = $0388
enemy_frightened = $0389          ; "current enemy" working-set copy,
                                       ; loaded/stored the same way
                                       ; enemy_rel_x_lo and the rest
                                       ; already are

; decide_enemy_direction/consider_enemy_candidate's own flag for
; whether any open, non-reversing candidate has been found yet this
; call -- see decide_enemy_direction's own comment for why this
; replaced comparing enemy_best_dist against a fixed sentinel once
; fleeing (maximizing distance) was added alongside chasing
; (minimizing it): a sentinel that's correct for telling "nothing
; found" apart from "found something" when minimizing isn't
; automatically correct once the comparison direction can flip.
enemy_found_candidate = $038a

; update_enemies' own per-enemy scratch for which throttle mask/skip-
; frame pair applies this frame -- frightened enemies move on a
; different (slower) cycle than normal ones, see FRIGHTENED_THROTTLE_
; MASK's own comment, just below ENEMY_THROTTLE_MASK's.
enemy_throttle_mask_scratch = $038b
enemy_throttle_skip_scratch = $038c

; check_enemy_collision's own saved copy of SPRITE_SPRITE_COLLISION --
; needed because reading that register clears it, so multiple bits
; (the player's own, plus each enemy's) have to be checked against one
; saved snapshot rather than by reading the live register again for
; each one.
collision_scratch = $038d

; --- tile types (see this file's own header comment for why these
; exact values, and CHAR_MEM's own layout, matter together) ---
TILE_EMPTY  = 0
TILE_WALL   = 1
TILE_DOT    = 2
TILE_PELLET = 3

; Character codes for the score HUD's own text -- borrowed from the
; character ROM at runtime (see init_text_characters, further down)
; since this program's own custom character set has nothing but the
; four tile graphics above otherwise. Deliberately not the tile types'
; own codes (0-3): these are new, separate codes starting at 4, so
; text and tiles can coexist in the same character memory without
; colliding.
CHAR_DIGIT_0 = 4    ; digits 4-13, in order (CHAR_DIGIT_0+N = digit N)
CHAR_S = 14
CHAR_C = 15
CHAR_O = 16
CHAR_R = 17
CHAR_E = 18

CHAR_MEM  = $3000          ; 2KB: $3000-$37FF
MAZE_GRID = $3800          ; TILE_COLS*TILE_ROWS = 836 bytes
                              ; ($3800-$3B43), right after CHAR_MEM

TILE_COLS = 38
TILE_ROWS = 22

; Where the maze's own top-left tile lands on the 40-column, 25-row
; screen. This now uses nearly the entire screen (a 1-column margin on
; each side horizontally -- MAZE_SCREEN_COL=1, TILE_COLS=38, and
; 1+38+1=40 exactly -- 1 row on top and 2 on the bottom vertically),
; deliberately embracing the player's own maximum possible sprite X
; now exceeding 255 (see this file's own header comment on
; player_rel_x for exactly how far, and PLAYER_SPRITE_X_MSB_BIT below
; for how that's handled) rather than an earlier, smaller version of
; this same maze staying artificially narrow specifically to avoid
; that. The 1 free row at the very top is still a reasonable place for
; a HUD (score/lives) a later stage could add.
MAZE_SCREEN_COL = 1
MAZE_SCREEN_ROW = 1

; The maze's own top-left pixel, in absolute sprite coordinates (see
; this file's own header comment on player_rel_x/player_rel_y for why
; this is added back in only once, in update_sprite_position, rather
; than carried through the rest of the player's own position math).
MAZE_ORIGIN_X = 24 + MAZE_SCREEN_COL*8
MAZE_ORIGIN_Y = 50 + MAZE_SCREEN_ROW*8

MOVE_SPEED = 1                    ; pixels/frame -- easy to retune later

; Enemies move on only 3 out of every 4 frames -- 75% of the player's
; own effective speed, reported directly as feeling a bit too fast at
; a straight 1:1 frame rate. A throttle on how *often* a full
; MOVE_SPEED step happens, not a change to MOVE_SPEED itself: each
; step taken is still a full pixel, frames are skipped instead of
; steps shrunk, avoiding any need for sub-pixel/fractional position
; tracking to get a fractional overall speed. Easy to retune: these
; two constants together say "out of every (MASK+1) frames, skip the
; one where (frame AND MASK) equals SKIP_FRAME" -- e.g. MASK=%00000001,
; SKIP_FRAME=%00000001 would skip 1 of every 2 frames (50% speed,
; noticeably slower than this); MASK=%00000111, SKIP_FRAME=%00000111
; would skip 1 of every 8 (87.5% speed, only slightly slower than
; full).
ENEMY_THROTTLE_MASK = %00000011
ENEMY_THROTTLE_SKIP_FRAME = %00000011

; While frightened, enemies move on only 1 of every 2 frames (half
; their own already-throttled normal speed) -- the classic slow-down
; that's specifically what makes a fleeing enemy actually catchable
; rather than merely a different color, matching this mechanic's own
; well-known convention.
FRIGHTENED_THROTTLE_MASK = %00000001
FRIGHTENED_THROTTLE_SKIP_FRAME = %00000001

; How long (in frames) a power pellet's own vulnerability window
; lasts. 400 frames is roughly 7-8 seconds at a typical PAL/NTSC
; refresh rate -- long enough to be a real, meaningful window without
; needing this program to detect which refresh rate it's actually
; running at.
PELLET_VULNERABLE_DURATION = 400

; Points for eating a frightened enemy -- meaningfully more than a
; power pellet itself (50) or a plain dot (10), since successfully
; turning the tables and catching a fleeing enemy is the whole payoff
; of this mechanic. A flat amount per enemy eaten, not the classic
; escalating 200/400/800/1600 sequence for successive ghosts within
; the same window -- a deliberate simplification, not an oversight.
ENEMY_EATEN_SCORE = 200

; Each enemy's own normal (non-frightened) color, and the shared
; color both use while frightened -- named here so init_enemies (the
; only place these were previously set) and the vulnerability-window
; code (which needs to set and later revert them) share the same
; values rather than each hardcoding its own copy.
ENEMY1_COLOR = 2                  ; red
ENEMY2_COLOR = 4                  ; purple
FRIGHTENED_COLOR = 14             ; light blue

; Enemies' own starting tiles -- compile-time constants for now, the
; same way the player's own start tile was before stage 4 moved it
; into the level file; not part of that file format yet, since this
; stage's own focus is the AI itself, not extending the format again
; (a natural follow-up once a level-authoring tool makes hand-editing
; more of these practical). Both on row 1: open across its entire
; width by construction (the maze generation's own wall blocks start
; at row 3), so these are safe regardless of which columns get picked,
; unlike most other rows where a chosen column could land inside a
; block.
ENEMY1_START_COL = 5
ENEMY1_START_ROW = 1
ENEMY2_START_COL = 32
ENEMY2_START_ROW = 1

; Sprites 1 and 2's own color and data-pointer registers -- not in
; hardware.inc (only sprite 0's own are, since no program before this
; one needed more than a single sprite), so defined directly here
; instead, matching this file's own already-established precedent for
; program-specific additions (the KERNAL disk I/O constants, further
; up: "not every program needs this"). Derived from hardware.inc's own
; SPRITE0_COLOR/SPRITE_PTR0, the same way that file's own header
; comment already derives SPRITE_PTR0 from SCREEN -- the VIC-II's own
; regular per-sprite register spacing (+1 per sprite for color, since
; SPRITE0_COLOR through SPRITE7_COLOR are eight consecutive bytes;
; likewise +1 per sprite for the pointer bytes at the end of screen
; memory), not independently chosen addresses. update_enemy_sprite_
; position itself doesn't need X/Y equivalents of these: it reaches
; any sprite's own X/Y registers directly via indexed addressing off
; SPRITE0_X/SPRITE0_Y instead, since that runs every frame and
; benefits from being generic; these two are only ever needed once
; each, at startup, so naming them directly reads more clearly than
; indexing would for a one-time use.
SPRITE1_COLOR = SPRITE0_COLOR + 1
SPRITE2_COLOR = SPRITE0_COLOR + 2
SPRITE_PTR1 = SPRITE_PTR0 + 1
SPRITE_PTR2 = SPRITE_PTR0 + 2

; VIC-II's own hardware sprite-sprite collision register -- pixel-
; accurate, read-only, bit N set for each sprite involved in an
; overlap this frame; reading it also clears it, ready for the next
; frame. Verified against several independent sources given how
; significant getting a hardware register's own address wrong would
; be, same discipline as this file's own $01/CHAREN and CIA2_PRA work
; already established. Not in hardware.inc: no program before this
; one needed sprite-sprite collision detection at all.
SPRITE_SPRITE_COLLISION = $d01e

; Sprite 0's own bit within SPRITE_X_MSB ($D010) -- bit N holds sprite
; N's X-coordinate 9th bit. Named here (rather than a bare %00000001
; at each call site) since update_sprite_position needs to set AND
; clear this one specific bit without disturbing the other 7 sprites'
; own MSB bits sharing that same register -- exactly the same shared-
; register care hardware.inc's own CIA2_PRA comment already documents
; for a different register, for the same underlying reason.
PLAYER_SPRITE_X_MSB_BIT = %00000001

SPRITE_DATA = $3b80   ; 64-byte aligned (15232 / 64 = 238 exactly),
                          ; right after MAZE_GRID ($3800-$3B43), still
                          ; comfortably inside VIC bank 0 and well
                          ; clear of the $1000-$1FFF/$9000-$9FFF
                          ; character-ROM shadow ranges no sprite data
                          ; can safely occupy

; KERNAL disk I/O -- the same well-known, fixed addresses this
; project's own editor.asm already uses successfully, defined
; directly here rather than in hardware.inc since not every program
; needs disk I/O (editor.asm's own precedent for this).
SETLFS = $ffba
SETNAM = $ffbd
OPEN   = $ffc0
CLOSE  = $ffc3
CHKIN  = $ffc6
CLRCHN = $ffcc
CHRIN  = $ffcf
READST = $ffb7

; Level file format (see load_level's own comment for the full
; layout): a fixed filename, not user-typed, so the ",S,R" (sequential
; file, read mode) suffix editor.asm's own dynamically-typed filenames
; need appended at runtime can just be written directly into this same
; static string instead.
level_filename:
        .text "LEVEL1,S,R"
LEVEL_FILENAME_LEN = 10

start:
        ; Clears the whole screen directly (4 pages of 256 bytes each,
        ; $0400-$07FF -- slightly more than the 1000 bytes screen
        ; memory actually needs, including $07F8-$07FF where
        ; init_sprite will later set SPRITE_PTR0, harmless to
        ; overwrite here since that happens well before init_sprite
        ; runs), before this program writes anything of its own.
        ; Without this, render_maze/render_score only ever write to
        ; the specific cells they actually care about (the maze's own
        ; bounds, and the HUD row) -- any other screen position, like
        ; the single-column margin left of the maze, is never touched
        ; by this program at all, and keeps showing whatever was
        ; already there from the BASIC LOAD/RUN commands actually
        ; typed to start it. Reported directly from real hardware
        ; after running this program normally (typing LOAD and RUN,
        ; not starting from an already-blank screen); not reproducible
        ; in mini6502.py, whose own CHROUT simulation is a captured
        ; text log only and never actually mutates screen memory --
        ; this direct loop, unlike relying on CHROUT's $93 clear-
        ; screen control code, is fully verifiable there instead of
        ; just trusted to work correctly on real hardware.
        ; TILE_EMPTY (0), not the standard PETSCII space code ($20):
        ; this program's own custom character set only has bitmaps
        ; defined for codes 0-18 (see this file's own header comment)
        ; -- code $20 has no defined bitmap here at all, and would
        ; display whatever garbage happens to be in that unused
        ; character memory slot, the exact same class of bug this
        ; whole fix exists to remove.
        lda #TILE_EMPTY
        ldx #$00
@clear_screen_loop:
        sta SCREEN,x
        sta SCREEN+256,x
        sta SCREEN+512,x
        sta SCREEN+768,x
        inx
        bne @clear_screen_loop

        lda CIA2_DDRA
        ora #%00000011
        sta CIA2_DDRA

        lda CIA2_PRA
        and #%11111100
        ora #VIC_BANK_0
        sta CIA2_PRA

        lda #%00011100          ; screen $0400 (bits 7-4 = 1), char
        sta VIC_MEMPTRS            ; memory $3000 (bits 3-1 = 6)

        lda #$00                  ; black background and border --
        sta VIC_BORDER               ; makes the tile graphics the only
        sta VIC_BG0                    ; thing with any brightness to it

        jsr init_tile_characters
        jsr init_text_characters
        jsr load_level
        jsr render_maze
        jsr init_sprite
        jsr init_enemies

        lda #$00
        sta score_lo
        sta score_hi
        jsr render_score

main_loop:
        jsr wait_frame
        jsr update_player
        jsr update_sprite_position
        jsr check_eat_dot
        jsr update_enemies
        jsr update_vulnerability_timer
        jsr check_enemy_collision
        jmp main_loop

; Copies the player sprite's own bitmap (player_sprite_data, further
; down) to SPRITE_DATA, points sprite 0 at it, sets its color, enables
; it, and places it at the player's own starting tile (player_start_
; col/player_start_row, loaded from disk by load_level -- this runs
; after that, not before, so those are already valid by the time
; they're read here). Deliberately not this project's own existing
; SPRITE_INIT macro (graphics.inc): that macro itself would be enough,
; but everything else in graphics.inc comes bundled with it (.include
; splices in the whole file, and its own header comment is explicit
; that nine bounce-movement-specific symbols are required as soon as
; the file's included, whether or not anything in it actually calls
; sprite0_bounce_step) -- symbols this file's own tile-grid collision
; movement has no use for and no natural values to give. Simple,
; direct register writes here avoid that mismatch entirely.
init_sprite:
        ldx #$00
@copy_loop:
        lda player_sprite_data,x
        sta SPRITE_DATA,x
        inx
        cpx #63
        bne @copy_loop

        lda #(SPRITE_DATA / 64)
        sta SPRITE_PTR0
        lda #$01                  ; white
        sta SPRITE0_COLOR
        lda #%00000001
        sta SPRITE_ENABLE

        ; player_rel_x (16-bit) := player_start_col * 8 -- a genuine
        ; 16-bit shift, not an 8-bit one: player_start_col can reach
        ; 37 here, and 37*8=296 doesn't fit in a single byte.
        lda player_start_col
        sta player_rel_x_lo
        lda #$00
        sta player_rel_x_hi
        asl player_rel_x_lo
        rol player_rel_x_hi
        asl player_rel_x_lo
        rol player_rel_x_hi
        asl player_rel_x_lo
        rol player_rel_x_hi

        ; player_rel_y := player_start_row * 8 -- safe as a plain
        ; 8-bit shift, unlike X above: the largest possible result
        ; here, (TILE_ROWS-1)*8, always fits in a single byte
        ; regardless of maze height, the same reason player_rel_y
        ; itself never needed a second byte the way player_rel_x did.
        lda player_start_row
        asl a
        asl a
        asl a
        sta player_rel_y

        lda #$00
        sta player_direction

        jsr update_sprite_position
        rts

; Copies the enemy sprite's own bitmap (enemy_sprite_data, further
; down -- a simple ghost silhouette, deliberately distinct from the
; player's own small circle, both for basic sprite-vs-sprite
; readability during play and because two visually identical chasers
; would read as far less alive than two that actually look like
; something) to SPRITE_DATA's own second and third 64-byte slots
; (SPRITE_DATA itself already holds the player's own shape in the
; first), points sprites 1 and 2 at their own copies, sets their own
; colors and enables them, and places each at its own starting tile
; (ENEMY1_START_COL/ROW, ENEMY2_START_COL/ROW). Mirrors init_sprite's
; own overall shape closely, but isn't built from update_enemies' own
; shared "current enemy" working-set pattern the way the per-frame
; enemy routines are -- this runs once, directly against each enemy's
; own permanent storage, so the extra load/store indirection that
; pattern exists for elsewhere would only add complexity here without
; the reuse benefit that justifies it in update_enemies itself.
init_enemies:
        ldx #$00
@copy_loop:
        lda enemy_sprite_data,x
        sta SPRITE_DATA + 64,x
        lda enemy_sprite_data,x
        sta SPRITE_DATA + 128,x
        inx
        cpx #63
        bne @copy_loop

        lda #((SPRITE_DATA + 64) / 64)
        sta SPRITE_PTR1
        lda #((SPRITE_DATA + 128) / 64)
        sta SPRITE_PTR2
        lda #ENEMY1_COLOR
        sta SPRITE1_COLOR
        lda #ENEMY2_COLOR
        sta SPRITE2_COLOR
        lda SPRITE_ENABLE
        ora #%00000110             ; sprites 1 and 2, alongside
        sta SPRITE_ENABLE            ; whatever sprite 0's own bit
                                         ; already is

        lda #$00
        sta pellet_vulnerable_timer_lo
        sta pellet_vulnerable_timer_hi

        lda #1
        sta enemy_sprite_num
        jsr reset_enemy_to_start
        jsr update_enemy_sprite_position
        lda enemy_rel_x_lo
        sta enemy1_rel_x_lo
        lda enemy_rel_x_hi
        sta enemy1_rel_x_hi
        lda enemy_rel_y
        sta enemy1_rel_y
        lda enemy_direction
        sta enemy1_direction
        lda enemy_frightened
        sta enemy1_frightened

        lda #2
        sta enemy_sprite_num
        jsr reset_enemy_to_start
        jsr update_enemy_sprite_position
        lda enemy_rel_x_lo
        sta enemy2_rel_x_lo
        lda enemy_rel_x_hi
        sta enemy2_rel_x_hi
        lda enemy_rel_y
        sta enemy2_rel_y
        lda enemy_direction
        sta enemy2_direction
        lda enemy_frightened
        sta enemy2_frightened

        rts

; Resets the "current enemy" (keyed by enemy_sprite_num, already set
; by the caller) back to its own starting tile, facing no particular
; direction, and not frightened. Shared by init_enemies (once at
; startup, for both enemies in turn) and eat_enemy1/eat_enemy2
; (mid-game, for whichever one specific enemy the player just caught
; while it was frightened), so there's only one place this logic
; lives rather than two copies that could quietly drift apart.
reset_enemy_to_start:
        lda enemy_sprite_num
        cmp #1
        bne @enemy2
        lda #<(ENEMY1_START_COL * 8)
        sta enemy_rel_x_lo
        lda #>(ENEMY1_START_COL * 8)
        sta enemy_rel_x_hi
        lda #(ENEMY1_START_ROW * 8)
        sta enemy_rel_y
        jmp @done
@enemy2:
        lda #<(ENEMY2_START_COL * 8)
        sta enemy_rel_x_lo
        lda #>(ENEMY2_START_COL * 8)
        sta enemy_rel_x_hi
        lda #(ENEMY2_START_ROW * 8)
        sta enemy_rel_y
@done:
        lda #$00
        sta enemy_direction
        sta enemy_frightened
        rts

; Busy-waits for a raster line near the bottom of the visible display,
; then returns -- a simple polling way to sync the main loop to the
; screen's refresh rate (roughly 50/60 Hz), so movement speed stays
; consistent regardless of how many cycles the rest of the loop
; happens to take. The same raster line and technique this project's
; own graphics.inc uses for its own wait_frame (bounce.asm/pong.asm/
; lander.asm), reimplemented directly here rather than pulled in via
; that file specifically to avoid its own nine required symbols this
; file has no use for -- see init_sprite's own comment for the full
; reasoning.
wait_frame:
        lda VIC_RASTER
        cmp #$fb
        bne wait_frame
        rts

; Returns in A the tile at grid column tile_col, row tile_row (both
; set by the caller before calling this), or TILE_WALL if either is
; out of the maze's own bounds -- treating "off the edge" the same as
; a wall, a defensive fallback the test maze's own full border should
; mean is never actually exercised, but a future maze without one
; shouldn't be able to walk the player off the grid into whatever
; memory happens to follow it.
;
; Computes a full 16-bit pointer into MAZE_GRID (reusing grid_ptr --
; safe since every other routine sharing it, render_maze (startup)
; and check_eat_dot (every frame, reusing exactly the value this
; routine itself leaves grid_ptr holding), runs sequentially, never
; concurrently or reentrantly with this one) rather than an 8-bit
; Y-indexed offset: TILE_ROWS*TILE_COLS is 836 for this maze, which
; doesn't fit in a single byte the way the smaller maze this project
; shipped with first did (200, safely under 256).
get_tile:
        lda tile_col
        cmp #TILE_COLS
        bcs @out_of_bounds
        lda tile_row
        cmp #TILE_ROWS
        bcs @out_of_bounds

        lda tile_row
        jsr compute_row_offset      ; row_offset_lo/hi := tile_row * TILE_COLS

        lda row_offset_lo
        clc
        adc tile_col
        sta grid_ptr
        lda row_offset_hi
        adc #$00
        sta grid_ptr+1

        lda grid_ptr
        clc
        adc #<MAZE_GRID
        sta grid_ptr
        lda grid_ptr+1
        adc #>MAZE_GRID
        sta grid_ptr+1

        ldy #$00
        lda (grid_ptr),y
        rts
@out_of_bounds:
        lda #TILE_WALL
        rts

; row_offset_lo/hi (16-bit) := A * TILE_COLS (38), as (A*32)+(A*4)+
; (A*2) -- a genuine 16-bit result throughout: this maze's own
; TILE_ROWS-1 (21) * 38 = 798, far past what a single byte can hold.
compute_row_offset:
        sta row_offset_lo
        lda #$00
        sta row_offset_hi

        asl row_offset_lo            ; *2
        rol row_offset_hi
        lda row_offset_lo
        sta row_times2_lo
        lda row_offset_hi
        sta row_times2_hi

        asl row_offset_lo            ; *4
        rol row_offset_hi
        lda row_offset_lo
        sta row_times4_lo
        lda row_offset_hi
        sta row_times4_hi

        asl row_offset_lo            ; *8
        rol row_offset_hi
        asl row_offset_lo            ; *16
        rol row_offset_hi
        asl row_offset_lo            ; *32
        rol row_offset_hi

        lda row_offset_lo
        clc
        adc row_times4_lo
        sta row_offset_lo
        lda row_offset_hi
        adc row_times4_hi
        sta row_offset_hi

        lda row_offset_lo
        clc
        adc row_times2_lo
        sta row_offset_lo
        lda row_offset_hi
        adc row_times2_hi
        sta row_offset_hi

        rts

; screen_row_offset_lo/hi (16-bit) := A * 40 (the screen's own row
; stride), as (A*32)+(A*8) -- a genuine 16-bit result, since
; (MAZE_SCREEN_ROW+tile_row) can reach 22 here, and 22*40=880 doesn't
; fit in a single byte.
compute_screen_row_offset:
        sta screen_row_offset_lo
        lda #$00
        sta screen_row_offset_hi

        asl screen_row_offset_lo     ; *2
        rol screen_row_offset_hi
        asl screen_row_offset_lo     ; *4
        rol screen_row_offset_hi
        asl screen_row_offset_lo     ; *8
        rol screen_row_offset_hi
        lda screen_row_offset_lo
        sta screen_row_times8_lo
        lda screen_row_offset_hi
        sta screen_row_times8_hi

        asl screen_row_offset_lo     ; *16
        rol screen_row_offset_hi
        asl screen_row_offset_lo     ; *32
        rol screen_row_offset_hi

        lda screen_row_offset_lo
        clc
        adc screen_row_times8_lo
        sta screen_row_offset_lo
        lda screen_row_offset_hi
        adc screen_row_times8_hi
        sta screen_row_offset_hi
        rts

; Computes the screen memory address of tile (tile_col, tile_row) --
; SCREEN + MAZE_SCREEN_COL (a compile-time constant) + (MAZE_SCREEN_
; ROW+tile_row)*40 + tile_col -- leaving the result in screen_ptr.
compute_tile_screen_addr:
        lda tile_row
        clc
        adc #MAZE_SCREEN_ROW
        jsr compute_screen_row_offset

        lda screen_row_offset_lo
        clc
        adc tile_col
        sta screen_ptr
        lda screen_row_offset_hi
        adc #$00
        sta screen_ptr+1

        lda screen_ptr
        clc
        adc #<(SCREEN + MAZE_SCREEN_COL)
        sta screen_ptr
        lda screen_ptr+1
        adc #>(SCREEN + MAZE_SCREEN_COL)
        sta screen_ptr+1
        rts

; Checks the tile the player is CURRENTLY standing on (not a
; neighboring one -- can_move_direction's own concern) and, if it's a
; dot or power pellet, eats it: clears it in MAZE_GRID and on screen,
; adds to the score, and redraws the score display. Called once per
; main-loop iteration regardless of whether the player is tile-aligned
; -- harmless when it isn't, since get_tile always resolves to
; whichever tile the player's current pixel position falls within, and
; a tile once eaten becomes TILE_EMPTY, so finding it again on a later
; call is simply a no-op rather than double-scoring the same dot.
check_eat_dot:
        lda player_rel_x_lo
        sta x_shift_lo
        lda player_rel_x_hi
        sta x_shift_hi
        lsr x_shift_hi
        ror x_shift_lo
        lsr x_shift_hi
        ror x_shift_lo
        lsr x_shift_hi
        ror x_shift_lo
        lda x_shift_lo
        sta tile_col

        lda player_rel_y
        lsr a
        lsr a
        lsr a
        sta tile_row

        jsr get_tile
        cmp #TILE_DOT
        beq @eat_dot
        cmp #TILE_PELLET
        beq @eat_pellet
        rts

@eat_dot:
        lda #TILE_EMPTY
        ldy #$00
        sta (grid_ptr),y         ; grid_ptr still points at this exact
                                     ; tile, left there by get_tile
        jsr compute_tile_screen_addr
        lda #TILE_EMPTY
        ldy #$00
        sta (screen_ptr),y
        jsr add_dot_score
        jsr render_score
        rts

@eat_pellet:
        lda #TILE_EMPTY
        ldy #$00
        sta (grid_ptr),y
        jsr compute_tile_screen_addr
        lda #TILE_EMPTY
        ldy #$00
        sta (screen_ptr),y
        jsr add_pellet_score
        jsr render_score
        jsr activate_pellet_effect
        rts

; Adds 10 (a plain dot) to the 16-bit score.
add_dot_score:
        lda score_lo
        clc
        adc #10
        sta score_lo
        lda score_hi
        adc #$00
        sta score_hi
        rts

; Adds 50 (a power pellet) to the 16-bit score.
add_pellet_score:
        lda score_lo
        clc
        adc #50
        sta score_lo
        lda score_hi
        adc #$00
        sta score_hi
        rts

; Adds ENEMY_EATEN_SCORE (200) for catching a frightened enemy.
add_enemy_eaten_score:
        lda score_lo
        clc
        adc #ENEMY_EATEN_SCORE
        sta score_lo
        lda score_hi
        adc #$00
        sta score_hi
        rts

; Runs whichever effect a power pellet triggers -- currently just one
; (start_enemy_vulnerability), called directly, but kept as its own
; separate routine rather than inlined into check_eat_dot's own
; @eat_pellet block specifically so a future pellet type (a different
; tile value from TILE_PELLET, dispatched to a different effect
; routine here) doesn't need check_eat_dot's own logic restructured to
; make room for it -- this is the seam a second effect would be added
; at, not check_eat_dot itself.
activate_pellet_effect:
        jsr start_enemy_vulnerability
        rts

; Starts (or refreshes, if one's already running -- eating a second
; pellet while enemies are already frightened resets the full
; duration rather than stacking with or being ignored during the
; first, matching the classic convention this mechanic is drawn from)
; a vulnerability window: both enemies flee and can be eaten for
; PELLET_VULNERABLE_DURATION frames, shown with FRIGHTENED_COLOR.
start_enemy_vulnerability:
        lda #<PELLET_VULNERABLE_DURATION
        sta pellet_vulnerable_timer_lo
        lda #>PELLET_VULNERABLE_DURATION
        sta pellet_vulnerable_timer_hi

        lda #$01
        sta enemy1_frightened
        sta enemy2_frightened

        lda #FRIGHTENED_COLOR
        sta SPRITE1_COLOR
        sta SPRITE2_COLOR
        rts

; Counts pellet_vulnerable_timer down by one every frame while active
; (nonzero); the moment it reaches exactly 0, ends the vulnerability
; window for whichever enemies are still frightened (an enemy already
; eaten during this same window -- and thus no longer frightened --
; is correctly left alone, still displaying its own normal color from
; whenever it was eaten, not reverted a second time here) by clearing
; their own frightened flags and restoring their own normal colors.
update_vulnerability_timer:
        lda pellet_vulnerable_timer_lo
        ora pellet_vulnerable_timer_hi
        beq @done                  ; already inactive -- nothing to
                                        ; count down

        lda pellet_vulnerable_timer_lo
        bne @dec_lo
        dec pellet_vulnerable_timer_hi
@dec_lo:
        dec pellet_vulnerable_timer_lo

        lda pellet_vulnerable_timer_lo
        ora pellet_vulnerable_timer_hi
        bne @done                  ; still counting down

        lda #$00
        sta enemy1_frightened
        sta enemy2_frightened
        lda #ENEMY1_COLOR
        sta SPRITE1_COLOR
        lda #ENEMY2_COLOR
        sta SPRITE2_COLOR
@done:
        rts

; Called when the player touches enemy 1 while it's frightened:
; resets it back to its own starting tile (reset_enemy_to_start, the
; same routine init_enemies itself already uses) and updates its own
; sprite position/color immediately, rather than waiting for this
; frame's own update_enemies call (which already ran earlier this
; frame) to notice -- without this, the just-caught enemy would still
; show its own frightened position/color for one more visible frame.
eat_enemy1:
        lda enemy1_rel_x_lo
        sta enemy_rel_x_lo
        lda enemy1_rel_x_hi
        sta enemy_rel_x_hi
        lda enemy1_rel_y
        sta enemy_rel_y
        lda enemy1_direction
        sta enemy_direction
        lda #1
        sta enemy_sprite_num

        jsr reset_enemy_to_start
        lda #ENEMY1_COLOR
        sta SPRITE1_COLOR
        jsr update_enemy_sprite_position

        lda enemy_rel_x_lo
        sta enemy1_rel_x_lo
        lda enemy_rel_x_hi
        sta enemy1_rel_x_hi
        lda enemy_rel_y
        sta enemy1_rel_y
        lda enemy_direction
        sta enemy1_direction
        lda enemy_frightened
        sta enemy1_frightened

        jsr add_enemy_eaten_score
        jmp render_score

; The same as eat_enemy1, but for enemy 2.
eat_enemy2:
        lda enemy2_rel_x_lo
        sta enemy_rel_x_lo
        lda enemy2_rel_x_hi
        sta enemy_rel_x_hi
        lda enemy2_rel_y
        sta enemy_rel_y
        lda enemy2_direction
        sta enemy_direction
        lda #2
        sta enemy_sprite_num

        jsr reset_enemy_to_start
        lda #ENEMY2_COLOR
        sta SPRITE2_COLOR
        jsr update_enemy_sprite_position

        lda enemy_rel_x_lo
        sta enemy2_rel_x_lo
        lda enemy_rel_x_hi
        sta enemy2_rel_x_hi
        lda enemy_rel_y
        sta enemy2_rel_y
        lda enemy_direction
        sta enemy2_direction
        lda enemy_frightened
        sta enemy2_frightened

        jsr add_enemy_eaten_score
        jmp render_score

; Subtracts the 16-bit constant in (subtract_amount_lo,
; subtract_amount_hi) from score_scratch_lo/hi repeatedly, counting
; how many subtractions succeed before it would go negative -- that
; count (0-9 for every power of ten render_score actually uses) is the
; next decimal digit, left in A.
extract_digit:
        ldx #$00
@loop:
        lda score_scratch_hi
        cmp subtract_amount_hi
        bcc @done                  ; scratch_hi < amount_hi: too small
        bne @do_subtract           ; scratch_hi > amount_hi: big enough
        lda score_scratch_lo
        cmp subtract_amount_lo
        bcc @done                  ; equal hi, but lo < amount_lo
@do_subtract:
        lda score_scratch_lo
        sec
        sbc subtract_amount_lo
        sta score_scratch_lo
        lda score_scratch_hi
        sbc subtract_amount_hi
        sta score_scratch_hi
        inx
        jmp @loop
@done:
        txa
        rts

; Renders "SCORE " followed by the 16-bit score as 5 decimal digits
; (00000-65535, always all 5 -- no leading-zero suppression, so the
; digit count never shifts around as the score changes) at row 0,
; starting at MAZE_SCREEN_COL, the same column the maze itself starts
; at.
render_score:
        lda #<(SCREEN + MAZE_SCREEN_COL)
        sta screen_ptr
        lda #>(SCREEN + MAZE_SCREEN_COL)
        sta screen_ptr+1

        ldy #$00
        lda #CHAR_S
        sta (screen_ptr),y
        iny
        lda #CHAR_C
        sta (screen_ptr),y
        iny
        lda #CHAR_O
        sta (screen_ptr),y
        iny
        lda #CHAR_R
        sta (screen_ptr),y
        iny
        lda #CHAR_E
        sta (screen_ptr),y
        iny
        lda #TILE_EMPTY
        sta (screen_ptr),y
        iny

        lda score_lo
        sta score_scratch_lo
        lda score_hi
        sta score_scratch_hi

        lda #<10000
        sta subtract_amount_lo
        lda #>10000
        sta subtract_amount_hi
        jsr extract_digit
        clc
        adc #CHAR_DIGIT_0
        sta (screen_ptr),y
        iny

        lda #<1000
        sta subtract_amount_lo
        lda #>1000
        sta subtract_amount_hi
        jsr extract_digit
        clc
        adc #CHAR_DIGIT_0
        sta (screen_ptr),y
        iny

        lda #<100
        sta subtract_amount_lo
        lda #>100
        sta subtract_amount_hi
        jsr extract_digit
        clc
        adc #CHAR_DIGIT_0
        sta (screen_ptr),y
        iny

        lda #<10
        sta subtract_amount_lo
        lda #>10
        sta subtract_amount_hi
        jsr extract_digit
        clc
        adc #CHAR_DIGIT_0
        sta (screen_ptr),y
        iny

        lda #<1
        sta subtract_amount_lo
        lda #>1
        sta subtract_amount_hi
        jsr extract_digit
        clc
        adc #CHAR_DIGIT_0
        sta (screen_ptr),y

        rts

; Checks whether the player could move one tile in direction A (1=up,
; 2=down, 3=left, 4=right) from its own CURRENT tile (derived fresh
; from player_rel_x/player_rel_y, not any separately-tracked tile
; position, so there's only ever one source of truth for where the
; player actually is) -- returns with the zero flag SET if blocked (a
; wall or the maze's own edge), CLEAR if open. A pure query: doesn't
; move anything, and leaves tile_col/tile_row holding whichever
; neighboring tile it just checked (update_player relies on this,
; rather than recomputing the same thing itself, right after deciding
; to actually move there).
can_move_direction:
        sta can_move_dir_scratch

        ; tile_col := player_rel_x (16-bit) / 8, via a 3-position
        ; 16-bit right shift on a scratch copy -- player_rel_x itself
        ; is untouched, and the result always fits in a single byte
        ; (max 37), so only x_shift_lo is read back afterward.
        lda player_rel_x_lo
        sta x_shift_lo
        lda player_rel_x_hi
        sta x_shift_hi
        lsr x_shift_hi
        ror x_shift_lo
        lsr x_shift_hi
        ror x_shift_lo
        lsr x_shift_hi
        ror x_shift_lo
        lda x_shift_lo
        sta tile_col

        lda player_rel_y
        lsr a
        lsr a
        lsr a
        sta tile_row

        lda can_move_dir_scratch
        cmp #1
        bne @try_down
        dec tile_row
        jmp @check
@try_down:
        cmp #2
        bne @try_left
        inc tile_row
        jmp @check
@try_left:
        cmp #3
        bne @must_be_right
        dec tile_col
        jmp @check
@must_be_right:
        inc tile_col
@check:
        jsr get_tile
        cmp #TILE_WALL
        rts

; Mirrors can_move_direction exactly -- same contract (A is the
; direction to check; returns with the zero flag set if blocked, clear
; if open, leaving tile_col/tile_row holding the checked neighbor
; tile) -- just reading enemy_rel_x/enemy_rel_y (the "current enemy"
; working set update_enemies loads before calling this, further down)
; instead of player_rel_x/player_rel_y. Kept as its own, separate
; routine rather than adding a parameter to can_move_direction itself,
; to avoid any risk of disturbing that already-proven routine while
; adding this. Safely reuses can_move_direction's own scratch
; (x_shift_lo/x_shift_hi, can_move_dir_scratch, tile_col/tile_row):
; update_player (which uses these for the player) always fully
; completes before update_enemies (which uses them here, for the
; enemies) ever starts within the same frame, never interleaved or
; reentrant.
can_enemy_move_direction:
        sta can_move_dir_scratch

        lda enemy_rel_x_lo
        sta x_shift_lo
        lda enemy_rel_x_hi
        sta x_shift_hi
        lsr x_shift_hi
        ror x_shift_lo
        lsr x_shift_hi
        ror x_shift_lo
        lsr x_shift_hi
        ror x_shift_lo
        lda x_shift_lo
        sta tile_col

        lda enemy_rel_y
        lsr a
        lsr a
        lsr a
        sta tile_row

        lda can_move_dir_scratch
        cmp #1
        bne @try_down
        dec tile_row
        jmp @check
@try_down:
        cmp #2
        bne @try_left
        inc tile_row
        jmp @check
@try_left:
        cmp #3
        bne @must_be_right
        dec tile_col
        jmp @check
@must_be_right:
        inc tile_col
@check:
        jsr get_tile
        cmp #TILE_WALL
        rts

; Called right after can_enemy_move_direction confirms a candidate
; direction is open, with tile_col/tile_row already holding that
; candidate's own tile (can_enemy_move_direction's own contract) and
; the direction just checked still sitting in can_move_dir_scratch.
; Computes that candidate's own Manhattan distance to the player's
; own tile (player_tile_col/player_tile_row, computed once per frame
; by update_enemies) -- |dcol|+|drow| rather than true Euclidean
; distance, needing only subtraction and a sign flip, not
; multiplication or a square root -- and keeps it, and the direction
; that reaches it, if it's better than the best one found so far this
; call. "Better" depends on enemy_frightened: normally the closest
; candidate wins (chasing), but while frightened the *farthest* one
; does instead (fleeing) -- the same distance calculation either way,
; just compared in the opposite direction.
;
; The first candidate found this call is always accepted outright,
; via enemy_found_candidate, rather than compared against a fixed
; starting sentinel for enemy_best_dist: a sentinel chosen to always
; lose when minimizing (a very large number) would incorrectly reject
; a valid distance-0 candidate when maximizing instead, since 0 isn't
; greater than a sentinel of 0 -- an edge case worth avoiding
; outright rather than special-casing.
consider_enemy_candidate:
        lda tile_col
        sec
        sbc player_tile_col
        bcs @col_positive
        eor #$ff                  ; two's-complement negate (no
        clc                          ; direct NEG on 6502): flips a
        adc #$01                     ; negative difference positive,
@col_positive:                       ; the same effect as abs()
        sta enemy_dist_scratch

        lda tile_row
        sec
        sbc player_tile_row
        bcs @row_positive
        eor #$ff
        clc
        adc #$01
@row_positive:
        clc
        adc enemy_dist_scratch     ; total Manhattan distance
        sta enemy_dist_scratch     ; overwrite with the final total --
                                       ; its own earlier, intermediate
                                       ; value is no longer needed, and
                                       ; this keeps it available across
                                       ; the branches below without a
                                       ; second scratch byte

        lda enemy_found_candidate
        beq @accept                 ; nothing found yet this call --
                                        ; always accept the first one

        lda enemy_frightened
        beq @seeking_min

        lda enemy_dist_scratch      ; fleeing: keep this candidate
        cmp enemy_best_dist            ; only if it's strictly farther
        bcc @not_better                 ; than the current best
        beq @not_better
        jmp @accept

@seeking_min:
        lda enemy_dist_scratch      ; chasing: keep this candidate
        cmp enemy_best_dist            ; only if it's strictly closer
        bcs @not_better                 ; than the current best

@accept:
        lda enemy_dist_scratch
        sta enemy_best_dist
        lda can_move_dir_scratch
        sta enemy_best_direction
        lda #$01
        sta enemy_found_candidate
@not_better:
        rts

; Picks the best direction for the "current enemy" (enemy_rel_x/
; enemy_rel_y/enemy_direction/enemy_frightened, loaded by update_
; enemies before calling this) to move in next, from among the up to
; 4 open neighboring tiles -- whichever one is closest to the
; player's own current tile normally (chasing), or farthest while
; frightened (fleeing), per consider_enemy_candidate's own distance
; check either way. Won't reverse the enemy's own current direction
; unless every other direction is blocked (a dead end): without this,
; an enemy sitting exactly between two equally-good candidate tiles
; would flicker back and forth between them forever rather than
; actually committing to progress.
decide_enemy_direction:
        lda #$00
        sta enemy_found_candidate
        sta enemy_best_direction
        sta enemy_best_dist

        lda enemy_direction         ; try up (1) -- skip if already
        cmp #2                         ; moving down (2), up's own
        beq @skip_up                   ; reverse
        lda #1
        jsr can_enemy_move_direction
        beq @skip_up
        jsr consider_enemy_candidate
@skip_up:

        lda enemy_direction         ; try down (2) -- skip if already
        cmp #1                         ; moving up (1)
        beq @skip_down
        lda #2
        jsr can_enemy_move_direction
        beq @skip_down
        jsr consider_enemy_candidate
@skip_down:

        lda enemy_direction         ; try left (3) -- skip if already
        cmp #4                         ; moving right (4)
        beq @skip_left
        lda #3
        jsr can_enemy_move_direction
        beq @skip_left
        jsr consider_enemy_candidate
@skip_left:

        lda enemy_direction         ; try right (4) -- skip if already
        cmp #3                         ; moving left (3)
        beq @skip_right
        lda #4
        jsr can_enemy_move_direction
        beq @skip_right
        jsr consider_enemy_candidate
@skip_right:

        lda enemy_found_candidate
        beq @dead_end
        jmp @have_direction
@dead_end:

        ; every non-reversing direction was blocked -- a dead end.
        ; the only way back out is the reverse of the enemy's own
        ; current direction, which must be open (that's the tile it
        ; just came from) -- picked directly, no distance comparison
        ; needed since it's the only option left
        lda enemy_direction
        cmp #1
        bne @rev_try_down
        lda #2
        jmp @have_reverse
@rev_try_down:
        cmp #2
        bne @rev_try_left
        lda #1
        jmp @have_reverse
@rev_try_left:
        cmp #3
        bne @rev_must_be_right
        lda #4
        jmp @have_reverse
@rev_must_be_right:
        lda #3
@have_reverse:
        sta enemy_best_direction

@have_direction:
        lda enemy_best_direction
        sta enemy_direction
        rts

; Moves the "current enemy" MOVE_SPEED pixels along enemy_direction --
; mirrors update_player's own @move_step block exactly (including the
; same 16-bit increment/decrement carry-into-the-high-byte handling
; for X), just reading and writing enemy_rel_x/enemy_rel_y instead of
; player_rel_x/player_rel_y.
move_enemy_step:
        lda enemy_direction
        beq @done
        cmp #1
        bne @try_down
        dec enemy_rel_y
        jmp @done
@try_down:
        cmp #2
        bne @try_left
        inc enemy_rel_y
        jmp @done
@try_left:
        cmp #3
        bne @must_be_right
        lda enemy_rel_x_lo
        bne @no_borrow
        dec enemy_rel_x_hi
@no_borrow:
        dec enemy_rel_x_lo
        jmp @done
@must_be_right:
        inc enemy_rel_x_lo
        bne @done
        inc enemy_rel_x_hi
@done:
        rts

; Updates the "current enemy" for one frame: if tile-aligned, lets the
; AI pick a fresh direction (decide_enemy_direction), then moves
; MOVE_SPEED pixels along whatever direction is now current. Mirrors
; update_player's own overall shape (check alignment, maybe change
; direction, then move), but the enemy always re-decides at every
; alignment point rather than only changing direction when a new one
; is specifically requested -- there's no "input" for an enemy to hold
; steady between decisions the way a player's own joystick/keyboard
; state persists from frame to frame.
update_enemy_ai_and_move:
        lda enemy_rel_x_lo          ; only the low byte's low 3 bits
        and #%00000111                 ; matter for alignment, the
        bne @skip_decide                ; same reasoning update_
                                            ; player's own alignment
                                            ; check already documents
        lda enemy_rel_y
        and #%00000111
        bne @skip_decide

        jsr decide_enemy_direction

@skip_decide:
        jsr move_enemy_step
        rts

; Converts the "current enemy" (enemy_rel_x/enemy_rel_y, loaded by
; update_enemies before calling this) to absolute sprite coordinates
; and writes them to whichever VIC-II sprite enemy_sprite_num
; indicates. Mirrors update_sprite_position closely (see that
; routine's own comment for the full reasoning behind the 16-bit X
; computation and the MSB-bit handling), generalized here to target
; any sprite via indexed addressing (SPRITEn_X/SPRITEn_Y = SPRITE0_X/
; SPRITE0_Y + n*2 -- the VIC-II's own regular per-sprite register
; spacing) and a small lookup table for the correct single bit of
; SPRITE_X_MSB, rather than the single hardcoded bit player-only
; update_sprite_position can afford to use directly.
update_enemy_sprite_position:
        lda enemy_rel_x_lo
        clc
        adc #<MAZE_ORIGIN_X
        sta enemy_sprite_x_lo
        lda enemy_rel_x_hi
        adc #>MAZE_ORIGIN_X
        sta enemy_sprite_x_hi

        lda enemy_sprite_num
        asl a                       ; sprite register offset: n*2
        tax

        lda enemy_sprite_x_lo
        sta SPRITE0_X,x

        ldy enemy_sprite_num
        lda SPRITE_X_MSB
        and enemy_msb_clear_masks,y
        sta SPRITE_X_MSB
        lda enemy_sprite_x_hi
        beq @msb_clear
        lda SPRITE_X_MSB
        ora enemy_msb_set_masks,y
        sta SPRITE_X_MSB
@msb_clear:

        lda enemy_rel_y
        clc
        adc #MAZE_ORIGIN_Y
        sta SPRITE0_Y,x
        rts

; Lookup tables for update_enemy_sprite_position's own SPRITE_X_MSB
; bit handling, indexed directly by sprite number (0-2 -- only 1 and 2
; are ever actually used here, sprite 0 being the player's own, handled
; separately by update_sprite_position itself, but the table covers
; index 0 too for a clean, direct index rather than an off-by-one
; adjustment on every read). Spelled out directly, not computed as
; each other's bitwise complement, since c64asm has no bitwise-NOT
; operator (update_sprite_position's own comment already establishes
; this same point for its own single hardcoded bit).
enemy_msb_set_masks:
        .byte %00000001, %00000010, %00000100
enemy_msb_clear_masks:
        .byte %11111110, %11111101, %11111011

; Drives both enemies through one frame's own AI, movement, and
; sprite-position update: computes the player's own current tile once
; (player_tile_col/player_tile_row -- shared by both enemies' own
; decide_enemy_direction calls via consider_enemy_candidate, rather
; than recomputed twice), then for each enemy in turn, loads its own
; stored state into the shared "current enemy" working set, runs the
; generic per-enemy update, and writes back whatever changed to that
; enemy's own permanent storage. The actual AI/movement step itself
; (not the sprite-position update, which always runs, keeping
; whatever position an enemy already has correctly drawn even on a
; throttled frame) is skipped on 1 out of every (MASK+1) frames, using
; ENEMY_THROTTLE_MASK/SKIP_FRAME normally or the slower FRIGHTENED_
; THROTTLE_MASK/SKIP_FRAME pair while that particular enemy is
; frightened -- see those constants' own comments for the full
; reasoning.
update_enemies:
        inc enemy_move_throttle

        lda player_rel_x_lo
        sta x_shift_lo
        lda player_rel_x_hi
        sta x_shift_hi
        lsr x_shift_hi
        ror x_shift_lo
        lsr x_shift_hi
        ror x_shift_lo
        lsr x_shift_hi
        ror x_shift_lo
        lda x_shift_lo
        sta player_tile_col

        lda player_rel_y
        lsr a
        lsr a
        lsr a
        sta player_tile_row

        lda enemy1_rel_x_lo
        sta enemy_rel_x_lo
        lda enemy1_rel_x_hi
        sta enemy_rel_x_hi
        lda enemy1_rel_y
        sta enemy_rel_y
        lda enemy1_direction
        sta enemy_direction
        lda enemy1_frightened
        sta enemy_frightened
        lda #1
        sta enemy_sprite_num

        lda enemy_frightened
        beq @normal_throttle_1
        lda #FRIGHTENED_THROTTLE_MASK
        sta enemy_throttle_mask_scratch
        lda #FRIGHTENED_THROTTLE_SKIP_FRAME
        sta enemy_throttle_skip_scratch
        jmp @throttle_check_1
@normal_throttle_1:
        lda #ENEMY_THROTTLE_MASK
        sta enemy_throttle_mask_scratch
        lda #ENEMY_THROTTLE_SKIP_FRAME
        sta enemy_throttle_skip_scratch
@throttle_check_1:
        lda enemy_move_throttle
        and enemy_throttle_mask_scratch
        cmp enemy_throttle_skip_scratch
        beq @skip_move_1
        jsr update_enemy_ai_and_move
@skip_move_1:
        jsr update_enemy_sprite_position

        lda enemy_rel_x_lo
        sta enemy1_rel_x_lo
        lda enemy_rel_x_hi
        sta enemy1_rel_x_hi
        lda enemy_rel_y
        sta enemy1_rel_y
        lda enemy_direction
        sta enemy1_direction
        lda enemy_frightened
        sta enemy1_frightened

        lda enemy2_rel_x_lo
        sta enemy_rel_x_lo
        lda enemy2_rel_x_hi
        sta enemy_rel_x_hi
        lda enemy2_rel_y
        sta enemy_rel_y
        lda enemy2_direction
        sta enemy_direction
        lda enemy2_frightened
        sta enemy_frightened
        lda #2
        sta enemy_sprite_num

        lda enemy_frightened
        beq @normal_throttle_2
        lda #FRIGHTENED_THROTTLE_MASK
        sta enemy_throttle_mask_scratch
        lda #FRIGHTENED_THROTTLE_SKIP_FRAME
        sta enemy_throttle_skip_scratch
        jmp @throttle_check_2
@normal_throttle_2:
        lda #ENEMY_THROTTLE_MASK
        sta enemy_throttle_mask_scratch
        lda #ENEMY_THROTTLE_SKIP_FRAME
        sta enemy_throttle_skip_scratch
@throttle_check_2:
        lda enemy_move_throttle
        and enemy_throttle_mask_scratch
        cmp enemy_throttle_skip_scratch
        beq @skip_move_2
        jsr update_enemy_ai_and_move
@skip_move_2:
        jsr update_enemy_sprite_position

        lda enemy_rel_x_lo
        sta enemy2_rel_x_lo
        lda enemy_rel_x_hi
        sta enemy2_rel_x_hi
        lda enemy_rel_y
        sta enemy2_rel_y
        lda enemy_direction
        sta enemy2_direction
        lda enemy_frightened
        sta enemy2_frightened

        rts

; Checks whether the player (sprite 0, bit 0 of SPRITE_SPRITE_
; COLLISION) touched anything this frame -- reading that register both
; returns which sprites were involved and clears it for the next
; frame, standard, well-documented VIC-II behavior. Doesn't
; distinguish which enemy touched the player (bit 1 vs bit 2), since
; it doesn't matter yet -- either resets the player the same way --
; and ignores bits 1/2 colliding with each other without bit 0, since
; two enemies running into one another isn't something this stage
; does anything about.
;
; On a hit, resets the player back to its own starting tile by simply
; calling init_sprite again -- the exact same setup it already
; performs once at startup (re-copying the player's own sprite data
; and re-setting SPRITE_PTR0/color/enable is unnecessary work since
; none of that ever changes, but harmless, and reusing this already-
; proven routine directly avoids a second, parallel "reset player
; position" routine that could drift out of sync with it over time).
; Checks whether the player (sprite 0, bit 0 of SPRITE_SPRITE_
; COLLISION) touched anything this frame -- reading that register both
; returns which sprites were involved and clears it for the next
; frame, standard, well-documented VIC-II behavior; saved into
; collision_scratch immediately since a second read (checking a later
; bit) would otherwise just see it already cleared. If the player
; touched a frightened enemy, that enemy is eaten instead of the
; player being caught -- checked independently per enemy (bit 1 for
; enemy 1, bit 2 for enemy 2), so touching one frightened and one
; normal enemy in the same frame (an unlikely but possible tie) still
; correctly catches the player via the normal one, without skipping
; the eaten check for the frightened one first.
check_enemy_collision:
        lda SPRITE_SPRITE_COLLISION
        sta collision_scratch

        lda collision_scratch
        and #%00000001
        beq @done                  ; player wasn't involved at all

        lda collision_scratch
        and #%00000010
        beq @check_enemy2
        lda enemy1_frightened
        beq @player_caught         ; touched enemy 1, not frightened
        jsr eat_enemy1

@check_enemy2:
        lda collision_scratch
        and #%00000100
        beq @done
        lda enemy2_frightened
        beq @player_caught         ; touched enemy 2, not frightened
        jsr eat_enemy2
        jmp @done

@player_caught:
        jsr init_sprite

@done:
        rts

; Reads the joystick, commits to a new direction at an intersection if
; one's both requested and open, confirms the current direction (quite
; possibly just changed) is still open, and finally moves the player
; MOVE_SPEED pixels along it if so -- see this file's own header
; comment for the full movement model this implements.
update_player:
        lda player_rel_x_lo    ; only the low byte's low 3 bits matter
                                   ; for alignment -- 8 is a power of 2,
                                   ; so the high byte can never affect
                                   ; whether the low 3 bits are clear
        and #%00000111
        beq @check_y_aligned
        jmp @move_step
@check_y_aligned:
        lda player_rel_y
        and #%00000111
        beq @aligned
        jmp @move_step
@aligned:

        ; tile-aligned -- a turn can only be committed here
        jsr read_joy2
        sta joy_state

        ; also check WASD -- combined with the joystick via OR, not a
        ; replacement for it, so either input method works and
        ; neither can accidentally mask the other; read_joy2 and
        ; READ_KEY are each documented as safe to call in either
        ; order within the same frame (input.inc's own header
        ; comment), so no special sequencing is needed here beyond
        ; what's already written below
        READ_KEY KEY_W_COL, KEY_W_ROW
        beq @not_w
        lda joy_state
        ora #%00000001
        sta joy_state
@not_w:
        READ_KEY KEY_S_COL, KEY_S_ROW
        beq @not_s
        lda joy_state
        ora #%00000010
        sta joy_state
@not_s:
        READ_KEY KEY_A_COL, KEY_A_ROW
        beq @not_a
        lda joy_state
        ora #%00000100
        sta joy_state
@not_a:
        READ_KEY KEY_D_COL, KEY_D_ROW
        beq @not_d
        lda joy_state
        ora #%00001000
        sta joy_state
@not_d:

        lda joy_state
        and #%00000001          ; up
        beq @check_req_down
        lda #1
        jsr can_move_direction
        beq @check_req_down       ; blocked -- fall through to the next
        lda #1
        sta player_direction
        jmp @direction_set
@check_req_down:
        lda joy_state
        and #%00000010          ; down
        beq @check_req_left
        lda #2
        jsr can_move_direction
        beq @check_req_left
        lda #2
        sta player_direction
        jmp @direction_set
@check_req_left:
        lda joy_state
        and #%00000100          ; left
        beq @check_req_right
        lda #3
        jsr can_move_direction
        beq @check_req_right
        lda #3
        sta player_direction
        jmp @direction_set
@check_req_right:
        lda joy_state
        and #%00001000          ; right
        beq @direction_set
        lda #4
        jsr can_move_direction
        beq @direction_set
        lda #4
        sta player_direction

@direction_set:
        lda player_direction
        beq @move_step             ; already stopped -- nothing to confirm
        jsr can_move_direction
        bne @move_step               ; still open -- proceed
        lda #$00
        sta player_direction

@move_step:
        lda player_direction
        beq @done
        cmp #1
        bne @try_down
        dec player_rel_y
        jmp @done
@try_down:
        cmp #2
        bne @try_left
        inc player_rel_y
        jmp @done
@try_left:
        cmp #3
        bne @must_be_right
        ; 16-bit decrement: borrow into the high byte only when the
        ; low byte is about to wrap from 0 to 255
        lda player_rel_x_lo
        bne @no_borrow
        dec player_rel_x_hi
@no_borrow:
        dec player_rel_x_lo
        jmp @done
@must_be_right:
        ; 16-bit increment: carry into the high byte only when the low
        ; byte just wrapped from 255 to 0
        inc player_rel_x_lo
        bne @done
        inc player_rel_x_hi
@done:
        rts

; Converts the player's own maze-relative position (player_rel_x/
; player_rel_y) to absolute sprite coordinates and writes them --
; see this file's own header comment for why this addition happens
; only here, once, rather than being carried through the rest of the
; player's own position math.
;
; X is a genuine 16-bit computation (sprite_x_lo/sprite_x_hi), since
; MAZE_ORIGIN_X + player_rel_x can exceed 255 here (see this file's
; own header comment for the exact threshold) -- SPRITE0_X gets the
; low byte, and PLAYER_SPRITE_X_MSB_BIT (SPRITE_X_MSB's own bit for
; sprite 0) gets set or cleared based on whether the high byte is
; nonzero, touching only that one bit so the other 7 sprites' own MSB
; bits sharing that same register are never disturbed.
update_sprite_position:
        lda player_rel_x_lo
        clc
        adc #<MAZE_ORIGIN_X
        sta sprite_x_lo
        lda player_rel_x_hi
        adc #>MAZE_ORIGIN_X
        sta sprite_x_hi

        lda sprite_x_lo
        sta SPRITE0_X

        lda SPRITE_X_MSB
        and #%11111110          ; clear bit 0 (PLAYER_SPRITE_X_MSB_BIT)
                                    ; -- hardcoded, not "~
                                    ; PLAYER_SPRITE_X_MSB_BIT", since
                                    ; this assembler has no bitwise-NOT
                                    ; operator (c64asm-reference.md's
                                    ; own expression table lists only
                                    ; +, -, *, /, and unary </>)
        sta SPRITE_X_MSB
        lda sprite_x_hi
        beq @msb_clear
        lda SPRITE_X_MSB
        ora #PLAYER_SPRITE_X_MSB_BIT
        sta SPRITE_X_MSB
@msb_clear:

        lda player_rel_y
        clc
        adc #MAZE_ORIGIN_Y
        sta SPRITE0_Y
        rts

; A small, roughly circular shape (~8-pixel diameter), anchored at the
; sprite's own (0,0) origin -- deliberately: this is the same origin
; update_sprite_position writes to, and the same origin can_move_
; direction/get_tile treat as "the player's own tile," so the visible
; shape has to actually sit there too, not just be small. An earlier
; version of this same sprite was small but centered in the middle of
; the full 24x21 canvas instead, which put the visible pixels a good
; 6-7 pixels right and down from the sprite's own (X,Y) -- almost a
; whole tile's worth of offset, on an 8x8 tile. The collision system
; was never actually wrong: it always used the sprite's own real
; coordinate correctly. What was wrong was that the drawn shape didn't
; visually line up with what that coordinate meant, so the collision
; boundary looked shifted by however far off-center the circle
; happened to be -- confirmed directly by computing where the earlier
; bitmap's own lit pixels actually sat before touching anything here.
player_sprite_data:
        .byte %00011000, %00000000, %00000000
        .byte %01111110, %00000000, %00000000
        .byte %01111110, %00000000, %00000000
        .byte %11111111, %00000000, %00000000
        .byte %11111111, %00000000, %00000000
        .byte %01111110, %00000000, %00000000
        .byte %01111110, %00000000, %00000000
        .byte %00011000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000

; A simple ghost silhouette (rounded top, scalloped bottom edge),
; anchored at the sprite's own (0,0) origin the same way player_
; sprite_data already is -- see that data's own comment for why this
; matters (the collision system's own coordinate is that origin, so a
; visible shape drawn away from it looks visibly shifted from where
; collision actually happens). Deliberately distinct from the
; player's own small circle, not just recolored the same shape: two
; visually identical sprites chasing the player would read as far
; less alive than something actually shaped like an adversary.
enemy_sprite_data:
        .byte %00111100, %00000000, %00000000
        .byte %01111110, %00000000, %00000000
        .byte %11111111, %00000000, %00000000
        .byte %11111111, %00000000, %00000000
        .byte %11111111, %00000000, %00000000
        .byte %11111111, %00000000, %00000000
        .byte %10101010, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000

; Copies the four tile character bitmaps (8 bytes each, 32 bytes
; total) into character memory at CHAR_MEM, at character codes 0-3 --
; TILE_EMPTY/TILE_WALL/TILE_DOT/TILE_PELLET exactly, so render_maze
; can write the grid's own tile values straight to screen memory with
; no translation.
init_tile_characters:
        ldx #$00
@loop:
        lda tile_bitmaps,x
        sta CHAR_MEM,x
        inx
        cpx #32
        bne @loop
        rts

; Copies digit glyphs (0-9) and the 5 letters needed for the score
; HUD's own "SCORE" label from the character ROM into this program's
; own custom character memory, at character codes 4 and up (0-3 are
; already the tile graphics). This program's own character set has
; nothing else in it -- switching VIC_MEMPTRS to custom RAM (stage 1)
; means the KERNAL's own font is nowhere the VIC-II can see it, at any
; character code.
;
; The character ROM shares the CPU's own $D000-$DFFF address range
; with I/O -- normally I/O is what's visible there (CHAREN, bit 2 of
; $01, set). Clearing that one bit exposes the ROM to the CPU instead,
; confirmed against several independent sources given how significant
; getting this wrong would be: with I/O hidden, the VIC-II/SID/CIA
; registers this program depends on everywhere else aren't reachable
; either, and the KERNAL's own IRQ handler (firing roughly 60 times a
; second for the jiffy clock and keyboard scan) lives in exactly the
; memory this temporarily replaces -- interrupts are disabled for the
; whole window specifically because that handler would crash or
; misbehave if it fired mid-copy, not as a general precaution.
init_text_characters:
        sei

        lda $01
        and #%11111011          ; clear CHAREN -- character ROM
        sta $01                    ; visible at $D000-$DFFF instead of I/O

        ; digits 0-9: contiguous in both the ROM's own source (screen
        ; codes 48-57) and this program's own destination (character
        ; codes 4-13), so copied as one 80-byte run rather than ten
        ; separate ones.
        ldx #$00
@digit_loop:
        lda $d000 + 48*8,x
        sta CHAR_MEM + CHAR_DIGIT_0*8,x
        inx
        cpx #(10*8)
        bne @digit_loop

        ; S, C, O, R, E -- scattered in the ROM's own screen-code
        ; layout (19, 3, 15, 18, 5 respectively), so copied
        ; individually rather than as one run. Screen code 0 is '@',
        ; not 'A' -- confirmed against multiple independent sources
        ; given a first version of this same routine got this exact
        ; point wrong, using each letter's own alphabet position
        ; directly (A=0, B=1, ...) rather than its real screen code
        ; (A=1, B=2, ...), which produced "RBNQD" (each letter
        ; correct, but one whole screen code lower than intended)
        ; instead of "SCORE" when actually run.
        ldx #$00
@s_loop:
        lda $d000 + 19*8,x
        sta CHAR_MEM + CHAR_S*8,x
        inx
        cpx #8
        bne @s_loop

        ldx #$00
@c_loop:
        lda $d000 + 3*8,x
        sta CHAR_MEM + CHAR_C*8,x
        inx
        cpx #8
        bne @c_loop

        ldx #$00
@o_loop:
        lda $d000 + 15*8,x
        sta CHAR_MEM + CHAR_O*8,x
        inx
        cpx #8
        bne @o_loop

        ldx #$00
@r_loop:
        lda $d000 + 18*8,x
        sta CHAR_MEM + CHAR_R*8,x
        inx
        cpx #8
        bne @r_loop

        ldx #$00
@e_loop:
        lda $d000 + 5*8,x
        sta CHAR_MEM + CHAR_E*8,x
        inx
        cpx #8
        bne @e_loop

        lda $01
        ora #%00000100           ; restore CHAREN -- I/O visible again
        sta $01

        cli
        rts

; Loads this game's own level file ("LEVEL1") from disk: the player's
; own starting tile (2 bytes: column, row), the level's own title (20
; bytes, PETSCII, space-padded -- LEVEL_TITLE_LEN), then the maze grid
; itself (TILE_COLS*TILE_ROWS bytes, row-major, straight into
; MAZE_GRID) -- 858 bytes total for this maze's own dimensions. Stands
; in for what stage 1-3 shipped with first: a fixed test maze
; assembled directly into the program's own bytes. A real file on a
; real disk now, so a different level means authoring a new file, not
; reassembling this program.
;
; If the file can't be opened at all (not found, wrong device, and so
; on), there's no safe way to proceed -- MAZE_GRID would be left
; entirely uninitialized, and rendering or colliding against that is
; worse than doing nothing. Signals failure the simplest way
; available without any of this program's own text rendering (a red
; border, a well-known, recognizable "something's wrong" signal on
; this platform) and halts, rather than silently continuing with a
; maze that was never actually loaded.
;
; READST is checked before every single CHRIN in this routine, not
; just once at the very start -- confirmed as a real gap the hard way,
; not a theoretical one: reported directly from real hardware as a
; screen full of a single repeated tile, which a dedicated diagnostic
; tool (hexdump.asm) traced to the file being shorter on disk than
; this program expected. Without checking again during the read, a
; file that runs out partway through leaves everything past that
; point as whatever CHRIN happens to keep returning once there's
; nothing legitimate left -- silently, with no indication anything
; went wrong, which is exactly what produced that screen full of
; repeated tiles. The check matches this project's own already-
; established pattern (dir_raw.asm) exactly: READST checked *before*
; each CHRIN, not after -- checking after would incorrectly treat the
; file's own last, perfectly valid byte as an error, since READST's
; own EOF bit is already set by the time CHRIN returns that final
; byte.
load_level:
        lda #LEVEL_FILENAME_LEN
        ldx #<level_filename
        ldy #>level_filename
        jsr SETNAM
        lda #2                     ; logical file 2
        ldx #8                     ; device 8
        ldy #2                      ; secondary address 2 (read)
        jsr SETLFS
        jsr OPEN
        ldx #2
        jsr CHKIN

        jsr READST
        and #$40
        beq @found
        jmp @load_error

@found:
        jsr READST
        bne @load_error
        jsr CHRIN
        sta player_start_col

        jsr READST
        bne @load_error
        jsr CHRIN
        sta player_start_row

        ldx #$00
@title_loop:
        jsr READST
        bne @load_error
        jsr CHRIN
        sta level_title,x
        inx
        cpx #LEVEL_TITLE_LEN
        bne @title_loop

        lda #<MAZE_GRID
        sta grid_ptr
        lda #>MAZE_GRID
        sta grid_ptr+1
        lda #<(TILE_COLS * TILE_ROWS)
        sta maze_copy_count_lo
        lda #>(TILE_COLS * TILE_ROWS)
        sta maze_copy_count_hi

@tile_loop:
        jsr READST
        bne @load_error
        jsr CHRIN
        ldy #$00
        sta (grid_ptr),y

        inc grid_ptr
        bne @no_carry
        inc grid_ptr+1
@no_carry:
        lda maze_copy_count_lo
        bne @dec_lo
        dec maze_copy_count_hi
@dec_lo:
        dec maze_copy_count_lo

        lda maze_copy_count_lo
        ora maze_copy_count_hi
        bne @tile_loop

        jsr CLRCHN
        lda #2
        jsr CLOSE
        rts

@load_error:
        jsr CLRCHN
        lda #2
        jsr CLOSE
        lda #$02                  ; red
        sta VIC_BORDER
@halt:
        jmp @halt

; Draws MAZE_GRID onto the screen at (MAZE_SCREEN_COL,
; MAZE_SCREEN_ROW), one tile per screen cell, no per-cell translation
; needed (see this file's own header comment for why the tile values
; already are the right screen codes).
render_maze:
        lda #<(SCREEN + MAZE_SCREEN_ROW*40 + MAZE_SCREEN_COL)
        sta screen_ptr
        lda #>(SCREEN + MAZE_SCREEN_ROW*40 + MAZE_SCREEN_COL)
        sta screen_ptr+1

        lda #<MAZE_GRID
        sta grid_ptr
        lda #>MAZE_GRID
        sta grid_ptr+1

        ldx #$00
@row_loop:
        ldy #$00
@col_loop:
        lda (grid_ptr),y
        sta (screen_ptr),y
        iny
        cpy #TILE_COLS
        bne @col_loop

        lda grid_ptr
        clc
        adc #TILE_COLS
        sta grid_ptr
        lda grid_ptr+1
        adc #$00
        sta grid_ptr+1

        lda screen_ptr
        clc
        adc #40
        sta screen_ptr
        lda screen_ptr+1
        adc #$00
        sta screen_ptr+1

        inx
        cpx #TILE_ROWS
        bne @row_loop
        rts

; Each tile's 8x8 pixel bitmap, one bit per pixel, MSB = leftmost --
; matching CHAR_MEM's own layout (8 consecutive bytes per character
; code, in character-code order starting at 0). TILE_EMPTY is a blank
; cell; TILE_WALL is solid; TILE_DOT is a small centered mark; TILE_
; PELLET is a larger, roughly circular one, meant to read as clearly
; more significant than a plain dot at a glance once both are on
; screen together.
tile_bitmaps:
        .byte %00000000, %00000000, %00000000, %00000000
        .byte %00000000, %00000000, %00000000, %00000000
        .byte %11111111, %11111111, %11111111, %11111111
        .byte %11111111, %11111111, %11111111, %11111111
        .byte %00000000, %00000000, %00000000, %00011000
        .byte %00011000, %00000000, %00000000, %00000000
        .byte %00000000, %00011000, %00111100, %01111110
        .byte %01111110, %00111100, %00011000, %00000000
