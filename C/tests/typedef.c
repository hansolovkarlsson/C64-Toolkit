#include <print.h>

/* Plain scalar alias. */
typedef int MyInt;

/* Pointer alias - String IS already a pointer type, no extra '*'
 * needed (or allowed) at a use site. */
typedef char *String;

/* struct Tag Tag; - the common "give the tag itself a shorter name"
 * idiom. */
struct Point { int x; int y; };
typedef struct Point Point;

/* union Tag Tag; - same idiom, for union. */
union Number { int i; char c; };
typedef union Number Number;

/* enum Tag Tag; */
enum Color { RED, GREEN, BLUE };
typedef enum Color Color;

/* A typedef built from another typedef. */
typedef MyInt AnotherInt;

/* A typedef used as a global's type, including an initializer and an
 * array size. */
MyInt g_count = 5;
MyInt g_values[3];

/* A typedef used as a function parameter and return type. */
MyInt add_one(MyInt n) {
    return n + 1;
}

/* A typedef used as a struct member's type - Point* here, not Point,
 * for the same reason test_struct_member_typedef()'s own comment
 * (below) explains: a struct/union-by-value member isn't supported,
 * typedef or not. */
struct Labeled {
    String name;
    Point *pos;
};

void test_scalar(void) {
    MyInt x;
    x = 10;
    printf("%d\n", x);
    printf("%d\n", add_one(x));
}

void test_pointer(void) {
    String s;
    s = "hello";
    puts(s);
    printf("\n");
}

void test_struct_tag(void) {
    Point p;
    p.x = 3;
    p.y = 4;
    printf("%d %d\n", p.x, p.y);
}

void test_union_tag(void) {
    Number n;
    n.i = 0x4241;
    printf("%x\n", n.i);
    printf("%d\n", n.c);
}

void test_enum_tag(void) {
    Color c;
    c = GREEN;
    printf("%d\n", c);
}

void test_chained_typedef(void) {
    AnotherInt x;
    x = 42;
    printf("%d\n", x);
}

void test_globals(void) {
    int i;
    printf("%d\n", g_count);
    for (i = 0; i < 3; i = i + 1) g_values[i] = i * 100;
    printf("%d %d %d\n", g_values[0], g_values[1], g_values[2]);
}

void test_struct_member_typedef(void) {
    struct Labeled l;
    Point pt;
    pt.x = 1;
    pt.y = 2;
    l.name = "widget";
    l.pos = &pt;
    puts(l.name);
    printf("\n%d %d\n", l.pos->x, l.pos->y);
}

int main(void) {
    test_scalar();
    test_pointer();
    test_struct_tag();
    test_union_tag();
    test_enum_tag();
    test_chained_typedef();
    test_globals();
    test_struct_member_typedef();
    return 0;
}
