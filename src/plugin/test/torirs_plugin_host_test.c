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

#include <assert.h>
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
    int native_root;
    int native_tab_selects;
    int frame_provides;
    int layout_sets;
    int frame_active;
    int layout_canvas;
    int layout_fixed_w;
    int layout_fixed_h;
    char frame_preference[TORIRS_PLUGIN_FRAME_ID_MAX];
    int frame_preference_present;
    int frame_migration_version;
    /* The platform band: what platform_safe_rect answers, when `safe_present`. */
    int safe_present;
    int safe_x;
    int safe_y;
    int safe_w;
    int safe_h;
};

static struct FakeEngine g_engine;

/* In game: these harnesses exercise behaviour that is gated on it. Mutable so
 * the on_screen_changed test can move it; everything else leaves it alone.
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
static int fake_scene_x=3184,fake_scene_z=3392;
static bool fake_scene_present=true;
static bool fake_scene_origin(void* user,int* x,int* z)
{
    (void)user;
    if( !fake_scene_present ) return false;
    *x=fake_scene_x;*z=fake_scene_z;return true;
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

static uint64_t frame_provide_owner;
static void
fake_frame_provide(void* u, uint64_t owner)
{
    struct FakeEngine* e = u;
    assert(owner);
    frame_provide_owner = owner;
    e->frame_provides++;
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
fake_frame_activate(
    void* u,
    int active,
    int canvas,
    int fixed_w,
    int fixed_h)
{
    struct FakeEngine* e = u;
    e->layout_sets++;
    e->frame_active = active;
    e->layout_canvas = canvas;
    e->layout_fixed_w = fixed_w;
    e->layout_fixed_h = fixed_h;
}
static int fake_frame_root(void* user) { return ((struct FakeEngine*)user)->native_root; }
static int fake_platform_safe_rect(void* user,int* out_x,int* out_y,int* out_w,int* out_h)
{
    struct FakeEngine* e=user;
    if( !e->safe_present ) return 0;
    *out_x=e->safe_x;*out_y=e->safe_y;*out_w=e->safe_w;*out_h=e->safe_h;
    return 1;
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
    ((struct FakeEngine*)u)->native_tab_selects++;
    (void)tabno;
    return 1;
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
    e.scene_origin = fake_scene_origin;
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
    e.slot_native_size = fake_slot_native_size;
    e.component_rect = fake_component_rect;
    e.frame_activate = fake_frame_activate;
    e.frame_root = fake_frame_root;
    e.platform_safe_rect = fake_platform_safe_rect;
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
    e.if_click = fake_if_click;
    e.menu_add = fake_menu_add;
    e.menu_drop = fake_menu_drop;
    e.obj_next = fake_obj_next;
    e.asset_read = fake_asset_read;
    e.asset_write = fake_asset_write;
    e.screenshot = fake_screenshot;
    e.frame_provide = fake_frame_provide;
    e.model_publish = fake_model_publish;
    e.model_release = fake_model_release;
    e.mesh_create = fake_mesh_create;
    e.mesh_destroy = fake_mesh_destroy;
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
static struct ToriRS_Api* g_v2_api[4];
static int g_v2_starts[4];
static int g_v2_stops[4];
static int g_v2_zeroed_starts;
static int g_v2_typed_calls;
static int g_v2_panel_builds;
static int g_v2_panel_actions;
static int g_v2_select_actions;
static char g_v2_select_value[TORIRS_PLUGIN_SELECT_VALUE_MAX];
static int g_v2_panel_draws;
static int g_v2_frame_provisions;
static int g_v2_frame_width;
static int g_v2_frame_canvas;
static int g_v2_prefix_starts;
static int g_v2_started_with_saved_config;

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
    struct ToriRS_Api* api,
    void* state_ptr)
{
    struct V2ProbeState* state = state_ptr;
    struct ToriRS_PlayerSnapshot player;
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
    CHECK(
        api->scene.instance_create(api, &state->instance) == TORIRS_RESULT_OK,
        "v2 typed scene instance is allocated");
    g_v2_typed_calls++;

    if( marker == 1 )
    {
        char const* loaded = NULL;
        CHECK(
            api->config.get_string(api, "loaded", &loaded) && loaded &&
                strcmp(loaded, "saved") == 0,
            "v2 on_start observes config loaded before the explicit startup fence");
        g_v2_started_with_saved_config = loaded && strcmp(loaded, "saved") == 0;
        CHECK(
            api->panel.request(api, &panel) == TORIRS_RESULT_OK,
            "v2 on_start can register a panel through the typed module");
    }
}

static void
v2_probe_stop(
    struct ToriRS_Api* api,
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
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_TickEvent const* event)
{
    struct V2ProbeState* state = state_ptr;
    (void)api;
    state->ticks += event->cycle;
}

static void
v2_probe_canvas(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_Graphics* draw)
{
    struct V2ProbeState* state = state_ptr;
    (void)api;
    CHECK(g_engine.draw_canvas == 1, "v2 canvas callback runs only inside the canvas draw scope");
    draw->rect(draw, (struct ToriRS_Rect){ state->marker, 1, 2, 3 }, 0xabcdefu, 255);
    state->canvas_draws++;
}

static void
v2_probe_ui_build(
    struct ToriRS_Api* api,
    void* state,
    struct ToriRS_PanelBuilder* panel,
    int view)
{
    struct ToriRS_PanelNode labelled_custom = {
        .struct_size = sizeof(labelled_custom),
        .kind = TORIRS_PANEL_CUSTOM,
        .id = "labelled_chart",
        .label = "Activity chart",
        .preferred_height = 72,
    };

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
    (void)panel->node(panel, &labelled_custom);
    g_v2_panel_builds++;
}

static void
v2_probe_ui_action(
    struct ToriRS_Api* api,
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
    struct ToriRS_Api* api,
    void* state,
    char const* node,
    struct ToriRS_Graphics* draw)
{
    (void)api;
    (void)state;
    if( strcmp(node, "chart") == 0 )
    {
        draw->rect(draw, (struct ToriRS_Rect){ 0, 0, 10, 10 }, 0x123456u, 255);
        g_v2_panel_draws++;
    }
}

static void
v2_prefix_start(struct ToriRS_Api* api, void* state)
{
    (void)api;
    (void)state;
    g_v2_prefix_starts++;
}

static enum ToriRS_FrameBuildResult
v2_probe_gameframe(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_GameframeEvent const* event)
{
    struct V2ProbeState* state = state_ptr;
    int navigation_before = g_engine.native_tab_selects;

    CHECK(!api->cache.tab_select(api, 3) && g_engine.native_tab_selects == navigation_before,
          "frame provision cannot issue native navigation commands");
    CHECK(state && state->marker == 3, "selected frame receives its own v2 state");
    CHECK(strcmp(event->offer_id, "test") == 0, "frame provision receives the local offer id");
    if( !event->active )
        return TORIRS_FRAME_READY;
    g_v2_frame_width = event->width;
    g_v2_frame_canvas = event->canvas;
    g_v2_frame_provisions++;
    return TORIRS_FRAME_READY;
}

static struct ToriRS_ConfigItem const V2_CONFIG_A_ITEMS[] = {
    { .key = "marker", .label = "Marker", .type = TORIRS_CONFIG_INT, .default_value = "1" },
    { .key = "loaded", .label = "Loaded", .type = TORIRS_CONFIG_STRING,
      .default_value = "default" },
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

static struct ToriRS_ConfigItem const V2_CONFIG_BAD_KEY_ITEMS[] = {
    { .key = "bad-key", .type = TORIRS_CONFIG_STRING, .default_value = "safe" },
    { 0 },
};
static struct ToriRS_ConfigItem const V2_CONFIG_DUPLICATE_ITEMS[] = {
    { .key = "same", .type = TORIRS_CONFIG_STRING, .default_value = "one" },
    { .key = "same", .type = TORIRS_CONFIG_STRING, .default_value = "two" },
    { 0 },
};
static struct ToriRS_ConfigItem const V2_CONFIG_MULTILINE_DEFAULT_ITEMS[] = {
    { .key = "safe", .type = TORIRS_CONFIG_STRING,
      .default_value = "value\n[plugin:other]\nenabled=0" },
    { 0 },
};
static struct ToriRS_ConfigSchema const V2_CONFIG_BAD_KEY = {
    .struct_size = sizeof(V2_CONFIG_BAD_KEY),
    .items = V2_CONFIG_BAD_KEY_ITEMS,
};
static struct ToriRS_ConfigSchema const V2_CONFIG_DUPLICATE = {
    .struct_size = sizeof(V2_CONFIG_DUPLICATE),
    .items = V2_CONFIG_DUPLICATE_ITEMS,
};
static struct ToriRS_ConfigSchema const V2_CONFIG_MULTILINE_DEFAULT = {
    .struct_size = sizeof(V2_CONFIG_MULTILINE_DEFAULT),
    .items = V2_CONFIG_MULTILINE_DEFAULT_ITEMS,
};

static struct ToriRS_PluginDef const V2_BAD_CONFIG_KEY = {
    .struct_size = sizeof(V2_BAD_CONFIG_KEY),
    .id = "v2-bad-config-key",
    .title = "Bad Config Key",
    .version = "2.0.0",
    .config = &V2_CONFIG_BAD_KEY,
    .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks) },
};
static struct ToriRS_PluginDef const V2_DUPLICATE_CONFIG_KEY = {
    .struct_size = sizeof(V2_DUPLICATE_CONFIG_KEY),
    .id = "v2-duplicate-config-key",
    .title = "Duplicate Config Key",
    .version = "2.0.0",
    .config = &V2_CONFIG_DUPLICATE,
    .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks) },
};
static struct ToriRS_PluginDef const V2_MULTILINE_CONFIG_DEFAULT = {
    .struct_size = sizeof(V2_MULTILINE_CONFIG_DEFAULT),
    .id = "v2-multiline-config-default",
    .title = "Multiline Config Default",
    .version = "2.0.0",
    .config = &V2_CONFIG_MULTILINE_DEFAULT,
    .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks) },
};

static struct ToriRS_FrameOffer const V2_FRAME_OFFERS[] = {
    {
     .struct_size = sizeof(struct ToriRS_FrameOffer),
     .id = "test",
     .title = "V2 Test Frame",
     .canvas = TORIRS_FRAME_CANVAS_WINDOW,
     .min_width = 640,
     .min_height = 480,
     },
    { .struct_size = sizeof(struct ToriRS_FrameOffer) },
};

static struct ToriRS_PluginDef const V2_PROBE_A = {
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
    },
};

static struct ToriRS_PluginDef const V2_PROBE_B = {
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
    },
    .flags = TORIRS_PLUGIN_RUNTIME_HOST,
};

static struct ToriRS_PluginDef const V2_FRAME_PROVIDER = {
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
        .on_gameframe = v2_probe_gameframe,
    },
    .frames = V2_FRAME_OFFERS,
    .flags = TORIRS_PLUGIN_DISABLED_BY_DEFAULT,
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
v2_seam_start(struct ToriRS_Api* api, void* state)
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
    struct ToriRS_Api* api,
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

static struct ToriRS_PluginDef const V2_SEAM_PROBE = {
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
static struct ToriRS_Api* g_v2_aba_api;

static void
v2_aba_start(struct ToriRS_Api* api, void* state_ptr)
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
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_TickEvent const* event)
{
    struct V2AbaState* state = state_ptr;
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
v2_aba_gameframe(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_GameframeEvent const* event)
{
    (void)api;
    (void)state_ptr;
    (void)event;
    return TORIRS_FRAME_READY;
}

static struct ToriRS_FrameOffer const V2_ABA_FRAMES[] = {
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "frame",
        .title = "ABA frame",
        .canvas = TORIRS_FRAME_CANVAS_WINDOW,
        .min_width = 640,
        .min_height = 480,
    },
    { .struct_size = sizeof(struct ToriRS_FrameOffer) },
};

static struct ToriRS_PluginDef const V2_ABA_PROBE = {
    .struct_size = sizeof(V2_ABA_PROBE),
    .id = "v2-aba",
    .title = "V2 ABA Probe",
    .version = "2.0.0",
    .state_size = sizeof(struct V2AbaState),
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = v2_aba_start,
        .on_logic_tick = v2_aba_logic,
        .on_gameframe = v2_aba_gameframe,
    },
    .frames = V2_ABA_FRAMES,
    .flags = TORIRS_PLUGIN_DISABLED_BY_DEFAULT,
};

/* A definition ending inside its final callback table exercises append-only
 * minor-version reads without granting access to any callback tail. */
