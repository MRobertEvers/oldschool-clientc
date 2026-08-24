#ifndef SRC_UITREE_EMIT_H
#define SRC_UITREE_EMIT_H

#include "uitree.h"
#include "uitree_host.h"

#include <stdbool.h>
#include <stdint.h>

/** Buffer for text after CS1 %N placeholder substitution. */
#define UITREE_EMIT_TEXT_FMT_MAX 128

/** Draw descriptors produced by a UITree walk. Game resolves scene_ids. */
enum UITreeEmitKind
{
    UITREE_EMIT_NONE = 0,
    UITREE_EMIT_SPRITE,
    UITREE_EMIT_TEXT,
    UITREE_EMIT_RECT,
    UITREE_EMIT_LINE,
    UITREE_EMIT_ARC,
    UITREE_EMIT_MODEL,
    UITREE_EMIT_CC_OBJ,
    UITREE_EMIT_SCROLLBAR_V,
    UITREE_EMIT_SCROLLBAR_H,
    UITREE_EMIT_WORLD,
    UITREE_EMIT_MINIMAP,
    UITREE_EMIT_COMPASS,
    UITREE_EMIT_ENTITY_OVERLAY,
    UITREE_EMIT_WORLDMAP,
    UITREE_EMIT_DEBUG_OVERLAY,
};

/** Host request that owns an ENTITY_OVERLAY descriptor's volatile array. */
enum UITreeEmitOverlaySource
{
    UITREE_EMIT_OVERLAY_NONE = 0,
    UITREE_EMIT_OVERLAY_ENTITY,
    UITREE_EMIT_OVERLAY_CANVAS,
    UITREE_EMIT_OVERLAY_FRAME,
};

struct UITreeEmitClip
{
    int x;
    int y;
    int w;
    int h;
};

