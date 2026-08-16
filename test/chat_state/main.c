#include "ui/chat_state.h"

#include <stdio.h>
#include <string.h>

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
test_add_line_overflow(void)
{
    struct ChatState state;
    chat_state_reset(&state);

    for( int i = 0; i < CHAT_STATE_LINE_MAX + 5; i++ )
    {
        char text[32];
        snprintf(text, sizeof(text), "line%d", i);
        chat_state_add_line(&state, text, "user", 1);
    }

    TEST_ASSERT(state.line_count == CHAT_STATE_LINE_MAX, "line count capped");
    TEST_ASSERT(strcmp(state.lines[0].text, "line5") == 0, "oldest dropped");
    TEST_ASSERT(strcmp(state.lines[CHAT_STATE_LINE_MAX - 1].text, "line104") == 0, "newest kept");
    return 0;
}

static int
test_friend_list(void)
{
    struct ChatState state;
    chat_state_reset(&state);

    TEST_ASSERT(chat_state_add_friend(&state, "alice"), "add alice");
    TEST_ASSERT(chat_state_is_friend(&state, "alice"), "alice is friend");
    TEST_ASSERT(!chat_state_add_friend(&state, "alice"), "duplicate rejected");
    TEST_ASSERT(chat_state_add_friend(&state, "bob"), "add bob");
    TEST_ASSERT(chat_state_remove_friend(&state, "alice"), "remove alice");
    TEST_ASSERT(!chat_state_is_friend(&state, "alice"), "alice removed");
    TEST_ASSERT(state.friend_count == 1, "one friend remains");
    return 0;
}

static int
test_mode_cycling(void)
{
    struct ChatState state;
    chat_state_reset(&state);

    for( int i = 0; i < 4; i++ )
        chat_state_cycle_public_mode(&state);
    TEST_ASSERT(state.public_mode == 0, "public mode wraps at 4");

    for( int i = 0; i < 3; i++ )
        chat_state_cycle_private_mode(&state);
    TEST_ASSERT(state.private_mode == 0, "private mode wraps at 3");

    chat_state_set_filter_modes(&state, 2, 1, 0);
    TEST_ASSERT(state.public_mode == 2 && state.private_mode == 1 && state.trade_mode == 0,
                "set filter modes");
    return 0;
}

int
main(void)
{
    int failures = 0;
    failures += test_add_line_overflow();
    failures += test_friend_list();
    failures += test_mode_cycling();

    if( failures == 0 )
    {
        printf("All chat_state tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d test group(s) failed.\n", failures);
    return 1;
}
