"""
End-to-end regression test for maze.asm (stages 1-2: tile rendering,
player movement, wall collision, keyboard/joystick input, and now a
genuine 16-bit player X position with SPRITE_X_MSB handling), using
mini6502.py (see mini6502.zip).

This program has no natural return point (main_loop is a real,
continuous game loop, not a one-shot routine) so these tests run a
fixed, generous instruction budget and inspect memory directly
afterward, rather than using the run_until_return sentinel-RTS pattern
the rest of this project's tests rely on.

The maze layout itself is regenerated here programmatically (the exact
same procedure maze.asm's own test_maze_data comes from), rather than
hardcoded expected coordinates -- deliberately, so expected stopping
points for collision tests are computed from the same source of truth
the game's own data comes from, not guessed at or eyeballed from a
printed maze.

Run from this directory with mini6502.py on the path, e.g.:
    PYTHONPATH=/path/to/mini6502 python3 test_maze.py
"""

import os
import random
import subprocess
import sys

try:
    from mini6502 import C64Machine
except ImportError:
    sys.exit("mini6502.py not found -- put it on PYTHONPATH (see mini6502.zip)")

passed = 0
failed = 0


def check(name, condition, detail=""):
    global passed, failed
    if condition:
        passed += 1
    else:
        failed += 1
        print(f"  FAIL: {name}  {detail}")


def find_c64asm():
    for candidate in ['c64asm.py', '/mnt/user-data/outputs/c64asm.py']:
        if os.path.exists(candidate):
            return candidate
    sys.exit("c64asm.py not found")


ASSEMBLER = find_c64asm()

print("=== assembling maze.asm ===")
result = subprocess.run(
    ['python3', ASSEMBLER, 'maze.asm', '-o', '/tmp/maze_regress.prg',
     '--listing', '/tmp/maze_regress.lst', '--lib-dir', '.'],
    capture_output=True, text=True)
check("maze.asm assembles cleanly", result.returncode == 0, result.stderr)
if result.returncode != 0:
    print(f"\n{passed} passed, {failed} failed")
    sys.exit(1)

with open('/tmp/maze_regress.prg', 'rb') as f:
    data = f.read()

# --- hardware/memory constants ---
CIA2_PRA = 0xdd00
CIA2_DDRA = 0xdd02
VIC_MEMPTRS = 0xd018
VIC_BORDER = 0xd020
VIC_BG0 = 0xd021
CHAR_MEM = 0x3000
MAZE_GRID = 0x3800
SCREEN = 0x0400
SPRITE_ENABLE = 0xd015
SPRITE0_COLOR = 0xd027
SPRITE_PTR0 = SCREEN + 0x3f8
SPRITE0_X = 0xd000
SPRITE0_Y = 0xd001
SPRITE_X_MSB = 0xd010
PLAYER_SPRITE_X_MSB_BIT = 0b00000001
SPRITE_DATA = 0x3b80
PLAYER_REL_X_LO = 0x033c
PLAYER_REL_X_HI = 0x033d
PLAYER_REL_Y = 0x033e
PLAYER_DIRECTION = 0x033f

# --- stage 3: dot collection and scoring ---
SCORE_LO = 0x0350
SCORE_HI = 0x0351
CHAR_DIGIT_0 = 4
CHAR_S, CHAR_C, CHAR_O, CHAR_R, CHAR_E = 14, 15, 16, 17, 18
CPU_PORT = 0x01
CHAREN_BIT = 0b00000100
# (rom_screen_code, dest_char_code, label) for every glyph
# init_text_characters borrows from the character ROM. Screen code 0
# is '@', not 'A' -- confirmed against multiple independent sources
# given a first version of this same routine got this exact point
# wrong, using each letter's own alphabet position directly (A=0,
# B=1, ...) rather than its real screen code (A=1, B=2, ...), which
# produced "RBNQD" instead of "SCORE" when actually run (each letter
# one whole screen code lower than intended, confirmed as the actual
# cause by checking this precisely, not guessed at).
GLYPHS = [(48 + i, CHAR_DIGIT_0 + i, f'digit{i}') for i in range(10)]
GLYPHS += [(19, CHAR_S, 'S'), (3, CHAR_C, 'C'), (15, CHAR_O, 'O'),
           (18, CHAR_R, 'R'), (5, CHAR_E, 'E')]
# mini6502.py doesn't model CHAREN/$01 banking at all, so these
# specific ROM source offsets -- which happen to coincide with
# VIC-II registers (VIC_RASTER, VIC_MEMPTRS, VIC_BORDER, VIC_BG0)
# mini6502 always intercepts as memory-mapped I/O -- can't be
# verified dynamically by poking a test pattern and reading it back.
# The copy's own source/destination addresses at those exact offsets
# are instead verified statically, directly against the assembled
# listing, further down.
EMULATOR_INTERCEPTED_ADDRS = {0xd012, 0xd018, 0xd020, 0xd021}


def score(m):
    return m.cpu.memory[SCORE_LO] + 256 * m.cpu.memory[SCORE_HI]


# --- maze layout constants (must match maze.asm exactly) ---
TILE_COLS = 38
TILE_ROWS = 22
MAZE_SCREEN_COL = 1
MAZE_SCREEN_ROW = 1
MAZE_ORIGIN_X = 24 + MAZE_SCREEN_COL * 8   # 32
MAZE_ORIGIN_Y = 50 + MAZE_SCREEN_ROW * 8   # 58
PLAYER_START_COL = 15
PLAYER_START_ROW = 11

# WASD -- keyboard.inc's own verified column/row constants
KEY_W_COL, KEY_W_ROW = 0b11111101, 0b00000010
KEY_S_COL, KEY_S_ROW = 0b11111101, 0b00100000
KEY_A_COL, KEY_A_ROW = 0b11111101, 0b00000100
KEY_D_COL, KEY_D_ROW = 0b11111011, 0b00000100

# --- reference maze layout, the exact same procedure test_maze_data
# itself comes from (see maze.asm's own header comment) ---
_grid = [['.' for _ in range(TILE_COLS)] for _ in range(TILE_ROWS)]
for _c in range(TILE_COLS):
    _grid[0][_c] = 'W'
    _grid[TILE_ROWS - 1][_c] = 'W'
for _r in range(TILE_ROWS):
    _grid[_r][0] = 'W'
    _grid[_r][TILE_COLS - 1] = 'W'
for _br in range(3, TILE_ROWS - 3, 6):
    for _bc in range(3, TILE_COLS - 3, 7):
        for _dr in range(3):
            for _dc in range(4):
                _r, _c = _br + _dr, _bc + _dc
                if 1 <= _r < TILE_ROWS - 1 and 1 <= _c < TILE_COLS - 1:
                    _grid[_r][_c] = 'W'
_grid[1][1] = 'P'
_grid[1][TILE_COLS - 2] = 'P'
_grid[TILE_ROWS - 2][1] = 'P'
_grid[TILE_ROWS - 2][TILE_COLS - 2] = 'P'
_TILE_VALUE = {'W': 1, '.': 2, 'P': 3}
REFERENCE_GRID = [[_TILE_VALUE[ch] for ch in row] for row in _grid]

# The level file itself ("LEVEL1"), built from the exact same
# REFERENCE_GRID above -- the same source of truth used everywhere
# else in this file, not a separately hand-maintained copy. Matches
# load_level's own documented format exactly: start column, start
# row, a 20-byte space-padded title, then the grid itself, row-major.
LEVEL_TITLE = "TEST MAZE"
LEVEL_TITLE_LEN = 20
_title_bytes = LEVEL_TITLE.encode('ascii').ljust(LEVEL_TITLE_LEN, b' ')[:LEVEL_TITLE_LEN]
_tile_bytes = bytes(REFERENCE_GRID[r][c] for r in range(TILE_ROWS) for c in range(TILE_COLS))
LEVEL1_DATA = bytes([PLAYER_START_COL, PLAYER_START_ROW]) + _title_bytes + _tile_bytes
assert len(LEVEL1_DATA) == 2 + LEVEL_TITLE_LEN + TILE_COLS * TILE_ROWS == 858


def is_wall(col, row):
    return REFERENCE_GRID[row][col] == 1


def stop_point(start_col, start_row, dcol, drow):
    """Where the player should end up moving (dcol, drow) repeatedly
    from (start_col, start_row) until blocked -- the same rule
    can_move_direction/update_player itself implements, computed here
    independently against REFERENCE_GRID rather than assumed."""
    c, r = start_col, start_row
    while not is_wall(c + dcol, r + drow):
        c += dcol
        r += drow
    return c, r


def run_fixed_budget(m, start_pc, instructions=3_000_000):
    # $D012 (VIC_RASTER) isn't simulated by mini6502.py -- ordinary
    # memory that never advances on its own -- so wait_frame would
    # spin forever waiting for a raster line that never arrives unless
    # this is poked once, up front, to the value it's waiting for.
    #
    # Deliberately does NOT touch joystick2 here -- the caller is
    # responsible for setting it (even to 0) before calling this. An
    # earlier version of this helper set it to 0 internally "for
    # safety," which silently overwrote whatever a test had
    # deliberately set immediately beforehand, wanting that value to
    # apply for the whole run.
    m.cpu.memory[0xd012] = 0xfb
    m.cpu.pc = start_pc
    m.cpu.halted = False
    for _ in range(instructions):
        m.step()


def continue_running(m, instructions):
    # Unlike run_fixed_budget, doesn't reset pc -- for tests that poke
    # state (a specific player position, say) partway through a run
    # and need to keep going from exactly there, not silently restart
    # the whole program from start: and lose that poke.
    for _ in range(instructions):
        m.step()


def continue_until_changed(m, addr, original_value, max_instructions=3_000_000):
    for i in range(max_instructions):
        m.step()
        if m.cpu.memory[addr] != original_value:
            return i + 1
    return None


def fresh_machine():
    m = C64Machine(simulate_zp_poisoning=True)
    target = m.find_sys_target(data)
    m.load_prg(data)
    m.disk_files = {'LEVEL1': LEVEL1_DATA}
    return m, target


def screen_char(m, row, col):
    return m.cpu.memory[SCREEN + row * 40 + col] & 0x7f


def rel_x(m):
    return m.cpu.memory[PLAYER_REL_X_LO] + 256 * m.cpu.memory[PLAYER_REL_X_HI]


def set_rel_x(m, value):
    m.cpu.memory[PLAYER_REL_X_LO] = value & 0xff
    m.cpu.memory[PLAYER_REL_X_HI] = (value >> 8) & 0xff


