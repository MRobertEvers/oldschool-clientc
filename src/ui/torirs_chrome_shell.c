#include "torirs_chrome_shell.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

static int
shell_clamp(int value, int minimum, int maximum)
{
    if( maximum < minimum )
        maximum = minimum;
    if( value < minimum )
        return minimum;
    if( value > maximum )
        return maximum;
    return value;
}

static uint32_t
shell_next_generation(uint32_t generation)
{
    generation++;
    /* Zero is the value of an unstamped event, so never publish it. */
    if( generation == 0 )
        generation = 1;
    return generation;
}

void
ToriRSChromeShell_Init(struct ToriRSChromeShell* shell, int preferred_panel_w)
{
    assert(shell);
    memset(shell, 0, sizeof(*shell));
    shell->active_plugin = TORIRS_CHROME_SHELL_PAGE_NONE;
    shell->selection_generation = 1;
    shell->panel_w = preferred_panel_w > 0 ? preferred_panel_w : 320;
}

uint32_t
ToriRSChromeShell_Select(struct ToriRSChromeShell* shell, int plugin)
{
    assert(shell);
    if( plugin < TORIRS_CHROME_SHELL_PAGE_MANAGE )
        plugin = TORIRS_CHROME_SHELL_PAGE_NONE;
    if( shell->active_plugin == plugin &&
        (plugin == TORIRS_CHROME_SHELL_PAGE_NONE || shell->expanded) )
        return shell->selection_generation;
    /* Move the generation first: anything queued by the old page is stale
     * before its controls begin disappearing. */
    shell->selection_generation = shell_next_generation(shell->selection_generation);
    shell->active_plugin = plugin;
    shell->expanded = plugin != TORIRS_CHROME_SHELL_PAGE_NONE;
    if( plugin == TORIRS_CHROME_SHELL_PAGE_NONE )
        shell->detached = 0;
    return shell->selection_generation;
}

uint32_t
ToriRSChromeShell_Collapse(struct ToriRSChromeShell* shell)
{
    assert(shell);
    if( !shell->expanded )
        return shell->selection_generation;
    /* Invalidate queued page work before its controls are removed, while
     * retaining active_plugin as the rail's remembered selection. */
    shell->selection_generation = shell_next_generation(shell->selection_generation);
    shell->expanded = 0;
    shell->detached = 0;
    return shell->selection_generation;
}

int
ToriRSChromeShell_Accepts(
    struct ToriRSChromeShell const* shell, int plugin, uint32_t selection_generation)
{
    assert(shell);
    return shell->expanded && plugin >= 0 && plugin == shell->active_plugin &&
           selection_generation != 0 && selection_generation == shell->selection_generation;
}

void
ToriRSChromeShell_SetDetached(struct ToriRSChromeShell* shell, int detached)
{
    assert(shell);
    shell->detached = shell->expanded && detached ? 1 : 0;
}

void
ToriRSChromeShell_SetPanelWidth(
    struct ToriRSChromeShell* shell, int width, int minimum, int maximum)
{
    assert(shell);
    if( minimum <= 0 )
        minimum = 1;
    if( maximum <= 0 )
        maximum = INT_MAX;
    shell->panel_w = shell_clamp(width, minimum, maximum);
}

void
ToriRSChromeShell_Layout(
    struct ToriRSChromeShell const* shell,
    struct ToriRSChromeShellInput const* input,
    struct ToriRSChromeShellLayout* out)
{
    int rail_w;
    int panel_w;
    int game_w;
    int required_w;

    assert(shell);
    assert(input);
    assert(out);
    memset(out, 0, sizeof(*out));

    if( input->window_w <= 0 || input->window_h <= 0 )
        return;

    rail_w = shell_clamp(input->rail_w, 0, input->window_w);
    out->rail.x = input->window_w - rail_w;
    out->rail.w = rail_w;
    out->rail.h = input->window_h;

    if( !shell->expanded || shell->active_plugin == TORIRS_CHROME_SHELL_PAGE_NONE )
    {
        out->mode = TORIRS_CHROME_SHELL_COLLAPSED;
        out->game.w = input->window_w - rail_w;
        out->game.h = input->window_h;
        return;
    }

    if( shell->detached )
    {
        out->mode = TORIRS_CHROME_SHELL_DETACHED;
        out->game.w = input->window_w - rail_w;
        out->game.h = input->window_h;
        return;
    }

    panel_w = shell->panel_w > 0 ? shell->panel_w : input->preferred_panel_w;
    if( panel_w <= 0 )
        panel_w = input->min_panel_w;
    panel_w = shell_clamp(panel_w, input->min_panel_w > 0 ? input->min_panel_w : 1,
        input->window_w > rail_w ? input->window_w - rail_w : 1);

    required_w = input->target_game_w + rail_w + panel_w;
    if( input->may_grow && input->target_game_w > 0 && input->target_game_h > 0 &&
        input->window_w < required_w )
    {
        out->request_window_w = required_w;
        out->request_window_h = input->window_h > input->target_game_h
                                    ? input->window_h
                                    : input->target_game_h;
    }

    game_w = input->window_w - rail_w - panel_w;
    if( game_w >= input->min_game_w && panel_w >= input->min_panel_w )
    {
        out->mode = TORIRS_CHROME_SHELL_SPLIT;
        out->game.w = game_w;
        out->game.h = input->window_h;
        out->rail.x = game_w;
        out->panel.x = game_w + rail_w;
        out->panel.w = panel_w;
        out->panel.h = input->window_h;
        return;
    }

    /* Compact is a full replacement, never a partial overlay. The rail stays
     * at the leading edge of the page so the selected entry and way back are
     * still reachable. */
    out->mode = TORIRS_CHROME_SHELL_EXCLUSIVE;
    out->game.w = 0;
    out->game.h = 0;
    out->rail.x = 0;
    out->panel.x = rail_w;
    out->panel.w = input->window_w - rail_w;
    out->panel.h = input->window_h;
}