static struct ToriRS_PluginDef const V2_PREFIX_ONLY = {
    .struct_size = offsetof(struct ToriRS_PluginDef, callbacks) +
                   offsetof(struct ToriRS_PluginCallbacks, on_stop),
    .id = "v2-prefix",
    .title = "V2 Prefix",
    .version = "2.0.0",
    .callbacks = {
        .struct_size = offsetof(struct ToriRS_PluginCallbacks, on_stop),
        .on_start = v2_prefix_start,
    },
};


/* Minimal providers used only to exercise the one-time preference migration.
 * Their canonical ids intentionally match the bundled providers; no layout is
 * executed in these cases, but registration still requires on_gameframe. */
static enum ToriRS_FrameBuildResult
legacy_gameframe(
    struct ToriRS_Api* api,
    void* state,
    struct ToriRS_GameframeEvent const* event)
{
    (void)api;
    (void)state;
    (void)event;
    return TORIRS_FRAME_READY;
}

static struct ToriRS_FrameOffer const LEGACY_DESKTOP_OFFERS[] = {
    { .struct_size = sizeof(struct ToriRS_FrameOffer),
      .id = "classic-fixed", .title = "Classic Fixed",
      .canvas = TORIRS_FRAME_CANVAS_FIXED, .width = 765, .height = 503 },
    { .struct_size = sizeof(struct ToriRS_FrameOffer),
      .id = "modern-fixed", .title = "Modern Fixed",
      .canvas = TORIRS_FRAME_CANVAS_FIXED, .width = 765, .height = 503 },
    { .struct_size = sizeof(struct ToriRS_FrameOffer),
      .id = "modern-resizable", .title = "Modern Resizable",
      .canvas = TORIRS_FRAME_CANVAS_WINDOW, .min_width = 765, .min_height = 503 },
    { .struct_size = sizeof(struct ToriRS_FrameOffer) },
};

static struct ToriRS_FrameOffer const LEGACY_MOBILE_OFFERS[] = {
    { .struct_size = sizeof(struct ToriRS_FrameOffer),
      .id = "stone-drawer", .title = "Stone Drawer",
      .canvas = TORIRS_FRAME_CANVAS_WINDOW, .min_width = 640, .min_height = 437 },
    { .struct_size = sizeof(struct ToriRS_FrameOffer) },
};

static struct ToriRS_PluginDef const LEGACY_DESKTOP_PROVIDER = {
    .struct_size = sizeof(struct ToriRS_PluginDef),
    .id = "gameframe-layout",
    .title = "Legacy desktop frame",
    .version = "1",
    .flags = TORIRS_PLUGIN_DISABLED_BY_DEFAULT,
    .frames = LEGACY_DESKTOP_OFFERS,
    .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks),
                   .on_gameframe = legacy_gameframe },
};

static struct ToriRS_PluginDef const LEGACY_MOBILE_PROVIDER = {
    .struct_size = sizeof(struct ToriRS_PluginDef),
    .id = "mobile-gameframe",
    .title = "Legacy mobile frame",
    .version = "1",
    .flags = TORIRS_PLUGIN_DISABLED_BY_DEFAULT,
    .frames = LEGACY_MOBILE_OFFERS,
    .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks),
                   .on_gameframe = legacy_gameframe },
};

static void
check_legacy_frame_migration(
    char const* layout,
    int desktop_enabled,
    int mobile_enabled,
    int preferred_present,
    char const* preferred,
    char const* expected,
    char const* what)
{
    struct ToriRS_PluginEngine engine;
    struct ToriRS_PluginHost* host;
    int desktop;
    int mobile;

    memset(&g_engine, 0, sizeof(g_engine));
    g_screen_now = TORIRS_SCREEN_GAME;
    engine = fake_engine();
    host = PluginHost_New(&engine);
    desktop = PluginHost_Register(host, &LEGACY_DESKTOP_PROVIDER);
    mobile = PluginHost_Register(host, &LEGACY_MOBILE_PROVIDER);
    CHECK(desktop >= 0 && mobile >= 0, "legacy migration providers register");
    if( desktop_enabled )
        PluginHost_ConfigApply(host, "gameframe-layout", "enabled", "1");
    if( mobile_enabled )
        PluginHost_ConfigApply(host, "mobile-gameframe", "enabled", "1");
    if( layout )
        PluginHost_ConfigApply(host, "gameframe-layout", "layout", layout);
    g_engine.frame_preference_present = preferred_present;
    g_engine.frame_migration_version = 0;
    if( preferred )
        (void)snprintf(
            g_engine.frame_preference,
            sizeof(g_engine.frame_preference),
            "%s",
            preferred);

    PluginHost_Start(host);
    CHECK(
        strcmp(g_engine.frame_preference, expected) == 0 &&
            g_engine.frame_preference_present &&
            g_engine.frame_migration_version == 1,
        what);
    PluginHost_Free(host);
}

/* ------------------------------------------------------------------ tests */

static struct ToriRS_WidgetApi saved_widgets;
static struct ToriRS_WidgetActionRef saved_widget_action;
static int widget_requests, widget_resets;
static uint64_t widget_owner;
static struct ToriRS_WidgetRef watched_native = {{77,2,3}};
static struct ToriRS_WidgetRef const stale_control = {{77,9,1}};
static int img_slot=-1, img_w, img_h, img_sets, img_opacity=-1;
static char op_label[64];
static uint64_t op_registration;
static int op_requests;
static int anchor_relation,anchor_sets;
static int mask_slot,mask_sets;
static struct ToriRS_WidgetRef anchor_target;
static enum ToriRS_ContractResult fake_widget_request(void* user, uint64_t owner, struct PluginWidgetRequest* r)
{
    (void)user;
    widget_owner = owner;
    ++widget_requests;
    if( r->kind >= PLUGIN_WIDGET_POSITION && ToriRS_WidgetRefEqual(r->ref,stale_control) )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    if( r->kind == PLUGIN_WIDGET_VISIBLE && ToriRS_WidgetRefEqual(r->ref,stale_control) )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    if( r->kind == PLUGIN_WIDGET_SET_ON_OP )
    {
        ++op_requests;
        snprintf(op_label,sizeof(op_label),"%s",r->name);
        op_registration=r->registration;
    }
    if( r->kind == PLUGIN_WIDGET_CREATE_IMAGE ) *r->refs=(struct ToriRS_WidgetRef){{77,40,1}};
    if( r->kind == PLUGIN_WIDGET_SET_IMAGE ) { img_slot=r->id; img_w=r->a; img_h=r->b; ++img_sets; }
    if( r->kind == PLUGIN_WIDGET_OPACITY ) { img_opacity=r->a; }
    if( r->kind == PLUGIN_WIDGET_ANCHOR ) { anchor_relation=r->a; anchor_target=r->target; ++anchor_sets; }
    if( r->kind == PLUGIN_WIDGET_SET_MASK ) { mask_slot=r->id; ++mask_sets; }
    if( r->kind == PLUGIN_WIDGET_FIND ) *r->refs = watched_native;
    if( r->kind == PLUGIN_WIDGET_VISIBLE ) *r->flag=true;
    if( r->kind == PLUGIN_WIDGET_ACTIONS )
    {
        *r->count=1;
        if( r->capacity ) r->actions[0]=(struct ToriRS_WidgetAction){.ref={watched_native,2,99},.label="Activate"};
        else return TORIRS_CONTRACT_BUDGET_EXCEEDED;
    }
    if( r->kind == PLUGIN_WIDGET_RESET_OWNER ) ++widget_resets;
    return TORIRS_CONTRACT_OK;
}
static void widget_probe_draw(struct ToriRS_Api* api,void* state,struct ToriRS_Graphics* graphics)
{
    (void)state;(void)graphics;
    CHECK(api->widgets.invoke(api->widgets.context,saved_widget_action)==TORIRS_CONTRACT_WRONG_CONTEXT,
          "paint cannot invoke a native widget action");
}
static void widget_probe_start(struct ToriRS_Api* api, void* state)
{
    struct ToriRS_WidgetRef ref;
    (void)state;
    saved_widgets = api->widgets;
    CHECK(api->major_version == 3, "live widget runtime uses the breaking API major");
    int x=0,z=0;
    fake_scene_x=3184;fake_scene_z=3392;fake_scene_present=true;
    CHECK(api->world.scene_origin(api,&x,&z) && x==3184 && z==3392,
          "scene origin is readable immediately on plugin start");
    fake_scene_x=3192;fake_scene_z=3400;
    CHECK(api->world.scene_origin(api,&x,&z) && x==3192 && z==3400,
          "scene origin getter reads current native values without event replay");
    fake_scene_present=false;
    CHECK(!api->world.scene_origin(api,&x,&z),"unloaded world has no scene origin");
    fake_scene_present=true;
    CHECK(api->widgets.find(api->widgets.context, "sidebar", &ref) == TORIRS_CONTRACT_OK && ref.opaque[0] == 77,
          "live widget lookup routes through the current plugin host");
    CHECK(api->widgets.set_position(api->widgets.context, ref, 12, 34) == TORIRS_CONTRACT_OK,
          "live widget setter routes on the client callback");
    CHECK(api->widgets.revalidate(api->widgets.context, ref) == TORIRS_CONTRACT_OK,
          "live widget revalidation routes on the client callback");
    bool visible=false;size_t count=0;struct ToriRS_WidgetAction action;
    CHECK(api->widgets.visible(api->widgets.context,ref,&visible)==TORIRS_CONTRACT_OK && visible,
          "current widget visibility reaches the adapter");
    CHECK(api->widgets.actions(api->widgets.context,ref,&action,1,&count)==TORIRS_CONTRACT_OK && count==1 && !strcmp(action.label,"Activate"),
          "native action snapshot crosses the public C widget API");
    saved_widget_action=action.ref;
    CHECK(api->widgets.invoke(api->widgets.context,action.ref)==TORIRS_CONTRACT_OK,
          "checked native action dispatch reaches the adapter in an event context");
}

static void widget_probe_stop(struct ToriRS_Api* api, void* state)
{
    (void)state;
    struct ToriRS_WidgetRef out;
    CHECK(api->widgets.invoke(api->widgets.context,saved_widget_action)==TORIRS_CONTRACT_WRONG_CONTEXT,
          "shutdown cannot invoke native widget actions");
    CHECK(api->widgets.create_text(api->widgets.context,(struct ToriRS_WidgetRef){{77,2,3}},"late",&out)
              == TORIRS_CONTRACT_WRONG_CONTEXT,
          "shutdown cannot create new owned widgets");
}

/* Owned-control operations: registration, replacement, removal, budget,
 * reentry, self-disable and context rules through the public C API. */
