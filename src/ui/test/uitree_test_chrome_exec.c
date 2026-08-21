/*
 * The executor seam: what Sync emits, and what an intent does when it lands.
 *
 * The properties worth pinning are the ones a native executor would be broken
 * by and a screenshot would never show:
 *
 *  - a clean frame emits nothing, so a Win32 control is not destroyed and
 *    recreated (losing focus and caret) on a frame where nothing moved;
 *  - a changed field emits ONLY that field;
 *  - removal is announced before the handle can be recycled by an add;
 *  - a closed panel takes its rows with it, without a REMOVE per row;
 *  - an intent lands on the model exactly where a click would, so five
 *    presentations of one panel cannot disagree about what is checked.
 */
#include "test_harness.h"

#include "torirs_chrome_exec.h"
#include "uitree_debug_overlay.h"

static struct ToriRSChrome g_ui;
static struct ToriRSChromeSync g_sync;
static struct ToriRSChromeRecorder g_rec;

/** Bind a fresh recorder to a fresh model and catch it up to the model's state. */
static void
exec_reset(void)
{
    struct ToriRSChromeExec exec;

    ToriRSChrome_Init(&g_ui);
    ToriRSChromeRecorder_Init(&g_rec);
    exec = ToriRSChromeExec_Recorder(&g_rec);
    TEST_ASSERT(ToriRSChromeSync_Init(&g_sync, &exec) == 1, "the recorder comes up");
}

/** Run a sync and forget what it said, so the next one starts from clean. */
static int
exec_settle(void)
{
    int const n = ToriRSChromeSync_Run(&g_sync, &g_ui);
    g_rec.count = 0;
    return n;
}

static void
test_chrome_exec_catchup(void)
{
    int panel;
    int check;
    struct ToriRSChromeCmd const* add;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 12, 20, 160, "Settings");
    check = ToriRSChrome_Checkbox(&g_ui, panel, "enabled", 1);
    ToriRSChrome_Build(&g_ui);

    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) > 0, "the first sync says something");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_SYNC_BEGIN) == 1,
        "a sync is bracketed at the front");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_SYNC_END) == 1,
        "a sync is bracketed at the back");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_PANEL_OPEN) == 1,
        "the panel is opened once");

    add = ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD, check);
    TEST_ASSERT(add != NULL, "the checkbox is added");
    TEST_ASSERT(add->value == TORIDBG_W_CHECKBOX, "the add carries the widget's kind");
    TEST_ASSERT(strcmp(add->label, "enabled") == 0, "the add carries the label, copied");
    TEST_ASSERT(
        ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_CHECKED, check) != NULL,
        "and its initial checked state");
}

/*
 * The property the whole shadow exists for. A frame where nothing moved must
 * emit nothing, or every native control is torn down and rebuilt 50 times a
 * second and no text field can be typed into.
 */
static void
test_chrome_exec_quiet_frame(void)
{
    int panel;
    int input;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 12, 20, 160, "Settings");
    input = ToriRSChrome_TextInput(&g_ui, panel, "colour", "#FFCC00");
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) == 0, "a frame with no change says nothing");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD) == 0,
        "and re-adds nothing");

    /* Setting a value to what it already is is not a change, the same way the
     * chrome's own compare-then-set mutators treat it. */
    ToriRSChrome_SetText(&g_ui, input, "#FFCC00");
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) == 0, "a no-op write is still quiet");

    /* A real change emits exactly one thing. */
    g_rec.count = 0;
    ToriRSChrome_SetText(&g_ui, input, "#00FF00");
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) == 1, "one change is one command");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_TEXT) == 1,
        "and it is the text");
    TEST_ASSERT(
        strcmp(ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_TEXT, input)->text,
               "#00FF00") == 0,
        "carrying the new value");
}

