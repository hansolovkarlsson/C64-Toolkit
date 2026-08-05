/*
 * print.h - fixed-arity print helpers. cc64 does have a `printf()`
 * builtin now (no #include needed - see the README's "How printf
 * works"), but only because its format string has to be a compile-time
 * string literal, which is what lets the compiler expand each call
 * into the equivalent chain of fixed-arity calls itself; there's still
 * no real variadic-function support underneath it. print_int(n)/
 * print_uint(n)/print_hex(n) below are that same expansion, just
 * written out by hand - useful on their own when you want one piece of
 * a message without the rest of it being a compile-time-constant
 * format string at all. All of these build on putchar(), so they
 * automatically get its PETSCII case handling - see the README's
 * "PETSCII and case" section if that's unfamiliar.
 *
 * print_uint() used to be missing here (and printf() had no `%u`),
 * since with no `unsigned` type there was no correct way to write
 * "divide this 16-bit bit pattern as if it were unsigned" using cc64's
 * own `/` and `%` (always signed at the time) - now that `unsigned
 * int` exists (see the README's "How unsigned works"), it's exactly as
 * straightforward as print_int() minus the sign handling.
 */

/* Prints n in decimal, with a leading '-' if negative. */
void print_int(int n) {
    char buf[6];
    int i;
    int neg;
    neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) {
        putchar('0');
        return;
    }
    i = 0;
    while (n > 0) {
        buf[i] = n % 10;
        n = n / 10;
        i = i + 1;
    }
    if (neg) putchar('-');
    while (i > 0) {
        i = i - 1;
        putchar(buf[i] + 48);
    }
}

/* Prints n in decimal as unsigned - the same digit-collection algorithm
 * as print_int() above, minus the negation/sign handling (there's none
 * to do). */
void print_uint(unsigned int n) {
    char buf[6];
    int i;
    if (n == 0) {
        putchar('0');
        return;
    }
    i = 0;
    while (n > 0) {
        buf[i] = n % 10;
        n = n / 10;
        i = i + 1;
    }
    while (i > 0) {
        i = i - 1;
        putchar(buf[i] + 48);
    }
}

/* Prints n as 4 uppercase hex digits (0000-FFFF), the raw bit pattern
 * regardless of sign - useful for addresses (e.g. print_hex(&x)
 * won't compile since &x is a pointer, not an int, but
 * print_hex(peek(addr)) or printing a value built from shifts and
 * masks works fine). Extracting 4 bits at a time with `(n >> k) & 15`
 * gives the right answer even though cc64's `>>` sign-extends
 * negative values: the sign-extended copies only ever land ABOVE the
 * 4 bits `& 15` keeps, for every shift amount used here (0, 4, 8, 12),
 * so they never contaminate the digit being extracted. */
void print_hex(int n) {
    int i;
    int shift;
    int digit;
    for (i = 3; i >= 0; i = i - 1) {
        shift = i * 4;
        digit = (n >> shift) & 15;
        if (digit < 10) putchar(digit + 48);
        else putchar(digit - 10 + 65);
    }
}

/* Shorthand for putchar('\n') - a small thing, but it's the single
 * most-repeated line across this project's own test programs before
 * this library existed. */
void newline(void) {
    putchar('\n');
}