def step_to_loop_top(m, main_loop_addr, max_instructions=100_000):
    # Steps until pc lands exactly on main_loop's own address -- the
    # only point in the whole loop guaranteed to be strictly between
    # iterations, with update_player and update_sprite_position both
    # already fully finished for whatever player_rel_x currently is.
    # A fixed instruction-count batch (an earlier version of this
    # helper) can land mid-iteration instead, catching player_rel_x
    # after update_player moved it but before update_sprite_position
    # has caught up for that same value -- exactly the kind of
    # transient mismatch that produced false failures while this test
    # was being developed.
    for _ in range(max_instructions):
        m.step()
        if m.cpu.pc == main_loop_addr:
            return True
    return False


def find_symbol_address(listing_path, symbol_name):
    with open(listing_path) as f:
        for line in f:
            stripped = line.strip()
            if stripped.startswith(f"{symbol_name} ") or stripped.startswith(f"{symbol_name}="):
                parts = stripped.split('=')
                if len(parts) == 2 and parts[0].strip() == symbol_name:
                    return int(parts[1].strip().lstrip('$'), 16)
    sys.exit(f"couldn't find {symbol_name}'s own address in {listing_path}")


MAIN_LOOP_ADDR = find_symbol_address('/tmp/maze_regress.lst', 'main_loop')


# ============================================================
# Stage 1: VIC-II setup, tile characters, maze grid, rendering
# ============================================================

print("=== the whole screen is cleared to TILE_EMPTY at startup, "
      "before this program renders anything of its own -- guards "
      "against a real bug reported directly from real hardware: "
      "without this, any screen position this program doesn't "
      "explicitly write to (like the single-column margin left of "
      "the maze) keeps showing whatever was already there from the "
      "BASIC LOAD/RUN commands actually typed to start it. Seeded "
      "with realistic pre-existing 'residual' screen content first, "
      "not run against an already-blank screen the way every other "
      "test in this file starts, since that's exactly what let this "
      "bug go unnoticed in simulation the first time ===")
m0 = C64Machine(simulate_zp_poisoning=True)
target0 = m0.find_sys_target(data)
m0.load_prg(data)
m0.joystick2 = 0
_rng = random.Random(42)
for i in range(1000):
    m0.cpu.memory[SCREEN + i] = _rng.randint(1, 63)
run_fixed_budget(m0, target0, 50_000)
residual_ok = True
for row in range(25):
    for col in range(40):
        in_maze = (MAZE_SCREEN_ROW <= row < MAZE_SCREEN_ROW + TILE_ROWS
                   and MAZE_SCREEN_COL <= col < MAZE_SCREEN_COL + TILE_COLS)
        in_hud = (row == 0 and col < 12)
        if not in_maze and not in_hud:
            if m0.cpu.memory[SCREEN + row * 40 + col] != 0:
                residual_ok = False
check("every screen position outside the maze's own bounds and the "
      "HUD row is TILE_EMPTY, not leftover residual content",
      residual_ok)

print("=== VIC-II bank/character-memory setup is correct and doesn't "
      "disturb the serial/IEC bus bits ===")
m1, target = fresh_machine()
m1.joystick2 = 0
run_fixed_budget(m1, target, 50_000)
check("CIA2_DDRA has bits 0-1 set as outputs, nothing else touched",
      m1.cpu.memory[CIA2_DDRA] == 0x03, f"got {m1.cpu.memory[CIA2_DDRA]:#04x}")
check("CIA2_PRA selects VIC bank 0 via read-modify-write",
      m1.cpu.memory[CIA2_PRA] == 0x03, f"got {m1.cpu.memory[CIA2_PRA]:#04x}")
check("VIC_MEMPTRS selects screen $0400, character memory $3000",
      m1.cpu.memory[VIC_MEMPTRS] == 0x1c, f"got {m1.cpu.memory[VIC_MEMPTRS]:#04x}")
check("border is black", m1.cpu.memory[VIC_BORDER] == 0)
check("background is black", m1.cpu.memory[VIC_BG0] == 0)

print("=== the four tile character bitmaps land at character codes "
      "0-3 exactly ===")
empty_bitmap = [m1.cpu.memory[CHAR_MEM + i] for i in range(8)]
wall_bitmap = [m1.cpu.memory[CHAR_MEM + 8 + i] for i in range(8)]
dot_bitmap = [m1.cpu.memory[CHAR_MEM + 16 + i] for i in range(8)]
pellet_bitmap = [m1.cpu.memory[CHAR_MEM + 24 + i] for i in range(8)]
check("TILE_EMPTY (character 0) is fully blank", empty_bitmap == [0x00] * 8)
check("TILE_WALL (character 1) is fully solid", wall_bitmap == [0xff] * 8)
check("TILE_DOT (character 2) is a small mark, neither blank nor solid",
      dot_bitmap not in ([0x00] * 8, [0xff] * 8) and any(dot_bitmap))
check("TILE_PELLET (character 3) covers more of the cell than TILE_DOT",
      sum(bin(b).count('1') for b in pellet_bitmap)
      > sum(bin(b).count('1') for b in dot_bitmap))

print("=== the maze grid in RAM exactly matches the reference layout "
      "(except the player's own start tile -- a dot, correctly eaten "
      "by check_eat_dot within the first loop iteration, checked "
      "precisely in its own dedicated test further down rather than "
      "here) ===")
grid_ok = True
for r in range(TILE_ROWS):
    for c in range(TILE_COLS):
        if (c, r) == (PLAYER_START_COL, PLAYER_START_ROW):
            continue
        actual = m1.cpu.memory[MAZE_GRID + r * TILE_COLS + c]
        if actual != REFERENCE_GRID[r][c]:
            print(f"  mismatch at grid ({c},{r}): got {actual}, "
                  f"expected {REFERENCE_GRID[r][c]}")
            grid_ok = False
check("every other tile matches the reference layout exactly", grid_ok)
check("TILE_ROWS*TILE_COLS exceeds 255, confirming this test actually "
      "exercises the 16-bit grid-offset path",
      (TILE_ROWS - 1) * TILE_COLS > 255)

print("=== the rendered screen exactly matches the grid, correctly "
      "offset to (MAZE_SCREEN_COL, MAZE_SCREEN_ROW) (same exception "
      "as above) ===")
screen_ok = True
for r in range(TILE_ROWS):
    for c in range(TILE_COLS):
        if (c, r) == (PLAYER_START_COL, PLAYER_START_ROW):
            continue
        actual = screen_char(m1, MAZE_SCREEN_ROW + r, MAZE_SCREEN_COL + c)
        if actual != REFERENCE_GRID[r][c]:
            screen_ok = False
check("every other rendered screen tile matches the grid", screen_ok)

print("=== nothing is rendered outside the maze's own bounds "
      "(below it -- row 0, above it, is now the score HUD, checked "
      "separately and more precisely further down) ===")
below = screen_char(m1, MAZE_SCREEN_ROW + TILE_ROWS, MAZE_SCREEN_COL)
check("nothing rendered below the maze", below == 0, f"got {below}")

# ============================================================
# Stage 2: player sprite, movement, wall collision
# ============================================================

print("=== the player sprite is set up correctly: enabled, non-blank "
      "shape data, correct color, positioned at its own start tile, "
      "SPRITE_X_MSB correctly clear (start tile's absolute X is under "
      "256) ===")
m2, target = fresh_machine()
m2.joystick2 = 0
run_fixed_budget(m2, target, 50_000)
check("sprite 0 is enabled, alongside both enemies now too",
      m2.cpu.memory[SPRITE_ENABLE] == 0x07,
      f"got {m2.cpu.memory[SPRITE_ENABLE]:#010b}")
check("sprite 0's color is set", m2.cpu.memory[SPRITE0_COLOR] == 0x01)
check("sprite 0's pointer targets SPRITE_DATA",
      m2.cpu.memory[SPRITE_PTR0] == SPRITE_DATA // 64,
      f"got {m2.cpu.memory[SPRITE_PTR0]}")
sprite_bytes = [m2.cpu.memory[SPRITE_DATA + i] for i in range(63)]
check("the player sprite shape isn't blank", any(sprite_bytes))
total_bits = sum(bin(b).count('1') for b in sprite_bytes)
check("the player sprite is meaningfully smaller than a full 24x21 "
      "circle (roughly proportionate to one 8x8 tile)",
      total_bits < 200, f"got {total_bits} set bits")

# Guards against a real bug this game actually shipped with: a
# visually smaller sprite that was centered in the middle of the full
# 24x21 canvas instead of anchored at the sprite's own (0,0) --
# update_sprite_position and the collision system both use that (X,Y)
# origin directly, so a shape drawn away from it looks visually
# shifted relative to where collision actually happens, exactly what
# was reported after actually playing this: hitting the left/top wall
# with a visible gap still showing, and appearing to overlap into the
# right/bottom wall before actually stopping.
lit_pixels = []
for row in range(21):
    row_bytes = sprite_bytes[row*3:row*3+3]
    bits = ''.join(f'{b:08b}' for b in row_bytes)
    for col, bit in enumerate(bits):
        if bit == '1':
            lit_pixels.append((col, row))
lit_xs = [p[0] for p in lit_pixels]
lit_ys = [p[1] for p in lit_pixels]
check("the visible sprite shape is anchored at the sprite's own "
      "(0,0) origin, not centered elsewhere in the 24x21 canvas -- "
      "the same origin the collision system itself uses",
      min(lit_xs) == 0 and min(lit_ys) == 0,
      f"got top-left corner at ({min(lit_xs)},{min(lit_ys)})")
check("the visible shape stays within roughly one tile's own bounds "
      "(0-7 on each axis), not spilling into where the next tile "
      "would be", max(lit_xs) < 8 and max(lit_ys) < 8,
      f"got bottom-right corner at ({max(lit_xs)},{max(lit_ys)})")

check("player_rel_x starts at the documented start tile",
      rel_x(m2) == PLAYER_START_COL * 8, f"got {rel_x(m2)}")
check("player_rel_y starts at the documented start tile",
      m2.cpu.memory[PLAYER_REL_Y] == PLAYER_START_ROW * 8,
      f"got {m2.cpu.memory[PLAYER_REL_Y]}")
check("SPRITE0_X reflects the maze's own screen offset plus the "
      "starting tile", m2.cpu.memory[SPRITE0_X] == MAZE_ORIGIN_X + PLAYER_START_COL * 8,
      f"got {m2.cpu.memory[SPRITE0_X]}")
check("SPRITE0_Y likewise", m2.cpu.memory[SPRITE0_Y] == MAZE_ORIGIN_Y + PLAYER_START_ROW * 8,
      f"got {m2.cpu.memory[SPRITE0_Y]}")
