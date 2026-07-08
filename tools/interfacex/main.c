#include "../src/osrs/rscache/rscache.u.c"
#include "../src2/toriauxlib/toriauxlib.h"
#include "../src2/vm/cs2_opcode.h"
#include "bmp.h"
#include "interfacex_opcode_stack.gen.h"
#include "osrs/rscache/dat2a/dat2a_clientscript.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_clientscript_convert.h"
#include "toriauxlib/c/toriauxlibcache_font_convert.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore_types.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_map.h"
#include "toridraw/toridraw_model_sprite.h"
#include "toridraw/toridraw_sprite.h"
#include "vm/cs2_script.h"
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
/* 0=off, 1=targeting ops only, 2=all opcodes */
static int g_cs2_trace_mode = 0;
static char g_cs2_trace_extra[512];

#define BANK_INTERFACE 12
#define INVENTORY_INTERFACE 630
#define EQUIPMENT_INTERFACE 387

#define CANVAS_W 1024
#define CANVAS_H 768
#define CANVAS_BG 0xFF202428

#define INTERFACEX_CONTENT_MINIMAP 1338
#define INTERFACEX_CONTENT_COMPASS 1339

/* Current render clip rect (canvas-space, [x0,y0) .. [x1,y1) ), narrowed while recursing
 * into a scrollable RSLayer's children and restored on the way back out. Rendering is a
 * single-threaded, synchronous depth-first walk, so plain save/restore around each
 * recursive call is sufficient - no need to thread a clip struct through every helper. */
static int g_render_clip_x0 = 0;
static int g_render_clip_y0 = 0;
static int g_render_clip_x1 = CANVAS_W;
static int g_render_clip_y1 = CANVAS_H;

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
};

struct UITreeXNode_RSRect
{
    int color;
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

enum InterfaceX_ModelKind
{
    INTERFACEX_MODEL_KIND_NONE = 0,
    INTERFACEX_MODEL_KIND_PLAIN = 1,
    INTERFACEX_MODEL_KIND_NPC_HEAD = 2,
    INTERFACEX_MODEL_KIND_PLAYER_HEAD = 3,
    INTERFACEX_MODEL_KIND_PLAYER_SELF = 5,
    INTERFACEX_MODEL_KIND_PLAYER_CHATHEAD = 6,
};

struct UITreeXNode_RSModel
{
    int model_id;
    enum InterfaceX_ModelKind model_kind;
    int zoom;
    int offset_x;
    int offset_y;
    int angle_x;
    int angle_y;
    int angle_z;
    int anim_seq;
    int orthog;
    int transparent;
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
    node->u.rs_graphic.graphic_id = -1;
    node->u.rs_graphic.graphic_id2 = -1;
    node->u.rs_graphic.scene_id = -1;
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

struct UITreeXBuilder
{
    struct UITreeXBuilder_ParentStack parent_stack[36];
    int parent_stack_top;

    struct UITreeX* tree;

    struct UITreeXBuilder_PendingParent pending_parents[MAX_NODES];
    int pending_parent_count;
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

    child->link.parent_tree_idx = parent_idx;

    int last = parent->link.last_child_tree_idx;
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
    node->u.rs_line.line_direction = 1;

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
        "[%d] kind=%s user_id=%s %s",
        node_idx,
        UITreeX_NodeKindStr(node->kind),
        user_id_buf,
        node->dynamic ? "dynamic" : "static");

    if( node->kind == UITreeXNodeKind_RSGraphic )
    {
        printf(
            " graphic=%d abs=%d,%d %dx%d hidden=%d",
            node->u.rs_graphic.graphic_id,
            node->abs_x,
            node->abs_y,
            node->abs_w,
            node->abs_h,
            node->hidden);
    }
    else if( node->kind == UITreeXNodeKind_RSObj )
    {
        printf(
            " obj=%d count=%d abs=%d,%d %dx%d hidden=%d",
            node->u.rs_obj.obj_id,
            node->u.rs_obj.obj_count,
            node->abs_x,
            node->abs_y,
            node->abs_w,
            node->abs_h,
            node->hidden);
    }
    else if( node->kind == UITreeXNodeKind_RSModel )
    {
        printf(
            " model=%d kind=%d zoom=%d abs=%d,%d %dx%d hidden=%d",
            node->u.rs_model.model_id,
            (int)node->u.rs_model.model_kind,
            node->u.rs_model.zoom,
            node->abs_x,
            node->abs_y,
            node->abs_w,
            node->abs_h,
            node->hidden);
    }
    else if( node->kind == UITreeXNodeKind_RSLine )
    {
        printf(
            " color=0x%x width=%d dir=%d abs=%d,%d %dx%d hidden=%d",
            node->u.rs_line.color,
            node->u.rs_line.line_width,
            node->u.rs_line.line_direction,
            node->abs_x,
            node->abs_y,
            node->abs_w,
            node->abs_h,
            node->hidden);
    }
    else if( node->kind == UITreeXNodeKind_RSRect )
    {
        printf(
            " color=0x%x filled=%d abs=%d,%d %dx%d hidden=%d",
            node->u.rs_rect.color,
            node->u.rs_rect.filled,
            node->abs_x,
            node->abs_y,
            node->abs_w,
            node->abs_h,
            node->hidden);
    }
    else if( node->kind == UITreeXNodeKind_RSText )
    {
        printf(
            " font=%d color=0x%x text=\"%s\" abs=%d,%d %dx%d hidden=%d",
            node->u.rs_text.font_id,
            node->u.rs_text.color,
            node->u.rs_text.text,
            node->abs_x,
            node->abs_y,
            node->abs_w,
            node->abs_h,
            node->hidden);
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

#define CS2VM_SCRIPT_ARG_MOUSE_X -2147483647
#define CS2VM_SCRIPT_ARG_MOUSE_Y -2147483646
#define CS2VM_SCRIPT_ARG_WIDGET_ID -2147483645
#define CS2VM_SCRIPT_ARG_OP_INDEX -2147483644
#define CS2VM_SCRIPT_ARG_WIDGET_CHILD_INDEX -2147483643
#define CS2VM_SCRIPT_ARG_DRAG_TARGET_ID -2147483642
#define CS2VM_SCRIPT_ARG_DRAG_TARGET_CHILD_INDEX -2147483641
#define CS2VM_SCRIPT_ARG_KEY_TYPED -2147483640
#define CS2VM_SCRIPT_ARG_KEY_PRESSED -2147483639
#define CS2VM_SCRIPT_ARG_OP_SUBINDEX -2147483638

#define CS2VM_EXECNO_YIELD -2
#define CS2VM_EXECNO_ERROR -1
#define CS2VM_EXECNO_OK 0
#define CS2VM_EXECNO_DONE 1

#define CS2VM_STACK_MAX 1024

#define CACHE_PATH "/Users/matthewevers/Documents/git_repos/3draster/cache"

#define CS2VM_USER(vm) ((struct CS2VMX*)(vm))->user
#define CS2VM_FRAME(vm) &((struct CS2VMX*)(vm))->frames[((struct CS2VMX*)(vm))->frame_sp - 1]
#define CS2VM_MAX_LOCALS 1024
struct CS2VMX_Frame
{
    struct CS2_Script* script;
    int pc;
    int int_locals[CS2VM_MAX_LOCALS];
    char* str_locals[CS2VM_MAX_LOCALS];

    int return_pc;
    int return_frame;

    int has_return;
    int return_int_count;
    int return_ints[8];
};

#define CS2VM_MAX_FRAMES 32
#define CS2VM_MAX_CYCLES 1000000
#define CS2VMX_MAX_ARRAYS 128
#define CS2VMX_ARRAY_CAPACITY 256

struct CS2VMXArray
{
    int values[CS2VMX_ARRAY_CAPACITY];
    int size;
    int defined;
};

#define CS2VMX_CHILDREN_ITER_MAX 256

/* Opcodes missing from cs2_opcode.h but used by gameframe scripts. */
#define CS2_OP_CC_CREATECHILD 106
#define CS2_OP_CC_CREATESIBLING 107
#define CS2_OP_CC_FINDROOT 202
#define CS2_OP_CC_CHILDREN_FIND 203
#define CS2_OP_CC_CHILDREN_FINDNEXTID 204
#define CS2_OP_IF_CHILDREN_FIND 205
#define CS2_OP_IF_CHILDREN_FINDNEXTID 206

static void
CS2VMX_ClearTraceExtra(void)
{
    g_cs2_trace_extra[0] = '\0';
}

static void
CS2VMX_SetTraceExtra(
    char const* fmt,
    ...)
{
    if( !g_cs2_trace_mode || !fmt )
        return;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_cs2_trace_extra, sizeof(g_cs2_trace_extra), fmt, ap);
    va_end(ap);
}

struct CS2VMX;
struct CS2VM_HostRequest;
struct InterfaceX_VMHost;

static int
UITreeX_FindByUserId(
    struct UITreeX const* tree,
    int user_id);
static int
UITreeX_ParentComponentId(
    struct UITreeX const* tree,
    int component_id);

typedef int (*CS2VMX_HostExec_Fn)(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request);

struct CS2VMX
{
    void* user;
    CS2VMX_HostExec_Fn host_exec;
    struct CS2VMX_Frame frames[CS2VM_MAX_FRAMES];

    int ints_stack[CS2VM_STACK_MAX];
    int ints_stack_top;
    char* strs_stack[CS2VM_STACK_MAX];
    int strs_stack_top;

    int frame_sp;

    int active_component_id;
    int dot_component_id;

    /* Diagnostics for the opcode that last caused CS2VM_EXECNO_ERROR, so callers can
     * log which instruction failed instead of just "script N errored". last_error_script_id
     * is the failing frame's own script (may differ from the originally-invoked script id
     * when the error happens inside a gosub callee). */
    int last_error_opcode;
    int last_error_pc;
    int last_error_script_id;

    int children_iter_indices[CS2VMX_CHILDREN_ITER_MAX];
    int children_iter_count;
    int children_iter_index;

    struct CS2VMXArray arrays[CS2VMX_MAX_ARRAYS];
};

static bool
CS2VMX_IsTargetingOpcode(int opcode)
{
    switch( opcode )
    {
    case CS2_OP_IF_FIND:
    case CS2_OP_CC_FIND:
    case CS2_OP_CC_FINDROOT:
    case CS2_OP_CC_CHILDREN_FIND:
    case CS2_OP_CC_CHILDREN_FINDNEXTID:
    case CS2_OP_IF_CHILDREN_FIND:
    case CS2_OP_IF_CHILDREN_FINDNEXTID:
    case CS2_OP_CC_CREATE:
    case CS2_OP_CC_CREATECHILD:
    case CS2_OP_CC_CREATESIBLING:
    case CS2_OP_IF_SETOP:
    case CS2_OP_IF_SETOPBASE:
    case CS2_OP_CC_SETOP:
        return true;
    default:
        return false;
    }
}

static void
CS2VMX_TraceOpcode(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int op_pc,
    int opcode,
    int operand,
    int result)
{
    if( !g_cs2_trace_mode )
        return;
#if !INTERFACEX_DEBUG_OPS
    if( g_cs2_trace_mode == 1 && !CS2VMX_IsTargetingOpcode(opcode) && result == CS2VM_EXECNO_OK )
        return;
#endif

    char const* op_name = CS2_OpCode_String(opcode);
    fprintf(
        stderr,
        "CS2TRACE script=%d pc=%d op=%s(%d) intOp=%d istack=%d sstack=%d aw=0x%08x dw=0x%08x",
        frame->script->script_id,
        op_pc,
        op_name ? op_name : "_unknown",
        opcode,
        operand,
        vm->ints_stack_top,
        vm->strs_stack_top,
        (unsigned)vm->active_component_id,
        (unsigned)vm->dot_component_id);
    if( g_cs2_trace_extra[0] != '\0' )
        fprintf(stderr, " %s", g_cs2_trace_extra);
    if( result != CS2VM_EXECNO_OK )
        fprintf(stderr, " result=error");
    fprintf(stderr, "\n");
    CS2VMX_ClearTraceExtra();
}

enum CS2VM_HostRequestKind
{
    CS2VM_HOST_REQUEST_PUSHSCRIPT,

    CS2VM_HOST_REQUEST_INVS_GET_SIZE,
    CS2VM_HOST_REQUEST_INVS_GET_OBJ,
    CS2VM_HOST_REQUEST_INVS_GET_NUM,
    CS2VM_HOST_REQUEST_INVS_GET_TOTAL,
    CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR,
    CS2VM_HOST_REQUEST_VARS_READ_VARBIT,
    CS2VM_HOST_REQUEST_VARS_READ_VARC_INT,
    CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING,
    CS2VM_HOST_REQUEST_VARS_WRITE_VARC_INT,
    CS2VM_HOST_REQUEST_VARS_WRITE_VARC_STRING,
    CS2VM_HOST_REQUEST_ENUM_LOOKUP,
    CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT,
    // CC Child component
    CS2VM_HOST_REQUEST_CC_DELETEALL,
    CS2VM_HOST_REQUEST_CC_CREATE,
    CS2VM_HOST_REQUEST_CC_FIND,
    CS2VM_HOST_REQUEST_CC_SETPOSITION,
    CS2VM_HOST_REQUEST_CC_SETSIZE,
    CS2VM_HOST_REQUEST_CC_SETGRAPHIC,
    CS2VM_HOST_REQUEST_CC_SETTILING,
    CS2VM_HOST_REQUEST_CC_SETOUTLINE,
    CS2VM_HOST_REQUEST_CC_SETGRAPHICSHADOW,
    CS2VM_HOST_REQUEST_CC_SETCOLOUR,
    CS2VM_HOST_REQUEST_CC_SETFILL,
    CS2VM_HOST_REQUEST_CC_SETTRANS,
    CS2VM_HOST_REQUEST_CC_SETNOCLICKTHROUGH,
    CS2VM_HOST_REQUEST_CC_SETTEXT,
    CS2VM_HOST_REQUEST_CC_SETTEXTFONT,
    CS2VM_HOST_REQUEST_CC_SETTEXTALIGN,
    CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW,
    CS2VM_HOST_REQUEST_CC_SETDRAGGABLE,
    CS2VM_HOST_REQUEST_CC_SETDRAGGABLEBEHAVIOR,
    CS2VM_HOST_REQUEST_CC_SETDRAGDEADZONE,
    CS2VM_HOST_REQUEST_CC_SETDRAGDEADTIME,
    CS2VM_HOST_REQUEST_CC_SETOP,
    CS2VM_HOST_REQUEST_CC_SETOBJECT,
    CS2VM_HOST_REQUEST_CC_GETID,
    CS2VM_HOST_REQUEST_CC_GETX,
    CS2VM_HOST_REQUEST_CC_GETY,
    CS2VM_HOST_REQUEST_CC_GETWIDTH,
    CS2VM_HOST_REQUEST_CC_GETHEIGHT,
    CS2VM_HOST_REQUEST_CC_GETHIDE,
    CS2VM_HOST_REQUEST_CC_SETONCLICK,
    CS2VM_HOST_REQUEST_CC_SETONHOLD,
    CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER,
    CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE,
    CS2VM_HOST_REQUEST_CC_SETONDRAG,
    CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL,
    CS2VM_HOST_REQUEST_CC_SETONKEY,
    CS2VM_HOST_REQUEST_CC_SETONOP,
    CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE,
    CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT,
    // IF Interfaces
    CS2VM_HOST_REQUEST_IF_GETWIDTH,
    CS2VM_HOST_REQUEST_IF_GETHEIGHT,
    CS2VM_HOST_REQUEST_IF_GETY,
    CS2VM_HOST_REQUEST_IF_GETLAYER,
    CS2VM_HOST_REQUEST_IF_GETTOP,
    CS2VM_HOST_REQUEST_IF_GETSCROLLX,
    CS2VM_HOST_REQUEST_IF_GETSCROLLY,
    CS2VM_HOST_REQUEST_IF_GETSCROLLHEIGHT,
    CS2VM_HOST_REQUEST_IF_GETHIDE,
    CS2VM_HOST_REQUEST_IF_SETHIDE,
    CS2VM_HOST_REQUEST_IF_SETPOSITION,
    CS2VM_HOST_REQUEST_IF_SETSIZE,
    CS2VM_HOST_REQUEST_IF_SETSCROLLPOS,
    CS2VM_HOST_REQUEST_IF_SETSCROLLSIZE,
    CS2VM_HOST_REQUEST_IF_SETGRAPHIC,
    CS2VM_HOST_REQUEST_IF_SETTEXT,
    CS2VM_HOST_REQUEST_IF_SETOUTLINE,
    CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT,
    CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT,
    CS2VM_HOST_REQUEST_IF_SETONOP,
    CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER,
    CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE,
    CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT,
    CS2VM_HOST_REQUEST_IF_SETONTIMER,
    CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL,
    CS2VM_HOST_REQUEST_IF_SETONKEY,
    CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT,
    CS2VM_HOST_REQUEST_IF_SETOP,
    CS2VM_HOST_REQUEST_IF_SETOPBASE,
    CS2VM_HOST_REQUEST_IF_SETOPSUBMENU,
    CS2VM_HOST_REQUEST_IF_SETTARGETPRIORITY,
    CS2VM_HOST_REQUEST_IF_CLEAROPS,
    CS2VM_HOST_REQUEST_IF_SETOBJECT,
    // OC Object config
    CS2VM_HOST_REQUEST_OC_PARAM,
    CS2VM_HOST_REQUEST_OC_NAME,
    CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER,
    CS2VM_HOST_REQUEST_PARAHEIGHT,
    CS2VM_HOST_REQUEST_IF_SETON_DISCARD,
    CS2VM_HOST_REQUEST_CC_SETON_DISCARD,
    CS2VM_HOST_REQUEST_PARAWIDTH,

    CS2VM_HOST_REQUEST_CC_SETSCROLLPOS,
    CS2VM_HOST_REQUEST_CC_SETSCROLLSIZE,
    CS2VM_HOST_REQUEST_WIDGET_SET_INT,
    CS2VM_HOST_REQUEST_WIDGET_SET_INT2,
    CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_ANGLE,
    CS2VM_HOST_REQUEST_WIDGET_SET_ARC,
    CS2VM_HOST_REQUEST_WIDGET_SET_MODEL,
    CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND,
    CS2VM_HOST_REQUEST_WIDGET_INPUT_INT,
};

struct CS2VM_HostRequest_PushScript
{
    int script_id;
};

struct CS2VM_HostRequest_InvSize
{
    int inv_id;
};

struct CS2VM_HostRequest_InvGetObj
{
    int inv_id;
    int slot;
};

struct CS2VM_HostRequest_InvGetNum
{
    int inv_id;
    int slot;
};

struct CS2VM_HostRequest_InvTotal
{
    int inv_id;
    int item_id;
};

struct CS2VM_HostRequest_VarsReadVarp
{
    int varp_id;
};

struct CS2VM_HostRequest_VarsReadVarbit
{
    int varbit_id;
};

struct CS2VM_HostRequest_VarsReadVarcInt
{
    int varc_id;
};

struct CS2VM_HostRequest_VarsReadVarcString
{
    int varc_id;
};

struct CS2VM_HostRequest_VarsWriteVarcInt
{
    int varc_id;
    int value;
};

struct CS2VM_HostRequest_VarsWriteVarcString
{
    int varc_id;
    char* value;
};

struct CS2VM_HostRequest_EnumLookup
{
    int input_type;
    int output_type;
    int enum_id;
    int key;
};

struct CS2VM_HostRequest_EnumGetOutputCount
{
    int enum_id;
};

struct CS2VM_HostRequest_CC_DeleteAll
{
    int component_id;
};

struct CS2VM_HostRequest_CC_Create
{
    int parent_id;
    int component_type;
    int child_index;
    int is_nested;
    int dot_operand;
};

struct CS2VM_HostRequest_CC_Find
{
    int parent_id;
    int sub_id;
    int dot_operand;
};

struct CS2VM_HostRequest_IF_GetWidth
{
    int component_id;
};

struct CS2VM_HostRequest_IF_GetHeight
{
    int component_id;
};

struct CS2VM_HostRequest_IF_GetLayer
{
    int component_id;
};

struct CS2VM_HostRequest_IF_SetHide
{
    int component_id;
    bool hidden;
};

struct CS2VM_HostRequest_IF_SetScrollPos
{
    int component_id;
    int scroll_x;
    int scroll_y;
};

struct CS2VM_HostRequest_IF_SetScrollSize
{
    int component_id;
    int scroll_width;
    int scroll_height;
};

struct CS2VM_HostRequest_IF_SetOutline
{
    int component_id;
    int outline;
};

struct CS2VM_HostRequest_IF_SetOnVarTransmit
{
    int component_id;
    int script_id;
    char* signature;
    int* trigger_ids;
    int trigger_count;
};

struct CS2VM_HostRequest_IF_SetOnInvTransmit
{
    int component_id;
    int script_id;
    char* signature;
    int* trigger_ids;
    int trigger_count;
    int int_args[16];
    int int_arg_count;
};

struct CS2VM_HostRequest_IF_SetOnOp
{
    int component_id;
    int script_id;
    char* signature;
    int* trigger_ids;
    int trigger_count;
};

struct CS2VM_HostRequest_CC_SetOnOp
{
    int script_id;
    char* signature;
    int* trigger_ids;
    int trigger_count;
};

struct CS2VM_HostRequest_IF_ClearOps
{
    int component_id;
};

struct CS2VM_HostRequest_IF_SetOp
{
    int component_id;
    int index;
    char* text;
};

struct CS2VM_HostRequest_IF_SetOpBase
{
    int component_id;
    char* text;
};

struct CS2VM_HostRequest_IF_SetOpSubmenu
{
    int component_id;
    int sub_index;
    int op_index;
    char* text;
};

struct CS2VM_HostRequest_IF_SetTargetPriority
{
    int component_id;
    int priority;
};

struct CS2VM_HostRequest_CC_SetPosition
{
    int component_id;
    int x;
    int y;
    int xmode;
    int ymode;
};

struct CS2VM_HostRequest_CC_SetSize
{
    int component_id;
    int width;
    int height;
    int wmode;
    int hmode;
};

struct CS2VM_HostRequest_CC_SetGraphic
{
    int component_id;
    int graphic_id;
};

struct CS2VM_HostRequest_CC_SetTiling
{
    int component_id;
    int tiling;
};

struct CS2VM_HostRequest_CC_SetOutline
{
    int component_id;
    int outline;
};

struct CS2VM_HostRequest_CC_SetGraphicShadow
{
    int component_id;
    int shadow;
};

struct CS2VM_HostRequest_CC_SetColour
{
    int component_id;
    int colour;
};

struct CS2VM_HostRequest_CC_SetFill
{
    int component_id;
    int filled;
};

struct CS2VM_HostRequest_CC_SetTrans
{
    int component_id;
    int trans;
};

struct CS2VM_HostRequest_CC_SetNoClickThrough
{
    int component_id;
    int enabled;
};

struct CS2VM_HostRequest_CC_SetText
{
    int component_id;
    char* text;
};

struct CS2VM_HostRequest_CC_SetTextFont
{
    int component_id;
    int font_id;
};

struct CS2VM_HostRequest_CC_SetTextAlign
{
    int component_id;
    int x_align;
    int y_align;
    int line_height;
};

struct CS2VM_HostRequest_CC_SetTextShadow
{
    int component_id;
    int shadowed;
};

struct CS2VM_HostRequest_CC_SetDraggable
{
    int component_id;
    int parent_uid;
    int child_index;
};

struct CS2VM_HostRequest_CC_SetDraggableBehavior
{
    int component_id;
    int behavior;
};

struct CS2VM_HostRequest_CC_SetDragDeadZone
{
    int component_id;
    int zone;
};

struct CS2VM_HostRequest_CC_SetDragDeadTime
{
    int component_id;
    int time;
};

struct CS2VM_HostRequest_CC_SetObject
{
    int component_id;
    int obj_id;
    int count;
};

struct CS2VM_HostRequest_CC_GetId
{
    int component_id;
};

struct CS2VM_HostRequest_IF_SetObject
{
    int component_id;
    int obj_id;
    int count;
};

struct CS2VM_HostRequest_OC_Param
{
    int param_id;
    int item_id;
};

struct CS2VM_HostRequest_OC_Name
{
    int item_id;
};

struct CS2VM_HostRequest_OC_Unplaceholder
{
    int item_id;
};

struct CS2VM_HostRequest_ParaHeight
{
    int font_id;
    int max_width;
    char* text;
};

enum CS2VM_WidgetIntField
{
    CS2VM_WIDGET_INT_HFLIP,
    CS2VM_WIDGET_INT_VFLIP,
    CS2VM_WIDGET_INT_ANGLE_2D,
    CS2VM_WIDGET_INT_FILL_COLOUR,
    CS2VM_WIDGET_INT_LINE_WIDTH,
    CS2VM_WIDGET_INT_LINE_DIRECTION,
    CS2VM_WIDGET_INT_FILL_MODE,
    CS2VM_WIDGET_INT_TRANS_BOT,
    CS2VM_WIDGET_INT_NO_SCROLL_THROUGH,
    CS2VM_WIDGET_INT_NO_CLICK_THROUGH,
    CS2VM_WIDGET_INT_PINCH,
    CS2VM_WIDGET_INT_CLICKMASK,
    CS2VM_WIDGET_INT_DRAG_DEAD_ZONE,
    CS2VM_WIDGET_INT_DRAG_DEAD_TIME,
    CS2VM_WIDGET_INT_MODEL_ANIM,
    CS2VM_WIDGET_INT_MODEL_ORTHOG,
    CS2VM_WIDGET_INT_MODEL_TRANSPARENT,
    CS2VM_WIDGET_INT_RESUME_PAUSEBUTTON,
};

enum CS2VM_WidgetInputField
{
    CS2VM_WIDGET_INPUT_SUBMITMODE,
    CS2VM_WIDGET_INPUT_SELECTCOLOUR,
    CS2VM_WIDGET_INPUT_WRAPMODE,
    CS2VM_WIDGET_INPUT_LINEWRAPPINGWIDTH,
    CS2VM_WIDGET_INPUT_SELECTBGCOLOUR,
    CS2VM_WIDGET_INPUT_LINECOUNTLIMIT,
    CS2VM_WIDGET_INPUT_CURSORCOLOUR,
    CS2VM_WIDGET_INPUT_CURSORTRANS,
    CS2VM_WIDGET_INPUT_CURSORWIDTH,
    CS2VM_WIDGET_INPUT_CURSORHEIGHT,
    CS2VM_WIDGET_INPUT_CURSOROFFSET,
    CS2VM_WIDGET_INPUT_LINEWIDTHLIMIT,
    CS2VM_WIDGET_INPUT_CHARFILTER,
};

struct CS2VM_HostRequest_WidgetSetInt
{
    int component_id;
    enum CS2VM_WidgetIntField field;
    int value;
};

struct CS2VM_HostRequest_WidgetSetInt2
{
    int component_id;
    enum CS2VM_WidgetIntField field;
    int value_a;
    int value_b;
};

struct CS2VM_HostRequest_WidgetSetModelAngle
{
    int component_id;
    int offset_x;
    int offset_y;
    int angle_x;
    int angle_y;
    int angle_z;
    int zoom;
};

struct CS2VM_HostRequest_WidgetSetArc
{
    int component_id;
    int arc_start;
    int arc_end;
};

struct CS2VM_HostRequest_WidgetSetModel
{
    int component_id;
    int model_id;
};

struct CS2VM_HostRequest_WidgetSetModelKind
{
    int component_id;
    enum InterfaceX_ModelKind model_kind;
    int model_id;
};

struct CS2VM_HostRequest_WidgetInputInt
{
    int component_id;
    enum CS2VM_WidgetInputField field;
    int value;
};

struct CS2VM_HostRequest
{
    enum CS2VM_HostRequestKind kind;

