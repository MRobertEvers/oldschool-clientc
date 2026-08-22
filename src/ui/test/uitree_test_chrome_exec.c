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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 12, 20, 160, "Settings");
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
    TEST_ASSERT(add->value == TORIRS_CHROME_W_CHECKBOX, "the add carries the widget's kind");
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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 12, 20, 160, "Settings");
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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 160, "P");
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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 160, "P");
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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 200, "P");
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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 200, "P");
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
/*
 * A window that closed, and the one signal a host has to watch.
 *
 * A presentation that owns a window -- an OS window, a page's container -- can
 * lose it from EITHER side: the panel's own close box, or the window's title
 * bar. Both end in the model, at PanelSetVisible(0), and that is deliberate:
 * the model is the only thing every presentation shares, so it is the only
 * place the answer can be agreed on.
 *
 * Which makes "the panel is no longer visible" the host's cue to take the
 * executor down. It was not, once: Close hid the panel and left the plugin
 * window standing empty, with the client insisting it was closed and its
 * toggle spending a press re-closing it. Nothing below the seam can notice
 * that -- the executor was still being driven, correctly, for a panel with
 * nothing in it -- so what is pinned here is the signal, at the point the host
 * reads it.
 */
static void
test_chrome_exec_close_reported(void)
{
    struct ToriRSChromeIntent intent;
    int panel;

    exec_reset();
    /* A window of its own, so this is the case that has one to lose. */
    g_rec.surface_w = 360;
    g_rec.surface_h = 420;
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 72, 160, "Plugins");
    ToriRSChrome_Checkbox(&g_ui, panel, "enabled", 1);
    ToriRSChrome_Build(&g_ui);
    exec_settle();
    TEST_ASSERT(g_ui.panels[panel].visible == 1, "the window starts open");

    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_CLOSE;
    intent.panel = panel;
    intent.widget = -1;
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);

    TEST_ASSERT(ToriRSChromeSync_Pump(&g_sync, &g_ui) == 1, "a reported close applies");
    TEST_ASSERT(
        g_ui.panels[panel].visible == 0,
        "and lands in the MODEL, where the host reads it");

    /* Idempotent like every other intent: a transport that delivers the close
     * twice must not leave the host with two answers. */
    TEST_ASSERT(
        ToriRSChromeIntent_Apply(&g_ui, &intent) == 1 && g_ui.panels[panel].visible == 0,
        "a duplicated close is still closed");

    /* An executor still bound is told, once, and the rows go with it. */
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_PANEL_CLOSE) == 1,
        "the presentation hears the panel close");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_REMOVE) == 0,
        "without a REMOVE per row");

    g_rec.surface_w = 0;
    g_rec.surface_h = 0;
}

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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 160, "P");
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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 200, "P");
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
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 200, "P");
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

/*
 * A colour row across the seam.
 *
 * It carries no command of its own -- the value rides WIDGET_SELECTED, the hex
 * rides WIDGET_TEXT, and "the axis popup is open" rides WIDGET_CHECKED -- so
 * what this pins is that a native executor can rebuild the whole control from
 * commands that already existed, and that the intents it sends back land where
 * a click on the in-canvas row would.
 */
