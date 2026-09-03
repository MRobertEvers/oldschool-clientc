/*
 * Plugin host tests.
 *
 * The host runs against a FAKE engine here -- that is the point of the engine
 * being a vtable rather than a direct call into app.c. Everything below is
 * behaviour that fails silently in a real client if it breaks: a verdict that
 * stops being honoured means an interception quietly does nothing, a menu
 * route that goes to the wrong plugin means someone else's row fires, and a
 * config round-trip that drops a key means settings vanish at the next launch.
 */

#include "plugin/torirs_plugin_host.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failures++;                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));                        \
        }                                                                                          \
    } while( 0 )

/* ------------------------------------------------------------ fake engine */

#define FAKE_OBJECTS_MAX 8

struct FakeObject
{
    int in_use;
    int source;
    int model_id;
    int seq_id;
    int active;
    int recolors;
};

struct FakeEngine
{
    int draw_items;
    int draw_canvas;
    int menu_rows;
    int last_action;
    char last_text[128];
    /* Assets: what the engine was asked to do, and what it will answer with. */
    int asset_reads;
    int asset_writes;
    char last_asset_plugin[64];
    char last_asset_name[64];
    char last_written[128];
    int last_written_size;
    /* Screenshots: the host validates the name and the destination, so the
     * engine only has to record what got through. */
    /* api->notify: what the player was told, and how often. */
    char last_notify[200];
    int notifies;
    int screenshots;
    char last_shot_dir[192];
    char last_shot_name[64];
    struct FakeObject objects[FAKE_OBJECTS_MAX];
    int objects_live;
    /* Meshes: counts only. What the triangles ARE is the engine's business
     * and is tested where the engine is; what the host owes is that a handle
     * reaches the engine, that the budget refuses past it, and that a stopped
     * plugin's meshes go with it. */
    int model_publishes;
    int model_releases;
    int image_releases;
    int last_model_size;
    int meshes_live;
    int mesh_creates;
    int mesh_vertices;
    int mesh_faces;
    int mesh_clears;
    int layout_begins;
    int layout_ends;
    int layout_sets;
    int layout_owned;
    int layout_canvas;
    int layout_fixed_w;
    int layout_fixed_h;
    char frame_preference[TORIRS_PLUGIN_FRAME_ID_MAX];
    int frame_preference_present;
    int frame_migration_version;
};

static struct FakeEngine g_engine;

/* In game: these harnesses exercise behaviour that is gated on it. Mutable so
 * the EV_SCREEN_CHANGE test can move it; everything else leaves it alone.
 * @see api->core.screen. */
static int g_screen_now = TORIRS_SCREEN_GAME;

static int
fake_plugin_screen(void* u)
{
    (void)u;
    return g_screen_now;
}

