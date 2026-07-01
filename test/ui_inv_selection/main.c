#include "ui/ui_inv_selection.h"

#include <stdio.h>

#define TEST_ASSERT(cond, msg) \
    do \
    { \
        if( !(cond) ) \
        { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            return 1; \
        } \
    } while( 0 )

static int
test_selection_lifecycle(void)
{
    struct UIInvSelection sel;
    ui_inv_selection_reset(&sel);
    TEST_ASSERT(!ui_inv_selection_is_set(&sel), "not set after reset");

    ui_inv_selection_set(&sel, 2, 5);
    TEST_ASSERT(ui_inv_selection_is_set(&sel), "set after assign");
    TEST_ASSERT(ui_inv_selection_matches(&sel, 2, 5), "matches");
    TEST_ASSERT(!ui_inv_selection_matches(&sel, 2, 6), "slot mismatch");
    TEST_ASSERT(!ui_inv_selection_matches(&sel, 3, 5), "source mismatch");

    ui_inv_selection_clear(&sel);
    TEST_ASSERT(!ui_inv_selection_is_set(&sel), "cleared");
    return 0;
}

int
main(void)
{
    int failures = 0;
    failures += test_selection_lifecycle();

    if( failures == 0 )
    {
        printf("All ui_inv_selection tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