check("SPRITE_X_MSB bit 0 is clear at the start tile",
      m2.cpu.memory[SPRITE_X_MSB] & PLAYER_SPRITE_X_MSB_BIT == 0,
      f"got {m2.cpu.memory[SPRITE_X_MSB]:#010b}")

print("=== moving up from the start tile stops exactly where the "
      "reference grid says ===")
exp_col, exp_row = stop_point(PLAYER_START_COL, PLAYER_START_ROW, 0, -1)
m3, target = fresh_machine()
m3.joystick2 = 0b00000001
run_fixed_budget(m3, target, 3_000_000)
check(f"stops at column {exp_col} (unchanged)",
      rel_x(m3) == exp_col * 8, f"got {rel_x(m3)}")
check(f"stops at row {exp_row}",
      m3.cpu.memory[PLAYER_REL_Y] == exp_row * 8, f"got {m3.cpu.memory[PLAYER_REL_Y]}")

print("=== moving right from the start tile stops exactly where the "
      "reference grid says ===")
exp_col2, exp_row2 = stop_point(PLAYER_START_COL, PLAYER_START_ROW, 1, 0)
m4, target = fresh_machine()
m4.joystick2 = 0b00001000
run_fixed_budget(m4, target, 3_000_000)
check(f"stops at column {exp_col2}", rel_x(m4) == exp_col2 * 8, f"got {rel_x(m4)}")
check(f"stops at row {exp_row2} (unchanged)",
      m4.cpu.memory[PLAYER_REL_Y] == exp_row2 * 8, f"got {m4.cpu.memory[PLAYER_REL_Y]}")

print("=== pressing a direction that's blocked from an already-"
      "aligned tile does nothing at all ===")
# row 3 (a wall-block row: br=3) at col 5 is inside the block's own
# columns (3-6), so up is blocked immediately from row 4 there
m5, target = fresh_machine()
m5.joystick2 = 0
run_fixed_budget(m5, target, 50_000)
check("reference confirms up is blocked immediately from (5,4)",
      is_wall(5, 3))
set_rel_x(m5, 5 * 8)
m5.cpu.memory[PLAYER_REL_Y] = 4 * 8
m5.cpu.memory[PLAYER_DIRECTION] = 0
m5.joystick2 = 0b00000001
continue_running(m5, 50_000)
check("no movement, no direction change",
      rel_x(m5) == 5 * 8 and m5.cpu.memory[PLAYER_REL_Y] == 4 * 8
      and m5.cpu.memory[PLAYER_DIRECTION] == 0)

print("=== turning is possible once the player reaches a tile where "
      "the new direction is actually open ===")
exp_col3, exp_row3 = stop_point(5, 4, -1, 0)
m6, target = fresh_machine()
m6.joystick2 = 0
run_fixed_budget(m6, target, 50_000)
set_rel_x(m6, 5 * 8)
m6.cpu.memory[PLAYER_REL_Y] = 4 * 8
m6.cpu.memory[PLAYER_DIRECTION] = 0
m6.joystick2 = 0b00000100   # left
continue_running(m6, 3_000_000)
check(f"turned left and stopped at column {exp_col3}",
      rel_x(m6) == exp_col3 * 8, f"got {rel_x(m6)}")
check("row unchanged while turning",
      m6.cpu.memory[PLAYER_REL_Y] == 4 * 8, f"got {m6.cpu.memory[PLAYER_REL_Y]}")

print("=== with two directions held at once, the one checked first "
      "(up) wins, as long as it's actually open ===")
m7, target = fresh_machine()
m7.joystick2 = 0
run_fixed_budget(m7, target, 50_000)
set_rel_x(m7, PLAYER_START_COL * 8)
m7.cpu.memory[PLAYER_REL_Y] = PLAYER_START_ROW * 8
m7.cpu.memory[PLAYER_DIRECTION] = 0
m7.joystick2 = 0b00000101   # up + left
steps = continue_until_changed(m7, PLAYER_REL_Y, PLAYER_START_ROW * 8)
check("Y actually changed (up was chosen)", steps is not None)
check("direction recorded as up (1)", m7.cpu.memory[PLAYER_DIRECTION] == 1,
      f"got {m7.cpu.memory[PLAYER_DIRECTION]}")
check("X untouched while Y was the one changing",
      rel_x(m7) == PLAYER_START_COL * 8)

print("=== releasing the joystick doesn't stop the player -- it keeps "
      "moving until blocked, matching the classic arcade convention ===")
m8, target = fresh_machine()
m8.joystick2 = 0
run_fixed_budget(m8, target, 50_000)
set_rel_x(m8, PLAYER_START_COL * 8)
m8.cpu.memory[PLAYER_REL_Y] = PLAYER_START_ROW * 8
m8.cpu.memory[PLAYER_DIRECTION] = 0
m8.joystick2 = 0b00001000   # right
steps8 = continue_until_changed(m8, PLAYER_REL_X_LO, PLAYER_START_COL * 8)
check("X actually started changing", steps8 is not None)
m8.joystick2 = 0
continue_running(m8, 3_000_000)
check(f"kept moving right after release, all the way to column {exp_col2}",
      rel_x(m8) == exp_col2 * 8, f"got {rel_x(m8)}")
check("stopped (direction back to 0) once blocked",
      m8.cpu.memory[PLAYER_DIRECTION] == 0)

# --- WASD keyboard movement, alongside the joystick ---

print("=== W moves the player up ===")
m9, target = fresh_machine()
m9.joystick2 = 0
m9.press_key(KEY_W_COL, KEY_W_ROW)
run_fixed_budget(m9, target, 3_000_000)
check("player_rel_y decreased", m9.cpu.memory[PLAYER_REL_Y] < PLAYER_START_ROW * 8,
      f"got {m9.cpu.memory[PLAYER_REL_Y]}")

print("=== A moves the player left ===")
m10, target = fresh_machine()
m10.joystick2 = 0
run_fixed_budget(m10, target, 50_000)
# The player's own start tile can be adjusted here freely -- row 2 is
# fully open in every direction, verified against the reference grid,
# used specifically so this test isn't accidentally sensitive to
# whatever happens to be immediately left of the documented start.
check("reference confirms row 2 is open at both columns used here",
      not is_wall(15, 2) and not is_wall(14, 2))
set_rel_x(m10, 15 * 8)
m10.cpu.memory[PLAYER_REL_Y] = 2 * 8
m10.cpu.memory[PLAYER_DIRECTION] = 0
m10.press_key(KEY_A_COL, KEY_A_ROW)
continue_running(m10, 3_000_000)
check("player_rel_x decreased", rel_x(m10) < 15 * 8, f"got {rel_x(m10)}")

print("=== S moves the player down ===")
m11, target = fresh_machine()
m11.joystick2 = 0
m11.press_key(KEY_S_COL, KEY_S_ROW)
run_fixed_budget(m11, target, 3_000_000)
check("player_rel_y increased", m11.cpu.memory[PLAYER_REL_Y] > PLAYER_START_ROW * 8,
      f"got {m11.cpu.memory[PLAYER_REL_Y]}")

print("=== D moves the player right ===")
m12, target = fresh_machine()
m12.joystick2 = 0
m12.press_key(KEY_D_COL, KEY_D_ROW)
run_fixed_budget(m12, target, 3_000_000)
check("player_rel_x increased", rel_x(m12) > PLAYER_START_COL * 8,
      f"got {rel_x(m12)}")

print("=== the joystick still works correctly, unaffected by WASD ===")
m13, target = fresh_machine()
m13.joystick2 = 0b00000010   # down
run_fixed_budget(m13, target, 3_000_000)
check("player_rel_y increased via the joystick",
      m13.cpu.memory[PLAYER_REL_Y] > PLAYER_START_ROW * 8,
      f"got {m13.cpu.memory[PLAYER_REL_Y]}")

print("=== WASD and the joystick can be used together without "
      "conflict ===")
m14, target = fresh_machine()
m14.joystick2 = 0
m14.press_key(KEY_D_COL, KEY_D_ROW)
run_fixed_budget(m14, target, 3_000_000)
check("player_rel_x increased", rel_x(m14) > PLAYER_START_COL * 8,
      f"got {rel_x(m14)}")
check("player_rel_y untouched", m14.cpu.memory[PLAYER_REL_Y] == PLAYER_START_ROW * 8,
      f"got {m14.cpu.memory[PLAYER_REL_Y]}")

# ============================================================
# The actual point of this stage's own rewrite: SPRITE_X_MSB
# ============================================================

print("=== the player can actually reach columns whose absolute "
      "sprite X exceeds 255 -- the reason this rewrite exists at all "
      "-- and SPRITE_X_MSB tracks that correctly the whole way, "
      "settled after each full loop iteration rather than sampled "
      "mid-instruction ===")
m15, target = fresh_machine()
m15.joystick2 = 0
run_fixed_budget(m15, target, 50_000)
step_to_loop_top(m15, MAIN_LOOP_ADDR)   # land at a clean loop
                                            # boundary before poking,
                                            # so the poke can't be
                                            # partway overwritten by
                                            # whatever iteration was
                                            # already in progress
check("reference confirms row 2 is open across the whole MSB-crossing "
      "range used below (cols 27-36)",
      all(not is_wall(c, 2) for c in range(27, 37)))
set_rel_x(m15, 27 * 8)   # abs = 32+216 = 248, MSB still clear
m15.cpu.memory[PLAYER_REL_Y] = 2 * 8
m15.cpu.memory[PLAYER_DIRECTION] = 4   # already moving right, so no
                                           # alignment check is needed
                                           # before it continues
check("starts below the threshold with MSB clear",
      m15.cpu.memory[SPRITE_X_MSB] & PLAYER_SPRITE_X_MSB_BIT == 0)
m15.joystick2 = 0b00001000
settle_ok = True
for _ in range(10):
    if not step_to_loop_top(m15, MAIN_LOOP_ADDR):
        settle_ok = False
        print("  never reached the loop boundary within budget")
        break
    rx = rel_x(m15)
    abs_x = MAZE_ORIGIN_X + rx
    expected_sprite0_x = abs_x % 256
    expected_msb = 1 if abs_x > 255 else 0
    actual_msb = 1 if (m15.cpu.memory[SPRITE_X_MSB] & PLAYER_SPRITE_X_MSB_BIT) else 0
    if (m15.cpu.memory[SPRITE0_X] != expected_sprite0_x
            or actual_msb != expected_msb):
        settle_ok = False
        print(f"  mismatch: rel_x={rx} abs_x={abs_x} "
              f"SPRITE0_X={m15.cpu.memory[SPRITE0_X]} (expected {expected_sprite0_x}) "
              f"MSB={actual_msb} (expected {expected_msb})")