static void
test_chrome_exec_colorpick(void)
{
    int panel;
    int pick;
    struct ToriRSChromeIntent intent;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 4, 4, 200, "Markers");
    pick = ToriRSChrome_ColorPick(
        &g_ui, panel, "True tile", ToriRSChrome_Hsl16NearestRgb(0x00FFFFu));
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);

    {
        struct ToriRSChromeCmd const* add =
            ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD, pick);
        struct ToriRSChromeCmd const* sel =
            ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_SELECTED, pick);
        TEST_ASSERT(add && add->value == TORIRS_CHROME_W_COLORPICK, "the kind crosses");
        TEST_ASSERT(add && add->text[0] == '#', "and the hex rides the add");
        TEST_ASSERT(
            sel && sel->value == ToriRSChrome_ColorPickValue(&g_ui, pick),
            "the packed HSL16 crosses as the SELECTION -- no command of its own");
    }

    /* A PICK is how a presentation says "the user swept a bar". */
    exec_settle();
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_PICK;
    intent.panel = panel;
    intent.widget = pick;
    intent.value = ToriRSChrome_Hsl16Pack(20, 5, 90);
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);
    TEST_ASSERT(ToriRSChromeSync_Pump(&g_sync, &g_ui) == 1, "the pick applies");
    TEST_ASSERT(
        ToriRSChrome_ColorPickValue(&g_ui, pick) == ToriRSChrome_Hsl16Pack(20, 5, 90),
        "and lands as the value, not as a dropdown index");
    TEST_ASSERT(
        ToriRSChrome_TakeActivated(&g_ui) == pick,
        "and reports through the ordinary activation latch");

    /* A TEXT is how one says "the user typed a hex", and the model quantises
     * it -- which is what makes a browser's 24-bit colour input honest. */
    ToriRSChrome_Build(&g_ui);
    exec_settle();
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_TEXT;
    intent.panel = panel;
    intent.widget = pick;
    snprintf(intent.text, sizeof(intent.text), "%s", "#123456");
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);
    TEST_ASSERT(ToriRSChromeSync_Pump(&g_sync, &g_ui) == 1, "the edit applies");
    TEST_ASSERT(
        ToriRSChrome_ColorPickValue(&g_ui, pick) == ToriRSChrome_Hsl16NearestRgb(0x123456u),
        "an un-quantised colour from a presentation snaps onto a palette entry");
    TEST_ASSERT(
        strcmp(ToriRSChrome_Text(&g_ui, pick), "#123456") != 0,
        "and the field is rewritten to the entry, so the snap is visible");

    /* ACTIVATE is the swatch, ACTION is the field: the two zones, told apart by
     * WHICH intent arrives rather than by a coordinate every executor would
     * otherwise have to carry. */
    ToriRSChrome_Build(&g_ui);
    exec_settle();
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_ACTIVATE;
    intent.panel = panel;
    intent.widget = pick;
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);
    ToriRSChromeSync_Pump(&g_sync, &g_ui);
    TEST_ASSERT(ToriRSChrome_ColorPickIsOpen(&g_ui, pick), "ACTIVATE opens the axis popup");
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    {
        struct ToriRSChromeCmd const* checked =
            ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_CHECKED, pick);
        TEST_ASSERT(
            checked && checked->value == 1,
            "and says so as WIDGET_CHECKED, which is how a native executor knows to "
            "draw its own bars");
    }

    exec_settle();
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_ACTION;
    intent.panel = panel;
    intent.widget = pick;
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);
    ToriRSChromeSync_Pump(&g_sync, &g_ui);
    TEST_ASSERT(g_ui.focus == pick, "ACTION puts the keyboard in the hex field");
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    {
        struct ToriRSChromeCmd const* focus =
            ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_FOCUS, pick);
        /* The command a presentation with no focus of its own needs: without
         * it the CS2 window drew every field identically and a click on one
         * looked like it had done nothing. */
        TEST_ASSERT(focus && focus->value == 1, "and the focus crosses the seam");
    }
}

/*
 * A presentation with a window of its own fills it.
 *
 * The panel's authored geometry -- (8,72), 320 wide -- is a box that floats
 * over the GAME CANVAS. Put the same box in a window that holds nothing else
 * and it is a window inside a window: bands of empty background on three
 * sides, and dragging the frame wider grows the background rather than the
 * settings. Driven through the recorder rather than through SDL so it runs on
 * a machine with no display, which is every machine this suite runs on.
 */
