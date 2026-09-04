/*
 * The executor seam: what Sync emits, and what an intent does when it lands.
 *
 * The properties worth pinning are the ones a retained DOM executor would be
 * broken by and a screenshot would never show:
 *
 *  - a clean frame emits nothing, so a DOM control is not destroyed and
 *    recreated (losing focus and caret) on a frame where nothing moved;
 *  - a changed field emits ONLY that field;
 *  - removal is announced before the handle can be recycled by an add;
 *  - a closed panel takes its rows with it, without a REMOVE per row;
 *  - an intent lands on the model exactly where a click would, so WEB and
 *    BROWSER cannot disagree about what is checked.
 */
#include "test_harness.h"

#include "torirs_chrome_exec.h"
#include "torirs_chrome_mirror.h"
#include "uitree_debug_overlay.h"

static struct ToriRSChrome g_ui;
static struct ToriRSChromeSync g_sync;
static struct ToriRSChromeRecorder g_rec;
static int g_snapshot_request;
static int g_fail_on_end;
static void (*g_forward_apply)(void*, struct ToriRSChromeCmd const*);

struct CountExecFixture { int commands; };

static void
count_exec_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    struct CountExecFixture* fixture = user;
    (void)cmd;
    fixture->commands++;
}

static int
exec_take_snapshot_request(void* user)
{
    int const requested = g_snapshot_request;
    (void)user;
    g_snapshot_request = 0;
    return requested;
}

static void
exec_apply_with_injected_loss(void* user, struct ToriRSChromeCmd const* cmd)
{
    g_forward_apply(user, cmd);
    if( g_fail_on_end && cmd->kind == TORIRS_CHROME_CMD_SYNC_END )
    {
        g_fail_on_end = 0;
        g_snapshot_request = 1;
    }
}

/** Bind a fresh recorder to a fresh model and catch it up to the model's state. */
static void
exec_reset(void)
{
    struct ToriRSChromeExec exec;

    g_snapshot_request = 0;
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
test_chrome_exec_internal_fallback(void)
{
    struct ToriRSChromeExec exec;
    int got = -1;

    /* This platform-free harness deliberately undefines WEB/BROWSER. Asking
     * for either public kind must produce the non-selectable internal sink. */
    exec = ToriRSChromeExec_ForKind(TORIRS_CHROME_EXEC_WEB, NULL, &got);
    TEST_ASSERT(
        got == TORIRS_CHROME_EXEC_BUFFER && !exec.begin && !exec.apply &&
            !exec.end && !exec.poll,
        "an unavailable public web executor becomes the empty BUFFER fallback");
    got = -1;
    exec = ToriRSChromeExec_ForKind(-1, NULL, &got);
    TEST_ASSERT(
        got == TORIRS_CHROME_EXEC_BUFFER && !exec.apply,
        "a build with no web backend defaults internally to BUFFER");
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
 * The property the delivered shadow exists for. A frame where nothing moved
 * must emit nothing, or every DOM control is torn down and rebuilt 50 times a
 * second and no text field can be typed into.
 */
static void
test_chrome_exec_quiet_frame(void)
{
    int panel;
    int input;
    int commands;
    uint32_t visits;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 12, 20, 160, "Settings");
    input = ToriRSChrome_TextInput(&g_ui, panel, "colour", "#FFCC00");
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    commands = g_sync.cmd_count;
    visits = g_sync.change_visit_count;
    TEST_ASSERT(g_ui.change_count == 0, "the catch-up consumes pre-bind changes");
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) == 0, "a frame with no change says nothing");
    TEST_ASSERT(
        g_rec.count == 0 && g_sync.cmd_count == commands &&
            g_sync.change_visit_count == visits,
        "a settled frame starts no executor transaction and visits no node or shadow");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD) == 0,
        "and re-adds nothing");

    /* Setting a value to what it already is is not a change, the same way the
     * chrome's own compare-then-set mutators treat it. */
    ToriRSChrome_SetText(&g_ui, input, "#FFCC00");
    TEST_ASSERT(g_ui.change_count == 0, "a compare-equal setter queues no work");
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) == 0, "a no-op write is still quiet");
    TEST_ASSERT(g_rec.count == 0, "a compare-equal write reaches no web executor callback");

    /* A real change emits exactly one thing. */
    g_rec.count = 0;
    ToriRSChrome_SetText(&g_ui, input, "#00FF00");
    TEST_ASSERT(
        g_ui.change_count == 1 &&
            g_ui.changes[0].kind == TORIRS_CHROME_CHANGE_WIDGET &&
            g_ui.changes[0].widget == input &&
            g_ui.changes[0].flags == TORIRS_CHROME_CHANGE_WIDGET_TEXT,
        "one changed node and its exact property are recorded once");
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) == 1, "one change is one command");
    TEST_ASSERT(
        g_sync.change_visit_count == visits + 6,
        "the efficiency counter reports all six fixed-pass journal inspections");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_TEXT) == 1,
        "and it is the text");
    TEST_ASSERT(
        strcmp(ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_TEXT, input)->text,
               "#00FF00") == 0,
        "carrying the new value");
}

static void
test_chrome_exec_coalesces_setter_burst(void)
{
    static char const* const first[] = { "alpha", "beta" };
    static char const* const second[] = { "one", "two", "three" };
    int panel;
    int input;
    int dropdown;
    int commands;
    uint32_t visits;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(
        &g_ui, TORIRS_CHROME_PANEL_WINDOW, 12, 20, 180, "Coalesce");
    input = ToriRSChrome_TextInput(&g_ui, panel, "name", "start");
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    ToriRSChrome_SetText(&g_ui, input, "one");
    ToriRSChrome_SetText(&g_ui, input, "two");
    ToriRSChrome_SetText(&g_ui, input, "final");
    TEST_ASSERT(
        g_ui.change_count == 1 &&
            g_ui.changes[0].flags == TORIRS_CHROME_CHANGE_WIDGET_TEXT,
        "same-field writes coalesce to one exact-property node event");
    ToriRSChrome_Build(&g_ui);
    visits = g_sync.change_visit_count;
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) == 1, "a setter burst emits one delta");
    TEST_ASSERT(
        g_sync.change_visit_count == visits + 6 &&
            ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_TEXT) == 1 &&
            strcmp(ToriRSChromeRecorder_Find(
                       &g_rec, TORIRS_CHROME_CMD_WIDGET_TEXT, input)->text,
                   "final") == 0,
        "only the final retained value crosses to the executor");

    g_rec.count = 0;
    commands = g_sync.cmd_count;
    ToriRSChrome_SetText(&g_ui, input, "temporary");
    ToriRSChrome_SetText(&g_ui, input, "final");
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(g_ui.change_count == 1, "a net-zero burst remains one queued comparison");
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) == 0 && g_rec.count == 0 &&
            g_sync.cmd_count == commands,
        "a burst ending at the delivered value opens no executor transaction");

    /* Lists use a bounded content snapshot as their retained value. Returning
     * the text and selection to what the executor already holds is the list
     * form of the same net-zero burst and must not rebuild a DOM select. */
    dropdown = ToriRSChrome_Dropdown(&g_ui, panel, "mode", first, 2, 0);
    ToriRSChrome_Build(&g_ui);
    exec_settle();
    ToriRSChrome_DropdownSetOptions(&g_ui, dropdown, second, 3, 1);
    ToriRSChrome_DropdownSetOptions(&g_ui, dropdown, first, 2, 0);
    ToriRSChrome_Build(&g_ui);
    g_rec.count = 0;
    commands = g_sync.cmd_count;
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) == 0 && g_rec.count == 0 &&
            g_sync.cmd_count == commands,
        "an option-list burst ending at the delivered value opens no transaction");

    /* An identity that is born and removed entirely between executor ticks was
     * never part of the delivered tree. Its queued ADD is discarded by remove,
     * and its REMOVE compares against no live shadow. */
    {
        int const transient = ToriRSChrome_Label(&g_ui, panel, "transient");
        ToriRSChrome_WidgetRemove(&g_ui, transient);
    }
    ToriRSChrome_Build(&g_ui);
    g_rec.count = 0;
    commands = g_sync.cmd_count;
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) == 0 && g_rec.count == 0 &&
            g_sync.cmd_count == commands,
        "an add/remove burst for an undelivered node opens no transaction");
}

