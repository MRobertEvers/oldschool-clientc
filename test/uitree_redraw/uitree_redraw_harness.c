/*
 * Deterministic retained-UITree parity and performance harness.
 *
 * This file deliberately lives outside src/.  Its Makefile links this one
 * driver against either origin/v3 or the candidate SOURCE_ROOT, making the
 * fixture and transition stream identical while the implementation changes.
 */

#include "engine/torirs_debug_font_baked.h"
#include "engine/uitree_cmd_render.h"
#include "ui/uitree.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_host.h"
#include "ui/uitree_layout.h"

/* The candidate-owned pure projection leaf is also used by App. The harness
 * source is shared between SOURCE_ROOT builds, so both sides receive the same
 * camera/orbit input generator while their UITree implementations differ. */
#include "../../src/render/torirs_world_projection.h"

#include "bmp.h"
#include "toridraw.h"
#include "toridraw_font.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "world/world_pickset.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef CANDIDATE
#define CANDIDATE 0
#endif

#define CANVAS_W 640
#define CANVAS_H 420
#define FONT_SCENE_ID 1
#define SPRITE_SCENE_ID 100
#define FIXTURE_GROUP 0x6A0
#define BULK_GROUP 0x6A1
#define ORBIT_ANCHOR_X 4160
#define ORBIT_ANCHOR_Y (-58)
#define ORBIT_ANCHOR_Z 4160
#define FISH_WORLD_X 5312
#define FISH_WORLD_Y 0
#define FISH_WORLD_Z 4160
#define ORBIT_PITCH 128
#define ORBIT_DISTANCE 984
#define UID(group, child) (((group) << 16) | ((child) & 0xFFFF))
#define ARRAY_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* torirs_frame/torirs_pick link the real world boundary.  This harness never
 * attaches a World or PaintersBuffer, so these are total, deliberately inert
 * shims identical in spirit to test-debug-overlay-visual. */
struct World;
struct WorldEntity_Scenery;
struct WorldEntity_NPC;
struct WorldEntity_Player;
struct WorldEntity_ObjStack;

int
World_TerrainElementAt(struct World* world, int x, int z, int level)
{
    (void)world;
    (void)x;
    (void)z;
    (void)level;
    return -1;
}

struct WorldEntity_Scenery*
World_SceneryGetByElementId(struct World* world, int element_id)
{
    (void)world;
    (void)element_id;
    return NULL;
}

struct WorldEntity_NPC*
World_NpcGetByElementId(struct World* world, int element_id, int* out_index)
{
    (void)world;
    (void)element_id;
    (void)out_index;
    return NULL;
}

struct WorldEntity_Player*
World_PlayerGetByElementId(struct World* world, int element_id)
{
    (void)world;
    (void)element_id;
    return NULL;
}

struct WorldEntity_ObjStack*
World_ObjStackGetByElementId(struct World* world, int element_id)
{
    (void)world;
    (void)element_id;
    return NULL;
}

bool
WorldEntity_SceneryPickInactive(void)
{
    return false;
}

int
World_LocPaintLevel(struct World const* world, int x, int z, int cache_level)
{
    (void)world;
    (void)x;
    (void)z;
    return cache_level;
}

int
World_TerrainDrawLevel(struct World const* world, int x, int z, int mesh_level)
{
    (void)world;
    (void)x;
    (void)z;
    return mesh_level;
}

void
World_PickSetReset(struct World_PickSet* pickset)
{
    (void)pickset;
}

void
World_PickSetAdd(
    struct World_PickSet* pickset,
    int element_id,
    enum World_PickType type,
    int tile_x,
    int tile_z,
    int tile_level)
{
    (void)pickset;
    (void)element_id;
    (void)type;
    (void)tile_x;
    (void)tile_z;
    (void)tile_level;
}

/* ------------------------------------------------------------------------- */

struct Options
{
    enum
    {
        MODE_VISUAL,
        MODE_BENCH,
    } mode;
    char const* out_dir;
    int retention;
    int frames;
    uint32_t seed;
};

struct HostState
{
    int camera_yaw;
    int cs1_active;
    int active_component_id;
    int overlay_x;
    int overlay_y;
    int overlay_w;
    int overlay_h;
    int overlay_count;
    struct UITreeEntityOverlay overlay_items[2];
    struct ToriDraw_Font* font;
};

struct EmitDriver
{
    struct UITreeEmitBuffer emit;
#if CANDIDATE
    struct UITreeEmitRetainGate gate;
#else
    /* Exact origin/v3 App-local production gate. */
    uint32_t legacy_dirty_gen;
    uint32_t legacy_layout_resolve_seq;
    uint32_t legacy_tree_generation;
    int legacy_hovered_component_id;
    uint8_t legacy_primed;
#endif
    uint64_t full_walks;
    uint64_t retained_frames;
};

struct Fixture
{
    struct UITree* tree;
    struct UITreeHost host;
    struct HostState hs;
    struct EmitDriver driver;
    uint32_t seed;
    int hovered_component_id;
    struct ToriDraw_Camera projection_camera;
    struct ToriDraw_Position projection_eye;

    int32_t root;
    int32_t compass;
    int32_t scroll;
    int32_t hover_rect;
    int32_t content_text;
    int32_t alpha_rect;
    int32_t arc;
    int32_t rotating_sprite;
    int32_t topology_layer;
    int32_t hidden_layer;
    int32_t active_rect;
    int32_t overlay_builtin;
    int32_t fish_layer;
    int32_t fish_sprite;

    int scroll_id;
    int hover_id;
    int content_id;
    int alpha_id;
    int arc_id;
    int rotating_sprite_id;
    int hidden_layer_id;
    int active_rect_id;
};

struct BenchMetric
{
    char const* scenario;
    uint64_t elapsed_ns;
    uint64_t operations;
    uint64_t full_walks;
    uint64_t retained_frames;
    uint64_t emit_nodes;
};

static void
fatal(char const* message)
{
    fprintf(stderr, "uitree_redraw_harness: %s\n", message);
    exit(2);
}

static void*
xcalloc(size_t count, size_t size)
{
    void* p = calloc(count, size);
    if( !p )
        fatal("out of memory");
    return p;
}

