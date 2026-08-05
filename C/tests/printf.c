#include <print.h>

void test_basic(void) {
    printf("hello, %s! you are %d years old.\n", "world", 42);
}

void test_all_specifiers(void) {
    printf("d=%d x=%x c=%c s=%s pct=%%\n", 7, 255, 'A', "str");
}

void test_negative(void) {
    printf("%d %d %d\n", -1, -12345, 0);
}

void test_hex_widths(void) {
    printf("%x %x %x %x\n", 0, 15, 256, 65535);
}

void test_no_args(void) {
    printf("no specifiers here\n");
}

void test_only_specifier(void) {
    printf("%d", 99);
    printf("\n");
}

void test_expr_args(void) {
    int a;
    int b;
    a = 3;
    b = 4;
    printf("%d + %d = %d\n", a, b, a + b);
}

void test_leading_and_trailing_percent(void) {
    printf("%%at-start %d %%at-end\n", 5);
}

void test_char_arithmetic(void) {
    int base;
    base = 'A';
    printf("%c%c%c\n", base, base + 1, base + 2);
}

int main(void) {
    test_basic();
    test_all_specifiers();
    test_negative();
    test_hex_widths();
    test_no_args();
    test_only_specifier();
    test_expr_args();
    test_leading_and_trailing_percent();
    test_char_arithmetic();
    return 0;
}
