#include "test_harness.h"

#include <stdlib.h>

/*
 * IF3 text-entry fields (component type 12).
 *
 * The three things the rest of the client depends on and that nothing else in
 * this suite covers: cc_create's type-12 arm produces a node the hit test will
 * hand a click to, the focus survives being asked about after the field is
 * gone, and a focused field draws its caret -- including while it is empty,
 * which is the state it is in the instant after the click that focused it.
 *
 * The EDITING (measuring a candidate line, running the field's hooks) is
 * RS_CS2_InputKey's, in the game layer, because only that side holds a font
 * provider; this file is the tree half.
 */
void
test_input_field(void)
{
    struct UITree* tree = UITree_New(16);
    struct UITreeHost host;
    struct TestHostState host_state;
    int32_t root;
    int32_t panel;
    int32_t field;
    int field_id;

    printf("TEST: IF3 text-entry field (type 12)\n");
    UITree_TestHostInit(&host, &host_state);

    root = UITree_TestPushXy(tree, -1, UIELEM_RS_LAYER, 0x00010000, 0, 0, 200, 100);
    panel = UITree_TestPushXy(tree, root, UIELEM_RS_LAYER, 0x00010001, 0, 0, 200, 40);
    field = UITree_CcCreate(tree, panel, 0x00010001, 12, 2);
    TEST_ASSERT(field >= 0, "cc_create(type 12) makes a node");
    field_id = tree->components[field].component_id;
    tree->components[field].position.x = 0;
    tree->components[field].position.y = 0;
    tree->components[field].position.width = 200;
    tree->components[field].position.height = 20;
    tree->components[field].u.rs_text.font_id = 1;
    UITree_LayoutInvalidateBoxes(tree);
    UITree_TestResolve(tree);

    TEST_ASSERT(
        tree->components[field].type == UIELEM_RS_TEXT,
        "a type-12 field draws as text");
    TEST_ASSERT(
        UITree_IsInputNode(&tree->components[field]), "a type-12 field is an input node");
    /* The whole of "clicking the text box did nothing": with no op, no click
     * mask and no hook, every other rule in ComponentIsPassThrough calls the
     * field decoration and the click falls through to the panel behind it. */
    TEST_ASSERT(
        !UITree_ComponentIsPassThrough(&tree->components[field], &host),
        "an input node is a click target on the strength of being one");
    TEST_ASSERT(
        UITree_InputHitTest(tree, &host, 10, 10) == field_id,
        "the hit test finds the field under the point");

    TEST_ASSERT(UITree_InputFocusId(tree) < 0, "nothing is focused to begin with");
    TEST_ASSERT(
        UITree_InputSetFocusId(tree, field_id) < 0, "taking the caret loses nobody's");
    TEST_ASSERT(UITree_InputFocusId(tree) == field_id, "the field holds the caret");

    /* An empty focused field still emits, because the caret is its content. */
    {
        struct UITreeEmitDesc desc;
        memset(&desc, 0, sizeof(desc));
        TEST_ASSERT(
            UITree_EmitFill(tree, &host, &tree->components[field], field, -1, &desc),
            "an empty focused field still draws");
        TEST_ASSERT(strcmp(desc.text_formatted, "|") == 0, "an empty field draws just a caret");
    }

    UITree_SetTextAt(tree, field, "ab");
    tree->components[field].u.rs_text.caret = 1;
    {
        struct UITreeEmitDesc desc;
        memset(&desc, 0, sizeof(desc));
        TEST_ASSERT(
            UITree_EmitFill(tree, &host, &tree->components[field], field, -1, &desc),
            "a focused field with text draws");
        TEST_ASSERT(
            strcmp(desc.text_formatted, "a|b") == 0, "the caret draws at its own position");
    }

    /* Blurred, the same field is an ordinary text node again. */
    TEST_ASSERT(
        UITree_InputSetFocusId(tree, -1) == field_id, "dropping the caret names who lost it");
    {
        struct UITreeEmitDesc desc;
        memset(&desc, 0, sizeof(desc));
        TEST_ASSERT(
            UITree_EmitFill(tree, &host, &tree->components[field], field, -1, &desc),
            "an unfocused field with text still draws");
        TEST_ASSERT(desc.text_formatted[0] == '\0', "an unfocused field draws no caret");
    }

    /*
     * The rebuild case, which is why the focus is an id and not an index:
     * `~torirs_hiscores_layout_refresh` cc_deleteall's the search box whenever
     * the panel resizes, and the slot is handed to the next component created.
     */
    UITree_InputSetFocusId(tree, field_id);
    TEST_ASSERT(UITree_InputFocusId(tree) == field_id, "focused again");
    UITree_CcDeleteAll(tree, panel);
    TEST_ASSERT(
        UITree_InputFocusId(tree) < 0, "a deleted field holds no caret");

    UITree_Free(tree);
}
