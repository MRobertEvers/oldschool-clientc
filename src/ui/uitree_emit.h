#ifndef SRC_UITREE_EMIT_H
#define SRC_UITREE_EMIT_H

#include "uitree.h"
#include "uitree_host.h"

#include <stdbool.h>
#include <stdint.h>

/** Draw descriptors produced by a UITree walk. Game resolves scene_ids. */
enum UITreeEmitKind
{
    UITREE_EMIT_NONE = 0,
    UITREE_EMIT_SPRITE,
    UITREE_EMIT_TEXT,
    UITREE_EMIT_RECT,
    UITREE_EMIT_LINE,
    UITREE_EMIT_MODEL,
    UITREE_EMIT_INV_SLOT,
    UITREE_EMIT_CC_OBJ,
    UITREE_EMIT_SCROLLBAR_V,
    UITREE_EMIT_SCROLLBAR_H,
    UITREE_EMIT_WORLD,
    UITREE_EMIT_MINIMAP,
    UITREE_EMIT_COMPASS,
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
    int scene_id;
    int atlas_index;
    int font_id;
    int color;
    int filled;
    int rotation;
    int model_id;
    int model_zoom;
    int model_xan;
    int model_yan;
    int inv_source_id;
    int inv_slot;
    int obj_id;
    int obj_count;
    char const* text;
    int text_center;
    int text_shadowed;
    int text_line_height;
};

/** Fill a single emit descriptor for a node. Returns false if nothing to draw. */
bool
UITree_EmitFill(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeComponent const* component,
    int32_t node_index,
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
 */
void
UITree_EmitWalk(
    struct UITree const* tree,
    struct UITreeHost const* host,
    struct UITreeEmitBuffer* out);

#endif
