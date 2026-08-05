#include <print.h>

/* do/while: body runs at least once, even when the condition is false
 * from the start. */
void test_dowhile_runs_once(void) {
    int i;
    int count;
    i = 10;
    count = 0;
    do {
        count = count + 1;
    } while (i < 5);
    print_int(count);
    newline(); /* expect 1 */
}

/* do/while: ordinary counting loop, and continue re-tests the
 * condition rather than jumping straight back to the top. */
void test_dowhile_continue(void) {
    int i;
    int sum;
    i = 0;
    sum = 0;
    do {
        i = i + 1;
        if (i == 3) continue; /* skip adding 3, but still re-check i < 5 */
        sum = sum + i;
    } while (i < 5);
    print_int(sum); /* 1+2+4+5 = 12 */
    newline();
}

void test_dowhile_break(void) {
    int i;
    i = 0;
    do {
        if (i == 3) break;
        i = i + 1;
    } while (i < 100);
    print_int(i);
    newline(); /* expect 3 */
}

/* switch: basic dispatch, default, and break preventing fallthrough. */
void test_switch_basic(int x) {
    switch (x) {
        case 1:
            puts("one");
            break;
        case 2:
            puts("two");
            break;
        default:
            puts("other");
            break;
    }
}

/* switch: fallthrough when break is omitted. */
void test_switch_fallthrough(void) {
    int x;
    x = 1;
    switch (x) {
        case 1:
            puts("case1");
            /* no break -- falls through */
        case 2:
            puts("case2");
            break;
        case 3:
            puts("case3");
            break;
    }
}

/* switch: negative case constants, and no default at all. */
void test_switch_negative(int x) {
    switch (x) {
        case -1:
            puts("neg-one");
            break;
        case 0:
            puts("zero");
            break;
    }
}

/* switch inside a loop: break exits the switch, not the loop; continue
 * skips to the loop's next iteration, not blocked by the switch. */
void test_switch_in_loop(void) {
    int i;
    int sum;
    i = 0;
    sum = 0;
    while (i < 5) {
        i = i + 1;
        switch (i) {
            case 2:
                continue; /* must continue the WHILE, not just break the switch */
            case 4:
                break; /* must only break the SWITCH, loop keeps going */
            default:
                sum = sum + i;
        }
        sum = sum + 100; /* skipped when i==2 (continue), reached when i==4 (switch break only) */
    }
    print_int(sum); /* i=1: sum+=1,+100=101; i=2: continue, sum+=0; i=3: sum+=3,+100=204;
                        i=4: switch break, sum+=100=304; i=5: sum+=5,+100=409 */
    newline();
}

/* Nested switch: inner switch's break must not affect outer switch. */
void test_switch_nested(int outer, int inner) {
    switch (outer) {
        case 1:
            switch (inner) {
                case 1:
                    puts("outer1-inner1");
                    break;
                default:
                    puts("outer1-innerX");
            }
            puts("after-inner");
            break;
        default:
            puts("outerX");
    }
}

int main(void) {
    test_dowhile_runs_once();
    test_dowhile_continue();
    test_dowhile_break();

    test_switch_basic(1);
    test_switch_basic(2);
    test_switch_basic(99);

    test_switch_fallthrough();

    test_switch_negative(-1);
    test_switch_negative(0);

    test_switch_in_loop();

    test_switch_nested(1, 1);
    test_switch_nested(1, 9);
    test_switch_nested(9, 9);

    return 0;
}