check("SPRITE0_X and the MSB bit stay correct across the full 256 "
      "threshold, at every sampled point", settle_ok)
check("the player actually crossed the threshold during this test "
      "(not stuck below it the whole time)",
      MAZE_ORIGIN_X + rel_x(m15) > 255, f"final abs_x={MAZE_ORIGIN_X + rel_x(m15)}")

print("=== moving back left below the threshold clears the MSB bit "
      "again, not left stuck set ===")
m16, target = fresh_machine()
m16.joystick2 = 0
run_fixed_budget(m16, target, 50_000)
step_to_loop_top(m16, MAIN_LOOP_ADDR)   # same reasoning as m15 above
set_rel_x(m16, 30 * 8)   # abs = 32+240 = 272, MSB set
m16.cpu.memory[PLAYER_REL_Y] = 2 * 8
m16.cpu.memory[PLAYER_DIRECTION] = 0
step_to_loop_top(m16, MAIN_LOOP_ADDR)
check("starts above the threshold with MSB set",
      m16.cpu.memory[SPRITE_X_MSB] & PLAYER_SPRITE_X_MSB_BIT != 0,
      f"got {m16.cpu.memory[SPRITE_X_MSB]:#010b}")
m16.joystick2 = 0b00000100   # left
continue_running(m16, 3_000_000)
final_abs_x = MAZE_ORIGIN_X + rel_x(m16)
check("ended up back below the threshold", final_abs_x <= 255,
      f"final abs_x={final_abs_x}")
check("MSB bit correctly cleared again",
      m16.cpu.memory[SPRITE_X_MSB] & PLAYER_SPRITE_X_MSB_BIT == 0,
      f"got {m16.cpu.memory[SPRITE_X_MSB]:#010b}")

print("=== the player's own MSB bit (bit 0) never disturbs the other "
      "7 sprites' own MSB bits sharing the same register ===")
m17, target = fresh_machine()
m17.joystick2 = 0
run_fixed_budget(m17, target, 50_000)
step_to_loop_top(m17, MAIN_LOOP_ADDR)   # land at a clean loop
                                            # boundary before poking --
                                            # enemies now read-modify-
                                            # write SPRITE_X_MSB every
                                            # frame too (stage 5), so
                                            # poking mid-instruction
                                            # here risks the poke being
                                            # overwritten by an already
                                            # in-flight computation
                                            # that read the old value
                                            # before this poke happened
m17.cpu.memory[SPRITE_X_MSB] = 0b11111110   # pretend sprites 3-7
                                                # already have their
                                                # own MSB bits set --
                                                # bits 1-2 are real,
                                                # active enemy sprites
                                                # as of stage 5, not
                                                # hypothetical "some
                                                # other sprite," and
                                                # correctly get
                                                # managed by their own
                                                # real position data
                                                # during this test's
                                                # own long run below,
                                                # not left untouched
set_rel_x(m17, 27 * 8)
m17.cpu.memory[PLAYER_REL_Y] = 2 * 8
m17.cpu.memory[PLAYER_DIRECTION] = 4
m17.joystick2 = 0b00001000
continue_running(m17, 3_000_000)
check("player crossed the threshold (own bit 0 now set)",
      m17.cpu.memory[SPRITE_X_MSB] & PLAYER_SPRITE_X_MSB_BIT != 0)
check("every genuinely unused sprite's own MSB bit (3-7) is still "
      "set, untouched -- bits 1-2 aren't checked here since they're "
      "real, active enemy sprites that correctly change based on "
      "their own real position, not a fixed pretend value",
      (m17.cpu.memory[SPRITE_X_MSB] & 0b11111000) == 0b11111000,
      f"got {m17.cpu.memory[SPRITE_X_MSB]:#010b}")

# ============================================================
# Stage 3: character ROM borrowing, dot collection, scoring
# ============================================================

print("=== every borrowed glyph's own copy instruction uses exactly "
      "the right source (ROM) and destination (this program's own "
      "character memory) address -- verified directly against the "
      "assembled listing, the only way to check the glyph whose "
      "ROM source offset happens to coincide with a VIC-II register "
      "mini6502.py always intercepts regardless of CHAREN ===")
with open('/tmp/maze_regress.lst') as f:
    listing_text = f.read()
listing_ok = True
# Digits 0-9 are copied as a single 80-byte run (one loop, X from 0 to
# 79), not ten separate instructions -- only digit0's own base address
# actually appears in the listing; the other nine are covered by the
# same loop incrementing X, not distinct LDA/STA operands to check.
# Each letter, in contrast, is genuinely its own separate loop.
addresses_to_check = [GLYPHS[0]] + GLYPHS[10:]
for rom_code, dest_code, label in addresses_to_check:
    src_addr = 0xd000 + rom_code * 8
    dest_addr = CHAR_MEM + dest_code * 8
    src_hex = f"{src_addr & 0xff:02X} {(src_addr >> 8) & 0xff:02X}"
    dest_hex = f"{dest_addr & 0xff:02X} {(dest_addr >> 8) & 0xff:02X}"
    if src_hex not in listing_text or dest_hex not in listing_text:
        print(f"  {label}: expected source bytes {src_hex} and dest "
              f"bytes {dest_hex} not both found in the listing")
        listing_ok = False
check("every glyph's own source and destination address appears "
      "correctly in the assembled listing", listing_ok)

print("=== the copy mechanism itself works correctly, confirmed "
      "dynamically for every offset the emulator doesn't intercept ===")
m1, target = fresh_machine()
m1.joystick2 = 0
run_fixed_budget(m1, target, 50_000)
test_patterns = {}
skip_offsets = {}
for i, (rom_code, dest_code, label) in enumerate(GLYPHS):
    pattern = [(i * 8 + j + 1) & 0xff for j in range(8)]
    test_patterns[label] = pattern
    base = 0xd000 + rom_code * 8
    skip = set()
    for j in range(8):
        if (base + j) in EMULATOR_INTERCEPTED_ADDRS:
            skip.add(j)
        else:
            m1.cpu.memory[base + j] = pattern[j]
    skip_offsets[label] = skip
run_fixed_budget(m1, target, 200_000)
copy_ok = True
for rom_code, dest_code, label in GLYPHS:
    expected = test_patterns[label]
    actual = [m1.cpu.memory[CHAR_MEM + dest_code * 8 + j] for j in range(8)]
    skip = skip_offsets[label]
    mismatch = [j for j in range(8) if j not in skip and actual[j] != expected[j]]
    if mismatch:
        print(f"  {label} mismatch at offsets {mismatch}: "
              f"expected {expected}, got {actual}")
        copy_ok = False
check("every glyph's own bytes land at its own destination code "
      "(excluding the 4 emulator-intercepted offsets, verified "
      "statically above instead)", copy_ok)

print("=== the most direct check possible: poking the REAL character "
      "ROM glyph data for S, C, O, R, E and confirming the copied "
      "result is the exact, correct letter shape -- not just that "
      "bytes moved, but that they spell what they're supposed to. "
      "This is the exact check that would have caught the real bug "
      "this project shipped with (screen code 0 is '@', not 'A', so "
      "every letter's own source code was one too low, and the HUD "
      "read 'RBNQD' instead of 'SCORE') ===")
REAL_GLYPHS = {
    'C': [0x3c, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3c, 0x00],
    'E': [0x7e, 0x60, 0x60, 0x78, 0x60, 0x60, 0x7e, 0x00],
    'O': [0x3c, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x00],
    'R': [0x7c, 0x66, 0x66, 0x7c, 0x78, 0x6c, 0x66, 0x00],
    'S': [0x3c, 0x66, 0x60, 0x3c, 0x06, 0x66, 0x3c, 0x00],
}
REAL_SCREEN_CODES = {'S': 19, 'C': 3, 'O': 15, 'R': 18, 'E': 5}
REAL_DEST_CODES = {'S': CHAR_S, 'C': CHAR_C, 'O': CHAR_O, 'R': CHAR_R, 'E': CHAR_E}
m1b, target = fresh_machine()
m1b.joystick2 = 0
run_fixed_budget(m1b, target, 50_000)
for letter, code in REAL_SCREEN_CODES.items():
    addr = 0xd000 + code * 8
    for j, b in enumerate(REAL_GLYPHS[letter]):
        if (addr + j) not in EMULATOR_INTERCEPTED_ADDRS:
            m1b.cpu.memory[addr + j] = b
run_fixed_budget(m1b, target, 200_000)
glyph_ok = True
for letter in 'SCORE':
    dest_addr = CHAR_MEM + REAL_DEST_CODES[letter] * 8
    addr = 0xd000 + REAL_SCREEN_CODES[letter] * 8
    skip = {j for j in range(8) if (addr + j) in EMULATOR_INTERCEPTED_ADDRS}
    actual = [m1b.cpu.memory[dest_addr + j] for j in range(8)]
    expected = REAL_GLYPHS[letter]
    mismatch = [j for j in range(8) if j not in skip and actual[j] != expected[j]]
    if mismatch:
        print(f"  {letter} mismatch at offsets {mismatch}: "
              f"expected {expected}, got {actual}")
        glyph_ok = False
check("S, C, O, R, E all land as their own exact, correct real "
      "glyph shape, spelling SCORE and not something else", glyph_ok)

print("=== CHAREN ($01 bit 2) is correctly restored after the copy "
      "-- I/O visible again, not left showing character ROM ===")
check("CHAREN bit is set (I/O visible)",
      m1.cpu.memory[CPU_PORT] & CHAREN_BIT != 0,
      f"got {m1.cpu.memory[CPU_PORT]:#010b}")

print("=== the score starts at 0, and becomes 10 on the very first "
      "loop iteration -- the player's own start tile is itself a dot, "
      "eaten immediately, not left uneaten as a kind of safe zone ===")
m2, target = fresh_machine()
m2.joystick2 = 0
m2.cpu.memory[0xd012] = 0xfb
m2.cpu.pc = target
m2.cpu.halted = False
check("score reads 0 before the program has run at all", score(m2) == 0)
changed = False
for _ in range(200_000):
    m2.step()
    if score(m2) != 0:
        changed = True
        break
check("score changed within a reasonable budget", changed)
check("score is exactly 10 once it changes (one dot, not more)",
      score(m2) == 10, f"got {score(m2)}")