static void
test_chrome_exec_indexed_coalescing(void)
{
    enum { WIDGETS = 256 };
    int widgets[WIDGETS];
    int panel;
    uint32_t visits;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(
        &g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 320, "Indexed burst");
    for( int i = 0; i < WIDGETS; i++ )
        widgets[i] = ToriRSChrome_TextInput(&g_ui, panel, "field", "initial");
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    /* Touch every field repeatedly before one browser tick. The first pass
     * allocates one record per identity; every later pass must find that
     * record through the direct widget index, not scan an ever-growing queue. */
    for( int pass = 0; pass < 33; pass++ )
        for( int i = 0; i < WIDGETS; i++ )
            ToriRSChrome_SetText(&g_ui, widgets[i], (pass & 1) ? "odd" : "even");

    TEST_ASSERT(
        g_ui.change_count == WIDGETS && !g_ui.change_lost,
        "a repeated wide burst retains one journal entry per widget");
    for( int i = 0; i < WIDGETS; i++ )
    {
        int const pending = g_ui.change_pending_widget[widgets[i]];
        TEST_ASSERT(
            pending > 0 && pending <= g_ui.change_count &&
                g_ui.changes[pending - 1].kind == TORIRS_CHROME_CHANGE_WIDGET &&
                g_ui.changes[pending - 1].widget == widgets[i] &&
                g_ui.changes[pending - 1].serial == g_ui.widgets[widgets[i]].serial,
            "the O(1) pending index names the widget's exact retained identity");
    }

    ToriRSChrome_Build(&g_ui);
    visits = g_sync.change_visit_count;
    g_rec.count = 0;
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) == WIDGETS,
        "the wide burst emits only its final value per widget");
    TEST_ASSERT(
        g_sync.change_visit_count == visits + 6 * WIDGETS,
        "the drain reports every inspection in its fixed number of journal passes");
    TEST_ASSERT(
        g_ui.change_count == 0 &&
            g_ui.change_pending_widget[widgets[WIDGETS - 1]] == 0,
        "acknowledging a drain clears the journal and its pending indexes");
}

static void
test_chrome_exec_records_dependent_properties(void)
{
    static char const* const tabs[] = { "One", "Two" };
    struct ToriRSChromeCmd const* cmd;
    int panel;
    int custom;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(
        &g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 200, "Dependencies");
    custom = ToriRSChrome_Custom(&g_ui, panel, "view", 80);
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    /* The model stores the well in chrome pixels; the web command carries
     * logical units. A scale change must therefore identify the custom node,
     * not rely on a capacity walk to notice the derived value. */
    ToriRSChrome_SetScale(&g_ui, 2);
    TEST_ASSERT(
        g_ui.change_count == 2 &&
            g_ui.changes[0].kind == TORIRS_CHROME_CHANGE_PANEL &&
            g_ui.changes[0].flags == TORIRS_CHROME_CHANGE_PANEL_RECT &&
            g_ui.changes[1].kind == TORIRS_CHROME_CHANGE_WIDGET &&
            g_ui.changes[1].widget == custom &&
            g_ui.changes[1].flags == TORIRS_CHROME_CHANGE_WIDGET_HEIGHT,
        "scale names the affected panel and exact custom-height property");
    ToriRSChrome_Build(&g_ui);
    g_rec.count = 0;
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    cmd = ToriRSChromeRecorder_Find(
        &g_rec, TORIRS_CHROME_CMD_WIDGET_HEIGHT, custom);
    TEST_ASSERT(cmd && cmd->h == 40, "the derived logical custom height reaches the executor");

    /* A tab strip may be inserted into an already-mounted panel. Its ADD says
     * which tab owns the strip; PANEL_TAB separately says which tab's rows the
     * panel should show. */
    g_rec.count = 0;
    ToriRSChrome_Tabs(&g_ui, panel, tabs, 2, 1);
    TEST_ASSERT(
        g_ui.change_count == 2 &&
            g_ui.changes[0].kind == TORIRS_CHROME_CHANGE_PANEL &&
            (g_ui.changes[0].flags & TORIRS_CHROME_CHANGE_PANEL_TAB) &&
            g_ui.changes[1].kind == TORIRS_CHROME_CHANGE_WIDGET,
        "dynamic tabs record both the panel selection and new strip node");
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    cmd = ToriRSChromeRecorder_Find(
        &g_rec, TORIRS_CHROME_CMD_PANEL_TAB, -1);
    TEST_ASSERT(cmd && cmd->panel == panel && cmd->value == 1,
                "dynamic tab selection reaches an already-mounted panel");
}

static void
test_chrome_exec_queue_loss_snapshots_once(void)
{
    int panel;
    int widget;
    struct ToriRSChromeCmd const* begin;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 8, 160, "Loss");
    widget = ToriRSChrome_Label(&g_ui, panel, "first");
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    for( int i = 0; i < TORIRS_CHROME_MAX_CHANGES + 8; i++ )
    {
        ToriRSChrome_WidgetRemove(&g_ui, widget);
        widget = ToriRSChrome_Label(&g_ui, panel, (i & 1) ? "odd" : "even");
    }
    TEST_ASSERT(g_ui.change_lost, "bounded queue overflow is explicit");
    ToriRSChrome_Build(&g_ui);
    g_rec.count = 0;
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) > 0, "loss triggers one full catch-up");
    begin = ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_SYNC_BEGIN, -1);
    TEST_ASSERT(
        begin && begin->value == 1 &&
            ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_PANEL_OPEN) == 1 &&
            ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD) == 1,
        "overflow recovery is one marked authoritative snapshot");
    TEST_ASSERT(!g_ui.change_lost && g_ui.change_count == 0, "snapshot acknowledges the loss");
    g_rec.count = 0;
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) == 0 && g_rec.count == 0,
        "overflow catch-up is exactly once");
}

