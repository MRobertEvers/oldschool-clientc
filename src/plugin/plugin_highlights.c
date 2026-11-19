#include "plugin/plugin_highlights.h"

#include <assert.h>
#include <stddef.h>

/* ---- the walked list ---- */

void
PluginHighlights_Clear(struct PluginHighlights* hl)
{
    assert(hl);
    hl->count = 0;
}

struct ToriRS_HighlightItem*
PluginHighlights_Push(
    struct PluginHighlights* hl,
    struct ToriRS_HighlightItem const* proto)
{
    struct ToriRS_HighlightItem* item;

    assert(hl);
    assert(proto);
    if( hl->count >= PLUGIN_HIGHLIGHTS_MAX )
        return NULL;
    item = &hl->items[hl->count++];
    *item = *proto;
    return item;
}

int
PluginHighlights_Count(struct PluginHighlights const* hl)
{
    assert(hl);
    return hl->count;
}

struct ToriRS_HighlightItem const*
PluginHighlights_Item(struct PluginHighlights const* hl, int i)
{
    assert(hl);
    assert(i >= 0);
    assert(i < hl->count);
    return &hl->items[i];
}

/* ---- the kept loc list ---- */

bool
PluginHighlights_LocNeedsFull(
    struct PluginHighlights const* hl,
    int revision,
    bool scenery_changed_overflow)
{
    assert(hl);
    return !hl->loc_valid || hl->loc_revision != revision || scenery_changed_overflow;
}

void
PluginHighlights_LocClear(struct PluginHighlights* hl)
{
    assert(hl);
    hl->loc_count = 0;
}

void
PluginHighlights_LocBuilt(struct PluginHighlights* hl, int revision)
{
    assert(hl);
    hl->loc_revision = revision;
    hl->loc_valid = true;
}

struct ToriRS_HighlightItem*
PluginHighlights_LocPush(
    struct PluginHighlights* hl,
    struct ToriRS_HighlightItem const* proto)
{
    struct ToriRS_HighlightItem* item;

    assert(hl);
    assert(proto);
    if( hl->loc_count >= PLUGIN_HIGHLIGHTS_MAX )
        return NULL;
    item = &hl->loc[hl->loc_count++];
    *item = *proto;
    return item;
}

void
PluginHighlights_LocForget(struct PluginHighlights* hl, int element_id)
{
    int out = 0;

    assert(hl);
    for( int i = 0; i < hl->loc_count; i++ )
    {
        if( hl->loc[i].element_id == element_id )
            continue;
        if( out != i )
            hl->loc[out] = hl->loc[i];
        out++;
    }
    hl->loc_count = out;
}

int
PluginHighlights_LocCount(struct PluginHighlights const* hl)
{
    assert(hl);
    return hl->loc_count;
}

struct ToriRS_HighlightItem const*
PluginHighlights_LocItem(struct PluginHighlights const* hl, int i)
{
    assert(hl);
    assert(i >= 0);
    assert(i < hl->loc_count);
    return &hl->loc[i];
}
