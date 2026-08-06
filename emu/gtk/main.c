/*
 * main.c - minimal GTK4 shell (../ROADMAP.md step 3): drives the
 * CPU+memory core through a real GUI event loop and displays
 * something on screen, before any chip emulation (CIA/VIC-II/SID)
 * exists to add real graphics/input/sound.
 *
 * The "framebuffer" here is deliberately not real VIC-II output -
 * there's no character/bitmap mode decoding yet (step 5). It's screen
 * RAM ($0400-$07E7, the default 40x25 text screen location) rendered
 * one byte per cell as a raw grayscale value, so the same 320x200
 * canvas this window shows today can be handed to a real VIC-II text-
 * mode renderer later without changing the window/loop plumbing
 * around it.
 *
 * Keyboard events are captured and logged, not wired to anything -
 * CIA, which owns the real C64 keyboard matrix, doesn't exist yet
 * (step 4). This just proves GTK key events reach the machine loop.
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include "../src/cpu.h"
#include "../src/memory.h"

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
    Cpu6502 cpu;
    Memory mem;
    GtkWidget *drawing_area;
} Machine;

static void draw_screen(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data) {
    (void)area;
    (void)width;
    (void)height;
    Machine *m = user_data;
    for (int row = 0; row < SCREEN_ROWS; row++) {
        for (int col = 0; col < SCREEN_COLS; col++) {
            uint8_t v = memory_read(&m->mem, (uint16_t)(SCREEN_BASE + row * SCREEN_COLS + col));
            double shade = v / 255.0;
            cairo_set_source_rgb(cr, shade, shade, shade);
            cairo_rectangle(cr, col * CELL_PX, row * CELL_PX, CELL_PX, CELL_PX);
            cairo_fill(cr);
        }
    }
}

static gboolean tick(gpointer user_data) {
    Machine *m = user_data;
    int budget = CYCLES_PER_FRAME;
    while (budget > 0)
        budget -= cpu_step(&m->cpu);
    gtk_widget_queue_draw(m->drawing_area);
    return G_SOURCE_CONTINUE;
}

static gboolean on_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) {
    (void)ctrl;
    (void)keycode;
    (void)state;
    (void)user_data;
    printf("key down: %s\n", gdk_keyval_name(keyval));
    return GDK_EVENT_PROPAGATE;
}

static void on_key_released(GtkEventControllerKey *ctrl, guint keyval, guint keycode, GdkModifierType state, gpointer user_data) {
    (void)ctrl;
    (void)keycode;
    (void)state;
    (void)user_data;
    printf("key up:   %s\n", gdk_keyval_name(keyval));
}

static void activate(GtkApplication *app, gpointer user_data) {
    Machine *m = user_data;

    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "c64emu");
    gtk_window_set_default_size(GTK_WINDOW(window), CANVAS_W * 2, CANVAS_H * 2);

    m->drawing_area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(m->drawing_area), CANVAS_W);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(m->drawing_area), CANVAS_H);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(m->drawing_area), draw_screen, m, NULL);
    gtk_window_set_child(GTK_WINDOW(window), m->drawing_area);

    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), m);
    g_signal_connect(keys, "key-released", G_CALLBACK(on_key_released), m);
    gtk_widget_add_controller(window, keys);

    g_timeout_add(FRAME_MS, tick, m);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    Machine m;
    memory_init(&m.mem);

    /* Optional first arg overrides the ROM directory (default: "roms",
     * matching ../roms/README.md, resolved relative to cwd - run from
     * emu/ unless a path is given). Missing ROMs aren't fatal here
     * either: memory_load_roms() reports how many loaded, and the CPU
     * will just execute zeroed memory (a harmless BRK loop) if none
     * did - fine for proving the display/event loop on its own. */
    const char *rom_dir = argc > 1 ? argv[1] : "roms";
    int loaded = memory_load_roms(&m.mem, rom_dir);
    fprintf(stderr, "loaded %d/3 ROMs from %s\n", loaded, rom_dir);

    m.cpu.bus = memory_cpu_bus(&m.mem);
    cpu_reset(&m.cpu);

    GtkApplication *app = gtk_application_new("dev.c64toolkit.c64emu", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &m);
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
    return status;
}
