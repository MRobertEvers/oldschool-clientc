/*
 * The two All Settings pickers the client builds itself.
 *
 * Every rule below is invisible when it breaks. A picker that commits the
 * wrong encoding writes a colour one off the one you chose, and the row draws
 * it back without complaint. A picker that drains the chrome's activation
 * latch instead of peeking at it eats a click meant for the panel next door --
 * which is a bug this codebase has already had once, where the loc editor
 * swallowed the map editor's and a dropdown showed a new value while nothing
 * changed. A picker that asks whether its COMPONENT is still in the tree
 * instead of its GROUP never closes, because a swatch is a dynamic child and
 * the id it carries names a container that outlives the row.
 *
 * So the cases are those rules, one at a time, against a real chrome instance
 * and a real tree.
 *
 *   make -C src test-settings-pickers
 */
#include "ui/settings_pickers.h"

#include "ui/uitree.h"
#include "ui/uitree_debug_overlay.h"
#include "ui/uitree_layout.h"

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

/* What the commit callback saw. The pickers know a varp id and an integer and
 * nothing else, so this is the whole of their output. */
struct CommitLog
{
    int count;
    int varp_id;
    int value;
};

static void
commit_log(void* userdata, int varp_id, int value)
{
    struct CommitLog* log = userdata;

    log->count++;
    log->varp_id = varp_id;
    log->value = value;
}

/* A row's control lives in an interface GROUP, and the component id carries
 * that group in its high half. That is the id a request hands over and the id
 * the close test reads, so the fixture has to build one properly.
 *
 * ROW_SIBLING is the rest of All Settings: another component of the same
 * interface, so that the group can outlive the row's own component. That is
 * the only state in which the two ways of asking "is my row still there"
 * disagree, and it is the state the close rule exists for. */
#define ROW_GROUP 134
#define ROW_COMPONENT ((ROW_GROUP << 16) | 7)
#define ROW_SIBLING ((ROW_GROUP << 16) | 9)

static void
push_component(struct UITree* tree, int component_id)
{
    struct UITreeNodeSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = component_id;
    spec.width = 40;
    spec.height = 20;
    UITree_Push(tree, -1, &spec);
}

static struct UITree*
tree_with_the_row(void)
{
    struct UITree* tree = UITree_New(4);

    push_component(tree, ROW_COMPONENT);
    push_component(tree, ROW_SIBLING);
    UITree_LayoutResolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    return tree;
}

/* All Settings rebuilds its rows -- a search filter, a scroll -- and the
 * component the op was dispatched on goes with them while the interface stays
 * open. This is that, and nothing else. */
static struct UITree*
tree_with_the_row_rebuilt(void)
{
    struct UITree* tree = UITree_New(4);

    push_component(tree, ROW_SIBLING);
    UITree_LayoutResolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    return tree;
}

static struct RS_CS2SettingsColourRequest
colour_request(int varp_id)
{
    struct RS_CS2SettingsColourRequest request;

    memset(&request, 0, sizeof(request));
    request.setting_id = 12;
    request.varp_id = varp_id;
    request.colour = 0x336699;
    request.default_colour = 0x1A2B3C;
    request.component_id = ROW_COMPONENT;
    snprintf(request.label, sizeof(request.label), "%s", "Tile highlight colour");
    return request;
}

static struct RS_CS2SettingsNumberRequest
number_request(int varp_id)
{
    struct RS_CS2SettingsNumberRequest request;

    memset(&request, 0, sizeof(request));
    request.setting_id = 34;
    request.varp_id = varp_id;
    request.value = 20000;
    request.component_id = ROW_COMPONENT;
    snprintf(request.label, sizeof(request.label), "%s", "Price tier 3");
    return request;
}

/* ------------------------------------------------------------------ */