static void
test_chrome_exec_fill_surface(void)
{
    struct ToriRSChromeExec exec;
    struct ToriRSChromeRect rect;
    int panel;
    int title_h;

    ToriRSChrome_Init(&g_ui);
    ToriRSChromeRecorder_Init(&g_rec);
    g_rec.surface_w = 300;
    g_rec.surface_h = 200;
    exec = ToriRSChromeExec_Recorder(&g_rec);
    TEST_ASSERT(ToriRSChromeSync_Init(&g_sync, &exec) == 1, "the windowed recorder comes up");

    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 72, 160, "Plugins");
    ToriRSChrome_PanelSetResizable(&g_ui, panel, 1);
    ToriRSChrome_PanelSetScrollable(&g_ui, panel, 1);
    ToriRSChrome_Checkbox(&g_ui, panel, "enabled", 1);

    TEST_ASSERT(
        ToriRSChromeSync_FillSurface(&g_sync, &g_ui, panel) == 1,
        "an executor with a window of its own fills it");
    ToriRSChrome_Build(&g_ui);
    rect = ToriRSChrome_PanelRect(&g_ui, panel);
    TEST_ASSERT(rect.x == 0 && rect.y == 0, "the panel sits at the window's origin");
    TEST_ASSERT(rect.w == 300 && rect.h == 200, "and is exactly the window's size");

    /* Run every frame, so a repeat has to cost nothing: a rebuild per frame
     * would re-emit the whole window to a native executor 50 times a second,
     * which is the one thing the shadow exists to prevent. */
    exec_settle();
    TEST_ASSERT(
        ToriRSChromeSync_FillSurface(&g_sync, &g_ui, panel) == 1, "filling again still answers");
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 0, "but rebuilds nothing");
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) == 0, "and tells the executor nothing");

    /* The window grew: the panel grows with it, on the frame the size changed
     * rather than on the next one. */
    g_rec.surface_w = 420;
    g_rec.surface_h = 260;
    ToriRSChromeSync_FillSurface(&g_sync, &g_ui, panel);
    ToriRSChrome_Build(&g_ui);
    rect = ToriRSChrome_PanelRect(&g_ui, panel);
    TEST_ASSERT(rect.w == 420 && rect.h == 260, "a resized window resizes the panel");
    {
        struct ToriRSChromeCmd const* r;
        ToriRSChromeSync_Run(&g_sync, &g_ui);
        r = ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_PANEL_RECT, -1);
        TEST_ASSERT(
            r && r->w == 420 && r->h == 260, "and the new box crosses the seam");
    }

    /*
     * The drag and the grip are gone with it. Both write geometry the next
     * fill overwrites, so leaving them is a title bar that takes the cursor
     * and gives nothing back, and a corner that snaps.
     */
    title_h = ToriRSChrome_FontLineBox(TORIRS_CHROME_FONT_MENU, ToriRSChrome_Scale(&g_ui));
    ToriRSChrome_MouseDown(&g_ui, 100, 1 + title_h / 2);
    ToriRSChrome_MouseMove(&g_ui, 160, 60);
    ToriRSChrome_MouseUp(&g_ui, 160, 60);
    ToriRSChrome_Build(&g_ui);
    rect = ToriRSChrome_PanelRect(&g_ui, panel);
    TEST_ASSERT(rect.x == 0 && rect.y == 0, "a filled panel cannot be dragged off its origin");

    ToriRSChrome_MouseDown(&g_ui, 420 - 3, 260 - 3);
    ToriRSChrome_MouseMove(&g_ui, 200, 120);
    ToriRSChrome_MouseUp(&g_ui, 200, 120);
    ToriRSChrome_Build(&g_ui);
    rect = ToriRSChrome_PanelRect(&g_ui, panel);
    TEST_ASSERT(rect.w == 420 && rect.h == 260, "nor resized from the inside");

    /*
     * The other half, and the reason this is a property of the EXECUTOR rather
     * than of the panel: in-canvas chrome floats, because there it has a game
     * to float over.
     */
    ToriRSChrome_Init(&g_ui);
    ToriRSChromeRecorder_Init(&g_rec);
    exec = ToriRSChromeExec_Recorder(&g_rec);
    ToriRSChromeSync_Init(&g_sync, &exec);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 72, 160, "Plugins");
    ToriRSChrome_Checkbox(&g_ui, panel, "enabled", 1);
    TEST_ASSERT(
        ToriRSChromeSync_FillSurface(&g_sync, &g_ui, panel) == 0,
        "an executor with no window of its own does not fill");
    ToriRSChrome_Build(&g_ui);
    rect = ToriRSChrome_PanelRect(&g_ui, panel);
    TEST_ASSERT(rect.x == 8 && rect.y == 72, "and the panel keeps the box it was authored with");
}