static void
test_chrome_exec_delivery_loss_snapshots_once(void)
{
    struct ToriRSChromeCmd const* begin;
    int panel;

    exec_reset();
    g_sync.exec.take_snapshot_request = exec_take_snapshot_request;
    panel = ToriRSChrome_PanelAdd(
        &g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 8, 160, "Delivery loss");
    ToriRSChrome_Label(&g_ui, panel, "retained");
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    /* A bounded web transport reports a failed atomic batch through a one-shot
     * latch. It must be answered before deltas, as one authoritative page. */
    g_snapshot_request = 1;
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) > 0,
        "executor-side delivery loss triggers a full catch-up");
    begin = ToriRSChromeRecorder_Find(
        &g_rec, TORIRS_CHROME_CMD_SYNC_BEGIN, -1);
    TEST_ASSERT(
        begin && begin->value == 1 &&
            ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_PANEL_OPEN) == 1 &&
            ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD) == 1,
        "delivery recovery is one marked authoritative snapshot");

    g_rec.count = 0;
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) == 0 && g_rec.count == 0,
        "the consumed delivery-loss request does not snapshot again");
}

static void
test_chrome_exec_commit_failure_retains_journal(void)
{
    struct ToriRSChromeExec exec;
    struct ToriRSChromeCmd const* begin;
    int panel;

    g_snapshot_request = 0;
    g_fail_on_end = 0;
    ToriRSChrome_Init(&g_ui);
    ToriRSChromeRecorder_Init(&g_rec);
    exec = ToriRSChromeExec_Recorder(&g_rec);
    g_forward_apply = exec.apply;
    exec.apply = exec_apply_with_injected_loss;
    exec.take_snapshot_request = exec_take_snapshot_request;
    TEST_ASSERT(ToriRSChromeSync_Init(&g_sync, &exec) == 1, "loss fixture starts");
    panel = ToriRSChrome_PanelAdd(
        &g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 8, 160, "Atomic loss");
    ToriRSChrome_Label(&g_ui, panel, "must retry");
    ToriRSChrome_Build(&g_ui);

    g_fail_on_end = 1;
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) == 0 &&
            g_ui.change_count > 0 && !g_sync.primed && g_sync.restate &&
            !g_sync.last_run_restate,
        "a failed END neither acknowledges the journal nor advances the shadow");

    g_rec.count = 0;
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) > 0 &&
            g_ui.change_count == 0 && g_sync.primed && g_sync.last_run_restate,
        "the retained journal retries as one successful snapshot");
    begin = ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_SYNC_BEGIN, -1);
    TEST_ASSERT(begin && begin->value == 1,
        "the retry is explicitly an authoritative restatement");
    g_rec.count = 0;
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) == 0 &&
            !g_sync.last_run_restate && g_rec.count == 0,
        "the successful-restatement signal is one-shot on the following idle run");
}

static void
test_chrome_exec_retains_focused_node(void)
{
    struct ToriRSChromeIntent intent;
    int panel;
    int input;
    int check;
    int serial;
    int caret;
    int commands;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(
        &g_ui, TORIRS_CHROME_PANEL_WINDOW, 12, 20, 180, "Focused");
    input = ToriRSChrome_TextInput(&g_ui, panel, "name", "Rune");
    check = ToriRSChrome_Checkbox(&g_ui, panel, "enabled", 0);
    ToriRSChrome_Build(&g_ui);
    exec_settle();
    serial = g_ui.widgets[input].serial;

    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_ACTIVATE;
    intent.panel = panel;
    intent.widget = input;
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);
    TEST_ASSERT(
        ToriRSChromeSync_Pump(&g_sync, &g_ui) == 1,
        "DOM focus changes retained model state once");
    TEST_ASSERT(g_ui.focus == input && g_ui.dirty, "DOM focus dirties its surface row");
    caret = g_ui.widgets[input].caret;
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 1, "focus receives one retained repaint");
    g_rec.count = 0;
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) == 1, "focus is one semantic delta");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_FOCUS) == 1 &&
            ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD) == 0,
        "taking focus updates rather than recreates the DOM control");

    g_rec.count = 0;
    commands = g_sync.cmd_count;
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);
    TEST_ASSERT(
        ToriRSChromeSync_Pump(&g_sync, &g_ui) == 0,
        "a duplicate DOM focus intent is an idempotent no-op");
    TEST_ASSERT(
        g_ui.focus == input && g_ui.widgets[input].caret == caret &&
            g_ui.widgets[input].serial == serial && !g_ui.dirty,
        "duplicate focus preserves identity and caret without repainting");
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 0, "duplicate focus performs no layout");
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) == 0 && g_rec.count == 0 &&
            g_sync.cmd_count == commands,
        "duplicate focus performs no executor transaction");

    ToriRSChrome_SetChecked(&g_ui, check, 1);
    TEST_ASSERT(ToriRSChrome_Build(&g_ui) == 1, "a sibling state change rebuilds once");
    g_rec.count = 0;
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) == 1, "the sibling emits one delta");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_CHECKED) == 1 &&
            ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD) == 0 &&
            ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_REMOVE) == 0,
        "a node state patch does not replace its focused sibling subtree");
    TEST_ASSERT(
        g_ui.focus == input && g_ui.widgets[input].caret == caret &&
            g_ui.widgets[input].serial == serial,
        "sibling patches preserve native focus, caret, and identity");
}

static void
test_chrome_exec_maximum_clean_fast_path(void)
{
    int panel;
    int commands;
    int build_serial;
    uint32_t visits;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(
        &g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 320, "Maximum");
    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
        TEST_ASSERT(
            ToriRSChrome_Label(&g_ui, panel, "retained row") >= 0,
            "the maximum retained node budget is declarative");
    ToriRSChrome_Build(&g_ui);
    exec_settle();
    /* The recorder can intentionally truncate the initial catch-up; the sync
     * shadow itself still saw every fixed-array node. What is measured here is
     * the settled path after that one-time declaration. */
    g_rec.count = 0;
    commands = g_sync.cmd_count;
    build_serial = g_ui.build_serial;
    visits = g_sync.change_visit_count;
    for( int i = 0; i < 100; i++ )
        TEST_ASSERT(
            ToriRSChromeSync_Run(&g_sync, &g_ui) == 0,
            "a maximum-size clean tree remains quiet");
    TEST_ASSERT(
        g_rec.count == 0 && g_sync.cmd_count == commands &&
            g_ui.build_serial == build_serial &&
            g_sync.change_visit_count == visits,
        "100 clean maximum-size ticks inspect no node and perform no callback");
}

