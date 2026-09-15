/*
 * Who has the keyboard.
 *
 * Every hotkey in the client asks this before acting, and the failure is
 * always the same shape: a text input the rule does not know about leaves the
 * hotkeys live underneath it. That has happened four times, and each time it
 * looked like a different bug -- `f` switching a sidebar tab behind the caret,
 * a spawn digit spawning an npc while typing a bank amount, `W` flying the
 * camera through a search term, a height value both editing the map editor's
 * field and toggling the editor off.
 *
 * So the test's real job is not the boolean algebra. It is to state, once and
 * in one place, WHICH sources count -- and to fail if a source stops counting.
 *
 * What is asserted:
 *
 *   - each source on its own takes the keyboard. Five cases, one per source,
 *     so removing any single clause fails.
 *   - the chat flags need the chat line to exist. A dat2 gameframe with no
 *     revconfig chatbox carries stale flags from whatever ran before, and
 *     reading them there disarms every hotkey for the rest of the session.
 *   - the interface varc is compared against 1 and not merely truthy, because
 *     UNSET READS -1. Truthiness there says "a box has the keyboard" on every
 *     frame of every lane that never sets it.
 *   - a ZEROED struct reads as "somebody has it", because focus id 0 is a real
 *     IF3 component and the unset value is -1. That is the safer direction for
 *     a caller that forgot a field -- every hotkey stops working, which is
 *     noticed in seconds, rather than hotkeys firing under a caret, which is
 *     the subtle bug this module exists to prevent. It is asserted so that
 *     nobody "tidies" the unset value to 0 and quietly flips it.
 *
 * Build and run:
 *   make -C src test-uitree-keyboard-owner
 */

#include "ui/uitree_keyboard_owner.h"

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

/* Nobody is typing. The varc and the focus id both read their unset value,
 * which is -1 for each and is NOT the same as 0. */
static struct UIKeyboardOwners
nobody(void)
{
    struct UIKeyboardOwners owners;

    memset(&owners, 0, sizeof(owners));
    owners.interface_input_active_varc = -1;
    owners.tree_input_focus_id = -1;
    return owners;
}

static void
test_nobody_typing_leaves_the_hotkeys_live(void)
{
    struct UIKeyboardOwners owners = nobody();

    CHECK(!UIKeyboard_TextInputFocused(&owners), "an idle client held the keyboard");

    /* A chat line that exists but is not focused does not hold it either. */
    owners.chat_line_present = true;
    CHECK(!UIKeyboard_TextInputFocused(&owners), "an unfocused chat line held the keyboard");

    /*
     * A fully zeroed struct -- what a caller that forgot a field produces --
     * reads as HELD, because focus id 0 is a real IF3 component and the unset
     * value is -1.
     *
     * That is the direction to fail in. A forgotten field then stops every
     * hotkey, which someone notices immediately; the other way round it fires
     * hotkeys under a caret, which is the bug this module exists to prevent
     * and which took four separate sightings to recognise. Asserted so that
     * nobody "tidies" the unset value to 0 and flips it without noticing.
     */
    {
        struct UIKeyboardOwners zeroed;

        memset(&zeroed, 0, sizeof(zeroed));
        CHECK(
            UIKeyboard_TextInputFocused(&zeroed),
            "a zeroed struct read as nobody typing; a forgotten field now fires "
            "hotkeys under the caret instead of disabling them");
    }
}

static void
test_each_source_takes_it(void)
{
    /* The chat line, three ways. */
    {
        struct UIKeyboardOwners owners = nobody();

        owners.chat_line_present = true;
        owners.chat_input_active = true;
        CHECK(UIKeyboard_TextInputFocused(&owners), "the focused chat line did not take it");
    }
    {
        struct UIKeyboardOwners owners = nobody();

        owners.chat_line_present = true;
        owners.chat_social_input_open = true;
        CHECK(UIKeyboard_TextInputFocused(&owners), "the add-friend prompt did not take it");
    }
    {
        struct UIKeyboardOwners owners = nobody();

        owners.chat_line_present = true;
        owners.chat_dialog_input_open = true;
        CHECK(UIKeyboard_TextInputFocused(&owners), "a dialogue prompt did not take it");
    }

    /* An interface's own search field, via the cache's varc. */
    {
        struct UIKeyboardOwners owners = nobody();

        owners.interface_input_active_varc = 1;
        CHECK(UIKeyboard_TextInputFocused(&owners), "an interface search field did not take it");
    }

    /* An IF3 text-entry field with the caret in it. Id 0 is a real id. */
    {
        struct UIKeyboardOwners owners = nobody();

        owners.tree_input_focus_id = 0;
        CHECK(UIKeyboard_TextInputFocused(&owners), "an IF3 field at id 0 did not take it");
        owners.tree_input_focus_id = 4242;
        CHECK(UIKeyboard_TextInputFocused(&owners), "an IF3 field did not take it");
    }

    /* A chrome field, in either instance -- the caller collapses the two. */
    {
        struct UIKeyboardOwners owners = nobody();

        owners.chrome_field_focused = true;
        CHECK(UIKeyboard_TextInputFocused(&owners), "a chrome field did not take it");
    }
}

static void
test_the_two_traps(void)
{
    /* Trap one: stale chat flags on a lane with no chat line. Read without the
     * presence test, they disarm every hotkey for the rest of the session. */
    {
        struct UIKeyboardOwners owners = nobody();

        owners.chat_line_present = false;
        owners.chat_input_active = true;
        owners.chat_social_input_open = true;
        owners.chat_dialog_input_open = true;
        CHECK(
            !UIKeyboard_TextInputFocused(&owners),
            "chat flags took the keyboard on a lane with no chat line");
    }

    /* Trap two: the interface varc is tri-state and UNSET IS -1. Testing it
     * for truthiness says "a box has the keyboard" on every frame of every
     * lane that never sets it, which is most of them. */
    {
        struct UIKeyboardOwners owners = nobody();

        owners.interface_input_active_varc = -1;
        CHECK(!UIKeyboard_TextInputFocused(&owners), "an unset interface varc (-1) took it");
        owners.interface_input_active_varc = 0;
        CHECK(!UIKeyboard_TextInputFocused(&owners), "a cleared interface varc (0) took it");
        owners.interface_input_active_varc = 2;
        CHECK(
            !UIKeyboard_TextInputFocused(&owners),
            "an interface varc of 2 took it; only 1 means a box has focus");
    }
}

int
main(void)
{
    test_nobody_typing_leaves_the_hotkeys_live();
    test_each_source_takes_it();
    test_the_two_traps();

    if( g_failures )
    {
        printf("uitree_keyboard_owner_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("uitree_keyboard_owner_test: OK\n");
    return 0;
}
