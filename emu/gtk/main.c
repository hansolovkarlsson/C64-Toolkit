/*
 * main.c - minimal GTK4 shell (../ROADMAP.md step 3, keyboard wired
 * through step 4's CIA1, real VIC-II text-mode rendering as of step
 * 5). Drives the whole machine (CPU + memory + both CIAs + VIC-II,
 * see ../src/machine.h) through a real GUI event loop.
 *
 * The window shows ../src/vic.c's first-pass VIC-II output: real
 * 40x25 hi-res text mode (screen RAM + character ROM/RAM + color RAM,
 * through whichever VIC bank CIA2 currently selects) plus a solid
 * border/background color - see vic.h's header comment for exactly
 * what's modeled and what's deliberately deferred (raster IRQs,
 * multicolor/bitmap modes, sprites).
 *
 * Keyboard events are wired to something real: GDK key events are
 * translated to C64 keyboard-matrix positions (see c64_keymap below)
 * and fed into CIA1 via machine_set_key(), so typing in this window
 * reaches BASIC/KERNAL once real ROM images are present (see
 * ../roms/README.md).
 *
 * Joystick: SDL2's SDL_GameController API (already a dependency for
 * audio - no new library needed) drives port 2, the port every
 * existing asm/examples/ demo with joystick support reads (see
 * pong.asm's own header comment). GTK4 itself has no gamepad API, and
 * SDL_GameController already ships wide, maintained mappings for
 * common pads (Xbox/PlayStation/etc.) across platforms, so there's no
 * reason to hand-roll HID parsing. Polled once per tick() (see
 * poll_joystick()) rather than driven by its own event callback -
 * SDL's controller/joystick device-added/removed events still need
 * pumping somewhere, and tick() already runs once per frame on the
 * one thread that's allowed to touch Machine. Only one controller is
 * supported at a time (the first one found); a second connecting
 * later without the first disconnecting is just ignored, not a
 * crash - not worth more than that for a single-joystick-port
 * default like port 2.
 *
 * Audio: SDL2 (SDL_OpenAudioDevice + a callback, see main()/
 * audio_callback()) plays back real SID output - see the
 * SID_SAMPLE_RATE/SID_CLOCK_HZ/audio_* comments below for how samples
 * get from sid_output() (pulled from the GTK main thread, the only
 * thread that ever touches Sid) into SDL's own separate real-time
 * audio thread without a data race. Never fatal if it fails to open -
 * the emulator just runs silently, the same as running with 0/3 ROMs.
 *
 * Optional --prg PATH: auto-runs a c64asm-built .prg (e.g. one of the
 * demos under ../../asm/examples/) once boot reaches a real READY. prompt -
 * see try_inject_prg(). This is c64asm's own toolchain output running
 * against a real, general-purpose emulator rather than asm/examples/'s
 * own mini6502.py test harness, so this is where things like real
 * VIC-II sprite/bitmap rendering and real SID audio actually get
 * exercised end-to-end, not just each chip's own hand-derived unit
 * tests.
 */

#include <gtk/gtk.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/machine.h"

/* PAL C64: ~985248 Hz CPU clock, ~50.12 Hz frame rate -> 19656
 * cycles/frame (PAL_CYCLES_PER_LINE * PAL_LINES_PER_FRAME, see
 * vic.h). Approximated with a 20ms GTK timer (~50 Hz) rather than
 * anything cycle-exact or actually raster-synchronized - vic.c
 * renders a whole frame at once, not scanline by scanline. */
#define CYCLES_PER_FRAME (PAL_CYCLES_PER_LINE * PAL_LINES_PER_FRAME)
#define FRAME_MS 20

/* The window opens at this multiple of the VIC's native canvas size
 * (see activate()) - the window is still ordinarily resizable, and
 * draw_screen() scales the render to fill whatever size it currently
 * is, so this only picks the STARTING zoom level, not a cap. */
#define WINDOW_ZOOM_DEFAULT 2

/* SDL_GameController axis values run -32768..32767; a small deadzone
 * around 0 avoids stick drift/noise registering as a held direction.
 * Chosen generously (about half of full deflection) since C64
 * joysticks are digital switches, not analog - there's no reason to
 * treat a light stick tilt as a "soft" press when the machine only
 * understands on/off anyway. */
#define JOYSTICK_AXIS_THRESHOLD 16000