static struct ToriRS_PluginHost* op_host;
static int op_index, op_calls, op_mode;
static uint64_t op_last_revision;
static struct ToriRS_WidgetRef const op_control={{77,5,9}};
static void op_listener(struct ToriRS_Api* api,void* user,struct ToriRS_WidgetEvent const* event)
{
    struct ToriRS_WidgetApi* ui=&api->widgets;
    (void)user;
    ++op_calls;
    op_last_revision=event->native_revision;
    CHECK(event->type==TORIRS_WIDGET_OPERATION && ToriRS_WidgetRefEqual(event->widget,op_control) && event->operation==1,
          "operation event names the pressed control");
    CHECK(ui->set_text(ui->context,op_control,"pressed")==TORIRS_CONTRACT_OK,
          "an operation callback may mutate widgets");
    CHECK(ui->invoke(ui->context,(struct ToriRS_WidgetActionRef){op_control,1,1})==TORIRS_CONTRACT_OK,
          "an operation callback may invoke checked native actions");
    int mode=op_mode;op_mode=0;
    if( mode==1 )
        CHECK(ui->set_on_op(ui->context,op_control,NULL,NULL,NULL)==TORIRS_CONTRACT_OK,
              "a listener can remove its own operation while handling it");
    if( mode==2 )
        PluginHost_SetEnabled(op_host,op_index,false);
    if( mode==3 )
        CHECK(ui->set_on_op(ui->context,op_control,"Again",op_listener,NULL)==TORIRS_CONTRACT_OK,
              "a listener can re-arm its control while handling it");
    if( mode==4 )
    {
        int ok=0;
        for( int i=0;i<126;++i )
            if( ui->set_on_op(ui->context,(struct ToriRS_WidgetRef){{77,200+(uint64_t)i,1}},"Row",op_listener,NULL)==TORIRS_CONTRACT_OK ) ++ok;
        CHECK(ok==126,"the per-owner budget admits 128 armed controls");
        CHECK(ui->set_on_op(ui->context,stale_control,"Row",op_listener,NULL)==TORIRS_CONTRACT_STALE_REFERENCE,
              "arming a vanished control reports the stale reference");
        CHECK(ui->set_on_op(ui->context,(struct ToriRS_WidgetRef){{77,900,1}},"Row",op_listener,NULL)==TORIRS_CONTRACT_OK,
              "the last free slot is granted");
        CHECK(ui->set_on_op(ui->context,(struct ToriRS_WidgetRef){{77,901,1}},"Row",op_listener,NULL)==TORIRS_CONTRACT_BUDGET_EXCEEDED,
              "a full table never evicts a live registration");
        CHECK(ui->set_on_op(ui->context,(struct ToriRS_WidgetRef){{77,902,1}},NULL,NULL,NULL)==TORIRS_CONTRACT_OK,
              "removing an operation that was never armed is a no-op");
        int before=op_requests;
        CHECK(ui->set_on_op(ui->context,(struct ToriRS_WidgetRef){{77,903,1}},NULL,NULL,NULL)==TORIRS_CONTRACT_OK && op_requests==before,
              "a no-op removal sends nothing to the adapter");
    }
}
static void op_start(struct ToriRS_Api* api,void* state)
{
    struct ToriRS_WidgetApi* ui=&api->widgets;
    char too_long[TORIRS_WIDGET_OP_LABEL_MAX+1];
    (void)state;
    memset(too_long,'x',sizeof(too_long)-1);too_long[sizeof(too_long)-1]=0;
    CHECK(ui->set_on_op(ui->context,op_control,"",op_listener,NULL)==TORIRS_CONTRACT_INVALID_ARGUMENT,
          "an empty operation label is rejected");
    CHECK(ui->set_on_op(ui->context,op_control,NULL,op_listener,NULL)==TORIRS_CONTRACT_INVALID_ARGUMENT,
          "a missing operation label is rejected");
    CHECK(ui->set_on_op(ui->context,op_control,too_long,op_listener,NULL)==TORIRS_CONTRACT_INVALID_ARGUMENT,
          "an over-long operation label is rejected");
    int before=op_requests;
    CHECK(ui->set_on_op(ui->context,op_control,"Press",op_listener,NULL)==TORIRS_CONTRACT_OK &&
          op_requests==before+1 && strcmp(op_label,"Press")==0 && op_registration!=0,
          "arming reaches the native adapter with its label and registration");
}
static void op_draw(struct ToriRS_Api* api,void* state,struct ToriRS_Graphics* graphics)
{
    (void)state;(void)graphics;
    CHECK(api->widgets.set_on_op(api->widgets.context,op_control,"Paint",op_listener,NULL)==TORIRS_CONTRACT_WRONG_CONTEXT,
          "paint cannot arm an owned control");
}
static void op_stop(struct ToriRS_Api* api,void* state)
{
    (void)state;
    CHECK(api->widgets.set_on_op(api->widgets.context,op_control,"Late",op_listener,NULL)==TORIRS_CONTRACT_WRONG_CONTEXT,
          "shutdown cannot arm an owned control");
    CHECK(api->widgets.set_on_op(api->widgets.context,op_control,NULL,NULL,NULL)==TORIRS_CONTRACT_OK,
          "shutdown removal is accepted as a no-op");
}
/* Owned image controls through the public C API: only this plugin's live
 * image tokens reach the adapter, and they reach it as validated slots. */
static struct ToriRS_WidgetRef img_control;
static struct ToriRS_ImageRef img_ref;
static void img_start(struct ToriRS_Api* api,void* state)
{
    struct ToriRS_WidgetApi* ui=&api->widgets;
    struct ToriRS_WidgetRef parent={{77,2,3}};
    uint32_t argb[4]={0xff102030,0xff405060,0xff708090,0xffa0b0c0};
    (void)state;
    CHECK(api->assets.image_compose(api,"camera",2,2,argb,&img_ref)==TORIRS_ASSET_READY && img_ref.value!=0,
          "a composed image is a live image token");
    CHECK(ui->create_image(ui->context,parent,"",&img_control)==TORIRS_CONTRACT_INVALID_ARGUMENT,"an owned image needs a key");
    CHECK(ui->create_image(ui->context,parent,"camera",&img_control)==TORIRS_CONTRACT_OK && img_control.opaque[1]==40,
          "owned image creation returns the adapter's child");
    CHECK(ui->set_image(ui->context,img_control,(struct ToriRS_ImageRef){0},2,2)==TORIRS_CONTRACT_INVALID_ARGUMENT,
          "a zero image token is rejected");
    CHECK(ui->set_image(ui->context,img_control,(struct ToriRS_ImageRef){img_ref.value+1000},2,2)==TORIRS_CONTRACT_INVALID_ARGUMENT,
          "an image token this plugin never received is rejected");
    CHECK(ui->set_image(ui->context,img_control,img_ref,5000,2)==TORIRS_CONTRACT_INVALID_ARGUMENT,"image control size is bounded");
    CHECK(ui->set_image(ui->context,img_control,img_ref,2,2)==TORIRS_CONTRACT_OK && img_sets==1 && img_slot>=0 && img_w==2 && img_h==2,
          "a live image reaches the adapter as a validated slot with the control size");
    CHECK(ui->set_opacity(ui->context,img_control,300)==TORIRS_CONTRACT_INVALID_ARGUMENT,"opacity is bounded");
    CHECK(ui->set_opacity(ui->context,img_control,85)==TORIRS_CONTRACT_OK && img_opacity==85,"opacity reaches the adapter");
    CHECK(ui->set_mask(ui->context,parent,(struct ToriRS_ImageRef){img_ref.value+1000})==TORIRS_CONTRACT_INVALID_ARGUMENT,
          "a mask token this plugin never received is rejected");
    CHECK(ui->set_mask(ui->context,parent,img_ref)==TORIRS_CONTRACT_OK && mask_sets==1 && mask_slot==img_slot,
          "a live mask reaches the adapter as its validated slot");
    CHECK(ui->set_mask(ui->context,parent,(struct ToriRS_ImageRef){0})==TORIRS_CONTRACT_OK && mask_sets==2 && mask_slot==-1,
          "an empty mask reference is the explicit unmask");
    CHECK(ui->set_anchor(ui->context,img_control,parent,(enum ToriRS_WidgetRelation)7)==TORIRS_CONTRACT_INVALID_ARGUMENT,
          "an unknown anchor relation is rejected");
    CHECK(ui->set_anchor(ui->context,img_control,(struct ToriRS_WidgetRef){{0,0,0}},TORIRS_WIDGET_RELATION_OVER)==TORIRS_CONTRACT_STALE_REFERENCE,
          "an empty anchor target is stale before reaching the adapter");
    CHECK(ui->set_anchor(ui->context,img_control,parent,TORIRS_WIDGET_RELATION_BEHIND)==TORIRS_CONTRACT_OK && anchor_sets==1 &&
          anchor_relation==TORIRS_WIDGET_RELATION_BEHIND && ToriRS_WidgetRefEqual(anchor_target,parent),
          "an anchor reaches the adapter with its target and relation");
    CHECK(ui->set_anchor(ui->context,img_control,(struct ToriRS_WidgetRef){{0,0,0}},TORIRS_WIDGET_RELATION_NATIVE)==TORIRS_CONTRACT_OK && anchor_sets==2,
          "clearing an anchor needs no target");
    api->assets.image_release(api,img_ref);
    CHECK(ui->set_image(ui->context,img_control,img_ref,2,2)==TORIRS_CONTRACT_INVALID_ARGUMENT,
          "a released image token can no longer be installed");
}
static void img_draw(struct ToriRS_Api* api,void* state,struct ToriRS_Graphics* graphics)
{
    (void)state;(void)graphics;
    CHECK(api->widgets.set_image(api->widgets.context,img_control,img_ref,2,2)==TORIRS_CONTRACT_WRONG_CONTEXT,
          "paint cannot change an owned image");
    CHECK(api->widgets.set_opacity(api->widgets.context,img_control,85)==TORIRS_CONTRACT_WRONG_CONTEXT,
          "paint cannot change owned opacity");
    CHECK(api->widgets.set_anchor(api->widgets.context,img_control,(struct ToriRS_WidgetRef){{77,2,3}},TORIRS_WIDGET_RELATION_OVER)==TORIRS_CONTRACT_WRONG_CONTEXT,
          "paint cannot anchor a widget");
    CHECK(api->widgets.set_mask(api->widgets.context,(struct ToriRS_WidgetRef){{77,2,3}},img_ref)==TORIRS_CONTRACT_WRONG_CONTEXT,
          "paint cannot re-skin a widget");
}
/* Frame provision through the widget API: every offer is served by
 * on_gameframe; the host takes only the chrome (frame_provide). */
static int gf_events,gf_active_last,gf_w,gf_h,gf_canvas,gf_widget_ok;
static int gf_safe_x,gf_safe_y,gf_safe_w,gf_safe_h;
static struct ToriRS_Api* gf_api;
static char gf_offer[TORIRS_PLUGIN_FRAME_LOCAL_ID_MAX];
static enum ToriRS_FrameBuildResult gf_answer=TORIRS_FRAME_READY;
static enum ToriRS_FrameBuildResult gf_on_gameframe(struct ToriRS_Api* api,void* state,struct ToriRS_GameframeEvent const* ev)
{
    struct ToriRS_WidgetRef ref;
    (void)state;gf_api=api;
    ++gf_events;gf_active_last=ev->active;gf_w=ev->width;gf_h=ev->height;gf_canvas=ev->canvas;
    gf_safe_x=ev->safe.x;gf_safe_y=ev->safe.y;gf_safe_w=ev->safe.width;gf_safe_h=ev->safe.height;
    snprintf(gf_offer,sizeof(gf_offer),"%s",ev->offer_id ? ev->offer_id : "");
    if( api->widgets.find(api->widgets.context,"viewport",&ref)==TORIRS_CONTRACT_OK &&
        api->widgets.set_position(api->widgets.context,ref,4,4)==TORIRS_CONTRACT_OK ) gf_widget_ok=1;
    if( ev->active && gf_answer!=TORIRS_FRAME_READY ) snprintf(ev->reason,ev->reason_capacity,"%s","No stones cut for this lane.");
    return ev->active ? gf_answer : TORIRS_FRAME_READY;
}
static struct ToriRS_FrameOffer const GF_OFFERS[]={
    {.struct_size=sizeof(struct ToriRS_FrameOffer),.id="event",.title="Event Frame",.canvas=TORIRS_FRAME_CANVAS_FIXED,.width=765,.height=503},
    {.struct_size=sizeof(struct ToriRS_FrameOffer)}};
static struct ToriRS_PluginDef const GF_PROVIDER={.struct_size=sizeof(GF_PROVIDER),.id="gf-provider",.title="Provider",.version="3.0.0",
    .frames=GF_OFFERS,.callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_gameframe=gf_on_gameframe}};
