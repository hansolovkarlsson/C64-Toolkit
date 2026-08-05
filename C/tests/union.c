#include <print.h>

/* cc64 doesn't support array members, so overlap is exercised with
 * two scalar members instead - still genuinely overlapping storage,
 * just not indexed. */
union Overlap {
    int as_int;
    char as_char;
};

union Pair {
    int i;
    char c;
    int *p;
};

/* A union-by-value struct member would hit the same documented
 * restriction a nested struct-by-value member already has ("struct
 * members that are themselves a struct-by-value or an array" isn't
 * supported - see README.md's "Not supported yet") - union reuses
 * struct's own TY_STRUCT machinery, so it correctly inherits that
 * restriction too. A pointer member is the same workaround real
 * self-referential structs already use. */
struct Tagged {
    int kind;
    union Pair *data;
};

void test_size_and_overlap(void) {
    union Overlap u;
    u.as_int = 0x4241; /* low byte 0x41='A', high byte 0x42='B' */
    printf("%x\n", u.as_int);
    printf("%d\n", u.as_char); /* low byte only: 0x41 = 65 */
    u.as_char = 5;
    printf("%x\n", u.as_int); /* high byte (0x42) untouched, low byte now 5 */
}

void test_pointer_member(void) {
    union Pair u;
    int x;
    x = 99;
    u.p = &x;
    printf("%d\n", *(u.p));
}

void test_struct_member(void) {
    struct Tagged t;
    union Pair p;
    t.kind = 1;
    t.data = &p;
    t.data->i = 42;
    printf("%d %d\n", t.kind, t.data->i);
    t.data->c = 7; /* overlaps t.data->i's low byte */
    printf("%d\n", t.data->i);
}

int sum_pair_as_int(union Pair *u) {
    return u->i;
}

void test_union_pointer_param(void) {
    union Pair u;
    u.i = 123;
    printf("%d\n", sum_pair_as_int(&u));
}

int main(void) {
    test_size_and_overlap();
    test_pointer_member();
    test_struct_member();
    test_union_pointer_param();
    return 0;
}
