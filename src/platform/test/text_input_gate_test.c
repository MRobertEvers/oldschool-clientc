/*
 * Whether the on-screen keyboard should be up, and whether the platform must
 * be told again.
 *
 * The rules, with no platform and no App: any of the client's fields, the
 * scripts' request or a plugin's is enough; the answer is handed out only on
 * a change, except that a plugin's statement and an invalidate both make the
 * next take hand it out again; and the keyboard inset falling to zero
 * retracts the scripts' request but not a plugin's.
 *
 * Run: make -C src test-text-input-gate
 */

#include "platform/text_input_gate.h"

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

static struct TextInputGate
fresh(void)
{
    struct TextInputGate gate;

    memset(&gate, 0x5a, sizeof(gate));
    TextInputGate_Reset(&gate);
    return gate;
}

static void
test_wanted_any_source(void)
{
    struct TextInputGate gate = fresh();

    TEST_ASSERT(TextInputGate_Wanted(&gate, 0) == 0, "nothing wants it");
    TEST_ASSERT(TextInputGate_Wanted(&gate, 1) == 1, "the client's fields are enough");
    TEST_ASSERT(TextInputGate_Wanted(&gate, 7) == 1, "the answer is 1, not the caller's value");

    TextInputGate_SetScriptRequest(&gate, 1);
    TEST_ASSERT(TextInputGate_Wanted(&gate, 0) == 1, "the scripts' request is enough");
    TextInputGate_SetScriptRequest(&gate, 0);
    TEST_ASSERT(TextInputGate_Wanted(&gate, 0) == 0, "the scripts' hide puts it away");

    TextInputGate_SetPluginRequest(&gate, 3);
    TEST_ASSERT(gate.plugin_on == 1, "a plugin's on is stored as 1");
    TEST_ASSERT(TextInputGate_Wanted(&gate, 0) == 1, "a plugin's request is enough");
    TextInputGate_SetPluginRequest(&gate, 0);
    TEST_ASSERT(TextInputGate_Wanted(&gate, 0) == 0, "a plugin's off puts it away");
}

static void
test_take_only_on_change(void)
{
    struct TextInputGate gate = fresh();
    int on = 42;

    TEST_ASSERT(TextInputGate_Take(&gate, 0, &on) == 0, "a fresh gate has pushed off already");
    TEST_ASSERT(on == 42, "no take leaves out_on untouched");

    TEST_ASSERT(TextInputGate_Take(&gate, 1, &on) == 1, "wanted rising is taken");
    TEST_ASSERT(on == 1, "taken as on");
    TEST_ASSERT(TextInputGate_Take(&gate, 1, &on) == 0, "the same answer is not taken twice");

    TEST_ASSERT(TextInputGate_Take(&gate, 0, &on) == 1, "wanted falling is taken");
    TEST_ASSERT(on == 0, "taken as off");
    TEST_ASSERT(TextInputGate_Take(&gate, 0, &on) == 0, "and only once");
}

static void
test_invalidate_pushes_again(void)
{
    struct TextInputGate gate = fresh();
    int on = -5;

    TEST_ASSERT(TextInputGate_Take(&gate, 1, &on) == 1, "pushed on");
    TEST_ASSERT(TextInputGate_Take(&gate, 1, &on) == 0, "nothing new");

    /* The person put the keyboard away with the system's button: nothing
     * here changed, and a tap on the field must bring it back. */
    TextInputGate_Invalidate(&gate);
    TEST_ASSERT(gate.effective == -1, "invalidate forgets what was pushed");
    on = -5;
    TEST_ASSERT(TextInputGate_Take(&gate, 1, &on) == 1, "the same answer is pushed again");
    TEST_ASSERT(on == 1, "and it is on");
    TEST_ASSERT(TextInputGate_Take(&gate, 1, &on) == 0, "once");

    TextInputGate_Invalidate(&gate);
    TEST_ASSERT(TextInputGate_Take(&gate, 0, &on) == 1, "an invalidated off is pushed too");
    TEST_ASSERT(on == 0, "as off");
}

static void
test_plugin_statement_always_taken(void)
{
    struct TextInputGate gate = fresh();
    int on = 9;

    TEST_ASSERT(TextInputGate_Take(&gate, TextInputGate_Wanted(&gate, 1), &on) == 1, "client on");
    TEST_ASSERT(TextInputGate_Take(&gate, TextInputGate_Wanted(&gate, 1), &on) == 0, "settled");

    /* The answer does not move (the client already wants it), but the plugin's
     * statement is still pushed. */
    TextInputGate_SetPluginRequest(&gate, 1);
    on = 9;
    TEST_ASSERT(TextInputGate_Take(&gate, TextInputGate_Wanted(&gate, 1), &on) == 1,
                "a plugin's statement is taken though the answer did not change");
    TEST_ASSERT(on == 1, "with the current answer");

    /* A plugin's off while the client still wants it pushes the client's on. */
    TextInputGate_SetPluginRequest(&gate, 0);
    TEST_ASSERT(TextInputGate_Take(&gate, TextInputGate_Wanted(&gate, 1), &on) == 1,
                "a plugin's off is taken");
    TEST_ASSERT(on == 1, "and the client's fields still win");
}

static void
test_inset_falling_retracts_script(void)
{
    struct TextInputGate gate = fresh();

    TEST_ASSERT(TextInputGate_KeyboardInset(&gate) == 0, "no inset at first");
    TEST_ASSERT(TextInputGate_SetKeyboardInset(&gate, 0) == 0, "the same inset is no change");

    TextInputGate_SetScriptRequest(&gate, 1);
    TextInputGate_SetPluginRequest(&gate, 1);
    TEST_ASSERT(TextInputGate_SetKeyboardInset(&gate, 120) == 1, "the keyboard came up");
    TEST_ASSERT(TextInputGate_KeyboardInset(&gate) == 120, "its rows are kept");
    TEST_ASSERT(gate.vm_keyboard_open == 1, "rising keeps the scripts' request");

    TEST_ASSERT(TextInputGate_SetKeyboardInset(&gate, 80) == 1, "a smaller inset is a change");
    TEST_ASSERT(gate.vm_keyboard_open == 1, "a smaller inset is not the keyboard going away");
    TEST_ASSERT(TextInputGate_SetKeyboardInset(&gate, 80) == 0, "the same inset again is not");

    TEST_ASSERT(TextInputGate_SetKeyboardInset(&gate, 0) == 1, "the keyboard went away");
    TEST_ASSERT(gate.vm_keyboard_open == 0, "falling to zero retracts the scripts' request");
    TEST_ASSERT(gate.plugin_on == 1, "but not a plugin's");
    TEST_ASSERT(TextInputGate_KeyboardInset(&gate) == 0, "and the inset is zero");

    /* With no inset before, a script request survives a zero report. */
    TextInputGate_SetScriptRequest(&gate, 1);
    TEST_ASSERT(TextInputGate_SetKeyboardInset(&gate, 0) == 0, "zero to zero is no change");
    TEST_ASSERT(gate.vm_keyboard_open == 1, "and retracts nothing");
}

int
main(void)
{
    test_wanted_any_source();
    test_take_only_on_change();
    test_invalidate_pushes_again();
    test_plugin_statement_always_taken();
    test_inset_falling_retracts_script();

    if( g_failures )
    {
        printf("text_input_gate_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("text_input_gate_test: ok\n");
    return 0;
}