static void
test_chrome_exec_maximum_snapshot_fits_protocol(void)
{
    char const* legacy[TORIRS_CHROME_OPTION_MAX];
    struct ToriRSChromeSelectOptionInput
        structured[TORIRS_CHROME_SELECT_OPTIONS_MAX];
    struct CountExecFixture fixture = { 0 };
    struct ToriRSChromeExec exec;
    int widget = 0;
    int payload;

    for( int i = 0; i < TORIRS_CHROME_OPTION_MAX; i++ )
        legacy[i] = "entry";
    for( int i = 0; i < TORIRS_CHROME_SELECT_OPTIONS_MAX; i++ )
    {
        structured[i].value = "value";
        structured[i].label = "label";
        structured[i].enabled = 1;
        structured[i].detail = "";
    }
    ToriRSChrome_Init(&g_ui);
    for( int p = 0; p < TORIRS_CHROME_MAX_PANELS; p++ )
    {
        int const panel = ToriRSChrome_PanelAdd(
            &g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 320, "Full");
        for( int row = 0;
             row < TORIRS_CHROME_MAX_WIDGETS / TORIRS_CHROME_MAX_PANELS;
             row++, widget++ )
        {
            if( widget < TORIRS_CHROME_LEGACY_OPTIONS_TOTAL_MAX /
                             TORIRS_CHROME_OPTION_MAX )
                TEST_ASSERT(
                    ToriRSChrome_Dropdown(
                        &g_ui, panel, "Legacy", legacy,
                        TORIRS_CHROME_OPTION_MAX, 0) >= 0,
                    "the worst-case legacy option budget is retained");
            else if( widget == TORIRS_CHROME_LEGACY_OPTIONS_TOTAL_MAX /
                                  TORIRS_CHROME_OPTION_MAX )
                TEST_ASSERT(
                    ToriRSChrome_DropdownStructured(
                        &g_ui, panel, "Structured", structured,
                        TORIRS_CHROME_SELECT_OPTIONS_MAX, "value") >= 0,
                    "the structured pool coexists with the full legacy budget");
            else
                TEST_ASSERT(ToriRSChrome_Label(&g_ui, panel, "row") >= 0,
                    "the full widget budget is retained");
        }
    }
    memset(&exec, 0, sizeof(exec));
    exec.user = &fixture;
    exec.apply = count_exec_apply;
    TEST_ASSERT(ToriRSChromeSync_Init(&g_sync, &exec), "counting executor starts");
    payload = ToriRSChromeSync_Run(&g_sync, &g_ui);
    TEST_ASSERT(
        payload > 0 && fixture.commands == payload + 2 &&
            fixture.commands <= TORIRS_CHROME_PROTOCOL_COMMAND_MAX,
        "the maximum legal full snapshot fits one WEB/BROWSER transaction");
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

static void
test_chrome_exec_same_tick_recycle_order(void)
{
    int panel;
    int old_widget;
    int new_widget;
    int old_serial;
    int remove_at = -1;
    int add_at = -1;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(
        &g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 160, "Recycle");
    old_widget = ToriRSChrome_Label(&g_ui, panel, "old");
    old_serial = g_ui.widgets[old_widget].serial;
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    ToriRSChrome_WidgetRemove(&g_ui, old_widget);
    new_widget = ToriRSChrome_Checkbox(&g_ui, panel, "new", 1);
    TEST_ASSERT(new_widget == old_widget, "the replacement reuses the removed handle");
    TEST_ASSERT(
        g_ui.widgets[new_widget].serial != old_serial,
        "the replacement keeps a distinct retained identity");
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);

    for( int i = 0; i < g_rec.count; i++ )
    {
        if( g_rec.cmds[i].kind == TORIRS_CHROME_CMD_WIDGET_REMOVE &&
            g_rec.cmds[i].widget == old_widget )
            remove_at = i;
        if( g_rec.cmds[i].kind == TORIRS_CHROME_CMD_WIDGET_ADD &&
            g_rec.cmds[i].widget == new_widget )
            add_at = i;
    }
    TEST_ASSERT(remove_at >= 0 && add_at >= 0, "same-tick replacement emits both lifecycle ends");
    TEST_ASSERT(remove_at < add_at, "a recycled handle is removed before it is re-added");
}

static void
test_chrome_exec_reset_replacement_order(void)
{
    int panel;
    int widget;
    int close_at = -1;
    int open_at = -1;
    int add_at = -1;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(
        &g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 160, "Old page");
    widget = ToriRSChrome_Label(&g_ui, panel, "old row");
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    ToriRSChrome_Reset(&g_ui);
    TEST_ASSERT(
        ToriRSChrome_PanelAdd(
            &g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 160, "New page") == panel,
        "the replacement page reuses the panel handle");
    TEST_ASSERT(
        ToriRSChrome_Label(&g_ui, panel, "new row") == widget,
        "the replacement page reuses the widget handle");
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);

    for( int i = 0; i < g_rec.count; i++ )
    {
        if( g_rec.cmds[i].kind == TORIRS_CHROME_CMD_PANEL_CLOSE )
            close_at = i;
        if( g_rec.cmds[i].kind == TORIRS_CHROME_CMD_PANEL_OPEN )
            open_at = i;
        if( g_rec.cmds[i].kind == TORIRS_CHROME_CMD_WIDGET_ADD )
            add_at = i;
    }
    TEST_ASSERT(
        close_at >= 0 && open_at >= 0 && add_at >= 0,
        "a page replacement emits close, open, and child add");
    TEST_ASSERT(
        close_at < open_at && open_at < add_at,
        "a replaced page closes before its panel and child handles are recycled");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(
            &g_rec, TORIRS_CHROME_CMD_WIDGET_REMOVE) == 0,
        "panel close removes the old subtree without redundant child removals");
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

    /* Closing dominates child removals recorded in the same model tick. The
     * executor drops that subtree on CLOSE, so REMOVE-per-row would be pure
     * transport and DOM churn. */
    g_rec.count = 0;
    ToriRSChrome_PanelSetVisible(&g_ui, panel, 0);
    ToriRSChrome_WidgetRemove(&g_ui, g_ui.panels[panel].first_widget);
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_PANEL_CLOSE) == 1 &&
            ToriRSChromeRecorder_CountKind(
                &g_rec, TORIRS_CHROME_CMD_WIDGET_REMOVE) == 0,
        "same-tick panel close subsumes queued child removal");
}

