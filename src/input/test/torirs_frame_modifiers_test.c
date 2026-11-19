/*
 * The modifier keys latched once a frame (input/torirs_frame_modifiers.h).
 *
 * Nothing is held after a reset; a latch stores the frame's Ctrl as 1 or 0
 * whatever nonzero value the input gave; and the latch holds until the next
 * one, so every reader later in the frame sees the same answer. The parked
 * input is NULL until a frame parks one, is the pointer parked, and is not
 * touched by a Ctrl latch.
 *
 * Run: make -C src test-frame-modifiers
 */

#include "input/torirs_frame_modifiers.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static void
test_reset_holds_nothing(void)
{
    struct ToriRS_FrameModifiers mods;
    memset(&mods, 0x5a, sizeof(mods));
    ToriRS_FrameModifiersReset(&mods);
    TEST_ASSERT(ToriRS_FrameModifiersCtrl(&mods) == 0, "reset: Ctrl is not held");
    TEST_ASSERT(ToriRS_FrameModifiersInput(&mods) == NULL, "reset: no input parked");

    struct ToriRS_FrameModifiers zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    TEST_ASSERT(memcmp(&zeroed, &mods, sizeof(mods)) == 0, "reset is the zeroed struct");
}

static void
test_latch_holds_until_the_next(void)
{
    struct ToriRS_FrameModifiers mods;
    ToriRS_FrameModifiersReset(&mods);

    ToriRS_FrameModifiersLatchCtrl(&mods, 4);
    TEST_ASSERT(ToriRS_FrameModifiersCtrl(&mods) == 1, "a held Ctrl is stored as 1");
    TEST_ASSERT(ToriRS_FrameModifiersCtrl(&mods) == 1, "and read again it is still held");

    ToriRS_FrameModifiersLatchCtrl(&mods, 0);
    TEST_ASSERT(ToriRS_FrameModifiersCtrl(&mods) == 0, "the next frame's release is latched");
}

static void
test_parked_input_is_the_pointer_parked(void)
{
    struct ToriRS_FrameModifiers mods;
    /* The module never looks inside the input; any address stands for one. */
    int frame_a;
    int frame_b;
    struct LibToriRS_Input* a = (struct LibToriRS_Input*)(void*)&frame_a;
    struct LibToriRS_Input* b = (struct LibToriRS_Input*)(void*)&frame_b;

    ToriRS_FrameModifiersReset(&mods);
    ToriRS_FrameModifiersParkInput(&mods, a);
    TEST_ASSERT(ToriRS_FrameModifiersInput(&mods) == a, "the parked input reads back");

    ToriRS_FrameModifiersLatchCtrl(&mods, 1);
    TEST_ASSERT(ToriRS_FrameModifiersInput(&mods) == a, "a Ctrl latch leaves the input");

    ToriRS_FrameModifiersParkInput(&mods, b);
    TEST_ASSERT(ToriRS_FrameModifiersInput(&mods) == b, "the next frame's input replaces it");
    TEST_ASSERT(ToriRS_FrameModifiersCtrl(&mods) == 1, "and parking leaves Ctrl");

    ToriRS_FrameModifiersParkInput(&mods, NULL);
    TEST_ASSERT(ToriRS_FrameModifiersInput(&mods) == NULL, "NULL parks no input");
}

int
main(void)
{
    test_reset_holds_nothing();
    test_latch_holds_until_the_next();
    test_parked_input_is_the_pointer_parked();
    if( g_failures )
    {
        printf("torirs_frame_modifiers_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("torirs_frame_modifiers_test: ok\n");
    return 0;
}