static struct ToriRS_FrameOffer const GF_BUILDERLESS[]={
    {.struct_size=sizeof(struct ToriRS_FrameOffer),.id="none",.title="No Handler",.canvas=TORIRS_FRAME_CANVAS_FIXED,.width=765,.height=503},
    {.struct_size=sizeof(struct ToriRS_FrameOffer)}};
static struct ToriRS_PluginDef const GF_NO_HANDLER={.struct_size=sizeof(GF_NO_HANDLER),.id="gf-none",.title="None",.version="3.0.0",
    .frames=GF_BUILDERLESS,.callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks)}};

/* ------------------------------------------------------------------------ */
/* Repair-pass regressions                                                   */
/*                                                                           */
/* Four host behaviours that each failed SILENTLY -- settings deleted by a   */
/* save, a mistyped colour drawn as black, art that stopped loading, a well  */
/* that drew off its own end -- so nothing above would have gone red for any */
/* of them.                                                                  */
/* ------------------------------------------------------------------------ */

static struct ToriRS_Api* g_pin_api;
static int g_pin_asset_refusals;
static int g_pin_asset_requests;
static uint32_t g_pin_colour;
static int g_pin_rows;
static int g_pin_beam;
static enum ToriRS_Result g_pin_height_exact;
static enum ToriRS_Result g_pin_height_over;

static struct ToriRS_ConfigItem const PIN_CONFIG_ITEMS[] = {
    { .key = "hull_colour", .label = "Hull", .type = TORIRS_CONFIG_COLOR,
      .default_value = "0x00ff00" },
    { .key = "rows", .label = "Rows", .type = TORIRS_CONFIG_INT, .default_value = "12" },
    { .key = "beam", .label = "Beam", .type = TORIRS_CONFIG_BOOL, .default_value = "true" },
    { .key = "shape", .label = "Shape", .type = TORIRS_CONFIG_ENUM,
      .default_value = "hull", .choices = "hull|tile" },
    { .key = "hotkey", .label = "Hotkey", .type = TORIRS_CONFIG_INT,
      .default_value = "0", .min = 0, .max = 52 },
    { 0 },
};
static struct ToriRS_ConfigSchema const PIN_CONFIG = {
    .struct_size = sizeof(PIN_CONFIG),
    .items = PIN_CONFIG_ITEMS,
};

static void
pin_start(
    struct ToriRS_Api* api,
    void* state)
{
    struct ToriRS_PanelDescriptor panel = { .preferred_width = 320 };

    (void)state;
    g_pin_api = api;
    CHECK(
        api->panel.request(api, &panel) == TORIRS_RESULT_OK,
        "the pin probe registers a page");
}

static void
pin_read_config(struct ToriRS_Api* api)
{
    uint32_t colour = 0xdeadbeef;
    int rows = -1;
    bool beam = false;

    if( api->config.get_color(api, "hull_colour", &colour) )
        g_pin_colour = colour;
    if( api->config.get_int(api, "rows", &rows) )
        g_pin_rows = rows;
    if( api->config.get_bool(api, "beam", &beam) )
        g_pin_beam = beam ? 1 : 0;
}

static void
pin_logic(
    struct ToriRS_Api* api,
    void* state,
    struct ToriRS_TickEvent const* event)
{
    (void)state;
    (void)event;
    pin_read_config(api);
}

static void
pin_ui_build(
    struct ToriRS_Api* api,
    void* state,
    struct ToriRS_PanelBuilder* panel,
    int view)
{
    (void)state;
    if( view != TORIRS_PANEL_VIEW_PAGE )
        return;
    panel->custom(panel, "well", TORIRS_PANEL_CUSTOM_HEIGHT_DEFAULT);
    g_pin_height_exact = api->panel.set_height(api, "well", 200);
    g_pin_height_over =
        api->panel.set_height(api, "well", TORIRS_PANEL_CUSTOM_HEIGHT_MAX + 400);
}

static struct ToriRS_PluginDef const PIN_PROBE = {
    .struct_size = sizeof(PIN_PROBE),
    .id = "pin-probe",
    .title = "Pin Probe",
    .version = "3.0.0",
    .config = &PIN_CONFIG,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = pin_start,
        .on_logic_tick = pin_logic,
        .on_ui_build = pin_ui_build,
    },
};

/* A second definition under a name the FIRST host does not carry, so the
 * carried-through section can be handed to a real plugin later. */
static struct ToriRS_PluginDef const PIN_LATE = {
    .struct_size = sizeof(PIN_LATE),
    .id = "absent-lua",
    .title = "Absent Lua",
    .version = "3.0.0",
    .config = &PIN_CONFIG,
    .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks) },
};

/* Asks for more files than the pre-repair ceiling of 128 held, from on_start,
 * the way a frame provider asks for its whole atlas. */
static void
pin_hungry_start(
    struct ToriRS_Api* api,
    void* state)
{
    char name[32];
    int i;

    (void)state;
    for( i = 0; i < 200; i++ )
    {
        snprintf(name, sizeof(name), "pin_%03d.png", i);
        g_pin_asset_requests++;
        if( api->assets.request(api, name) == TORIRS_ASSET_BUDGET )
            g_pin_asset_refusals++;
    }
}

static struct ToriRS_PluginDef const PIN_HUNGRY = {
    .struct_size = sizeof(PIN_HUNGRY),
    .id = "pin-hungry",
    .title = "Pin Hungry",
    .version = "3.0.0",
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = pin_hungry_start,
    },
};

static void
pin_image_roster_start(struct ToriRS_Api* api, void* state)
{
    uint32_t pixel = 0xff102030u;
    char name[32];
    (void)state;
    for( int i = 0; i < 110; i++ )
    {
        struct ToriRS_ImageRef image = { 0 };
        snprintf(name, sizeof(name), "composed-%d", i);
        CHECK(api->assets.image_compose(api, name, 1, 1, &pixel, &image) == TORIRS_ASSET_READY,
            "concurrent providers and remaining roster can hold their images");
        CHECK(image.value > 0, "an image in a high shared slot gets a valid owner token");
    }
}

