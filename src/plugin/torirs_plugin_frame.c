#include "plugin/torirs_plugin_frame.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int
plugin_frame_id_valid(char const* id)
{
    assert(id);
    if( !id[0] )
        return 0;
    for( char const* at = id; *at; at++ )
    {
        char const c = *at;
        if( (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' )
            continue;
        return 0;
    }
    return 1;
}

void
PluginFrameCatalog_Init(struct PluginFrameCatalog* catalog)
{
    assert(catalog);
    memset(catalog, 0, sizeof(*catalog));
}

int
PluginFrameCatalog_Find(
    struct PluginFrameCatalog const* catalog,
    char const* canonical_id)
{
    assert(catalog);
    assert(canonical_id);
    for( int i = 0; i < catalog->count; i++ )
        if( strcmp(catalog->entries[i].id, canonical_id) == 0 )
            return i;
    return -1;
}

enum PluginFrameCatalogResult
PluginFrameCatalog_Add(
    struct PluginFrameCatalog* catalog,
    int plugin,
    char const* provider,
    struct ToriRS_FrameOffer const* offers)
{
    struct PluginFrameCatalog candidate;

    assert(catalog);
    assert(provider);
    assert(offers);
    assert(plugin >= 0);

    if( !plugin_frame_id_valid(provider) )
        return PLUGIN_FRAME_CATALOG_INVALID;
    candidate = *catalog;
    for( int i = 0; offers[i].id; i++ )
    {
        struct ToriRS_FrameOffer const* offer = &offers[i];
        struct PluginFrameCatalogEntry* entry;
        int n;

        if( !plugin_frame_id_valid(offer->id) )
            return PLUGIN_FRAME_CATALOG_INVALID;
        if( !offer->title || !offer->title[0] )
            return PLUGIN_FRAME_CATALOG_INVALID;
        if( offer->canvas != TORIRS_FRAME_CANVAS_FIXED &&
            offer->canvas != TORIRS_FRAME_CANVAS_WINDOW )
            return PLUGIN_FRAME_CATALOG_INVALID;
        if( (offer->canvas == TORIRS_FRAME_CANVAS_FIXED &&
             (offer->width <= 0 || offer->height <= 0)) ||
            (offer->canvas == TORIRS_FRAME_CANVAS_WINDOW &&
             (offer->min_width <= 0 || offer->min_height <= 0)) )
            return PLUGIN_FRAME_CATALOG_INVALID;
        if( candidate.count >= TORIRS_PLUGIN_FRAME_OFFERS_MAX )
            return PLUGIN_FRAME_CATALOG_FULL;

        entry = &candidate.entries[candidate.count];
        memset(entry, 0, sizeof(*entry));
        n = snprintf(entry->id, sizeof(entry->id), "%s/%s", provider, offer->id);
        if( n < 0 || n >= (int)sizeof(entry->id) )
            return PLUGIN_FRAME_CATALOG_INVALID;
        if( PluginFrameCatalog_Find(&candidate, entry->id) >= 0 )
            return PLUGIN_FRAME_CATALOG_DUPLICATE;

        n = snprintf(entry->local_id, sizeof(entry->local_id), "%s", offer->id);
        if( n < 0 || n >= (int)sizeof(entry->local_id) )
            return PLUGIN_FRAME_CATALOG_INVALID;
        n = snprintf(entry->title, sizeof(entry->title), "%s", offer->title);
        if( n < 0 || n >= (int)sizeof(entry->title) )
            return PLUGIN_FRAME_CATALOG_INVALID;
        n = snprintf(entry->provider, sizeof(entry->provider), "%s", provider);
        if( n < 0 || n >= (int)sizeof(entry->provider) )
            return PLUGIN_FRAME_CATALOG_INVALID;

        entry->plugin = plugin;
        entry->canvas = offer->canvas;
        entry->width = offer->canvas == TORIRS_FRAME_CANVAS_FIXED
                           ? offer->width
                           : offer->min_width;
        entry->height = offer->canvas == TORIRS_FRAME_CANVAS_FIXED
                            ? offer->height
                            : offer->min_height;
        entry->available = 1;
        candidate.count++;
    }
    if( candidate.count == catalog->count )
        return PLUGIN_FRAME_CATALOG_INVALID;

    *catalog = candidate;
    return PLUGIN_FRAME_CATALOG_OK;
}

int
PluginFrameCatalog_Count(struct PluginFrameCatalog const* catalog)
{
    assert(catalog);
    return catalog->count;
}

struct PluginFrameCatalogEntry const*
PluginFrameCatalog_At(
    struct PluginFrameCatalog const* catalog,
    int index)
{
    assert(catalog);
    if( index < 0 || index >= catalog->count )
        return NULL;
    return &catalog->entries[index];
}

void
PluginFrameCatalog_SetAvailable(
    struct PluginFrameCatalog* catalog,
    int plugin,
    int available)
{
    assert(catalog);
    assert(plugin >= 0);
    for( int i = 0; i < catalog->count; i++ )
        if( catalog->entries[i].plugin == plugin )
            catalog->entries[i].available = available ? 1 : 0;
}

void
PluginFrameCatalog_RemovePlugin(
    struct PluginFrameCatalog* catalog,
    int plugin)
{
    int write = 0;

    assert(catalog);
    assert(plugin >= 0);
    for( int read = 0; read < catalog->count; read++ )
    {
        if( catalog->entries[read].plugin == plugin )
            continue;
        if( write != read )
            catalog->entries[write] = catalog->entries[read];
        write++;
    }
    for( int i = write; i < catalog->count; i++ )
        memset(&catalog->entries[i], 0, sizeof(catalog->entries[i]));
    catalog->count = write;
}