/* Borrowed lists are restated in full, as copies. */
static void
test_chrome_exec_options(void)
{
    static char const* const first[] = { "alpha", "beta" };
    static char const* const second[] = { "one", "two", "three" };
    char mutable_first[32] = "red";
    char mutable_second[32] = "green";
    char const* mutable_options[] = { mutable_first, mutable_second };
    char mutable_tab_first[32] = "Overview";
    char mutable_tab_second[32] = "Details";
    char const* mutable_tabs[] = { mutable_tab_first, mutable_tab_second };
    int panel;
    int drop;
    int tabs;

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

    g_rec.count = 0;
    ToriRSChrome_DropdownSetOptions(&g_ui, drop, second, 3, 1);
    TEST_ASSERT(
        g_ui.change_count == 0 && ToriRSChromeSync_Run(&g_sync, &g_ui) == 0 &&
            g_rec.count == 0,
        "a compare-equal option setter queues and emits nothing");

    /* The array AND its character buffers retain their addresses while the
     * caller refills one label. Pointer/count/selection comparison alone
     * misses this and leaves a browser <select> showing stale text. */
    ToriRSChrome_DropdownSetOptions(&g_ui, drop, mutable_options, 2, 0);
    ToriRSChrome_Build(&g_ui);
    exec_settle();
    strcpy(mutable_second, "emerald");
    ToriRSChrome_DropdownSetOptions(&g_ui, drop, mutable_options, 2, 0);
    {
        int const pending = g_ui.change_pending_widget[drop];
        TEST_ASSERT(
            pending > 0 &&
                (g_ui.changes[pending - 1].flags &
                 TORIRS_CHROME_CHANGE_WIDGET_OPTIONS),
            "same-pointer option text mutation records an OPTIONS refresh");
    }
    ToriRSChrome_Build(&g_ui);
    g_rec.count = 0;
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    {
        int found = 0;
        for( int i = 0; i < g_rec.count; i++ )
            if( g_rec.cmds[i].kind == TORIRS_CHROME_CMD_WIDGET_OPTION &&
                g_rec.cmds[i].widget == drop && g_rec.cmds[i].value == 1 &&
                strcmp(g_rec.cmds[i].text, "emerald") == 0 )
                found = 1;
        TEST_ASSERT(
            ToriRSChromeRecorder_CountKind(
                &g_rec, TORIRS_CHROME_CMD_WIDGET_OPTION) == 2 && found,
            "the executor receives the refilled stable option buffers in full");
    }

    /* Tab strips borrow title buffers through the same field and therefore
     * need the same content snapshot, even though their setter has no selected
     * argument. */
    tabs = ToriRSChrome_Tabs(&g_ui, panel, mutable_tabs, 2, 0);
    ToriRSChrome_Build(&g_ui);
    exec_settle();
    strcpy(mutable_tab_first, "Summary");
    ToriRSChrome_TabsSetTitles(&g_ui, tabs, mutable_tabs, 2);
    {
        int const pending = g_ui.change_pending_widget[tabs];
        TEST_ASSERT(
            pending > 0 &&
                (g_ui.changes[pending - 1].flags &
                 TORIRS_CHROME_CHANGE_WIDGET_OPTIONS),
            "same-pointer tab-title mutation records an OPTIONS refresh");
    }
    ToriRSChrome_Build(&g_ui);
    g_rec.count = 0;
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    {
        int found = 0;
        for( int i = 0; i < g_rec.count; i++ )
            if( g_rec.cmds[i].kind == TORIRS_CHROME_CMD_WIDGET_OPTION &&
                g_rec.cmds[i].widget == tabs && g_rec.cmds[i].value == 0 &&
                strcmp(g_rec.cmds[i].text, "Summary") == 0 )
                found = 1;
        TEST_ASSERT(found, "the executor receives a refilled stable tab title");
    }
}

static void
test_chrome_exec_structured_options(void)
{
    static struct ToriRSChromeSelectOptionInput const options[] = {
        { "auto", "Same|label", 1, "Uses the lane default" },
        { "missing/frame", "Same|label", 0, "Provider is not installed" },
        { "ready/frame", "Ready", 1, "Available now" },
    };
    struct ToriRSChromeIntent intent;
    struct ToriRSChromeCmd const* header;
    struct ToriRSChromeCmd const* missing = NULL;
    struct ToriRSChromeCmd const* selected;
    int panel;
    int drop;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(
        &g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 240, "Structured");
    drop = ToriRSChrome_DropdownStructured(
        &g_ui, panel, "Gameframe", options, 3, "missing/frame");
    TEST_ASSERT(drop >= 0, "a structured dropdown is retained");
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);

    header = ToriRSChromeRecorder_Find(
        &g_rec, TORIRS_CHROME_CMD_WIDGET_OPTIONS, drop);
    selected = ToriRSChromeRecorder_Find(
        &g_rec, TORIRS_CHROME_CMD_WIDGET_SELECTED, drop);
    for( int i = 0; i < g_rec.count; i++ )
        if( g_rec.cmds[i].kind == TORIRS_CHROME_CMD_WIDGET_OPTION &&
            g_rec.cmds[i].widget == drop && g_rec.cmds[i].value == 1 )
            missing = &g_rec.cmds[i];
    TEST_ASSERT(
        header && header->x == 1 && header->value == 3,
        "the option-list boundary marks the structured representation");
    TEST_ASSERT(
        missing && strcmp(missing->text, "missing/frame") == 0 &&
            strcmp(missing->label, "Same|label") == 0 && !missing->x &&
            strcmp(missing->detail, "Provider is not installed") == 0,
        "stable value, duplicate delimiter label, disabled state, and detail cross separately");
    TEST_ASSERT(
        selected && selected->value == 1 &&
            strcmp(selected->text, "missing/frame") == 0 &&
            strcmp(ToriRSChrome_DropdownSelectedValue(&g_ui, drop), "missing/frame") == 0,
        "a disabled missing choice may remain the selected retained value");

    exec_settle();
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_PICK;
    intent.panel = panel;
    intent.widget = drop;
    intent.value = 1;
    snprintf(intent.text, sizeof(intent.text), "%s", "missing/frame");
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);
    TEST_ASSERT(
        ToriRSChromeSync_Pump(&g_sync, &g_ui) == 0 &&
            ToriRSChrome_TakeActivated(&g_ui) < 0,
        "an executor cannot select a disabled structured option");

    intent.value = 2;
    snprintf(intent.text, sizeof(intent.text), "%s", "stale/index-value");
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);
    TEST_ASSERT(
        ToriRSChromeSync_Pump(&g_sync, &g_ui) == 0,
        "a stale value cannot retarget a reused option index");

    snprintf(intent.text, sizeof(intent.text), "%s", "ready/frame");
    ToriRSChromeRecorder_PushIntent(&g_rec, &intent);
    TEST_ASSERT(
        ToriRSChromeSync_Pump(&g_sync, &g_ui) == 1 &&
            ToriRSChrome_TakeActivated(&g_ui) == drop &&
            strcmp(ToriRSChrome_DropdownSelectedValue(&g_ui, drop), "ready/frame") == 0,
        "an enabled pick lands as its stable value rather than its label");
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
    TEST_ASSERT(
        ToriRSChrome_TakeActivated(&g_ui) == check,
        "a native result-state toggle reaches the active plugin drain");

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