/* Audio output (../ROADMAP.md step 7's last piece): SID itself (see
 * sid.h) has no notion of a sample rate - sid_tick() already advances
 * in lockstep with the CPU inside machine_step(), same as CIA/VIC, so
 * this file is where "how many samples is one real-world second worth"
 * gets decided, and where each sample actually gets pulled via
 * sid_output(). Two real bugs here, from two different (wrong) designs
 * this went through before landing on the one below:
 *
 * 1. sid_output() must be called INTERLEAVED with the cycle-stepping
 *    loop in tick() (one call per small slice of cycles, spread across
 *    the whole frame), not in a burst afterward - it only ever reads
 *    Sid's CURRENT instantaneous state, it doesn't advance anything
 *    itself, so calling it many times in a row with no cycles ticked
 *    in between reads back the exact same frozen value every time.
 *    Caught immediately by ear the first time this was tried without
 *    the interleaving: every sound came out as the same low, warbling
 *    tone - moving sample generation to after the loop downsampled
 *    every waveform to ~50Hz (one flat step per frame), aliasing
 *    anything above ~25Hz into low beat-frequency warbling.
 * 2. The RATE that interleaving is paced against must stay a FIXED
 *    constant (SID_CLOCK_HZ below) derived from CYCLES_PER_FRAME/
 *    FRAME_MS, the SAME nominal cycle-based clock CPU/VIC/CIA are all
 *    already implicitly paced against (see CYCLES_PER_FRAME's own
 *    comment) - NOT actual measured wall-clock time between tick()
 *    calls, even though real time is what SDL's audio hardware
 *    genuinely consumes samples against. An earlier version paced
 *    against real elapsed time instead (g_get_monotonic_time()),
 *    reasoning that g_timeout_add() doesn't guarantee exactly 20ms
 *    between calls (true) - but every OTHER subsystem (CPU speed, VIC
 *    animation speed, keyboard timing) stays paced against the SAME
 *    fixed nominal clock regardless of real-time drift, since
 *    CYCLES_PER_FRAME is a flat constant processed once per call no
 *    matter how long that call actually took in real time. Making
 *    ONLY audio track true wall-clock time decouples it from the very
 *    oscillator whose cycle-domain content it's sampling - confirmed
 *    by capturing real output and measuring its actual frequency: a
 *    tone that should measure ~720Hz consistently measured ~520-550Hz
 *    instead once real-time pacing was in the loop, a real, audible
 *    pitch error (every sound "sounding the same, low pitch", the
 *    long tone additionally "oscillating" - the same aliasing/beat-
 *    frequency character as bug 1, just milder since the drift was
 *    only in the RATE, not a full per-frame freeze).
 *
 * The real underrun problem the real-time experiment above was trying
 * to fix is genuine - GTK's timer and SDL's independently hardware-
 * clocked audio thread really don't stay perfectly in lockstep, this
 * emulator's own nominal ~50Hz clock genuinely runs a few percent
 * slower than real time on this system (unsurprising - g_timeout_add()
 * schedules relative to when the PREVIOUS call returned, not on a
 * fixed absolute schedule, so tick()'s own execution time - stepping
 * the CPU, rendering a frame, painting it - adds directly to the real
 * interval every single call). Measured directly: enlarging the ring
 * buffer from 8192 to 65536 samples changed NOTHING about the
 * underrun rate or the buffer's own steady-state fill level - a
 * PERSISTENT rate deficit drains any size buffer down to the same
 * near-empty equilibrium, it only takes a bigger buffer slightly
 * longer to get there. So this is a real, currently-accepted
 * trade-off, not something buffer sizing alone fixes: audio pitch is
 * correct (see above), and underruns are back to occasional brief
 * (a few ms) silence gaps rather than the earlier click or aliasing
 * bugs, but they haven't been eliminated outright - doing that
 * properly would mean making the WHOLE emulated machine's timing
 * genuinely real-time-locked (not just audio), a bigger change than
 * this file alone, and not undertaken here without weighing it
 * first. */
#define SID_SAMPLE_RATE 44100
#define SID_CLOCK_HZ (CYCLES_PER_FRAME * 1000 / FRAME_MS) /* == 982800, the SAME nominal clock CPU/VIC/CIA are implicitly paced against - see this section's own header comment for why audio has to match it exactly, not track real time independently */
#define AUDIO_RING_SAMPLES 16384 /* ~370ms at 44.1kHz - modest headroom against short jitter bursts; a much bigger buffer was measured to make no difference to the underrun rate at all (see this section's header comment) since the real deficit is persistent, not just jitter, so there's no point sizing this any larger */

