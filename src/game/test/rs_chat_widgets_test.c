/*
 * The rev-230 widget chatbox.
 *
 * What is worth testing here is the two things that have no symptom other than
 * "the chatbox looks wrong", and which a screenshot cannot distinguish from a
 * rendering problem:
 *
 *   - the ORDER. The message ring is newest-first and the display is
 *     oldest-first, so the layout walks it backwards. Getting that wrong prints
 *     a perfectly formatted chatbox reading bottom-to-top, which looks like the
 *     server sent the messages in the wrong order.
 *   - the ANCHOR. A short chat sits at the bottom of the box, against the input
 *     line; a long one scrolls. Both are "text in the right font in the right
 *     colour in the right box", and only the y coordinates tell them apart.
 *
 * The tree here is built by hand rather than from the cache: the point is the
 * arithmetic over an interface of a stated shape, and a cache-backed test would
 * fail for cache reasons.
 */

#include "game/rs_chat.h"
#include "game/rs_chat_widgets.h"
#include "ui/test/test_harness.h"
#include "ui/uitree.h"
#include "ui/uitree_layout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_chat_failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "  FAIL ");                                                            \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fprintf(stderr, "\n       (%s at %s:%d)\n", #cond, __FILE__, __LINE__);                \
            g_chat_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

enum
{
    TEST_IFACE = 162,
    TEST_MESSAGES_CHILD = 58,
    TEST_FIRST_LINE = 59,
    TEST_LINE_COUNT = 24,
    TEST_INPUT_CHILD = 57,
    TEST_LINE_HEIGHT = 14,
    /* Eight lines of room, so "fits" and "overflows" are both reachable with a
     * handful of messages. */
    TEST_VIEWPORT_H = 8 * TEST_LINE_HEIGHT,
};

static int
uid_of(int child)
{
    return (TEST_IFACE << 16) | child;
}

/* A scrolling layer with TEST_LINE_COUNT text children and an input line —
 * interface 162's shape, at a size a test can reason about. */
static void
build_tree(struct UITree** tree)
{
    int32_t root;
    int32_t scroll;

    *tree = UITree_New(64);
    root = UITree_TestPushXy(*tree, -1, UIELEM_RS_LAYER, uid_of(0), 0, 0, 500, 200);
    scroll = UITree_TestPushXy(*tree, root, UIELEM_RS_LAYER, uid_of(TEST_MESSAGES_CHILD), 0, 0, 489,
                               TEST_VIEWPORT_H);
    for( int i = 0; i < TEST_LINE_COUNT; i++ )
    {
        UITree_TestPushXy(*tree, scroll, UIELEM_RS_TEXT, uid_of(TEST_FIRST_LINE + i), 3, 1, 486,
                          TEST_LINE_HEIGHT);
    }
    UITree_TestPushXy(*tree, root, UIELEM_RS_TEXT, uid_of(TEST_INPUT_CHILD), 3, 180, 486, 16);

    UITree_LayoutResolve(*tree, 0, 0, 800, 600);
}

static struct UITreeComponent const*
node_for(
    struct UITree const* tree,
    int child)
{
    int32_t idx = UITree_FindByComponentId((struct UITree*)tree, uid_of(child));

    return idx >= 0 ? &tree->components[idx] : NULL;
}

static char const*
text_of(
    struct UITree const* tree,
    int child)
{
    struct UITreeComponent const* node = node_for(tree, child);

    return node && node->u.rs_text.text ? node->u.rs_text.text : "";
}

static int
y_of(
    struct UITree const* tree,
    int child)
{
    struct UITreeComponent const* node = node_for(tree, child);

    return node ? node->position.y : -1;
}