static void
test_repair_pins(void)
{
    /* ---- (a) a save must not delete a plugin this run does not have ---- */
    {
        struct ToriRS_PluginEngine engine = fake_engine();
        struct ToriRS_PluginHost* host = PluginHost_New(&engine);
        static char const SAVED[] =
            "; torirs plugin settings\n"
            "[plugin:pin-probe]\nrows=3\n"
            "[plugin:absent-lua]\nhull_colour=0x112233\nrows=9\n"
            "[plugin:absent-c]\nenabled=0\n";
        void* encoded = NULL;
        int encoded_size = 0;
        int index;

        index = PluginHost_Register(host, &PIN_PROBE);
        CHECK(index == 0, "the pin probe registers");
        PluginHost_ConfigDecode(host, SAVED, (int)(sizeof(SAVED) - 1));
        CHECK(
            strcmp(PluginHost_ConfigGet(host, index, "rows"), "3") == 0,
            "a saved value for a registered plugin still lands on it");

        /* The save that used to do the damage: one change to one plugin. */
        CHECK(
            PluginHost_ConfigSet(host, index, "rows", "5"),
            "changing a setting on the loaded plugin succeeds");
        CHECK(
            PluginHost_ConfigEncode(host, &encoded, &encoded_size) && encoded,
            "the store encodes after the change");
        CHECK(
            strstr((char const*)encoded, "[plugin:pin-probe]") &&
                strstr((char const*)encoded, "rows=5"),
            "the changed plugin's own section is written");
        CHECK(
            strstr((char const*)encoded, "[plugin:absent-lua]") &&
                strstr((char const*)encoded, "hull_colour=0x112233") &&
                strstr((char const*)encoded, "rows=9"),
            "a section whose plugin never registered survives the rewrite whole");
        CHECK(
            strstr((char const*)encoded, "[plugin:absent-c]") &&
                strstr((char const*)encoded, "enabled=0"),
            "an absent plugin's enabled=0 survives the rewrite too");

        /* Round trip: re-reading what was written must not multiply or lose
         * the carried sections. */
        {
            struct ToriRS_PluginHost* second = PluginHost_New(&engine);
            void* again = NULL;
            int again_size = 0;
            char const* at;
            int sections = 0;

            PluginHost_ConfigDecode(second, encoded, encoded_size);
            CHECK(
                PluginHost_ConfigEncode(second, &again, &again_size) && again,
                "the carried sections encode again from a host that knows none of them");
            for( at = strstr((char const*)again, "[plugin:absent-lua]"); at;
                 at = strstr(at + 1, "[plugin:absent-lua]") )
                sections++;
            CHECK(sections == 1, "a carried section is written exactly once per round trip");
            CHECK(
                strstr((char const*)again, "[plugin:pin-probe]") &&
                    strstr((char const*)again, "rows=5"),
                "a section for a plugin the second host does not have is carried as well");
            free(again);
            PluginHost_Free(second);
        }
        free(encoded);
        PluginHost_Free(host);
    }

    /* A large unavailable roster must not turn preservation into another
     * fixed-capacity truncation. Update and replay also remain idempotent. */
    {
        struct ToriRS_PluginEngine engine = fake_engine();
        struct ToriRS_PluginHost* host = PluginHost_New(&engine);
        char name[32];
        void* encoded = NULL;
        int size = 0;
        for( int i = 0; i < 256; i++ )
        {
            snprintf(name, sizeof(name), "unavailable-%d", i);
            PluginHost_ConfigApply(host, name, "setting", "saved");
        }
        PluginHost_ConfigApply(host, "unavailable-255", "setting", "updated");
        CHECK(PluginHost_ConfigEncode(host, &encoded, &size), "a large unavailable roster encodes");
        for( int i = 0; i < 256; i++ )
        {
            snprintf(name, sizeof(name), "[plugin:unavailable-%d]", i);
            CHECK(strstr(encoded, name) != NULL, "every unavailable plugin survives beyond 128 rows");
        }
        CHECK(strstr(encoded, "setting=updated") != NULL, "a carried setting is updated in place");
        free(encoded);
        PluginHost_Free(host);
    }

    /* ---- (a2) the plugin that turns up late gets its saved values ------- */
    {
        struct ToriRS_PluginEngine engine = fake_engine();
        struct ToriRS_PluginHost* host = PluginHost_New(&engine);
        static char const SAVED[] = "[plugin:absent-lua]\nrows=9\n";
        void* encoded = NULL;
        int encoded_size = 0;
        char const* at;
        int sections = 0;
        int late;

        PluginHost_ConfigDecode(host, SAVED, (int)(sizeof(SAVED) - 1));
        late = PluginHost_Register(host, &PIN_LATE);
        CHECK(late >= 0, "a script that finishes loading after the settings file registers");
        CHECK(
            strcmp(PluginHost_ConfigGet(host, late, "rows"), "9") == 0,
            "a late registration adopts the values that were being held for its name");
        CHECK(
            PluginHost_ConfigEncode(host, &encoded, &encoded_size) && encoded,
            "the adopted store encodes");
        for( at = strstr((char const*)encoded, "[plugin:absent-lua]"); at;
             at = strstr(at + 1, "[plugin:absent-lua]") )
            sections++;
        CHECK(sections == 1, "an adopted plugin is written once, not once from each store");
        free(encoded);
        PluginHost_Free(host);
    }

    /* ---- (b) an unreadable number falls back to the DECLARED default --- */
    {
        struct ToriRS_PluginEngine engine = fake_engine();
        struct ToriRS_PluginHost* host = PluginHost_New(&engine);
        int index = PluginHost_Register(host, &PIN_PROBE);

        CHECK(index >= 0, "the pin probe registers for the config read");
        PluginHost_Start(host);
        PluginHost_LogicTick(host, 1);
        CHECK(g_pin_colour == 0x00ff00u, "a declared colour default reads back");
        CHECK(!PluginHost_ConfigSet(host, index, "shape", "hul"),
            "an enum write outside the declared choices is refused");
        CHECK(strcmp(PluginHost_ConfigGet(host, index, "shape"), "hull") == 0,
            "a refused enum write preserves the previous setting");
        CHECK(PluginHost_ConfigSet(host, index, "shape", "tile"),
            "a declared enum choice remains writable");

        CHECK(
            !PluginHost_ConfigSet(host, index, "hull_colour", "cyan"),
            "a mistyped colour is refused without saving it");
        PluginHost_ConfigApply(host, "pin-probe", "hull_colour", "cyan");
        g_pin_colour = 0xdeadbeefu;
        PluginHost_LogicTick(host, 1);
        CHECK(
            g_pin_colour == 0x00ff00u,
            "a colour that will not parse reads as the plugin's declared default, not black");

        CHECK(
            !PluginHost_ConfigSet(host, index, "rows", "12 rows"),
            "a trailing word in a number write is refused");
        PluginHost_ConfigApply(host, "pin-probe", "rows", "12 rows");
        g_pin_rows = -1;
        PluginHost_LogicTick(host, 1);
        CHECK(g_pin_rows == 12, "an unparseable int reads as the declared default");

        /* The bool spellings must survive the fallback: `false` is not a
         * number, and falling back on it would turn a default-on setting
         * back on every time somebody switched it off by hand. */
        CHECK(
            PluginHost_ConfigSet(host, index, "beam", "false"),
            "a bool can be spelled out");
        g_pin_beam = -1;
        PluginHost_LogicTick(host, 1);
        CHECK(g_pin_beam == 0, "'false' switches a default-on bool off");
        CHECK(!PluginHost_ConfigGetBool(host, index, "beam"),
            "the settings panel reads the same false spelling as the plugin");
        {
            char const* const spellings[] = { "true", "yes", "on", "1 << 4" };
            for( unsigned i = 0; i < sizeof(spellings) / sizeof(spellings[0]); i++ )
            {
                CHECK(PluginHost_ConfigSet(host, index, "beam", spellings[i]),
                    "a supported true spelling is writable");
                PluginHost_LogicTick(host, 1);
                CHECK(g_pin_beam == 1 && PluginHost_ConfigGetBool(host, index, "beam"),
                    "the settings panel and plugin agree on every true spelling");
            }
        }
        CHECK(
            !PluginHost_ConfigSet(host, index, "beam", "nonsense"),
            "a bool write refuses a typo");
        PluginHost_ConfigApply(host, "pin-probe", "beam", "nonsense");
        g_pin_beam = -1;
        PluginHost_LogicTick(host, 1);
        CHECK(g_pin_beam == 1, "an unparseable bool reads as the declared default");
        CHECK(PluginHost_ConfigGetBool(host, index, "beam"),
            "the settings panel uses the same declared boolean fallback");
        CHECK(!PluginHost_ConfigSet(host, index, "hotkey", "112"),
            "an unreachable screenshot key is refused at the host boundary");
        CHECK(strcmp(PluginHost_ConfigGet(host, index, "hotkey"), "0") == 0,
            "a refused key leaves the saved preference unchanged");
        CHECK(PluginHost_ConfigSet(host, index, "hotkey", "16"),
            "a reachable screenshot key is accepted");
        PluginHost_Free(host);
    }

    /* A runtime may remove or reorder settings while rebuilding on reload. */
    {
        struct ToriRS_ConfigItem items[] = {
            { "keep", TORIRS_CONFIG_INT, "Keep", "1", 0, 99, NULL, 0 },
            { "removed", TORIRS_CONFIG_STRING, "Removed", "seed", 0, 0, NULL, 0 },
            { "moved", TORIRS_CONFIG_BOOL, "Moved", "true", 0, 0, NULL, 0 },
            { NULL, TORIRS_CONFIG_BOOL, NULL, NULL, 0, 0, NULL, 0 }
        };
        struct ToriRS_ConfigItem const replacement[] = {
            { "moved", TORIRS_CONFIG_BOOL, "Moved", "true", 0, 0, NULL, 0 },
            { "keep", TORIRS_CONFIG_INT, "Keep", "1", 0, 99, NULL, 0 },
            { "added", TORIRS_CONFIG_STRING, "Added", "new default", 0, 0, NULL, 0 },
            { NULL, TORIRS_CONFIG_BOOL, NULL, NULL, 0, 0, NULL, 0 }
        };
        struct ToriRS_ConfigSchema schema = {
            .struct_size = sizeof(schema), .items = items
        };
        struct ToriRS_PluginDef def = {
            .struct_size = sizeof(def), .id = "reload-schema", .title = "Reload schema",
            .version = "1", .config = &schema,
            .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks) }
        };
        struct ToriRS_PluginEngine engine = fake_engine();
        struct ToriRS_PluginHost* host = PluginHost_New(&engine);
        int index = PluginHost_Register(host, &def);
        void* encoded = NULL;
        int encoded_size = 0;
        CHECK(index >= 0, "a mutable runtime schema registers");
        PluginHost_Start(host);
        CHECK(PluginHost_ConfigSet(host, index, "removed", "legacy before"),
            "the original string key is writable");
        CHECK(PluginHost_ConfigSet(host, index, "keep", "9"),
            "the retained int has a non-default value");
        memcpy(items, replacement, sizeof(items));
        PluginHost_Reload(host, index);
        CHECK(PluginHost_ConfigSet(host, index, "removed", "legacy after"),
            "a removed key remains writable without its old row's new int constraint");
        CHECK(strcmp(PluginHost_ConfigGet(host, index, "keep"), "9") == 0,
            "reload preserves a reordered key's value");
        CHECK(!PluginHost_ConfigSet(host, index, "keep", "100"),
            "the reordered int keeps its own range constraint");
        CHECK(PluginHost_ConfigGetBool(host, index, "moved"),
            "the reordered bool keeps its value");
        CHECK(strcmp(PluginHost_ConfigGet(host, index, "added"), "new default") == 0,
            "reload seeds only the newly declared key");
        items[0] = (struct ToriRS_ConfigItem){0};
        PluginHost_Reload(host, index);
        CHECK(PluginHost_ConfigSet(host, index, "removed", "past terminator"),
            "an empty replacement schema leaves old keys unclaimed");
        CHECK(PluginHost_ConfigEncode(host, &encoded, &encoded_size),
            "old keys still encode after the replacement schema becomes empty");
        CHECK(strstr((char const*)encoded, "removed=past terminator") != NULL,
            "removed settings remain preserved in the saved store");
        free(encoded);
        PluginHost_Free(host);
    }

    /* ---- (c) the shared asset table seats the shipped roster ----------- */
    {
        struct ToriRS_PluginEngine engine = fake_engine();
        struct ToriRS_PluginHost* host = PluginHost_New(&engine);

        g_pin_asset_requests = 0;
        g_pin_asset_refusals = 0;
        CHECK(PluginHost_Register(host, &PIN_HUNGRY) >= 0, "the hungry probe registers");
        PluginHost_Start(host);
        CHECK(g_pin_asset_requests == 200, "the probe asked for every file it ships");
        CHECK(
            g_pin_asset_refusals == 0,
            "a frame provider's whole atlas fits the shared asset table");
        PluginHost_Free(host);
    }

    {
        struct ToriRS_PluginEngine engine = fake_engine();
        struct ToriRS_PluginHost* host = PluginHost_New(&engine);
        struct ToriRS_PluginDef defs[3];
        char const* names[] = { "frame-old", "frame-loading", "other-plugins" };
        int const releases = g_engine.image_releases;
        for( int i = 0; i < 3; i++ )
        {
            defs[i] = PIN_HUNGRY;
            defs[i].id = names[i];
            defs[i].callbacks.on_start = pin_image_roster_start;
            CHECK(PluginHost_Register(host, &defs[i]) >= 0, "a concurrent image owner registers");
        }
        PluginHost_Start(host);
        CHECK(g_composed.slot >= 192, "image composition reaches beyond the previous shared ceiling");
        PluginHost_Free(host);
        CHECK(g_engine.image_releases - releases == 330, "teardown releases all three owners' images");
    }

    /* ---- (d) a clamped well height is reported, not answered OK -------- */
    {
        struct ToriRS_PluginEngine engine = fake_engine();
        struct ToriRS_PluginHost* host = PluginHost_New(&engine);
        int index = PluginHost_Register(host, &PIN_PROBE);
        uint32_t generation;
        struct ToriRS_PanelWidget const* widget;

        CHECK(index >= 0, "the pin probe registers for the panel");
        PluginHost_Start(host);
        CHECK(PluginHost_PanelSelect(host, index), "the pin probe's page can be selected");
        generation = PluginHost_PanelSelectionGeneration(host);
        CHECK(
            PluginHost_PanelWidgetCount(host, generation) == 1,
            "the page carries its one custom well");
        CHECK(
            g_pin_height_exact == TORIRS_RESULT_OK,
            "a height inside the bound is answered OK");
        CHECK(
            g_pin_height_over != TORIRS_RESULT_OK,
            "a height past the bound is NOT answered OK -- the plugin can page or shrink");
        widget = PluginHost_PanelWidgetAt(host, generation, 0);
        CHECK(
            widget && widget->preferred_height == TORIRS_PANEL_CUSTOM_HEIGHT_MAX,
            "the clamped height is still recorded, so the well is bounded rather than stale");
        PluginHost_Free(host);
    }
}