static int
fake_world_cycle(void* u)
{
    (void)u;
    return 42;
}
static uint64_t
fake_frame_ms(void* u)
{
    (void)u;
    return 1000;
}
static uint64_t
fake_frame_work_us(void* u)
{
    (void)u;
    return 4000;
}
static int g_capability_touch;
static int g_capability_browser;
static int g_capability_web;
static int
fake_capability(void* u, char const* name)
{
    (void)u;
    if( strcmp(name, "touch") == 0 )
        return g_capability_touch;
    if( strcmp(name, "browser") == 0 )
        return g_capability_browser;
    if( strcmp(name, "web") == 0 )
        return g_capability_web;
    return 0;
}
static int
fake_local_player(
    void* u,
    struct ToriRS_PlayerSnapshot* out)
{
    (void)u;
    memset(out, 0, sizeof(*out));
    out->true_x = 3200;
    out->true_z = 3200;
    return 1;
}
static int
fake_npc_next(
    void* u,
    int iter,
    struct ToriRS_NpcSnapshot* out)
{
    (void)u;
    if( iter >= 1 )
        return -1;
    memset(out, 0, sizeof(*out));
    out->server_slot = iter + 1;
    out->base_npc_id = 100 + iter;
    return iter + 1;
}
static int
fake_npc_by_slot(
    void* u,
    int slot,
    struct ToriRS_NpcSnapshot* out)
{
    (void)u;
    memset(out, 0, sizeof(*out));
    out->server_slot = slot;
    return slot >= 0 ? 1 : 0;
}
static int
fake_player_next(
    void* u,
    int iter,
    struct ToriRS_PlayerSnapshot* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_loc_next(
    void* u,
    int iter,
    struct ToriRS_ScenerySnapshot* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_highlight_next(
    void* u,
    int iter,
    struct ToriRS_HighlightItem* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static void
fake_notify(
    void* u,
    char const* text)
{
    (void)u;
    snprintf(g_engine.last_notify, sizeof(g_engine.last_notify), "%s", text);
    g_engine.notifies++;
}
static int
fake_key_held(
    void* u,
    int key)
{
    (void)u;
    return key == 42;
}
static int
fake_hover_tile(
    void* u,
    int* ox,
    int* oz,
    int* olevel)
{
    (void)u;
    *ox = 3200;
    *oz = 3200;
    *olevel = 0;
    return 1;
}
static int
fake_hover_entity(
    void* u,
    struct ToriRS_HoverTarget* out)
{
    (void)u;
    out->kind = TORIRS_HOVER_NPC;
    out->element_id = 7;
    out->tile_x = 3200;
    out->tile_z = 3200;
    out->level = 0;
    return 1;
}
/* Two ids with values, so a test can tell a read from a zeroed struct; every
 * other id answers 0, which is what the api promises for one this revision
 * does not define. */
static int
fake_element_height(
    void* u,
    int element_id)
{
    (void)u;
    return element_id >= 0 ? 200 : 0;
}
/*
 * Feature flags, as a fake engine publishes them: one int, one enum, and a
 * boot snapshot so the UNSET restore has something to restore TO. Two is
 * enough to exercise everything the host forwards -- the walk, the range
 * refusal and the sentinel -- without this file growing a copy of app.c's
 * table, which is the client's business and not the host's.
 */
struct FakeFeature
{
    char const* key;
    char const* label;
    int kind;
    int min;
    int max;
    char const* choices;
    int values[2];
    int value_count;
    int boot;
    int value;
};

static struct FakeFeature g_fake_features[] = {
    { "draw_distance",
     "Draw distance", TORIRS_FEATURE_INT,
     25, 90,
     NULL,               { 0, 0 },
     0, 25,
     25 },
    { "camera_zoom",
     "Camera zoom",   TORIRS_FEATURE_ENUM,
     0,  0,
     "Adjustable|Fixed", { 0, 1 },
     2, 0,
     0  },
};

#define FAKE_FEATURE_COUNT ((int)(sizeof(g_fake_features) / sizeof(g_fake_features[0])))

static struct FakeFeature*
fake_feature_find(char const* key)
{
    for( int i = 0; i < FAKE_FEATURE_COUNT; i++ )
    {
        if( strcmp(g_fake_features[i].key, key) == 0 )
            return &g_fake_features[i];
    }
    return NULL;
}

static int
fake_feature_next(
    void* u,
    int i,
    struct ToriRS_FeatureInfo* o)
{
    (void)u;

    int const at = i < 0 ? 0 : i + 1;
    if( at >= FAKE_FEATURE_COUNT )
        return -1;

    struct FakeFeature const* f = &g_fake_features[at];
    memset(o, 0, sizeof(*o));
    snprintf(o->key, sizeof(o->key), "%s", f->key);
    snprintf(o->label, sizeof(o->label), "%s", f->label);
    o->kind = f->kind;
    o->min = f->min;
    o->max = f->max;
    if( f->choices )
        snprintf(o->choices, sizeof(o->choices), "%s", f->choices);
    o->value_count = f->value_count;
    for( int v = 0; v < f->value_count; v++ )
        o->values[v] = f->values[v];
    o->value = f->value;
    o->is_default = f->value == f->boot;
    return at;
}

static int
fake_feature_get(
    void* u,
    char const* k)
{
    (void)u;

    struct FakeFeature const* f = fake_feature_find(k);
    return f ? f->value : TORIRS_FEATURE_UNSET;
}

static int
fake_feature_set(
    void* u,
    char const* k,
    int v)
{
    (void)u;

    struct FakeFeature* f = fake_feature_find(k);
    if( !f )
        return 0;
    if( v == TORIRS_FEATURE_UNSET )
    {
        f->value = f->boot;
        return 1;
    }
    if( f->kind == TORIRS_FEATURE_ENUM )
    {
        int legal = 0;
        for( int i = 0; i < f->value_count; i++ )
            legal |= f->values[i] == v;
        if( !legal )
            return 0;
    }
    else if( v < f->min || v > f->max )
        return 0;
    f->value = v;
    return 1;
}

static int
fake_varbit(
    void* u,
    int id)
{
    (void)u;
    return id == 12977 ? 1 : 0;
}
static int
fake_varp(
    void* u,
    int id)
{
    (void)u;
    /* The colour rows store `colour + 1`; 0x00FF00 + 1 here. */
    return id == 3108 ? 0x00FF01 : 0;
}
static int
fake_project(
    void* u,
    int fx,
    int fz,
    int h,
    int* ox,
    int* oy)
{
    (void)u;
    (void)h;
    *ox = fx / 128;
    *oy = fz / 128;
    return 1;
}
static int
fake_draw_tile(
    void* u,
    int a,
    int b,
    int c,
    uint32_t d,
    uint32_t e,
    int f)
{
    (void)u;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    g_engine.draw_items += 5;
    return 5;
}
/* The shape a plugin asked for, so the test can prove it survives the trip
 * through the api rather than being dropped on the way to the engine -- which
 * is silent otherwise: the wrong shape still draws an outline. */
static int g_hull_shape;
static int
fake_draw_hull(
    void* u,
    int a,
    uint32_t b,
    int c,
    int d)
{
    (void)u;
    (void)a;
    (void)b;
    (void)c;
    g_hull_shape = d;
    g_engine.draw_items += 3;
    return 3;
}
static int
fake_draw_line(
    void* u,
    int a,
    int b,
    int c,
    int d,
    uint32_t e)
{
    (void)u;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    g_engine.draw_items += 1;
    return 1;
}
static int
fake_draw_text(
    void* u,
    int a,
    int b,
    char const* s,
    uint32_t c)
{
    (void)u;
    (void)a;
    (void)b;
    (void)s;
    (void)c;
    g_engine.draw_items += 1;
    return 1;
}
static int
fake_draw_rect(
    void* u,
    int a,
    int b,
    int c,
    int d,
    uint32_t e,
    int f)
{
    (void)u;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    g_engine.draw_items += 1;
    return 1;
}
static int
fake_obj_next(
    void* u,
    int iter,
    struct ToriRS_GroundItemSnapshot* out)
{
    (void)u;
    /* Exactly one stack: enough to prove the iterator both yields and ends. */
    if( iter >= 0 )
        return -1;
    memset(out, 0, sizeof(*out));
    out->obj_id = 4151;
    out->count = 1;
    out->cost = 120000;
    out->tile_x = 3200;
    out->tile_z = 3200;
    out->element_id = 7;
    snprintf(out->name, sizeof(out->name), "Abyssal whip");
    return 0;
}

static int
fake_asset_read(
    void* u,
    char const* plugin,
    char const* name)
{
    struct FakeEngine* e = u;
    e->asset_reads++;
    snprintf(e->last_asset_plugin, sizeof(e->last_asset_plugin), "%s", plugin);
    snprintf(e->last_asset_name, sizeof(e->last_asset_name), "%s", name);
    return 1;
}

static int
fake_asset_write(
    void* u,
    char const* plugin,
    char const* name,
    void const* data,
    int size)
{
    struct FakeEngine* e = u;
    (void)plugin;
    (void)name;
    e->asset_writes++;
    e->last_written_size = size;
    snprintf(
        e->last_written,
        sizeof(e->last_written),
        "%.*s",
        size < (int)sizeof(e->last_written) - 1 ? size : (int)sizeof(e->last_written) - 1,
        (char const*)data);
    return 1;
}

static int
fake_screenshot(
    void* u,
    char const* plugin,
    char const* dir,
    char const* name,
    char* out_path,
    int out_path_size)
{
    struct FakeEngine* e = u;
    (void)plugin;
    e->screenshots++;
    snprintf(e->last_shot_dir, sizeof(e->last_shot_dir), "%s", dir ? dir : "");
    snprintf(e->last_shot_name, sizeof(e->last_shot_name), "%s", name);
    /* The engine owns the folders, so the path it answers with is its own
     * doing; this fake states the shape the app builds -- destination then
     * name -- so the host's pass-through can be checked without one. */
    snprintf(
        out_path, (size_t)out_path_size, "%s%s%s", dir ? dir : "", (dir && *dir) ? "/" : "", name);
    return 1;
}

static void
fake_layout_begin(void* u)
{
    struct FakeEngine* e = u;
    e->layout_begins++;
}
static void
fake_layout_end(void* u)
{
    struct FakeEngine* e = u;
    e->layout_ends++;
}
static int
fake_model_publish(
    void* u,
    int model,
    void const* data,
    int size)
{
    struct FakeEngine* e = u;
    (void)model;
    (void)data;
    e->model_publishes++;
    /* Size is the whole test: the host must forward the asset's bytes, and a
     * publish of nothing is the bug this catches. */
    e->last_model_size = size;
    return size > 0;
}

static void
fake_model_release(
    void* u,
    int model)
{
    struct FakeEngine* e = u;
    (void)model;
    e->model_releases++;
}

static int
fake_mesh_create(void* u)
{
    struct FakeEngine* e = u;
    e->mesh_creates++;
    return e->meshes_live++;
}

static void
fake_mesh_destroy(
    void* u,
    int mesh)
{
    struct FakeEngine* e = u;
    (void)mesh;
    e->meshes_live--;
}

static void
fake_mesh_clear(
    void* u,
    int mesh)
{
    struct FakeEngine* e = u;
    (void)mesh;
    e->mesh_clears++;
}

static int
fake_mesh_vertex(
    void* u,
    int mesh,
    int x,
    int y,
    int z)
{
    struct FakeEngine* e = u;
    (void)mesh;
    (void)x;
    (void)y;
    (void)z;
    return e->mesh_vertices++;
}

static int
fake_mesh_face(
    void* u,
    int mesh,
    int a,
    int b,
    int c,
    int hsl,
    int alpha)
{
    struct FakeEngine* e = u;
    (void)mesh;
    (void)a;
    (void)b;
    (void)c;
    (void)hsl;
    (void)alpha;
    return e->mesh_faces++;
}

static int
fake_object_create(void* u)
{
    struct FakeEngine* e = u;
    for( int i = 0; i < FAKE_OBJECTS_MAX; i++ )
    {
        if( e->objects[i].in_use )
            continue;
        memset(&e->objects[i], 0, sizeof(e->objects[i]));
        e->objects[i].in_use = 1;
        e->objects[i].model_id = -1;
        e->objects[i].seq_id = -1;
        e->objects_live++;
        return i;
    }
    return -1;
}

static void
fake_object_destroy(
    void* u,
    int object)
{
    struct FakeEngine* e = u;
    if( object < 0 || object >= FAKE_OBJECTS_MAX || !e->objects[object].in_use )
        return;
    memset(&e->objects[object], 0, sizeof(e->objects[object]));
    e->objects_live--;
}

static void
fake_object_set_model(
    void* u,
    int object,
    int source,
    int id)
{
    struct FakeEngine* e = u;
    e->objects[object].source = source;
    e->objects[object].model_id = id;
}

static void
fake_object_recolor(
    void* u,
    int object,
    int from,
    int to)
{
    struct FakeEngine* e = u;
    (void)from;
    (void)to;
    e->objects[object].recolors++;
}

static void
fake_object_clear_recolors(
    void* u,
    int object)
{
    struct FakeEngine* e = u;
    e->objects[object].recolors = 0;
}

static void
fake_object_set_anim(
    void* u,
    int object,
    int seq_id,
    int loop)
{
    struct FakeEngine* e = u;
    (void)loop;
    e->objects[object].seq_id = seq_id;
}

static void
fake_object_set_light(
    void* u,
    int object,
    int ambient,
    int contrast)
{
    (void)u;
    (void)object;
    (void)ambient;
    (void)contrast;
}

static void
fake_object_set_position(
    void* u,
    int object,
    int x,
    int z,
    int level,
    int height,
    int yaw)
{
    (void)u;
    (void)object;
    (void)x;
    (void)z;
    (void)level;
    (void)height;
    (void)yaw;
}

static void
fake_object_set_active(
    void* u,
    int object,
    int active)
{
    struct FakeEngine* e = u;
    e->objects[object].active = active;
}

static int
fake_object_ready(
    void* u,
    int object)
{
    struct FakeEngine* e = u;
    return e->objects[object].model_id >= 0;
}

static int
fake_hsl_from_rgb(
    void* u,
    uint32_t rgb)
{
    (void)u;
    return (int)(rgb & 0xffff);
}

static uint32_t
fake_hsl_to_rgb(
    void* u,
    int hsl)
{
    (void)u;
    return (uint32_t)hsl;
}

static int
fake_menu_add(
    void* u,
    void* cursor,
    char const* text,
    int action)
{
    (void)u;
    (void)cursor;
    g_engine.menu_rows++;
    g_engine.last_action = action;
    snprintf(g_engine.last_text, sizeof(g_engine.last_text), "%s", text);
    return 1;
}

static int
fake_menu_drop(
    void* u,
    void* cursor,
    int index)
{
    (void)u;
    (void)cursor;
    (void)index;
    return 1;
}

/* ---- the 2026-08-22 additions: the canvas surface, images and if_click ----
 *
 * Stubs, deliberately: what these tests exercise is the HOST -- the bus, the
 * budget, the sandbox -- and none of that cares what the engine does with a
 * blit. What they do have to do is EXIST, because PluginHost_New asserts every
 * entry: a fake engine missing one is a fake that has fallen behind the
 * contract, and the assert is what says so. */
static int
fake_mouse_pos(
    void* u,
    int* x,
    int* y)
{
    (void)u;
    if( x )
        *x = 0;
    if( y )
        *y = 0;
    return 1;
}
/*
 * The engine entry points this suite does not exercise.
 *
 * PluginHost_New asserts every one of them, so a seam that grows a callback
 * aborts the whole suite on its first line until the fake catches up -- which
 * is the point of the assert, and is why these are stubs with honest answers
 * rather than omissions. Each returns the "this frame has none" answer its
 * contract defines.
 */
static void
fake_layout_set(
    void* u,
    int owned,
    int canvas,
    int fixed_w,
    int fixed_h)
{
    struct FakeEngine* e = u;
    e->layout_sets++;
    e->layout_owned = owned;
    e->layout_canvas = canvas;
    e->layout_fixed_w = fixed_w;
    e->layout_fixed_h = fixed_h;
}
static int
fake_layout_slot(
    void* u,
    int slot,
    int member,
    int x,
    int y,
    int w,
    int h)
{
    (void)u;
    (void)slot;
    (void)member;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return 0;
}
static int
fake_slot_native_size(
    void* u,
    int slot,
    int* out_w,
    int* out_h)
{
    (void)u;
    (void)slot;
    (void)out_w;
    (void)out_h;
    return 0;
}
static int
fake_layout_slot_skin(
    void* u,
    int slot,
    int art,
    int mask)
{
    (void)u;
    (void)slot;
    (void)art;
    (void)mask;
    return 0;
}
static int
fake_layout_slot_overlay(
    void* u,
    int slot,
    int image,
    int x,
    int y,
    int trans)
{
    (void)u;
    (void)slot;
    (void)image;
    (void)x;
    (void)y;
    (void)trans;
    return 0;
}
static int
fake_layout_scrollbar(
    void* u,
    int const* images,
    int count)
{
    (void)u;
    (void)images;
    (void)count;
    return 0;
}
static int
fake_display_setting(
    void* u,
    int setting,
    int* out_value,
    int* out_min,
    int* out_max)
{
    (void)u;
    (void)setting;
    (void)out_value;
    (void)out_min;
    (void)out_max;
    return 0;
}
static int
fake_display_setting_set(
    void* u,
    int setting,
    int value)
{
    (void)u;
    (void)setting;
    (void)value;
    return 0;
}
static int
fake_frame_preference(
    void* u,
    char* out,
    int out_size,
    int* migration)
{
    struct FakeEngine* e = u;
    char const* value = e->frame_preference[0] ? e->frame_preference : "auto";
    snprintf(out, (size_t)out_size, "%s", value);
    if( migration )
        *migration = e->frame_migration_version;
    return e->frame_preference_present;
}
static int
fake_frame_preference_set(
    void* u,
    char const* id,
    int migration)
{
    struct FakeEngine* e = u;
    snprintf(e->frame_preference, sizeof(e->frame_preference), "%s", id);
    e->frame_preference_present = 1;
    e->frame_migration_version = migration;
    return 1;
}
static int
fake_tab_active(void* u)
{
    (void)u;
    return -1;
}
static int
fake_tab_select(
    void* u,
    int tabno)
{
    (void)u;
    (void)tabno;
    return 0;
}
static int
fake_tab_enabled(
    void* u,
    int tabno)
{
    (void)u;
    (void)tabno;
    return 1;
}
static int
fake_obj_info(
    void* u,
    int obj_id,
    struct ToriRS_ItemInfo* out)
{
    (void)u;
    (void)obj_id;
    (void)out;
    return 0;
}
static int
fake_inv_slot(
    void* u,
    int inv,
    int slot,
    int* out_obj_id,
    int* out_count)
{
    (void)u;
    (void)inv;
    (void)slot;
    (void)out_obj_id;
    (void)out_count;
    return 0;
}
static int
fake_inv_size(
    void* u,
    int inv)
{
    (void)u;
    (void)inv;
    return 0;
}

/* Regions, by role. `w` of 0 means "this gameframe has no such region", which
 * is how the fallback chain in slot_rect's contract gets exercised. */
static int g_slot_x[TORIRS_HOST_SURFACE_COUNT];
static int g_slot_y[TORIRS_HOST_SURFACE_COUNT];
static int g_slot_w[TORIRS_HOST_SURFACE_COUNT];
static int g_slot_h[TORIRS_HOST_SURFACE_COUNT];
static struct ToriRS_PlacementRect g_platform_safe[4];
static int g_platform_safe_count;

static int
fake_platform_safe_next(
    void* u,
    int iter,
    int* x,
    int* y,
    int* w,
    int* h)
{
    int const next = iter + 1;
    struct ToriRS_PlacementRect const* rect;

    (void)u;
    if( next < 0 || next >= g_platform_safe_count )
        return -1;
    rect = &g_platform_safe[next];
    if( x )
        *x = rect->x;
    if( y )
        *y = rect->y;
    if( w )
        *w = rect->w;
    if( h )
        *h = rect->h;
    return next;
}

static int
fake_slot_rect(
    void* u,
    int slot,
    int* x,
    int* y,
    int* w,
    int* h)
{
    (void)u;
    if( slot < 0 || slot >= TORIRS_HOST_SURFACE_COUNT )
        return 0;
    if( g_slot_w[slot] <= 0 || g_slot_h[slot] <= 0 )
        return 0;
    if( x )
        *x = g_slot_x[slot];
    if( y )
        *y = g_slot_y[slot];
    if( w )
        *w = g_slot_w[slot];
    if( h )
        *h = g_slot_h[slot];
    return 1;
}

/* One member, so that a readout naming one can be told from a readout that
 * fell back to the role. `g_member_no` of -1 means this frame declares none.
 */
static int g_member_slot = -1;
static int g_member_no = -1;
static int g_member_box[4];

static int
fake_slot_member_rect(
    void* u,
    int slot,
    int member,
    int* x,
    int* y,
    int* w,
    int* h)
{
    (void)u;
    if( slot != g_member_slot || member != g_member_no )
        return 0;
    if( x )
        *x = g_member_box[0];
    if( y )
        *y = g_member_box[1];
    if( w )
        *w = g_member_box[2];
    if( h )
        *h = g_member_box[3];
    return 1;
}

/* One mounted component, so a readout by id can be told from one that missed.
 * `g_component_id` of -1 means nothing is mounted. */
static int g_component_id = -1;
static int g_component_box[4];

static int
fake_component_rect(
    void* u,
    int component_id,
    int* x,
    int* y,
    int* w,
    int* h)
{
    (void)u;
    if( component_id != g_component_id )
        return 0;
    if( x )
        *x = g_component_box[0];
    if( y )
        *y = g_component_box[1];
    if( w )
        *w = g_component_box[2];
    if( h )
        *h = g_component_box[3];
    return 1;
}
/*
 * One bound role, so a name that resolves can be told from one that does not.
 * `g_role_name` NULL means this revision declares nothing at all -- which is
 * the state every lane is in before its profile is written, and the one the
 * contract's "an unbound role is an answer" rule is about.
 */
static char const* g_role_name;
static int g_role_box[4];
static int g_role_visible;
static int g_role_component_id = -1;
static int g_role_clicked_op = -1;
static char const* g_role_clicked;
static int g_role_suppress_calls;
static int g_role_suppress_paint;
static int g_role_suppress_input;
static char g_role_suppress_name[TORIRS_PLUGIN_ROLE_NAME_MAX];
static int g_ui_boundary_calls;
static int g_ui_boundary_resets;
static int g_ui_boundary_invalids;
static int g_ui_boundary_current_plugin = -1;
static int g_ui_boundary_replace = -1;
static int g_ui_boundary_last_replace = -1;
static int g_ui_boundary_last_place = -1;
static int g_lane_rail_box[4];
static int g_lane_rail_visible;

static int
role_is(char const* role)
{
    return g_role_name && strcmp(role, g_role_name) == 0;
}

static int
fake_role_rect(
    void* u,
    char const* role,
    int* x,
    int* y,
    int* w,
    int* h)
{
    (void)u;
    if( strcmp(role, "lane_chrome_0") == 0 && g_lane_rail_visible )
    {
        if( x )
            *x = g_lane_rail_box[0];
        if( y )
            *y = g_lane_rail_box[1];
        if( w )
            *w = g_lane_rail_box[2];
        if( h )
            *h = g_lane_rail_box[3];
        return 1;
    }
    if( !role_is(role) )
        return 0;
    if( x )
        *x = g_role_box[0];
    if( y )
        *y = g_role_box[1];
    if( w )
        *w = g_role_box[2];
    if( h )
        *h = g_role_box[3];
    return 1;
}

static int
fake_role_visible(
    void* u,
    char const* role)
{
    (void)u;
    if( strcmp(role, "lane_chrome_0") == 0 )
        return g_lane_rail_visible;
    return role_is(role) ? g_role_visible : 0;
}

static int
fake_role_click(
    void* u,
    char const* role,
    int op)
{
    (void)u;
    if( !role_is(role) )
        return 0;
    g_role_clicked = role;
    g_role_clicked_op = op;
    return 1;
}

static int
fake_role_id(
    void* u,
    char const* role)
{
    (void)u;
    return role_is(role) ? g_role_component_id : -1;
}

/* No role in these fakes binds to a frame slot: the tests that care about
 * chrome parts drive them through the slot verbs directly. */
static int
fake_role_slot(
    void* user,
    char const* role,
    int* out_slot,
    int* out_member)
{
    (void)user;
    (void)role;
    (void)out_slot;
    (void)out_member;
    return 0;
}

static int
fake_role_suppress_facets(
    void* u,
    char const* role,
    int paint,
    int input)
{
    (void)u;
    g_role_suppress_calls++;
    g_role_suppress_paint = paint;
    g_role_suppress_input = input;
    (void)snprintf(
        g_role_suppress_name,
        sizeof(g_role_suppress_name),
        "%s",
        role ? role : "");
    return role && role_is(role);
}

static int
fake_ui_boundary(
    void* u,
    int plugin,
    char const* role,
    int replace,
    int place)
{
    (void)u;
    if( !role )
    {
        g_ui_boundary_resets++;
        g_ui_boundary_current_plugin = -1;
        g_ui_boundary_replace = -1;
        return 1;
    }
    if( role[0] == '\0' )
    {
        g_ui_boundary_invalids++;
        g_ui_boundary_current_plugin = plugin;
        g_ui_boundary_replace = -2;
        return 0;
    }
    g_ui_boundary_calls++;
    g_ui_boundary_current_plugin = plugin;
    g_ui_boundary_replace = replace;
    g_ui_boundary_last_replace = replace;
    g_ui_boundary_last_place = place;
    return role_is(role);
}

static int
fake_stat(
    void* u,
    int skill,
    int* cur,
    int* base)
{
    (void)u;
    (void)skill;
    if( cur )
        *cur = 10;
    if( base )
        *base = 10;
    return 1;
}
/* Level 10 with 1154 xp: the hitpoints a fresh account starts on, so the
 * thresholds either side of it are real numbers rather than zeroes. */
static int
fake_stat_xp(
    void* u,
    int skill,
    int* xp,
    int* level_xp,
    int* next_xp)
{
    (void)u;
    if( skill < 0 || skill >= 25 )
        return 0;
    if( xp )
        *xp = 1154;
    if( level_xp )
        *level_xp = 1154;
    if( next_xp )
        *next_xp = 1358;
    return 1;
}
static char const*
fake_skill_name(
    void* u,
    int skill)
{
    static char const* const NAMES[] = { "Attack", "Defence", "Strength", "Hitpoints" };
    (void)u;
    if( skill < 0 || skill >= (int)(sizeof(NAMES) / sizeof(NAMES[0])) )
        return NULL;
    return NAMES[skill];
}
static int
fake_run_energy(void* u)
{
    (void)u;
    return 100;
}
static void
fake_draw_select_canvas(
    void* u,
    int canvas)
{
    (void)u;
    g_engine.draw_canvas = canvas;
}
static struct
{
    int slot;
    int w;
    int h;
    uint32_t argb[64 * 64];
} g_loaded_image;

static int
fake_image_publish(
    void* u,
    int slot,
    void const* data,
    int size,
    int* w,
    int* h)
{
    unsigned char const* bytes = data;
    (void)u;
    if( size >= 4 && memcmp(data, "FAIL", 4) == 0 )
        return 0;
    g_loaded_image.slot = slot;
    g_loaded_image.w = 26;
    g_loaded_image.h = 26;
    if( size >= 10 && memcmp(data, "ICON", 4) == 0 )
    {
        g_loaded_image.w = bytes[4];
        g_loaded_image.h = bytes[5];
    }
    if( w )
        *w = g_loaded_image.w;
    if( h )
        *h = g_loaded_image.h;
    if( g_loaded_image.w > 0 && g_loaded_image.h > 0 &&
        g_loaded_image.w * g_loaded_image.h <=
            (int)(sizeof(g_loaded_image.argb) / sizeof(g_loaded_image.argb[0])) )
    {
        uint32_t color = 0xFF336699u;
        if( size >= 10 && memcmp(data, "ICON", 4) == 0 )
            memcpy(&color, bytes + 6, sizeof(color));
        for( int i = 0; i < g_loaded_image.w * g_loaded_image.h; i++ )
            g_loaded_image.argb[i] = color;
    }
    return 1;
}
/* The composed image the host last published, so a test can see the pixels
 * came through and read them back the way the engine's own scene would. */
static struct
{
    int slot;
    int w;
    int h;
    uint32_t argb[64 * 64];
} g_composed;

static int
fake_image_publish_argb(
    void* u,
    int slot,
    int w,
    int h,
    uint32_t const* argb)
{
    (void)u;
    if( w <= 0 || h <= 0 || w * h > (int)(sizeof(g_composed.argb) / sizeof(uint32_t)) )
        return 0;
    g_composed.slot = slot;
    g_composed.w = w;
    g_composed.h = h;
    memcpy(g_composed.argb, argb, (size_t)(w * h) * sizeof(uint32_t));
    return 1;
}
static int
fake_image_read(
    void* u,
    int slot,
    uint32_t* out,
    int max)
{
    (void)u;
    int const loaded_pixels = g_loaded_image.w * g_loaded_image.h;
    int const pixels = g_composed.w * g_composed.h;

    if( slot == g_loaded_image.slot && loaded_pixels > 0 && loaded_pixels <= max &&
        loaded_pixels <= (int)(sizeof(g_loaded_image.argb) / sizeof(g_loaded_image.argb[0])) )
    {
        memcpy(out, g_loaded_image.argb, (size_t)loaded_pixels * sizeof(uint32_t));
        return loaded_pixels;
    }
    if( slot != g_composed.slot || pixels <= 0 || pixels > max )
        return 0;
    memcpy(out, g_composed.argb, (size_t)pixels * sizeof(uint32_t));
    return pixels;
}
static void
fake_image_release(
    void* u,
    int slot)
{
    struct FakeEngine* e = u;
    (void)slot;
    e->image_releases++;
}

/* The icon cache's engine end. A fake objtype has no inventory model, so the
 * honest answer here is the same one a real client gives before one is
 * resident: not yet. Tests that want an icon override this. */
static int
fake_obj_image(
    void* u,
    int slot,
    int obj_id,
    int count,
    int style,
    int* out_w,
    int* out_h)
{
    (void)u;
    (void)slot;
    (void)obj_id;
    (void)count;
    (void)style;
    (void)out_w;
    (void)out_h;
    return 0;
}

/* The client's own loot record. A fake engine records nothing, which is the
 * ordinary answer on a lane whose server has no kill hook. */
static int
fake_loot_source_next(
    void* u,
    int iter,
    struct ToriRS_LootSource* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_loot_row_next(
    void* u,
    int source_id,
    int iter,
    struct ToriRS_LootRow* out)
{
    (void)u;
    (void)source_id;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_draw_image(
    void* u,
    int slot,
    int x,
    int y,
    int w,
    int h,
    int cx,
    int cy,
    int cw,
    int ch,
    int trans)
{
    (void)u;
    (void)slot;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)cx;
    (void)cy;
    (void)cw;
    (void)ch;
    (void)trans;
    g_engine.draw_items += 1;
    return 1;
}
static int g_hit_region_calls;
static int g_hit_region_plugin;
static int g_hit_region_box[4];
static int g_hit_region_op_count;
static uint32_t g_hit_region_tag;
static char g_hit_region_ops[TORIRS_PLUGIN_REGION_OPS_MAX][TORIRS_UI_ACTION_MAX];

static int
fake_hit_region(
    void* u,
    int plugin,
    int x,
    int y,
    int w,
    int h,
    char const* const* ops,
    int op_count,
    uint32_t tag)
{
    (void)u;
    g_hit_region_calls++;
    g_hit_region_plugin = plugin;
    g_hit_region_box[0] = x;
    g_hit_region_box[1] = y;
    g_hit_region_box[2] = w;
    g_hit_region_box[3] = h;
    g_hit_region_op_count = op_count;
    g_hit_region_tag = tag;
    memset(g_hit_region_ops, 0, sizeof(g_hit_region_ops));
    for( int i = 0; i < op_count && i < TORIRS_PLUGIN_REGION_OPS_MAX; i++ )
        (void)snprintf(
            g_hit_region_ops[i], sizeof(g_hit_region_ops[i]), "%s", ops[i]);
    return 1;
}
static int
fake_if_click(
    void* u,
    int component_id,
    int op)
{
    (void)u;
    (void)component_id;
    (void)op;
    return 1;
}

/*
 * The lane this fake booted on, as `[cache:boot]` would have stated it.
 *
 * A global rather than a field of the fake, because a lane is decided before
 * anything the host does and never changes under a running client -- the tests
 * set it, build a host, and that is the world that host lives in.
 */
static int g_lane_game = TORIRS_GAME_UNKNOWN;

static int
fake_lane(
    void* u,
    struct ToriRS_LaneInfo* o)
{
    (void)u;
    memset(o, 0, sizeof(*o));
    o->game = g_lane_game;
    /* An unidentified cache answers 0 with the whole struct zeroed, which is
     * the one answer a plugin is told not to decide on. */
    if( g_lane_game == TORIRS_GAME_UNKNOWN )
        return 0;
    o->epoch = TORIRS_CACHE_EPOCH_DAT2;
    o->revision = g_lane_game == TORIRS_GAME_OLDSCHOOL ? 239 : 254;
    return 1;
}

static struct ToriRS_PluginEngine
fake_engine(void)
{
    struct ToriRS_PluginEngine e;
    memset(&e, 0, sizeof(e));
    e.user = &g_engine;
    e.screen = fake_plugin_screen;
    e.world_cycle = fake_world_cycle;
    e.frame_ms = fake_frame_ms;
    e.frame_work_us = fake_frame_work_us;
    e.capability = fake_capability;
    e.local_player = fake_local_player;
    e.npc_next = fake_npc_next;
    e.npc_by_slot = fake_npc_by_slot;
    e.player_next = fake_player_next;
    e.loc_next = fake_loc_next;
    e.highlight_next = fake_highlight_next;
    e.notify = fake_notify;
    e.key_held = fake_key_held;
    e.hover_tile = fake_hover_tile;
    e.hover_entity = fake_hover_entity;
    e.element_height = fake_element_height;
    e.lane = fake_lane;
    e.feature_next = fake_feature_next;
    e.feature_get = fake_feature_get;
    e.feature_set = fake_feature_set;
    e.varbit = fake_varbit;
    e.varp = fake_varp;
    e.project = fake_project;
    e.draw_tile = fake_draw_tile;
    e.draw_hull = fake_draw_hull;
    e.draw_line = fake_draw_line;
    e.draw_text = fake_draw_text;
    e.draw_rect = fake_draw_rect;
    e.mouse_pos = fake_mouse_pos;
    if( g_platform_safe_count > 0 )
        e.platform_safe_next = fake_platform_safe_next;
    e.slot_rect = fake_slot_rect;
    e.slot_member_rect = fake_slot_member_rect;
    e.slot_native_size = fake_slot_native_size;
    e.component_rect = fake_component_rect;
    e.role_rect = fake_role_rect;
    e.role_visible = fake_role_visible;
    e.role_click = fake_role_click;
    e.role_id = fake_role_id;
    e.role_slot = fake_role_slot;
    e.role_suppress_facets = fake_role_suppress_facets;
    e.ui_boundary = fake_ui_boundary;
    e.layout_set = fake_layout_set;
    e.layout_slot = fake_layout_slot;
    e.layout_slot_skin = fake_layout_slot_skin;
    e.layout_slot_overlay = fake_layout_slot_overlay;
    e.layout_scrollbar = fake_layout_scrollbar;
    e.display_setting = fake_display_setting;
    e.display_setting_set = fake_display_setting_set;
    e.frame_preference = fake_frame_preference;
    e.frame_preference_set = fake_frame_preference_set;
    e.tab_active = fake_tab_active;
    e.tab_select = fake_tab_select;
    e.tab_enabled = fake_tab_enabled;
    e.obj_info = fake_obj_info;
    e.inv_slot = fake_inv_slot;
    e.inv_size = fake_inv_size;
    e.stat = fake_stat;
    e.stat_xp = fake_stat_xp;
    e.skill_name = fake_skill_name;
    e.run_energy = fake_run_energy;
    e.draw_select_canvas = fake_draw_select_canvas;
    e.image_publish = fake_image_publish;
    e.image_publish_argb = fake_image_publish_argb;
    e.image_read = fake_image_read;
    e.image_release = fake_image_release;
    e.obj_image = fake_obj_image;
    e.loot_source_next = fake_loot_source_next;
    e.loot_row_next = fake_loot_row_next;
    e.draw_image = fake_draw_image;
    e.hit_region = fake_hit_region;
    e.if_click = fake_if_click;
    e.menu_add = fake_menu_add;
    e.menu_drop = fake_menu_drop;
    e.obj_next = fake_obj_next;
    e.asset_read = fake_asset_read;
    e.asset_write = fake_asset_write;
    e.screenshot = fake_screenshot;
    e.layout_begin = fake_layout_begin;
    e.layout_end = fake_layout_end;
    e.model_publish = fake_model_publish;
    e.model_release = fake_model_release;
    e.mesh_create = fake_mesh_create;
    e.mesh_destroy = fake_mesh_destroy;
    e.mesh_clear = fake_mesh_clear;
    e.mesh_vertex = fake_mesh_vertex;
    e.mesh_face = fake_mesh_face;
    e.object_create = fake_object_create;
    e.object_destroy = fake_object_destroy;
    e.object_set_model = fake_object_set_model;
    e.object_recolor = fake_object_recolor;
    e.object_clear_recolors = fake_object_clear_recolors;
    e.object_set_anim = fake_object_set_anim;
    e.object_set_light = fake_object_set_light;
    e.object_set_position = fake_object_set_position;
    e.object_set_active = fake_object_set_active;
    e.object_ready = fake_object_ready;
    e.hsl_from_rgb = fake_hsl_from_rgb;
    e.hsl_to_rgb = fake_hsl_to_rgb;
    return e;
}

/* ------------------------------------------------ synthetic V2 instances */

struct V2ProbeState
{
    int starts;
    int marker;
    int ticks;
    int canvas_draws;
    struct ToriRS_SceneInstanceRef instance;
};

static void* g_v2_first_state[4];
static void* g_v2_latest_state[4];
static struct ToriRS_ApiV2* g_v2_api[4];
static int g_v2_starts[4];
static int g_v2_stops[4];
static int g_v2_zeroed_starts;
static int g_v2_typed_calls;
static int g_v2_placement_callbacks;
static int g_v2_panel_builds;
static int g_v2_panel_actions;
static int g_v2_select_actions;
static char g_v2_select_value[TORIRS_PLUGIN_SELECT_VALUE_MAX];
static int g_v2_panel_draws;
static int g_v2_frame_builds;
static int g_v2_frame_draws;
static int g_v2_frame_width;
static int g_v2_frame_canvas;
static int g_v2_node_actions;
static int g_v2_prefix_starts;

static char g_v2_option_label_a[] = "Same|label";
static char g_v2_option_label_missing[] = "Same|label";
static char g_v2_option_detail_missing[] = "Provider is not installed";
static struct ToriRS_SelectOption const V2_PANEL_OPTIONS[] = {
    { .struct_size = sizeof(struct ToriRS_SelectOption),
      .value = "auto",
      .label = g_v2_option_label_a,
      .enabled = true,
      .detail = "Uses the lane default" },
    { .struct_size = sizeof(struct ToriRS_SelectOption),
      .value = "missing/frame",
      .label = g_v2_option_label_missing,
      .enabled = false,
      .detail = g_v2_option_detail_missing },
    { .struct_size = sizeof(struct ToriRS_SelectOption),
      .value = "ready/frame",
      .label = "Ready",
      .enabled = true,
      .detail = "Available now" },
};

static void
v2_probe_start(
    struct ToriRS_ApiV2* api,
    void* state_ptr)
{
    struct V2ProbeState* state = state_ptr;
    struct ToriRS_PlayerSnapshot player;
    struct ToriRS_UiNodeInfo info = { .struct_size = sizeof(info) };
    struct ToriRS_UiContributionInfo contribution = {
        .struct_size = sizeof(contribution),
    };
    struct ToriRS_Rect placed;
    struct ToriRS_UiNodeRef own;
    struct ToriRS_UiNodeRef shared;
    struct ToriRS_PanelDescriptor panel = {
        .preferred_width = 320,
    };
    int marker = 0;

    if( state->starts == 0 )
        g_v2_zeroed_starts++;
    state->starts++;
    CHECK(api->config.get_int(api, "marker", &marker), "v2 start reads normalized config");
    state->marker = marker;
    CHECK(marker > 0 && marker < 4, "each v2 probe has a valid marker");
    if( marker <= 0 || marker >= 4 )
        return;
    if( !g_v2_first_state[marker] )
        g_v2_first_state[marker] = state;
    g_v2_latest_state[marker] = state;
    g_v2_api[marker] = api;
    g_v2_starts[marker]++;

    CHECK(api->core.screen(api) == TORIRS_SCREEN_GAME, "v2 core module reaches host");
    CHECK(api->world.local_player(api, &player), "v2 world module reaches host");
    shared = api->ui.ref(api, "frame.xp.drops");
    CHECK(
        api->ui.info(api, shared, &info) && (info.available_facets & TORIRS_UI_FACET_BOUNDS),
        "all enabled static contributions exist before the first v2 on_start");
    CHECK(
        api->placement.place(
            api, TORIRS_AREA_OVERLAY_SAFE, TORIRS_ANCHOR_TOP_LEFT, 20, 10, 2, &placed),
        "v2 placement module returns a composed safe position");
    CHECK(
        api->scene.instance_create(api, &state->instance) == TORIRS_RESULT_OK,
        "v2 typed scene instance is allocated");
    g_v2_typed_calls++;

    if( marker == 1 )
    {
        own = api->ui.ref(api, "status");
        CHECK(
            api->ui.info(api, own, &info) && info.visible,
            "plugin-private v2 UI resolves through the host hook");
        CHECK(
            api->ui.contribution_info(api, "status", TORIRS_UI_FACET_ALL, &contribution) &&
                contribution.state == TORIRS_UI_CONTRIBUTION_ACTIVE,
            "v2 contribution status resolves from the retained handle");
        {
            struct ToriRS_UiContributionInfo prefix;
            unsigned char* bytes = (unsigned char*)&prefix;
            uint32_t const capacity =
                offsetof(struct ToriRS_UiContributionInfo, conflict_plugin);

            memset(&prefix, 0xa5, sizeof(prefix));
            prefix.struct_size = capacity;
            CHECK(
                api->ui.contribution_info(
                    api, "status", TORIRS_UI_FACET_ALL, &prefix) &&
                    prefix.struct_size == capacity &&
                    prefix.state == TORIRS_UI_CONTRIBUTION_ACTIVE &&
                    bytes[capacity] == 0xa5,
                "contribution-info output never overruns an older caller's capacity");
            CHECK(
                api->ui.contribution_info(
                    api, "status", TORIRS_UI_FACET_ALL, &prefix) &&
                    prefix.struct_size == capacity && bytes[capacity] == 0xa5,
                "a reused contribution-info prefix retains its safe capacity");
        }
        CHECK(
            api->panel.request(api, &panel) == TORIRS_RESULT_OK,
            "v2 on_start can register a panel through the typed module");
    }
}

static void
v2_probe_stop(
    struct ToriRS_ApiV2* api,
    void* state_ptr)
{
    struct V2ProbeState* state = state_ptr;
    (void)api;
    CHECK(state && state->starts == 1, "v2 state remains alive through on_stop");
    if( state && state->marker > 0 && state->marker < 4 )
        g_v2_stops[state->marker]++;
}

static void
v2_probe_logic(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_TickEvent const* event)
{
    struct V2ProbeState* state = state_ptr;
    (void)api;
    state->ticks += event->cycle;
}

static void
v2_probe_canvas(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_DrawBuilder* draw)
{
    struct V2ProbeState* state = state_ptr;
    (void)api;
    CHECK(g_engine.draw_canvas == 1, "v2 canvas callback runs only inside the canvas draw scope");
    draw->rect(draw, (struct ToriRS_Rect){ state->marker, 1, 2, 3 }, 0xabcdefu, 255);
    state->canvas_draws++;
}

static void
v2_probe_placement(
    struct ToriRS_ApiV2* api,
    void* state,
    uint32_t revision)
{
    (void)api;
    (void)state;
    if( revision )
        g_v2_placement_callbacks++;
}

static void
v2_probe_ui_build(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_PanelBuilder* panel,
    int view)
{
    (void)api;
    (void)state;
    if( view != TORIRS_PANEL_VIEW_PAGE )
        return;
    panel->heading(panel, "V2 probe");
    panel->toggle(panel, "enabled", "Enabled", true);
    panel->select(
        panel,
        "frame",
        "Gameframe",
        "missing/frame",
        V2_PANEL_OPTIONS,
        (int)(sizeof(V2_PANEL_OPTIONS) / sizeof(V2_PANEL_OPTIONS[0])));
    panel->custom(panel, "chart", 96);
    g_v2_panel_builds++;
}

static void
v2_probe_ui_action(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_PanelActionEvent const* event)
{
    (void)api;
    (void)state;
    if( strcmp(event->id, "enabled") == 0 )
        g_v2_panel_actions++;
    else if( strcmp(event->id, "frame") == 0 )
    {
        g_v2_select_actions++;
        snprintf(
            g_v2_select_value,
            sizeof(g_v2_select_value),
            "%s",
            event->text ? event->text : "");
    }
}

static void
v2_probe_ui_draw(
    struct ToriRS_ApiV2* api,
    void* state,
    char const* node,
    struct ToriRS_DrawBuilder* draw)
{
    (void)api;
    (void)state;
    if( strcmp(node, "chart") == 0 )
    {
        draw->rect(draw, (struct ToriRS_Rect){ 0, 0, 10, 10 }, 0x123456u, 255);
        g_v2_panel_draws++;
    }
}

static enum ToriRS_CallbackResult
v2_probe_node_action(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_UiNodeRef node,
    char const* action)
{
    struct ToriRS_UiNodeInfo info = { .struct_size = sizeof(info) };
    (void)state;
    if( api->ui.info(api, node, &info) && strcmp(action, "inspect") == 0 )
    {
        g_v2_node_actions++;
        return TORIRS_CALLBACK_CONSUME;
    }
    return TORIRS_CALLBACK_CONTINUE;
}

static void
v2_prefix_start(struct ToriRS_ApiV2* api, void* state)
{
    (void)api;
    (void)state;
    g_v2_prefix_starts++;
}

static enum ToriRS_FrameBuildResult
v2_probe_frame_build(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_FrameBuilder* frame,
    struct ToriRS_FrameBuildContext const* context)
{
    struct V2ProbeState* state = state_ptr;
    struct ToriRS_UiNode housing = {
        .struct_size = sizeof(housing),
        .bounds = { 610, 8, 170, 170 },
        .parent = "frame.minimap",
        .anchor = TORIRS_ANCHOR_TOP_LEFT,
        .paint_order = TORIRS_UI_PAINT_AFTER_PARENT,
        .flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED | TORIRS_UI_NODE_BLOCKS_OVERLAY,
        .image = { 0 },
        .clip = TORIRS_UI_CLIP_PARENT,
        .state_image_mask = 1u << TORIRS_UI_VISUAL_HOVER,
        .state_images = { [TORIRS_UI_VISUAL_HOVER] = { 0 } },
        .label = "Map",
        .label_x = 4,
        .label_y = 5,
        .hit_rect_mode = TORIRS_UI_HIT_RECT_CUSTOM,
        .hit_rect = { 608, 6, 174, 174 },
        .action_count = 2,
        .actions = { "activate", "inspect" },
    };
    (void)api;
    CHECK(state && state->marker == 3, "selected frame receives its own v2 state");
    CHECK(strcmp(context->offer_id, "test") == 0, "frame build receives local offer id");
    g_v2_frame_width = context->logical_canvas.width;
    g_v2_frame_canvas = context->canvas;
    frame->surface(frame, TORIRS_SURFACE_VIEWPORT, (struct ToriRS_Rect){ 0, 0, 600, 500 });
    frame->surface(frame, TORIRS_SURFACE_MINIMAP, (struct ToriRS_Rect){ 620, 10, 150, 150 });
    frame->ui_node(frame, "frame.minimap.housing", &housing);
    g_v2_frame_builds++;
    return TORIRS_FRAME_READY;
}

static void
v2_probe_frame_draw(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_DrawBuilder* draw)
{
    (void)api;
    (void)state;
    CHECK(g_engine.draw_canvas == 2, "v2 frame callback runs in the frame draw scope");
    draw->rect(draw, (struct ToriRS_Rect){ 0, 0, 5, 5 }, 0x010203u, 255);
    g_v2_frame_draws++;
}

static struct ToriRS_ConfigItem const V2_CONFIG_A_ITEMS[] = {
    { .key = "marker", .label = "Marker", .type = TORIRS_CONFIG_INT, .default_value = "1" },
    { 0 },
};
static struct ToriRS_ConfigItem const V2_CONFIG_B_ITEMS[] = {
    { .key = "marker", .label = "Marker", .type = TORIRS_CONFIG_INT, .default_value = "2" },
    { 0 },
};
static struct ToriRS_ConfigItem const V2_CONFIG_FRAME_ITEMS[] = {
    { .key = "marker", .label = "Marker", .type = TORIRS_CONFIG_INT, .default_value = "3" },
    { 0 },
};
static struct ToriRS_ConfigSchema const V2_CONFIG_A = { .struct_size = sizeof(V2_CONFIG_A),
                                                        .items = V2_CONFIG_A_ITEMS };
static struct ToriRS_ConfigSchema const V2_CONFIG_B = { .struct_size = sizeof(V2_CONFIG_B),
                                                        .items = V2_CONFIG_B_ITEMS };
static struct ToriRS_ConfigSchema const V2_CONFIG_FRAME = { .struct_size = sizeof(V2_CONFIG_FRAME),
                                                            .items = V2_CONFIG_FRAME_ITEMS };

static struct ToriRS_UiContribution const V2_UI_A[] = {
    {
        .struct_size = sizeof(struct ToriRS_UiContribution),
        .node = "status",
        .mode = TORIRS_UI_REPLACE_OR_PROVIDE,
        .facets = TORIRS_UI_FACET_ALL,
        .value = {
            .struct_size = sizeof(struct ToriRS_UiNode),
            .bounds = { 4, 5, 20, 10 },
            .parent = "frame.viewport",
            .anchor = TORIRS_ANCHOR_TOP_LEFT,
            .paint_order = TORIRS_UI_PAINT_AFTER_PARENT,
            .flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED,
            .image = { 0 },
            .clip = TORIRS_UI_CLIP_PARENT,
            .state_image_mask = 1u << TORIRS_UI_VISUAL_HOVER,
            .state_images = { [TORIRS_UI_VISUAL_HOVER] = { 72 } },
            .label = "Status",
            .label_x = 3,
            .label_y = 4,
            .hit_rect_mode = TORIRS_UI_HIT_RECT_CUSTOM,
            .hit_rect = { 2, 3, 24, 14 },
            .action_count = 2,
            .actions = { "activate", "inspect" },
        },
    },
    { .struct_size = sizeof(struct ToriRS_UiContribution) },
};

static struct ToriRS_UiContribution const V2_UI_B[] = {
    {
        .struct_size = sizeof(struct ToriRS_UiContribution),
        .node = "frame.xp.drops",
        .mode = TORIRS_UI_REPLACE_OR_PROVIDE,
        .facets = TORIRS_UI_FACET_BOUNDS,
        .value = {
            .struct_size = sizeof(struct ToriRS_UiNode),
            .bounds = { 30, 30, 40, 20 },
            .parent = "frame.viewport",
            .anchor = TORIRS_ANCHOR_TOP_LEFT,
            .paint_order = TORIRS_UI_PAINT_AFTER_PARENT,
        },
    },
    { .struct_size = sizeof(struct ToriRS_UiContribution) },
};

static struct ToriRS_FrameOffer const V2_FRAME_OFFERS[] = {
    {
     .struct_size = sizeof(struct ToriRS_FrameOffer),
     .id = "test",
     .title = "V2 Test Frame",
     .canvas = TORIRS_FRAME_CANVAS_WINDOW,
     .min_width = 640,
     .min_height = 480,
     .build = v2_probe_frame_build,
     .draw = v2_probe_frame_draw,
     },
    { .struct_size = sizeof(struct ToriRS_FrameOffer) },
};

static struct ToriRS_PluginDefV2 const V2_PROBE_A = {
    .struct_size = sizeof(V2_PROBE_A),
    .id = "v2-probe-a",
    .title = "V2 Probe A",
    .version = "2.0.0",
    .state_size = sizeof(struct V2ProbeState),
    .config = &V2_CONFIG_A,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = v2_probe_start,
        .on_stop = v2_probe_stop,
        .on_logic_tick = v2_probe_logic,
        .on_draw_canvas = v2_probe_canvas,
        .on_ui_build = v2_probe_ui_build,
        .on_ui_action = v2_probe_ui_action,
        .on_ui_draw = v2_probe_ui_draw,
        .on_placement_changed = v2_probe_placement,
        .on_ui_node_action = v2_probe_node_action,
    },
    .ui_contributions = V2_UI_A,
};

static struct ToriRS_PluginDefV2 const V2_PROBE_B = {
    .struct_size = sizeof(V2_PROBE_B),
    .id = "v2-probe-b",
    .title = "V2 Probe B",
    .version = "2.0.0",
    .state_size = sizeof(struct V2ProbeState),
    .config = &V2_CONFIG_B,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = v2_probe_start,
        .on_stop = v2_probe_stop,
        .on_logic_tick = v2_probe_logic,
        .on_draw_canvas = v2_probe_canvas,
        .on_ui_build = v2_probe_ui_build,
        .on_ui_action = v2_probe_ui_action,
        .on_ui_draw = v2_probe_ui_draw,
        .on_placement_changed = v2_probe_placement,
    },
    .ui_contributions = V2_UI_B,
    .flags = TORIRS_PLUGIN_V2_RUNTIME_HOST,
};

static struct ToriRS_PluginDefV2 const V2_FRAME_PROVIDER = {
    .struct_size = sizeof(V2_FRAME_PROVIDER),
    .id = "v2-frame",
    .title = "V2 Frame",
    .version = "2.0.0",
    .state_size = sizeof(struct V2ProbeState),
    .config = &V2_CONFIG_FRAME,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = v2_probe_start,
        .on_stop = v2_probe_stop,
    },
    .frames = V2_FRAME_OFFERS,
    .flags = TORIRS_PLUGIN_V2_DISABLED_BY_DEFAULT,
};

struct V2SeamResults
{
    int touch;
    int browser;
    int web;
    int unknown;
    enum ToriRS_AssetState raw_initial;
    enum ToriRS_AssetState image_initial;
    enum ToriRS_AssetState model_initial;
    enum ToriRS_AssetState missing_initial;
    enum ToriRS_AssetState bad_image_initial;
    enum ToriRS_AssetState invalid;
    enum ToriRS_AssetState raw_final;
    enum ToriRS_AssetState image_final;
    enum ToriRS_AssetState model_final;
    enum ToriRS_AssetState missing_final;
    enum ToriRS_AssetState bad_image_final;
    enum ToriRS_AssetState model_budget;
    struct ToriRS_ImageRef image;
    struct ToriRS_ModelRef model;
    int bytes_ready;
};

static struct V2SeamResults g_v2_seam;

static void
v2_seam_start(struct ToriRS_ApiV2* api, void* state)
{
    struct ToriRS_ImageRef bad_image = { 0 };
    (void)state;
    g_v2_seam.touch = api->core.capability(api, "touch");
    g_v2_seam.browser = api->core.capability(api, "browser");
    g_v2_seam.web = api->core.capability(api, "web");
    g_v2_seam.unknown = api->core.capability(api, "telepathy");
    g_v2_seam.raw_initial = api->assets.request(api, "raw.bin");
    g_v2_seam.image_initial = api->assets.image(api, "image.bin", &g_v2_seam.image);
    g_v2_seam.model_initial = api->assets.model(api, "model.bin", &g_v2_seam.model);
    g_v2_seam.missing_initial = api->assets.request(api, "missing.bin");
    g_v2_seam.bad_image_initial =
        api->assets.image(api, "bad-image.bin", &bad_image);
    g_v2_seam.invalid = api->assets.request(api, "../invalid");
}

static void
v2_seam_logic(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_TickEvent const* event)
{
    struct ToriRS_ImageRef image = { 0 };
    struct ToriRS_ModelRef model = { 0 };
    void const* bytes = NULL;
    size_t size = 0;
    (void)state;
    (void)event;
    g_v2_seam.raw_final = api->assets.request(api, "raw.bin");
    g_v2_seam.image_final = api->assets.image(api, "image.bin", &image);
    g_v2_seam.model_final = api->assets.model(api, "model.bin", &model);
    g_v2_seam.missing_final = api->assets.request(api, "missing.bin");
    g_v2_seam.bad_image_final =
        api->assets.image(api, "bad-image.bin", &image);
    g_v2_seam.bytes_ready =
        api->assets.bytes(api, "raw.bin", &bytes, &size) && bytes && size == 4;

    g_v2_seam.model_budget = TORIRS_ASSET_PENDING;
    for( int i = 0; i < TORIRS_PLUGIN_MODELS_MAX; i++ )
    {
        char name[32];
        snprintf(name, sizeof(name), "budget-%d.model", i);
        g_v2_seam.model_budget = api->assets.model(api, name, &model);
        if( g_v2_seam.model_budget == TORIRS_ASSET_BUDGET )
            break;
    }
}

static struct ToriRS_PluginDefV2 const V2_SEAM_PROBE = {
    .struct_size = sizeof(V2_SEAM_PROBE),
    .id = "v2-seam-probe",
    .title = "V2 Seam Probe",
    .version = "2.0.0",
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = v2_seam_start,
        .on_logic_tick = v2_seam_logic,
    },
};

