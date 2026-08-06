/*
 * main.c - minimal GTK4 shell (../ROADMAP.md step 3, keyboard now
 * wired through step 4's CIA1). Drives the whole machine (CPU +
 * memory + both CIAs, see ../src/machine.h) through a real GUI event
 * loop.
 *
 * The "framebuffer" here is deliberately not real VIC-II output -
 * there's no character/bitmap mode decoding yet (step 5). It's screen
 * RAM ($0400-$07E7, the default 40x25 text screen location) rendered
 * one byte per cell as a raw grayscale value, so the same 320x200
 * canvas this window shows today can be handed to a real VIC-II text-
 * mode renderer later without changing the window/loop plumbing
 * around it.
 *
 * Keyboard events ARE now wired to something real: GDK key events are
 * translated to C64 keyboard-matrix positions (see c64_keymap below)
 * and fed into CIA1 via machine_set_key(), so typing in this window
 * reaches BASIC/KERNAL once real ROM images are present (see
 * ../roms/README.md). Joystick input isn't wired up yet.
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include "../src/machine.h"

#define SCREEN_COLS 40
#define SCREEN_ROWS 25
#define SCREEN_BASE 0x0400
#define CELL_PX 8
#define CANVAS_W (SCREEN_COLS * CELL_PX)
#define CANVAS_H (SCREEN_ROWS * CELL_PX)

/* PAL C64: ~985248 Hz CPU clock, ~50.12 Hz frame rate -> ~19656
 * cycles/frame. Approximated with a 20ms GTK timer (~50 Hz) rather
 * than anything cycle-exact - there's no VIC-II raster to synchronize
 * against yet, so real frame timing isn't meaningful until step 5. */
#define CYCLES_PER_FRAME 19656
#define FRAME_MS 20

typedef struct {
    Machine machine;
    GtkWidget *drawing_area;
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
    for (int row = 0; row < SCREEN_ROWS; row++) {
        for (int col = 0; col < SCREEN_COLS; col++) {
            uint8_t v = memory_read(&app->machine.mem, (uint16_t)(SCREEN_BASE + row * SCREEN_COLS + col));
            double shade = v / 255.0;
            cairo_set_source_rgb(cr, shade, shade, shade);
            cairo_rectangle(cr, col * CELL_PX, row * CELL_PX, CELL_PX, CELL_PX);
            cairo_fill(cr);
        }
    }
}

static gboolean tick(gpointer user_data) {
    App *app = user_data;
    int budget = CYCLES_PER_FRAME;
    while (budget > 0)
        budget -= machine_step(&app->machine);
    gtk_widget_queue_draw(app->drawing_area);
    return G_SOURCE_CONTINUE;
}

static gboolean on_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) {
    (void)ctrl;
    (void)keycode;
    (void)state;
    App *app = user_data;
    int pa, pb;
    if (lookup_key(keyval, &pa, &pb))
        machine_set_key(&app->machine, pa, pb, 1);
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

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    App *app = user_data;

    GtkWidget *window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(window), "c64emu");
    gtk_window_set_default_size(GTK_WINDOW(window), CANVAS_W * 2, CANVAS_H * 2);

    app->drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(app->drawing_area), CANVAS_W);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(app->drawing_area), CANVAS_H);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->drawing_area), draw_screen, app, NULL);
    gtk_window_set_child(GTK_WINDOW(window), app->drawing_area);

    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), app);
    g_signal_connect(keys, "key-released", G_CALLBACK(on_key_released), app);
    gtk_widget_add_controller(window, keys);

    g_timeout_add(FRAME_MS, tick, app);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    App app;
    machine_init(&app.machine);

    /* Optional first arg overrides the ROM directory (default: "roms",
     * matching ../roms/README.md, resolved relative to cwd - run from
     * emu/ unless a path is given). Missing ROMs aren't fatal here
     * either: machine_load_roms() reports how many loaded, and the CPU
     * will just execute zeroed memory (a harmless BRK loop) if none
     * did - fine for proving the display/event loop on its own. */
    const char *rom_dir = argc > 1 ? argv[1] : "roms";
    int loaded = machine_load_roms(&app.machine, rom_dir);
    fprintf(stderr, "loaded %d/3 ROMs from %s\n", loaded, rom_dir);

    machine_reset(&app.machine);

    GtkApplication *gtk_app = gtk_application_new("dev.c64toolkit.c64emu", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), &app);
    int status = g_application_run(G_APPLICATION(gtk_app), 0, NULL);
    g_object_unref(gtk_app);
    return status;
}