static void
test_chrome_exec_remove(void)
{
    int panel;
    int a;
    int b;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 0, 0, 160, "P");
    a = ToriRSChrome_Label(&g_ui, panel, "a");
    b = ToriRSChrome_Checkbox(&g_ui, panel, "b", 0);
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    ToriRSChrome_WidgetRemove(&g_ui, b);
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_REMOVE) == 1,
        "a removal is announced exactly once");
    TEST_ASSERT(
        ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_REMOVE, b) != NULL,
        "and it names the removed widget");
    /* The panel got shorter by a row, so its box is restated too -- that is a
     * real change, not noise, and an executor sizing a native window needs it. */
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_PANEL_RECT) == 1,
        "the panel it shrank is remeasured");

    /*
     * The ordering that matters: the freed handle is recycled by the very next
     * add, so within one sync the executor must hear REMOVE before ADD for the
     * same number -- otherwise it applies them in the order given and is left
     * with no control at all.
     */
    g_rec.count = 0;
    {
        int const reused = ToriRSChrome_TextInput(&g_ui, panel, "reused", "x");
        int remove_at = -1;
        int add_at = -1;

        TEST_ASSERT(reused == b, "the slot really is recycled (else this proves nothing)");
        ToriRSChrome_WidgetRemove(&g_ui, a);
        ToriRSChrome_Build(&g_ui);
        ToriRSChromeSync_Run(&g_sync, &g_ui);

        for( int i = 0; i < g_rec.count; i++ )
        {
            if( g_rec.cmds[i].kind == TORIRS_CHROME_CMD_WIDGET_REMOVE &&
                g_rec.cmds[i].widget == a )
                remove_at = i;
            if( g_rec.cmds[i].kind == TORIRS_CHROME_CMD_WIDGET_ADD && g_rec.cmds[i].widget == b )
                add_at = i;
        }
        TEST_ASSERT(remove_at >= 0 && add_at >= 0, "both the removal and the add were said");
        TEST_ASSERT(remove_at < add_at, "removals are said before additions");
    }
}

/* A hidden panel takes its rows with it, and says so once rather than per row. */
static void
test_chrome_exec_panel_close(void)
{
    int panel;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 0, 0, 160, "P");
    for( int i = 0; i < 5; i++ )
        ToriRSChrome_Label(&g_ui, panel, "row");
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    ToriRSChrome_PanelSetVisible(&g_ui, panel, 0);
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_PANEL_CLOSE) == 1,
        "the panel closes once");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_REMOVE) == 0,
        "its rows go with it, unannounced");

    /* Reopening restates the whole panel: the executor threw it away. */
    g_rec.count = 0;
    ToriRSChrome_PanelSetVisible(&g_ui, panel, 1);
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_PANEL_OPEN) == 1,
        "reopening opens it again");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD) == 5,
        "and re-adds every row");
}

/* Borrowed lists are restated in full, as copies. */
static void
test_chrome_exec_options(void)
{
    static char const* const first[] = { "alpha", "beta" };
    static char const* const second[] = { "one", "two", "three" };
    int panel;
    int drop;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 0, 0, 200, "P");
    drop = ToriRSChrome_Dropdown(&g_ui, panel, "mode", first, 2, 0);
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);

    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_OPTION) == 2,
        "each option is a command of its own");
    {
        struct ToriRSChromeCmd const* head =
            ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_OPTIONS, drop);
        TEST_ASSERT(head && head->value == 2, "the list is announced with its length");
    }

    g_rec.count = 0;
    ToriRSChrome_DropdownSetOptions(&g_ui, drop, second, 3, 1);
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_OPTION) == 3,
        "a new list is restated in full");
    TEST_ASSERT(
        ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_SELECTED, drop) != NULL,
        "and the selection into it is restated with it");
}

