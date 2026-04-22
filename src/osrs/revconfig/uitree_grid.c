#include "uitree_grid.h"

#include "uitree.h"

#include <stdlib.h>

void
uitree_grid_clear(struct UITree* tree)
{
    if( !tree )
        return;
    int total = UI_GRID_W * UI_GRID_H;
    for( int i = 0; i < total; i++ )
    {
        free(tree->grid[i].indices);
        tree->grid[i].indices = NULL;
        tree->grid[i].count = 0;
        tree->grid[i].capacity = 0;
    }
}

void
uitree_rebuild_grid(struct UITree* tree)
{
    if( !tree )
        return;
    uitree_grid_clear(tree);
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* c = &tree->components[i];
        if( c->position.kind != UIPOS_XY )
            continue;
        int x = c->position.x;
        int y = c->position.y;
        int w = c->position.width;
        int h = c->position.height;
        if( w <= 0 || h <= 0 )
            continue;
        int tx_min = x / UI_GRID_TILE_SIZE;
        int ty_min = y / UI_GRID_TILE_SIZE;
        int tx_max = (x + w - 1) / UI_GRID_TILE_SIZE;
        int ty_max = (y + h - 1) / UI_GRID_TILE_SIZE;
        if( tx_min < 0 )
            tx_min = 0;
        if( ty_min < 0 )
            ty_min = 0;
        if( tx_max >= UI_GRID_W )
            tx_max = UI_GRID_W - 1;
        if( ty_max >= UI_GRID_H )
            ty_max = UI_GRID_H - 1;
        for( int ty = ty_min; ty <= ty_max; ty++ )
        {
            for( int tx = tx_min; tx <= tx_max; tx++ )
            {
                struct UIGridTile* tile = &tree->grid[ty * UI_GRID_W + tx];
                if( tile->count >= tile->capacity )
                {
                    int newcap = tile->capacity == 0 ? 4 : tile->capacity * 2;
                    int32_t* newbuf = realloc(tile->indices, (size_t)newcap * sizeof(int32_t));
                    if( !newbuf )
                        continue;
                    tile->indices = newbuf;
                    tile->capacity = newcap;
                }
                tile->indices[tile->count++] = (int32_t)i;
            }
        }
    }
}

void
uitree_grid_dirty_prepass(
    struct UITree* tree,
    int mouse_x,
    int mouse_y,
    bool force)
{
    if( !tree )
        return;
    uint32_t n = tree->component_count;

    for( uint32_t i = 0; i < n; i++ )
    {
        struct StaticUIComponent* c = &tree->components[i];
        if( force || c->always_dirty )
            c->is_dirty = true;

        switch( c->type )
        {
        case UIELEM_BUILTIN_WORLD:
        case UIELEM_BUILTIN_MINIMAP:
        case UIELEM_BUILTIN_COMPASS:
            c->is_dirty = true;
        }
    }

    if( force )
        return;

    int total_tiles = UI_GRID_W * UI_GRID_H;
    for( int i = 0; i < total_tiles; i++ )
        tree->grid[i].dirty = 0;

    /* Step 1: for each element in the mouse tile, mark all tiles it spans. */
    int tx = mouse_x / UI_GRID_TILE_SIZE;
    int ty = mouse_y / UI_GRID_TILE_SIZE;
    if( tx < 0 )
        tx = 0;
    if( ty < 0 )
        ty = 0;
    if( tx >= UI_GRID_W )
        tx = UI_GRID_W - 1;
    if( ty >= UI_GRID_H )
        ty = UI_GRID_H - 1;
    struct UIGridTile* mouse_tile = &tree->grid[ty * UI_GRID_W + tx];
    for( int j = 0; j < mouse_tile->count; j++ )
    {
        int32_t idx = mouse_tile->indices[j];
        if( idx < 0 || (uint32_t)idx >= n )
            continue;
        struct StaticUIComponent* c = &tree->components[idx];
        if( c->position.kind != UIPOS_XY )
            continue;
        int x = c->position.x, y = c->position.y;
        int w = c->position.width, h = c->position.height;
        if( w <= 0 || h <= 0 )
            continue;
        int etx_min = x / UI_GRID_TILE_SIZE;
        int ety_min = y / UI_GRID_TILE_SIZE;
        int etx_max = (x + w - 1) / UI_GRID_TILE_SIZE;
        int ety_max = (y + h - 1) / UI_GRID_TILE_SIZE;
        if( etx_min < 0 )
            etx_min = 0;
        if( ety_min < 0 )
            ety_min = 0;
        if( etx_max >= UI_GRID_W )
            etx_max = UI_GRID_W - 1;
        if( ety_max >= UI_GRID_H )
            ety_max = UI_GRID_H - 1;
        for( int ety = ety_min; ety <= ety_max; ety++ )
            for( int etx = etx_min; etx <= etx_max; etx++ )
                tree->grid[ety * UI_GRID_W + etx].dirty = 1;
    }

    /* Step 2: mark all elements in tile-dirty tiles as is_dirty. */
    for( int ti = 0; ti < total_tiles; ti++ )
    {
        if( !tree->grid[ti].dirty )
            continue;
        struct UIGridTile* tile = &tree->grid[ti];
        for( int j = 0; j < tile->count; j++ )
        {
            int32_t idx = tile->indices[j];
            if( idx >= 0 && (uint32_t)idx < n )
                tree->components[idx].is_dirty = 1;
        }
    }
}