static void
test_a_row_with_no_varp_opens_nothing(void)
{
    struct ToriRSChrome chrome;
    struct UISettingsPickers pickers;
    struct UITree* tree = tree_with_the_row();

    printf("TEST: a row with nowhere to store its answer\n");

    ToriRSChrome_Init(&chrome);
    UISettingsPickers_Init(&pickers, &chrome);

    /*
     * The read hub never found where this row keeps its colour. Opening a
     * picker anyway would be a control the player drags while every move is
     * thrown away -- worse than the inert swatch it replaced, because that at
     * least did not pretend.
     */
    {
        struct RS_CS2SettingsColourRequest request = colour_request(-1);
        UISettingsPickers_OpenColour(&pickers, &chrome, tree, &request);
        CHECK(!UISettingsPickers_ColourVisible(&pickers), "a varp-less colour row opened a picker");
    }
    {
        struct RS_CS2SettingsNumberRequest request = number_request(-1);
        UISettingsPickers_OpenNumber(&pickers, &chrome, tree, &request);
        CHECK(!UISettingsPickers_NumberVisible(&pickers), "a varp-less number row opened an entry");
    }

    UITree_Free(tree);
}

static void
test_a_colour_commits_plus_one(void)
{
    struct ToriRSChrome chrome;
    struct UISettingsPickers pickers;
    struct UITree* tree = tree_with_the_row();
    struct RS_CS2SettingsColourRequest request = colour_request(2345);
    struct CommitLog log = { 0, 0, 0 };

    printf("TEST: a colour commits as value + 1\n");

    ToriRSChrome_Init(&chrome);
    UISettingsPickers_Init(&pickers, &chrome);
    UISettingsPickers_OpenColour(&pickers, &chrome, tree, &request);
    CHECK(UISettingsPickers_ColourVisible(&pickers), "the picker did not open");

    /*
     * Seeded from the row's CURRENT colour, not its default. The picker opens
     * on what the swatch is showing -- that is what makes "nudge it a shade
     * darker" possible -- and opening on the default would silently discard
     * whatever the player had already chosen the moment they touched it.
     *
     * Through NearestRgb rather than the reference quantiser: this value is
     * read back and re-shown every time the row is opened, and the reference
     * round trip moves nearly every entry by a shade each pass.
     */
    CHECK(
        ToriRSChrome_ColorPickValue(&chrome, pickers.colour_pick) ==
            ToriRSChrome_Hsl16NearestRgb(0x336699),
        "the picker did not open on the row's current colour");

    /*
     * `colour + 1`, which is what the row reads back with `calc(%var - 1)`.
     * The offset is what makes a varp of 0 mean "never chosen" rather than
     * "black" -- drop it and every row the player has not touched reads as
     * deliberately set to the darkest thing there is.
     */
    chrome.activated = pickers.colour_pick;
    (void)UISettingsPickers_ColourTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(log.count == 1, "the pick did not commit once, it committed %d times", log.count);
    CHECK(log.varp_id == 2345, "the commit went to varp %d, not the row's", log.varp_id);
    {
        uint32_t const shown = ToriRSChrome_Hsl16ToRgb(
            ToriRSChrome_ColorPickValue(&chrome, pickers.colour_pick));
        CHECK(
            log.value == (int)(shown & 0xFFFFFFu) + 1,
            "committed 0x%06X for a picker showing 0x%06X", (unsigned)log.value, shown);
        CHECK(log.value != (int)(shown & 0xFFFFFFu), "the +1 offset is gone");
    }

    UITree_Free(tree);
}

static void
test_default_commits_verbatim(void)
{
    struct ToriRSChrome chrome;
    struct UISettingsPickers pickers;
    struct UITree* tree = tree_with_the_row();
    struct RS_CS2SettingsColourRequest request = colour_request(2345);
    struct CommitLog log = { 0, 0, 0 };

    printf("TEST: Default restores the colour the cache authored\n");

    ToriRSChrome_Init(&chrome);
    UISettingsPickers_Init(&pickers, &chrome);
    UISettingsPickers_OpenColour(&pickers, &chrome, tree, &request);

    /*
     * The default is committed EXACTLY, not as the nearest colour the picker's
     * axes can hold. `default_colour` is a colour the cache authored and the
     * row draws its swatch in before anyone picks; restoring an approximation
     * of it would mean "Default" never quite got back to where the row
     * started, and a row reset twice would drift.
     */
    chrome.activated = pickers.colour_default_button;
    (void)UISettingsPickers_ColourTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(log.count == 1, "Default did not commit once");
    CHECK(
        log.value == 0x1A2B3C + 1,
        "Default committed 0x%06X, not the authored 0x1A2B3C + 1", (unsigned)log.value);

    /*
     * And the picker shows the NEAREST entry to it, which is a different
     * number. That is not a mismatch: the axes cannot hold the authored colour,
     * and what the picker shows is honestly what the next pick would produce.
     * This case exists so that a rewrite "fixing" the difference has to decide
     * which half it is breaking.
     */
    {
        uint32_t const shown = ToriRSChrome_Hsl16ToRgb(
            ToriRSChrome_ColorPickValue(&chrome, pickers.colour_pick));
        uint32_t const nearest = ToriRSChrome_Hsl16ToRgb(ToriRSChrome_Hsl16NearestRgb(0x1A2B3C));
        CHECK(shown == nearest, "the picker is not showing the nearest entry to the default");
    }

    UITree_Free(tree);
}