/* Intents land on the model where a click would. */
static void
test_chrome_exec_intents(void)
{
    static char const* const tabs[] = { "One", "Two" };
    int panel;
    int check;
    int input;
    int strip;
    struct ToriRSChromeIntent intent;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 0, 0, 200, "P");
    strip = ToriRSChrome_Tabs(&g_ui, panel, tabs, 2, 0);
    check = ToriRSChrome_Checkbox(&g_ui, panel, "enabled", 0);
    input = ToriRSChrome_TextInput(&g_ui, panel, "colour", "#000000");
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_TOGGLE;
    intent.panel = panel;
    intent.widget = check;
    intent.value = 1;
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);

    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_TEXT;
    intent.panel = panel;
    intent.widget = input;
    strcpy(intent.text, "#FFFFFF");
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);

    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_TAB;
    intent.panel = panel;
    intent.widget = strip;
    intent.value = 1;
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);

    TEST_ASSERT(ToriRSChromeSync_Pump(&g_sync, &g_ui) == 3, "every queued intent applies");
    TEST_ASSERT(ToriRSChrome_Checked(&g_ui, check) == 1, "the toggle landed on the model");
    TEST_ASSERT(strcmp(ToriRSChrome_Text(&g_ui, input), "#FFFFFF") == 0, "the edit landed");
    TEST_ASSERT(ToriRSChrome_PanelActiveTab(&g_ui, panel) == 1, "the tab switch landed");

    /* An activation reaches the same latch the in-canvas click uses, which is
     * what lets one host drain clicks from every presentation at once. */
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_ACTIVATE;
    intent.panel = panel;
    intent.widget = check;
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);
    ToriRSChromeSync_Pump(&g_sync, &g_ui);
    TEST_ASSERT(
        ToriRSChrome_TakeActivated(&g_ui) == check,
        "a remote activation drains through TakeActivated");

    /* Applying the same intent twice is harmless: intents state results, not
     * edits, because no transport under this promises exactly-once. */
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_TOGGLE;
    intent.panel = panel;
    intent.widget = check;
    intent.value = 1;
    ToriRSChromeIntent_Apply(&g_ui, &intent);
    ToriRSChromeIntent_Apply(&g_ui, &intent);
    TEST_ASSERT(ToriRSChrome_Checked(&g_ui, check) == 1, "a duplicated intent is idempotent");
}

/*
 * An executor that cannot come up leaves the sync inert rather than half-bound,
 * so the caller's fallback is a plain "did Init succeed" rather than a state it
 * has to unwind.
 */
static void
test_chrome_exec_refused(void)
{
    struct ToriRSChromeExec exec;
    int panel;

    ToriRSChrome_Init(&g_ui);
    ToriRSChromeRecorder_Init(&g_rec);
    g_rec.refuse = 1;
    exec = ToriRSChromeExec_Recorder(&g_rec);

    TEST_ASSERT(ToriRSChromeSync_Init(&g_sync, &exec) == 0, "a refusing executor reports it");
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 0, 0, 160, "P");
    ToriRSChrome_Label(&g_ui, panel, "row");
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) == 0, "and is never driven");
    TEST_ASSERT(g_rec.count == 0, "nothing reached it");

    /* The buffer executor is the fallback, and it always comes up. */
    {
        struct ToriRSChromeExec buffer = ToriRSChromeExec_Buffer();
        TEST_ASSERT(
            ToriRSChromeSync_Init(&g_sync, &buffer) == 1, "the buffer executor always comes up");
        TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) > 0, "and is driven normally");
    }
}

/*
 * Widgets are announced in ROW order, not handle order.
 *
 * The two diverge as soon as a panel is cleared and rebuilt: the free list
 * hands handles back in removal order, so a rebuilt panel's rows carry handles
 * in an order that has nothing to do with where they sit. A native-widget
 * executor creates its controls in the order the ADDs arrive, so getting this
 * wrong lays the window out in the order rows were FIRST created -- which is
 * how it was found: a Save button above the settings it commits.
 */
