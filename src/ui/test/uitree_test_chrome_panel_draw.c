#include "test_harness.h"

#include "torirs_chrome_panel_draw.h"

static void
test_chrome_prim_from_panel_overlay(void);

void
test_chrome_panel_draw(void)
{
    test_chrome_prim_from_panel_overlay();

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

/*
 * A panel's retained primitives, translated for the in-canvas fallback -- the
 * path that draws a plugin panel with the client's own chrome renderer when
 * there is no separate window to put it in.
 *
 * Two things here are easy to get wrong and invisible when you do. The
 * "nothing to draw" answer is PER KIND, not a shared test: a rect always
 * draws, a text with an empty string draws nothing, and a sprite whose asset
 * has not arrived draws nothing yet. Emit those anyway and the panel grows
 * stray background blocks where its labels are still empty. And the colour
 * carries an alpha byte that chrome does not want, because chrome keeps
 * transparency in its own field -- leaving it in multiplies one by the other,
 * which reads as a panel that is darker than the plugin asked for.
 */
static void
test_chrome_prim_from_panel_overlay(void)
{
    struct UITreeEntityOverlay item;
    struct ToriRSChromePrim prim;

    printf("TEST: panel primitives translated for the canvas fallback\n");

    /* The geometry and the clip come across unchanged -- the transform that
     * places them has already run by here. */
    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_RECT;
    item.x = 11;
    item.y = 22;
    item.w = 33;
    item.h = 44;
    item.clip_x = 5;
    item.clip_y = 6;
    item.clip_w = 7;
    item.clip_h = 8;
    item.trans = 90;
    item.color = 0x7F123456u;
    TEST_ASSERT(ToriRSChromePanelDraw_ToChromePrim(&item, &prim), "a rect draws");
    TEST_ASSERT(prim.kind == TORIRS_CHROME_PRIM_RECT, "a rect is not a chrome rect");
    TEST_ASSERT(prim.filled, "a panel rect came out unfilled");
    TEST_ASSERT(prim.x == 11 && prim.y == 22, "the position did not come across");
    TEST_ASSERT(prim.w == 33 && prim.h == 44, "the size did not come across");
    TEST_ASSERT(
        prim.clip.x == 5 && prim.clip.y == 6 && prim.clip.w == 7 && prim.clip.h == 8,
        "the clip did not come across");
    TEST_ASSERT(prim.trans == 90, "the transparency did not come across");

    /* The alpha byte is dropped, and the rest of the colour is not. */
    TEST_ASSERT(prim.color == 0x00123456u, "the overlay's alpha byte reached the chrome colour");

    /* Text: the string is carried by pointer, and an empty one draws nothing.
     * A text primitive is drawn from its BASELINE, unlike everything else
     * here, which is why the flag is set rather than defaulted. */
    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_TEXT;
    snprintf(item.text, sizeof(item.text), "%s", "Loot");
    TEST_ASSERT(ToriRSChromePanelDraw_ToChromePrim(&item, &prim), "a text with a string draws");
    TEST_ASSERT(prim.kind == TORIRS_CHROME_PRIM_TEXT, "a text is not a chrome text");
    TEST_ASSERT(prim.text && strcmp(prim.text, "Loot") == 0, "the string did not come across");
    TEST_ASSERT(prim.baseline, "panel text is not drawn from its baseline");
    TEST_ASSERT(prim.font_slot == TORIRS_CHROME_FONT_BODY, "panel text is not in the body font");

    item.text[0] = '\0';
    TEST_ASSERT(!ToriRSChromePanelDraw_ToChromePrim(&item, &prim), "an empty label still drew");

    /* Sprite: an unresolved scene id is an asset that has not arrived, not an
     * error, and it draws nothing until it does. */
    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_SPRITE;
    item.scene_id = 77;
    TEST_ASSERT(ToriRSChromePanelDraw_ToChromePrim(&item, &prim), "a resolved sprite draws");
    TEST_ASSERT(prim.kind == TORIRS_CHROME_PRIM_SPRITE, "a sprite is not a chrome sprite");
    TEST_ASSERT(prim.sprite_scene_id == 77, "the sprite's scene id did not come across");
    item.scene_id = 0;
    TEST_ASSERT(!ToriRSChromePanelDraw_ToChromePrim(&item, &prim), "an unresolved sprite drew");
    item.scene_id = -1;
    TEST_ASSERT(!ToriRSChromePanelDraw_ToChromePrim(&item, &prim), "a sprite with no scene drew");

    /* Line: direction and width are its own two fields, and it always draws --
     * a zero-length line is the caller's business, not this translation's. */
    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_LINE;
    item.line_direction = 1;
    item.line_width = 3;
    TEST_ASSERT(ToriRSChromePanelDraw_ToChromePrim(&item, &prim), "a line draws");
    TEST_ASSERT(prim.kind == TORIRS_CHROME_PRIM_LINE, "a line is not a chrome line");
    TEST_ASSERT(prim.line_direction == 1, "the line direction did not come across");
    TEST_ASSERT(prim.line_width == 3, "the line width did not come across");

    /* A world polygon is not a panel primitive. The panel API exposes rect,
     * line, text and image; a polygon in a panel's list came from elsewhere,
     * and all three of its pieces are refused rather than just its opener. */
    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_POLY_BEGIN;
    TEST_ASSERT(!ToriRSChromePanelDraw_ToChromePrim(&item, &prim), "a polygon opener drew");
    item.kind = UITREE_ENTITY_OVERLAY_POLY_POINT;
    TEST_ASSERT(!ToriRSChromePanelDraw_ToChromePrim(&item, &prim), "a polygon point drew");
    item.kind = UITREE_ENTITY_OVERLAY_POLY_END;
    TEST_ASSERT(!ToriRSChromePanelDraw_ToChromePrim(&item, &prim), "a polygon closer drew");

    /* Nothing is carried over between calls: the output is cleared first, so a
     * sprite following a text does not inherit the text's string. */
    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_TEXT;
    snprintf(item.text, sizeof(item.text), "%s", "Loot");
    ToriRSChromePanelDraw_ToChromePrim(&item, &prim);
    memset(&item, 0, sizeof(item));
    item.kind = UITREE_ENTITY_OVERLAY_SPRITE;
    item.scene_id = 5;
    ToriRSChromePanelDraw_ToChromePrim(&item, &prim);
    TEST_ASSERT(!prim.text, "a sprite inherited the previous primitive's string");
    TEST_ASSERT(!prim.baseline, "a sprite inherited the previous primitive's baseline flag");
    TEST_ASSERT(!prim.filled, "a sprite inherited a previous rect's fill flag");
}