/* ------------------------------------------------ resource-token ABA probe */

struct V2AbaState
{
    struct ToriRS_ImageRef image;
    struct ToriRS_ModelRef model;
    struct ToriRS_MeshRef mesh;
    struct ToriRS_SceneInstanceRef instance;
    int phase;
};

struct V2AbaResults
{
    struct ToriRS_ImageRef image_old;
    struct ToriRS_ImageRef image_new;
    struct ToriRS_ModelRef model_old;
    struct ToriRS_ModelRef model_new;
    struct ToriRS_MeshRef mesh_old;
    struct ToriRS_MeshRef mesh_new;
    struct ToriRS_SceneInstanceRef instance_old;
    struct ToriRS_SceneInstanceRef instance_new;
    enum ToriRS_Result ui_install;
    enum ToriRS_Result ui_stale;
    enum ToriRS_Result mesh_stale;
    enum ToriRS_Result instance_stale;
    enum ToriRS_Result model_stale;
    int image_stale_size;
    int image_new_size;
    int new_mesh_ok;
    int new_instance_ok;
    int new_model_ok;
    struct ToriRS_ImageRef reload_image_old;
    struct ToriRS_ImageRef reload_image_new;
    struct ToriRS_ModelRef reload_model_old;
    struct ToriRS_ModelRef reload_model_new;
    struct ToriRS_MeshRef reload_mesh_old;
    struct ToriRS_MeshRef reload_mesh_new;
    struct ToriRS_SceneInstanceRef reload_instance_old;
    struct ToriRS_SceneInstanceRef reload_instance_new;
    enum ToriRS_Result reload_mesh_stale;
    enum ToriRS_Result reload_instance_stale;
    enum ToriRS_Result reload_model_stale;
    int reload_image_stale_size;
    int reload_new_mesh_ok;
    int reload_new_instance_ok;
    int reload_new_model_ok;
};

