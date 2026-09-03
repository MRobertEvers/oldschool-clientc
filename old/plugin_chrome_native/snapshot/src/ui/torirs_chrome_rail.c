#include "torirs_chrome_rail.h"

#include "torirs_chrome_exec.h"

#include <assert.h>
#include <string.h>

static void
rail_copy(char* out, int cap, char const* in)
{
    int i = 0;

    if( in )
        for( ; i < cap - 1 && in[i]; i++ )
            out[i] = in[i];
    out[i] = '\0';
}

void
ToriRSChromeRailSnapshot_Init(struct ToriRSChromeRailSnapshot* snapshot)
{
    assert(snapshot);
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->active_plugin = -1;
    snapshot->last_selected_plugin = -1;
    snapshot->selected_entry = -1;
}

int
ToriRSChromeRailSnapshot_Add(
    struct ToriRSChromeRailSnapshot* snapshot,
    int plugin_index,
    char const* title,
    char const* icon_asset,
    int preferred_width,
    char const* badge,
    int attention)
{
    struct ToriRSChromeRailEntry* entry;

    assert(snapshot);
    if( plugin_index < 0 || snapshot->entry_count >= TORIRS_CHROME_RAIL_ENTRY_MAX )
        return 0;
    entry = &snapshot->entries[snapshot->entry_count++];
    memset(entry, 0, sizeof(*entry));
    entry->kind = TORIRS_CHROME_RAIL_ENTRY_PLUGIN;
    entry->plugin_index = plugin_index;
    entry->preferred_width = preferred_width;
    entry->attention = attention ? 1 : 0;
    rail_copy(entry->title, sizeof(entry->title), title);
    rail_copy(entry->icon_asset, sizeof(entry->icon_asset), icon_asset);
    rail_copy(entry->badge, sizeof(entry->badge), badge);
    return 1;
}

int
ToriRSChromeRailSnapshot_AddManage(
    struct ToriRSChromeRailSnapshot* snapshot,
    int manage_sentinel,
    char const* title)
{
    struct ToriRSChromeRailEntry* entry;

    assert(snapshot);
    if( snapshot->entry_count >= TORIRS_CHROME_RAIL_ENTRY_MAX )
        return 0;
    entry = &snapshot->entries[snapshot->entry_count++];
    memset(entry, 0, sizeof(*entry));
    entry->kind = TORIRS_CHROME_RAIL_ENTRY_MANAGE;
    entry->plugin_index = manage_sentinel;
    rail_copy(entry->title, sizeof(entry->title), title);
    return 1;
}

void
ToriRSChromeRailSync_Init(struct ToriRSChromeRailSync* sync)
{
    assert(sync);
    memset(sync, 0, sizeof(*sync));
}

static int
rail_snapshot_same(
    struct ToriRSChromeRailSnapshot const* a,
    struct ToriRSChromeRailSnapshot const* b)
{
    /* Registry revision covers entry shape and metadata. The remaining fields
     * move independently as the shared shell is selected/collapsed. */
    return a->registry_revision == b->registry_revision &&
           a->selection_generation == b->selection_generation &&
           a->page_generation == b->page_generation &&
           a->active_plugin == b->active_plugin &&
           a->last_selected_plugin == b->last_selected_plugin &&
           a->selected_entry == b->selected_entry &&
           a->expanded == b->expanded && a->entry_count == b->entry_count;
}

int
ToriRSChromeRailSync_Run(
    struct ToriRSChromeRailSync* sync,
    struct ToriRSChromeExec const* exec,
    struct ToriRSChromeRailSnapshot const* snapshot)
{
    assert(sync);
    assert(exec);
    assert(snapshot);
    if( !exec->rail_sync )
        return 0;
    if( sync->primed && sync->presented_user == exec->user &&
        sync->presented_apply == exec->rail_sync &&
        rail_snapshot_same(&sync->presented, snapshot) )
        return 0;
    if( sync->presented_user != exec->user ||
        sync->presented_apply != exec->rail_sync )
    {
        memset(sync->icon_known, 0, sizeof(sync->icon_known));
        sync->icon_user = NULL;
        sync->icon_apply = NULL;
    }
    exec->rail_sync(exec->user, snapshot);
    sync->presented = *snapshot;
    sync->presented_user = exec->user;
    sync->presented_apply = exec->rail_sync;
    sync->primed = 1;
    return 1;
}

int
ToriRSChromeRailSync_Icon(
    struct ToriRSChromeRailSync* sync,
    struct ToriRSChromeExec const* exec,
    struct ToriRSChromeRailIcon const* icon)
{
    int plugin;

    assert(sync);
    assert(exec);
    assert(icon);
    if( !exec->rail_icon )
        return 0;
    plugin = icon->plugin_index;
    if( plugin < 0 || plugin >= TORIRS_CHROME_RAIL_ENTRY_MAX || icon->revision == 0 ||
        icon->width < 0 || icon->height < 0 ||
        icon->width > TORIRS_CHROME_RAIL_ICON_SIDE_MAX ||
        icon->height > TORIRS_CHROME_RAIL_ICON_SIDE_MAX )
        return 0;
    if( sync->icon_user != exec->user || sync->icon_apply != exec->rail_icon )
    {
        memset(sync->icon_known, 0, sizeof(sync->icon_known));
        sync->icon_user = exec->user;
        sync->icon_apply = exec->rail_icon;
    }
    if( sync->icon_known[plugin] &&
        sync->icon_revision[plugin] == icon->revision )
        return 0;
    exec->rail_icon(exec->user, icon);
    sync->icon_revision[plugin] = icon->revision;
    sync->icon_known[plugin] = 1;
    return 1;
}

int
ToriRSChromeRail_Poll(
    struct ToriRSChromeExec const* exec,
    struct ToriRSChromeRailIntent* out,
    int max)
{
    assert(exec);
    if( !out || max <= 0 || !exec->rail_poll )
        return 0;
    return exec->rail_poll(exec->user, out, max);
}
