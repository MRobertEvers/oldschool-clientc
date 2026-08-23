#include "3rd/minipt.h"
#include "bmp.h"
#include "buildcache/dat2_buildcache.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "games/ie_enum_lookup.h"
#include "games/ie_param_lookup.h"
#include "games/ie_struct_lookup.h"
#include "interfacex_host_io.h"
#include "ioqueue/libtorirs_io.h"
#include "osrs/rscache/dat2a/dat2a_clientscript.h"
#include "osrs/rscache/dat2a/dat2a_config_npctype.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "osrs/rscache/rscache_unity.h"
#include "runescape/appearance.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_clientscript_convert.h"
#include "toriauxlib/cache/toriauxlibcache_font_convert.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/toriauxlib_tasks.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/core/toriauxlibcore_types.h"
#include "toriauxlib/td/toridraw_cachemodel.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_2d.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_light_model.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_model_sprite.h"
#include "toridraw/toridraw_scene.h"
#include "toridraw/toridraw_sprite.h"
#include "vm/cs2_opcode.h"
#include "vm/cs2_opcode_meta.h"
#include "vm/cs2_script.h"
#include "vm/cs2vmx.h"
#include <sys/stat.h>

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INTERFACEX_DEBUG_OPS 0

static int g_interfacex_write_bmp = 1;
/* g_cs2_trace_mode / g_cs2_trace_extra live in cs2vmx.c */

#define BANK_INTERFACE 12
#define INVENTORY_INTERFACE 630
#define EQUIPMENT_INTERFACE 387

#define CANVAS_W 1024
#define CANVAS_H 768
#define CANVAS_BG 0xFF202428

// portal nexus viewport size
// There is some padding that gets added so we account for that
// It's either 10 or 9 all the way around.
// #define CANVAS_W (492 + 20)
// #define CANVAS_H (314 + 20)

// export const ContentType = {
//     VIEWPORT: 1337, // 3D game viewport
//     MINIMAP: 1338, // Minimap area
//     COMPASS: 1339, // Compass (rotates with camera yaw)
//     WORLDMAP: 1400, // World map
// } as const;
// 1401 is not in ContentType; widgets-gl.ts handles it as the world-map overview pane.
#define INTERFACEX_CONTENT_NONE 0
#define INTERFACEX_CONTENT_WORLD 1337
#define INTERFACEX_CONTENT_MINIMAP 1338
#define INTERFACEX_CONTENT_COMPASS 1339
#define INTERFACEX_CONTENT_WORLDMAP 1400
#define INTERFACEX_CONTENT_WORLDMAP_OVERVIEW 1401

/* Current render clip rect (canvas-space, [x0,y0) .. [x1,y1) ), narrowed while recursing
 * into a scrollable RSLayer's children and restored on the way back out. Rendering is a
 * single-threaded, synchronous depth-first walk, so plain save/restore around each
 * recursive call is sufficient - no need to thread a clip struct through every helper. */
static int g_render_clip_x0 = 0;
static int g_render_clip_y0 = 0;
static int g_render_clip_x1 = CANVAS_W;
static int g_render_clip_y1 = CANVAS_H;

static struct ToriDraw_ViewPort
InterfaceX_RenderViewPort(void)
{
    return (struct ToriDraw_ViewPort){
        .clip_left = g_render_clip_x0,
        .clip_top = g_render_clip_y0,
        .clip_right = g_render_clip_x1,
        .clip_bottom = g_render_clip_y1,
        .stride = CANVAS_W,
    };
}

static inline int
uitree_mul_shift14(
    int a,
    int b)
{
    return (int)(((int64_t)a * (int64_t)b) >> 14);
}

struct UITreeNodeXLink
{
    int parent_tree_idx;
    int next_sibling_tree_idx;
    int first_child_tree_idx;
    int last_child_tree_idx;
};

struct UITreeXNode_RSLayer
{
    int scroll_width;
    int scroll_height;
    int scroll_x;
    int scroll_y;
};

struct UITreeXNode_RSGraphic
{
    int graphic_id;
    int graphic_id2;
    int scene_id;
    int outline;
    int graphic_shadow;
    /** IF1 only: true when cache CS1 comparison scripts all pass (getIfActive). */
    int cs1_active;
};

struct UITreeXNode_RSRect
{
    int color;
    int color2;
    int filled;
};

struct UITreeXNode_RSText
{
    int font_id;
    int color;
    int center;
    int y_align;
    int line_height;
    int shadowed;
    char text[TORIAUXLIBCORE_COMPONENT_TEXT_MAX];
};

struct UITreeXNode_RSObj
{
    int obj_id;
    int obj_count;
    int scene_id;
};

/* Model kinds live in cs2vmx_host.h as CS2VM_ModelKind. */
#define InterfaceX_ModelKind CS2VM_ModelKind
#define INTERFACEX_MODEL_KIND_NONE CS2VM_MODEL_KIND_NONE
#define INTERFACEX_MODEL_KIND_PLAIN CS2VM_MODEL_KIND_PLAIN
#define INTERFACEX_MODEL_KIND_NPC_HEAD CS2VM_MODEL_KIND_NPC_HEAD
#define INTERFACEX_MODEL_KIND_PLAYER_HEAD CS2VM_MODEL_KIND_PLAYER_HEAD
#define INTERFACEX_MODEL_KIND_PLAYER_SELF CS2VM_MODEL_KIND_PLAYER_SELF
#define INTERFACEX_MODEL_KIND_PLAYER_CHATHEAD CS2VM_MODEL_KIND_PLAYER_CHATHEAD

struct UITreeXNode_RSModel
{
    int model_id;
    enum InterfaceX_ModelKind model_kind;
    int zoom;
    /** drawModel2D orientation (modelOrientation / SETMODELANGLE arg 1). */
    int offset_x;
    /** drawModel2D modelOffset (SETMODELANGLE arg 2); cache anInt5921. */
    int offset_y;
    int angle_x;
    int angle_y;
    int angle_z;
    int anim_seq;
    /** 1 = orthographic projection (no z divide); 0 = perspective (objRender / drawModel2D). */
    int orthog;
    /** 1 = zoom3d uses widget zoom (drawModel2DAtZoom); 0 = fixed 512 scale (drawModel2D). */
    int fixed_zoom;
    int16_t cache_short50;
    int16_t cache_short49;
    int32_t cache_an5957;
    int32_t cache_an5920;
    int transparent;
    int item_id;
    int item_count;
    int scene_id;
};

struct UITreeXNode_RSLine
{
    int color;
    int line_width;
    int line_direction;
};

// 0
// Layer (container)
// Yes — scroll lists, tab bodies, nested layouts
// 1
// Legacy / rare
// Seldom (mostly IF1)
// 2
// Inventory grid
// Rarely created dynamically; backpacks etc. are usually static in cache. Item UIs more often use
// type 5 children + cc_setobject
// 3
// Rectangle
// Yes — click zones, highlights, colored boxes
// 4
// Text (font)
// Yes — quest lines, bank text, labels
// 5
// Sprite
// Yes — item icons, spell icons, images
// 6
// Model
// Sometimes — 3D previews
// 7
// Unknown
// Rare
// 8
// Unknown
// Rare
// 9
// Line
// Sometimes — dividers
// 11
// Layer (IF1-style)
// Yes — containers that hold mounted/dynamic children

#define INTERFACEX_OP_SLOTS 10
#define INTERFACEX_OP_LEN 32

enum UITreeXNodeKind
{
    UITreeXNodeKind_Root,
    UITreeXNodeKind_RSLayer,
    UITreeXNodeKind_RSGraphic,
    UITreeXNodeKind_RSRect,
    UITreeXNodeKind_RSText,
    UITreeXNodeKind_RSObj,
    UITreeXNodeKind_RSModel,
    UITreeXNodeKind_RSLine,
};

struct UITreeXNode
{
    int user_id;
    int idx;
    struct UITreeNodeXLink link;
    enum UITreeXNodeKind kind;
    int x;
    int y;
    int w;
    int h;
    int8_t x_mode;
    int8_t y_mode;
    int8_t w_mode;
    int8_t h_mode;
    /* IF3 interface component (modern dat2 layout). When set, type-5 graphics
     * stretch sprites to abs_w/abs_h; when clear (IF1), sprites draw at native size. */
    int if3;
    int hidden;
    char op_base[32];
    char ops[INTERFACEX_OP_SLOTS][INTERFACEX_OP_LEN];
    int tiling;
    int trans;
    int trans_bot;
    int no_click_through;
    int no_scroll_through;
    int pinch_enabled;
    int clickmask;
    int client_code;
    int hflip;
    int vflip;
    int angle_2d;
    int fill_mode;
    int arc_start;
    int arc_end;
    int aspect_w;
    int aspect_h;
    int abs_x;
    int abs_y;
    int abs_w;
    int abs_h;
    int layout_resolved;
    int dynamic;
    int child_index;
    int draggable;
    int drag_parent_uid;
    int drag_child_index;
    int drag_behavior;
    int is_scroll_bar;
    uint8_t drag_dead_zone;
    uint8_t drag_dead_time;
    /* CS2 model fields for non-type-6 widgets (SETMODEL must not clobber rs_rect). */
    struct UITreeXNode_RSModel model_overlay;
    union
    {
        struct UITreeXNode_RSLayer rs_layer;
        struct UITreeXNode_RSGraphic rs_graphic;
        struct UITreeXNode_RSRect rs_rect;
        struct UITreeXNode_RSText rs_text;
        struct UITreeXNode_RSObj rs_obj;
        struct UITreeXNode_RSModel rs_model;
        struct UITreeXNode_RSLine rs_line;
    } u;
};

/* Some onLoad scripts (e.g. the bank's search-slot prefetch loop) create 1200+ dynamic
 * children up front, well past the old cap of 1024. */
#define MAX_NODES 4096
struct UITreeX
{
    int node_count;
    struct UITreeXNode nodes[MAX_NODES];
};

static void
UITreeX_NodeInit(
    struct UITreeXNode* node,
    int idx)
{
    assert(node);
    memset(node, 0, sizeof(struct UITreeXNode));
    node->idx = idx;
    node->user_id = -1;
    node->link.parent_tree_idx = -1;
    node->link.next_sibling_tree_idx = -1;
    node->link.first_child_tree_idx = -1;
    node->link.last_child_tree_idx = -1;
    /* Union is fully zeroed by memset above. Do not touch rs_layer/rs_graphic here:
     * rs_layer.scroll_x aliases rs_model.zoom and rs_graphic.scene_id. */
    node->model_overlay.model_id = -1;
    node->model_overlay.model_kind = INTERFACEX_MODEL_KIND_NONE;
    node->model_overlay.zoom = 2000;
    node->model_overlay.item_id = -1;
    node->model_overlay.scene_id = -1;
}

static struct UITreeXNode_RSModel*
UITreeX_NodeModelMut(struct UITreeXNode* node)
{
    assert(node);
    if( node->kind == UITreeXNodeKind_RSModel )
        return &node->u.rs_model;
    return &node->model_overlay;
}

static struct UITreeXNode_RSModel const*
UITreeX_NodeModel(struct UITreeXNode const* node)
{
    assert(node);
    if( node->kind == UITreeXNodeKind_RSModel )
        return &node->u.rs_model;
    return &node->model_overlay;
}

static int
UITreeX_NodeIsLiveRoot(struct UITreeXNode const* node)
{
    return node->link.parent_tree_idx == -1 && node->user_id != -1;
}

struct UITreeX*
UITreeX_New(void)
{
    struct UITreeX* tree = calloc(1, sizeof(struct UITreeX));
    assert(tree);

    for( int i = 0; i < MAX_NODES; i++ )
        UITreeX_NodeInit(&tree->nodes[i], i);

    return tree;
}

struct UITreeXNode*
UITreeX_NodeEmplace(struct UITreeX* tree)
{
    assert(tree);
    if( tree->node_count >= MAX_NODES )
    {
        fprintf(stderr, "UITreeX_NodeEmplace: node cap %d exceeded\n", MAX_NODES);
        return NULL;
    }
    struct UITreeXNode* node = &tree->nodes[tree->node_count++];
    UITreeX_NodeInit(node, tree->node_count - 1);
    return node;
}

struct UITreeXBuilder_ParentStack
{
    int parent_idx;
    int last_sibling_idx;
};

struct UITreeXBuilder_PendingParent
{
    int child_idx;
    int parent_user_id;
};

enum InterfaceX_PendingAssetKind
{
    INTERFACEX_PENDING_GRAPHIC = 0,
    INTERFACEX_PENDING_FONT,
    INTERFACEX_PENDING_OBJ_ICON,
};

struct InterfaceX_PendingAsset
{
    int node_idx;
    enum InterfaceX_PendingAssetKind kind;
    int id;
    int id2;
    int id3;
    int id4;
};

struct UITreeXBuilder
{
    struct UITreeXBuilder_ParentStack parent_stack[36];
    int parent_stack_top;

    struct UITreeX* tree;

    struct UITreeXBuilder_PendingParent pending_parents[MAX_NODES];
    int pending_parent_count;

    struct InterfaceX_PendingAsset pending_assets[MAX_NODES];
    int pending_asset_count;
};

void
UITreeXBuilder_ParentStackInitNode(struct UITreeXBuilder_ParentStack* parent_stack)
{
    assert(parent_stack);
    memset(parent_stack, 0, sizeof(struct UITreeXBuilder_ParentStack));
    parent_stack->parent_idx = -1;
    parent_stack->last_sibling_idx = -1;
}

void
UITreeXBuilder_Init(
    struct UITreeXBuilder* builder,
    struct UITreeX* tree)
{
    assert(builder);
    assert(tree);
    memset(builder, 0, sizeof(struct UITreeXBuilder));
    builder->tree = tree;
    builder->parent_stack_top = 0;

    for( int i = 0; i < 36; i++ )
        UITreeXBuilder_ParentStackInitNode(&builder->parent_stack[i]);
}

int
UITreeXBuilder_LinkPushSibling(
    struct UITreeXBuilder* builder,
    struct UITreeXNode* node)
{
    assert(builder);

    int parent_idx = builder->parent_stack[builder->parent_stack_top].parent_idx;
    if( parent_idx != -1 )
    {
        int first_child_idx = builder->tree->nodes[parent_idx].link.first_child_tree_idx;
        if( first_child_idx == -1 )
            builder->tree->nodes[parent_idx].link.first_child_tree_idx = node->idx;

        int last_child_idx = builder->tree->nodes[parent_idx].link.last_child_tree_idx;
        if( last_child_idx != -1 )
            builder->tree->nodes[last_child_idx].link.next_sibling_tree_idx = node->idx;

        builder->tree->nodes[parent_idx].link.last_child_tree_idx = node->idx;
        node->link.parent_tree_idx = parent_idx;
    }
    else
    {
        int last_sibling_idx = builder->parent_stack[builder->parent_stack_top].last_sibling_idx;
        if( last_sibling_idx != -1 )
            builder->tree->nodes[last_sibling_idx].link.next_sibling_tree_idx = node->idx;

        builder->parent_stack[builder->parent_stack_top].last_sibling_idx = node->idx;
    }

    return 0;
}

int
UITreeXBuilder_LinkPushParent(
    struct UITreeXBuilder* builder,
    struct UITreeXNode* node)
{
    assert(builder);
    assert(builder->parent_stack_top < 36);

    UITreeXBuilder_LinkPushSibling(builder, node);

    builder->parent_stack_top += 1;
    builder->parent_stack[builder->parent_stack_top].parent_idx = node->idx;

    return 0;
}

int
UITreeXBuilder_SetActiveParentByUserId(
    struct UITreeXBuilder* builder,
    int user_id)
{
    assert(builder);
    assert(builder->parent_stack_top < 36);

    if( user_id == -1 )
    {
        builder->parent_stack_top = 0;
        builder->parent_stack[builder->parent_stack_top].parent_idx = -1;
        builder->parent_stack[builder->parent_stack_top].last_sibling_idx = -1;
        return 0;
    }

    for( int i = 0; i < builder->tree->node_count; i++ )
    {
        if( builder->tree->nodes[i].user_id == user_id )
        {
            builder->parent_stack[builder->parent_stack_top].parent_idx = i;
            builder->parent_stack[builder->parent_stack_top].last_sibling_idx =
                builder->tree->nodes[i].link.last_child_tree_idx;
            return 0;
        }
    }

    builder->parent_stack[builder->parent_stack_top].parent_idx = -1;
    builder->parent_stack[builder->parent_stack_top].last_sibling_idx = -1;
    return 0;
}

static int
UITreeX_FindByUserId(
    struct UITreeX const* tree,
    int user_id);

static void
UITreeXBuilder_AppendChild(
    struct UITreeX* tree,
    int parent_idx,
    int child_idx)
{
    assert(tree);
    assert(parent_idx >= 0 && parent_idx < tree->node_count);
    assert(child_idx >= 0 && child_idx < tree->node_count);

    struct UITreeXNode* parent = &tree->nodes[parent_idx];
    struct UITreeXNode* child = &tree->nodes[child_idx];

    assert(
        child->link.parent_tree_idx == -1 && "UITreeXBuilder_AppendChild: child already linked "
                                             "(duplicate ResolvePendingParents entry?)");

    child->link.parent_tree_idx = parent_idx;

    int last = parent->link.last_child_tree_idx;
    assert(last != child_idx && "UITreeXBuilder_AppendChild: child already last sibling");
    if( last != -1 )
        tree->nodes[last].link.next_sibling_tree_idx = child_idx;
    else
        parent->link.first_child_tree_idx = child_idx;
    parent->link.last_child_tree_idx = child_idx;
}

static void
UITreeXBuilder_EnqueueParent(
    struct UITreeXBuilder* builder,
    int child_idx,
    int parent_user_id)
{
    assert(builder);

    if( parent_user_id < 0 )
        return;

    if( builder->pending_parent_count >= MAX_NODES )
    {
        fprintf(stderr, "UITreeXBuilder: pending parent cap %d exceeded\n", MAX_NODES);
        return;
    }

    struct UITreeXBuilder_PendingParent* entry =
        &builder->pending_parents[builder->pending_parent_count++];
    entry->child_idx = child_idx;
    entry->parent_user_id = parent_user_id;
}

static void
UITreeXBuilder_ResolvePendingParents(struct UITreeXBuilder* builder)
{
    assert(builder);
    assert(builder->tree);

    struct UITreeX* tree = builder->tree;

    for( int i = 0; i < builder->pending_parent_count; i++ )
    {
        struct UITreeXBuilder_PendingParent const* pending = &builder->pending_parents[i];
        int child_idx = pending->child_idx;
        int parent_user_id = pending->parent_user_id;

        if( child_idx < 0 || child_idx >= tree->node_count )
            continue;

        int parent_idx = UITreeX_FindByUserId(tree, parent_user_id);
        if( parent_idx < 0 )
        {
            fprintf(
                stderr,
                "static link NOT-FOUND: child=0x%08x parent=0x%08x (root fallback)\n",
                (unsigned)tree->nodes[child_idx].user_id,
                (unsigned)parent_user_id);
            continue;
        }

        UITreeXBuilder_AppendChild(tree, parent_idx, child_idx);
    }

    builder->pending_parent_count = 0;
}

static void
UITreeXBuilder_RecordPendingAsset(
    struct UITreeXBuilder* builder,
    int node_idx,
    enum InterfaceX_PendingAssetKind kind,
    int id,
    int id2,
    int id3,
    int id4)
{
    assert(builder);
    if( builder->pending_asset_count >= MAX_NODES )
        return;

    struct InterfaceX_PendingAsset* entry =
        &builder->pending_assets[builder->pending_asset_count++];
    entry->node_idx = node_idx;
    entry->kind = kind;
    entry->id = id;
    entry->id2 = id2;
    entry->id3 = id3;
    entry->id4 = id4;
}

int
UITreeXBuilder_PushLayerWithParentUserId(
    struct UITreeXBuilder* builder,
    int user_id,
    int parent_user_id)
{
    assert(builder);
    assert(builder->parent_stack_top < 36);
    struct UITreeXNode* node = UITreeX_NodeEmplace(builder->tree);
    if( !node )
        return -1;

    (void)parent_user_id;

    node->kind = UITreeXNodeKind_RSLayer;
    node->user_id = user_id;

    UITreeXBuilder_EnqueueParent(builder, node->idx, parent_user_id);

    return node->idx;
}

#define UITREEXBUILDER_GRAPHIC_ID_NULL -1

int
UITreeXBuilder_PushGraphicWithParentUserId(
    struct UITreeXBuilder* builder,
    int user_id,
    int graphic_id,
    int parent_user_id)
{
    assert(builder);
    assert(builder->parent_stack_top < 36);
    struct UITreeXNode* node = UITreeX_NodeEmplace(builder->tree);
    if( !node )
        return -1;

    node->kind = UITreeXNodeKind_RSGraphic;
    node->user_id = user_id;

    node->u.rs_graphic.graphic_id = graphic_id;
    node->u.rs_graphic.graphic_id2 = -1;
    node->u.rs_graphic.scene_id = -1;
    node->u.rs_graphic.cs1_active = 0;

    UITreeXBuilder_EnqueueParent(builder, node->idx, parent_user_id);

    return node->idx;
}

int
UITreeXBuilder_PushRectWithParentUserId(
    struct UITreeXBuilder* builder,
    int user_id,
    int parent_user_id)
{
    assert(builder);
    assert(builder->parent_stack_top < 36);
    struct UITreeXNode* node = UITreeX_NodeEmplace(builder->tree);
    if( !node )
        return -1;

    node->kind = UITreeXNodeKind_RSRect;
    node->user_id = user_id;
    node->u.rs_rect.color = 0;
    node->u.rs_rect.color2 = 0;
    node->u.rs_rect.filled = 1;

    UITreeXBuilder_EnqueueParent(builder, node->idx, parent_user_id);

    return node->idx;
}

int
UITreeXBuilder_PushTextWithParentUserId(
    struct UITreeXBuilder* builder,
    int user_id,
    int parent_user_id)
{
    assert(builder);
    assert(builder->parent_stack_top < 36);
    struct UITreeXNode* node = UITreeX_NodeEmplace(builder->tree);
    if( !node )
        return -1;

    node->kind = UITreeXNodeKind_RSText;
    node->user_id = user_id;
    node->u.rs_text.font_id = 0;
    node->u.rs_text.color = 0;
    node->u.rs_text.center = 0;
    node->u.rs_text.y_align = 0;
    node->u.rs_text.line_height = 0;
    node->u.rs_text.shadowed = 0;
    node->u.rs_text.text[0] = '\0';

    UITreeXBuilder_EnqueueParent(builder, node->idx, parent_user_id);

    return node->idx;
}

int
UITreeXBuilder_PushModelWithParentUserId(
    struct UITreeXBuilder* builder,
    int user_id,
    int parent_user_id)
{
    assert(builder);
    assert(builder->parent_stack_top < 36);
    struct UITreeXNode* node = UITreeX_NodeEmplace(builder->tree);
    if( !node )
        return -1;

    node->kind = UITreeXNodeKind_RSModel;
    node->user_id = user_id;
    node->u.rs_model.model_id = -1;
    node->u.rs_model.model_kind = INTERFACEX_MODEL_KIND_NONE;
    node->u.rs_model.zoom = 2000;
    node->u.rs_model.item_id = -1;
    node->u.rs_model.scene_id = -1;

    UITreeXBuilder_EnqueueParent(builder, node->idx, parent_user_id);

    return node->idx;
}

int
UITreeXBuilder_PushLineWithParentUserId(
    struct UITreeXBuilder* builder,
    int user_id,
    int parent_user_id)
{
    assert(builder);
    assert(builder->parent_stack_top < 36);
    struct UITreeXNode* node = UITreeX_NodeEmplace(builder->tree);
    if( !node )
        return -1;

    node->kind = UITreeXNodeKind_RSLine;
    node->user_id = user_id;
    node->u.rs_line.color = 0;
    node->u.rs_line.line_width = 1;
    node->u.rs_line.line_direction = 0;

    UITreeXBuilder_EnqueueParent(builder, node->idx, parent_user_id);

    return node->idx;
}

static char const*
UITreeX_NodeKindStr(enum UITreeXNodeKind kind)
{
    switch( kind )
    {
    case UITreeXNodeKind_Root:
        return "root";
    case UITreeXNodeKind_RSLayer:
        return "layer";
    case UITreeXNodeKind_RSGraphic:
        return "graphic";
    case UITreeXNodeKind_RSRect:
        return "rect";
    case UITreeXNodeKind_RSText:
        return "text";
    case UITreeXNodeKind_RSObj:
        return "obj";
    case UITreeXNodeKind_RSModel:
        return "model";
    case UITreeXNodeKind_RSLine:
        return "line";
    default:
        return "?";
    }
}

static void
UITreeX_UserIdFormat(
    char* buf,
    size_t buf_size,
    int user_id)
{
    if( !buf || buf_size == 0 )
        return;

    if( user_id < 0 )
    {
        snprintf(buf, buf_size, "-1");
        return;
    }

    snprintf(
        buf,
        buf_size,
        "0x%08x (%d<<16|%d)",
        (unsigned)user_id,
        (user_id >> 16) & 0xFFFF,
        user_id & 0xFFFF);
}

/* Union tag guards: set INTERFACEX_UNION_GUARD=1 to log wrong-kind reads/writes. */
static int g_interfacex_union_guard = -1;

static int
UITreeX_UnionGuardEnabled(void)
{
    if( g_interfacex_union_guard < 0 )
        g_interfacex_union_guard = getenv("INTERFACEX_UNION_GUARD") != NULL ? 1 : 0;
    return g_interfacex_union_guard;
}

static void
UITreeX_ReportTagMismatch(
    struct UITreeXNode const* node,
    enum UITreeXNodeKind expected,
    char const* file,
    int line,
    char const* func)
{
    char user_id_buf[48];
    UITreeX_UserIdFormat(user_id_buf, sizeof(user_id_buf), node->user_id);
    fprintf(
        stderr,
        "UITreeX union tag mismatch: node[%d] user_id=%s expected=%s actual=%s at %s:%d:%s\n",
        node->idx,
        user_id_buf,
        UITreeX_NodeKindStr(expected),
        UITreeX_NodeKindStr(node->kind),
        file,
        line,
        func);
}