static void
test_a_number_commits_plain(void)
{
    struct ToriRSChrome chrome;
    struct UISettingsPickers pickers;
    struct UITree* tree = tree_with_the_row();
    struct RS_CS2SettingsNumberRequest request = number_request(678);
    struct CommitLog log = { 0, 0, 0 };

    printf("TEST: a number commits plain, and clamps what was typed\n");

    ToriRSChrome_Init(&chrome);
    UISettingsPickers_Init(&pickers, &chrome);
    UISettingsPickers_OpenNumber(&pickers, &chrome, tree, &request);
    CHECK(UISettingsPickers_NumberVisible(&pickers), "the entry did not open");

    /*
     * PLAIN, unlike a colour's `value + 1`. Zero is a real answer for every one
     * of these rows -- a price threshold of 0 colours everything at that tier,
     * a line limit of 0 hides the overlay -- so there is no never-chosen
     * sentinel to make room for, and an offset here would put every threshold
     * one gp out.
     */
    ToriRSChrome_SetText(&chrome, pickers.number_input, "20000");
    chrome.activated = pickers.number_input;
    (void)UISettingsPickers_NumberTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(log.count == 1 && log.varp_id == 678, "Enter did not commit to the row's varp");
    CHECK(log.value == 20000, "committed %d for a typed 20000", log.value);

    /* Enter leaves the box up, so a mistyped threshold can be corrected
     * without clicking the row again. */
    CHECK(UISettingsPickers_NumberVisible(&pickers), "Enter closed the box");

    /* An emptied field commits zero rather than being ignored: "off" is a
     * thing these rows can be set to, and the row prints its own word for it. */
    ToriRSChrome_SetText(&chrome, pickers.number_input, "");
    chrome.activated = pickers.number_input;
    (void)UISettingsPickers_NumberTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(log.count == 2 && log.value == 0, "an emptied field did not commit zero");

    /*
     * Clamped rather than asserted, on both ends. This is a number a person
     * typed: "2000000000000" is a typo, and a negative threshold would colour
     * every pile at that tier for ever.
     */
    ToriRSChrome_SetText(&chrome, pickers.number_input, "2000000000000");
    chrome.activated = pickers.number_input;
    (void)UISettingsPickers_NumberTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(log.value == 2147483647, "an overflowing number committed %d", log.value);

    ToriRSChrome_SetText(&chrome, pickers.number_input, "-40");
    chrome.activated = pickers.number_input;
    (void)UISettingsPickers_NumberTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(log.value == 0, "a negative number committed %d", log.value);

    /* Done commits what is in the box and then closes -- both, so a value
     * typed and confirmed with the button is not lost. */
    ToriRSChrome_SetText(&chrome, pickers.number_input, "55");
    chrome.activated = pickers.number_close_button;
    (void)UISettingsPickers_NumberTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(log.value == 55, "Done did not commit the typed value, it committed %d", log.value);
    CHECK(!UISettingsPickers_NumberVisible(&pickers), "Done did not close the box");

    UITree_Free(tree);
}