static void test_gameframe_provider(void)
{
    struct ToriRS_PluginEngine engine;
    struct ToriRS_PluginHost* host;
    struct ToriRS_FrameSelection selection={.struct_size=sizeof(selection)};
    memset(&g_engine,0,sizeof(g_engine));
    g_screen_now=TORIRS_SCREEN_GAME;
    engine=fake_engine();engine.widget_request=fake_widget_request;
    host=PluginHost_New(&engine);
    CHECK(PluginHost_Register(host,&GF_NO_HANDLER)<0,"an offer without on_gameframe is refused");
    CHECK(PluginHost_Register(host,&GF_PROVIDER)>=0,"an event-driven frame provider registers");
    g_engine.frame_preference_present=1;g_engine.frame_migration_version=1;
    snprintf(g_engine.frame_preference,sizeof(g_engine.frame_preference),"%s","gf-provider/event");
    gf_events=0;gf_answer=TORIRS_FRAME_READY;gf_widget_ok=0;frame_provide_owner=0;widget_owner=0;
    PluginHost_Start(host);
    PluginHost_Layout(host,900,600);
    gf_api->frame.selection(gf_api,&selection);
    CHECK(frame_provide_owner!=0 && frame_provide_owner==widget_owner,
          "the publication carries the providing plugin's owner id, the one its widget requests used");
    printf("GAMEFRAME_PROVIDER events=%d active=%d %dx%d canvas=%d offer=%s provides=%d status=%d id=%s\n",
           gf_events,gf_active_last,gf_w,gf_h,gf_canvas,gf_offer,g_engine.frame_provides,selection.status,selection.active_id);
    CHECK(gf_events==1 && gf_active_last==1 && gf_w==765 && gf_h==503 && gf_canvas==TORIRS_FRAME_CANVAS_FIXED && strcmp(gf_offer,"event")==0,
          "the provider hears its offer activate against the pinned fixed canvas");
    CHECK(gf_widget_ok==1,"the provider event may find and move widgets");
    CHECK(g_engine.frame_provides==1 && g_engine.frame_active==1 &&
          g_engine.layout_canvas==TORIRS_FRAME_CANVAS_FIXED && g_engine.layout_fixed_w==765,
          "a READY provider publishes chrome suppression and its canvas policy through frame_provide");
    CHECK(selection.status==TORIRS_FRAME_STATUS_ACTIVE && strcmp(selection.active_id,"gf-provider/event")==0,
          "the provided offer is the active frame");
    {
        struct ToriRS_FrameOfferInfo offer={.struct_size=sizeof(offer)};
        CHECK(gf_api->frame.offer_next(gf_api,-1,&offer)==0 && strcmp(offer.id,"gf-provider/event")==0,
              "the catalogue iterator starts at the provider's own offer");
        CHECK(gf_api->frame.offer_next(gf_api,INT_MAX,&offer)==-1,
              "the public iterator rejects INT_MAX without signed overflow");
    }
    /* A canvas change asks again. */
    PluginHost_Layout(host,900,600);
    CHECK(gf_events==2 && g_engine.frame_provides==2,"every layout pass re-asks the provider");
    /* The platform band. With none reported the event's safe rect is the
     * canvas; a band that comes up re-asks the provider once, through the
     * frame boundary, carrying the engine's rect. */
    CHECK(gf_safe_x==0 && gf_safe_y==0 && gf_safe_w==765 && gf_safe_h==503,
          "without a platform band the event's safe rect is the whole canvas");
    PluginHost_FrameStart(host,1,0);
    CHECK(!PluginHost_FrameNeedsLayout(host),"an unchanged band requests nothing");
    g_engine.safe_present=1;g_engine.safe_x=0;g_engine.safe_y=0;g_engine.safe_w=765;g_engine.safe_h=300;
    PluginHost_FrameStart(host,2,0);
    CHECK(PluginHost_FrameNeedsLayout(host),"a band that moved requests a re-ask at the frame boundary");
    PluginHost_Layout(host,900,600);
    CHECK(gf_events==3 && gf_active_last==1 && gf_safe_x==0 && gf_safe_y==0 && gf_safe_w==765 && gf_safe_h==300,
          "the re-ask carries the engine's safe rect");
    CHECK(!PluginHost_FrameNeedsLayout(host) && g_engine.frame_active==1,"the re-ask consumed its request");
    PluginHost_FrameStart(host,3,0);
    CHECK(!PluginHost_FrameNeedsLayout(host) && gf_events==3,"the same band re-asks exactly once");
    g_engine.safe_present=0;
    PluginHost_FrameStart(host,4,0);
    CHECK(PluginHost_FrameNeedsLayout(host),"the band going away is a move too");
    PluginHost_Layout(host,900,600);
    CHECK(gf_events==4 && gf_safe_w==765 && gf_safe_h==503,"the re-ask carries the whole canvas again");
    /* PENDING keeps the request standing so the next fence asks again. */
    gf_answer=TORIRS_FRAME_PENDING;
    PluginHost_Layout(host,900,600);
    gf_api->frame.selection(gf_api,&selection);
    CHECK(gf_events==6 && gf_active_last==0 && PluginHost_FrameNeedsLayout(host) &&
          selection.status==TORIRS_FRAME_STATUS_LOADING && g_engine.frame_active==0,
          "PENDING releases the standing provision, keeps native up and asks again next fence");
    gf_answer=TORIRS_FRAME_READY;
    PluginHost_Layout(host,900,600);
    gf_api->frame.selection(gf_api,&selection);
    CHECK(gf_events==7 && selection.status==TORIRS_FRAME_STATUS_ACTIVE && !PluginHost_FrameNeedsLayout(host) &&
          g_engine.frame_active==1,
          "the next READY answer provides the frame again and the request is consumed");
    gf_answer=TORIRS_FRAME_UNSUPPORTED;
    /* Declining with a reason falls back to native with that reason. */
    PluginHost_Layout(host,900,600);
    gf_api->frame.selection(gf_api,&selection);
    CHECK(gf_events==9 && gf_active_last==0,"an unsupported answer releases the provider, which hears the release");
    CHECK(selection.status==TORIRS_FRAME_STATUS_FALLBACK && strcmp(selection.reason,"No stones cut for this lane.")==0 &&
          g_engine.frame_active==0,
          "UNSUPPORTED falls back to native carrying the provider's reason");
    PluginHost_Free(host);
}
static void test_widget_images(void)
{
    struct ToriRS_PluginEngine engine=fake_engine();engine.widget_request=fake_widget_request;
    struct ToriRS_PluginHost* host=PluginHost_New(&engine);img_sets=0;img_slot=-1;img_opacity=-1;
    struct ToriRS_PluginDef def={.struct_size=sizeof(def),.id="img-plugin",.title="Img",.version="3",
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_start=img_start,.on_draw_canvas=img_draw}};
    CHECK(PluginHost_Register(host,&def)>=0,"image fixture registers");
    PluginHost_Start(host);
    PluginHost_DrawCanvas(host,765,503);
    CHECK(img_sets==1,"paint attempts never reached the adapter");
    PluginHost_Free(host);
}

static void test_widget_operations(void)
{
    struct ToriRS_PluginEngine engine=fake_engine();engine.widget_request=fake_widget_request;
    op_host=PluginHost_New(&engine);op_calls=0;op_mode=0;op_requests=0;op_label[0]=0;op_registration=0;
    struct ToriRS_PluginDef def={.struct_size=sizeof(def),.id="op-plugin",.title="Op",.version="3",
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_start=op_start,.on_stop=op_stop,.on_draw_canvas=op_draw}};
    struct ToriRS_PluginDef other=def;other.id="op-other";other.title="Other";other.callbacks.on_start=NULL;other.callbacks.on_stop=NULL;other.callbacks.on_draw_canvas=NULL;
    op_index=PluginHost_Register(op_host,&def);
    int other_index=PluginHost_Register(op_host,&other);
    CHECK(op_index>=0 && other_index>=0,"operation fixtures register");
    PluginHost_Start(op_host);
    uint64_t owner=(uint64_t)op_index+1,serial=op_registration;
    CHECK(serial!=0,"startup armed the control");
    CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,0),"a zero registration never dispatches");
    CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,serial+1),"a mismatched registration is rejected");
    CHECK(!PluginHost_WidgetOperation(op_host,owner,(struct ToriRS_WidgetRef){{77,5,10}},serial),"a different widget incarnation is rejected");
    CHECK(!PluginHost_WidgetOperation(op_host,(uint64_t)other_index+1,op_control,serial),"another plugin's registration cannot be dispatched to a foreign owner");
    CHECK(!PluginHost_WidgetOperation(op_host,owner+7,op_control,serial),"an unknown owner is rejected");
    CHECK(op_calls==0,"rejected dispatches reach no listener");
    CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,serial) && op_calls==1 && op_last_revision==serial,
          "a current registration dispatches to the owner's listener");
    PluginHost_DrawCanvas(op_host,765,503);
    op_mode=3;
    CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,serial) && op_calls==2,"dispatch during which the listener re-arms");
    uint64_t replaced=op_registration;
    CHECK(replaced!=serial && replaced!=0 && strcmp(op_label,"Again")==0,"re-arming issues a fresh registration with the new label");
    CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,serial),"a retired registration no longer dispatches");
    CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,replaced) && op_calls==3,"the replacement registration dispatches");
    op_mode=1;
    CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,replaced) && op_calls==4,"dispatch during which the listener removes itself");
    CHECK(op_registration==0 && op_label[0]==0,"removal clears the adapter operation");
    CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,replaced) && op_calls==4,"a removed registration no longer dispatches");
    PluginHost_SetEnabled(op_host,op_index,false);
    CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,replaced),"a disabled plugin receives no operations");
    PluginHost_SetEnabled(op_host,op_index,true);
    uint64_t fresh=op_registration;
    CHECK(fresh!=0 && fresh!=replaced && PluginHost_WidgetOperation(op_host,owner,op_control,fresh) && op_calls==5,
          "restart re-arms with a new registration");
    op_mode=4;
    CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,fresh) && op_calls==6,"budget probe dispatched");
    op_mode=2;
    CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,fresh) && op_calls==7,"a listener may disable its own plugin");
    CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,fresh) && op_calls==7,"nothing dispatches after self-disable");
    PluginHost_Free(op_host);
}

static struct ToriRS_PluginHost* watched_host;
static int watched_b, watch_churn, watch_self_remove;
static char watch_trace[128];
static void record_watch(struct ToriRS_Api* api, void* user, struct ToriRS_WidgetEvent const* event)
{
    char who = (char)(intptr_t)user;
    size_t n = strlen(watch_trace);
    watch_trace[n] = who;
    watch_trace[n+1] = event->type == TORIRS_WIDGET_BOUND ? '+' : '-';
    watch_trace[n+2] = 0;
    CHECK(strcmp(event->role,"sidebar") == 0, "binding event identifies its subscription");
    if( who == 'A' && watch_churn && event->type == TORIRS_WIDGET_BOUND )
    {
        watch_churn = 0;
        struct ToriRS_WidgetBounds box;
        PluginHost_SetEnabled(watched_host, watched_b, false);
        CHECK(api->widgets.position(api->widgets.context,event->widget,&box) == TORIRS_CONTRACT_OK,
              "nested disable preserves the outer widget callback context");
        PluginHost_SetEnabled(watched_host, watched_b, true);
        CHECK(api->widgets.position(api->widgets.context,event->widget,&box) == TORIRS_CONTRACT_OK,
              "nested enable preserves the outer widget callback context");
    }
    if( who == 'A' && watch_self_remove && event->type == TORIRS_WIDGET_UNBOUND )
    {
        watch_self_remove = 0;
        CHECK(api->widgets.watch(api->widgets.context,"sidebar",NULL,NULL) == TORIRS_CONTRACT_OK,
              "watch callback can unsubscribe itself");
    }
}
static void watch_start(struct ToriRS_Api* api, void* state)
{
    (void)state;
    char who = strcmp(api->core.plugin_id(api),"watch-a") == 0 ? 'A' : 'B';
    CHECK(api->widgets.watch(api->widgets.context,"sidebar",record_watch,(void*)(intptr_t)who) == TORIRS_CONTRACT_OK,
          "widget binding subscription registers during startup");
}

static int tree_events, tree_churn;
static void tree_listener(struct ToriRS_Api* api,void* user,struct ToriRS_WidgetEvent const* event)
{
    (void)user;
    CHECK(event->type==TORIRS_WIDGET_TREE_CHANGED && !event->widget.opaque[2],
        "tree event requests live queries instead of fabricating widget identity");
    ++tree_events;
    if( tree_churn )
    {
        tree_churn=0;
        PluginHost_SetEnabled(watched_host,watched_b,false);
        PluginHost_SetEnabled(watched_host,watched_b,true);
    }
    struct ToriRS_WidgetRef refs[2];size_t count=0;
    CHECK(api->widgets.find_all(api->widgets.context,"sidebar",refs,2,&count)==TORIRS_CONTRACT_OK,
        "tree notification has a valid query context after nested lifecycle changes");
}
static void tree_watch_start(struct ToriRS_Api* api,void* state)
{
    (void)state;
    CHECK(api->widgets.watch_tree(api->widgets.context,tree_listener,NULL)==TORIRS_CONTRACT_OK,
        "tree subscription registers from startup");
}

