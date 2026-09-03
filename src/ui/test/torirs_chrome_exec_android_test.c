#include "ui/torirs_chrome_exec.h"
#include "platform/platform_android.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECT(condition, message)                                                     \
    do                                                                                 \
    {                                                                                  \
        if( !(condition) )                                                             \
        {                                                                              \
            fprintf(stderr, "FAIL: %s\n", (message));                                \
            exit(1);                                                                   \
        }                                                                              \
    } while( 0 )

static int bridge_available;
static int batch_calls;
static int batch_count;
static int end_calls;
static struct ToriRSChromeCmd batch[16];
static int custom_calls;
static struct ToriRSChromeCustomFrame custom_frame;
static int rail_calls;
static int rail_icon_calls;
static struct ToriRSChromeRailSnapshot rail_snapshot;
static struct ToriRSChromeRailIcon rail_icon;

int
PlatformAndroidJni_ChromeAvailable(void)
{
    return bridge_available;
}

void
PlatformAndroidJni_ApplyChromeBatch(struct ToriRSChromeCmd const* commands, int count)
{
    batch_calls++;
    batch_count = count;
    if( count > (int)(sizeof(batch) / sizeof(batch[0])) )
        count = (int)(sizeof(batch) / sizeof(batch[0]));
    memcpy(batch, commands, (size_t)count * sizeof(batch[0]));
}

void
PlatformAndroidJni_ApplyChromeCustom(
    struct ToriRSChromeCustomFrame const* frame)
{
    custom_calls++;
    custom_frame = *frame;
}

void
PlatformAndroidJni_ApplyChromeRail(
    struct ToriRSChromeRailSnapshot const* snapshot)
{
    rail_calls++;
    rail_snapshot = *snapshot;
}

void
PlatformAndroidJni_ApplyChromeRailIcon(
    struct ToriRSChromeRailIcon const* icon)
{
    rail_icon_calls++;
    rail_icon = *icon;
}

void
PlatformAndroidJni_EndChrome(void)
{
    end_calls++;
}

static struct ToriRSChromeCmd
command(int kind, int panel, int widget)
{
    struct ToriRSChromeCmd out;

    memset(&out, 0, sizeof(out));
    out.kind = kind;
    out.panel = panel;
    out.widget = widget;
    out.tab = -1;
    return out;
}

static void
apply(struct ToriRSChromeExec* exec, struct ToriRSChromeCmd cmd)
{
    exec->apply(exec->user, &cmd);
}

