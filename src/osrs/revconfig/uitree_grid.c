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