/* Titles borrowed by the strips below; they have to outlive the widget. */
static char const* const g_drag_tabs[] = { "One", "Two" };
static char const* const g_drag_many_tabs[] = { "Alpha",   "Bravo", "Charlie",
                                                "Delta",   "Echo",  "Foxtrot" };

/** Centre of a rect, which is the point a user aims at. */
static int
drag_mid_x(struct ToriRSChromeRect r)
{
    return r.x + r.w / 2;
}

static int
drag_mid_y(struct ToriRSChromeRect r)
{
    return r.y + r.h / 2;
}

/*
 * The handles a frameless window is moved by, and -- the half that actually
 * breaks -- the controls that have to be punched back out of them.
 *
 * A draggable region SWALLOWS the press that begins a drag: the application is
 * never told about a mouse-down inside one. So every one of these assertions is
 * really the same assertion twice over -- this box drags the window, and that
 * box still reaches the control it is drawn on. A screenshot cannot tell the
 * two apart, and neither can the window until someone tries to click a tab.
 *
 * Driven through the recorder, so it runs on a machine with no display.
 */
static void
test_chrome_exec_drag_region(void)
{
    struct ToriRSChromeExec exec;
    struct ToriRSChromeDragRegion region;
    struct ToriRSChromeRect title;
    struct ToriRSChromeRect strip;
    int panel;
    int tabs;

    ToriRSChrome_Init(&g_ui);
    ToriRSChromeRecorder_Init(&g_rec);
    g_rec.surface_w = 300;
    g_rec.surface_h = 220;
    exec = ToriRSChromeExec_Recorder(&g_rec);
    TEST_ASSERT(ToriRSChromeSync_Init(&g_sync, &exec) == 1, "the windowed recorder comes up");

    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 72, 160, "Plugins");
    tabs = ToriRSChrome_Tabs(&g_ui, panel, g_drag_tabs, 2, 0);
    ToriRSChrome_Checkbox(&g_ui, panel, "enabled", 1);

    /*
     * FLOATING first, because this is the case that must NOT have a handle. In
     * the canvas the same title bar already moves the panel inside the frame,
     * and a bar that did both would drag the game out from under the pointer.
     */
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(
        ToriRSChrome_WindowDragRegion(&g_ui, panel, &region) == 0,
        "a floating panel offers the OS window no handle");
    TEST_ASSERT(region.handle_count == 0, "and the region it cleared is empty");

    /* Filled: the panel IS the window, so its chrome is the window's chrome. */
    TEST_ASSERT(
        ToriRSChromeSync_FillSurface(&g_sync, &g_ui, panel) == 1, "the window fills with the panel");
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(
        ToriRSChrome_WindowDragRegion(&g_ui, panel, &region) == 1,
        "a filled panel has a drag region");
    TEST_ASSERT(region.handle_count == 2, "the title bar and the tab strip, both");
    title = region.handles[0];
    strip = region.handles[1];
    TEST_ASSERT(title.w > 0 && title.h > 0, "the title bar is a real box");
    TEST_ASSERT(strip.y > title.y, "and the strip is below it");

    TEST_ASSERT(
        ToriRSChromeDragRegion_Contains(&region, drag_mid_x(title), drag_mid_y(title)),
        "the title bar drags the window");
    TEST_ASSERT(
        ToriRSChromeDragRegion_Contains(&region, strip.x + strip.w - 2, drag_mid_y(strip)),
        "and so does the empty tail of the tab strip");

    /*
     * The tabs themselves do not -- and the proof that the point tested is a
     * tab is that pressing it selects one. Asserting "not draggable" against a
     * coordinate nothing is drawn at would pass on an empty strip.
     */
    TEST_ASSERT(
        !ToriRSChromeDragRegion_Contains(&region, strip.x + 2, drag_mid_y(strip)),
        "a tab is not a drag handle");
    ToriRSChrome_MouseDown(&g_ui, strip.x + 2, drag_mid_y(strip));
    ToriRSChrome_MouseUp(&g_ui, strip.x + 2, drag_mid_y(strip));
    TEST_ASSERT(ToriRSChrome_PanelActiveTab(&g_ui, panel) == 0, "because the press picks tab 0");
    TEST_ASSERT(ToriRSChrome_HitTest(&g_ui, strip.x + 2, drag_mid_y(strip)) == tabs,
                "on the strip the region claims");

    /*
     * Close lives IN the title bar. Unpunched it would be unreachable rather
     * than merely awkward: the press that should shut the window would start a
     * drag of it, and the button would never see a click at all.
     *
     * Probed at the HOLE's own centre rather than at a measured offset from the
     * bar's right edge. The button is placed with padding inside the frame's
     * rail, and both of those have changed with the art -- a coordinate guessed
     * from the outside stops landing on the button and the assertion then
     * passes for the wrong reason.
     */
    {
        int hole = -1;

        ToriRSChrome_PanelSetClosable(&g_ui, panel, 1);
        ToriRSChrome_Build(&g_ui);
        ToriRSChrome_WindowDragRegion(&g_ui, panel, &region);
        title = region.handles[0];
        TEST_ASSERT(
            ToriRSChromeDragRegion_Contains(&region, title.x + 2, drag_mid_y(title)),
            "the left of the title bar still drags");
        for( int i = 0; i < region.hole_count; i++ )
        {
            struct ToriRSChromeRect const h = region.holes[i];
            if( h.w > 0 && h.y >= title.y && h.y + h.h <= title.y + title.h )
                hole = i;
        }
        TEST_ASSERT(hole >= 0, "and a hole was punched inside it");
        TEST_ASSERT(
            !ToriRSChromeDragRegion_Contains(
                &region, drag_mid_x(region.holes[hole]), drag_mid_y(region.holes[hole])),
            "which is the Close button, and does not drag");
    }

    /*
     * A strip whose tabs have been compressed to fill its width has no tail,
     * and correctly offers nothing: every pixel of it is a tab. This is why the
     * title bar is the handle that has to always be there.
     */
    ToriRSChrome_Init(&g_ui);
    ToriRSChromeRecorder_Init(&g_rec);
    g_rec.surface_w = 120;
    g_rec.surface_h = 200;
    exec = ToriRSChromeExec_Recorder(&g_rec);
    ToriRSChromeSync_Init(&g_sync, &exec);
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 120, "Plugins");
    ToriRSChrome_Tabs(&g_ui, panel, g_drag_many_tabs, 6, 0);
    ToriRSChromeSync_FillSurface(&g_sync, &g_ui, panel);
    ToriRSChrome_Build(&g_ui);
    ToriRSChrome_WindowDragRegion(&g_ui, panel, &region);
    strip = region.handles[region.handle_count - 1];
    TEST_ASSERT(
        !ToriRSChromeDragRegion_Contains(&region, strip.x + strip.w - 2, drag_mid_y(strip)),
        "a strip packed edge to edge with tabs has no tail to grab");
    TEST_ASSERT(
        ToriRSChromeDragRegion_Contains(
            &region, drag_mid_x(region.handles[0]), drag_mid_y(region.handles[0])),
        "and the title bar is what is left to drag it by");

    /*
     * The region is published on EVERY frame, including a frame that has none.
     * An executor that simply stopped hearing about handles would go on
     * offering the last set it was told -- a band of the window that eats
     * presses because a strip used to be there.
     */
    ToriRSChrome_PanelSetVisible(&g_ui, panel, 0);
    ToriRSChrome_Build(&g_ui);
    g_rec.drag_publishes = 0;
    TEST_ASSERT(
        ToriRSChromeSync_PublishDragRegion(&g_sync, &g_ui, panel) == 1,
        "a hidden panel is still published for");
    TEST_ASSERT(g_rec.drag_publishes == 1, "exactly once");
    TEST_ASSERT(
        g_rec.drag.handle_count == 0 && g_rec.drag.hole_count == 0,
        "and what crosses is an empty region, not a stale one");
}

