#include "ui/torirs_chrome_exec.h"
#include "platform/platform_window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if( !(x) ) { fprintf(stderr, "browser exec: %s\n", #x); exit(1); } } while( 0 )

struct PlatformWindow { int unused; };
static struct PlatformWindow platform;
static char sent[16][32768];
static int sent_count;
static char inbound[2048];
static int opened;
static int collapsed;
static int page_width;

bool PlatformWindow_PluginBrowserEnsure(struct PlatformWindow* p)
{ return p == &platform; }
bool PlatformWindow_PluginBrowserReady(struct PlatformWindow const* p)
{ return p == &platform; }
bool PlatformWindow_PluginBrowserFailed(struct PlatformWindow const* p)
{ return p != &platform; }
void PlatformWindow_PluginBrowserSend(struct PlatformWindow* p, char const* json)
{
    CHECK(p == &platform && json && sent_count < 16);
    snprintf(sent[sent_count++], sizeof(sent[0]), "%s", json);
}
int PlatformWindow_PluginBrowserPoll(struct PlatformWindow* p, char* out, int cap)
{
    int n;
    CHECK(p == &platform);
    if( !inbound[0] ) return 0;
    n = (int)strlen(inbound);
    if( n >= cap ) n = cap - 1;
    memcpy(out, inbound, (size_t)n);
    out[n] = 0;
    inbound[0] = 0;
    return n;
}
bool PlatformWindow_PluginBrowserBitmapUrl(
    struct PlatformWindow* p, char const* key, uint32_t revision,
    uint32_t const* argb, int width, int height, char* out, int cap)
{
    CHECK(p == &platform && key && revision && argb && width > 0 && height > 0);
    snprintf(out, (size_t)cap, "bitmap/%s-r%u.bmp", key, (unsigned)revision);
    return true;
}
bool PlatformWindow_ChromeOpen(
    struct PlatformWindow* p, int width, int height, char const* title)
{
    CHECK(p == &platform && height > 0 && title);
    opened++;
    page_width = width;
    return true;
}
void PlatformWindow_ChromeClose(struct PlatformWindow* p)
{ CHECK(p == &platform); collapsed++; }

static struct ToriRSChromeCmd command(int kind, int panel, int widget)
{
    struct ToriRSChromeCmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = kind;
    cmd.panel = panel;
    cmd.widget = widget;
    cmd.tab = -1;
    return cmd;
}