static struct V2AbaResults g_v2_aba;
static int g_v2_aba_starts;
static struct ToriRS_ApiV2* g_v2_aba_api;

static void
v2_aba_start(struct ToriRS_ApiV2* api, void* state_ptr)
{
    struct V2AbaState* state = state_ptr;
    g_v2_aba_api = api;

    (void)api->assets.image(api, "aba-old.png", &state->image);
    (void)api->assets.model(api, "aba-old.model", &state->model);
    (void)api->scene.mesh_create(api, &state->mesh);
    (void)api->scene.instance_create(api, &state->instance);
    g_v2_aba_starts++;
    if( g_v2_aba_starts > 1 )
    {
        int width = 0;
        int height = 0;

        g_v2_aba.reload_image_new = state->image;
        g_v2_aba.reload_model_new = state->model;
        g_v2_aba.reload_mesh_new = state->mesh;
        g_v2_aba.reload_instance_new = state->instance;
        g_v2_aba.reload_image_stale_size = api->assets.image_size(
            api, g_v2_aba.reload_image_old, &width, &height);
        g_v2_aba.reload_mesh_stale =
            api->scene.mesh_vertex(api, g_v2_aba.reload_mesh_old, 1, 2, 3);
        g_v2_aba.reload_instance_stale = api->scene.instance_position(
            api, g_v2_aba.reload_instance_old, 3200, 3201, 0, 0, 0);
        g_v2_aba.reload_model_stale = api->scene.instance_model(
            api, state->instance, g_v2_aba.reload_model_old);

        api->assets.image_release(api, g_v2_aba.reload_image_old);
        api->assets.model_release(api, g_v2_aba.reload_model_old);
        api->scene.mesh_destroy(api, g_v2_aba.reload_mesh_old);
        api->scene.instance_active(api, g_v2_aba.reload_instance_old, true);
        api->scene.instance_destroy(api, g_v2_aba.reload_instance_old);
        g_v2_aba.reload_new_mesh_ok =
            api->scene.mesh_vertex(api, state->mesh, 4, 5, 6) == TORIRS_RESULT_OK;
        g_v2_aba.reload_new_instance_ok =
            api->scene.instance_position(api, state->instance, 3202, 3203, 0, 0, 0) ==
            TORIRS_RESULT_OK;
        g_v2_aba.reload_new_model_ok =
            api->scene.instance_model(api, state->instance, state->model) == TORIRS_RESULT_OK;
    }
}

static void
v2_aba_logic(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_TickEvent const* event)
{
    struct V2AbaState* state = state_ptr;
    struct ToriRS_UiNode appearance = {
        .struct_size = sizeof(appearance),
        .flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED,
    };
    struct ToriRS_UiNodeRef const badge = api->ui.ref(api, "badge");
    int width = 0;
    int height = 0;

    (void)event;
    if( state->phase == 0 )
    {
        struct ToriRS_ImageRef same_image = { 0 };
        struct ToriRS_ModelRef same_model = { 0 };

        (void)api->assets.image(api, "aba-old.png", &same_image);
        (void)api->assets.model(api, "aba-old.model", &same_model);
        CHECK(
            same_image.value == state->image.value && same_model.value == state->model.value,
            "host requests for the same live resources preserve their current tokens");
        appearance.image = state->image;
        g_v2_aba.ui_install =
            api->ui.update(api, badge, TORIRS_UI_FACET_APPEARANCE, &appearance);
        state->phase = 1;
        return;
    }
    if( state->phase == 1 )
    {
        g_v2_aba.image_old = state->image;
        g_v2_aba.model_old = state->model;
        g_v2_aba.mesh_old = state->mesh;
        g_v2_aba.instance_old = state->instance;

        api->assets.image_release(api, state->image);
        api->assets.model_release(api, state->model);
        api->scene.mesh_destroy(api, state->mesh);
        api->scene.instance_destroy(api, state->instance);

        (void)api->assets.image(api, "aba-new.png", &state->image);
        (void)api->assets.model(api, "aba-new.model", &state->model);
        (void)api->scene.mesh_create(api, &state->mesh);
        (void)api->scene.instance_create(api, &state->instance);
        g_v2_aba.image_new = state->image;
        g_v2_aba.model_new = state->model;
        g_v2_aba.mesh_new = state->mesh;
        g_v2_aba.instance_new = state->instance;

        appearance.image = g_v2_aba.image_old;
        g_v2_aba.ui_stale =
            api->ui.update(api, badge, TORIRS_UI_FACET_APPEARANCE, &appearance);
        g_v2_aba.image_stale_size = api->assets.image_size(
            api, g_v2_aba.image_old, &width, &height);
        g_v2_aba.mesh_stale =
            api->scene.mesh_vertex(api, g_v2_aba.mesh_old, 1, 2, 3);
        g_v2_aba.instance_stale = api->scene.instance_position(
            api, g_v2_aba.instance_old, 3200, 3201, 0, 0, 0);
        g_v2_aba.model_stale = api->scene.instance_model(
            api, g_v2_aba.instance_new, g_v2_aba.model_old);

        /* Void stale operations must be no-ops too. In particular they must
         * not destroy/release the just-reallocated same internal slots. */
        api->assets.image_release(api, g_v2_aba.image_old);
        api->assets.model_release(api, g_v2_aba.model_old);
        api->scene.mesh_destroy(api, g_v2_aba.mesh_old);
        api->scene.instance_active(api, g_v2_aba.instance_old, true);
        api->scene.instance_destroy(api, g_v2_aba.instance_old);
        state->phase = 2;
        return;
    }

    g_v2_aba.image_new_size =
        api->assets.image_size(api, state->image, &width, &height);
    g_v2_aba.new_mesh_ok =
        api->scene.mesh_vertex(api, state->mesh, 4, 5, 6) == TORIRS_RESULT_OK;
    g_v2_aba.new_instance_ok =
        api->scene.instance_position(api, state->instance, 3202, 3203, 0, 0, 0) ==
        TORIRS_RESULT_OK;
    g_v2_aba.new_model_ok =
        api->scene.instance_model(api, state->instance, state->model) == TORIRS_RESULT_OK;
}

static enum ToriRS_FrameBuildResult
v2_aba_frame(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_FrameBuilder* frame,
    struct ToriRS_FrameBuildContext const* context)
{
    struct V2AbaState* state = state_ptr;
    struct ToriRS_FrameSkin skin = {
        .struct_size = sizeof(skin),
        .image = state->image,
    };
    struct ToriRS_UiNode named = {
        .struct_size = sizeof(named),
        .bounds = { 600, 0, 32, 32 },
        .parent = "frame.viewport",
        .flags = TORIRS_UI_NODE_VISIBLE,
        .image = state->image,
    };

    (void)api;
    (void)context;
    frame->surface(frame, TORIRS_SURFACE_VIEWPORT, (struct ToriRS_Rect){ 0, 0, 640, 480 });
    frame->surface(frame, TORIRS_SURFACE_COMPASS, (struct ToriRS_Rect){ 600, 0, 32, 32 });
    frame->skin(frame, TORIRS_SURFACE_COMPASS, &skin);
    frame->ui_node(frame, "aba-art", &named);
    return TORIRS_FRAME_READY;
}

static struct ToriRS_UiContribution const V2_ABA_UI[] = {
    {
        .struct_size = sizeof(struct ToriRS_UiContribution),
        .node = "badge",
        .mode = TORIRS_UI_REPLACE_OR_PROVIDE,
        .facets = TORIRS_UI_FACET_ALL,
        .value = {
            .struct_size = sizeof(struct ToriRS_UiNode),
            .bounds = { 10, 10, 26, 26 },
            .parent = "frame.viewport",
            .flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED,
        },
    },
    { .struct_size = sizeof(struct ToriRS_UiContribution) },
};

static struct ToriRS_FrameOffer const V2_ABA_FRAMES[] = {
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "frame",
        .title = "ABA frame",
        .canvas = TORIRS_FRAME_CANVAS_WINDOW,
        .min_width = 640,
        .min_height = 480,
        .build = v2_aba_frame,
    },
    { .struct_size = sizeof(struct ToriRS_FrameOffer) },
};

static struct ToriRS_PluginDefV2 const V2_ABA_PROBE = {
    .struct_size = sizeof(V2_ABA_PROBE),
    .id = "v2-aba",
    .title = "V2 ABA Probe",
    .version = "2.0.0",
    .state_size = sizeof(struct V2AbaState),
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = v2_aba_start,
        .on_logic_tick = v2_aba_logic,
    },
    .frames = V2_ABA_FRAMES,
    .ui_contributions = V2_ABA_UI,
    .flags = TORIRS_PLUGIN_V2_DISABLED_BY_DEFAULT,
};

