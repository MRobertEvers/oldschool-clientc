#ifndef SRC_UI_UITREE_WORLDMAP_TILE_H
#define SRC_UI_UITREE_WORLDMAP_TILE_H

/*
 * Its own header so the code that DECIDES which regions are on the map, and
 * where, does not have to pull in the whole host interface to say so.
 */

/* One blit on the world map surface: a baked map region, or a map element icon
 * over it. Both are positioned by the host in absolute screen pixels — regions
 * are baked at exactly the view's pixels-per-tile, so nothing scales here — the
 * same division of labour as UITreeEntityOverlay: the host projects, the draw
 * layer draws, and ui/ knows nothing about map coordinates. */
struct UITreeWorldMapTile
{
    int scene_id;
    int atlas_index;
    int x;
    int y;
    int w;
    int h;
    /** Stretch the sprite to w x h rather than blitting it at its own size.
     *  Region tiles set this so a zoom change can keep drawing the bake it
     *  already has, scaled, until the new-zoom bake replaces it. */
    int scaled;
};

#endif
