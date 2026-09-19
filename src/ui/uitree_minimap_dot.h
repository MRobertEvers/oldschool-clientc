#ifndef SRC_UI_UITREE_MINIMAP_DOT_H
#define SRC_UI_UITREE_MINIMAP_DOT_H

/*
 * One thing drawn on the minimap, in the minimap's own coordinates.
 *
 * Its own header so the code that DECIDES where a dot goes does not have to
 * pull in the whole host interface to say so.
 */

#include <stdint.h>

/** Minimap overlay dot (reference minimapDrawDot output), host-computed and
 * already rotated: sprite top-left goes at (box_center_x + dx,
 * box_center_y + dy), drawn w*h. scene_id <= 0 draws a filled rect of
 * `color` instead (the local-player white square). */
struct UITreeMinimapDot
{
    int dx;
    int dy;
    int w;
    int h;
    int scene_id;
    int atlas_index;
    uint32_t color;
    /** Sprite-content rotation in 2048-per-turn units, pivoted at the icon
     * centre (a sailing hull's minimap icon turns with its yaw — deob
     * client.method2412). 0 = plain blit. */
    int rotate;
};

#endif