int
main(void)
{
    struct ToriRSChromeExec exec = ToriRSChromeExec_Android(NULL, NULL, NULL);
    struct ToriRSChromeIntent intents[4];
    struct ToriRSChromeCmd cmd;
    struct ToriRSChromeRailSnapshot snapshot;
    struct ToriRSChromeRailIcon icon;
    struct ToriRSChromeRailIntent rail_intents[4];
    int expanded = -1;

    EXPECT(!exec.is_surface, "Android WebView consumes semantic commands");
    EXPECT(exec.begin && exec.apply && exec.poll && exec.end, "complete native vtable");
    EXPECT(exec.rail_sync && exec.rail_icon && exec.rail_poll && exec.custom_present &&
            !exec.present && !exec.surface_size && !exec.surface_input,
        "persistent rail, DOM commands, and custom bitmap hooks are complete");
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.registry_revision = 3;
    snapshot.selection_generation = 7;
    snapshot.page_generation = 11;
    snapshot.selected_entry = -2;
    snapshot.entry_count = 2;
    snapshot.entries[0].kind = TORIRS_CHROME_RAIL_ENTRY_MANAGE;
    snapshot.entries[0].plugin_index = -2;
    snprintf(snapshot.entries[0].title, sizeof(snapshot.entries[0].title), "%s", "Manage Plugins");
    snapshot.entries[1].kind = TORIRS_CHROME_RAIL_ENTRY_PLUGIN;
    snapshot.entries[1].plugin_index = 4;
    snapshot.entries[1].attention = 1;
    snprintf(snapshot.entries[1].title, sizeof(snapshot.entries[1].title), "%s", "Loot Tracker");
    snprintf(snapshot.entries[1].icon_asset, sizeof(snapshot.entries[1].icon_asset), "%s", "loot.png");
    snprintf(snapshot.entries[1].badge, sizeof(snapshot.entries[1].badge), "%s", "8");
    exec.rail_sync(exec.user, &snapshot);
    EXPECT(rail_calls == 1 && rail_snapshot.entry_count == 2 &&
            rail_snapshot.entries[0].plugin_index == -2 &&
            strcmp(rail_snapshot.entries[1].icon_asset, "loot.png") == 0,
        "rail snapshot is copied before the page executor begins");
    memset(&icon, 0, sizeof(icon));
    icon.plugin_index = 4;
    icon.revision = 2;
    icon.width = icon.height = 1;
    icon.argb[0] = 0xFFAABBCCu;
    exec.rail_icon(exec.user, &icon);
    EXPECT(rail_icon_calls == 1 && rail_icon.argb[0] == 0xFFAABBCCu,
        "revisioned authored icon pixels are copied separately");
    EXPECT(!exec.begin(exec.user), "executor refuses before ClientActivity bridge exists");

    bridge_available = 1;
    EXPECT(exec.begin(exec.user), "executor begins with a live Activity bridge");

    apply(&exec, command(TORIRS_CHROME_CMD_SYNC_BEGIN, -1, -1));
    cmd = command(TORIRS_CHROME_CMD_PANEL_OPEN, 2, -1);
    snprintf(cmd.text, sizeof(cmd.text), "%s", "Plugins");
    apply(&exec, cmd);
    cmd = command(TORIRS_CHROME_CMD_WIDGET_ADD, 2, 7);
    cmd.value = TORIRS_CHROME_W_CHECKBOX;
    cmd.serial = 42;
    snprintf(cmd.label, sizeof(cmd.label), "%s", "Ground items");
    apply(&exec, cmd);
    cmd = command(TORIRS_CHROME_CMD_WIDGET_ADD, 2, 8);
    cmd.value = TORIRS_CHROME_W_CUSTOM;
    cmd.serial = 34;
    cmd.h = 120;
    apply(&exec, cmd);
    cmd = command(TORIRS_CHROME_CMD_WIDGET_ADD, 2, 9);
    cmd.value = TORIRS_CHROME_W_TEXTINPUT;
    cmd.serial = 43;
    apply(&exec, cmd);
    EXPECT(batch_calls == 0, "no JNI delivery before SYNC_END");
    apply(&exec, command(TORIRS_CHROME_CMD_SYNC_END, -1, -1));
    EXPECT(batch_calls == 1 && batch_count == 4, "one JNI delivery for one transaction");
    EXPECT(batch[0].kind == TORIRS_CHROME_CMD_PANEL_OPEN, "markers stay native-side");
    EXPECT(strcmp(batch[1].label, "Ground items") == 0 && batch[1].serial == 42,
        "command strings and semantic serial are copied for the DOM envelope");

    {
        uint32_t pixel = 0xFFAABBCCu;
        struct ToriRSChromeCustomFrame frame;
        memset(&frame, 0, sizeof(frame));
        frame.panel = 2;
        frame.widget = 8;
        frame.selection_generation = 11;
        frame.widget_serial = 34;
        frame.scale_milli = 1000;
        frame.width = frame.height = frame.stride = 1;
        frame.argb = &pixel;
        exec.custom_present(exec.user, &frame);
        EXPECT(custom_calls == 1 && custom_frame.selection_generation == 11 &&
                custom_frame.widget_serial == 34,
            "custom bitmap crosses with page and widget identity");
        PlatformAndroidChrome_PostIntent(
            TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE, 2, 8, 0, "", 0, 0, 11, 34);
        EXPECT(exec.poll(exec.user, intents, 4) == 1 &&
                intents[0].kind == TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE &&
                intents[0].selection_generation == 11 && intents[0].widget_serial == 34,
            "typed WebView custom intent retains both identity fences");
        PlatformAndroidChrome_PostIntent(
            TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE, 2, 8, 0, "", 1, 0, 11, 34);
        PlatformAndroidChrome_PostIntent(
            TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE, 2, 8, 0, "", 0, 0, 10, 34);
        PlatformAndroidChrome_PostIntent(
            TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE, 2, 8, 0, "", 0, 0, 11, 35);
        EXPECT(exec.poll(exec.user, intents, 4) == 0,
            "out-of-bounds and stale custom activations are rejected");
        frame.width = frame.height = frame.stride = 4096;
        exec.custom_present(exec.user, &frame);
        EXPECT(custom_calls == 1, "oversized custom bitmap is rejected before JNI");
    }

    apply(&exec, command(TORIRS_CHROME_CMD_SYNC_BEGIN, -1, -1));
    apply(&exec, command(TORIRS_CHROME_CMD_SYNC_END, -1, -1));
    EXPECT(batch_calls == 1, "quiet sync does not post an empty UI Runnable");

    PlatformAndroidChrome_PostIntent(
        TORIRS_CHROME_INTENT_TOGGLE, 2, 7, 1, "ignored", 0, 0, 11, 42);
    EXPECT(exec.poll(exec.user, intents, 1) == 1, "toggle reaches frame thread");
    EXPECT(
        intents[0].kind == TORIRS_CHROME_INTENT_TOGGLE && intents[0].value == 1,
        "toggle carries result state");
    EXPECT(exec.poll(exec.user, intents, 4) == 1, "toggle activation remains queued");
    EXPECT(
        intents[0].kind == TORIRS_CHROME_INTENT_ACTIVATE && intents[0].widget == 7,
        "toggle is paired with row activation");

    PlatformAndroidChrome_PostIntent(
        TORIRS_CHROME_INTENT_TEXT,
        2,
        9,
        0,
        "abyssal whip\ntwisted bow",
        0,
        0,
        11,
        43);
    EXPECT(exec.poll(exec.user, intents, 4) == 1, "text intent reaches frame thread");
    EXPECT(
        strcmp(intents[0].text, "abyssal whip\ntwisted bow") == 0,
        "text is copied across the UI-thread boundary");

    PlatformAndroidChrome_PostIntent(
        TORIRS_CHROME_INTENT_ACTIVATE, 2, 7, 0, "", 0, 0, 11, 41);
    EXPECT(exec.poll(exec.user, intents, 4) == 0,
        "a recycled generic widget serial is rejected before model mutation");
    PlatformAndroidChrome_PostIntent(
        TORIRS_CHROME_INTENT_ACTIVATE, 2, 7, 0, "", 0, 0, 11, 42);
    snapshot.page_generation = 12;
    exec.rail_sync(exec.user, &snapshot);
    EXPECT(exec.poll(exec.user, intents, 4) == 0,
        "a page-generation transition clears already queued widget actions");
    PlatformAndroidChrome_PostIntent(
        TORIRS_CHROME_INTENT_ACTIVATE, 2, 7, 0, "", 0, 0, 11, 42);
    EXPECT(exec.poll(exec.user, intents, 4) == 0,
        "the old page cannot enqueue after the generation transition");

    /* 8,193 deltas exceed the executor's fixed transaction capacity. None of
     * them may reach Java; the following complete transaction still must. */
    apply(&exec, command(TORIRS_CHROME_CMD_SYNC_BEGIN, -1, -1));
    for( int i = 0; i < 8193; i++ )
        apply(&exec, command(TORIRS_CHROME_CMD_WIDGET_TEXT, 2, 7));
    apply(&exec, command(TORIRS_CHROME_CMD_SYNC_END, -1, -1));
    EXPECT(batch_calls == 1, "overflow drops the entire transaction");
    apply(&exec, command(TORIRS_CHROME_CMD_SYNC_BEGIN, -1, -1));
    apply(&exec, command(TORIRS_CHROME_CMD_PANEL_TITLE, 2, -1));
    apply(&exec, command(TORIRS_CHROME_CMD_SYNC_END, -1, -1));
    EXPECT(batch_calls == 2 && batch_count == 1, "next complete transaction recovers");

    PlatformAndroidChrome_PostIntent(
        TORIRS_CHROME_INTENT_CLOSE, 2, -1, 0, "", 0, 0, 11, 0);
    PlatformAndroidChrome_RequestExpanded(1);
    PlatformAndroidChrome_PostRailSelect(4, 7);
    PlatformAndroidChrome_PostRailSelect(-2, 7);
    PlatformAndroidChrome_PostRailLayout(7, 320, 500, 2000, 1, 1, 0);
    exec.end(exec.user);
    EXPECT(end_calls == 1, "end tears down the Activity presenter");
    EXPECT(exec.poll(exec.user, intents, 4) == 0, "end clears queued UI actions");
    PlatformAndroidChrome_PostIntent(
        TORIRS_CHROME_INTENT_CLOSE, 2, -1, 0, "", 0, 0, 0, 0);
    EXPECT(exec.poll(exec.user, intents, 4) == 0, "closed presenter cannot enqueue");
    EXPECT(
        PlatformAndroidChrome_TakeExpandedRequest(&expanded) && expanded == 1,
        "collapsed rail action survives executor end");
    EXPECT(
        !PlatformAndroidChrome_TakeExpandedRequest(&expanded),
        "rail action is consumed exactly once");
    EXPECT(exec.rail_poll(exec.user, rail_intents, 4) == 3,
        "selection and latest allocation survive selected-page executor end");
    EXPECT(rail_intents[0].plugin_index == 4 && rail_intents[1].plugin_index == -2,
        "rail queues concrete destinations, including permanent Manage");
    EXPECT(rail_intents[2].kind == TORIRS_CHROME_RAIL_INTENT_LAYOUT &&
            rail_intents[2].selection_generation == 7 && !rail_intents[2].game_visible,
        "compact native allocation returns neutral visibility facts");

    puts("android chrome executor: ok");
    return 0;
}