int main(void)
{
    struct ToriRSChromeExec exec = ToriRSChromeExec_Browser(&platform);
    struct ToriRSChromeRailSnapshot rail;
    struct ToriRSChromeRailIntent rail_intent;
    struct ToriRSChromeIntent intent;
    struct ToriRSChromeCmd cmd;
    struct ToriRSChromeCustomFrame frame;
    uint32_t pixels[4] = { 0xff000000u, 0xffffffffu, 0xffff981fu, 0xff372e22u };

    CHECK(exec.begin && exec.apply && exec.end && exec.poll &&
          exec.rail_sync && exec.rail_icon && exec.rail_poll && exec.custom_present &&
          exec.take_snapshot_request);
    memset(&rail, 0, sizeof(rail));
    rail.registry_revision = 3;
    rail.selection_generation = 7;
    rail.page_generation = 11;
    rail.selected_entry = 4;
    rail.expanded = 1;
    rail.entry_count = 2;
    rail.entries[0].kind = TORIRS_CHROME_RAIL_ENTRY_MANAGE;
    rail.entries[0].plugin_index = -2;
    snprintf(rail.entries[0].title, sizeof(rail.entries[0].title), "Manage Plugins");
    rail.entries[1].kind = TORIRS_CHROME_RAIL_ENTRY_PLUGIN;
    rail.entries[1].plugin_index = 4;
    rail.entries[1].preferred_width = 420;
    snprintf(rail.entries[1].title, sizeof(rail.entries[1].title), "Loot Tracker");
    exec.rail_sync(exec.user, &rail);
    CHECK(sent_count == 2);
    CHECK(strstr(sent[0], "\"type\":\"theme\"") != NULL);
    CHECK(strstr(sent[0], "\"checkBoxOn\":\"skin/CheckBoxOn.png\"") != NULL);
    CHECK(strstr(sent[0], "\"dropdownBody\":\"skin/DropdownBody.png\"") != NULL);
    CHECK(strstr(sent[0], "\"scrollGripMiddle\":\"skin/ScrollGripMid.png\"") != NULL);
    CHECK(strstr(sent[1], "\"selectionGeneration\":7") != NULL);
    CHECK(strstr(sent[1], "\"pageGeneration\":11") != NULL);

    CHECK(exec.begin(exec.user));
    CHECK(opened == 1 && page_width == 420);
    cmd = command(TORIRS_CHROME_CMD_SYNC_BEGIN, -1, -1);
    exec.apply(exec.user, &cmd);
    cmd = command(TORIRS_CHROME_CMD_PANEL_OPEN, 2, -1);
    snprintf(cmd.text, sizeof(cmd.text), "Loot Tracker");
    exec.apply(exec.user, &cmd);
    cmd = command(TORIRS_CHROME_CMD_WIDGET_ADD, 2, 9);
    cmd.value = TORIRS_CHROME_W_CUSTOM;
    cmd.serial = 481;
    cmd.h = 48;
    snprintf(cmd.label, sizeof(cmd.label), "Chart");
    exec.apply(exec.user, &cmd);
    cmd = command(TORIRS_CHROME_CMD_WIDGET_ADD, 2, 10);
    cmd.value = TORIRS_CHROME_W_DROPDOWN;
    cmd.serial = 490;
    snprintf(cmd.label, sizeof(cmd.label), "Gameframe");
    exec.apply(exec.user, &cmd);
    cmd = command(TORIRS_CHROME_CMD_WIDGET_OPTIONS, 2, 10);
    cmd.value = 2;
    cmd.x = 1;
    exec.apply(exec.user, &cmd);
    cmd = command(TORIRS_CHROME_CMD_WIDGET_OPTION, 2, 10);
    cmd.value = 0;
    cmd.x = 0;
    snprintf(cmd.text, sizeof(cmd.text), "missing/frame");
    snprintf(cmd.label, sizeof(cmd.label), "Same|label");
    snprintf(cmd.detail, sizeof(cmd.detail), "Provider is not installed");
    exec.apply(exec.user, &cmd);
    cmd = command(TORIRS_CHROME_CMD_WIDGET_OPTION, 2, 10);
    cmd.value = 1;
    cmd.x = 1;
    snprintf(cmd.text, sizeof(cmd.text), "ready/frame");
    snprintf(cmd.label, sizeof(cmd.label), "Same|label");
    snprintf(cmd.detail, sizeof(cmd.detail), "Available now");
    exec.apply(exec.user, &cmd);
    cmd = command(TORIRS_CHROME_CMD_WIDGET_SELECTED, 2, 10);
    cmd.value = 0;
    snprintf(cmd.text, sizeof(cmd.text), "missing/frame");
    exec.apply(exec.user, &cmd);
    cmd = command(TORIRS_CHROME_CMD_SYNC_END, -1, -1);
    exec.apply(exec.user, &cmd);
    CHECK(sent_count == 3);
    CHECK(strstr(sent[2], "\"type\":\"page.snapshot\"") != NULL);
    CHECK(strstr(sent[2], "\"pageGeneration\":11") != NULL);
    CHECK(strstr(sent[2], "\"s\":481") != NULL);
    CHECK(strstr(sent[2], "\"x\":1") != NULL &&
          strstr(sent[2], "\"text\":\"ready/frame\"") != NULL &&
          strstr(sent[2], "\"label\":\"Same|label\"") != NULL &&
          strstr(sent[2], "\"detail\":\"Provider is not installed\"") != NULL);
    CHECK(exec.take_snapshot_request(exec.user) == 0);

    /* A bounded transport never commits a transaction prefix. It reports the
     * loss once so Sync can replace the retained page with one full snapshot. */
    cmd = command(TORIRS_CHROME_CMD_SYNC_BEGIN, -1, -1);
    exec.apply(exec.user, &cmd);
    for( int i = 0; i < 8200; i++ )
    {
        cmd = command(TORIRS_CHROME_CMD_WIDGET_LABEL, 2, 9);
        exec.apply(exec.user, &cmd);
    }
    cmd = command(TORIRS_CHROME_CMD_SYNC_END, -1, -1);
    exec.apply(exec.user, &cmd);
    CHECK(sent_count == 3);
    CHECK(exec.take_snapshot_request(exec.user) == 1);
    CHECK(exec.take_snapshot_request(exec.user) == 0);

    memset(&frame, 0, sizeof(frame));
    frame.panel = 2;
    frame.widget = 9;
    frame.selection_generation = 11;
    frame.widget_serial = 481;
    frame.scale_milli = 1000;
    frame.width = frame.height = frame.stride = 2;
    frame.argb = pixels;
    exec.custom_present(exec.user, &frame);
    CHECK(sent_count == 4);
    CHECK(strstr(sent[3], "\"type\":\"custom.bitmap\"") != NULL);
    CHECK(strstr(sent[3], "bitmap/custom-11-481-r1.bmp") != NULL);

    snprintf(inbound, sizeof(inbound),
        "{\"protocol\":1,\"type\":\"widget.intent\",\"sequence\":1,"
        "\"intent\":{\"k\":8,\"p\":2,\"w\":9,\"v\":0,\"text\":\"\","
        "\"x\":1,\"y\":1,\"g\":10,\"s\":481}}");
    CHECK(exec.poll(exec.user, &intent, 1) == 0);
    snprintf(inbound, sizeof(inbound),
        "{\"protocol\":1,\"type\":\"widget.intent\",\"sequence\":2,"
        "\"intent\":{\"k\":8,\"p\":2,\"w\":9,\"v\":0,\"text\":\"\","
        "\"x\":1,\"y\":1,\"g\":11,\"s\":481}}");
    CHECK(exec.poll(exec.user, &intent, 1) == 1);
    CHECK(intent.kind == TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE &&
          intent.selection_generation == 11 && intent.widget_serial == 481);

    /* User text is before x/y/g/s in the canonical object. Escaped field-like
     * text must neither spoof an invalid tail nor make a valid tail look stale. */
    snprintf(inbound, sizeof(inbound),
        "{\"protocol\":1,\"type\":\"widget.intent\",\"sequence\":3,"
        "\"intent\":{\"k\":8,\"p\":2,\"w\":9,\"v\":0,"
        "\"text\":\"spoof \\\"g\\\":11,\\\"s\\\":481\","
        "\"x\":1,\"y\":1,\"g\":10,\"s\":999}}");
    CHECK(exec.poll(exec.user, &intent, 1) == 0);
    snprintf(inbound, sizeof(inbound),
        "{\"protocol\":1,\"type\":\"widget.intent\",\"sequence\":4,"
        "\"intent\":{\"k\":8,\"p\":2,\"w\":9,\"v\":0,"
        "\"text\":\"not fields: \\\"g\\\":0,\\\"s\\\":0\","
        "\"x\":1,\"y\":1,\"g\":11,\"s\":481}}");
    CHECK(exec.poll(exec.user, &intent, 1) == 1);
    CHECK(intent.selection_generation == 11 && intent.widget_serial == 481 &&
          strstr(intent.text, "\"g\":0") != NULL);

    snprintf(inbound, sizeof(inbound),
        "{\"protocol\":1,\"type\":\"rail.select\",\"sequence\":5,"
        "\"pluginIndex\":-2,\"selectionGeneration\":7}");
    CHECK(exec.rail_poll(exec.user, &rail_intent, 1) == 1);
    CHECK(rail_intent.kind == TORIRS_CHROME_RAIL_INTENT_SELECT &&
          rail_intent.plugin_index == -2 && rail_intent.selection_generation == 7);
    /* XP's push bridge and pull fallback can expose the same copied envelope;
     * replaying a runtime sequence must never select the plugin twice. */
    snprintf(inbound, sizeof(inbound),
        "{\"protocol\":1,\"type\":\"rail.select\",\"sequence\":5,"
        "\"pluginIndex\":-2,\"selectionGeneration\":7}");
    CHECK(exec.rail_poll(exec.user, &rail_intent, 1) == 0);

    /* Replacing A with B while expanded keeps the one browser, closes A with
     * A's generation, and makes B's first transaction a snapshot (not a delta
     * that the old DOM must reject). Panel CLOSE is omitted from that fresh
     * image because handles may be recycled. */
    rail.selection_generation = 8;
    rail.page_generation = 12;
    rail.active_plugin = 7;
    rail.selected_entry = 7;
    rail.entries[1].plugin_index = 7;
    snprintf(rail.entries[1].title, sizeof(rail.entries[1].title), "Ground Markers");
    exec.rail_sync(exec.user, &rail);
    CHECK(sent_count == 6);
    CHECK(strstr(sent[4], "\"type\":\"page.close\"") != NULL &&
          strstr(sent[4], "\"pageGeneration\":11") != NULL);
    CHECK(strstr(sent[5], "\"type\":\"rail.snapshot\"") != NULL &&
          strstr(sent[5], "\"pageGeneration\":12") != NULL);

    cmd = command(TORIRS_CHROME_CMD_SYNC_BEGIN, -1, -1);
    exec.apply(exec.user, &cmd);
    cmd = command(TORIRS_CHROME_CMD_PANEL_CLOSE, 2, -1);
    exec.apply(exec.user, &cmd);
    cmd = command(TORIRS_CHROME_CMD_PANEL_OPEN, 3, -1);
    snprintf(cmd.text, sizeof(cmd.text), "Ground Markers");
    exec.apply(exec.user, &cmd);
    cmd = command(TORIRS_CHROME_CMD_WIDGET_ADD, 3, 9);
    cmd.value = TORIRS_CHROME_W_BUTTON;
    cmd.serial = 482;
    snprintf(cmd.text, sizeof(cmd.text), "Apply");
    exec.apply(exec.user, &cmd);
    cmd = command(TORIRS_CHROME_CMD_SYNC_END, -1, -1);
    exec.apply(exec.user, &cmd);
    CHECK(sent_count == 7);
    CHECK(strstr(sent[6], "\"type\":\"page.snapshot\"") != NULL &&
          strstr(sent[6], "\"pageGeneration\":12") != NULL &&
          strstr(sent[6], "\"panel\":3") != NULL &&
          strstr(sent[6], "\"s\":482") != NULL &&
          strstr(sent[6], "\"k\":4") == NULL &&
          strstr(sent[6], "\"commands\":[,") == NULL);

    /*
     * A page boundary the RAIL never mentioned.
     *
     * This is the second face of one plugin -- its settings, reached from a
     * button on its own page -- so the rail entry, the selected entry and the
     * expanded state are all unchanged and no rail.snapshot is sent at all.
     * The only thing that says the page was replaced is the flag on
     * SYNC_BEGIN, and the whole point of putting it there is that the close
     * and the snapshot then land in one transaction, in order, on whatever
     * frame the model happened to move.
     *
     * Without it this is the blank pane: the restatement goes out as a delta
     * against a DOM that is about to be thrown away, and the throw-away
     * arrives afterwards with nothing following it.
     */
    {
        int const before = sent_count;
        cmd = command(TORIRS_CHROME_CMD_SYNC_BEGIN, -1, -1);
        cmd.value = 1;      /* restates a new page... */
        cmd.serial = 13;    /* ...and this is which one */
        exec.apply(exec.user, &cmd);
        cmd = command(TORIRS_CHROME_CMD_PANEL_OPEN, 4, -1);
        snprintf(cmd.text, sizeof(cmd.text), "Ground Markers settings");
        exec.apply(exec.user, &cmd);
        cmd = command(TORIRS_CHROME_CMD_WIDGET_ADD, 4, 11);
        cmd.value = TORIRS_CHROME_W_CHECKBOX;
        cmd.serial = 483;
        snprintf(cmd.text, sizeof(cmd.text), "Show markers");
        exec.apply(exec.user, &cmd);
        cmd = command(TORIRS_CHROME_CMD_SYNC_END, -1, -1);
        exec.apply(exec.user, &cmd);

        CHECK(sent_count == before + 2);
        /* The close names the page being DISCARDED... */
        CHECK(strstr(sent[before], "\"type\":\"page.close\"") != NULL &&
              strstr(sent[before], "\"pageGeneration\":12") != NULL);
        /* ...and the snapshot names the one REPLACING it. Stamping the
         * snapshot with the discarded page's identity is what made the rail's
         * later arrival read as another change and close it again. */
        CHECK(strstr(sent[before + 1], "\"type\":\"page.snapshot\"") != NULL &&
              strstr(sent[before + 1], "\"pageGeneration\":13") != NULL &&
              strstr(sent[before + 1], "\"panel\":4") != NULL &&
              strstr(sent[before + 1], "\"s\":483") != NULL);
    }

    /*
     * The rail catching up must be a NO-OP.
     *
     * It arrives a frame later carrying the same generation the page stream
     * already stated. An executor that learned the identity only from here
     * would see a generation it had never been told about, conclude the page
     * had changed again, and close the snapshot it had just mounted -- leaving
     * a blank pane with nothing following it, because the application's shadow
     * is by then in agreement with the model and emits nothing more.
     */
    {
        int const before = sent_count;
        rail.page_generation = 13;
        exec.rail_sync(exec.user, &rail);
        CHECK(sent_count == before + 1);
        CHECK(strstr(sent[before], "\"type\":\"rail.snapshot\"") != NULL);
        CHECK(strstr(sent[before], "\"type\":\"page.close\"") == NULL);
    }

    /* And an ordinary edit after it is a PATCH again, or every keystroke in a
     * settings field would tear the page down and rebuild it. */
    {
        int const before = sent_count;
        cmd = command(TORIRS_CHROME_CMD_SYNC_BEGIN, -1, -1);
        cmd.value = 0;
        exec.apply(exec.user, &cmd);
        cmd = command(TORIRS_CHROME_CMD_WIDGET_TEXT, 4, 11);
        snprintf(cmd.text, sizeof(cmd.text), "Show markers ");
        exec.apply(exec.user, &cmd);
        cmd = command(TORIRS_CHROME_CMD_SYNC_END, -1, -1);
        exec.apply(exec.user, &cmd);
        CHECK(sent_count == before + 1);
        CHECK(strstr(sent[before], "\"type\":\"page.delta\"") != NULL);
    }

    exec.end(exec.user);
    CHECK(collapsed == 1 && strstr(sent[sent_count - 1], "page.close") != NULL);
    puts("browser chrome executor: ok");
    return 0;
}
