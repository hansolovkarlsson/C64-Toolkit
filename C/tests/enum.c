#include <print.h>

/* Anonymous enum, plain auto-increment starting at 0. */
enum { RED, GREEN, BLUE };

/* Tagged enum, explicit values mixed with auto-increment (continuing
 * from the last explicit value + 1), including a negative value. */
enum Status {
    STATUS_ERROR = -1,
    STATUS_OK = 0,
    STATUS_PENDING,      /* = 1 */
    STATUS_DONE = 10,
    STATUS_ARCHIVED      /* = 11 */
};

/* Enum constant used as a global array size (and, in test_local_array()
 * below, a local one too - enum definitions themselves are top-level
 * only, same restriction struct has, but a constant defined at the top
 * level can still size a LOCAL array just fine). */
enum { MAX_ITEMS = 4, LOCAL_SIZE = 3 };
int g_items[MAX_ITEMS];

/* Enum constant used as a global initializer. */
int g_default_color = GREEN;

struct Widget {
    enum Status status;
    int id;
};

void print_status(enum Status s) {
    switch (s) {
        case STATUS_ERROR:
            puts("error");
            break;
        case STATUS_OK:
            puts("ok");
            break;
        case STATUS_PENDING:
            puts("pending");
            break;
        case STATUS_DONE:
        case STATUS_ARCHIVED:
            puts("done-or-archived");
            break;
        default:
            puts("unknown");
    }
}

int color_value(int c) {
    if (c == RED) return 100;
    if (c == GREEN) return 200;
    if (c == BLUE) return 300;
    return -1;
}

void test_values(void) {
    printf("%d %d %d\n", RED, GREEN, BLUE);
    printf("%d %d %d %d %d\n", STATUS_ERROR, STATUS_OK, STATUS_PENDING,
           STATUS_DONE, STATUS_ARCHIVED);
}

void test_expr(void) {
    int c;
    c = BLUE;
    printf("%d\n", color_value(c));
    printf("%d\n", RED + GREEN + BLUE); /* 0+1+2=3 */
}

void test_switch(void) {
    print_status(STATUS_ERROR);
    print_status(STATUS_OK);
    print_status(STATUS_PENDING);
    print_status(STATUS_DONE);
    print_status(STATUS_ARCHIVED);
}

void test_global_array_and_init(void) {
    int i;
    for (i = 0; i < MAX_ITEMS; i = i + 1) {
        g_items[i] = i * 10;
    }
    printf("%d %d %d %d\n", g_items[0], g_items[1], g_items[2], g_items[3]);
    printf("%d\n", g_default_color);
}

void test_local_array(void) {
    int buf[LOCAL_SIZE];
    int i;
    for (i = 0; i < LOCAL_SIZE; i = i + 1) buf[i] = i + 1;
    printf("%d %d %d\n", buf[0], buf[1], buf[2]);
}

void test_struct_member(void) {
    struct Widget w;
    w.status = STATUS_DONE;
    w.id = 7;
    print_status(w.status);
    printf("%d\n", w.id);
}

int main(void) {
    test_values();
    test_expr();
    test_switch();
    test_global_array_and_init();
    test_local_array();
    test_struct_member();
    return 0;
}