static void
InterfaceX_ReportOpKindMismatch(
    struct UITreeXNode const* node,
    char const* expected_desc,
    char const* op_name)
{
    if( !node || !UITreeX_UnionGuardEnabled() || !expected_desc || !op_name )
        return;

    char user_id_buf[48];
    UITreeX_UserIdFormat(user_id_buf, sizeof(user_id_buf), node->user_id);
    fprintf(
        stderr,
        "interfacex op kind mismatch: op=%s node[%d] user_id=%s expected=%s actual=%s\n",
        op_name,
        node->idx,
        user_id_buf,
        expected_desc,
        UITreeX_NodeKindStr(node->kind));
}

#define UITreeX_UnionCheckTag(node, expected, file, line, func)                                    \
    do                                                                                             \
    {                                                                                              \
        struct UITreeXNode const* _ug_node = (node);                                               \
        if( _ug_node && UITreeX_UnionGuardEnabled() && _ug_node->kind != (expected) )              \
            UITreeX_ReportTagMismatch(_ug_node, (expected), (file), (line), (func));               \
    } while( 0 )

#define UITREEX_DEFINE_UNION_ACCESSOR(TypeName, Kind, Member)                                      \
    static struct UITreeXNode_##TypeName* UITreeX_Node##TypeName##Mut_at(                          \
        struct UITreeXNode* node, char const* file, int line, char const* func)                    \
    {                                                                                              \
        assert(node);                                                                              \
        UITreeX_UnionCheckTag(node, Kind, file, line, func);                                       \
        return &node->u.Member;                                                                    \
    }                                                                                              \
    static struct UITreeXNode_##TypeName const* UITreeX_Node##TypeName##_at(                       \
        struct UITreeXNode const* node, char const* file, int line, char const* func)              \
    {                                                                                              \
        assert(node);                                                                              \
        UITreeX_UnionCheckTag(node, Kind, file, line, func);                                       \
        return &node->u.Member;                                                                    \
    }

UITREEX_DEFINE_UNION_ACCESSOR(
    RSLayer,
    UITreeXNodeKind_RSLayer,
    rs_layer)
UITREEX_DEFINE_UNION_ACCESSOR(
    RSGraphic,
    UITreeXNodeKind_RSGraphic,
    rs_graphic)
UITREEX_DEFINE_UNION_ACCESSOR(
    RSRect,
    UITreeXNodeKind_RSRect,
    rs_rect)
UITREEX_DEFINE_UNION_ACCESSOR(
    RSText,
    UITreeXNodeKind_RSText,
    rs_text)
UITREEX_DEFINE_UNION_ACCESSOR(
    RSObj,
    UITreeXNodeKind_RSObj,
    rs_obj)
UITREEX_DEFINE_UNION_ACCESSOR(
    RSLine,
    UITreeXNodeKind_RSLine,
    rs_line)

#define UITreeX_NodeRSLayer(n) UITreeX_NodeRSLayer##_at((n), __FILE__, __LINE__, __func__)
#define UITreeX_NodeRSLayerMut(n) UITreeX_NodeRSLayer##Mut_at((n), __FILE__, __LINE__, __func__)
#define UITreeX_NodeRSGraphic(n) UITreeX_NodeRSGraphic##_at((n), __FILE__, __LINE__, __func__)
#define UITreeX_NodeRSGraphicMut(n) UITreeX_NodeRSGraphic##Mut_at((n), __FILE__, __LINE__, __func__)
#define UITreeX_NodeRSRect(n) UITreeX_NodeRSRect##_at((n), __FILE__, __LINE__, __func__)
#define UITreeX_NodeRSRectMut(n) UITreeX_NodeRSRect##Mut_at((n), __FILE__, __LINE__, __func__)
#define UITreeX_NodeRSText(n) UITreeX_NodeRSText##_at((n), __FILE__, __LINE__, __func__)
#define UITreeX_NodeRSTextMut(n) UITreeX_NodeRSText##Mut_at((n), __FILE__, __LINE__, __func__)
#define UITreeX_NodeRSObj(n) UITreeX_NodeRSObj##_at((n), __FILE__, __LINE__, __func__)
#define UITreeX_NodeRSObjMut(n) UITreeX_NodeRSObj##Mut_at((n), __FILE__, __LINE__, __func__)
#define UITreeX_NodeRSLine(n) UITreeX_NodeRSLine##_at((n), __FILE__, __LINE__, __func__)
#define UITreeX_NodeRSLineMut(n) UITreeX_NodeRSLine##Mut_at((n), __FILE__, __LINE__, __func__)

#undef UITREEX_DEFINE_UNION_ACCESSOR

static void
UITreeX_PrintNode(
    struct UITreeX const* tree,
    int node_idx,
    int depth)
{
    assert(tree);
    assert(node_idx >= 0 && node_idx < tree->node_count);

    struct UITreeXNode const* node = &tree->nodes[node_idx];
    char user_id_buf[48];

    for( int i = 0; i < depth * 2; i++ )
        putchar(' ');

    UITreeX_UserIdFormat(user_id_buf, sizeof(user_id_buf), node->user_id);
    printf(
        "[%d] kind=%s trans=%d fill_mode=%d user_id=%s %s abs=%d,%d %dx%d hidden=%d",
        node_idx,
        UITreeX_NodeKindStr(node->kind),
        node->trans,
        node->fill_mode,
        user_id_buf,
        node->dynamic ? "dynamic" : "static",
        node->abs_x,
        node->abs_y,
        node->abs_w,
        node->abs_h,
        node->hidden);

    if( node->if3 )
    {
        int parent_w = 0;
        int parent_h = 0;
        if( node->link.parent_tree_idx >= 0 )
        {
            struct UITreeXNode const* parent = &tree->nodes[node->link.parent_tree_idx];
            parent_w = parent->abs_w;
            parent_h = parent->abs_h;
        }
        printf(
            " if3 x=%d y=%d xm=%d ym=%d wm=%d hm=%d w=%d h=%d aspect=%d:%d parent=%dx%d",
            node->x,
            node->y,
            node->x_mode,
            node->y_mode,
            node->w_mode,
            node->h_mode,
            node->w,
            node->h,
            node->aspect_w,
            node->aspect_h,
            parent_w,
            parent_h);
    }

    if( node->kind == UITreeXNodeKind_RSGraphic )
    {
        struct UITreeXNode_RSGraphic const* graphic = UITreeX_NodeRSGraphic(node);
        printf(
            " graphic=%d scene_id=%d angle=%d tiling=%d",
            graphic->graphic_id,
            graphic->scene_id,
            node->angle_2d,
            node->tiling);
    }
    else if( node->kind == UITreeXNodeKind_RSObj )
    {
        struct UITreeXNode_RSObj const* obj = UITreeX_NodeRSObj(node);
        printf(" obj=%d count=%d scene_id=%d", obj->obj_id, obj->obj_count, obj->scene_id);
    }
    else if( node->kind == UITreeXNodeKind_RSModel )
    {
        struct UITreeXNode_RSModel const* model = UITreeX_NodeModel(node);
        printf(
            " model=%d kind=%d scene_id=%d zoom=%d xan=%d yan=%d zan=%d orient=%d offset=%d "
            "orthog=%d "
            "fixed_zoom=%d short50=%d short49=%d an5957=%d an5920=%d",
            model->model_id,
            (int)model->model_kind,
            model->scene_id,
            model->zoom,
            model->angle_x,
            model->angle_y,
            model->angle_z,
            model->offset_x,
            model->offset_y,
            model->orthog,
            model->fixed_zoom,
            (int)model->cache_short50,
            (int)model->cache_short49,
            model->cache_an5957,
            model->cache_an5920);
    }
    else if( node->kind == UITreeXNodeKind_RSLine )
    {
        struct UITreeXNode_RSLine const* line = UITreeX_NodeRSLine(node);
        printf(" color=0x%x width=%d dir=%d", line->color, line->line_width, line->line_direction);
    }
    else if( node->kind == UITreeXNodeKind_RSRect )
    {
        struct UITreeXNode_RSRect const* rect = UITreeX_NodeRSRect(node);
        printf(" color=0x%x color2=0x%x filled=%d", rect->color, rect->color2, rect->filled);
    }
    else if( node->kind == UITreeXNodeKind_RSText )
    {
        struct UITreeXNode_RSText const* text = UITreeX_NodeRSText(node);
        printf(
            " font=%d color=0x%x x_align=%d y_align=%d line_h=%d text=\"%s\"",
            text->font_id,
            text->color,
            text->center,
            text->y_align,
            text->line_height,
            text->text);
    }
    else if( node->kind == UITreeXNodeKind_RSLayer )
    {
        struct UITreeXNode_RSLayer const* layer = &node->u.rs_layer;
        printf(" scroll=%dx%d", layer->scroll_width, layer->scroll_height);
    }

    putchar('\n');

    for( int child = node->link.first_child_tree_idx; child != -1;
         child = tree->nodes[child].link.next_sibling_tree_idx )
        UITreeX_PrintNode(tree, child, depth + 1);
}

void
UITreeX_PrintNodes(struct UITreeX const* tree)
{
    if( !tree )
    {
        printf("UITreeX_PrintNodes: tree is NULL\n");
        return;
    }

    printf("uitreex: %d nodes\n", tree->node_count);

    for( int i = 0; i < tree->node_count; i++ )
    {
        if( UITreeX_NodeIsLiveRoot(&tree->nodes[i]) )
            UITreeX_PrintNode(tree, i, 0);
    }
}

/**
 * Magic number constants used in script args for runtime substitution
 * These are special values that get replaced with actual event data at execution time
 */
//  export const ScriptArgMagic = {
//     MOUSE_X: -2147483647, // Integer.MIN_VALUE + 1
//     MOUSE_Y: -2147483646, // Integer.MIN_VALUE + 2
//     WIDGET_ID: -2147483645, // Integer.MIN_VALUE + 3
//     OP_INDEX: -2147483644, // Integer.MIN_VALUE + 4
//     WIDGET_CHILD_INDEX: -2147483643, // Integer.MIN_VALUE + 5
//     DRAG_TARGET_ID: -2147483642, // Integer.MIN_VALUE + 6
//     DRAG_TARGET_CHILD_INDEX: -2147483641, // Integer.MIN_VALUE + 7
//     KEY_TYPED: -2147483640, // Integer.MIN_VALUE + 8
//     KEY_PRESSED: -2147483639, // Integer.MIN_VALUE + 9
//     OP_SUBINDEX: -2147483638, // Integer.MIN_VALUE + 10
// } as const;

#define CACHE_PATH "/Users/matthewevers/Documents/git_repos/3draster/cache"

#define INTERFACEX_SCRIPT_QUEUE_MAX 8192
#define INTERFACEX_INV_TRANSMIT_HOOK_MAX 32
#define INTERFACEX_VAR_TRANSMIT_HOOK_MAX 32
#define INTERFACEX_VAR_TRANSMIT_TRIGGER_MAX 8
#define INTERFACEX_VARC_INT_MAX 256
#define INTERFACEX_VARC_STRING_MAX 64
#define INTERFACEX_VARC_STRING_LEN 128
#define INTERFACEX_INV_TRANSMIT_INT_ARG_MAX 16
#define INTERFACEX_INV_TRANSMIT_TRIGGER_MAX 8

#define INTERFACEX_INV_CONTAINER_MAX 8
#define INTERFACEX_INV_SLOT_MAX 2048
#define INTERFACEX_OBJ_ICON_CACHE_MAX 128
#define INTERFACEX_INV_CONTAINER_WORN 94
#define INTERFACEX_INV_CONTAINER_BACKPACK 93
#define INTERFACEX_MAX_LOADED_GROUPS 64

struct InterfaceX_InvSlot
{
    int obj_id;
    int count;
};

struct InterfaceX_InvContainer
{
    int inv_id;
    int size;
    struct InterfaceX_InvSlot slots[INTERFACEX_INV_SLOT_MAX];
};

struct InterfaceX_ObjIconCacheEntry
{
    int obj_id;
    int scene_id;
    struct InterfaceX_ObjIconCacheEntry* next;
};

struct InterfaceX_GraphicSceneCacheEntry
{
    int graphic_id;
    int scene_id;
    struct InterfaceX_GraphicSceneCacheEntry* next;
};

struct InterfaceX_ScriptQueueEntry
{
    int script_id;
    int component_id;
    int int_args[TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX];
    int int_arg_count;
    char* string_args[TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX];
    int string_arg_count;
};

struct InterfaceX_InvTransmitHook
{
    int component_id;
    int script_id;
    int int_args[INTERFACEX_INV_TRANSMIT_INT_ARG_MAX];
    int int_arg_count;
    int trigger_ids[INTERFACEX_INV_TRANSMIT_TRIGGER_MAX];
    int trigger_count;
};

struct InterfaceX_VarTransmitHook
{
    int component_id;
    int script_id;
    int int_args[INTERFACEX_INV_TRANSMIT_INT_ARG_MAX];
    int int_arg_count;
    int trigger_ids[INTERFACEX_VAR_TRANSMIT_TRIGGER_MAX];
    int trigger_count;
};

struct InterfaceX_ParamType
{
    int id;
    bool is_string;
    int default_int;
    char* default_string;
};

struct InterfaceX_ObjTypeParam
{
    int key;
    bool is_string;
    int int_value;
    char* str_value;
};

struct InterfaceX_ObjType
{
    int id;
    struct InterfaceX_ObjTypeParam* params;
    int param_count;
};

struct InterfaceX_LoadedInterface
{
    int interface_id;
    int component_count;
    struct ToriAuxLibCore_Component** components;
    bool owns_components;
};

struct InterfaceX_VMHost
{
    struct ToriDraw_Scene* scene;
    struct InterfaceX_HostIO host_io;
    struct RSCacheDat2Disk* disk_cache;

    struct UITreeXBuilder* builder;
    struct UITreeX* tree;
    int interface_id;
    uint16_t next_dynamic_uid;

    struct InterfaceX_LoadedInterface extra_groups[INTERFACEX_MAX_LOADED_GROUPS];
    int extra_group_ids[INTERFACEX_MAX_LOADED_GROUPS];
    int extra_group_count;

    struct InterfaceX_InvTransmitHook inv_transmit_hooks[INTERFACEX_INV_TRANSMIT_HOOK_MAX];
    int inv_transmit_hook_count;

    struct InterfaceX_VarTransmitHook var_transmit_hooks[INTERFACEX_VAR_TRANSMIT_HOOK_MAX];
    int var_transmit_hook_count;

    struct InterfaceX_ScriptQueueEntry* script_queue;
    int script_queue_head;
    int script_queue_count;

    struct InterfaceX_InvContainer inv_containers[INTERFACEX_INV_CONTAINER_MAX];
    int inv_container_count;

    int varc_int[INTERFACEX_VARC_INT_MAX];
    char varc_string[INTERFACEX_VARC_STRING_MAX][INTERFACEX_VARC_STRING_LEN];

    int next_scene_id;
    int client_clock;

    int player_appearance[RUNESCAPE_APPEARANCE_SLOT_COUNT];

    struct InterfaceX_ModelLoadRequest
    {
        int user_id;
        struct InterfaceX_ModelLoadArg arg;
    } model_load_queue[MAX_NODES];
    int model_load_count;

    bool primary_pack_integrated;

    bool has_pending_host_request;
    struct CS2VM_HostRequest pending_host_request;
};

static void
UITreeXBuilder_LoadPendingAssets(
    struct InterfaceX_VMHost* host,
    struct UITreeXBuilder* builder)
{
    assert(host);
    assert(builder);

    for( int i = 0; i < builder->pending_asset_count; i++ )
    {
        struct InterfaceX_PendingAsset const* pending = &builder->pending_assets[i];
        if( pending->node_idx < 0 || pending->node_idx >= builder->tree->node_count )
            continue;

        struct UITreeXNode* node = &builder->tree->nodes[pending->node_idx];

        switch( pending->kind )
        {
        case INTERFACEX_PENDING_GRAPHIC:
        {
            int scene_id = -1;
            if( !InterfaceX_HostIO_GraphicSceneId(&host->host_io, pending->id, &scene_id) )
                InterfaceX_HostIO_LoadGraphicScene(&host->host_io, pending->id, &scene_id);
            if( scene_id >= 0 && node->kind == UITreeXNodeKind_RSGraphic )
                UITreeX_NodeRSGraphicMut(node)->scene_id = scene_id;
            break;
        }
        case INTERFACEX_PENDING_FONT:
            InterfaceX_HostIO_LoadSceneFont(&host->host_io, pending->id);
            break;
        case INTERFACEX_PENDING_OBJ_ICON:
        {
            int scene_id = -1;
            if( !InterfaceX_HostIO_ObjIconSceneId(
                    &host->host_io, pending->id, pending->id2, &scene_id) )
                InterfaceX_HostIO_LoadObjIconScene(
                    &host->host_io, pending->id, pending->id2, &scene_id);
            if( scene_id >= 0 )
            {
                if( node->kind == UITreeXNodeKind_RSObj )
                    UITreeX_NodeRSObjMut(node)->scene_id = scene_id;
                else if( node->kind == UITreeXNodeKind_RSGraphic )
                    UITreeX_NodeRSGraphicMut(node)->scene_id = scene_id;
            }
            break;
        }
        }
    }
}

static void
InterfaceX_ModelNodeInvalidateScene(struct UITreeXNode* node);

static void
UITreeX_ResolveModelSceneIds(
    struct UITreeX* tree,
    struct InterfaceX_BatchModelLoad const* batch);

static bool
InterfaceX_ModelLoadArgFromNode(
    struct InterfaceX_VMHost* host,
    struct UITreeXNode const* node,
    struct InterfaceX_ModelLoadArg* out)
{
    assert(host);
    assert(node);
    assert(out);

    if( node->kind != UITreeXNodeKind_RSModel )
        return false;

    struct UITreeXNode_RSModel const* model = UITreeX_NodeModel(node);

    memset(out, 0, sizeof(*out));
    out->user_id = node->user_id;

    if( model->item_id >= 0 )
    {
        out->kind = INTERFACEX_MLOAD_OBJ;
        out->u.obj_id = model->item_id;
    }
    else if( model->model_kind == INTERFACEX_MODEL_KIND_PLAIN )
    {
        if( model->model_id < 0 )
            return false;
        out->kind = INTERFACEX_MLOAD_PLAIN;
        out->u.model_id = model->model_id;
    }
    else if( model->model_kind == INTERFACEX_MODEL_KIND_NPC_HEAD )
    {
        if( model->model_id < 0 )
            return false;
        out->kind = INTERFACEX_MLOAD_NPC;
        out->u.npc_id = model->model_id;
    }
    else if(
        model->model_kind == INTERFACEX_MODEL_KIND_PLAYER_HEAD ||
        model->model_kind == INTERFACEX_MODEL_KIND_PLAYER_SELF ||
        model->model_kind == INTERFACEX_MODEL_KIND_PLAYER_CHATHEAD )
    {
        out->kind = INTERFACEX_MLOAD_PLAYER;
        memcpy(
            out->u.appearance,
            host->player_appearance,
            (size_t)RUNESCAPE_APPEARANCE_SLOT_COUNT * sizeof(int));
    }
    else
        return false;

    return true;
}

static bool
InterfaceX_ModelLoadArgsEqual(
    struct InterfaceX_ModelLoadArg const* a,
    struct InterfaceX_ModelLoadArg const* b)
{
    assert(a);
    assert(b);

    if( a->kind != b->kind )
        return false;

    switch( a->kind )
    {
    case INTERFACEX_MLOAD_PLAIN:
        return a->u.model_id == b->u.model_id;
    case INTERFACEX_MLOAD_NPC:
        return a->u.npc_id == b->u.npc_id;
    case INTERFACEX_MLOAD_OBJ:
        return a->u.obj_id == b->u.obj_id;
    case INTERFACEX_MLOAD_PLAYER:
        return memcmp(
                   a->u.appearance,
                   b->u.appearance,
                   (size_t)RUNESCAPE_APPEARANCE_SLOT_COUNT * sizeof(int)) == 0;
    default:
        return false;
    }
}

static void
InterfaceX_EnqueueModelNodeLoad(
    struct InterfaceX_VMHost* host,
    struct UITreeXNode* node)
{
    assert(host);
    assert(node);

    struct InterfaceX_ModelLoadArg arg;
    if( !InterfaceX_ModelLoadArgFromNode(host, node, &arg) )
        return;

    for( int i = 0; i < host->model_load_count; i++ )
    {
        if( host->model_load_queue[i].user_id != arg.user_id )
            continue;

        bool changed = !InterfaceX_ModelLoadArgsEqual(&host->model_load_queue[i].arg, &arg);
        host->model_load_queue[i].arg = arg;
        if( changed )
            InterfaceX_ModelNodeInvalidateScene(node);
        return;
    }

    if( host->model_load_count >= MAX_NODES )
        return;

    host->model_load_queue[host->model_load_count].user_id = arg.user_id;
    host->model_load_queue[host->model_load_count].arg = arg;
    host->model_load_count++;
    InterfaceX_ModelNodeInvalidateScene(node);
}

static void
InterfaceX_FlushModelLoads(struct InterfaceX_VMHost* host)
{
    assert(host);

    if( host->model_load_count <= 0 )
        return;

    struct InterfaceX_ModelLoadArg args[MAX_NODES];
    for( int i = 0; i < host->model_load_count; i++ )
        args[i] = host->model_load_queue[i].arg;

    int arg_count = host->model_load_count;
    host->model_load_count = 0;

    struct InterfaceX_BatchModelLoad* batch =
        InterfaceX_BatchModelLoad_New(&host->host_io, args, arg_count);
    if( !batch )
        return;

    InterfaceX_BatchModelLoad_Queue(batch, &host->host_io);
    InterfaceX_HostIO_DrainTasks(&host->host_io);
    UITreeX_ResolveModelSceneIds(host->tree, batch);
    InterfaceX_BatchModelLoad_Destroy(batch);
}

static void
UITreeX_ResolveModelSceneIds(
    struct UITreeX* tree,
    struct InterfaceX_BatchModelLoad const* batch)
{
    assert(tree);

    if( !batch )
        return;

    int result_count = 0;
    struct InterfaceX_ModelLoadResult const* results =
        InterfaceX_BatchModelLoad_Results(batch, &result_count);
    if( !results || result_count <= 0 )
        return;

    for( int i = 0; i < result_count; i++ )
    {
        int node_idx = UITreeX_FindByUserId(tree, results[i].user_id);
        if( node_idx < 0 || node_idx >= tree->node_count )
            continue;

        struct UITreeXNode* node = &tree->nodes[node_idx];
        if( node->kind != UITreeXNodeKind_RSModel )
            continue;

        UITreeX_NodeModelMut(node)->scene_id = results[i].scene_id;
    }
}

static int
UITreeX_FindByUserId(
    struct UITreeX const* tree,
    int user_id)
{
    if( !tree || user_id < 0 )
        return -1;

    for( int i = 0; i < tree->node_count; i++ )
    {
        if( tree->nodes[i].user_id == user_id )
            return i;
    }
    return -1;
}

static int
UITreeX_ParentComponentId(
    struct UITreeX const* tree,
    int component_id)
{
    int idx = UITreeX_FindByUserId(tree, component_id);
    if( idx < 0 )
        return -1;

    int parent_idx = tree->nodes[idx].link.parent_tree_idx;
    if( parent_idx < 0 )
        return -1;

    return tree->nodes[parent_idx].user_id;
}

/* Host-side: collect dynamic child indices (was CS2VMX helper). */
static int
CS2VMX_CollectDynamicChildIndices(
    struct UITreeX* tree,
    int parent_component_id,
    int start_index,
    int* out_indices,
    int out_cap)
{
    if( !tree || parent_component_id < 0 || !out_indices || out_cap <= 0 )
        return 0;

    int parent_idx = UITreeX_FindByUserId(tree, parent_component_id);
    if( parent_idx < 0 )
        return 0;

    int count = 0;
    for( int child = tree->nodes[parent_idx].link.first_child_tree_idx; child != -1;
         child = tree->nodes[child].link.next_sibling_tree_idx )
    {
        struct UITreeXNode* c = &tree->nodes[child];
        if( !c->dynamic )
            continue;
        if( c->child_index <= start_index )
            continue;
        if( count < out_cap )
            out_indices[count++] = c->child_index;
    }

    for( int i = 1; i < count; i++ )
    {
        int key = out_indices[i];
        int j = i - 1;
        while( j >= 0 && out_indices[j] > key )
        {
            out_indices[j + 1] = out_indices[j];
            j--;
        }
        out_indices[j + 1] = key;
    }

    return count;
}

static void
UITreeX_InvalidateLayout(struct UITreeX* tree)
{
    assert(tree);
    for( int i = 0; i < tree->node_count; i++ )
        tree->nodes[i].layout_resolved = 0;
}

static int
UITreeX_DimFromParentMode(
    int8_t mode,
    int orig,
    int parent_dim)
{
    switch( mode )
    {
    case 0:
        return orig;
    case 1:
        return parent_dim - orig;
    case 2:
        return uitree_mul_shift14(parent_dim, orig);
    default:
        return orig;
    }
}

static int
UITreeX_AxisFromPositionMode(
    int8_t mode,
    int base,
    int parent_origin,
    int parent_dim,
    int self_dim)
{
    switch( mode )
    {
    case 0:
        return parent_origin + base;
    case 1:
        return parent_origin + ((parent_dim - self_dim) >> 1) + base;
    case 2:
        return parent_origin + parent_dim - base - self_dim;
    case 3:
        return parent_origin + uitree_mul_shift14(parent_dim, base);
    case 4:
        return parent_origin + ((parent_dim - self_dim) >> 1) +
               uitree_mul_shift14(parent_dim, base);
    case 5:
        return parent_origin + parent_dim - uitree_mul_shift14(parent_dim, base) - self_dim;
    default:
        return parent_origin + base;
    }
}