static struct ToriRS_PluginHost* script_test_host;
static struct ToriRS_Api* script_test_api;
static struct ToriRS_ScriptRef script_test_retained;
static int script_test_b,script_test_calls,script_test_churn;
static int script_test_ints[2];
static char script_test_string[64];
static int script_test_capability(void* u,char const* name)
{ (void)u;return strcmp(name,"scripts.callbacks")==0; }
static int32_t script_test_get_int(void* u,size_t index)
{ (void)u;return script_test_ints[1-index]; }
static void script_test_set_int(void* u,size_t index,int32_t value)
{ (void)u;script_test_ints[1-index]=value; }
static char const* script_test_get_string(void* u,size_t index)
{ (void)u;(void)index;return script_test_string; }
static bool script_test_set_string(void* u,size_t index,char const* value)
{ (void)u;(void)index;snprintf(script_test_string,sizeof(script_test_string),"%s",value);return true; }
static void script_test_start(struct ToriRS_Api* api,void* state)
{
    (void)state;
    CHECK(api->scripts.available(api->scripts.context),"script callback capability reaches the adapter");
    int32_t value;
    CHECK(api->scripts.get_int(api->scripts.context,script_test_retained,0,&value)==TORIRS_CONTRACT_WRONG_CONTEXT,
        "nested startup cannot borrow another callback's script context");
}
static void script_test_callback(struct ToriRS_Api* api,void* state,struct ToriRS_ScriptEvent const* event)
{
    (void)state;++script_test_calls;script_test_api=api;
    struct ToriRS_WidgetActionRef action={{{77,2,3}},1,1};
    CHECK(api->widgets.invoke(api->widgets.context,action)==TORIRS_CONTRACT_WRONG_CONTEXT,
          "synchronous script callback cannot invoke native widget actions");
    CHECK(strcmp(event->name,"caption")==0 && event->script_id==99,"script event identifies native execution");
    if( script_test_retained.token && script_test_retained.token!=event->ref.token )
    {
        int32_t value;
        CHECK(api->scripts.get_int(api->scripts.context,script_test_retained,0,&value)==TORIRS_CONTRACT_STALE_REFERENCE,
            "a previous script callback reference cannot address this callback");
    }
    script_test_retained=event->ref;
    size_t ints=0,strings=0,required=0;
    CHECK(api->scripts.counts(api->scripts.context,event->ref,&ints,&strings)==TORIRS_CONTRACT_OK && ints==2 && strings==1,
        "callback sees remaining native stack sizes");
    int32_t value;
    CHECK(api->scripts.get_int(api->scripts.context,event->ref,1,&value)==TORIRS_CONTRACT_OK && value==1127,
        "script stack indices are checked and top-relative");
    CHECK(api->scripts.set_int(api->scripts.context,event->ref,2,9)==TORIRS_CONTRACT_INVALID_ARGUMENT,
        "callback cannot write beyond its native stack");
    CHECK(api->scripts.set_int(api->scripts.context,event->ref,1,9)==TORIRS_CONTRACT_NATIVE_BLOCKED,
        "script callbacks cannot overwrite read-only native arguments");
    char text[64];
    CHECK(api->scripts.get_string(api->scripts.context,event->ref,0,text,sizeof(text),&required)==TORIRS_CONTRACT_OK,
        "callback copies native strings without exposing VM memory");
    bool a=strcmp(api->core.plugin_id(api),"script-a")==0;
    CHECK(strcmp(text,a ? "native" : "A")==0,"script callbacks observe earlier callback writes in order");
    CHECK(api->scripts.set_string(api->scripts.context,event->ref,0,a ? "A" : "AB")==TORIRS_CONTRACT_OK,
        "script callback writes a native result");
    if( a && script_test_churn )
    {
        script_test_churn=0;
        PluginHost_SetEnabled(script_test_host,script_test_b,false);
        PluginHost_SetEnabled(script_test_host,script_test_b,true);
    }
}
static void test_script_callbacks(void)
{
    struct ToriRS_PluginEngine engine=fake_engine();engine.capability=script_test_capability;
    script_test_host=PluginHost_New(&engine);script_test_calls=0;script_test_retained.token=0;
    struct ToriRS_PluginDef a={.struct_size=sizeof(a),.id="script-a",.title="A",.version="3",
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_start=script_test_start,.on_script_callback=script_test_callback}};
    struct ToriRS_PluginDef b=a;b.id="script-b";b.title="B";
    script_test_b=PluginHost_Register(script_test_host,&b);
    CHECK(script_test_b>=0 && PluginHost_Register(script_test_host,&a)>=0,"script callback fixtures register");
    PluginHost_Start(script_test_host);
    struct PluginScriptStack stack={.int_count=2,.string_count=1,.writable_ints=1,.writable_strings=1,.get_int=script_test_get_int,
        .set_int=script_test_set_int,.get_string=script_test_get_string,.set_string=script_test_set_string};
    script_test_ints[0]=1127;script_test_ints[1]=7;strcpy(script_test_string,"native");
    PluginHost_ScriptCallback(script_test_host,"caption",99,&stack);
    CHECK(script_test_calls==2 && strcmp(script_test_string,"AB")==0,"callbacks finish before native execution resumes");
    int32_t value;
    CHECK(script_test_api->scripts.get_int(script_test_api->scripts.context,script_test_retained,0,&value)==TORIRS_CONTRACT_WRONG_CONTEXT,
        "script access is revoked immediately after dispatch");
    strcpy(script_test_string,"native");script_test_churn=1;
    PluginHost_ScriptCallback(script_test_host,"caption",99,&stack);
    CHECK(script_test_calls==3 && strcmp(script_test_string,"A")==0,"restarted plugin cannot join an active script callback dispatch");
    strcpy(script_test_string,"native");
    PluginHost_ScriptCallback(script_test_host,"caption",99,&stack);
    CHECK(script_test_calls==5,"restarted script plugin receives the next callback");
    PluginHost_Free(script_test_host);
}