static void
test_chrome_exec_intent_burst_is_explicit(void)
{
    struct ToriRSChromeMirror mirror;
    struct ToriRSChromeIntent intents[TORIRS_CHROME_MIRROR_INTENTS];

    ToriRSChromeMirror_Init(&mirror);
    ToriRSChromeMirror_PushToggle(&mirror, 1, 2, 1);
    TEST_ASSERT(
        mirror.intent_count == 1 && mirror.intents[0].kind == TORIRS_CHROME_INTENT_TOGGLE,
        "one checkbox gesture occupies one intent and carries activation itself");
    while( mirror.intent_count < TORIRS_CHROME_MIRROR_INTENTS )
        ToriRSChromeMirror_PushActivate(&mirror, 1, 2);
    ToriRSChromeMirror_PushActivate(&mirror, 1, 2);
    TEST_ASSERT(
        ToriRSChromeMirror_TakeIntentOverflow(&mirror) == 1 &&
            ToriRSChromeMirror_TakeIntentOverflow(&mirror) == 0,
        "a full intent queue exposes one explicit loss indication");
    TEST_ASSERT(
        ToriRSChromeMirror_Poll(
            &mirror, intents, TORIRS_CHROME_MIRROR_INTENTS) ==
            TORIRS_CHROME_MIRROR_INTENTS,
        "the accepted prefix remains drainable after an overflow report");
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
        ToriRSChromeIntent_Apply(&g_ui, &intent) == 0 && g_ui.panels[panel].visible == 0,
        "a duplicated close is a settled no-op");

    /* An executor still bound is told, once, and the rows go with it. */
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_PANEL_CLOSE) == 1,
        "the presentation hears the panel close");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_REMOVE) == 0,
        "without a REMOVE per row");

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
        TEST_ASSERT(
            ToriRSChromeSync_Run(&g_sync, &g_ui) == 0 && g_ui.change_count == 0,
            "and acknowledges retained changes without an executor transaction");
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
    second.count = 0;
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) == 0 && second.count == 0,
        "rebind catch-up is exactly one snapshot");
}

/*
 * A colour row across the seam.
 *
 * It carries no command of its own -- the value rides WIDGET_SELECTED, the hex
 * rides WIDGET_TEXT, and "the axis popup is open" rides WIDGET_CHECKED -- so
 * what this pins is that a web executor can rebuild the whole control from
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
            "and says so as WIDGET_CHECKED, which is how a web executor knows to "
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
 * Two things a web executor is broken by and nothing on screen would show:
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

    /* Scrolling the box is a SELECTED mutation, and it must not be mistaken
     * for a dropdown's selected-option index. */
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

static void
test_chrome_exec_custom_resizes_in_place(void)
{
    struct ToriRSChromeCmd const* height;
    int panel;
    int custom;

    exec_reset();
    ToriRSChrome_SetScale(&g_ui, 2);
    panel = ToriRSChrome_PanelAdd(
        &g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 400, "Custom");
    custom = ToriRSChrome_Custom(
        &g_ui, panel, "Chart", 150 * ToriRSChrome_Scale(&g_ui));
    ToriRSChrome_Build(&g_ui);
    exec_settle();

    ToriRSChrome_SetCustomHeight(&g_ui, custom, 220 * ToriRSChrome_Scale(&g_ui));
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) > 0, "the resize says something");

    height = ToriRSChromeRecorder_Find(
        &g_rec, TORIRS_CHROME_CMD_WIDGET_HEIGHT, custom);
    TEST_ASSERT(height != NULL, "a resized well states its height");
    TEST_ASSERT(
        height && height->h == 220,
        "in the same logical units the add used");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_REMOVE) == 0 &&
            ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD) == 0,
        "and is NOT re-declared -- no remove, no add");

    exec_settle();
    ToriRSChrome_SetCustomHeight(&g_ui, custom, 220 * ToriRSChrome_Scale(&g_ui));
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_HEIGHT) == 0,
        "restating the height it already has says nothing");
}


static void
test_chrome_exec_custom_shape(void)
{
    struct ToriRSChromeCmd const* add;
    struct ToriRSChromeIntent intent;
    int panel;
    int custom;

    exec_reset();
    ToriRSChrome_SetScale(&g_ui, 2);
    panel = ToriRSChrome_PanelAdd(
        &g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 400, "Custom");
    custom = ToriRSChrome_Custom(
        &g_ui, panel, "Chart", 150 * ToriRSChrome_Scale(&g_ui));
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);

    add = ToriRSChromeRecorder_Find(
        &g_rec, TORIRS_CHROME_CMD_WIDGET_ADD, custom);
    TEST_ASSERT(add && add->value == TORIRS_CHROME_W_CUSTOM, "custom kind crosses");
    TEST_ASSERT(
        add && add->h == 150,
        "custom height crosses in logical units, independent of display scale");
    TEST_ASSERT(add && strcmp(add->label, "Chart") == 0, "custom accessible label crosses");

    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE;
    intent.panel = panel;
    intent.widget = custom;
    intent.x = 7;
    intent.y = 11;
    intent.selection_generation = 23;
    intent.widget_serial = 42;
    TEST_ASSERT(
        ToriRSChromeIntent_Apply(&g_ui, &intent),
        "a bounded native custom activation applies to the chrome model");
    {
        int x = -1;
        int y = -1;
        uint32_t generation = 0;
        uint32_t serial = 0;
        TEST_ASSERT(ToriRSChrome_TakeActivated(&g_ui) == custom, "custom handle is drained");
        TEST_ASSERT(
            ToriRSChrome_ActivationWasCustom(
                &g_ui, &x, &y, &generation, &serial) && x == 7 && y == 11 &&
                generation == 23 && serial == 42,
            "native custom coordinates and semantic fences survive the intent seam");
    }
    intent.x = 10000;
    TEST_ASSERT(
        !ToriRSChromeIntent_Apply(&g_ui, &intent),
        "an out-of-bounds native custom activation is dropped before dispatch");
}

static void
test_chrome_exec_external_intent_serial(void)
{
    struct ToriRSChromeCmd const* add;
    int panel;
    int widget;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(
        &g_ui, TORIRS_CHROME_PANEL_WINDOW, 0, 0, 200, "Semantic");
    widget = ToriRSChrome_Button(&g_ui, panel, "Run");
    ToriRSChrome_WidgetSetIntentSerial(&g_ui, widget, 7001);
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    add = ToriRSChromeRecorder_Find(
        &g_rec, TORIRS_CHROME_CMD_WIDGET_ADD, widget);
    TEST_ASSERT(add && add->serial == 7001,
        "WIDGET_ADD carries the semantic serial bound by the host");

    exec_settle();
    ToriRSChrome_WidgetSetIntentSerial(&g_ui, widget, 7002);
    ToriRSChrome_Build(&g_ui);
    ToriRSChromeSync_Run(&g_sync, &g_ui);
    add = ToriRSChromeRecorder_Find(
        &g_rec, TORIRS_CHROME_CMD_WIDGET_ADD, widget);
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(
            &g_rec, TORIRS_CHROME_CMD_WIDGET_REMOVE) == 1 &&
            add && add->serial == 7002,
        "rebinding identity removes/adds the node so old DOM listeners stay stale");
}