typedef struct {
    Machine machine;
    GtkWidget *drawing_area;
    uint32_t *pixel_buf;  /* VIC_CANVAS_W x VIC_CANVAS_H, row stride given by pixel_stride (may have alignment padding - see activate()) */
    int pixel_stride;     /* in uint32_t units, not bytes */
    guint timeout_id;     /* the frame timer's source id, so it can be removed on window close - see on_window_destroy() */

    /* Audio: a small SPSC ring buffer bridging the GTK main thread
     * (the ONLY thread that ever calls sid_tick()/sid_output() - see
     * machine_step() in tick() below) and SDL's own dedicated real-time
     * audio callback thread (audio_callback(), which only ever READS
     * this ring, never touches Sid directly) - see tick()'s comment for
     * why ticking SID from two threads at once would be a real data
     * race, not just a style choice. SDL_LockAudioDevice()/
     * SDL_UnlockAudioDevice() (SDL's own documented mechanism for this
     * exact producer/consumer split - locking pauses callback
     * invocation, not the whole audio subsystem) guard every access
     * from the main-thread side; the callback needs no locking of its
     * own since SDL guarantees it never runs concurrently with a locked
     * section. audio_dev is 0 if SDL audio failed to initialize (see
     * main()) - the emulator still runs, just silently, the same
     * graceful-degradation spirit as running with 0/3 ROMs loaded. */
    SDL_AudioDeviceID audio_dev;
    double sample_cycle_accum; /* fractional SID clock cycles owed toward the next sample - see tick() */
    int16_t audio_ring[AUDIO_RING_SAMPLES];
    int audio_ring_read, audio_ring_write, audio_ring_count;

    /* NULL if no controller is connected - see poll_joystick(). Only
     * ever opened/closed/read from the GTK main thread, same as
     * everything else touching Machine, so no locking needed here
     * (unlike the audio ring buffer). */
    SDL_GameController *controller;

    /* Optional --prg PATH (see main()): a .prg to auto-inject and jump
     * straight into once boot reaches READY. - see tick()'s own
     * comment for why it waits for that rather than injecting
     * immediately. prg_state tracks progress so this only ever
     * happens once per run. */
    const char *prg_path;
    enum { PRG_NONE, PRG_PENDING, PRG_DONE } prg_state;
} App;

/* GDK keyval -> C64 keyboard matrix (pa_bit selects the column CIA1's
 * PRA drives low, pb_bit is the row CIA1's PRB reads back) - see
 * ../src/machine.h's key_matrix comment. Positions match the
 * standard, widely published C64 keyboard matrix chart. Not
 * exhaustive: no numeric keypad, no POUND/up-arrow keys, and Left/Up
 * arrow keys land on the same matrix position as Right/Down (a real
 * C64 has one physical CRSR L/R key and one CRSR U/D key; the
 * direction reverses with Shift, which isn't synthesized here) -
 * extend this table as a concrete need for either comes up. */
typedef struct {
    guint keyval;
    int pa, pb;
} KeyMapEntry;

static const KeyMapEntry c64_keymap[] = {
    /* letters (keyval already normalized to lowercase, see on_key()) */
    {GDK_KEY_a, 1, 2}, {GDK_KEY_b, 3, 4}, {GDK_KEY_c, 2, 4}, {GDK_KEY_d, 2, 2},
    {GDK_KEY_e, 1, 6}, {GDK_KEY_f, 2, 5}, {GDK_KEY_g, 3, 2}, {GDK_KEY_h, 3, 5},
    {GDK_KEY_i, 4, 1}, {GDK_KEY_j, 4, 2}, {GDK_KEY_k, 4, 5}, {GDK_KEY_l, 5, 2},
    {GDK_KEY_m, 4, 4}, {GDK_KEY_n, 4, 7}, {GDK_KEY_o, 4, 6}, {GDK_KEY_p, 5, 1},
    {GDK_KEY_q, 7, 6}, {GDK_KEY_r, 2, 1}, {GDK_KEY_s, 1, 5}, {GDK_KEY_t, 2, 6},
    {GDK_KEY_u, 3, 6}, {GDK_KEY_v, 3, 7}, {GDK_KEY_w, 1, 1}, {GDK_KEY_x, 2, 7},
    {GDK_KEY_y, 3, 1}, {GDK_KEY_z, 1, 4},

    /* digit row, plus the shifted symbol on the same physical key
     * (US layout keysyms) - the real Shift key press is what actually
     * tells the C64 it's shifted, this just makes sure the digit-row
     * matrix position still registers when the host reports the
     * shifted keysym instead of the bare digit. */
    {GDK_KEY_0, 4, 3}, {GDK_KEY_1, 7, 0}, {GDK_KEY_exclam, 7, 0},
    {GDK_KEY_2, 7, 3}, {GDK_KEY_quotedbl, 7, 3},
    {GDK_KEY_3, 1, 0}, {GDK_KEY_numbersign, 1, 0},
    {GDK_KEY_4, 1, 3}, {GDK_KEY_dollar, 1, 3},
    {GDK_KEY_5, 2, 0}, {GDK_KEY_percent, 2, 0},
    {GDK_KEY_6, 2, 3}, {GDK_KEY_ampersand, 2, 3},
    {GDK_KEY_7, 3, 0}, {GDK_KEY_apostrophe, 3, 0},
    {GDK_KEY_8, 3, 3}, {GDK_KEY_parenleft, 3, 3},
    {GDK_KEY_9, 4, 0}, {GDK_KEY_parenright, 4, 0},

    /* punctuation */
    {GDK_KEY_plus, 5, 0}, {GDK_KEY_minus, 5, 3}, {GDK_KEY_equal, 6, 5},
    {GDK_KEY_colon, 5, 5}, {GDK_KEY_semicolon, 6, 2}, {GDK_KEY_comma, 5, 7},
    {GDK_KEY_period, 5, 4}, {GDK_KEY_slash, 6, 7}, {GDK_KEY_at, 5, 6},
    {GDK_KEY_asterisk, 6, 1},

    /* whitespace/editing/control */
    {GDK_KEY_Return, 0, 1}, {GDK_KEY_space, 7, 4},
    {GDK_KEY_BackSpace, 0, 0}, {GDK_KEY_Delete, 0, 0},
    {GDK_KEY_Escape, 7, 7}, /* RUN/STOP */
    {GDK_KEY_Home, 6, 3},   /* HOME/CLR */
    {GDK_KEY_Left, 0, 2}, {GDK_KEY_Right, 0, 2},   /* CRSR L/R - see table comment */
    {GDK_KEY_Up, 0, 7}, {GDK_KEY_Down, 0, 7},      /* CRSR U/D */
    {GDK_KEY_Shift_L, 1, 7}, {GDK_KEY_Shift_R, 6, 4},
    {GDK_KEY_Control_L, 7, 2},
    {GDK_KEY_Alt_L, 7, 5}, /* stand-in for the Commodore key */

    {GDK_KEY_F1, 0, 4}, {GDK_KEY_F3, 0, 5}, {GDK_KEY_F5, 0, 6}, {GDK_KEY_F7, 0, 3},
};

