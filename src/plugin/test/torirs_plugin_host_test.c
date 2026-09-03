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
 * @see ToriRS_PluginApi::screen. */
static int g_screen_now = TORIRS_PLUGIN_SCREEN_GAME;

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
    struct ToriRS_PluginPlayerSnap* out)
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
    struct ToriRS_PluginNpcSnap* out)
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
    struct ToriRS_PluginNpcSnap* out)
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
    struct ToriRS_PluginPlayerSnap* out)
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
    struct ToriRS_PluginLocSnap* out)
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
    struct ToriRS_PluginHighlightItem* out)
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
    struct ToriRS_PluginHoverEntity* out)
{
    (void)u;
    out->kind = TORIRS_PLUGIN_HOVER_NPC;
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
     "Draw distance", TORIRS_PLUGIN_FEATURE_INT,
     25, 90,
     NULL,               { 0, 0 },
     0, 25,
     25 },
    { "camera_zoom",
     "Camera zoom",   TORIRS_PLUGIN_FEATURE_ENUM,
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
    struct ToriRS_PluginFeature* o)
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
    return f ? f->value : TORIRS_PLUGIN_FEATURE_UNSET;
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
    if( v == TORIRS_PLUGIN_FEATURE_UNSET )
    {
        f->value = f->boot;
        return 1;
    }
    if( f->kind == TORIRS_PLUGIN_FEATURE_ENUM )
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
    struct ToriRS_PluginObjSnap* out)
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
    struct ToriRS_PluginObjInfo* out)
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
static int g_slot_x[TORIRS_PLUGIN_SLOT_COUNT];
static int g_slot_y[TORIRS_PLUGIN_SLOT_COUNT];
static int g_slot_w[TORIRS_PLUGIN_SLOT_COUNT];
static int g_slot_h[TORIRS_PLUGIN_SLOT_COUNT];
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
    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_COUNT )
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
static int g_role_replace_calls;
static int g_role_replace_plugin = -1;
static int g_role_replace_enabled = -1;
static char g_role_replace_name[TORIRS_PLUGIN_ROLE_NAME_MAX];
static int g_role_suppress_calls;
static int g_role_suppress_paint;
static int g_role_suppress_input;
static char g_role_suppress_name[TORIRS_PLUGIN_ROLE_NAME_MAX];
static int g_role_anchor_calls;
static int g_role_anchor_resets;
static int g_role_anchor_invalids;
static int g_role_anchor_current_plugin = -1;
static int g_role_anchor_replace = -1;
static int g_role_anchor_last_replace = -1;
static int g_role_anchor_last_place = -1;
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
fake_role_replace(
    void* u,
    int plugin,
    char const* role,
    int enabled)
{
    (void)u;
    g_role_replace_calls++;
    g_role_replace_plugin = plugin;
    g_role_replace_enabled = enabled;
    snprintf(g_role_replace_name, sizeof(g_role_replace_name), "%s", role ? role : "");
    return role && role_is(role);
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
fake_role_anchor(
    void* u,
    int plugin,
    char const* role,
    int replace,
    int place)
{
    (void)u;
    if( !role )
    {
        g_role_anchor_resets++;
        g_role_anchor_current_plugin = -1;
        g_role_anchor_replace = -1;
        return 1;
    }
    if( role[0] == '\0' )
    {
        g_role_anchor_invalids++;
        g_role_anchor_current_plugin = plugin;
        g_role_anchor_replace = -2;
        return 0;
    }
    g_role_anchor_calls++;
    g_role_anchor_current_plugin = plugin;
    g_role_anchor_replace = replace;
    g_role_anchor_last_replace = replace;
    g_role_anchor_last_place = place;
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
    struct ToriRS_PluginLootSource* out)
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
    struct ToriRS_PluginLootRow* out)
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
static int g_lane_game = TORIRS_PLUGIN_GAME_UNKNOWN;

static int
fake_lane(
    void* u,
    struct ToriRS_PluginLane* o)
{
    (void)u;
    memset(o, 0, sizeof(*o));
    o->game = g_lane_game;
    /* An unidentified cache answers 0 with the whole struct zeroed, which is
     * the one answer a plugin is told not to decide on. */
    if( g_lane_game == TORIRS_PLUGIN_GAME_UNKNOWN )
        return 0;
    o->epoch = TORIRS_PLUGIN_EPOCH_DAT2;
    o->revision = g_lane_game == TORIRS_PLUGIN_GAME_OLDSCHOOL ? 239 : 254;
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
    e.role_replace = fake_role_replace;
    e.role_suppress_facets = fake_role_suppress_facets;
    e.role_anchor = fake_role_anchor;
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

/* ---------------------------------------------------------- test plugins */

static int g_order[8];
static int g_order_count;
static int g_alpha_ticks;
static uint32_t g_last_tag;
static int g_select_calls;
static int g_screen_changes;
static int g_screen_change_to;
static int g_screen_change_from;

static enum ToriRS_PluginVerdict
alpha_tick(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_alpha_ticks++;
    if( g_order_count < 8 )
        g_order[g_order_count++] = 1;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
alpha_screen(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ud;
    struct ToriRS_PluginEvScreen const* screen_ev = ev;
    g_screen_changes++;
    g_screen_change_to = screen_ev->screen;
    g_screen_change_from = screen_ev->previous;
    return TORIRS_PLUGIN_PASS;
}

static struct ToriRS_PluginApi const* g_api;

static enum ToriRS_PluginVerdict
alpha_menu_add(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ud;
    g_api->menu_add(ctx, (struct ToriRS_PluginEvMenuBuild*)ev, "Tag Goblin", 7u);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
alpha_select(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ud;
    struct ToriRS_PluginEvMenuSelect* sel = ev;
    g_select_calls++;
    g_last_tag = sel->plugin_tag;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
alpha_packet_in(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ud;
    struct ToriRS_PluginEvPacketIn* p = ev;
    if( p->name == 99 )
        p->drop = true;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
alpha_draw(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ud;
    struct ToriRS_PluginEvDraw* d = ev;
    g_api->draw_hull(ctx, d->surface, 3, 0xff0000u, 0, TORIRS_PLUGIN_HULL_MESH);
    /* Well past the budget, to prove the host stops handing calls through. */
    for( int i = 0; i < 400; i++ )
        g_api->draw_tile(ctx, d->surface, 1, 1, 0, 0xffffffu, 0xffffffu, 0);
    return TORIRS_PLUGIN_PASS;
}

static int g_alpha_anchor_second_saw_reset;

static enum ToriRS_PluginVerdict
alpha_canvas_anchor(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ev;
    (void)ud;
    (void)g_api->role_anchor(ctx, "report_button", TORIRS_PLUGIN_ANCHOR_AFTER);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
alpha_canvas_after_anchor(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_alpha_anchor_second_saw_reset = g_role_anchor_current_plugin < 0;
    return TORIRS_PLUGIN_PASS;
}

static void
alpha_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LOGIC_TICK, alpha_tick, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_SCREEN_CHANGE, alpha_screen, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_MENU_BUILD, alpha_menu_add, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_MENU_SELECT, alpha_select, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_PACKET_IN, alpha_packet_in, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_WORLD, alpha_draw, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_CANVAS, alpha_canvas_anchor, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_CANVAS, alpha_canvas_after_anchor, NULL);
}

static struct ToriRS_PluginConfigItem const ALPHA_CONFIG[] = {
    { "colour", TORIRS_PLUGIN_CFG_COLOR,  "Colour", "#00FF00", 0, 0,  NULL },
    { "level",  TORIRS_PLUGIN_CFG_INT,    "Level",  "3",       0, 10, NULL },
    { "on",     TORIRS_PLUGIN_CFG_BOOL,   "On",     "1",       0, 0,  NULL },
    { "hidden", TORIRS_PLUGIN_CFG_STRING, NULL,     "",        0, 0,  NULL },
    { NULL,     TORIRS_PLUGIN_CFG_BOOL,   NULL,     NULL,      0, 0,  NULL },
};

static struct ToriRS_PluginDef const ALPHA = {
    .name = "alpha",
    .title = "Alpha The Plugin",
    .version = "1",
    .priority = 0,
    .config = ALPHA_CONFIG,
    .init = alpha_init,
};

/*
 * gamma: the loot-beam shape, cut down to what the host owns.
 *
 * It exists to pin three things that are silent when they break: an asset name
 * that must be refused before it reaches the engine, objects that must leave
 * the world when their plugin stops, and an asset delivered to a plugin that
 * did not ask for it.
 */
static int g_gamma_assets;
static int g_gamma_asset_ok;
static int g_gamma_objects[3];
static int g_gamma_object_count;
static int g_gamma_chats;
static char g_gamma_chat_text[200];
static int g_gamma_game_events;
static char g_gamma_event_kind[32];
static char g_gamma_event_subject[64];
static int g_gamma_event_value;

static enum ToriRS_PluginVerdict
gamma_chat(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ud;
    struct ToriRS_PluginEvChat* c = ev;
    g_gamma_chats++;
    snprintf(g_gamma_chat_text, sizeof(g_gamma_chat_text), "%s", c->text);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
gamma_game_event(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ud;
    struct ToriRS_PluginEvGameEvent* g = ev;
    g_gamma_game_events++;
    snprintf(g_gamma_event_kind, sizeof(g_gamma_event_kind), "%s", g->kind ? g->kind : "");
    snprintf(g_gamma_event_subject, sizeof(g_gamma_event_subject), "%s", g->subject);
    g_gamma_event_value = g->value;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
gamma_asset(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ud;
    struct ToriRS_PluginEvAsset* a = ev;
    g_gamma_assets++;
    g_gamma_asset_ok = a->ok;
    return TORIRS_PLUGIN_PASS;
}

static int g_gamma_mesh = -1;

static void
gamma_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    api->subscribe(ctx, TORIRS_PLUGIN_EV_ASSET, gamma_asset, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_CHAT_MESSAGE, gamma_chat, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_GAME_EVENT, gamma_game_event, NULL);

    g_gamma_object_count = 0;
    for( int i = 0; i < 3; i++ )
    {
        int const handle = api->object_create(ctx);
        if( handle >= 0 )
            g_gamma_objects[g_gamma_object_count++] = handle;
    }

    /* One authored triangle, so the mesh seam is exercised by a plugin that
     * also uses the cache seam: the two sources have to coexist on one host. */
    g_gamma_mesh = api->mesh_create(ctx);
    if( g_gamma_mesh >= 0 )
    {
        int const a = api->mesh_vertex(ctx, g_gamma_mesh, -64, 0, 0);
        int const b = api->mesh_vertex(ctx, g_gamma_mesh, 64, 0, 0);
        int const c = api->mesh_vertex(ctx, g_gamma_mesh, 0, -256, 0);
        api->mesh_face(ctx, g_gamma_mesh, a, b, c, api->hsl_from_rgb(ctx, 0xFF9600), 64);
    }

    for( int i = 0; i < g_gamma_object_count; i++ )
    {
        api->object_set_model(ctx, g_gamma_objects[i], TORIRS_PLUGIN_MODEL_CACHE, 43330);
        api->object_recolor(ctx, g_gamma_objects[i], 26432, api->hsl_from_rgb(ctx, 0xFF9600));
        api->object_set_anim(ctx, g_gamma_objects[i], 9260, 1);
        api->object_set_position(ctx, g_gamma_objects[i], 3200 + i, 3200, 0, 0, 0);
        api->object_set_active(ctx, g_gamma_objects[i], 1);
    }
}

/* Declared beside the objects it stands next to; see gamma_init. */
static struct ToriRS_PluginDef const GAMMA = {
    .name = "gamma",
    .version = "1",
    .priority = 0,
    .config = NULL,
    .init = gamma_init,
};

/* ---- a plugin with a window tab ------------------------------------------
 *
 * Declares its controls in EV_UI_BUILD rather than in init, which is the shape
 * the contract asks for: the host re-raises BUILD whenever the tab is empty --
 * after a reload, after a re-enable -- and a plugin that built its tab only
 * once would come back from either with a blank one.
 */
static int g_win_builds;
static int g_win_events;
static char g_win_last_id[64];
static int g_win_last_action;
static int g_win_last_value;
static char g_win_last_text[64];

static enum ToriRS_PluginVerdict
win_build(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    struct ToriRS_PluginApi const* api = g_api;
    (void)ev;
    (void)ud;
    g_win_builds++;
    api->win_request(ctx, "Beams");
    api->win_widget(ctx, TORIRS_PLUGIN_W_CHECKBOX, "enabled", "enabled");
    api->win_widget(ctx, TORIRS_PLUGIN_W_INPUT, "colour", "colour");
    api->win_widget(ctx, TORIRS_PLUGIN_W_DROPDOWN, "mode", "mode");
    api->win_widget(ctx, TORIRS_PLUGIN_W_BUTTON, "reset", "Reset");
    api->win_set_checked(ctx, "enabled", true);
    api->win_set_text(ctx, "colour", "#FFCC00");
    api->win_set_options(ctx, "mode", "beam|ring|off", 0);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
win_ui(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    struct ToriRS_PluginEvUi const* e = ev;
    (void)ctx;
    (void)ud;
    g_win_events++;
    snprintf(g_win_last_id, sizeof(g_win_last_id), "%s", e->widget_id ? e->widget_id : "");
    g_win_last_action = e->action;
    g_win_last_value = e->value;
    snprintf(g_win_last_text, sizeof(g_win_last_text), "%s", e->text ? e->text : "");
    return TORIRS_PLUGIN_PASS;
}

static void
winner_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_UI_BUILD, win_build, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_UI, win_ui, NULL);
}

static struct ToriRS_PluginDef const WINNER = {
    .name = "winner",
    .version = "1",
    .priority = 0,
    .config = NULL,
    .init = winner_init,
};

/* ---- two plugins contending for the one application panel --------------- */
static int g_panel_a_index;
static int g_panel_b_index;
static int g_panel_a_builds;
static int g_panel_b_builds;
static int g_panel_a_actions;
static int g_panel_b_actions;
static int g_panel_a_layouts;
static int g_panel_b_layouts;
static int g_panel_a_hides;
static int g_panel_b_hides;
static int g_panel_a_draws;
static int g_panel_b_draws;
static int g_panel_draw_surface_mode;
static uint32_t g_panel_last_generation;
static int g_panel_last_view;
static uint64_t g_panel_last_sequence;
static char g_panel_last_id[64];

static enum ToriRS_PluginVerdict
panel_start(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    struct ToriRS_PluginPanelDesc desc;
    int const index = PluginHost_CtxIndex(ctx);
    (void)ev;
    (void)ud;

    memset(&desc, 0, sizeof(desc));
    if( index == g_panel_a_index )
    {
        desc.icon_asset = "alpha.png";
        desc.preferred_width = 100; /* proves the common lower clamp */
    }
    else
    {
        desc.preferred_width = 900; /* and the upper clamp */
    }
    CHECK(g_api->panel_request(ctx, &desc), "EV_START may register panel metadata");
    if( index == g_panel_b_index )
    {
        CHECK(
            g_api->panel_set_attention(ctx, true), "an inactive rail entry may request attention");
    }
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
panel_build(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* ud)
{
    struct ToriRS_PluginEvPanelBuild const* ev = payload;
    int const index = PluginHost_CtxIndex(ctx);
    (void)ud;

    g_panel_last_generation = ev->selection_generation;
    g_panel_last_view = ev->view;
    if( index == g_panel_a_index )
        g_panel_a_builds++;
    else
        g_panel_b_builds++;
    /* The SETTINGS face of these probes declares nothing, exactly as a plugin
     * whose knobs are all config keys does. @see enum ToriRS_PluginPanelView. */
    if( ev->view != TORIRS_PLUGIN_PANEL_VIEW_PAGE )
        return TORIRS_PLUGIN_PASS;
    CHECK(
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_CHECKBOX, "shared", "Enabled"),
        "the selected build may declare a semantic control");
    CHECK(
        g_api->panel_widget(ctx, TORIRS_PLUGIN_W_CUSTOM, "chart", "Activity chart"),
        "the selected build may declare a custom region");
    CHECK(g_api->panel_set_value(ctx, "shared", 1), "a build can seed result state");
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
panel_action(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* ud)
{
    struct ToriRS_PluginEvPanelAction const* ev = payload;
    (void)ud;
    if( PluginHost_CtxIndex(ctx) == g_panel_a_index )
        g_panel_a_actions++;
    else
        g_panel_b_actions++;
    snprintf(g_panel_last_id, sizeof(g_panel_last_id), "%s", ev->id ? ev->id : "");
    g_panel_last_generation = ev->selection_generation;
    g_panel_last_sequence = ev->intent_sequence;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
panel_layout(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* ud)
{
    struct ToriRS_PluginEvPanelLayout const* ev = payload;
    (void)ud;
    if( PluginHost_CtxIndex(ctx) == g_panel_a_index )
    {
        g_panel_a_layouts++;
        if( !ev->visible )
            g_panel_a_hides++;
    }
    else
    {
        g_panel_b_layouts++;
        if( !ev->visible )
            g_panel_b_hides++;
    }
    g_panel_last_generation = ev->selection_generation;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
panel_draw(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* ud)
{
    struct ToriRS_PluginEvPanelDraw const* ev = payload;
    (void)ud;
    if( PluginHost_CtxIndex(ctx) == g_panel_a_index )
        g_panel_a_draws++;
    else
        g_panel_b_draws++;
    g_panel_draw_surface_mode = g_engine.draw_canvas;
    g_api->draw_rect(ctx, ev->surface, 0, 0, ev->width, ev->height, 0x123456u, 255);
    return TORIRS_PLUGIN_PASS;
}

static void
panel_plugin_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, panel_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_PANEL_BUILD, panel_build, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_PANEL_ACTION, panel_action, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_PANEL_LAYOUT, panel_layout, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_PANEL_DRAW, panel_draw, NULL);
}

static struct ToriRS_PluginDef const PANEL_ALPHA = {
    .name = "panel-alpha",
    .title = "Panel Alpha",
    .version = "1",
    .init = panel_plugin_init,
};

static struct ToriRS_PluginDef const PANEL_BETA = {
    .name = "panel-beta",
    .title = "Panel Beta",
    .version = "1",
    .init = panel_plugin_init,
};

/* ---- a plugin that reads its config at start ------------------------------
 *
 * The shape reload exists for: a plugin reads a key in on_start and caches
 * what it found, so writing that key underneath a running plugin leaves it
 * running on the old value. The counter proves the restart happened and the
 * captured string proves it happened AFTER the write.
 */
static int g_reload_starts;
static int g_reload_stops;
static int g_reload_hook_calls;
static char g_reload_seen[64];

static enum ToriRS_PluginVerdict
reloader_start(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    char const* v;
    (void)ev;
    (void)ud;
    g_reload_starts++;
    v = g_api->cfg_str(ctx, "colour");
    snprintf(g_reload_seen, sizeof(g_reload_seen), "%s", v ? v : "");
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
reloader_stop(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_reload_stops++;
    return TORIRS_PLUGIN_PASS;
}

static struct ToriRS_PluginConfigItem const RELOADER_CFG[] = {
    { .key = "colour",
     .label = "colour",
     .type = TORIRS_PLUGIN_CFG_STRING,
     .default_value = "#000000" },
    { 0 },
};

static void
reloader_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, reloader_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_STOP, reloader_stop, NULL);
}

/* Stands in for the Lua adapter's rebuild-from-source hook. */
static void
reloader_reload(struct ToriRS_PluginCtx* ctx)
{
    (void)ctx;
    g_reload_hook_calls++;
}

static struct ToriRS_PluginDef const RELOADER = {
    .name = "reloader",
    .version = "1",
    .priority = 0,
    .config = RELOADER_CFG,
    .init = reloader_init,
    .reload = reloader_reload,
};

/* Higher priority: must be dispatched before alpha regardless of order. */
static enum ToriRS_PluginVerdict
beta_tick(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    if( g_order_count < 8 )
        g_order[g_order_count++] = 2;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
beta_key_consume(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    return TORIRS_PLUGIN_CONSUME;
}

static int g_beta_anchor_attempt;
static int g_beta_anchor_retarget_invalid;
static int g_beta_draw_saw_invalid;

static enum ToriRS_PluginVerdict
beta_canvas_competing_anchor(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    struct ToriRS_PluginEvDraw* draw = ev;

    (void)ud;
    if( !g_beta_anchor_attempt )
        return TORIRS_PLUGIN_PASS;
    g_beta_anchor_retarget_invalid =
        !g_api->role_anchor(ctx, "report_button", TORIRS_PLUGIN_ANCHOR_AFTER) &&
        g_role_anchor_current_plugin >= 0 && g_role_anchor_replace == -2;
    g_beta_draw_saw_invalid = g_role_anchor_replace == -2;
    g_api->draw_rect(ctx, draw->surface, 1, 1, 2, 2, 0xffffffu, 0);
    return TORIRS_PLUGIN_PASS;
}

static void
beta_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LOGIC_TICK, beta_tick, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_KEY, beta_key_consume, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_CANVAS, beta_canvas_competing_anchor, NULL);
}

static struct ToriRS_PluginDef const BETA = {
    .name = "beta",
    .version = "1",
    .priority = 10,
    .config = NULL,
    .init = beta_init,
};

/*
 * The shape the gameframe plugin has: it asks which lane it booted on and, on
 * one it cannot work on, switches ITSELF off.
 *
 * A local def rather than the real plugin because what is under test is the
 * HOST's half -- that a refusal reads as off, that it survives a second Start,
 * that it does not reach the settings file, and that an explicit enable or a
 * reload puts the question back. The gameframe's own answer is tested beside
 * the gameframe.
 *
 * `disabled_by_default` because that is where the trap is: the encoder writes
 * `enabled=` only when the switch DIFFERS from the declared default, so a
 * refusal that cleared `enabled` on this def would silently drop the user's
 * saved `enabled=1` instead of leaving it for the next lane.
 */
static int g_standoff_inits;
static int g_standoff_starts;
static int g_standoff_stops;
static int g_standoff_ticks;

static enum ToriRS_PluginVerdict
standoff_start(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_standoff_starts++;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
standoff_stop(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_standoff_stops++;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
standoff_tick(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_standoff_ticks++;
    return TORIRS_PLUGIN_PASS;
}

static void
standoff_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    struct ToriRS_PluginLane lane;

    g_api = api;
    g_standoff_inits++;

    /* Before the subscriptions, so a lane it cannot run on never gets a
     * handler of this plugin's registered at all. */
    if( api->lane(ctx, &lane) && lane.game == TORIRS_PLUGIN_GAME_OLDSCHOOL )
    {
        api->disable_self(ctx, "this cache brings its own gameframe");
        return;
    }
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, standoff_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_STOP, standoff_stop, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LOGIC_TICK, standoff_tick, NULL);
}

static struct ToriRS_PluginDef const STANDOFF = {
    .name = "standoff",
    .version = "1",
    .disabled_by_default = true,
    .init = standoff_init,
};

/*
 * And one that decides from inside a handler instead of from init.
 *
 * The api verb is documented to be legal there too, and the caller goes on
 * running afterwards -- which is the part that can break: the teardown it runs
 * is the same one PluginHost_SetEnabled uses, so it ends by clearing the
 * host's dispatching mark out from under the handler that called it.
 */
static int g_latecomer_after;
static int g_latecomer_stops;

static enum ToriRS_PluginVerdict
latecomer_stop(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_latecomer_stops++;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
latecomer_start(
    struct ToriRS_PluginCtx* ctx,
    void* ev,
    void* ud)
{
    (void)ev;
    (void)ud;
    g_api->disable_self(ctx, "not on this lane");
    /* Still this plugin's ctx, still answering for it: an api call the rest of
     * the handler makes must not come back for whoever ran before it. */
    if( g_api->cfg_str(ctx, "colour") )
        g_latecomer_after++;
    return TORIRS_PLUGIN_PASS;
}

static struct ToriRS_PluginConfigItem const LATECOMER_CFG[] = {
    { .key = "colour",
     .label = "colour",
     .type = TORIRS_PLUGIN_CFG_STRING,
     .default_value = "#000000" },
    { 0 },
};

static void
latecomer_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, latecomer_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_STOP, latecomer_stop, NULL);
}

static struct ToriRS_PluginDef const LATECOMER = {
    .name = "latecomer",
    .version = "1",
    .config = LATECOMER_CFG,
    .init = latecomer_init,
};

static struct ToriRS_PluginDef const OFF_BY_DEFAULT = {
    .name = "sleeper",
    .version = "1",
    .disabled_by_default = true,
};

/*
 * The shape the Feature Flags plugin has: listed, but with one state.
 *
 * Registered here rather than tested through the real plugin because what is
 * under test is the HOST's half of the contract -- that `essential` is
 * reported, that SetEnabled will not clear it, and that a saved `enabled=0`
 * does not either. The plugin's own page is the panel's business.
 */
static struct ToriRS_PluginDef const ESSENTIAL = {
    .name = "always-on",
    .title = "Always On",
    .version = "1",
    .essential = true,
};

/* Declares no title, so the host must make one: the roster is not allowed to
 * fall back to printing the id at anybody. */
static struct ToriRS_PluginDef const TITLELESS = {
    .name = "ground-items_2",
    .version = "1",
};

static int g_frame_desktop_starts;
static int g_frame_desktop_stops;
static int g_frame_desktop_layouts;
static int g_frame_mobile_starts;
static int g_frame_mobile_stops;
static int g_frame_mobile_layouts;

static enum ToriRS_PluginVerdict
frame_desktop_event(
    struct ToriRS_PluginCtx* ctx,
    void* event,
    void* userdata)
{
    int const which = (int)(intptr_t)userdata;
    (void)ctx;
    (void)event;
    if( which == TORIRS_PLUGIN_EV_START )
        g_frame_desktop_starts++;
    else if( which == TORIRS_PLUGIN_EV_STOP )
        g_frame_desktop_stops++;
    else
    {
        struct ToriRS_PluginEvLayout const* layout = event;
        (void)g_api->layout_slot(
            ctx, TORIRS_PLUGIN_SLOT_VIEWPORT, 0, 0, layout->width, layout->height);
        g_frame_desktop_layouts++;
    }
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
frame_mobile_event(
    struct ToriRS_PluginCtx* ctx,
    void* event,
    void* userdata)
{
    int const which = (int)(intptr_t)userdata;
    (void)ctx;
    (void)event;
    if( which == TORIRS_PLUGIN_EV_START )
        g_frame_mobile_starts++;
    else if( which == TORIRS_PLUGIN_EV_STOP )
        g_frame_mobile_stops++;
    else
    {
        struct ToriRS_PluginEvLayout const* layout = event;
        (void)g_api->layout_slot(
            ctx, TORIRS_PLUGIN_SLOT_VIEWPORT, 0, 0, layout->width, layout->height);
        g_frame_mobile_layouts++;
    }
    return TORIRS_PLUGIN_PASS;
}

static void
frame_desktop_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    g_api = api;
    api->subscribe(
        ctx, TORIRS_PLUGIN_EV_START, frame_desktop_event, (void*)(intptr_t)TORIRS_PLUGIN_EV_START);
    api->subscribe(
        ctx, TORIRS_PLUGIN_EV_STOP, frame_desktop_event, (void*)(intptr_t)TORIRS_PLUGIN_EV_STOP);
    api->subscribe(
        ctx,
        TORIRS_PLUGIN_EV_LAYOUT,
        frame_desktop_event,
        (void*)(intptr_t)TORIRS_PLUGIN_EV_LAYOUT);
}

static void
frame_mobile_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    g_api = api;
    api->subscribe(
        ctx, TORIRS_PLUGIN_EV_START, frame_mobile_event, (void*)(intptr_t)TORIRS_PLUGIN_EV_START);
    api->subscribe(
        ctx, TORIRS_PLUGIN_EV_STOP, frame_mobile_event, (void*)(intptr_t)TORIRS_PLUGIN_EV_STOP);
    api->subscribe(
        ctx, TORIRS_PLUGIN_EV_LAYOUT, frame_mobile_event, (void*)(intptr_t)TORIRS_PLUGIN_EV_LAYOUT);
}

static struct ToriRS_PluginFrameOffer const FRAME_DESKTOP_OFFERS[] = {
    { "fixed", "Fixed Test Frame", TORIRS_PLUGIN_CANVAS_FIXED, 765, 503 },
    { NULL,    NULL,               0,                          0,   0   },
};

static struct ToriRS_PluginFrameOffer const FRAME_MOBILE_OFFERS[] = {
    { "phone", "Phone Test Frame", TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW, 320, 240 },
    { NULL,    NULL,               0,                                  0,   0   },
};

static struct ToriRS_PluginDef const FRAME_DESKTOP_PROVIDER = {
    .name = "frame-desktop",
    .version = "1",
    .disabled_by_default = true,
    .frames = FRAME_DESKTOP_OFFERS,
    .init = frame_desktop_init,
};

static struct ToriRS_PluginDef const FRAME_MOBILE_PROVIDER = {
    .name = "frame-mobile",
    .version = "1",
    .disabled_by_default = true,
    .frames = FRAME_MOBILE_OFFERS,
    .init = frame_mobile_init,
};

/* ------------------------------------------------ synthetic v2 instances */

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
    struct ToriRS_PluginPlayerSnap player;
    struct ToriRS_UiNodeInfo info = { .struct_size = sizeof(info) };
    struct ToriRS_UiContributionInfo contribution = {
        .struct_size = sizeof(contribution),
    };
    struct ToriRS_Rect placed;
    struct ToriRS_UiNodeRef own;
    struct ToriRS_UiNodeRef shared;
    struct ToriRS_PluginPanelDesc panel = {
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
    g_v2_starts[marker]++;