/* A definition ending inside its final callback table exercises append-only
 * minor-version reads without granting access to any callback tail. */
static struct ToriRS_PluginDefV2 const V2_PREFIX_ONLY = {
    .struct_size = offsetof(struct ToriRS_PluginDefV2, callbacks) +
                   offsetof(struct ToriRS_PluginCallbacks, on_stop),
    .id = "v2-prefix",
    .title = "V2 Prefix",
    .version = "2.0.0",
    .callbacks = {
        .struct_size = offsetof(struct ToriRS_PluginCallbacks, on_stop),
        .on_start = v2_prefix_start,
    },
};

static int g_placement_v2_calls;
static int g_placement_v2_depth;
static int g_placement_v2_max_depth;
static int g_placement_v2_reenter;
static uint32_t g_placement_v2_revision;
static struct ToriRS_ApiV2* g_placement_v2_api;

static void
placement_v2_start(struct ToriRS_ApiV2* api, void* state)
{
    (void)state;
    g_placement_v2_api = api;
}

static void
placement_v2_changed(
    struct ToriRS_ApiV2* api,
    void* state,
    uint32_t revision)
{
    (void)state;
    g_placement_v2_depth++;
    if( g_placement_v2_depth > g_placement_v2_max_depth )
        g_placement_v2_max_depth = g_placement_v2_depth;
    g_placement_v2_calls++;
    g_placement_v2_revision = revision;
    CHECK(
        api->placement.revision(api) == revision,
        "on_placement_changed receives the committed placement revision");
    if( g_placement_v2_reenter )
    {
        g_placement_v2_reenter = 0;
        CHECK(
            api->placement.reserve(
                api, "inside-callback", TORIRS_AREA_OVERLAY_SAFE, TORIRS_EDGE_RIGHT, 3) ==
                TORIRS_RESERVE_OK,
            "a placement callback may restate a reservation for the next transaction");
    }
    g_placement_v2_depth--;
}

static struct ToriRS_PluginDefV2 const V2_PLACEMENT_PROBE = {
    .struct_size = sizeof(V2_PLACEMENT_PROBE),
    .id = "v2-placement-probe",
    .title = "V2 Placement Probe",
    .version = "2.0.0",
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = placement_v2_start,
        .on_placement_changed = placement_v2_changed,
    },
};

struct V2TeardownState
{
    uint32_t canary;
};

static int g_v2_teardown_stop_alive;
static int g_v2_teardown_placement_calls;
static int g_v2_teardown_after_shutdown;

static void
v2_teardown_start(struct ToriRS_ApiV2* api, void* state_ptr)
{
    struct V2TeardownState* state = state_ptr;

    state->canary = 0x7e4d0a11u;
    CHECK(
        api->placement.reserve(
            api, "teardown-strip", TORIRS_AREA_OVERLAY_SAFE, TORIRS_EDGE_LEFT, 2) ==
            TORIRS_RESERVE_OK,
        "teardown fixture owns a named reservation");
}

static void
v2_teardown_stop(struct ToriRS_ApiV2* api, void* state_ptr)
{
    struct V2TeardownState* state = state_ptr;

    g_v2_teardown_stop_alive = state && state->canary == 0x7e4d0a11u &&
                               api->core.screen(api) == TORIRS_SCREEN_GAME;
}

static void
v2_teardown_placement(struct ToriRS_ApiV2* api, void* state_ptr, uint32_t revision)
{
    struct V2TeardownState* state = state_ptr;

    (void)api;
    (void)revision;
    if( !state || state->canary != 0x7e4d0a11u )
        g_v2_teardown_after_shutdown++;
    else
        g_v2_teardown_placement_calls++;
}

static struct ToriRS_PluginDefV2 const V2_TEARDOWN_PROBE = {
    .struct_size = sizeof(V2_TEARDOWN_PROBE),
    .id = "v2-teardown-probe",
    .title = "V2 Teardown Probe",
    .version = "2.0.0",
    .state_size = sizeof(struct V2TeardownState),
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = v2_teardown_start,
        .on_stop = v2_teardown_stop,
        .on_placement_changed = v2_teardown_placement,
    },
};

/* ------------------------------------------------ retained v2 presenter */

static int g_present_draws;
static int g_present_actions;
static enum ToriRS_Result g_present_foreign_update;
static enum ToriRS_Result g_present_appearance_update;
static enum ToriRS_Result g_present_actions_update;
static char g_present_last_action[TORIRS_UI_ACTION_MAX];
static int g_present_order_count;
static char g_present_order[8][TORIRS_UI_LABEL_MAX];
static int g_present_reorder_requested;
static enum ToriRS_Result g_present_reorder_result;
static int g_present_visibility_request = -1;
static enum ToriRS_Result g_present_visibility_result;
static int g_present_ancestor_visibility_request = -1;
static enum ToriRS_Result g_present_ancestor_visibility_result;
static void
v2_present_start(struct ToriRS_ApiV2* api, void* state)
{
    struct ToriRS_UiNodeRef const node = api->ui.ref(api, "frame.orb.run");
    struct ToriRS_UiNode appearance = {
        .struct_size = sizeof(appearance),
        .flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ACTIVE,
        .image = { 0 },
        .label = "Updated winner",
        .label_x = 2,
        .label_y = 3,
    };
    struct ToriRS_UiNode actions = {
        .struct_size = sizeof(actions),
        .flags = TORIRS_UI_NODE_ENABLED,
        .hit_rect_mode = TORIRS_UI_HIT_RECT_CUSTOM,
        .hit_rect = { 65, 18, 20, 20 },
        .action_count = 2,
        .actions = { "inspect", "activate" },
    };
    struct ToriRS_UiNode foreign_image = appearance;

    (void)state;
    foreign_image.image.value = 1; /* typed handle 1 -> unowned internal slot 0 */
    g_present_foreign_update =
        api->ui.update(api, node, TORIRS_UI_FACET_APPEARANCE, &foreign_image);
    g_present_appearance_update =
        api->ui.update(api, node, TORIRS_UI_FACET_APPEARANCE, &appearance);
    g_present_actions_update = api->ui.update(api, node, TORIRS_UI_FACET_ACTIONS, &actions);
}

static void
v2_present_draw(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_UiNodeRef node,
    struct ToriRS_DrawBuilder* draw)
{
    struct ToriRS_UiNodeInfo info = { .struct_size = sizeof(info) };

    (void)state;
    CHECK(api->ui.info(api, node, &info), "presenter callback receives a live semantic ref");
    draw->rect(draw, info.bounds, 0x336699u, 255);
    if( g_present_order_count < (int)(sizeof(g_present_order) / sizeof(g_present_order[0])) )
        (void)snprintf(
            g_present_order[g_present_order_count++],
            sizeof(g_present_order[0]),
            "%s",
            info.label);
    g_present_draws++;
}

static enum ToriRS_CallbackResult
v2_present_action(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_UiNodeRef node,
    char const* action)
{
    (void)api;
    (void)state;
    (void)node;
    g_present_actions++;
    (void)snprintf(g_present_last_action, sizeof(g_present_last_action), "%s", action);
    return TORIRS_CALLBACK_CONSUME;
}

static struct ToriRS_UiContribution const V2_PRESENT_A_UI[] = {
    {
        .struct_size = sizeof(struct ToriRS_UiContribution),
        .node = "frame.orb.run",
        .mode = TORIRS_UI_MODIFY,
        .facets = TORIRS_UI_FACET_ALL,
        .value = {
            .struct_size = sizeof(struct ToriRS_UiNode),
            .bounds = { 72, 35, 20, 15 },
            .parent = "frame.orbs",
            .anchor = TORIRS_ANCHOR_TOP_LEFT,
            .paint_order = TORIRS_UI_PAINT_AFTER_PARENT,
            .flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED,
            .label = "Initial winner",
            .action = "activate",
            .clip = TORIRS_UI_CLIP_PARENT,
        },
    },
    { .struct_size = sizeof(struct ToriRS_UiContribution) },
};

static struct ToriRS_UiContribution const V2_PRESENT_B_UI[] = {
    {
        .struct_size = sizeof(struct ToriRS_UiContribution),
        .node = "frame.orb.run",
        .mode = TORIRS_UI_MODIFY,
        .facets = TORIRS_UI_FACET_ALL,
        .value = {
            .struct_size = sizeof(struct ToriRS_UiNode),
            .bounds = { 73, 36, 18, 12 },
            .parent = "frame.orbs",
            .anchor = TORIRS_ANCHOR_TOP_LEFT,
            .paint_order = TORIRS_UI_PAINT_AFTER_PARENT,
            .flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED,
            .label = "Conflicting provider",
            .action = "activate",
            .clip = TORIRS_UI_CLIP_PARENT,
        },
    },
    { .struct_size = sizeof(struct ToriRS_UiContribution) },
};

static struct ToriRS_PluginDefV2 const V2_PRESENT_A = {
    .struct_size = sizeof(V2_PRESENT_A),
    .id = "v2-present-a",
    .title = "V2 Present A",
    .version = "2.0.0",
    .ui_contributions = V2_PRESENT_A_UI,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = v2_present_start,
        .on_ui_node_draw = v2_present_draw,
        .on_ui_node_action = v2_present_action,
    },
};

static struct ToriRS_PluginDefV2 const V2_PRESENT_B = {
    .struct_size = sizeof(V2_PRESENT_B),
    .id = "v2-present-b",
    .title = "V2 Present B",
    .version = "2.0.0",
    .ui_contributions = V2_PRESENT_B_UI,
    .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks) },
};

static void
v2_present_reorder_logic(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_TickEvent const* event)
{
    struct ToriRS_UiNode actions = {
        .struct_size = sizeof(actions),
        .flags = TORIRS_UI_NODE_ENABLED,
        .hit_rect_mode = TORIRS_UI_HIT_RECT_CUSTOM,
        .hit_rect = { 65, 18, 20, 20 },
        .action_count = 2,
        .actions = { "second", "first" },
    };

    (void)state;
    (void)event;
    if( !g_present_reorder_requested )
        return;
    g_present_reorder_requested = 0;
    g_present_reorder_result = api->ui.update(
        api,
        api->ui.ref(api, "frame.orb.run"),
        TORIRS_UI_FACET_ACTIONS,
        &actions);
}

static void
v2_present_visibility_logic(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_TickEvent const* event)
{
    struct ToriRS_UiNode appearance = {
        .struct_size = sizeof(appearance),
        .label = "appearance-only",
    };

    (void)state;
    (void)event;
    if( g_present_visibility_request < 0 )
        return;
    if( g_present_visibility_request )
        appearance.flags = TORIRS_UI_NODE_VISIBLE;
    g_present_visibility_request = -1;
    g_present_visibility_result = api->ui.update(
        api,
        api->ui.ref(api, "frame.orb.run"),
        TORIRS_UI_FACET_APPEARANCE,
        &appearance);
}

static void
v2_present_ancestor_visibility_logic(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_TickEvent const* event)
{
    struct ToriRS_UiNode appearance = {
        .struct_size = sizeof(appearance),
        .label = "target",
    };

    (void)state;
    (void)event;
    if( g_present_ancestor_visibility_request < 0 )
        return;
    if( g_present_ancestor_visibility_request )
        appearance.flags = TORIRS_UI_NODE_VISIBLE;
    g_present_ancestor_visibility_request = -1;
    g_present_ancestor_visibility_result = api->ui.update(
        api,
        api->ui.ref(api, "frame.orb.run"),
        TORIRS_UI_FACET_APPEARANCE,
        &appearance);
}

static struct ToriRS_UiContribution const V2_PRESENT_APPEARANCE_UI[] = {
    {
        .struct_size = sizeof(struct ToriRS_UiContribution),
        .node = "frame.orb.run",
        .mode = TORIRS_UI_MODIFY,
        .facets = TORIRS_UI_FACET_APPEARANCE,
        .value = {
            .struct_size = sizeof(struct ToriRS_UiNode),
            .flags = TORIRS_UI_NODE_VISIBLE,
            .label = "appearance-only",
        },
    },
    { .struct_size = sizeof(struct ToriRS_UiContribution) },
};

static struct ToriRS_UiContribution const V2_PRESENT_ACTIONS_UI[] = {
    {
        .struct_size = sizeof(struct ToriRS_UiContribution),
        .node = "frame.orb.run",
        .mode = TORIRS_UI_MODIFY,
        .facets = TORIRS_UI_FACET_ACTIONS,
        .value = {
            .struct_size = sizeof(struct ToriRS_UiNode),
            .flags = TORIRS_UI_NODE_ENABLED,
            .hit_rect_mode = TORIRS_UI_HIT_RECT_CUSTOM,
            .hit_rect = { 65, 18, 20, 20 },
            .action_count = 2,
            .actions = { "first", "second" },
        },
    },
    { .struct_size = sizeof(struct ToriRS_UiContribution) },
};

static struct ToriRS_PluginDefV2 const V2_PRESENT_APPEARANCE = {
    .struct_size = sizeof(V2_PRESENT_APPEARANCE),
    .id = "v2-present-appearance",
    .title = "V2 Present Appearance",
    .version = "2.0.0",
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_logic_tick = v2_present_visibility_logic,
        .on_ui_node_draw = v2_present_draw,
    },
    .ui_contributions = V2_PRESENT_APPEARANCE_UI,
    .flags = TORIRS_PLUGIN_V2_DISABLED_BY_DEFAULT,
};

static struct ToriRS_PluginDefV2 const V2_PRESENT_ACTIONS = {
    .struct_size = sizeof(V2_PRESENT_ACTIONS),
    .id = "v2-present-actions",
    .title = "V2 Present Actions",
    .version = "2.0.0",
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_logic_tick = v2_present_reorder_logic,
        .on_ui_node_action = v2_present_action,
    },
    .ui_contributions = V2_PRESENT_ACTIONS_UI,
    .flags = TORIRS_PLUGIN_V2_DISABLED_BY_DEFAULT,
};

static struct ToriRS_UiContribution const V2_PRESENT_NESTED_UI[] = {
    {
        .struct_size = sizeof(struct ToriRS_UiContribution),
        .node = "frame.orb.run",
        .mode = TORIRS_UI_MODIFY,
        .facets = TORIRS_UI_FACET_APPEARANCE,
        .value = {
            .struct_size = sizeof(struct ToriRS_UiNode),
            .flags = TORIRS_UI_NODE_VISIBLE,
            .label = "target",
        },
    },
    {
        .struct_size = sizeof(struct ToriRS_UiContribution),
        .node = "present.before",
        .mode = TORIRS_UI_REPLACE_OR_PROVIDE,
        .facets = TORIRS_UI_FACET_BOUNDS | TORIRS_UI_FACET_APPEARANCE,
        .value = {
            .struct_size = sizeof(struct ToriRS_UiNode),
            .bounds = { 0, 0, 4, 4 },
            .parent = "frame.orb.run",
            .anchor = TORIRS_ANCHOR_TOP_LEFT,
            .paint_order = TORIRS_UI_PAINT_BEFORE_PARENT,
            .flags = TORIRS_UI_NODE_VISIBLE,
            .label = "before",
        },
    },
    {
        .struct_size = sizeof(struct ToriRS_UiContribution),
        .node = "present.before.grandchild",
        .mode = TORIRS_UI_REPLACE_OR_PROVIDE,
        .facets = TORIRS_UI_FACET_BOUNDS | TORIRS_UI_FACET_APPEARANCE,
        .value = {
            .struct_size = sizeof(struct ToriRS_UiNode),
            .bounds = { 1, 1, 2, 2 },
            .parent = "present.before",
            .anchor = TORIRS_ANCHOR_TOP_LEFT,
            .paint_order = TORIRS_UI_PAINT_BEFORE_PARENT,
            .flags = TORIRS_UI_NODE_VISIBLE,
            .label = "grand-before",
        },
    },
    {
        .struct_size = sizeof(struct ToriRS_UiContribution),
        .node = "present.after",
        .mode = TORIRS_UI_REPLACE_OR_PROVIDE,
        .facets = TORIRS_UI_FACET_ALL,
        .value = {
            .struct_size = sizeof(struct ToriRS_UiNode),
            .bounds = { 2, 2, 4, 4 },
            .parent = "frame.orb.run",
            .anchor = TORIRS_ANCHOR_TOP_LEFT,
            .paint_order = TORIRS_UI_PAINT_AFTER_PARENT,
            .flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED,
            .label = "after",
            .hit_rect_mode = TORIRS_UI_HIT_RECT_CUSTOM,
            .hit_rect = { 74, 37, 4, 4 },
            .action_count = 1,
            .actions = { "nested-action" },
        },
    },
    { .struct_size = sizeof(struct ToriRS_UiContribution) },
};

static struct ToriRS_PluginDefV2 const V2_PRESENT_NESTED = {
    .struct_size = sizeof(V2_PRESENT_NESTED),
    .id = "v2-present-nested",
    .title = "V2 Present Nested",
    .version = "2.0.0",
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_logic_tick = v2_present_ancestor_visibility_logic,
        .on_ui_node_draw = v2_present_draw,
        .on_ui_node_action = v2_present_action,
    },
    .ui_contributions = V2_PRESENT_NESTED_UI,
    .flags = TORIRS_PLUGIN_V2_DISABLED_BY_DEFAULT,
};

/* ------------------------------------------------------------------ tests */