static void
UITreeX_ComputeSize(
    struct UITreeXNode* node,
    int parent_w,
    int parent_h,
    int* out_w,
    int* out_h)
{
    int aspect_w = node->aspect_w > 0 ? node->aspect_w : 1;
    int aspect_h = node->aspect_h > 0 ? node->aspect_h : 1;
    int w = UITreeX_DimFromParentMode(node->w_mode, node->w, parent_w);
    int h = UITreeX_DimFromParentMode(node->h_mode, node->h, parent_h);

    if( node->w_mode == 4 )
        w = aspect_w * h / aspect_h;
    if( node->h_mode == 4 )
        h = aspect_h * w / aspect_w;

    if( w < 0 )
        w = 0;
    if( h < 0 )
        h = 0;
    *out_w = w;
    *out_h = h;
}

static void
UITreeX_LayoutNode(
    struct UITreeX* tree,
    int node_idx,
    int parent_x,
    int parent_y,
    int parent_w,
    int parent_h,
    int is_root,
    int* visiting)
{
    assert(tree);
    assert(visiting);
    assert(node_idx >= 0 && node_idx < tree->node_count);

    if( visiting[node_idx] )
        assert(!"UITreeX_LayoutNode: parent/child cycle");

    visiting[node_idx] = 1;

    struct UITreeXNode* node = &tree->nodes[node_idx];
    int w = 0;
    int h = 0;

    if( node->if3 )
    {
        UITreeX_ComputeSize(node, parent_w, parent_h, &w, &h);
        node->abs_x = UITreeX_AxisFromPositionMode(node->x_mode, node->x, parent_x, parent_w, w);
        node->abs_y = UITreeX_AxisFromPositionMode(node->y_mode, node->y, parent_y, parent_h, h);
    }
    else
    {
        w = node->w;
        h = node->h;
        node->abs_x = parent_x + node->x;
        node->abs_y = parent_y + node->y;
    }

    if( is_root && w == 0 && h == 0 )
    {
        w = parent_w;
        h = parent_h;
    }

    node->abs_w = w;
    node->abs_h = h;
    node->layout_resolved = 1;

    int child_pw = w;
    int child_ph = h;
    int child_px = node->abs_x;
    int child_py = node->abs_y;
    if( node->kind == UITreeXNodeKind_RSLayer )
    {
        struct UITreeXNode_RSLayer const* layer = UITreeX_NodeRSLayer(node);
        if( layer->scroll_width > 0 )
            child_pw = layer->scroll_width;
        if( layer->scroll_height > 0 )
            child_ph = layer->scroll_height;
        /* Scrolled content is laid out at its natural position, then shifted up/left
         * by the scroll offset; UITreeX_RenderNode clips it back to the layer's bounds. */
        child_px -= layer->scroll_x;
        child_py -= layer->scroll_y;
    }

    int first_child = node->link.first_child_tree_idx;
    for( int child = first_child, steps = 0; child != -1; steps++ )
    {
        assert(
            steps < tree->node_count &&
            "UITreeX_LayoutNode: sibling cycle (duplicate AppendChild?)");
        assert(child >= 0 && child < tree->node_count);

        UITreeX_LayoutNode(tree, child, child_px, child_py, child_pw, child_ph, 0, visiting);
        child = tree->nodes[child].link.next_sibling_tree_idx;
    }

    visiting[node_idx] = 0;
}

static void
UITreeX_LayoutResolve(
    struct UITreeX* tree,
    int root_w,
    int root_h)
{
    assert(tree);

    UITreeX_InvalidateLayout(tree);

    int visiting[MAX_NODES] = { 0 };
    for( int i = 0; i < tree->node_count; i++ )
    {
        if( UITreeX_NodeIsLiveRoot(&tree->nodes[i]) )
            UITreeX_LayoutNode(tree, i, 0, 0, root_w, root_h, 1, visiting);
    }
}

static void
CS2VMX_InvalidateComponentIfGone(
    struct CS2VMX* vm,
    struct UITreeX* tree,
    int* component_id)
{
    assert(vm);
    assert(tree);
    assert(component_id);

    if( *component_id < 0 )
        return;

    if( UITreeX_FindByUserId(tree, *component_id) < 0 )
        *component_id = -1;
}

static int
UITreeX_GetLayoutWidth(
    struct UITreeX* tree,
    int user_id)
{
    assert(tree);

    UITreeX_LayoutResolve(tree, CANVAS_W, CANVAS_H);

    int idx = UITreeX_FindByUserId(tree, user_id);
    if( idx < 0 )
        return 0;

    struct UITreeXNode* node = &tree->nodes[idx];
    if( node->layout_resolved && node->abs_w > 0 )
        return node->abs_w;
    return node->w > 0 ? node->w : 0;
}

static int
UITreeX_GetPosX(
    struct UITreeX* tree,
    int user_id)
{
    assert(tree);

    UITreeX_LayoutResolve(tree, CANVAS_W, CANVAS_H);

    int idx = UITreeX_FindByUserId(tree, user_id);
    if( idx < 0 )
        return 0;

    struct UITreeXNode* node = &tree->nodes[idx];
    if( !node->layout_resolved )
        return node->x;

    int parent_abs_x = 0;
    int parent_idx = node->link.parent_tree_idx;
    if( parent_idx >= 0 )
        parent_abs_x = tree->nodes[parent_idx].abs_x;
    return node->abs_x - parent_abs_x;
}

static int
UITreeX_GetPosY(
    struct UITreeX* tree,
    int user_id)
{
    assert(tree);

    UITreeX_LayoutResolve(tree, CANVAS_W, CANVAS_H);

    int idx = UITreeX_FindByUserId(tree, user_id);
    if( idx < 0 )
        return 0;

    struct UITreeXNode* node = &tree->nodes[idx];
    if( !node->layout_resolved )
        return node->y;

    int parent_abs_y = 0;
    int parent_idx = node->link.parent_tree_idx;
    if( parent_idx >= 0 )
        parent_abs_y = tree->nodes[parent_idx].abs_y;
    return node->abs_y - parent_abs_y;
}

static int
UITreeX_GetLayer(
    struct UITreeX* tree,
    int user_id)
{
    assert(tree);

    int idx = UITreeX_FindByUserId(tree, user_id);
    if( idx < 0 )
        return -1;

    int parent_idx = tree->nodes[idx].link.parent_tree_idx;
    if( parent_idx < 0 )
        return -1;

    return tree->nodes[parent_idx].user_id;
}

static int
UITreeX_GetTop(
    struct UITreeX* tree,
    int interface_id)
{
    assert(tree);

    if( interface_id <= 0 )
        return -1;

    int top = interface_id << 16;
    if( UITreeX_FindByUserId(tree, top) >= 0 )
        return top;

    for( int i = 0; i < tree->node_count; i++ )
    {
        struct UITreeXNode* node = &tree->nodes[i];
        if( node->link.parent_tree_idx < 0 && node->user_id >= 0 )
            return node->user_id;
    }

    return -1;
}

static int
UITreeX_GetLayoutHeight(
    struct UITreeX* tree,
    int user_id)
{
    assert(tree);

    UITreeX_LayoutResolve(tree, CANVAS_W, CANVAS_H);

    int idx = UITreeX_FindByUserId(tree, user_id);
    if( idx < 0 )
        return 0;

    struct UITreeXNode* node = &tree->nodes[idx];
    if( node->layout_resolved && node->abs_h > 0 )
        return node->abs_h;
    return node->h > 0 ? node->h : 0;
}

static struct UITreeXNode*
UITreeX_NodeByComponentId(
    struct InterfaceX_VMHost* host,
    int component_id)
{
    assert(host);
    if( component_id < 0 )
        return NULL;

    int idx = UITreeX_FindByUserId(host->tree, component_id);
    if( idx < 0 )
        return NULL;
    return &host->tree->nodes[idx];
}

static int
InterfaceX_VMHost_AllocateDynamicUid(struct InterfaceX_VMHost* host)
{
    assert(host);

    uint16_t next = host->next_dynamic_uid;
    if( next < 0x8000u )
        next = 0x8000u;

    for( int i = 0; i < 0x8000; i++ )
    {
        uint16_t child_id = next;
        int uid = (host->interface_id << 16) | (int)child_id;
        next = (uint16_t)((child_id + 1u) & 0xffffu);
        if( next < 0x8000u )
            next = 0x8000u;
        if( UITreeX_FindByUserId(host->tree, uid) < 0 )
        {
            host->next_dynamic_uid = next;
            return uid;
        }
    }
    return -1;
}

static void
UITreeX_UnlinkChild(
    struct UITreeX* tree,
    int parent_idx,
    int child_idx)
{
    assert(tree);
    assert(parent_idx >= 0 && parent_idx < tree->node_count);
    assert(child_idx >= 0 && child_idx < tree->node_count);

    struct UITreeXNode* parent = &tree->nodes[parent_idx];
    struct UITreeXNode* child = &tree->nodes[child_idx];
    int prev = -1;

    for( int cur = parent->link.first_child_tree_idx; cur != -1;
         cur = tree->nodes[cur].link.next_sibling_tree_idx )
    {
        if( cur == child_idx )
        {
            if( prev == -1 )
                parent->link.first_child_tree_idx = child->link.next_sibling_tree_idx;
            else
                tree->nodes[prev].link.next_sibling_tree_idx = child->link.next_sibling_tree_idx;

            if( parent->link.last_child_tree_idx == child_idx )
                parent->link.last_child_tree_idx = prev;

            child->link.parent_tree_idx = -1;
            child->link.next_sibling_tree_idx = -1;
            return;
        }
        prev = cur;
    }
}

static int
UITreeX_FindDynamicChild(
    struct UITreeX* tree,
    int parent_idx,
    int child_index)
{
    if( !tree || parent_idx < 0 )
        return -1;

    for( int child = tree->nodes[parent_idx].link.first_child_tree_idx; child != -1;
         child = tree->nodes[child].link.next_sibling_tree_idx )
    {
        if( tree->nodes[child].dynamic && tree->nodes[child].child_index == child_index )
            return child;
    }
    return -1;
}

static int
UITreeX_FindChildBySubid(
    struct UITreeX* tree,
    int parent_idx,
    int sub_id)
{
    if( !tree || parent_idx < 0 )
        return -1;

    for( int child = tree->nodes[parent_idx].link.first_child_tree_idx; child != -1;
         child = tree->nodes[child].link.next_sibling_tree_idx )
    {
        struct UITreeXNode* c = &tree->nodes[child];
        if( c->dynamic && c->child_index == sub_id )
            return child;
        if( !c->dynamic && (c->user_id & 0xFFFF) == (sub_id & 0xFFFF) )
            return child;
    }
    return -1;
}

static void
UITreeX_DeleteDynamicChildren(
    struct UITreeX* tree,
    int parent_user_id)
{
    int parent_idx = UITreeX_FindByUserId(tree, parent_user_id);
    if( parent_idx < 0 )
        return;

    for( ;; )
    {
        int victim = -1;
        for( int child = tree->nodes[parent_idx].link.first_child_tree_idx; child != -1;
             child = tree->nodes[child].link.next_sibling_tree_idx )
        {
            if( tree->nodes[child].dynamic )
            {
                victim = child;
                break;
            }
        }
        if( victim < 0 )
            break;
        UITreeX_UnlinkChild(tree, parent_idx, victim);
        tree->nodes[victim].user_id = -1;
    }
}

static enum UITreeXNodeKind
InterfaceX_ComponentTypeToKind(int component_type)
{
    switch( component_type )
    {
    case TORIAUXLIBCORE_COMPONENT_LAYER:
    case TORIAUXLIBCORE_COMPONENT_INV:
        return UITreeXNodeKind_RSLayer;
    case TORIAUXLIBCORE_COMPONENT_RECT:
        return UITreeXNodeKind_RSRect;
    case TORIAUXLIBCORE_COMPONENT_TEXT:
    case TORIAUXLIBCORE_COMPONENT_INV_TEXT:
        return UITreeXNodeKind_RSText;
    case TORIAUXLIBCORE_COMPONENT_GRAPHIC:
        return UITreeXNodeKind_RSGraphic;
    case TORIAUXLIBCORE_COMPONENT_MODEL:
        return UITreeXNodeKind_RSModel;
    case TORIAUXLIBCORE_COMPONENT_LINE:
        return UITreeXNodeKind_RSLine;
    default:
        return UITreeXNodeKind_RSLayer;
    }
}

static void
UITreeX_ApplyComponentGeometry(
    struct UITreeXNode* node,
    struct ToriAuxLibCore_Component const* component)
{
    assert(node);
    assert(component);

    node->if3 = component->if3 ? 1 : 0;
    node->hidden = component->hide ? 1 : 0;
    node->tiling = component->tiled ? 1 : 0;
    node->trans = component->transparency;
    node->kind = InterfaceX_ComponentTypeToKind(component->type);
    node->client_code = component->client_code;
    node->hflip = component->horizontal_flip ? 1 : 0;
    node->vflip = component->vertical_flip ? 1 : 0;
    node->angle_2d = component->sprite_angle;
    node->aspect_w = component->aspect_w > 0 ? component->aspect_w : 1;
    node->aspect_h = component->aspect_h > 0 ? component->aspect_h : 1;

    if( node->kind == UITreeXNodeKind_RSGraphic )
    {
        node->u.rs_graphic.outline = component->outline;
        node->u.rs_graphic.graphic_shadow = component->graphic_shadow;
        node->u.rs_graphic.graphic_id2 = component->graphic_active;
    }
    else if( node->kind == UITreeXNodeKind_RSLayer )
    {
        node->u.rs_layer.scroll_width = component->scroll_width;
        node->u.rs_layer.scroll_height = component->scroll_height;
    }
    else if( node->kind == UITreeXNodeKind_RSRect )
    {
        node->u.rs_rect.color = component->color;
        node->u.rs_rect.color2 =
            component->active_color != 0 ? component->active_color : component->color;
        node->u.rs_rect.filled = component->filled ? 1 : 0;
    }
    else if( node->kind == UITreeXNodeKind_RSText )
    {
        node->u.rs_text.font_id = component->font_id;
        node->u.rs_text.color = component->color;
        node->u.rs_text.center = component->text_h_align;
        node->u.rs_text.y_align = component->text_v_align;
        node->u.rs_text.line_height = component->text_line_height;
        node->u.rs_text.shadowed = component->shadowed ? 1 : 0;
        strncpy(node->u.rs_text.text, component->text, sizeof(node->u.rs_text.text) - 1);
        node->u.rs_text.text[sizeof(node->u.rs_text.text) - 1] = '\0';
    }
    else if( node->kind == UITreeXNodeKind_RSModel )
    {
        node->u.rs_model.model_id = component->model_id;
        node->u.rs_model.model_kind = (enum InterfaceX_ModelKind)component->model_type;
        node->u.rs_model.zoom = component->model_zoom > 0 ? component->model_zoom : 2000;
        node->u.rs_model.angle_x = component->model_xan;
        node->u.rs_model.angle_y = component->model_yan;
        node->u.rs_model.angle_z = component->model_zan;
        node->u.rs_model.offset_x = component->model_x_offset;
        node->u.rs_model.offset_y = component->model_y_offset;
        node->u.rs_model.orthog = component->model_orthog ? 1 : 0;
        node->u.rs_model.fixed_zoom = component->model_fixed_zoom ? 1 : 0;
        node->u.rs_model.cache_short50 = component->model_cache_short50;
        node->u.rs_model.cache_short49 = component->model_cache_short49;
        node->u.rs_model.cache_an5957 = component->model_cache_an5957;
        node->u.rs_model.cache_an5920 = component->model_cache_an5920;
        node->u.rs_model.scene_id = -1;
    }
    else if( node->kind == UITreeXNodeKind_RSLine )
    {
        node->u.rs_line.color = component->color;
        node->u.rs_line.line_width = component->line_width > 0 ? component->line_width : 1;
        node->u.rs_line.line_direction = component->line_horizontal ? 1 : 0;
    }

    if( node->if3 )
    {
        node->x = component->base_x;
        node->y = component->base_y;
        node->w = component->base_width;
        node->h = component->base_height;
        node->x_mode = component->x_mode;
        node->y_mode = component->y_mode;
        node->w_mode = component->width_mode;
        node->h_mode = component->height_mode;
    }
    else
    {
        node->x = component->base_x;
        node->y = component->base_y;
        node->w = component->width;
        node->h = component->height;
        node->x_mode = 0;
        node->y_mode = 0;
        node->w_mode = 0;
        node->h_mode = 0;
    }
}

static struct ToriDraw_Sprite const*
interface_x_scene_sprite_at(
    struct InterfaceX_VMHost* host,
    int scene_id)
{
    if( !host || scene_id < 0 )
        return NULL;

    int sprite_count = 0;
    struct ToriDraw_Sprite** sprites =
        ToriDraw_SceneSpriteGet(host->scene, scene_id, &sprite_count);
    if( !sprites || sprite_count <= 0 || !sprites[0] || !sprites[0]->pixels_argb )
        return NULL;
    return sprites[0];
}

static void
InterfaceX_BlitCompassGraphic(
    struct InterfaceX_VMHost* host,
    int* pixels,
    struct UITreeXNode const* node,
    int node_alpha)
{
    if( !host || !pixels || !node )
        return;

    struct UITreeXNode_RSGraphic const* graphic = UITreeX_NodeRSGraphic(node);
    int mask_id = graphic->graphic_id;
    if( mask_id < 0 )
        return;

    int content_id = graphic->graphic_id2;
    if( content_id < 0 )
        return;

    int mask_scene = graphic->scene_id;
    if( mask_scene < 0 )
        (void)InterfaceX_HostIO_GraphicSceneId(&host->host_io, mask_id, &mask_scene);

    int content_scene = -1;
    (void)InterfaceX_HostIO_GraphicSceneId(&host->host_io, content_id, &content_scene);
    struct ToriDraw_Sprite const* mask_spr = interface_x_scene_sprite_at(host, mask_scene);
    struct ToriDraw_Sprite const* content_spr = interface_x_scene_sprite_at(host, content_scene);
    if( !mask_spr || !content_spr )
        return;

    int mask_w = mask_spr->width > 0 ? mask_spr->width : 1;
    int mask_h = mask_spr->height > 0 ? mask_spr->height : 1;
    int widget_w = node->abs_w > 0 ? node->abs_w : mask_w;
    int widget_h = node->abs_h > 0 ? node->abs_h : mask_h;
    int draw_x = node->abs_x + (widget_w - mask_w) / 2;
    int draw_y = node->abs_y + (widget_h - mask_h) / 2;

    struct ToriDraw_ViewPort view_port = InterfaceX_RenderViewPort();
    ToriDraw2D_BlitArgbRotatedMaskedInverted(
        &view_port,
        draw_x,
        draw_y,
        mask_w,
        mask_h,
        content_spr->pixels_argb,
        content_spr->width,
        content_spr->height,
        mask_spr->pixels_argb,
        mask_w,
        mask_h,
        node->angle_2d,
        TORIDRAW_SPRITE_ANGLE_SCALE,
        node_alpha,
        pixels);
}

/* Composite an expanded sprite buffer back into a nominal canvas, clipping pixels
 * that fall outside. Mirrors the reference pad()/copyNormalized() clamping. */
static uint32_t*
interface_x_sprite_clamp_to_nominal(
    uint32_t const* src,
    int src_w,
    int src_h,
    int src_ox,
    int src_oy,
    int nominal_w,
    int nominal_h)
{
    if( !src || nominal_w <= 0 || nominal_h <= 0 || src_w <= 0 || src_h <= 0 )
        return NULL;

    uint32_t* dst = calloc((size_t)nominal_w * (size_t)nominal_h, sizeof(uint32_t));
    if( !dst )
        return NULL;

    for( int y = 0; y < src_h; y++ )
    {
        int dst_y = y + src_oy;
        if( dst_y < 0 || dst_y >= nominal_h )
            continue;
        for( int x = 0; x < src_w; x++ )
        {
            int dst_x = x + src_ox;
            if( dst_x < 0 || dst_x >= nominal_w )
                continue;
            dst[dst_y * nominal_w + dst_x] = src[y * src_w + x];
        }
    }

    return dst;
}

/* Scales the alpha channel of every pixel in buf by `alpha` (0-255), leaving
 * color untouched. Used to apply a node's `trans` property to cached sprite data
 * without mutating the shared scene cache. No-op when alpha >= 255. */
static void
interface_x_scale_pixel_alpha(
    uint32_t* buf,
    size_t count,
    int alpha)
{
    if( !buf || alpha >= 255 )
        return;
    if( alpha < 0 )
        alpha = 0;

    for( size_t i = 0; i < count; i++ )
    {
        uint32_t p = buf[i];
        int a = (int)((p >> 24) & 0xFF);
        a = (a * alpha) / 255;
        buf[i] = (p & 0x00FFFFFFu) | ((uint32_t)a << 24);
    }
}

/* Blit a type-5 graphic sprite. if3 selects stretch-vs-native drawing:
 * IF3 stretches the nominal sprite to lw x lh; IF1 blits at native size (+ crop offset).
 * alpha (0-255) applies the node's trans property; 255 = fully opaque. */
static void
InterfaceX_BlitSceneSprite(
    int* pixels,
    struct ToriDraw_Sprite const* sprite,
    int dx,
    int dy,
    int lw,
    int lh,
    int tiling,
    int outline,
    int graphic_shadow,
    int if3,
    int alpha,
    int hflip,
    int vflip,
    int angle_2d)
{
    if( !pixels || !sprite || !sprite->pixels_argb || sprite->width <= 0 || sprite->height <= 0 )
        return;

    int nominal_w = sprite->width;
    int nominal_h = sprite->height;
    int sw = nominal_w;
    int sh = nominal_h;
    int ox = sprite->crop_x;
    int oy = sprite->crop_y;
    size_t pixel_count = (size_t)sw * (size_t)sh;

    uint32_t* spr_px = malloc(pixel_count * sizeof(uint32_t));
    if( !spr_px )
        return;

    memcpy(spr_px, sprite->pixels_argb, pixel_count * sizeof(uint32_t));

    if( outline > 0 )
    {
        int sw2 = 0;
        int sh2 = 0;
        uint32_t* outlined = ToriDraw_SpriteNewGraphicOutline(spr_px, sw, sh, outline, &sw2, &sh2);
        if( outlined )
        {
            free(spr_px);
            spr_px = outlined;
            sw = sw2;
            sh = sh2;
            ox -= outline;
            oy -= outline;
        }
    }

    if( graphic_shadow != 0 )
    {
        int sw2 = 0;
        int sh2 = 0;
        uint32_t* shadowed =
            ToriDraw_SpriteNewGraphicShadow(spr_px, sw, sh, graphic_shadow, &sw2, &sh2);
        if( shadowed )
        {
            free(spr_px);
            spr_px = shadowed;
            sw = sw2;
            sh = sh2;
        }
    }

    interface_x_scale_pixel_alpha(spr_px, (size_t)sw * (size_t)sh, alpha);
    int pre_rot_sw = sw;
    int pre_rot_sh = sh;
    int pre_rot_ox = ox;
    int pre_rot_oy = oy;
    struct ToriDraw_ViewPort view_port = InterfaceX_RenderViewPort();

    /* IF3: stretch nominal sprite to widget bounds (lw x lh). IF1: native-size blit below. */
    if( if3 && !tiling )
    {
        ToriDraw_SpriteTransformPixels(&spr_px, &sw, &sh, hflip, vflip, 0);

        uint32_t* clamped =
            interface_x_sprite_clamp_to_nominal(spr_px, sw, sh, ox, oy, nominal_w, nominal_h);
        if( clamped )
        {
            free(spr_px);
            spr_px = clamped;
            sw = nominal_w;
            sh = nominal_h;
            ox = 0;
            oy = 0;
        }

        ToriDraw_SpriteTransformPixels(&spr_px, &sw, &sh, 0, 0, angle_2d);

        int draw_w = lw > 0 ? lw : sw;
        int draw_h = lh > 0 ? lh : sh;
        ToriDraw2D_BlitArgbScaled(&view_port, dx, dy, draw_w, draw_h, spr_px, sw, sh, pixels);
    }
    else
    {
        ToriDraw_SpriteTransformPixels(&spr_px, &sw, &sh, hflip, vflip, angle_2d);

        if( tiling )
            ToriDraw2D_BlitArgbTiled(
                &view_port, dx, dy, lw, lh, spr_px, sw, sh, dx + ox, dy + oy, pixels);
        else
        {
            int draw_x = dx + ox;
            int draw_y = dy + oy;
            if( angle_2d != 0 )
            {
                int center_x = dx + pre_rot_ox + pre_rot_sw / 2;
                int center_y = dy + pre_rot_oy + pre_rot_sh / 2;
                draw_x = center_x - sw / 2;
                draw_y = center_y - sh / 2;
            }
            ToriDraw2D_BlitArgb(&view_port, draw_x, draw_y, spr_px, sw, sh, pixels);
        }
    }
    free(spr_px);
}

static void
InterfaceX_RasterModelNodeToCanvas(
    struct InterfaceX_VMHost* host,
    int* dest,
    int dest_stride,
    struct UITreeXNode const* node,
    int node_alpha);