/*
 * The checkbox style crosses the seam, once, before anything it applies to.
 *
 * A native executor sizes and places its own controls, so hearing which art a
 * checkbox wears AFTER the rows have been declared means every one of them was
 * laid out against the wrong width -- 17 where the art is 18. And an executor
 * that is never told at all draws the tick pair while the in-canvas chrome
 * beside it draws the well, which is precisely the disagreement this seam
 * exists to prevent.
 */
static void
test_chrome_exec_check_style(void)
{
    int panel;
    struct ToriRSChromeCmd const* said;
    int style_at = -1;
    int open_at = -1;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 12, 20, 160, "Settings");
    ToriRSChrome_Checkbox(&g_ui, panel, "enabled", 1);
    ToriRSChrome_Build(&g_ui);

    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) > 0, "the first sync says something");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_CHECK_STYLE) == 1,
        "an executor is told the checkbox style even at the default");
    said = ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_CHECK_STYLE, -1);
    TEST_ASSERT(said != NULL, "the style command names no widget");
    TEST_ASSERT(
        said->value == TORIRS_CHROME_CHECK_STYLE_TICK, "and carries the style it is at");

    for( int i = 0; i < g_rec.count; i++ )
    {
        if( g_rec.cmds[i].kind == TORIRS_CHROME_CMD_CHECK_STYLE && style_at < 0 )
            style_at = i;
        if( g_rec.cmds[i].kind == TORIRS_CHROME_CMD_PANEL_OPEN && open_at < 0 )
            open_at = i;
    }
    TEST_ASSERT(style_at >= 0 && open_at >= 0, "both were said (else this proves nothing)");
    TEST_ASSERT(style_at < open_at, "the style is said before the panel it applies to");

    /* A frame that changes nothing does not restate it -- the whole point of
     * the shadow. */
    TEST_ASSERT(exec_settle() == 0, "a clean frame says nothing about the style");

    ToriRSChrome_SetCheckStyle(&g_ui, TORIRS_CHROME_CHECK_STYLE_BOX);
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) > 0, "a style change is said");
    said = ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_CHECK_STYLE, -1);
    TEST_ASSERT(said != NULL, "as its own command");
    TEST_ASSERT(said->value == TORIRS_CHROME_CHECK_STYLE_BOX, "carrying the new style");

    g_rec.count = 0;
    ToriRSChrome_SetCheckStyle(&g_ui, TORIRS_CHROME_CHECK_STYLE_BOX);
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_CHECK_STYLE) == 0,
        "and setting it to what it already is says nothing at all");

    /*
     * The two arts are different sizes, so the style is a LAYOUT input: a row
     * that did not re-measure would draw the 18px well in a box reserved for
     * the 17px tick, which is the speckled edge every other blit here avoids.
     */
    TEST_ASSERT(
        ToriRSChrome_CheckBoxMetric(TORIRS_CHROME_CHECK_STYLE_BOX) >
            ToriRSChrome_CheckBoxMetric(TORIRS_CHROME_CHECK_STYLE_TICK),
        "the well is the wider of the two boxes");
    TEST_ASSERT(
        ToriRSChrome_CheckSlot(TORIRS_CHROME_CHECK_STYLE_BOX, 1) ==
            TORIRS_CHROME_SKIN_CHECK_BOX_ON,
        "the box style draws the boxed tick when on");
    TEST_ASSERT(
        ToriRSChrome_CheckSlot(TORIRS_CHROME_CHECK_STYLE_BOX, 0) ==
            TORIRS_CHROME_SKIN_CHECK_BOX_OFF,
        "and the empty well when off");
    TEST_ASSERT(
        ToriRSChrome_CheckSlot(TORIRS_CHROME_CHECK_STYLE_TICK, 1) ==
            TORIRS_CHROME_SKIN_CHECK_ON,
        "the tick style is untouched by any of this");
}