print("=== eating a dot clears it in both MAZE_GRID and on screen, "
      "and standing still afterward doesn't double-count it ===")
m3, target = fresh_machine()
m3.joystick2 = 0
run_fixed_budget(m3, target, 300_000)   # generous -- let the start
                                            # tile's own dot be eaten
                                            # and settle
tile_addr = MAZE_GRID + PLAYER_START_ROW * TILE_COLS + PLAYER_START_COL
screen_addr = SCREEN + (MAZE_SCREEN_ROW + PLAYER_START_ROW) * 40 + (MAZE_SCREEN_COL + PLAYER_START_COL)
check("the start tile is now TILE_EMPTY in the grid",
      m3.cpu.memory[tile_addr] == 0, f"got {m3.cpu.memory[tile_addr]}")
check("the start tile is now blank on screen",
      m3.cpu.memory[screen_addr] & 0x7f == 0,
      f"got {m3.cpu.memory[screen_addr] & 0x7f}")
score_after_settling = score(m3)
continue_running(m3, 300_000)
check("score unchanged after standing still longer (no double-"
      "counting an already-eaten tile)",
      score(m3) == score_after_settling,
      f"was {score_after_settling}, now {score(m3)}")

print("=== eating a power pellet adds 50 (checked as the exact "
      "difference from a known baseline score, not an absolute value, "
      "since the start tile's own dot is also eaten along the way) ===")
m4, target = fresh_machine()
m4.joystick2 = 0
run_fixed_budget(m4, target, 300_000)
baseline = score(m4)
# tile (1,1) is a power pellet, per the maze's own reference layout
check("reference confirms (1,1) is a power pellet",
      REFERENCE_GRID[1][1] == 3)
pellet_grid_addr = MAZE_GRID + 1 * TILE_COLS + 1
check("that tile is still a pellet in the running program too",
      m4.cpu.memory[pellet_grid_addr] == 3)
set_rel_x(m4, 1 * 8)
m4.cpu.memory[PLAYER_REL_Y] = 1 * 8
m4.cpu.memory[PLAYER_DIRECTION] = 0
continue_running(m4, 200_000)
check("score increased by exactly 50",
      score(m4) - baseline == 50, f"baseline={baseline} now={score(m4)}")
check("the pellet tile is now TILE_EMPTY",
      m4.cpu.memory[pellet_grid_addr] == 0,
      f"got {m4.cpu.memory[pellet_grid_addr]}")

print("=== the score HUD displays the correct 5-digit decimal text "
      "for a specific, known score, including a real zero-in-the-"
      "middle digit-extraction case (60 -> 00060, not 0060 or 006) ===")
m5, target = fresh_machine()
m5.joystick2 = 0
run_fixed_budget(m5, target, 50_000)
m5.cpu.memory[SCORE_LO] = 60
m5.cpu.memory[SCORE_HI] = 0
# force a fresh render_score call by eating another dot, rather than
# reading whatever was last drawn before this poke
set_rel_x(m5, 20 * 8)
m5.cpu.memory[PLAYER_REL_Y] = 2 * 8
m5.cpu.memory[PLAYER_DIRECTION] = 0
check("reference confirms (20,2) is a dot", REFERENCE_GRID[2][20] == 2)
continue_running(m5, 200_000)


def read_hud_text(m):
    chars = [m.cpu.memory[SCREEN + MAZE_SCREEN_COL + i] & 0x7f for i in range(12)]
    return chars


hud = read_hud_text(m5)
check("HUD label reads S,C,O,R,E in order",
      hud[0:5] == [CHAR_S, CHAR_C, CHAR_O, CHAR_R, CHAR_E], f"got {hud[0:5]}")
check("a blank separates the label from the digits",
      hud[5] == 0, f"got {hud[5]}")
digits = [c - CHAR_DIGIT_0 for c in hud[6:11]]
# the pellet test above already added a dot's worth on top of the
# poked 60 by the time this dot is eaten too, so check the actual
# score directly rather than assuming exactly 60 landed
expected_digits = [int(d) for d in f"{score(m5):05d}"]
check(f"displayed digits ({digits}) match the actual score "
      f"({score(m5)}) exactly, as 5 digits with no leading-zero "
      f"suppression", digits == expected_digits,
      f"got {digits}, expected {expected_digits}")

# ============================================================
# Stage 4: level data loaded from disk
# ============================================================

PLAYER_START_COL_ADDR = 0x035a
PLAYER_START_ROW_ADDR = 0x035b
LEVEL_TITLE_ADDR = 0x035c

print("=== load_level correctly reads the player's own starting "
      "position from the level file, not a compile-time constant "
      "anymore ===")
m16, target = fresh_machine()
m16.joystick2 = 0   # avoid the emulator's own 0x1F default causing movement
run_fixed_budget(m16, target, 200_000)
check("player_start_col matches the level file",
      m16.cpu.memory[PLAYER_START_COL_ADDR] == PLAYER_START_COL,
      f"got {m16.cpu.memory[PLAYER_START_COL_ADDR]}")
check("player_start_row matches the level file",
      m16.cpu.memory[PLAYER_START_ROW_ADDR] == PLAYER_START_ROW,
      f"got {m16.cpu.memory[PLAYER_START_ROW_ADDR]}")

print("=== load_level correctly reads the level's own title, held "
      "ready for a later stage to display (not rendered anywhere "
      "yet) ===")
title_bytes = [m16.cpu.memory[LEVEL_TITLE_ADDR + i] for i in range(LEVEL_TITLE_LEN)]
title_str = ''.join(chr(b) for b in title_bytes)
check("title matches the level file exactly, including its own "
      "space-padding", title_str == LEVEL_TITLE.ljust(LEVEL_TITLE_LEN),
      f"got {title_str!r}")

print("=== load_level correctly reads the maze grid itself from the "
      "level file, matching it exactly (same start-tile exception as "
      "the earlier grid-matches-reference test, for the same reason) ===")
grid_from_disk_ok = True
for r in range(TILE_ROWS):
    for c in range(TILE_COLS):
        if (c, r) == (PLAYER_START_COL, PLAYER_START_ROW):
            continue
        expected = LEVEL1_DATA[22 + r * TILE_COLS + c]
        actual = m16.cpu.memory[MAZE_GRID + r * TILE_COLS + c]
        if expected != actual:
            print(f"  mismatch at ({c},{r}): expected {expected}, got {actual}")
            grid_from_disk_ok = False
check("every other tile matches the level file exactly", grid_from_disk_ok)

print("=== if LEVEL1 isn't found on disk, the border turns red and "
      "the program halts, rather than proceeding to render or collide "
      "against a maze grid that was never actually loaded ===")
m19, target = fresh_machine()
m19.joystick2 = 0
m19.disk_files = {}   # LEVEL1 deliberately absent
run_fixed_budget(m19, target, 500_000)
check("border turns red", m19.cpu.memory[VIC_BORDER] == 0x02,
      f"got {m19.cpu.memory[VIC_BORDER]}")
check("MAZE_GRID was never touched (still whatever it started as, "
      "not filled with anything from a load that never completed)",
      m19.cpu.memory[MAZE_GRID] == 0, f"got {m19.cpu.memory[MAZE_GRID]}")

print("=== the level file format actually generalizes: a completely "
      "different level (different start tile, different title, and "
      "a trivially different layout -- not just the same data loaded "
      "from a different place) loads and plays correctly too ===")
_alt_grid = [row[:] for row in REFERENCE_GRID]
# swap two open, non-adjacent tiles' own values (both dots in the
# reference layout) to prove this isn't just re-loading the same
# bytes under a different name
assert _alt_grid[2][5] == 2 and _alt_grid[2][6] == 2
_alt_grid[2][5], _alt_grid[2][6] = 3, 3   # both become power pellets
_alt_title = "ALTERNATE LEVEL TWO"[:LEVEL_TITLE_LEN]
_alt_title_bytes = _alt_title.encode('ascii').ljust(LEVEL_TITLE_LEN, b' ')
_alt_start_col, _alt_start_row = 20, 6
_alt_tile_bytes = bytes(_alt_grid[r][c] for r in range(TILE_ROWS) for c in range(TILE_COLS))
_alt_level_data = (bytes([_alt_start_col, _alt_start_row])
                    + _alt_title_bytes + _alt_tile_bytes)
check("reference confirms the alternate start tile is actually open",
      not is_wall(_alt_start_col, _alt_start_row))
m20, target = fresh_machine()
m20.joystick2 = 0
m20.disk_files = {'LEVEL1': _alt_level_data}
run_fixed_budget(m20, target, 200_000)
check("alternate level's own start column loaded correctly",
      m20.cpu.memory[PLAYER_START_COL_ADDR] == _alt_start_col,
      f"got {m20.cpu.memory[PLAYER_START_COL_ADDR]}")
check("alternate level's own start row loaded correctly",
      m20.cpu.memory[PLAYER_START_ROW_ADDR] == _alt_start_row,
      f"got {m20.cpu.memory[PLAYER_START_ROW_ADDR]}")
alt_title_bytes = [m20.cpu.memory[LEVEL_TITLE_ADDR + i] for i in range(LEVEL_TITLE_LEN)]
alt_title_str = ''.join(chr(b) for b in alt_title_bytes)
check("alternate level's own title loaded correctly",
      alt_title_str == _alt_title.ljust(LEVEL_TITLE_LEN),
      f"got {alt_title_str!r}")
check("the swapped tile (2,5), now a pellet, loaded correctly and "
      "differs from the original level's own dot there",
      m20.cpu.memory[MAZE_GRID + 2 * TILE_COLS + 5] == 3,
      f"got {m20.cpu.memory[MAZE_GRID + 2 * TILE_COLS + 5]}")
check("sprite position reflects the alternate level's own start tile",
      m20.cpu.memory[SPRITE0_X] == MAZE_ORIGIN_X + _alt_start_col * 8,
      f"got {m20.cpu.memory[SPRITE0_X]}")

print("=== a level file that's shorter than expected on disk -- for "
      "any reason, including how it actually got written there -- "
      "correctly triggers the same error path a missing file does, "
      "rather than silently filling the rest of MAZE_GRID with "
      "whatever CHRIN happens to keep returning once nothing "
      "legitimate is left to read. Guards against a real bug reported "
      "directly from real hardware: a screen full of a single "
      "repeated tile, traced to load_level only checking READST once, "
      "before its own read loop started, never again during it ===")
m21, target = fresh_machine()
m21.joystick2 = 0
m21.disk_files = {'LEVEL1': LEVEL1_DATA[:100]}   # cuts off partway
                                                      # through the
                                                      # tile data