static void
UITreeX_RenderNodeImpl(
    struct InterfaceX_VMHost* host,
    struct UITreeX const* tree,
    int node_idx,
    int* pixels,
    int text_pass)
{
    assert(tree);
    assert(node_idx >= 0 && node_idx < tree->node_count);

    struct UITreeXNode const* node = &tree->nodes[node_idx];
    if( node->hidden )
        return;

    /* trans: 0 = fully opaque, 255 = fully invisible (client semantics). The node's
     * own content is skipped once fully transparent, but children still recurse -
     * matching the reference client, since child nodes carry their own trans value. */
    int trans = node->trans;
    if( trans < 0 )
        trans = 0;
    else if( trans > 255 )
        trans = 255;
    if( trans >= 255 )
        goto render_children;
    int node_alpha = 255 - trans;

    if( text_pass == 0 )
    {
        if( node->kind == UITreeXNodeKind_RSGraphic && host )
        {
            struct UITreeXNode_RSGraphic const* graphic = UITreeX_NodeRSGraphic(node);

            if( node->client_code == INTERFACEX_CONTENT_COMPASS )
            {
                InterfaceX_BlitCompassGraphic(host, pixels, node, node_alpha);
                goto render_children;
            }

            int chosen_graphic = graphic->graphic_id;
            if( !node->if3 && graphic->cs1_active && graphic->graphic_id2 >= 0 )
                chosen_graphic = graphic->graphic_id2;

            if( chosen_graphic < 0 && graphic->scene_id < 0 )
                goto render_children;

            if( node->client_code == INTERFACEX_CONTENT_MINIMAP )
                goto render_children;

            int scene_id = -1;
            if( graphic->scene_id >= 0 )
                scene_id = graphic->scene_id;
            else if( chosen_graphic >= 0 )
            {
                (void)InterfaceX_HostIO_GraphicSceneId(&host->host_io, chosen_graphic, &scene_id);
                if( scene_id < 0 )
                    (void)InterfaceX_HostIO_ObjIconSceneId(
                        &host->host_io, chosen_graphic, 1, &scene_id);
            }

            if( scene_id < 0 )
                goto render_children;

            int sprite_count = 0;
            struct ToriDraw_Sprite** sprites =
                ToriDraw_SceneSpriteGet(host->scene, scene_id, &sprite_count);
            if( sprites && sprite_count > 0 && sprites[0] && sprites[0]->pixels_argb )
            {
                InterfaceX_BlitSceneSprite(
                    pixels,
                    sprites[0],
                    node->abs_x,
                    node->abs_y,
                    node->abs_w > 0 ? node->abs_w : CANVAS_W,
                    node->abs_h > 0 ? node->abs_h : CANVAS_H,
                    node->tiling,
                    graphic->outline,
                    graphic->graphic_shadow,
                    node->if3,
                    node_alpha,
                    node->hflip,
                    node->vflip,
                    node->angle_2d);
            }
        }
        else if( node->kind == UITreeXNodeKind_RSModel && host )
        {
            struct UITreeXNode_RSModel const* model = UITreeX_NodeModel(node);
            if( model->model_kind == INTERFACEX_MODEL_KIND_PLAIN && model->model_id < 0 )
                goto render_children;
            if( model->model_kind == INTERFACEX_MODEL_KIND_NPC_HEAD && model->model_id < 0 )
                goto render_children;
            InterfaceX_RasterModelNodeToCanvas(host, pixels, CANVAS_W, node, node_alpha);
        }
        else if( node->kind == UITreeXNodeKind_RSLine )
        {
            struct UITreeXNode_RSLine const* line = UITreeX_NodeRSLine(node);
            int px = node->abs_x;
            int py = node->abs_y;
            int pw = node->abs_w;
            int ph = node->abs_h;
            int argb = (node_alpha << 24) | (line->color & 0xFFFFFF);
            int thickness = line->line_width > 0 ? line->line_width : 1;
            int x1;
            int y1;
            int x2;
            int y2;

            /* Type-9 line: endpoints are widget corners; line_direction flips diagonal. */
            if( line->line_direction )
            {
                x1 = px;
                y1 = py + ph;
                x2 = px + pw;
                y2 = py;
            }
            else
            {
                x1 = px;
                y1 = py;
                x2 = px + pw;
                y2 = py + ph;
            }

            struct ToriDraw_ViewPort view_port = InterfaceX_RenderViewPort();
            ToriDraw2D_DrawLine(&view_port, x1, y1, x2, y2, thickness, argb, pixels);
        }
        else if(
            node->kind == UITreeXNodeKind_RSObj && UITreeX_NodeRSObj(node)->obj_id > 0 && host )
        {
            struct UITreeXNode_RSObj const* obj = UITreeX_NodeRSObj(node);
            int scene_id = obj->scene_id;
            if( scene_id < 0 )
                (void)InterfaceX_HostIO_ObjIconSceneId(
                    &host->host_io, obj->obj_id, obj->obj_count, &scene_id);

            if( scene_id >= 0 )
            {
                int sprite_count = 0;
                struct ToriDraw_Sprite** sprites =
                    ToriDraw_SceneSpriteGet(host->scene, scene_id, &sprite_count);
                if( sprites && sprite_count > 0 && sprites[0] && sprites[0]->pixels_argb )
                {
                    struct ToriDraw_Sprite const* spr = sprites[0];
                    int sw = spr->width > 0 ? spr->width : 1;
                    int sh = spr->height > 0 ? spr->height : 1;
                    int bw = node->abs_w > 0 ? node->abs_w : sw;
                    int bh = node->abs_h > 0 ? node->abs_h : sh;
                    struct ToriDraw_ViewPort view_port = InterfaceX_RenderViewPort();

                    if( node_alpha >= 255 )
                    {
                        ToriDraw2D_BlitArgbScaled(
                            &view_port,
                            node->abs_x,
                            node->abs_y,
                            bw,
                            bh,
                            spr->pixels_argb,
                            sw,
                            sh,
                            pixels);
                    }
                    else
                    {
                        /* Sprite pixels are cache-owned; copy before scaling alpha so we
                         * don't mutate data shared with other nodes/frames. */
                        size_t pixel_count = (size_t)sw * (size_t)sh;
                        uint32_t* tmp = malloc(pixel_count * sizeof(uint32_t));
                        if( tmp )
                        {
                            memcpy(tmp, spr->pixels_argb, pixel_count * sizeof(uint32_t));
                            interface_x_scale_pixel_alpha(tmp, pixel_count, node_alpha);
                            ToriDraw2D_BlitArgbScaled(
                                &view_port, node->abs_x, node->abs_y, bw, bh, tmp, sw, sh, pixels);
                            free(tmp);
                        }
                    }
                }
            }
        }
        else if( node->kind == UITreeXNodeKind_RSRect )
        {
            struct UITreeXNode_RSRect const* rect = UITreeX_NodeRSRect(node);
            int px = node->abs_x;
            int py = node->abs_y;
            int pw = node->abs_w > 0 ? node->abs_w : 1;
            int ph = node->abs_h > 0 ? node->abs_h : 1;
            int color = rect->color & 0xFFFFFF;
            int color2 = rect->color2 ? (rect->color2 & 0xFFFFFF) : color;
            int argb = (node_alpha << 24) | color;
            struct ToriDraw_ViewPort view_port = InterfaceX_RenderViewPort();
            if( rect->filled )
            {
                switch( node->fill_mode )
                {
                case 1:
                    ToriDraw2D_FillRectGradientVertical(
                        &view_port, px, py, px + pw, py + ph, color, color2, node_alpha, pixels);
                    break;
                case 2:
                {
                    int trans_bot = node->trans_bot >= 0 ? node->trans_bot : node->trans;
                    int alpha_bot = 255 - trans_bot;
                    ToriDraw2D_FillRectGradientAlpha(
                        &view_port,
                        px,
                        py,
                        px + pw,
                        py + ph,
                        color,
                        color2,
                        node_alpha,
                        alpha_bot,
                        pixels);
                    break;
                }
                default:
                    ToriDraw2D_FillRect(&view_port, px, py, px + pw, py + ph, argb, pixels);
                    break;
                }
            }
            else
                ToriDraw2D_DrawRectOutline(&view_port, px, py, px + pw, py + ph, argb, pixels);
        }
    }
    else if( node->kind == UITreeXNodeKind_RSText && UITreeX_NodeRSText(node)->text[0] && host )
    {
        struct UITreeXNode_RSText const* text = UITreeX_NodeRSText(node);
        int font_id = text->font_id;
        if( font_id < 0 )
            font_id = 495;

        struct ToriDraw_Font* font = InterfaceX_HostIO_SceneFontGet(&host->host_io, font_id);
        if( font )
        {
            struct ToriDraw_ViewPort view_port = InterfaceX_RenderViewPort();

            int lw = node->abs_w;
            int lh = node->abs_h;
            int color = text->color & 0xFFFFFF;
            bool shadowed = text->shadowed != 0;
            bool center = text->center == 1;

            if( lw > 0 && lh > 0 )
            {
                (void)ToriDraw2D_DrawStringBox(
                    font,
                    &view_port,
                    node->abs_x,
                    node->abs_y,
                    lw,
                    lh,
                    text->text,
                    color,
                    text->center,
                    text->y_align,
                    text->line_height,
                    shadowed,
                    pixels);
            }
            else
            {
                int tx = node->abs_x;
                int ty = node->abs_y + (lh > 0 ? lh : font->line_height);
                if( center && lw > 0 )
                {
                    int tw = ToriDraw2D_MeasureString(font, text->text);
                    tx = node->abs_x + (lw - tw) / 2;
                }
                (void)ToriDraw2D_DrawString(
                    font, &view_port, tx, ty, text->text, color, center, shadowed, pixels);
            }
        }
    }

render_children:
{
    int saved_clip_x0 = g_render_clip_x0;
    int saved_clip_y0 = g_render_clip_y0;
    int saved_clip_x1 = g_render_clip_x1;
    int saved_clip_y1 = g_render_clip_y1;

    /* Every positive-size layer clips its children to the layer viewport (OSRS parity).
     * Scroll offsets are handled separately during layout; clipping uses visible bounds.
     * Zero-size layers (pure grouping containers) leave the inherited clip as-is. */
    if( node->kind == UITreeXNodeKind_RSLayer && node->abs_w > 0 && node->abs_h > 0 )
    {
        int lx0 = node->abs_x;
        int ly0 = node->abs_y;
        int lx1 = node->abs_x + node->abs_w;
        int ly1 = node->abs_y + node->abs_h;
        if( lx0 > g_render_clip_x0 )
            g_render_clip_x0 = lx0;
        if( ly0 > g_render_clip_y0 )
            g_render_clip_y0 = ly0;
        if( lx1 < g_render_clip_x1 )
            g_render_clip_x1 = lx1;
        if( ly1 < g_render_clip_y1 )
            g_render_clip_y1 = ly1;
    }

    for( int child = node->link.first_child_tree_idx; child != -1;
         child = tree->nodes[child].link.next_sibling_tree_idx )
        UITreeX_RenderNodeImpl(host, tree, child, pixels, text_pass);

    g_render_clip_x0 = saved_clip_x0;
    g_render_clip_y0 = saved_clip_y0;
    g_render_clip_x1 = saved_clip_x1;
    g_render_clip_y1 = saved_clip_y1;
}
}

static void
UITreeX_RenderNode(
    struct InterfaceX_VMHost* host,
    struct UITreeX const* tree,
    int node_idx,
    int* pixels,
    int text_pass)
{
    UITreeX_RenderNodeImpl(host, tree, node_idx, pixels, text_pass);
}

static void
UITreeX_Render(
    struct InterfaceX_VMHost* host,
    struct UITreeX* tree,
    int* pixels)
{
    assert(tree);
    assert(pixels);

    UITreeX_LayoutResolve(tree, CANVAS_W, CANVAS_H);

    g_render_clip_x0 = 0;
    g_render_clip_y0 = 0;
    g_render_clip_x1 = CANVAS_W;
    g_render_clip_y1 = CANVAS_H;

    for( int i = 0; i < tree->node_count; i++ )
    {
        if( UITreeX_NodeIsLiveRoot(&tree->nodes[i]) )
            UITreeX_RenderNode(host, tree, i, pixels, 0);
    }

    for( int i = 0; i < tree->node_count; i++ )
    {
        if( UITreeX_NodeIsLiveRoot(&tree->nodes[i]) )
            UITreeX_RenderNode(host, tree, i, pixels, 1);
    }
}

static RSCacheDat2A_Component*
component_decode_from_bytes(
    int packed_id,
    char* data,
    int size)
{
    if( !data || size <= 0 )
        return NULL;

    RSCacheDat2A_Component* comp = calloc(1, sizeof(RSCacheDat2A_Component));
    if( !comp )
        return NULL;

    if( packed_id == 1245189 )
    {
        printf("wow");
    }

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, size);
    RSCacheDat2A_ComponentInit(comp);
    comp->id = packed_id;
    if( (unsigned char)data[0] == (unsigned char)255 )
        RSCacheDat2A_ComponentDecodeIf3(comp, &buf);
    else
        RSCacheDat2A_ComponentDecodeIf1(comp, &buf);
    return comp;
}

static int
process_component(
    struct InterfaceX_VMHost* host,
    struct UITreeXBuilder* builder,
    struct ToriAuxLibCore_Component* component)
{
    assert(host);
    assert(builder);
    assert(component);

    int layer = component->parent_id;
    int idx = 0;

    switch( component->type )
    {
    case TORIAUXLIBCORE_COMPONENT_LAYER:
        idx = UITreeXBuilder_PushLayerWithParentUserId(builder, component->id, layer);
        break;
    case TORIAUXLIBCORE_COMPONENT_GRAPHIC:
        idx = UITreeXBuilder_PushGraphicWithParentUserId(
            builder, component->id, component->graphic, layer);
        break;
    case TORIAUXLIBCORE_COMPONENT_RECT:
        idx = UITreeXBuilder_PushRectWithParentUserId(builder, component->id, layer);
        break;
    case TORIAUXLIBCORE_COMPONENT_TEXT:
    case TORIAUXLIBCORE_COMPONENT_INV_TEXT:
        idx = UITreeXBuilder_PushTextWithParentUserId(builder, component->id, layer);
        break;
    case TORIAUXLIBCORE_COMPONENT_MODEL:
        idx = UITreeXBuilder_PushModelWithParentUserId(builder, component->id, layer);
        break;
    case TORIAUXLIBCORE_COMPONENT_LINE:
        idx = UITreeXBuilder_PushLineWithParentUserId(builder, component->id, layer);
        break;
    default:
        idx = UITreeXBuilder_PushLayerWithParentUserId(builder, component->id, layer);
        break;
    }

    if( idx >= 0 && idx < builder->tree->node_count )
    {
        UITreeX_ApplyComponentGeometry(&builder->tree->nodes[idx], component);

        if( component->type == TORIAUXLIBCORE_COMPONENT_GRAPHIC && !component->if3 &&
            component->scripts_count > 0 && component->script_comparator )
        {
            assert(!"interfacex: CS1 getIfActive evaluation is not supported");
        }

        if( component->type == TORIAUXLIBCORE_COMPONENT_GRAPHIC && component->graphic >= 0 )
        {
            UITreeXBuilder_RecordPendingAsset(
                builder, idx, INTERFACEX_PENDING_GRAPHIC, component->graphic, 0, 0, 0);
        }
        else if(
            (component->type == TORIAUXLIBCORE_COMPONENT_TEXT ||
             component->type == TORIAUXLIBCORE_COMPONENT_INV_TEXT) &&
            component->font_id > 0 )
        {
            UITreeXBuilder_RecordPendingAsset(
                builder, idx, INTERFACEX_PENDING_FONT, component->font_id, 0, 0, 0);
        }
        else if( component->type == TORIAUXLIBCORE_COMPONENT_MODEL )
            InterfaceX_EnqueueModelNodeLoad(host, &builder->tree->nodes[idx]);
    }

    return idx;
}

static struct InterfaceX_InvContainer*
InterfaceX_InvContainerGet(
    struct InterfaceX_VMHost* host,
    int inv_id,
    bool create)
{
    assert(host);
    assert(inv_id >= 0);

    for( int i = 0; i < host->inv_container_count; i++ )
    {
        if( host->inv_containers[i].inv_id == inv_id )
            return &host->inv_containers[i];
    }

    if( !create || host->inv_container_count >= INTERFACEX_INV_CONTAINER_MAX )
        return NULL;

    struct InterfaceX_InvContainer* container = &host->inv_containers[host->inv_container_count++];
    memset(container, 0, sizeof(*container));
    container->inv_id = inv_id;
    if( inv_id == INTERFACEX_INV_CONTAINER_WORN )
        container->size = 14;
    else if( inv_id == INTERFACEX_INV_CONTAINER_BACKPACK )
        container->size = 28;
    else
        container->size = 28;
    return container;
}

static void
InterfaceX_InvContainerSetSlot(
    struct InterfaceX_InvContainer* container,
    int slot,
    int obj_id,
    int count)
{
    assert(container);
    assert(slot >= 0 && slot < container->size && slot < INTERFACEX_INV_SLOT_MAX);

    container->slots[slot].obj_id = obj_id > 0 ? obj_id : -1;
    container->slots[slot].count = obj_id > 0 ? (count > 0 ? count : 1) : 0;
}

static void
InterfaceX_InvStoreSeedDefaults(struct InterfaceX_VMHost* host)
{
    assert(host);

    memcpy(
        host->player_appearance,
        RUNESCAPE_EXAMPLE_PLAYER_APPEARANCE,
        sizeof(host->player_appearance));

    struct InterfaceX_InvContainer* worn =
        InterfaceX_InvContainerGet(host, INTERFACEX_INV_CONTAINER_WORN, true);
    struct InterfaceX_InvContainer* backpack =
        InterfaceX_InvContainerGet(host, INTERFACEX_INV_CONTAINER_BACKPACK, true);
    assert(worn);
    assert(backpack);

    for( int slot = 0; slot < worn->size; slot++ )
        InterfaceX_InvContainerSetSlot(worn, slot, -1, 0);
    for( int slot = 0; slot < backpack->size; slot++ )
        InterfaceX_InvContainerSetSlot(backpack, slot, -1, 0);

    /* Sequential seed matching osrs_static_ui.ini [inv:worn] / [inv:inventory]. */
    int const k_worn_items[] = { 1153, 1007, 1725, 1333, 1115, 1201, 1189, 1063, 1067, 2564, 882 };
    for( int i = 0; i < (int)(sizeof(k_worn_items) / sizeof(k_worn_items[0])); i++ )
    {
        if( i >= worn->size )
            break;
        InterfaceX_InvContainerSetSlot(worn, i, k_worn_items[i], 1);
    }

    InterfaceX_InvContainerSetSlot(backpack, 0, 1333, 1);
}

static void
interface_x_bake_model_raster_alpha(
    int* dest,
    int dest_stride,
    int draw_x,
    int draw_y,
    int sw,
    int sh,
    int node_alpha)
{
    int x0 = draw_x;
    int y0 = draw_y;
    int x1 = draw_x + sw;
    int y1 = draw_y + sh;

    if( x0 < g_render_clip_x0 )
        x0 = g_render_clip_x0;
    if( y0 < g_render_clip_y0 )
        y0 = g_render_clip_y0;
    if( x1 > g_render_clip_x1 )
        x1 = g_render_clip_x1;
    if( y1 > g_render_clip_y1 )
        y1 = g_render_clip_y1;
    if( x0 < 0 )
        x0 = 0;
    if( y0 < 0 )
        y0 = 0;
    if( x1 > CANVAS_W )
        x1 = CANVAS_W;
    if( y1 > CANVAS_H )
        y1 = CANVAS_H;
    if( x0 >= x1 || y0 >= y1 )
        return;

    for( int y = y0; y < y1; y++ )
    {
        for( int x = x0; x < x1; x++ )
        {
            int* p = &dest[y * dest_stride + x];
            uint32_t px = (uint32_t)*p;
            if( (px & 0xFFFFFFu) == 0 )
                continue;

            int a = (int)((px >> 24) & 0xFFu);
            if( a == 0 )
                a = 255;
            if( node_alpha < 255 )
                a = (a * node_alpha) / 255;

            *p = (int)((px & 0x00FFFFFFu) | ((uint32_t)a << 24));
        }
    }
}

static int
InterfaceX_NormalizeModelItemZoom(
    int zoom2d,
    int widget_width)
{
    int zoom = zoom2d > 0 ? zoom2d : 2000;
    int width_units = widget_width > 0 ? widget_width : 32;
    int scaled = (zoom * 32) / width_units;
    return scaled > 0 ? scaled : 1;
}

static void
InterfaceX_RasterModelNodeToCanvas(
    struct InterfaceX_VMHost* host,
    int* dest,
    int dest_stride,
    struct UITreeXNode const* node,
    int node_alpha)
{
    assert(host);
    assert(node);
    assert(dest);

    if( node->kind != UITreeXNodeKind_RSModel )
        return;

    struct UITreeXNode_RSModel const* model = UITreeX_NodeModel(node);
    if( model->model_kind == INTERFACEX_MODEL_KIND_PLAIN && model->model_id < 0 )
        return;
    if( model->model_kind == INTERFACEX_MODEL_KIND_NPC_HEAD && model->model_id < 0 )
        return;

    if( model->scene_id < 0 )
    {
        fprintf(
            stderr,
            "interfacex: model scene missing user_id=0x%x kind=%d model_id=%d scene_id=%d\n",
            node->user_id,
            (int)model->model_kind,
            model->model_id,
            model->scene_id);
        return;
    }

    struct ToriDraw_ModelHandle hnd = ToriDraw_SceneModelGet(host->scene, model->scene_id);
    if( hnd.kind != TORIDRAWMK_MODEL )
    {
        fprintf(
            stderr,
            "interfacex: model not in scene user_id=0x%x kind=%d model_id=%d scene_id=%d\n",
            node->user_id,
            (int)model->model_kind,
            model->model_id,
            model->scene_id);
        return;
    }

    int zoom = model->zoom > 0 ? model->zoom : 2000;
    if( model->item_id >= 0 )
    {
        if( node->w_mode != 0 && model->cache_an5957 > 0 )
            zoom = InterfaceX_NormalizeModelItemZoom(zoom, model->cache_an5957);
        else if( node->w > 0 )
            zoom = InterfaceX_NormalizeModelItemZoom(zoom, node->w);
    }

    struct ToriDraw_BoundsCylinder* bounds = ToriDraw_ModelGetBoundsCylinder(hnd);
    int model_center_y = 0;
    if( model->item_id >= 0 && bounds )
        model_center_y = -bounds->min_y / 2;

    ToriDraw_LightModelScene(hnd, 0, 0);

    int draw_x = 0;
    int draw_y = 0;
    int sw = 0;
    int sh = 0;
    bool ok = ToriDraw_RenderModelExtentsAtWidget(
        host->scene,
        hnd,
        zoom,
        model->angle_x,
        model->angle_y,
        model->angle_z,
        model->offset_x,
        model->offset_y,
        model_center_y,
        model->orthog != 0,
        model->fixed_zoom != 0,
        dest,
        dest_stride,
        CANVAS_W,
        CANVAS_H,
        node->abs_x,
        node->abs_y,
        node->abs_w,
        node->abs_h,
        g_render_clip_x0,
        g_render_clip_y0,
        g_render_clip_x1,
        g_render_clip_y1,
        &draw_x,
        &draw_y,
        &sw,
        &sh);

    if( !ok || sw <= 0 || sh <= 0 )
        return;

    interface_x_bake_model_raster_alpha(dest, dest_stride, draw_x, draw_y, sw, sh, node_alpha);
}

static int
InterfaceX_VMHost_Exec_CC_SetObjectOnNode(
    struct InterfaceX_VMHost* host,
    int component_id,
    int obj_id,
    int count)
{
    assert(host);

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, component_id);
    if( !node )
    {
        fprintf(stderr, "failed to resolve node in CC_SetObjectOnNode: %d\n", component_id);
        return CS2VM_EXECNO_ERROR;
    }

    if( obj_id <= 0 )
    {
        fprintf(stderr, "invalid obj_id in CC_SetObjectOnNode: %d\n", obj_id);
        return CS2VM_EXECNO_ERROR;
    }

    switch( node->kind )
    {
    case UITreeXNodeKind_RSObj:
    {
        break;
    }
    case UITreeXNodeKind_RSGraphic:
    {
        break;
    }
    case UITreeXNodeKind_RSModel:
    {
        break;
    }
    default:
        fprintf(stderr, "unexpected node kind in CC_SetObjectOnNode: %d\n", node->kind);
        return CS2VM_EXECNO_ERROR;
    }

    // CC_SETOBJECT/IF_SETOBJECT can target a plain sprite (RSGraphic, e.g. a type-5 slot).
    // node->u is a union keyed by node->kind, so the node must be retagged as RSObj before
    // writing rs_obj fields — otherwise obj_id aliases onto rs_graphic.graphic_id while the
    // node still renders via the RSGraphic path.
    // node->kind = UITreeXNodeKind_RSObj;

    return CS2VM_EXECNO_OK;
}

static void
InterfaceX_SetHookIntLocal(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id,
    int local_idx,
    int argi)
{
    switch( argi )
    {
    case CS2VM_SCRIPT_ARG_WIDGET_ID:
        CS2VMX_SetIntCurrentFrameLocal(vm, local_idx, component_id);
        break;
    case CS2VM_SCRIPT_ARG_WIDGET_CHILD_INDEX:
    {
        int child_index = -1;
        if( host && host->tree )
        {
            int idx = UITreeX_FindByUserId(host->tree, component_id);
            if( idx >= 0 && host->tree->nodes[idx].dynamic )
                child_index = host->tree->nodes[idx].child_index;
        }
        CS2VMX_SetIntCurrentFrameLocal(vm, local_idx, child_index);
        break;
    }
    case CS2VM_SCRIPT_ARG_MOUSE_X:
    case CS2VM_SCRIPT_ARG_MOUSE_Y:
    case CS2VM_SCRIPT_ARG_OP_INDEX:
    case CS2VM_SCRIPT_ARG_DRAG_TARGET_ID:
    case CS2VM_SCRIPT_ARG_DRAG_TARGET_CHILD_INDEX:
    case CS2VM_SCRIPT_ARG_KEY_TYPED:
    case CS2VM_SCRIPT_ARG_KEY_PRESSED:
    case CS2VM_SCRIPT_ARG_OP_SUBINDEX:
        break;
    default:
        CS2VMX_SetIntCurrentFrameLocal(vm, local_idx, argi);
        break;
    }
}

static void
InterfaceX_VMHost_QueueScript(
    struct InterfaceX_VMHost* host,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count,
    char const* const* string_args,
    int string_arg_count)
{
    assert(host);

    if( script_id <= 0 )
        return;

    if( host->script_queue_count >= INTERFACEX_SCRIPT_QUEUE_MAX )
    {
        fprintf(
            stderr,
            "script queue full, dropping script %d for component %d\n",
            script_id,
            component_id);
        return;
    }

    if( !host->script_queue )
        return;

    if( int_arg_count > TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX )
        int_arg_count = TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX;
    if( string_arg_count > TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX )
        string_arg_count = TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX;

    int tail = (host->script_queue_head + host->script_queue_count) % INTERFACEX_SCRIPT_QUEUE_MAX;
    struct InterfaceX_ScriptQueueEntry* entry = &host->script_queue[tail];

    entry->script_id = script_id;
    entry->component_id = component_id;
    entry->int_arg_count = int_arg_count;
    entry->string_arg_count = 0;
    if( int_arg_count > 0 && int_args )
        memcpy(entry->int_args, int_args, (size_t)int_arg_count * sizeof(entry->int_args[0]));
    if( string_arg_count > 0 && string_args )
    {
        for( int i = 0; i < string_arg_count; i++ )
        {
            char const* src = string_args[i] ? string_args[i] : "";
            entry->string_args[i] = strdup(src);
            if( !entry->string_args[i] )
            {
                for( int j = 0; j < i; j++ )
                {
                    free(entry->string_args[j]);
                    entry->string_args[j] = NULL;
                }
                return;
            }
            entry->string_arg_count++;
        }
    }

    host->script_queue_count++;
}