    CHECK(api->core.screen(api) == TORIRS_PLUGIN_SCREEN_GAME, "v2 core module reaches host");
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
    struct ToriRS_PluginEvTick const* event)
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
    if( view != TORIRS_PLUGIN_PANEL_VIEW_PAGE )
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
    struct ToriRS_PluginEvPanelAction const* event)
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

static struct ToriRS_PluginConfigItem const V2_CONFIG_A_ITEMS[] = {
    { .key = "marker", .label = "Marker", .type = TORIRS_PLUGIN_CFG_INT, .default_value = "1" },
    { 0 },
};
static struct ToriRS_PluginConfigItem const V2_CONFIG_B_ITEMS[] = {
    { .key = "marker", .label = "Marker", .type = TORIRS_PLUGIN_CFG_INT, .default_value = "2" },
    { 0 },
};
static struct ToriRS_PluginConfigItem const V2_CONFIG_FRAME_ITEMS[] = {
    { .key = "marker", .label = "Marker", .type = TORIRS_PLUGIN_CFG_INT, .default_value = "3" },
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
    .flags = TORIRS_PLUGIN_V2_ADAPTER,
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

enum V2TransitionMode
{
    V2_TRANSITION_READY = 0,
    V2_TRANSITION_PENDING,
    V2_TRANSITION_NO_VIEWPORT,
    V2_TRANSITION_DUP_SURFACE,
    V2_TRANSITION_DUP_MEMBER,
    V2_TRANSITION_BAD_RECT,
    V2_TRANSITION_TOO_MANY_NODES,
    V2_TRANSITION_CYCLE,
    V2_TRANSITION_FOREIGN_IMAGE,
    V2_TRANSITION_UNREADY_IMAGE,
    V2_TRANSITION_ORPHAN_SKIN,
    V2_TRANSITION_SELECT_AUTO,
};

static int g_v2_transition_mode;
static int g_v2_transition_callback_alive;
static int g_v2_transition_stops;

struct V2TransitionState
{
    uint32_t canary;
};

static void
v2_transition_start(struct ToriRS_ApiV2* api, void* state_ptr)
{
    struct V2TransitionState* state = state_ptr;
    (void)api;
    state->canary = 0x51a7e123u;
}

static void
v2_transition_stop(struct ToriRS_ApiV2* api, void* state_ptr)
{
    struct V2TransitionState* state = state_ptr;
    (void)api;
    CHECK(state && state->canary == 0x51a7e123u, "frame provider state is live through on_stop");
    g_v2_transition_stops++;
}

static enum ToriRS_FrameBuildResult
v2_transition_build(
    struct ToriRS_ApiV2* api,
    void* state,
    struct ToriRS_FrameBuilder* frame,
    struct ToriRS_FrameBuildContext const* context)
{
    struct V2TransitionState* transition_state = state;
    struct ToriRS_UiNode marker = {
        .struct_size = sizeof(marker),
        .bounds = { 2, 3, 20, 10 },
        .parent = "frame.viewport",
        .anchor = TORIRS_ANCHOR_TOP_LEFT,
        .paint_order = TORIRS_UI_PAINT_AFTER_PARENT,
        .flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED,
    };
    (void)context;

    if( g_v2_transition_mode == V2_TRANSITION_PENDING )
    {
        frame->reason(frame, "Waiting for transition test data.");
        return TORIRS_FRAME_PENDING;
    }
    if( g_v2_transition_mode != V2_TRANSITION_NO_VIEWPORT )
        frame->surface(
            frame,
            TORIRS_SURFACE_VIEWPORT,
            g_v2_transition_mode == V2_TRANSITION_BAD_RECT
                ? (struct ToriRS_Rect){ 0, 0, 0, 480 }
                : (struct ToriRS_Rect){ 0, 0, 640, 480 });
    if( g_v2_transition_mode == V2_TRANSITION_DUP_SURFACE )
        frame->surface(
            frame, TORIRS_SURFACE_VIEWPORT, (struct ToriRS_Rect){ 1, 1, 639, 479 });
    if( g_v2_transition_mode == V2_TRANSITION_DUP_MEMBER )
    {
        frame->surface_member(
            frame, TORIRS_SURFACE_SIDEBAR, 0, (struct ToriRS_Rect){ 500, 100, 120, 200 });
        frame->surface_member(
            frame, TORIRS_SURFACE_SIDEBAR, 0, (struct ToriRS_Rect){ 501, 100, 120, 200 });
    }
    marker.label = g_v2_transition_mode == V2_TRANSITION_READY ? "committed" : "candidate";
    frame->ui_node(frame, "marker", &marker);
    if( g_v2_transition_mode == V2_TRANSITION_TOO_MANY_NODES )
        for( int i = 0; i < 17; i++ )
        {
            char name[24];
            snprintf(name, sizeof(name), "extra.%d", i);
            frame->ui_node(frame, name, &marker);
        }
    if( g_v2_transition_mode == V2_TRANSITION_CYCLE )
    {
        struct ToriRS_UiNode a = marker;
        struct ToriRS_UiNode b = marker;
        a.parent = "cycle.b";
        b.parent = "cycle.a";
        frame->ui_node(frame, "cycle.a", &a);
        frame->ui_node(frame, "cycle.b", &b);
    }
    if( g_v2_transition_mode == V2_TRANSITION_FOREIGN_IMAGE )
    {
        struct ToriRS_FrameSkin skin = {
            .struct_size = sizeof(skin),
            .image = { 1000 },
        };
        frame->surface(
            frame, TORIRS_SURFACE_COMPASS, (struct ToriRS_Rect){ 600, 0, 32, 32 });
        frame->skin(frame, TORIRS_SURFACE_COMPASS, &skin);
    }
    if( g_v2_transition_mode == V2_TRANSITION_UNREADY_IMAGE )
    {
        struct ToriRS_ImageRef pending = { 0 };
        struct ToriRS_FrameSkin skin = { .struct_size = sizeof(skin) };
        (void)api->assets.image(api, "pending-frame.png", &pending);
        skin.image = pending;
        frame->surface(
            frame, TORIRS_SURFACE_COMPASS, (struct ToriRS_Rect){ 600, 0, 32, 32 });
        frame->skin(frame, TORIRS_SURFACE_COMPASS, &skin);
    }
    if( g_v2_transition_mode == V2_TRANSITION_ORPHAN_SKIN )
    {
        struct ToriRS_FrameSkin skin = { .struct_size = sizeof(skin) };
        frame->skin(frame, TORIRS_SURFACE_COMPASS, &skin);
    }
    if( g_v2_transition_mode == V2_TRANSITION_SELECT_AUTO )
    {
        int const stops = g_v2_transition_stops;
        enum ToriRS_Result const selected = api->frame.select(api, "auto");
        g_v2_transition_callback_alive =
            selected == TORIRS_RESULT_OK && transition_state &&
            transition_state->canary == 0x51a7e123u && g_v2_transition_stops == stops &&
            api->core.screen(api) == TORIRS_PLUGIN_SCREEN_GAME;
    }
    return TORIRS_FRAME_READY;
}

static struct ToriRS_FrameOffer const V2_TRANSITION_OFFERS[] = {
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "candidate",
        .title = "Candidate",
        .canvas = TORIRS_FRAME_CANVAS_WINDOW,
        .min_width = 640,
        .min_height = 480,
        .build = v2_transition_build,
    },
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "invalid",
        .title = "Invalid candidate",
        .canvas = TORIRS_FRAME_CANVAS_WINDOW,
        .min_width = 640,
        .min_height = 480,
        .build = v2_transition_build,
    },
    { .struct_size = sizeof(struct ToriRS_FrameOffer) },
};