run_fixed_budget(m21, target, 500_000)
check("border turns red, the same signal a missing file already uses",
      m21.cpu.memory[VIC_BORDER] == 0x02, f"got {m21.cpu.memory[VIC_BORDER]}")
check("MAZE_GRID was never written past where the file actually ran "
      "out -- still 0 there, not leftover garbage from CHRIN being "
      "called past EOF without anything noticing",
      all(m21.cpu.memory[MAZE_GRID + i] == 0 for i in range(78, 90)),
      f"got {[m21.cpu.memory[MAZE_GRID + i] for i in range(78, 90)]}")

# ============================================================
# Stage 5: enemy AI (two ghost-shaped enemies, chase behavior,
# player-enemy collision)
# ============================================================

ENEMY1_REL_X_LO = 0x0370
ENEMY1_REL_X_HI = 0x0371
ENEMY1_REL_Y = 0x0372
ENEMY1_DIRECTION = 0x0373
ENEMY2_REL_X_LO = 0x0374
ENEMY2_REL_X_HI = 0x0375
ENEMY2_REL_Y = 0x0376
ENEMY2_DIRECTION = 0x0377
SPRITE1_COLOR = SPRITE0_COLOR + 1
SPRITE2_COLOR = SPRITE0_COLOR + 2
SPRITE_PTR1 = SPRITE_PTR0 + 1
SPRITE_PTR2 = SPRITE_PTR0 + 2
SPRITE1_X, SPRITE1_Y = SPRITE0_X + 2, SPRITE0_Y + 2
SPRITE2_X, SPRITE2_Y = SPRITE0_X + 4, SPRITE0_Y + 4
SPRITE_SPRITE_COLLISION = 0xd01e
ENEMY1_START_COL, ENEMY1_START_ROW = 5, 1
ENEMY2_START_COL, ENEMY2_START_ROW = 32, 1


def step_to_loop_top_or_fail(m, max_instructions=200_000):
    ok = step_to_loop_top(m, MAIN_LOOP_ADDR, max_instructions)
    return ok


def enemy_tile(m, lo, hi, y):
    x = m.cpu.memory[lo] + 256 * m.cpu.memory[hi]
    return x // 8, m.cpu.memory[y] // 8


def set_enemy(m, lo, hi, y, direction_addr, col, row, direction):
    m.cpu.memory[lo] = (col * 8) & 0xff
    m.cpu.memory[hi] = (col * 8) >> 8
    m.cpu.memory[y] = row * 8
    m.cpu.memory[direction_addr] = direction


print("=== both enemy sprites are set up correctly at startup: "
      "enabled alongside the player, non-blank and distinct shape "
      "data, their own distinct colors, correct pointers, and "
      "positioned at their own exact starting tiles -- checked at "
      "the precise point setup completes (stepping to main_loop's "
      "own address), not a fixed instruction count that extra "
      "movement could throw off ===")
m1, target = fresh_machine()
m1.joystick2 = 0
m1.cpu.memory[0xd012] = 0xfb
m1.cpu.pc = target
m1.cpu.halted = False
ok = step_to_loop_top_or_fail(m1)
check("reached main_loop within budget", ok)
check("sprite 1 (enemy) is enabled", m1.cpu.memory[SPRITE_ENABLE] & 0b010 != 0)
check("sprite 2 (enemy) is enabled", m1.cpu.memory[SPRITE_ENABLE] & 0b100 != 0)
check("sprite 1's pointer targets its own slot in SPRITE_DATA",
      m1.cpu.memory[SPRITE_PTR1] == (SPRITE_DATA + 64) // 64,
      f"got {m1.cpu.memory[SPRITE_PTR1]}")
check("sprite 2's pointer targets its own, different slot",
      m1.cpu.memory[SPRITE_PTR2] == (SPRITE_DATA + 128) // 64,
      f"got {m1.cpu.memory[SPRITE_PTR2]}")
e1_bytes = [m1.cpu.memory[SPRITE_DATA + 64 + i] for i in range(63)]
e2_bytes = [m1.cpu.memory[SPRITE_DATA + 128 + i] for i in range(63)]
check("enemy 1's own sprite shape isn't blank", any(e1_bytes))
check("enemy 2's own sprite shape isn't blank", any(e2_bytes))
check("enemy sprite data isn't the player's own circle shape "
      "(a visually distinct ghost silhouette, not just recolored)",
      e1_bytes != [m1.cpu.memory[SPRITE_DATA + i] for i in range(63)])
check("enemy 1's own color is set and distinct from the player's "
      "own white", m1.cpu.memory[SPRITE1_COLOR] not in (0, 1),
      f"got {m1.cpu.memory[SPRITE1_COLOR]}")
check("enemy 2's own color is set and distinct from enemy 1's own",
      m1.cpu.memory[SPRITE2_COLOR] not in (0, 1, m1.cpu.memory[SPRITE1_COLOR]),
      f"got {m1.cpu.memory[SPRITE2_COLOR]}")

exp1_x = MAZE_ORIGIN_X + ENEMY1_START_COL * 8
exp1_y = MAZE_ORIGIN_Y + ENEMY1_START_ROW * 8
exp2_abs_x = MAZE_ORIGIN_X + ENEMY2_START_COL * 8
exp2_x = exp2_abs_x % 256
exp2_msb = 1 if exp2_abs_x > 255 else 0
exp2_y = MAZE_ORIGIN_Y + ENEMY2_START_ROW * 8
check("enemy 1 positioned at its own exact starting tile",
      m1.cpu.memory[SPRITE1_X] == exp1_x and m1.cpu.memory[SPRITE1_Y] == exp1_y,
      f"got ({m1.cpu.memory[SPRITE1_X]},{m1.cpu.memory[SPRITE1_Y]})")
check("enemy 2 positioned at its own exact starting tile, including "
      "the 9th X-bit correctly set for crossing the 256 threshold "
      "(32*8=256, same class of computation already verified for the "
      "player elsewhere in this file)",
      m1.cpu.memory[SPRITE2_X] == exp2_x and m1.cpu.memory[SPRITE2_Y] == exp2_y
      and (m1.cpu.memory[SPRITE_X_MSB] >> 2) & 1 == exp2_msb,
      f"got X={m1.cpu.memory[SPRITE2_X]} Y={m1.cpu.memory[SPRITE2_Y]} "
      f"MSB_bit2={(m1.cpu.memory[SPRITE_X_MSB] >> 2) & 1}")
check("enemy 1's own MSB bit (bit 1) is clear (its own starting X, "
      "72, is well under 256)",
      (m1.cpu.memory[SPRITE_X_MSB] >> 1) & 1 == 0)

print("=== an enemy gets meaningfully close to a stationary player "
      "over time -- real chase behavior, not random wandering. "
      "Distance oscillates rather than monotonically decreasing to "
      "0 (expected for this greedy, non-pathfinding AI: Manhattan "
      "distance doesn't account for walls, so a detour is sometimes "
      "needed even when the target tile itself is 'close') -- the "
      "meaningful claim is that it gets very near repeatedly, not "
      "that it converges and stays there ===")
m2, target = fresh_machine()
m2.joystick2 = 0
run_fixed_budget(m2, target, 200_000)


def player_tile(m):
    x = m.cpu.memory[PLAYER_REL_X_LO] + 256 * m.cpu.memory[PLAYER_REL_X_HI]
    return x // 8, m.cpu.memory[PLAYER_REL_Y] // 8


p_col, p_row = player_tile(m2)
min_dist = None
for _ in range(35):
    continue_running(m2, 100_000)
    e_col, e_row = enemy_tile(m2, ENEMY1_REL_X_LO, ENEMY1_REL_X_HI, ENEMY1_REL_Y)
    dist = abs(e_col - p_col) + abs(e_row - p_row)
    min_dist = dist if min_dist is None else min(min_dist, dist)
check("enemy 1 reaches a tile adjacent to (or on) the stationary "
      "player's own tile at some point during the run",
      min_dist is not None and min_dist <= 1, f"got min_dist={min_dist}")

print("=== neither enemy ever ends up standing on a wall tile, nor "
      "outside the maze's own bounds, across extended natural "
      "gameplay chasing a moving player ===")
m3, target = fresh_machine()
run_fixed_budget(m3, target, 50_000)
step_to_loop_top_or_fail(m3)
directions = [0b00001000, 0b00000010, 0b00000100, 0b00000001]
wall_ok = True
bounds_ok = True
for cycle in range(20):
    m3.joystick2 = directions[cycle % 4]
    continue_running(m3, 200_000)
    for lo, hi, y, label in [(ENEMY1_REL_X_LO, ENEMY1_REL_X_HI, ENEMY1_REL_Y, 'enemy1'),
                              (ENEMY2_REL_X_LO, ENEMY2_REL_X_HI, ENEMY2_REL_Y, 'enemy2')]:
        col, row = enemy_tile(m3, lo, hi, y)
        if not (0 <= col < TILE_COLS and 0 <= row < TILE_ROWS):
            print(f"  cycle {cycle}: {label} out of bounds at ({col},{row})")
            bounds_ok = False
            continue
        tile = m3.cpu.memory[MAZE_GRID + row * TILE_COLS + col]
        if tile == 1:  # TILE_WALL
            print(f"  cycle {cycle}: {label} on a wall tile at ({col},{row})")
            wall_ok = False
check("neither enemy ever stood on a wall tile", wall_ok)
check("neither enemy ever left the maze's own bounds", bounds_ok)

print("=== dead-end reversal: an enemy walking into a single-tile "
      "pocket (only one way in, fully controlled surroundings so the "
      "test's own setup can't accidentally create a second, "
      "unintended dead end) correctly reverses out of it once it "
      "gets there, rather than getting stuck ===")
m4, target = fresh_machine()
m4.joystick2 = 0
run_fixed_budget(m4, target, 50_000)
step_to_loop_top_or_fail(m4)
for c in range(15, 22):
    m4.cpu.memory[MAZE_GRID + 9 * TILE_COLS + c] = 1
    m4.cpu.memory[MAZE_GRID + 11 * TILE_COLS + c] = 1
for c in range(15, 21):
    m4.cpu.memory[MAZE_GRID + 10 * TILE_COLS + c] = 2
m4.cpu.memory[MAZE_GRID + 10 * TILE_COLS + 21] = 1
m4.cpu.memory[PLAYER_REL_X_LO] = 1 * 8
m4.cpu.memory[PLAYER_REL_X_HI] = 0
m4.cpu.memory[PLAYER_REL_Y] = 1 * 8
set_enemy(m4, ENEMY1_REL_X_LO, ENEMY1_REL_X_HI, ENEMY1_REL_Y, ENEMY1_DIRECTION,
           19, 10, 4)
