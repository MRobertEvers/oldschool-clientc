/*
 * The "Use <item> with ..." and "Cast <spell> on ..." selections
 * (ui/target_select.h).
 *
 * Arming each kind, the clear that drops only the flag and leaves the record,
 * the name cut to its buffer, and the spell's prompt text for a classic target
 * button (verb split at its first space, with and without one, parts capped)
 * and an IF3 one (with and without an op base).
 *
 * Run: make -C src test-target-select
 */

#include "ui/target_select.h"

#include <stdio.h>
#include <stdlib.h>
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
test_reset_is_nothing_armed(void)
{
    struct UITargetSelect sel;
    memset(&sel, 0x5a, sizeof(sel));
    UITargetSelect_Reset(&sel);
    TEST_ASSERT(!UITargetSelect_Armed(&sel), "reset arms nothing");
    TEST_ASSERT(sel.obj.component_id == 0 && sel.target.component_id == 0, "reset zeroes the record");
    TEST_ASSERT(sel.obj.name[0] == '\0' && sel.target.op[0] == '\0', "reset empties the strings");
}

static void
test_arm_and_clear_obj(void)
{
    struct UITargetSelect sel;
    UITargetSelect_Reset(&sel);
    UITargetSelect_ArmObj(&sel, 1511, 3, 0x00950000, "Logs");
    TEST_ASSERT(sel.obj.active, "the item is armed");
    TEST_ASSERT(!sel.target.active, "arming the item does not arm a spell");
    TEST_ASSERT(UITargetSelect_Armed(&sel), "something is armed");
    TEST_ASSERT(sel.obj.obj_id == 1511, "obj id kept");
    TEST_ASSERT(sel.obj.slot == 3, "slot kept");
    TEST_ASSERT(sel.obj.component_id == 0x00950000, "component kept");
    TEST_ASSERT(strcmp(sel.obj.name, "Logs") == 0, "name kept");

    UITargetSelect_ClearObj(&sel);
    TEST_ASSERT(!UITargetSelect_Armed(&sel), "nothing armed after the clear");
    TEST_ASSERT(sel.obj.obj_id == 1511 && sel.obj.slot == 3, "the clear leaves the record");
    TEST_ASSERT(strcmp(sel.obj.name, "Logs") == 0, "the clear leaves the name");
}

static void
test_obj_name_is_cut_to_fit(void)
{
    struct UITargetSelect sel;
    char longname[128];
    memset(longname, 'a', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    UITargetSelect_Reset(&sel);
    UITargetSelect_ArmObj(&sel, 1, 0, 0, longname);
    TEST_ASSERT(strlen(sel.obj.name) == sizeof(sel.obj.name) - 1, "a long name fills the buffer");
}

static void
test_arm_and_clear_target(void)
{
    struct UITargetSelect sel;
    UITargetSelect_Reset(&sel);
    UITargetSelect_ArmTarget(&sel, 0x00da0004);
    TEST_ASSERT(sel.target.active, "the spell is armed");
    TEST_ASSERT(!sel.obj.active, "arming the spell does not arm an item");
    TEST_ASSERT(sel.target.component_id == 0x00da0004, "component kept");
    TEST_ASSERT(sel.target.mask == 0, "a fresh arm has mask 0");
    TEST_ASSERT(sel.target.op[0] == '\0', "a fresh arm has no prompt");

    UITargetSelect_SetTargetMask(&sel, 0x2);
    TEST_ASSERT(sel.target.mask == 0x2, "mask set");

    UITargetSelect_ClearTarget(&sel);
    TEST_ASSERT(!UITargetSelect_Armed(&sel), "nothing armed after the clear");
    TEST_ASSERT(sel.target.component_id == 0x00da0004, "the clear leaves the component");
    TEST_ASSERT(sel.target.mask == 0x2, "the clear leaves the mask");

    UITargetSelect_ArmTarget(&sel, 7);
    TEST_ASSERT(sel.target.mask == 0 && sel.target.op[0] == '\0', "a re-arm starts from mask 0, no prompt");
}

static void
test_switching_kinds(void)
{
    struct UITargetSelect sel;
    UITargetSelect_Reset(&sel);
    UITargetSelect_ArmObj(&sel, 1511, 3, 10, "Logs");
    UITargetSelect_ClearObj(&sel);
    UITargetSelect_ArmTarget(&sel, 20);
    TEST_ASSERT(sel.target.active && !sel.obj.active, "item cleared, spell armed");
    UITargetSelect_ClearTarget(&sel);
    UITargetSelect_ArmObj(&sel, 1512, 4, 10, "Oak logs");
    TEST_ASSERT(sel.obj.active && !sel.target.active, "spell cleared, item armed");
}

static void
test_classic_prompt(void)
{
    struct UITargetSelect sel;
    UITargetSelect_Reset(&sel);
    UITargetSelect_ArmTarget(&sel, 1);

    UITargetSelect_SetTargetOpClassic(&sel, "Cast on", "Wind Strike");
    TEST_ASSERT(strcmp(sel.target.op, "Cast Wind Strike on") == 0, "verb split at its first space");

    UITargetSelect_SetTargetOpClassic(&sel, "Cast on the", "Bind");
    TEST_ASSERT(strcmp(sel.target.op, "Cast Bind on the") == 0, "only the first space splits");

    UITargetSelect_SetTargetOpClassic(&sel, "Use", "Charge");
    TEST_ASSERT(strcmp(sel.target.op, "Use Charge") == 0, "a verb with no space goes in front");

    UITargetSelect_SetTargetOpClassic(
        &sel, "Abcdefghijklmnopqrstuvwxyz on", "0123456789012345678901234");
    TEST_ASSERT(
        strcmp(sel.target.op, "Abcdefghijklmnopqrst 01234567890123456789 on") == 0,
        "prefix and base are each cut at 20");

    UITargetSelect_SetTargetOpClassic(
        &sel,
        "VerbVerbVerbVerbVerbVerbVerbVerbVerb",
        "BaseBaseBaseBaseBaseBaseBaseBaseBase");
    TEST_ASSERT(strlen(sel.target.op) == 31 + 1 + 31, "no-space parts are each cut at 31");
}

static void
test_if3_prompt(void)
{
    struct UITargetSelect sel;
    UITargetSelect_Reset(&sel);
    UITargetSelect_ArmTarget(&sel, 1);

    UITargetSelect_SetTargetOpIf3(&sel, "Cast", "<col=00ff00>Wind Strike</col>");
    TEST_ASSERT(
        strcmp(sel.target.op, "Cast <col=00ff00>Wind Strike</col> ->") == 0, "verb, op base, arrow");

    UITargetSelect_SetTargetOpIf3(&sel, "Edit-navigator", "");
    TEST_ASSERT(strcmp(sel.target.op, "Edit-navigator ->") == 0, "an empty op base is the verb alone");

    UITargetSelect_SetTargetOpIf3(
        &sel, "VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV", "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB");
    TEST_ASSERT(strlen(sel.target.op) == 29 + 1 + 29 + 3, "verb and op base are each cut at 29");
}

int
main(void)
{
    test_reset_is_nothing_armed();
    test_arm_and_clear_obj();
    test_obj_name_is_cut_to_fit();
    test_arm_and_clear_target();
    test_switching_kinds();
    test_classic_prompt();
    test_if3_prompt();
    if( g_failures )
    {
        printf("target_select_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("target_select_test: ok\n");
    return 0;
}