struct UITreeEmitDesc
{
    enum UITreeEmitKind kind;
    int32_t node_index;
    int component_id;
    int x;
    int y;
    int w;
    int h;
    struct UITreeEmitClip clip;
    int scroll_off_x;
    int scroll_off_y;
    /** For SCROLLBAR_*: content scroll_height (V) or scroll_width (H). */
    int scroll_content;
    int scene_id;
    int atlas_index;
    /** COMPASS/MINIMAP: circular alpha mask (the pack's placeholder graphic);
     *  0 = draw unmasked. Sampled axis-aligned, never rotates with content. */
    int mask_scene_id;
    int mask_atlas_index;
    /** Which of the mask's pixels are the window: 1 = the opaque ones,
     *  0 = the transparent ones. See UITree.mask_keep_opaque. */
    int mask_keep_opaque;
    int font_id;
    int color;
    int filled;
    /** Camera-yaw rotation for compass/minimap chrome (2048 = full turn). */
    int rotation_r2pi2048;
    /** SPRITE: IF3 spriteAngle / CC_SET2DANGLE (65536 = full turn). Rotates the
     *  image about the widget box centre; a different scale to the field above,
     *  and the two never apply to the same emit. */
    int sprite_angle_r2pi65536;
    /** MINIMAP: pivot inside the baked map texture for the camera position. */
    int src_anchor_x;
    int src_anchor_y;
    /** MINIMAP: host-computed dot overlay (players/NPCs/objs/flag) for this
     * frame. Host-owned pointer, valid same-frame only (like `text`). */
    struct UITreeMinimapDot const* minimap_dots;
    int minimap_dot_count;
    /** ENTITY_OVERLAY: host-computed health bars + hitsplats for this frame.
     * Host-owned pointer, same-frame lifetime (like `minimap_dots`). */
    struct UITreeEntityOverlay const* entity_overlays;
    int entity_overlay_count;
    /** Which host list produced entity_overlays; retained refresh must reissue
     *  that exact request because ENTITY, CANVAS, and FRAME layer differently. */
    uint8_t entity_overlay_source;
    /** WORLDMAP: the baked map-surface regions covering the widget this frame,
     * already positioned in absolute screen pixels by the host. Host-owned
     * pointer, same-frame lifetime (like `minimap_dots`). */
    struct UITreeWorldMapTile const* worldmap_tiles;
    int worldmap_tile_count;
    /** DEBUG_OVERLAY: the retained display list, handed on by pointer — it is
     * rebuilt only when a widget changed, so a steady overlay costs one pointer
     * copy per frame. Owned by the host's ToriRSChrome. */
    struct ToriRSChromePrim const* debug_prims;
    int debug_prim_count;
    /** DEBUG_OVERLAY: scene font per enum ToriRSChromeFontSlot. A prim names a slot,
     * not a font, so the desc carries the mapping the host set up. */
    int debug_font_id[TORIRS_CHROME_FONT_SLOT_COUNT];
    /** DEBUG_OVERLAY: the scene the baked chrome skin was uploaded under, and
     * the atlas index within it per enum ToriRSChromeSkinSlot. Same slot-not-id
     * indirection as the fonts above; -1 in either means "no skin", and the
     * chrome has already fallen back to its flat form. */
    int debug_skin_scene_id;
    int debug_skin_atlas[TORIRS_CHROME_SKIN_SLOT_COUNT];
    int model_id;
    int model_zoom;
    int model_xan;
    int model_yan;
    int model_zan;
    int model_x_offset;
    int model_y_offset;
    uint8_t model_orthog;
    uint8_t model_fixed_zoom;
    /* This MODEL node is drawing an OBJ (CC_SETOBJECT), so it wants the obj
     * icon's composition: the model's own vertical centring, which an ordinary
     * widget model does not get because its record already places it. */
    uint8_t model_obj_composed;
    int inv_source_id;
    int inv_slot;
    int obj_id;
    int obj_count;
    char const* text;
    /** %N substitution result. Descs are copied by value into the emit buffer,
     *  so the expansion is stored inline rather than behind a pointer. When the
     *  first byte is non-zero this supersedes `text`. */
    char text_formatted[UITREE_EMIT_TEXT_FMT_MAX];
    int text_center;      /* h_align 0/1/2 */
    int text_y_align;     /* v_align 0/1/2 */
    int text_shadowed;
    int text_line_height;
    /** Baseline text: `y` is the reference `PixFont.drawString` y — the bottom
     *  of the line box, from which the renderer subtracts the font's line
     *  height — instead of the top of a w x h alignment box. Set it for any
     *  draw the reference expresses as `font.drawString(s, x, y)` /
     *  `renderLeft(s, x, y, …)`; `text_center`/`text_y_align`/`w`/`h` do not
     *  apply. Anything else has to guess the font's ascent, and guessing it as
     *  the line height is what put the item stack count three pixels above the
     *  icon it belongs inside. */
    int text_baseline;
    /* Sprite blit params (interfacex-aligned). */
    uint8_t if3;
    uint8_t tiled;
    int outline;
    int graphic_shadow;
    int trans;
    uint8_t flip_h;
    uint8_t flip_v;
    /* Type-9 LINE: cache lineWidth + lineDirection (stored as rs_line.horizontal). */
    int line_width;
    uint8_t line_direction;
    /* Type-10 ARC: the sector's start and end angle, 65536 to a full turn, 0
     * straight up and clockwise. `filled` and `line_width` are shared with RECT
     * and LINE and mean the same things they do there: a filled arc is the whole
     * disc, an unfilled one a `line_width`-pixel band along the arc. */
    int arc_start;
    int arc_end;
    /* WORLD: scene levels the painter may draw (bit per level). */
    uint8_t world_level_mask;
};

/** Fill a single emit descriptor for a node. Returns false if nothing to draw.
 * @param hovered_component_id id currently hovered (-1 = none). Selects over_*
 *        colour/text/sprite variants for the matching component (TS Client draw). */
bool
UITree_EmitFill(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeComponent const* component,
    int32_t node_index,
    int hovered_component_id,
    struct UITreeEmitDesc* out);