int
main(void)
{
    struct RS_ChatWidgetLayout layout = {
        .interface_id = TEST_IFACE,
        .messages_child = TEST_MESSAGES_CHILD,
        .first_line = TEST_FIRST_LINE,
        .line_count = TEST_LINE_COUNT,
        .input_child = TEST_INPUT_CHILD,
        .line_height = TEST_LINE_HEIGHT,
    };
    struct RS_ChatFilters filters;
    struct UITree* tree = NULL;
    struct RS_Chat chat;

    memset(&filters, 0, sizeof(filters));

    fprintf(stderr, "chat widgets: composition\n");
    {
        struct RS_ChatMessage message;
        char out[512];

        memset(&message, 0, sizeof(message));
        message.type = RS_CHAT_TYPE_GAME;
        snprintf(message.text, sizeof(message.text), "Welcome to Lumbridge.");
        RS_ChatWidgets_ComposeLine(&message, out, (int)sizeof(out));
        CHECK(strstr(out, "Welcome to Lumbridge.") != NULL, "a game line carries its text: %s",
              out);
        CHECK(strstr(out, "<col=") != NULL, "and a colour tag: %s", out);

        memset(&message, 0, sizeof(message));
        message.type = RS_CHAT_TYPE_PUBLIC;
        snprintf(message.sender, sizeof(message.sender), "Zezima");
        snprintf(message.text, sizeof(message.text), "hi");
        RS_ChatWidgets_ComposeLine(&message, out, (int)sizeof(out));
        /* Sender and text in one string, with the colon — the 254 path draws
         * them as two independently positioned spans, and this is what that
         * becomes when the line is a single component. */
        CHECK(strstr(out, "Zezima:") != NULL && strstr(out, "hi") != NULL,
              "public chat is `sender: text`, got %s", out);
    }

    fprintf(stderr, "chat widgets: order and anchoring\n");
    {
        build_tree(&tree);
        RS_Chat_Init(&chat, "tester");
        /* Sent oldest first; the ring stores newest first. */
        RS_Chat_AddMessage(&chat, RS_CHAT_TYPE_GAME, "", "first");
        RS_Chat_AddMessage(&chat, RS_CHAT_TYPE_GAME, "", "second");
        RS_Chat_AddMessage(&chat, RS_CHAT_TYPE_GAME, "", "third");

        int written = RS_ChatWidgets_Apply(tree, &chat, &filters, &layout);

        CHECK(written == 3, "three messages fill three lines, got %d", written);
        CHECK(strstr(text_of(tree, TEST_FIRST_LINE + 0), "first") != NULL,
              "the OLDEST message is the top line, got `%s`",
              text_of(tree, TEST_FIRST_LINE + 0));
        CHECK(strstr(text_of(tree, TEST_FIRST_LINE + 2), "third") != NULL,
              "and the newest is the bottom one, got `%s`",
              text_of(tree, TEST_FIRST_LINE + 2));

        /* Bottom-anchored: three lines in an eight-line box start five lines
         * down, not at the top. */
        CHECK(y_of(tree, TEST_FIRST_LINE + 0) == TEST_VIEWPORT_H - 3 * TEST_LINE_HEIGHT,
              "a short chat sits against the input line, top line y=%d",
              y_of(tree, TEST_FIRST_LINE + 0));
        CHECK(y_of(tree, TEST_FIRST_LINE + 1) - y_of(tree, TEST_FIRST_LINE + 0) ==
                  TEST_LINE_HEIGHT,
              "lines are one line-height apart, got %d",
              y_of(tree, TEST_FIRST_LINE + 1) - y_of(tree, TEST_FIRST_LINE + 0));

        /* Unused lines are hidden, not blanked — they carry a right-click op
         * and would otherwise eat clicks over the whole chatbox. */
        CHECK(node_for(tree, TEST_FIRST_LINE + 3) &&
                  node_for(tree, TEST_FIRST_LINE + 3)->behavior.hide,
              "unused lines are hidden");
        CHECK(node_for(tree, TEST_FIRST_LINE + 0) &&
                  !node_for(tree, TEST_FIRST_LINE + 0)->behavior.hide,
              "and used ones are not");

        CHECK(strstr(text_of(tree, TEST_INPUT_CHILD), "tester") != NULL,
              "the input line names the player, got `%s`", text_of(tree, TEST_INPUT_CHILD));
        CHECK(strstr(text_of(tree, TEST_INPUT_CHILD), "*") != NULL,
              "and carries the caret, got `%s`", text_of(tree, TEST_INPUT_CHILD));

        UITree_Free(tree);
    }

    fprintf(stderr, "chat widgets: overflow scrolls instead of anchoring\n");
    {
        build_tree(&tree);
        RS_Chat_Init(&chat, "tester");
        for( int i = 0; i < 12; i++ )
        {
            char text[32];

            snprintf(text, sizeof(text), "line %d", i);
            RS_Chat_AddMessage(&chat, RS_CHAT_TYPE_GAME, "", text);
        }

        int written = RS_ChatWidgets_Apply(tree, &chat, &filters, &layout);
        struct UITreeComponent const* scroll = node_for(tree, TEST_MESSAGES_CHILD);

        CHECK(written == 12, "twelve messages fill twelve lines, got %d", written);
        /* Past the viewport the anchor term is zero: the column starts at the
         * top and the scroll offset is what moves it. */
        CHECK(y_of(tree, TEST_FIRST_LINE + 0) == 0, "an overflowing chat starts at the top, y=%d",
              y_of(tree, TEST_FIRST_LINE + 0));
        CHECK(scroll && scroll->u.rs_layer.scroll_height == 12 * TEST_LINE_HEIGHT,
              "scroll height is the content height, got %d",
              scroll ? scroll->u.rs_layer.scroll_height : -1);
        CHECK(scroll && scroll->scroll_y == 12 * TEST_LINE_HEIGHT - TEST_VIEWPORT_H,
              "and the view sits at the bottom, scroll_y=%d",
              scroll ? scroll->scroll_y : -1);

        UITree_Free(tree);
    }

    if( g_chat_failures )
    {
        fprintf(stderr, "chat widgets: %d failure(s)\n", g_chat_failures);
        return 1;
    }
    fprintf(stderr, "chat widgets: all checks passed\n");
    return 0;
}