static uint32_t
prng_next(uint32_t* state)
{
    uint32_t x = *state;
    if( x == 0 )
        x = 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint64_t
now_ns(void)
{
    struct timespec ts;
    if( clock_gettime(CLOCK_MONOTONIC, &ts) != 0 )
        fatal("clock_gettime failed");
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static int
mkdir_one(char const* path)
{
    if( mkdir(path, 0777) == 0 || errno == EEXIST )
        return 0;
    return -1;
}

static void
mkdir_p(char const* path)
{
    char* copy;
    size_t n;

    if( !path || !path[0] )
        fatal("empty output directory");
    n = strlen(path);
    copy = xcalloc(n + 1, 1);
    memcpy(copy, path, n + 1);
    for( size_t i = 1; i < n; i++ )
    {
        if( copy[i] != '/' )
            continue;
        copy[i] = '\0';
        if( copy[0] && mkdir_one(copy) != 0 )
        {
            free(copy);
            fatal("could not create output directory parent");
        }
        copy[i] = '/';
    }
    if( mkdir_one(copy) != 0 )
    {
        free(copy);
        fatal("could not create output directory");
    }
    free(copy);
}

static void
path_join(char* out, size_t cap, char const* dir, char const* leaf)
{
    int n = snprintf(out, cap, "%s/%s", dir, leaf);
    if( n < 0 || (size_t)n >= cap )
        fatal("output path too long");
}

/* Fixed-endian FNV-1a.  Never hash C structs: the candidate legitimately adds
 * fields/padding, and host pointers differ between processes. */
static uint64_t
hash_byte(uint64_t h, uint8_t byte)
{
    return (h ^ byte) * UINT64_C(1099511628211);
}

static uint64_t
hash_u32(uint64_t h, uint32_t value)
{
    h = hash_byte(h, (uint8_t)(value >> 0));
    h = hash_byte(h, (uint8_t)(value >> 8));
    h = hash_byte(h, (uint8_t)(value >> 16));
    h = hash_byte(h, (uint8_t)(value >> 24));
    return h;
}

static uint64_t
hash_i32(uint64_t h, int value)
{
    return hash_u32(h, (uint32_t)value);
}

static uint64_t
hash_text(uint64_t h, char const* text)
{
    if( !text )
        return hash_byte(h, 0);
    while( *text )
        h = hash_byte(h, (uint8_t)*text++);
    return hash_byte(h, 0);
}

static uint64_t
pixel_hash(int const* pixels, int width, int height)
{
    uint64_t h = UINT64_C(1469598103934665603);
    h = hash_i32(h, width);
    h = hash_i32(h, height);
    for( int i = 0; i < width * height; i++ )
        h = hash_u32(h, (uint32_t)pixels[i]);
    return h;
}

static uint64_t
emit_hash(struct UITreeEmitBuffer const* buf)
{
    uint64_t h = UINT64_C(1469598103934665603);
    h = hash_i32(h, buf->count);
    for( int i = 0; i < buf->count; i++ )
    {
        struct UITreeEmitDesc const* d = &buf->cmds[i];
        char const* text = d->text_formatted[0] ? d->text_formatted : d->text;
        h = hash_i32(h, (int)d->kind);
        h = hash_i32(h, d->component_id);
        h = hash_i32(h, d->x);
        h = hash_i32(h, d->y);
        h = hash_i32(h, d->w);
        h = hash_i32(h, d->h);
        h = hash_i32(h, d->clip.x);
        h = hash_i32(h, d->clip.y);
        h = hash_i32(h, d->clip.w);
        h = hash_i32(h, d->clip.h);
        h = hash_i32(h, d->scroll_off_x);
        h = hash_i32(h, d->scroll_off_y);
        h = hash_i32(h, d->scroll_content);
        h = hash_i32(h, d->scene_id);
        h = hash_i32(h, d->atlas_index);
        h = hash_i32(h, d->mask_scene_id);
        h = hash_i32(h, d->mask_atlas_index);
        h = hash_i32(h, d->mask_keep_opaque);
        h = hash_i32(h, d->font_id);
        h = hash_i32(h, d->color);
        h = hash_i32(h, d->filled);
        h = hash_i32(h, d->rotation_r2pi2048);
        h = hash_i32(h, d->sprite_angle_r2pi65536);
        h = hash_i32(h, d->src_anchor_x);
        h = hash_i32(h, d->src_anchor_y);
        h = hash_i32(h, d->model_id);
        h = hash_i32(h, d->model_zoom);
        h = hash_i32(h, d->model_xan);
        h = hash_i32(h, d->model_yan);
        h = hash_i32(h, d->model_zan);
        h = hash_i32(h, d->model_x_offset);
        h = hash_i32(h, d->model_y_offset);
        h = hash_i32(h, d->model_orthog);
        h = hash_i32(h, d->model_fixed_zoom);
        h = hash_text(h, text);
        h = hash_i32(h, d->text_center);
        h = hash_i32(h, d->text_y_align);
        h = hash_i32(h, d->text_shadowed);
        h = hash_i32(h, d->text_line_height);
        h = hash_i32(h, d->text_baseline);
        h = hash_i32(h, d->if3);
        h = hash_i32(h, d->tiled);
        h = hash_i32(h, d->outline);
        h = hash_i32(h, d->graphic_shadow);
        h = hash_i32(h, d->trans);
        h = hash_i32(h, d->flip_h);
        h = hash_i32(h, d->flip_v);
        h = hash_i32(h, d->line_width);
        h = hash_i32(h, d->line_direction);
        h = hash_i32(h, d->arc_start);
        h = hash_i32(h, d->arc_end);
        h = hash_i32(h, d->entity_overlay_count);
        for( int j = 0; j < d->entity_overlay_count && d->entity_overlays; j++ )
        {
            struct UITreeEntityOverlay const* item = &d->entity_overlays[j];
            h = hash_i32(h, item->kind);
            h = hash_i32(h, item->x);
            h = hash_i32(h, item->y);
            h = hash_i32(h, item->w);
            h = hash_i32(h, item->h);
            h = hash_u32(h, item->color);
            h = hash_i32(h, item->scene_id);
            h = hash_i32(h, item->atlas_index);
            h = hash_i32(h, item->font_id);
            h = hash_i32(h, item->trans);
            h = hash_text(h, item->text);
        }
    }
    return h;
}

/* ------------------------------------------------------------------------- */

static int
host_request(void* user, struct UITreeHostRequest* req)
{
    struct HostState* hs = user;
    switch( req->kind )
    {
    case UITREE_HOST_GET_CAMERA_YAW:
        return hs->camera_yaw;
    case UITREE_HOST_SCENE_SPRITE_HAS:
        return req->u.scene_sprite_has.scene_id == SPRITE_SCENE_ID;
    case UITREE_HOST_SCENE_FONT_HAS:
        return req->u.scene_font_has.font_id == FONT_SCENE_ID;
    case UITREE_HOST_SCENE_MODEL_HAS:
        return 0;
    case UITREE_HOST_MEASURE_TEXT:
        if( req->u.measure_text.font_id != FONT_SCENE_ID || !hs->font )
            return 0;
        return ToriDraw2D_MeasureString(hs->font, req->u.measure_text.text);
    case UITREE_HOST_GET_ENTITY_OVERLAYS:
        if( req->u.get_entity_overlays.out_items )
            *req->u.get_entity_overlays.out_items = hs->overlay_items;
        if( req->u.get_entity_overlays.out_clip_x )
            *req->u.get_entity_overlays.out_clip_x = hs->overlay_x;
        if( req->u.get_entity_overlays.out_clip_y )
            *req->u.get_entity_overlays.out_clip_y = hs->overlay_y;
        if( req->u.get_entity_overlays.out_clip_w )
            *req->u.get_entity_overlays.out_clip_w = hs->overlay_w;
        if( req->u.get_entity_overlays.out_clip_h )
            *req->u.get_entity_overlays.out_clip_h = hs->overlay_h;
        return hs->overlay_count;
    case UITREE_HOST_GET_CANVAS_OVERLAYS:
    case UITREE_HOST_GET_FRAME_OVERLAYS:
        if( req->u.get_entity_overlays.out_items )
            *req->u.get_entity_overlays.out_items = NULL;
        return 0;
    case UITREE_HOST_GET_ROLE_OVERLAY_GROUPS:
        if( req->u.get_role_overlay_groups.out_groups )
            *req->u.get_role_overlay_groups.out_groups = NULL;
        if( req->u.get_role_overlay_groups.out_anchor_seen )
            *req->u.get_role_overlay_groups.out_anchor_seen = 0;
        return 0;
    case UITREE_HOST_IS_ACTIVE:
        return hs->cs1_active && req->u.is_active.component &&
               req->u.is_active.component->component_id == hs->active_component_id;
    case UITREE_HOST_EVAL_TEXT_PLACEHOLDER:
    case UITREE_HOST_GET_SELECTED_TAB:
    case UITREE_HOST_GET_CROSS_ACTIVE:
    case UITREE_HOST_GET_CROSS_ATLAS_FRAME:
    case UITREE_HOST_GET_CROSS_POSITION:
    case UITREE_HOST_GET_MINIMENU_VISIBLE:
    case UITREE_HOST_GET_MINIMENU_STATE:
    case UITREE_HOST_GET_HOVERTEXT_STATE:
    case UITREE_HOST_GET_MINIMAP_HIDDEN:
    case UITREE_HOST_GET_MULTIWAY:
    case UITREE_HOST_GET_REBOOT_TIMER:
    case UITREE_HOST_GET_MINIMAP_DOTS:
    case UITREE_HOST_BEGIN_OVERLAYS:
    case UITREE_HOST_GET_DEBUG_OVERLAY:
        return 0;
    case UITREE_HOST_GET_SCROLLBAR_SCENE:
    case UITREE_HOST_GET_STATIC_SPRITE_SCENE:
    case UITREE_HOST_GET_MINIMAP_STATE:
    case UITREE_HOST_GET_INV_COUNT_FONT:
        return -1;
    default:
        return 0;
    }
}

static int32_t
push_xy(
    struct UITree* tree,
    int32_t parent,
    enum UITreeComponentType type,
    int component_id,
    int x,
    int y,
    int w,
    int h)
{
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = type;
    spec.component_id = component_id;
    spec.x = x;
    spec.y = y;
    spec.width = w;
    spec.height = h;
    return UITree_Push(tree, parent, &spec);
}

static int32_t
push_rect(
    struct UITree* tree,
    int32_t parent,
    int id,
    int x,
    int y,
    int w,
    int h,
    int color,
    int filled)
{
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_RECT;
    spec.component_id = id;
    spec.x = x;
    spec.y = y;
    spec.width = w;
    spec.height = h;
    spec.u.rs_rect.color = color;
    spec.u.rs_rect.filled = filled;
    return UITree_Push(tree, parent, &spec);
}

static int32_t
push_text(
    struct UITree* tree,
    int32_t parent,
    int id,
    int x,
    int y,
    int w,
    int h,
    char const* text,
    int color,
    int center)
{
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_TEXT;
    spec.component_id = id;
    spec.x = x;
    spec.y = y;
    spec.width = w;
    spec.height = h;
    spec.u.rs_text.font_id = FONT_SCENE_ID;
    spec.u.rs_text.text = text;
    spec.u.rs_text.color = color;
    spec.u.rs_text.center = center;
    spec.u.rs_text.y_align = 1;
    spec.u.rs_text.shadowed = 1;
    spec.u.rs_text.line_height = 16;
    return UITree_Push(tree, parent, &spec);
}

static int32_t
push_graphic(
    struct UITree* tree,
    int32_t parent,
    int id,
    int x,
    int y,
    int w,
    int h,
    int atlas,
    int if3)
{
    struct UITreeNodeSpec spec;
    int32_t idx;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_GRAPHIC;
    spec.component_id = id;
    spec.x = x;
    spec.y = y;
    spec.width = w;
    spec.height = h;
    spec.u.rs_graphic.scene_id = SPRITE_SCENE_ID;
    spec.u.rs_graphic.atlas_index = atlas;
    idx = UITree_Push(tree, parent, &spec);
    tree->components[idx].if3 = if3 ? 1 : 0;
    return idx;
}

static int32_t
push_line(
    struct UITree* tree,
    int32_t parent,
    int id,
    int x,
    int y,
    int w,
    int h,
    int color,
    int width,
    int direction)
{
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LINE;
    spec.component_id = id;
    spec.x = x;
    spec.y = y;
    spec.width = w;
    spec.height = h;
    spec.u.rs_line.color = color;
    spec.u.rs_line.line_width = width;
    spec.u.rs_line.horizontal = direction;
    return UITree_Push(tree, parent, &spec);
}

static int32_t
push_arc(
    struct UITree* tree,
    int32_t parent,
    int id,
    int x,
    int y,
    int w,
    int h,
    int color,
    int start,
    int end)
{
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_ARC;
    spec.component_id = id;
    spec.x = x;
    spec.y = y;
    spec.width = w;
    spec.height = h;
    spec.u.rs_arc.color = color;
    spec.u.rs_arc.filled = 1;
    spec.u.rs_arc.line_width = 1;
    spec.u.rs_arc.arc_start = start;
    spec.u.rs_arc.arc_end = end;
    return UITree_Push(tree, parent, &spec);
}

static void
emit_driver_init(struct EmitDriver* driver)
{
    memset(driver, 0, sizeof(*driver));
    UITree_EmitBufferInit(&driver->emit);
}

static void
emit_driver_free(struct EmitDriver* driver)
{
    UITree_EmitBufferFree(&driver->emit);
    memset(driver, 0, sizeof(*driver));
}

static void
emit_primary(struct Fixture* fx, int retention)
{
    struct EmitDriver* d = &fx->driver;
    int retained = 0;

    UITree_EnsureLayout(fx->tree);
#if CANDIDATE
    if( retention )
    {
        int quiet = UITree_EmitRetainGateQuiet(
            fx->tree,
            &fx->host,
            &d->emit,
            fx->hovered_component_id,
            &d->gate);
        if( quiet && d->emit.count > 0 && !d->emit.volatile_unrefreshable )
        {
            retained = UITree_EmitRetainGateRefreshVolatile(
                fx->tree,
                &fx->host,
                &d->emit,
                &fx->hovered_component_id,
                &d->gate);
        }
    }
#else
    if( retention )
    {
        /* Match origin/v3 App exactly: capture immediately after evaluating
         * the legacy tree/layout/hover predicate, then refresh volatile
         * descriptors or rebuild. Host state is intentionally absent; the
         * four-way visual lane records the stale result this permits. */
        int const quiet =
            d->legacy_primed && !fx->tree->layout_stale &&
            fx->tree->generation == d->legacy_tree_generation &&
            fx->tree->dirty_gen == d->legacy_dirty_gen &&
            fx->tree->layout_resolve_seq == d->legacy_layout_resolve_seq &&
            fx->hovered_component_id == d->legacy_hovered_component_id;

        d->legacy_dirty_gen = fx->tree->dirty_gen;
        d->legacy_layout_resolve_seq = fx->tree->layout_resolve_seq;
        d->legacy_tree_generation = fx->tree->generation;
        d->legacy_hovered_component_id = fx->hovered_component_id;
        d->legacy_primed = 1;
        if( quiet && d->emit.count > 0 && !d->emit.volatile_unrefreshable )
        {
            retained = !d->emit.volatile_refs ||
                       UITree_EmitRefreshVolatile(fx->tree, &fx->host, &d->emit);
        }
    }
#endif
    if( retained )
    {
        d->retained_frames++;
    }
    else
    {
        d->emit.count = 0;
        UITree_EmitWalk(fx->tree, &fx->host, &d->emit, fx->hovered_component_id);
        d->full_walks++;
    }
#if CANDIDATE
    /* Match App: capture after the refresh or walk publication fence. */
    UITree_EmitRetainGateCapture(
        fx->tree, &d->emit, fx->hovered_component_id, &d->gate);
#endif
}

static void
set_transparency(struct Fixture* fx, int32_t idx, int value)
{
    struct UITreeComponent* c = &fx->tree->components[idx];
    if( c->trans == value )
        return;
    c->trans = value;
    UITree_MarkNodeDirty(fx->tree, idx);
}

static void
set_arc_angles(struct Fixture* fx, int start, int end)
{
    struct UITreeComponent* c = &fx->tree->components[fx->arc];
    if( c->u.rs_arc.arc_start == start && c->u.rs_arc.arc_end == end )
        return;
    c->u.rs_arc.arc_start = start;
    c->u.rs_arc.arc_end = end;
    UITree_MarkNodeDirty(fx->tree, fx->arc);
}

static void
set_projection_hidden(struct Fixture* fx, int hidden)
{
#if CANDIDATE
    if( !UITree_SetProjectionHiddenAt(fx->tree, fx->fish_layer, hidden) )
        fatal("candidate projection mutation rejected fish layer");
#else
    struct UITreeComponent* c = &fx->tree->components[fx->fish_layer];
    hidden = hidden ? 1 : 0;
    if( c->behavior.hide != hidden )
    {
        c->behavior.hide = (uint8_t)hidden;
        UITree_MarkNodeVisibilityDirty(fx->tree, fx->fish_layer);
    }
#endif
}

static void
set_camera_yaw(struct Fixture* fx, int yaw)
{
    if( fx->hs.camera_yaw == yaw )
        return;
    fx->hs.camera_yaw = yaw;
#if CANDIDATE
    UITree_HostInputsChanged(
        &fx->host, UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_CAMERA));
#endif
}

static void
set_cs1_active(struct Fixture* fx, int active)
{
    active = active ? 1 : 0;
    if( fx->hs.cs1_active == active )
        return;
    fx->hs.cs1_active = active;
#if CANDIDATE
    UITree_HostInputsChanged(
        &fx->host, UITREE_HOST_INPUT_BIT(UITREE_HOST_INPUT_CLIENT_STATE));
#endif
}

static void
set_volatile_overlay_count(struct Fixture* fx, int count)
{
    if( count < 0 )
        count = 0;
    if( count > ARRAY_COUNT(fx->hs.overlay_items) )
        count = ARRAY_COUNT(fx->hs.overlay_items);
    /* Deliberately no epoch bump: these arrays have same-frame lifetime and
     * are exactly what UITree_EmitRefreshVolatile must reissue in place. */
    fx->hs.overlay_count = count;
}

static int
project_fish_at_yaw(struct Fixture* fx, int yaw, int* out_screen_x, int* out_screen_y)
{
    int screen_x = 0;
    int screen_y = 0;
    int visible;

    set_camera_yaw(fx, yaw);
    fx->projection_camera.yaw = yaw & 0x7ff;
    ToriRS_OrbitCameraEye(
        ORBIT_ANCHOR_X,
        ORBIT_ANCHOR_Y,
        ORBIT_ANCHOR_Z,
        fx->projection_camera.pitch,
        fx->projection_camera.yaw,
        ORBIT_DISTANCE,
        &fx->projection_eye);
    visible = ToriRS_WorldProjectPoint(
        &fx->projection_camera,
        &fx->projection_eye,
        fx->hs.overlay_x,
        fx->hs.overlay_y,
        fx->hs.overlay_w,
        fx->hs.overlay_h,
        50,
        FISH_WORLD_X,
        FISH_WORLD_Y,
        FISH_WORLD_Z,
        &screen_x,
        &screen_y);

    if( visible )
    {
        set_projection_hidden(fx, 0);
        /* Match App's ABOVE-band placement, then convert the absolute projected
         * point into the overlay parent's local coordinates. */
        if( !UITree_EntityOverlaySetLayerPosition(
                fx->tree,
                fx->fish_layer,
                screen_x - fx->tree->components[fx->fish_layer].position.width / 2 -
                    fx->hs.overlay_x,
                screen_y - fx->tree->components[fx->fish_layer].position.height -
                    fx->hs.overlay_y) )
            fatal("projected fishing-layer position rejected");
    }
    else
        set_projection_hidden(fx, 1);

    if( out_screen_x )
        *out_screen_x = screen_x;
    if( out_screen_y )
        *out_screen_y = screen_y;
    return visible;
}

static void
verify_projection_contract(void)
{
    static int const yaw[] = { 1408, 1536, 1664, 1792, 512 };
    static int const expected_x[] = { 106, 219, 332, 456, 0 };
    static int const expected_y[] = { 121, 117, 121, 137, 0 };
    struct ToriDraw_Camera camera = {
        .proj_mode = TORIDRAW_PROJ_MODE_SCALE,
        .proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT,
        .pitch = ORBIT_PITCH,
    };

    for( int i = 0; i < ARRAY_COUNT(yaw); i++ )
    {
        struct ToriDraw_Position eye = { 0 };
        int x = 0;
        int y = 0;
        int visible;

        camera.yaw = yaw[i];
        ToriRS_OrbitCameraEye(
            ORBIT_ANCHOR_X,
            ORBIT_ANCHOR_Y,
            ORBIT_ANCHOR_Z,
            camera.pitch,
            camera.yaw,
            ORBIT_DISTANCE,
            &eye);
        visible = ToriRS_WorldProjectPoint(
            &camera,
            &eye,
            18,
            62,
            402,
            300,
            50,
            FISH_WORLD_X,
            FISH_WORLD_Y,
            FISH_WORLD_Z,
            &x,
            &y);
        if( i == ARRAY_COUNT(yaw) - 1 )
        {
            if( visible )
                fatal("projection golden: behind-camera fish was visible");
        }
        else if( !visible || x != expected_x[i] || y != expected_y[i] )
            fatal("projection golden: orbit result drifted from App integer math");
    }
}

static void
configure_dynamic_rect(struct Fixture* fx, int32_t idx, int x, int y, int color)
{
    int id = fx->tree->components[idx].component_id;
    if( !UITree_ApplyPosition(fx->tree, id, x, y) ||
        !UITree_ApplySize(fx->tree, id, 48, 22) ||
        !UITree_ApplyColour(fx->tree, id, color) )
        fatal("dynamic topology mutation failed");
    if( !fx->tree->components[idx].u.rs_rect.filled )
    {
        fx->tree->components[idx].u.rs_rect.filled = 1;
        UITree_MarkNodeDirty(fx->tree, idx);
    }
}

static struct Fixture*
fixture_new(uint32_t seed, int bulk_nodes)
{
    struct Fixture* fx = xcalloc(1, sizeof(*fx));
    struct UITreeBehavior hover_behavior;
    uint32_t rng = seed;
    int idn = 1;

    fx->tree = UITree_New((uint32_t)(128 + bulk_nodes));
    if( !fx->tree )
        fatal("UITree_New failed");
    fx->hovered_component_id = -1;
    fx->seed = seed;
    fx->hs.camera_yaw = 128;
    fx->hs.overlay_x = 18;
    fx->hs.overlay_y = 62;
    fx->hs.overlay_w = 402;
    fx->hs.overlay_h = 300;
    fx->hs.font = ToriRSChromeFont_Body();
    fx->hs.overlay_items[0].kind = UITREE_ENTITY_OVERLAY_RECT;
    fx->hs.overlay_items[0].x = 44;
    fx->hs.overlay_items[0].y = 326;
    fx->hs.overlay_items[0].w = 92;
    fx->hs.overlay_items[0].h = 8;
    fx->hs.overlay_items[0].color = 0xFF22C55Eu;
    fx->hs.overlay_items[1].kind = UITREE_ENTITY_OVERLAY_LINE;
    fx->hs.overlay_items[1].x = 148;
    fx->hs.overlay_items[1].y = 312;
    fx->hs.overlay_items[1].w = 74;
    fx->hs.overlay_items[1].h = 26;
    fx->hs.overlay_items[1].color = 0xFFFFD166u;
    fx->hs.overlay_items[1].line_width = 3;
    fx->projection_camera.proj_mode = TORIDRAW_PROJ_MODE_SCALE;
    fx->projection_camera.proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT;
    fx->projection_camera.fov_rpi2048 = TORIDRAW_PROJ_FOV_DEFAULT;
    fx->projection_camera.near_plane_z = 50;
    fx->projection_camera.pitch = ORBIT_PITCH;
    fx->projection_camera.yaw = fx->hs.camera_yaw;
    UITree_HostInit(&fx->host);
    fx->host.user = &fx->hs;
    fx->host.request = host_request;
    emit_driver_init(&fx->driver);

    UITree_LayoutSetRootSize(CANVAS_W, CANVAS_H);
    fx->root = push_xy(
        fx->tree, -1, UIELEM_RS_LAYER, UID(FIXTURE_GROUP, idn++), 0, 0, CANVAS_W, CANVAS_H);
    push_rect(
        fx->tree, fx->root, UID(FIXTURE_GROUP, idn++), 0, 0, CANVAS_W, CANVAS_H,
        0x16222B, 1);
    push_rect(
        fx->tree, fx->root, UID(FIXTURE_GROUP, idn++), 8, 8, 624, 42, 0x293E50, 1);
    push_text(
        fx->tree, fx->root, UID(FIXTURE_GROUP, idn++), 20, 12, 500, 32,
        "UITree retained redraw laboratory", 0xF4D35E, 0);

    /* Camera-dependent custom compass. */
    {
        struct UITreeNodeSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.type = UIELEM_BUILTIN_COMPASS;
        spec.component_id = UID(FIXTURE_GROUP, idn++);
        spec.x = 570;
        spec.y = 10;
        spec.width = 40;
        spec.height = 36;
        spec.u.sprite.scene_id = SPRITE_SCENE_ID;
        spec.u.sprite.atlas_index = 0;
        fx->compass = UITree_Push(fx->tree, fx->root, &spec);
    }

    /* Main clipped panel with nested scrolling and mixed primitives. */
    {
        int32_t panel = push_xy(
            fx->tree, fx->root, UIELEM_RS_LAYER, UID(FIXTURE_GROUP, idn++),
            18, 62, 402, 300);
        push_rect(
            fx->tree, panel, UID(FIXTURE_GROUP, idn++), 0, 0, 402, 300, 0x25333C, 1);
        push_rect(
            fx->tree, panel, UID(FIXTURE_GROUP, idn++), 8, 8, 386, 34, 0x0B7285, 1);
        push_text(
            fx->tree, panel, UID(FIXTURE_GROUP, idn++), 14, 8, 370, 32,
            "Nested clip + scroll viewport", 0xFFFFFF, 0);

        fx->scroll_id = UID(FIXTURE_GROUP, idn++);
        fx->scroll = push_xy(
            fx->tree, panel, UIELEM_RS_LAYER, fx->scroll_id, 12, 50, 270, 220);
        fx->tree->components[fx->scroll].u.rs_layer.scroll_width = 330;
        fx->tree->components[fx->scroll].u.rs_layer.scroll_height = 590;
        push_rect(
            fx->tree, fx->scroll, UID(FIXTURE_GROUP, idn++), 0, 0, 330, 590, 0x11181E, 1);
        for( int row = 0; row < 14; row++ )
        {
            int color = (row & 1) ? 0x314550 : 0x263943;
            char label[48];
            snprintf(label, sizeof(label), "scroll row %02d / deterministic", row);
            push_rect(
                fx->tree, fx->scroll, UID(FIXTURE_GROUP, idn++), 6, 8 + row * 40,
                300, 32, color, 1);
            push_text(
                fx->tree, fx->scroll, UID(FIXTURE_GROUP, idn++), 14, 8 + row * 40,
                270, 32, label, row == 7 ? 0x5EEAD4 : 0xDCE7EC, 0);
        }
        /* Oversized children prove the parent clip, not authored dimensions,
         * controls the final raster. */
        push_line(
            fx->tree, fx->scroll, UID(FIXTURE_GROUP, idn++), -20, 94, 370, 54,
            0xFF7A90, 3, 0);
        push_arc(
            fx->tree, fx->scroll, UID(FIXTURE_GROUP, idn++), 218, 150, 70, 70,
            0x23C9FF, 0, 43000);
    }

    /* Hover, opacity, shape and sprite transforms outside the scroll region. */
    fx->hover_id = UID(FIXTURE_GROUP, idn++);
    fx->hover_rect = push_rect(
        fx->tree, fx->root, fx->hover_id, 438, 72, 180, 44, 0x334155, 1);
    memset(&hover_behavior, 0, sizeof(hover_behavior));
    hover_behavior.button_type = 1;
    hover_behavior.over_color = 0xE879F9;
    UITree_SetBehavior(fx->tree, fx->hover_rect, &hover_behavior);
    push_text(
        fx->tree, fx->root, UID(FIXTURE_GROUP, idn++), 448, 78, 160, 30,
        "hover target", 0xFFFFFF, 1);

    push_rect(
        fx->tree, fx->root, UID(FIXTURE_GROUP, idn++), 438, 130, 180, 70, 0x5B2333, 1);
    fx->alpha_id = UID(FIXTURE_GROUP, idn++);
    fx->alpha_rect = push_rect(
        fx->tree, fx->root, fx->alpha_id, 456, 146, 148, 58, 0x33D6A6, 1);
    set_transparency(fx, fx->alpha_rect, 92);

    fx->arc_id = UID(FIXTURE_GROUP, idn++);
    fx->arc = push_arc(
        fx->tree, fx->root, fx->arc_id, 444, 218, 66, 66, 0xF97316, 0, 38000);
    push_line(
        fx->tree, fx->root, UID(FIXTURE_GROUP, idn++), 520, 222, 92, 54,
        0xA7F3D0, 4, 1);
    fx->rotating_sprite_id = UID(FIXTURE_GROUP, idn++);
    fx->rotating_sprite = push_graphic(
        fx->tree, fx->root, fx->rotating_sprite_id, 532, 286, 64, 54, 0, 1);

    fx->active_rect_id = UID(FIXTURE_GROUP, idn++);
    fx->active_rect = push_rect(
        fx->tree, fx->root, fx->active_rect_id, 430, 350, 90, 48, 0x475569, 1);
    {
        struct UITreeBehavior active_behavior;
        memset(&active_behavior, 0, sizeof(active_behavior));
        active_behavior.active_color = 0xEF4444;
        UITree_SetBehavior(fx->tree, fx->active_rect, &active_behavior);
        fx->hs.active_component_id = fx->active_rect_id;
    }

    fx->content_id = UID(FIXTURE_GROUP, idn++);
    fx->content_text = push_text(
        fx->tree, fx->root, fx->content_id, 526, 350, 106, 48,
        "state A", 0xF8FAFC, 1);

    fx->topology_layer = push_xy(
        fx->tree, fx->root, UIELEM_RS_LAYER, UID(FIXTURE_GROUP, idn++),
        294, 278, 116, 74);
    push_rect(
        fx->tree, fx->topology_layer, UID(FIXTURE_GROUP, idn++), 0, 0, 116, 74,
        0x1E293B, 1);
    {
        int32_t dynamic = UITree_CcCreate(
            fx->tree,
            fx->topology_layer,
            fx->tree->components[fx->topology_layer].component_id,
            3,
            0);
        configure_dynamic_rect(fx, dynamic, 8, 8, 0x60A5FA);
    }

    /* A previously unreachable child exercises unhide/reachability identity. */
    fx->hidden_layer_id = UID(FIXTURE_GROUP, idn++);
    fx->hidden_layer = push_xy(
        fx->tree,
        fx->root,
        UIELEM_RS_LAYER,
        fx->hidden_layer_id,
        294,
        365,
        116,
        42);
    push_rect(
        fx->tree,
        fx->hidden_layer,
        UID(FIXTURE_GROUP, idn++),
        0,
        0,
        116,
        42,
        0xFACC15,
        1);
    (void)UITree_ApplyHide(fx->tree, fx->hidden_layer_id, 1);

    /* Script-created fishing marker: ordinary graphic under a dynamic layer
     * owned by the entity-overlay builtin.  Camera projection moves the layer;
     * the fish is clipped by both its 64x52 layer and the world-like viewport. */
    fx->overlay_builtin = push_xy(
        fx->tree,
        fx->root,
        UIELEM_BUILTIN_ENTITY_OVERLAY,
        UID(FIXTURE_GROUP, idn++),
        fx->hs.overlay_x,
        fx->hs.overlay_y,
        fx->hs.overlay_w,
        fx->hs.overlay_h);
    fx->fish_layer = UITree_EntityOverlayCreateLayer(fx->tree, 7, 64, 52);
    if( fx->fish_layer < 0 ||
        !UITree_EntityOverlaySetLayerPosition(fx->tree, fx->fish_layer, 168, 116) )
        fatal("could not create scripted fish overlay layer");
    fx->fish_sprite = UITree_CcCreate(
        fx->tree,
        fx->fish_layer,
        fx->tree->components[fx->fish_layer].component_id,
        5,
        0);
    if( fx->fish_sprite < 0 )
        fatal("could not create scripted fish sprite");
    {
        int fish_id = fx->tree->components[fx->fish_sprite].component_id;
        if( !UITree_ApplyPosition(fx->tree, fish_id, 8, 9) ||
            !UITree_ApplySize(fx->tree, fish_id, 48, 32) ||
            !UITree_ApplyGraphic(fx->tree, fish_id, SPRITE_SCENE_ID, 1) )
            fatal("could not configure scripted fish sprite");
    }
    (void)project_fish_at_yaw(fx, 1536, NULL, NULL);

    /* Thousands of actual retained components for benchmark mode. */
    if( bulk_nodes > 0 )
    {
        int32_t bulk = push_xy(
            fx->tree, fx->root, UIELEM_RS_LAYER, UID(BULK_GROUP, 0), 4, 4, 628, 408);
        fx->tree->components[bulk].u.rs_layer.scroll_width = 628;
        fx->tree->components[bulk].u.rs_layer.scroll_height = 1000;
        for( int i = 0; i < bulk_nodes; i++ )
        {
            uint32_t r = prng_next(&rng);
            int x = (i % 64) * 10;
            int y = (i / 64) * 8;
            int color = (int)(0x202020u | (r & 0x5F5F5Fu));
            push_rect(
                fx->tree, bulk, UID(BULK_GROUP, i + 1), x, y, 9, 7, color, 1);
        }
    }

    UITree_LayoutInvalidateBoxes(fx->tree);
    UITree_LayoutResolve(fx->tree, 0, 0, CANVAS_W, CANVAS_H);
    return fx;
}

static void
fixture_free(struct Fixture* fx)
{
    if( !fx )
        return;
    emit_driver_free(&fx->driver);
    UITree_Free(fx->tree);
    free(fx);
}

static struct ToriDraw_Scene*
scene_new(void)
{
    struct ToriDraw_Scene* scene =
        ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    struct ToriDraw_Sprite** sprites;

    if( !scene )
        fatal("could not allocate ToriDraw scene");
    /* The baked face is static.  The process deliberately does not free this
     * scene, because SceneFree owns registered fonts and would free the bake. */
    ToriDraw_SceneFontAdd(scene, FONT_SCENE_ID, ToriRSChromeFont_Body());

    sprites = xcalloc(2, sizeof(*sprites));
    {
        int w = 32, h = 32;
        uint32_t* p = xcalloc((size_t)w * (size_t)h, sizeof(*p));
        for( int y = 0; y < h; y++ )
            for( int x = 0; x < w; x++ )
            {
                int dx = x - 16;
                int dy = y - 16;
                int rr = dx * dx + dy * dy;
                if( rr <= 210 )
                    p[y * w + x] = rr > 165 ? 0xFFE2E8F0u : 0xFF0F766Eu;
                if( x >= 15 && x <= 17 && y >= 3 && y <= 16 )
                    p[y * w + x] = 0xFFFFD166u;
            }
        sprites[0] = ToriDraw_SpriteNewFromArgbOwned(p, w, h);
    }
    {
        int w = 48, h = 32;
        uint32_t* p = xcalloc((size_t)w * (size_t)h, sizeof(*p));
        for( int y = 0; y < h; y++ )
            for( int x = 0; x < w; x++ )
            {
                int body = ((x - 25) * (x - 25)) * 5 +
                           ((y - 16) * (y - 16)) * 12 <= 1250;
                int tail = x < 13 && x >= 3 && y >= 7 + x / 2 && y <= 25 - x / 2;
                if( body || tail )
                    p[y * w + x] = 0xFF2DD4BFu;
                if( body && (x < 10 || x > 42 || y < 8 || y > 24) )
                    p[y * w + x] = 0xFF0E7490u;
            }
        p[12 * w + 36] = 0xFFFFFFFFu;
        p[12 * w + 37] = 0xFF111827u;
        sprites[1] = ToriDraw_SpriteNewFromArgbOwned(p, w, h);
    }
    ToriDraw_SceneSpriteAdd(scene, SPRITE_SCENE_ID, sprites, 2);
    return scene;
}

/* ------------------------------------------------------------------------- */

static void
visual_transition(
    struct Fixture* fx,
    int frame,
    char const** out_scenario,
    char const** out_checkpoint)
{
    int step = frame % 24;
    int cycle = frame / 24;
    uint32_t value = fx->seed ^ ((uint32_t)frame * UINT32_C(0x9E3779B9));
    value = prng_next(&value);
    *out_checkpoint = "checkpoint";

    switch( step )
    {
    case 0:
        *out_scenario = "initial";
        *out_checkpoint = "initial";
        break;
    case 1:
        *out_scenario = "steady";
        *out_checkpoint = "steady";
        break;
    case 2:
        *out_scenario = "hover";
        *out_checkpoint = "hover_on";
        fx->hovered_component_id = fx->hover_id;
        break;
    case 3:
        *out_scenario = "hover";
        *out_checkpoint = "hover_off";
        fx->hovered_component_id = -1;
        break;
    case 4:
        *out_scenario = "scroll";
        *out_checkpoint = "scroll_a";
        (void)UITree_ApplyScrollPos(
            fx->tree, fx->scroll_id, 3 + (int)(value % 56), 17 + (int)((value >> 9) % 320));
        break;
    case 5:
        *out_scenario = "scroll";
        *out_checkpoint = "scroll_b";
        (void)UITree_ApplyScrollPos(
            fx->tree, fx->scroll_id, 2 + (int)((value >> 3) % 57), 9 + (int)((value >> 11) % 340));
        break;
    case 6:
    {
        char text[32];
        *out_scenario = "content";
        *out_checkpoint = "content_mutated";
        snprintf(text, sizeof(text), "state %02d", cycle % 100);
        (void)UITree_ApplyText(fx->tree, fx->content_id, text);
        (void)UITree_ApplyColour(
            fx->tree,
            fx->content_id,
            (int)(0x404040u | (value & 0xBFBFBFu)));
        break;
    }
    case 7:
        *out_scenario = "transparency";
        *out_checkpoint = "alpha_changed";
        set_transparency(fx, fx->alpha_rect, 24 + (int)(value % 210));
        break;
    case 8:
    {
        int32_t dynamic;
        *out_scenario = "topology";
        *out_checkpoint = "topology_replace";
        dynamic = UITree_CcCreate(
            fx->tree,
            fx->topology_layer,
            fx->tree->components[fx->topology_layer].component_id,
            3,
            cycle & 1);
        configure_dynamic_rect(
            fx,
            dynamic,
            8 + (int)(value % 58),
            8 + (int)((value >> 8) % 40),
            (int)(0x202020u | (value & 0xDFDFDFu)));
        break;
    }
    case 9:
        /* Host-only input: origin/v3's production gate retains the stale
         * compass here. Candidate host epochs must force the corrected frame. */
        *out_scenario = "host-camera";
        *out_checkpoint = "camera_host_only";
        set_camera_yaw(fx, (int)((value >> 5) & 0x7ff));
        break;
    case 10:
        *out_scenario = "projection";
        *out_checkpoint = "fish_orbit_a";
        (void)project_fish_at_yaw(fx, 1408 + (cycle % 2) * 32, NULL, NULL);
        break;
    case 11:
        *out_scenario = "projection";
        *out_checkpoint = "fish_orbit_b";
        (void)project_fish_at_yaw(fx, 1536 + (cycle % 2) * 32, NULL, NULL);
        break;
    case 12:
        *out_scenario = "projection";
        *out_checkpoint = "fish_viewport_clipped";
        (void)project_fish_at_yaw(fx, 1792, NULL, NULL);
        break;
    case 13:
        *out_scenario = "projection";
        *out_checkpoint = "fish_near_plane_hidden";
        (void)project_fish_at_yaw(fx, 512, NULL, NULL);
        break;
    case 14:
        *out_scenario = "projection";
        *out_checkpoint = "fish_revealed";
        (void)project_fish_at_yaw(fx, 1664, NULL, NULL);
        break;
    case 15:
        *out_scenario = "shape";
        *out_checkpoint = "arc_sprite_rotated";
        set_arc_angles(
            fx, 3000 + (int)(value % 18000), 41000 + (int)((value >> 8) % 23000));
        (void)UITree_ApplyGraphic2DAngle(
            fx->tree, fx->rotating_sprite_id, 4096 + (int)((value >> 3) % 57344));
        break;
    case 16:
        *out_scenario = "host-input";
        *out_checkpoint = "cs1_active_on";
        set_cs1_active(fx, 1);
        break;
    case 17:
        *out_scenario = "reachability";
        *out_checkpoint = "hidden_subtree_revealed";
        (void)UITree_ApplyHide(fx->tree, fx->hidden_layer_id, 0);
        break;
    case 18:
        *out_scenario = "volatile";
        *out_checkpoint = "overlay_zero_to_one";
        fx->hs.overlay_items[0].x = 44 + (int)(value % 220);
        fx->hs.overlay_items[0].color =
            0xFF000000u | (value & 0x00FFFFFFu);
        set_volatile_overlay_count(fx, 1);
        break;
    case 19:
        *out_scenario = "volatile";
        *out_checkpoint = "overlay_one_to_two";
        fx->hs.overlay_items[1].x = 148 + (int)((value >> 7) % 160);
        set_volatile_overlay_count(fx, 2);
        break;
    case 20:
        *out_scenario = "volatile";
        *out_checkpoint = "overlay_two_to_zero";
        set_volatile_overlay_count(fx, 0);
        break;
    case 21:
        *out_scenario = "host-input";
        *out_checkpoint = "cs1_active_off";
        set_cs1_active(fx, 0);
        break;
    case 22:
        *out_scenario = "reachability";
        *out_checkpoint = "subtree_hidden_again";
        (void)UITree_ApplyHide(fx->tree, fx->hidden_layer_id, 1);
        break;
    default:
    {
        char text[32];
        *out_scenario = "reset";
        *out_checkpoint = "reset";
        fx->hovered_component_id = -1;
        (void)UITree_ApplyScrollPos(fx->tree, fx->scroll_id, 0, 0);
        snprintf(text, sizeof(text), "state A%02d", (cycle + 1) % 100);
        (void)UITree_ApplyText(fx->tree, fx->content_id, text);
        (void)UITree_ApplyColour(fx->tree, fx->content_id, 0xF8FAFC);
        set_transparency(fx, fx->alpha_rect, 92);
        set_arc_angles(fx, 0, 38000);
        (void)UITree_ApplyGraphic2DAngle(fx->tree, fx->rotating_sprite_id, 0);
        set_cs1_active(fx, 0);
        set_volatile_overlay_count(fx, 0);
        (void)UITree_ApplyHide(fx->tree, fx->hidden_layer_id, 1);
        (void)project_fish_at_yaw(fx, 1536, NULL, NULL);
        UITree_CcDeleteAll(fx->tree, fx->topology_layer);
        {
            int32_t dynamic = UITree_CcCreate(
                fx->tree,
                fx->topology_layer,
                fx->tree->components[fx->topology_layer].component_id,
                3,
                0);
            configure_dynamic_rect(fx, dynamic, 8, 8, 0x60A5FA);
        }
        break;
    }
    }
}

static int
run_visual(struct Options const* opt)
{
    struct Fixture* fx = fixture_new(opt->seed, 0);
    struct Fixture* oracle_fx = NULL;
    struct ToriDraw_Scene* scene = scene_new();
    int* pixels = xcalloc((size_t)CANVAS_W * CANVAS_H, sizeof(*pixels));
    int* oracle_pixels = xcalloc((size_t)CANVAS_W * CANVAS_H, sizeof(*oracle_pixels));
    char path[1024];
    FILE* csv;
    int failures = 0;

    mkdir_p(opt->out_dir);
    path_join(path, sizeof(path), opt->out_dir, "frames.csv");
    csv = fopen(path, "wb");
    if( !csv )
        fatal("could not open frames.csv");
    fprintf(
        csv,
        "frame,scenario,checkpoint,pixel_hash,emit_hash,emit_count,full_walks,retained_frames\n");
#if CANDIDATE
    if( opt->retention )
        oracle_fx = fixture_new(opt->seed, 0);
#endif

    for( int frame = 0; frame < opt->frames; frame++ )
    {
        char const* scenario;
        char const* checkpoint;
        uint64_t ph;
        uint64_t eh;

        visual_transition(fx, frame, &scenario, &checkpoint);
#if CANDIDATE
        if( oracle_fx )
        {
            char const* oracle_scenario;
            char const* oracle_checkpoint;
            visual_transition(oracle_fx, frame, &oracle_scenario, &oracle_checkpoint);
            if( strcmp(scenario, oracle_scenario) != 0 ||
                strcmp(checkpoint, oracle_checkpoint) != 0 )
                fatal("retained/reference transition streams diverged");
        }
#endif
        emit_primary(fx, opt->retention);
        UITreeCmd_RenderToPixels(
            scene,
            fx->driver.emit.cmds,
            fx->driver.emit.count,
            pixels,
            CANVAS_W,
            CANVAS_H);
        ph = pixel_hash(pixels, CANVAS_W, CANVAS_H);
        eh = emit_hash(&fx->driver.emit);

#if CANDIDATE
        if( oracle_fx )
        {
            uint64_t oracle_emit_hash;
            emit_primary(oracle_fx, 0);
            UITreeCmd_RenderToPixels(
                scene,
                oracle_fx->driver.emit.cmds,
                oracle_fx->driver.emit.count,
                oracle_pixels,
                CANVAS_W,
                CANVAS_H);
            oracle_emit_hash = emit_hash(&oracle_fx->driver.emit);
            if( memcmp(
                    pixels,
                    oracle_pixels,
                    (size_t)CANVAS_W * CANVAS_H * sizeof(*pixels)) != 0 ||
                eh != oracle_emit_hash )
            {
                int first = -1;
                for( int i = 0; i < CANVAS_W * CANVAS_H; i++ )
                    if( pixels[i] != oracle_pixels[i] )
                    {
                        first = i;
                        break;
                    }
                fprintf(
                    stderr,
                    "oracle mismatch frame=%d scenario=%s first_pixel=%d retained_emit=%016" PRIx64
                    " full_emit=%016" PRIx64 "\n",
                    frame,
                    scenario,
                    first,
                    eh,
                    oracle_emit_hash);
                failures++;
            }
        }
#endif

        fprintf(
            csv,
            "%d,%s,%s,%016" PRIx64 ",%016" PRIx64 ",%d,%" PRIu64 ",%" PRIu64 "\n",
            frame,
            scenario,
            checkpoint,
            ph,
            eh,
            fx->driver.emit.count,
            fx->driver.full_walks,
            fx->driver.retained_frames);

        if( checkpoint[0] )
        {
            char leaf[160];
            snprintf(leaf, sizeof(leaf), "frame_%04d_%s.bmp", frame, checkpoint);
            path_join(path, sizeof(path), opt->out_dir, leaf);
            bmp_write_file(path, pixels, CANVAS_W, CANVAS_H);
        }
    }

    fclose(csv);
    free(pixels);
    free(oracle_pixels);
    fixture_free(oracle_fx);
    fixture_free(fx);
    if( failures )
    {
        fprintf(stderr, "visual oracle: %d mismatch(es)\n", failures);
        return 1;
    }
    printf(
        "visual ok: frames=%d candidate=%d retention=%d out=%s\n",
        opt->frames,
        CANDIDATE,
        opt->retention,
        opt->out_dir);
    return 0;
}

/* ------------------------------------------------------------------------- */

enum BenchScenario
{
    BENCH_STEADY,
    BENCH_CAMERA,
    BENCH_OVERLAY_POSITION,
    BENCH_SCROLL,
    BENCH_HOVER,
    BENCH_CONTENT,
    BENCH_TOPOLOGY,
    BENCH_SCENARIO_COUNT,
};

static char const*
bench_name(enum BenchScenario scenario)
{
    static char const* const names[] = {
        "steady", "camera", "overlay_position", "scroll", "hover", "content", "topology",
    };
    return names[(int)scenario];
}

static void
bench_mutate(struct Fixture* fx, enum BenchScenario scenario, int frame, uint32_t seed)
{
    uint32_t value = seed ^ ((uint32_t)frame * UINT32_C(0x9E3779B9));
    value = prng_next(&value);
    switch( scenario )
    {
    case BENCH_STEADY:
        break;
    case BENCH_CAMERA:
        set_camera_yaw(fx, (int)((value >> 5) & 2047));
        break;
    case BENCH_OVERLAY_POSITION:
        (void)UITree_EntityOverlaySetLayerPosition(
            fx->tree,
            fx->fish_layer,
            24 + (int)(value % 330),
            20 + (int)((value >> 10) % 240));
        break;
    case BENCH_SCROLL:
        (void)UITree_ApplyScrollPos(
            fx->tree,
            fx->scroll_id,
            (int)(value % 58),
            (int)((value >> 8) % 360));
        break;
    case BENCH_HOVER:
        fx->hovered_component_id = (frame & 1) ? fx->hover_id : -1;
        break;
    case BENCH_CONTENT:
        (void)UITree_ApplyColour(
            fx->tree,
            fx->content_id,
            (int)(0x202020u | (value & 0xDFDFDFu)));
        set_transparency(fx, fx->alpha_rect, 32 + (int)(value % 190));
        break;
    case BENCH_TOPOLOGY:
    {
        int sub = frame & 31;
        int32_t dynamic = UITree_CcCreate(
            fx->tree,
            fx->topology_layer,
            fx->tree->components[fx->topology_layer].component_id,
            3,
            sub);
        configure_dynamic_rect(
            fx,
            dynamic,
            4 + (sub % 2) * 54,
            4 + (sub % 3) * 22,
            (int)(0x303030u | (value & 0xCFCFCFu)));
        break;
    }
    case BENCH_SCENARIO_COUNT:
        break;
    }
}

static void
bench_warmup(enum BenchScenario scenario, struct Options const* opt)
{
    struct Fixture* fx = fixture_new(opt->seed ^ UINT32_C(0xA5A5A5A5), 4096);
    emit_primary(fx, opt->retention);
    for( int i = 0; i < 16; i++ )
    {
        bench_mutate(fx, scenario, i, opt->seed);
        emit_primary(fx, opt->retention);
    }
    fixture_free(fx);
}

static struct BenchMetric
bench_measure(enum BenchScenario scenario, struct Options const* opt)
{
    struct BenchMetric metric;
    struct Fixture* fx;
    uint64_t start;
    uint64_t stop;

    bench_warmup(scenario, opt);
    /* Fresh deterministic state per scenario prevents the previous workload's
     * topology/scroll/content endpoint from biasing this one.  One untimed
     * publication primes the candidate production gate. */
    fx = fixture_new(opt->seed, 4096);
    emit_primary(fx, opt->retention);
    fx->driver.full_walks = 0;
    fx->driver.retained_frames = 0;

    memset(&metric, 0, sizeof(metric));
    metric.scenario = bench_name(scenario);
    metric.operations = (uint64_t)opt->frames;
    start = now_ns();
    for( int frame = 0; frame < opt->frames; frame++ )
    {
        bench_mutate(fx, scenario, frame, opt->seed);
        emit_primary(fx, opt->retention);
        metric.emit_nodes += (uint64_t)fx->driver.emit.count;
    }
    stop = now_ns();
    metric.elapsed_ns = stop - start;
    metric.full_walks = fx->driver.full_walks;
    metric.retained_frames = fx->driver.retained_frames;
    fixture_free(fx);
    return metric;
}

static int
run_bench(struct Options const* opt)
{
    struct BenchMetric metrics[BENCH_SCENARIO_COUNT];
    struct BenchMetric aggregate = { .scenario = "aggregate" };
    char path[1024];
    FILE* csv;
    FILE* json;

    mkdir_p(opt->out_dir);
    for( int i = 0; i < ARRAY_COUNT(metrics); i++ )
    {
        metrics[i] = bench_measure((enum BenchScenario)i, opt);
        aggregate.elapsed_ns += metrics[i].elapsed_ns;
        aggregate.operations += metrics[i].operations;
        aggregate.full_walks += metrics[i].full_walks;
        aggregate.retained_frames += metrics[i].retained_frames;
        aggregate.emit_nodes += metrics[i].emit_nodes;
    }

    path_join(path, sizeof(path), opt->out_dir, "metrics.csv");
    csv = fopen(path, "wb");
    if( !csv )
        fatal("could not open metrics.csv");
    fprintf(
        csv,
        "scenario,elapsed_ns,operations,ns_per_op,full_walks,retained_frames,emit_nodes\n");
    for( int i = 0; i <= ARRAY_COUNT(metrics); i++ )
    {
        struct BenchMetric const* m = i < ARRAY_COUNT(metrics) ? &metrics[i] : &aggregate;
        double ns_per_op = m->operations ? (double)m->elapsed_ns / (double)m->operations : 0.0;
        fprintf(
            csv,
            "%s,%" PRIu64 ",%" PRIu64 ",%.3f,%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
            m->scenario,
            m->elapsed_ns,
            m->operations,
            ns_per_op,
            m->full_walks,
            m->retained_frames,
            m->emit_nodes);
    }
    fclose(csv);

    path_join(path, sizeof(path), opt->out_dir, "metrics.json");
    json = fopen(path, "wb");
    if( !json )
        fatal("could not open metrics.json");
    fprintf(
        json,
        "{\n  \"candidate\": %d,\n  \"retention\": %d,\n  \"frames\": %d,\n  \"seed\": %u,\n  \"metrics\": [\n",
        CANDIDATE,
        opt->retention,
        opt->frames,
        opt->seed);
    for( int i = 0; i <= ARRAY_COUNT(metrics); i++ )
    {
        struct BenchMetric const* m = i < ARRAY_COUNT(metrics) ? &metrics[i] : &aggregate;
        double ns_per_op = m->operations ? (double)m->elapsed_ns / (double)m->operations : 0.0;
        fprintf(
            json,
            "    {\"scenario\":\"%s\",\"elapsed_ns\":%" PRIu64
            ",\"operations\":%" PRIu64 ",\"ns_per_op\":%.3f,\"full_walks\":%" PRIu64
            ",\"retained_frames\":%" PRIu64 ",\"emit_nodes\":%" PRIu64 "}%s\n",
            m->scenario,
            m->elapsed_ns,
            m->operations,
            ns_per_op,
            m->full_walks,
            m->retained_frames,
            m->emit_nodes,
            i < ARRAY_COUNT(metrics) ? "," : "");
    }
    fprintf(json, "  ]\n}\n");
    fclose(json);

    printf(
        "bench ok: frames=%d candidate=%d retention=%d aggregate_ns=%" PRIu64 " out=%s\n",
        opt->frames,
        CANDIDATE,
        opt->retention,
        aggregate.elapsed_ns,
        opt->out_dir);
    return 0;
}

/* ------------------------------------------------------------------------- */

static long
parse_long(char const* value, char const* flag)
{
    char* end = NULL;
    long n;
    errno = 0;
    n = strtol(value, &end, 0);
    if( errno || !end || *end )
    {
        fprintf(stderr, "invalid %s value: %s\n", flag, value);
        exit(2);
    }
    return n;
}

static void
usage(FILE* out, char const* argv0)
{
    fprintf(
        out,
        "usage: %s --mode visual|bench --out DIR --retention 0|1 --frames N --seed N\n"
        "\n"
        "Deterministic UITree redraw parity/performance harness.\n"
        "\n"
        "  --mode visual  Run the scripted UI transition trace, write frames.csv,\n"
        "                 and write a BMP at every named checkpoint. With\n"
        "                 --retention 1, compare every published frame against a\n"
        "                 fresh full-walk raster and fail on any pixel/emit drift.\n"
        "  --mode bench   Rebuild a 4,000+ node fixture per workload, warm it up,\n"
        "                 and write metrics.csv plus metrics.json.\n"
        "  --out DIR      Artifact directory (created recursively).\n"
        "  --retention N  0 forces full walks; 1 enables the candidate production\n"
        "                 gate. An origin/v3 CANDIDATE=0 build stays forced-full.\n"
        "  --frames N     Trace frames or timed operations per workload (default 32).\n"
        "  --seed N       Unsigned 32-bit deterministic fixture seed (default 1).\n"
        "\n"
        "Visual CSV columns: frame,scenario,checkpoint,pixel_hash,emit_hash,\n"
        "emit_count,full_walks,retained_frames.\n"
        "Benchmark CSV columns: scenario,elapsed_ns,operations,ns_per_op,\n"
        "full_walks,retained_frames,emit_nodes.\n",
        argv0);
}

static struct Options
parse_options(int argc, char** argv)
{
    struct Options opt;
    int have_mode = 0;
    int have_out = 0;

    memset(&opt, 0, sizeof(opt));
    opt.frames = 32;
    opt.seed = 1;
    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0 )
        {
            usage(stdout, argv[0]);
            exit(0);
        }
        if( i + 1 >= argc )
        {
            usage(stderr, argv[0]);
            exit(2);
        }
        if( strcmp(argv[i], "--mode") == 0 )
        {
            char const* mode = argv[++i];
            if( strcmp(mode, "visual") == 0 )
                opt.mode = MODE_VISUAL;
            else if( strcmp(mode, "bench") == 0 )
                opt.mode = MODE_BENCH;
            else
                fatal("--mode must be visual or bench");
            have_mode = 1;
        }
        else if( strcmp(argv[i], "--out") == 0 )
        {
            opt.out_dir = argv[++i];
            have_out = 1;
        }
        else if( strcmp(argv[i], "--retention") == 0 )
        {
            long value = parse_long(argv[++i], "--retention");
            if( value != 0 && value != 1 )
                fatal("--retention must be 0 or 1");
            opt.retention = (int)value;
        }
        else if( strcmp(argv[i], "--frames") == 0 )
        {
            long value = parse_long(argv[++i], "--frames");
            if( value <= 0 || value > 1000000 )
                fatal("--frames must be in 1..1000000");
            opt.frames = (int)value;
        }
        else if( strcmp(argv[i], "--seed") == 0 )
        {
            long value = parse_long(argv[++i], "--seed");
            if( value < 0 || (unsigned long)value > UINT32_MAX )
                fatal("--seed must fit uint32");
            opt.seed = (uint32_t)value;
        }
        else
        {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(stderr, argv[0]);
            exit(2);
        }
    }
    if( !have_mode || !have_out )
    {
        usage(stderr, argv[0]);
        exit(2);
    }
    return opt;
}

int
main(int argc, char** argv)
{
    ToriDraw_Init();
    verify_projection_contract();
    struct Options opt = parse_options(argc, argv);
    return opt.mode == MODE_VISUAL ? run_visual(&opt) : run_bench(&opt);
}