int
main(void)
{
    struct ToriRS_PluginEngine engine;

    /* ---- V2 registration and lifecycle ---------------------------------- */
    {
        struct ToriRS_PluginHost* hv2;
        struct ToriRS_UiNodeInfo ui_info = { .struct_size = sizeof(ui_info) };
        struct ToriRS_UiNodeRef private_ref;
        struct ToriRS_UiNodeRef housing_ref;
        struct ToriRS_FrameSelection selection;
        struct ToriRS_PanelWidget const* widget;
        uint32_t generation;
        uint32_t presentation_rebuilds;
        int draw_before;
        int placement_before;
        int a2;
        int b2;
        int frame2;
        int prefix2;

        memset(&g_engine, 0, sizeof(g_engine));
        memset(g_slot_x, 0, sizeof(g_slot_x));
        memset(g_slot_y, 0, sizeof(g_slot_y));
        memset(g_slot_w, 0, sizeof(g_slot_w));
        memset(g_slot_h, 0, sizeof(g_slot_h));
        memset(g_v2_first_state, 0, sizeof(g_v2_first_state));
        memset(g_v2_latest_state, 0, sizeof(g_v2_latest_state));
        memset(g_v2_api, 0, sizeof(g_v2_api));
        memset(g_v2_starts, 0, sizeof(g_v2_starts));
        memset(g_v2_stops, 0, sizeof(g_v2_stops));
        g_v2_zeroed_starts = 0;
        g_v2_typed_calls = 0;
        g_v2_placement_callbacks = 0;
        g_v2_panel_builds = 0;
        g_v2_panel_actions = 0;
        g_v2_select_actions = 0;
        g_v2_select_value[0] = '\0';
        g_v2_panel_draws = 0;
        g_v2_frame_builds = 0;
        g_v2_frame_draws = 0;
        g_v2_node_actions = 0;
        g_v2_prefix_starts = 0;
        g_screen_now = TORIRS_SCREEN_GAME;
        g_lane_game = TORIRS_GAME_RS2;
        g_role_name = NULL;
        g_member_slot = -1;
        g_member_no = -1;
        g_slot_w[TORIRS_HOST_SURFACE_CANVAS] = 900;
        g_slot_h[TORIRS_HOST_SURFACE_CANVAS] = 600;
        g_slot_w[TORIRS_HOST_SURFACE_VIEWPORT] = 900;
        g_slot_h[TORIRS_HOST_SURFACE_VIEWPORT] = 600;
        g_slot_x[TORIRS_HOST_SURFACE_MINIMAP] = 620;
        g_slot_y[TORIRS_HOST_SURFACE_MINIMAP] = 10;
        g_slot_w[TORIRS_HOST_SURFACE_MINIMAP] = 150;
        g_slot_h[TORIRS_HOST_SURFACE_MINIMAP] = 150;
        snprintf(
            g_engine.frame_preference, sizeof(g_engine.frame_preference), "%s", "v2-frame/test");
        g_engine.frame_preference_present = 1;
        g_engine.frame_migration_version = 1;
        engine = fake_engine();
        hv2 = PluginHost_New(&engine);

        a2 = PluginHost_RegisterV2(hv2, &V2_PROBE_A);
        b2 = PluginHost_RegisterV2(hv2, &V2_PROBE_B);
        frame2 = PluginHost_RegisterV2(hv2, &V2_FRAME_PROVIDER);
        prefix2 = PluginHost_RegisterV2(hv2, &V2_PREFIX_ONLY);
        CHECK(a2 == 0 && b2 == 1 && frame2 == 2, "v2 registration shares host indexing");
        CHECK(prefix2 == 3, "a definition ending in a shorter callback-table prefix registers");
        CHECK(
            strcmp(PluginHost_Name(hv2, a2), "v2-probe-a") == 0 &&
                strcmp(PluginHost_Title(hv2, a2), "V2 Probe A") == 0 &&
                PluginHost_ConfigCount(hv2, a2) == 1,
            "v2 identity, title, and config schema normalize into host metadata");
        CHECK(PluginHost_IsRuntimeHost(hv2, b2), "v2 runtime-host flag normalizes");
        CHECK(
            PluginHost_IsEssential(hv2, frame2),
            "a v2 frame provider has host-controlled lifetime and no switch");

        PluginHost_Start(hv2);
        PluginHost_Start(hv2);
        CHECK(
            g_v2_starts[1] == 1 && g_v2_starts[2] == 1 && g_v2_starts[3] == 1,
            "automatic v2 on_start dispatch happens exactly once");
        CHECK(
            g_v2_prefix_starts == 1,
            "a shorter callback table dispatches its declared prefix without reading its tail");
        CHECK(
            g_v2_zeroed_starts == 3 && g_v2_first_state[1] != g_v2_first_state[2] &&
                g_v2_first_state[2] != g_v2_first_state[3],
            "each v2 registration receives isolated zeroed state");
        CHECK(
            g_v2_typed_calls == 3 && g_engine.objects_live == 3,
            "typed module calls execute for every live instance");
        memset(&selection, 0, sizeof(selection));
        selection.struct_size = sizeof(selection);
        g_v2_api[1]->frame.selection(g_v2_api[1], &selection);        CHECK(
            strcmp(selection.active_id, "core/native") == 0 &&
                selection.status == TORIRS_FRAME_STATUS_LOADING &&
                PluginHost_FrameNeedsLayout(hv2) && g_engine.layout_owned == 0,
            "v2 offer conversion prepares a candidate without changing native policy");

        PluginHost_LogicTick(hv2, 7);
        CHECK(
            ((struct V2ProbeState*)g_v2_latest_state[1])->ticks == 7 &&
                ((struct V2ProbeState*)g_v2_latest_state[2])->ticks == 7,
            "automatic callback dispatch passes each instance its own state");
        PluginHost_ReconcileUi(hv2);
        presentation_rebuilds = PluginHost_UiPresentationRebuilds(hv2);
        g_role_name = "viewport";
        g_role_visible = 1;
        g_role_box[0] = 0;
        g_role_box[1] = 0;
        g_role_box[2] = 900;
        g_role_box[3] = 600;
        PluginHost_ReconcileUi(hv2);
        CHECK(
            PluginHost_UiPresentationRebuilds(hv2) == presentation_rebuilds + 1,
            "a previously unresolved native boundary becoming live rebuilds the compact presenter once");
        presentation_rebuilds = PluginHost_UiPresentationRebuilds(hv2);
        PluginHost_ReconcileUi(hv2);
        CHECK(
            PluginHost_UiPresentationRebuilds(hv2) == presentation_rebuilds,
            "steady late-bound roles do not rescan the full named registry");
        draw_before = g_engine.draw_items;
        PluginHost_DrawCanvas(hv2, 900, 600);
        CHECK(
            g_engine.draw_items == draw_before + 3 &&
                ((struct V2ProbeState*)g_v2_latest_state[1])->canvas_draws == 1 &&
                ((struct V2ProbeState*)g_v2_latest_state[2])->canvas_draws == 1,
            "v2 canvas callbacks and one retained named label use scoped builders");

        PluginHost_Layout(hv2, 900, 600);        CHECK(
            g_v2_frame_builds == 1 && g_v2_frame_width == 900 &&
                g_v2_frame_canvas == TORIRS_FRAME_CANVAS_WINDOW && g_engine.layout_begins == 1 &&
                g_engine.layout_ends == 1,
            "selected v2 offer builds once and atomically commits READY geometry");
        selection.struct_size = sizeof(selection);
        g_v2_api[1]->frame.selection(g_v2_api[1], &selection);        CHECK(
            strcmp(selection.active_id, "v2-frame/test") == 0 &&
                selection.status == TORIRS_FRAME_STATUS_ACTIVE && g_engine.layout_owned == 1 &&
                g_engine.layout_canvas == TORIRS_FRAME_CANVAS_WINDOW &&
                g_engine.layout_fixed_w == 640 && g_engine.layout_fixed_h == 480,
            "READY v2 geometry and its canvas policy publish together");
        PluginHost_LayoutChanged(hv2);
        housing_ref = PluginHost_UiRef(hv2, frame2, "frame.minimap.housing");
        CHECK(
            PluginHost_UiInfo(hv2, housing_ref, &ui_info) && ui_info.bounds.x == 610 &&
                ui_info.bounds.width == 170 && ui_info.clip == TORIRS_UI_CLIP_PARENT &&
                ui_info.state_images[TORIRS_UI_VISUAL_HOVER].value == 0 &&
                strcmp(ui_info.label, "Map") == 0 && ui_info.label_x == 4 &&
                ui_info.label_y == 5 && ui_info.action_count == 2 &&
                strcmp(ui_info.actions[1], "inspect") == 0 &&
                ui_info.hit_rect.width == 174,
            "frame-builder named nodes retain the complete canonical facet payload");
        draw_before = g_engine.draw_items;
        PluginHost_DrawFrame(hv2, 900, 600);
        CHECK(
            g_v2_frame_draws == 1 && g_engine.draw_items == draw_before + 1,
            "selected v2 offer receives a callback-scoped frame draw builder");

        CHECK(PluginHost_PanelHasPage(hv2, a2), "v2 on_start panel registration is retained");
        CHECK(PluginHost_PanelSelect(hv2, a2), "v2 panel can be selected");
        generation = PluginHost_PanelSelectionGeneration(hv2);
        CHECK(
            g_v2_panel_builds == 1 && PluginHost_PanelWidgetCount(hv2, generation) == 4,
            "v2 on_ui_build receives the semantic panel builder");
        CHECK(
            PluginHost_PanelLayout(
                hv2, generation, 320, 480, 1000, TORIRS_PANEL_SIZE_COMPACT, true, true),
            "v2 panel receives a visible neutral allocation");
        widget = PluginHost_PanelWidgetAt(hv2, generation, 1);
        CHECK(
            widget &&
                PluginHost_PanelDispatch(
                    hv2,
                    generation,
                    widget->serial,
                    1,
                    "enabled",
                    TORIRS_PANEL_ACTION_TOGGLE,
                    0,
                    NULL,
                    0,
                    0) &&
                g_v2_panel_actions == 1,
            "v2 panel action dispatch is owner scoped");
        widget = PluginHost_PanelWidgetAt(hv2, generation, 2);
        CHECK(
            widget && widget->structured_select && widget->select_option_count == 3 &&
                widget->selected == 1 &&
                strcmp(widget->selected_value, "missing/frame") == 0 &&
                strcmp(widget->select_options[0].label, "Same|label") == 0 &&
                strcmp(widget->select_options[1].label, "Same|label") == 0 &&
                !widget->select_options[1].enabled &&
                strcmp(widget->select_options[1].detail, "Provider is not installed") == 0,
            "the host retains copied structured values, duplicate delimiter labels, and detail");
        g_v2_option_label_missing[0] = 'X';
        g_v2_option_detail_missing[0] = 'X';
        CHECK(
            widget && strcmp(widget->select_options[1].label, "Same|label") == 0 &&
                strcmp(widget->select_options[1].detail, "Provider is not installed") == 0,
            "structured option strings are copied rather than borrowed from plugin storage");
        g_v2_option_label_missing[0] = 'S';
        g_v2_option_detail_missing[0] = 'P';
        CHECK(
            widget && !PluginHost_PanelDispatch(
                          hv2,
                          generation - 1,
                          widget->serial,
                          2,
                          "frame",
                          TORIRS_PANEL_ACTION_PICK,
                          2,
                          "ready/frame",
                          0,
                          0),
            "a stale page generation cannot select a structured row");
        CHECK(
            widget && !PluginHost_PanelDispatch(
                          hv2,
                          generation,
                          widget->serial,
                          2,
                          "frame",
                          TORIRS_PANEL_ACTION_PICK,
                          1,
                          "missing/frame",
                          0,
                          0),
            "a disabled selected row remains visible but cannot be chosen");
        CHECK(
            widget && !PluginHost_PanelDispatch(
                          hv2,
                          generation,
                          widget->serial,
                          2,
                          "frame",
                          TORIRS_PANEL_ACTION_PICK,
                          2,
                          "stale/index-value",
                          0,
                          0),
            "a stale stable value cannot retarget a reused option index");
        CHECK(
            widget && PluginHost_PanelDispatch(
                          hv2,
                          generation,
                          widget->serial,
                          2,
                          "frame",
                          TORIRS_PANEL_ACTION_PICK,
                          2,
                          "ready/frame",
                          0,
                          0) &&
                g_v2_select_actions == 1 &&
                strcmp(g_v2_select_value, "ready/frame") == 0,
            "an enabled selection dispatches its stable value, never its duplicate label");
        widget = PluginHost_PanelWidgetAt(hv2, generation, 3);
        CHECK(
            widget &&
                PluginHost_PanelDraw(hv2, generation, widget->serial, &g_engine, 0, 0, 100, 96) &&
                g_v2_panel_draws == 1,
            "v2 custom panel drawing is callback scoped");

        private_ref = PluginHost_UiRef(hv2, a2, "status");
        CHECK(
            PluginHost_UiInfo(hv2, private_ref, &ui_info) &&
                ui_info.state_images[TORIRS_UI_VISUAL_HOVER].value == 72 &&
                strcmp(ui_info.label, "Status") == 0 && ui_info.label_x == 3 &&
                ui_info.hit_rect.width == 24 && ui_info.action_count == 2,
            "v2 private node exposes pointer-free rich facet snapshots");
        {
            struct ToriRS_UiNodeInfo prefix_info;
            unsigned char* bytes = (unsigned char*)&prefix_info;

            memset(&prefix_info, 0xa5, sizeof(prefix_info));
            prefix_info.struct_size = TORIRS_UI_NODE_INFO_LEGACY_SIZE;
            CHECK(
                PluginHost_UiInfo(hv2, private_ref, &prefix_info) &&
                    prefix_info.struct_size == TORIRS_UI_NODE_INFO_LEGACY_SIZE &&
                    prefix_info.bounds.width == 20 &&
                    bytes[TORIRS_UI_NODE_INFO_LEGACY_SIZE] == 0xa5,
                "ui.info honors an older caller's output capacity without touching its tail");
            CHECK(
                PluginHost_UiInfo(hv2, private_ref, &prefix_info) &&
                    prefix_info.struct_size == TORIRS_UI_NODE_INFO_LEGACY_SIZE &&
                    bytes[TORIRS_UI_NODE_INFO_LEGACY_SIZE] == 0xa5,
                "a reused ui.info prefix remains safely bounded");
        }
        CHECK(
            PluginHost_UiInvoke(hv2, private_ref, "inspect") && g_v2_node_actions == 1,
            "named node actions dispatch to their v2 facet provider");
        placement_before = g_v2_placement_callbacks;
        PluginHost_SetEnabled(hv2, a2, false);
        CHECK(
            g_v2_stops[1] == 1 && g_engine.objects_live == 2 &&
                !PluginHost_UiInfo(hv2, private_ref, &ui_info),
            "v2 stop releases state-owned scene and UI resources");
        CHECK(
            g_v2_placement_callbacks == placement_before,
            "removing non-occluding UI does not invent a placement change");
        PluginHost_SetEnabled(hv2, a2, true);
        CHECK(
            g_v2_starts[1] == 2 && g_v2_zeroed_starts == 4 && g_engine.objects_live == 3,
            "re-enabling allocates a fresh zeroed state and restores resources");

        PluginHost_Free(hv2);
        CHECK(
            g_v2_stops[1] == 2 && g_v2_stops[2] == 1 && g_v2_stops[3] == 1 &&
                g_engine.objects_live == 0,
            "host destruction stops every v2 instance and cleans its resources");
        g_role_name = NULL;
    }

    /* ---- authoritative V2 capabilities and asset states --------------- */
    {
        struct ToriRS_PluginHost* seam_host;
        unsigned char* data;

        memset(&g_engine, 0, sizeof(g_engine));
        memset(&g_v2_seam, 0, sizeof(g_v2_seam));
        g_capability_touch = 1;
        g_capability_browser = 1;
        g_capability_web = 0;
        engine = fake_engine();
        seam_host = PluginHost_New(&engine);
        CHECK(
            PluginHost_RegisterV2(seam_host, &V2_SEAM_PROBE) == 0,
            "the V2 capability/asset seam probe registers");
        PluginHost_Start(seam_host);
        CHECK(
            g_v2_seam.touch && g_v2_seam.browser && !g_v2_seam.web &&
                !g_v2_seam.unknown,
            "core.capability forwards the engine bridge's named truth and rejects unknowns");
        CHECK(
            g_v2_seam.raw_initial == TORIRS_ASSET_PENDING &&
                g_v2_seam.image_initial == TORIRS_ASSET_PENDING &&
                g_v2_seam.model_initial == TORIRS_ASSET_PENDING &&
                g_v2_seam.missing_initial == TORIRS_ASSET_PENDING &&
                g_v2_seam.bad_image_initial == TORIRS_ASSET_PENDING &&
                g_v2_seam.image.value != 0 && g_v2_seam.model.value != 0,
            "new byte/image/model requests report pending with zero-safe live handles");
        CHECK(
            g_v2_seam.invalid == TORIRS_ASSET_INVALID,
            "invalid names are rejected before the host starts IO");

        data = malloc(4);
        memcpy(data, "DATA", 4);
        PluginHost_AssetDeliver(seam_host, "v2-seam-probe", "raw.bin", data, 4);
        data = malloc(3);
        memcpy(data, "IMG", 3);
        PluginHost_AssetDeliver(seam_host, "v2-seam-probe", "image.bin", data, 3);
        data = malloc(5);
        memcpy(data, "MODEL", 5);
        PluginHost_AssetDeliver(seam_host, "v2-seam-probe", "model.bin", data, 5);
        PluginHost_AssetDeliver(seam_host, "v2-seam-probe", "missing.bin", NULL, 0);
        data = malloc(4);
        memcpy(data, "FAIL", 4);
        PluginHost_AssetDeliver(seam_host, "v2-seam-probe", "bad-image.bin", data, 4);
        PluginHost_LogicTick(seam_host, 1);
        CHECK(
            g_v2_seam.raw_final == TORIRS_ASSET_READY && g_v2_seam.bytes_ready &&
                g_v2_seam.image_final == TORIRS_ASSET_READY &&
                g_v2_seam.model_final == TORIRS_ASSET_READY,
            "delivered bytes and decoded resources report ready from host state");
        CHECK(
            g_v2_seam.missing_final == TORIRS_ASSET_MISSING &&
                g_v2_seam.bad_image_final == TORIRS_ASSET_ERROR,
            "cached miss and decode failure are terminal states, never perpetual pending");
        CHECK(
            g_v2_seam.model_budget == TORIRS_ASSET_BUDGET,
            "the host reports its real model-table budget through V2");
        PluginHost_Free(seam_host);
    }

    /* ---- incarnation-fenced V2 resources survive internal slot reuse ----- */
    {
        struct ToriRS_PluginHost* aba_host;
        struct ToriRS_FrameSelection selection;
        struct ToriRS_UiNodeInfo badge_info = { .struct_size = sizeof(badge_info) };
        struct ToriRS_UiNodeRef badge;
        unsigned char* data;
        int draw_before;

        memset(&g_engine, 0, sizeof(g_engine));
        memset(&g_v2_aba, 0, sizeof(g_v2_aba));
        g_v2_aba_starts = 0;
        g_screen_now = TORIRS_SCREEN_GAME;
        snprintf(
            g_engine.frame_preference,
            sizeof(g_engine.frame_preference),
            "%s",
            "v2-aba/frame");
        g_engine.frame_preference_present = 1;
        g_engine.frame_migration_version = 1;
        engine = fake_engine();
        aba_host = PluginHost_New(&engine);
        CHECK(
            PluginHost_RegisterV2(aba_host, &V2_ABA_PROBE) == 0,
            "the V2 resource-incarnation probe registers");
        PluginHost_Start(aba_host);

        data = malloc(3);
        memcpy(data, "IMG", 3);
        PluginHost_AssetDeliver(aba_host, "v2-aba", "aba-old.png", data, 3);
        data = malloc(5);
        memcpy(data, "MODEL", 5);
        PluginHost_AssetDeliver(aba_host, "v2-aba", "aba-old.model", data, 5);
        PluginHost_FrameStart(aba_host, 1, 0);
        PluginHost_LogicTick(aba_host, 1);
        CHECK(
            g_v2_aba.ui_install == TORIRS_RESULT_OK && PluginHost_FrameNeedsLayout(aba_host),
            "live image tokens install into retained UI and frame candidates");
        PluginHost_Layout(aba_host, 640, 480);
        memset(&selection, 0, sizeof(selection));
        selection.struct_size = sizeof(selection);
        g_v2_aba_api->frame.selection(g_v2_aba_api, &selection);
        CHECK(
            strcmp(selection.active_id, "v2-aba/frame") == 0 &&
                selection.status == TORIRS_FRAME_STATUS_ACTIVE,
            "the resource probe commits image-backed frame art");
        badge = PluginHost_UiRef(aba_host, 0, "badge");
        CHECK(
            PluginHost_UiInfo(aba_host, badge, &badge_info) &&
                badge_info.state_images[TORIRS_UI_VISUAL_IDLE].value != 0,
            "ui.update retains the first live image token");

        PluginHost_LogicTick(aba_host, 2);
        selection.struct_size = sizeof(selection);
        g_v2_aba_api->frame.selection(g_v2_aba_api, &selection);
        CHECK(
            g_v2_aba.image_old.value != 0 &&
                g_v2_aba.image_new.value != g_v2_aba.image_old.value &&
                g_v2_aba.model_new.value != g_v2_aba.model_old.value &&
                g_v2_aba.mesh_new.value != g_v2_aba.mesh_old.value &&
                g_v2_aba.instance_new.value != g_v2_aba.instance_old.value,
            "reallocated image/model/mesh/instance slots receive new typed tokens");
        CHECK(
            g_v2_aba.ui_stale == TORIRS_RESULT_INVALID && !g_v2_aba.image_stale_size &&
                g_v2_aba.mesh_stale == TORIRS_RESULT_INVALID &&
                g_v2_aba.instance_stale == TORIRS_RESULT_INVALID &&
                g_v2_aba.model_stale == TORIRS_RESULT_INVALID,
            "every stale typed resource operation fails before reaching a reused internal slot");
        CHECK(
            g_engine.image_releases == 1 && g_engine.model_releases == 1 &&
                g_engine.meshes_live == 1 && g_engine.objects_live == 1,
            "repeated stale release/destroy calls leave all four replacements live");
        CHECK(
            strcmp(selection.active_id, "core/native") == 0 &&
                selection.status == TORIRS_FRAME_STATUS_FALLBACK,
            "releasing retained frame artwork removes the frame before its slot is reusable");
        memset(&badge_info, 0, sizeof(badge_info));
        badge_info.struct_size = sizeof(badge_info);
        CHECK(
            PluginHost_UiInfo(aba_host, badge, &badge_info) &&
                badge_info.state_images[TORIRS_UI_VISUAL_IDLE].value ==
                    g_v2_aba.image_old.value,
            "the retained UI model still holds the old token, never the replacement by slot");

        data = malloc(3);
        memcpy(data, "NEW", 3);
        PluginHost_AssetDeliver(aba_host, "v2-aba", "aba-new.png", data, 3);
        data = malloc(5);
        memcpy(data, "MODEL", 5);
        PluginHost_AssetDeliver(aba_host, "v2-aba", "aba-new.model", data, 5);
        draw_before = g_engine.draw_items;
        PluginHost_DrawCanvas(aba_host, 640, 480);
        CHECK(
            g_engine.draw_items == draw_before,
            "a retained stale UI image never draws newly published pixels from the reused slot");
        PluginHost_LogicTick(aba_host, 3);
        CHECK(
            g_v2_aba.image_new_size && g_v2_aba.new_mesh_ok &&
                g_v2_aba.new_instance_ok && g_v2_aba.new_model_ok,
            "current replacement tokens remain usable after every stale operation");

        g_v2_aba.reload_image_old = g_v2_aba.image_new;
        g_v2_aba.reload_model_old = g_v2_aba.model_new;
        g_v2_aba.reload_mesh_old = g_v2_aba.mesh_new;
        g_v2_aba.reload_instance_old = g_v2_aba.instance_new;
        {
            int const image_releases = g_engine.image_releases;
            int const model_releases = g_engine.model_releases;

            PluginHost_Reload(aba_host, 0);
            CHECK(
                g_v2_aba_starts == 2 &&
                    g_v2_aba.reload_image_new.value != g_v2_aba.reload_image_old.value &&
                    g_v2_aba.reload_model_new.value != g_v2_aba.reload_model_old.value &&
                    g_v2_aba.reload_mesh_new.value != g_v2_aba.reload_mesh_old.value &&
                    g_v2_aba.reload_instance_new.value != g_v2_aba.reload_instance_old.value,
                "host reload cannot resurrect any pre-reload typed resource token");
            CHECK(
                !g_v2_aba.reload_image_stale_size &&
                    g_v2_aba.reload_mesh_stale == TORIRS_RESULT_INVALID &&
                    g_v2_aba.reload_instance_stale == TORIRS_RESULT_INVALID &&
                    g_v2_aba.reload_model_stale == TORIRS_RESULT_INVALID,
                "all four pre-reload refs reject post-reload same-slot operations");
            CHECK(
                g_engine.image_releases == image_releases + 1 &&
                    g_engine.model_releases == model_releases + 1 &&
                    g_engine.meshes_live == 1 && g_engine.objects_live == 1 &&
                    g_v2_aba.reload_new_mesh_ok && g_v2_aba.reload_new_instance_ok &&
                    g_v2_aba.reload_new_model_ok,
                "pre-reload stale release/destroy calls leave every new resource live");
        }
        PluginHost_Free(aba_host);
    }

    /* ---- resolved placement revision and composed fragmented safe area -- */
    {
        struct ToriRS_PluginHost* hp;
        struct ToriRS_ApiV2* api;
        struct ToriRS_Rect rect;
        struct ToriRS_Rect notch = { 0, 0, 10, 10 };
        struct ToriRS_Rect keyboard = { 0, 80, 100, 20 };
        struct ToriRS_Rect minimap = { 70, 10, 20, 20 };
        struct ToriRS_Rect rail = { 90, 0, 10, 80 };
        struct ToriRS_PlacementAreaRef platform_area;
        struct ToriRS_PlacementAreaRef frame_area;
        struct ToriRS_PlacementAreaRef overlay_area;
        uint32_t baseline;
        uint32_t reserved_revision;
        int fragments = 0;
        int iter = -1;
        int probe;

        memset(&g_engine, 0, sizeof(g_engine));
        memset(g_slot_x, 0, sizeof(g_slot_x));
        memset(g_slot_y, 0, sizeof(g_slot_y));
        memset(g_slot_w, 0, sizeof(g_slot_w));
        memset(g_slot_h, 0, sizeof(g_slot_h));
        g_screen_now = TORIRS_SCREEN_GAME;
        g_role_name = NULL;
        g_role_visible = 0;
        g_member_slot = -1;
        g_member_no = -1;
        g_slot_w[TORIRS_HOST_SURFACE_CANVAS] = 100;
        g_slot_h[TORIRS_HOST_SURFACE_CANVAS] = 100;
        g_slot_w[TORIRS_HOST_SURFACE_VIEWPORT] = 100;
        g_slot_h[TORIRS_HOST_SURFACE_VIEWPORT] = 100;
        g_slot_x[TORIRS_HOST_SURFACE_MINIMAP] = minimap.x;
        g_slot_y[TORIRS_HOST_SURFACE_MINIMAP] = minimap.y;
        g_slot_w[TORIRS_HOST_SURFACE_MINIMAP] = minimap.width;
        g_slot_h[TORIRS_HOST_SURFACE_MINIMAP] = minimap.height;
        /* Canvas minus a top-left notch and the keyboard's bottom band. */
        g_platform_safe_count = 2;
        g_platform_safe[0] = (struct ToriRS_PlacementRect){ 10, 0, 90, 10 };
        g_platform_safe[1] = (struct ToriRS_PlacementRect){ 0, 10, 100, 70 };
        g_lane_rail_box[0] = rail.x;
        g_lane_rail_box[1] = rail.y;
        g_lane_rail_box[2] = rail.width;
        g_lane_rail_box[3] = rail.height;
        g_lane_rail_visible = 1;
        g_placement_v2_calls = 0;
        g_placement_v2_depth = 0;
        g_placement_v2_max_depth = 0;
        g_placement_v2_reenter = 0;
        g_placement_v2_revision = 0;

        engine = fake_engine();
        hp = PluginHost_New(&engine);
        probe = PluginHost_RegisterV2(hp, &V2_PLACEMENT_PROBE);
        CHECK(probe == 0, "placement probe registers as a v2 plugin");
        PluginHost_Start(hp);
        api = g_placement_v2_api;
        platform_area = api->placement.area(api, TORIRS_AREA_PLATFORM_SAFE);
        frame_area = api->placement.area(api, TORIRS_AREA_FRAME_BUILD);
        overlay_area = api->placement.area(api, TORIRS_AREA_OVERLAY_SAFE);

        baseline = api->placement.revision(api);
        CHECK(baseline != 0, "the first complete placement snapshot has a revision");
        {
            struct ToriRS_FrameOfferInfo offer = { .struct_size = sizeof(offer) };
            CHECK(
                api->placement.rect_next(api, overlay_area, INT_MAX, &rect) == -1 &&
                    api->frame.offer_next(api, INT_MAX, &offer) == -1,
                "public iterators reject INT_MAX without signed overflow");
        }
        CHECK(
            !api->placement.contains(api, platform_area, notch) &&
                !api->placement.contains(api, platform_area, keyboard),
            "platform-safe preserves both the corner notch and keyboard exclusion");
        CHECK(
            !api->placement.contains(api, frame_area, rail) &&
                api->placement.contains(api, frame_area, minimap),
            "frame-build excludes the lane rail but not replaceable frame furniture");
        CHECK(
            !api->placement.contains(api, overlay_area, rail) &&
                !api->placement.contains(api, overlay_area, minimap),
            "overlay-safe composes lane and frame occluders");
        while( (iter = api->placement.rect_next(api, overlay_area, iter, &rect)) >= 0 )
            fragments++;
        CHECK(fragments >= 3, "the composed overlay area remains fragmented");

        PluginHost_LayoutChanged(hp);
        CHECK(
            api->placement.revision(api) == baseline && g_placement_v2_calls == 0,
            "an identical layout rebuild neither bumps nor notifies placement");

        CHECK(
            api->placement.reserve(
                api,
                "left-dock",
                TORIRS_AREA_OVERLAY_SAFE,
                TORIRS_EDGE_LEFT,
                5) == TORIRS_RESERVE_OK,
            "a named reservation joins the composed placement transaction");
        reserved_revision = api->placement.revision(api);
        CHECK(
            reserved_revision == baseline + 1 && g_placement_v2_calls == 1 &&
                g_placement_v2_revision == reserved_revision,
            "an assigned reservation advances and publishes exactly one revision");
        CHECK(
            api->placement.reservation_rect(api, "left-dock", &rect) && rect.x == 0 &&
                rect.y == 30 && rect.width == 5 && rect.height == 50 &&
                !api->placement.contains(api, overlay_area, rect),
            "the named reservation reports the exact fragment it consumed");

        PluginHost_LayoutChanged(hp);
        CHECK(
            api->placement.revision(api) == reserved_revision && g_placement_v2_calls == 1,
            "restating identical areas and assignments remains silent");

        /* A callback-side reservation is deferred to the next frame rather
         * than recursively entering on_placement_changed. */
        g_placement_v2_reenter = 1;
        g_platform_safe[1].h = 60;
        PluginHost_LayoutChanged(hp);
        CHECK(
            g_placement_v2_calls == 2 && g_placement_v2_max_depth == 1,
            "placement callbacks are non-recursive");
        PluginHost_FrameStart(hp, 1, 0);
        CHECK(
            g_placement_v2_calls == 3 && g_placement_v2_max_depth == 1 &&
                g_placement_v2_revision == api->placement.revision(api),
            "callback-side changes coalesce into one later placement transaction");

        PluginHost_Free(hp);
        g_platform_safe_count = 0;
        g_lane_rail_visible = 0;
    }

    /* ---- teardown cannot notify freed v2 state ----------------------- */
    {
        struct ToriRS_PluginHost* teardown_host;
        struct ToriRS_PluginEngine teardown_engine;
        int calls_before;
        int probe;

        memset(&g_engine, 0, sizeof(g_engine));
        memset(g_slot_x, 0, sizeof(g_slot_x));
        memset(g_slot_y, 0, sizeof(g_slot_y));
        memset(g_slot_w, 0, sizeof(g_slot_w));
        memset(g_slot_h, 0, sizeof(g_slot_h));
        g_slot_w[TORIRS_HOST_SURFACE_CANVAS] = 100;
        g_slot_h[TORIRS_HOST_SURFACE_CANVAS] = 100;
        g_slot_w[TORIRS_HOST_SURFACE_VIEWPORT] = 100;
        g_slot_h[TORIRS_HOST_SURFACE_VIEWPORT] = 100;
        g_role_name = NULL;
        g_role_visible = 0;
        g_platform_safe_count = 0;
        g_v2_teardown_stop_alive = 0;
        g_v2_teardown_placement_calls = 0;
        g_v2_teardown_after_shutdown = 0;
        teardown_engine = fake_engine();
        teardown_host = PluginHost_New(&teardown_engine);
        probe = PluginHost_RegisterV2(teardown_host, &V2_TEARDOWN_PROBE);
        PluginHost_Start(teardown_host);
        CHECK(probe == 0 && g_v2_teardown_placement_calls > 0,
            "teardown fixture starts with live state and an assigned reservation");
        calls_before = g_v2_teardown_placement_calls;
        PluginHost_SetEnabled(teardown_host, probe, false);
        CHECK(
            g_v2_teardown_stop_alive && g_v2_teardown_after_shutdown == 0 &&
                g_v2_teardown_placement_calls == calls_before,
            "reservation cleanup dispatches nothing after v2 shutdown while on_stop stays live");
        PluginHost_Free(teardown_host);
    }

    /* ---- independent named-UI facets and semantic placement --------- */
    {
        struct ToriRS_PluginHost* facet_host;
        struct ToriRS_PluginEngine facet_engine;
        uint32_t late_rebuilds;
        uint32_t old_tag;
        uint32_t change_visits;
        uint32_t registry_visits;
        uint32_t role_probe_visits;
        int old_actions;
        int old_draws;
        int old_hits;
        int appearance;
        int actions;
        int nested;

        memset(&g_engine, 0, sizeof(g_engine));
        memset(g_slot_x, 0, sizeof(g_slot_x));
        memset(g_slot_y, 0, sizeof(g_slot_y));
        memset(g_slot_w, 0, sizeof(g_slot_w));
        memset(g_slot_h, 0, sizeof(g_slot_h));
        g_slot_w[TORIRS_HOST_SURFACE_CANVAS] = 100;
        g_slot_h[TORIRS_HOST_SURFACE_CANVAS] = 100;
        g_slot_w[TORIRS_HOST_SURFACE_VIEWPORT] = 100;
        g_slot_h[TORIRS_HOST_SURFACE_VIEWPORT] = 100;
        g_slot_x[TORIRS_HOST_SURFACE_ORBS] = 70;
        g_slot_y[TORIRS_HOST_SURFACE_ORBS] = 5;
        g_slot_w[TORIRS_HOST_SURFACE_ORBS] = 25;
        g_slot_h[TORIRS_HOST_SURFACE_ORBS] = 80;
        g_role_name = "orb_run";
        g_role_visible = 1;
        g_role_box[0] = 72;
        g_role_box[1] = 35;
        g_role_box[2] = 20;
        g_role_box[3] = 20;
        g_role_suppress_calls = 0;
        g_role_suppress_paint = -1;
        g_role_suppress_input = -1;
        g_role_suppress_name[0] = '\0';
        g_present_draws = 0;
        g_present_actions = 0;
        g_present_order_count = 0;
        g_present_reorder_requested = 0;
        g_present_reorder_result = TORIRS_RESULT_ERROR;
        g_present_visibility_request = -1;
        g_present_visibility_result = TORIRS_RESULT_ERROR;
        g_present_ancestor_visibility_request = -1;
        g_present_ancestor_visibility_result = TORIRS_RESULT_ERROR;
        g_hit_region_calls = 0;
        facet_engine = fake_engine();
        facet_host = PluginHost_New(&facet_engine);
        appearance = PluginHost_RegisterV2(facet_host, &V2_PRESENT_APPEARANCE);
        actions = PluginHost_RegisterV2(facet_host, &V2_PRESENT_ACTIONS);
        nested = PluginHost_RegisterV2(facet_host, &V2_PRESENT_NESTED);
        CHECK(
            appearance == 0 && actions == 1 && nested == 2,
            "independent facet presenter fixtures register");
        PluginHost_Start(facet_host);
        PluginHost_LayoutChanged(facet_host);

        PluginHost_SetEnabled(facet_host, appearance, true);
        /* The base snapshot still names the target, but its live tree role is
         * absent during this reconciliation (the rebuild gap). The presenter
         * must retain the standing role dependency rather than forgetting it. */
        g_role_name = NULL;
        PluginHost_ReconcileUi(facet_host);
        late_rebuilds = PluginHost_UiPresentationRebuilds(facet_host);
        g_role_suppress_calls = 0;
        g_role_name = "orb_run";
        PluginHost_ReconcileUi(facet_host);
        CHECK(
            PluginHost_UiPresentationRebuilds(facet_host) == late_rebuilds + 1 &&
                g_role_suppress_calls > 0 && g_role_suppress_paint == 1 &&
                g_role_suppress_input == 0,
            "a contribution retained through a rebuild gap suppresses its newly live role without a registry change");
        late_rebuilds = PluginHost_UiPresentationRebuilds(facet_host);
        PluginHost_ReconcileUi(facet_host);
        CHECK(
            PluginHost_UiPresentationRebuilds(facet_host) == late_rebuilds,
            "late-role reconciliation returns to compact steady-state work");
        PluginHost_DrawCanvas(facet_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationCount(facet_host) == 1 &&
                g_role_suppress_paint == 1 && g_role_suppress_input == 0 &&
                strcmp(g_role_suppress_name, "orb_run") == 0 &&
                g_present_draws == 1 && g_hit_region_calls == 0,
            "an APPEARANCE winner suppresses only native paint and presents no action surface");

        /* A resolved boundary is not polled speculatively. Its failed anchor is
         * the exact event that schedules one role refresh; while absent it is
         * then the only dependency probed for a later appearance. */
        late_rebuilds = PluginHost_UiPresentationRebuilds(facet_host);
        role_probe_visits = PluginHost_UiPresentationRoleProbeVisits(facet_host);
        old_draws = g_present_draws;
        g_role_name = NULL;
        PluginHost_DrawCanvas(facet_host, 100, 100);
        CHECK(
            g_present_draws == old_draws &&
                PluginHost_UiPresentationRebuilds(facet_host) == late_rebuilds,
            "a failed live-boundary anchor schedules recovery without a global pre-scan");
        PluginHost_ReconcileUi(facet_host);
        CHECK(
            PluginHost_UiPresentationRebuilds(facet_host) == late_rebuilds + 1 &&
                PluginHost_UiPresentationRoleProbeVisits(facet_host) > role_probe_visits,
            "the scheduled refresh retires a disappeared live boundary once");
        late_rebuilds = PluginHost_UiPresentationRebuilds(facet_host);
        role_probe_visits = PluginHost_UiPresentationRoleProbeVisits(facet_host);
        PluginHost_ReconcileUi(facet_host);
        CHECK(
            PluginHost_UiPresentationRebuilds(facet_host) == late_rebuilds &&
                PluginHost_UiPresentationRoleProbeVisits(facet_host) ==
                    role_probe_visits + 1,
            "only the unresolved boundary is probed while it remains absent");
        g_role_name = "orb_run";
        PluginHost_ReconcileUi(facet_host);
        CHECK(
            PluginHost_UiPresentationRebuilds(facet_host) == late_rebuilds + 1,
            "the unresolved boundary becoming live restores the retained row once");

        PluginHost_SetEnabled(facet_host, actions, true);
        PluginHost_DrawCanvas(facet_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationCount(facet_host) == 1 &&
                g_role_suppress_paint == 1 && g_role_suppress_input == 1 &&
                g_present_draws == 2 && g_hit_region_calls == 1,
            "independent APPEARANCE and ACTIONS winners suppress both matching native facets");
        old_tag = g_hit_region_tag;
        old_actions = g_present_actions;
        change_visits = PluginHost_UiPresentationChangeVisits(facet_host);
        registry_visits = PluginHost_UiPresentationRegistryVisits(facet_host);
        role_probe_visits = PluginHost_UiPresentationRoleProbeVisits(facet_host);
        g_present_reorder_requested = 1;
        PluginHost_LogicTick(facet_host, 1);
        PluginHost_CanvasClick(
            facet_host,
            actions,
            old_tag,
            0,
            g_hit_region_box[0],
            g_hit_region_box[1]);
        CHECK(
            g_present_reorder_result == TORIRS_RESULT_OK &&
                g_present_actions == old_actions,
            "a menu row retained before ui.update cannot invoke a reordered action");
        CHECK(
            PluginHost_UiPresentationChangeVisits(facet_host) == change_visits + 1 &&
                PluginHost_UiPresentationRegistryVisits(facet_host) == registry_visits &&
                PluginHost_UiPresentationRoleProbeVisits(facet_host) == role_probe_visits,
            "one ui.update consumes one indexed node change without a registry scan or role probe");
        PluginHost_DrawCanvas(facet_host, 100, 100);
        CHECK(
            g_hit_region_tag != old_tag && strcmp(g_hit_region_ops[0], "second") == 0,
            "action-content changes mint a new operation identity");
        CHECK(
            PluginHost_UiPresentationChangeVisits(facet_host) == change_visits + 1 &&
                PluginHost_UiPresentationRegistryVisits(facet_host) == registry_visits &&
                PluginHost_UiPresentationRoleProbeVisits(facet_host) == role_probe_visits,
            "a settled named presenter reconcile is O(1)");
        PluginHost_CanvasClick(
            facet_host,
            actions,
            g_hit_region_tag,
            0,
            g_hit_region_box[0],
            g_hit_region_box[1]);
        CHECK(
            g_present_actions == old_actions + 1 &&
                strcmp(g_present_last_action, "second") == 0,
            "the newly declared operation identity invokes its matching reordered action");

        old_tag = g_hit_region_tag;
        old_actions = g_present_actions;
        old_draws = g_present_draws;
        old_hits = g_hit_region_calls;
        g_present_visibility_request = 0;
        PluginHost_LogicTick(facet_host, 2);
        PluginHost_CanvasClick(
            facet_host,
            actions,
            old_tag,
            0,
            g_hit_region_box[0],
            g_hit_region_box[1]);
        CHECK(
            g_present_visibility_result == TORIRS_RESULT_OK &&
                g_present_actions == old_actions,
            "a retained menu action is rejected when its own node becomes hidden");
        PluginHost_DrawCanvas(facet_host, 100, 100);
        CHECK(
            g_present_draws == old_draws && g_hit_region_calls == old_hits &&
                g_role_suppress_paint == 1 && g_role_suppress_input == 1,
            "a hidden winner keeps native facets suppressed but publishes no paint or input");
        g_present_visibility_request = 1;
        PluginHost_LogicTick(facet_host, 3);
        PluginHost_DrawCanvas(facet_host, 100, 100);
        CHECK(
            g_present_visibility_result == TORIRS_RESULT_OK &&
                g_hit_region_calls == old_hits + 1 && g_present_draws == old_draws + 1 &&
                g_hit_region_tag != old_tag,
            "showing the node again publishes a fresh action identity");

        PluginHost_SetEnabled(facet_host, appearance, false);
        old_actions = g_present_draws;
        PluginHost_DrawCanvas(facet_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationCount(facet_host) == 1 &&
                g_role_suppress_paint == 0 && g_role_suppress_input == 1 &&
                g_present_draws == old_actions,
            "an ACTIONS-only winner leaves native paint live");
        PluginHost_SetEnabled(facet_host, actions, false);
        PluginHost_DrawCanvas(facet_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationCount(facet_host) == 0 &&
                g_role_suppress_paint == 0 && g_role_suppress_input == 0,
            "provider teardown releases each native facet independently");

        g_present_order_count = 0;
        PluginHost_SetEnabled(facet_host, nested, true);
        PluginHost_DrawCanvas(facet_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationCount(facet_host) == 4 &&
                g_present_order_count == 4 &&
                strcmp(g_present_order[0], "grand-before") == 0 &&
                strcmp(g_present_order[1], "before") == 0 &&
                strcmp(g_present_order[2], "target") == 0 &&
                strcmp(g_present_order[3], "after") == 0 &&
                g_ui_boundary_last_place == PLUGIN_UI_BOUNDARY_SELF,
            "nested before/after presentation stays contiguous at the live target's SELF boundary");
        old_tag = g_hit_region_tag;
        old_actions = g_present_actions;
        old_draws = g_present_draws;
        old_hits = g_hit_region_calls;
        g_present_ancestor_visibility_request = 0;
        PluginHost_LogicTick(facet_host, 4);
        PluginHost_CanvasClick(
            facet_host,
            nested,
            old_tag,
            0,
            g_hit_region_box[0],
            g_hit_region_box[1]);
        PluginHost_DrawCanvas(facet_host, 100, 100);
        CHECK(
            g_present_ancestor_visibility_result == TORIRS_RESULT_OK &&
                g_present_actions == old_actions && g_present_draws == old_draws &&
                g_hit_region_calls == old_hits,
            "an ancestor becoming hidden retires a descendant's open menu identity");
        g_present_ancestor_visibility_request = 1;
        PluginHost_LogicTick(facet_host, 5);
        PluginHost_DrawCanvas(facet_host, 100, 100);
        CHECK(
            g_present_ancestor_visibility_result == TORIRS_RESULT_OK &&
                g_present_draws == old_draws + 4 && g_hit_region_calls == old_hits + 1 &&
                g_hit_region_tag != old_tag,
            "restoring the ancestor remints and republishes the descendant action");
        PluginHost_SetEnabled(facet_host, nested, false);
        CHECK(
            g_role_suppress_paint == 0 && g_role_suppress_input == 0,
            "nested provider teardown restores the target's native appearance");
        PluginHost_Free(facet_host);
        g_role_name = NULL;
    }

    /* ---- retained named-UI presenter -------------------------------- */
    {
        struct ToriRS_PluginHost* present_host;
        struct ToriRS_PluginEngine present_engine;
        struct ToriRS_UiNodeInfo info = { .struct_size = sizeof(info) };
        struct ToriRS_UiNodeRef node;
        uint32_t rebuilds;
        int draw_items;
        int hit_calls;
        int present_a;
        int present_b;

        memset(&g_engine, 0, sizeof(g_engine));
        memset(g_slot_x, 0, sizeof(g_slot_x));
        memset(g_slot_y, 0, sizeof(g_slot_y));
        memset(g_slot_w, 0, sizeof(g_slot_w));
        memset(g_slot_h, 0, sizeof(g_slot_h));
        g_slot_w[TORIRS_HOST_SURFACE_CANVAS] = 100;
        g_slot_h[TORIRS_HOST_SURFACE_CANVAS] = 100;
        g_slot_w[TORIRS_HOST_SURFACE_VIEWPORT] = 100;
        g_slot_h[TORIRS_HOST_SURFACE_VIEWPORT] = 100;
        g_slot_x[TORIRS_HOST_SURFACE_ORBS] = 70;
        g_slot_y[TORIRS_HOST_SURFACE_ORBS] = 5;
        g_slot_w[TORIRS_HOST_SURFACE_ORBS] = 25;
        g_slot_h[TORIRS_HOST_SURFACE_ORBS] = 80;
        g_role_name = "orb_run";
        g_role_visible = 1;
        g_role_box[0] = 72;
        g_role_box[1] = 35;
        g_role_box[2] = 20;
        g_role_box[3] = 20;
        g_hit_region_calls = 0;
        g_hit_region_plugin = -1;
        g_hit_region_tag = 0;
        g_role_suppress_calls = 0;
        g_role_suppress_paint = -1;
        g_role_suppress_input = -1;
        g_present_draws = 0;
        g_present_actions = 0;
        g_present_foreign_update = TORIRS_RESULT_ERROR;
        g_present_appearance_update = TORIRS_RESULT_ERROR;
        g_present_actions_update = TORIRS_RESULT_ERROR;
        g_present_last_action[0] = '\0';
        present_engine = fake_engine();
        present_host = PluginHost_New(&present_engine);
        present_a = PluginHost_RegisterV2(present_host, &V2_PRESENT_A);
        present_b = PluginHost_RegisterV2(present_host, &V2_PRESENT_B);
        CHECK(present_a == 0 && present_b == 1, "presenter providers register");
        PluginHost_Start(present_host);
        PluginHost_LayoutChanged(present_host);
        CHECK(
            g_present_foreign_update == TORIRS_RESULT_INVALID &&
                g_present_appearance_update == TORIRS_RESULT_OK &&
                g_present_actions_update == TORIRS_RESULT_OK,
            "ui.update rejects foreign art and atomically restates owned facets");

        draw_items = g_engine.draw_items;
        hit_calls = g_hit_region_calls;
        PluginHost_DrawCanvas(present_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationCount(present_host) == 0 &&
                g_engine.draw_items == draw_items && g_hit_region_calls == hit_calls &&
                g_role_suppress_paint != 1 && g_role_suppress_input != 1,
            "two conflicting providers leave base paint/input and neither draws nor acts");
        rebuilds = PluginHost_UiPresentationRebuilds(present_host);
        PluginHost_DrawCanvas(present_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationRebuilds(present_host) == rebuilds,
            "an unchanged frame does not rescan the named registry");

        PluginHost_SetEnabled(present_host, present_b, false);
        node = PluginHost_UiRef(present_host, present_a, "frame.orb.run");
        CHECK(
            PluginHost_UiInfo(present_host, node, &info) && info.active &&
                strcmp(info.label, "Updated winner") == 0 && info.hit_rect.x == 65 &&
                info.action_count == 2,
            "teardown reveals the remaining provider's complete restated snapshot");
        draw_items = g_engine.draw_items;
        hit_calls = g_hit_region_calls;
        PluginHost_DrawCanvas(present_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationCount(present_host) == 1 &&
                g_engine.draw_items == draw_items + 2 && g_present_draws == 1 &&
                g_hit_region_calls == hit_calls + 1 && g_hit_region_plugin == present_a &&
                g_hit_region_box[0] == 70 && g_hit_region_box[1] == 18 &&
                g_hit_region_box[2] == 15 && g_hit_region_box[3] == 20 &&
                g_hit_region_op_count == 2 &&
                strcmp(g_hit_region_ops[0], "inspect") == 0 &&
                g_role_suppress_paint == 1 && g_role_suppress_input == 1,
            "one winning provider yields exactly one visual callback, label, and hit region");
        CHECK(
            (g_hit_region_tag & 0x80000000u) != 0,
            "retained named actions use the host-reserved route namespace");
        rebuilds = PluginHost_UiPresentationRebuilds(present_host);
        draw_items = g_engine.draw_items;
        PluginHost_DrawCanvas(present_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationRebuilds(present_host) == rebuilds &&
                g_engine.draw_items == draw_items + 2 && g_present_draws == 2,
            "steady retained presentation is O(active entries), not O(registry nodes)");
        PluginHost_CanvasClick(
            present_host, present_a, g_hit_region_tag, 0, g_hit_region_box[0], g_hit_region_box[1]);
        CHECK(
            g_present_actions == 1 && strcmp(g_present_last_action, "inspect") == 0,
            "left click routes the retained action through UiInvoke to its v2 winner");

        PluginHost_LayoutChanged(present_host);
        rebuilds = PluginHost_UiPresentationRebuilds(present_host);
        PluginHost_DrawCanvas(present_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationRebuilds(present_host) == rebuilds + 1 &&
                PluginHost_UiPresentationCount(present_host) == 1 && g_present_draws == 3,
            "a base-tree rebuild reconciles once and preserves one visual result");

        PluginHost_SetEnabled(present_host, present_b, true);
        draw_items = g_engine.draw_items;
        hit_calls = g_hit_region_calls;
        PluginHost_DrawCanvas(present_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationCount(present_host) == 0 &&
                g_engine.draw_items == draw_items && g_hit_region_calls == hit_calls &&
                g_role_suppress_paint == 0 && g_role_suppress_input == 0,
            "restoring the contender returns to conflict and restores base facets");
        PluginHost_SetEnabled(present_host, present_b, false);
        PluginHost_DrawCanvas(present_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationCount(present_host) == 1 && g_present_draws == 4 &&
                g_role_suppress_paint == 1 && g_role_suppress_input == 1,
            "tearing the contender down restores the sole winner exactly once");
        PluginHost_SetEnabled(present_host, present_a, false);
        draw_items = g_engine.draw_items;
        hit_calls = g_hit_region_calls;
        PluginHost_DrawCanvas(present_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationCount(present_host) == 0 &&
                g_engine.draw_items == draw_items && g_hit_region_calls == hit_calls &&
                g_role_suppress_paint == 0 && g_role_suppress_input == 0,
            "winner teardown removes retained visuals/actions and restores base facets");
        PluginHost_Free(present_host);
        g_role_name = NULL;
    }

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
