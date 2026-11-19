/*
 * The device facts stated at boot (platform/device_caps.h), and the touch
 * policy among them.
 *
 * The policy follows the clientscript identity (clienttype 7, or on_mobile),
 * is on wherever the platform's pointer is a finger, and an override read as
 * a number outranks both ways: `0` turns a mobile identity back to the desk,
 * `1` turns a desk to touch, and an empty or absent override says nothing.
 * The three facts are stored apart: the touch policy does not imply a
 * keyboard or a finger camera.
 *
 * Run: make -C src test-device-caps
 */

#include "platform/device_caps.h"

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
test_reset_is_a_desk(void)
{
    struct DeviceCaps caps;
    memset(&caps, 0x5a, sizeof(caps));
    DeviceCaps_Reset(&caps);
    TEST_ASSERT(!DeviceCaps_TouchCamera(&caps), "reset: the mouse turns the camera");
    TEST_ASSERT(!DeviceCaps_TouchUi(&caps), "reset: the desktop interface");
    TEST_ASSERT(!DeviceCaps_ScreenKeyboard(&caps), "reset: no keys to summon");

    struct DeviceCaps zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    TEST_ASSERT(memcmp(&zeroed, &caps, sizeof(caps)) == 0, "reset is the zeroed struct");
}

static void
test_touch_follows_the_identity(void)
{
    TEST_ASSERT(DeviceCaps_TouchUiRule(10, 0, 0, NULL) == 0, "the desktop client is not touch");
    TEST_ASSERT(DeviceCaps_TouchUiRule(7, 0, 0, NULL) == 1, "clienttype 7 is touch");
    TEST_ASSERT(DeviceCaps_TouchUiRule(10, 1, 0, NULL) == 1, "on_mobile is touch");
    TEST_ASSERT(DeviceCaps_TouchUiRule(8, 1, 0, NULL) == 1, "the iOS client, on mobile, is touch");
    TEST_ASSERT(DeviceCaps_TouchUiRule(8, 0, 0, NULL) == 0, "clienttype 8 alone is not");
    TEST_ASSERT(DeviceCaps_TouchUiRule(10, 0, 1, NULL) == 1,
                "a finger platform is touch whatever the identity says");
}

static void
test_override_outranks_both_ways(void)
{
    TEST_ASSERT(DeviceCaps_TouchUiRule(7, 1, 1, "0") == 0,
                "an override of 0 takes even a finger platform back to the desk");
    TEST_ASSERT(DeviceCaps_TouchUiRule(10, 0, 0, "1") == 1, "an override of 1 turns a desk to touch");
    TEST_ASSERT(DeviceCaps_TouchUiRule(10, 0, 0, "2") == 1, "any nonzero number is on");
    TEST_ASSERT(DeviceCaps_TouchUiRule(10, 0, 0, "no") == 0,
                "a word is read as a number, and is 0");
    TEST_ASSERT(DeviceCaps_TouchUiRule(7, 0, 0, "") == 1, "an empty override says nothing");
    TEST_ASSERT(DeviceCaps_TouchUiRule(10, 0, 0, "") == 0, "an empty override says nothing on a desk");
}

static void
test_facts_are_stored_apart(void)
{
    struct DeviceCaps caps;
    DeviceCaps_Reset(&caps);

    DeviceCaps_SetTouchUi(&caps, 5);
    TEST_ASSERT(DeviceCaps_TouchUi(&caps) == 1, "the touch policy is stored as 1");
    TEST_ASSERT(!DeviceCaps_TouchCamera(&caps), "the touch policy is not the finger camera");
    TEST_ASSERT(!DeviceCaps_ScreenKeyboard(&caps), "the touch policy has no keys to summon");

    DeviceCaps_SetTouchCamera(&caps, 1);
    TEST_ASSERT(DeviceCaps_TouchCamera(&caps) == 1, "the finger camera is stated");
    DeviceCaps_SetTouchCamera(&caps, 0);
    TEST_ASSERT(DeviceCaps_TouchCamera(&caps) == 0, "and can be stated off");

    DeviceCaps_SetScreenKeyboard(&caps, 1);
    TEST_ASSERT(DeviceCaps_ScreenKeyboard(&caps) == 1, "the keyboard is the platform's answer");
    DeviceCaps_SetTouchUi(&caps, 0);
    TEST_ASSERT(DeviceCaps_ScreenKeyboard(&caps) == 1, "and turning touch off leaves it");
}

int
main(void)
{
    test_reset_is_a_desk();
    test_touch_follows_the_identity();
    test_override_outranks_both_ways();
    test_facts_are_stored_apart();
    if( g_failures )
    {
        printf("device_caps_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("device_caps_test: ok\n");
    return 0;
}