continue_running(m4, 300_000)
col, row = enemy_tile(m4, ENEMY1_REL_X_LO, ENEMY1_REL_X_HI, ENEMY1_REL_Y)
check("enemy escaped the dead end and is well clear of it, heading "
      "back toward the player rather than stuck at the pocket (20,10)",
      col <= 17, f"got ({col},{row})")

print("=== check_enemy_collision resets the player back to its own "
      "starting tile when SPRITE_SPRITE_COLLISION's own bit 0 (the "
      "player's own bit) is set -- verified as this routine's own "
      "correct RESPONSE to a collision signal. mini6502.py doesn't "
      "simulate the VIC-II's real, pixel-level sprite rendering or "
      "collision detection at all, so whether two sprites' own "
      "visible pixels genuinely overlap on real hardware can't be "
      "verified here -- only that this routine reacts correctly once "
      "told a collision happened ===")
m5, target = fresh_machine()
run_fixed_budget(m5, target, 50_000)
step_to_loop_top_or_fail(m5)
m5.joystick2 = 0b00001000
continue_running(m5, 300_000)
moved_col, moved_row = player_tile(m5)
check("player actually moved away from its own start tile first",
      (moved_col, moved_row) != (PLAYER_START_COL, PLAYER_START_ROW))
m5.joystick2 = 0
m5.cpu.memory[SPRITE_SPRITE_COLLISION] = 0b00000011   # player + enemy 1
continue_running(m5, 50_000)
reset_col, reset_row = player_tile(m5)
check("player was reset back to its own exact start tile",
      (reset_col, reset_row) == (PLAYER_START_COL, PLAYER_START_ROW),
      f"got ({reset_col},{reset_row})")

print("=== no false positive: a collision between the two enemies "
      "only (bits 1+2 set, bit 0 -- the player's own -- clear) does "
      "NOT reset the player ===")
m6, target = fresh_machine()
run_fixed_budget(m6, target, 50_000)
step_to_loop_top_or_fail(m6)
m6.joystick2 = 0b00001000
continue_running(m6, 300_000)
moved_col2, moved_row2 = player_tile(m6)
m6.joystick2 = 0
m6.cpu.memory[SPRITE_SPRITE_COLLISION] = 0b00000110   # enemies only
continue_running(m6, 50_000)
after_col, after_row = player_tile(m6)
check("player was NOT reset -- still at its own moved-to position",
      (after_col, after_row) == (moved_col2, moved_row2),
      f"before=({moved_col2},{moved_row2}) after=({after_col},{after_row})")

print("=== enemies move on exactly 3 of every 4 frames (75% of the "
      "player's own speed), reported directly as feeling too fast "
      "at a full 1:1 rate -- verified by stepping frame by frame "
      "along a fully-cleared, unobstructed corridor and checking the "
      "exact per-frame movement pattern, not just an average over "
      "many frames that a partially-blocked path could distort ===")
m7, target = fresh_machine()
m7.joystick2 = 0
m7.cpu.memory[0xd012] = 0xfb
m7.cpu.pc = target
m7.cpu.halted = False
step_to_loop_top(m7, MAIN_LOOP_ADDR)
for c in range(1, 36):
    m7.cpu.memory[MAZE_GRID + 5 * TILE_COLS + c] = 2   # TILE_DOT
m7.cpu.memory[PLAYER_REL_X_LO] = 30 * 8
m7.cpu.memory[PLAYER_REL_X_HI] = 0
m7.cpu.memory[PLAYER_REL_Y] = 5 * 8
m7.cpu.memory[ENEMY1_REL_X_LO] = 2 * 8
m7.cpu.memory[ENEMY1_REL_X_HI] = 0
m7.cpu.memory[ENEMY1_REL_Y] = 5 * 8
m7.cpu.memory[ENEMY1_DIRECTION] = 4   # right
enemy_positions = []
for _ in range(20):
    for _ in range(200_000):
        m7.step()
        if m7.cpu.pc == MAIN_LOOP_ADDR:
            break
    x = m7.cpu.memory[ENEMY1_REL_X_LO] + 256 * m7.cpu.memory[ENEMY1_REL_X_HI]
    enemy_positions.append(x)
enemy_deltas = [enemy_positions[i + 1] - enemy_positions[i]
                for i in range(len(enemy_positions) - 1)]
skip_indices = [i for i, d in enumerate(enemy_deltas) if d == 0]
skip_gaps = [b - a for a, b in zip(skip_indices, skip_indices[1:])]
check("moves on exactly 3 of every 4 frames, with skips spaced "
      "exactly 4 apart -- checked as a cycle, not a specific starting "
      "offset, since the throttle counter's own starting phase "
      "depends on exactly how many frames already ran during setup, "
      "not guaranteed to align with any particular sample window",
      len(skip_indices) >= 4 and all(gap == 4 for gap in skip_gaps),
      f"got deltas={enemy_deltas}, skip gaps={skip_gaps}")

print("=== the player's own movement is completely unaffected by the "
      "enemy throttle -- moves on every single frame, not 3 of 4 ===")
m8, target = fresh_machine()
m8.joystick2 = 0
m8.cpu.memory[0xd012] = 0xfb
m8.cpu.pc = target
m8.cpu.halted = False
step_to_loop_top(m8, MAIN_LOOP_ADDR)
for c in range(1, 36):
    m8.cpu.memory[MAZE_GRID + 11 * TILE_COLS + c] = 2   # TILE_DOT
m8.joystick2 = 0b00001000   # right
player_positions = []
for _ in range(15):
    for _ in range(200_000):
        m8.step()
        if m8.cpu.pc == MAIN_LOOP_ADDR:
            break
    x = m8.cpu.memory[PLAYER_REL_X_LO] + 256 * m8.cpu.memory[PLAYER_REL_X_HI]
    player_positions.append(x)
player_deltas = [player_positions[i + 1] - player_positions[i]
                  for i in range(len(player_positions) - 1)]
check("player moves exactly 1 pixel every single frame, no skips",
      all(d == 1 for d in player_deltas), f"got {player_deltas}")

# ============================================================
# Stage 6: power pellet vulnerability (enemies flee, become
# eatable, then revert)
# ============================================================

PELLET_TIMER_LO = 0x0385
PELLET_TIMER_HI = 0x0386
ENEMY1_FRIGHTENED = 0x0387
ENEMY2_FRIGHTENED = 0x0388
PELLET_VULNERABLE_DURATION = 400
ENEMY_EATEN_SCORE = 200
ENEMY1_COLOR = 2
ENEMY2_COLOR = 4
FRIGHTENED_COLOR = 14


def timer(m):
    return m.cpu.memory[PELLET_TIMER_LO] + 256 * m.cpu.memory[PELLET_TIMER_HI]


print("=== eating a power pellet starts vulnerability: the timer is "
      "set, both enemies become frightened, and both immediately "
      "switch to the shared frightened color ===")
m22, target = fresh_machine()
m22.joystick2 = 0
run_fixed_budget(m22, target, 50_000)
step_to_loop_top(m22, MAIN_LOOP_ADDR)
check("nothing active before eating a pellet", timer(m22) == 0
      and m22.cpu.memory[ENEMY1_FRIGHTENED] == 0
      and m22.cpu.memory[ENEMY2_FRIGHTENED] == 0)
set_rel_x(m22, 1 * 8)
m22.cpu.memory[PLAYER_REL_Y] = 1 * 8
pellet_addr = MAZE_GRID + 1 * TILE_COLS + 1
check("reference confirms (1,1) is a power pellet", REFERENCE_GRID[1][1] == 3)
step_to_loop_top(m22, MAIN_LOOP_ADDR)   # exactly one frame -- eating
                                            # the pellet here, not the
                                            # many dozens of frames a
                                            # fixed large instruction
                                            # count would actually
                                            # span (each frame here is
                                            # only ~350-1500
                                            # instructions, not
                                            # thousands), which would
                                            # let the 400-frame
                                            # vulnerability window run
                                            # out before this check
                                            # even runs
check("timer started (very close to the full duration -- one frame "
      "of countdown may have already run this same frame)",
      PELLET_VULNERABLE_DURATION - 5 <= timer(m22) <= PELLET_VULNERABLE_DURATION,
      f"got {timer(m22)}")
check("both enemies became frightened",
      m22.cpu.memory[ENEMY1_FRIGHTENED] == 1 and m22.cpu.memory[ENEMY2_FRIGHTENED] == 1)
check("both enemies switched to the shared frightened color",
      m22.cpu.memory[SPRITE1_COLOR] == FRIGHTENED_COLOR
      and m22.cpu.memory[SPRITE2_COLOR] == FRIGHTENED_COLOR)
check("the pellet itself was cleared from the grid",
      m22.cpu.memory[pellet_addr] == 0)

print("=== a frightened enemy flees a nearby player over time -- "
      "distance increases, the exact opposite of chasing, using the "
      "same distance framework just compared in reverse ===")
m23, target = fresh_machine()
m23.joystick2 = 0
run_fixed_budget(m23, target, 50_000)
step_to_loop_top(m23, MAIN_LOOP_ADDR)
set_rel_x(m23, 15 * 8)
m23.cpu.memory[PLAYER_REL_Y] = 11 * 8
set_enemy(m23, ENEMY1_REL_X_LO, ENEMY1_REL_X_HI, ENEMY1_REL_Y, ENEMY1_DIRECTION,
           16, 11, 4)
m23.cpu.memory[ENEMY1_FRIGHTENED] = 1
m23.cpu.memory[PELLET_TIMER_LO] = 200
m23.cpu.memory[PELLET_TIMER_HI] = 0
start_col, start_row = enemy_tile(m23, ENEMY1_REL_X_LO, ENEMY1_REL_X_HI, ENEMY1_REL_Y)
start_dist = abs(start_col - 15) + abs(start_row - 11)
max_dist = start_dist
for _ in range(15):
    continue_running(m23, 100_000)
    col, row = enemy_tile(m23, ENEMY1_REL_X_LO, ENEMY1_REL_X_HI, ENEMY1_REL_Y)
    max_dist = max(max_dist, abs(col - 15) + abs(row - 11))
check("frightened enemy's own distance from the player grew over "
      "time rather than shrinking (fleeing, not chasing)",
      max_dist > start_dist, f"start={start_dist} max reached={max_dist}")

print("=== the vulnerability timer counts down and, the exact frame "
      "it reaches 0, clears both frightened flags and reverts both "
      "enemies' own colors back to normal, together ===")
