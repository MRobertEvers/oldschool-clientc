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
}