struct RailExecFixture
{
    int snapshots;
    int icons;
    int fail_snapshot;
    int fail_icon;
    struct ToriRSChromeRailSnapshot snapshot;
    struct ToriRSChromeRailIcon icon;
    struct ToriRSChromeRailIntent intents[4];
    int intent_count;
};

static int
rail_fixture_sync(void* user, struct ToriRSChromeRailSnapshot const* snapshot)
{
    struct RailExecFixture* fixture = user;
    if( fixture->fail_snapshot )
    {
        fixture->fail_snapshot = 0;
        return 0;
    }
    fixture->snapshots++;
    fixture->snapshot = *snapshot;
    return 1;
}

static int
rail_fixture_icon(void* user, struct ToriRSChromeRailIcon const* icon)
{
    struct RailExecFixture* fixture = user;
    if( fixture->fail_icon )
    {
        fixture->fail_icon = 0;
        return 0;
    }
    fixture->icons++;
    fixture->icon = *icon;
    return 1;
}

static int
rail_fixture_poll(void* user, struct ToriRSChromeRailIntent* out, int max)
{
    struct RailExecFixture* fixture = user;
    int const count = fixture->intent_count < max ? fixture->intent_count : max;
    for( int i = 0; i < count; i++ )
        out[i] = fixture->intents[i];
    for( int i = count; i < fixture->intent_count; i++ )
        fixture->intents[i - count] = fixture->intents[i];
    fixture->intent_count -= count;
    return count;
}

/*
 * A page BOUNDARY has to restate the whole page, not drain property commands
 * recorded against the page that was just discarded.
 *
 * The shared plugin shell mounts one page at a time, and a page-retaining
 * executor drops its DOM when the selection moves -- it must, because a
 * mutation authored for one page cannot patch another's. The delivered shadow
 * still describes the discarded page at that moment, so a plain Run sees the
 * similar roots as already mounted and sends only queued text properties.
 *
 * Where the two pages are structurally alike -- the same rows in the same
 * order, which is the ordinary case for a family of plugin readouts -- that
 * queued journal carries no WIDGET_ADD at all. The executor receives three
 * label changes and no controls, and mounts an EMPTY pane. Switching straight
 * from one plugin's page to another's is exactly the gesture that produces it,
 * which is why the two pages below differ only in their text.
 */
static void
test_chrome_exec_invalidate_restates_the_page(void)
{
    int panel;
    int label;
    int button;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 72, 320, "XP Tracker");
    label = ToriRSChrome_Label(&g_ui, panel, "XP gained: 0");
    button = ToriRSChrome_Button(&g_ui, panel, "Reset all");
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(exec_settle() > 0, "the first page is stated in full");

    /* The SECOND plugin's page: same panel, same two rows, same kinds, and
     * only the words differ. */
    ToriRSChrome_SetText(&g_ui, label, "Kills: 0");
    ToriRSChrome_SetText(&g_ui, button, "Clear all loot");
    ToriRSChrome_PanelSetTitle(&g_ui, panel, "Loot Tracker");
    ToriRSChrome_Build(&g_ui);

    /* Without the invalidate this is a handful of text patches and nothing
     * else -- which is the bug, stated as an assertion so the fix below is
     * measuring something real rather than restating the obvious. */
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) > 0, "a text-only change still says something");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD) == 0,
        "and it carries no control at all, because none of them moved");
    g_rec.count = 0;

    /* Now the page boundary, announced. Nothing about the BINDING changed --
     * the window stays open -- so the next Run must restate the page over an
     * executor that has thrown its copy away. */
    ToriRSChromeSync_Invalidate(&g_sync, 77);
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) > 0, "an invalidated shadow restates");
    TEST_ASSERT(
        ToriRSChromeRecorder_CountKind(&g_rec, TORIRS_CHROME_CMD_PANEL_OPEN) == 1,
        "the panel is opened again");
    TEST_ASSERT(
        ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD, label) != NULL &&
            ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_WIDGET_ADD, button) != NULL,
        "and EVERY control is added, not only the ones whose text moved");
    g_rec.count = 0;

    /* And it is a one-shot: the frame after a restatement is quiet again, so
     * a page boundary costs one transaction rather than turning the whole
     * pane into a per-frame rebuild. */
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) == 0,
        "the frame after a restatement is quiet again");
}

/*
 * The boundary is ANNOUNCED, on the stream that carries the page.
 *
 * An executor that retains a page has to drop it, and the only orderings that
 * work are the ones where the drop and the replacement are the same
 * transaction. Inferring the boundary from a selection generation arriving on
 * the rail channel put the two on different frames and produced a blank pane
 * whichever way they fell -- reset first and the restatement is a patch to a
 * page that is gone, restate first and the reset wipes it while the shadow
 * claims it is still there, so the next transaction is empty.
 */
static void
test_chrome_exec_restate_is_announced(void)
{
    int panel;
    struct ToriRSChromeCmd const* begin;

    exec_reset();
    panel = ToriRSChrome_PanelAdd(&g_ui, TORIRS_CHROME_PANEL_WINDOW, 8, 72, 320, "A");
    ToriRSChrome_Label(&g_ui, panel, "row");
    ToriRSChrome_Build(&g_ui);

    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) > 0, "the first page is stated");
    begin = ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_SYNC_BEGIN, -1);
    TEST_ASSERT(begin != NULL, "and it is bracketed");
    TEST_ASSERT(
        begin->value == 1,
        "the FIRST transaction on a binding is itself a restatement -- the "
        "executor holds nothing and everything about to arrive is new");
    g_rec.count = 0;

    ToriRSChrome_Label(&g_ui, panel, "another row");
    ToriRSChrome_Build(&g_ui);
    TEST_ASSERT(ToriRSChromeSync_Run(&g_sync, &g_ui) > 0, "an ordinary change says something");
    begin = ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_SYNC_BEGIN, -1);
    TEST_ASSERT(
        begin && begin->value == 0,
        "but an ordinary change is a PATCH and says so, or every edit would "
        "tear the page down and rebuild it");
    g_rec.count = 0;

    /*
     * The case that was blank: the boundary is declared on a frame where the
     * MODEL does not otherwise move, and the transaction announcing it is the
     * next one to happen -- not the next frame. A flag that needed a
     * transaction to already exist would be dropped exactly here.
     */
    ToriRSChromeSync_Invalidate(&g_sync, 77);
    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) > 0,
        "an invalidation produces a transaction even with a clean model");
    begin = ToriRSChromeRecorder_Find(&g_rec, TORIRS_CHROME_CMD_SYNC_BEGIN, -1);
    TEST_ASSERT(begin && begin->value == 1, "and that transaction is marked a restatement");
    g_rec.count = 0;

    TEST_ASSERT(
        ToriRSChromeSync_Run(&g_sync, &g_ui) == 0, "then it settles");
}