static struct ToriRS_PluginDefV2 const V2_TRANSITION_PROVIDER = {
    .struct_size = sizeof(V2_TRANSITION_PROVIDER),
    .id = "v2-transition",
    .title = "V2 Transition",
    .version = "2.0.0",
    .state_size = sizeof(struct V2TransitionState),
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = v2_transition_start,
        .on_stop = v2_transition_stop,
    },
    .frames = V2_TRANSITION_OFFERS,
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
    struct ToriRS_PluginEvTick const* event)
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

static void
v2_aba_start(struct ToriRS_ApiV2* api, void* state_ptr)
{
    struct V2AbaState* state = state_ptr;

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
    struct ToriRS_PluginEvTick const* event)
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
         * not destroy/release the just-reallocated same legacy slots. */
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
                               api->core.screen(api) == TORIRS_PLUGIN_SCREEN_GAME;
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
static uint32_t g_present_v1_tag;
static int g_present_order_count;
static char g_present_order[8][TORIRS_UI_LABEL_MAX];
static int g_present_reorder_requested;
static enum ToriRS_Result g_present_reorder_result;
static int g_present_visibility_request = -1;
static enum ToriRS_Result g_present_visibility_result;
static int g_present_ancestor_visibility_request = -1;
static enum ToriRS_Result g_present_ancestor_visibility_result;

static enum ToriRS_PluginVerdict
present_v1_click(
    struct ToriRS_PluginCtx* context,
    void* event,
    void* userdata)
{
    (void)context;
    (void)userdata;
    g_present_v1_tag = ((struct ToriRS_PluginEvCanvasClick const*)event)->tag;
    return TORIRS_PLUGIN_PASS;
}

static void
present_v1_init(
    struct ToriRS_PluginCtx* context,
    struct ToriRS_PluginApi const* api)
{
    api->subscribe(context, TORIRS_PLUGIN_EV_CANVAS_CLICK, present_v1_click, NULL);
}