struct UITreeEmitBuffer
{
    struct UITreeEmitDesc* cmds;
    int count;
    int cap;
    /** Set by `UITree_EmitWalk` when any desc in `cmds` carries a host-owned
     *  pointer whose lifetime is this frame only — minimap dots, entity
     *  overlays, worldmap tiles, debug prims.
     *
     *  Such a desc can compare byte-identical to last frame's while the buffer
     *  behind the pointer holds entirely different contents, so a list that is
     *  "unchanged" by memcmp is NOT evidence that the pixels are unchanged.
     *  Emit retention must not skip a frame whose previous list had any. This is
     *  computed by testing the pointers, not by listing the kinds that set them,
     *  so a kind added later cannot quietly opt itself out of the check. */
    int volatile_refs;
    /** Bitsets keyed by enum UITreeEmitOverlaySource. `seen` includes a source
     *  that returned zero items, so retained refresh can detect its first item
     *  without putting a no-op descriptor in the renderer's command list. */
    uint8_t volatile_overlay_seen;
    uint8_t volatile_overlay_nonempty;
    /** Fully processed descriptor shapes, including node identity and common
     *  clipping/scroll fields, retained even while a source has zero items. */
    struct UITreeEmitDesc volatile_overlay_template[UITREE_EMIT_OVERLAY_FRAME + 1];
    struct UITreeEmitClip volatile_overlay_enclosing_clip[UITREE_EMIT_OVERLAY_FRAME + 1];
    int volatile_overlay_insert_at[UITREE_EMIT_OVERLAY_FRAME + 1];
    /** Set when at least one volatile desc cannot be re-issued from the desc
     *  alone, so the whole list must be rebuilt by the walk instead of
     *  refreshed. Today that is WORLDMAP, whose desc does not record tiles vs
     *  overview, or a legacy entity-overlay desc with no recorded source. */
    int volatile_unrefreshable;
    /** External host domains actually read while constructing this list, and
     * their versions at publication. Unlike `volatile_refs`, these cover host
     * values copied into ordinary descriptors (camera yaw, selected tab,
     * inventory contents, asset availability, etc.). */
    UITreeHostInputMask host_input_dependencies;
    struct UITreeHostInputStamp host_input_stamp;
};

void
UITree_EmitBufferInit(struct UITreeEmitBuffer* buf);

void
UITree_EmitBufferFree(struct UITreeEmitBuffer* buf);

/** True when every external host input consumed by the last full walk still
 * has the version captured in `buf`. The retained-list gate should require
 * this in addition to tree/layout/hover identity. */
bool
UITree_EmitBufferHostInputsCurrent(
    struct UITreeEmitBuffer const* buf,
    struct UITreeHost const* host);

/**
 * Full DFS walk: fill clip, EmitFill, append drawable cmds.
 * Two passes (non-text then text). No asset backends — scene_ids only.
 * @param hovered_component_id component id that makes hide-gated nodes visible (-1 = none).
 */
void
UITree_EmitWalk(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out,
    int hovered_component_id);

/**
 * Re-issue the host requests behind the same-frame pointers in an already-built
 * list, leaving every other desc untouched.
 *
 * The point of the emit retention gate is to skip the tree walk on a frame where
 * nothing the walk reads has moved. On such a frame most descs are genuinely
 * reusable, but a handful hold host-owned pointers whose *contents* change every
 * frame regardless of the tree — health bars, hitsplats, minimap dots. Those are
 * the reason a byte-identical list is not a byte-identical picture. Refreshing
 * just them costs a few host calls against a walk that visits every node.
 *
 * Requires `volatile_unrefreshable == 0`.
 */
/** Returns 1 when every refreshable volatile source was re-issued in place.
 *  The retained overlay templates also handle empty/non-empty transitions
 *  without exposing empty commands to the renderer. */
int
UITree_EmitRefreshVolatile(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out);

#endif
