#include "plugin/torirs_plugin_panel_route.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                                           \
    do                                                                             \
    {                                                                              \
        if( !(condition) )                                                         \
        {                                                                          \
            fprintf(stderr, "panel route: %s\n", #condition);                   \
            exit(1);                                                               \
        }                                                                          \
    } while( 0 )

struct RailQueue
{
    struct ToriRSChromeRailIntent intent[8];
    int count;
    int next;
};

static int
rail_poll(void* user, struct ToriRSChromeRailIntent* out, int max)
{
    struct RailQueue* queue = user;
    int count = queue->count - queue->next;

    if( count > max )
        count = max;
    for( int i = 0; i < count; i++ )
        out[i] = queue->intent[queue->next + i];
    queue->next += count;
    return count;
}

int
main(void)
{
    int const xp = 8;
    struct AppPluginRailDrainResult drained;
    struct RailQueue queue = { 0 };
    struct ToriRSChromeExec exec = { 0 };
    struct AppPluginRailRouteInput input = {
        .destination = TORIRS_CHROME_SHELL_PAGE_MANAGE,
        .destination_has_page = 0,
        .panel_visible = 0,
        .shell_selection = TORIRS_CHROME_SHELL_PAGE_NONE,
        .requested_view = TORIRS_PANEL_VIEW_SETTINGS,
        .host_selection = -1,
        .host_view = TORIRS_PANEL_VIEW_PAGE,
    };

    CHECK(AppPluginRailRoute_Decide(&input) == APP_PLUGIN_RAIL_ROUTE_MANAGE);

    /* A Manage-roster row mounted XP's generated form. The XP rail stone is
     * still an explicit PAGE destination, never a toggle for that form. */
    input.destination = xp;
    input.destination_has_page = 1;
    input.panel_visible = 1;
    input.shell_selection = TORIRS_CHROME_SHELL_PAGE_MANAGE;
    input.requested_view = TORIRS_PANEL_VIEW_SETTINGS;
    input.host_selection = xp;
    input.host_view = TORIRS_PANEL_VIEW_SETTINGS;
    CHECK(AppPluginRailRoute_Decide(&input) == APP_PLUGIN_RAIL_ROUTE_PAGE);

    /* Only unanimous PAGE state makes the selected stone a collapse action. */
    input.shell_selection = xp;
    input.requested_view = TORIRS_PANEL_VIEW_PAGE;
    input.host_view = TORIRS_PANEL_VIEW_PAGE;
    CHECK(AppPluginRailRoute_Decide(&input) == APP_PLUGIN_RAIL_ROUTE_CLOSE);
    input.host_view = TORIRS_PANEL_VIEW_SETTINGS;
    CHECK(AppPluginRailRoute_Decide(&input) == APP_PLUGIN_RAIL_ROUTE_PAGE);

    input.destination = TORIRS_CHROME_SHELL_PAGE_MANAGE;
    input.shell_selection = xp;
    CHECK(AppPluginRailRoute_Decide(&input) == APP_PLUGIN_RAIL_ROUTE_MANAGE);
    input.shell_selection = TORIRS_CHROME_SHELL_PAGE_MANAGE;
    CHECK(AppPluginRailRoute_Decide(&input) == APP_PLUGIN_RAIL_ROUTE_CLOSE);

    input.destination = 12;
    input.destination_has_page = 0;
    CHECK(AppPluginRailRoute_Decide(&input) == APP_PLUGIN_RAIL_ROUTE_NONE);

    /* Exercise the presenter queue boundary used by App, not just a
     * hand-authored final state. An older Manage click, a layout report and a
     * stale-generation click precede XP. Draining must land on XP, and that
     * destination must repair a currently mounted XP SETTINGS face to PAGE. */
    queue.intent[0] = (struct ToriRSChromeRailIntent){
        .kind = TORIRS_CHROME_RAIL_INTENT_SELECT,
        .plugin_index = TORIRS_CHROME_SHELL_PAGE_MANAGE,
        .selection_generation = 7,
        .sequence = 20,
    };
    queue.intent[1] = (struct ToriRSChromeRailIntent){
        .kind = TORIRS_CHROME_RAIL_INTENT_LAYOUT,
        .selection_generation = 7,
        .page_generation = 31,
        .sequence = 21,
        .width = 320,
        .height = 480,
        .custom_width = 302,
        .scale_milli = 1000,
    };
    queue.intent[2] = (struct ToriRSChromeRailIntent){
        .kind = TORIRS_CHROME_RAIL_INTENT_SELECT,
        .plugin_index = 3,
        .selection_generation = 6,
        .sequence = 99,
    };
    queue.intent[3] = (struct ToriRSChromeRailIntent){
        .kind = TORIRS_CHROME_RAIL_INTENT_SELECT,
        .plugin_index = xp,
        .selection_generation = 7,
        .sequence = 22,
    };
    queue.count = 4;
    exec.user = &queue;
    exec.rail_poll = rail_poll;
    AppPluginRailRoute_Drain(&exec, 7, &drained);
    CHECK(drained.have_select && drained.select.plugin_index == xp &&
          drained.select.sequence == 22);
    CHECK(drained.have_layout && drained.layout.width == 320 &&
          drained.layout.page_generation == 31);

    input.destination = drained.select.plugin_index;
    input.destination_has_page = 1;
    input.panel_visible = 1;
    input.shell_selection = TORIRS_CHROME_SHELL_PAGE_MANAGE;
    input.requested_view = TORIRS_PANEL_VIEW_SETTINGS;
    input.host_selection = xp;
    input.host_view = TORIRS_PANEL_VIEW_SETTINGS;
    CHECK(AppPluginRailRoute_Decide(&input) == APP_PLUGIN_RAIL_ROUTE_PAGE);

    puts("plugin panel rail route: ok");
    return 0;
}
