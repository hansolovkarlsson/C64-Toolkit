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
 * ../roms/README.md). Joystick input isn't wired up yet.
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

/* Audio output (../ROADMAP.md step 7's last piece): SID itself (see
 * sid.h) has no notion of a sample rate - sid_tick() already advances
 * in lockstep with the CPU inside machine_step(), same as CIA/VIC, so
 * this file is where "how many real SID clock cycles is one 44.1kHz
 * sample worth" gets decided, the same way it's already where "how
 * many CPU cycles is one video frame worth" (CYCLES_PER_FRAME above)
 * gets decided. SID_CLOCK_HZ reuses this file's own 20ms/50Hz frame
 * timer approximation rather than introducing a second, different
 * approximation of the real PAL clock. */
#define SID_SAMPLE_RATE 44100
#define SID_CLOCK_HZ (CYCLES_PER_FRAME * 1000 / FRAME_MS) /* == 982800, this file's existing ~50Hz-timer approximation of the real ~985248Hz PAL clock */
#define AUDIO_RING_SAMPLES 8192 /* ~185ms at 44.1kHz - generous slack against GTK timer jitter, see tick()'s own comment */

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

static void draw_screen(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)area;
    (void)width;
    (void)height;
    App *app = user_data;

    uint8_t bank = machine_vic_bank(&app->machine);
    vic_render_frame(&app->machine.vic, &app->machine.mem, bank, app->pixel_buf, app->pixel_stride);

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        (unsigned char *)app->pixel_buf, CAIRO_FORMAT_RGB24,
        VIC_CANVAS_W, VIC_CANVAS_H, app->pixel_stride * 4);
    cairo_set_source_surface(cr, surface, 0, 0);
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
 * touches Sid. Underruns (the emulator falling behind real time, or
 * simply nothing enabled on any SID voice) are filled with silence
 * rather than left as garbage/repeated stale samples. */
static void audio_callback(void *userdata, Uint8 *stream, int len) {
    App *app = userdata;
    int16_t *out = (int16_t *)stream;
    int n = len / (int)sizeof(int16_t);
    for (int i = 0; i < n; i++) {
        if (app->audio_ring_count > 0) {
            out[i] = app->audio_ring[app->audio_ring_read];
            app->audio_ring_read = (app->audio_ring_read + 1) % AUDIO_RING_SAMPLES;
            app->audio_ring_count--;
        } else {
            out[i] = 0;
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
 * RUN, no real BASIC tokenizing), and jumps the CPU straight there. */
static void try_inject_prg(App *app) {
    if (app->prg_state != PRG_PENDING) return;
    if (!screen_has_ready(&app->machine)) return;

    uint16_t load_addr = machine_load_prg(&app->machine, app->prg_path);
    uint16_t sys_target = load_addr ? machine_find_sys_target(&app->machine, load_addr) : 0;
    if (sys_target == 0) {
        fprintf(stderr, "--prg %s: couldn't find a BASIC-stub SYS target to jump to (not a .basic-style c64asm .prg?)\n", app->prg_path);
    } else {
        fprintf(stderr, "--prg %s: loaded at $%04X, jumping to $%04X\n", app->prg_path, load_addr, sys_target);
        app->machine.cpu.pc = sys_target;
    }
    app->prg_state = PRG_DONE; /* either way - don't keep retrying every frame */
}

static gboolean tick(gpointer user_data) {
    App *app = user_data;
    try_inject_prg(app);
    int budget = CYCLES_PER_FRAME;
    while (budget > 0) {
        int cycles = machine_step(&app->machine);
        budget -= cycles;

        /* SID ticked exactly once already, inside machine_step() above
         * (see machine.c) - this just decides when enough of ITS clock
         * cycles have gone by to owe the audio thread another sample,
         * accumulating fractionally since SID_CLOCK_HZ/SID_SAMPLE_RATE
         * (~22.29) isn't a whole number. Skipped entirely if SDL audio
         * never opened (audio_dev == 0) - see main(). */
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

/* No VIC-II exists yet (step 5), so a keypress produces no visible
 * change in the window by itself - it only reaches BASIC/KERNAL (and
 * so gets echoed to the screen) once real ROM images are loaded and
 * actually running. This log line is the only feedback available
 * without that - confirms a key was recognized and which matrix
 * position it landed on. */
static gboolean on_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) {
    (void)ctrl;
    (void)keycode;
    (void)state;
    App *app = user_data;
    int pa, pb;
    if (lookup_key(keyval, &pa, &pb)) {
        machine_set_key(&app->machine, pa, pb, 1);
        fprintf(stderr, "key down: %s (matrix pa=%d pb=%d)\n", gdk_keyval_name(keyval), pa, pb);
    } else {
        fprintf(stderr, "key down: %s (not in c64_keymap)\n", gdk_keyval_name(keyval));
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
    /* Matches the drawing area's own fixed content size exactly - a
     * bigger default would leave dead space around it, since nothing
     * here stretches the canvas to fill extra window area. */
    gtk_window_set_default_size(GTK_WINDOW(window), VIC_CANVAS_W, VIC_CANVAS_H);

    /* cairo_image_surface_create_for_data() requires a stride that
     * matches cairo's own alignment rules for the format, which isn't
     * necessarily VIC_CANVAS_W * 4 - so the pixel buffer's row stride
     * is whatever cairo says it should be, not assumed. */
    int stride_bytes = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, VIC_CANVAS_W);
    app->pixel_stride = stride_bytes / 4;
    app->pixel_buf = calloc((size_t)app->pixel_stride * VIC_CANVAS_H, sizeof(uint32_t));

    app->drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(app->drawing_area), VIC_CANVAS_W);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(app->drawing_area), VIC_CANVAS_H);
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
     * which tick() checks before ever touching SDL). */
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init(SDL_INIT_AUDIO) failed: %s - running without audio\n", SDL_GetError());
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
    SDL_Quit();
    return status;
}