/*
 * A multiline field across the seam.
 *
 * Two things a native executor is broken by and nothing on screen would show:
 * the line count only ever rides the ADD (so an executor that missed it builds
 * every box one line tall), and a TEXTAREA's "selection" is its scroll offset
 * rather than an option index -- which is how a presentation that draws its own
 * lines shows the window of a long list the model is showing.
 */
static void
test_chrome_exec_textarea(void)
{
    int panel;
    int area;
    struct ToriRSChromeIntent intent;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 4, 4, 200, "Ground Items");
    area = ToriRSChrome_TextArea(&g_ui, panel, "Highlighted items", "abyssal whip", 5);
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);

    {
        struct ToriRSChromeCmd const* add =
            ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD, area);
        TEST_ASSERT(add && add->value == TORIRS_CHROME_W_TEXTAREA, "the kind crosses");
        TEST_ASSERT(add && add->h == 5, "and the line count rides the ADD, as its shape");
        TEST_ASSERT(
            add && strcmp(add->text, "abyssal whip") == 0, "with the value it opens on");
    }

    /* Scrolling the box is a SELECTED, because that is the field the seam
     * already diffs -- and it must not be mistaken for a dropdown's index. */
    exec_settle();
    ToriRSChrome_SetText(&g_ui, area, "a\nb\nc\nd\ne\nf\ng");
    g_ui.widgets[area].caret = (int)strlen(ToriRSChrome_Text(&g_ui, area));
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    {
        struct ToriRSChromeCmd const* sel =
            ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_SELECTED, area);
        TEST_ASSERT(g_ui.widgets[area].scroll > 0, "seven lines do not fit a five-line box");
        TEST_ASSERT(
            sel && sel->value == g_ui.widgets[area].scroll,
            "the top visible line crosses as the SELECTION");
    }

    /* A TEXT intent carrying newlines lands whole -- the shape a DOM textarea
     * and a multiline EDIT both commit. */
    exec_settle();
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_TEXT;
    intent.panel = panel;
    intent.widget = area;
    snprintf(intent.text, sizeof(intent.text), "%s", "whip\ntbow");
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);
    TEST_ASSERT(ToriRSChromeSync_Pump(&g_sync, &g_ui) == 1, "the edit applies");
    TEST_ASSERT(
        strcmp(ToriRSChrome_Text(&g_ui, area), "whip\ntbow") == 0,
        "and the newline survives the crossing");

    /* ACTIVATE is a click on the box, and what a click on a field does is take
     * the focus -- the same answer a one-line field gives, so the host's
     * keyboard routing lands typing in it. */
    ToriRSChrome_Build(&g_ui);
    exec_settle();
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_ACTIVATE;
    intent.panel = panel;
    intent.widget = area;
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);
    ToriRSChromeSync_Pump(&g_sync, &g_ui);
    TEST_ASSERT(g_ui.focus == area, "a click on the box focuses it");
    TEST_ASSERT(
        ToriRSChrome_TakeActivated(&g_ui) == -1,
        "and does not also fire the activation latch");
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
    test_chrome_exec_close_reported();
    test_chrome_exec_refused();
    test_chrome_exec_row_order();
    test_chrome_exec_lost();
    test_chrome_exec_colorpick();
    test_chrome_exec_fill_surface();
    test_chrome_exec_drag_region();
    test_chrome_exec_check_style();
    test_chrome_exec_textarea();
}
