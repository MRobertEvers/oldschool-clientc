/*
 * The session facts the server sends once or rarely, and the reboot countdown
 * among them.
 *
 * A countdown that steps into 0 makes the system-update line vanish just
 * before the update; one stored in server ticks instead of client cycles
 * shows a time thirty times too short.
 *
 *   make -C src test-rs-session-info
 */
#include "game/rs_session_info.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* The client's own ratios, written out so this test needs nothing else. */
enum
{
    TEST_CYCLES_PER_TICK = 30,
    TEST_CYCLES_PER_SECOND = 50,
};

static void
test_reset(void)
{
    struct RS_SessionInfo info;

    printf("TEST: reset says nothing\n");

    memset(&info, 0x5a, sizeof(info));
    RS_SessionInfo_Reset(&info);
    CHECK(info.welcome.last_ip == 0, "last_ip %d", info.welcome.last_ip);
    CHECK(info.welcome.member_warning == 0, "member_warning %d", info.welcome.member_warning);
    CHECK(info.reboot_timer == 0, "reboot_timer %d", info.reboot_timer);
    CHECK(info.unknown_string[0] == '\0', "unknown_string not empty");
    CHECK(info.site_settings[0] == '\0', "site_settings not empty");
    CHECK(info.website_token[0] == '\0', "website_token not empty");
    CHECK(info.tracking_enabled == 0, "tracking_enabled %d", info.tracking_enabled);
    CHECK(RS_SessionInfo_Multiway(&info) == 0, "multiway %d", RS_SessionInfo_Multiway(&info));
    CHECK(RS_SessionInfo_RebootTimerText(&info, TEST_CYCLES_PER_SECOND) == NULL,
        "no update pending has no line");
}

static void
test_reboot_countdown(void)
{
    struct RS_SessionInfo info;
    char const* text;

    printf("TEST: ticks become cycles; the countdown stops at 1\n");

    RS_SessionInfo_Reset(&info);
    CHECK(!RS_SessionInfo_StepRebootTimer(&info), "no countdown does not step");
    CHECK(info.reboot_timer == 0, "stepping nothing left %d", info.reboot_timer);

    /* 100 ticks of 600 ms = 60 s. */
    RS_SessionInfo_SetRebootTimer(&info, 100, TEST_CYCLES_PER_TICK);
    CHECK(info.reboot_timer == 100 * TEST_CYCLES_PER_TICK, "stored %d", info.reboot_timer);
    text = RS_SessionInfo_RebootTimerText(&info, TEST_CYCLES_PER_SECOND);
    CHECK(text && strcmp(text, "System update in: 1:00") == 0, "line '%s'", text ? text : "(null)");
    CHECK(text == info.reboot_timer_text, "the line is the info's own buffer");

    CHECK(RS_SessionInfo_StepRebootTimer(&info), "a running countdown steps");
    CHECK(info.reboot_timer == 100 * TEST_CYCLES_PER_TICK - 1, "stepped to %d", info.reboot_timer);
    text = RS_SessionInfo_RebootTimerText(&info, TEST_CYCLES_PER_SECOND);
    CHECK(text && strcmp(text, "System update in: 0:59") == 0, "line '%s'", text ? text : "(null)");

    RS_SessionInfo_SetRebootTimer(&info, 0, TEST_CYCLES_PER_TICK);
    CHECK(info.reboot_timer == 0, "0 ticks cancels (%d)", info.reboot_timer);

    /* Two cycles left: one step to 1, then it holds there. */
    info.reboot_timer = 2;
    CHECK(RS_SessionInfo_StepRebootTimer(&info), "2 steps to 1");
    CHECK(info.reboot_timer == 1, "at %d", info.reboot_timer);
    CHECK(!RS_SessionInfo_StepRebootTimer(&info), "1 does not step");
    CHECK(info.reboot_timer == 1, "held at %d", info.reboot_timer);
    text = RS_SessionInfo_RebootTimerText(&info, TEST_CYCLES_PER_SECOND);
    CHECK(text && strcmp(text, "System update in: 0:00") == 0, "line '%s'", text ? text : "(null)");

    /* Minutes past ten, seconds zero-padded. */
    RS_SessionInfo_SetRebootTimer(&info, 1205, TEST_CYCLES_PER_TICK);
    text = RS_SessionInfo_RebootTimerText(&info, TEST_CYCLES_PER_SECOND);
    CHECK(text && strcmp(text, "System update in: 12:03") == 0, "line '%s'", text ? text : "(null)");
}

static void
test_multiway(void)
{
    struct RS_SessionInfo info;

    printf("TEST: SET_MULTIWAY is stored as sent and reports a change\n");

    RS_SessionInfo_Reset(&info);
    CHECK(RS_SessionInfo_SetMultiway(&info, 1), "0 -> 1 is a change");
    CHECK(RS_SessionInfo_Multiway(&info) == 1, "stored %d", RS_SessionInfo_Multiway(&info));
    CHECK(!RS_SessionInfo_SetMultiway(&info, 1), "1 -> 1 is not");
    CHECK(RS_SessionInfo_SetMultiway(&info, 2), "any other value is stored as sent");
    CHECK(RS_SessionInfo_Multiway(&info) == 2, "stored %d", RS_SessionInfo_Multiway(&info));
    CHECK(RS_SessionInfo_SetMultiway(&info, 0), "back to 0 is a change");
    CHECK(RS_SessionInfo_Multiway(&info) == 0, "stored %d", RS_SessionInfo_Multiway(&info));
}

int
main(void)
{
    test_reset();
    test_reboot_countdown();
    test_multiway();
    if( g_failures )
    {
        printf("rs_session_info: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("rs_session_info: all checks passed\n");
    return 0;
}