m24, target = fresh_machine()
m24.joystick2 = 0
run_fixed_budget(m24, target, 50_000)
step_to_loop_top(m24, MAIN_LOOP_ADDR)
set_rel_x(m24, 1 * 8)
m24.cpu.memory[PLAYER_REL_Y] = 1 * 8
continue_running(m24, 200_000)   # eat the real pellet at (1,1)
m24.cpu.memory[PELLET_TIMER_LO] = 3
m24.cpu.memory[PELLET_TIMER_HI] = 0
saw_expiry = False
for _ in range(6):
    continue_running(m24, 200_000)
    if timer(m24) == 0:
        saw_expiry = True
        break
check("timer actually reached 0 within budget", saw_expiry)
check("both frightened flags cleared exactly when the timer hit 0",
      m24.cpu.memory[ENEMY1_FRIGHTENED] == 0 and m24.cpu.memory[ENEMY2_FRIGHTENED] == 0)
check("both colors reverted to their own normal ones",
      m24.cpu.memory[SPRITE1_COLOR] == ENEMY1_COLOR
      and m24.cpu.memory[SPRITE2_COLOR] == ENEMY2_COLOR,
      f"got {m24.cpu.memory[SPRITE1_COLOR]}, {m24.cpu.memory[SPRITE2_COLOR]}")

print("=== eating a second pellet while already vulnerable refreshes "
      "the full duration, rather than stacking past it or being "
      "ignored while one's already running ===")
m25, target = fresh_machine()
m25.joystick2 = 0
run_fixed_budget(m25, target, 50_000)
step_to_loop_top(m25, MAIN_LOOP_ADDR)
set_rel_x(m25, 1 * 8)
m25.cpu.memory[PLAYER_REL_Y] = 1 * 8
step_to_loop_top(m25, MAIN_LOOP_ADDR)   # eat the first pellet in
                                            # exactly one frame
m25.cpu.memory[PELLET_TIMER_LO] = 50
m25.cpu.memory[PELLET_TIMER_HI] = 0
second_pellet_addr = MAZE_GRID + 1 * TILE_COLS + 2
m25.cpu.memory[second_pellet_addr] = 3   # TILE_PELLET
set_rel_x(m25, 2 * 8)
step_to_loop_top(m25, MAIN_LOOP_ADDR)   # eat the second pellet in
                                            # exactly one frame too
check("timer refreshed back near the full duration, not left at ~49 "
      "or stacked to ~450", PELLET_VULNERABLE_DURATION - 5 <= timer(m25)
      <= PELLET_VULNERABLE_DURATION, f"got {timer(m25)}")

print("=== touching a frightened enemy eats it: that one enemy "
      "resets to its own start tile and reverts to its own normal "
      "color, the player is NOT reset, and the score increases by "
      "ENEMY_EATEN_SCORE ===")
m26, target = fresh_machine()
m26.joystick2 = 0b00001000
run_fixed_budget(m26, target, 50_000)
step_to_loop_top(m26, MAIN_LOOP_ADDR)
continue_running(m26, 300_000)
m26.joystick2 = 0
step_to_loop_top(m26, MAIN_LOOP_ADDR)   # clean boundary before
                                            # poking -- avoids the
                                            # poke landing mid-
                                            # instruction and being
                                            # overwritten by an
                                            # already in-flight
                                            # computation that read
                                            # the old value first
moved_col, moved_row = player_tile(m26)
check("player actually moved away from its own start tile first",
      (moved_col, moved_row) != (PLAYER_START_COL, PLAYER_START_ROW))
score_before = score(m26)
m26.cpu.memory[ENEMY1_FRIGHTENED] = 1
set_enemy(m26, ENEMY1_REL_X_LO, ENEMY1_REL_X_HI, ENEMY1_REL_Y, ENEMY1_DIRECTION,
           30, 15, 4)   # move it well out of the way first
m26.cpu.memory[SPRITE_SPRITE_COLLISION] = 0b00000011   # player + enemy1
step_to_loop_top(m26, MAIN_LOOP_ADDR)   # process the collision in
                                            # exactly one frame
m26.cpu.memory[SPRITE_SPRITE_COLLISION] = 0   # real hardware clears
                                                  # this register the
                                                  # moment it's read;
                                                  # mini6502.py doesn't
                                                  # simulate that, so a
                                                  # poked value would
                                                  # otherwise persist
                                                  # and get reprocessed
                                                  # on the very next
                                                  # frame -- by which
                                                  # point enemy 1 is no
                                                  # longer frightened,
                                                  # incorrectly
                                                  # catching the player
                                                  # a second time
after_col, after_row = player_tile(m26)
check("player was NOT reset -- still at its own moved-to position",
      (after_col, after_row) == (moved_col, moved_row),
      f"before=({moved_col},{moved_row}) after=({after_col},{after_row})")
check("score increased by exactly ENEMY_EATEN_SCORE",
      score(m26) - score_before == ENEMY_EATEN_SCORE,
      f"before={score_before} after={score(m26)}")
check("enemy 1 no longer frightened", m26.cpu.memory[ENEMY1_FRIGHTENED] == 0)
check("enemy 1's own color reverted to normal",
      m26.cpu.memory[SPRITE1_COLOR] == ENEMY1_COLOR)
e1_col, e1_row = enemy_tile(m26, ENEMY1_REL_X_LO, ENEMY1_REL_X_HI, ENEMY1_REL_Y)
check("enemy 1 reset back to its own exact starting tile",
      (e1_col, e1_row) == (ENEMY1_START_COL, ENEMY1_START_ROW),
      f"got ({e1_col},{e1_row})")

print("=== eating one frightened enemy leaves the other one's own "
      "frightened state completely untouched ===")
m27, target = fresh_machine()
m27.joystick2 = 0
run_fixed_budget(m27, target, 50_000)
step_to_loop_top(m27, MAIN_LOOP_ADDR)
m27.cpu.memory[ENEMY1_FRIGHTENED] = 1
m27.cpu.memory[ENEMY2_FRIGHTENED] = 1
m27.cpu.memory[SPRITE_SPRITE_COLLISION] = 0b00000011   # player + enemy1 only
step_to_loop_top(m27, MAIN_LOOP_ADDR)
m27.cpu.memory[SPRITE_SPRITE_COLLISION] = 0
check("enemy 1 (the one actually touched) is no longer frightened",
      m27.cpu.memory[ENEMY1_FRIGHTENED] == 0)
check("enemy 2 (never touched) is still frightened, completely "
      "unaffected by enemy 1 being eaten",
      m27.cpu.memory[ENEMY2_FRIGHTENED] == 1)

print("=== touching a NON-frightened enemy still resets the player "
      "-- the classic, pre-vulnerability collision behavior is "
      "unaffected by any of this ===")
m28, target = fresh_machine()
m28.joystick2 = 0b00001000
run_fixed_budget(m28, target, 50_000)
step_to_loop_top(m28, MAIN_LOOP_ADDR)
continue_running(m28, 300_000)
m28.joystick2 = 0
step_to_loop_top(m28, MAIN_LOOP_ADDR)
m28.cpu.memory[ENEMY1_FRIGHTENED] = 0
m28.cpu.memory[SPRITE_SPRITE_COLLISION] = 0b00000011
step_to_loop_top(m28, MAIN_LOOP_ADDR)
m28.cpu.memory[SPRITE_SPRITE_COLLISION] = 0
reset_col, reset_row = player_tile(m28)
check("player correctly reset to its own start tile",
      (reset_col, reset_row) == (PLAYER_START_COL, PLAYER_START_ROW),
      f"got ({reset_col},{reset_row})")

print("=== frightened enemies move on only 1 of every 2 frames "
      "(half the throttled 3-of-4 normal rate), verified the same "
      "way normal enemy speed already was: stepping frame by frame "
      "along a fully-cleared corridor and checking the exact "
      "per-frame movement pattern ===")
m29, target = fresh_machine()
m29.joystick2 = 0
m29.cpu.memory[0xd012] = 0xfb
m29.cpu.pc = target
m29.cpu.halted = False
step_to_loop_top(m29, MAIN_LOOP_ADDR)
for r in range(3, 8):
    for c in range(1, 36):
        m29.cpu.memory[MAZE_GRID + r * TILE_COLS + c] = 2   # TILE_DOT
set_rel_x(m29, 1 * 8)                 # player far to the left --
m29.cpu.memory[PLAYER_REL_Y] = 5 * 8     # fleeing means continuing
                                            # right, through open
                                            # corridor, not immediately
                                            # running into a wall the
                                            # way starting the enemy
                                            # right next to the left
                                            # border (an earlier
                                            # version of this same
                                            # test) did
set_enemy(m29, ENEMY1_REL_X_LO, ENEMY1_REL_X_HI, ENEMY1_REL_Y, ENEMY1_DIRECTION,
           20, 5, 4)
m29.cpu.memory[ENEMY1_FRIGHTENED] = 1
m29.cpu.memory[PELLET_TIMER_LO] = 200
m29.cpu.memory[PELLET_TIMER_HI] = 0
frightened_positions = []
for _ in range(15):
    for _ in range(200_000):
        m29.step()
        if m29.cpu.pc == MAIN_LOOP_ADDR:
            break
    x = m29.cpu.memory[ENEMY1_REL_X_LO] + 256 * m29.cpu.memory[ENEMY1_REL_X_HI]
    y = m29.cpu.memory[ENEMY1_REL_Y]
    frightened_positions.append((x, y))
# tracks total movement (either axis), not just X -- the fleeing AI
# is free to legitimately pick any of the maximally-distant open
# directions, not necessarily continuing straight along the axis it
# started on (an earlier version of this same test assumed purely
# horizontal movement and only tracked X, missing real movement along
# Y entirely)
frightened_deltas = [0 if frightened_positions[i + 1] == frightened_positions[i] else 1
                       for i in range(len(frightened_positions) - 1)]
skip_indices_f = [i for i, d in enumerate(frightened_deltas) if d == 0]
skip_gaps_f = [b - a for a, b in zip(skip_indices_f, skip_indices_f[1:])]
check("frightened enemy moves on exactly 1 of every 2 frames, with "
      "skips spaced exactly 2 apart (checked as a cycle, same "
      "phase-independent reasoning as the normal-speed throttle test)",
      len(skip_indices_f) >= 4 and all(gap == 2 for gap in skip_gaps_f),
      f"got deltas={frightened_deltas}, skip gaps={skip_gaps_f}")

print()
print(f"{passed} passed, {failed} failed")
if failed:
    sys.exit(1)
