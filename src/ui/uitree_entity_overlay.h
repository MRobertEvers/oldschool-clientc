#ifndef SRC_UI_UITREE_ENTITY_OVERLAY_H
#define SRC_UI_UITREE_ENTITY_OVERLAY_H

/*
 * Its own header so the code that COLLECTS these primitives does not have to
 * pull in the whole host interface to hold one.
 */

#include <stdint.h>

/** One screen-space primitive of the entity overlay pass (reference
 * drawEntities' health bars + hitmarks, Client.ts:4897-4932). The host
 * projects the entity, applies the reference's per-slot nudges and hands the
 * draw layer flat, already-positioned primitives — ui/ stays leaf and knows
 * nothing about entities or the camera. */
enum UITreeEntityOverlayKind
{
    UITREE_ENTITY_OVERLAY_RECT = 0,
    UITREE_ENTITY_OVERLAY_SPRITE,
    UITREE_ENTITY_OVERLAY_TEXT,
    /** A diagonal of the (x,y,w,h) box: direction 0 runs top-left to
     *  bottom-right, 1 bottom-left to top-right (TORIRSRC_LINE's contract).
     *  Any projected world segment fits by picking box + direction. */
    UITREE_ENTITY_OVERLAY_LINE,
    /* Convex polygon, as a begin / point... / end run.
     *
     * Bracketed rather than one item carrying an array so that each item is
     * still ONE render command: the emit walk produces one command per step,
     * and this keeps a variable-length primitive from needing a sub-step
     * counter threaded through the walk and every backend. `color` and `trans`
     * ride on the BEGIN; the POINTs carry only x and y. */
    UITREE_ENTITY_OVERLAY_POLY_BEGIN,
    UITREE_ENTITY_OVERLAY_POLY_POINT,
    UITREE_ENTITY_OVERLAY_POLY_END,
};

/* Long enough for a full overhead chat line (reference chatMessage); hitsplat
 * numbers use only the first few bytes. */
#define UITREE_ENTITY_OVERLAY_TEXT_LEN 100

struct UITreeEntityOverlay
{
    int kind;
    int x;
    int y;
    int w;
    int h;
    uint32_t color;
    int scene_id;
    int atlas_index;
    int font_id;
    /** SPRITE, RECT and POLY_BEGIN: 0 = opaque, 255 = invisible.
     *  Health bars and plugin fills retain their native transparency. */
    int trans;
    /** Optional extra clip, intersected with the world viewport. A zero `w` or
     *  `h` means "no extra clip", which is what every primitive but the health
     *  bar's filled half wants -- that one is a full-width sprite drawn cut off
     *  at the current fill, exactly as the reference clips it. */
    int clip_x;
    int clip_y;
    int clip_w;
    int clip_h;
    /** LINE only: which diagonal of the box, and its thickness (0 = 1px). */
    uint8_t line_direction;
    uint8_t line_width;
    /** TEXT: centred on x, baseline at y (reference centreString). */
    char text[UITREE_ENTITY_OVERLAY_TEXT_LEN];
};

#endif