static void
InterfaceX_VMHost_QueueCoreScriptHook(
    struct InterfaceX_VMHost* host,
    int component_id,
    struct ToriAuxLibCore_ScriptHook const* hook)
{
    assert(host);

    if( !hook || hook->argc <= 0 )
        return;

    int script_id = hook->argv[0];
    if( script_id <= 0 )
        return;

    int int_arg_count = hook->argc - 1;
    if( int_arg_count > TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX )
        int_arg_count = TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX;

    InterfaceX_VMHost_QueueScript(
        host,
        script_id,
        component_id,
        int_arg_count > 0 ? &hook->argv[1] : NULL,
        int_arg_count,
        NULL,
        0);
}

static int
InterfaceX_RunClientScript(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count,
    char const* const* string_args,
    int string_arg_count)
{
    assert(host);
    assert(vm);

    if( script_id <= 0 )
        return CS2VM_EXECNO_OK;

    struct ToriAuxLibCore_ClientScript* client_script =
        InterfaceX_HostIO_ClientScriptGet(&host->host_io, script_id);
    if( !client_script )
    {
        if( !InterfaceX_HostIO_LoadClientScript(&host->host_io, script_id) )
        {
            fprintf(stderr, "failed to resolve script: %d\n", script_id);
            return CS2VM_EXECNO_ERROR;
        }
        client_script = InterfaceX_HostIO_ClientScriptGet(&host->host_io, script_id);
    }
    if( !client_script )
    {
        fprintf(stderr, "failed to resolve script: %d\n", script_id);
        return CS2VM_EXECNO_ERROR;
    }

    CS2VMX_PushCallScript(vm, &client_script->script);
    {
        struct CS2VMX_Frame* frame = CS2VM_FRAME(vm);
        for( int j = 0; j < string_arg_count; j++ )
        {
            char const* src = string_args && string_args[j] ? string_args[j] : "";
            frame->str_locals[j] = strdup(src);
            if( !frame->str_locals[j] )
                return CS2VM_EXECNO_ERROR;
        }
    }
    for( int j = 0; j < int_arg_count; j++ )
        InterfaceX_SetHookIntLocal(host, vm, component_id, j, int_args[j]);

    CS2VMX_SetActiveAndDotComponentId(vm, component_id);

    if( g_cs2_trace_mode )
        fprintf(
            stderr,
            "CS2TRACE BEGIN script=%d component=0x%08x\n",
            script_id,
            (unsigned)component_id);

    int res;
    while( (res = CS2VMX_RunScript(vm)) )
    {
        switch( res )
        {
        case CS2VM_EXECNO_DONE:
            if( g_cs2_trace_mode )
                fprintf(
                    stderr,
                    "CS2TRACE END script=%d component=0x%08x\n",
                    script_id,
                    (unsigned)component_id);
            return CS2VM_EXECNO_OK;
        case CS2VM_EXECNO_YIELD:
            if( !host->has_pending_host_request )
            {
                fprintf(stderr, "CS2VM: yield without pending host request\n");
                return CS2VM_EXECNO_ERROR;
            }
            break;
        case CS2VM_EXECNO_ERROR:
            fprintf(
                stderr,
                "script %d failed at opcode %d pc %d (invoked as script %d for component 0x%x)\n",
                vm->last_error_script_id,
                vm->last_error_opcode,
                vm->last_error_pc,
                script_id,
                (unsigned)component_id);
            if( g_cs2_trace_mode )
                fprintf(
                    stderr,
                    "CS2TRACE FAIL script=%d component=0x%08x opcode=%d pc=%d\n",
                    script_id,
                    (unsigned)component_id,
                    vm->last_error_opcode,
                    vm->last_error_pc);
            return CS2VM_EXECNO_ERROR;
        }
    }
    if( g_cs2_trace_mode )
        fprintf(
            stderr, "CS2TRACE END script=%d component=0x%08x\n", script_id, (unsigned)component_id);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Init(struct InterfaceX_VMHost* host)
{
    assert(host);

    host->script_queue =
        calloc((size_t)INTERFACEX_SCRIPT_QUEUE_MAX, sizeof(struct InterfaceX_ScriptQueueEntry));
    if( !host->script_queue )
        return false;

    host->client_clock = 100;
    return true;
}

static bool
InterfaceX_IsGroupLoaded(
    struct InterfaceX_VMHost const* host,
    int group_id);

static bool
InterfaceX_IntegrateInterfaceGroup(
    struct InterfaceX_VMHost* host,
    int group_id);

static int
InterfaceX_VMHost_LoadInterfaceGroup(
    struct InterfaceX_VMHost* host,
    int group_id)
{
    assert(host);
    if( group_id <= 0 )
        return -1;
    if( InterfaceX_IsGroupLoaded(host, group_id) )
        return 0;
    if( !InterfaceX_HostIO_LoadInterfaceGroup(&host->host_io, group_id) )
        return -1;
    if( !InterfaceX_IntegrateInterfaceGroup(host, group_id) )
        return -1;
    return 0;
}

static int
InterfaceX_VMHost_ClientClock(struct CS2VMX* vm)
{
    assert(vm);
    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)CS2VM_USER(vm);
    int clock = host ? host->client_clock : 0;
    return CS2VMX_PushInt(vm, clock);
}

static int
InterfaceX_VMHost_Load_CC_Create(
    struct InterfaceX_VMHost* host,
    struct CS2VM_HostRequest_CC_Create const* request)
{
    int group_id;

    assert(host);
    assert(request);
    group_id = (request->parent_id >> 16) & 0xffff;
    if( request->parent_id <= 0 || group_id <= 0 )
        return 0;
    return InterfaceX_VMHost_LoadInterfaceGroup(host, group_id);
}

static int
InterfaceX_VMHost_Load_CC_Find(
    struct InterfaceX_VMHost* host,
    struct CS2VM_HostRequest_CC_Find const* request)
{
    int group_id;

    assert(host);
    assert(request);
    group_id = (request->parent_id >> 16) & 0xffff;
    if( request->parent_id <= 0 || group_id <= 0 )
        return 0;
    return InterfaceX_VMHost_LoadInterfaceGroup(host, group_id);
}

static int
InterfaceX_VMHost_Load_IF_Find(
    struct InterfaceX_VMHost* host,
    struct CS2VM_HostRequest_TargetFind const* request)
{
    int group_id;

    assert(host);
    assert(request);
    group_id = (request->component_id >> 16) & 0xffff;
    if( request->component_id <= 0 || group_id <= 0 )
        return 0;
    return InterfaceX_VMHost_LoadInterfaceGroup(host, group_id);
}

static int
InterfaceX_VMHost_Load_CC_ChildrenFind(
    struct InterfaceX_VMHost* host,
    struct CS2VM_HostRequest_CC_ChildrenFind const* request)
{
    int group_id;

    assert(host);
    assert(request);
    group_id = (request->parent_id >> 16) & 0xffff;
    if( request->parent_id <= 0 || group_id <= 0 )
        return 0;
    return InterfaceX_VMHost_LoadInterfaceGroup(host, group_id);
}

static int
InterfaceX_VMHost_Load_IF_ChildrenFind(
    struct InterfaceX_VMHost* host,
    struct CS2VM_HostRequest_IF_ChildrenFind const* request)
{
    int group_id;

    assert(host);
    assert(request);
    group_id = (request->uid >> 16) & 0xffff;
    if( request->uid <= 0 || group_id <= 0 )
        return 0;
    return InterfaceX_VMHost_LoadInterfaceGroup(host, group_id);
}

static int
InterfaceX_VMHost_Yield(
    struct InterfaceX_VMHost* host,
    struct CS2VM_HostRequest const* request)
{
    assert(host);
    assert(request);
    host->pending_host_request = *request;
    host->has_pending_host_request = true;
    return CS2VM_EXECNO_YIELD;
}

static int
InterfaceX_VMHost_YieldIfGroupNeeded(
    struct InterfaceX_VMHost* host,
    int component_id,
    struct CS2VM_HostRequest const* request)
{
    int group_id;

    assert(host);
    assert(request);
    group_id = (component_id >> 16) & 0xffff;
    if( component_id <= 0 || group_id <= 0 )
        return CS2VM_EXECNO_OK;
    if( InterfaceX_IsGroupLoaded(host, group_id) )
        return CS2VM_EXECNO_OK;
    return InterfaceX_VMHost_Yield(host, request);
}

static int
InterfaceX_VMHost_Load(
    struct InterfaceX_VMHost* host,
    struct CS2VM_HostRequest const* request)
{
    assert(host);
    assert(request);

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_PUSHSCRIPT:
        if( !InterfaceX_HostIO_LoadClientScript(&host->host_io, request->u.push_script.script_id) )
            return -1;
        return 0;
    case CS2VM_HOST_REQUEST_PARAHEIGHT:
    case CS2VM_HOST_REQUEST_PARAWIDTH:
        if( !InterfaceX_HostIO_LoadSceneFont(&host->host_io, request->u.para_height.font_id) )
            return -1;
        return 0;
    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC:
    case CS2VM_HOST_REQUEST_IF_SETGRAPHIC:
    {
        return 0;
    }
    case CS2VM_HOST_REQUEST_CC_SETOBJECT:
    case CS2VM_HOST_REQUEST_IF_SETOBJECT:
    {
        int scene_id = -1;
        if( !InterfaceX_HostIO_LoadObjIconScene(
                &host->host_io,
                request->u.cc_set_object.obj_id,
                request->u.cc_set_object.count,
                &scene_id) )
            return -1;
        return 0;
    }
    case CS2VM_HOST_REQUEST_ENUM_LOOKUP:
        if( !InterfaceX_HostIO_LoadConfigEntry(
                &host->host_io, RSCacheDat2A_ConfigKind_Enum, request->u.enum_lookup.enum_id) )
            return -1;
        return 0;
    case CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT:
        if( !InterfaceX_HostIO_LoadConfigEntry(
                &host->host_io,
                RSCacheDat2A_ConfigKind_Enum,
                request->u.enum_get_output_count.enum_id) )
            return -1;
        return 0;
    case CS2VM_HOST_REQUEST_STRUCT_PARAM:
        if( !InterfaceX_HostIO_LoadConfigEntry(
                &host->host_io, RSCacheDat2A_ConfigKind_Struct, request->u.struct_param.struct_id) )
            return -1;
        if( !InterfaceX_HostIO_LoadConfigEntry(
                &host->host_io, RSCacheDat2A_ConfigKind_Params, request->u.struct_param.param_id) )
            return -1;
        return 0;
    case CS2VM_HOST_REQUEST_OC_PARAM:
        if( !InterfaceX_HostIO_LoadObjectConfig(&host->host_io, request->u.oc_param.item_id) )
            return -1;
        if( !InterfaceX_HostIO_LoadConfigEntry(
                &host->host_io, RSCacheDat2A_ConfigKind_Params, request->u.oc_param.param_id) )
            return -1;
        return 0;
    case CS2VM_HOST_REQUEST_OC_NAME:
        if( !InterfaceX_HostIO_LoadObjectConfig(&host->host_io, request->u.oc_name.item_id) )
            return -1;
        return 0;
    case CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER:
        if( !InterfaceX_HostIO_LoadObjectConfig(
                &host->host_io, request->u.oc_unplaceholder.item_id) )
            return -1;
        return 0;
    case CS2VM_HOST_REQUEST_OC_INT_PARAM:
        if( !InterfaceX_HostIO_LoadObjectConfig(&host->host_io, request->u.oc_int_param.item_id) )
            return -1;
        return 0;
    case CS2VM_HOST_REQUEST_CC_SETTEXTFONT:
        if( !InterfaceX_HostIO_LoadSceneFont(&host->host_io, request->u.cc_set_text_font.font_id) )
            return -1;
        return 0;
    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL:
        if( request->u.widget_set_model.model_id < 0 )
            return 0;
        if( !InterfaceX_HostIO_LoadModel(&host->host_io, request->u.widget_set_model.model_id) )
            return -1;
        return 0;
    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND:
    {
        enum InterfaceX_ModelKind kind = request->u.widget_set_model_kind.model_kind;
        int model_id = request->u.widget_set_model_kind.model_id;

        if( kind == INTERFACEX_MODEL_KIND_PLAIN )
        {
            if( model_id < 0 )
                return 0;
            if( !InterfaceX_HostIO_LoadModel(&host->host_io, model_id) )
                return -1;
        }
        else if( kind == INTERFACEX_MODEL_KIND_NPC_HEAD )
        {
            if( model_id < 0 )
                return 0;
            if( !InterfaceX_HostIO_LoadNpctype(&host->host_io, model_id) )
                return -1;
        }
        else if(
            kind == INTERFACEX_MODEL_KIND_PLAYER_HEAD ||
            kind == INTERFACEX_MODEL_KIND_PLAYER_SELF ||
            kind == INTERFACEX_MODEL_KIND_PLAYER_CHATHEAD )
        {
            if( !InterfaceX_HostIO_LoadPlayerAppearance(&host->host_io, host->player_appearance) )
                return -1;
        }
        return 0;
    }
    case CS2VM_HOST_REQUEST_CC_CREATE:
        return InterfaceX_VMHost_Load_CC_Create(host, &request->u.cc_create);
    case CS2VM_HOST_REQUEST_CC_FIND:
        return InterfaceX_VMHost_Load_CC_Find(host, &request->u.cc_find);
    case CS2VM_HOST_REQUEST_IF_FIND:
        return InterfaceX_VMHost_Load_IF_Find(host, &request->u.if_find);
    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND:
        return InterfaceX_VMHost_Load_CC_ChildrenFind(host, &request->u.cc_children_find);
    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
        return InterfaceX_VMHost_Load_IF_ChildrenFind(host, &request->u.if_children_find);
    default:
        fprintf(stderr, "InterfaceX_VMHost_Load: unhandled kind %d\n", (int)request->kind);
        return -1;
    }
}

int
InterfaceX_VMHost_Exec_PushScript(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int script_id)
{
    assert(host);
    assert(host->builder);
    struct ToriAuxLibCore_ClientScript* cs;

    cs = InterfaceX_HostIO_ClientScriptGet(&host->host_io, script_id);
    if( !cs )
    {
        struct CS2VM_HostRequest req = { 0 };
        req.kind = CS2VM_HOST_REQUEST_PUSHSCRIPT;
        req.u.push_script.script_id = script_id;
        return InterfaceX_VMHost_Yield(host, &req);
    }

    return CS2VMX_PushCallScript(vm, &cs->script);
}