static void
test_chrome_exec_row_order(void)
{
    int panel;
    int first;
    int second;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 0, 0, 200, "P");
    /* Three rows, then a rebuild that puts them back in the SAME order. The
     * handles come back off the free list reversed, which is the whole point. */
    ToriRSChrome_Label(&g_ui, panel, "one");
    ToriRSChrome_Label(&g_ui, panel, "two");
    ToriRSChrome_Label(&g_ui, panel, "three");
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    ToriRSChrome_PanelClearWidgets(&g_ui, panel);
    first = ToriRSChrome_Label(&g_ui, panel, "alpha");
    second = ToriRSChrome_Label(&g_ui, panel, "beta");
    ToriRSChrome_Label(&g_ui, panel, "gamma");
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);

    /* A recycled handle is a DIFFERENT widget, so the rebuild must announce a
     * removal and a fresh add for each -- not "nothing changed". */
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD) == 3,
        "a rebuilt panel re-adds every row");

    {
        int alpha_at = -1;
        int beta_at = -1;
        for( int i = 0; i < g_rec.count; i++ )
        {
            if( g_rec.cmds[i].kind != TORIRS_CHROME_CMD_WIDGET_ADD )
                continue;
            if( g_rec.cmds[i].widget == first )
                alpha_at = i;
            if( g_rec.cmds[i].widget == second )
                beta_at = i;
        }
        TEST_ASSERT(alpha_at >= 0 && beta_at >= 0, "both rows were announced");
        TEST_ASSERT(alpha_at < beta_at, "and in the order they sit in the panel");
    }

    /* The property that makes the above possible: a handle reused for a new
     * widget is not mistaken for the old one still being there. */
    TEST_ASSERT(
        g_ui.widgets[first].serial != g_ui.widgets[second].serial,
        "every widget has its own serial");
}

/*
 * An executor lost mid-session.
 *
 * Not the same case as one that refuses to start: this one came up, was driven
 * for a while, and then went away -- the user closed its window, the page
 * navigated, the tool window was destroyed. What has to hold is that the sync
 * goes inert rather than calling into a presentation that is gone, and that
 * whatever is bound NEXT is caught up from the model rather than from a shadow
 * describing a window nobody can see.
 */
static void
test_chrome_exec_lost(void)
{
    struct ToriRSChromeExec exec;
    static struct ToriRSChromeRecorder second;
    int panel;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIDBG_PANEL_WINDOW, 0, 0, 200, "P");
    ToriRSChrome_Checkbox(&g_ui, panel, "enabled", 1);
    ToriRSChrome_TextInput(&g_ui, panel, "colour", "#FFCC00");
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    /* Lost. The executor's own end() runs, and everything after is a no-op. */
    ToriRSChromeSync_Shutdown(&g_sync);
    TEST_ASSERT(g_rec.begun == 0, "shutdown takes the executor down");

    g_rec.count = 0;
    ToriRSChrome_SetText(&g_ui, 1, "#00FF00");
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) == 0, "a lost executor is not driven");
    TEST_ASSERT(g_rec.count == 0, "and nothing reaches it");
    TEST_ASSERT(ToriRSChromeSync_Pump(&g_sync, &g_ui) == 0, "nor is it polled");

    /*
     * Rebinding catches the NEW executor up from the model, not from the old
     * one's shadow. A shadow carried across would describe a window this
     * executor never built, so it would be told about nothing and show nothing.
     */
    ToriRSChromeRecorder_Init(&second);
    exec = ToriRSChromeExec_Recorder(&second);
    TEST_ASSERT(ToriRSChromeSync_Init(&g_sync, &exec) == 1, "a replacement binds");
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&second, TORIRS_CHROME_CMD_PANEL_OPEN) == 1,
        "the replacement is told about the panel");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&second, TORIRS_CHROME_CMD_WIDGET_ADD) == 2,
        "and about every widget, from the model");
    {
        struct ToriRSChromeCmd const* text =
            ToriRSChromeRecorder_Find(&second, TORIRS_CHROME_CMD_WIDGET_TEXT, 1);
        struct ToriRSChromeCmd const* add =
            ToriRSChromeRecorder_Find(&second, TORIRS_CHROME_CMD_WIDGET_ADD, 1);
        /* The edit made while nothing was bound is present -- carried on the
         * ADD, since to this executor the widget is new. */
        TEST_ASSERT(
            (text && strcmp(text->text, "#00FF00") == 0) ||
                (add && strcmp(add->text, "#00FF00") == 0),
            "including a change made while no executor was bound");
    }
}

void
test_chrome_exec(void)
{
    printf("TEST: chrome executor seam (deltas / removal order / intents)\n");

    test_chrome_exec_catchup();
    test_chrome_exec_quiet_frame();
    test_chrome_exec_remove();
    test_chrome_exec_panel_close();
    test_chrome_exec_options();
    test_chrome_exec_intents();
    test_chrome_exec_refused();
    test_chrome_exec_row_order();
    test_chrome_exec_lost();
}