int
main(void)
{
    struct ToriRS_PluginEngine engine;

    /* ---- pre-V2 frame preference migration ------------------------------ */
    check_legacy_frame_migration(
        "Auto", 1, 0, 0, NULL, "auto",
        "legacy desktop Auto label migrates to automatic selection");
    check_legacy_frame_migration(
        "3", 1, 0, 0, NULL, "auto",
        "legacy desktop Auto numeric value migrates to automatic selection");
    check_legacy_frame_migration(
        "Classic Fixed", 1, 0, 0, NULL,
        "gameframe-layout/classic-fixed",
        "legacy desktop Classic Fixed label migrates to its stable offer id");
    check_legacy_frame_migration(
        "0", 1, 0, 0, NULL, "gameframe-layout/classic-fixed",
        "legacy desktop Classic Fixed numeric value migrates to its stable offer id");
    check_legacy_frame_migration(
        "Modern Fixed", 1, 0, 0, NULL,
        "gameframe-layout/modern-fixed",
        "legacy desktop Modern Fixed label migrates to its stable offer id");
    check_legacy_frame_migration(
        "1", 1, 0, 0, NULL, "gameframe-layout/modern-fixed",
        "legacy desktop Modern Fixed numeric value migrates to its stable offer id");
    check_legacy_frame_migration(
        "Modern Resizable", 1, 0, 0, NULL,
        "gameframe-layout/modern-resizable",
        "legacy desktop Modern Resizable label migrates to its stable offer id");
    check_legacy_frame_migration(
        "2", 1, 0, 0, NULL, "gameframe-layout/modern-resizable",
        "legacy desktop Modern Resizable numeric value migrates to its stable offer id");
    check_legacy_frame_migration(
        "Not a frame", 1, 0, 0, NULL, "auto",
        "invalid legacy desktop selection migrates to automatic selection");
    check_legacy_frame_migration(
        NULL, 0, 1, 0, NULL, "mobile-gameframe/stone-drawer",
        "legacy mobile-only selection migrates to Stone Drawer");
    check_legacy_frame_migration(
        "Modern Fixed", 1, 1, 0, NULL,
        "gameframe-layout/modern-fixed",
        "legacy desktop selection takes precedence when both frame plugins were enabled");
    check_legacy_frame_migration(
        NULL, 0, 0, 0, NULL, "auto",
        "absent legacy frame selection migrates to automatic selection");
    check_legacy_frame_migration(
        "Modern Fixed", 1, 1, 1, "INVALID SAVED VALUE", "auto",
        "invalid explicit preferred frame migrates to auto instead of legacy settings");

    /* ---- V2 registration and lifecycle ---------------------------------- */
    {
        struct ToriRS_PluginHost* hv2;
        struct ToriRS_FrameSelection selection;
        struct ToriRS_PanelWidget const* widget;
        uint32_t generation;
        int draw_before;
        int a2;
        int b2;
        int frame2;
        int prefix2;

        memset(&g_engine, 0, sizeof(g_engine));
        memset(g_v2_first_state, 0, sizeof(g_v2_first_state));
        memset(g_v2_latest_state, 0, sizeof(g_v2_latest_state));
        memset(g_v2_api, 0, sizeof(g_v2_api));
        memset(g_v2_starts, 0, sizeof(g_v2_starts));
        memset(g_v2_stops, 0, sizeof(g_v2_stops));
        g_v2_zeroed_starts = 0;
        g_v2_typed_calls = 0;
        g_v2_panel_builds = 0;
        g_v2_panel_actions = 0;
        g_v2_select_actions = 0;
        g_v2_select_value[0] = '\0';
        g_v2_panel_draws = 0;
        g_v2_frame_provisions = 0;
        g_v2_prefix_starts = 0;
        g_v2_started_with_saved_config = 0;
        g_screen_now = TORIRS_SCREEN_GAME;
        g_lane_game = TORIRS_GAME_RS2;
        engine = fake_engine();
        hv2 = PluginHost_New(&engine);

        CHECK(
            PluginHost_Register(hv2, &V2_BAD_CONFIG_KEY) < 0,
            "v2 registration rejects config keys outside [a-z0-9_]");
        CHECK(
            PluginHost_Register(hv2, &V2_DUPLICATE_CONFIG_KEY) < 0,
            "v2 registration rejects duplicate config keys");
        CHECK(
            PluginHost_Register(hv2, &V2_MULTILINE_CONFIG_DEFAULT) < 0,
            "v2 registration rejects defaults that cannot occupy one INI line");
        CHECK(
            PluginHost_Count(hv2) == 0,
            "rejected config schemas consume no host registration slots");

        a2 = PluginHost_Register(hv2, &V2_PROBE_A);
        b2 = PluginHost_Register(hv2, &V2_PROBE_B);
        frame2 = PluginHost_Register(hv2, &V2_FRAME_PROVIDER);
        prefix2 = PluginHost_Register(hv2, &V2_PREFIX_ONLY);
        CHECK(a2 == 0 && b2 == 1 && frame2 == 2, "v2 registration shares host indexing");
        CHECK(prefix2 == 3, "a definition ending in a shorter callback-table prefix registers");
        CHECK(
            strcmp(PluginHost_Name(hv2, a2), "v2-probe-a") == 0 &&
                strcmp(PluginHost_Title(hv2, a2), "V2 Probe A") == 0 &&
                PluginHost_ConfigCount(hv2, a2) == 2,
            "v2 identity, title, and config schema normalize into host metadata");
        CHECK(PluginHost_IsRuntimeHost(hv2, b2), "v2 runtime-host flag normalizes");
        CHECK(
            PluginHost_IsEssential(hv2, frame2),
            "a v2 frame provider has host-controlled lifetime and no switch");

        PluginHost_SetEnabled(hv2, a2, false);
        PluginHost_SetEnabled(hv2, a2, true);
        CHECK(
            g_v2_starts[1] == 0 && g_v2_starts[2] == 0 &&
                g_v2_starts[3] == 0 && g_v2_prefix_starts == 0,
            "pre-boot enable-state decoding cannot start V2 plugins");

        /* FrameStart runs while App's asynchronous boot is still loading the
         * client and plugin preference files. It may reset budgets and observe
         * the screen, but it must not implicitly start the registry on defaults
         * or latch an empty frame preference. */
        PluginHost_FrameStart(hv2, 1, 0);
        CHECK(
            g_v2_starts[1] == 0 && g_v2_starts[2] == 0 &&
                g_v2_starts[3] == 0 && g_v2_prefix_starts == 0,
            "a pre-boot frame boundary cannot start V2 plugins");
        PluginHost_ConfigApply(hv2, "v2-probe-a", "loaded", "saved");
        snprintf(
            g_engine.frame_preference, sizeof(g_engine.frame_preference), "%s", "v2-frame/test");
        g_engine.frame_preference_present = 1;
        g_engine.frame_migration_version = 1;
        PluginHost_Start(hv2);
        PluginHost_Start(hv2);
        CHECK(
            g_v2_starts[1] == 1 && g_v2_starts[2] == 1 && g_v2_starts[3] == 1,
            "automatic v2 on_start dispatch happens exactly once");
        CHECK(
            g_v2_prefix_starts == 1,
            "a shorter callback table dispatches its declared prefix without reading its tail");
        CHECK(
            g_v2_started_with_saved_config,
            "the explicit startup fence publishes saved config before on_start");
        CHECK(
            g_v2_zeroed_starts == 3 && g_v2_first_state[1] != g_v2_first_state[2] &&
                g_v2_first_state[2] != g_v2_first_state[3],
            "each v2 registration receives isolated zeroed state");
        PluginHost_ConfigClearDirty(hv2);
        CHECK(
            g_v2_api[1]->config.set(
                g_v2_api[1],
                "marker",
                "1\n[plugin:v2-probe-b]\nmarker=99") == TORIRS_RESULT_INVALID &&
                strcmp(PluginHost_ConfigGet(hv2, a2, "marker"), "1") == 0 &&
                !PluginHost_ConfigDirty(hv2),
            "config.set rejects newline section injection without changing stored state");
        CHECK(
            g_v2_api[1]->config.set(g_v2_api[1], "marker", "1\renabled=0") ==
                    TORIRS_RESULT_INVALID &&
                g_v2_api[1]->config.set(g_v2_api[1], "bad-key", "value") ==
                    TORIRS_RESULT_INVALID,
            "config.set rejects carriage returns and invalid key spellings");
        {
            void* encoded = NULL;
            int encoded_size = 0;
            CHECK(
                PluginHost_ConfigEncode(hv2, &encoded, &encoded_size) && encoded &&
                    encoded_size > 0 && !strstr((char const*)encoded, "[plugin:v2-probe-b]"),
                "rejected config text cannot create another plugin section when encoded");
            free(encoded);
        }
        CHECK(
            g_v2_typed_calls == 3 && g_engine.objects_live == 3,
            "typed module calls execute for every live instance");
        memset(&selection, 0, sizeof(selection));
        selection.struct_size = sizeof(selection);
        g_v2_api[1]->frame.selection(g_v2_api[1], &selection);        CHECK(
            strcmp(selection.active_id, "core/native") == 0 &&
                selection.status == TORIRS_FRAME_STATUS_LOADING &&
                PluginHost_FrameNeedsLayout(hv2) && g_engine.frame_active == 0,
            "v2 offer conversion prepares a candidate without changing native policy");

        PluginHost_LogicTick(hv2, 7);
        CHECK(
            ((struct V2ProbeState*)g_v2_latest_state[1])->ticks == 7 &&
                ((struct V2ProbeState*)g_v2_latest_state[2])->ticks == 7,
            "automatic callback dispatch passes each instance its own state");
        draw_before = g_engine.draw_items;
        PluginHost_DrawCanvas(hv2, 900, 600);
        CHECK(
            g_engine.draw_items == draw_before + 2 &&
                ((struct V2ProbeState*)g_v2_latest_state[1])->canvas_draws == 1 &&
                ((struct V2ProbeState*)g_v2_latest_state[2])->canvas_draws == 1,
            "v2 canvas callbacks use scoped builders");

        PluginHost_Layout(hv2, 900, 600);
        CHECK(
            g_v2_frame_provisions == 1 && g_v2_frame_width == 900 &&
                g_v2_frame_canvas == TORIRS_FRAME_CANVAS_WINDOW && g_engine.frame_provides == 1,
            "the selected v2 offer is provided once and the engine takes the chrome");
        selection.struct_size = sizeof(selection);
        g_v2_api[1]->frame.selection(g_v2_api[1], &selection);        CHECK(
            strcmp(selection.active_id, "v2-frame/test") == 0 &&
                selection.status == TORIRS_FRAME_STATUS_ACTIVE && g_engine.frame_active == 1 &&
                g_engine.layout_canvas == TORIRS_FRAME_CANVAS_WINDOW &&
                g_engine.layout_fixed_w == 640 && g_engine.layout_fixed_h == 480,
            "READY v2 geometry and its canvas policy publish together");
        CHECK(PluginHost_PanelHasPage(hv2, a2), "v2 on_start panel registration is retained");
        CHECK(PluginHost_PanelSelect(hv2, a2), "v2 panel can be selected");
        generation = PluginHost_PanelSelectionGeneration(hv2);
        CHECK(
            g_v2_panel_builds == 1 && PluginHost_PanelWidgetCount(hv2, generation) == 5,
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
            widget && widget->kind == TORIRS_PANEL_WIDGET_CUSTOM &&
                strcmp(widget->id, "chart") == 0 && widget->label[0] == '\0' &&
                PluginHost_PanelDraw(hv2, generation, widget->serial, &g_engine, 0, 0, 100, 96) &&
                g_v2_panel_draws == 1,
            "shorthand custom panels retain no invented label and draw in callback scope");
        widget = PluginHost_PanelWidgetAt(hv2, generation, 4);
        CHECK(
            widget && widget->kind == TORIRS_PANEL_WIDGET_CUSTOM &&
                strcmp(widget->id, "labelled_chart") == 0 &&
                strcmp(widget->label, "Activity chart") == 0,
            "general custom nodes preserve their explicitly authored label");

        PluginHost_SetEnabled(hv2, a2, false);
        CHECK(
            g_v2_stops[1] == 1 && g_engine.objects_live == 2,
            "v2 stop releases state-owned scene resources");
        PluginHost_SetEnabled(hv2, a2, true);
        CHECK(
            g_v2_starts[1] == 2 && g_v2_zeroed_starts == 4 && g_engine.objects_live == 3,
            "re-enabling allocates a fresh zeroed state and restores resources");

        PluginHost_Free(hv2);
        CHECK(
            g_v2_stops[1] == 2 && g_v2_stops[2] == 1 && g_v2_stops[3] == 1 &&
                g_engine.objects_live == 0,
            "host destruction stops every v2 instance and cleans its resources");
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
            PluginHost_Register(seam_host, &V2_SEAM_PROBE) == 0,
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
        unsigned char* data;

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
            PluginHost_Register(aba_host, &V2_ABA_PROBE) == 0,
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
        CHECK(PluginHost_FrameNeedsLayout(aba_host), "the resource probe's offer is requested");
        PluginHost_Layout(aba_host, 640, 480);
        memset(&selection, 0, sizeof(selection));
        selection.struct_size = sizeof(selection);
        g_v2_aba_api->frame.selection(g_v2_aba_api, &selection);
        CHECK(
            strcmp(selection.active_id, "v2-aba/frame") == 0 &&
                selection.status == TORIRS_FRAME_STATUS_ACTIVE,
            "the resource probe provides its frame");

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
            !g_v2_aba.image_stale_size &&
                g_v2_aba.mesh_stale == TORIRS_RESULT_INVALID &&
                g_v2_aba.instance_stale == TORIRS_RESULT_INVALID &&
                g_v2_aba.model_stale == TORIRS_RESULT_INVALID,
            "every stale typed resource operation fails before reaching a reused internal slot");
        CHECK(
            g_engine.image_releases == 1 && g_engine.model_releases == 1 &&
                g_engine.meshes_live == 1 && g_engine.objects_live == 1,
            "repeated stale release/destroy calls leave all four replacements live");
        CHECK(
            strcmp(selection.active_id, "v2-aba/frame") == 0 &&
                selection.status == TORIRS_FRAME_STATUS_ACTIVE,
            "a provided frame retains no host-side artwork, so releasing an image leaves it active");

        data = malloc(3);
        memcpy(data, "NEW", 3);
        PluginHost_AssetDeliver(aba_host, "v2-aba", "aba-new.png", data, 3);
        data = malloc(5);
        memcpy(data, "MODEL", 5);
        PluginHost_AssetDeliver(aba_host, "v2-aba", "aba-new.model", data, 5);
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

    {
        memset(&g_engine,0,sizeof(g_engine));
        struct ToriRS_PluginEngine widget_engine = fake_engine();
        widget_engine.widget_request = fake_widget_request;
        struct ToriRS_PluginHost* widget_host = PluginHost_New(&widget_engine);
        struct ToriRS_PluginDef widget_def = {
            .struct_size=sizeof(widget_def), .id="widget-host-test", .title="Widget", .version="3",
            .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks), .on_start=widget_probe_start,.on_stop=widget_probe_stop,.on_draw_canvas=widget_probe_draw}
        };
        int owner = PluginHost_Register(widget_host, &widget_def);
        PluginHost_Start(widget_host);
        CHECK(widget_requests == 6 && widget_owner == (uint64_t)owner + 1,
              "widget bridge receives its plugin owner");
        struct ToriRS_WidgetRef ref;
        CHECK(saved_widgets.find(saved_widgets.context, "sidebar", &ref) == TORIRS_CONTRACT_WRONG_CONTEXT && widget_requests == 6,
              "retained widget API cannot mutate outside a live dispatch");
        PluginHost_DrawCanvas(widget_host,765,503);
        PluginHost_SetEnabled(widget_host, owner, false);
        CHECK(widget_resets == 1, "plugin disable releases its native widget edits");
        PluginHost_Free(widget_host);
    }

    {
        memset(&g_engine,0,sizeof(g_engine));
        struct ToriRS_PluginEngine engine = fake_engine();
        engine.widget_request = fake_widget_request;
        watched_host = PluginHost_New(&engine);
        struct ToriRS_PluginDef b = {.struct_size=sizeof(b),.id="watch-b",.title="B",.version="3",
            .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_start=watch_start}};
        struct ToriRS_PluginDef a = b; a.id="watch-a"; a.title="A";
        watched_b = PluginHost_Register(watched_host,&b);
        PluginHost_Register(watched_host,&a);
        PluginHost_Start(watched_host);
        PluginHost_WidgetsChanged(watched_host,77,1);
        CHECK(strcmp(watch_trace,"A+B+")==0, "binding callbacks use stable plugin order, not registration order");
        int queries = widget_requests;
        PluginHost_WidgetsChanged(watched_host,77,1);
        CHECK(widget_requests==queries, "unchanged native topology does not poll widget subscriptions");
        watch_trace[0]=0;
        PluginHost_WidgetsChanged(watched_host,77,2);
        CHECK(!watch_trace[0], "unrelated topology changes do not replay a stable binding");
        watched_native.opaque[2]=4;
        PluginHost_WidgetsChanged(watched_host,77,3);
        CHECK(strcmp(watch_trace,"A-A+B-B+")==0, "native replacement unbinds old incarnation before binding new one");
        watch_trace[0]=0; watch_churn=1; watched_native.opaque[2]=5;
        PluginHost_WidgetsChanged(watched_host,77,4);
        CHECK(strcmp(watch_trace,"A-A+")==0, "restarted subscription cannot join the old dispatch snapshot");
        watch_trace[0]=0;
        PluginHost_WidgetsChanged(watched_host,77,4);
        CHECK(strcmp(watch_trace,"B+")==0, "restarted subscription receives an initial binding in the next dispatch");
        watch_trace[0]=0; watch_self_remove=1; watched_native.opaque[2]=6;
        PluginHost_WidgetsChanged(watched_host,77,5);
        CHECK(strcmp(watch_trace,"A-B-B+")==0, "unsubscribing in unbound prevents the following bound callback");
        watch_trace[0]=0;
        PluginHost_WidgetsChanged(watched_host,0,0);
        CHECK(strcmp(watch_trace,"B-")==0, "closing the native tree unbinds remaining subscriptions");
        PluginHost_Free(watched_host);
    }
    {
        struct ToriRS_PluginEngine engine=fake_engine();engine.widget_request=fake_widget_request;
        watched_host=PluginHost_New(&engine);tree_events=0;tree_churn=0;
        struct ToriRS_PluginDef a={.struct_size=sizeof(a),.id="tree-a",.title="A",.version="3",
            .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_start=tree_watch_start}};
        struct ToriRS_PluginDef b=a;b.id="tree-b";b.title="B";
        int tree_a=PluginHost_Register(watched_host,&a);watched_b=PluginHost_Register(watched_host,&b);
        CHECK(tree_a>=0 && watched_b>=0,"tree subscription fixtures register");
        PluginHost_Start(watched_host);
        PluginHost_WidgetsChanged(watched_host,77,1);
        CHECK(tree_events==2,"each tree subscription receives one initial notification");
        PluginHost_WidgetsChanged(watched_host,77,1);
        CHECK(tree_events==2,"stable publications do not repeat tree callbacks");
        tree_churn=1;PluginHost_WidgetsChanged(watched_host,77,2);
        CHECK(tree_events==3,"restarted tree subscription cannot join an old dispatch");
        PluginHost_WidgetsChanged(watched_host,77,2);
        CHECK(tree_events==4,"only the new tree subscription receives its initial notification");
        PluginHost_SetEnabled(watched_host,watched_b,false);
        PluginHost_WidgetsChanged(watched_host,77,3);
        CHECK(tree_events==5,"disabled tree subscriber receives no later callback");
        PluginHost_Free(watched_host);
    }
    test_script_callbacks();
    test_widget_operations();
    test_widget_images();
    test_gameframe_provider();
    test_repair_pins();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
