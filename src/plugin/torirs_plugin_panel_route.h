#ifndef TORIRS_PLUGIN_PANEL_ROUTE_H
#define TORIRS_PLUGIN_PANEL_ROUTE_H

#include "plugin/torirs_plugin_types.h"
#include "ui/torirs_chrome_exec.h"
#include "ui/torirs_chrome_shell.h"

/* Pure decision at the presenter -> application rail boundary. Keeping this
 * separate from App mutation makes the page/settings distinction testable:
 * the same plugin may back both faces, but only its rail stone opens PAGE. */
enum AppPluginRailRouteAction
{
    APP_PLUGIN_RAIL_ROUTE_NONE = 0,
    APP_PLUGIN_RAIL_ROUTE_CLOSE,
    APP_PLUGIN_RAIL_ROUTE_MANAGE,
    APP_PLUGIN_RAIL_ROUTE_PAGE,
};

struct AppPluginRailRouteInput
{
    int destination;
    int destination_has_page;
    int panel_visible;
    int shell_selection;
    int requested_view;
    int host_selection;
    int host_view;
};

/**
 * The copied result of draining one presenter's bounded rail queue.
 *
 * Selection is desired state, so several clicks against one published shell
 * generation collapse to the newest sequence. Layout is independent state
 * and is retained separately. Keeping this logic beside the route decision
 * lets the queue -> PAGE boundary be exercised without constructing an App.
 */
struct AppPluginRailDrainResult
{
    int have_select;
    int have_layout;
    struct ToriRSChromeRailIntent select;
    struct ToriRSChromeRailIntent layout;
};

static inline void
AppPluginRailRoute_Drain(
    struct ToriRSChromeExec const* exec,
    uint32_t selection_generation,
    struct AppPluginRailDrainResult* out)
{
    struct ToriRSChromeRailIntent batch[32];

    if( !out )
        return;
    *out = (struct AppPluginRailDrainResult){ 0 };
    if( !exec || selection_generation == 0 )
        return;

    for( int pass = 0; pass < 4; pass++ )
    {
        int count = ToriRSChromeRail_Poll(
            exec, batch, (int)(sizeof(batch) / sizeof(batch[0])));
        if( count <= 0 )
            break;
        /* An executor is application code, but do not let a broken callback's
         * claimed count turn this boundary into an out-of-bounds read. */
        if( count > (int)(sizeof(batch) / sizeof(batch[0])) )
            count = (int)(sizeof(batch) / sizeof(batch[0]));
        for( int i = 0; i < count; i++ )
        {
            struct ToriRSChromeRailIntent const* intent = &batch[i];

            if( intent->selection_generation != selection_generation )
                continue;
            if( intent->kind == TORIRS_CHROME_RAIL_INTENT_LAYOUT )
            {
                if( intent->width < 0 || intent->height < 0 ||
                    intent->custom_width < 0 || intent->page_generation == 0 ||
                    intent->scale_milli <= 0 )
                    continue;
                if( !out->have_layout || intent->sequence >= out->layout.sequence )
                {
                    out->layout = *intent;
                    out->have_layout = 1;
                }
            }
            else if( intent->kind == TORIRS_CHROME_RAIL_INTENT_SELECT &&
                     (!out->have_select || intent->sequence >= out->select.sequence) )
            {
                out->select = *intent;
                out->have_select = 1;
            }
        }
        if( count < (int)(sizeof(batch) / sizeof(batch[0])) )
            break;
    }
}

static inline int
AppPluginRailRoute_Decide(struct AppPluginRailRouteInput const* input)
{
    if( !input )
        return APP_PLUGIN_RAIL_ROUTE_NONE;
    if( input->destination == TORIRS_CHROME_SHELL_PAGE_MANAGE )
        return input->panel_visible &&
                       input->shell_selection == TORIRS_CHROME_SHELL_PAGE_MANAGE
                   ? APP_PLUGIN_RAIL_ROUTE_CLOSE
                   : APP_PLUGIN_RAIL_ROUTE_MANAGE;
    if( input->destination < 0 || !input->destination_has_page )
        return APP_PLUGIN_RAIL_ROUTE_NONE;
    if( input->panel_visible && input->shell_selection == input->destination &&
        input->requested_view == TORIRS_PANEL_VIEW_PAGE &&
        input->host_selection == input->destination &&
        input->host_view == TORIRS_PANEL_VIEW_PAGE )
        return APP_PLUGIN_RAIL_ROUTE_CLOSE;
    return APP_PLUGIN_RAIL_ROUTE_PAGE;
}

#endif
