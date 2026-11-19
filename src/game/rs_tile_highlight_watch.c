#include "game/rs_tile_highlight_watch.h"

#include <assert.h>

static bool
tile_highlight_watch_changed(
    int* last,
    int value)
{
    if( value == *last )
        return false;
    *last = value;
    return true;
}

void
RS_TileHighlightWatch_Reset(struct RS_TileHighlightWatch* watch)
{
    assert(watch);
    watch->hover_coord = RS_TILE_HIGHLIGHT_WATCH_UNSEEN;
    watch->route = RS_TILE_HIGHLIGHT_WATCH_UNSEEN;
    watch->dest_coord = RS_TILE_HIGHLIGHT_WATCH_UNSEEN;
}

bool
RS_TileHighlightWatch_HoverChanged(
    struct RS_TileHighlightWatch* watch,
    int hover_coord)
{
    assert(watch);
    return tile_highlight_watch_changed(&watch->hover_coord, hover_coord);
}

bool
RS_TileHighlightWatch_RouteChanged(
    struct RS_TileHighlightWatch* watch,
    int route)
{
    assert(watch);
    return tile_highlight_watch_changed(&watch->route, route);
}

bool
RS_TileHighlightWatch_DestChanged(
    struct RS_TileHighlightWatch* watch,
    int dest_coord)
{
    assert(watch);
    return tile_highlight_watch_changed(&watch->dest_coord, dest_coord);
}