static void
test_the_activation_is_peeked(void)
{
    struct ToriRSChrome chrome;
    struct UISettingsPickers pickers;
    struct UITree* tree = tree_with_the_row();
    struct RS_CS2SettingsColourRequest request = colour_request(2345);
    struct CommitLog log = { 0, 0, 0 };
    int const somebody_elses = 4242;

    printf("TEST: an activation that is not ours is left where it is\n");

    ToriRSChrome_Init(&chrome);
    UISettingsPickers_Init(&pickers, &chrome);
    UISettingsPickers_OpenColour(&pickers, &chrome, tree, &request);

    /*
     * The chrome's activation latch is SHARED with everything else drawing
     * into the same instance -- the frame-time panel, the loc editor, the map
     * editor. Draining it unconditionally is how the loc editor once swallowed
     * the map editor's clicks: a dropdown that showed the new value while
     * nothing changed, because the widget that owned the click never saw it.
     *
     * So the pickers PEEK, and take only what is theirs. This is the case that
     * says so, and a tick that ends with `TakeActivated` outside its own
     * branches fails it.
     */
    chrome.activated = somebody_elses;
    (void)UISettingsPickers_ColourTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(chrome.activated == somebody_elses, "the colour tick ate somebody else's activation");
    CHECK(log.count == 0, "the colour tick committed on somebody else's activation");

    UISettingsPickers_OpenNumber(&pickers, &chrome, tree, &(struct RS_CS2SettingsNumberRequest){
        .setting_id = 34, .varp_id = 678, .value = 1, .component_id = ROW_COMPONENT });
    chrome.activated = somebody_elses;
    (void)UISettingsPickers_NumberTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(chrome.activated == somebody_elses, "the number tick ate somebody else's activation");
    CHECK(log.count == 0, "the number tick committed on somebody else's activation");

    /* And our own IS taken, or the next tick would commit it again. */
    chrome.activated = pickers.colour_pick;
    (void)UISettingsPickers_ColourTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(chrome.activated != pickers.colour_pick, "the picker did not take its own activation");
    CHECK(log.count == 1, "the picker did not commit its own activation");

    UITree_Free(tree);
}

static void
test_a_picker_follows_its_row_out(void)
{
    struct ToriRSChrome chrome;
    struct UISettingsPickers pickers;
    struct UITree* tree = tree_with_the_row();
    struct RS_CS2SettingsColourRequest request = colour_request(2345);
    struct CommitLog log = { 0, 0, 0 };

    printf("TEST: a picker closes when All Settings does\n");

    ToriRSChrome_Init(&chrome);
    UISettingsPickers_Init(&pickers, &chrome);
    UISettingsPickers_OpenColour(&pickers, &chrome, tree, &request);

    /* While the row's interface is up, the picker stays. */
    (void)UISettingsPickers_ColourTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(UISettingsPickers_ColourVisible(&pickers), "the picker closed while its row was open");

    /*
     * The question is asked of the GROUP -- the interface the component id
     * names in its high half -- and not of the component, and this is the case
     * where those two differ.
     *
     * A colour row's swatch is a DYNAMIC child, and the id the request carries
     * names the container it was created on. All Settings rebuilds its rows
     * whenever the panel is filtered or scrolled, so that id can stop naming
     * anything while the interface is plainly still open. Asked about the
     * COMPONENT, the picker vanishes out from under a player mid-drag for no
     * reason they can see.
     */
    {
        struct UITree* rebuilt = tree_with_the_row_rebuilt();
        CHECK(
            UITree_FindByComponentId(rebuilt, ROW_COMPONENT) < 0,
            "the fixture still holds the row's own component");
        CHECK(UITree_GroupPresent(rebuilt, ROW_GROUP), "the fixture lost the whole interface");
        (void)UISettingsPickers_ColourTick(&pickers, &chrome, rebuilt, commit_log, &log);
        CHECK(
            UISettingsPickers_ColourVisible(&pickers),
            "a rebuilt row closed the picker while All Settings was still open");
        UITree_Free(rebuilt);
    }

    /*
     * And when the interface itself goes, the picker goes with it. All Settings
     * is opened and closed by the interface stack, and a picker still floating
     * over the game after the panel it belongs to has gone has nothing to point
     * at.
     */
    UITree_Clear(tree);
    (void)UISettingsPickers_ColourTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(!UISettingsPickers_ColourVisible(&pickers), "the picker outlived its interface");
    CHECK(log.count == 0, "closing the interface committed something");

    UITree_Free(tree);
}

