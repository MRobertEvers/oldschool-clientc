#include "test_harness.h"

#include "torirs_chrome_panel_draw.h"

void
test_chrome_panel_draw(void)
{
    struct ToriRSChromeRect visible = { 100, 200, 60, 40 };
    struct UITreeEntityOverlay item;
    struct UITreeEntityOverlay out;

    printf("TEST: plugin custom-region transform and clipping\n");

    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_RECT;
    item.x = -20;
    item.y = -10;
    item.w = 100;
    item.h = 80;
    TEST_ASSERT(
        ToriRSChromePanelDraw_Transform(&item, 110, 210, 2, visible, &out),
        "an intersecting local primitive transforms");
    TEST_ASSERT(
        out.x == 70 && out.y == 190 && out.w == 200 && out.h == 160,
        "plugin-local logical geometry scales into the pane");
    TEST_ASSERT(
        out.clip_x == 100 && out.clip_y == 200 && out.clip_w == 60 && out.clip_h == 40,
        "an unclipped primitive receives the custom well's visible clip");

    item.clip_x = 5;
    item.clip_y = 6;
    item.clip_w = 10;
    item.clip_h = 11;
    TEST_ASSERT(
        ToriRSChromePanelDraw_Transform(&item, 110, 210, 2, visible, &out),
        "a plugin-supplied clip inside the well transforms");
    TEST_ASSERT(
        out.clip_x == 120 && out.clip_y == 222 && out.clip_w == 20 && out.clip_h == 18,
        "the plugin clip narrows but never enlarges the visible well");

    item.clip_x = 1000;
    item.clip_y = 1000;
    TEST_ASSERT(
        !ToriRSChromePanelDraw_Transform(&item, 110, 210, 2, visible, &out),
        "a primitive whose own clip misses the well is dropped");

    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_LINE;
    item.line_width = 200;
    TEST_ASSERT(
        ToriRSChromePanelDraw_Transform(&item, 110, 210, 2, visible, &out) &&
            out.line_width == 255,
        "line thickness scales and saturates without wrapping");

    TEST_ASSERT(
        !ToriRSChromePanelDraw_Transform(&item, 110, 210, 0, visible, &out),
        "a missing presentation scale cannot leak an untransformed primitive");

    {
        struct ToriRSChromeRect const old_region = { 100, 200, 300, 120 };
        struct ToriRSChromeRect const old_clip = { 100, 210, 300, 100 };
        struct ToriRSChromeRect const scrolled_region = { 100, 180, 300, 120 };
        struct ToriRSChromeRect const scrolled_clip = { 100, 210, 300, 90 };
        struct ToriRSChromeRect const hidden = { 0, 0, 0, 0 };
        struct ToriRSChromeRect const reentered_region = { 100, 140, 300, 120 };
        struct ToriRSChromeRect const reentered_clip = { 100, 210, 300, 50 };
        struct ToriRSChromeRect const resized_region = { 100, 140, 301, 120 };
        unsigned changes;

        changes = ToriRSChromePanelDraw_Changes(
            1, old_region, old_clip, 1, scrolled_region, scrolled_clip);
        TEST_ASSERT(
            (changes & TORIRS_CHROME_PANEL_DRAW_ORIGIN) &&
                (changes & TORIRS_CHROME_PANEL_DRAW_CLIP) &&
                !(changes & TORIRS_CHROME_PANEL_DRAW_SIZE),
            "scroll movement translates and reclips a retained run without redrawing it");

        changes = ToriRSChromePanelDraw_Changes(
            1, scrolled_region, scrolled_clip, 0, hidden, hidden);
        TEST_ASSERT(
            (changes & TORIRS_CHROME_PANEL_DRAW_HIDDEN) &&
                (changes & TORIRS_CHROME_PANEL_DRAW_CLIP) &&
                !(changes & TORIRS_CHROME_PANEL_DRAW_SIZE),
            "a fully scrolled-out well hides but retains its completed run");

        changes = ToriRSChromePanelDraw_Changes(
            1, scrolled_region, hidden, 1, reentered_region, reentered_clip);
        TEST_ASSERT(
            (changes & TORIRS_CHROME_PANEL_DRAW_ORIGIN) &&
                (changes & TORIRS_CHROME_PANEL_DRAW_CLIP) &&
                !(changes & TORIRS_CHROME_PANEL_DRAW_SIZE),
            "a same-sized well re-enters by translating its retained run");

        changes = ToriRSChromePanelDraw_Changes(
            1, reentered_region, reentered_clip, 1, resized_region, reentered_clip);
        TEST_ASSERT(
            (changes & TORIRS_CHROME_PANEL_DRAW_SIZE) != 0,
            "a custom content-width change is the geometry that requests a redraw");
    }
}
