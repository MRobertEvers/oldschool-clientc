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
    /* WORLD: scene levels the painter may draw (bit per level). */
    uint8_t world_level_mask;
    /* WORLD: camera gestures this viewport accepts (revconfig mmb_rotate= /
     * wheel_zoom=). The host reads them off the cached WORLD desc, which is
     * also what gates the pointer to the viewport rect. */
    uint8_t world_mmb_rotate;
    uint8_t world_wheel_zoom;
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
};

void
UITree_EmitBufferInit(struct UITreeEmitBuffer* buf);

void
UITree_EmitBufferFree(struct UITreeEmitBuffer* buf);

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

#endif