static void
test_chrome_exec_retained_rail(void)
{
    struct RailExecFixture fixture;
    struct ToriRSChromeExec exec;
    struct ToriRSChromeRailSync sync;
    struct ToriRSChromeRailSnapshot snapshot;
    struct ToriRSChromeRailIcon icon;
    struct ToriRSChromeRailIntent intent;

    memset(&fixture, 0, sizeof(fixture));
    memset(&exec, 0, sizeof(exec));
    exec.user = &fixture;
    exec.rail_sync = rail_fixture_sync;
    exec.rail_icon = rail_fixture_icon;
    exec.rail_poll = rail_fixture_poll;
    ToriRSChromeRailSync_Init(&sync);
    ToriRSChromeRailSnapshot_Init(&snapshot);
    TEST_ASSERT(
        !ToriRSChromeRailSnapshot_IncludesPlugin(1, 1) &&
            ToriRSChromeRailSnapshot_IncludesPlugin(1, 0) &&
            !ToriRSChromeRailSnapshot_IncludesPlugin(0, 0),
        "managed settings stay under Manage while standalone pages enter the rail");
    snapshot.registry_revision = 9;
    snapshot.selection_generation = 4;
    snapshot.page_generation = 7;
    snapshot.selected_entry = -2;
    snapshot.expanded = 1;
    TEST_ASSERT(
        ToriRSChromeRailSnapshot_AddManage(&snapshot, -2, "Manage Plugins"),
        "the permanent Manage destination accepts its reserved negative key");
    TEST_ASSERT(
        ToriRSChromeRailSnapshot_Add(
            &snapshot, 3, "Ground Items", "ground.png", 360, "12", 1),
        "a plugin destination is copied into the shared snapshot");
    fixture.fail_snapshot = 1;
    TEST_ASSERT(
        ToriRSChromeRailSync_Run(&sync, &exec, &snapshot) == 0 &&
            fixture.snapshots == 0,
        "a rejected rail snapshot does not advance its retained shadow");
    memset(&icon, 0, sizeof(icon));
    icon.plugin_index = 3;
    icon.revision = 5;
    icon.width = 2;
    icon.height = 1;
    TEST_ASSERT(
        ToriRSChromeRailSync_Icon(&sync, &exec, &icon) == 0 && fixture.icons == 0,
        "icons wait until their owning rail snapshot is accepted");
    TEST_ASSERT(
        ToriRSChromeRailSync_Run(&sync, &exec, &snapshot) == 1 &&
            fixture.snapshots == 1 && fixture.snapshot.entry_count == 2 &&
            fixture.snapshot.entries[0].kind == TORIRS_CHROME_RAIL_ENTRY_MANAGE &&
            strcmp(fixture.snapshot.entries[1].icon_asset, "ground.png") == 0 &&
            strcmp(fixture.snapshot.entries[1].badge, "12") == 0 &&
            fixture.snapshot.entries[1].attention,
        "one retained snapshot carries Manage and all plugin rail metadata");
    TEST_ASSERT(
        ToriRSChromeRailSync_Run(&sync, &exec, &snapshot) == 0 &&
            fixture.snapshots == 1,
        "an idle rail is not rebuilt every frame");
    snapshot.expanded = 0;
    TEST_ASSERT(
        ToriRSChromeRailSync_Run(&sync, &exec, &snapshot) == 1,
        "collapse republishes selection state without changing the registry");

    memset(&icon, 0, sizeof(icon));
    icon.plugin_index = 3;
    icon.revision = 5;
    icon.width = 2;
    icon.height = 1;
    icon.argb[0] = 0xFF112233u;
    icon.argb[1] = 0xFF445566u;
    TEST_ASSERT(
        ToriRSChromeRailSync_Icon(&sync, &exec, &icon) == 1 &&
            fixture.icons == 1 && fixture.icon.argb[1] == 0xFF445566u,
        "authored icon pixels cross in a separate bounded copied payload");
    TEST_ASSERT(
        ToriRSChromeRailSync_Icon(&sync, &exec, &icon) == 0 && fixture.icons == 1,
        "an unchanged icon revision stays cached");
    icon.revision++;
    icon.width = icon.height = 0;
    TEST_ASSERT(
        ToriRSChromeRailSync_Icon(&sync, &exec, &icon) == 1 &&
            fixture.icon.width == 0,
        "a failed icon revision explicitly selects presenter fallback");

    memset(&fixture.intents[0], 0, sizeof(fixture.intents[0]));
    fixture.intents[0].kind = TORIRS_CHROME_RAIL_INTENT_SELECT;
    fixture.intents[0].plugin_index = -2;
    fixture.intents[0].selection_generation = 4;
    fixture.intent_count = 1;
    TEST_ASSERT(
        ToriRSChromeRail_Poll(&exec, &intent, 1) == 1 &&
            intent.plugin_index == -2 && intent.selection_generation == 4,
        "rail selection queues a destination key and generation, not a boolean");

    ToriRSChromeRailSnapshot_Init(&snapshot);
    TEST_ASSERT(ToriRSChromeRailSnapshot_AddManage(&snapshot, -2, "Manage Plugins"),
        "capacity reserves the permanent destination");
    for( int plugin = 0; plugin < 32; plugin++ )
        TEST_ASSERT(
            ToriRSChromeRailSnapshot_Add(
                &snapshot, plugin, "Plugin", "", 320, "", 0),
            "all PluginHost slots fit beside Manage");
    TEST_ASSERT(snapshot.entry_count == 33,
        "Manage plus all 32 plugins fills, rather than truncates, the rail");
}

void
test_chrome_exec(void)
{
    printf("TEST: chrome executor seam (deltas / removal order / intents)\n");

    test_chrome_exec_internal_fallback();
    test_chrome_exec_catchup();
    test_chrome_exec_quiet_frame();
    test_chrome_exec_coalesces_setter_burst();
    test_chrome_exec_indexed_coalescing();
    test_chrome_exec_records_dependent_properties();
    test_chrome_exec_queue_loss_snapshots_once();
    test_chrome_exec_delivery_loss_snapshots_once();
    test_chrome_exec_commit_failure_retains_journal();
    test_chrome_exec_retains_focused_node();
    test_chrome_exec_maximum_clean_fast_path();
    test_chrome_exec_maximum_snapshot_fits_protocol();
    test_chrome_exec_remove();
    test_chrome_exec_same_tick_recycle_order();
    test_chrome_exec_reset_replacement_order();
    test_chrome_exec_panel_close();
    test_chrome_exec_options();
    test_chrome_exec_structured_options();
    test_chrome_exec_intents();
    test_chrome_exec_intent_burst_is_explicit();
    test_chrome_exec_close_reported();
    test_chrome_exec_refused();
    test_chrome_exec_row_order();
    test_chrome_exec_lost();
    test_chrome_exec_colorpick();
    test_chrome_exec_check_style();
    test_chrome_exec_textarea();
    test_chrome_exec_custom_shape();
    test_chrome_exec_custom_resizes_in_place();
    test_chrome_exec_external_intent_serial();
    test_chrome_exec_retained_rail();
    test_chrome_exec_invalidate_restates_the_page();
    test_chrome_exec_restate_is_announced();
}