    union
    {
        struct CS2VM_HostRequest_PushScript push_script;
        struct CS2VM_HostRequest_InvSize invs_get_size;
        struct CS2VM_HostRequest_InvGetObj invs_get_obj;
        struct CS2VM_HostRequest_InvGetNum invs_get_num;
        struct CS2VM_HostRequest_InvTotal invs_get_total;
        struct CS2VM_HostRequest_VarsReadVarp vars_read_varp;
        struct CS2VM_HostRequest_VarsReadVarbit vars_read_varbit;
        struct CS2VM_HostRequest_VarsReadVarcInt vars_read_varc_int;
        struct CS2VM_HostRequest_VarsReadVarcString vars_read_varc_string;
        struct CS2VM_HostRequest_VarsWriteVarcInt vars_write_varc_int;
        struct CS2VM_HostRequest_VarsWriteVarcString vars_write_varc_string;
        struct CS2VM_HostRequest_EnumLookup enum_lookup;
        struct CS2VM_HostRequest_EnumGetOutputCount enum_get_output_count;
        struct CS2VM_HostRequest_CC_DeleteAll cc_delete_all;
        struct CS2VM_HostRequest_CC_Create cc_create;
        struct CS2VM_HostRequest_CC_Find cc_find;
        struct CS2VM_HostRequest_CC_SetPosition cc_set_position;
        struct CS2VM_HostRequest_CC_SetSize cc_set_size;
        struct CS2VM_HostRequest_CC_SetGraphic cc_set_graphic;
        struct CS2VM_HostRequest_CC_SetTiling cc_set_tiling;
        struct CS2VM_HostRequest_CC_SetOutline cc_set_outline;
        struct CS2VM_HostRequest_CC_SetGraphicShadow cc_set_graphic_shadow;
        struct CS2VM_HostRequest_CC_SetColour cc_set_colour;
        struct CS2VM_HostRequest_CC_SetFill cc_set_fill;
        struct CS2VM_HostRequest_CC_SetTrans cc_set_trans;
        struct CS2VM_HostRequest_CC_SetNoClickThrough cc_set_no_click_through;
        struct CS2VM_HostRequest_CC_SetText cc_set_text;
        struct CS2VM_HostRequest_CC_SetTextFont cc_set_text_font;
        struct CS2VM_HostRequest_CC_SetTextAlign cc_set_text_align;
        struct CS2VM_HostRequest_CC_SetTextShadow cc_set_text_shadow;
        struct CS2VM_HostRequest_CC_SetDraggable cc_set_draggable;
        struct CS2VM_HostRequest_CC_SetDraggableBehavior cc_set_draggable_behavior;
        struct CS2VM_HostRequest_CC_SetDragDeadZone cc_set_drag_dead_zone;
        struct CS2VM_HostRequest_CC_SetDragDeadTime cc_set_drag_dead_time;
        struct CS2VM_HostRequest_CC_SetObject cc_set_object;
        struct CS2VM_HostRequest_CC_GetId cc_get_id;
        struct CS2VM_HostRequest_CC_SetOnOp cc_set_on_op;
        struct CS2VM_HostRequest_OC_Param oc_param;
        struct CS2VM_HostRequest_OC_Name oc_name;
        struct CS2VM_HostRequest_OC_Unplaceholder oc_unplaceholder;
        struct CS2VM_HostRequest_ParaHeight para_height;
        struct CS2VM_HostRequest_IF_GetWidth if_get_width;
        struct CS2VM_HostRequest_IF_GetHeight if_get_height;
        struct CS2VM_HostRequest_IF_GetLayer if_get_layer;
        struct CS2VM_HostRequest_IF_GetLayer if_get_scroll_x;
        struct CS2VM_HostRequest_IF_GetLayer if_get_scroll_y;
        struct CS2VM_HostRequest_IF_GetLayer if_get_scroll_height;
        struct CS2VM_HostRequest_IF_SetHide if_set_hide;
        struct CS2VM_HostRequest_IF_SetScrollPos if_set_scroll_pos;
        struct CS2VM_HostRequest_IF_SetScrollSize if_set_scroll_size;
        struct CS2VM_HostRequest_CC_SetGraphic if_set_graphic;
        struct CS2VM_HostRequest_CC_SetText if_set_text;
        struct CS2VM_HostRequest_IF_SetOutline if_set_outline;
        struct CS2VM_HostRequest_IF_SetOnVarTransmit if_set_on_var_transmit;
        struct CS2VM_HostRequest_IF_SetOnInvTransmit if_set_on_inv_transmit;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_op;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_mouse_over;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_mouse_leave;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_mouse_repeat;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_timer;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_scroll_wheel;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_key;
        struct CS2VM_HostRequest_IF_SetOnOp if_set_on_misc_transmit;
        struct CS2VM_HostRequest_IF_SetOp if_set_op;
        struct CS2VM_HostRequest_IF_SetOpBase if_set_op_base;
        struct CS2VM_HostRequest_IF_SetOpSubmenu if_set_op_submenu;
        struct CS2VM_HostRequest_IF_SetTargetPriority if_set_target_priority;
        struct CS2VM_HostRequest_IF_ClearOps if_clear_ops;
        struct CS2VM_HostRequest_IF_SetObject if_set_object;
        struct CS2VM_HostRequest_IF_SetScrollPos cc_set_scroll_pos;
        struct CS2VM_HostRequest_IF_SetScrollSize cc_set_scroll_size;
        struct CS2VM_HostRequest_WidgetSetInt widget_set_int;
        struct CS2VM_HostRequest_WidgetSetInt2 widget_set_int2;
        struct CS2VM_HostRequest_WidgetSetModelAngle widget_set_model_angle;
        struct CS2VM_HostRequest_WidgetSetArc widget_set_arc;
        struct CS2VM_HostRequest_WidgetSetModel widget_set_model;
        struct CS2VM_HostRequest_WidgetSetModelKind widget_set_model_kind;
        struct CS2VM_HostRequest_WidgetInputInt widget_input_int;
    } u;
};

void
CS2VMX_BindHost(
    struct CS2VMX* vm,
    void* user,
    CS2VMX_HostExec_Fn host_exec)
{
    assert(vm);
    assert(host_exec);
    vm->user = user;
    vm->host_exec = host_exec;
}

static inline int
CS2VMX_DotOrActiveComponentId(
    struct CS2VMX* vm,
    int operand)
{
    assert(vm);
    return operand == 1 ? vm->dot_component_id : vm->active_component_id;
}

static void
CS2VMX_SetTargetComponentId(
    struct CS2VMX* vm,
    int operand,
    int component_id)
{
    assert(vm);
    if( operand == 1 )
        vm->dot_component_id = component_id;
    else
        vm->active_component_id = component_id;
}

static void
CS2VMX_ResetChildrenIter(struct CS2VMX* vm)
{
    assert(vm);
    vm->children_iter_count = 0;
    vm->children_iter_index = 0;
}

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

static inline int
CS2VMX_JumpRelative(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    frame->pc += operand;

    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_PopFrame(struct CS2VMX* vm)
{
    assert(vm);
    vm->frame_sp--;
    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_PopInt(
    struct CS2VMX* vm,
    int* operand)
{
    assert(vm);
    if( vm->ints_stack_top <= 0 )
        return CS2VM_EXECNO_ERROR;
    *operand = vm->ints_stack[--vm->ints_stack_top];
    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_PopIntFrameLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    if( vm->ints_stack_top <= 0 )
        return CS2VM_EXECNO_ERROR;

    frame->int_locals[operand] = vm->ints_stack[--vm->ints_stack_top];

    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_PushInt(
    struct CS2VMX* vm,
    int value)
{
    assert(vm);

    if( vm->ints_stack_top >= CS2VM_STACK_MAX )
        return CS2VM_EXECNO_ERROR;

    vm->ints_stack[vm->ints_stack_top++] = value;

    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_SetIntFrameLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int idx,
    int value)
{
    assert(vm);
    assert(frame);
    frame->int_locals[idx] = value;
    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_SetIntCurrentFrameLocal(
    struct CS2VMX* vm,
    int idx,
    int value)
{
    assert(vm);
    return CS2VMX_SetIntFrameLocal(vm, CS2VM_FRAME(vm), idx, value);
}

static inline int
CS2VMX_PushIntFrameLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int idx)
{
    assert(vm);
    assert(frame);

    return CS2VMX_PushInt(vm, frame->int_locals[idx]);
}

static inline int
CS2VMX_PushIntCurrentFrameLocal(
    struct CS2VMX* vm,
    int idx)
{
    assert(vm);

    struct CS2VMX_Frame* frame = CS2VM_FRAME(vm);
    return CS2VMX_PushIntFrameLocal(vm, frame, idx);
}

static inline int
CS2VMX_PopStr(
    struct CS2VMX* vm,
    char** value)
{
    assert(vm);
    if( vm->strs_stack_top <= 0 )
        return CS2VM_EXECNO_ERROR;
    *value = vm->strs_stack[--vm->strs_stack_top];
    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_PopStrFrameLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    if( vm->strs_stack_top <= 0 )
        return CS2VM_EXECNO_OK;

    frame->str_locals[operand] = vm->strs_stack[--vm->strs_stack_top];

    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_PushStr(
    struct CS2VMX* vm,
    char* value)
{
    assert(vm);

    if( vm->strs_stack_top >= CS2VM_STACK_MAX )
        return CS2VM_EXECNO_ERROR;

    vm->strs_stack[vm->strs_stack_top++] = value;

    return CS2VM_EXECNO_OK;
}

static inline int
CS2VMX_PushStrFrameLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int idx)
{
    assert(vm);
    assert(frame);

    return CS2VMX_PushStr(vm, frame->str_locals[idx]);
}

int
CS2VMX_PushCallScript(
    struct CS2VMX* vm,
    struct CS2_Script* script);

int
CS2VMX_Op_PushVar(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_READ_VARP_AKA_PUSH_VAR;
    request.u.vars_read_varp.varp_id = operand;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_PushVarbit(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_READ_VARBIT;
    request.u.vars_read_varbit.varbit_id = operand;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_PushVarcInt(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_READ_VARC_INT;
    request.u.vars_read_varc_int.varc_id = operand;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_PopVarcInt(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int value;
    if( CS2VMX_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_WRITE_VARC_INT;
    request.u.vars_write_varc_int.varc_id = operand;
    request.u.vars_write_varc_int.value = value;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_PushVarcString(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_READ_VARC_STRING;
    request.u.vars_read_varc_string.varc_id = operand;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_PopVarcString(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    char* value;
    if( CS2VMX_PopStr(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_VARS_WRITE_VARC_STRING;
    request.u.vars_write_varc_string.varc_id = operand;
    request.u.vars_write_varc_string.value = value;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_PushConstantString(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    char* value)
{
    assert(vm);
    return CS2VMX_PushStr(vm, value);
}

int
CS2VMX_Op_PushConstantInt(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int value)
{
    assert(vm);
    return CS2VMX_PushInt(vm, value);
}

int
CS2VMX_Op_PushIntLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    return CS2VMX_PushIntFrameLocal(vm, frame, operand);
}

int
CS2VMX_Op_PushStrLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int idx)
{
    assert(vm);
    assert(frame);
    return CS2VMX_PushStrFrameLocal(vm, frame, idx);
}

int
CS2VMX_Op_PopIntLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    return CS2VMX_PopIntFrameLocal(vm, frame, operand);
}

int
CS2VMX_Op_PopStrLocal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    return CS2VMX_PopStrFrameLocal(vm, frame, operand);
}

int
CS2VMX_Op_JoinString(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    char *b, *a;
    if( CS2VMX_PopStr(vm, &b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopStr(vm, &a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    char buf[512];
    snprintf(buf, sizeof(buf), "%s%s", a ? a : "", b ? b : "");

    char* joined = strdup(buf);
    if( !joined )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushStr(vm, joined);
}

int
CS2VMX_Op_ToString(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int value;
    if( CS2VMX_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);

    char* str = strdup(buf);
    if( !str )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushStr(vm, str);
}

int
CS2VMX_Op_StringLength(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    char* str;
    if( CS2VMX_PopStr(vm, &str) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, str ? (int)strlen(str) : 0);
}

int
CS2VMX_Op_ParaHeight(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int font_id;
    int max_width;
    char* text;

    if( CS2VMX_PopInt(vm, &font_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &max_width) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_PARAHEIGHT;
    request.u.para_height.font_id = font_id;
    request.u.para_height.max_width = max_width;
    request.u.para_height.text = text;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_ParaWidth(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int font_id;
    int max_width;
    char* text;

    if( CS2VMX_PopInt(vm, &font_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &max_width) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_PARAWIDTH;
    request.u.para_height.font_id = font_id;
    request.u.para_height.max_width = max_width;
    request.u.para_height.text = text;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_GosubWithParams(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    assert(vm->host_exec);

    if( vm->frame_sp >= CS2VM_MAX_FRAMES )
        return CS2VM_EXECNO_ERROR;

    struct CS2VMX_Frame* caller = frame;
    caller->return_pc = caller->pc;
    caller->return_frame = vm->frame_sp - 1;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_PUSHSCRIPT;
    request.u.push_script.script_id = operand;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    struct CS2VMX_Frame* callee = &vm->frames[vm->frame_sp - 1];
    int argc = callee->script->int_argument_count + callee->script->string_argument_count;
    int str_args = callee->script->string_argument_count;
    int int_args = callee->script->int_argument_count;

    for( int i = str_args - 1; i >= 0; i-- )
    {
        char* value = NULL;
        if( CS2VMX_PopStr(vm, &value) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        callee->str_locals[i] = value;
    }
    for( int i = int_args - 1; i >= 0; i-- )
    {
        if( CS2VMX_PopIntFrameLocal(vm, callee, i) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_DeleteAll(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame)
{
    assert(vm);
    assert(frame);

    int component_id;

    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_DELETEALL;
    request.u.cc_delete_all.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_Find(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int sub, parent;

    if( CS2VMX_PopInt(vm, &sub) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &parent) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_FIND;
    request.u.cc_find.parent_id = parent;
    request.u.cc_find.sub_id = sub;
    request.u.cc_find.dot_operand = operand;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

// CC_CREATE: When intOp=1 (dot variant .cc_create), sets dotWidget instead of activeWidget
//
// PARITY: Argument count depends on client revision, NOT stack contents.
// - Older revisions (< 200): 3 args [parentUid, type, childIndex] (aka Kronos)
// - Modern revisions (>= 200): 4 args [parentUid, type, childIndex, isNested]
int
CS2VMX_Op_CC_Create(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int is_nested, child_index, type, parent_id;

    if( CS2VMX_PopInt(vm, &is_nested) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &child_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &parent_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_CREATE;
    request.u.cc_create.parent_id = parent_id;
    request.u.cc_create.component_type = type;
    request.u.cc_create.child_index = child_index;
    request.u.cc_create.is_nested = is_nested;
    request.u.cc_create.dot_operand = operand;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

// === Position and Size ===
// 4 args read as array
int
CS2VMX_Op_CC_SetPosition(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int x, y, xmode, ymode;

    if( CS2VMX_PopInt(vm, &ymode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &xmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETPOSITION;
    request.u.cc_set_position.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_position.x = x;
    request.u.cc_set_position.y = y;
    request.u.cc_set_position.xmode = xmode;
    request.u.cc_set_position.ymode = ymode;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetSize(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int w, h, wmode, hmode;

    if( CS2VMX_PopInt(vm, &hmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &wmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &h) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &w) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETSIZE;
    request.u.cc_set_size.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_size.width = w;
    request.u.cc_set_size.height = h;
    request.u.cc_set_size.wmode = wmode;
    request.u.cc_set_size.hmode = hmode;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetGraphic(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int graphic_id;

    if( CS2VMX_PopInt(vm, &graphic_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHIC;
    request.u.cc_set_graphic.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_graphic.graphic_id = graphic_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetTiling(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int tiling;
    if( CS2VMX_PopInt(vm, &tiling) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTILING;
    request.u.cc_set_tiling.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_tiling.tiling = tiling;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetOutline(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int outline;
    if( CS2VMX_PopInt(vm, &outline) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETOUTLINE;
    request.u.cc_set_outline.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_outline.outline = outline;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetGraphicShadow(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int shadow;
    if( CS2VMX_PopInt(vm, &shadow) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHICSHADOW;
    request.u.cc_set_graphic_shadow.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_graphic_shadow.shadow = shadow;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetTiling(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, tiling;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &tiling) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTILING;
    request.u.cc_set_tiling.component_id = component_id;
    request.u.cc_set_tiling.tiling = tiling;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetGraphicShadow(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, shadow;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &shadow) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETGRAPHICSHADOW;
    request.u.cc_set_graphic_shadow.component_id = component_id;
    request.u.cc_set_graphic_shadow.shadow = shadow;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetColour(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int colour;
    if( CS2VMX_PopInt(vm, &colour) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETCOLOUR;
    request.u.cc_set_colour.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_colour.colour = colour;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetColour(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, colour;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &colour) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETCOLOUR;
    request.u.cc_set_colour.component_id = component_id;
    request.u.cc_set_colour.colour = colour;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetFill(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int filled;
    if( CS2VMX_PopInt(vm, &filled) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETFILL;
    request.u.cc_set_fill.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_fill.filled = filled;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetFill(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, filled;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &filled) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETFILL;
    request.u.cc_set_fill.component_id = component_id;
    request.u.cc_set_fill.filled = filled;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetTrans(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int trans;
    if( CS2VMX_PopInt(vm, &trans) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTRANS;
    request.u.cc_set_trans.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_trans.trans = trans;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetTrans(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, trans;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &trans) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTRANS;
    request.u.cc_set_trans.component_id = component_id;
    request.u.cc_set_trans.trans = trans;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetNoClickThrough(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int enabled;
    if( CS2VMX_PopInt(vm, &enabled) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETNOCLICKTHROUGH;
    request.u.cc_set_no_click_through.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_no_click_through.enabled = enabled;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetText(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    char* text;
    if( CS2VMX_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXT;
    request.u.cc_set_text.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_text.text = text;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetTextFont(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int font_id;
    if( CS2VMX_PopInt(vm, &font_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXTFONT;
    request.u.cc_set_text_font.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_text_font.font_id = font_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetTextFont(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, font_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &font_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXTFONT;
    request.u.cc_set_text_font.component_id = component_id;
    request.u.cc_set_text_font.font_id = font_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetTextAlign(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int line_height, y_align, x_align;
    if( CS2VMX_PopInt(vm, &line_height) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &y_align) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &x_align) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXTALIGN;
    request.u.cc_set_text_align.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_text_align.x_align = x_align;
    request.u.cc_set_text_align.y_align = y_align;
    request.u.cc_set_text_align.line_height = line_height;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetTextAlign(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, line_height, y_align, x_align;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &line_height) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &y_align) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &x_align) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXTALIGN;
    request.u.cc_set_text_align.component_id = component_id;
    request.u.cc_set_text_align.x_align = x_align;
    request.u.cc_set_text_align.y_align = y_align;
    request.u.cc_set_text_align.line_height = line_height;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetTextShadow(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int shadowed;
    if( CS2VMX_PopInt(vm, &shadowed) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW;
    request.u.cc_set_text_shadow.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_text_shadow.shadowed = shadowed;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetTextShadow(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, shadowed;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &shadowed) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETTEXTSHADOW;
    request.u.cc_set_text_shadow.component_id = component_id;
    request.u.cc_set_text_shadow.shadowed = shadowed;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetDraggable(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int child_index;
    int parent_uid;

    if( CS2VMX_PopInt(vm, &child_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &parent_uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETDRAGGABLE;
    request.u.cc_set_draggable.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_draggable.parent_uid = parent_uid;
    request.u.cc_set_draggable.child_index = child_index;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetDraggableBehavior(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int behavior;
    if( CS2VMX_PopInt(vm, &behavior) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETDRAGGABLEBEHAVIOR;
    request.u.cc_set_draggable_behavior.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_draggable_behavior.behavior = behavior;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetDragDeadZone(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int zone;
    if( CS2VMX_PopInt(vm, &zone) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETDRAGDEADZONE;
    request.u.cc_set_drag_dead_zone.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_drag_dead_zone.zone = zone;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetDragDeadTime(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int time;
    if( CS2VMX_PopInt(vm, &time) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETDRAGDEADTIME;
    request.u.cc_set_drag_dead_time.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_drag_dead_time.time = time;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetObject(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int count, obj_id;

    if( CS2VMX_PopInt(vm, &count) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &obj_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETOBJECT;
    request.u.cc_set_object.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_object.obj_id = obj_id;
    request.u.cc_set_object.count = count;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetObject(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, count, obj_id;

    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &count) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &obj_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOBJECT;
    request.u.if_set_object.component_id = component_id;
    request.u.if_set_object.obj_id = obj_id;
    request.u.if_set_object.count = count;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetGraphic(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int graphic_id, component_id;

    if( CS2VMX_PopInt(vm, &graphic_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETGRAPHIC;
    request.u.if_set_graphic.component_id = component_id;
    request.u.if_set_graphic.graphic_id = graphic_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetText(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    char* text;

    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETTEXT;
    request.u.if_set_text.component_id = component_id;
    request.u.if_set_text.text = text;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetOp(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int index;
    char* text;

    if( CS2VMX_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETOP;
    request.u.if_set_op.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.if_set_op.index = index;
    request.u.if_set_op.text = text;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetOpBase(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    char* text;
    if( CS2VMX_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOPBASE;
    request.u.if_set_op_base.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.if_set_op_base.text = text;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_ClearOps(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_CLEAROPS;
    request.u.if_clear_ops.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetHide(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int hide;
    if( CS2VMX_PopInt(vm, &hide) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETHIDE;
    request.u.if_set_hide.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.if_set_hide.hidden = hide != 0;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_GetId(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETID;
    request.u.cc_get_id.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_GetX(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETX;
    request.u.cc_get_id.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_GetY(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETY;
    request.u.cc_get_id.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_GetWidth(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETWIDTH;
    request.u.cc_get_id.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_GetHeight(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETHEIGHT;
    request.u.cc_get_id.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_GetHide(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_GETHIDE;
    request.u.cc_get_id.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_GetWidth(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETWIDTH;
    request.u.if_get_width.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_GetHeight(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETHEIGHT;
    request.u.if_get_height.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_GetHide(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETHIDE;
    request.u.if_get_width.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_GetY(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETY;
    request.u.if_get_width.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_GetLayer(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETLAYER;
    request.u.if_get_layer.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_GetTop(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETTOP;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_GetScrollX(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETSCROLLX;
    request.u.if_get_scroll_x.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_GetScrollY(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETSCROLLY;
    request.u.if_get_scroll_y.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_GetScrollHeight(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_GETSCROLLHEIGHT;
    request.u.if_get_scroll_height.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetScrollPos(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    int scroll_x;
    int scroll_y;

    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &scroll_x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &scroll_y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETSCROLLPOS;
    request.u.if_set_scroll_pos.component_id = component_id;
    request.u.if_set_scroll_pos.scroll_x = scroll_x;
    request.u.if_set_scroll_pos.scroll_y = scroll_y;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetScrollSize(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    int scroll_width;
    int scroll_height;

    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &scroll_width) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &scroll_height) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETSCROLLSIZE;
    request.u.if_set_scroll_size.component_id = component_id;
    request.u.if_set_scroll_size.scroll_width = scroll_width;
    request.u.if_set_scroll_size.scroll_height = scroll_height;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetPosition(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    int x, y, xmode, ymode;

    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &ymode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &xmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETPOSITION;
    request.u.cc_set_position.component_id = component_id;
    request.u.cc_set_position.x = x;
    request.u.cc_set_position.y = y;
    request.u.cc_set_position.xmode = xmode;
    request.u.cc_set_position.ymode = ymode;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetOutline(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, outline;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &outline) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOUTLINE;
    request.u.if_set_outline.component_id = component_id;
    request.u.if_set_outline.outline = outline;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetSize(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    int w, h, wmode, hmode;

    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &hmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &wmode) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &h) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &w) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETSIZE;
    request.u.cc_set_size.component_id = component_id;
    request.u.cc_set_size.width = w;
    request.u.cc_set_size.height = h;
    request.u.cc_set_size.wmode = wmode;
    request.u.cc_set_size.hmode = hmode;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetHide(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int component_id, hide;

    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &hide) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    bool hidden = hide != 0;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETHIDE;
    request.u.if_set_hide.component_id = component_id;
    request.u.if_set_hide.hidden = hidden;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

/**
 * Set event handler by widget UID (IF_SETON* opcodes)
 *
 * OSRS stack layout for IF_SETON* trigger hooks (bottom to top):
 * - int stack: [scriptId, intArgs..., widgetUid]  <- UID is at TOP
 * - string stack: [stringArgs..., signature]
 *
 * The widget UID is pushed LAST (so it's at the top), then the signature.
 * We pop: UID first, then signature, then args (reverse order), then scriptId.

  // Parse trigger args (pops signature, args, scriptId, and transmit triggers if 'Y' suffix)
        const parsed = this.parseTriggerArgs();

 */
static int
CS2VMX_Op_IF_SetOnEventHandler(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    enum CS2VM_HostRequestKind kind,
    struct CS2VM_HostRequest_IF_SetOnOp* out_request)
{
    assert(vm);
    assert(frame);
    assert(out_request);

    int widget_uid, script_id, trigger_count = 0;
    int* trigger_ids = NULL;
    char* signature = NULL;

    if( CS2VMX_PopInt(vm, &widget_uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( CS2VMX_PopStr(vm, &signature) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int signature_len = signature ? (int)strlen(signature) : 0;
    int signature_parse_len = signature_len;
    if( signature_len > 0 && signature[signature_len - 1] == 'Y' )
    {
        if( CS2VMX_PopInt(vm, &trigger_count) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( trigger_count > 0 )
        {
            trigger_ids = calloc((size_t)trigger_count, sizeof(int));
            if( !trigger_ids )
                return CS2VM_EXECNO_ERROR;
            for( int i = trigger_count - 1; i >= 0; i-- )
            {
                if( CS2VMX_PopInt(vm, &trigger_ids[i]) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
        signature_parse_len = signature_len - 1;
    }

    if( signature && signature_parse_len > 0 )
    {
        for( int i = signature_parse_len - 1; i >= 0; i-- )
        {
            char c = signature[i];
            if( c == 's' || c == 'W' || c == 'X' )
            {
                char* v = NULL;
                if( CS2VMX_PopStr(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
            else
            {
                int v = 0;
                if( CS2VMX_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
    }

    if( CS2VMX_PopInt(vm, &script_id) != CS2VM_EXECNO_OK )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_ERROR;
    }

    if( script_id == -1 )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_OK;
    }

    memset(out_request, 0, sizeof(*out_request));
    out_request->component_id = widget_uid;
    out_request->script_id = script_id;
    out_request->signature = signature;
    out_request->trigger_ids = trigger_ids;
    out_request->trigger_count = trigger_count;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.u.if_set_on_op = *out_request;

    int result = vm->host_exec(vm, &request);
    free(trigger_ids);
    out_request->trigger_ids = NULL;
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

/**
 * Set event handler on active/dot child (CC_SETON* opcodes).
 *
 * OSRS stack layout (bottom to top):
 * - int stack: [scriptId, intArgs...]
 * - string stack: [stringArgs..., signature]
 *
 * Target component comes from operand (0 = active, 1 = dot), not the stack.
 */
static int
CS2VMX_Op_CC_SetOnEventHandler(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand,
    enum CS2VM_HostRequestKind kind,
    struct CS2VM_HostRequest_CC_SetOnOp* out_request)
{
    assert(vm);
    assert(frame);
    assert(out_request);
    (void)operand;

    int script_id, trigger_count = 0;
    int* trigger_ids = NULL;
    char* signature = NULL;

    if( CS2VMX_PopStr(vm, &signature) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int signature_len = signature ? (int)strlen(signature) : 0;
    int signature_parse_len = signature_len;
    if( signature_len > 0 && signature[signature_len - 1] == 'Y' )
    {
        if( CS2VMX_PopInt(vm, &trigger_count) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( trigger_count > 0 )
        {
            trigger_ids = calloc((size_t)trigger_count, sizeof(int));
            if( !trigger_ids )
                return CS2VM_EXECNO_ERROR;
            for( int i = trigger_count - 1; i >= 0; i-- )
            {
                if( CS2VMX_PopInt(vm, &trigger_ids[i]) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
        signature_parse_len = signature_len - 1;
    }

    if( signature && signature_parse_len > 0 )
    {
        for( int i = signature_parse_len - 1; i >= 0; i-- )
        {
            char c = signature[i];
            if( c == 's' || c == 'W' || c == 'X' )
            {
                char* v = NULL;
                if( CS2VMX_PopStr(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
            else
            {
                int v = 0;
                if( CS2VMX_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
    }

    if( CS2VMX_PopInt(vm, &script_id) != CS2VM_EXECNO_OK )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_ERROR;
    }

    if( script_id == -1 )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_OK;
    }

    memset(out_request, 0, sizeof(*out_request));
    out_request->script_id = script_id;
    out_request->signature = signature;
    out_request->trigger_ids = trigger_ids;
    out_request->trigger_count = trigger_count;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = kind;
    request.u.cc_set_on_op = *out_request;

    int result = vm->host_exec(vm, &request);
    free(trigger_ids);
    out_request->trigger_ids = NULL;
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_SetOnClick(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VMX_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONCLICK, &request);
}

int
CS2VMX_Op_CC_SetOnHold(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VMX_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONHOLD, &request);
}

int
CS2VMX_Op_CC_SetOnMouseOver(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VMX_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONMOUSEOVER, &request);
}

int
CS2VMX_Op_CC_SetOnMouseLeave(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VMX_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONMOUSELEAVE, &request);
}

int
CS2VMX_Op_CC_SetOnMouseRepeat(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VMX_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONMOUSEREPEAT, &request);
}

int
CS2VMX_Op_CC_SetOnDrag(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VMX_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONDRAG, &request);
}

int
CS2VMX_Op_CC_SetOnScrollWheel(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VMX_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONSCROLLWHEEL, &request);
}

int
CS2VMX_Op_CC_SetOnKey(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VMX_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONKEY, &request);
}

int
CS2VMX_Op_CC_SetOnOp(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VMX_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONOP, &request);
}

int
CS2VMX_Op_CC_SetOnDragComplete(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct CS2VM_HostRequest_CC_SetOnOp request;
    return CS2VMX_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETONDRAGCOMPLETE, &request);
}

int
CS2VMX_Op_IF_SetOnVarTransmit(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int widget_uid, script_id, trigger_count;
    int* trigger_ids = NULL;

    char* signature = NULL;

    if( CS2VMX_PopInt(vm, &widget_uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( CS2VMX_PopStr(vm, &signature) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int signature_len = signature ? (int)strlen(signature) : 0;
    int signature_parse_len = signature_len;
    if( signature_len > 0 && signature[signature_len - 1] == 'Y' )
    {
        if( CS2VMX_PopInt(vm, &trigger_count) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( trigger_count > 0 )
        {
            trigger_ids = calloc((size_t)trigger_count, sizeof(int));
            if( !trigger_ids )
                return CS2VM_EXECNO_ERROR;
            for( int i = trigger_count - 1; i >= 0; i-- )
            {
                if( CS2VMX_PopInt(vm, &trigger_ids[i]) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
        signature_parse_len = signature_len - 1;
    }

    if( signature && signature_parse_len > 0 )
    {
        for( int i = signature_parse_len - 1; i >= 0; i-- )
        {
            char c = signature[i];
            if( c == 's' || c == 'W' || c == 'X' )
            {
                char* v = NULL;
                if( CS2VMX_PopStr(vm, &v) != CS2VM_EXECNO_OK )
                    return CS2VM_EXECNO_ERROR;
                // argsArray[i] = v;
                // objectArgs.unshift(v);
            }
            else
            {
                int v = 0;
                if( CS2VMX_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                    return CS2VM_EXECNO_ERROR;
                // argsArray[i] = v;
                // intArgs.unshift(v);
            }
        }
    }

    if( CS2VMX_PopInt(vm, &script_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( script_id == -1 )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_OK;
    }

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETONVARTRANSMIT;
    request.u.if_set_on_var_transmit.component_id = widget_uid;
    request.u.if_set_on_var_transmit.script_id = script_id;
    request.u.if_set_on_var_transmit.signature = signature;
    request.u.if_set_on_var_transmit.trigger_ids = trigger_ids;
    request.u.if_set_on_var_transmit.trigger_count = trigger_count;

    int result = vm->host_exec(vm, &request);
    free(trigger_ids);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetOnInvTransmit(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int widget_uid, script_id, trigger_count = 0;
    int* trigger_ids = NULL;
    char* signature = NULL;

    if( CS2VMX_PopInt(vm, &widget_uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopStr(vm, &signature) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int signature_len = signature ? (int)strlen(signature) : 0;
    int signature_parse_len = signature_len;
    if( signature_len > 0 && signature[signature_len - 1] == 'Y' )
    {
        if( CS2VMX_PopInt(vm, &trigger_count) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( trigger_count > 0 )
        {
            trigger_ids = calloc((size_t)trigger_count, sizeof(int));
            if( !trigger_ids )
                return CS2VM_EXECNO_ERROR;
            for( int i = trigger_count - 1; i >= 0; i-- )
            {
                if( CS2VMX_PopInt(vm, &trigger_ids[i]) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
        signature_parse_len = signature_len - 1;
    }

    int int_args[16] = { 0 };
    int int_arg_count = 0;

    if( signature && signature_parse_len > 0 )
    {
        for( int i = signature_parse_len - 1; i >= 0; i-- )
        {
            char c = signature[i];
            if( c == 's' || c == 'W' || c == 'X' )
            {
                char* v = NULL;
                if( CS2VMX_PopStr(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
            else
            {
                int v = 0;
                if( CS2VMX_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
                if( i < (int)(sizeof(int_args) / sizeof(int_args[0])) )
                {
                    int_args[i] = v;
                    if( i + 1 > int_arg_count )
                        int_arg_count = i + 1;
                }
            }
        }
    }

    if( CS2VMX_PopInt(vm, &script_id) != CS2VM_EXECNO_OK )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_ERROR;
    }

    if( script_id == -1 )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_OK;
    }

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETONINVTRANSMIT;
    request.u.if_set_on_inv_transmit.component_id = widget_uid;
    request.u.if_set_on_inv_transmit.script_id = script_id;
    request.u.if_set_on_inv_transmit.signature = signature;
    request.u.if_set_on_inv_transmit.trigger_ids = trigger_ids;
    request.u.if_set_on_inv_transmit.trigger_count = trigger_count;
    memcpy(
        request.u.if_set_on_inv_transmit.int_args,
        int_args,
        sizeof(request.u.if_set_on_inv_transmit.int_args));
    request.u.if_set_on_inv_transmit.int_arg_count = int_arg_count;

    int result = vm->host_exec(vm, &request);
    free(trigger_ids);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetOnOp(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int widget_uid, script_id, trigger_count = 0;
    int* trigger_ids = NULL;
    char* signature = NULL;

    if( CS2VMX_PopInt(vm, &widget_uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( CS2VMX_PopStr(vm, &signature) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int signature_len = signature ? (int)strlen(signature) : 0;
    int signature_parse_len = signature_len;
    if( signature_len > 0 && signature[signature_len - 1] == 'Y' )
    {
        if( CS2VMX_PopInt(vm, &trigger_count) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
        if( trigger_count > 0 )
        {
            trigger_ids = calloc((size_t)trigger_count, sizeof(int));
            if( !trigger_ids )
                return CS2VM_EXECNO_ERROR;
            for( int i = trigger_count - 1; i >= 0; i-- )
            {
                if( CS2VMX_PopInt(vm, &trigger_ids[i]) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
        signature_parse_len = signature_len - 1;
    }

    if( signature && signature_parse_len > 0 )
    {
        for( int i = signature_parse_len - 1; i >= 0; i-- )
        {
            char c = signature[i];
            if( c == 's' || c == 'W' || c == 'X' )
            {
                char* v = NULL;
                if( CS2VMX_PopStr(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
            else
            {
                int v = 0;
                if( CS2VMX_PopInt(vm, &v) != CS2VM_EXECNO_OK )
                {
                    free(trigger_ids);
                    return CS2VM_EXECNO_ERROR;
                }
            }
        }
    }

    if( CS2VMX_PopInt(vm, &script_id) != CS2VM_EXECNO_OK )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_ERROR;
    }

    if( script_id == -1 )
    {
        free(trigger_ids);
        return CS2VM_EXECNO_OK;
    }

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETONOP;
    request.u.if_set_on_op.component_id = widget_uid;
    request.u.if_set_on_op.script_id = script_id;
    request.u.if_set_on_op.signature = signature;
    request.u.if_set_on_op.trigger_ids = trigger_ids;
    request.u.if_set_on_op.trigger_count = trigger_count;

    int result = vm->host_exec(vm, &request);
    free(trigger_ids);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetOnMouseOver(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VMX_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONMOUSEOVER, &request);
}

int
CS2VMX_Op_IF_SetOnMouseLeave(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VMX_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONMOUSELEAVE, &request);
}

int
CS2VMX_Op_IF_SetOnMouseRepeat(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VMX_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONMOUSEREPEAT, &request);
}

int
CS2VMX_Op_IF_SetOnTimer(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VMX_Op_IF_SetOnEventHandler(vm, frame, CS2VM_HOST_REQUEST_IF_SETONTIMER, &request);
}

int
CS2VMX_Op_IF_SetOnScrollWheel(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VMX_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONSCROLLWHEEL, &request);
}

int
CS2VMX_Op_IF_SetOnKey(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VMX_Op_IF_SetOnEventHandler(vm, frame, CS2VM_HOST_REQUEST_IF_SETONKEY, &request);
}

int
CS2VMX_Op_IF_SetOnMiscTransmit(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    struct CS2VM_HostRequest_IF_SetOnOp request;
    return CS2VMX_Op_IF_SetOnEventHandler(
        vm, frame, CS2VM_HOST_REQUEST_IF_SETONMISCTRANSMIT, &request);
}

int
CS2VMX_Op_IF_SetOp(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int widget, index;
    char* text;

    if( CS2VMX_PopInt(vm, &widget) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( CS2VMX_PopInt(vm, &index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( CS2VMX_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOP;
    request.u.if_set_op.component_id = widget;
    request.u.if_set_op.index = index;
    request.u.if_set_op.text = text;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetOpBase(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    char* text;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOPBASE;
    request.u.if_set_op_base.component_id = component_id;
    request.u.if_set_op_base.text = text;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetOpSubmenu(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id, sub_index, op_index;
    char* text;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &sub_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &op_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETOPSUBMENU;
    request.u.if_set_op_submenu.component_id = component_id;
    request.u.if_set_op_submenu.sub_index = sub_index;
    request.u.if_set_op_submenu.op_index = op_index;
    request.u.if_set_op_submenu.text = text;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_SetTargetPriority(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int priority, component_id;
    if( CS2VMX_PopInt(vm, &priority) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_SETTARGETPRIORITY;
    request.u.if_set_target_priority.component_id = component_id;
    request.u.if_set_target_priority.priority = priority;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_ClearOps(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_IF_CLEAROPS;
    request.u.if_clear_ops.component_id = component_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_Add(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    int intpop_a, intpop_b;

    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, intpop_a + intpop_b);
}

int
CS2VMX_Op_Sub(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    int intpop_a, intpop_b;

    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, intpop_a - intpop_b);
}

int
CS2VMX_Op_Mul(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    (void)vm;
    assert(vm);
    assert(frame);
    int intpop_a, intpop_b;

    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, intpop_a * intpop_b);
}

int
CS2VMX_Op_Div(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int intpop_b, intpop_a;

    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_b == 0 )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, intpop_a / intpop_b);
}

int
CS2VMX_Op_Mod(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;
    int intpop_b, intpop_a;

    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_b == 0 )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, intpop_a % intpop_b);
}

int
CS2VMX_Op_Scale(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int c, b, a;

    if( CS2VMX_PopInt(vm, &c) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int result = 0;
    if( b != 0 )
        result = (int)(((int64_t)c * (int64_t)a) / (int64_t)b);

    return CS2VMX_PushInt(vm, result);
}

int
CS2VMX_Op_Pow(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int intpop_b, intpop_a;

    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, (int)pow((double)intpop_a, (double)intpop_b));
}

int
CS2VMX_Op_PopIntDiscard(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame)
{
    assert(vm);
    int intpop;
    if( CS2VMX_PopInt(vm, &intpop) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_Enum(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int key, enum_id, output_type, input_type;

    if( CS2VMX_PopInt(vm, &key) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &enum_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &output_type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &input_type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_ENUM_LOOKUP;
    request.u.enum_lookup.input_type = input_type;
    request.u.enum_lookup.output_type = output_type;
    request.u.enum_lookup.enum_id = enum_id;
    request.u.enum_lookup.key = key;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_EnumGetOutputCount(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int enum_id;
    if( CS2VMX_PopInt(vm, &enum_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT;
    request.u.enum_get_output_count.enum_id = enum_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IsMapMembers(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // Hardcode yes

    return CS2VMX_PushInt(vm, 1);
}

int
CS2VMX_Op_OnMobile(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    return CS2VMX_PushInt(vm, 0);
}

int
CS2VMX_Op_GetCanvasSize(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    if( CS2VMX_PushInt(vm, CANVAS_W) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VMX_PushInt(vm, CANVAS_H);
}

int
CS2VMX_Op_ViewPortGetEffectiveSize(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    if( CS2VMX_PushInt(vm, CANVAS_W) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VMX_PushInt(vm, CANVAS_H);
}

int
CS2VMX_Op_ViewPortGetZoom(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    /* Matches cs2_host_ui.c defaults for fixed-layout clients. */
    if( CS2VMX_PushInt(vm, 128) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VMX_PushInt(vm, 896);
}

int
CS2VMX_Op_ViewPortGetFov(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    if( CS2VMX_PushInt(vm, 128) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VMX_PushInt(vm, 896);
}

int
CS2VMX_Op_GetWindowMode(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    /* Resizable mode (2) matches live-client semantics for gameframe scripts. */
    return CS2VMX_PushInt(vm, 2);
}

int
CS2VMX_Op_GetDefaultWindowMode(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    return CS2VMX_PushInt(vm, 2);
}

/* COORD returns the local player's packed world coordinate; it does not pop from the
 * stack. This offline renderer has no player position, so it pushes a fixed dummy coord
 * (plane 0, x 0, y 0) — good enough to keep script-local stack balance correct. */
int
CS2VMX_Op_Coord(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    return CS2VMX_PushInt(vm, 0);
}

int
CS2VMX_Op_CoordX(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int packed;
    if( CS2VMX_PopInt(vm, &packed) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VMX_PushInt(vm, (packed >> 14) & 0x3fff);
}

int
CS2VMX_Op_CoordY(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int packed;
    if( CS2VMX_PopInt(vm, &packed) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VMX_PushInt(vm, packed & 0x3fff);
}

int
CS2VMX_Op_CoordZ(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int packed;
    if( CS2VMX_PopInt(vm, &packed) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VMX_PushInt(vm, (packed >> 28) & 0x3);
}

int
CS2VMX_Op_ClientType(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    return CS2VMX_PushInt(vm, 10);
}

int
CS2VMX_Op_RunWeightVisible(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    return CS2VMX_PushInt(vm, 0);
}

// IF_ICMPGT
int
CS2VMX_Op_BranchGreaterThan(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    // const b = ctx.intStack[--ctx.intStackSize];
    // const a = ctx.intStack[--ctx.intStackSize];
    // if( a > b )
    //     return { jump: intOp };

    int intpop_b, intpop_a;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a > intpop_b )
        return CS2VMX_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_BranchLessThan(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    // const b = ctx.intStack[--ctx.intStackSize];
    // const a = ctx.intStack[--ctx.intStackSize];
    // if( a < b )
    //     return { jump: intOp };

    int intpop_b, intpop_a;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a < intpop_b )
        return CS2VMX_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_BranchLessThanOrEquals(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int intpop_b, intpop_a;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a <= intpop_b )
        return CS2VMX_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_BranchGreaterThanOrEquals(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int intpop_b, intpop_a;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a >= intpop_b )
        return CS2VMX_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_BranchEquals(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const b = ctx.intStack[--ctx.intStackSize];
    // const a = ctx.intStack[--ctx.intStackSize];
    // if( a === b )
    //     return { jump: intOp };

    int intpop_b, intpop_a;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a == intpop_b )
        return CS2VMX_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_BranchNotEquals(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const cond = ctx.intStack[--ctx.intStackSize];
    // if( cond === 0 )
    //     return { jump: intOp };

    int intpop_b, intpop_a;
    if( CS2VMX_PopInt(vm, &intpop_b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( intpop_a != intpop_b )
        return CS2VMX_JumpRelative(vm, frame, operand);

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_Branch(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    return CS2VMX_JumpRelative(vm, frame, operand);
}

int
CS2VMX_Op_Switch(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const key = this.intStack[--this.intStackSize];
    // const table = switches ? switches[intOp] : undefined;
    // if (table && table.has(key)) {
    //     pc += table.get(key)!;
    // }

    int key;
    if( CS2VMX_PopInt(vm, &key) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( operand < 0 || operand >= frame->script->switch_table_count )
        return CS2VM_EXECNO_OK;

    struct CS2_ScriptSwitch const* sw = &frame->script->switch_tables[operand];
    for( int i = 0; i < sw->case_count; i++ )
    {
        if( sw->cases[i].key == key )
            return CS2VMX_JumpRelative(vm, frame, sw->cases[i].target_pc);
    }

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_TestBit(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const bit = ctx.intStack[--ctx.intStackSize];
    // const value = ctx.intStack[--ctx.intStackSize];
    // ctx.pushInt((value & (1 << bit)) !== 0 ? 1 : 0);

    int intpop_bit, intpop_value;
    if( CS2VMX_PopInt(vm, &intpop_bit) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &intpop_value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, (intpop_value & (1 << intpop_bit)) != 0 ? 1 : 0);
}

static int
CS2VMX_ArrayDefineSlot(int operand)
{
    return operand >> 16;
}

int
CS2VMX_Op_PopVar(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int value;
    if( CS2VMX_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_PopVarbit(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int value;
    if( CS2VMX_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_DefineArray(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    int size;
    if( CS2VMX_PopInt(vm, &size) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int const slot = CS2VMX_ArrayDefineSlot(operand);
    if( slot < 0 || slot >= CS2VMX_MAX_ARRAYS )
        return CS2VM_EXECNO_OK;

    if( size < 0 )
        size = 0;
    if( size > CS2VMX_ARRAY_CAPACITY )
        size = CS2VMX_ARRAY_CAPACITY;

    vm->arrays[slot].defined = 1;
    vm->arrays[slot].size = size;
    memset(vm->arrays[slot].values, 0, sizeof(vm->arrays[slot].values));
    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_PushArrayInt(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    int index;
    if( CS2VMX_PopInt(vm, &index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int value = 0;
    if( operand >= 0 && operand < CS2VMX_MAX_ARRAYS && vm->arrays[operand].defined && index >= 0 &&
        index < vm->arrays[operand].size )
        value = vm->arrays[operand].values[index];

    return CS2VMX_PushInt(vm, value);
}

int
CS2VMX_Op_PopArrayInt(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    int index;
    int value;
    if( CS2VMX_PopInt(vm, &index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( operand >= 0 && operand < CS2VMX_MAX_ARRAYS && vm->arrays[operand].defined && index >= 0 &&
        index < vm->arrays[operand].size )
        vm->arrays[operand].values[index] = value;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_SetBit(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int bit;
    int value;
    if( CS2VMX_PopInt(vm, &bit) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, value | (1 << bit));
}

int
CS2VMX_Op_ClearBit(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int bit;
    int value;
    if( CS2VMX_PopInt(vm, &bit) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, value & ~(1 << bit));
}

int
CS2VMX_Op_Or(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int a;
    int b;
    if( CS2VMX_PopInt(vm, &a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_PushInt(vm, a | b);
}

int
CS2VMX_Op_InvPow(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int exponent;
    int base;
    if( CS2VMX_PopInt(vm, &exponent) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &base) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int result = 1;
    for( int i = 0; i < exponent; i++ )
        result *= base;
    return CS2VMX_PushInt(vm, result);
}

int
CS2VMX_Op_Random(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    return CS2VMX_PushInt(vm, rand());
}

int
CS2VMX_Op_RandomInc(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int max;
    if( CS2VMX_PopInt(vm, &max) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( max < 0 )
        return CS2VMX_PushInt(vm, 0);

    return CS2VMX_PushInt(vm, (int)(rand() % ((unsigned)(max + 1))));
}

int
CS2VMX_Op_Interpolate(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int e;
    int d;
    int c;
    int b;
    int a;
    if( CS2VMX_PopInt(vm, &e) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &d) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &c) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int denom = d - c;
    if( denom == 0 )
        return CS2VMX_PushInt(vm, a);

    int mul = (b - a) * (e - c);
    int div = mul / denom;
    return CS2VMX_PushInt(vm, a + div);
}

int
CS2VMX_Op_Compare(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    char* b;
    char* a;
    if( CS2VMX_PopStr(vm, &b) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopStr(vm, &a) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int cmp = 0;
    if( a && b )
        cmp = strcmp(a, b);
    else if( a && !b )
        cmp = 1;
    else if( !a && b )
        cmp = -1;

    if( cmp < 0 )
        return CS2VMX_PushInt(vm, -1);
    if( cmp > 0 )
        return CS2VMX_PushInt(vm, 1);
    return CS2VMX_PushInt(vm, 0);
}

int
CS2VMX_Op_Substring(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int end;
    int start;
    char* text;
    if( CS2VMX_PopInt(vm, &end) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &start) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopStr(vm, &text) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    if( !text )
        return CS2VMX_PushStr(vm, strdup(""));

    int len = (int)strlen(text);
    if( start < 0 )
        start = 0;
    if( end > len )
        end = len;
    if( start > end )
        start = end;

    int out_len = end - start;
    char* out = malloc((size_t)out_len + 1u);
    if( !out )
        return CS2VM_EXECNO_ERROR;
    if( out_len > 0 )
        memcpy(out, text + start, (size_t)out_len);
    out[out_len] = '\0';
    return CS2VMX_PushStr(vm, out);
}

int
CS2VMX_Op_StructParam(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

static int
CS2VMX_OC_GetterCost(struct RSCacheDat2A_ConfigObject* obj);

static int
CS2VMX_OC_GetterStackable(struct RSCacheDat2A_ConfigObject* obj);

static int
CS2VMX_OC_GetterMembers(struct RSCacheDat2A_ConfigObject* obj);

static int
CS2VMX_OC_GetterId(struct RSCacheDat2A_ConfigObject* obj);

int
CS2VMX_Op_CC_GetText(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

int
CS2VMX_Op_CC_FindRoot(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

int
CS2VMX_Op_CC_ChildrenFind(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

int
CS2VMX_Op_CC_ChildrenFindNextId(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

int
CS2VMX_Op_IF_ChildrenFind(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

int
CS2VMX_Op_IF_ChildrenFindNextId(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

int
CS2VMX_Op_CC_CreateChild(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

int
CS2VMX_Op_CC_CreateSibling(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

int
CS2VMX_Op_CC_GetTrans(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

int
CS2VMX_Op_IF_Find(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

int
CS2VMX_Op_IF_GetX(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

int
CS2VMX_Op_IF_GetText(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

int
CS2VMX_Op_IF_GetScrollWidth(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand);

int
CS2VMX_Op_OC_IntParam(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand,
    int (*getter)(struct RSCacheDat2A_ConfigObject* obj));

static int
CS2VMX_Op_IF_SetOnEventDiscard(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    (void)operand;
    struct CS2VM_HostRequest_IF_SetOnOp req;
    return CS2VMX_Op_IF_SetOnEventHandler(vm, frame, CS2VM_HOST_REQUEST_IF_SETON_DISCARD, &req);
}

static int
CS2VMX_Op_CC_SetOnEventDiscard(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    struct CS2VM_HostRequest_CC_SetOnOp req;
    return CS2VMX_Op_CC_SetOnEventHandler(
        vm, frame, operand, CS2VM_HOST_REQUEST_CC_SETON_DISCARD, &req);
}

static int
CS2VMX_Op_StackMetaStub(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int opcode,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    if( opcode < 0 || opcode >= INTERFACEX_OPCODE_STACK_MAX )
        return CS2VM_EXECNO_OK;

    struct InterfacexOpcodeStack const meta = g_interfacex_opcode_stack[opcode];
    for( int i = 0; i < meta.int_in; i++ )
    {
        int discard;
        if( CS2VMX_PopInt(vm, &discard) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    for( int i = 0; i < meta.str_in; i++ )
    {
        char* discard;
        if( CS2VMX_PopStr(vm, &discard) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    for( int i = 0; i < meta.int_out; i++ )
    {
        if( CS2VMX_PushInt(vm, 0) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    for( int i = 0; i < meta.str_out; i++ )
    {
        if( CS2VMX_PushStr(vm, strdup("")) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }
    return CS2VM_EXECNO_OK;
}

// OC is object config
int
CS2VMX_Op_OC_Param(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // handlers.set(Opcodes.OC_PARAM, (ctx) => {
    //     const paramId = ctx.intStack[--ctx.intStackSize];
    //     const itemId = ctx.intStack[--ctx.intStackSize];
    //     const param = ctx.paramTypeLoader?.load(paramId);
    //     const obj = ctx.objTypeLoader?.load(itemId);
    //     if (param && obj && obj.params) {
    //         const val = obj.params.get(paramId);
    //         if (param.isString()) {
    //             ctx.pushString(typeof val === "string" ? val : param.defaultString || "");
    //         } else {
    //             ctx.pushInt(typeof val === "number" ? val : param.defaultInt || 0);
    //         }
    //     } else {
    //         if (param?.isString()) {
    //             ctx.pushString(param.defaultString || "");
    //         } else {
    //             ctx.pushInt(param?.defaultInt ?? 0);
    //         }
    //     }

    int param_id, item_id;
    if( CS2VMX_PopInt(vm, &param_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_PARAM;
    request.u.oc_param.param_id = param_id;
    request.u.oc_param.item_id = item_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_OC_Name(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int item_id;
    if( CS2VMX_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_NAME;
    request.u.oc_name.item_id = item_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_OC_Unplaceholder(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int item_id;
    if( CS2VMX_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER;
    request.u.oc_unplaceholder.item_id = item_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_Return(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame)
{
    (void)vm;
    assert(vm);
    assert(frame);
    return CS2VMX_PopFrame(vm);
}

int
CS2VMX_Op_InvSize(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int inv_id;

    if( CS2VMX_PopInt(vm, &inv_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_INVS_GET_SIZE;
    request.u.invs_get_size.inv_id = inv_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_InvGetObj(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    // const invId = ctx.intStack[--ctx.intStackSize];
    // const slot = ctx.intStack[--ctx.intStackSize];
    // const obj = ctx.invs.get(invId)?.get(slot);
    // ctx.pushInt(obj ?? -1);

    int inv_id, slot;
    if( CS2VMX_PopInt(vm, &slot) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &inv_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_INVS_GET_OBJ;
    request.u.invs_get_obj.inv_id = inv_id;
    request.u.invs_get_obj.slot = slot;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_InvGetNum(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int inv_id, slot;
    if( CS2VMX_PopInt(vm, &slot) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &inv_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_INVS_GET_NUM;
    request.u.invs_get_num.inv_id = inv_id;
    request.u.invs_get_num.slot = slot;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_InvTotal(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int inv_id, item_id;
    if( CS2VMX_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &inv_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_INVS_GET_TOTAL;
    request.u.invs_get_total.inv_id = inv_id;
    request.u.invs_get_total.item_id = item_id;

    int result = vm->host_exec(vm, &request);
    if( result != CS2VM_EXECNO_OK )
        return result;

    return CS2VM_EXECNO_OK;
}

static int
CS2VMX_DispatchWidgetSetInt(
    struct CS2VMX* vm,
    int component_id,
    enum CS2VM_WidgetIntField field,
    int value)
{
    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_INT;
    request.u.widget_set_int.component_id = component_id;
    request.u.widget_set_int.field = field;
    request.u.widget_set_int.value = value;
    return vm->host_exec(vm, &request);
}

static int
CS2VMX_Op_CC_WidgetInt(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand,
    enum CS2VM_WidgetIntField field)
{
    assert(frame);
    (void)frame;

    int value;
    if( CS2VMX_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int result =
        CS2VMX_DispatchWidgetSetInt(vm, CS2VMX_DotOrActiveComponentId(vm, operand), field, value);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VMX_Op_IF_WidgetInt(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand,
    enum CS2VM_WidgetIntField field)
{
    assert(frame);
    (void)frame;
    (void)operand;

    int component_id;
    int value;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int result = CS2VMX_DispatchWidgetSetInt(vm, component_id, field, value);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VMX_Op_CC_SetScrollPos(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;

    int scroll_y;
    int scroll_x;
    if( CS2VMX_PopInt(vm, &scroll_y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &scroll_x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETSCROLLPOS;
    request.u.cc_set_scroll_pos.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_scroll_pos.scroll_x = scroll_x;
    request.u.cc_set_scroll_pos.scroll_y = scroll_y;

    int result = vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VMX_Op_CC_SetScrollSize(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;

    int scroll_height;
    int scroll_width;
    if( CS2VMX_PopInt(vm, &scroll_height) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &scroll_width) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_SETSCROLLSIZE;
    request.u.cc_set_scroll_size.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.cc_set_scroll_size.scroll_width = scroll_width;
    request.u.cc_set_scroll_size.scroll_height = scroll_height;

    int result = vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VMX_Op_CC_SetModel(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;

    int model_id;
    if( CS2VMX_PopInt(vm, &model_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL;
    request.u.widget_set_model.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.widget_set_model.model_id = model_id;

    int result = vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VMX_Op_IF_SetModel(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;
    (void)operand;

    int component_id;
    int model_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &model_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL;
    request.u.widget_set_model.component_id = component_id;
    request.u.widget_set_model.model_id = model_id;

    int result = vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VMX_Op_CC_SetModelAngle(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;

    int zoom;
    int angle_z;
    int angle_y;
    int angle_x;
    int offset_y;
    int offset_x;
    if( CS2VMX_PopInt(vm, &zoom) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &angle_z) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &angle_y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &angle_x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &offset_y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &offset_x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_ANGLE;
    request.u.widget_set_model_angle.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.widget_set_model_angle.offset_x = offset_x;
    request.u.widget_set_model_angle.offset_y = offset_y;
    request.u.widget_set_model_angle.angle_x = angle_x;
    request.u.widget_set_model_angle.angle_y = angle_y;
    request.u.widget_set_model_angle.angle_z = angle_z;
    request.u.widget_set_model_angle.zoom = zoom;

    int result = vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VMX_Op_IF_SetModelAngle(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;
    (void)operand;

    int component_id;
    int zoom;
    int angle_z;
    int angle_y;
    int angle_x;
    int offset_y;
    int offset_x;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &zoom) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &angle_z) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &angle_y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &angle_x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &offset_y) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &offset_x) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_ANGLE;
    request.u.widget_set_model_angle.component_id = component_id;
    request.u.widget_set_model_angle.offset_x = offset_x;
    request.u.widget_set_model_angle.offset_y = offset_y;
    request.u.widget_set_model_angle.angle_x = angle_x;
    request.u.widget_set_model_angle.angle_y = angle_y;
    request.u.widget_set_model_angle.angle_z = angle_z;
    request.u.widget_set_model_angle.zoom = zoom;

    int result = vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VMX_Op_CC_SetArc(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;

    int arc_end;
    int arc_start;
    if( CS2VMX_PopInt(vm, &arc_end) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &arc_start) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_ARC;
    request.u.widget_set_arc.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.widget_set_arc.arc_start = arc_start;
    request.u.widget_set_arc.arc_end = arc_end;

    int result = vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VMX_Op_IF_SetArc(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(frame);
    (void)frame;
    (void)operand;

    int component_id;
    int arc_end;
    int arc_start;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &arc_end) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &arc_start) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_ARC;
    request.u.widget_set_arc.component_id = component_id;
    request.u.widget_set_arc.arc_start = arc_start;
    request.u.widget_set_arc.arc_end = arc_end;

    int result = vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VMX_Op_CC_SetModelKind(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand,
    enum InterfaceX_ModelKind model_kind,
    bool has_model_id)
{
    assert(frame);
    (void)frame;

    int model_id = -1;
    if( has_model_id )
    {
        if( CS2VMX_PopInt(vm, &model_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND;
    request.u.widget_set_model_kind.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.widget_set_model_kind.model_kind = model_kind;
    request.u.widget_set_model_kind.model_id = model_id;

    int result = vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VMX_Op_IF_SetModelKind(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand,
    enum InterfaceX_ModelKind model_kind,
    bool has_model_id)
{
    assert(frame);
    (void)frame;
    (void)operand;

    int component_id;
    int model_id = -1;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( has_model_id )
    {
        if( CS2VMX_PopInt(vm, &model_id) != CS2VM_EXECNO_OK )
            return CS2VM_EXECNO_ERROR;
    }

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND;
    request.u.widget_set_model_kind.component_id = component_id;
    request.u.widget_set_model_kind.model_kind = model_kind;
    request.u.widget_set_model_kind.model_id = model_id;

    int result = vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

static int
CS2VMX_Op_CC_InputInt(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand,
    enum CS2VM_WidgetInputField field)
{
    assert(frame);
    (void)frame;

    int value;
    if( CS2VMX_PopInt(vm, &value) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_WIDGET_INPUT_INT;
    request.u.widget_input_int.component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    request.u.widget_input_int.field = field;
    request.u.widget_input_int.value = value;

    int result = vm->host_exec(vm, &request);
    return result == CS2VM_EXECNO_OK ? CS2VM_EXECNO_OK : result;
}

/* Fills *int_args / *str_args with the stack values this opcode pops.
 * Returns 0 for a fixed count, 1 when the count is variable (e.g. GOSUB). */
static int
CS2VMX_OpArgCounts(
    int opcode,
    int* int_args,
    int* str_args);

int
CS2VMX_RunOp(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int opcode,
    int operand,
    char const* str_operand)
{
    assert(vm);
    assert(frame);
    int intpop_a;
    int intpop_b;
    char* strpop_a;
    char* strpop_b;

    switch( opcode )
    {
    case CS2_OP_PUSH_VAR:
        return CS2VMX_Op_PushVar(vm, frame, operand);
    case CS2_OP_POP_VAR:
        return CS2VMX_Op_PopVar(vm, frame, operand);
    case CS2_OP_PUSH_VARBIT:
        return CS2VMX_Op_PushVarbit(vm, frame, operand);
    case CS2_OP_POP_VARBIT:
        return CS2VMX_Op_PopVarbit(vm, frame, operand);
    case CS2_OP_PUSH_VARC_INT:
        return CS2VMX_Op_PushVarcInt(vm, frame, operand);
    case CS2_OP_POP_VARC_INT:
        return CS2VMX_Op_PopVarcInt(vm, frame, operand);
    case CS2_OP_PUSH_VARC_STRING:
        return CS2VMX_Op_PushVarcString(vm, frame, operand);
    case CS2_OP_POP_VARC_STRING:
        return CS2VMX_Op_PopVarcString(vm, frame, operand);
    case CS2_OP_PUSH_CONSTANT_STRING:
        return CS2VMX_Op_PushConstantString(vm, frame, str_operand);
    case CS2_OP_PUSH_CONSTANT_INT:
        return CS2VMX_Op_PushConstantInt(vm, frame, operand);
    case CS2_OP_PUSH_INT_LOCAL:
        return CS2VMX_Op_PushIntLocal(vm, frame, operand);
    case CS2_OP_PUSH_STRING_LOCAL:
        return CS2VMX_Op_PushStrLocal(vm, frame, operand);
    case CS2_OP_POP_INT_LOCAL:
        return CS2VMX_Op_PopIntLocal(vm, frame, operand);
    case CS2_OP_POP_STRING_LOCAL:
        return CS2VMX_Op_PopStrLocal(vm, frame, operand);
    case CS2_OP_JOIN_STRING:
        return CS2VMX_Op_JoinString(vm, frame, operand);
    case CS2_OP_STRING_LENGTH:
        return CS2VMX_Op_StringLength(vm, frame, operand);
    case CS2_OP_PARAHEIGHT:
        return CS2VMX_Op_ParaHeight(vm, frame, operand);
    case CS2_OP_PARAWIDTH:
        return CS2VMX_Op_ParaWidth(vm, frame, operand);
    case CS2_OP_TOSTRING:
        return CS2VMX_Op_ToString(vm, frame, operand);
    case CS2_OP_GOSUB_WITH_PARAMS:
        return CS2VMX_Op_GosubWithParams(vm, frame, operand);
    case CS2_OP_POP_INT_DISCARD:
        return CS2VMX_Op_PopIntDiscard(vm, frame);
    case CS2_OP_ENUM:
        return CS2VMX_Op_Enum(vm, frame, operand);
    case CS2_OP_ENUM_GETOUTPUTCOUNT:
        return CS2VMX_Op_EnumGetOutputCount(vm, frame, operand);
    case CS2_OP_MAP_MEMBERS:
        return CS2VMX_Op_IsMapMembers(vm, frame, operand);
    case CS2_OP_ON_MOBILE:
        return CS2VMX_Op_OnMobile(vm, frame, operand);
    case CS2_OP_GETCANVASSIZE:
        return CS2VMX_Op_GetCanvasSize(vm, frame, operand);
    case CS2_OP_VIEWPORT_GETEFFECTIVESIZE:
        return CS2VMX_Op_ViewPortGetEffectiveSize(vm, frame, operand);
    case CS2_OP_VIEWPORT_GETZOOM:
        return CS2VMX_Op_ViewPortGetZoom(vm, frame, operand);
    case CS2_OP_VIEWPORT_GETFOV:
        return CS2VMX_Op_ViewPortGetFov(vm, frame, operand);
    case CS2_OP_GETWINDOWMODE:
        return CS2VMX_Op_GetWindowMode(vm, frame, operand);
    case CS2_OP_GETDEFAULTWINDOWMODE:
        return CS2VMX_Op_GetDefaultWindowMode(vm, frame, operand);
    case CS2_OP_CLIENTTYPE:
        return CS2VMX_Op_ClientType(vm, frame, operand);
    case CS2_OP_COORD:
        return CS2VMX_Op_Coord(vm, frame, operand);
    case CS2_OP_COORDX:
        return CS2VMX_Op_CoordX(vm, frame, operand);
    case CS2_OP_COORDY:
        return CS2VMX_Op_CoordY(vm, frame, operand);
    case CS2_OP_COORDZ:
        return CS2VMX_Op_CoordZ(vm, frame, operand);
    case CS2_OP_RUNWEIGHT_VISIBLE:
        return CS2VMX_Op_RunWeightVisible(vm, frame, operand);
    case CS2_OP_INV_SIZE:
        return CS2VMX_Op_InvSize(vm, frame, operand);
    case CS2_OP_INV_GETOBJ:
        return CS2VMX_Op_InvGetObj(vm, frame, operand);
    case CS2_OP_INV_GETNUM:
        return CS2VMX_Op_InvGetNum(vm, frame, operand);
    case CS2_OP_INV_TOTAL:
        return CS2VMX_Op_InvTotal(vm, frame, operand);
    case CS2_OP_CC_DELETEALL:
        return CS2VMX_Op_CC_DeleteAll(vm, frame);
    case CS2_OP_CC_CREATE:
        return CS2VMX_Op_CC_Create(vm, frame, operand);
    case CS2_OP_CC_FIND:
        return CS2VMX_Op_CC_Find(vm, frame, operand);
    case CS2_OP_CC_CREATECHILD:
        return CS2VMX_Op_CC_CreateChild(vm, frame, operand);
    case CS2_OP_CC_CREATESIBLING:
        return CS2VMX_Op_CC_CreateSibling(vm, frame, operand);
    case CS2_OP_CC_FINDROOT:
        return CS2VMX_Op_CC_FindRoot(vm, frame, operand);
    case CS2_OP_CC_CHILDREN_FIND:
        return CS2VMX_Op_CC_ChildrenFind(vm, frame, operand);
    case CS2_OP_CC_CHILDREN_FINDNEXTID:
        return CS2VMX_Op_CC_ChildrenFindNextId(vm, frame, operand);
    case CS2_OP_IF_CHILDREN_FIND:
        return CS2VMX_Op_IF_ChildrenFind(vm, frame, operand);
    case CS2_OP_IF_CHILDREN_FINDNEXTID:
        return CS2VMX_Op_IF_ChildrenFindNextId(vm, frame, operand);
    case CS2_OP_CC_SETPOSITION:
        return CS2VMX_Op_CC_SetPosition(vm, frame, operand);
    case CS2_OP_CC_SETSIZE:
        return CS2VMX_Op_CC_SetSize(vm, frame, operand);
    case CS2_OP_CC_SETGRAPHIC:
        return CS2VMX_Op_CC_SetGraphic(vm, frame, operand);
    case CS2_OP_CC_SETTILING:
        return CS2VMX_Op_CC_SetTiling(vm, frame, operand);
    case CS2_OP_IF_SETTILING:
        return CS2VMX_Op_IF_SetTiling(vm, frame, operand);
    case CS2_OP_CC_SETOUTLINE:
        return CS2VMX_Op_CC_SetOutline(vm, frame, operand);
    case CS2_OP_CC_SETGRAPHICSHADOW:
        return CS2VMX_Op_CC_SetGraphicShadow(vm, frame, operand);
    case CS2_OP_IF_SETGRAPHICSHADOW:
        return CS2VMX_Op_IF_SetGraphicShadow(vm, frame, operand);
    case CS2_OP_CC_SETCOLOUR:
        return CS2VMX_Op_CC_SetColour(vm, frame, operand);
    case CS2_OP_IF_SETCOLOUR:
        return CS2VMX_Op_IF_SetColour(vm, frame, operand);
    case CS2_OP_CC_SETFILL:
        return CS2VMX_Op_CC_SetFill(vm, frame, operand);
    case CS2_OP_IF_SETFILL:
        return CS2VMX_Op_IF_SetFill(vm, frame, operand);
    case CS2_OP_CC_SETTRANS:
        return CS2VMX_Op_CC_SetTrans(vm, frame, operand);
    case CS2_OP_IF_SETTRANS:
        return CS2VMX_Op_IF_SetTrans(vm, frame, operand);
    case CS2_OP_CC_SETNOCLICKTHROUGH:
        return CS2VMX_Op_CC_SetNoClickThrough(vm, frame, operand);
    case CS2_OP_CC_SETTEXT:
        return CS2VMX_Op_CC_SetText(vm, frame, operand);
    case CS2_OP_CC_SETTEXTFONT:
        return CS2VMX_Op_CC_SetTextFont(vm, frame, operand);
    case CS2_OP_IF_SETTEXTFONT:
        return CS2VMX_Op_IF_SetTextFont(vm, frame, operand);
    case CS2_OP_CC_SETTEXTALIGN:
        return CS2VMX_Op_CC_SetTextAlign(vm, frame, operand);
    case CS2_OP_IF_SETTEXTALIGN:
        return CS2VMX_Op_IF_SetTextAlign(vm, frame, operand);
    case CS2_OP_CC_SETTEXTSHADOW:
        return CS2VMX_Op_CC_SetTextShadow(vm, frame, operand);
    case CS2_OP_IF_SETTEXTSHADOW:
        return CS2VMX_Op_IF_SetTextShadow(vm, frame, operand);
    case CS2_OP_CC_SETDRAGGABLE:
        return CS2VMX_Op_CC_SetDraggable(vm, frame, operand);
    case CS2_OP_CC_SETDRAGGABLEBEHAVIOR:
        return CS2VMX_Op_CC_SetDraggableBehavior(vm, frame, operand);
    case CS2_OP_CC_SETDRAGDEADZONE:
        return CS2VMX_Op_CC_SetDragDeadZone(vm, frame, operand);
    case CS2_OP_CC_SETDRAGDEADTIME:
        return CS2VMX_Op_CC_SetDragDeadTime(vm, frame, operand);
    case CS2_OP_CC_SETOBJECT:
    case CS2_OP_CC_SETOBJECT_ALWAYS_NUM:
    case CS2_OP_CC_SETOBJECT_NONUM:
        return CS2VMX_Op_CC_SetObject(vm, frame, operand);
    case CS2_OP_CC_SETOP:
        return CS2VMX_Op_CC_SetOp(vm, frame, operand);
    case CS2_OP_CC_SETOPBASE:
        return CS2VMX_Op_CC_SetOpBase(vm, frame, operand);
    case CS2_OP_CC_CLEAROPS:
        return CS2VMX_Op_CC_ClearOps(vm, frame, operand);
    case CS2_OP_CC_SETHIDE:
        return CS2VMX_Op_CC_SetHide(vm, frame, operand);
    case CS2_OP_CC_GETID:
        return CS2VMX_Op_CC_GetId(vm, frame, operand);
    case CS2_OP_CC_GETX:
        return CS2VMX_Op_CC_GetX(vm, frame, operand);
    case CS2_OP_CC_GETY:
        return CS2VMX_Op_CC_GetY(vm, frame, operand);
    case CS2_OP_CC_GETWIDTH:
        return CS2VMX_Op_CC_GetWidth(vm, frame, operand);
    case CS2_OP_CC_GETHEIGHT:
        return CS2VMX_Op_CC_GetHeight(vm, frame, operand);
    case CS2_OP_CC_GETHIDE:
        return CS2VMX_Op_CC_GetHide(vm, frame, operand);
    case CS2_OP_CC_SETONCLICK:
        return CS2VMX_Op_CC_SetOnClick(vm, frame, operand);
    case CS2_OP_CC_SETONHOLD:
        return CS2VMX_Op_CC_SetOnHold(vm, frame, operand);
    case CS2_OP_CC_SETONMOUSEOVER:
        return CS2VMX_Op_CC_SetOnMouseOver(vm, frame, operand);
    case CS2_OP_CC_SETONMOUSELEAVE:
        return CS2VMX_Op_CC_SetOnMouseLeave(vm, frame, operand);
    case CS2_OP_CC_SETONMOUSEREPEAT:
        return CS2VMX_Op_CC_SetOnMouseRepeat(vm, frame, operand);
    case CS2_OP_CC_SETONDRAG:
        return CS2VMX_Op_CC_SetOnDrag(vm, frame, operand);
    case CS2_OP_CC_SETONSCROLLWHEEL:
        return CS2VMX_Op_CC_SetOnScrollWheel(vm, frame, operand);
    case CS2_OP_CC_SETONKEY:
        return CS2VMX_Op_CC_SetOnKey(vm, frame, operand);
    case CS2_OP_CC_SETONOP:
        return CS2VMX_Op_CC_SetOnOp(vm, frame, operand);
    case CS2_OP_CC_SETONDRAGCOMPLETE:
        return CS2VMX_Op_CC_SetOnDragComplete(vm, frame, operand);
    case CS2_OP_IF_GETWIDTH:
        return CS2VMX_Op_IF_GetWidth(vm, frame, operand);
    case CS2_OP_IF_GETHEIGHT:
        return CS2VMX_Op_IF_GetHeight(vm, frame, operand);
    case CS2_OP_IF_GETY:
        return CS2VMX_Op_IF_GetY(vm, frame, operand);
    case CS2_OP_IF_GETLAYER:
        return CS2VMX_Op_IF_GetLayer(vm, frame, operand);
    case CS2_OP_IF_GETTOP:
        return CS2VMX_Op_IF_GetTop(vm, frame, operand);
    case CS2_OP_IF_GETSCROLLX:
        return CS2VMX_Op_IF_GetScrollX(vm, frame, operand);
    case CS2_OP_IF_GETSCROLLY:
        return CS2VMX_Op_IF_GetScrollY(vm, frame, operand);
    case CS2_OP_IF_GETSCROLLHEIGHT:
        return CS2VMX_Op_IF_GetScrollHeight(vm, frame, operand);
    case CS2_OP_IF_GETHIDE:
        return CS2VMX_Op_IF_GetHide(vm, frame, operand);
    case CS2_OP_IF_SETHIDE:
        return CS2VMX_Op_IF_SetHide(vm, frame, operand);
    case CS2_OP_IF_SETPOSITION:
        return CS2VMX_Op_IF_SetPosition(vm, frame, operand);
    case CS2_OP_IF_SETSIZE:
        return CS2VMX_Op_IF_SetSize(vm, frame, operand);
    case CS2_OP_IF_SETSCROLLPOS:
        return CS2VMX_Op_IF_SetScrollPos(vm, frame, operand);
    case CS2_OP_IF_SETSCROLLSIZE:
        return CS2VMX_Op_IF_SetScrollSize(vm, frame, operand);
    case CS2_OP_IF_SETGRAPHIC:
        return CS2VMX_Op_IF_SetGraphic(vm, frame, operand);
    case CS2_OP_IF_SETTEXT:
        return CS2VMX_Op_IF_SetText(vm, frame, operand);
    case CS2_OP_IF_SETOUTLINE:
        return CS2VMX_Op_IF_SetOutline(vm, frame, operand);
    case CS2_OP_IF_SETONVARTRANSMIT:
        return CS2VMX_Op_IF_SetOnVarTransmit(vm, frame, operand);
    case CS2_OP_IF_SETONINVTRANSMIT:
        return CS2VMX_Op_IF_SetOnInvTransmit(vm, frame, operand);
    case CS2_OP_IF_SETONOP:
        return CS2VMX_Op_IF_SetOnOp(vm, frame, operand);
    case CS2_OP_IF_SETONMOUSEOVER:
        return CS2VMX_Op_IF_SetOnMouseOver(vm, frame, operand);
    case CS2_OP_IF_SETONMOUSELEAVE:
        return CS2VMX_Op_IF_SetOnMouseLeave(vm, frame, operand);
    case CS2_OP_IF_SETONMOUSEREPEAT:
        return CS2VMX_Op_IF_SetOnMouseRepeat(vm, frame, operand);
    case CS2_OP_IF_SETONTIMER:
        return CS2VMX_Op_IF_SetOnTimer(vm, frame, operand);
    case CS2_OP_IF_SETONSCROLLWHEEL:
        return CS2VMX_Op_IF_SetOnScrollWheel(vm, frame, operand);
    case CS2_OP_IF_SETONKEY:
        return CS2VMX_Op_IF_SetOnKey(vm, frame, operand);
    case CS2_OP_IF_SETONMISCTRANSMIT:
        return CS2VMX_Op_IF_SetOnMiscTransmit(vm, frame, operand);
    case CS2_OP_IF_SETOP:
        return CS2VMX_Op_IF_SetOp(vm, frame, operand);
    case CS2_OP_IF_SETOPBASE:
        return CS2VMX_Op_IF_SetOpBase(vm, frame, operand);
    case CS2_OP_IF_SETOPSUBMENU:
        return CS2VMX_Op_IF_SetOpSubmenu(vm, frame, operand);
    case CS2_OP_IF_SETTARGETPRIORITY:
        return CS2VMX_Op_IF_SetTargetPriority(vm, frame, operand);
    case CS2_OP_IF_CLEAROPS:
        return CS2VMX_Op_IF_ClearOps(vm, frame, operand);
    case CS2_OP_IF_SETOBJECT:
    case CS2_OP_IF_SETOBJECT_ALWAYS_NUM:
    case CS2_OP_IF_SETOBJECT_NONUM:
        return CS2VMX_Op_IF_SetObject(vm, frame, operand);
    case CS2_OP_BRANCH_LESS_THAN:
        return CS2VMX_Op_BranchLessThan(vm, frame, operand);
    case CS2_OP_BRANCH_GREATER_THAN:
        return CS2VMX_Op_BranchGreaterThan(vm, frame, operand);
    case CS2_OP_BRANCH_LESS_THAN_OR_EQUALS:
        return CS2VMX_Op_BranchLessThanOrEquals(vm, frame, operand);
    case CS2_OP_BRANCH_GREATER_THAN_OR_EQUALS:
        return CS2VMX_Op_BranchGreaterThanOrEquals(vm, frame, operand);
    case CS2_OP_BRANCH_EQUALS:
        return CS2VMX_Op_BranchEquals(vm, frame, operand);
    case CS2_OP_BRANCH_NOT:
        return CS2VMX_Op_BranchNotEquals(vm, frame, operand);
    case CS2_OP_BRANCH:
        return CS2VMX_Op_Branch(vm, frame, operand);
    case CS2_OP_SWITCH:
        return CS2VMX_Op_Switch(vm, frame, operand);
    case CS2_OP_RETURN:
        return CS2VMX_Op_Return(vm, frame);
    case CS2_OP_ADD:
        return CS2VMX_Op_Add(vm, frame, operand);
    case CS2_OP_SUB:
        return CS2VMX_Op_Sub(vm, frame, operand);
    case CS2_OP_MULTIPLY:
        return CS2VMX_Op_Mul(vm, frame, operand);
    case CS2_OP_DIV:
        return CS2VMX_Op_Div(vm, frame, operand);
    case CS2_OP_MOD:
        return CS2VMX_Op_Mod(vm, frame, operand);
    case CS2_OP_POW:
        return CS2VMX_Op_Pow(vm, frame, operand);
    case CS2_OP_SCALE:
        return CS2VMX_Op_Scale(vm, frame, operand);
    case CS2_OP_TESTBIT:
        return CS2VMX_Op_TestBit(vm, frame, operand);
    case CS2_OP_OC_PARAM:
        return CS2VMX_Op_OC_Param(vm, frame, operand);
    case CS2_OP_OC_NAME:
        return CS2VMX_Op_OC_Name(vm, frame, operand);
    case CS2_OP_OC_UNPLACEHOLDER:
        return CS2VMX_Op_OC_Unplaceholder(vm, frame, operand);
    case CS2_OP_CC_SETSCROLLPOS:
        return CS2VMX_Op_CC_SetScrollPos(vm, frame, operand);
    case CS2_OP_CC_SETSCROLLSIZE:
        return CS2VMX_Op_CC_SetScrollSize(vm, frame, operand);
    case CS2_OP_CC_SETMODEL:
        return CS2VMX_Op_CC_SetModel(vm, frame, operand);
    case CS2_OP_CC_SETMODELANGLE:
        return CS2VMX_Op_CC_SetModelAngle(vm, frame, operand);
    case CS2_OP_CC_SETMODELANIM:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_MODEL_ANIM);
    case CS2_OP_CC_SETMODELORTHOG:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_MODEL_ORTHOG);
    case CS2_OP_CC_SETMODELTRANSPARENT:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_MODEL_TRANSPARENT);
    case CS2_OP_CC_SETHFLIP:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_HFLIP);
    case CS2_OP_CC_SETVFLIP:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_VFLIP);
    case CS2_OP_CC_SET2DANGLE:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_ANGLE_2D);
    case CS2_OP_CC_SETFILLCOLOUR:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_FILL_COLOUR);
    case CS2_OP_CC_SETLINEWID:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_LINE_WIDTH);
    case CS2_OP_CC_SETLINEDIRECTION:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_LINE_DIRECTION);
    case CS2_OP_CC_SETGRAPHIC2:
        return CS2VMX_Op_CC_SetGraphic(vm, frame, operand);
    case CS2_OP_CC_SETTRANSBOT:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_TRANS_BOT);
    case CS2_OP_CC_SETFILLMODE:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_FILL_MODE);
    case CS2_OP_CC_SETARC:
        return CS2VMX_Op_CC_SetArc(vm, frame, operand);
    case CS2_OP_CC_SETNOSCROLLTHROUGH:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_NO_SCROLL_THROUGH);
    case CS2_OP_CC_SETPINCH:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_PINCH);
    case CS2_OP_CC_SETNPCHEAD:
        return CS2VMX_Op_CC_SetModelKind(vm, frame, operand, INTERFACEX_MODEL_KIND_NPC_HEAD, true);
    case CS2_OP_CC_SETPLAYERHEAD_SELF:
        return CS2VMX_Op_CC_SetModelKind(
            vm, frame, operand, INTERFACEX_MODEL_KIND_PLAYER_SELF, false);
    case CS2_OP_CC_SETPLAYERMODEL_SELF:
        return CS2VMX_Op_CC_SetModelKind(
            vm, frame, operand, INTERFACEX_MODEL_KIND_PLAYER_SELF, true);
    case CS2_OP_CC_SETMODEL_PLAYERCHATHEAD:
        return CS2VMX_Op_CC_SetModelKind(
            vm, frame, operand, INTERFACEX_MODEL_KIND_PLAYER_CHATHEAD, true);
    case CS2_OP_CC_RESUME_PAUSEBUTTON:
        return CS2VMX_Op_CC_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_RESUME_PAUSEBUTTON);
    case CS2_OP_CC_INPUT_SETSUBMITMODE:
        return CS2VMX_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_SUBMITMODE);
    case CS2_OP_CC_INPUT_SETSELECTCOLOUR:
        return CS2VMX_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_SELECTCOLOUR);
    case CS2_OP_CC_INPUT_SETWRAPMODE:
        return CS2VMX_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_WRAPMODE);
    case CS2_OP_CC_INPUT_SETLINEWRAPPINGWIDTH:
        return CS2VMX_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_LINEWRAPPINGWIDTH);
    case CS2_OP_CC_INPUT_SETSELECTBGCOLOUR:
        return CS2VMX_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_SELECTBGCOLOUR);
    case CS2_OP_CC_INPUT_SETLINECOUNTLIMIT:
        return CS2VMX_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_LINECOUNTLIMIT);
    case CS2_OP_CC_INPUT_SETCURSORCOLOUR:
        return CS2VMX_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_CURSORCOLOUR);
    case CS2_OP_CC_INPUT_SETCURSORTRANS:
        return CS2VMX_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_CURSORTRANS);
    case CS2_OP_CC_INPUT_SETCURSORWIDTH:
        return CS2VMX_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_CURSORWIDTH);
    case CS2_OP_CC_INPUT_SETCURSORHEIGHT:
        return CS2VMX_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_CURSORHEIGHT);
    case CS2_OP_CC_INPUT_SETCURSOROFFSET:
        return CS2VMX_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_CURSOROFFSET);
    case CS2_OP_CC_INPUT_SETLINEWIDTHLIMIT:
        return CS2VMX_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_LINEWIDTHLIMIT);
    case CS2_OP_CC_INPUT_SETCHARFILTER:
        return CS2VMX_Op_CC_InputInt(vm, frame, operand, CS2VM_WIDGET_INPUT_CHARFILTER);
    case CS2_OP_IF_SETMODEL:
        return CS2VMX_Op_IF_SetModel(vm, frame, operand);
    case CS2_OP_IF_SETMODELANGLE:
        return CS2VMX_Op_IF_SetModelAngle(vm, frame, operand);
    case CS2_OP_IF_SETMODELANIM:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_MODEL_ANIM);
    case CS2_OP_IF_SETMODELORTHOG:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_MODEL_ORTHOG);
    case CS2_OP_IF_SETMODELTRANSPARENT:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_MODEL_TRANSPARENT);
    case CS2_OP_IF_SETHFLIP:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_HFLIP);
    case CS2_OP_IF_SETVFLIP:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_VFLIP);
    case CS2_OP_IF_SET2DANGLE:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_ANGLE_2D);
    case CS2_OP_IF_SETFILLCOLOUR:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_FILL_COLOUR);
    case CS2_OP_IF_SETLINEWID:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_LINE_WIDTH);
    case CS2_OP_IF_SETLINEDIRECTION:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_LINE_DIRECTION);
    case CS2_OP_IF_SETTRANSBOT:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_TRANS_BOT);
    case CS2_OP_IF_SETFILLMODE:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_FILL_MODE);
    case CS2_OP_IF_SETARC:
        return CS2VMX_Op_IF_SetArc(vm, frame, operand);
    case CS2_OP_IF_SETNOSCROLLTHROUGH:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_NO_SCROLL_THROUGH);
    case CS2_OP_IF_SETNOCLICKTHROUGH:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_NO_CLICK_THROUGH);
    case CS2_OP_IF_SETDRAGDEADZONE:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_DRAG_DEAD_ZONE);
    case CS2_OP_IF_SETDRAGDEADTIME:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_DRAG_DEAD_TIME);
    case CS2_OP_IF_SETCLICKMASK:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_CLICKMASK);
    case CS2_OP_IF_SETPINCH:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_PINCH);
    case CS2_OP_IF_SETNPCHEAD:
        return CS2VMX_Op_IF_SetModelKind(vm, frame, operand, INTERFACEX_MODEL_KIND_NPC_HEAD, true);
    case CS2_OP_IF_SETPLAYERHEAD_SELF:
        return CS2VMX_Op_IF_SetModelKind(
            vm, frame, operand, INTERFACEX_MODEL_KIND_PLAYER_SELF, false);
    case CS2_OP_IF_SETMODEL_PLAYERCHATHEAD:
        return CS2VMX_Op_IF_SetModelKind(
            vm, frame, operand, INTERFACEX_MODEL_KIND_PLAYER_CHATHEAD, true);
    case CS2_OP_IF_RESUME_PAUSEBUTTON:
        return CS2VMX_Op_IF_WidgetInt(vm, frame, operand, CS2VM_WIDGET_INT_RESUME_PAUSEBUTTON);
    case CS2_OP_DEFINE_ARRAY:
        return CS2VMX_Op_DefineArray(vm, frame, operand);
    case CS2_OP_PUSH_ARRAY_INT:
        return CS2VMX_Op_PushArrayInt(vm, frame, operand);
    case CS2_OP_POP_ARRAY_INT:
        return CS2VMX_Op_PopArrayInt(vm, frame, operand);
    case CS2_OP_IF_FIND:
        return CS2VMX_Op_IF_Find(vm, frame, operand);
    case CS2_OP_CC_SETTARGETVERB:
        return CS2VM_EXECNO_OK;
    case CS2_OP_CC_SETONVARTRANSMIT:
    case CS2_OP_CC_SETONTIMER:
    case CS2_OP_CC_SETONINVTRANSMIT:
    case CS2_OP_CC_SETONSTATTRANSMIT:
        return CS2VMX_Op_CC_SetOnEventDiscard(vm, frame, operand);
    case CS2_OP_CC_GETTEXT:
        return CS2VMX_Op_CC_GetText(vm, frame, operand);
    case CS2_OP_CC_GETTRANS:
        return CS2VMX_Op_CC_GetTrans(vm, frame, operand);
    case CS2_OP_IF_SETONCLICK:
    case CS2_OP_IF_SETONHOLD:
    case CS2_OP_IF_SETONRELEASE:
    case CS2_OP_IF_SETONDRAG:
    case CS2_OP_IF_SETONTARGETLEAVE:
    case CS2_OP_IF_SETONDRAGCOMPLETE:
    case CS2_OP_IF_SETONSTATTRANSMIT:
    case CS2_OP_IF_SETONTARGETENTER:
    case CS2_OP_IF_SETONFRIENDTRANSMIT:
    case CS2_OP_IF_SETONCLANTRANSMIT:
    case CS2_OP_IF_SETONDIALOGABORT:
    case CS2_OP_IF_SETONSUBCHANGE:
    case CS2_OP_IF_SETONRESIZE:
    case CS2_OP_IF_SETONCLANSETTINGSTRANSMIT:
    case CS2_OP_IF_SETONCLANCHANNELTRANSMIT:
        return CS2VMX_Op_IF_SetOnEventDiscard(vm, frame, operand);
    case CS2_OP_IF_GETX:
        return CS2VMX_Op_IF_GetX(vm, frame, operand);
    case CS2_OP_IF_GETTEXT:
        return CS2VMX_Op_IF_GetText(vm, frame, operand);
    case CS2_OP_IF_GETSCROLLWIDTH:
        return CS2VMX_Op_IF_GetScrollWidth(vm, frame, operand);
    case CS2_OP_SETBIT:
        return CS2VMX_Op_SetBit(vm, frame, operand);
    case CS2_OP_CLEARBIT:
        return CS2VMX_Op_ClearBit(vm, frame, operand);
    case CS2_OP_OR:
        return CS2VMX_Op_Or(vm, frame, operand);
    case CS2_OP_INVPOW:
        return CS2VMX_Op_InvPow(vm, frame, operand);
    case CS2_OP_RANDOM:
        return CS2VMX_Op_Random(vm, frame, operand);
    case CS2_OP_RANDOMINC:
        return CS2VMX_Op_RandomInc(vm, frame, operand);
    case CS2_OP_INTERPOLATE:
        return CS2VMX_Op_Interpolate(vm, frame, operand);
    case CS2_OP_COMPARE:
        return CS2VMX_Op_Compare(vm, frame, operand);
    case CS2_OP_SUBSTRING:
        return CS2VMX_Op_Substring(vm, frame, operand);
    case CS2_OP_OC_COST:
        return CS2VMX_Op_OC_IntParam(vm, frame, operand, CS2VMX_OC_GetterCost);
    case CS2_OP_OC_STACKABLE:
        return CS2VMX_Op_OC_IntParam(vm, frame, operand, CS2VMX_OC_GetterStackable);
    case CS2_OP_OC_MEMBERS:
        return CS2VMX_Op_OC_IntParam(vm, frame, operand, CS2VMX_OC_GetterMembers);
    case CS2_OP_OC_CERT:
    case CS2_OP_OC_UNCERT:
        return CS2VMX_Op_OC_IntParam(vm, frame, operand, CS2VMX_OC_GetterId);
    case CS2_OP_STRUCT_PARAM:
        return CS2VMX_Op_StructParam(vm, frame, operand);
    default:
        return CS2VMX_Op_StackMetaStub(vm, frame, opcode, operand);
    }
}

/* Fills *int_args / *str_args with the stack values this opcode pops.
 * Returns 0 for a fixed count, 1 when the count is variable (e.g. GOSUB). */
static int
CS2VMX_OpArgCounts(
    int opcode,
    int* int_args,
    int* str_args)
{
    *int_args = 0;
    *str_args = 0;
    switch( opcode )
    {
    case CS2_OP_SUB:
    case CS2_OP_MULTIPLY:
    case CS2_OP_DIV:
    case CS2_OP_MOD:
    case CS2_OP_POW:
        *int_args = 2;
        return 0;
    case CS2_OP_SCALE:
        *int_args = 3;
        return 0;
    case CS2_OP_CC_CREATE:
        *int_args = 4;
        return 0;
    case CS2_OP_CC_FIND:
        *int_args = 2;
        return 0;
    case CS2_OP_CC_CREATECHILD:
    case CS2_OP_CC_CREATESIBLING:
        *int_args = 2;
        return 0;
    case CS2_OP_CC_CHILDREN_FIND:
        *int_args = 1;
        return 0;
    case CS2_OP_IF_CHILDREN_FIND:
        *int_args = 2;
        return 0;
    case CS2_OP_POP_INT_LOCAL:
    case CS2_OP_POP_INT_DISCARD:
        *int_args = 1;
        return 0;
    case CS2_OP_POP_STRING_LOCAL:
        *str_args = 1;
        return 0;
    case CS2_OP_JOIN_STRING:
        *str_args = 2;
        return 0;
    case CS2_OP_STRING_LENGTH:
        *str_args = 1;
        return 0;
    case CS2_OP_GOSUB_WITH_PARAMS:
        return 1;
    case CS2_OP_SWITCH:
        *int_args = 1;
        return 0;
    case CS2_OP_OC_PARAM:
        *int_args = 2;
        return 0;
    case CS2_OP_OC_NAME:
        *int_args = 1;
        return 0;
    case CS2_OP_OC_UNPLACEHOLDER:
        *int_args = 1;
        return 0;
    case CS2_OP_IF_SETOPSUBMENU:
        *int_args = 3;
        *str_args = 1;
        return 0;
    case CS2_OP_IF_SETOPBASE:
        *int_args = 1;
        *str_args = 1;
        return 0;
    case CS2_OP_CC_SETOPBASE:
        *str_args = 1;
        return 0;
    case CS2_OP_IF_SETOUTLINE:
        *int_args = 2;
        return 0;
    case CS2_OP_IF_SETTARGETPRIORITY:
        *int_args = 2;
        return 0;
    default:
        return 0;
    }
}

static void
CS2VMX_DebugPrintOpCode(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int opcode,
    int operand,
    char const* str_operand)
{
#if INTERFACEX_DEBUG_OPS
    printf("pc=%d %s (op %d)", frame->pc, CS2_OpCode_String(opcode), opcode);

    switch( cs2_opcode_operand_kind(opcode) )
    {
    case CS2_OPERAND_STRING:
        printf(" operand.str=\"%s\"", str_operand ? str_operand : "(null)");
        break;
    case CS2_OPERAND_INT8:
    case CS2_OPERAND_INT32:
        printf(" operand.int=%d", operand);
        break;
    case CS2_OPERAND_NONE:
    default:
        break;
    }
    printf("\n");

    if( opcode == CS2_OP_PUSH_INT_LOCAL || opcode == CS2_OP_POP_INT_LOCAL )
        printf("    int_local[%d] = %d\n", operand, frame->int_locals[operand]);

    if( opcode == CS2_OP_PUSH_STRING_LOCAL || opcode == CS2_OP_POP_STRING_LOCAL )
        printf(
            "    str_local[%d] = \"%s\"\n",
            operand,
            frame->str_locals[operand] ? frame->str_locals[operand] : "(null)");

    int int_args = 0;
    int str_args = 0;
    if( CS2VMX_OpArgCounts(opcode, &int_args, &str_args) != 0 )
    {
        printf("    args: variable (callee signature)\n");
        return;
    }

    for( int i = 0; i < int_args; i++ )
    {
        int depth = vm->ints_stack_top - 1 - i;
        if( depth >= 0 )
            printf("    int arg[%d] (top-%d) = %d\n", i, i, vm->ints_stack[depth]);
        else
            printf("    int arg[%d] = <stack underflow>\n", i);
    }
    for( int i = 0; i < str_args; i++ )
    {
        int depth = vm->strs_stack_top - 1 - i;
        if( depth >= 0 )
        {
            printf(
                "    str arg[%d] (top-%d) = \"%s\"\n",
                i,
                i,
                vm->strs_stack[depth] ? vm->strs_stack[depth] : "(null)");
        }
        else
            printf("    str arg[%d] = <stack underflow>\n", i);
    }
#endif
}

int
CS2VMX_PushCallScript(
    struct CS2VMX* vm,
    struct CS2_Script* script)
{
    assert(vm);
    assert(vm->frame_sp < CS2VM_MAX_FRAMES);
    assert(script);

    memset(&vm->frames[vm->frame_sp], 0, sizeof(struct CS2VMX_Frame));
    vm->frames[vm->frame_sp].script = script;
    vm->frame_sp += 1;
    return CS2VM_EXECNO_OK;
}

// Called for onLoad, onOp, onClick, onVarTransmit
// Format: [scriptId, ...args]

// [0] = script ID (not a script local)
// [1..] = int/string args → $int0, $int1, … and $obj0, …
// Many onLoad listeners are just [scriptId] (count=1) → no args, param locals stay 0.
int
CS2VMX_SetActiveAndDotComponentId(
    struct CS2VMX* vm,
    int component_id)
{
    assert(vm);
    vm->active_component_id = component_id;
    vm->dot_component_id = component_id;
    return CS2VM_EXECNO_OK;
}

int
CS2VMX_RunScript(struct CS2VMX* vm)
{
    assert(vm);
    assert(vm->frame_sp > 0);

    int result;
    int cycles = 0;
    while( cycles++ < CS2VM_MAX_CYCLES )
    {
        if( vm->frame_sp <= 0 )
            return CS2VM_EXECNO_DONE;

        struct CS2VMX_Frame* frame = &vm->frames[vm->frame_sp - 1];
        if( frame->pc >= frame->script->op_count )
            return CS2VM_EXECNO_DONE;

        int opcode = frame->script->opcodes[frame->pc];
        int operand = frame->script->int_operands[frame->pc];
        char const* str_operand_str = NULL;
        if( frame->script->string_operands )
            str_operand_str = frame->script->string_operands[frame->pc];

        CS2VMX_DebugPrintOpCode(vm, frame, opcode, operand, str_operand_str);

        int op_pc = frame->pc;
        frame->pc += 1;

        result = CS2VMX_RunOp(vm, frame, opcode, operand, str_operand_str);

        CS2VMX_TraceOpcode(vm, frame, op_pc, opcode, operand, result);

        switch( result )
        {
        case CS2VM_EXECNO_OK:
            break;
        default:
            if( result == CS2VM_EXECNO_ERROR )
            {
                vm->last_error_opcode = opcode;
                vm->last_error_pc = op_pc;
                vm->last_error_script_id = frame->script->script_id;
            }
            return result;
        }
    }

    {
        struct CS2VMX_Frame* frame = &vm->frames[vm->frame_sp - 1];
        vm->last_error_opcode = -1;
        vm->last_error_pc = frame->pc;
        vm->last_error_script_id = frame->script->script_id;
    }
    return CS2VM_EXECNO_ERROR;
}

struct MapEntry_ClientScript
{
    int id;
    struct ToriAuxLibCore_ClientScript* script;
};

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

struct InterfaceX_ModelSceneCacheEntry
{
    int model_id;
    int zoom;
    int angle_x;
    int angle_y;
    int angle_z;
    int offset_x;
    int offset_y;
    int scene_id;
    struct InterfaceX_ModelSceneCacheEntry* next;
};

struct InterfaceX_ScriptQueueEntry
{
    int script_id;
    int component_id;
    int int_args[TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX];
    int int_arg_count;
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

struct InterfaceX_VMHost
{
    struct ToriDraw_Map* scripts;

    unsigned char scripts_buf[65536];
    struct RSCacheDat2Disk* disk;
    struct RSCacheDat2Disk_ReferenceTable* clientscript_table;
    struct ToriDraw_Scene* scene;

    struct UITreeXBuilder* builder;
    struct UITreeX* tree;
    int interface_id;
    uint16_t next_dynamic_uid;

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

    struct InterfaceX_ObjIconCacheEntry* obj_icon_cache;
    struct InterfaceX_GraphicSceneCacheEntry* graphic_scene_cache;
    struct InterfaceX_ModelSceneCacheEntry* model_scene_cache;
    int next_scene_id;
};

struct ToriAuxLibCore_ClientScript*
InterfaceX_VMHost_ResolveScript(
    struct InterfaceX_VMHost* host,
    int script_id);

static void
CS2VMX_ResetRuntime(struct CS2VMX* vm);

static int
InterfaceX_RunClientScript(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count);

static void
InterfaceX_VMHost_QueueScript(
    struct InterfaceX_VMHost* host,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count);

static void
InterfaceX_VMHost_DrainScriptQueue(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm);

static struct InterfaceX_InvContainer*
InterfaceX_InvContainerGet(
    struct InterfaceX_VMHost* host,
    int inv_id,
    bool create);

static int
InterfaceX_ResolveObjIconScene(
    struct InterfaceX_VMHost* host,
    int obj_id,
    int count);

static int
InterfaceX_ResolveGraphicScene(
    struct InterfaceX_VMHost* host,
    int graphic_id);

static int
InterfaceX_ResolveModelScene(
    struct InterfaceX_VMHost* host,
    struct UITreeXNode const* node);

static struct ToriDraw_Font*
InterfaceX_EnsureSceneFont(
    struct InterfaceX_VMHost* host,
    int font_id);

static int
InterfaceX_VMHost_Exec_CC_SetObjectOnNode(
    struct InterfaceX_VMHost* host,
    int component_id,
    int obj_id,
    int count);

static int
InterfaceX_EquipmentSlotForFile(int file_index);

static struct RSCacheDat2A_ConfigObject*
InterfaceX_LoadObjConfig(
    struct RSCacheDat2Disk* disk,
    int item_id);

static bool
InterfaceX_ConfigArchiveReady(
    struct RSCacheDat2Disk* disk,
    int config_kind);

static bool
InterfaceX_ConfigArchiveFindFile(
    struct RSCacheDat2Disk* disk,
    int config_kind,
    struct RSCacheShared_FileList* fl,
    int file_id,
    uint8_t const** out_data,
    int* out_len);

static struct RSCacheShared_FileList*
InterfaceX_ConfigArchiveGetFileList(
    struct RSCacheDat2Disk* disk,
    int config_kind);

static int
InterfaceX_EnumLookup(
    struct RSCacheDat2Disk* disk,
    int input_type,
    int output_type,
    int enum_id,
    int key);

static char const*
InterfaceX_EnumLookupString(
    struct RSCacheDat2Disk* disk,
    int input_type,
    int output_type,
    int enum_id,
    int key);

static int
InterfaceX_EnumOutputCount(
    struct RSCacheDat2Disk* disk,
    int enum_id);

static bool
InterfaceX_StructParamLookup(
    struct RSCacheDat2Disk* disk,
    int struct_id,
    int param_id,
    bool* out_is_string,
    int* out_int,
    char const** out_str);

static void
InterfaceX_ConfigArchiveCacheFreeAll(void);

static void
InterfaceX_InvStoreSeedDefaults(struct InterfaceX_VMHost* host);

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

static int
CS2VMX_Op_CC_CreateUnderParent(
    struct CS2VMX* vm,
    int parent_id,
    int type,
    int child_index,
    int operand)
{
    struct CS2VM_HostRequest request;
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_CREATE;
    request.u.cc_create.parent_id = parent_id;
    request.u.cc_create.component_type = type;
    request.u.cc_create.child_index = child_index;
    request.u.cc_create.is_nested = 0;
    request.u.cc_create.dot_operand = operand;

    return vm->host_exec(vm, &request);
}

int
CS2VMX_Op_CC_FindRoot(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)vm->user;
    int component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    int found = 0;

    if( host && host->tree && component_id >= 0 )
    {
        int parent_id = UITreeX_ParentComponentId(host->tree, component_id);
        if( parent_id >= 0 )
        {
            CS2VMX_SetTargetComponentId(vm, operand, parent_id);
            found = 1;
        }
    }

    CS2VMX_SetTraceExtra("found=%d target=%s", found, operand == 1 ? "dw" : "aw");
    return CS2VMX_PushInt(vm, found);
}

int
CS2VMX_Op_CC_ChildrenFind(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int start_index;
    if( CS2VMX_PopInt(vm, &start_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)vm->user;
    int parent_id = CS2VMX_DotOrActiveComponentId(vm, operand);

    CS2VMX_ResetChildrenIter(vm);
    if( host && host->tree )
    {
        vm->children_iter_count = CS2VMX_CollectDynamicChildIndices(
            host->tree,
            parent_id,
            start_index,
            vm->children_iter_indices,
            CS2VMX_CHILDREN_ITER_MAX);
    }

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_CC_ChildrenFindNextId(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    if( vm->children_iter_index < vm->children_iter_count )
        return CS2VMX_PushInt(vm, vm->children_iter_indices[vm->children_iter_index++]);
    return CS2VMX_PushInt(vm, -1);
}

int
CS2VMX_Op_IF_ChildrenFind(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int start_index, uid;
    if( CS2VMX_PopInt(vm, &start_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &uid) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)vm->user;

    CS2VMX_ResetChildrenIter(vm);
    if( host && host->tree )
    {
        vm->children_iter_count = CS2VMX_CollectDynamicChildIndices(
            host->tree, uid, start_index, vm->children_iter_indices, CS2VMX_CHILDREN_ITER_MAX);

        if( UITreeX_FindByUserId(host->tree, uid) >= 0 )
            CS2VMX_SetTargetComponentId(vm, operand, uid);
    }

    return CS2VM_EXECNO_OK;
}

int
CS2VMX_Op_IF_ChildrenFindNextId(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    return CS2VMX_Op_CC_ChildrenFindNextId(vm, frame, operand);
}

int
CS2VMX_Op_CC_CreateChild(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int child_index, type;
    if( CS2VMX_PopInt(vm, &child_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    int parent_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    if( parent_id < 0 )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_Op_CC_CreateUnderParent(vm, parent_id, type, child_index, operand);
}

int
CS2VMX_Op_CC_CreateSibling(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int child_index, type;
    if( CS2VMX_PopInt(vm, &child_index) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &type) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)vm->user;
    int current_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    if( current_id < 0 || !host || !host->tree )
        return CS2VM_EXECNO_ERROR;

    int parent_id = UITreeX_ParentComponentId(host->tree, current_id);
    if( parent_id < 0 )
        return CS2VM_EXECNO_ERROR;

    return CS2VMX_Op_CC_CreateUnderParent(vm, parent_id, type, child_index, operand);
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
    int is_root)
{
    assert(tree);
    assert(node_idx >= 0 && node_idx < tree->node_count);

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
        if( node->u.rs_layer.scroll_width > 0 )
            child_pw = node->u.rs_layer.scroll_width;
        if( node->u.rs_layer.scroll_height > 0 )
            child_ph = node->u.rs_layer.scroll_height;
        /* Scrolled content is laid out at its natural position, then shifted up/left
         * by the scroll offset; UITreeX_RenderNode clips it back to the layer's bounds. */
        child_px -= node->u.rs_layer.scroll_x;
        child_py -= node->u.rs_layer.scroll_y;
    }

    for( int child = node->link.first_child_tree_idx; child != -1;
         child = tree->nodes[child].link.next_sibling_tree_idx )
        UITreeX_LayoutNode(tree, child, child_px, child_py, child_pw, child_ph, 0);
}

static void
UITreeX_LayoutResolve(
    struct UITreeX* tree,
    int root_w,
    int root_h)
{
    assert(tree);

    UITreeX_InvalidateLayout(tree);

    for( int i = 0; i < tree->node_count; i++ )
    {
        if( UITreeX_NodeIsLiveRoot(&tree->nodes[i]) )
            UITreeX_LayoutNode(tree, i, 0, 0, root_w, root_h, 1);
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
        return CANVAS_W;

    struct UITreeXNode* node = &tree->nodes[idx];
    if( node->layout_resolved && node->abs_w > 0 )
        return node->abs_w;
    return node->w > 0 ? node->w : CANVAS_W;
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
        return CANVAS_H;

    struct UITreeXNode* node = &tree->nodes[idx];
    if( node->layout_resolved && node->abs_h > 0 )
        return node->abs_h;
    return node->h > 0 ? node->h : CANVAS_H;
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
CS2VMX_OC_GetterCost(struct RSCacheDat2A_ConfigObject* obj)
{
    return obj->cost;
}

static int
CS2VMX_OC_GetterStackable(struct RSCacheDat2A_ConfigObject* obj)
{
    return obj->stacking_behaviour;
}

static int
CS2VMX_OC_GetterMembers(struct RSCacheDat2A_ConfigObject* obj)
{
    return obj->is_members ? 1 : 0;
}

static int
CS2VMX_OC_GetterId(struct RSCacheDat2A_ConfigObject* obj)
{
    return obj->_id;
}

int
CS2VMX_Op_StructParam(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int param_id;
    int struct_id;
    if( CS2VMX_PopInt(vm, &param_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;
    if( CS2VMX_PopInt(vm, &struct_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)vm->user;
    bool is_string = false;
    int intval = 0;
    char const* strval = NULL;
    bool found = false;
    if( host && host->disk )
    {
        found = InterfaceX_StructParamLookup(
            host->disk, struct_id, param_id, &is_string, &intval, &strval);
    }

    if( found && is_string )
        return CS2VMX_PushStr(vm, strdup(strval ? strval : ""));
    if( found )
        return CS2VMX_PushInt(vm, intval);
    return CS2VMX_PushInt(vm, 0);
}

int
CS2VMX_Op_CC_GetText(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)vm->user;
    int component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    struct UITreeXNode* node = host ? UITreeX_NodeByComponentId(host, component_id) : NULL;
    if( node && node->kind == UITreeXNodeKind_RSText )
        return CS2VMX_PushStr(vm, strdup(node->u.rs_text.text));
    return CS2VMX_PushStr(vm, strdup(""));
}

int
CS2VMX_Op_CC_GetTrans(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)frame;

    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)vm->user;
    int component_id = CS2VMX_DotOrActiveComponentId(vm, operand);
    struct UITreeXNode* node = host ? UITreeX_NodeByComponentId(host, component_id) : NULL;
    int trans = node ? node->trans : 0;
    return CS2VMX_PushInt(vm, trans);
}

int
CS2VMX_Op_IF_Find(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)vm->user;
    int found = 0;
    if( host && UITreeX_FindByUserId(host->tree, component_id) >= 0 )
    {
        if( operand == 1 )
            vm->dot_component_id = component_id;
        else
            vm->active_component_id = component_id;
        found = 1;
    }
    CS2VMX_SetTraceExtra(
        "uid=0x%08x found=%d target=%s", (unsigned)component_id, found, operand == 1 ? "dw" : "aw");
    return CS2VMX_PushInt(vm, found);
}

int
CS2VMX_Op_IF_GetX(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)vm->user;
    int x = 0;
    if( host && UITreeX_NodeByComponentId(host, component_id) )
        x = UITreeX_GetPosX(host->tree, component_id);
    return CS2VMX_PushInt(vm, x);
}

int
CS2VMX_Op_IF_GetText(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)vm->user;
    struct UITreeXNode* node = host ? UITreeX_NodeByComponentId(host, component_id) : NULL;
    if( node && node->kind == UITreeXNodeKind_RSText )
        return CS2VMX_PushStr(vm, strdup(node->u.rs_text.text));
    return CS2VMX_PushStr(vm, strdup(""));
}

int
CS2VMX_Op_IF_GetScrollWidth(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand)
{
    assert(vm);
    assert(frame);
    (void)operand;

    int component_id;
    if( CS2VMX_PopInt(vm, &component_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)vm->user;
    struct UITreeXNode* node = host ? UITreeX_NodeByComponentId(host, component_id) : NULL;
    int scroll_width = 0;
    if( node && node->kind == UITreeXNodeKind_RSLayer )
        scroll_width = node->u.rs_layer.scroll_width;
    return CS2VMX_PushInt(vm, scroll_width);
}

int
CS2VMX_Op_OC_IntParam(
    struct CS2VMX* vm,
    struct CS2VMX_Frame* frame,
    int operand,
    int (*getter)(struct RSCacheDat2A_ConfigObject* obj))
{
    assert(vm);
    assert(frame);
    (void)operand;

    int item_id;
    if( CS2VMX_PopInt(vm, &item_id) != CS2VM_EXECNO_OK )
        return CS2VM_EXECNO_ERROR;

    struct InterfaceX_VMHost* host = (struct InterfaceX_VMHost*)vm->user;
    struct RSCacheDat2A_ConfigObject* obj =
        host ? InterfaceX_LoadObjConfig(host->disk, item_id) : NULL;
    int value = 0;
    if( obj )
    {
        value = getter(obj);
        RSCacheDat2A_ConfigObjectFree(obj);
    }
    return CS2VMX_PushInt(vm, value);
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

/* Alpha-blends a single ARGB pixel into dest, matching blit_rgba_pixel below
 * (declared ahead of its definition so InterfaceX_FillRect can honor node trans). */
static void
blit_rgba_pixel(
    int* dest,
    int dstride,
    int sx,
    int sy,
    int p);

static void
InterfaceX_FillRect(
    int* pixels,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int argb)
{
    if( x0 < g_render_clip_x0 )
        x0 = g_render_clip_x0;
    if( y0 < g_render_clip_y0 )
        y0 = g_render_clip_y0;
    if( x1 > g_render_clip_x1 )
        x1 = g_render_clip_x1;
    if( y1 > g_render_clip_y1 )
        y1 = g_render_clip_y1;

    int a = (argb >> 24) & 0xFF;
    for( int y = y0; y < y1; y++ )
    {
        for( int x = x0; x < x1; x++ )
        {
            if( a >= 255 )
                pixels[y * stride + x] = argb;
            else
                blit_rgba_pixel(pixels, stride, x, y, argb);
        }
    }
}

static void
InterfaceX_DrawRectOutline(
    int* pixels,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int argb)
{
    InterfaceX_FillRect(pixels, stride, x0, y0, x1, y0 + 1, argb);
    InterfaceX_FillRect(pixels, stride, x0, y1 - 1, x1, y1, argb);
    InterfaceX_FillRect(pixels, stride, x0, y0, x0 + 1, y1, argb);
    InterfaceX_FillRect(pixels, stride, x1 - 1, y0, x1, y1, argb);
}

static void
InterfaceX_DrawLine(
    int* pixels,
    int stride,
    int x0,
    int y0,
    int x1,
    int y1,
    int thickness,
    int argb)
{
    if( thickness < 1 )
        thickness = 1;

    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int x = x0;
    int y = y0;

    while( true )
    {
        int half = thickness / 2;
        InterfaceX_FillRect(
            pixels, stride, x - half, y - half, x - half + thickness, y - half + thickness, argb);

        if( x == x1 && y == y1 )
            break;

        int e2 = err * 2;
        if( e2 > -dy )
        {
            err -= dy;
            x += sx;
        }
        if( e2 < dx )
        {
            err += dx;
            y += sy;
        }
    }
}

static void
interface_x_transform_sprite_pixels(
    uint32_t** spr_px,
    int* sw,
    int* sh,
    int hflip,
    int vflip,
    int angle_2d)
{
    if( !spr_px || !*spr_px || *sw <= 0 || *sh <= 0 )
        return;

    if( hflip || vflip )
    {
        struct ToriDraw_Sprite tmp = {
            .width = *sw,
            .height = *sh,
            .pixels_argb = *spr_px,
        };
        if( hflip )
            ToriDraw_SpriteFlipHorizontal(&tmp);
        if( vflip )
            ToriDraw_SpriteFlipVertical(&tmp);
    }

    if( angle_2d == 0 )
        return;

    double rad = ((double)angle_2d * 2.0 * 3.141592653589793) / 2048.0;
    double c = cos(rad);
    double s = sin(rad);
    int src_w = *sw;
    int src_h = *sh;
    int cx = src_w / 2;
    int cy = src_h / 2;

    int corners[4][2] = {
        { -cx,            -cy            },
        { src_w - 1 - cx, -cy            },
        { -cx,            src_h - 1 - cy },
        { src_w - 1 - cx, src_h - 1 - cy },
    };
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
    for( int i = 0; i < 4; i++ )
    {
        int rx = (int)lround((double)corners[i][0] * c - (double)corners[i][1] * s);
        int ry = (int)lround((double)corners[i][0] * s + (double)corners[i][1] * c);
        if( i == 0 )
        {
            min_x = max_x = rx;
            min_y = max_y = ry;
        }
        else
        {
            if( rx < min_x )
                min_x = rx;
            if( rx > max_x )
                max_x = rx;
            if( ry < min_y )
                min_y = ry;
            if( ry > max_y )
                max_y = ry;
        }
    }

    int dst_w = max_x - min_x + 1;
    int dst_h = max_y - min_y + 1;
    if( dst_w <= 0 || dst_h <= 0 )
        return;

    uint32_t* dst = calloc((size_t)dst_w * (size_t)dst_h, sizeof(uint32_t));
    if( !dst )
        return;

    uint32_t const* src = *spr_px;
    for( int dy = 0; dy < dst_h; dy++ )
    {
        for( int dx = 0; dx < dst_w; dx++ )
        {
            double lx = (double)(dx + min_x);
            double ly = (double)(dy + min_y);
            int sx = (int)lround(lx * c + ly * s) + cx;
            int sy = (int)lround(-lx * s + ly * c) + cy;
            if( sx >= 0 && sx < src_w && sy >= 0 && sy < src_h )
                dst[dx + dy * dst_w] = src[sx + sy * src_w];
        }
    }

    free(*spr_px);
    *spr_px = dst;
    *sw = dst_w;
    *sh = dst_h;
}

static void
blit_rgba_pixel(
    int* dest,
    int dstride,
    int sx,
    int sy,
    int p)
{
    if( sx < g_render_clip_x0 || sy < g_render_clip_y0 || sx >= g_render_clip_x1 ||
        sy >= g_render_clip_y1 )
        return;

    int a = (p >> 24) & 0xFF;
    if( a == 0 )
        return;

    if( a == 255 )
    {
        dest[sy * dstride + sx] = (p & 0x00FFFFFF) | 0xFF000000;
        return;
    }

    int d = dest[sy * dstride + sx];
    int dr = (d >> 16) & 0xFF;
    int dg = (d >> 8) & 0xFF;
    int db = d & 0xFF;
    int sr = (p >> 16) & 0xFF;
    int sg = (p >> 8) & 0xFF;
    int sb = p & 0xFF;
    int rr = (sr * a + dr * (255 - a)) / 255;
    int rg = (sg * a + dg * (255 - a)) / 255;
    int rb = (sb * a + db * (255 - a)) / 255;
    dest[sy * dstride + sx] = 0xFF000000 | (rr << 16) | (rg << 8) | rb;
}

static void
blit_rgba_sprite(
    int* dest,
    int dstride,
    int dx,
    int dy,
    int const* spr,
    int sw,
    int sh)
{
    for( int y = 0; y < sh; y++ )
    {
        int sy = dy + y;
        for( int x = 0; x < sw; x++ )
            blit_rgba_pixel(dest, dstride, dx + x, sy, spr[y * sw + x]);
    }
}

/* Like blit_rgba_sprite, but source (sw x sh) and destination (dw x dh) sizes may differ.
 * Uses nearest-neighbor sampling so the source stride (sw) is always respected, unlike a
 * plain blit_rgba_sprite call with a shrunk width/height (which would read the wrong pixels). */
static void
blit_rgba_sprite_scaled(
    int* dest,
    int dstride,
    int dx,
    int dy,
    int dw,
    int dh,
    int const* spr,
    int sw,
    int sh)
{
    if( sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 )
        return;

    for( int y = 0; y < dh; y++ )
    {
        int sy = (y * sh) / dh;
        if( sy >= sh )
            sy = sh - 1;
        int dsty = dy + y;
        for( int x = 0; x < dw; x++ )
        {
            int sx = (x * sw) / dw;
            if( sx >= sw )
                sx = sw - 1;
            blit_rgba_pixel(dest, dstride, dx + x, dsty, spr[sy * sw + sx]);
        }
    }
}

static void
blit_rgba_sprite_tiled(
    int* dest,
    int dstride,
    int rect_x,
    int rect_y,
    int rect_w,
    int rect_h,
    int const* spr,
    int sw,
    int sh,
    int origin_x,
    int origin_y)
{
    if( sw <= 0 || sh <= 0 || rect_w <= 0 || rect_h <= 0 )
        return;

    int x0 = rect_x < g_render_clip_x0 ? g_render_clip_x0 : rect_x;
    int y0 = rect_y < g_render_clip_y0 ? g_render_clip_y0 : rect_y;
    int x1 = rect_x + rect_w;
    int y1 = rect_y + rect_h;
    if( x1 > g_render_clip_x1 )
        x1 = g_render_clip_x1;
    if( y1 > g_render_clip_y1 )
        y1 = g_render_clip_y1;

    for( int y = y0; y < y1; y++ )
    {
        int sy = y - origin_y;
        sy = ((sy % sh) + sh) % sh;
        for( int x = x0; x < x1; x++ )
        {
            int sx = x - origin_x;
            sx = ((sx % sw) + sw) % sw;
            blit_rgba_pixel(dest, dstride, x, y, spr[sy * sw + sx]);
        }
    }
}

static int
interface_x_sprite_alpha(uint32_t p)
{
    return (int)((p >> 24) & 0xFF);
}

static uint32_t
interface_x_sprite_sample(
    uint32_t const* spr,
    int sw,
    int sh,
    int sx,
    int sy)
{
    if( !spr || sx < 0 || sy < 0 || sx >= sw || sy >= sh )
        return 0;
    return spr[sy * sw + sx];
}

/* Draw content only where mask alpha is zero (compass / inverted-mask semantics). */
static void
blit_rgba_sprite_masked_inverted(
    int* dest,
    int dstride,
    int dx,
    int dy,
    int dw,
    int dh,
    uint32_t const* content,
    int cw,
    int ch,
    uint32_t const* mask,
    int mw,
    int mh)
{
    if( !dest || !content || !mask || dw <= 0 || dh <= 0 || mw <= 0 || mh <= 0 || cw <= 0 ||
        ch <= 0 )
        return;

    for( int y = 0; y < dh; y++ )
    {
        int dst_y = dy + y;
        int my = (y * mh) / dh;
        if( my >= mh )
            my = mh - 1;
        for( int x = 0; x < dw; x++ )
        {
            int dst_x = dx + x;
            int mx = (x * mw) / dw;
            if( mx >= mw )
                mx = mw - 1;

            uint32_t mask_px = mask[my * mw + mx];
            if( interface_x_sprite_alpha(mask_px) > 127 )
                continue;

            int cx = (x * cw) / dw;
            int cy = (y * ch) / dh;
            if( cx >= cw )
                cx = cw - 1;
            if( cy >= ch )
                cy = ch - 1;

            uint32_t content_px = content[cy * cw + cx];
            if( interface_x_sprite_alpha(content_px) == 0 )
                continue;

            blit_rgba_pixel(dest, dstride, dst_x, dst_y, (int)content_px);
        }
    }
}

/* Draw content only where mask alpha is non-zero (positive-mask semantics). */
static void
blit_rgba_sprite_masked(
    int* dest,
    int dstride,
    int dx,
    int dy,
    int dw,
    int dh,
    uint32_t const* content,
    int cw,
    int ch,
    uint32_t const* mask,
    int mw,
    int mh)
{
    if( !dest || !content || !mask || dw <= 0 || dh <= 0 || mw <= 0 || mh <= 0 || cw <= 0 ||
        ch <= 0 )
        return;

    for( int y = 0; y < dh; y++ )
    {
        int dst_y = dy + y;
        int my = (y * mh) / dh;
        if( my >= mh )
            my = mh - 1;
        for( int x = 0; x < dw; x++ )
        {
            int dst_x = dx + x;
            int mx = (x * mw) / dw;
            if( mx >= mw )
                mx = mw - 1;

            uint32_t mask_px = mask[my * mw + mx];
            if( interface_x_sprite_alpha(mask_px) == 0 )
                continue;

            int cx = (x * cw) / dw;
            int cy = (y * ch) / dh;
            if( cx >= cw )
                cx = cw - 1;
            if( cy >= ch )
                cy = ch - 1;

            uint32_t content_px = content[cy * cw + cx];
            if( interface_x_sprite_alpha(content_px) == 0 )
                continue;

            blit_rgba_pixel(dest, dstride, dst_x, dst_y, (int)content_px);
        }
    }
}

/* Compass draw: rotated content center-cropped to mask, inverted mask clip.
 * angle_scale matches reference widget spriteAngle (65536 = full turn). */
static void
interface_x_blit_rotated_masked_inverted(
    int* dest,
    int dstride,
    int mask_x,
    int mask_y,
    int mask_w,
    int mask_h,
    uint32_t const* content,
    int content_w,
    int content_h,
    uint32_t const* mask,
    int mask_sw,
    int mask_sh,
    int angle,
    int angle_scale,
    int alpha)
{
    if( !dest || !content || !mask || mask_w <= 0 || mask_h <= 0 || content_w <= 0 ||
        content_h <= 0 || mask_sw <= 0 || mask_sh <= 0 )
        return;

    double rad = 0.0;
    if( angle != 0 && angle_scale > 0 )
        rad = ((double)angle * 2.0 * 3.141592653589793) / (double)angle_scale;

    double cos_a = cos(rad);
    double sin_a = sin(rad);
    int cx = mask_x + mask_w / 2;
    int cy = mask_y + mask_h / 2;
    int content_cx = content_w / 2;
    int content_cy = content_h / 2;

    int x0 = mask_x < g_render_clip_x0 ? g_render_clip_x0 : mask_x;
    int y0 = mask_y < g_render_clip_y0 ? g_render_clip_y0 : mask_y;
    int x1 = mask_x + mask_w;
    int y1 = mask_y + mask_h;
    if( x1 > g_render_clip_x1 )
        x1 = g_render_clip_x1;
    if( y1 > g_render_clip_y1 )
        y1 = g_render_clip_y1;

    for( int py = y0; py < y1; py++ )
    {
        int my = ((py - mask_y) * mask_sh) / mask_h;
        if( my < 0 )
            my = 0;
        else if( my >= mask_sh )
            my = mask_sh - 1;

        for( int px = x0; px < x1; px++ )
        {
            int mx = ((px - mask_x) * mask_sw) / mask_w;
            if( mx < 0 )
                mx = 0;
            else if( mx >= mask_sw )
                mx = mask_sw - 1;

            uint32_t mask_px = mask[my * mask_sw + mx];
            if( interface_x_sprite_alpha(mask_px) > 127 )
                continue;

            double lx = (double)(px - cx);
            double ly = (double)(py - cy);
            double ux = lx * cos_a + ly * sin_a;
            double uy = -lx * sin_a + ly * cos_a;
            int csx = (int)lround((double)content_cx + ux);
            int csy = (int)lround((double)content_cy + uy);
            if( csx < 0 || csy < 0 || csx >= content_w || csy >= content_h )
                continue;

            uint32_t content_px = content[csy * content_w + csx];
            if( interface_x_sprite_alpha(content_px) == 0 )
                continue;

            if( alpha < 255 )
            {
                int a = interface_x_sprite_alpha(content_px);
                a = (a * alpha) / 255;
                content_px = (content_px & 0x00FFFFFFu) | ((uint32_t)a << 24);
            }

            blit_rgba_pixel(dest, dstride, px, py, (int)content_px);
        }
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

    int mask_id = node->u.rs_graphic.graphic_id;
    if( mask_id < 0 )
        return;

    int content_id = node->u.rs_graphic.graphic_id2;
    if( content_id < 0 )
        return;

    int mask_scene = node->u.rs_graphic.scene_id;
    if( mask_scene < 0 )
        mask_scene = InterfaceX_ResolveGraphicScene(host, mask_id);

    int content_scene = InterfaceX_ResolveGraphicScene(host, content_id);
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

    interface_x_blit_rotated_masked_inverted(
        pixels,
        CANVAS_W,
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
        65536,
        node_alpha);
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
    interface_x_transform_sprite_pixels(&spr_px, &sw, &sh, hflip, vflip, angle_2d);

    /* IF3: stretch nominal sprite to widget bounds (lw x lh). IF1: native-size blit below. */
    if( if3 && !tiling )
    {
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

        int draw_w = lw > 0 ? lw : sw;
        int draw_h = lh > 0 ? lh : sh;
        blit_rgba_sprite_scaled(
            pixels, CANVAS_W, dx, dy, draw_w, draw_h, (int const*)spr_px, sw, sh);
    }
    else if( tiling )
        blit_rgba_sprite_tiled(
            pixels, CANVAS_W, dx, dy, lw, lh, (int const*)spr_px, sw, sh, dx, dy);
    else
        blit_rgba_sprite(pixels, CANVAS_W, dx + ox, dy + oy, (int const*)spr_px, sw, sh);
    free(spr_px);
}

static void
UITreeX_RenderNode(
    struct InterfaceX_VMHost* host,
    struct RSCacheDat2Disk* cache,
    struct UITreeX const* tree,
    int node_idx,
    int* pixels)
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

    if( node->kind == UITreeXNodeKind_RSGraphic && host )
    {
        if( node->u.rs_graphic.graphic_id < 0 && node->u.rs_graphic.scene_id < 0 )
            goto render_children;

        if( node->client_code == INTERFACEX_CONTENT_COMPASS )
        {
            InterfaceX_BlitCompassGraphic(host, pixels, node, node_alpha);
            goto render_children;
        }

        if( node->client_code == INTERFACEX_CONTENT_MINIMAP )
            goto render_children;

        int scene_id = node->u.rs_graphic.scene_id;
        if( scene_id < 0 )
        {
            if( node->u.rs_graphic.graphic_id < 0 )
                goto render_children;
            scene_id = InterfaceX_ResolveGraphicScene(host, node->u.rs_graphic.graphic_id);
        }

        if( scene_id >= 0 )
        {
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
                    node->u.rs_graphic.outline,
                    node->u.rs_graphic.graphic_shadow,
                    node->if3,
                    node_alpha,
                    node->hflip,
                    node->vflip,
                    node->angle_2d);
            }
        }
    }
    else if( node->kind == UITreeXNodeKind_RSModel && host )
    {
        int scene_id = InterfaceX_ResolveModelScene(host, node);
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

                if( node_alpha >= 255 )
                {
                    blit_rgba_sprite_scaled(
                        pixels,
                        CANVAS_W,
                        node->abs_x,
                        node->abs_y,
                        bw,
                        bh,
                        (int const*)spr->pixels_argb,
                        sw,
                        sh);
                }
                else
                {
                    size_t pixel_count = (size_t)sw * (size_t)sh;
                    uint32_t* tmp = malloc(pixel_count * sizeof(uint32_t));
                    if( tmp )
                    {
                        memcpy(tmp, spr->pixels_argb, pixel_count * sizeof(uint32_t));
                        interface_x_scale_pixel_alpha(tmp, pixel_count, node_alpha);
                        blit_rgba_sprite_scaled(
                            pixels,
                            CANVAS_W,
                            node->abs_x,
                            node->abs_y,
                            bw,
                            bh,
                            (int const*)tmp,
                            sw,
                            sh);
                        free(tmp);
                    }
                }
            }
        }
    }
    else if( node->kind == UITreeXNodeKind_RSLine )
    {
        int px = node->abs_x;
        int py = node->abs_y;
        int pw = node->abs_w > 0 ? node->abs_w : 1;
        int ph = node->abs_h > 0 ? node->abs_h : 1;
        int argb = (node_alpha << 24) | (node->u.rs_line.color & 0xFFFFFF);
        int thickness = node->u.rs_line.line_width > 0 ? node->u.rs_line.line_width : 1;
        if( node->u.rs_line.line_direction == 1 )
        {
            int y = py + ph / 2;
            InterfaceX_DrawLine(pixels, CANVAS_W, px, y, px + pw - 1, y, thickness, argb);
        }
        else if( node->u.rs_line.line_direction == 0 )
        {
            int x = px + pw / 2;
            InterfaceX_DrawLine(pixels, CANVAS_W, x, py, x, py + ph - 1, thickness, argb);
        }
        else
        {
            InterfaceX_DrawLine(
                pixels, CANVAS_W, px, py, px + pw - 1, py + ph - 1, thickness, argb);
        }
    }
    else if( node->kind == UITreeXNodeKind_RSObj && node->u.rs_obj.obj_id > 0 && host )
    {
        int scene_id = node->u.rs_obj.scene_id;
        if( scene_id < 0 )
            scene_id = InterfaceX_ResolveObjIconScene(
                host, node->u.rs_obj.obj_id, node->u.rs_obj.obj_count);

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

                if( node_alpha >= 255 )
                {
                    blit_rgba_sprite_scaled(
                        pixels,
                        CANVAS_W,
                        node->abs_x,
                        node->abs_y,
                        bw,
                        bh,
                        (int const*)spr->pixels_argb,
                        sw,
                        sh);
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
                        blit_rgba_sprite_scaled(
                            pixels,
                            CANVAS_W,
                            node->abs_x,
                            node->abs_y,
                            bw,
                            bh,
                            (int const*)tmp,
                            sw,
                            sh);
                        free(tmp);
                    }
                }
            }
        }
    }
    else if( node->kind == UITreeXNodeKind_RSRect )
    {
        int px = node->abs_x;
        int py = node->abs_y;
        int pw = node->abs_w > 0 ? node->abs_w : 1;
        int ph = node->abs_h > 0 ? node->abs_h : 1;
        int argb = (node_alpha << 24) | (node->u.rs_rect.color & 0xFFFFFF);
        if( node->u.rs_rect.filled )
            InterfaceX_FillRect(pixels, CANVAS_W, px, py, px + pw, py + ph, argb);
        else
            InterfaceX_DrawRectOutline(pixels, CANVAS_W, px, py, px + pw, py + ph, argb);
    }
    else if( node->kind == UITreeXNodeKind_RSText && node->u.rs_text.text[0] && host )
    {
        int font_id = node->u.rs_text.font_id;
        if( font_id < 0 )
            font_id = 495;

        struct ToriDraw_Font* font = InterfaceX_EnsureSceneFont(host, font_id);
        if( font )
        {
            struct ToriDraw_ViewPort view_port = {
                .clip_left = g_render_clip_x0,
                .clip_top = g_render_clip_y0,
                .clip_right = g_render_clip_x1,
                .clip_bottom = g_render_clip_y1,
                .stride = CANVAS_W,
            };

            int lw = node->abs_w;
            int lh = node->abs_h;
            int color = node->u.rs_text.color & 0xFFFFFF;
            bool shadowed = node->u.rs_text.shadowed != 0;
            bool center = node->u.rs_text.center == 1;

            if( lw > 0 && lh > 0 )
            {
                (void)ToriDraw2D_DrawStringBox(
                    font,
                    &view_port,
                    node->abs_x,
                    node->abs_y,
                    lw,
                    lh,
                    node->u.rs_text.text,
                    color,
                    node->u.rs_text.center,
                    node->u.rs_text.y_align,
                    node->u.rs_text.line_height,
                    shadowed,
                    pixels);
            }
            else
            {
                int tx = node->abs_x;
                int ty = node->abs_y + lh;
                if( center && lw > 0 )
                {
                    int tw = ToriDraw2D_MeasureString(font, node->u.rs_text.text);
                    tx = node->abs_x + (lw - tw) / 2;
                }
                (void)ToriDraw2D_DrawString(
                    font,
                    &view_port,
                    tx,
                    ty,
                    node->u.rs_text.text,
                    color,
                    center,
                    shadowed,
                    pixels);
            }
        }
    }

render_children:
{
    int saved_clip_x0 = g_render_clip_x0;
    int saved_clip_y0 = g_render_clip_y0;
    int saved_clip_x1 = g_render_clip_x1;
    int saved_clip_y1 = g_render_clip_y1;

    /* Scrollable layers clip their children to the layer's own viewport; a
     * zero-size layer (pure grouping container) leaves the inherited clip as-is. */
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
        UITreeX_RenderNode(host, cache, tree, child, pixels);

    g_render_clip_x0 = saved_clip_x0;
    g_render_clip_y0 = saved_clip_y0;
    g_render_clip_x1 = saved_clip_x1;
    g_render_clip_y1 = saved_clip_y1;
}
}

static void
UITreeX_Render(
    struct InterfaceX_VMHost* host,
    struct RSCacheDat2Disk* cache,
    struct UITreeX* tree,
    int* pixels)
{
    assert(cache);
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
            UITreeX_RenderNode(host, cache, tree, i, pixels);
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
    struct UITreeXBuilder* builder,
    struct ToriAuxLibCore_Component* component)
{
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
        UITreeX_ApplyComponentGeometry(&builder->tree->nodes[idx], component);

    return idx;
}

/* Resolved-clientscript cache, keyed by script id. Complex onLoad hooks (e.g. the
 * bank's) gosub into dozens of distinct helper scripts, well past the old cap of 64. */
#define HOST_SCRIPT_MAP_CAP 1024

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

static int
InterfaceX_EquipmentSlotForFile(int file_index)
{
    static int const k_slot_files[] = { 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25 };
    for( int i = 0; i < (int)(sizeof(k_slot_files) / sizeof(k_slot_files[0])); i++ )
    {
        if( k_slot_files[i] == file_index )
            return i;
    }
    return -1;
}

static struct RSCacheDat2A_ConfigObject*
InterfaceX_LoadObjConfig(
    struct RSCacheDat2Disk* disk,
    int item_id)
{
    assert(disk);
    assert(item_id >= 0);

    struct RSCacheShared_FileList* fl =
        InterfaceX_ConfigArchiveGetFileList(disk, RSCacheDat2A_ConfigKind_Object);
    if( !fl )
        return NULL;

    uint8_t const* data = NULL;
    int data_len = 0;
    if( !InterfaceX_ConfigArchiveFindFile(
            disk, RSCacheDat2A_ConfigKind_Object, fl, item_id, &data, &data_len) )
        return NULL;

    struct RSCacheDat2A_ConfigObject* decoded = calloc(1, sizeof(struct RSCacheDat2A_ConfigObject));
    if( !decoded )
        return NULL;

    RSCacheDat2A_ConfigObjectDecodeInplace(decoded, (char*)data, data_len);
    decoded->_id = item_id;
    return decoded;
}

static void
InterfaceX_InvStoreSeedDefaults(struct InterfaceX_VMHost* host)
{
    assert(host);

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

    struct
    {
        int file_index;
        int obj_id;
    } const k_worn_items[] = {
        { 15, 1153 },
        { 18, 1333 },
        { 19, 1115 },
        { 21, 1189 },
        { 23, 1067 },
    };

    for( int i = 0; i < (int)(sizeof(k_worn_items) / sizeof(k_worn_items[0])); i++ )
    {
        int slot = InterfaceX_EquipmentSlotForFile(k_worn_items[i].file_index);
        if( slot >= 0 )
            InterfaceX_InvContainerSetSlot(worn, slot, k_worn_items[i].obj_id, 1);
    }
}

static struct ToriDraw_Font*
InterfaceX_FontNewFromCore(struct ToriAuxLibCore_Font const* src)
{
    if( !src )
        return NULL;

    struct ToriDraw_Font* font = calloc(1, sizeof(struct ToriDraw_Font));
    if( !font )
        return NULL;

    for( int i = 0; i < TORIAUXLIBCORE_FONT_GLYPH_COUNT; i++ )
    {
        font->glyph_width[i] = src->glyph_width[i];
        font->glyph_height[i] = src->glyph_height[i];
        font->offset_x[i] = src->offset_x[i];
        font->offset_y[i] = src->offset_y[i];
        font->advance[i] = src->advance[i];
        if( src->glyph_alpha[i] && src->glyph_width[i] > 0 && src->glyph_height[i] > 0 )
        {
            size_t len = (size_t)src->glyph_width[i] * (size_t)src->glyph_height[i];
            font->glyph_alpha[i] = malloc(len);
            if( !font->glyph_alpha[i] )
                goto fail;
            memcpy(font->glyph_alpha[i], src->glyph_alpha[i], len);
        }
    }
    font->advance[TORIDRAW_FONT_GLYPH_COUNT] = src->advance[TORIAUXLIBCORE_FONT_GLYPH_COUNT];
    memcpy(font->draw_width, src->draw_width, sizeof(font->draw_width));
    font->line_height = src->line_height;
    memcpy(font->charcodeset, src->charcodeset, sizeof(font->charcodeset));
    if( !ToriDraw_FontValidate(font) )
        goto fail;
    return font;

fail:
    ToriDraw_FontFree(font);
    return NULL;
}

static struct ToriDraw_Font*
InterfaceX_EnsureSceneFont(
    struct InterfaceX_VMHost* host,
    int font_id)
{
    assert(host);
    assert(host->scene);
    assert(host->disk);
    assert(font_id >= 0);

    struct ToriDraw_Font* font = ToriDraw_SceneFontGet(host->scene, font_id);
    if( font )
        return font;

    struct RSCacheDat2Disk_Archive* archive =
        RSCacheDat2Disk_ArchiveNewLoad(host->disk, RSCacheDat2Disk_Table_Fonts, font_id);
    if( !archive )
        return NULL;

    struct ToriAuxLibCore_Font* core =
        ToriAuxLibCache_FontNewFromDat2Archive(host->disk, archive, font_id);
    if( !core )
        return NULL;

    font = InterfaceX_FontNewFromCore(core);
    ToriAuxLibCore_FontFree(core);
    if( !font )
        return NULL;

    ToriDraw_SceneFontAdd(host->scene, font_id, font);
    return font;
}

static int
InterfaceX_ResolveGraphicScene(
    struct InterfaceX_VMHost* host,
    int graphic_id)
{
    assert(host);

    if( graphic_id < 0 )
        return -1;

    for( struct InterfaceX_GraphicSceneCacheEntry* it = host->graphic_scene_cache; it;
         it = it->next )
    {
        if( it->graphic_id == graphic_id )
            return it->scene_id;
    }

    struct RSCacheDat2A_SpritePack* pack =
        RSCacheDat2A_SpritePackNewFromCache(host->disk, graphic_id);
    assert(pack && pack->count > 0 && pack->palette);

    struct RSCacheDat2A_Sprite* frame = &pack->sprites[0];
    int* spr_px = RSCacheDat2A_SpriteGetPixels(frame, pack->palette, 0);
    int sw = frame->width;
    int sh = frame->height;
    int ox = frame->offset_x;
    int oy = frame->offset_y;
    RSCacheDat2A_SpritePackFree(pack);
    assert(spr_px);
    assert(sw > 0);
    assert(sh > 0);

    uint32_t* argb = malloc((size_t)sw * (size_t)sh * sizeof(uint32_t));
    assert(argb);

    for( int i = 0; i < sw * sh; i++ )
        argb[i] = (uint32_t)spr_px[i];
    free(spr_px);

    struct ToriDraw_Sprite* spr = ToriDraw_SpriteNewFromArgbOwned(argb, sw, sh);
    if( !spr )
        return -1;

    spr->crop_x = ox;
    spr->crop_y = oy;

    int scene_id = host->next_scene_id++;
    struct ToriDraw_Sprite** sprites = calloc(1, sizeof(struct ToriDraw_Sprite*));
    if( !sprites )
    {
        ToriDraw_SpriteFree(spr);
        return -1;
    }

    sprites[0] = spr;
    ToriDraw_SceneSpriteAdd(host->scene, scene_id, sprites, 1);

    struct InterfaceX_GraphicSceneCacheEntry* entry =
        calloc(1, sizeof(struct InterfaceX_GraphicSceneCacheEntry));
    if( !entry )
        return scene_id;

    entry->graphic_id = graphic_id;
    entry->scene_id = scene_id;
    entry->next = host->graphic_scene_cache;
    host->graphic_scene_cache = entry;

    return scene_id;
}

static int
InterfaceX_ResolveObjIconCountVariant(
    struct InterfaceX_VMHost* host,
    int obj_id,
    int count)
{
    assert(host);

    struct RSCacheDat2A_ConfigObject* obj = InterfaceX_LoadObjConfig(host->disk, obj_id);
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

    RSCacheDat2A_ConfigObjectFree(obj);
    return resolved;
}

static int
InterfaceX_ResolveObjIconScene(
    struct InterfaceX_VMHost* host,
    int obj_id,
    int count)
{
    assert(host);
    assert(obj_id >= 0);

    int resolved_obj_id = InterfaceX_ResolveObjIconCountVariant(host, obj_id, count);

    for( struct InterfaceX_ObjIconCacheEntry* it = host->obj_icon_cache; it; it = it->next )
    {
        if( it->obj_id == resolved_obj_id )
            return it->scene_id;
    }

    struct RSCacheDat2A_ConfigObject* obj = InterfaceX_LoadObjConfig(host->disk, resolved_obj_id);
    if( !obj )
        return -1;

    struct ToriAuxLibCore_Objtype* objtype =
        ToriAuxLibCache_ObjtypeNewFromDat2ConfigObject(obj, resolved_obj_id);
    RSCacheDat2A_ConfigObjectFree(obj);
    if( !objtype || objtype->inventory_model_id <= 0 )
    {
        if( objtype )
            ToriAuxLibCore_ObjtypeFree(objtype);
        return -1;
    }

    struct RSCacheDat2A_Model* dat2a_model =
        RSCacheDat2A_ModelNewFromCache(host->disk, objtype->inventory_model_id);
    if( !dat2a_model )
    {
        ToriAuxLibCore_ObjtypeFree(objtype);
        return -1;
    }

    struct ToriDraw_Model* td_model = ToriDraw_ModelNewFromCacheModel(dat2a_model);
    RSCacheDat2A_ModelFree(dat2a_model);
    if( !td_model )
    {
        ToriAuxLibCore_ObjtypeFree(objtype);
        return -1;
    }

    if( objtype->resize_x != 128 || objtype->resize_y != 128 || objtype->resize_z != 128 )
        ToriDraw_ModelScale(td_model, objtype->resize_x, objtype->resize_z, objtype->resize_y);

    for( int i = 0; i < objtype->recolor_count; i++ )
        ToriDraw_ModelRecolor(td_model, objtype->recolors_from[i], objtype->recolors_to[i]);

    ToriDraw_ModelSetBoundsCylinder(td_model);

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td_model,
    };
    ToriDraw_LightModelDefaultPreScaled(hnd, objtype->contrast, objtype->ambient);

    int zoom = objtype->zoom2d;
    if( zoom == 0 )
        zoom = 2000;

    struct ToriDraw_Sprite* spr = ToriDraw_SpriteNewFromObjIconRaster(
        host->scene,
        hnd,
        zoom,
        objtype->xan2d,
        objtype->yan2d,
        objtype->zan2d,
        objtype->offset_x2d,
        objtype->offset_y2d,
        36,
        32,
        true);

    ToriDraw_ModelFree(td_model);
    ToriAuxLibCore_ObjtypeFree(objtype);

    if( !spr )
        return -1;

    for( int i = 0; i < spr->width * spr->height; i++ )
    {
        if( spr->pixels_argb[i] != 0 && (spr->pixels_argb[i] >> 24) == 0 )
            spr->pixels_argb[i] |= 0xFF000000u;
    }

    char filename[256];
    snprintf(filename, sizeof(filename), "obj_icon_%d.bmp", resolved_obj_id);
    if( g_interfacex_write_bmp )
        ToriDraw_SpriteWriteBmpFile(spr, filename);

    int scene_id = host->next_scene_id++;
    struct ToriDraw_Sprite** sprites = calloc(1, sizeof(struct ToriDraw_Sprite*));
    assert(sprites);

    sprites[0] = spr;

    ToriDraw_SceneSpriteAdd(host->scene, scene_id, sprites, 1);

    struct InterfaceX_ObjIconCacheEntry* entry =
        calloc(1, sizeof(struct InterfaceX_ObjIconCacheEntry));
    assert(entry);

    entry->obj_id = resolved_obj_id;
    entry->scene_id = scene_id;
    entry->next = host->obj_icon_cache;
    host->obj_icon_cache = entry;

    return scene_id;
}

static int
InterfaceX_ResolveModelScene(
    struct InterfaceX_VMHost* host,
    struct UITreeXNode const* node)
{
    assert(host);
    assert(node);

    if( node->kind != UITreeXNodeKind_RSModel )
        return -1;
    if( node->u.rs_model.model_kind != INTERFACEX_MODEL_KIND_PLAIN )
        return -1;
    if( node->u.rs_model.model_id < 0 )
        return -1;

    int model_id = node->u.rs_model.model_id;
    int zoom = node->u.rs_model.zoom > 0 ? node->u.rs_model.zoom : 2000;
    int angle_x = node->u.rs_model.angle_x;
    int angle_y = node->u.rs_model.angle_y;
    int angle_z = node->u.rs_model.angle_z;
    int offset_x = node->u.rs_model.offset_x;
    int offset_y = node->u.rs_model.offset_y;

    for( struct InterfaceX_ModelSceneCacheEntry* it = host->model_scene_cache; it; it = it->next )
    {
        if( it->model_id == model_id && it->zoom == zoom && it->angle_x == angle_x &&
            it->angle_y == angle_y && it->angle_z == angle_z && it->offset_x == offset_x &&
            it->offset_y == offset_y )
            return it->scene_id;
    }

    struct RSCacheDat2A_Model* dat2a_model = RSCacheDat2A_ModelNewFromCache(host->disk, model_id);
    if( !dat2a_model )
        return -1;

    struct ToriDraw_Model* td_model = ToriDraw_ModelNewFromCacheModel(dat2a_model);
    RSCacheDat2A_ModelFree(dat2a_model);
    if( !td_model )
        return -1;

    ToriDraw_ModelSetBoundsCylinder(td_model);

    struct ToriDraw_ModelHandle hnd = {
        .kind = TORIDRAWMK_MODEL,
        .u.model.model = td_model,
    };
    ToriDraw_LightModelDefaultPreScaled(hnd, 0, 0);

    int width = node->abs_w > 0 ? node->abs_w : 64;
    int height = node->abs_h > 0 ? node->abs_h : 64;

    struct ToriDraw_Sprite* spr = ToriDraw_SpriteNewFromModelRaster(
        host->scene, hnd, zoom, angle_x, angle_y, width, height, false);

    ToriDraw_ModelFree(td_model);
    if( !spr )
        return -1;

    for( int i = 0; i < spr->width * spr->height; i++ )
    {
        if( spr->pixels_argb[i] != 0 && (spr->pixels_argb[i] >> 24) == 0 )
            spr->pixels_argb[i] |= 0xFF000000u;
    }

    int scene_id = host->next_scene_id++;
    struct ToriDraw_Sprite** sprites = calloc(1, sizeof(struct ToriDraw_Sprite*));
    if( !sprites )
    {
        ToriDraw_SpriteFree(spr);
        return -1;
    }

    sprites[0] = spr;
    ToriDraw_SceneSpriteAdd(host->scene, scene_id, sprites, 1);

    struct InterfaceX_ModelSceneCacheEntry* entry =
        calloc(1, sizeof(struct InterfaceX_ModelSceneCacheEntry));
    if( entry )
    {
        entry->model_id = model_id;
        entry->zoom = zoom;
        entry->angle_x = angle_x;
        entry->angle_y = angle_y;
        entry->angle_z = angle_z;
        entry->offset_x = offset_x;
        entry->offset_y = offset_y;
        entry->scene_id = scene_id;
        entry->next = host->model_scene_cache;
        host->model_scene_cache = entry;
    }

    return scene_id;
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
        node->u.rs_obj.obj_id = obj_id;
        node->u.rs_obj.obj_count = count > 0 ? count : 1;
        node->u.rs_obj.scene_id = InterfaceX_ResolveObjIconScene(host, obj_id, count);
        break;
    case UITreeXNodeKind_RSGraphic:
        node->u.rs_graphic.graphic_id = obj_id;
        node->u.rs_graphic.outline = 0;
        node->u.rs_graphic.scene_id = InterfaceX_ResolveObjIconScene(host, obj_id, count);
        break;
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
    int int_arg_count)
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

    int tail = (host->script_queue_head + host->script_queue_count) % INTERFACEX_SCRIPT_QUEUE_MAX;
    struct InterfaceX_ScriptQueueEntry* entry = &host->script_queue[tail];

    entry->script_id = script_id;
    entry->component_id = component_id;
    entry->int_arg_count = int_arg_count;
    if( int_arg_count > 0 && int_args )
        memcpy(entry->int_args, int_args, (size_t)int_arg_count * sizeof(entry->int_args[0]));

    host->script_queue_count++;
}

static void
InterfaceX_VMHost_DrainScriptQueue(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm)
{
    assert(host);
    assert(vm);

    while( host->script_queue_count > 0 )
    {
        struct InterfaceX_ScriptQueueEntry entry = host->script_queue[host->script_queue_head];
        host->script_queue_head = (host->script_queue_head + 1) % INTERFACEX_SCRIPT_QUEUE_MAX;
        host->script_queue_count--;

        (void)InterfaceX_RunClientScript(
            host, vm, entry.script_id, entry.component_id, entry.int_args, entry.int_arg_count);
    }
}

static int
InterfaceX_RunClientScript(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count)
{
    assert(host);
    assert(vm);

    if( script_id <= 0 )
        return CS2VM_EXECNO_OK;

    struct ToriAuxLibCore_ClientScript* client_script =
        InterfaceX_VMHost_ResolveScript(host, script_id);
    if( !client_script )
    {
        fprintf(stderr, "failed to resolve script: %d\n", script_id);
        return CS2VM_EXECNO_ERROR;
    }

    CS2VMX_ResetRuntime(vm);
    CS2VMX_PushCallScript(vm, &client_script->script);
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
            CS2VMX_ResetRuntime(vm);
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

    int entry_size = (int)sizeof(struct MapEntry_ClientScript);
    int buffer_size = ToriDraw_MapBufferSizeFor(entry_size, HOST_SCRIPT_MAP_CAP);
    if( buffer_size > (int)sizeof(host->scripts_buf) )
        return false;

    struct ToriDraw_MapConfig config = {
        .buffer = host->scripts_buf,
        .buffer_size = (size_t)buffer_size,
        .key_size = sizeof(int),
        .entry_size = (size_t)entry_size,
        .capacity = HOST_SCRIPT_MAP_CAP,
    };

    host->scripts = ToriDraw_MapNew(&config, 0);
    if( !host->scripts )
        return false;

    host->script_queue =
        calloc((size_t)INTERFACEX_SCRIPT_QUEUE_MAX, sizeof(struct InterfaceX_ScriptQueueEntry));
    if( !host->script_queue )
        return false;

    return true;
}

struct ToriAuxLibCore_ClientScript*
InterfaceX_VMHost_ResolveScript(
    struct InterfaceX_VMHost* host,
    int script_id)
{
    assert(host);
    assert(script_id >= 0);

    struct MapEntry_ClientScript* entry =
        ToriDraw_MapSearch(host->scripts, &script_id, TORIDRAW_MAP_FIND);

    struct RSCacheDat2Disk_Archive* archive =
        RSCacheDat2Disk_ArchiveNewLoad(host->disk, RSCacheDat2Disk_Table_Clientscript, script_id);
    if( !archive )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadataFromTable(host->clientscript_table, archive);

    struct ToriAuxLibCore_ClientScript* loaded = ToriAuxLibCache_ClientScriptNewFromDat2Archive2(
        archive, script_id, CLIENTSCRIPT_DECODE_TRAILER_LEGACY);
    if( !loaded )
    {
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    entry = ToriDraw_MapSearch(host->scripts, &script_id, TORIDRAW_MAP_INSERT);
    if( !entry )
    {
        ToriAuxLibCore_ClientScriptFree(loaded);
        RSCacheDat2Disk_ArchiveFree(archive);
        return NULL;
    }

    entry->id = script_id;
    entry->script = loaded;

    return loaded;
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

    cs = InterfaceX_VMHost_ResolveScript(host, script_id);
    if( !cs )
    {
        fprintf(stderr, "failed to resolve script for push: %d\n", script_id);
        return CS2VM_EXECNO_ERROR;
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
        struct ToriDraw_Font* font = InterfaceX_EnsureSceneFont(host, request.font_id);
        if( font )
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
        struct ToriDraw_Font* font = InterfaceX_EnsureSceneFont(host, request.font_id);
        if( font )
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

int
InterfaceX_VMHost_Exec_VarsReadVarbit(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_VarsReadVarbit request)
{
    assert(host);
    assert(vm);
    (void)request;

    CS2VMX_PushInt(vm, 0);
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

    if( request.output_type == (int)'s' )
    {
        char const* value = InterfaceX_EnumLookupString(
            host->disk, request.input_type, request.output_type, request.enum_id, request.key);
        return CS2VMX_PushStr(vm, strdup(value ? value : "null"));
    }

    int value = -1;
    if( host->disk )
        value = InterfaceX_EnumLookup(
            host->disk, request.input_type, request.output_type, request.enum_id, request.key);

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

    int count = 0;
    if( host->disk )
        count = InterfaceX_EnumOutputCount(host->disk, request.enum_id);

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

    CS2VMX_PushInt(vm, node->u.rs_layer.scroll_x);
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

    CS2VMX_PushInt(vm, node->u.rs_layer.scroll_y);
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

    CS2VMX_PushInt(vm, node->u.rs_layer.scroll_height);
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

    UITreeX_LayoutResolve(host->tree, CANVAS_W, CANVAS_H);

    int view_w = node->layout_resolved && node->abs_w > 0 ? node->abs_w : node->w;
    int view_h = node->layout_resolved && node->abs_h > 0 ? node->abs_h : node->h;
    if( view_w < 0 )
        view_w = 0;
    if( view_h < 0 )
        view_h = 0;

    int max_x = node->u.rs_layer.scroll_width > view_w ? node->u.rs_layer.scroll_width - view_w : 0;
    int max_y =
        node->u.rs_layer.scroll_height > view_h ? node->u.rs_layer.scroll_height - view_h : 0;

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

    node->u.rs_layer.scroll_x = scroll_x;
    node->u.rs_layer.scroll_y = scroll_y;
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

    bool size_changed = node->u.rs_layer.scroll_width != request.scroll_width ||
                        node->u.rs_layer.scroll_height != request.scroll_height;
    node->u.rs_layer.scroll_width = request.scroll_width;
    node->u.rs_layer.scroll_height = request.scroll_height;

    UITreeX_LayoutResolve(host->tree, CANVAS_W, CANVAS_H);

    int view_w = node->layout_resolved && node->abs_w > 0 ? node->abs_w : node->w;
    int view_h = node->layout_resolved && node->abs_h > 0 ? node->abs_h : node->h;
    if( view_w < 0 )
        view_w = 0;
    if( view_h < 0 )
        view_h = 0;

    int max_x = node->u.rs_layer.scroll_width > view_w ? node->u.rs_layer.scroll_width - view_w : 0;
    int max_y =
        node->u.rs_layer.scroll_height > view_h ? node->u.rs_layer.scroll_height - view_h : 0;

    int scroll_x = node->u.rs_layer.scroll_x;
    int scroll_y = node->u.rs_layer.scroll_y;
    if( scroll_x < 0 )
        scroll_x = 0;
    if( scroll_x > max_x )
        scroll_x = max_x;
    if( scroll_y < 0 )
        scroll_y = 0;
    if( scroll_y > max_y )
        scroll_y = max_y;

    bool scroll_changed =
        scroll_x != node->u.rs_layer.scroll_x || scroll_y != node->u.rs_layer.scroll_y;
    node->u.rs_layer.scroll_x = scroll_x;
    node->u.rs_layer.scroll_y = scroll_y;

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
InterfaceX_EnsureModelNode(struct UITreeXNode* node)
{
    assert(node);
    if( node->kind == UITreeXNodeKind_RSModel )
        return;

    memset(&node->u.rs_model, 0, sizeof(node->u.rs_model));
    node->kind = UITreeXNodeKind_RSModel;
    node->u.rs_model.model_id = -1;
    node->u.rs_model.model_kind = INTERFACEX_MODEL_KIND_PLAIN;
    node->u.rs_model.zoom = 2000;
    node->u.rs_model.scene_id = -1;
}

static void
InterfaceX_ApplyWidgetSetInt(
    struct UITreeXNode* node,
    enum CS2VM_WidgetIntField field,
    int value)
{
    assert(node);

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
            node->u.rs_rect.color = value;
        else if( node->kind == UITreeXNodeKind_RSLine )
            node->u.rs_line.color = value;
        break;
    case CS2VM_WIDGET_INT_LINE_WIDTH:
        if( node->kind == UITreeXNodeKind_RSLine )
            node->u.rs_line.line_width = value > 0 ? value : 1;
        break;
    case CS2VM_WIDGET_INT_LINE_DIRECTION:
        if( node->kind == UITreeXNodeKind_RSLine )
            node->u.rs_line.line_direction = value;
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
        InterfaceX_EnsureModelNode(node);
        node->u.rs_model.anim_seq = value;
        break;
    case CS2VM_WIDGET_INT_MODEL_ORTHOG:
        InterfaceX_EnsureModelNode(node);
        node->u.rs_model.orthog = value;
        InterfaceX_ModelNodeInvalidateScene(node);
        break;
    case CS2VM_WIDGET_INT_MODEL_TRANSPARENT:
        InterfaceX_EnsureModelNode(node);
        node->u.rs_model.transparent = value;
        InterfaceX_ModelNodeInvalidateScene(node);
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

    InterfaceX_ApplyWidgetSetInt(node, request.field, request.value);
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

    InterfaceX_EnsureModelNode(node);
    node->u.rs_model.model_id = request.model_id;
    node->u.rs_model.model_kind = INTERFACEX_MODEL_KIND_PLAIN;
    InterfaceX_ModelNodeInvalidateScene(node);
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

    InterfaceX_EnsureModelNode(node);
    node->u.rs_model.offset_x = request.offset_x;
    node->u.rs_model.offset_y = request.offset_y;
    node->u.rs_model.angle_x = request.angle_x;
    node->u.rs_model.angle_y = request.angle_y;
    node->u.rs_model.angle_z = request.angle_z;
    if( request.zoom > 0 )
        node->u.rs_model.zoom = request.zoom;
    InterfaceX_ModelNodeInvalidateScene(node);
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

    InterfaceX_EnsureModelNode(node);
    node->u.rs_model.model_kind = request.model_kind;
    if( request.model_id >= 0 )
        node->u.rs_model.model_id = request.model_id;
    InterfaceX_ModelNodeInvalidateScene(node);
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

    if( node->kind != UITreeXNodeKind_RSGraphic )
    {
        node->kind = UITreeXNodeKind_RSGraphic;
        node->u.rs_graphic.graphic_id = -1;
        node->u.rs_graphic.graphic_id2 = -1;
        node->u.rs_graphic.scene_id = -1;
    }

    node->u.rs_graphic.outline = request.outline;
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
    if( parent_id == 10551393 )
    {
        printf("Hello");
    }
    int type = request.component_type;
    int child_index = request.child_index;
    (void)request.is_nested;

    int parent_idx = UITreeX_FindByUserId(host->tree, parent_id);
    if( parent_idx < 0 )
        return CS2VM_EXECNO_OK;

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

    if( node->user_id == 10551394 )
    {
        printf("Hello");
    }

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
        break;
    case 3:
        node->kind = UITreeXNodeKind_RSRect;
        node->u.rs_rect.color = 0;
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
        node->u.rs_model.scene_id = -1;
        break;
    case 9:
        node->kind = UITreeXNodeKind_RSLine;
        node->u.rs_line.color = 0;
        node->u.rs_line.line_width = 1;
        node->u.rs_line.line_direction = 1;
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

    if( node->kind != UITreeXNodeKind_RSGraphic )
    {
        node->kind = UITreeXNodeKind_RSGraphic;
        node->u.rs_graphic.scene_id = -1;
    }

    node->u.rs_graphic.graphic_id = request.graphic_id;
    if( request.graphic_id >= 0 )
        node->u.rs_graphic.scene_id = InterfaceX_ResolveGraphicScene(host, request.graphic_id);
    else
        node->u.rs_graphic.scene_id = -1;

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

    if( node->kind != UITreeXNodeKind_RSGraphic )
    {
        node->kind = UITreeXNodeKind_RSGraphic;
        node->u.rs_graphic.graphic_id = -1;
        node->u.rs_graphic.graphic_id2 = -1;
        node->u.rs_graphic.scene_id = -1;
    }

    node->u.rs_graphic.outline = request.outline;
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

    if( node->kind != UITreeXNodeKind_RSGraphic )
    {
        node->kind = UITreeXNodeKind_RSGraphic;
        node->u.rs_graphic.graphic_id = -1;
        node->u.rs_graphic.graphic_id2 = -1;
        node->u.rs_graphic.scene_id = -1;
    }

    node->u.rs_graphic.graphic_shadow = request.shadow;
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
    if( !node || node->kind != UITreeXNodeKind_RSRect )
        return CS2VM_EXECNO_OK;

    node->u.rs_rect.color = request.colour;
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
    if( !node || node->kind != UITreeXNodeKind_RSRect )
        return CS2VM_EXECNO_OK;

    node->u.rs_rect.filled = request.filled != 0;
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

    node->u.rs_text.font_id = request.font_id;
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

    strncpy(
        node->u.rs_text.text, request.text ? request.text : "", sizeof(node->u.rs_text.text) - 1);
    node->u.rs_text.text[sizeof(node->u.rs_text.text) - 1] = '\0';
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

    node->u.rs_text.center = request.x_align;
    node->u.rs_text.y_align = request.y_align;
    node->u.rs_text.line_height = request.line_height;
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

    node->u.rs_text.shadowed = request.shadowed != 0;
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

static bool
InterfaceX_ConfigArchiveReady(
    struct RSCacheDat2Disk* disk,
    int config_kind)
{
    assert(disk);
    assert(config_kind >= 0);

    struct RSCacheDat2Disk_ReferenceTable* table = disk->tables[RSCacheDat2Disk_Table_Configs];
    if( !table || config_kind >= table->archive_count )
        return false;

    return true;
}

static bool
InterfaceX_ConfigArchiveFindFile(
    struct RSCacheDat2Disk* disk,
    int config_kind,
    struct RSCacheShared_FileList* fl,
    int file_id,
    uint8_t const** out_data,
    int* out_len)
{
    assert(disk);
    assert(fl);
    assert(file_id >= 0);

    struct RSCacheDat2Disk_ReferenceTable* table = disk->tables[RSCacheDat2Disk_Table_Configs];
    struct RSCacheDat2Disk_ArchiveReference* ref = NULL;
    if( table && table->archives )
        ref = &table->archives[config_kind];

    for( int i = 0; i < fl->file_count; i++ )
    {
        int id = (ref && i < ref->children.count) ? ref->children.files[i].id : i;
        if( id != file_id )
            continue;
        if( out_data )
            *out_data = (uint8_t const*)fl->files[i];
        if( out_len )
            *out_len = fl->file_sizes[i];
        return true;
    }
    return false;
}

#define INTERFACEX_CONFIG_ARCHIVE_CACHE_CAP 4

struct InterfaceX_ConfigArchiveCacheEntry
{
    struct RSCacheDat2Disk* disk;
    int config_kind;
    struct RSCacheShared_FileList* file_list;
};

static struct InterfaceX_ConfigArchiveCacheEntry
    s_config_archive_cache[INTERFACEX_CONFIG_ARCHIVE_CACHE_CAP];
static int s_config_archive_cache_count = 0;

static void
InterfaceX_ConfigArchiveCacheFreeAll(void)
{
    for( int i = 0; i < s_config_archive_cache_count; i++ )
        RSCacheShared_FileListFree(s_config_archive_cache[i].file_list);
    s_config_archive_cache_count = 0;
}

static struct RSCacheShared_FileList*
InterfaceX_ConfigArchiveGetFileList(
    struct RSCacheDat2Disk* disk,
    int config_kind)
{
    if( !disk || config_kind < 0 )
        return NULL;

    for( int i = 0; i < s_config_archive_cache_count; i++ )
    {
        if( s_config_archive_cache[i].disk == disk &&
            s_config_archive_cache[i].config_kind == config_kind )
            return s_config_archive_cache[i].file_list;
    }

    if( !InterfaceX_ConfigArchiveReady(disk, config_kind) )
        return NULL;

    struct RSCacheDat2Disk_Archive* arch =
        RSCacheDat2Disk_ArchiveNewLoad(disk, RSCacheDat2Disk_Table_Configs, config_kind);
    if( !arch )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadata(disk, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    RSCacheDat2Disk_ArchiveFree(arch);
    if( !fl )
        return NULL;

    if( s_config_archive_cache_count >= INTERFACEX_CONFIG_ARCHIVE_CACHE_CAP )
    {
        RSCacheShared_FileListFree(fl);
        return NULL;
    }

    s_config_archive_cache[s_config_archive_cache_count].disk = disk;
    s_config_archive_cache[s_config_archive_cache_count].config_kind = config_kind;
    s_config_archive_cache[s_config_archive_cache_count].file_list = fl;
    s_config_archive_cache_count++;

    return fl;
}

struct InterfaceX_EnumCacheEntry
{
    int enum_id;
    bool output_is_string;
    int default_int;
    char* default_string;
    int* keys;
    int* int_values;
    char** string_values;
    int count;
};

static struct InterfaceX_EnumCacheEntry* s_enum_cache;
static int s_enum_cache_count;
static int s_enum_cache_cap;

static void
InterfaceX_EnumCacheEntryFree(struct InterfaceX_EnumCacheEntry* entry)
{
    if( !entry )
        return;
    free(entry->keys);
    free(entry->int_values);
    free(entry->default_string);
    if( entry->string_values )
    {
        for( int i = 0; i < entry->count; i++ )
            free(entry->string_values[i]);
        free(entry->string_values);
    }
    memset(entry, 0, sizeof(*entry));
}

static void
InterfaceX_DecodeEnumConfig(
    uint8_t const* data,
    int len,
    struct InterfaceX_EnumCacheEntry* entry)
{
    assert(entry);
    assert(data);
    assert(len > 0);

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, len);

    int key_cap = 0;
    int* keys = NULL;
    int* int_values = NULL;
    char** string_values = NULL;
    int count = 0;

    for( ;; )
    {
        int opcode = g1(&buf);
        if( opcode == 0 )
            break;
        switch( opcode )
        {
        case 1:
            (void)g1(&buf);
            break;
        case 2:
            entry->output_is_string = g1(&buf) == (int)'s';
            break;
        case 3:
        {
            char* s = gcstring(&buf);
            free(entry->default_string);
            entry->default_string = s;
            break;
        }
        case 4:
            entry->default_int = g4(&buf);
            break;
        case 5:
        {
            int size = g2(&buf);
            for( int i = 0; i < size; i++ )
            {
                int key = g4(&buf);
                char* value = gcstring(&buf);
                if( count >= key_cap )
                {
                    int new_cap = key_cap < 8 ? 8 : key_cap * 2;
                    int* new_keys = realloc(keys, (size_t)new_cap * sizeof(int));
                    char** new_strings = realloc(string_values, (size_t)new_cap * sizeof(char*));
                    if( !new_keys || !new_strings )
                    {
                        free(value);
                        goto decode_fail;
                    }
                    keys = new_keys;
                    string_values = new_strings;
                    key_cap = new_cap;
                }
                keys[count] = key;
                string_values[count] = value;
                count++;
            }
            break;
        }
        case 6:
        {
            int size = g2(&buf);
            for( int i = 0; i < size; i++ )
            {
                int key = g4(&buf);
                int value = g4(&buf);
                if( count >= key_cap )
                {
                    int new_cap = key_cap < 8 ? 8 : key_cap * 2;
                    int* new_keys = realloc(keys, (size_t)new_cap * sizeof(int));
                    int* new_values = realloc(int_values, (size_t)new_cap * sizeof(int));
                    if( !new_keys || !new_values )
                        goto decode_fail;
                    keys = new_keys;
                    int_values = new_values;
                    key_cap = new_cap;
                }
                keys[count] = key;
                int_values[count] = value;
                count++;
            }
            break;
        }
        case 7:
        {
            int size = g2(&buf);
            for( int i = 0; i < size; i++ )
            {
                (void)g4(&buf);
                (void)g8(&buf);
            }
            break;
        }
        case 8:
            (void)g8(&buf);
            break;
        default:
            break;
        }
    }

    entry->keys = keys;
    entry->int_values = int_values;
    entry->string_values = string_values;
    entry->count = count;
    return;

decode_fail:
    free(keys);
    free(int_values);
    if( string_values )
    {
        for( int i = 0; i < count; i++ )
            free(string_values[i]);
        free(string_values);
    }
}

static struct InterfaceX_EnumCacheEntry*
InterfaceX_EnumCacheGet(
    struct RSCacheDat2Disk* disk,
    int enum_id)
{
    for( int i = 0; i < s_enum_cache_count; i++ )
    {
        if( s_enum_cache[i].enum_id == enum_id )
            return &s_enum_cache[i];
    }

    struct RSCacheShared_FileList* fl =
        InterfaceX_ConfigArchiveGetFileList(disk, RSCacheDat2A_ConfigKind_Enum);
    if( !fl )
        return NULL;

    uint8_t const* data = NULL;
    int data_len = 0;
    if( enum_id >= 0 )
    {
        (void)InterfaceX_ConfigArchiveFindFile(
            disk, RSCacheDat2A_ConfigKind_Enum, fl, enum_id, &data, &data_len);
    }

    struct InterfaceX_EnumCacheEntry entry = {
        .enum_id = enum_id,
        .default_int = -1,
    };
    InterfaceX_DecodeEnumConfig(data, data_len, &entry);

    if( s_enum_cache_count >= s_enum_cache_cap )
    {
        int new_cap = s_enum_cache_cap < 8 ? 8 : s_enum_cache_cap * 2;
        struct InterfaceX_EnumCacheEntry* grown =
            realloc(s_enum_cache, (size_t)new_cap * sizeof(*s_enum_cache));
        if( !grown )
        {
            InterfaceX_EnumCacheEntryFree(&entry);
            return NULL;
        }
        s_enum_cache = grown;
        s_enum_cache_cap = new_cap;
    }

    s_enum_cache[s_enum_cache_count++] = entry;
    return &s_enum_cache[s_enum_cache_count - 1];
}

static int
InterfaceX_EnumLookup(
    struct RSCacheDat2Disk* disk,
    int input_type,
    int output_type,
    int enum_id,
    int key)
{
    (void)input_type;
    (void)output_type;
    if( !disk || enum_id < 0 )
        return -1;

    if( enum_id == 139 && key == 10551394 )
        return (165 << 16) | 1;

    struct InterfaceX_EnumCacheEntry* entry = InterfaceX_EnumCacheGet(disk, enum_id);
    if( !entry )
        return -1;
    if( entry->output_is_string )
        return -1;
    if( !entry->keys || entry->count <= 0 )
        return entry->default_int;

    for( int i = 0; i < entry->count; i++ )
    {
        if( entry->keys[i] == key )
            return entry->int_values ? entry->int_values[i] : -1;
    }
    return entry->default_int;
}

static char const*
InterfaceX_EnumLookupString(
    struct RSCacheDat2Disk* disk,
    int input_type,
    int output_type,
    int enum_id,
    int key)
{
    (void)input_type;
    (void)output_type;
    if( !disk || enum_id < 0 )
        return "null";

    struct InterfaceX_EnumCacheEntry* entry = InterfaceX_EnumCacheGet(disk, enum_id);
    if( !entry || !entry->output_is_string )
        return "null";
    if( !entry->keys || entry->count <= 0 )
        return entry->default_string ? entry->default_string : "null";

    for( int i = 0; i < entry->count; i++ )
    {
        if( entry->keys[i] == key )
        {
            if( entry->string_values && entry->string_values[i] )
                return entry->string_values[i];
            return entry->default_string ? entry->default_string : "null";
        }
    }
    return entry->default_string ? entry->default_string : "null";
}

static int
InterfaceX_EnumOutputCount(
    struct RSCacheDat2Disk* disk,
    int enum_id)
{
    if( !disk || enum_id < 0 )
        return 0;

    struct InterfaceX_EnumCacheEntry* entry = InterfaceX_EnumCacheGet(disk, enum_id);
    return entry ? entry->count : 0;
}

struct InterfaceX_StructCacheEntry
{
    int struct_id;
    struct RSCacheShared_Params params;
};

static struct InterfaceX_StructCacheEntry* s_struct_cache;
static int s_struct_cache_count;
static int s_struct_cache_cap;

static struct InterfaceX_StructCacheEntry*
InterfaceX_StructCacheGet(
    struct RSCacheDat2Disk* disk,
    int struct_id)
{
    for( int i = 0; i < s_struct_cache_count; i++ )
    {
        if( s_struct_cache[i].struct_id == struct_id )
            return &s_struct_cache[i];
    }

    struct RSCacheShared_FileList* fl =
        InterfaceX_ConfigArchiveGetFileList(disk, RSCacheDat2A_ConfigKind_Struct);
    if( !fl )
        return NULL;

    uint8_t const* data = NULL;
    int data_len = 0;
    if( struct_id >= 0 )
    {
        (void)InterfaceX_ConfigArchiveFindFile(
            disk, RSCacheDat2A_ConfigKind_Struct, fl, struct_id, &data, &data_len);
    }

    struct InterfaceX_StructCacheEntry entry = {
        .struct_id = struct_id,
    };
    if( data && data_len > 0 && !(data_len == 1 && data[0] == 0) )
    {
        struct RSCacheShared_RSBuffer buf;
        RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, (uint32_t)data_len);
        for( ;; )
        {
            int opcode = g1(&buf);
            if( opcode == 0 )
                break;
            if( opcode == 249 )
                RSCacheShared_RSBufferReadParams(&buf, &entry.params);
        }
    }

    if( s_struct_cache_count >= s_struct_cache_cap )
    {
        int new_cap = s_struct_cache_cap < 8 ? 8 : s_struct_cache_cap * 2;
        struct InterfaceX_StructCacheEntry* grown =
            realloc(s_struct_cache, (size_t)new_cap * sizeof(*s_struct_cache));
        if( !grown )
            return NULL;
        s_struct_cache = grown;
        s_struct_cache_cap = new_cap;
    }

    s_struct_cache[s_struct_cache_count++] = entry;
    return &s_struct_cache[s_struct_cache_count - 1];
}

static bool
InterfaceX_StructParamLookup(
    struct RSCacheDat2Disk* disk,
    int struct_id,
    int param_id,
    bool* out_is_string,
    int* out_int,
    char const** out_str)
{
    if( out_is_string )
        *out_is_string = false;
    if( out_int )
        *out_int = 0;
    if( out_str )
        *out_str = NULL;
    if( !disk || struct_id < 0 || param_id < 0 )
        return false;

    struct InterfaceX_StructCacheEntry* entry = InterfaceX_StructCacheGet(disk, struct_id);
    if( !entry || entry->params.count <= 0 )
        return false;

    for( int i = 0; i < entry->params.count; i++ )
    {
        if( entry->params.keys[i] != param_id )
            continue;
        if( entry->params.is_string[i] )
        {
            if( out_is_string )
                *out_is_string = true;
            if( out_str )
                *out_str = (char const*)entry->params.values[i];
            return true;
        }
        if( out_int )
            *out_int = entry->params.values[i] ? *(int*)entry->params.values[i] : 0;
        return true;
    }
    return false;
}

static char
InterfaceX_ParamTypeIdToChar(int id)
{
    if( id == 36 )
        return 's';
    return 'i';
}

static void
InterfaceX_DecodeParamType(
    uint8_t const* data,
    int len,
    struct InterfaceX_ParamType* out)
{
    if( !out || !data || len <= 0 || (len == 1 && data[0] == 0) )
        return;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, (uint32_t)len);

    for( ;; )
    {
        int opcode = g1(&buf);
        if( opcode == 0 )
            break;
        switch( opcode )
        {
        case 1:
            out->is_string = g1(&buf) == 's';
            break;
        case 8:
            out->is_string = InterfaceX_ParamTypeIdToChar(g1(&buf)) == 's';
            break;
        case 2:
            out->default_int = g4(&buf);
            break;
        case 5:
            free(out->default_string);
            out->default_string = gcstring(&buf);
            break;
        default:
            break;
        }
    }
}

static bool
InterfaceX_LoadParamType(
    struct RSCacheDat2Disk* disk,
    int param_id,
    struct InterfaceX_ParamType* out)
{
    assert(out);
    memset(out, 0, sizeof(*out));
    out->id = param_id;

    if( !disk || param_id < 0 )
        return false;

    struct RSCacheShared_FileList* fl =
        InterfaceX_ConfigArchiveGetFileList(disk, RSCacheDat2A_ConfigKind_Params);
    if( !fl )
        return false;

    uint8_t const* data = NULL;
    int data_len = 0;
    bool found = InterfaceX_ConfigArchiveFindFile(
        disk, RSCacheDat2A_ConfigKind_Params, fl, param_id, &data, &data_len);

    if( found )
        InterfaceX_DecodeParamType(data, data_len, out);

    return found;
}

static struct InterfaceX_ObjType*
InterfaceX_LoadObjType(
    struct RSCacheDat2Disk* disk,
    int item_id)
{
    if( !disk || item_id <= 0 )
        return NULL;

    struct RSCacheShared_FileList* fl =
        InterfaceX_ConfigArchiveGetFileList(disk, RSCacheDat2A_ConfigKind_Object);
    if( !fl )
        return NULL;

    uint8_t const* data = NULL;
    int data_len = 0;
    if( !InterfaceX_ConfigArchiveFindFile(
            disk, RSCacheDat2A_ConfigKind_Object, fl, item_id, &data, &data_len) )
        return NULL;

    struct RSCacheDat2A_ConfigObject* decoded = calloc(1, sizeof(struct RSCacheDat2A_ConfigObject));
    if( !decoded )
        return NULL;

    RSCacheDat2A_ConfigObjectDecodeInplace(decoded, (char*)data, data_len);

    struct InterfaceX_ObjType* obj = calloc(1, sizeof(struct InterfaceX_ObjType));
    if( !obj )
    {
        RSCacheDat2A_ConfigObjectFree(decoded);
        return NULL;
    }

    obj->id = item_id;
    obj->param_count = decoded->params.count;
    if( obj->param_count > 0 )
    {
        obj->params = calloc((size_t)obj->param_count, sizeof(struct InterfaceX_ObjTypeParam));
        if( !obj->params )
        {
            RSCacheDat2A_ConfigObjectFree(decoded);
            free(obj);
            return NULL;
        }

        for( int i = 0; i < obj->param_count; i++ )
        {
            struct InterfaceX_ObjTypeParam* dst = &obj->params[i];
            dst->key = decoded->params.keys[i];
            dst->is_string = decoded->params.is_string[i];
            if( dst->is_string )
            {
                char const* src =
                    decoded->params.values[i] ? (char const*)decoded->params.values[i] : "";
                dst->str_value = src[0] != '\0' ? strdup(src) : NULL;
            }
            else
            {
                dst->int_value = decoded->params.values[i] ? *(int*)decoded->params.values[i] : 0;
            }
        }
    }

    RSCacheDat2A_ConfigObjectFree(decoded);
    return obj;
}

static struct InterfaceX_ObjTypeParam*
InterfaceX_ObjTypeParamGet(
    struct InterfaceX_ObjType const* obj,
    int param_id)
{
    if( !obj || !obj->params || obj->param_count <= 0 )
        return NULL;

    for( int i = 0; i < obj->param_count; i++ )
    {
        if( obj->params[i].key == param_id )
            return &obj->params[i];
    }
    return NULL;
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

    struct InterfaceX_ParamType param;
    bool have_param = false;
    struct InterfaceX_ObjType* obj = NULL;
    struct InterfaceX_ObjTypeParam* val = NULL;

    if( host->disk )
    {
        have_param = InterfaceX_LoadParamType(host->disk, request.param_id, &param);
        obj = InterfaceX_LoadObjType(host->disk, request.item_id);
        val = obj ? InterfaceX_ObjTypeParamGet(obj, request.param_id) : NULL;
    }

    if( have_param && obj && obj->params && obj->param_count > 0 )
    {
        if( param.is_string )
        {
            char const* pushed = "";
            if( val && val->is_string && val->str_value )
                pushed = val->str_value;
            else if( param.default_string )
                pushed = param.default_string;
            CS2VMX_PushStr(vm, (char*)pushed);
        }
        else
        {
            int pushed = param.default_int;
            if( val && !val->is_string )
                pushed = val->int_value;
            CS2VMX_PushInt(vm, pushed);
        }
    }
    else
    {
        if( have_param && param.is_string )
        {
            CS2VMX_PushStr(vm, param.default_string ? param.default_string : (char*)"");
        }
        else
        {
            CS2VMX_PushInt(vm, have_param ? param.default_int : 0);
        }
    }

    return CS2VM_EXECNO_OK;
}

int
InterfaceX_VMHost_Exec_OC_Name(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_OC_Name request)
{
    assert(host);
    assert(vm);

    char* name = strdup("null");
    struct RSCacheDat2A_ConfigObject* obj = InterfaceX_LoadObjConfig(host->disk, request.item_id);
    assert(obj);
    if( obj->name && obj->name[0] != '\0' )
        name = strdup(obj->name);
    RSCacheDat2A_ConfigObjectFree(obj);
    return CS2VMX_PushStr(vm, name);
}

int
InterfaceX_VMHost_Exec_OC_Unplaceholder(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    struct CS2VM_HostRequest_OC_Unplaceholder request)
{
    assert(host);
    assert(vm);

    int result = request.item_id;
    if( request.item_id <= 0 || !host->disk )
        return CS2VMX_PushInt(vm, result);

    struct RSCacheDat2A_ConfigObject* obj = InterfaceX_LoadObjConfig(host->disk, request.item_id);
    if( obj )
    {
        if( obj->placeholder_template_id >= 0 && obj->placeholder_id >= 0 )
            result = obj->placeholder_id;
        RSCacheDat2A_ConfigObjectFree(obj);
    }

    return CS2VMX_PushInt(vm, result);
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
    default:
        printf("VMHost: unknown request kind: %d\n", request->kind);
        return CS2VM_EXECNO_ERROR;
    }
}

static void
CS2VMX_ResetRuntime(struct CS2VMX* vm)
{
    assert(vm);
    vm->ints_stack_top = 0;
    vm->strs_stack_top = 0;
    vm->frame_sp = 0;
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
            "running onVarTransmit script %d for component %d\n",
            hook->script_id,
            hook->component_id);

        InterfaceX_VMHost_QueueScript(host, hook->script_id, hook->component_id, NULL, 0);
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
        InterfaceX_VMHost_QueueScript(host, script_id, comp->id, args, arg_count);
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
            host, hook->script_id, hook->component_id, hook->int_args, hook->int_arg_count);
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
        InterfaceX_VMHost_QueueScript(host, script_id, comp->id, args, arg_count);
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
        "usage: %s [--list] [--no-bmp] [--cs2-trace] [--cs2-trace-all] [interface_id]\n"
        "  default interface_id: %d (inventory)\n"
        "  --cs2-trace: log targeting opcode trace to stderr (CS2TRACE lines)\n"
        "  --cs2-trace-all: log every opcode to stderr\n",
        argv0,
        INVENTORY_INTERFACE);
}

int
main(
    int argc,
    char** argv)
{
    int interface_id = INVENTORY_INTERFACE;
    bool list_only = false;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--list") == 0 )
            list_only = true;
        else if( strcmp(argv[i], "--no-bmp") == 0 )
            g_interfacex_write_bmp = 0;
        else if( strcmp(argv[i], "--cs2-trace") == 0 )
            g_cs2_trace_mode = 1;
        else if( strcmp(argv[i], "--cs2-trace-all") == 0 )
            g_cs2_trace_mode = 2;
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
    struct RSCacheDat2Disk_Archive* archive = NULL;
    struct RSCacheDat2Disk_ReferenceTable* reference_table = NULL;
    RSCacheDat2A_Component* component = NULL;
    struct ToriAuxLibCore_Component* component_core = NULL;
    struct ToriAuxLibCore_Component** components = NULL;
    struct UITreeX* tree = NULL;
    struct UITreeXBuilder* builder = NULL;
    struct RSCacheShared_FileList* file_list = NULL;
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
    vmhost.disk = cache;
    vmhost.clientscript_table = cache->tables[RSCacheDat2Disk_Table_Clientscript];

    if( !InterfaceX_VMHost_Init(&vmhost) )
    {
        fprintf(stderr, "failed to init host script cache\n");
        return 1;
    }

    CS2VMX_BindHost(&vm, &vmhost, InterfaceX_VMHost_Exec);

    reference_table = cache->tables[RSCacheDat2Disk_Table_Interfaces];
    if( !reference_table )
    {
        fprintf(stderr, "failed to load reference table: %d\n", interface_id);
        return 1;
    }

    archive = RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Interfaces, interface_id);
    if( !archive )
    {
        fprintf(stderr, "failed to load archive: %d\n", interface_id);
        return 1;
    }

    RSCacheDat2Disk_ArchiveInitMetadataFromTable(reference_table, archive);

    file_list = RSCacheShared_FileListNewFromCacheArchive(archive);
    if( !file_list )
    {
        fprintf(stderr, "failed to create file list: %d\n", interface_id);
        return 1;
    }

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0);
    if( !scene )
    {
        fprintf(stderr, "failed to create scene: %d\n", interface_id);
        return 1;
    }

    // Layout loading loop
    tree = UITreeX_New();
    builder = calloc(1, sizeof(struct UITreeXBuilder));
    UITreeXBuilder_Init(builder, tree);

    vmhost.builder = builder;
    vmhost.tree = tree;
    vmhost.interface_id = interface_id;
    vmhost.scene = scene;
    vmhost.next_scene_id = 1;
    InterfaceX_InvStoreSeedDefaults(&vmhost);

    components = calloc(file_list->file_count, sizeof(struct ToriAuxLibCore_Component*));
    for( int i = 0; i < file_list->file_count; i++ )
    {
        int packed_id = (interface_id << 16) | (i & 0xFFFF);
        component =
            component_decode_from_bytes(packed_id, file_list->files[i], file_list->file_sizes[i]);
        if( !component )
        {
            fprintf(stderr, "failed to decode component: %d (file %d)\n", interface_id, i);
            return 1;
        }

        component_core = ToriAuxLibCache_ComponentNewFromCacheDat2Component(component);
        if( !component_core )
        {
            fprintf(stderr, "failed to create component core: %d (file %d)\n", interface_id, i);
            return 1;
        }

        components[i] = component_core;
    }

    for( int i = 0; i < file_list->file_count; i++ )
    {
        component_core = components[i];
        process_component(builder, component_core);
    }

    UITreeXBuilder_ResolvePendingParents(builder);

    reference_table = cache->tables[RSCacheDat2Disk_Table_Clientscript];
    for( int i = 0; i < file_list->file_count; i++ )
    {
        component_core = components[i];

        int script_id = component_core->on_load.argv[0];
        if( script_id <= 0 )
            continue;

        int args[TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX];
        int arg_count = component_core->on_load.argc - 1;
        if( arg_count > TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX )
            arg_count = TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX;
        for( int j = 0; j < arg_count; j++ )
            args[j] = component_core->on_load.argv[j + 1];

        InterfaceX_VMHost_QueueScript(&vmhost, script_id, component_core->id, args, arg_count);
    }

    InterfaceX_VMHost_DrainScriptQueue(&vmhost, &vm);

    InterfaceX_VMHost_FireVarTransmitHooks(&vmhost, &vm);
    InterfaceX_VMHost_DrainScriptQueue(&vmhost, &vm);

    InterfaceX_VMHost_FireCacheVarTransmitHooks(&vmhost, &vm, components, file_list->file_count);
    InterfaceX_VMHost_DrainScriptQueue(&vmhost, &vm);

    InterfaceX_VMHost_FireInvTransmitHooks(&vmhost, &vm);
    InterfaceX_VMHost_DrainScriptQueue(&vmhost, &vm);

    InterfaceX_VMHost_FireCacheInvTransmitHooks(&vmhost, &vm, components, file_list->file_count);
    InterfaceX_VMHost_DrainScriptQueue(&vmhost, &vm);

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

    UITreeX_Render(&vmhost, cache, tree, pixels);

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
    InterfaceX_ConfigArchiveCacheFreeAll();
    return 0;
}