static int lookup_key(guint keyval, int *pa, int *pb) {
    guint lower = gdk_keyval_to_lower(keyval);
    for (size_t i = 0; i < sizeof(c64_keymap) / sizeof(c64_keymap[0]); i++) {
        if (c64_keymap[i].keyval == keyval || c64_keymap[i].keyval == lower) {
            *pa = c64_keymap[i].pa;
            *pb = c64_keymap[i].pb;
            return 1;
        }
    }
    return 0;
}

/* Renders at the VIC's native VIC_CANVAS_W x VIC_CANVAS_H resolution
 * into app->pixel_buf as always, then scales that surface up to fill
 * whatever size the drawing area's widget actually is (width/height,
 * previously ignored) - this is what makes "zoom" a real window-resize
 * rather than a fixed multiplier: the drawing area is set to expand
 * with the window (see activate()), so dragging the window bigger
 * directly zooms the picture. Scale is uniform (min of the two axis
 * ratios) and the result is centered, so resizing to a non-4:3-ish
 * shape letterboxes/pillarboxes instead of distorting the image -
 * real C64 software assumes square-ish pixels. */
static void draw_screen(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)area;
    App *app = user_data;

    uint8_t bank = machine_vic_bank(&app->machine);
    vic_render_frame(&app->machine.vic, &app->machine.mem, bank, app->pixel_buf, app->pixel_stride);

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        (unsigned char *)app->pixel_buf, CAIRO_FORMAT_RGB24,
        VIC_CANVAS_W, VIC_CANVAS_H, app->pixel_stride * 4);

    double scale_x = (double)width / VIC_CANVAS_W;
    double scale_y = (double)height / VIC_CANVAS_H;
    double scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale <= 0.0) scale = 1.0;

    double scaled_w = VIC_CANVAS_W * scale;
    double scaled_h = VIC_CANVAS_H * scale;
    cairo_translate(cr, (width - scaled_w) / 2.0, (height - scaled_h) / 2.0);
    cairo_scale(cr, scale, scale);

    /* Nearest-neighbor rather than cairo's default smooth filtering -
     * a blurred-upscale look reads as wrong for pixel-art-era C64
     * software; crisp blocky pixels at 2x/3x matches how every other
     * C64 emulator scales its display. */
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_surface_destroy(surface);
}

/* Main-thread side only - caller must hold the SDL audio device lock
 * (see App's own comment on the ring buffer). Drops the sample if the
 * ring is full rather than blocking or overwriting - shouldn't happen
 * in practice given AUDIO_RING_SAMPLES's generous sizing, but dropping
 * is the right failure mode for real-time audio either way (a stale
 * sample is worse than a brief silence). */
static void audio_push_sample(App *app, int16_t sample) {
    if (app->audio_ring_count >= AUDIO_RING_SAMPLES) return;
    app->audio_ring[app->audio_ring_write] = sample;
    app->audio_ring_write = (app->audio_ring_write + 1) % AUDIO_RING_SAMPLES;
    app->audio_ring_count++;
}