int
InterfaceX_VMHost_Exec_InvSize(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int inv_id)
{
    assert(host);
    assert(host->builder);

    struct InterfaceX_InvContainer* container = InterfaceX_InvContainerGet(host, inv_id, false);
    int size = container ? container->size : 0;
    if( size <= 0 )
    {
        if( inv_id == INTERFACEX_INV_CONTAINER_WORN )
            size = 14;
        else if( inv_id == INTERFACEX_INV_CONTAINER_BACKPACK )
            size = 28;
    }

    CS2VMX_PushInt(vm, size);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_InvGetObj(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_InvGetObj request)
{
    assert(host);
    assert(host->builder);

    struct InterfaceX_InvContainer* container =
        InterfaceX_InvContainerGet(host, request.inv_id, false);
    int obj_id = -1;
    if( container && request.slot >= 0 && request.slot < container->size )
        obj_id = container->slots[request.slot].obj_id;

    CS2VMX_PushInt(vm, obj_id);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_InvGetNum(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_InvGetNum request)
{
    assert(host);
    assert(host->builder);

    struct InterfaceX_InvContainer* container =
        InterfaceX_InvContainerGet(host, request.inv_id, false);
    int count = 0;
    if( container && request.slot >= 0 && request.slot < container->size )
    {
        struct InterfaceX_InvSlot const* slot = &container->slots[request.slot];
        if( slot->obj_id > 0 )
            count = slot->count > 0 ? slot->count : 1;
    }

    CS2VMX_PushInt(vm, count);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_InvTotal(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_InvTotal request)
{
    assert(host);
    assert(host->builder);

    struct InterfaceX_InvContainer* container =
        InterfaceX_InvContainerGet(host, request.inv_id, false);
    int total = 0;
    if( container && request.item_id > 0 )
    {
        for( int slot = 0; slot < container->size; slot++ )
        {
            if( container->slots[slot].obj_id == request.item_id )
                total += container->slots[slot].count > 0 ? container->slots[slot].count : 1;
        }
    }

    CS2VMX_PushInt(vm, total);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_ParaHeight(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_ParaHeight request)
{
    assert(host);
    assert(host->builder);

    int lines = 0;
    char const* text = request.text ? request.text : "";
    if( text[0] != '\0' )
    {
        struct ToriDraw_Font* font =
            InterfaceX_HostIO_SceneFontGet(&host->host_io, request.font_id);
        if( !font )
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = CS2VM_HOST_REQUEST_PARAHEIGHT;
            req.u.para_height = request;
            return InterfaceX_VMHost_Yield(host, &req);
        }
        lines = ToriDraw2D_WrapLineCount(font, text, request.max_width);
    }

    CS2VMX_PushInt(vm, lines);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_ParaWidth(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_ParaHeight request)
{
    assert(host);
    assert(host->builder);

    int width = 0;
    char const* text = request.text ? request.text : "";
    if( text[0] != '\0' )
    {
        struct ToriDraw_Font* font =
            InterfaceX_HostIO_SceneFontGet(&host->host_io, request.font_id);
        if( !font )
        {
            struct CS2VM_HostRequest req = { 0 };
            req.kind = CS2VM_HOST_REQUEST_PARAWIDTH;
            req.u.para_height = request;
            return InterfaceX_VMHost_Yield(host, &req);
        }
        width = ToriDraw2D_WrapMaxLineWidth(font, text, request.max_width);
    }

    CS2VMX_PushInt(vm, width);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_VarsReadVarp(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_VarsReadVarp request)
{
    assert(host);
    assert(host->builder);

    int varp_id = request.varp_id;
    (void)varp_id;

    CS2VMX_PushInt(vm, 0);

    return CS2VM_EXECNO_OK;
}

static int
InterfaceX_DefaultVarbitValue(int varbit_id)
{
    /* Bank quantity dropdown (script 2578): case 0 builds full-width chrome on layer 28.
     * Default closed state is non-zero so only the small per-button layers get chrome. */
    if( varbit_id == 6590 )
        return 1;
    return 0;
}

int
InterfaceX_VMHost_Exec_VarsReadVarbit(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_VarsReadVarbit request)
{
    assert(host);
    assert(vm);

    CS2VMX_PushInt(vm, InterfaceX_DefaultVarbitValue(request.varbit_id));
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_VarsReadVarcInt(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_VarsReadVarcInt request)
{
    assert(host);
    assert(vm);

    int value = 0;
    if( request.varc_id >= 0 && request.varc_id < INTERFACEX_VARC_INT_MAX )
        value = host->varc_int[request.varc_id];

    CS2VMX_PushInt(vm, value);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_VarsReadVarcString(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_VarsReadVarcString request)
{
    assert(host);
    assert(vm);

    char* value = (char*)"";
    if( request.varc_id >= 0 && request.varc_id < INTERFACEX_VARC_STRING_MAX )
        value = host->varc_string[request.varc_id];

    CS2VMX_PushStr(vm, value);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_VarsWriteVarcInt(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_VarsWriteVarcInt request)
{
    assert(host);
    (void)vm;

    if( request.varc_id >= 0 && request.varc_id < INTERFACEX_VARC_INT_MAX )
        host->varc_int[request.varc_id] = request.value;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_VarsWriteVarcString(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_VarsWriteVarcString request)
{
    assert(host);
    (void)vm;

    if( request.varc_id >= 0 && request.varc_id < INTERFACEX_VARC_STRING_MAX )
    {
        strncpy(
            host->varc_string[request.varc_id],
            request.value ? request.value : "",
            INTERFACEX_VARC_STRING_LEN - 1);
        host->varc_string[request.varc_id][INTERFACEX_VARC_STRING_LEN - 1] = '\0';
    }

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_EnumLookup(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_EnumLookup request)
{
    assert(host);
    assert(vm);

    struct Dat2BuildCache* bc = dat2(InterfaceX_HostIO_Cache(&host->host_io));

    if( !dat2_buildcache_enum_get(bc, request.enum_id) )
        return InterfaceX_VMHost_Yield(
            host,
            &(struct CS2VM_HostRequest){
                .kind = CS2VM_HOST_REQUEST_ENUM_LOOKUP,
                .u.enum_lookup = request,
            });

    if( request.output_type == (int)'s' )
    {
        char const* value = ie_enum_lookup_string(
            bc, request.input_type, request.output_type, request.enum_id, request.key);
        return CS2VMX_PushStr(vm, strdup(value ? value : "null"));
    }

    int value =
        ie_enum_lookup(bc, request.input_type, request.output_type, request.enum_id, request.key);

    CS2VMX_PushInt(vm, value);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_EnumGetOutputCount(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_EnumGetOutputCount request)
{
    assert(host);
    assert(vm);

    struct Dat2BuildCache* bc = dat2(InterfaceX_HostIO_Cache(&host->host_io));

    if( !dat2_buildcache_enum_get(bc, request.enum_id) )
        return InterfaceX_VMHost_Yield(
            host,
            &(struct CS2VM_HostRequest){
                .kind = CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT,
                .u.enum_get_output_count = request,
            });

    int count = ie_enum_output_count(bc, request.enum_id);

    CS2VMX_PushInt(vm, count);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_GetWidth(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(host->builder);

    CS2VMX_PushInt(vm, UITreeX_GetLayoutWidth(host->tree, component_id));
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_GetHeight(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(host->builder);

    CS2VMX_PushInt(vm, UITreeX_GetLayoutHeight(host->tree, component_id));
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_GetY(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(host->builder);

    CS2VMX_PushInt(vm, UITreeX_GetPosY(host->tree, component_id));
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_GetLayer(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(host->builder);

    CS2VMX_PushInt(vm, UITreeX_GetLayer(host->tree, component_id));
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_GetTop(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm)
{
    assert(host);
    assert(host->builder);

    CS2VMX_PushInt(vm, UITreeX_GetTop(host->tree, host->interface_id));
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_GetScrollX(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(host->builder);

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, component_id);
    if( !node || node->kind != UITreeXNodeKind_RSLayer )
    {
        CS2VMX_PushInt(vm, 0);
        return CS2VM_EXECNO_OK;
    }

    CS2VMX_PushInt(vm, UITreeX_NodeRSLayer(node)->scroll_x);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_GetScrollY(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(host->builder);

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, component_id);
    if( !node || node->kind != UITreeXNodeKind_RSLayer )
    {
        CS2VMX_PushInt(vm, 0);
        return CS2VM_EXECNO_OK;
    }

    CS2VMX_PushInt(vm, UITreeX_NodeRSLayer(node)->scroll_y);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_GetScrollHeight(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(host->builder);

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, component_id);
    if( !node || node->kind != UITreeXNodeKind_RSLayer )
    {
        CS2VMX_PushInt(vm, 0);
        return CS2VM_EXECNO_OK;
    }

    CS2VMX_PushInt(vm, UITreeX_NodeRSLayer(node)->scroll_height);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetScrollPos(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetScrollPos request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node || node->kind != UITreeXNodeKind_RSLayer )
        return CS2VM_EXECNO_OK;

    struct UITreeXNode_RSLayer* layer = UITreeX_NodeRSLayerMut(node);

    UITreeX_LayoutResolve(host->tree, CANVAS_W, CANVAS_H);

    int view_w = node->layout_resolved && node->abs_w > 0 ? node->abs_w : node->w;
    int view_h = node->layout_resolved && node->abs_h > 0 ? node->abs_h : node->h;
    if( view_w < 0 )
        view_w = 0;
    if( view_h < 0 )
        view_h = 0;

    int max_x = layer->scroll_width > view_w ? layer->scroll_width - view_w : 0;
    int max_y = layer->scroll_height > view_h ? layer->scroll_height - view_h : 0;

    int scroll_x = request.scroll_x;
    int scroll_y = request.scroll_y;
    if( scroll_x < 0 )
        scroll_x = 0;
    if( scroll_x > max_x )
        scroll_x = max_x;
    if( scroll_y < 0 )
        scroll_y = 0;
    if( scroll_y > max_y )
        scroll_y = max_y;

    layer->scroll_x = scroll_x;
    layer->scroll_y = scroll_y;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetScrollSize(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetScrollSize request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node || node->kind != UITreeXNodeKind_RSLayer )
        return CS2VM_EXECNO_OK;

    struct UITreeXNode_RSLayer* layer = UITreeX_NodeRSLayerMut(node);

    bool size_changed = layer->scroll_width != request.scroll_width ||
                        layer->scroll_height != request.scroll_height;
    layer->scroll_width = request.scroll_width;
    layer->scroll_height = request.scroll_height;

    UITreeX_LayoutResolve(host->tree, CANVAS_W, CANVAS_H);

    int view_w = node->layout_resolved && node->abs_w > 0 ? node->abs_w : node->w;
    int view_h = node->layout_resolved && node->abs_h > 0 ? node->abs_h : node->h;
    if( view_w < 0 )
        view_w = 0;
    if( view_h < 0 )
        view_h = 0;

    int max_x = layer->scroll_width > view_w ? layer->scroll_width - view_w : 0;
    int max_y = layer->scroll_height > view_h ? layer->scroll_height - view_h : 0;

    int scroll_x = layer->scroll_x;
    int scroll_y = layer->scroll_y;
    if( scroll_x < 0 )
        scroll_x = 0;
    if( scroll_x > max_x )
        scroll_x = max_x;
    if( scroll_y < 0 )
        scroll_y = 0;
    if( scroll_y > max_y )
        scroll_y = max_y;

    bool scroll_changed = scroll_x != layer->scroll_x || scroll_y != layer->scroll_y;
    layer->scroll_x = scroll_x;
    layer->scroll_y = scroll_y;

    if( size_changed || scroll_changed )
        UITreeX_InvalidateLayout(host->tree);

    return CS2VM_EXECNO_OK;
}

static void
InterfaceX_ModelNodeInvalidateScene(struct UITreeXNode* node)
{
    if( node && node->kind == UITreeXNodeKind_RSModel )
        node->u.rs_model.scene_id = -1;
}

static void
InterfaceX_ApplySetModel(
    struct InterfaceX_VMHost* host,
    struct UITreeXNode* node,
    int model_id,
    enum InterfaceX_ModelKind model_kind,
    char const* op_name)
{
    assert(host);
    assert(node);

    if( node->kind != UITreeXNodeKind_RSModel )
    {
        InterfaceX_ReportOpKindMismatch(node, "model", op_name ? op_name : "SETMODEL");
        return;
    }

    struct UITreeXNode_RSModel* model = UITreeX_NodeModelMut(node);
    model->model_id = model_id;
    model->model_kind = model_kind;
    InterfaceX_EnqueueModelNodeLoad(host, node);
}

static char const*
InterfaceX_WidgetIntFieldOpName(enum CS2VM_WidgetIntField field)
{
    switch( field )
    {
    case CS2VM_WIDGET_INT_HFLIP:
        return "CC_SETHFLIP";
    case CS2VM_WIDGET_INT_VFLIP:
        return "CC_SETVFLIP";
    case CS2VM_WIDGET_INT_ANGLE_2D:
        return "CC_SETANGLE2D";
    case CS2VM_WIDGET_INT_FILL_COLOUR:
        return "CC_SETFILLCOLOUR";
    case CS2VM_WIDGET_INT_LINE_WIDTH:
        return "CC_SETLINEWID";
    case CS2VM_WIDGET_INT_LINE_DIRECTION:
        return "CC_SETLINEDIRECTION";
    case CS2VM_WIDGET_INT_FILL_MODE:
        return "CC_SETFILLMODE";
    case CS2VM_WIDGET_INT_TRANS_BOT:
        return "CC_SETTRANSBOT";
    case CS2VM_WIDGET_INT_NO_SCROLL_THROUGH:
        return "CC_SETNOSCROLLTHROUGH";
    case CS2VM_WIDGET_INT_NO_CLICK_THROUGH:
        return "CC_SETNOCLICKTHROUGH";
    case CS2VM_WIDGET_INT_PINCH:
        return "CC_SETPINCH";
    case CS2VM_WIDGET_INT_CLICKMASK:
        return "CC_SETCLICKMASK";
    case CS2VM_WIDGET_INT_DRAG_DEAD_ZONE:
        return "CC_SETDRAGDEADZONE";
    case CS2VM_WIDGET_INT_DRAG_DEAD_TIME:
        return "CC_SETDRAGDEADTIME";
    case CS2VM_WIDGET_INT_MODEL_ANIM:
        return "CC_SETMODELANIM";
    case CS2VM_WIDGET_INT_MODEL_ORTHOG:
        return "CC_SETMODELORTHOG";
    case CS2VM_WIDGET_INT_MODEL_TRANSPARENT:
        return "CC_SETMODELTRANSPARENT";
    case CS2VM_WIDGET_INT_RESUME_PAUSEBUTTON:
        return "CC_SETRESUMEPAUSEBUTTON";
    default:
        return "WIDGET_SET_INT";
    }
}

static void
InterfaceX_ApplyWidgetSetInt(
    struct UITreeXNode* node,
    enum CS2VM_WidgetIntField field,
    int value,
    char const* op_name)
{
    assert(node);

    if( !op_name )
        op_name = InterfaceX_WidgetIntFieldOpName(field);

    switch( field )
    {
    case CS2VM_WIDGET_INT_HFLIP:
        node->hflip = value ? 1 : 0;
        break;
    case CS2VM_WIDGET_INT_VFLIP:
        node->vflip = value ? 1 : 0;
        break;
    case CS2VM_WIDGET_INT_ANGLE_2D:
        node->angle_2d = value;
        break;
    case CS2VM_WIDGET_INT_FILL_COLOUR:
        if( node->kind == UITreeXNodeKind_RSRect )
            UITreeX_NodeRSRectMut(node)->color = value;
        else if( node->kind == UITreeXNodeKind_RSLine )
            UITreeX_NodeRSLineMut(node)->color = value;
        else
            InterfaceX_ReportOpKindMismatch(node, "rect|line", op_name);
        break;
    case CS2VM_WIDGET_INT_LINE_WIDTH:
        if( node->kind == UITreeXNodeKind_RSLine )
            UITreeX_NodeRSLineMut(node)->line_width = value > 0 ? value : 1;
        else
            InterfaceX_ReportOpKindMismatch(node, "line", op_name);
        break;
    case CS2VM_WIDGET_INT_LINE_DIRECTION:
        if( node->kind == UITreeXNodeKind_RSLine )
            UITreeX_NodeRSLineMut(node)->line_direction = value;
        else
            InterfaceX_ReportOpKindMismatch(node, "line", op_name);
        break;
    case CS2VM_WIDGET_INT_FILL_MODE:
        node->fill_mode = value;
        break;
    case CS2VM_WIDGET_INT_TRANS_BOT:
        node->trans_bot = value;
        break;
    case CS2VM_WIDGET_INT_NO_SCROLL_THROUGH:
        node->no_scroll_through = value ? 1 : 0;
        break;
    case CS2VM_WIDGET_INT_NO_CLICK_THROUGH:
        node->no_click_through = value ? 1 : 0;
        break;
    case CS2VM_WIDGET_INT_PINCH:
        node->pinch_enabled = value ? 1 : 0;
        break;
    case CS2VM_WIDGET_INT_CLICKMASK:
        node->clickmask = value;
        break;
    case CS2VM_WIDGET_INT_DRAG_DEAD_ZONE:
        node->drag_dead_zone = (uint8_t)value;
        break;
    case CS2VM_WIDGET_INT_DRAG_DEAD_TIME:
        node->drag_dead_time = (uint8_t)value;
        break;
    case CS2VM_WIDGET_INT_MODEL_ANIM:
        if( node->kind == UITreeXNodeKind_RSModel )
            UITreeX_NodeModelMut(node)->anim_seq = value;
        else
            InterfaceX_ReportOpKindMismatch(node, "model", op_name);
        break;
    case CS2VM_WIDGET_INT_MODEL_ORTHOG:
        if( node->kind == UITreeXNodeKind_RSModel )
            UITreeX_NodeModelMut(node)->orthog = value;
        else
            InterfaceX_ReportOpKindMismatch(node, "model", op_name);
        break;
    case CS2VM_WIDGET_INT_MODEL_TRANSPARENT:
        if( node->kind == UITreeXNodeKind_RSModel )
            UITreeX_NodeModelMut(node)->transparent = value;
        else
            InterfaceX_ReportOpKindMismatch(node, "model", op_name);
        break;
    case CS2VM_WIDGET_INT_RESUME_PAUSEBUTTON:
        break;
    default:
        break;
    }
}

int
InterfaceX_VMHost_Exec_CC_SetScrollPos(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetScrollPos request)
{
    return InterfaceX_VMHost_Exec_IF_SetScrollPos(host, vm, request);
}

int
InterfaceX_VMHost_Exec_CC_SetScrollSize(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetScrollSize request)
{
    return InterfaceX_VMHost_Exec_IF_SetScrollSize(host, vm, request);
}

int
InterfaceX_VMHost_Exec_WidgetSetInt(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetSetInt request)
{
    assert(host);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    InterfaceX_ApplyWidgetSetInt(
        node, request.field, request.value, InterfaceX_WidgetIntFieldOpName(request.field));
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_WidgetSetModel(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetSetModel request)
{
    assert(host);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    InterfaceX_ApplySetModel(
        host, node, request.model_id, INTERFACEX_MODEL_KIND_PLAIN, "CC_SETMODEL");
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_WidgetSetModelAngle(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetSetModelAngle request)
{
    assert(host);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    struct UITreeXNode_RSModel* model = UITreeX_NodeModelMut(node);
    model->offset_x = request.offset_x;
    model->offset_y = request.offset_y;
    model->angle_x = request.angle_x;
    model->angle_y = request.angle_y;
    model->angle_z = request.angle_z;
    if( request.zoom > 0 )
        model->zoom = request.zoom;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_WidgetSetArc(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetSetArc request)
{
    assert(host);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    node->arc_start = request.arc_start;
    node->arc_end = request.arc_end;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_WidgetSetModelKind(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetSetModelKind request)
{
    assert(host);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    struct UITreeXNode_RSModel* model = UITreeX_NodeModelMut(node);
    model->model_kind = request.model_kind;
    if( request.model_id >= 0 )
        model->model_id = request.model_id;
    InterfaceX_EnqueueModelNodeLoad(host, node);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_WidgetInputInt(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_WidgetInputInt request)
{
    assert(host);
    (void)vm;
    (void)request;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetHide(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetHide request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( node )
    {
        node->hidden = request.hidden ? 1 : 0;
        UITreeX_InvalidateLayout(host->tree);
    }

    return CS2VM_EXECNO_OK;
}

static int
InterfaceX_NodeIsGraphicKind(struct UITreeXNode const* node)
{
    return node && node->kind == UITreeXNodeKind_RSGraphic;
}

/* OSRS sets Widget.spriteId on any type; only type-5 widgets draw sprites. */
static int
InterfaceX_NodeStoreGraphicId(
    struct InterfaceX_VMHost* host,
    struct UITreeXNode* node,
    int graphic_id,
    int component_id)
{
    assert(host);
    assert(node);

    struct UITreeXNode_RSGraphic* graphic = UITreeX_NodeRSGraphicMut(node);
    graphic->graphic_id = graphic_id;
    if( InterfaceX_NodeIsGraphicKind(node) )
    {
        if( graphic_id >= 0 )
        {
            int scene_id = -1;
            if( !InterfaceX_HostIO_GraphicSceneId(&host->host_io, graphic_id, &scene_id) )
            {
                struct CS2VM_HostRequest req = { 0 };
                req.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHIC;
                req.u.cc_set_graphic.component_id = component_id;
                req.u.cc_set_graphic.graphic_id = graphic_id;
                return InterfaceX_VMHost_Yield(host, &req);
            }
            graphic->scene_id = scene_id;
        }
        else
            graphic->scene_id = -1;
    }

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOutline(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOutline request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    UITreeX_NodeRSGraphicMut(node)->outline = request.outline;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetOnClick(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetOnHold(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetOnMouseOver(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetOnMouseLeave(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetOnMouseRepeat(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetOnDrag(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetOnScrollWheel(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetOnKey(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetOnOp(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetOnDragComplete(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOnInvTransmit(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOnInvTransmit request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request.signature;

    if( host->inv_transmit_hook_count >= INTERFACEX_INV_TRANSMIT_HOOK_MAX )
        return CS2VM_EXECNO_OK;

    struct InterfaceX_InvTransmitHook* hook =
        &host->inv_transmit_hooks[host->inv_transmit_hook_count++];

    hook->component_id = request.component_id;
    hook->script_id = request.script_id;
    hook->int_arg_count = request.int_arg_count;
    if( hook->int_arg_count > INTERFACEX_INV_TRANSMIT_INT_ARG_MAX )
        hook->int_arg_count = INTERFACEX_INV_TRANSMIT_INT_ARG_MAX;
    memcpy(hook->int_args, request.int_args, sizeof(hook->int_args));

    hook->trigger_count = request.trigger_count;
    if( hook->trigger_count > INTERFACEX_INV_TRANSMIT_TRIGGER_MAX )
        hook->trigger_count = INTERFACEX_INV_TRANSMIT_TRIGGER_MAX;
    if( request.trigger_ids && hook->trigger_count > 0 )
    {
        memcpy(
            hook->trigger_ids,
            request.trigger_ids,
            (size_t)hook->trigger_count * sizeof(hook->trigger_ids[0]));
    }
    else
    {
        hook->trigger_count = 0;
    }

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOnVarTransmit(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOnVarTransmit request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    if( host->var_transmit_hook_count >= INTERFACEX_VAR_TRANSMIT_HOOK_MAX )
        return CS2VM_EXECNO_OK;

    struct InterfaceX_VarTransmitHook* hook =
        &host->var_transmit_hooks[host->var_transmit_hook_count++];

    hook->component_id = request.component_id;
    hook->script_id = request.script_id;
    hook->int_arg_count = request.int_arg_count;
    if( hook->int_arg_count > INTERFACEX_INV_TRANSMIT_INT_ARG_MAX )
        hook->int_arg_count = INTERFACEX_INV_TRANSMIT_INT_ARG_MAX;
    memcpy(hook->int_args, request.int_args, sizeof(hook->int_args));

    hook->trigger_count = request.trigger_count;
    if( hook->trigger_count > INTERFACEX_VAR_TRANSMIT_TRIGGER_MAX )
        hook->trigger_count = INTERFACEX_VAR_TRANSMIT_TRIGGER_MAX;
    if( request.trigger_ids && hook->trigger_count > 0 )
    {
        memcpy(
            hook->trigger_ids,
            request.trigger_ids,
            (size_t)hook->trigger_count * sizeof(hook->trigger_ids[0]));
    }
    else
    {
        hook->trigger_count = 0;
    }

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOnOp(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOnMouseOver(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOnMouseLeave(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOnMouseRepeat(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOnTimer(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOnScrollWheel(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOnKey(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOnMiscTransmit(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOnOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;
    (void)request;

    return CS2VM_EXECNO_OK;
}

static void
UITreeX_ApplyOp(
    struct UITreeXNode* node,
    int index,
    char const* text)
{
    if( !node || index < 1 || index > INTERFACEX_OP_SLOTS )
        return;

    strncpy(node->ops[index - 1], text ? text : "", INTERFACEX_OP_LEN - 1);
    node->ops[index - 1][INTERFACEX_OP_LEN - 1] = '\0';
}

static void
UITreeX_ClearOps(struct UITreeXNode* node)
{
    if( !node )
        return;

    for( int i = 0; i < INTERFACEX_OP_SLOTS; i++ )
        node->ops[i][0] = '\0';
}

int
InterfaceX_VMHost_Exec_CC_SetOp(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( node )
        UITreeX_ApplyOp(node, request.index, request.text);

    CS2VMX_SetTraceExtra(
        "widget=0x%08x index=%d text=\"%s\" hit=%d",
        (unsigned)request.component_id,
        request.index,
        request.text ? request.text : "",
        node ? 1 : 0);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOp(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOp request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( node )
        UITreeX_ApplyOp(node, request.index, request.text);

    CS2VMX_SetTraceExtra(
        "widget=0x%08x index=%d text=\"%s\" hit=%d",
        (unsigned)request.component_id,
        request.index,
        request.text ? request.text : "",
        node ? 1 : 0);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOpBase(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOpBase request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( node )
    {
        strncpy(node->op_base, request.text ? request.text : "", sizeof(node->op_base) - 1);
        node->op_base[sizeof(node->op_base) - 1] = '\0';
    }

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetOpSubmenu(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetOpSubmenu request)
{
    assert(host);
    assert(vm);
    (void)request;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetTargetPriority(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetTargetPriority request)
{
    assert(host);
    assert(vm);
    (void)request;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_ClearOps(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_ClearOps request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( node )
        UITreeX_ClearOps(node);

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_DeleteAll(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(host->builder);
    assert(vm);

    UITreeX_DeleteDynamicChildren(host->tree, component_id);
    CS2VMX_InvalidateComponentIfGone(vm, host->tree, &vm->active_component_id);
    CS2VMX_InvalidateComponentIfGone(vm, host->tree, &vm->dot_component_id);
    UITreeX_InvalidateLayout(host->tree);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_Create(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_Create request)
{
    assert(host);
    assert(host->builder);
    assert(host->tree);

    int parent_id = request.parent_id;
    int type = request.component_type;
    int child_index = request.child_index;
    (void)request.is_nested;

    {
        struct CS2VM_HostRequest yield_req = { 0 };
        int yield_res;

        yield_req.kind = CS2VM_HOST_REQUEST_CC_CREATE;
        yield_req.u.cc_create = request;
        yield_res = InterfaceX_VMHost_YieldIfGroupNeeded(host, parent_id, &yield_req);
        if( yield_res != CS2VM_EXECNO_OK )
            return yield_res;
    }

    int parent_idx = UITreeX_FindByUserId(host->tree, parent_id);
    if( parent_idx < 0 )
    {
        if( parent_id > 0 )
        {
            fprintf(
                stderr,
                "CC_CREATE: parent not found parent=0x%08x type=%d child=%d active=0x%08x "
                "dot=0x%08x\n",
                (unsigned)parent_id,
                type,
                child_index,
                vm ? (unsigned)vm->active_component_id : 0u,
                vm ? (unsigned)vm->dot_component_id : 0u);
            return CS2VM_EXECNO_ERROR;
        }
        /* Tutorial scripts pass parent=0 when disabled; remove the prior dynamic child. */
        if( parent_id <= 0 && vm && child_index >= 0 )
        {
            int active_id =
                request.dot_operand == 1 ? vm->dot_component_id : vm->active_component_id;
            int active_idx = UITreeX_FindByUserId(host->tree, active_id);
            if( active_idx >= 0 )
            {
                int existing = UITreeX_FindDynamicChild(host->tree, active_idx, child_index);
                if( existing >= 0 )
                {
                    UITreeX_UnlinkChild(host->tree, active_idx, existing);
                    host->tree->nodes[existing].user_id = -1;
                    CS2VMX_InvalidateComponentIfGone(vm, host->tree, &vm->active_component_id);
                    CS2VMX_InvalidateComponentIfGone(vm, host->tree, &vm->dot_component_id);
                    UITreeX_InvalidateLayout(host->tree);
                }
            }
        }
        return CS2VM_EXECNO_OK;
    }

    int existing = UITreeX_FindDynamicChild(host->tree, parent_idx, child_index);
    if( existing >= 0 )
    {
        UITreeX_UnlinkChild(host->tree, parent_idx, existing);
        host->tree->nodes[existing].user_id = -1;
        CS2VMX_InvalidateComponentIfGone(vm, host->tree, &vm->active_component_id);
        CS2VMX_InvalidateComponentIfGone(vm, host->tree, &vm->dot_component_id);
    }

    struct UITreeXNode* node = UITreeX_NodeEmplace(host->tree);
    if( !node )
        return CS2VM_EXECNO_ERROR;

    node->user_id = InterfaceX_VMHost_AllocateDynamicUid(host);
    if( node->user_id < 0 )
        return CS2VM_EXECNO_ERROR;

    node->dynamic = 1;
    node->child_index = child_index;
    node->if3 = 1;
    node->aspect_w = 1;
    node->aspect_h = 1;

    switch( type )
    {
    case 5:
        node->kind = UITreeXNodeKind_RSGraphic;
        node->u.rs_graphic.graphic_id = -1;
        node->u.rs_graphic.graphic_id2 = -1;
        node->u.rs_graphic.scene_id = -1;
        node->u.rs_graphic.cs1_active = 0;
        break;
    case 3:
        node->kind = UITreeXNodeKind_RSRect;
        node->u.rs_rect.color = 0;
        node->u.rs_rect.color2 = 0;
        node->u.rs_rect.filled = 1;
        break;
    case 4:
        node->kind = UITreeXNodeKind_RSText;
        node->u.rs_text.font_id = 0;
        node->u.rs_text.color = 0;
        node->u.rs_text.center = 0;
        node->u.rs_text.y_align = 0;
        node->u.rs_text.line_height = 0;
        node->u.rs_text.shadowed = 0;
        node->u.rs_text.text[0] = '\0';
        break;
    case 6:
        node->kind = UITreeXNodeKind_RSModel;
        node->u.rs_model.model_id = -1;
        node->u.rs_model.model_kind = INTERFACEX_MODEL_KIND_NONE;
        node->u.rs_model.zoom = 2000;
        node->u.rs_model.item_id = -1;
        node->u.rs_model.scene_id = -1;
        break;
    case 9:
        node->kind = UITreeXNodeKind_RSLine;
        node->u.rs_line.color = 0;
        node->u.rs_line.line_width = 1;
        node->u.rs_line.line_direction = 0;
        break;
    case 0:
        node->kind = UITreeXNodeKind_RSLayer;
        break;
    default:
        node->kind = UITreeXNodeKind_RSObj;
        node->u.rs_obj.obj_id = 0;
        node->u.rs_obj.obj_count = 0;
        node->u.rs_obj.scene_id = -1;
        break;
    }

    if( type == 5 )
    {
        struct UITreeXNode* parent_node = &host->tree->nodes[parent_idx];
        if( parent_node->kind == UITreeXNodeKind_RSGraphic )
        {
            struct UITreeXNode_RSGraphic const* parent_graphic = UITreeX_NodeRSGraphic(parent_node);
            if( parent_graphic->graphic_id >= 0 )
                InterfaceX_NodeStoreGraphicId(
                    host, node, parent_graphic->graphic_id, node->user_id);
            UITreeX_NodeRSGraphicMut(node)->outline = parent_graphic->outline;
            UITreeX_NodeRSGraphicMut(node)->graphic_shadow = parent_graphic->graphic_shadow;
        }
    }

    node->link.parent_tree_idx = parent_idx;
    int last = host->tree->nodes[parent_idx].link.last_child_tree_idx;
    if( last == -1 )
        host->tree->nodes[parent_idx].link.first_child_tree_idx = node->idx;
    else
        host->tree->nodes[last].link.next_sibling_tree_idx = node->idx;
    host->tree->nodes[parent_idx].link.last_child_tree_idx = node->idx;

    if( request.dot_operand == 1 )
        vm->dot_component_id = node->user_id;
    else
        vm->active_component_id = node->user_id;

    CS2VMX_SetTraceExtra(
        "parent=0x%08x type=%d child=%d new=0x%08x target=%s",
        (unsigned)parent_id,
        type,
        child_index,
        (unsigned)node->user_id,
        request.dot_operand == 1 ? "dw" : "aw");
#if INTERFACEX_DEBUG_OPS
    fprintf(
        stderr,
        "CC_CREATE: child=0x%x parent=0x%x type=%d sub=%d active=0x%x dot=0x%x\n",
        (unsigned)node->user_id,
        (unsigned)parent_id,
        type,
        child_index,
        (unsigned)vm->active_component_id,
        (unsigned)vm->dot_component_id);
#endif

    UITreeX_InvalidateLayout(host->tree);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_Find(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_Find request)
{
    assert(host);
    assert(vm);

    {
        struct CS2VM_HostRequest yield_req = { 0 };
        int yield_res;

        yield_req.kind = CS2VM_HOST_REQUEST_CC_FIND;
        yield_req.u.cc_find = request;
        yield_res = InterfaceX_VMHost_YieldIfGroupNeeded(host, request.parent_id, &yield_req);
        if( yield_res != CS2VM_EXECNO_OK )
            return yield_res;
    }

    int parent_idx = UITreeX_FindByUserId(host->tree, request.parent_id);
    int found = 0;
    if( parent_idx >= 0 )
    {
        int child_idx = UITreeX_FindChildBySubid(host->tree, parent_idx, request.sub_id);
        if( child_idx >= 0 )
        {
            int child_id = host->tree->nodes[child_idx].user_id;
            if( request.dot_operand == 1 )
                vm->dot_component_id = child_id;
            else
                vm->active_component_id = child_id;
            found = 1;
        }
    }

    CS2VMX_SetTraceExtra(
        "parent=0x%08x sub=%d found=%d target=%s",
        (unsigned)request.parent_id,
        request.sub_id,
        found,
        request.dot_operand == 1 ? "dw" : "aw");
    CS2VMX_PushInt(vm, found);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetPosition(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetPosition request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);

#if INTERFACEX_DEBUG_OPS
    fprintf(
        stderr,
        "CC_SETPOSITION: component=0x%x x=%d y=%d xm=%d ym=%d %s\n",
        (unsigned)request.component_id,
        request.x,
        request.y,
        request.xmode,
        request.ymode,
        node ? "ok" : "MISS (active/dot stale?)");
#endif
    if( !node )
        return CS2VM_EXECNO_OK;

    node->x = request.x;
    node->y = request.y;
    node->x_mode = (int8_t)request.xmode;
    node->y_mode = (int8_t)request.ymode;
    node->if3 = 1;
    UITreeX_InvalidateLayout(host->tree);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetSize(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetSize request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    node->w = request.width;
    node->h = request.height;
    node->w_mode = (int8_t)request.wmode;
    node->h_mode = (int8_t)request.hmode;
    node->if3 = 1;
    UITreeX_InvalidateLayout(host->tree);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetGraphic(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetGraphic request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    return InterfaceX_NodeStoreGraphicId(host, node, request.graphic_id, request.component_id);
}

int
InterfaceX_VMHost_Exec_CC_SetGraphic2(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetGraphic request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    UITreeX_NodeRSGraphicMut(node)->graphic_id2 = request.graphic_id;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetObject(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetObject request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    return InterfaceX_VMHost_Exec_CC_SetObjectOnNode(
        host, request.component_id, request.obj_id, request.count);
}

int
InterfaceX_VMHost_Exec_IF_SetObject(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_SetObject request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    return InterfaceX_VMHost_Exec_CC_SetObjectOnNode(
        host, request.component_id, request.obj_id, request.count);
}

int
InterfaceX_VMHost_Exec_IF_SetGraphic(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetGraphic request)
{
    return InterfaceX_VMHost_Exec_CC_SetGraphic(host, vm, request);
}

int
InterfaceX_VMHost_Exec_IF_SetPosition(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetPosition request)
{
    return InterfaceX_VMHost_Exec_CC_SetPosition(host, vm, request);
}

int
InterfaceX_VMHost_Exec_IF_SetSize(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetSize request)
{
    return InterfaceX_VMHost_Exec_CC_SetSize(host, vm, request);
}

int
InterfaceX_VMHost_Exec_CC_SetTiling(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetTiling request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( node )
        node->tiling = request.tiling != 0;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetOutline(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetOutline request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    UITreeX_NodeRSGraphicMut(node)->outline = request.outline;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetGraphicShadow(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetGraphicShadow request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    UITreeX_NodeRSGraphicMut(node)->graphic_shadow = request.shadow;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetColour(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetColour request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    if( node->kind == UITreeXNodeKind_RSText )
    {
        UITreeX_NodeRSTextMut(node)->color = request.colour;
        return CS2VM_EXECNO_OK;
    }

    if( node->kind == UITreeXNodeKind_RSRect )
    {
        UITreeX_NodeRSRectMut(node)->color = request.colour;
        return CS2VM_EXECNO_OK;
    }

    InterfaceX_ReportOpKindMismatch(node, "text|rect", "CC_SETCOLOUR");
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetFill(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetFill request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    if( node->kind != UITreeXNodeKind_RSRect )
    {
        InterfaceX_ReportOpKindMismatch(node, "rect", "CC_SETFILL");
        return CS2VM_EXECNO_OK;
    }

    UITreeX_NodeRSRectMut(node)->filled = request.filled != 0;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetTrans(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetTrans request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    node->trans = request.trans;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetNoClickThrough(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetNoClickThrough request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    node->no_click_through = request.enabled != 0;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetTextFont(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetTextFont request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node || node->kind != UITreeXNodeKind_RSText )
        return CS2VM_EXECNO_OK;

    UITreeX_NodeRSTextMut(node)->font_id = request.font_id;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetText(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetText request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node || node->kind != UITreeXNodeKind_RSText )
        return CS2VM_EXECNO_OK;

    struct UITreeXNode_RSText* text = UITreeX_NodeRSTextMut(node);
    strncpy(text->text, request.text ? request.text : "", sizeof(text->text) - 1);
    text->text[sizeof(text->text) - 1] = '\0';
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_SetText(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetText request)
{
    return InterfaceX_VMHost_Exec_CC_SetText(host, vm, request);
}

int
InterfaceX_VMHost_Exec_CC_SetTextAlign(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetTextAlign request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node || node->kind != UITreeXNodeKind_RSText )
        return CS2VM_EXECNO_OK;

    struct UITreeXNode_RSText* text = UITreeX_NodeRSTextMut(node);
    text->center = request.x_align;
    text->y_align = request.y_align;
    text->line_height = request.line_height;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetTextShadow(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetTextShadow request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node || node->kind != UITreeXNodeKind_RSText )
        return CS2VM_EXECNO_OK;

    UITreeX_NodeRSTextMut(node)->shadowed = request.shadowed != 0;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetDraggable(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetDraggable request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    node->draggable = 1;
    node->drag_parent_uid = request.parent_uid;
    node->drag_child_index = request.child_index;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetDraggableBehavior(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetDraggableBehavior request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_OK;

    node->drag_behavior = request.behavior;
    node->is_scroll_bar = request.behavior == 1;
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetDragDeadZone(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetDragDeadZone request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( node )
        node->drag_dead_zone = (uint8_t)request.zone;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_SetDragDeadTime(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_SetDragDeadTime request)
{
    assert(host);
    assert(host->builder);
    (void)vm;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( node )
        node->drag_dead_time = (uint8_t)request.time;

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_GetId(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_GetId request)
{
    assert(host);
    assert(host->builder);

    if( request.component_id < 0 )
        return CS2VM_EXECNO_ERROR;

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( !node )
        return CS2VM_EXECNO_ERROR;

    int child_index = node->dynamic ? node->child_index : -1;
    CS2VMX_PushInt(vm, child_index);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_GetX(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_GetId request)
{
    assert(host);
    assert(host->builder);

    CS2VMX_PushInt(vm, UITreeX_GetPosX(host->tree, request.component_id));
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_GetY(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_GetId request)
{
    assert(host);
    assert(host->builder);

    CS2VMX_PushInt(vm, UITreeX_GetPosY(host->tree, request.component_id));
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_GetWidth(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_GetId request)
{
    assert(host);
    assert(host->builder);

    CS2VMX_PushInt(vm, UITreeX_GetLayoutWidth(host->tree, request.component_id));
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_GetHeight(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_GetId request)
{
    assert(host);
    assert(host->builder);

    CS2VMX_PushInt(vm, UITreeX_GetLayoutHeight(host->tree, request.component_id));
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_GetHide(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_GetId request)
{
    assert(host);
    assert(host->builder);

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    CS2VMX_PushInt(vm, node && node->hidden ? 1 : 0);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_FindRoot(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_TargetFind request)
{
    assert(host);
    assert(vm);

    int component_id = request.component_id;
    int found = 0;

    if( host->tree && component_id >= 0 )
    {
        int parent_id = UITreeX_ParentComponentId(host->tree, component_id);
        if( parent_id >= 0 )
        {
            CS2VMX_SetTargetComponentId(vm, request.dot_operand, parent_id);
            found = 1;
        }
    }

    CS2VMX_SetTraceExtra("found=%d target=%s", found, request.dot_operand == 1 ? "dw" : "aw");
    return CS2VMX_PushInt(vm, found);
}

int
InterfaceX_VMHost_Exec_CC_ChildrenFind(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_ChildrenFind request)
{
    assert(host);
    assert(vm);

    {
        struct CS2VM_HostRequest yield_req = { 0 };
        int yield_res;

        yield_req.kind = CS2VM_HOST_REQUEST_CC_CHILDREN_FIND;
        yield_req.u.cc_children_find = request;
        yield_res = InterfaceX_VMHost_YieldIfGroupNeeded(host, request.parent_id, &yield_req);
        if( yield_res != CS2VM_EXECNO_OK )
            return yield_res;
    }

    CS2VMX_ResetChildrenIter(vm);
    if( host->tree )
    {
        vm->children_iter_count = CS2VMX_CollectDynamicChildIndices(
            host->tree,
            request.parent_id,
            request.start_index,
            vm->children_iter_indices,
            CS2VMX_CHILDREN_ITER_MAX);
    }

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_IF_ChildrenFind(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_IF_ChildrenFind request)
{
    assert(host);
    assert(vm);

    {
        struct CS2VM_HostRequest yield_req = { 0 };
        int yield_res;

        yield_req.kind = CS2VM_HOST_REQUEST_IF_CHILDREN_FIND;
        yield_req.u.if_children_find = request;
        yield_res = InterfaceX_VMHost_YieldIfGroupNeeded(host, request.uid, &yield_req);
        if( yield_res != CS2VM_EXECNO_OK )
            return yield_res;
    }

    CS2VMX_ResetChildrenIter(vm);
    if( host->tree )
    {
        vm->children_iter_count = CS2VMX_CollectDynamicChildIndices(
            host->tree,
            request.uid,
            request.start_index,
            vm->children_iter_indices,
            CS2VMX_CHILDREN_ITER_MAX);

        if( UITreeX_FindByUserId(host->tree, request.uid) >= 0 )
            CS2VMX_SetTargetComponentId(vm, request.dot_operand, request.uid);
    }

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_CC_ResolveParent(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_GetId request)
{
    assert(host);
    assert(vm);

    if( request.component_id < 0 || !host->tree )
        return CS2VM_EXECNO_ERROR;

    int parent_id = UITreeX_ParentComponentId(host->tree, request.component_id);
    if( parent_id < 0 )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, parent_id);
}

int
InterfaceX_VMHost_Exec_StructParam(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_StructParam request)
{
    assert(host);
    assert(vm);

    bool is_string = false;
    int intval = 0;
    char const* strval = NULL;
    bool found = false;
    struct Dat2BuildCache* bc = dat2(InterfaceX_HostIO_Cache(&host->host_io));
    if( !dat2_buildcache_struct_get(bc, request.struct_id) )
    {
        return InterfaceX_VMHost_Yield(
            host,
            &(struct CS2VM_HostRequest){
                .kind = CS2VM_HOST_REQUEST_STRUCT_PARAM,
                .u.struct_param = request,
            });
    }
    found = ie_struct_param_lookup(
        bc, request.struct_id, request.param_id, &is_string, &intval, &strval);

    if( found && is_string )
        return CS2VMX_PushStr(vm, strdup(strval ? strval : ""));
    if( found )
        return CS2VMX_PushInt(vm, intval);
    fprintf(
        stderr,
        "STRUCT_PARAM: key miss struct_id=%d param_id=%d\n",
        request.struct_id,
        request.param_id);
    return CS2VMX_PushInt(vm, 0);
}

int
InterfaceX_VMHost_Exec_CC_GetText(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_GetId request)
{
    assert(host);
    assert(vm);

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    if( node && node->kind == UITreeXNodeKind_RSText )
        return CS2VMX_PushStr(vm, strdup(UITreeX_NodeRSText(node)->text));
    return CS2VMX_PushStr(vm, strdup(""));
}

int
InterfaceX_VMHost_Exec_CC_GetTrans(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_CC_GetId request)
{
    assert(host);
    assert(vm);

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, request.component_id);
    int trans = node ? node->trans : 0;
    return CS2VMX_PushInt(vm, trans);
}

int
InterfaceX_VMHost_Exec_IF_Find(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_TargetFind request)
{
    assert(host);
    assert(vm);

    {
        struct CS2VM_HostRequest yield_req = { 0 };
        int yield_res;

        yield_req.kind = CS2VM_HOST_REQUEST_IF_FIND;
        yield_req.u.if_find = request;
        yield_res = InterfaceX_VMHost_YieldIfGroupNeeded(host, request.component_id, &yield_req);
        if( yield_res != CS2VM_EXECNO_OK )
            return yield_res;
    }

    int found = 0;
    if( host->tree && request.component_id >= 0 )
    {
        if( UITreeX_FindByUserId(host->tree, request.component_id) >= 0 )
        {
            CS2VMX_SetTargetComponentId(vm, request.dot_operand, request.component_id);
            found = 1;
        }
    }
    CS2VMX_SetTraceExtra(
        "uid=0x%08x found=%d target=%s",
        (unsigned)request.component_id,
        found,
        request.dot_operand == 1 ? "dw" : "aw");
    return CS2VMX_PushInt(vm, found);
}

int
InterfaceX_VMHost_Exec_IF_GetX(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(host->builder);

    int x = 0;
    if( UITreeX_NodeByComponentId(host, component_id) )
        x = UITreeX_GetPosX(host->tree, component_id);
    return CS2VMX_PushInt(vm, x);
}

int
InterfaceX_VMHost_Exec_IF_GetText(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(vm);

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, component_id);
    if( node && node->kind == UITreeXNodeKind_RSText )
        return CS2VMX_PushStr(vm, strdup(UITreeX_NodeRSText(node)->text));
    return CS2VMX_PushStr(vm, strdup(""));
}

int
InterfaceX_VMHost_Exec_IF_GetScrollWidth(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(vm);

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, component_id);
    int scroll_width = 0;
    if( node && node->kind == UITreeXNodeKind_RSLayer )
        scroll_width = UITreeX_NodeRSLayer(node)->scroll_width;
    return CS2VMX_PushInt(vm, scroll_width);
}

int
InterfaceX_VMHost_Exec_OC_IntParam(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_OC_IntParam request)
{
    assert(host);
    assert(vm);

    struct RSCacheDat2A_ConfigObject* obj =
        dat2_buildcache_object_get(dat2(InterfaceX_HostIO_Cache(&host->host_io)), request.item_id);
    if( !obj )
    {
        return InterfaceX_VMHost_Yield(
            host,
            &(struct CS2VM_HostRequest){
                .kind = CS2VM_HOST_REQUEST_OC_INT_PARAM,
                .u.oc_int_param = request,
            });
    }
    int value = 0;
    if( obj )
    {
        switch( request.field )
        {
        case CS2VM_OC_INT_COST:
            value = obj->cost;
            break;
        case CS2VM_OC_INT_STACKABLE:
            value = obj->stacking_behaviour;
            break;
        case CS2VM_OC_INT_MEMBERS:
            value = obj->is_members ? 1 : 0;
            break;
        case CS2VM_OC_INT_ID:
            value = obj->_id;
            break;
        }
    }
    return CS2VMX_PushInt(vm, value);
}

int
InterfaceX_VMHost_Exec_IF_GetHide(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int component_id)
{
    assert(host);
    assert(host->builder);

    struct UITreeXNode* node = UITreeX_NodeByComponentId(host, component_id);
    CS2VMX_PushInt(vm, node && node->hidden ? 1 : 0);
    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_OC_Param(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_OC_Param request)
{
    assert(host);
    assert(host->builder);
    assert(vm);

    return CS2VM_EXECNO_ERROR;
}

int
InterfaceX_VMHost_Exec_OC_Name(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_OC_Name request)
{
    assert(host);
    assert(vm);
    return CS2VM_EXECNO_ERROR;
}

int
InterfaceX_VMHost_Exec_OC_Unplaceholder(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_OC_Unplaceholder request)
{
    assert(host);
    assert(vm);

    return CS2VM_EXECNO_ERROR;
}

int
InterfaceX_VMHost_Exec(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request)
{
    struct InterfaceX_VMHost* vmhost = (struct InterfaceX_VMHost*)CS2VM_USER(vm);

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_PUSHSCRIPT:
        return InterfaceX_VMHost_Exec_PushScript(vmhost, vm, request->u.push_script.script_id);
    case CS2VM_HOST_REQUEST_INVS_GET_SIZE:
        return InterfaceX_VMHost_Exec_InvSize(vmhost, vm, request->u.invs_get_size.inv_id);
    case CS2VM_HOST_REQUEST_INVS_GET_OBJ:
        return InterfaceX_VMHost_Exec_InvGetObj(vmhost, vm, request->u.invs_get_obj);
    case CS2VM_HOST_REQUEST_INVS_GET_NUM:
        return InterfaceX_VMHost_Exec_InvGetNum(vmhost, vm, request->u.invs_get_num);
    case CS2VM_HOST_REQUEST_INVS_GET_TOTAL:
        return InterfaceX_VMHost_Exec_InvTotal(vmhost, vm, request->u.invs_get_total);
    case CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR:
        return InterfaceX_VMHost_Exec_VarsReadVarp(vmhost, vm, request->u.vars_read_varp);
    case CS2VM_HOST_REQUEST_VARS_READ_VARBIT:
        return InterfaceX_VMHost_Exec_VarsReadVarbit(vmhost, vm, request->u.vars_read_varbit);
    case CS2VM_HOST_REQUEST_VARS_READ_VARC_INT:
        return InterfaceX_VMHost_Exec_VarsReadVarcInt(vmhost, vm, request->u.vars_read_varc_int);
    case CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING:
        return InterfaceX_VMHost_Exec_VarsReadVarcString(
            vmhost, vm, request->u.vars_read_varc_string);
    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_INT:
        return InterfaceX_VMHost_Exec_VarsWriteVarcInt(vmhost, vm, request->u.vars_write_varc_int);
    case CS2VM_HOST_REQUEST_VARS_WRITE_VARC_STRING:
        return InterfaceX_VMHost_Exec_VarsWriteVarcString(
            vmhost, vm, request->u.vars_write_varc_string);
    case CS2VM_HOST_REQUEST_ENUM_LOOKUP:
        return InterfaceX_VMHost_Exec_EnumLookup(vmhost, vm, request->u.enum_lookup);
    case CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT:
        return InterfaceX_VMHost_Exec_EnumGetOutputCount(
            vmhost, vm, request->u.enum_get_output_count);
    case CS2VM_HOST_REQUEST_IF_GETWIDTH:
        return InterfaceX_VMHost_Exec_IF_GetWidth(vmhost, vm, request->u.if_get_width.component_id);
    case CS2VM_HOST_REQUEST_IF_GETHEIGHT:
        return InterfaceX_VMHost_Exec_IF_GetHeight(
            vmhost, vm, request->u.if_get_height.component_id);
    case CS2VM_HOST_REQUEST_IF_GETY:
        return InterfaceX_VMHost_Exec_IF_GetY(vmhost, vm, request->u.if_get_width.component_id);
    case CS2VM_HOST_REQUEST_IF_GETLAYER:
        return InterfaceX_VMHost_Exec_IF_GetLayer(vmhost, vm, request->u.if_get_layer.component_id);
    case CS2VM_HOST_REQUEST_IF_GETTOP:
        return InterfaceX_VMHost_Exec_IF_GetTop(vmhost, vm);
    case CS2VM_HOST_REQUEST_IF_GETSCROLLX:
        return InterfaceX_VMHost_Exec_IF_GetScrollX(
            vmhost, vm, request->u.if_get_scroll_x.component_id);
    case CS2VM_HOST_REQUEST_IF_GETSCROLLY:
        return InterfaceX_VMHost_Exec_IF_GetScrollY(
            vmhost, vm, request->u.if_get_scroll_y.component_id);
    case CS2VM_HOST_REQUEST_IF_GETSCROLLHEIGHT:
        return InterfaceX_VMHost_Exec_IF_GetScrollHeight(
            vmhost, vm, request->u.if_get_scroll_height.component_id);
    case CS2VM_HOST_REQUEST_IF_GETHIDE:
        return InterfaceX_VMHost_Exec_IF_GetHide(vmhost, vm, request->u.if_get_width.component_id);
    case CS2VM_HOST_REQUEST_IF_SETHIDE:
        return InterfaceX_VMHost_Exec_IF_SetHide(vmhost, vm, request->u.if_set_hide);
    case CS2VM_HOST_REQUEST_IF_SETPOSITION:
        return InterfaceX_VMHost_Exec_IF_SetPosition(vmhost, vm, request->u.cc_set_position);
    case CS2VM_HOST_REQUEST_IF_SETSIZE:
        return InterfaceX_VMHost_Exec_IF_SetSize(vmhost, vm, request->u.cc_set_size);
    case CS2VM_HOST_REQUEST_IF_SETSCROLLPOS:
        return InterfaceX_VMHost_Exec_IF_SetScrollPos(vmhost, vm, request->u.if_set_scroll_pos);
    case CS2VM_HOST_REQUEST_IF_SETSCROLLSIZE:
        return InterfaceX_VMHost_Exec_IF_SetScrollSize(vmhost, vm, request->u.if_set_scroll_size);
    case CS2VM_HOST_REQUEST_IF_SETGRAPHIC:
        return InterfaceX_VMHost_Exec_IF_SetGraphic(vmhost, vm, request->u.if_set_graphic);
    case CS2VM_HOST_REQUEST_IF_SETTEXT:
        return InterfaceX_VMHost_Exec_IF_SetText(vmhost, vm, request->u.if_set_text);
    case CS2VM_HOST_REQUEST_IF_SETOUTLINE:
        return InterfaceX_VMHost_Exec_IF_SetOutline(vmhost, vm, request->u.if_set_outline);
    case CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT:
        return InterfaceX_VMHost_Exec_IF_SetOnVarTransmit(
            vmhost, vm, request->u.if_set_on_var_transmit);
    case CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT:
        return InterfaceX_VMHost_Exec_IF_SetOnInvTransmit(
            vmhost, vm, request->u.if_set_on_inv_transmit);
    case CS2VM_HOST_REQUEST_IF_SETONOP:
        return InterfaceX_VMHost_Exec_IF_SetOnOp(vmhost, vm, request->u.if_set_on_op);
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER:
        return InterfaceX_VMHost_Exec_IF_SetOnMouseOver(
            vmhost, vm, request->u.if_set_on_mouse_over);
    case CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE:
        return InterfaceX_VMHost_Exec_IF_SetOnMouseLeave(
            vmhost, vm, request->u.if_set_on_mouse_leave);
    case CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT:
        return InterfaceX_VMHost_Exec_IF_SetOnMouseRepeat(
            vmhost, vm, request->u.if_set_on_mouse_repeat);
    case CS2VM_HOST_REQUEST_IF_SETONTIMER:
        return InterfaceX_VMHost_Exec_IF_SetOnTimer(vmhost, vm, request->u.if_set_on_timer);
    case CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL:
        return InterfaceX_VMHost_Exec_IF_SetOnScrollWheel(
            vmhost, vm, request->u.if_set_on_scroll_wheel);
    case CS2VM_HOST_REQUEST_IF_SETONKEY:
        return InterfaceX_VMHost_Exec_IF_SetOnKey(vmhost, vm, request->u.if_set_on_key);
    case CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT:
        return InterfaceX_VMHost_Exec_IF_SetOnMiscTransmit(
            vmhost, vm, request->u.if_set_on_misc_transmit);
    case CS2VM_HOST_REQUEST_IF_SETOP:
        return InterfaceX_VMHost_Exec_IF_SetOp(vmhost, vm, request->u.if_set_op);
    case CS2VM_HOST_REQUEST_IF_SETOPBASE:
        return InterfaceX_VMHost_Exec_IF_SetOpBase(vmhost, vm, request->u.if_set_op_base);
    case CS2VM_HOST_REQUEST_IF_SETOPSUBMENU:
        return InterfaceX_VMHost_Exec_IF_SetOpSubmenu(vmhost, vm, request->u.if_set_op_submenu);
    case CS2VM_HOST_REQUEST_IF_SETTARGETPRIORITY:
        return InterfaceX_VMHost_Exec_IF_SetTargetPriority(
            vmhost, vm, request->u.if_set_target_priority);
    case CS2VM_HOST_REQUEST_IF_CLEAROPS:
        return InterfaceX_VMHost_Exec_IF_ClearOps(vmhost, vm, request->u.if_clear_ops);
    case CS2VM_HOST_REQUEST_IF_SETOBJECT:
        return InterfaceX_VMHost_Exec_IF_SetObject(vmhost, vm, request->u.if_set_object);
    case CS2VM_HOST_REQUEST_CC_DELETEALL:
        return InterfaceX_VMHost_Exec_CC_DeleteAll(
            vmhost, vm, request->u.cc_delete_all.component_id);
    case CS2VM_HOST_REQUEST_CC_CREATE:
        return InterfaceX_VMHost_Exec_CC_Create(vmhost, vm, request->u.cc_create);
    case CS2VM_HOST_REQUEST_CC_FIND:
        return InterfaceX_VMHost_Exec_CC_Find(vmhost, vm, request->u.cc_find);
    case CS2VM_HOST_REQUEST_CC_SETPOSITION:
        return InterfaceX_VMHost_Exec_CC_SetPosition(vmhost, vm, request->u.cc_set_position);
    case CS2VM_HOST_REQUEST_CC_SETSIZE:
        return InterfaceX_VMHost_Exec_CC_SetSize(vmhost, vm, request->u.cc_set_size);
    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC:
        return InterfaceX_VMHost_Exec_CC_SetGraphic(vmhost, vm, request->u.cc_set_graphic);
    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC2:
        return InterfaceX_VMHost_Exec_CC_SetGraphic2(vmhost, vm, request->u.cc_set_graphic2);
    case CS2VM_HOST_REQUEST_CC_SETTILING:
        return InterfaceX_VMHost_Exec_CC_SetTiling(vmhost, vm, request->u.cc_set_tiling);
    case CS2VM_HOST_REQUEST_CC_SETOUTLINE:
        return InterfaceX_VMHost_Exec_CC_SetOutline(vmhost, vm, request->u.cc_set_outline);
    case CS2VM_HOST_REQUEST_CC_SETGRAPHICSHADOW:
        return InterfaceX_VMHost_Exec_CC_SetGraphicShadow(
            vmhost, vm, request->u.cc_set_graphic_shadow);
    case CS2VM_HOST_REQUEST_CC_SETCOLOUR:
        return InterfaceX_VMHost_Exec_CC_SetColour(vmhost, vm, request->u.cc_set_colour);
    case CS2VM_HOST_REQUEST_CC_SETFILL:
        return InterfaceX_VMHost_Exec_CC_SetFill(vmhost, vm, request->u.cc_set_fill);
    case CS2VM_HOST_REQUEST_CC_SETTRANS:
        return InterfaceX_VMHost_Exec_CC_SetTrans(vmhost, vm, request->u.cc_set_trans);
    case CS2VM_HOST_REQUEST_CC_SETNOCLICKTHROUGH:
        return InterfaceX_VMHost_Exec_CC_SetNoClickThrough(
            vmhost, vm, request->u.cc_set_no_click_through);
    case CS2VM_HOST_REQUEST_CC_SETTEXT:
        return InterfaceX_VMHost_Exec_CC_SetText(vmhost, vm, request->u.cc_set_text);
    case CS2VM_HOST_REQUEST_CC_SETTEXTFONT:
        return InterfaceX_VMHost_Exec_CC_SetTextFont(vmhost, vm, request->u.cc_set_text_font);
    case CS2VM_HOST_REQUEST_CC_SETTEXTALIGN:
        return InterfaceX_VMHost_Exec_CC_SetTextAlign(vmhost, vm, request->u.cc_set_text_align);
    case CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW:
        return InterfaceX_VMHost_Exec_CC_SetTextShadow(vmhost, vm, request->u.cc_set_text_shadow);
    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLE:
        return InterfaceX_VMHost_Exec_CC_SetDraggable(vmhost, vm, request->u.cc_set_draggable);
    case CS2VM_HOST_REQUEST_CC_SETDRAGGABLEBEHAVIOR:
        return InterfaceX_VMHost_Exec_CC_SetDraggableBehavior(
            vmhost, vm, request->u.cc_set_draggable_behavior);
    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADZONE:
        return InterfaceX_VMHost_Exec_CC_SetDragDeadZone(
            vmhost, vm, request->u.cc_set_drag_dead_zone);
    case CS2VM_HOST_REQUEST_CC_SETDRAGDEADTIME:
        return InterfaceX_VMHost_Exec_CC_SetDragDeadTime(
            vmhost, vm, request->u.cc_set_drag_dead_time);
    case CS2VM_HOST_REQUEST_CC_SETOP:
        return InterfaceX_VMHost_Exec_CC_SetOp(vmhost, vm, request->u.if_set_op);
    case CS2VM_HOST_REQUEST_CC_SETOBJECT:
        return InterfaceX_VMHost_Exec_CC_SetObject(vmhost, vm, request->u.cc_set_object);
    case CS2VM_HOST_REQUEST_CC_GETID:
        return InterfaceX_VMHost_Exec_CC_GetId(vmhost, vm, request->u.cc_get_id);
    case CS2VM_HOST_REQUEST_CC_GETX:
        return InterfaceX_VMHost_Exec_CC_GetX(vmhost, vm, request->u.cc_get_id);
    case CS2VM_HOST_REQUEST_CC_GETY:
        return InterfaceX_VMHost_Exec_CC_GetY(vmhost, vm, request->u.cc_get_id);
    case CS2VM_HOST_REQUEST_CC_GETWIDTH:
        return InterfaceX_VMHost_Exec_CC_GetWidth(vmhost, vm, request->u.cc_get_id);
    case CS2VM_HOST_REQUEST_CC_GETHEIGHT:
        return InterfaceX_VMHost_Exec_CC_GetHeight(vmhost, vm, request->u.cc_get_id);
    case CS2VM_HOST_REQUEST_CC_GETHIDE:
        return InterfaceX_VMHost_Exec_CC_GetHide(vmhost, vm, request->u.cc_get_id);
    case CS2VM_HOST_REQUEST_CC_SETONCLICK:
        return InterfaceX_VMHost_Exec_CC_SetOnClick(vmhost, vm, request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_CC_SETONHOLD:
        return InterfaceX_VMHost_Exec_CC_SetOnHold(vmhost, vm, request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER:
        return InterfaceX_VMHost_Exec_CC_SetOnMouseOver(vmhost, vm, request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE:
        return InterfaceX_VMHost_Exec_CC_SetOnMouseLeave(vmhost, vm, request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT:
        return InterfaceX_VMHost_Exec_CC_SetOnMouseRepeat(vmhost, vm, request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_CC_SETONDRAG:
        return InterfaceX_VMHost_Exec_CC_SetOnDrag(vmhost, vm, request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL:
        return InterfaceX_VMHost_Exec_CC_SetOnScrollWheel(vmhost, vm, request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_CC_SETONKEY:
        return InterfaceX_VMHost_Exec_CC_SetOnKey(vmhost, vm, request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_CC_SETONOP:
        return InterfaceX_VMHost_Exec_CC_SetOnOp(vmhost, vm, request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE:
        return InterfaceX_VMHost_Exec_CC_SetOnDragComplete(vmhost, vm, request->u.cc_set_on_op);
    case CS2VM_HOST_REQUEST_OC_PARAM:
        return InterfaceX_VMHost_Exec_OC_Param(vmhost, vm, request->u.oc_param);
    case CS2VM_HOST_REQUEST_OC_NAME:
        return InterfaceX_VMHost_Exec_OC_Name(vmhost, vm, request->u.oc_name);
    case CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER:
        return InterfaceX_VMHost_Exec_OC_Unplaceholder(vmhost, vm, request->u.oc_unplaceholder);
    case CS2VM_HOST_REQUEST_PARAHEIGHT:
        return InterfaceX_VMHost_Exec_ParaHeight(vmhost, vm, request->u.para_height);
    case CS2VM_HOST_REQUEST_IF_SETON_DISCARD:
    case CS2VM_HOST_REQUEST_CC_SETON_DISCARD:
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_PARAWIDTH:
        return InterfaceX_VMHost_Exec_ParaWidth(vmhost, vm, request->u.para_height);
    case CS2VM_HOST_REQUEST_CC_SETSCROLLPOS:
        return InterfaceX_VMHost_Exec_CC_SetScrollPos(vmhost, vm, request->u.cc_set_scroll_pos);
    case CS2VM_HOST_REQUEST_CC_SETSCROLLSIZE:
        return InterfaceX_VMHost_Exec_CC_SetScrollSize(vmhost, vm, request->u.cc_set_scroll_size);
    case CS2VM_HOST_REQUEST_WIDGET_SET_INT:
        return InterfaceX_VMHost_Exec_WidgetSetInt(vmhost, vm, request->u.widget_set_int);
    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL:
        return InterfaceX_VMHost_Exec_WidgetSetModel(vmhost, vm, request->u.widget_set_model);
    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_ANGLE:
        return InterfaceX_VMHost_Exec_WidgetSetModelAngle(
            vmhost, vm, request->u.widget_set_model_angle);
    case CS2VM_HOST_REQUEST_WIDGET_SET_ARC:
        return InterfaceX_VMHost_Exec_WidgetSetArc(vmhost, vm, request->u.widget_set_arc);
    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND:
        return InterfaceX_VMHost_Exec_WidgetSetModelKind(
            vmhost, vm, request->u.widget_set_model_kind);
    case CS2VM_HOST_REQUEST_WIDGET_INPUT_INT:
        return InterfaceX_VMHost_Exec_WidgetInputInt(vmhost, vm, request->u.widget_input_int);
    case CS2VM_HOST_REQUEST_CC_FINDROOT:
        return InterfaceX_VMHost_Exec_CC_FindRoot(vmhost, vm, request->u.cc_findroot);
    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND:
        return InterfaceX_VMHost_Exec_CC_ChildrenFind(vmhost, vm, request->u.cc_children_find);
    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
        return InterfaceX_VMHost_Exec_IF_ChildrenFind(vmhost, vm, request->u.if_children_find);
    case CS2VM_HOST_REQUEST_CC_RESOLVE_PARENT:
        return InterfaceX_VMHost_Exec_CC_ResolveParent(vmhost, vm, request->u.cc_resolve_parent);
    case CS2VM_HOST_REQUEST_STRUCT_PARAM:
        return InterfaceX_VMHost_Exec_StructParam(vmhost, vm, request->u.struct_param);
    case CS2VM_HOST_REQUEST_CC_GETTEXT:
        return InterfaceX_VMHost_Exec_CC_GetText(vmhost, vm, request->u.cc_gettext);
    case CS2VM_HOST_REQUEST_CC_GETTRANS:
        return InterfaceX_VMHost_Exec_CC_GetTrans(vmhost, vm, request->u.cc_gettrans);
    case CS2VM_HOST_REQUEST_IF_FIND:
        return InterfaceX_VMHost_Exec_IF_Find(vmhost, vm, request->u.if_find);
    case CS2VM_HOST_REQUEST_IF_GETX:
        return InterfaceX_VMHost_Exec_IF_GetX(vmhost, vm, request->u.if_getx.component_id);
    case CS2VM_HOST_REQUEST_IF_GETTEXT:
        return InterfaceX_VMHost_Exec_IF_GetText(vmhost, vm, request->u.if_gettext.component_id);
    case CS2VM_HOST_REQUEST_IF_GETSCROLLWIDTH:
        return InterfaceX_VMHost_Exec_IF_GetScrollWidth(
            vmhost, vm, request->u.if_getscrollwidth.component_id);
    case CS2VM_HOST_REQUEST_OC_INT_PARAM:
        return InterfaceX_VMHost_Exec_OC_IntParam(vmhost, vm, request->u.oc_int_param);
    case CS2VM_HOST_REQUEST_CLIENTCLOCK:
        return InterfaceX_VMHost_ClientClock(vm);
    default:
        printf("VMHost: unknown request kind: %d\n", request->kind);
        return CS2VM_EXECNO_ERROR;
    }
}

// dat2 disk
//   └─ Interfaces index, archive N
//        └─ RSCacheDat2Disk_Archive (compressed blob + file_count metadata)
//             └─ RSCacheShared_FileListNewFromCacheArchive
//                  └─ files[0..count-1]  (one encoded widget each)
//                       └─ component_decode_from_bytes((N<<16)|i, ...)
//                            ├─ byte[0]==255 → DecodeIf3
//                            └─ else         → DecodeIf1
//                                 └─ RSCacheDat2A_Component in InterfaceArchive
//                                      └─ (optional) ToriAuxLibCache_SubmitComponentsFromDat2
static int
InterfaceX_ListInterfaceIds(struct RSCacheDat2Disk* cache)
{
    struct RSCacheDat2Disk_ReferenceTable* table = cache->tables[RSCacheDat2Disk_Table_Interfaces];
    if( !table )
    {
        fprintf(stderr, "failed to load interfaces reference table\n");
        return 1;
    }

    for( int i = 0; i < table->id_count; i++ )
        printf("%d\n", table->ids[i]);

    return 0;
}

static void
InterfaceX_PrintUsage(char const* argv0)
{
    fprintf(
        stderr,
        "usage: %s [--list] [--no-bmp] [--cs2-trace] [--cs2-trace-all]\n"
        "       %s --dump-component <interface_id> <file_index>\n"
        "       %s [interface_id]\n"
        "  default interface_id: %d (inventory)\n"
        "  --dump-component: print raw bytes + decoded IF3 header for one component file\n"
        "  --cs2-trace: log targeting opcode trace to stderr (CS2TRACE lines)\n"
        "  --cs2-trace-all: log every opcode to stderr\n",
        argv0,
        argv0,
        argv0,
        INVENTORY_INTERFACE);
}

static int
InterfaceX_ResolveObjIconCountVariant(
    struct InterfaceX_VMHost* host,
    int obj_id,
    int count)
{
    assert(host);

    struct RSCacheDat2A_ConfigObject* obj =
        dat2_buildcache_object_get(dat2(InterfaceX_HostIO_Cache(&host->host_io)), obj_id);
    if( !obj )
        return obj_id;

    int resolved = obj_id;
    if( count > 1 )
    {
        int countobj_id = -1;
        for( int i = 0; i < 10; i++ )
        {
            if( obj->count_co[i] != 0 && count >= obj->count_co[i] )
                countobj_id = obj->count_obj[i];
        }
        if( countobj_id >= 0 )
            resolved = countobj_id;
    }

    return resolved;
}

static bool
InterfaceX_RuntimeHookOwnsComponent(
    struct InterfaceX_VMHost const* host,
    int component_id)
{
    if( !host )
        return false;
    for( int i = 0; i < host->inv_transmit_hook_count; i++ )
    {
        if( host->inv_transmit_hooks[i].component_id == component_id )
            return true;
    }
    for( int i = 0; i < host->var_transmit_hook_count; i++ )
    {
        if( host->var_transmit_hooks[i].component_id == component_id )
            return true;
    }
    return false;
}

static void
InterfaceX_VMHost_FireVarTransmitHooks(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm)
{
    assert(host);
    assert(vm);

    for( int i = 0; i < host->var_transmit_hook_count; i++ )
    {
        struct InterfaceX_VarTransmitHook const* hook = &host->var_transmit_hooks[i];
        if( hook->script_id <= 0 )
            continue;
        if( hook->trigger_count <= 0 )
            continue;

        printf(
            "running onVarTransmit script %d for component %d (args:",
            hook->script_id,
            hook->component_id);
        for( int j = 0; j < hook->int_arg_count; j++ )
            printf(" %d", hook->int_args[j]);
        printf(")\n");

        InterfaceX_VMHost_QueueScript(
            host,
            hook->script_id,
            hook->component_id,
            hook->int_args,
            hook->int_arg_count,
            NULL,
            0);
    }
}

static void
InterfaceX_VMHost_FireCacheVarTransmitHooks(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct ToriAuxLibCore_Component** components,
    int component_count)
{
    assert(host);
    assert(vm);
    if( !components || component_count <= 0 )
        return;

    for( int i = 0; i < component_count; i++ )
    {
        struct ToriAuxLibCore_Component* comp = components[i];
        if( !comp )
            continue;
        if( InterfaceX_RuntimeHookOwnsComponent(host, comp->id) )
            continue;
        if( comp->on_varp_transmit.argc <= 0 )
            continue;
        if( comp->varp_triggers_count <= 0 )
            continue;

        int script_id = comp->on_varp_transmit.argv[0];
        int args[TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX];
        int arg_count = comp->on_varp_transmit.argc - 1;
        if( arg_count > TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX )
            arg_count = TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX;
        for( int j = 0; j < arg_count; j++ )
            args[j] = comp->on_varp_transmit.argv[j + 1];

        printf("running cache onVarTransmit script %d for component %d\n", script_id, comp->id);
        InterfaceX_VMHost_QueueScript(host, script_id, comp->id, args, arg_count, NULL, 0);
    }
}

static void
InterfaceX_VMHost_FireInvTransmitHooks(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm)
{
    assert(host);
    assert(vm);

    for( int i = 0; i < host->inv_transmit_hook_count; i++ )
    {
        struct InterfaceX_InvTransmitHook const* hook = &host->inv_transmit_hooks[i];
        if( hook->script_id <= 0 )
            continue;

        printf(
            "running onInvTransmit script %d for component %d (args:",
            hook->script_id,
            hook->component_id);
        for( int j = 0; j < hook->int_arg_count; j++ )
            printf(" %d", hook->int_args[j]);
        printf(")\n");

        InterfaceX_VMHost_QueueScript(
            host,
            hook->script_id,
            hook->component_id,
            hook->int_args,
            hook->int_arg_count,
            NULL,
            0);
    }
}

static void
InterfaceX_VMHost_FireCacheInvTransmitHooks(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct ToriAuxLibCore_Component** components,
    int component_count)
{
    assert(host);
    assert(vm);
    if( !components || component_count <= 0 )
        return;

    for( int i = 0; i < component_count; i++ )
    {
        struct ToriAuxLibCore_Component* comp = components[i];
        if( !comp )
            continue;
        if( InterfaceX_RuntimeHookOwnsComponent(host, comp->id) )
            continue;
        if( comp->on_inv_transmit.argc <= 0 )
            continue;

        int script_id = comp->on_inv_transmit.argv[0];
        int args[INTERFACEX_INV_TRANSMIT_INT_ARG_MAX];
        int arg_count = comp->on_inv_transmit.argc - 1;
        if( arg_count > INTERFACEX_INV_TRANSMIT_INT_ARG_MAX )
            arg_count = INTERFACEX_INV_TRANSMIT_INT_ARG_MAX;
        for( int j = 0; j < arg_count; j++ )
            args[j] = comp->on_inv_transmit.argv[j + 1];

        printf("running cache onInvTransmit script %d for component %d\n", script_id, comp->id);
        InterfaceX_VMHost_QueueScript(host, script_id, comp->id, args, arg_count, NULL, 0);
    }
}

static void
prefetch_collect_script_id(
    int script_id,
    int** script_ids,
    int* script_count,
    int* script_cap)
{
    if( script_id < 0 )
        return;

    for( int j = 0; j < *script_count; j++ )
    {
        if( (*script_ids)[j] == script_id )
            return;
    }

    if( *script_count >= *script_cap )
    {
        int new_cap = *script_cap < 8 ? 8 : *script_cap * 2;
        int* grown = realloc(*script_ids, (size_t)new_cap * sizeof(int));
        if( !grown )
            return;
        *script_ids = grown;
        *script_cap = new_cap;
    }

    (*script_ids)[(*script_count)++] = script_id;
}

static void
prefetch_collect_core_hook_script_id(
    struct ToriAuxLibCore_ScriptHook const* hook,
    int** script_ids,
    int* script_count,
    int* script_cap)
{
    if( !hook || hook->argc <= 0 )
        return;

    prefetch_collect_script_id(hook->argv[0], script_ids, script_count, script_cap);
}

static void
InterfaceX_QueueInterfaceOnLoadScripts(
    struct InterfaceX_VMHost* host,
    struct InterfaceX_LoadedInterface const* loaded)
{
    assert(host);
    assert(loaded);

    for( int i = 0; i < loaded->component_count; i++ )
    {
        struct ToriAuxLibCore_Component* component = loaded->components[i];
        if( !component || component->on_load.argc <= 0 )
            continue;

        InterfaceX_VMHost_QueueCoreScriptHook(host, component->id, &component->on_load);
    }
}

static void
InterfaceX_HideInterfaceRoots(
    struct UITreeX* tree,
    int interface_id)
{
    assert(tree);
    if( interface_id < 0 )
        return;

    int iface_prefix = interface_id << 16;
    for( int i = 0; i < tree->node_count; i++ )
    {
        struct UITreeXNode* node = &tree->nodes[i];
        if( node->user_id < 0 )
            continue;
        if( (node->user_id & 0xFFFF0000) != (unsigned)iface_prefix )
            continue;
        if( node->link.parent_tree_idx < 0 )
            node->hidden = 1;
    }
}

static void
InterfaceX_FreeLoadedInterface(struct InterfaceX_LoadedInterface* loaded)
{
    if( !loaded )
        return;

    if( loaded->components )
    {
        if( loaded->owns_components )
        {
            for( int i = 0; i < loaded->component_count; i++ )
                ToriAuxLibCore_ComponentFree(loaded->components[i]);
        }
        free(loaded->components);
    }

    memset(loaded, 0, sizeof(*loaded));
}

static bool
InterfaceX_IsGroupLoaded(
    struct InterfaceX_VMHost const* host,
    int group_id)
{
    if( !host || group_id <= 0 )
        return false;
    if( host->interface_id == group_id )
        return host->primary_pack_integrated;
    for( int i = 0; i < host->extra_group_count; i++ )
    {
        if( host->extra_group_ids[i] == group_id )
            return true;
    }
    return false;
}

#include "interfacex_tasks.inc.c"

static bool
InterfaceX_IntegrateInterfaceGroup(
    struct InterfaceX_VMHost* host,
    int group_id)
{
    return InterfaceX_ProcessInterfacePack(host, group_id, NULL);
}

static void
InterfaceX_FreeExtraGroups(struct InterfaceX_VMHost* host)
{
    if( !host )
        return;

    for( int i = 0; i < host->extra_group_count; i++ )
        InterfaceX_FreeLoadedInterface(&host->extra_groups[i]);
    host->extra_group_count = 0;
}

static int g_cs2vm_yield_test_host_calls;

static int
CS2VMX_TestYieldHostExec(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request)
{
    (void)request;

    vm->active_component_id = 0xDEADBEEF;
    vm->dot_component_id = 0xCAFEBABE;
    g_cs2vm_yield_test_host_calls++;
    if( g_cs2vm_yield_test_host_calls == 1 )
        return CS2VM_EXECNO_YIELD;
    return CS2VM_EXECNO_OK;
}

static int
CS2VMX_TestYieldRollback(void)
{
    uint16_t opcodes[2] = { CS2_OP_CC_DELETEALL, CS2_OP_RETURN };
    int operands[2] = { 0, 0 };
    struct CS2_Script script = {
        .script_id = 1,
        .op_count = 2,
        .opcodes = opcodes,
        .int_operands = operands,
    };

    struct CS2VMX vm;
    memset(&vm, 0, sizeof(vm));
    CS2VMX_BindHost(&vm, NULL, CS2VMX_TestYieldHostExec);
    CS2VMX_PushCallScript(&vm, &script);
    CS2VMX_PushInt(&vm, 0x12345678);

    int const saved_int_top = vm.ints_stack_top;

    vm.active_component_id = 0x11111111;
    vm.dot_component_id = 0x22222222;

    g_cs2vm_yield_test_host_calls = 0;
    int res = CS2VMX_RunScript(&vm);
    if( res != CS2VM_EXECNO_YIELD )
    {
        fprintf(stderr, "yield test: expected YIELD, got %d\n", res);
        return 1;
    }
    if( vm.frames[0].pc != 0 )
    {
        fprintf(stderr, "yield test: expected pc rollback to 0, got %d\n", vm.frames[0].pc);
        return 1;
    }
    if( vm.ints_stack_top != saved_int_top || vm.ints_stack[0] != 0x12345678 )
    {
        fprintf(stderr, "yield test: stack not restored after yield\n");
        return 1;
    }
    if( vm.active_component_id != 0x11111111 || vm.dot_component_id != 0x22222222 )
    {
        fprintf(stderr, "yield test: component ids not restored after yield\n");
        return 1;
    }

    res = CS2VMX_RunScript(&vm);
    if( res != CS2VM_EXECNO_DONE )
    {
        fprintf(stderr, "yield test: expected DONE after resume, got %d\n", res);
        return 1;
    }

    if( g_cs2vm_yield_test_host_calls != 2 )
    {
        fprintf(
            stderr, "yield test: expected 2 host calls, got %d\n", g_cs2vm_yield_test_host_calls);
        return 1;
    }
    if( vm.frame_sp != 0 )
    {
        fprintf(stderr, "yield test: expected empty frame stack after completion\n");
        return 1;
    }

    printf("CS2VM yield rollback test passed\n");
    return 0;
}

int
main(
    int argc,
    char** argv)
{
    int interface_id = INVENTORY_INTERFACE;
    bool list_only = false;
    int dump_iface = -1;
    int dump_file = -1;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--dump-component") == 0 )
        {
            if( i + 2 >= argc )
            {
                fprintf(stderr, "--dump-component requires <interface_id> <file_index>\n");
                InterfaceX_PrintUsage(argv[0]);
                return 1;
            }
            dump_iface = atoi(argv[++i]);
            dump_file = atoi(argv[++i]);
            if( dump_iface <= 0 || dump_file < 0 )
            {
                fprintf(stderr, "invalid --dump-component args\n");
                return 1;
            }
        }
        else if( strcmp(argv[i], "--list") == 0 )
            list_only = true;
        else if( strcmp(argv[i], "--no-bmp") == 0 )
            g_interfacex_write_bmp = 0;
        else if( strcmp(argv[i], "--cs2-trace") == 0 )
            g_cs2_trace_mode = 1;
        else if( strcmp(argv[i], "--cs2-trace-all") == 0 )
            g_cs2_trace_mode = 2;
        else if( strcmp(argv[i], "--test-yield") == 0 )
            return CS2VMX_TestYieldRollback();
        else if( strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 )
        {
            InterfaceX_PrintUsage(argv[0]);
            return 0;
        }
        else if( argv[i][0] == '-' )
        {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            InterfaceX_PrintUsage(argv[0]);
            return 1;
        }
        else
        {
            interface_id = atoi(argv[i]);
            if( interface_id <= 0 )
            {
                fprintf(stderr, "invalid interface id: %s\n", argv[i]);
                return 1;
            }
        }
    }

    struct RSCacheDat2Disk* cache = NULL;
    struct InterfaceX_LoadedInterface primary = { 0 };
    struct UITreeX* tree = NULL;
    struct UITreeXBuilder* builder = NULL;
    struct InterfaceX_VMHost vmhost;

    cache = RSCacheDat2Disk_NewFromDirectory(CACHE_PATH);
    if( !cache )
    {
        fprintf(stderr, "failed to open cache: %s\n", CACHE_PATH);
        return 1;
    }

    if( list_only )
    {
        int rc = InterfaceX_ListInterfaceIds(cache);
        RSCacheDat2Disk_Free(cache);
        return rc;
    }

    // Populates the trig tables (ToriDraw_Sin/Cos, cull projection math) and the HSL16->RGB
    // color table used by model lighting/rasterization. Without this, obj icon models
    // (rendered via ToriDraw_SpriteNewFromModelRaster) project/cull to nothing and render
    // as fully black/transparent sprites.
    ToriDraw_Init();

    struct CS2VMX vm;
    memset(&vm, 0, sizeof(vm));
    vm.active_component_id = -1;
    vm.dot_component_id = -1;

    memset(&vmhost, 0, sizeof(vmhost));

    if( !InterfaceX_VMHost_Init(&vmhost) )
    {
        fprintf(stderr, "failed to init VM host\n");
        return 1;
    }

    CS2VMX_BindHost(&vm, &vmhost, InterfaceX_VMHost_Exec);
    vm.canvas_w = CANVAS_W;
    vm.canvas_h = CANVAS_H;

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    if( !scene )
    {
        fprintf(stderr, "failed to create scene: %d\n", interface_id);
        return 1;
    }

    tree = UITreeX_New();
    builder = calloc(1, sizeof(struct UITreeXBuilder));
    UITreeXBuilder_Init(builder, tree);

    vmhost.builder = builder;
    vmhost.tree = tree;
    vmhost.interface_id = interface_id;
    vmhost.disk_cache = cache;
    vmhost.scene = scene;
    vmhost.next_scene_id = 1;
    if( !InterfaceX_HostIO_Init(&vmhost.host_io, scene, &vmhost.next_scene_id, CACHE_PATH) )
    {
        fprintf(stderr, "failed to init host IO\n");
        return 1;
    }
    ToriAuxLibCache_SetClientscriptDecodeFlags(
        InterfaceX_HostIO_Cache(&vmhost.host_io), CLIENTSCRIPT_DECODE_TRAILER_LEGACY);
    InterfaceX_InvStoreSeedDefaults(&vmhost);

    {
        struct Task_InterfaceX_Main* open_task =
            Task_InterfaceX_Main_New(&vmhost, &vm, interface_id, true, &primary);
        if( !open_task )
        {
            fprintf(stderr, "failed to allocate open task\n");
            return 1;
        }
        InterfaceX_HostIO_QueueTask(
            &vmhost.host_io, open_task, Task_InterfaceX_Main_Run, Task_InterfaceX_Main_Free);
        InterfaceX_HostIO_DrainTasks(&vmhost.host_io);
        if( !vmhost.primary_pack_integrated )
        {
            fprintf(stderr, "failed to open interface %d\n", interface_id);
            InterfaceX_FreeLoadedInterface(&primary);
            return 1;
        }
    }

    UITreeX_LayoutResolve(tree, CANVAS_W, CANVAS_H);
    UITreeX_PrintNodes(tree);

    int* pixels = calloc((size_t)CANVAS_W * (size_t)CANVAS_H, sizeof(int));
    if( !pixels )
    {
        fprintf(stderr, "failed to allocate canvas\n");
        return 1;
    }

    for( int i = 0; i < CANVAS_W * CANVAS_H; i++ )
        pixels[i] = CANVAS_BG;

    UITreeX_Render(&vmhost, tree, pixels);

    if( g_interfacex_write_bmp )
    {
        char outpath[256];
        snprintf(outpath, sizeof(outpath), "./interfacex_%d-%d.bmp", interface_id, 3);
        bmp_write_file(outpath, pixels, CANVAS_W, CANVAS_H);
        printf("wrote %s (%dx%d)\n", outpath, CANVAS_W, CANVAS_H);
    }
    else
        printf("rendered interface %d (%dx%d, no bmp)\n", interface_id, CANVAS_W, CANVAS_H);

    free(pixels);

    free(vmhost.script_queue);
    InterfaceX_FreeLoadedInterface(&primary);
    InterfaceX_FreeExtraGroups(&vmhost);
    InterfaceX_HostIO_Free(&vmhost.host_io);
    return 0;
}
