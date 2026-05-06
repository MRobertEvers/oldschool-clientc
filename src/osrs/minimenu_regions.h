#ifndef MINIMENU_REGIONS_H
#define MINIMENU_REGIONS_H

/** openMenu / drawMinimenu regions (canvas coords). Hit-tests use strict inequalities like Client.ts:
 *  click_x > x0 && click_y > y0 && click_x < x1 && click_y < y1.
 *  place_*_max are local clamp extents for menu placement within each area (two integers per area). */
struct MinimenuIniRegions
{
    int viewport_x0, viewport_y0, viewport_x1, viewport_y1;
    int sidebar_x0, sidebar_y0, sidebar_x1, sidebar_y1;
    int chat_x0, chat_y0, chat_x1, chat_y1;
    int place_vp_max_x, place_vp_max_y;
    int place_sb_max_x, place_sb_max_y;
    int place_ch_max_x, place_ch_max_y;
};

void
minimenu_regions_default(struct MinimenuIniRegions* r);

/** Apply comma-separated overrides (`rect` = x0,y0,x1,y1; `pair` = ax,ay). Starts from
 *  `minimenu_regions_default`, then replaces any component whose string is non-empty and parses
 *  successfully. */
void
minimenu_regions_merge_ini(
    struct MinimenuIniRegions* r,
    const char* region_viewport,
    const char* region_sidebar,
    const char* region_chat,
    const char* place_viewport_max,
    const char* place_sidebar_max,
    const char* place_chat_max);

#endif