/* Runs on SDL's own audio thread, never the GTK main thread - see App's
 * ring-buffer comment for why this only ever reads the ring, never
 * touches Sid. Underruns (the emulator's persistent few-percent real-
 * time deficit - see this file's audio-pacing header comment for why
 * that hasn't been eliminated outright) hold the LAST real sample
 * rather than snapping to silence.
 *
 * Real bug, caught by ear testing asm/examples/bounce.asm: snapping
 * straight to 0 on every underrun is only inaudible while the actual
 * signal is ALSO near 0 (true silence, or a fully-decayed envelope) -
 * during a short one-shot sound effect's decay, the signal is very
 * much NOT near 0 for most of its brief life, and an underrun landing
 * there forces an abrupt jump down to 0 and then back up once real
 * samples resume, a genuine amplitude discontinuity, not a rendering
 * bug. Heard as a short "scratch" right at the tail of bounce.asm's
 * bounce-sound bleep specifically because that one-shot's decay (AD=
 * $06, an already-fast rate) burns through most of its audible range
 * in under 150ms - short enough that the routine ~5-10% underrun rate
 * (see this file's header comment) has good odds of landing exactly
 * during that brief loud window, where a jump is most audible, rather
 * than during the long tail where the signal's already quiet. Confirmed
 * directly with a headless capture that reproduced tick()'s exact
 * pacing against real jittered timing: every single amplitude jump
 * bigger than a real waveform step could produce coincided with an
 * underrun-filled sample (100% correlation); switching the fill
 * strategy to "hold last sample" against the SAME underrun rate
 * eliminated every one of those jumps. Holding rather than
 * interpolating is deliberately simple - a few milliseconds of a
 * stale-but-continuous sample is inaudible, while computing a real
 * interpolation would need lookahead this producer/consumer split
 * doesn't have. Local `static` rather than an App field: this is
 * audio-thread-only state SDL guarantees is never touched by the
 * locked main-thread section, so it doesn't need App's cross-thread
 * synchronization at all. */
static void audio_callback(void *userdata, Uint8 *stream, int len) {
    App *app = userdata;
    int16_t *out = (int16_t *)stream;
    int n = len / (int)sizeof(int16_t);
    static int16_t last_sample = 0;
    for (int i = 0; i < n; i++) {
        if (app->audio_ring_count > 0) {
            out[i] = app->audio_ring[app->audio_ring_read];
            app->audio_ring_read = (app->audio_ring_read + 1) % AUDIO_RING_SAMPLES;
            app->audio_ring_count--;
            last_sample = out[i];
        } else {
            out[i] = last_sample;
        }
    }
}

/* "READY." in C64 screen codes, NOT PETSCII/ASCII (screen RAM stores
 * screen codes - see c64-memory-reference.md if this ever needs
 * extending): R=$12 E=$05 A=$01 D=$04 Y=$19 .=$2E - identical check to
 * ../tests/boot/test_boot.c's own screen_has_ready(), duplicated here
 * rather than shared since that's a standalone CI check and this is
 * the interactive shell polling once per frame - they have no reason
 * to stay in lockstep. */
static const uint8_t READY_SCREEN_CODES[] = {0x12, 0x05, 0x01, 0x04, 0x19, 0x2E};