static void
test_a_number_entry_follows_its_row_out(void)
{
    struct ToriRSChrome chrome;
    struct UISettingsPickers pickers;
    struct UITree* tree = tree_with_the_row();
    struct RS_CS2SettingsNumberRequest request = number_request(678);
    struct CommitLog log = { 0, 0, 0 };

    printf("TEST: the number entry closes when All Settings does\n");

    ToriRSChrome_Init(&chrome);
    UISettingsPickers_Init(&pickers, &chrome);
    UISettingsPickers_OpenNumber(&pickers, &chrome, tree, &request);

    /* Same pair as the colour picker's, because it is the same trap and the
     * two blocks are written separately. */
    {
        struct UITree* rebuilt = tree_with_the_row_rebuilt();
        (void)UISettingsPickers_NumberTick(&pickers, &chrome, rebuilt, commit_log, &log);
        CHECK(
            UISettingsPickers_NumberVisible(&pickers),
            "a rebuilt row closed the entry while All Settings was still open");
        UITree_Free(rebuilt);
    }

    UITree_Clear(tree);
    (void)UISettingsPickers_NumberTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(!UISettingsPickers_NumberVisible(&pickers), "the entry outlived its interface");
    CHECK(log.count == 0, "closing the interface committed a typed value");

    UITree_Free(tree);
}

static void
test_the_panels_own_close_is_reconciled(void)
{
    struct ToriRSChrome chrome;
    struct UISettingsPickers pickers;
    struct UITree* tree = tree_with_the_row();
    struct RS_CS2SettingsColourRequest request = colour_request(2345);
    struct CommitLog log = { 0, 0, 0 };

    printf("TEST: the panel's own Close button is noticed\n");

    ToriRSChrome_Init(&chrome);
    UISettingsPickers_Init(&pickers, &chrome);
    UISettingsPickers_OpenColour(&pickers, &chrome, tree, &request);

    /*
     * The panel carries a close affordance of its own and hides itself without
     * telling anyone. The pickers keep a flag beside it, and left unreconciled
     * the next click on the same swatch "reopens" something this side still
     * believes is open.
     */
    ToriRSChrome_PanelSetVisible(&chrome, pickers.colour_panel, 0);
    (void)UISettingsPickers_ColourTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(!UISettingsPickers_ColourVisible(&pickers), "the flag did not follow the panel down");

    /* And the row can then be opened again, from the top. */
    UISettingsPickers_OpenColour(&pickers, &chrome, tree, &request);
    CHECK(UISettingsPickers_ColourVisible(&pickers), "the row could not be reopened");

    UITree_Free(tree);
}

static void
test_the_two_pickers_are_independent(void)
{
    struct ToriRSChrome chrome;
    struct UISettingsPickers pickers;
    struct UITree* tree = tree_with_the_row();
    struct RS_CS2SettingsColourRequest colour = colour_request(2345);
    struct RS_CS2SettingsNumberRequest number = number_request(678);
    struct CommitLog log = { 0, 0, 0 };

    printf("TEST: the colour picker and the number entry do not share a panel\n");

    ToriRSChrome_Init(&chrome);
    UISettingsPickers_Init(&pickers, &chrome);
    CHECK(pickers.colour_panel != pickers.number_panel, "both pickers took the same panel");

    UISettingsPickers_OpenColour(&pickers, &chrome, tree, &colour);
    UISettingsPickers_OpenNumber(&pickers, &chrome, tree, &number);
    CHECK(
        UISettingsPickers_ColourVisible(&pickers) && UISettingsPickers_NumberVisible(&pickers),
        "opening one picker closed the other");

    /* A colour commit does not touch the number row's varp, and the number
     * tick does not take the colour picker's activation. */
    chrome.activated = pickers.colour_pick;
    (void)UISettingsPickers_NumberTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(log.count == 0, "the number tick committed on the colour picker's activation");
    CHECK(chrome.activated == pickers.colour_pick, "the number tick ate the colour activation");

    (void)UISettingsPickers_ColourTick(&pickers, &chrome, tree, commit_log, &log);
    CHECK(log.count == 1 && log.varp_id == 2345, "the colour commit went to the wrong varp");

    UITree_Free(tree);
}

int
main(void)
{
    test_a_row_with_no_varp_opens_nothing();
    test_a_colour_commits_plus_one();
    test_default_commits_verbatim();
    test_a_number_commits_plain();
    test_the_activation_is_peeked();
    test_a_picker_follows_its_row_out();
    test_a_number_entry_follows_its_row_out();
    test_the_panels_own_close_is_reconciled();
    test_the_two_pickers_are_independent();

    if( g_failures )
    {
        printf("settings_pickers_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("settings_pickers_test: OK\n");
    return 0;
}