static struct ToriRS_PluginDef const PRESENT_V1 = {
    .name = "present-v1",
    .version = "1",
    .init = present_v1_init,
};

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
    foreign_image.image.value = 1; /* typed handle 1 -> unowned legacy slot 0 */
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
    struct ToriRS_PluginEvTick const* event)
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
    struct ToriRS_PluginEvTick const* event)
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
    struct ToriRS_PluginEvTick const* event)
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
    struct ToriRS_PluginEngine engine = fake_engine();
    struct ToriRS_PluginHost* host = PluginHost_New(&engine);

    int const a = PluginHost_Register(host, &ALPHA);
    int const b = PluginHost_Register(host, &BETA);
    int const z = PluginHost_Register(host, &OFF_BY_DEFAULT);
    CHECK(a == 0 && b == 1 && z == 2, "registration returns sequential indices");

    /* A duplicate name would silently share a settings section. */
    CHECK(PluginHost_Register(host, &ALPHA) < 0, "duplicate plugin name is refused");

    CHECK(!PluginHost_IsEnabled(host, z), "disabled_by_default starts off");
    CHECK(PluginHost_IsEnabled(host, a), "everything else starts on");

    /* Title is a LABEL and name is an IDENTITY: the panel reads one, the ini
     * section the other, and a plugin that declares no title still has to be
     * showable as words. */
    {
        int const t = PluginHost_Register(host, &TITLELESS);
        CHECK(
            strcmp(PluginHost_Title(host, a), "Alpha The Plugin") == 0,
            "a declared title is what the panel gets");
        CHECK(strcmp(PluginHost_Name(host, a), "alpha") == 0, "and the name is untouched by it");
        CHECK(
            strcmp(PluginHost_Title(host, t), "Ground Items 2") == 0,
            "a title-less plugin gets one derived from its id");
        CHECK(
            strcmp(PluginHost_Name(host, t), "ground-items_2") == 0,
            "which is not the id the ini section uses");
    }

    PluginHost_Start(host);

    /* Priority ordering: beta declared 10, alpha 0, so beta runs first even
     * though alpha registered first. */
    PluginHost_LogicTick(host, 1);
    CHECK(g_order_count == 2, "both tick subscribers ran");
    CHECK(g_order[0] == 2 && g_order[1] == 1, "higher priority dispatches first");

    /* Disable stops dispatch and drops subscriptions. */
    PluginHost_SetEnabled(host, a, false);
    g_alpha_ticks = 0;
    PluginHost_LogicTick(host, 2);
    CHECK(g_alpha_ticks == 0, "a disabled plugin receives nothing");
    PluginHost_SetEnabled(host, a, true);
    PluginHost_LogicTick(host, 3);
    CHECK(g_alpha_ticks == 1, "re-enabling restores its subscriptions");

    /*
     * EV_SCREEN_CHANGE: the frame boundary polls api->screen's source and
     * raises on a CHANGE, once, with both halves of the transition -- never
     * on a steady answer. The baseline is taken at host creation, so the
     * first frame of a session raises nothing.
     */
    {
        PluginHost_FrameStart(host, 100, 0);
        CHECK(g_screen_changes == 0, "a steady screen raises nothing");
        g_screen_now = TORIRS_PLUGIN_SCREEN_TITLE;
        PluginHost_FrameStart(host, 200, 0);
        CHECK(g_screen_changes == 1, "a moved screen raises once");
        CHECK(g_screen_change_to == TORIRS_PLUGIN_SCREEN_TITLE, "carrying the new answer");
        CHECK(g_screen_change_from == TORIRS_PLUGIN_SCREEN_GAME, "and the one it replaced");
        PluginHost_FrameStart(host, 300, 0);
        CHECK(g_screen_changes == 1, "and not again while it holds");
        g_screen_now = TORIRS_PLUGIN_SCREEN_GAME;
        PluginHost_FrameStart(host, 400, 0);
        CHECK(g_screen_changes == 2, "moving back is a change of its own");
        CHECK(
            g_screen_change_to == TORIRS_PLUGIN_SCREEN_GAME,
            "the login every gameframe gate waits for");
    }

    /*
     * Essential: listed, and with no second state.
     *
     * Every one of these is a way the switch could come back on a plugin whose
     * whole point is that it has none -- the panel writing it, an ini carrying
     * it from a build where it was ordinary, or the encoder saving one.
     */
    {
        int const e = PluginHost_Register(host, &ESSENTIAL);
        CHECK(PluginHost_IsEssential(host, e), "essential is reported to the roster");
        CHECK(!PluginHost_IsEssential(host, a), "and an ordinary plugin is not");
        CHECK(PluginHost_IsEnabled(host, e), "an essential plugin starts on");

        PluginHost_SetEnabled(host, e, false);
        CHECK(PluginHost_IsEnabled(host, e), "and cannot be switched off");

        PluginHost_ConfigApply(host, "always-on", "enabled", "0");
        CHECK(
            PluginHost_IsEnabled(host, e), "a saved enabled=0 from an older build is ignored too");
    }

    /*
     * Feature flags: the walk, the sentinel, and the two refusals.
     *
     * The refusals are the load-bearing half. A key the engine does not
     * publish is how a server-agreed flag stays out of a plugin's reach, and a
     * value outside the flag's range is what stops a typed number from
     * becoming a draw distance nothing can render.
     */
    {
        struct ToriRS_PluginApi const* api = PluginHost_Api(host);
        struct ToriRS_PluginCtx* ctx = PluginHost_Ctx(host, a);
        struct ToriRS_PluginFeature flag;
        int seen = 0;
        int iter = -1;

        while( (iter = api->feature_next(ctx, iter, &flag)) >= 0 )
            seen++;
        CHECK(seen == FAKE_FEATURE_COUNT, "the walk visits every published flag");

        CHECK(
            api->feature_get(ctx, "pathing_mode") == TORIRS_PLUGIN_FEATURE_UNSET,
            "an unpublished flag reads as unset");
        CHECK(
            !api->feature_set(ctx, "pathing_mode", 1),
            "and cannot be set, which is the whole server-agreement rule");

        CHECK(api->feature_set(ctx, "draw_distance", 60), "a value in range is taken");
        CHECK(api->feature_get(ctx, "draw_distance") == 60, "and is what reads back");
        CHECK(!api->feature_set(ctx, "draw_distance", 4000), "a value out of range is not");
        CHECK(api->feature_get(ctx, "draw_distance") == 60, "and leaves the flag alone");

        CHECK(
            api->feature_set(ctx, "draw_distance", TORIRS_PLUGIN_FEATURE_UNSET),
            "the sentinel is accepted");
        CHECK(api->feature_get(ctx, "draw_distance") == 25, "and restores what the boot resolved");

        CHECK(api->feature_set(ctx, "camera_zoom", 1), "an enum takes a declared value");
        CHECK(!api->feature_set(ctx, "camera_zoom", 7), "and refuses one it never offered");
        CHECK(api->feature_get(ctx, "camera_zoom") == 1, "leaving the declared one in place");
    }

    /*
     * Region readouts: the role, and one MEMBER of it.
     *
     * The member read is the half a plugin anchoring to the report abuse
     * button needs, and the two answers have to be able to DIFFER -- the role
     * answers with whichever chat button the frame found first, the member
     * with the one that was asked for. A member the frame does not declare is
     * an answer ("no such button here"), not a fault, and it must leave the
     * caller's outs alone rather than half-filling them.
     */
    {
        struct ToriRS_PluginApi const* api = PluginHost_Api(host);
        struct ToriRS_PluginCtx* ctx = PluginHost_Ctx(host, a);
        int x = -1, y = -1, w = -1, h = -1;

        g_slot_x[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS] = 6;
        g_slot_y[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS] = 467;
        g_slot_w[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS] = 56;
        g_slot_h[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS] = 19;
        g_member_slot = TORIRS_PLUGIN_SLOT_CHAT_BUTTONS;
        g_member_no = 3;
        g_member_box[0] = 408;
        g_member_box[1] = 467;
        g_member_box[2] = 56;
        g_member_box[3] = 19;

        CHECK(
            api->slot_rect(ctx, TORIRS_PLUGIN_SLOT_CHAT_BUTTONS, &x, &y, &w, &h),
            "the role answers");
        CHECK(x == 6, "with the box the frame gave the role");

        x = y = w = h = -1;
        CHECK(
            api->slot_member_rect(ctx, TORIRS_PLUGIN_SLOT_CHAT_BUTTONS, 3, &x, &y, &w, &h),
            "and the member answers for the number it was asked for");
        CHECK(
            x == 408 && y == 467 && w == 56 && h == 19,
            "with that member's own box, not the role's");

        x = y = w = h = -1;
        CHECK(
            !api->slot_member_rect(ctx, TORIRS_PLUGIN_SLOT_CHAT_BUTTONS, 2, &x, &y, &w, &h),
            "a member this frame does not declare is 0");
        CHECK(x == -1 && y == -1 && w == -1 && h == -1, "and leaves the outputs untouched");

        CHECK(
            !api->slot_member_rect(ctx, TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME, 0, &x, &y, &w, &h),
            "SAFE is derived and has no members to number");
        CHECK(
            !api->slot_member_rect(ctx, TORIRS_PLUGIN_SLOT_CHAT_BUTTONS, -1, &x, &y, &w, &h),
            "and \"any member\" is not a question this verb takes");

        /*
         * And the same box reached by COMPONENT ID, which is what a cache
         * frame leaves a plugin: its chat buttons are the interface's own
         * widgets, so they carry no role at all and the id is the only handle
         * on them. An id nothing mounted is an answer, like an absent member.
         */
        g_component_id = (162 << 16) | 31;
        g_component_box[0] = 437;
        g_component_box[1] = 480;
        g_component_box[2] = 79;
        g_component_box[3] = 23;

        x = y = w = h = -1;
        CHECK(
            api->component_rect(ctx, (162 << 16) | 31, &x, &y, &w, &h),
            "a mounted component answers by id");
        CHECK(x == 437 && y == 480 && w == 79 && h == 23, "with its own box");

        x = y = w = h = -1;
        CHECK(
            !api->component_rect(ctx, (162 << 16) | 33, &x, &y, &w, &h),
            "an id this tree does not carry is 0");
        CHECK(x == -1 && y == -1, "and leaves the outputs untouched");

        g_component_id = -1;
        g_member_slot = -1;
        g_member_no = -1;
        g_slot_w[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS] = 0;
        g_slot_h[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS] = 0;
    }

    /*
     * Semantic roles: the same four verbs, addressed by what the element is.
     *
     * The negative half is the point. A role no profile declared has to answer
     * "not here" through every verb, because that is the state every lane is
     * in until someone writes its binding -- and a plugin that got a plausible
     * answer instead would be drawing over, or pressing, whatever happened to
     * be there.
     */
    {
        struct ToriRS_PluginApi const* api = PluginHost_Api(host);
        struct ToriRS_PluginCtx* ctx = PluginHost_Ctx(host, a);
        int x, y, w, h;

        g_role_name = NULL;
        x = y = w = h = -1;
        CHECK(
            !api->role_rect(ctx, "report_button", &x, &y, &w, &h),
            "an undeclared role has no rectangle");
        CHECK(x == -1 && y == -1, "and leaves the outputs untouched");
        CHECK(!api->role_visible(ctx, "report_button"), "an undeclared role is not visible");
        CHECK(!api->role_click(ctx, "report_button", 0), "an undeclared role cannot be pressed");
        CHECK(api->role_id(ctx, "report_button") == -1, "an undeclared role has no id");

        /* An empty name is a plugin's own string handling and gets the same
         * answer rather than reaching the engine. */
        CHECK(!api->role_rect(ctx, "", &x, &y, &w, &h), "an empty role name is not a role");
        CHECK(api->role_id(ctx, "") == -1, "an empty role name has no id");

        g_role_name = "report_button";
        g_role_box[0] = 408;
        g_role_box[1] = 467;
        g_role_box[2] = 100;
        g_role_box[3] = 32;
        g_role_visible = 1;
        g_role_component_id = (553 << 16) | 0;

        x = y = w = h = -1;
        CHECK(api->role_rect(ctx, "report_button", &x, &y, &w, &h), "a bound role answers");
        CHECK(x == 408 && y == 467 && w == 100 && h == 32, "with its element's box");
        CHECK(api->role_visible(ctx, "report_button"), "and reports it on screen");
        CHECK(api->role_id(ctx, "report_button") == ((553 << 16) | 0), "and hands back its id");

        /* A different name is still unbound, so one binding cannot answer for
         * the whole vocabulary. */
        CHECK(
            !api->role_rect(ctx, "logout_screen", &x, &y, &w, &h),
            "a role this profile did not bind is still absent");

        g_role_visible = 0;
        CHECK(!api->role_visible(ctx, "report_button"), "a hidden element is not visible");
        CHECK(
            api->role_rect(ctx, "report_button", &x, &y, &w, &h),
            "…but it still has a box, which is why the two are separate verbs");

        g_role_clicked = NULL;
        g_role_clicked_op = -1;
        CHECK(api->role_click(ctx, "report_button", 1), "a bound role presses");
        CHECK(
            g_role_clicked && strcmp(g_role_clicked, "report_button") == 0,
            "the press reached the element the role names");
        CHECK(g_role_clicked_op == 1, "carrying the op it was given");

        /* Same reading as if_click's: an op out of range came from a config
         * key, so it is refused rather than passed down. */
        g_role_clicked_op = -1;
        CHECK(!api->role_click(ctx, "report_button", 11), "an op past 10 is refused");
        CHECK(g_role_clicked_op == -1, "and never reaches the engine");

        /* Replacement is a standing owner-scoped declaration, not a role
         * lookup result. A temporarily absent target is still accepted and is
         * reconciled again when the tree may have rebuilt. */
        g_role_replace_calls = 0;
        CHECK(
            api->role_replace(ctx, "temporarily_absent", 1),
            "an absent role can be claimed persistently");
        CHECK(
            g_role_replace_calls == 1 && g_role_replace_enabled == 1,
            "the accepted claim is published even before it resolves");
        CHECK(
            api->role_replace(ctx, "temporarily_absent", 0),
            "the absent standing claim releases idempotently");

        g_role_replace_calls = 0;
        CHECK(
            api->role_replace(ctx, "report_button", 1),
            "the first plugin claims a semantic replacement");
        CHECK(
            g_role_replace_plugin == a && g_role_replace_enabled == 1,
            "the engine receives the claim with its owner");
        CHECK(
            !api->role_replace(PluginHost_Ctx(host, b), "report_button", 1),
            "a second plugin cannot replace the same role");
        CHECK(g_role_replace_calls == 1, "a refused competing claim never reaches the engine");

        PluginHost_ReconcileRoleReplacements(host);
        CHECK(
            g_role_replace_calls == 2 && g_role_replace_enabled == 1,
            "standing claims are restated at the pre-interaction fence");

        g_role_anchor_calls = 0;
        g_role_anchor_resets = 0;
        g_role_anchor_invalids = 0;
        g_role_anchor_last_replace = -1;
        g_alpha_anchor_second_saw_reset = 0;
        g_beta_anchor_attempt = 1;
        g_beta_anchor_retarget_invalid = 0;
        g_beta_draw_saw_invalid = 0;
        PluginHost_DrawCanvas(host, 765, 503);
        g_beta_anchor_attempt = 0;
        /*
         * The name is the object. A second plugin anchoring to a role the
         * first one replaced is anchoring to the replacement, and paints at
         * its tombstone with replace=1 -- it is not refused into an
         * active-invalid anchor, which is what this used to pin and what
         * made every replacement an island for the plugins that referenced
         * it by name.
         */
        CHECK(
            g_role_anchor_invalids == 0 && !g_beta_anchor_retarget_invalid,
            "a competing anchor on a replaced role is not refused");
        CHECK(!g_beta_draw_saw_invalid, "and the competitor's draws are attributed, not dropped");
        CHECK(
            g_role_anchor_calls == 2 && g_role_anchor_last_replace == 1,
            "both the owner and the competitor anchor at the tombstone");
        CHECK(
            g_alpha_anchor_second_saw_reset,
            "a later canvas subscriber cannot inherit the prior subscriber's anchor");
        CHECK(
            g_role_anchor_resets >= 4 && g_role_anchor_current_plugin < 0,
            "the host resets the anchor on both sides of each subscriber");

        CHECK(api->role_replace(ctx, "report_button", 0), "the owner can release its replacement");
        CHECK(g_role_replace_enabled == 0, "release is published to reveal the native subtree");
        CHECK(
            api->role_replace(PluginHost_Ctx(host, b), "report_button", 1),
            "another plugin may claim after release");
        g_role_replace_calls = 0;
        PluginHost_SetEnabled(host, b, false);
        CHECK(
            g_role_replace_calls == 1 && g_role_replace_plugin == b && g_role_replace_enabled == 0,
            "teardown automatically releases every replacement the plugin owns");
        PluginHost_SetEnabled(host, b, true);

        CHECK(
            !api->role_replace(ctx, "safe_gamechrome", 1) && !api->role_replace(ctx, "canvas", 1),
            "derived rectangles cannot be claimed as component replacements");

        /*
         * `safe` never reaches the engine at all: it is derived from the
         * regions and the host's own reservation table, so it has to answer
         * with exactly what the region enum answers.
         */
        {
            int sx, sy, sw, sh, rx, ry, rw, rh;
            int by_slot =
                api->slot_rect(ctx, TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME, &sx, &sy, &sw, &sh);
            int by_role = api->role_rect(ctx, "safe_gamechrome", &rx, &ry, &rw, &rh);
            CHECK(by_slot == by_role, "the safe role and the safe region agree that it exists");
            if( by_slot && by_role )
                CHECK(sx == rx && sy == ry && sw == rw && sh == rh, "and agree on the rectangle");
        }

        /* The v2 semantic ref is the name, not the current node. Legacy and
         * canonical spellings intern to the same stable token, and a base
         * rebuild updates only the snapshot behind it. */
        {
            struct ToriRS_UiNodeRef const legacy = PluginHost_UiRef(host, a, "report_button");
            struct ToriRS_UiNodeRef const canonical =
                PluginHost_UiRef(host, a, "frame.chat.button.report");
            struct ToriRS_UiNodeInfo info = { .struct_size = sizeof(info) };
            struct ToriRS_UiChange change;
            int changed = 0;

            g_member_slot = TORIRS_PLUGIN_SLOT_CHAT_BUTTONS;
            g_member_no = 3;
            g_role_visible = 1;
            CHECK(
                legacy.value != 0 && legacy.value == canonical.value,
                "legacy and canonical UI names share one stable reference");
            PluginHost_LayoutChanged(host);
            CHECK(
                PluginHost_UiInfo(host, canonical, &info) && info.visible && info.enabled &&
                    info.bounds.x == 408 && info.bounds.y == 467 && info.bounds.width == 56 &&
                    info.bounds.height == 19,
                "the canonical reference resolves the current member snapshot");
            while( PluginHost_UiChangeNext(host, &change) )
                changed += change.node.value == canonical.value;
            CHECK(changed == 1, "the changed-node journal reports that semantic node once");

            g_role_clicked = NULL;
            g_role_clicked_op = -1;
            CHECK(
                PluginHost_UiInvoke(host, canonical, "activate"),
                "a canonical named action invokes the lane binding");
            CHECK(
                g_role_clicked && strcmp(g_role_clicked, "report_button") == 0,
                "the compatibility action resolves through the legacy RevConfig role");
        }

        g_member_slot = -1;
        g_member_no = -1;
        g_role_name = NULL;
        g_role_visible = 0;
        g_role_component_id = -1;
    }

    /* Packet interception. */
    CHECK(PluginHost_PacketIn(host, 5, -1) == 0, "an unremarkable packet passes");
    CHECK(PluginHost_PacketIn(host, 99, -1) == 1, "setting drop reports the drop");

    /* Key consume. */
    CHECK(PluginHost_Key(host, 1, 0, true) == 1, "CONSUME on a key is reported");

    /* Menu: a plugin row is added, gets a client action id, and routes back to
     * its owner carrying the tag it was added with. */
    {
        struct ToriRS_PluginEvMenuBuild menu;
        struct ToriRS_PluginMenuRow row;
        int cursor = 0;

        memset(&menu, 0, sizeof(menu));
        g_engine.menu_rows = 0;
        PluginHost_MenuBuild(host, &cursor, &menu, false);
        CHECK(g_engine.menu_rows == 1, "menu_add reached the engine");
        CHECK(strcmp(g_engine.last_text, "Tag Goblin") == 0, "row text is passed through");
        CHECK(
            g_engine.last_action >= 500000,
            "a plugin row uses a client action id, so it can never be the "
            "left-click default");
        CHECK(PluginHost_OwnsMenuAction(host, g_engine.last_action), "the route is recorded");

        memset(&row, 0, sizeof(row));
        row.action = g_engine.last_action;
        g_select_calls = 0;
        CHECK(
            PluginHost_MenuSelect(host, &row, 0, 0) == 1,
            "selecting a plugin row suppresses the engine dispatch");
        CHECK(g_select_calls == 1, "the owning plugin was told");
        CHECK(g_last_tag == 7u, "the tag survives the round trip");

        /* A native row nobody consumed must fall through to the engine. */
        memset(&row, 0, sizeof(row));
        row.action = 25;
        CHECK(PluginHost_MenuSelect(host, &row, 0, 0) == 0, "a native row is not suppressed");
    }

    /* Draw budget: the plugin asks for far more than it may have, and the host
     * has to stop rather than flood the shared overlay pool. */
    {
        g_engine.draw_items = 0;
        PluginHost_FrameStart(host, 1, 0);
        PluginHost_DrawWorld(host);
        CHECK(g_engine.draw_items > 0, "draw calls reach the engine");
        CHECK(
            g_hull_shape == TORIRS_PLUGIN_HULL_MESH,
            "the hull shape a plugin asked for reaches the engine");
        CHECK(
            g_engine.draw_items <= TORIRS_PLUGIN_DRAW_BUDGET + 8,
            "the per-frame draw budget is enforced");

        /* And the budget resets, or a plugin would get one frame of drawing
         * per session. */
        int const first = g_engine.draw_items;
        PluginHost_FrameStart(host, 2, 0);
        PluginHost_DrawWorld(host);
        CHECK(g_engine.draw_items > first, "the budget resets each frame");
    }

    /* Config: defaults, typed reads, and an ini round-trip that keeps what was
     * changed and omits what was not. */
    {
        void* data = NULL;
        int size = 0;

        CHECK(
            strcmp(PluginHost_ConfigGet(host, a, "colour"), "#00FF00") == 0,
            "defaults seed the store");
        CHECK(PluginHost_ConfigCount(host, a) == 4, "schema count includes hidden keys");

        PluginHost_ConfigSet(host, a, "level", "9");
        CHECK(PluginHost_ConfigDirty(host), "a change marks the store dirty");

        CHECK(PluginHost_ConfigEncode(host, &data, &size) == 1, "encode succeeds");
        CHECK(strstr((char*)data, "level=9") != NULL, "a changed key is written");
        CHECK(
            strstr((char*)data, "colour=") == NULL,
            "a key still at its default is omitted, as RS_Prefs does");
        CHECK(
            strstr((char*)data, "[plugin:sleeper]") == NULL,
            "a default-off plugin left off writes nothing");

        /* Round-trip into a fresh host. */
        {
            struct ToriRS_PluginHost* host2 = PluginHost_New(&engine);
            PluginHost_Register(host2, &ALPHA);
            PluginHost_ConfigDecode(host2, data, size);
            CHECK(
                strcmp(PluginHost_ConfigGet(host2, 0, "level"), "9") == 0,
                "the changed value survives a decode");
            CHECK(
                strcmp(PluginHost_ConfigGet(host2, 0, "colour"), "#00FF00") == 0,
                "an omitted key comes back as its default");
            PluginHost_Free(host2);
        }
        free(data);
    }

    /*
     * Typed reads. The store is text, so what cfg_int and cfg_color make of
     * that text IS the setting -- and the spellings a hand-edited
     * plugin_prefs.ini carries are revconfig's, not atoi's.
     */
    {
        struct ToriRS_PluginCtx* ctx = PluginHost_Ctx(host, a);

        PluginHost_ConfigSet(host, a, "colour", "#FF8000");
        CHECK(g_api->cfg_color(ctx, "colour") == 0xFF8000u, "#RRGGBB");
        PluginHost_ConfigSet(host, a, "colour", "rgb(255, 0, 0)");
        CHECK(g_api->cfg_color(ctx, "colour") == 0xFF0000u, "rgb()");
        PluginHost_ConfigSet(host, a, "colour", "0x0000FF");
        CHECK(g_api->cfg_color(ctx, "colour") == 0x0000FFu, "0x hex");
        PluginHost_ConfigSet(host, a, "colour", "65280");
        CHECK(g_api->cfg_color(ctx, "colour") == 0x00FF00u, "a bare run is decimal");
        PluginHost_ConfigSet(host, a, "colour", "rgba(255, 0, 0, 128)");
        CHECK(
            g_api->cfg_color(ctx, "colour") == 0xFF0000u,
            "rgba() parses; cfg_color drops the alpha byte its contract has no room for");
        PluginHost_ConfigSet(host, a, "colour", "cornflower");
        CHECK(g_api->cfg_color(ctx, "colour") == 0u, "a value that is not a number reads as 0");

        PluginHost_ConfigSet(host, a, "level", "0x10");
        CHECK(g_api->cfg_int(ctx, "level") == 16, "an int key is the same grammar");
        PluginHost_ConfigSet(host, a, "level", "1 << 4");
        CHECK(g_api->cfg_int(ctx, "level") == 16, "arithmetic");
        PluginHost_ConfigSet(host, a, "level", "hsl16(0, 7, 64)");
        CHECK(g_api->cfg_int(ctx, "level") == ((7 << 7) | 64), "hsl16() packs a palette index");
        PluginHost_ConfigSet(host, a, "level", "-1");
        CHECK(g_api->cfg_int(ctx, "level") == -1, "a negative value survives");
        PluginHost_ConfigSet(host, a, "level", "9 apples");
        CHECK(g_api->cfg_int(ctx, "level") == 0, "a number with text after it is a typo, not a 9");
        PluginHost_ConfigSet(host, a, "level", "9");
    }

    /* Enable state is saved state. */
    {
        void* data = NULL;
        int size = 0;
        PluginHost_SetEnabled(host, b, false);
        CHECK(PluginHost_ConfigEncode(host, &data, &size) == 1, "encode succeeds");
        CHECK(
            strstr((char*)data, "enabled=0") != NULL,
            "switching a default-on plugin off is persisted");
        free(data);
    }

    /*
     * Ground items, assets and world objects.
     *
     * Run on a host of their own so the object and asset bookkeeping is
     * measured against an empty engine rather than against whatever the
     * earlier cases left behind.
     */
    {
        struct ToriRS_PluginHost* host3 = PluginHost_New(&engine);
        int const g = PluginHost_Register(host3, &GAMMA);
        struct ToriRS_PluginCtx* ctx;

        memset(&g_engine, 0, sizeof(g_engine));
        g_gamma_assets = 0;
        PluginHost_Start(host3);
        ctx = PluginHost_Ctx(host3, g);

        CHECK(g_gamma_object_count == 3, "object_create hands out handles");
        CHECK(g_engine.objects_live == 3, "and they reach the engine");
        CHECK(g_engine.objects[g_gamma_objects[0]].model_id == 43330, "the model is forwarded");
        CHECK(g_engine.objects[g_gamma_objects[0]].seq_id == 9260, "so is the sequence");
        CHECK(g_engine.objects[g_gamma_objects[0]].recolors == 1, "so is the recolour pair");
        CHECK(g_api->object_ready(ctx, g_gamma_objects[0]) == 1, "object_ready reports the engine");

        /* Authored geometry. */
        CHECK(g_gamma_mesh >= 0, "mesh_create hands out a handle");
        CHECK(g_engine.meshes_live == 1, "and it reaches the engine");
        CHECK(g_engine.mesh_vertices == 3, "every vertex is forwarded");
        CHECK(g_engine.mesh_faces == 1, "and so is the face");
        {
            /* The budget is a runtime fact, not a contract violation: past it
             * mesh_create refuses rather than aborting, exactly as
             * object_create does. */
            int taken = 0;
            for( int i = 0; i < TORIRS_PLUGIN_MESH_BUDGET + 4; i++ )
            {
                if( g_api->mesh_create(ctx) >= 0 )
                    taken++;
            }
            CHECK(
                taken == TORIRS_PLUGIN_MESH_BUDGET - 1,
                "mesh_create refuses past the plugin's budget");
        }

        /* Ground items reach a plugin. */
        {
            struct ToriRS_PluginObjSnap snap;
            int const iter = g_api->obj_next(ctx, -1, &snap);
            CHECK(iter >= 0, "obj_next yields the stack");
            CHECK(snap.obj_id == 4151 && snap.cost == 120000, "with its id and its cost");
            CHECK(g_api->obj_next(ctx, iter, &snap) == -1, "and then ends");
        }

        /* An asset name that is a path never reaches the engine. */
        CHECK(
            g_api->asset_load(ctx, "../../plugin_prefs.ini") == 0, "a path asset name is refused");
        CHECK(g_api->asset_load(ctx, "sub/dir.txt") == 0, "so is a separator");
        CHECK(g_engine.asset_reads == 0, "and neither reaches the engine");

        /* The ordinary read: queued once, delivered once, readable after. */
        CHECK(
            g_api->asset_load(ctx, "prices.txt") == 0, "a first load reports 'queued', not 'here'");
        CHECK(g_engine.asset_reads == 1, "and queues exactly one read");
        CHECK(
            g_api->asset_load(ctx, "prices.txt") == 0 && g_engine.asset_reads == 1,
            "a second load of an in-flight name joins the first rather than queuing again");
        {
            char* bytes = malloc(8);
            memcpy(bytes, "4151=99", 8);
            PluginHost_AssetDeliver(host3, "gamma", "prices.txt", bytes, 7);
        }
        CHECK(g_gamma_assets == 1, "the delivery raises EV_ASSET");
        CHECK(g_gamma_asset_ok == 1, "and reports success");
        {
            int size = 0;
            void const* data = g_api->asset_data(ctx, "prices.txt", &size);
            CHECK(data && size == 7, "asset_data answers with the bytes");
            CHECK(data && memcmp(data, "4151=99", 7) == 0, "and they are the delivered ones");
        }
        CHECK(g_api->asset_load(ctx, "prices.txt") == 1, "a resident asset loads synchronously");
        CHECK(g_engine.asset_reads == 1, "and queues nothing");

        /* A failed read still reaches the plugin, or it would wait forever. */
        g_api->asset_load(ctx, "missing.txt");
        PluginHost_AssetDeliver(host3, "gamma", "missing.txt", NULL, 0);
        CHECK(g_gamma_assets == 2, "a failed read still raises EV_ASSET");
        CHECK(g_gamma_asset_ok == 0, "and says it failed");
        CHECK(g_api->asset_data(ctx, "missing.txt", NULL) == NULL, "and leaves nothing resident");

        /*
         * A shipped model is a file: the handle is live before the bytes are,
         * and the arrival is what publishes it.
         */
        {
            int const shipped = g_api->model_load(ctx, "beam.model");
            CHECK(shipped >= 0, "model_load hands out a handle");
            CHECK(
                g_api->model_load(ctx, "beam.model") == shipped,
                "and a second load of the same file is the same handle, not a second copy");
            CHECK(g_engine.model_publishes == 0, "nothing is published before the bytes land");
            {
                char* bytes = malloc(16);
                memset(bytes, 0x7f, 16);
                PluginHost_AssetDeliver(host3, "gamma", "beam.model", bytes, 16);
            }
            CHECK(g_engine.model_publishes == 1, "the delivery publishes it");
            CHECK(g_engine.last_model_size == 16, "with the asset's own bytes");
        }
        CHECK(
            g_api->model_load(ctx, "../beam.model") == -1,
            "a path model name is refused, like every other asset name");

        /* Save replaces the resident copy before the write is queued. */
        CHECK(g_api->asset_save(ctx, "prices.txt", "4151=1", 6) == 1, "asset_save is accepted");
        CHECK(g_engine.asset_writes == 1, "and queues a write");
        CHECK(g_engine.last_written_size == 6, "with the bytes it was given");
        {
            int size = 0;
            void const* data = g_api->asset_data(ctx, "prices.txt", &size);
            CHECK(
                data && size == 6 && memcmp(data, "4151=1", 6) == 0,
                "and the resident copy is the new one immediately, not after the IO");
        }

        /*
         * Composed images: the plugin rasterises, the host publishes, and the
         * pixels come back out through the same handle.
         *
         * The round trip is the point. A plugin that composes a picture out of
         * the art it ships does both halves -- read the icon, write the
         * composite -- and a read that answered plausible-but-wrong pixels
         * would draw a picture nobody could tell was wrong.
         */
        {
            uint32_t src[4] = { 0xFF102030u, 0xFF405060u, 0x80708090u, 0x00000000u };
            uint32_t back[4] = { 0 };
            int image;

            memset(&g_composed, 0, sizeof(g_composed));
            g_composed.slot = -1;

            CHECK(
                g_api->image_compose(ctx, "sub/dir.png", 2, 2, src) == -1,
                "a composed image's name goes through the same gate a file's does");
            CHECK(
                g_api->image_compose(ctx, "orb.png", 0, 2, src) == -1,
                "and a size that is not a picture is refused");

            image = g_api->image_compose(ctx, "orb.png", 2, 2, src);
            CHECK(image >= 0, "an ordinary compose hands out a handle");
            CHECK(g_composed.w == 2 && g_composed.h == 2, "the geometry reaches the engine");
            {
                int w = 0;
                int h = 0;
                CHECK(
                    g_api->image_size(ctx, image, &w, &h) == 1 && w == 2 && h == 2,
                    "and the image is resident at once -- there is no read to wait for");
            }
            CHECK(
                g_api->image_compose(ctx, "orb.png", 2, 2, src) == image,
                "composing the same name again replaces it in place");

            CHECK(
                g_api->image_pixels(ctx, image, back, 3) == 0,
                "a buffer too small for the whole image copies nothing");
            CHECK(
                g_api->image_pixels(ctx, image, back, 4) == 4,
                "a big enough one copies every pixel");
            CHECK(
                memcmp(back, src, sizeof(src)) == 0,
                "and they are the pixels that went in, alpha included");
            CHECK(
                g_api->image_pixels(ctx, image + 40, back, 4) == 0,
                "a handle this plugin does not own reads nothing");

            g_api->image_release(ctx, image);
        }

        /* stat_xp answers the three numbers a progress meter needs at once. */
        {
            int xp = -1;
            int level_xp = -1;
            int next_xp = -1;

            CHECK(
                g_api->stat_xp(ctx, 3, &xp, &level_xp, &next_xp) == 1,
                "stat_xp answers for a skill in range");
            CHECK(
                xp == 1154 && level_xp == 1154 && next_xp == 1358,
                "with the xp and both thresholds");
            xp = -1;
            CHECK(
                g_api->stat_xp(ctx, 99, &xp, NULL, NULL) == 0 && xp == -1,
                "and refuses one out of range without touching the outs");
        }

        /*
         * Screenshots.
         *
         * The name goes through the same gate an asset name does, because it
         * is the same kind of thing -- a filename the plugin chose. The
         * DESTINATION does not, deliberately: it is a path the user typed into
         * a config field, so separators are the point of it. What both refuse
         * is `..`, which is the only thing standing between a config field and
         * the rest of the disk.
         */
        {
            char shot_path[TORIRS_PLUGIN_SCREENSHOT_PATH_MAX];

            g_engine.screenshots = 0;
            CHECK(
                g_api->screenshot(ctx, NULL, "levelup.png", shot_path, (int)sizeof(shot_path)) == 1,
                "a bare filename with no destination is accepted");
            CHECK(g_engine.screenshots == 1, "and reaches the engine");
            CHECK(g_engine.last_shot_dir[0] == '\0', "with no destination of its own");
            CHECK(
                strcmp(shot_path, "levelup.png") == 0,
                "and the engine's answer for where it lands comes back");

            CHECK(
                g_api->screenshot(
                    ctx, "shots/levels", "levelup.png", shot_path, (int)sizeof(shot_path)) == 1,
                "a destination with a separator is accepted");
            CHECK(
                strcmp(g_engine.last_shot_dir, "shots/levels") == 0, "and is forwarded unchanged");
            CHECK(
                strcmp(shot_path, "shots/levels/levelup.png") == 0,
                "and the path it answers with carries it");

            /*
             * A refusal empties the path. It is the string a plugin puts in
             * front of the player -- "saved to X" -- so a stale one left over
             * from the last capture would name a file that was never written.
             */
            CHECK(
                g_api->screenshot(
                    ctx, "shots/../../etc", "levelup.png", shot_path, (int)sizeof(shot_path)) == 0,
                "a destination that climbs out is refused");
            CHECK(shot_path[0] == '\0', "and leaves no path behind it");
            CHECK(
                g_api->screenshot(ctx, NULL, "../levelup.png", shot_path, (int)sizeof(shot_path)) ==
                    0,
                "and so is a name that does");
            CHECK(shot_path[0] == '\0', "with no path either");
            CHECK(g_engine.screenshots == 2, "neither reaches the engine");
        }

        /* Chat, and the moments the client recognises in it. Both are plain
         * forwarding here -- the recognising happens in the client, and is
         * tested against real message text in test-game-events. */
        {
            g_gamma_chats = 0;
            g_gamma_game_events = 0;

            PluginHost_ChatMessage(host3, 0, NULL, "Your Zulrah kill count is: 122.");
            CHECK(g_gamma_chats == 1, "a chat line reaches its subscriber");
            CHECK(
                strcmp(g_gamma_chat_text, "Your Zulrah kill count is: 122.") == 0, "with its text");

            PluginHost_GameEvent(
                host3, "boss_kill", "Zulrah", 122, "Your Zulrah kill count is: 122.");
            CHECK(g_gamma_game_events == 1, "a game event reaches its subscriber");
            CHECK(strcmp(g_gamma_event_kind, "boss_kill") == 0, "naming the kind");
            CHECK(strcmp(g_gamma_event_subject, "Zulrah") == 0, "and the subject");
            CHECK(g_gamma_event_value == 122, "and the value");

            /* A sender-less system line and a subject-less moment both have to
             * arrive as empty strings rather than as NULL: a plugin reading
             * ev.sender must never have to test for one. */
            PluginHost_GameEvent(host3, "pet", NULL, -1, NULL);
            CHECK(g_gamma_event_subject[0] == '\0', "an unnamed subject reads as empty");
            CHECK(g_gamma_event_value == -1, "and a valueless moment as -1");
        }

        /* Stopping the plugin takes its geometry and its bytes with it. */
        PluginHost_SetEnabled(host3, g, false);
        CHECK(g_engine.objects_live == 0, "a stopped plugin's world objects are destroyed");
        CHECK(g_engine.meshes_live == 0, "and so are its meshes");
        CHECK(g_engine.model_releases == 1, "and its shipped models are released");
        {
            struct ToriRS_PluginHost* host4 = PluginHost_New(&engine);
            PluginHost_Register(host4, &GAMMA);
            memset(&g_engine, 0, sizeof(g_engine));
            g_gamma_assets = 0;
            PluginHost_Start(host4);
            /* Nobody by this name asked for it on THIS host, so the delivery
             * must not raise an event for a name that was never requested. */
            PluginHost_AssetDeliver(host4, "gamma", "never-asked.txt", NULL, 0);
            CHECK(g_gamma_assets == 0, "a delivery nobody asked for raises nothing");
            PluginHost_Free(host4);
            CHECK(g_engine.objects_live == 0, "and freeing a host destroys its objects too");
            CHECK(g_engine.meshes_live == 0, "meshes included");
        }

        PluginHost_Free(host3);
    }

    /* ---- the plugin window ------------------------------------------------ */
    {
        struct ToriRS_PluginHost* hw = PluginHost_New(&engine);
        int const w = PluginHost_Register(hw, &WINNER);
        int rev_after_build;

        g_win_builds = 0;
        g_win_events = 0;
        PluginHost_Start(hw);

        CHECK(!PluginHost_WinHasTab(hw, w), "a plugin has no tab until it asks");
        CHECK(PluginHost_WinWidgetCount(hw, w) == 0, "and no controls");

        PluginHost_WinBuild(hw, w);
        CHECK(g_win_builds == 1, "an empty tab is built once");
        CHECK(PluginHost_WinHasTab(hw, w), "the tab is claimed");
        CHECK(strcmp(PluginHost_WinTabTitle(hw, w), "Beams") == 0, "with the title it asked for");
        CHECK(PluginHost_WinWidgetCount(hw, w) == 4, "every declared control is registered");
        rev_after_build = PluginHost_WinRevision(hw);

        /* Values the plugin set during the build are held by the host, so a
         * presentation opening later shows them without asking the plugin. */
        {
            struct ToriRS_PluginWinWidget const* c = PluginHost_WinWidgetAt(hw, w, 0);
            struct ToriRS_PluginWinWidget const* t = PluginHost_WinWidgetAt(hw, w, 1);
            struct ToriRS_PluginWinWidget const* d = PluginHost_WinWidgetAt(hw, w, 2);
            CHECK(c && strcmp(c->id, "enabled") == 0, "controls keep declaration order");
            CHECK(c && c->checked == 1, "a checkbox holds the state the plugin set");
            CHECK(t && strcmp(t->text, "#FFCC00") == 0, "a field holds its text");
            CHECK(d && strcmp(d->choices, "beam|ring|off") == 0, "a dropdown holds its list");
        }

        /* Building again is a no-op: a non-empty tab must not be reset by
         * whatever else happens to open the window. */
        PluginHost_WinBuild(hw, w);
        CHECK(g_win_builds == 1, "a tab that already has controls is not rebuilt");
        CHECK(PluginHost_WinWidgetCount(hw, w) == 4, "and its controls are not duplicated");
        CHECK(PluginHost_WinRevision(hw) == rev_after_build, "nothing shape-like changed");

        /* Using a control reaches the plugin, and the host's copy is updated
         * FIRST so a handler reading its own control back sees the new value. */
        PluginHost_WinDispatch(hw, w, "enabled", TORIRS_PLUGIN_UI_TOGGLE, 0, NULL);
        CHECK(g_win_events == 1, "a control's use reaches the plugin");
        CHECK(strcmp(g_win_last_id, "enabled") == 0, "naming the control");
        CHECK(g_win_last_action == TORIRS_PLUGIN_UI_TOGGLE, "and the action");
        CHECK(PluginHost_WinWidgetAt(hw, w, 0)->checked == 0, "the host's copy is updated");

        PluginHost_WinDispatch(hw, w, "colour", TORIRS_PLUGIN_UI_TEXT, -1, "#00FF00");
        CHECK(strcmp(g_win_last_text, "#00FF00") == 0, "an edit carries its new text");
        CHECK(
            strcmp(PluginHost_WinWidgetAt(hw, w, 1)->text, "#00FF00") == 0,
            "and the host holds it");

        /* A control nobody declared is refused rather than dispatched: a stale
         * presentation must not be able to raise events for controls that are
         * gone. */
        CHECK(
            PluginHost_WinDispatch(hw, w, "ghost", TORIRS_PLUGIN_UI_ACTIVATE, -1, NULL) == 0,
            "an unknown control dispatches nothing");

        /* Disabling takes the tab with it -- controls left in the window would
         * dispatch to a plugin that is not running. */
        PluginHost_SetEnabled(hw, w, false);
        CHECK(!PluginHost_WinHasTab(hw, w), "a disabled plugin loses its tab");
        CHECK(PluginHost_WinWidgetCount(hw, w) == 0, "and its controls");
        CHECK(PluginHost_WinRevision(hw) != rev_after_build, "which is a shape change");

        /* Re-enabling gives it back, through the same one declaration site. */
        PluginHost_SetEnabled(hw, w, true);
        PluginHost_WinBuild(hw, w);
        CHECK(g_win_builds == 2, "a re-enabled plugin is asked to rebuild");
        CHECK(PluginHost_WinWidgetCount(hw, w) == 4, "and gets its controls back");

        PluginHost_Free(hw);
    }

    /* ---- one shared application plugin panel ----------------------------- */
    {
        struct ToriRS_PluginHost* hp = PluginHost_New(&engine);
        struct ToriRS_PluginPanelDesc outside = { "too-late.png", 320 };
        struct ToriRS_PluginWinWidget const* widget;
        uint32_t gen_a;
        uint32_t gen_b;
        uint32_t serial_a;
        uint32_t serial_b;
        uint32_t serial_b_rebuilt;
        uint32_t registry_before;
        uint32_t model_before;
        uint32_t icon_revision;
        uint32_t icon_pixels[64 * 64];
        int icon_w = 0;
        int icon_h = 0;
        int builds_before;

        g_panel_a_index = PluginHost_Register(hp, &PANEL_ALPHA);
        g_panel_b_index = PluginHost_Register(hp, &PANEL_BETA);
        g_panel_a_builds = 0;
        g_panel_b_builds = 0;
        g_panel_a_actions = 0;
        g_panel_b_actions = 0;
        g_panel_a_layouts = 0;
        g_panel_b_layouts = 0;
        g_panel_a_hides = 0;
        g_panel_b_hides = 0;
        g_panel_a_draws = 0;
        g_panel_b_draws = 0;
        g_panel_draw_surface_mode = -1;
        g_panel_last_generation = 0;
        g_panel_last_sequence = 0;
        g_engine.draw_items = 0;

        registry_before = PluginHost_PanelRegistryRevision(hp);
        PluginHost_Start(hp);
        CHECK(
            PluginHost_PanelRegistryRevision(hp) != registry_before,
            "EV_START registrations change the inert rail revision");
        CHECK(
            PluginHost_PanelHasPage(hp, g_panel_a_index) &&
                PluginHost_PanelHasPage(hp, g_panel_b_index),
            "both plugins can register entries in the one rail");
        CHECK(
            strcmp(PluginHost_PanelIconAsset(hp, g_panel_a_index), "alpha.png") == 0,
            "registration metadata is copied into the host");
        CHECK(
            strcmp(PluginHost_PanelTitle(hp, g_panel_a_index), "Panel Alpha") == 0,
            "and the rail entry is named by the PLUGIN, which its page cannot "
            "rename: the registration carries no title of its own");
        CHECK(
            strcmp(g_engine.last_asset_plugin, "panel-alpha") == 0 &&
                strcmp(g_engine.last_asset_name, "alpha.png") == 0,
            "panel registration automatically loads its icon through the plugin sandbox");
        icon_revision = PluginHost_PanelIconRevision(hp, g_panel_a_index);
        CHECK(
            icon_revision != 0 &&
                PluginHost_PanelIconPixels(
                    hp, g_panel_a_index, icon_pixels, 64 * 64, &icon_w, &icon_h) == 0,
            "a pending icon has a revision and resolves to presenter fallback");
        {
            unsigned char* image = malloc(10);
            uint32_t const color = 0xFFA1B2C3u;
            memcpy(image, "ICON", 4);
            image[4] = 2;
            image[5] = 2;
            memcpy(image + 6, &color, sizeof(color));
            PluginHost_AssetDeliver(hp, "panel-alpha", "alpha.png", image, 10);
            CHECK(
                PluginHost_PanelIconRevision(hp, g_panel_a_index) != icon_revision &&
                    PluginHost_PanelIconPixels(
                        hp, g_panel_a_index, icon_pixels, 64 * 64, &icon_w, &icon_h) == 4 &&
                    icon_w == 2 && icon_h == 2 && icon_pixels[0] == color,
                "a late authored icon publishes bounded ARGB pixels under a new revision");
            icon_revision = PluginHost_PanelIconRevision(hp, g_panel_a_index);
        }
        {
            int const huge_size = 256 * 1024 + 1;
            unsigned char* image = calloc((size_t)huge_size, 1);
            memcpy(image, "ICON", 4);
            image[4] = 2;
            image[5] = 2;
            PluginHost_AssetDeliver(hp, "panel-alpha", "alpha.png", image, huge_size);
            CHECK(
                PluginHost_PanelIconRevision(hp, g_panel_a_index) != icon_revision &&
                    PluginHost_PanelIconPixels(
                        hp, g_panel_a_index, icon_pixels, 64 * 64, &icon_w, &icon_h) == 0,
                "an over-budget icon source is rejected before automatic decode");
            icon_revision = PluginHost_PanelIconRevision(hp, g_panel_a_index);
        }
        {
            unsigned char* image = malloc(10);
            uint32_t const color = 0xFFFFFFFFu;
            memcpy(image, "ICON", 4);
            image[4] = 65;
            image[5] = 1;
            memcpy(image + 6, &color, sizeof(color));
            PluginHost_AssetDeliver(hp, "panel-alpha", "alpha.png", image, 10);
            CHECK(
                PluginHost_PanelIconRevision(hp, g_panel_a_index) != icon_revision &&
                    PluginHost_PanelIconPixels(
                        hp, g_panel_a_index, icon_pixels, 64 * 64, &icon_w, &icon_h) == 0 &&
                    icon_w == 0 && icon_h == 0,
                "an oversized authored icon is rejected for baked-wrench fallback");
            icon_revision = PluginHost_PanelIconRevision(hp, g_panel_a_index);
        }
        {
            char* bad = malloc(4);
            memcpy(bad, "FAIL", 4);
            PluginHost_AssetDeliver(hp, "panel-alpha", "alpha.png", bad, 4);
            CHECK(
                PluginHost_PanelIconRevision(hp, g_panel_a_index) != icon_revision &&
                    PluginHost_PanelIconPixels(
                        hp, g_panel_a_index, icon_pixels, 64 * 64, &icon_w, &icon_h) == 0,
                "a malformed authored icon reaches the same baked fallback");
        }
        CHECK(
            PluginHost_PanelPreferredWidth(hp, g_panel_a_index) == TORIRS_PLUGIN_PANEL_WIDTH_MIN &&
                PluginHost_PanelPreferredWidth(hp, g_panel_b_index) ==
                    TORIRS_PLUGIN_PANEL_WIDTH_MAX,
            "preferred width hints are clamped identically for every presenter");
        CHECK(
            PluginHost_PanelWantsAttention(hp, g_panel_b_index),
            "attention is retained without opening the page");
        CHECK(
            PluginHost_PanelActive(hp) == -1 && g_panel_a_builds == 0 && g_panel_b_builds == 0,
            "registration neither selects nor builds any plugin");
        CHECK(
            !g_api->panel_request(PluginHost_Ctx(hp, g_panel_a_index), &outside),
            "panel_request is refused outside EV_START");

        CHECK(PluginHost_PanelSelect(hp, g_panel_a_index), "the first rail entry selects");
        gen_a = PluginHost_PanelSelectionGeneration(hp);
        CHECK(
            gen_a != 0 && PluginHost_PanelActive(hp) == g_panel_a_index,
            "selection publishes one active plugin and a nonzero generation");
        CHECK(
            g_panel_a_builds == 1 && g_panel_b_builds == 0 && g_panel_last_generation == gen_a,
            "only the selected plugin receives PANEL_BUILD");
        CHECK(
            PluginHost_PanelWidgetCount(hp, gen_a) == 2,
            "only the active page's semantic records are mounted");
        widget = PluginHost_PanelWidgetAt(hp, gen_a, 0);
        CHECK(
            widget && strcmp(widget->id, "shared") == 0 && widget->checked == 1 &&
                widget->value == 1,
            "panel widgets use the win-compatible semantic record and result state");
        serial_a = widget ? widget->serial : 0;
        CHECK(serial_a != 0, "a mounted semantic node has a nonzero serial");
        builds_before = g_panel_a_builds;
        model_before = PluginHost_PanelModelRevision(hp);
        CHECK(
            PluginHost_PanelEnsureBuilt(hp, gen_a) && PluginHost_PanelEnsureBuilt(hp, gen_a) &&
                g_panel_a_builds == builds_before,
            "a retained active model does not rerun PANEL_BUILD");
        CHECK(
            g_api->panel_set_value(PluginHost_Ctx(hp, g_panel_a_index), "shared", 1) &&
                PluginHost_PanelModelRevision(hp) == model_before,
            "a compare-equal node update does not dirty the model subtree");
        builds_before = g_panel_a_builds;
        CHECK(
            PluginHost_PanelSelect(hp, g_panel_a_index) && PluginHost_PanelActive(hp) == -1 &&
                PluginHost_PanelLastSelected(hp) == g_panel_a_index &&
                g_panel_a_builds == builds_before,
            "clicking the selected rail icon collapses but remembers it");
        gen_a = PluginHost_PanelSelectionGeneration(hp);
        CHECK(
            PluginHost_PanelWidgetCount(hp, gen_a) == 0 && !PluginHost_PanelEnsureBuilt(hp, gen_a),
            "a collapsed shell mounts and builds no page model");

        /* ---- the two faces --------------------------------------------- */
        {
            uint32_t gen_page;
            uint32_t gen_settings;

            CHECK(
                PluginHost_PanelSelectView(hp, g_panel_a_index, TORIRS_PLUGIN_PANEL_VIEW_PAGE) &&
                    PluginHost_PanelView(hp) == TORIRS_PLUGIN_PANEL_VIEW_PAGE &&
                    g_panel_last_view == TORIRS_PLUGIN_PANEL_VIEW_PAGE,
                "the rail entry mounts the PAGE face and the build is told so");
            gen_page = PluginHost_PanelSelectionGeneration(hp);
            CHECK(
                PluginHost_PanelWidgetCount(hp, gen_page) == 2,
                "which is the face carrying the plugin's own controls");

            /*
             * The OTHER face of the SAME plugin is a REPLACEMENT, not a
             * collapse: a page and its settings are two models and nothing may
             * survive between them, so it advances the generation exactly as
             * moving to another plugin does.
             */
            CHECK(
                PluginHost_PanelSelectView(
                    hp, g_panel_a_index, TORIRS_PLUGIN_PANEL_VIEW_SETTINGS) &&
                    PluginHost_PanelActive(hp) == g_panel_a_index,
                "asking for the other face of the mounted plugin does not collapse it");
            gen_settings = PluginHost_PanelSelectionGeneration(hp);
            CHECK(
                gen_settings != gen_page &&
                    PluginHost_PanelView(hp) == TORIRS_PLUGIN_PANEL_VIEW_SETTINGS,
                "it advances the selection generation and records the new face");
            CHECK(
                g_panel_last_view == TORIRS_PLUGIN_PANEL_VIEW_SETTINGS &&
                    PluginHost_PanelWidgetCount(hp, gen_settings) == 0,
                "and a plugin that declares nothing for it mounts an empty model");
            CHECK(
                PluginHost_PanelWidgetCount(hp, gen_page) == 0,
                "the page face's generation no longer names a mounted model");

            /* Reselecting the face already mounted is still the collapse the
             * rail's own stone performs. */
            CHECK(
                PluginHost_PanelSelectView(
                    hp, g_panel_a_index, TORIRS_PLUGIN_PANEL_VIEW_SETTINGS) &&
                    PluginHost_PanelActive(hp) == -1,
                "reselecting the mounted face collapses");
            gen_a = PluginHost_PanelSelectionGeneration(hp);
            /* Re-baselined: the face cases above ran builds of their own, and
             * the expand-again check below counts from here. */
            builds_before = g_panel_a_builds;
        }
        CHECK(
            !PluginHost_PanelLayout(
                hp, gen_a, 320, 500, 2000, TORIRS_PLUGIN_PANEL_MEDIUM, true, true),
            "a collapsed shell rejects page layout work");
        CHECK(
            !PluginHost_PanelDispatch(
                hp, gen_a, serial_a, 1, "shared", TORIRS_PLUGIN_UI_ACTIVATE, -1, NULL, 0, 0),
            "a collapsed shell rejects page input");
        CHECK(
            !PluginHost_PanelNeedsDraw(hp, gen_a, serial_a) &&
                !PluginHost_PanelDraw(hp, gen_a, serial_a, &g_engine, 0, 0, 200, 100),
            "a collapsed shell rejects page draw work");
        CHECK(
            PluginHost_PanelSelect(hp, PluginHost_PanelLastSelected(hp)) &&
                PluginHost_PanelActive(hp) == g_panel_a_index &&
                g_panel_a_builds == builds_before + 1,
            "selecting the remembered icon expands and rebuilds its page");
        gen_a = PluginHost_PanelSelectionGeneration(hp);
        widget = PluginHost_PanelWidgetAt(hp, gen_a, 0);
        serial_a = widget ? widget->serial : 0;

        CHECK(
            PluginHost_PanelLayout(
                hp, gen_a, 320, 500, 2000, TORIRS_PLUGIN_PANEL_MEDIUM, true, true),
            "the shell can publish neutral allocation facts");
        CHECK(
            g_panel_a_layouts == 1 && g_panel_b_layouts == 0,
            "layout reaches only the selected plugin");
        CHECK(
            PluginHost_PanelLayout(
                hp, gen_a, 320, 500, 2000, TORIRS_PLUGIN_PANEL_MEDIUM, true, true) &&
                g_panel_a_layouts == 1 && g_panel_b_layouts == 0,
            "an unchanged allocation dispatches no duplicate layout event");
        CHECK(
            !g_api->panel_set_text(
                PluginHost_Ctx(hp, g_panel_b_index), "shared", "hidden mutation"),
            "a nonselected plugin cannot mutate the mounted model");
        CHECK(
            PluginHost_PanelDispatch(
                hp, gen_a, serial_a, 1, "shared", TORIRS_PLUGIN_UI_TOGGLE, 0, NULL, 0, 0),
            "a current generation/serial/sequence intent is accepted");
        CHECK(
            g_panel_a_actions == 1 && g_panel_b_actions == 0 &&
                strcmp(g_panel_last_id, "shared") == 0 && g_panel_last_sequence == 1,
            "input reaches the selected owner and no other plugin");
        CHECK(
            PluginHost_PanelWidgetAt(hp, gen_a, 0)->checked == 0,
            "result state is committed before the action callback");
        CHECK(
            !PluginHost_PanelDispatch(
                hp, gen_a, serial_a, 1, "shared", TORIRS_PLUGIN_UI_TOGGLE, 1, NULL, 0, 0) &&
                g_panel_a_actions == 1,
            "a duplicate momentary intent sequence dispatches once");

        widget = PluginHost_PanelWidgetAt(hp, gen_a, 1);
        CHECK(
            widget && widget->preferred_height == TORIRS_PLUGIN_PANEL_CUSTOM_HEIGHT_DEFAULT &&
                PluginHost_PanelNeedsDraw(hp, gen_a, widget->serial),
            "a new custom region starts at the portable default height and dirty");
        CHECK(
            !g_api->panel_set_height(PluginHost_Ctx(hp, g_panel_a_index), "shared", 120),
            "height is accepted only by a custom semantic node");
        CHECK(
            g_api->panel_set_height(PluginHost_Ctx(hp, g_panel_a_index), "chart", 1) &&
                PluginHost_PanelWidgetAt(hp, gen_a, 1)->preferred_height ==
                    TORIRS_PLUGIN_PANEL_CUSTOM_HEIGHT_MIN,
            "custom preferred height clamps at the portable lower bound");
        CHECK(
            g_api->panel_set_height(PluginHost_Ctx(hp, g_panel_a_index), "chart", 9999) &&
                PluginHost_PanelWidgetAt(hp, gen_a, 1)->preferred_height ==
                    TORIRS_PLUGIN_PANEL_CUSTOM_HEIGHT_MAX,
            "custom preferred height clamps at the portable upper bound");
        CHECK(
            g_api->panel_set_height(PluginHost_Ctx(hp, g_panel_a_index), "chart", 0) &&
                PluginHost_PanelWidgetAt(hp, gen_a, 1)->preferred_height ==
                    TORIRS_PLUGIN_PANEL_CUSTOM_HEIGHT_DEFAULT,
            "zero restores the custom height default");
        model_before = PluginHost_PanelModelRevision(hp);
        CHECK(
            widget && PluginHost_PanelDraw(hp, gen_a, widget->serial, &g_engine, 0, 0, 200, 100),
            "a visible selected custom region is eligible to draw");
        CHECK(
            g_panel_a_draws == 1 && g_panel_b_draws == 0 &&
                g_panel_draw_surface_mode == TORIRS_PLUGIN_ENGINE_DRAW_PANEL &&
                g_engine.draw_canvas == 0,
            "the draw is scoped to the panel target and restores the game target");
        CHECK(
            widget && !PluginHost_PanelNeedsDraw(hp, gen_a, widget->serial),
            "a custom region is clean after its draw pass");
        CHECK(
            PluginHost_PanelModelRevision(hp) == model_before,
            "consuming visual invalidation does not dirty semantic model state");
        CHECK(
            widget && PluginHost_PanelInvalidate(hp, gen_a, widget->serial) &&
                PluginHost_PanelNeedsDraw(hp, gen_a, widget->serial) &&
                PluginHost_PanelModelRevision(hp) == model_before,
            "visual invalidation does not dirty semantic model state");
        CHECK(
            !PluginHost_PanelInvalidate(hp, gen_a, serial_a) &&
                !PluginHost_PanelInvalidate(hp, gen_a + 1, widget->serial),
            "layout invalidation rejects non-custom and stale identities");

        CHECK(PluginHost_PanelSelect(hp, g_panel_b_index), "a newer selection replaces it");
        gen_b = PluginHost_PanelSelectionGeneration(hp);
        CHECK(gen_b != gen_a, "replacing the active page advances its generation");
        CHECK(
            g_panel_a_hides == 1 && g_panel_b_builds == 1 &&
                PluginHost_PanelActive(hp) == g_panel_b_index,
            "the old page is hidden before only the new plugin is built");
        CHECK(
            !PluginHost_PanelWantsAttention(hp, g_panel_b_index),
            "selecting an attention request acknowledges it");
        CHECK(
            PluginHost_PanelWidgetCount(hp, gen_a) == 0 &&
                PluginHost_PanelWidgetAt(hp, gen_a, 0) == NULL,
            "a presenter reading an old generation cannot see the new model");
        widget = PluginHost_PanelWidgetAt(hp, gen_b, 0);
        serial_b = widget ? widget->serial : 0;
        CHECK(
            serial_b != 0 && serial_b != serial_a,
            "the same plugin-local id on the replacement page has a new serial");
        CHECK(
            !PluginHost_PanelDispatch(
                hp, gen_a, serial_a, 2, "shared", TORIRS_PLUGIN_UI_ACTIVATE, -1, NULL, 0, 0),
            "late input from the old selection generation is dropped");
        CHECK(
            PluginHost_PanelLayout(
                hp, gen_b, 300, 480, 1000, TORIRS_PLUGIN_PANEL_COMPACT, true, false),
            "the replacement page receives its own exclusive allocation");
        CHECK(
            PluginHost_PanelDispatch(
                hp, gen_b, serial_b, 1, "shared", TORIRS_PLUGIN_UI_ACTIVATE, -1, NULL, 0, 0) &&
                g_panel_b_actions == 1,
            "intent sequence restarts within the new selection generation");

        /* Re-declaration inside one selection is fenced by the node serial,
         * independently of the selection-generation fence above. */
        g_api->panel_clear(PluginHost_Ctx(hp, g_panel_b_index));
        CHECK(
            PluginHost_PanelWidgetCount(hp, gen_b) == 0,
            "panel_clear drops the active retained model");
        CHECK(
            !PluginHost_PanelEnsureBuilt(hp, gen_a),
            "an old generation cannot trigger a page build");
        CHECK(
            PluginHost_PanelEnsureBuilt(hp, gen_b) && g_panel_b_builds == 2,
            "the current selected plugin alone can rebuild a cleared page");
        widget = PluginHost_PanelWidgetAt(hp, gen_b, 0);
        serial_b_rebuilt = widget ? widget->serial : 0;
        CHECK(
            serial_b_rebuilt != 0 && serial_b_rebuilt != serial_b,
            "redeclaring an id assigns a fresh widget serial");
        CHECK(
            !PluginHost_PanelDispatch(
                hp, gen_b, serial_b, 2, "shared", TORIRS_PLUGIN_UI_ACTIVATE, -1, NULL, 0, 0),
            "a stale serial cannot address the redeclared id");
        CHECK(
            PluginHost_PanelDispatch(
                hp,
                gen_b,
                serial_b_rebuilt,
                2,
                "shared",
                TORIRS_PLUGIN_UI_ACTIVATE,
                -1,
                NULL,
                0,
                0) &&
                g_panel_b_actions == 2,
            "the redeclared node accepts the next sequenced result");

        CHECK(PluginHost_PanelClose(hp), "the one shared page can collapse");
        CHECK(
            PluginHost_PanelActive(hp) == -1 && g_panel_b_hides == 1 &&
                PluginHost_PanelWidgetCount(hp, PluginHost_PanelSelectionGeneration(hp)) == 0,
            "collapse hides the selected plugin and unmounts its model");
        CHECK(!PluginHost_PanelClose(hp), "closing an already collapsed shell is a no-op");

        /* Reload preserves the user's selection but builds from the new run's
         * EV_START registration; disabling removes it instead. */
        CHECK(PluginHost_PanelSelect(hp, g_panel_a_index), "alpha can be selected again");
        gen_a = PluginHost_PanelSelectionGeneration(hp);
        builds_before = g_panel_a_builds;
        PluginHost_Reload(hp, g_panel_a_index);
        CHECK(
            PluginHost_PanelActive(hp) == g_panel_a_index &&
                PluginHost_PanelSelectionGeneration(hp) != gen_a &&
                g_panel_a_builds == builds_before + 1,
            "reloading the selected plugin re-registers and rebuilds only its page");
        PluginHost_SetEnabled(hp, g_panel_a_index, false);
        CHECK(
            !PluginHost_PanelHasPage(hp, g_panel_a_index) && PluginHost_PanelActive(hp) == -1,
            "disabling the active plugin removes its rail entry and page");
        CHECK(
            PluginHost_PanelIconRevision(hp, g_panel_a_index) == 0 &&
                PluginHost_PanelIconPixels(
                    hp, g_panel_a_index, icon_pixels, 64 * 64, &icon_w, &icon_h) == 0,
            "plugin teardown exposes neither stale icon revision nor pixels");
        PluginHost_SetEnabled(hp, g_panel_b_index, false);
        CHECK(
            !PluginHost_PanelHasPage(hp, g_panel_b_index),
            "disabling an inactive plugin removes its inert registration too");

        PluginHost_Free(hp);
    }

    /* ---- reload ------------------------------------------------------------ */
    {
        struct ToriRS_PluginHost* hr = PluginHost_New(&engine);
        int const r = PluginHost_Register(hr, &RELOADER);

        g_reload_starts = 0;
        g_reload_stops = 0;
        g_reload_hook_calls = 0;
        PluginHost_Start(hr);
        CHECK(g_reload_starts == 1, "the plugin started once");
        CHECK(strcmp(g_reload_seen, "#000000") == 0, "reading its declared default");

        /* The case the whole thing exists for: write the key, reload, and the
         * plugin's on_start sees the NEW value. Without the reload it would
         * still be running on the one it cached at boot. */
        PluginHost_ConfigSet(hr, r, "colour", "#FFCC00");
        PluginHost_Reload(hr, r);
        CHECK(g_reload_stops == 1, "reload stops the plugin");
        CHECK(g_reload_hook_calls == 1, "and gives the adapter its rebuild hook");
        CHECK(g_reload_starts == 2, "and starts it again");
        CHECK(strcmp(g_reload_seen, "#FFCC00") == 0, "on_start sees the saved value");

        /* Saved values SURVIVE the reload -- a reload that reset the store to
         * defaults would make Save a button that undoes itself. */
        CHECK(
            strcmp(PluginHost_ConfigGet(hr, r, "colour"), "#FFCC00") == 0,
            "the saved value survives the reload");

        /* Everything the previous run held is released: subscriptions are
         * dropped and rebuilt rather than accumulated, so a plugin reloaded
         * ten times still handles each event once. */
        {
            int const before = g_reload_starts;
            for( int i = 0; i < 5; i++ )
                PluginHost_Reload(hr, r);
            CHECK(g_reload_starts == before + 5, "five reloads are five starts, not thirty-two");
            CHECK(g_reload_stops == before + 4, "each one stopped the run before it");
        }

        /* A disabled plugin is left alone: reloading it here would switch it
         * back on behind the user's back. */
        PluginHost_SetEnabled(hr, r, false);
        {
            int const starts = g_reload_starts;
            PluginHost_Reload(hr, r);
            CHECK(g_reload_starts == starts, "a disabled plugin is not reloaded");
            CHECK(!PluginHost_IsEnabled(hr, r), "and is not switched on by the attempt");
        }

        PluginHost_Free(hr);
    }

    /* ---- standing down ------------------------------------------------- */

    /*
     * A plugin that looks at the lane it booted on and refuses it.
     *
     * The load-bearing half is everything the refusal does NOT do: it does not
     * touch the switch the user saved, and it does not leave the plugin
     * running-but-inert where the roster would go on showing it as on.
     */
    {
        struct ToriRS_PluginHost* hs;
        int s;

        /* A lane it can run on, first, so the checks below are about the lane
         * and not about the plugin never having worked. */
        g_lane_game = TORIRS_PLUGIN_GAME_RS2;
        g_standoff_inits = 0;
        g_standoff_starts = 0;
        g_standoff_stops = 0;
        g_standoff_ticks = 0;

        hs = PluginHost_New(&engine);
        s = PluginHost_Register(hs, &STANDOFF);
        PluginHost_SetEnabled(hs, s, true);
        CHECK(PluginHost_IsEnabled(hs, s), "on a lane it accepts, the plugin runs");
        CHECK(g_standoff_starts == 1, "and its EV_START handler is reached");
        PluginHost_Free(hs);

        /* The same plugin, the same switch, a lane it stands down on. */
        g_lane_game = TORIRS_PLUGIN_GAME_OLDSCHOOL;
        g_standoff_inits = 0;
        g_standoff_starts = 0;
        g_standoff_stops = 0;
        g_standoff_ticks = 0;

        hs = PluginHost_New(&engine);
        s = PluginHost_Register(hs, &STANDOFF);
        PluginHost_SetEnabled(hs, s, true);

        CHECK(g_standoff_inits == 1, "the plugin is still asked -- the host decides nothing");
        CHECK(!PluginHost_IsEnabled(hs, s), "and having stood down, it reads as off");
        CHECK(g_standoff_starts == 0, "EV_START never reaches a plugin that refused");
        CHECK(
            PluginHost_Error(hs, s) != NULL &&
                strstr(PluginHost_Error(hs, s), "own gameframe") != NULL,
            "the reason is on the row, in the words the plugin wrote");

        PluginHost_LogicTick(hs, 1);
        CHECK(g_standoff_ticks == 0, "and nothing else reaches it either");

        /* Every later Start runs this list again -- a script finishing its
         * load is one -- and none of them may re-take a decision already made
         * or the log fills with the same line once a second. */
        PluginHost_Start(hs);
        CHECK(g_standoff_inits == 1, "a later Start does not put the question again");

        /*
         * And the switch is still the user's.
         *
         * This client boots several lanes from one settings file: `enabled=1`
         * was stated once for all of them, so an OldSchool boot that cleared
         * it would take the plugin away from the next lane too.
         */
        {
            void* data = NULL;
            int size = 0;
            CHECK(PluginHost_ConfigEncode(hs, &data, &size) == 1, "the settings encode");
            CHECK(
                strstr((char*)data, "[plugin:standoff]") != NULL &&
                    strstr((char*)data, "enabled=1") != NULL,
                "and still carry the enabled=1 the user saved");
            free(data);
        }

        /* Asked again, explicitly: the question goes back to the plugin, which
         * on this lane gives the same answer -- and says so again, beside the
         * switch that was just clicked. */
        PluginHost_SetError(hs, s, NULL);
        PluginHost_SetEnabled(hs, s, true);
        CHECK(g_standoff_inits == 2, "flipping the switch on takes the decision again");
        CHECK(!PluginHost_IsEnabled(hs, s), "and on the same lane it stands down again");
        CHECK(PluginHost_Error(hs, s) != NULL, "leaving the reason where the row draws it");

        /* A reload is a fresh run decided from a fresh source, so the refusal
         * does not outlive it -- here the source now says a lane it accepts. */
        g_lane_game = TORIRS_PLUGIN_GAME_RS2;
        PluginHost_Reload(hs, s);
        CHECK(PluginHost_IsEnabled(hs, s), "a reload takes the refusal back off");
        CHECK(g_standoff_starts == 1, "and the plugin runs");

        /* Switched off by hand and on again is an ordinary enable, with no
         * refusal left over to make the click do nothing. */
        PluginHost_SetEnabled(hs, s, false);
        CHECK(!PluginHost_IsEnabled(hs, s), "an ordinary disable still switches it off");
        PluginHost_SetEnabled(hs, s, true);
        CHECK(PluginHost_IsEnabled(hs, s), "and an ordinary enable switches it back on");

        PluginHost_Free(hs);
        g_lane_game = TORIRS_PLUGIN_GAME_UNKNOWN;
    }

    /* Standing down from inside a handler rather than from init. */
    {
        struct ToriRS_PluginHost* hl = PluginHost_New(&engine);
        int const l = PluginHost_Register(hl, &LATECOMER);

        g_latecomer_after = 0;
        g_latecomer_stops = 0;
        PluginHost_Start(hl);

        CHECK(!PluginHost_IsEnabled(hl, l), "a handler can stand its own plugin down");
        CHECK(g_latecomer_after == 1, "and the api still answers for it on the way out");
        CHECK(g_latecomer_stops == 1, "the handlers it had registered are unwound");
        PluginHost_Free(hl);
    }

    /* ---- published frame resolver: the saved id, never registration order */
    {
        struct ToriRS_PluginHost* hf;
        struct ToriRS_PluginApi const* api;
        struct ToriRS_PluginFrameSelection selected;
        int desktop;
        int mobile;

        snprintf(
            g_engine.frame_preference,
            sizeof(g_engine.frame_preference),
            "%s",
            "frame-mobile/phone");
        g_engine.frame_preference_present = 1;
        g_engine.frame_migration_version = 1;
        g_engine.layout_sets = 0;
        g_engine.layout_owned = 0;
        g_frame_desktop_starts = 0;
        g_frame_desktop_stops = 0;
        g_frame_desktop_layouts = 0;
        g_frame_mobile_starts = 0;
        g_frame_mobile_stops = 0;
        g_frame_mobile_layouts = 0;
        g_v2_transition_callback_alive = 0;
        g_v2_transition_stops = 0;

        hf = PluginHost_New(&engine);
        desktop = PluginHost_Register(hf, &FRAME_DESKTOP_PROVIDER);
        mobile = PluginHost_Register(hf, &FRAME_MOBILE_PROVIDER);
        PluginHost_Start(hf);
        api = PluginHost_Api(hf);
        memset(&selected, 0, sizeof(selected));
        api->frame_selection(PluginHost_Ctx(hf, desktop), &selected);

        CHECK(
            strcmp(selected.requested, "frame-mobile/phone") == 0 &&
                strcmp(selected.active, "core/native") == 0 &&
                selected.status == TORIRS_PLUGIN_FRAME_LOADING &&
                PluginHost_FrameNeedsLayout(hf),
            "the exact saved mobile id prepares a candidate over native");
        CHECK(
            g_frame_mobile_starts == 1 && g_frame_desktop_starts == 0,
            "only the selected provider starts");
        CHECK(g_engine.layout_owned == 0, "native remains committed before validation");
        PluginHost_Layout(hf, 900, 600);
        api->frame_selection(PluginHost_Ctx(hf, desktop), &selected);
        CHECK(
            g_frame_mobile_layouts == 1 && g_frame_desktop_layouts == 0 &&
                strcmp(selected.active, "frame-mobile/phone") == 0 &&
                selected.status == TORIRS_PLUGIN_FRAME_ACTIVE && g_engine.layout_owned == 1 &&
                g_engine.layout_canvas == TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW &&
                g_engine.layout_fixed_w == 320 && g_engine.layout_fixed_h == 240,
            "only a complete mobile declaration atomically becomes active");

        CHECK(
            api->frame_select(PluginHost_Ctx(hf, mobile), "frame-desktop/fixed"),
            "a canonical id switches providers");
        PluginHost_FrameStart(hf, 1, 0);
        api->frame_selection(PluginHost_Ctx(hf, desktop), &selected);
        CHECK(
            strcmp(selected.active, "frame-mobile/phone") == 0 &&
                selected.status == TORIRS_PLUGIN_FRAME_LOADING && g_frame_mobile_stops == 0 &&
                g_frame_desktop_starts == 1 && PluginHost_FrameNeedsLayout(hf),
            "switching starts the candidate while the committed provider stays live");
        CHECK(
            g_engine.layout_canvas == TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW,
            "the candidate cannot change live canvas policy before validation");
        PluginHost_Layout(hf, 900, 600);
        api->frame_selection(PluginHost_Ctx(hf, desktop), &selected);
        CHECK(
            strcmp(selected.active, "frame-desktop/fixed") == 0 &&
                selected.status == TORIRS_PLUGIN_FRAME_ACTIVE && g_frame_mobile_stops == 1 &&
                g_engine.layout_canvas == TORIRS_PLUGIN_CANVAS_FIXED &&
                g_engine.layout_fixed_w == 765 && g_engine.layout_fixed_h == 503,
            "READY commits the new canvas policy before stopping the old provider");

        CHECK(
            api->frame_select(PluginHost_Ctx(hf, desktop), "missing-provider/frame"),
            "a temporarily missing saved id is retained");
        PluginHost_FrameStart(hf, 2, 0);
        api->frame_selection(PluginHost_Ctx(hf, desktop), &selected);
        CHECK(
            strcmp(selected.requested, "missing-provider/frame") == 0 &&
                strcmp(selected.active, "frame-desktop/fixed") == 0 &&
                selected.status == TORIRS_PLUGIN_FRAME_FALLBACK && g_engine.layout_owned == 1,
            "a missing request reports fallback without discarding the last valid frame");

        CHECK(
            api->frame_select(PluginHost_Ctx(hf, desktop), "auto"),
            "Auto is an explicit accepted preference");
        PluginHost_FrameStart(hf, 3, 0);
        api->frame_selection(PluginHost_Ctx(hf, desktop), &selected);
        CHECK(
            strcmp(selected.requested, "auto") == 0 &&
                strcmp(selected.active, "core/native") == 0 &&
                selected.status == TORIRS_PLUGIN_FRAME_NATIVE,
            "Auto leaves the lane/server's native frame in charge");
        PluginHost_Free(hf);

        /* Same exact selection with the providers registered in reverse. */
        snprintf(
            g_engine.frame_preference,
            sizeof(g_engine.frame_preference),
            "%s",
            "frame-desktop/fixed");
        g_engine.frame_preference_present = 1;
        g_engine.frame_migration_version = 1;
        g_frame_desktop_starts = 0;
        g_frame_mobile_starts = 0;
        hf = PluginHost_New(&engine);
        mobile = PluginHost_Register(hf, &FRAME_MOBILE_PROVIDER);
        desktop = PluginHost_Register(hf, &FRAME_DESKTOP_PROVIDER);
        PluginHost_Start(hf);
        api = PluginHost_Api(hf);
        CHECK(PluginHost_FrameNeedsLayout(hf), "the reverse-order selection awaits validation");
        PluginHost_Layout(hf, 900, 600);
        api->frame_selection(PluginHost_Ctx(hf, mobile), &selected);
        CHECK(
            strcmp(selected.active, "frame-desktop/fixed") == 0 && g_frame_desktop_starts == 1 &&
                g_frame_mobile_starts == 0,
            "reversing registration order cannot change the exact selected id");
        PluginHost_Free(hf);
    }

    PluginHost_Free(host);

    /* ---- callback-table v2 registration and lifecycle ---------------- */
    {
        struct ToriRS_PluginHost* hv2;
        struct ToriRS_UiNodeInfo ui_info = { .struct_size = sizeof(ui_info) };
        struct ToriRS_UiNodeRef private_ref;
        struct ToriRS_UiNodeRef housing_ref;
        struct ToriRS_PluginFrameSelection selection;
        struct ToriRS_PluginWinWidget const* widget;
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
        g_screen_now = TORIRS_PLUGIN_SCREEN_GAME;
        g_lane_game = TORIRS_PLUGIN_GAME_RS2;
        g_role_name = NULL;
        g_member_slot = -1;
        g_member_no = -1;
        g_slot_w[TORIRS_PLUGIN_SLOT_CANVAS] = 900;
        g_slot_h[TORIRS_PLUGIN_SLOT_CANVAS] = 600;
        g_slot_w[TORIRS_PLUGIN_SLOT_VIEWPORT] = 900;
        g_slot_h[TORIRS_PLUGIN_SLOT_VIEWPORT] = 600;
        g_slot_x[TORIRS_PLUGIN_SLOT_MINIMAP] = 620;
        g_slot_y[TORIRS_PLUGIN_SLOT_MINIMAP] = 10;
        g_slot_w[TORIRS_PLUGIN_SLOT_MINIMAP] = 150;
        g_slot_h[TORIRS_PLUGIN_SLOT_MINIMAP] = 150;
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
        CHECK(PluginHost_IsAdapter(hv2, b2), "v2 adapter flag normalizes");
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
        {
            int conflicts = -1;
            CHECK(
                PluginHost_Api(hv2)->ui_contribution_facets(
                    PluginHost_Ctx(hv2, a2), "status", &conflicts) == TORIRS_UI_FACET_ALL &&
                    conflicts == 0,
                "legacy adapters can query retained v2 contribution grants by stored ref");
        }
        CHECK(
            g_v2_typed_calls == 3 && g_engine.objects_live == 3,
            "typed module calls execute for every live instance");
        memset(&selection, 0, sizeof(selection));
        PluginHost_Api(hv2)->frame_selection(PluginHost_Ctx(hv2, a2), &selection);
        CHECK(
            strcmp(selection.active, "core/native") == 0 &&
                selection.status == TORIRS_PLUGIN_FRAME_LOADING &&
                PluginHost_FrameNeedsLayout(hv2) && g_engine.layout_owned == 0,
            "v2 offer conversion prepares a candidate without changing native policy");

        PluginHost_LogicTick(hv2, 7);
        CHECK(
            ((struct V2ProbeState*)g_v2_latest_state[1])->ticks == 7 &&
                ((struct V2ProbeState*)g_v2_latest_state[2])->ticks == 7,
            "automatic callback dispatch passes each instance its own state");
        PluginHost_ReconcileRoleReplacements(hv2);
        presentation_rebuilds = PluginHost_UiPresentationRebuilds(hv2);
        g_role_name = "viewport";
        g_role_visible = 1;
        g_role_box[0] = 0;
        g_role_box[1] = 0;
        g_role_box[2] = 900;
        g_role_box[3] = 600;
        PluginHost_ReconcileRoleReplacements(hv2);
        CHECK(
            PluginHost_UiPresentationRebuilds(hv2) == presentation_rebuilds + 1,
            "a previously unresolved native boundary becoming live rebuilds the compact presenter once");
        presentation_rebuilds = PluginHost_UiPresentationRebuilds(hv2);
        PluginHost_ReconcileRoleReplacements(hv2);
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

        PluginHost_Layout(hv2, 900, 600);
        CHECK(
            g_v2_frame_builds == 1 && g_v2_frame_width == 900 &&
                g_v2_frame_canvas == TORIRS_FRAME_CANVAS_WINDOW && g_engine.layout_begins == 1 &&
                g_engine.layout_ends == 1,
            "selected v2 offer builds once and atomically commits READY geometry");
        PluginHost_Api(hv2)->frame_selection(PluginHost_Ctx(hv2, a2), &selection);
        CHECK(
            strcmp(selection.active, "v2-frame/test") == 0 &&
                selection.status == TORIRS_PLUGIN_FRAME_ACTIVE && g_engine.layout_owned == 1 &&
                g_engine.layout_canvas == TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW &&
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
                hv2, generation, 320, 480, 1000, TORIRS_PLUGIN_PANEL_COMPACT, true, true),
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
                    TORIRS_PLUGIN_UI_TOGGLE,
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
                          TORIRS_PLUGIN_UI_PICK,
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
                          TORIRS_PLUGIN_UI_PICK,
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
                          TORIRS_PLUGIN_UI_PICK,
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
                          TORIRS_PLUGIN_UI_PICK,
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

    /* ---- frame candidates retain the last complete commit ------------ */
    {
        static int const INVALID_MODE[] = {
            V2_TRANSITION_NO_VIEWPORT,
            V2_TRANSITION_DUP_SURFACE,
            V2_TRANSITION_DUP_MEMBER,
            V2_TRANSITION_BAD_RECT,
            V2_TRANSITION_TOO_MANY_NODES,
            V2_TRANSITION_CYCLE,
            V2_TRANSITION_FOREIGN_IMAGE,
            V2_TRANSITION_UNREADY_IMAGE,
            V2_TRANSITION_ORPHAN_SKIN,
        };
        struct ToriRS_PluginHost* ht;
        struct ToriRS_PluginApi const* api;
        struct ToriRS_PluginFrameSelection selection;
        struct ToriRS_UiNodeInfo info;
        struct ToriRS_UiNodeRef marker;
        int old_provider;
        int candidate_provider;

        memset(&g_engine, 0, sizeof(g_engine));
        g_screen_now = TORIRS_PLUGIN_SCREEN_GAME;
        g_frame_mobile_starts = 0;
        g_frame_mobile_stops = 0;
        g_frame_mobile_layouts = 0;
        snprintf(
            g_engine.frame_preference,
            sizeof(g_engine.frame_preference),
            "%s",
            "frame-mobile/phone");
        g_engine.frame_preference_present = 1;
        g_engine.frame_migration_version = 1;
        engine = fake_engine();
        ht = PluginHost_New(&engine);
        old_provider = PluginHost_Register(ht, &FRAME_MOBILE_PROVIDER);
        candidate_provider = PluginHost_RegisterV2(ht, &V2_TRANSITION_PROVIDER);
        PluginHost_Start(ht);
        PluginHost_Layout(ht, 900, 600);
        api = PluginHost_Api(ht);
        api->frame_selection(PluginHost_Ctx(ht, old_provider), &selection);
        CHECK(
            strcmp(selection.active, "frame-mobile/phone") == 0,
            "transition fixture begins with one fully committed frame");

        g_v2_transition_mode = V2_TRANSITION_PENDING;
        CHECK(
            api->frame_select(
                PluginHost_Ctx(ht, old_provider), "v2-transition/candidate"),
            "a second provider can become the requested candidate");
        PluginHost_FrameStart(ht, 1, 0);
        {
            int const begins = g_engine.layout_begins;
            int const ends = g_engine.layout_ends;
            PluginHost_Layout(ht, 900, 600);
            api->frame_selection(PluginHost_Ctx(ht, old_provider), &selection);
            CHECK(
                strcmp(selection.active, "frame-mobile/phone") == 0 &&
                    selection.status == TORIRS_PLUGIN_FRAME_LOADING &&
                    g_engine.layout_begins == begins && g_engine.layout_ends == ends &&
                    g_frame_mobile_stops == 0,
                "A active -> B PENDING leaves A's provider and geometry untouched");
        }

        g_v2_transition_mode = V2_TRANSITION_READY;
        api->frame_invalidate(PluginHost_Ctx(ht, candidate_provider));
        PluginHost_Start(ht);
        CHECK(PluginHost_FrameNeedsLayout(ht), "B READY schedules one new candidate attempt");
        PluginHost_Layout(ht, 900, 600);
        api->frame_selection(PluginHost_Ctx(ht, old_provider), &selection);
        CHECK(
            strcmp(selection.active, "v2-transition/candidate") == 0 &&
                selection.status == TORIRS_PLUGIN_FRAME_ACTIVE && g_frame_mobile_stops == 1,
            "B commits before A is torn down");
        marker = PluginHost_UiRef(ht, candidate_provider, "marker");
        memset(&info, 0, sizeof(info));
        info.struct_size = sizeof(info);
        CHECK(
            PluginHost_UiInfo(ht, marker, &info) && strcmp(info.label, "committed") == 0,
            "B's geometry and named-node declaration commit together");

        CHECK(
            api->frame_select(
                PluginHost_Ctx(ht, candidate_provider), "v2-transition/invalid"),
            "the invalid offer becomes a candidate, not an active frame");
        PluginHost_FrameStart(ht, 2, 0);
        for( int i = 0; i < (int)(sizeof(INVALID_MODE) / sizeof(INVALID_MODE[0])); i++ )
        {
            int const begins = g_engine.layout_begins;
            int const ends = g_engine.layout_ends;
            g_v2_transition_mode = INVALID_MODE[i];
            if( i > 0 )
            {
                api->frame_invalidate(PluginHost_Ctx(ht, candidate_provider));
                PluginHost_Start(ht);
            }
            PluginHost_Layout(ht, 900, 600);
            api->frame_selection(PluginHost_Ctx(ht, candidate_provider), &selection);
            memset(&info, 0, sizeof(info));
            info.struct_size = sizeof(info);
            CHECK(
                strcmp(selection.active, "v2-transition/candidate") == 0 &&
                    selection.status == TORIRS_PLUGIN_FRAME_FALLBACK &&
                    g_engine.layout_begins == begins && g_engine.layout_ends == ends &&
                    PluginHost_UiInfo(ht, marker, &info) &&
                    strcmp(info.label, "committed") == 0,
                "an invalid candidate publishes neither partial geometry nor named state");
        }
        CHECK(
            api->frame_select(
                PluginHost_Ctx(ht, candidate_provider), "v2-transition/candidate"),
            "the committed offer can be selected again after invalid-candidate tests");
        PluginHost_FrameStart(ht, 3, 0);
        g_v2_transition_mode = V2_TRANSITION_SELECT_AUTO;
        api->frame_invalidate(PluginHost_Ctx(ht, candidate_provider));
        {
            int const begins = g_engine.layout_begins;
            int const ends = g_engine.layout_ends;
            int const stops = g_v2_transition_stops;

            PluginHost_Layout(ht, 900, 600);
            api->frame_selection(PluginHost_Ctx(ht, candidate_provider), &selection);
            CHECK(
                g_v2_transition_callback_alive && g_v2_transition_stops == stops &&
                    strcmp(selection.requested, "auto") == 0 &&
                    strcmp(selection.active, "v2-transition/candidate") == 0 &&
                    g_engine.layout_begins == begins && g_engine.layout_ends == ends,
                "frame selection inside build keeps state alive and fences that candidate");
        }
        PluginHost_FrameStart(ht, 4, 0);
        api->frame_selection(PluginHost_Ctx(ht, candidate_provider), &selection);
        CHECK(
            g_v2_transition_stops == 1 && strcmp(selection.active, "core/native") == 0 &&
                selection.status == TORIRS_PLUGIN_FRAME_NATIVE,
            "the next safe frame boundary resolves selection and only then stops the provider");
        PluginHost_Free(ht);
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

    /* ---- incarnation-fenced V2 resources survive legacy slot reuse ----- */
    {
        struct ToriRS_PluginHost* aba_host;
        struct ToriRS_PluginApi const* api;
        struct ToriRS_PluginFrameSelection selection;
        struct ToriRS_UiNodeInfo badge_info = { .struct_size = sizeof(badge_info) };
        struct ToriRS_UiNodeRef badge;
        unsigned char* data;
        int draw_before;

        memset(&g_engine, 0, sizeof(g_engine));
        memset(&g_v2_aba, 0, sizeof(g_v2_aba));
        g_v2_aba_starts = 0;
        g_screen_now = TORIRS_PLUGIN_SCREEN_GAME;
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
        api = PluginHost_Api(aba_host);
        api->frame_selection(PluginHost_Ctx(aba_host, 0), &selection);
        CHECK(
            strcmp(selection.active, "v2-aba/frame") == 0 &&
                selection.status == TORIRS_PLUGIN_FRAME_ACTIVE,
            "the resource probe commits image-backed frame art");
        badge = PluginHost_UiRef(aba_host, 0, "badge");
        CHECK(
            PluginHost_UiInfo(aba_host, badge, &badge_info) &&
                badge_info.state_images[TORIRS_UI_VISUAL_IDLE].value != 0,
            "ui.update retains the first live image token");

        PluginHost_LogicTick(aba_host, 2);
        api->frame_selection(PluginHost_Ctx(aba_host, 0), &selection);
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
            "every stale typed resource operation fails before reaching a reused legacy slot");
        CHECK(
            g_engine.image_releases == 1 && g_engine.model_releases == 1 &&
                g_engine.meshes_live == 1 && g_engine.objects_live == 1,
            "repeated stale release/destroy calls leave all four replacements live");
        CHECK(
            strcmp(selection.active, "core/native") == 0 &&
                selection.status == TORIRS_PLUGIN_FRAME_FALLBACK,
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
        struct ToriRS_PluginApi const* api;
        struct ToriRS_PluginCtx* ctx;
        struct ToriRS_PlacementRect rect;
        struct ToriRS_PlacementRect notch = { 0, 0, 10, 10 };
        struct ToriRS_PlacementRect keyboard = { 0, 80, 100, 20 };
        struct ToriRS_PlacementRect minimap = { 70, 10, 20, 20 };
        struct ToriRS_PlacementRect rail = { 90, 0, 10, 80 };
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
        g_screen_now = TORIRS_PLUGIN_SCREEN_GAME;
        g_role_name = NULL;
        g_role_visible = 0;
        g_member_slot = -1;
        g_member_no = -1;
        g_slot_w[TORIRS_PLUGIN_SLOT_CANVAS] = 100;
        g_slot_h[TORIRS_PLUGIN_SLOT_CANVAS] = 100;
        g_slot_w[TORIRS_PLUGIN_SLOT_VIEWPORT] = 100;
        g_slot_h[TORIRS_PLUGIN_SLOT_VIEWPORT] = 100;
        g_slot_x[TORIRS_PLUGIN_SLOT_MINIMAP] = minimap.x;
        g_slot_y[TORIRS_PLUGIN_SLOT_MINIMAP] = minimap.y;
        g_slot_w[TORIRS_PLUGIN_SLOT_MINIMAP] = minimap.w;
        g_slot_h[TORIRS_PLUGIN_SLOT_MINIMAP] = minimap.h;
        /* Canvas minus a top-left notch and the keyboard's bottom band. */
        g_platform_safe_count = 2;
        g_platform_safe[0] = (struct ToriRS_PlacementRect){ 10, 0, 90, 10 };
        g_platform_safe[1] = (struct ToriRS_PlacementRect){ 0, 10, 100, 70 };
        g_lane_rail_box[0] = rail.x;
        g_lane_rail_box[1] = rail.y;
        g_lane_rail_box[2] = rail.w;
        g_lane_rail_box[3] = rail.h;
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
        api = PluginHost_Api(hp);
        ctx = PluginHost_Ctx(hp, probe);

        baseline = api->placement_revision(ctx);
        CHECK(baseline != 0, "the first complete placement snapshot has a revision");
        {
            struct ToriRS_PluginFrameInfo offer;
            CHECK(
                api->placement_rect_next(
                    ctx, TORIRS_PLUGIN_AREA_OVERLAY_SAFE, INT_MAX, &rect) == -1 &&
                    api->frame_offer_next(ctx, INT_MAX, &offer) == -1,
                "public iterators reject INT_MAX without signed overflow");
        }
        CHECK(
            !api->placement_contains(ctx, TORIRS_PLUGIN_AREA_PLATFORM_SAFE, &notch) &&
                !api->placement_contains(
                    ctx, TORIRS_PLUGIN_AREA_PLATFORM_SAFE, &keyboard),
            "platform-safe preserves both the corner notch and keyboard exclusion");
        CHECK(
            !api->placement_contains(ctx, TORIRS_PLUGIN_AREA_FRAME_BUILD, &rail) &&
                api->placement_contains(ctx, TORIRS_PLUGIN_AREA_FRAME_BUILD, &minimap),
            "frame-build excludes the lane rail but not replaceable frame furniture");
        CHECK(
            !api->placement_contains(ctx, TORIRS_PLUGIN_AREA_OVERLAY_SAFE, &rail) &&
                !api->placement_contains(ctx, TORIRS_PLUGIN_AREA_OVERLAY_SAFE, &minimap),
            "overlay-safe composes lane and frame occluders");
        while( (iter = api->placement_rect_next(
                    ctx, TORIRS_PLUGIN_AREA_OVERLAY_SAFE, iter, &rect)) >= 0 )
            fragments++;
        CHECK(fragments >= 3, "the composed overlay area remains fragmented");

        PluginHost_LayoutChanged(hp);
        CHECK(
            api->placement_revision(ctx) == baseline && g_placement_v2_calls == 0,
            "an identical layout rebuild neither bumps nor notifies placement");

        CHECK(
            api->placement_reserve(
                ctx,
                "left-dock",
                TORIRS_PLUGIN_AREA_OVERLAY_SAFE,
                TORIRS_PLUGIN_PLACEMENT_EDGE_LEFT,
                5),
            "a named reservation joins the composed placement transaction");
        reserved_revision = api->placement_revision(ctx);
        CHECK(
            reserved_revision == baseline + 1 && g_placement_v2_calls == 1 &&
                g_placement_v2_revision == reserved_revision,
            "an assigned reservation advances and publishes exactly one revision");
        CHECK(
            api->placement_reservation_rect(ctx, "left-dock", &rect) && rect.x == 0 &&
                rect.y == 30 && rect.w == 5 && rect.h == 50 &&
                !api->placement_contains(ctx, TORIRS_PLUGIN_AREA_OVERLAY_SAFE, &rect),
            "the named reservation reports the exact fragment it consumed");

        PluginHost_LayoutChanged(hp);
        CHECK(
            api->placement_revision(ctx) == reserved_revision && g_placement_v2_calls == 1,
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
                g_placement_v2_revision == api->placement_revision(ctx),
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
        g_slot_w[TORIRS_PLUGIN_SLOT_CANVAS] = 100;
        g_slot_h[TORIRS_PLUGIN_SLOT_CANVAS] = 100;
        g_slot_w[TORIRS_PLUGIN_SLOT_VIEWPORT] = 100;
        g_slot_h[TORIRS_PLUGIN_SLOT_VIEWPORT] = 100;
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
        g_slot_w[TORIRS_PLUGIN_SLOT_CANVAS] = 100;
        g_slot_h[TORIRS_PLUGIN_SLOT_CANVAS] = 100;
        g_slot_w[TORIRS_PLUGIN_SLOT_VIEWPORT] = 100;
        g_slot_h[TORIRS_PLUGIN_SLOT_VIEWPORT] = 100;
        g_slot_x[TORIRS_PLUGIN_SLOT_ORBS] = 70;
        g_slot_y[TORIRS_PLUGIN_SLOT_ORBS] = 5;
        g_slot_w[TORIRS_PLUGIN_SLOT_ORBS] = 25;
        g_slot_h[TORIRS_PLUGIN_SLOT_ORBS] = 80;
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
        PluginHost_ReconcileRoleReplacements(facet_host);
        late_rebuilds = PluginHost_UiPresentationRebuilds(facet_host);
        g_role_suppress_calls = 0;
        g_role_name = "orb_run";
        PluginHost_ReconcileRoleReplacements(facet_host);
        CHECK(
            PluginHost_UiPresentationRebuilds(facet_host) == late_rebuilds + 1 &&
                g_role_suppress_calls > 0 && g_role_suppress_paint == 1 &&
                g_role_suppress_input == 0,
            "a contribution retained through a rebuild gap suppresses its newly live role without a registry change");
        late_rebuilds = PluginHost_UiPresentationRebuilds(facet_host);
        PluginHost_ReconcileRoleReplacements(facet_host);
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

        PluginHost_SetEnabled(facet_host, actions, true);
        PluginHost_DrawCanvas(facet_host, 100, 100);
        CHECK(
            PluginHost_UiPresentationCount(facet_host) == 1 &&
                g_role_suppress_paint == 1 && g_role_suppress_input == 1 &&
                g_present_draws == 2 && g_hit_region_calls == 1,
            "independent APPEARANCE and ACTIONS winners suppress both matching native facets");
        old_tag = g_hit_region_tag;
        old_actions = g_present_actions;
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
        PluginHost_DrawCanvas(facet_host, 100, 100);
        CHECK(
            g_hit_region_tag != old_tag && strcmp(g_hit_region_ops[0], "second") == 0,
            "action-content changes mint a new operation identity");
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
                g_role_anchor_last_place == PLUGIN_ANCHOR_PLACE_SELF,
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
        int present_v1;

        memset(&g_engine, 0, sizeof(g_engine));
        memset(g_slot_x, 0, sizeof(g_slot_x));
        memset(g_slot_y, 0, sizeof(g_slot_y));
        memset(g_slot_w, 0, sizeof(g_slot_w));
        memset(g_slot_h, 0, sizeof(g_slot_h));
        g_slot_w[TORIRS_PLUGIN_SLOT_CANVAS] = 100;
        g_slot_h[TORIRS_PLUGIN_SLOT_CANVAS] = 100;
        g_slot_w[TORIRS_PLUGIN_SLOT_VIEWPORT] = 100;
        g_slot_h[TORIRS_PLUGIN_SLOT_VIEWPORT] = 100;
        g_slot_x[TORIRS_PLUGIN_SLOT_ORBS] = 70;
        g_slot_y[TORIRS_PLUGIN_SLOT_ORBS] = 5;
        g_slot_w[TORIRS_PLUGIN_SLOT_ORBS] = 25;
        g_slot_h[TORIRS_PLUGIN_SLOT_ORBS] = 80;
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
        g_present_v1_tag = 0;
        present_engine = fake_engine();
        present_host = PluginHost_New(&present_engine);
        present_a = PluginHost_RegisterV2(present_host, &V2_PRESENT_A);
        present_b = PluginHost_RegisterV2(present_host, &V2_PRESENT_B);
        present_v1 = PluginHost_Register(present_host, &PRESENT_V1);
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
        PluginHost_CanvasClick(present_host, present_v1, 0x80001234u, 0, 0, 0);
        CHECK(
            g_present_v1_tag == 0x80001234u,
            "a legacy plugin keeps the full documented uint32 tag namespace");
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