static int screen_has_ready(Machine *m) {
    for (int i = 0; i <= 1000 - 6; i++) {
        int match = 1;
        for (int j = 0; j < 6; j++) {
            if (memory_read(&m->mem, (uint16_t)(0x0400 + i + j)) != READY_SCREEN_CODES[j]) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

/* Waits for a real READY. prompt before injecting --prg (see main())
 * rather than doing it immediately after reset: a real C64 can only
 * LOAD+RUN a program once BASIC/KERNAL have finished their own startup
 * (clearing the screen, setting up IRQ vectors and CIA timers for the
 * jiffy clock, etc.) - jumping into a demo before any of that ran would
 * leave it relying on KERNAL-standard state that was simply never set
 * up, the same class of bug as skipping the boot sequence entirely.
 * Loads the file, finds its BASIC stub's SYS target the shortcut way
 * (see machine_find_sys_target()'s own comment - no simulated typing of
 * RUN, no real BASIC tokenizing), and jumps the CPU straight there.
 *
 * Also disables the KERNAL's blinking text cursor ($CC, the standard
 * documented cursor-enable flag - nonzero suppresses it) right before
 * jumping in. This isn't cosmetic guesswork: the KERNAL's IRQ handler
 * is STILL running in the background after a raw SYS jump on real
 * hardware (see input.inc's own header comment), and it keeps
 * blinking the cursor at wherever it was left, forever,
 * unless the program either disables it or a well-behaved demo takes
 * over the IRQ itself. Typing LOAD+RUN normally leaves the cursor
 * somewhere unobtrusive after all that scrolling; jumping straight in
 * from the very first READY. (this shortcut) can leave it sitting
 * in the middle of a game's own play field instead - confirmed via a
 * real screenshot showing pong.asm's ball sharing the screen with a
 * second, perfectly stationary white block that traced back to screen
 * code $A0 (the real KERNAL cursor character) at the exact cell the
 * cursor was left at. Demos that already take over the IRQ themselves
 * are unaffected either way.
 *
 * Pushes a fake return address via machine_push_prg_return_trampoline()
 * before jumping in - see that function's own doc comment in
 * machine.h for why a raw `cpu.pc = sys_target` isn't safe for a
 * program that legitimately RTS's back, and tests/prg_inject/ for the
 * regression test covering it. */
static void try_inject_prg(App *app) {
    if (app->prg_state != PRG_PENDING) return;
    if (!screen_has_ready(&app->machine)) return;

    uint16_t load_addr = machine_load_prg(&app->machine, app->prg_path);
    uint16_t sys_target = load_addr ? machine_find_sys_target(&app->machine, load_addr) : 0;
    if (sys_target == 0) {
        fprintf(stderr, "--prg %s: couldn't find a BASIC-stub SYS target to jump to (not a .basic-style c64asm .prg?)\n", app->prg_path);
    } else {
        fprintf(stderr, "--prg %s: loaded at $%04X, jumping to $%04X\n", app->prg_path, load_addr, sys_target);
        memory_write(&app->machine.mem, 0xCC, 1);
        machine_push_prg_return_trampoline(&app->machine);
        app->machine.cpu.pc = sys_target;
    }
    app->prg_state = PRG_DONE; /* either way - don't keep retrying every frame */
}

/* Pumps SDL's event queue (the only place SDL_CONTROLLERDEVICEADDED/
 * REMOVED show up - SDL doesn't offer a polling-only hotplug check)
 * and updates joystick port 2 from whatever controller is currently
 * open. Called once per tick() (see below), the same cadence
 * machine_set_key() effectively runs at via GTK's own key events -
 * a controller's directions/fire button only need to be as fresh as
 * one frame, same as everything else driving Machine.
 *
 * D-pad buttons and the left stick both map to the same four
 * directions (whichever the controller/game favors - some Xbox-style
 * pads report the D-pad as a hat, others as buttons via
 * SDL_GameController's abstraction, so checking both costs nothing
 * and covers either), OR'd together. Button A is fire - the
 * conventional single-fire-button mapping every C64 joystick game
 * expects (see read_joystick's own comment in asm/examples/pong.asm).
 * machine_set_joystick() takes active-low bits (0 = held) - built
 * here as an active-high "pressed" mask first since that reads more
 * naturally, then inverted once at the end. */
static void poll_joystick(App *app) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_CONTROLLERDEVICEADDED && !app->controller) {
            app->controller = SDL_GameControllerOpen(e.cdevice.which);
        } else if (e.type == SDL_CONTROLLERDEVICEREMOVED && app->controller) {
            SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(app->controller));
            if (e.cdevice.which == id) {
                SDL_GameControllerClose(app->controller);
                app->controller = NULL;
                machine_set_joystick(&app->machine, 2, 0xFF); /* back to all-released, not whatever it last read */
            }
        }
    }

    if (!app->controller) return;

    int16_t lx = SDL_GameControllerGetAxis(app->controller, SDL_CONTROLLER_AXIS_LEFTX);
    int16_t ly = SDL_GameControllerGetAxis(app->controller, SDL_CONTROLLER_AXIS_LEFTY);
    int up = SDL_GameControllerGetButton(app->controller, SDL_CONTROLLER_BUTTON_DPAD_UP) || ly < -JOYSTICK_AXIS_THRESHOLD;
    int down = SDL_GameControllerGetButton(app->controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly > JOYSTICK_AXIS_THRESHOLD;
    int left = SDL_GameControllerGetButton(app->controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx < -JOYSTICK_AXIS_THRESHOLD;
    int right = SDL_GameControllerGetButton(app->controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx > JOYSTICK_AXIS_THRESHOLD;
    int fire = SDL_GameControllerGetButton(app->controller, SDL_CONTROLLER_BUTTON_A);

    uint8_t pressed = 0;
    if (up) pressed |= 0x01;
    if (down) pressed |= 0x02;
    if (left) pressed |= 0x04;
    if (right) pressed |= 0x08;
    if (fire) pressed |= 0x10;
    machine_set_joystick(&app->machine, 2, (uint8_t)~pressed);
}

static gboolean tick(gpointer user_data) {
    App *app = user_data;
    try_inject_prg(app);
    poll_joystick(app);

    int budget = CYCLES_PER_FRAME;
    while (budget > 0) {
        int cycles = machine_step(&app->machine);
        budget -= cycles;

        /* SID ticked exactly once already, inside machine_step() above
         * (see machine.c) - this just decides when enough of ITS clock
         * cycles have gone by to owe the audio thread another sample,
         * accumulating fractionally since SID_CLOCK_HZ/SID_SAMPLE_RATE
         * (~22.29) isn't a whole number, and MUST stay interleaved with
         * the cycle-stepping loop like this (one sid_output() call per
         * small slice of cycles) rather than run in a burst after the
         * loop finishes - see this section's own header comment for
         * why both that and pacing against SID_CLOCK_HZ specifically
         * (not measured real time) are load-bearing, not style
         * choices. Skipped entirely if SDL audio never opened
         * (audio_dev == 0) - see main(). */
        if (app->audio_dev != 0) {
            app->sample_cycle_accum += cycles;
            if (app->sample_cycle_accum >= (double)SID_CLOCK_HZ / SID_SAMPLE_RATE) {
                SDL_LockAudioDevice(app->audio_dev);
                while (app->sample_cycle_accum >= (double)SID_CLOCK_HZ / SID_SAMPLE_RATE) {
                    /* sid_output()'s raw value is used AS-IS, not
                     * artificially recentered around 0 - it's already
                     * DC-biased on real hardware (silence is always
                     * exactly 0, not some midpoint - see sid.h's own
                     * header comment), and critically, this way
                     * "silence" here exactly matches audio_callback()'s
                     * own underrun-fill value (also a literal 0). An
                     * earlier version of this line shifted samples down
                     * by 16384 to look more like "normal" bipolar PCM,
                     * which meant every ring-buffer underrun (routine,
                     * since GTK's timer and SDL's independent real-time
                     * audio thread never stay perfectly in lockstep)
                     * jumped between that shifted "silence" and the
                     * callback's unshifted 0 - an audible, repeating
                     * click even with nothing actually playing. */
                    int16_t sample = sid_output(&app->machine.sid);
                    audio_push_sample(app, sample);
                    app->sample_cycle_accum -= (double)SID_CLOCK_HZ / SID_SAMPLE_RATE;
                }
                SDL_UnlockAudioDevice(app->audio_dev);
            }
        }
    }

    gtk_widget_queue_draw(app->drawing_area);
    return G_SOURCE_CONTINUE;
}

static gboolean on_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) {
    (void)ctrl;
    (void)keycode;
    (void)state;
    App *app = user_data;
    int pa, pb;
    if (lookup_key(keyval, &pa, &pb)) {
        machine_set_key(&app->machine, pa, pb, 1);
    }
    return GDK_EVENT_PROPAGATE;
}

static void on_key_released(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) {
    (void)ctrl;
    (void)keycode;
    (void)state;
    App *app = user_data;
    int pa, pb;
    if (lookup_key(keyval, &pa, &pb))
        machine_set_key(&app->machine, pa, pb, 0);
}

/* Without this, the frame timer keeps firing after the window closes
 * (a GtkWindow's "destroy" doesn't implicitly cancel a g_timeout_add()
 * tied to it) and tick() calls gtk_widget_queue_draw() on a
 * by-then-dangling app->drawing_area, tripping a GTK-CRITICAL
 * assertion (harmless in that it doesn't crash, but a real bug - the
 * source should have been removed). */
static void on_window_destroy(GtkWidget *window, gpointer user_data) {
    (void)window;
    App *app = user_data;
    if (app->timeout_id) {
        g_source_remove(app->timeout_id);
        app->timeout_id = 0;
    }
}

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    App *app = user_data;

    GtkWidget *window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(window), "c64emu");
    /* The native VIC_CANVAS_W x VIC_CANVAS_H resolution (~403x284) is
     * tiny on a modern display, so the window opens at a readable
     * WINDOW_ZOOM_DEFAULT multiple of it instead - draw_screen() scales
     * app->pixel_buf's native-resolution render up to whatever size the
     * drawing area actually is, so this is just the STARTING size, not
     * a fixed multiplier: the window is ordinarily resizable (GTK4
     * default), and dragging it to any other size zooms the picture to
     * match, letterboxed to preserve the C64's aspect ratio. */
    gtk_window_set_default_size(GTK_WINDOW(window), VIC_CANVAS_W * WINDOW_ZOOM_DEFAULT, VIC_CANVAS_H * WINDOW_ZOOM_DEFAULT);

    /* cairo_image_surface_create_for_data() requires a stride that
     * matches cairo's own alignment rules for the format, which isn't
     * necessarily VIC_CANVAS_W * 4 - so the pixel buffer's row stride
     * is whatever cairo says it should be, not assumed. */
    int stride_bytes = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, VIC_CANVAS_W);
    app->pixel_stride = stride_bytes / 4;
    app->pixel_buf = calloc((size_t)app->pixel_stride * VIC_CANVAS_H, sizeof(uint32_t));

    app->drawing_area = gtk_drawing_area_new();
    /* Content width/height below is only the MINIMUM natural size GTK
     * will lay out (still the native resolution, so the window can
     * never shrink smaller than one real C64 pixel per source pixel) -
     * hexpand/vexpand is what lets the widget actually grow to fill
     * extra window area on resize, which draw_screen() then scales
     * into. Without these two calls the drawing area stays pinned to
     * its natural size and resizing the window would just add dead
     * space around it, not zoom anything. */
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(app->drawing_area), VIC_CANVAS_W);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(app->drawing_area), VIC_CANVAS_H);
    gtk_widget_set_hexpand(app->drawing_area, TRUE);
    gtk_widget_set_vexpand(app->drawing_area, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->drawing_area), draw_screen, app, NULL);
    gtk_window_set_child(GTK_WINDOW(window), app->drawing_area);

    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), app);
    g_signal_connect(keys, "key-released", G_CALLBACK(on_key_released), app);
    gtk_widget_add_controller(window, keys);

    app->timeout_id = g_timeout_add(FRAME_MS, tick, app);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), app);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    App app = {0}; /* zeroes audio_dev/sample_cycle_accum/audio_ring_* too - see tick()'s use of them */
    machine_init(&app.machine);

    /* Optional positional arg overrides the ROM directory (default:
     * "roms", matching ../roms/README.md, resolved relative to cwd -
     * run from emu/ unless a path is given). Missing ROMs aren't fatal
     * here either: machine_load_roms() reports how many loaded, and
     * the CPU will just execute zeroed memory (a harmless BRK loop) if
     * none did - fine for proving the display/event loop on its own.
     * Optional --prg PATH: a c64asm-built .prg (e.g. from
     * ../../asm/examples/) to auto-run once boot reaches READY. - see
     * try_inject_prg(). Simple manual parsing rather than getopt: this
     * is the only flag there is. */
    const char *rom_dir = "roms";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--prg") == 0 && i + 1 < argc) {
            app.prg_path = argv[++i];
            app.prg_state = PRG_PENDING;
        } else {
            rom_dir = argv[i];
        }
    }
    int loaded = machine_load_roms(&app.machine, rom_dir);
    fprintf(stderr, "loaded %d/3 ROMs from %s\n", loaded, rom_dir);

    machine_reset(&app.machine);

    /* Audio failing to open is never fatal - same graceful-degradation
     * spirit as running with 0/3 ROMs loaded (app.audio_dev stays 0,
     * which tick() checks before ever touching SDL). Same for
     * SDL_INIT_GAMECONTROLLER: if it fails, app.controller just stays
     * NULL forever and poll_joystick() is a no-op every frame - the
     * emulator runs fine with keyboard input only, same as it always
     * could. A controller already plugged in when this runs is picked
     * up automatically: SDL queues an SDL_CONTROLLERDEVICEADDED event
     * for every already-connected controller the first time the event
     * queue is pumped, which poll_joystick() does every tick() - no
     * separate enumerate-at-startup step needed. */
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s - running without audio/joystick\n", SDL_GetError());
    } else {
        SDL_AudioSpec want;
        SDL_zero(want);
        want.freq = SID_SAMPLE_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 1; /* SID mixes all 3 voices down to one channel already - see sid_output() */
        want.samples = 1024;
        want.callback = audio_callback;
        want.userdata = &app;
        /* NULL device name/allow_changes=0: take the system default
         * output device as-is rather than negotiating a different
         * rate/format - simplest choice, and 44.1kHz mono S16 is a
         * format essentially every backend supports natively anyway. */
        app.audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (app.audio_dev == 0) {
            fprintf(stderr, "SDL_OpenAudioDevice failed: %s - running without audio\n", SDL_GetError());
        } else {
            SDL_PauseAudioDevice(app.audio_dev, 0); /* SDL opens devices paused - this actually starts playback */
        }
    }

    GtkApplication *gtk_app = gtk_application_new("dev.c64toolkit.c64emu", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), &app);
    int status = g_application_run(G_APPLICATION(gtk_app), 0, NULL);
    g_object_unref(gtk_app);

    if (app.audio_dev != 0) SDL_CloseAudioDevice(app.audio_dev);
    if (app.controller != NULL) SDL_GameControllerClose(app.controller);
    SDL_Quit();
    return status;
}
