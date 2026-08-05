#include <print.h>

/* unsigned as a global, with a value that would be negative if
 * interpreted as a signed 16-bit int (50000 - 65536 = -15536). */
unsigned int g_big = 50000;

/* unsigned as a typedef, chained through parse_type_prefix()'s
 * typedef branch (see "How typedef works"/"How unsigned works"). */
typedef unsigned int UInt;

struct Counter {
    unsigned int value;
    int tag;
};

/* unsigned as a parameter and return type; `/` inside is the whole
 * point of this test - it must round differently than signed division
 * would for these same bit patterns. */
unsigned int udiv(unsigned int a, unsigned int b) {
    return a / b;
}
unsigned int umod(unsigned int a, unsigned int b) {
    return a % b;
}

void test_global(void) {
    printf("%u\n", g_big);          /* 50000 */
}

void test_local(void) {
    unsigned int u;
    u = 60000;
    printf("%u\n", u);              /* 60000 */
}

/* Division/modulo: 60000/7 as UNSIGNED is 8571 remainder 3. The same
 * bit pattern read as a signed 16-bit int is -5536, and -5536/7 would
 * truncate to -790 remainder -6 - a completely different answer, which
 * is exactly why `/`/`%` need to route to a different runtime routine
 * for unsigned operands (see gen_binop() in codegen_expr.c). */
void test_divmod(void) {
    unsigned int a;
    unsigned int q;
    unsigned int r;
    a = 60000;
    q = a / 7;
    r = a % 7;
    printf("%u %u\n", q, r);        /* 8571 3 */
    printf("%u %u\n", udiv(a, 7), umod(a, 7)); /* 8571 3 */
}

/* Right shift: 32768 >> 1 as UNSIGNED (logical, zero-fill) is 16384.
 * The same bit pattern read as signed int is -32768, and -32768 >> 1
 * (arithmetic, sign-extending) would give -16384 instead. */
void test_shift(void) {
    unsigned int u;
    u = 32768;
    printf("%u\n", u >> 1);         /* 16384 */
    u >>= 2;
    printf("%u\n", u);              /* 8192 */
}

/* Comparison: 65535 as UNSIGNED is greater than 1. The same bit
 * pattern read as signed int is -1, which is NOT greater than 1 -
 * again, why comparisons need the unsigned runtime routines (already
 * used for pointer comparisons before this feature, now reused here). */
void test_compare(void) {
    unsigned int u;
    u = 65535;
    if (u > 1) printf("gt\n"); else printf("not-gt\n");   /* gt */
    if (u >= 65535) printf("ge\n"); else printf("not-ge\n"); /* ge */
    if (1 < u) printf("lt\n"); else printf("not-lt\n");   /* lt */
}

/* Wraparound: subtracting past 0 wraps to 65535, not a negative value
 * (`-` itself needs no special unsigned handling - it's bit-pattern-
 * invariant - only how the RESULT is later divided/shifted/compared
 * or printed changes). */
void test_wrap(void) {
    unsigned int u;
    u = 0;
    u = u - 1;
    printf("%u\n", u);              /* 65535 */
}

/* unsigned struct member, including through a pointer and via
 * compound assignment (exercises resolve_lvalue_base()'s isUnsigned
 * on the N_MEMBER path, both direct-label and indirect). */
void test_struct_member(void) {
    struct Counter c;
    struct Counter *p;
    c.value = 50000;
    c.tag = -1;
    printf("%u\n", c.value);        /* 50000 */
    c.value /= 7;
    printf("%u\n", c.value);        /* 7142 */
    p = &c;
    p->value = 60000;
    p->value %= 7;
    printf("%u\n", p->value);       /* 3 */
}

/* unsigned array element (exercises resolve_lvalue_base()'s isUnsigned
 * on the N_INDEX fast path). */
void test_array(void) {
    unsigned int arr[3];
    arr[0] = 65535;
    arr[1] = arr[0] / 10;
    printf("%u %u\n", arr[0], arr[1]); /* 65535 6553 */
}

/* typedef'd unsigned type, chained through UInt -> unsigned int. */
void test_typedef(void) {
    UInt x;
    x = 40000;
    printf("%u\n", x / 3);          /* 13333 */
}

/* print_uint() (lib/print.h), the ordinary-cc64-source counterpart to
 * printf's %u. */
void test_print_uint(void) {
    print_uint(65535); newline();   /* 65535 */
    print_uint(0); newline();       /* 0 */
    print_uint(12345); newline();   /* 12345 */
}

void main(void) {
    test_global();
    test_local();
    test_divmod();
    test_shift();
    test_compare();
    test_wrap();
    test_struct_member();
    test_array();
    test_typedef();
    test_print_uint();
}
