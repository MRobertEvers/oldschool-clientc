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
#include "mem/torirs_mem.h"

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
    int outline_width,
    uint32_t e,
    int f,
    int g)
{
    (void)u;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)g;
    (void)outline_width;
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
/* Every draw_polygon the engine received, in order, so a case can read the
 * runtime's translation and its clip off what actually arrived. */
enum { FAKE_POLY_CALLS_MAX = 16, FAKE_POLY_POINTS_MAX = 32 };
static struct
{
    int count;
    int x[FAKE_POLY_POINTS_MAX];
    int y[FAKE_POLY_POINTS_MAX];
    int fill_alpha;
    int outline_width;
} g_poly[FAKE_POLY_CALLS_MAX];
static int g_poly_calls;
static int
fake_draw_polygon(
    void* u,
    int const* xs,
    int const* ys,
    int count,
    uint32_t fill_rgb,
    int fill_alpha,
    uint32_t outline_rgb,
    int outline_width)
{
    (void)u;
    (void)fill_rgb;
    (void)outline_rgb;
    if( g_poly_calls < FAKE_POLY_CALLS_MAX )
    {
        g_poly[g_poly_calls].count = count;
        for( int i = 0; i < count && i < FAKE_POLY_POINTS_MAX; i++ )
        {
            g_poly[g_poly_calls].x[i] = xs[i];
            g_poly[g_poly_calls].y[i] = ys[i];
        }
        g_poly[g_poly_calls].fill_alpha = fill_alpha;
        g_poly[g_poly_calls].outline_width = outline_width;
    }
    g_poly_calls++;
    g_engine.draw_items += count + 2;
    return count + 2;
}
static int
fake_text_width(void* u, char const* text)
{
    (void)u;
    return (int)strlen(text) * 6;
}
static int
fake_text_height(void* u)
{
    (void)u;
    return 12;
}
/* A minimap at canvas (550, 4), 152 square, centred on fine (0, 0) at one
 * pixel per 32 fine units: only the arithmetic the runtime adds is under test. */
static int
fake_minimap_project(void* u, int fine_x, int fine_z, int* out_x, int* out_y)
{
    (void)u;
    if( fine_x < 0 )
        return 0;
    *out_x = 626 + fine_x / 32;
    *out_y = 80 - fine_z / 32;
    return 1;
}
static int
fake_minimap_bounds(void* u, struct ToriRS_MinimapBounds* out)
{
    (void)u;
    out->box = (struct ToriRS_Rect){ 550, 4, 152, 152 };
    out->centre_x = 626;
    out->centre_y = 80;
    out->radius = 80;
    return 1;
}
/* The world map is open only while this is set. */
static int g_worldmap_open;
static int
fake_worldmap_project(void* u, int tile_x, int tile_z, int level, int* out_x, int* out_y)
{
    (void)u;
    if( !g_worldmap_open || level != 0 )
        return 0;
    *out_x = 100 + (tile_x - 3200) * 4;
    *out_y = 300 - (tile_z - 3200) * 4;
    return 1;
}
static int
fake_worldmap_bounds(void* u, struct ToriRS_Rect* out)
{
    (void)u;
    if( !g_worldmap_open )
        return 0;
    *out = (struct ToriRS_Rect){ 20, 40, 500, 400 };
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

/* The captures browser's engine half: no store, nothing queued. */
static int
fake_capture_root(void* u, char const* plugin, char const* dir, char* out, int out_size)
{
    (void)u;
    (void)plugin;
    (void)dir;
    out[0] = '\0';
    (void)out_size;
    return 0;
}
static int
fake_files_request(void* u, int serial, char const* verb, char const* body, int size)
{
    (void)u;
    (void)serial;
    (void)verb;
    (void)body;
    (void)size;
    return 0;
}
static int
fake_capture_read(void* u, int serial, char const* path)
{
    (void)u;
    (void)serial;
    (void)path;
    return 0;
}
static int
fake_image_publish_fit(
    void* u, int slot, void const* data, int size, int max_w, int max_h, int* out_w, int* out_h)
{
    (void)u;
    (void)slot;
    (void)data;
    (void)size;
    *out_w = max_w;
    *out_h = max_h;
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
/** No member of any surface has an authored box in this fake.
 *  @see ToriRS_FrameApi::surface_member_native_box. */
/** Nothing mounted to centre.  @see ToriRS_FrameApi::surface_center_content. */
/** One size per surface under test.  @see ToriRS_FrameApi::surface_fit. */
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
/* The skill this engine has no reading for, the way RS_PlayerStats answers a
 * skill the server has not stated yet. -1 when every skill is stated. */
static int g_stat_xp_unstated = -1;
static int
fake_stat_xp(
    void* u,
    int skill,
    int* xp,
    int* level_xp,
    int* next_xp)
{
    (void)u;
    if( skill < 0 || skill >= 25 || skill == g_stat_xp_unstated )
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
    e.draw_polygon = fake_draw_polygon;
    e.text_width = fake_text_width;
    e.text_height = fake_text_height;
    e.minimap_project = fake_minimap_project;
    e.minimap_bounds = fake_minimap_bounds;
    e.worldmap_project = fake_worldmap_project;
    e.worldmap_bounds = fake_worldmap_bounds;
    e.mouse_pos = fake_mouse_pos;
    e.component_rect = fake_component_rect;
    e.display_setting = fake_display_setting;
    e.display_setting_set = fake_display_setting_set;
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
    e.capture_root = fake_capture_root;
    e.files_request = fake_files_request;
    e.capture_read = fake_capture_read;
    e.image_publish_fit = fake_image_publish_fit;
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
static int g_v2_button_actions;
static int g_v2_custom_actions;
static int g_v2_select_actions;
static char g_v2_select_value[TORIRS_PLUGIN_SELECT_VALUE_MAX];
static int g_v2_panel_draws;
static int g_v2_prefix_starts;
static int g_v2_started_with_saved_config;

static char g_v2_option_label_a[] = "Same|label";
static char g_v2_option_label_missing[] = "Same|label";
static char g_v2_option_detail_missing[] = "Provider is not installed";
static struct ToriRS_SelectOption const V2_PANEL_OPTIONS_SECOND[] = {
    { .struct_size = sizeof(struct ToriRS_SelectOption), .value = "one", .label = "One", .enabled = true },
    { .struct_size = sizeof(struct ToriRS_SelectOption), .value = "two", .label = "Two", .enabled = true },
};
static struct ToriRS_SelectOption const V2_PANEL_OPTIONS_FOUR[] = {
    { .struct_size = sizeof(struct ToriRS_SelectOption), .value = "auto", .label = "Auto", .enabled = true },
    { .struct_size = sizeof(struct ToriRS_SelectOption), .value = "a/frame", .label = "A", .enabled = true },
    { .struct_size = sizeof(struct ToriRS_SelectOption), .value = "b/frame", .label = "B", .enabled = false },
    { .struct_size = sizeof(struct ToriRS_SelectOption), .value = "c/frame", .label = "C", .enabled = true },
};
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
    /* A second structured select AFTER the first: its option slice sits
     * behind "frame"'s in the host pool, so a count change on "frame" has
     * to slide it and keep its owner's pointer right. */
    panel->select(
        panel,
        "second",
        "Second",
        "two",
        V2_PANEL_OPTIONS_SECOND,
        (int)(sizeof(V2_PANEL_OPTIONS_SECOND) / sizeof(V2_PANEL_OPTIONS_SECOND[0])));
    /* A command the page cannot service yet. `enabled` was stored and read by
     * nobody, so this used to be a button that looked live and fired. */
    panel->button(panel, "commit", "Commit", false);
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
    else if( strcmp(event->id, "commit") == 0 )
        g_v2_button_actions++;
    else if( strcmp(event->id, "chart") == 0 )
        g_v2_custom_actions++;
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
static struct ToriRS_ConfigItem const V2_CONFIG_C_ITEMS[] = {
    { .key = "marker", .label = "Marker", .type = TORIRS_CONFIG_INT, .default_value = "3" },
    { 0 },
};
static struct ToriRS_ConfigSchema const V2_CONFIG_A = { .struct_size = sizeof(V2_CONFIG_A),
                                                        .items = V2_CONFIG_A_ITEMS };
static struct ToriRS_ConfigSchema const V2_CONFIG_B = { .struct_size = sizeof(V2_CONFIG_B),
                                                        .items = V2_CONFIG_B_ITEMS };
static struct ToriRS_ConfigSchema const V2_CONFIG_C = { .struct_size = sizeof(V2_CONFIG_C),
                                                            .items = V2_CONFIG_C_ITEMS };

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

static struct ToriRS_PluginDef const V2_PROBE_C = {
    .struct_size = sizeof(V2_PROBE_C),
    .id = "v2-probe-c",
    .title = "V2 Probe C",
    .version = "2.0.0",
    .state_size = sizeof(struct V2ProbeState),
    .config = &V2_CONFIG_C,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = v2_probe_start,
        .on_stop = v2_probe_stop,
    },
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
    /* The three answers of game.skill: a reading, a skill with no reading
     * yet, and an index this client has no skill for. */
    bool skill_stated_returned;
    bool skill_unstated_returned;
    bool skill_missing_returned;
    struct ToriRS_SkillSnapshot skill_stated;
    struct ToriRS_SkillSnapshot skill_unstated;
    struct ToriRS_SkillSnapshot skill_missing;
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
    g_v2_seam.skill_stated.struct_size = sizeof(g_v2_seam.skill_stated);
    g_v2_seam.skill_unstated.struct_size = sizeof(g_v2_seam.skill_unstated);
    g_v2_seam.skill_missing.struct_size = sizeof(g_v2_seam.skill_missing);
    g_v2_seam.skill_stated_returned =
        api->game->skill(api, 0, &g_v2_seam.skill_stated);
    g_v2_seam.skill_unstated_returned =
        api->game->skill(api, 1, &g_v2_seam.skill_unstated);
    g_v2_seam.skill_missing_returned =
        api->game->skill(api, 9, &g_v2_seam.skill_missing);
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
    },
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
static struct ToriRS_WidgetRef const op_control={{77,5,9}};
/* The adapter's op slots for op_control, as the tree would hold them: an
 * empty string is a slot with no row. The single `op_label` above is still
 * the LAST label written, which every one-row case reads. */
static char op_slots[TORIRS_WIDGET_OP_SLOTS][64];
static char const* op_slot_label(int op)
{
    if( op<1 || op>TORIRS_WIDGET_OP_SLOTS ) return NULL;
    return op_slots[op-1];
}
static int anchor_relation,anchor_sets;
static int mask_slot,mask_sets;
static struct ToriRS_WidgetRef anchor_target;
/*
 * The engine's answer to PLUGIN_WIDGET_STATE, as a table the test writes.
 * Keyed by the reference's NODE slot rather than the whole reference, so a
 * test that bumps the incarnation (a native replacement) keeps writing to the
 * same row -- which is what the real adapter does too.
 */
#define FAKE_WIDGET_STATE_MAX 4
static struct
{
    uint64_t node;
    struct ToriRS_WidgetState state;
} fake_states[FAKE_WIDGET_STATE_MAX];
static int fake_state_count;
static struct ToriRS_WidgetState* fake_state_row(uint64_t node)
{
    for( int i = 0; i < fake_state_count; ++i )
        if( fake_states[i].node == node ) return &fake_states[i].state;
    CHECK(fake_state_count < FAKE_WIDGET_STATE_MAX, "the fake state table has room");
    fake_states[fake_state_count].node = node;
    return &fake_states[fake_state_count++].state;
}
static void fake_states_reset(void)
{
    memset(fake_states, 0, sizeof(fake_states));
    fake_state_count = 0;
}

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
        if( r->op>=1 && r->op<=TORIRS_WIDGET_OP_SLOTS && ToriRS_WidgetRefEqual(r->ref,op_control) )
            snprintf(op_slots[r->op-1],sizeof(op_slots[r->op-1]),"%s",r->name);
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
    if( r->kind == PLUGIN_WIDGET_STATE )
    {
        uint32_t const declared = r->state->struct_size;
        *r->state = *fake_state_row(r->ref.opaque[1]);
        r->state->struct_size = declared;
        r->state->incarnation = r->ref.opaque[2];
    }
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
/* Set while the multi-row case runs: the shared listener then checks the
 * event's `operation` against what was dispatched rather than assuming 1. */
static int op_multi;
static int op_last_operation;
static uint64_t op_last_revision;
static void op_listener(struct ToriRS_Api* api,void* user,struct ToriRS_WidgetEvent const* event)
{
    struct ToriRS_WidgetApi* ui=&api->widgets;
    (void)user;
    ++op_calls;
    op_last_revision=event->native_revision;
    op_last_operation=event->operation;
    CHECK(event->type==TORIRS_WIDGET_OPERATION && ToriRS_WidgetRefEqual(event->widget,op_control) &&
              (op_multi ? event->operation>=1 : event->operation==1),
          "operation event names the pressed control");
    CHECK(ui->set_text(ui->context,op_control,"pressed")==TORIRS_CONTRACT_OK,
          "an operation callback may mutate widgets");
    CHECK(ui->invoke(ui->context,(struct ToriRS_WidgetActionRef){op_control,1,1})==TORIRS_CONTRACT_OK,
          "an operation callback may invoke checked native actions");
    int mode=op_mode;op_mode=0;
    if( mode==1 )
        CHECK(ui->set_on_op(ui->context,op_control,1,NULL,NULL,NULL)==TORIRS_CONTRACT_OK,
              "a listener can remove its own operation while handling it");
    if( mode==2 )
        PluginHost_SetEnabled(op_host,op_index,false);
    if( mode==3 )
        CHECK(ui->set_on_op(ui->context,op_control,1,"Again",op_listener,NULL)==TORIRS_CONTRACT_OK,
              "a listener can re-arm its control while handling it");
    if( mode==5 )
        CHECK(ui->set_on_op(ui->context,op_control,2,"Setup",op_listener,NULL)==TORIRS_CONTRACT_OK,
              "a listener can arm a second row on its control");
    if( mode==6 )
        CHECK(ui->set_on_op(ui->context,op_control,2,NULL,NULL,NULL)==TORIRS_CONTRACT_OK,
              "a listener can clear one row of several");
    if( mode==4 )
    {
        int ok=0;
        for( int i=0;i<126;++i )
            if( ui->set_on_op(ui->context,(struct ToriRS_WidgetRef){{77,200+(uint64_t)i,1}},1,"Row",op_listener,NULL)==TORIRS_CONTRACT_OK ) ++ok;
        CHECK(ok==126,"the per-owner budget admits 128 armed controls");
        CHECK(ui->set_on_op(ui->context,stale_control,1,"Row",op_listener,NULL)==TORIRS_CONTRACT_STALE_REFERENCE,
              "arming a vanished control reports the stale reference");
        CHECK(ui->set_on_op(ui->context,(struct ToriRS_WidgetRef){{77,900,1}},1,"Row",op_listener,NULL)==TORIRS_CONTRACT_OK,
              "the last free slot is granted");
        CHECK(ui->set_on_op(ui->context,(struct ToriRS_WidgetRef){{77,901,1}},1,"Row",op_listener,NULL)==TORIRS_CONTRACT_BUDGET_EXCEEDED,
              "a full table never evicts a live registration");
        CHECK(ui->set_on_op(ui->context,(struct ToriRS_WidgetRef){{77,902,1}},1,NULL,NULL,NULL)==TORIRS_CONTRACT_OK,
              "removing an operation that was never armed is a no-op");
        int before=op_requests;
        CHECK(ui->set_on_op(ui->context,(struct ToriRS_WidgetRef){{77,903,1}},1,NULL,NULL,NULL)==TORIRS_CONTRACT_OK && op_requests==before,
              "a no-op removal sends nothing to the adapter");
    }
}
static void op_start(struct ToriRS_Api* api,void* state)
{
    struct ToriRS_WidgetApi* ui=&api->widgets;
    char too_long[TORIRS_WIDGET_OP_LABEL_MAX+1];
    (void)state;
    memset(too_long,'x',sizeof(too_long)-1);too_long[sizeof(too_long)-1]=0;
    CHECK(ui->set_on_op(ui->context,op_control,1,"",op_listener,NULL)==TORIRS_CONTRACT_INVALID_ARGUMENT,
          "an empty operation label is rejected");
    CHECK(ui->set_on_op(ui->context,op_control,1,NULL,op_listener,NULL)==TORIRS_CONTRACT_INVALID_ARGUMENT,
          "a missing operation label is rejected");
    CHECK(ui->set_on_op(ui->context,op_control,1,too_long,op_listener,NULL)==TORIRS_CONTRACT_INVALID_ARGUMENT,
          "an over-long operation label is rejected");
    int before=op_requests;
    CHECK(ui->set_on_op(ui->context,op_control,1,"Press",op_listener,NULL)==TORIRS_CONTRACT_OK &&
          op_requests==before+1 && strcmp(op_label,"Press")==0 && op_registration!=0,
          "arming reaches the native adapter with its label and registration");
}
static void op_draw(struct ToriRS_Api* api,void* state,struct ToriRS_Graphics* graphics)
{
    (void)state;(void)graphics;
    CHECK(api->widgets.set_on_op(api->widgets.context,op_control,1,"Paint",op_listener,NULL)==TORIRS_CONTRACT_WRONG_CONTEXT,
          "paint cannot arm an owned control");
}
static void op_stop(struct ToriRS_Api* api,void* state)
{
    (void)state;
    CHECK(api->widgets.set_on_op(api->widgets.context,op_control,1,"Late",op_listener,NULL)==TORIRS_CONTRACT_WRONG_CONTEXT,
          "shutdown cannot arm an owned control");
    CHECK(api->widgets.set_on_op(api->widgets.context,op_control,1,NULL,NULL,NULL)==TORIRS_CONTRACT_OK,
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

/* The world pass must hand its overlays a valid draw context: six of the
 * seven overlay plugins draw there, and before the pass carried a region the
 * context answered false to every one of them. */
static int world_ctx_calls, world_ctx_valid, world_ctx_w, world_ctx_h, world_ctx_x, world_ctx_y;
static void world_ctx_draw(struct ToriRS_Api* api,void* state,struct ToriRS_Graphics* graphics)
{
    (void)api;(void)state;
    struct ToriRS_DrawContext context={.struct_size=sizeof(context)};
    world_ctx_calls++;
    world_ctx_valid=graphics->context(graphics,&context);
    if( world_ctx_valid ){ world_ctx_x=context.bounds.x; world_ctx_y=context.bounds.y; world_ctx_w=context.bounds.width; world_ctx_h=context.bounds.height; }
}
static void test_world_draw_context(void)
{
    struct ToriRS_PluginEngine engine=fake_engine();
    struct ToriRS_PluginHost* host=PluginHost_New(&engine);
    struct ToriRS_PluginDef def={.struct_size=sizeof(def),.id="world-ctx",.title="World ctx",.version="3",
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_draw_world=world_ctx_draw}};
    world_ctx_calls=world_ctx_valid=0;
    CHECK(PluginHost_Register(host,&def)>=0,"world context fixture registers");
    PluginHost_Start(host);
    PluginHost_DrawWorld(host,765,503);
    CHECK(world_ctx_calls==1,"the world pass ran the overlay once");
    CHECK(world_ctx_valid,"the world pass hands its overlay a valid draw context");
    CHECK(world_ctx_x==0 && world_ctx_y==0 && world_ctx_w==765 && world_ctx_h==503,
          "the world context is the whole canvas at origin zero (no coordinate shift)");
    PluginHost_Free(host);
}

/*
 * The Quest Helper drawing verbs: a polygon, the font's metrics, and the two
 * map projections, through the builder the way a plugin reaches them.
 *
 * The polygon is the one with logic of its own in the runtime. A polygon on
 * the surface goes to the engine whole, wash and border in one call. One that
 * crosses the edge goes as TWO things: the wash cut to the box, and the
 * border's own edges cut to the box -- never an outline of the cut, which
 * would draw a frame along the canvas edge nobody asked for.
 *
 * MUTATION 1: outline the clipped wash (pass `border` on the clipped fill call).
 *   Red: "the cut wash carries no border".
 * MUTATION 2: drop the clip and pass the raw points.
 *   Red: "the wash is cut at the canvas edge".
 * MUTATION 3: answer OK from api_draw_polygon's budget arm.
 *   Red: "a polygon past the budget answers BUDGET".
 */
static enum ToriRS_Result poly_inside, poly_clipped, poly_budget_last;
static int poly_tw, poly_th, poly_mm_ok, poly_mm_x, poly_mm_y, poly_mm_off;
static int poly_mb_ok, poly_wm_closed, poly_wb_closed, poly_wm_ok, poly_wm_x, poly_wm_y, poly_wb_ok;
static int poly_wm_level, poly_proj_ok, poly_proj_x, poly_proj_y;
static struct ToriRS_MinimapBounds poly_mb;
static struct ToriRS_Rect poly_wb;
static void poly_draw(struct ToriRS_Api* api,void* state,struct ToriRS_Graphics* graphics)
{
    int const qx[4] = { 10, 50, 50, 10 };
    int const qy[4] = { 10, 10, 40, 40 };
    int const tx[3] = { -20, 60, 60 };
    int const ty[3] = { 100, 100, 200 };
    int scratch_x = 0, scratch_y = 0;
    struct ToriRS_Rect scratch_rect;
    (void)api;(void)state;
    g_poly_calls = 0;
    poly_inside = graphics->polygon(graphics, qx, qy, 4, 0xff0000u, 80, 0xffffffu, 2);
    poly_clipped = graphics->polygon(graphics, tx, ty, 3, 0x00ff00u, 80, 0xffffffu, 1);
    poly_tw = graphics->text_width(graphics, "Quest");
    poly_th = graphics->text_height(graphics);
    poly_proj_ok = graphics->project(graphics, 1280, 2560, 0, &poly_proj_x, &poly_proj_y);
    poly_mm_ok = graphics->minimap_project(graphics, 320, 64, &poly_mm_x, &poly_mm_y);
    poly_mm_off = graphics->minimap_project(graphics, -1, 0, &scratch_x, &scratch_y);
    memset(&poly_mb, 0, sizeof(poly_mb));
    poly_mb_ok = graphics->minimap_bounds(graphics, &poly_mb);
    g_worldmap_open = 0;
    poly_wm_closed = graphics->worldmap_project(graphics, 3201, 3199, 0, &scratch_x, &scratch_y);
    poly_wb_closed = graphics->worldmap_bounds(graphics, &scratch_rect);
    g_worldmap_open = 1;
    poly_wm_ok = graphics->worldmap_project(graphics, 3201, 3199, 0, &poly_wm_x, &poly_wm_y);
    poly_wm_level = graphics->worldmap_project(graphics, 3201, 3199, 1, &scratch_x, &scratch_y);
    poly_wb_ok = graphics->worldmap_bounds(graphics, &poly_wb);
    g_worldmap_open = 0;
    for( int i = 0; i < 200; i++ )
        poly_budget_last = graphics->polygon(graphics, qx, qy, 4, 0xff0000u, 80, 0, 0);
}
static int poly_has_point(int call, int x, int y)
{
    for( int i = 0; i < g_poly[call].count && i < FAKE_POLY_POINTS_MAX; i++ )
        if( g_poly[call].x[i] == x && g_poly[call].y[i] == y )
            return 1;
    return 0;
}
static void test_draw_polygon_and_maps(void)
{
    struct ToriRS_PluginEngine engine=fake_engine();
    struct ToriRS_PluginHost* host=PluginHost_New(&engine);
    struct ToriRS_PluginDef def={.struct_size=sizeof(def),.id="poly-maps",.title="Poly",.version="3",
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_draw_canvas=poly_draw}};
    CHECK(PluginHost_Register(host,&def)>=0,"polygon fixture registers");
    PluginHost_Start(host);
    PluginHost_DrawCanvas(host,765,503);

    CHECK(poly_inside==TORIRS_RESULT_OK && g_poly[0].count==4 &&
          g_poly[0].fill_alpha==80 && g_poly[0].outline_width==2 &&
          g_poly[0].x[0]==10 && g_poly[0].y[0]==10 && g_poly[0].x[2]==50 && g_poly[0].y[2]==40,
          "a polygon on the surface reaches the engine whole, wash and border in one call");
    CHECK(poly_clipped==TORIRS_RESULT_OK && g_poly_calls>=5,
          "a polygon over the edge is drawn as its cut wash and its cut edges");
    CHECK(g_poly[1].fill_alpha==80 && g_poly[1].outline_width==0,
          "the cut wash carries no border");
    CHECK(g_poly[1].count==4 && poly_has_point(1,0,100) && poly_has_point(1,60,100) &&
          poly_has_point(1,60,200) && poly_has_point(1,0,125),
          "the wash is cut at the canvas edge");
    CHECK(g_poly[2].count==2 && g_poly[2].fill_alpha==0 && g_poly[2].outline_width==1 &&
          poly_has_point(2,0,100) && poly_has_point(2,60,100),
          "a border edge crossing the edge is cut to it");
    CHECK(g_poly[4].count==2 && poly_has_point(4,60,200) && poly_has_point(4,0,125),
          "the closing edge is cut too");
    CHECK(poly_budget_last==TORIRS_RESULT_BUDGET,"a polygon past the budget answers BUDGET");

    CHECK(poly_tw==30 && poly_th==12,"text metrics are the engine's");
    CHECK(poly_proj_ok && poly_proj_x==10 && poly_proj_y==20,"draw.project is api.draw.project at origin zero");
    CHECK(poly_mm_ok && poly_mm_x==636 && poly_mm_y==78,"a minimap point is the engine's canvas pixel");
    CHECK(!poly_mm_off,"an undrawn minimap point answers false");
    CHECK(poly_mb_ok && poly_mb.box.x==550 && poly_mb.box.width==152 && poly_mb.centre_x==626 &&
          poly_mb.centre_y==80 && poly_mb.radius==80,
          "the minimap's bounds come through whole");
    CHECK(!poly_wm_closed && !poly_wb_closed,"a closed world map answers false to both");
    CHECK(poly_wm_ok && poly_wm_x==104 && poly_wm_y==304,"an open world map projects a tile");
    CHECK(!poly_wm_level,"a level the map does not cover answers false");
    CHECK(poly_wb_ok && poly_wb.x==20 && poly_wb.y==40 && poly_wb.width==500 && poly_wb.height==400,
          "the open world map's view rectangle");
    PluginHost_Free(host);
}

/*
 * The world hull ANSWERS its two refusals.
 *
 * `ToriRS_Graphics::world_hull` is declared to return a result; api_draw_hull
 * was void and v2_builder_world_hull answered TORIRS_RESULT_OK
 * unconditionally, so the per-frame draw budget and the per-entity APPEARANCE
 * claim were both dropped between the two. What that costs is not an error
 * message: it is half the outlines in a mass of tagged npcs, gone, with the
 * plugin still reporting itself armed.
 *
 * MUTATION 1: `return TORIRS_RESULT_OK` from api_draw_hull's budget arm.
 *   Red: "the draw past the budget answers BUDGET".
 * MUTATION 2: the same on its claim arm.
 *   Red: "a claimed entity answers CONFLICT".
 * MUTATION 3: make v2_builder_world_hull ignore api_draw_hull and return OK,
 *   which is the shipped defect exactly. Red: both.
 */
static int hull_budget_first, hull_budget_last, hull_claimed_result, hull_free_result;
static void hull_budget_draw(struct ToriRS_Api* api,void* state,struct ToriRS_Graphics* graphics)
{
    (void)api;(void)state;
    /* Each fake hull costs 3 against the budget, so this runs well past it. */
    hull_budget_first = (int)graphics->world_hull(graphics, 5, 0xff0000u, 0, TORIRS_HULL_MESH);
    for( int i = 0; i < TORIRS_PLUGIN_DRAW_BUDGET; i++ )
        hull_budget_last = (int)graphics->world_hull(graphics, 5, 0xff0000u, 0, TORIRS_HULL_MESH);
}
static void hull_claim_draw(struct ToriRS_Api* api,void* state,struct ToriRS_Graphics* graphics)
{
    (void)api;(void)state;
    /* fake_npc_by_slot answers element_id 0 for every slot, so "npc:1" is
     * element 0 and anything else is unclaimed. */
    hull_claimed_result = (int)graphics->world_hull(graphics, 0, 0x00ff00u, 0, TORIRS_HULL_BOUNDS);
    hull_free_result = (int)graphics->world_hull(graphics, 9, 0x00ff00u, 0, TORIRS_HULL_BOUNDS);
}
static void hull_claim_start(struct ToriRS_Api* api,void* state)
{
    struct ToriRS_EntityAppearance look;
    (void)state;
    memset(&look, 0, sizeof(look));
    look.shape = TORIRS_HULL_BOUNDS;
    (void)api->game->entity_look(api, "npc:1", &look);
}
static void test_world_hull_answers_its_refusals(void)
{
    struct ToriRS_PluginEngine engine=fake_engine();
    struct ToriRS_PluginHost* host=PluginHost_New(&engine);
    struct ToriRS_PluginDef budget={.struct_size=sizeof(budget),.id="hull-budget",.title="Hull",.version="3",
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_draw_world=hull_budget_draw}};
    struct ToriRS_PluginDef holder={.struct_size=sizeof(holder),.id="hull-holder",.title="Holder",.version="3",
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_start=hull_claim_start}};
    struct ToriRS_PluginDef loser={.struct_size=sizeof(loser),.id="hull-loser",.title="Loser",.version="3",
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_draw_world=hull_claim_draw}};

    hull_budget_first=hull_budget_last=-1;
    CHECK(PluginHost_Register(host,&budget)>=0,"the budget fixture registers");
    PluginHost_Start(host);
    PluginHost_DrawWorld(host,765,503);
    CHECK(hull_budget_first==(int)TORIRS_RESULT_OK,"a draw inside the budget answers OK");
    CHECK(hull_budget_last==(int)TORIRS_RESULT_BUDGET,"the draw past the budget answers BUDGET");
    PluginHost_Free(host);

    hull_claimed_result=hull_free_result=-1;
    host=PluginHost_New(&engine);
    CHECK(PluginHost_Register(host,&holder)>=0,"the claim holder registers");
    CHECK(PluginHost_Register(host,&loser)>=0,"and so does the plugin that will lose");
    PluginHost_Start(host);
    PluginHost_DrawWorld(host,765,503);
    CHECK(hull_claimed_result==(int)TORIRS_RESULT_CONFLICT,"a claimed entity answers CONFLICT");
    CHECK(hull_free_result==(int)TORIRS_RESULT_OK,"an unclaimed one is still everybody's");
    PluginHost_Free(host);
}

/*
 * And the world TILE answers its one.
 *
 * The same silence world_hull had, over the verb the budget actually bites: a
 * marker is drawn per tile of a footprint, so a crowded overlay reaches the
 * 512-item allotment by multiplication and every tile past it vanished, with
 * `plugin_draw_allow` logging the truncation and no caller able to read it.
 * There is no claim arm here -- a tile is a place, so no plugin can hold it.
 *
 * MUTATION 1: `return TORIRS_RESULT_OK` from api_draw_tile's budget arm.
 *   Red: "the tile past the budget answers BUDGET".
 * MUTATION 2: make v2_builder_world_tile discard api_draw_tile's answer and
 *   return OK, which is the shipped defect exactly. Red: the same.
 */
static int tile_budget_first, tile_budget_last;
static void tile_budget_draw(struct ToriRS_Api* api,void* state,struct ToriRS_Graphics* graphics)
{
    (void)api;(void)state;
    tile_budget_first = (int)graphics->world_tile(
        graphics, 3200, 3200, 0, 0x00ff00u, 0x00ff00u, 2, 40, TORIRS_TILE_ON_TOP);
    for( int i = 0; i < TORIRS_PLUGIN_DRAW_BUDGET; i++ )
        tile_budget_last = (int)graphics->world_tile(
            graphics, 3200 + i, 3200, 0, 0x00ff00u, 0x00ff00u, 2, 40, TORIRS_TILE_ON_TOP);
}
static void test_world_tile_answers_its_refusal(void)
{
    struct ToriRS_PluginEngine engine=fake_engine();
    struct ToriRS_PluginHost* host=PluginHost_New(&engine);
    struct ToriRS_PluginDef budget={.struct_size=sizeof(budget),.id="tile-budget",.title="Tile",.version="3",
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_draw_world=tile_budget_draw}};

    tile_budget_first=tile_budget_last=-1;
    CHECK(PluginHost_Register(host,&budget)>=0,"the tile budget fixture registers");
    PluginHost_Start(host);
    PluginHost_DrawWorld(host,765,503);
    CHECK(tile_budget_first==(int)TORIRS_RESULT_OK,"a tile inside the budget answers OK");
    CHECK(tile_budget_last==(int)TORIRS_RESULT_BUDGET,"the tile past the budget answers BUDGET");
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
    CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,0,1),"a zero registration never dispatches");
    CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,serial+1,1),"a mismatched registration is rejected");
    CHECK(!PluginHost_WidgetOperation(op_host,owner,(struct ToriRS_WidgetRef){{77,5,10}},serial,1),"a different widget incarnation is rejected");
    CHECK(!PluginHost_WidgetOperation(op_host,(uint64_t)other_index+1,op_control,serial,1),"another plugin's registration cannot be dispatched to a foreign owner");
    CHECK(!PluginHost_WidgetOperation(op_host,owner+7,op_control,serial,1),"an unknown owner is rejected");
    CHECK(op_calls==0,"rejected dispatches reach no listener");
    CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,serial,1) && op_calls==1 && op_last_revision==serial,
          "a current registration dispatches to the owner's listener");
    PluginHost_DrawCanvas(op_host,765,503);
    op_mode=3;
    CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,serial,1) && op_calls==2,"dispatch during which the listener re-arms");
    uint64_t replaced=op_registration;
    CHECK(replaced!=serial && replaced!=0 && strcmp(op_label,"Again")==0,"re-arming issues a fresh registration with the new label");
    CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,serial,1),"a retired registration no longer dispatches");
    CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,replaced,1) && op_calls==3,"the replacement registration dispatches");
    op_mode=1;
    CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,replaced,1) && op_calls==4,"dispatch during which the listener removes itself");
    CHECK(op_registration==0 && op_label[0]==0,"removal clears the adapter operation");
    CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,replaced,1) && op_calls==4,"a removed registration no longer dispatches");
    PluginHost_SetEnabled(op_host,op_index,false);
    CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,replaced,1),"a disabled plugin receives no operations");
    PluginHost_SetEnabled(op_host,op_index,true);
    uint64_t fresh=op_registration;
    CHECK(fresh!=0 && fresh!=replaced && PluginHost_WidgetOperation(op_host,owner,op_control,fresh,1) && op_calls==5,
          "restart re-arms with a new registration");
    op_mode=4;
    CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,fresh,1) && op_calls==6,"budget probe dispatched");
    op_mode=2;
    CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,fresh,1) && op_calls==7,"a listener may disable its own plugin");
    CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,fresh,1) && op_calls==7,"nothing dispatches after self-disable");
    /*
     * A control with more than one row.
     *
     * The case numbered ops exist for: a cover over a component offering two
     * rows offers both, one listener answers for both, and taking one row away
     * leaves the other armed. The registration is the CONTROL's, so arming the
     * second row must not retire the rows already built for the first --
     * minting a serial per op is exactly the bug this pins.
     */
    PluginHost_SetEnabled(op_host,op_index,true);
    {
        uint64_t const first=op_registration;
        op_mode=0;op_multi=1;
        CHECK(first!=0,"the control is armed on its first row");
        CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,first,1),"op 1 dispatches");
        CHECK(op_last_operation==1,"and the listener is told which row was chosen");
        CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,first,2),
              "a row the plugin never armed is refused");
        op_mode=5;
        CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,first,1),"the arming dispatch runs");
        uint64_t const two=op_registration;
        CHECK(two!=0 && two!=first,
              "arming a second row mints a fresh registration, retiring rows built before it");
        CHECK(op_slot_label(2)!=NULL && strcmp(op_slot_label(2),"Setup")==0,
              "and reaches the adapter under its own number");
        CHECK(op_slot_label(1)!=NULL && strcmp(op_slot_label(1),"Press")==0,
              "without disturbing the row already armed");
        CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,two,2) && op_last_operation==2,
              "the second row dispatches to the same listener, naming itself");
        CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,two,1) && op_last_operation==1,
              "and so does the first, under the same registration");
        op_mode=6;
        CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,two,2),"the clearing dispatch runs");
        CHECK(op_registration==two,
              "clearing one row of two leaves the control registered, so the other row's menu rows stand");
        CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,two,2),
              "the cleared row no longer dispatches");
        CHECK(PluginHost_WidgetOperation(op_host,owner,op_control,two,1),
              "and the row that was left keeps working");
        CHECK(!PluginHost_WidgetOperation(op_host,owner,op_control,two,0) &&
                  !PluginHost_WidgetOperation(op_host,owner,op_control,two,TORIRS_WIDGET_OP_SLOTS+1),
              "an operation number outside the contract's range is refused");
        op_multi=0;
    }
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
/* ------------------------------------------------------------------------ *
 * Native state on a watched element.
 *
 * A hide, a move, a re-skin or a retype bumps no tree generation, so the
 * bindings pass cannot see any of it. PluginHost_WidgetStates stamps every
 * bound watch every frame and raises TORIRS_WIDGET_STATE_CHANGED on a
 * difference -- which is what a tab stone, a camera over the report button or
 * a plate over the compass follows instead of polling.
 * ------------------------------------------------------------------------ */
static struct ToriRS_PluginHost* state_host;
static int state_events, state_binds, state_unbinds, state_self_replace;
static bool state_read_ok;
static struct ToriRS_WidgetState state_last_read;
static void record_state(struct ToriRS_Api* api, void* user, struct ToriRS_WidgetEvent const* event)
{
    (void)user;
    if( event->type == TORIRS_WIDGET_BOUND ) { ++state_binds; return; }
    if( event->type == TORIRS_WIDGET_UNBOUND ) { ++state_unbinds; return; }
    CHECK(event->type == TORIRS_WIDGET_STATE_CHANGED, "the state pass raises only state changes");
    CHECK(strcmp(event->role, "sidebar") == 0, "a state event identifies its subscription");
    ++state_events;
    memset(&state_last_read, 0, sizeof(state_last_read));
    state_last_read.struct_size = (uint32_t)sizeof(state_last_read);
    state_read_ok = api->widgets.state(api->widgets.context, event->widget, &state_last_read)
        == TORIRS_CONTRACT_OK;
    if( state_self_replace )
    {
        state_self_replace = 0;
        CHECK(api->widgets.watch_state(api->widgets.context, "sidebar", record_state, NULL)
                  == TORIRS_CONTRACT_OK,
              "a state callback may replace its own watch");
    }
}
static int plain_binds, plain_state_events;
static void record_plain(struct ToriRS_Api* api, void* user, struct ToriRS_WidgetEvent const* event)
{
    (void)api; (void)user;
    if( event->type == TORIRS_WIDGET_BOUND || event->type == TORIRS_WIDGET_UNBOUND ) { ++plain_binds; return; }
    /* The regression this pins: minimap-orbs and xp-drop-orbs compute their ref
     * as "the widget if BOUND, else empty", so an unexpected kind made them drop
     * their controls in the client. A plain watch is BOUND/UNBOUND only. */
    ++plain_state_events;
}
static void plain_watch_start(struct ToriRS_Api* api, void* plugin_state)
{
    (void)plugin_state;
    CHECK(api->widgets.watch(api->widgets.context, "sidebar", record_plain, NULL) == TORIRS_CONTRACT_OK,
          "the plain fixture subscribes at startup");
}
static void state_watch_start(struct ToriRS_Api* api, void* plugin_state)
{
    (void)plugin_state;
    CHECK(api->widgets.watch_state(api->widgets.context, "sidebar", record_state, NULL)
              == TORIRS_CONTRACT_OK,
          "the state fixture subscribes at startup");
}
static void test_widget_states(void)
{
    memset(&g_engine, 0, sizeof(g_engine));
    fake_states_reset();
    struct ToriRS_PluginEngine engine = fake_engine();
    engine.widget_request = fake_widget_request;
    state_host = PluginHost_New(&engine);
    struct ToriRS_PluginDef def = {
        .struct_size = sizeof(def), .id = "state-a", .title = "State", .version = "3",
        .callbacks = {.struct_size = sizeof(struct ToriRS_PluginCallbacks),
                      .on_start = state_watch_start}};
    CHECK(PluginHost_Register(state_host, &def) >= 0, "the state fixture registers");
    struct ToriRS_PluginDef plain = {
        .struct_size = sizeof(plain), .id = "state-plain", .title = "Plain", .version = "3",
        .callbacks = {.struct_size = sizeof(struct ToriRS_PluginCallbacks),
                      .on_start = plain_watch_start}};
    plain_binds = plain_state_events = 0;
    CHECK(PluginHost_Register(state_host, &plain) >= 0, "the plain-watch fixture registers");
    watched_native = (struct ToriRS_WidgetRef){{77, 2, 3}};
    struct ToriRS_WidgetState* row = fake_state_row(watched_native.opaque[1]);
    row->bounds = (struct ToriRS_WidgetBounds){10, 20, 30, 40};
    row->local = (struct ToriRS_WidgetBounds){1, 2, 30, 40};
    row->presented = true;
    row->input_present = true;
    state_events = 0; state_binds = 0; state_unbinds = 0; state_self_replace = 0;
    PluginHost_Start(state_host);
    PluginHost_WidgetsChanged(state_host, 77, 1);
    CHECK(state_binds == 1, "the state fixture's watch binds");

    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 0, "the first stamp after a binding is a baseline, not a change");
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 0, "an unchanged pass raises nothing");

    row->presented = false;
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 1, "a presentation change raises exactly one state event");
    CHECK(state_read_ok, "the callback can read the state that produced its event");
    CHECK(!state_last_read.presented, "the event's own state read shows the new value");

    /* Fourteen writes and one pass: the pass reports the WIDGET moving, not
     * each write, so a plugin that reacts does it once. */
    for( int i = 0; i < 14; ++i ) row->bounds.x = 100 + i;
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 2, "fourteen writes between two passes raise one event");
    CHECK(state_last_read.bounds.x == 113, "the event carries the last value written");

    row->own_hidden = true;
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 3, "a script hide raises a state event");
    CHECK(state_last_read.own_hidden, "the node's own hide bit is reported");
    CHECK(!state_last_read.native_hidden, "a script hide is not reported as native suppression");
    row->native_hidden = true;
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 4, "native suppression raises a state event of its own");
    CHECK(state_last_read.own_hidden, "the script hide still stands");
    CHECK(state_last_read.native_hidden, "native suppression is reported separately");

    row->bounds.y += 7;
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 5, "a move alone raises one state event");
    CHECK(plain_binds == 1 && plain_state_events == 0,
          "a plain watch on the same role saw its bind and never a state change");
    CHECK(state_last_read.bounds.y == 27, "the move is the one the adapter answered");

    /* The widget goes away and comes back: the stored state goes with it, so
     * the rebind opens on a baseline instead of a difference between two
     * different widgets. */
    watched_native.opaque[2] = 0;
    PluginHost_WidgetsChanged(state_host, 77, 2);
    CHECK(state_unbinds == 1, "closing the native widget unbinds the watch");
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 5, "an unbound watch has no widget to stamp");
    watched_native.opaque[2] = 3;
    PluginHost_WidgetsChanged(state_host, 77, 3);
    CHECK(state_binds == 2, "the watch rebinds when the widget returns");
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 5, "a rebind starts fresh, with no spurious state event");

    /* A callback that replaces its own subscription mid-dispatch. */
    state_self_replace = 1;
    row->bounds.width += 3;
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 6, "the replacing pass still delivered its event");
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 6, "the replacement subscription has no binding to stamp yet");
    PluginHost_WidgetsChanged(state_host, 77, 3);
    CHECK(state_binds == 3, "the replacement subscription binds in the next publication");
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 6, "the replacement subscription's first stamp is a baseline");
    row->bounds.height += 5;
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 7, "the replacement subscription follows its widget from then on");

    /*
     * The lane's own answers move on their own.
     *
     * A tab the player is given, the tab that is selected, a minimap the
     * server took away: none of them touches the geometry, the hide bits or
     * the art token, so a pass that compared only those would tell a frame
     * nothing and it would keep drawing the stone it was told about last. The
     * facets are part of the difference, and they are the WHOLE difference
     * here -- nothing else is written between these two passes.
     */
    row->facets = TORIRS_WIDGET_FACET_GIVEN;
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 8, "a facet moving on its own raises a state event");
    CHECK(state_last_read.facets == TORIRS_WIDGET_FACET_GIVEN,
          "the event carries the lane's facet bits");
    row->facets |= TORIRS_WIDGET_FACET_SELECTED;
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 9, "a second facet arriving beside the first is another change");
    CHECK(state_last_read.facets ==
              (TORIRS_WIDGET_FACET_GIVEN | TORIRS_WIDGET_FACET_SELECTED),
          "facets accumulate as a mask rather than replacing one another");
    PluginHost_WidgetStates(state_host);
    CHECK(state_events == 9, "and an unchanged facet mask raises nothing");
    PluginHost_Free(state_host);
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

/* ------------------------------------------------------------------------ *
 * The Quest Helper port's cache, game and script verbs, end to end through
 * the host: the range and context checks the host adds, and what reaches the
 * engine. The engine's own answers are the fakes' below.
 *
 * MUTATION 1: drop plugin_ctx_may_mutate from api_set_varc_int.
 *   Red: "a draw callback cannot write a varc".
 * MUTATION 2: drop the type range test in api_chat_add.
 *   Red: "a chat type past the store's rings is refused".
 * MUTATION 3: drop the SCRIPT_CALLBACK arm of api_script_run.
 *   Red: "scripts.run is refused inside on_script_callback".
 * MUTATION 4: fill only `capacity` in the def list and report it as count.
 *   Red: "a def list states its true count past the caller's room".
 * ------------------------------------------------------------------------ */
static int qh_varc_ints[8];
static char qh_varc_str[64];
static int qh_chat_type, qh_chat_calls;
static char qh_chat_sender[32], qh_chat_text[64];
static int qh_arrow_kind, qh_arrow_a, qh_arrow_b;
static int qh_sound_id, qh_sound_loops, qh_sound_delay, qh_sound_calls;
static int qh_run_calls, qh_run_args;
static int qh_fake_varc_int(void* u,int id){(void)u;return id>=0&&id<8?qh_varc_ints[id]:-1;}
static char const* qh_fake_varc_str(void* u,int id){(void)u;return id==3?qh_varc_str:"";}
static void qh_fake_set_varc_int(void* u,int id,int v){(void)u;if(id>=0&&id<8)qh_varc_ints[id]=v;}
static void qh_fake_set_varc_str(void* u,int id,char const* v){(void)u;if(id==3)snprintf(qh_varc_str,sizeof(qh_varc_str),"%s",v);}
static int qh_fake_enum_def(void* u,int id,struct ToriRS_EnumDef* out,struct ToriRS_EnumEntry* entries,int capacity)
{
    (void)u;
    if( id==6 ) return TORIRS_ASSET_PENDING;
    if( id!=5 ) return TORIRS_ASSET_MISSING;
    memset(out,0,sizeof(*out));
    out->id=5;out->key_type='i';out->value_type='i';out->default_int=-1;out->count=3;
    for( int i=0;i<3&&i<capacity;i++ ){entries[i].key=10+i;entries[i].int_value=100+i;entries[i].string_value[0]=0;}
    return TORIRS_ASSET_READY;
}
static void qh_fake_list(struct ToriRS_DefList* list,int const* v,int n)
{ list->count=n;for( int i=0;i<n&&i<list->capacity;i++ ) list->values[i]=v[i]; }
static int qh_fake_npc_def(void* u,int id,struct ToriRS_NpcDef* out)
{
    int const transforms[3]={11,12,-1},models[2]={100,101},from[1]={5},to[1]={6};
    (void)u;
    if( id!=10 ) return TORIRS_ASSET_MISSING;
    out->id=10;snprintf(out->name,sizeof(out->name),"Cook");snprintf(out->actions[0],sizeof(out->actions[0]),"Talk-to");
    out->size=1;out->combat_level=0;out->transform_varbit=-1;out->transform_varp=29;out->impostor=12;
    qh_fake_list(&out->transforms,transforms,3);qh_fake_list(&out->models,models,2);
    qh_fake_list(&out->recolor_from,from,1);qh_fake_list(&out->recolor_to,to,1);
    return TORIRS_ASSET_READY;
}
static int qh_fake_loc_def(void* u,int id,struct ToriRS_LocDef* out)
{
    (void)u;
    if( id!=20 ) return TORIRS_ASSET_PENDING;
    out->id=20;snprintf(out->name,sizeof(out->name),"Door");out->size_x=1;out->size_z=2;out->impostor=-1;
    out->map_scene=-1;out->map_icon=-1;qh_fake_list(&out->transforms,NULL,0);
    return TORIRS_ASSET_READY;
}
static void qh_fake_chat_add(void* u,int type,char const* sender,char const* text)
{
    (void)u;qh_chat_calls++;qh_chat_type=type;
    snprintf(qh_chat_sender,sizeof(qh_chat_sender),"%s",sender);snprintf(qh_chat_text,sizeof(qh_chat_text),"%s",text);
}
static int qh_fake_arrow_set(void* u,int kind,int a,int b)
{
    (void)u;
    if( kind==TORIRS_HINT_ARROW_NPC && a!=7 ) return 0;
    qh_arrow_kind=kind;qh_arrow_a=a;qh_arrow_b=b;return 1;
}
static int qh_fake_arrow_get(void* u,struct ToriRS_HintArrowInfo* out)
{
    (void)u;
    if( qh_arrow_kind==TORIRS_HINT_ARROW_NONE ) return 0;
    out->kind=(enum ToriRS_HintArrowKind)qh_arrow_kind;
    if( qh_arrow_kind==TORIRS_HINT_ARROW_TILE ){out->tile_x=qh_arrow_a;out->tile_z=qh_arrow_b;}
    if( qh_arrow_kind==TORIRS_HINT_ARROW_NPC ) out->npc_slot=qh_arrow_a;
    return 1;
}
static void qh_fake_sound(void* u,int id,int loops,int delay){(void)u;qh_sound_calls++;qh_sound_id=id;qh_sound_loops=loops;qh_sound_delay=delay;}
static int qh_fake_weight(void* u){(void)u;return 42;}
static int qh_fake_world_types(void* u){(void)u;return 1;}
static int qh_fake_tick(void* u){(void)u;return 777;}
static int qh_fake_script_run(void* u,int script_id,struct ToriRS_ScriptArg const* args,int count,struct ToriRS_ScriptResult* out)
{
    (void)u;qh_run_calls++;qh_run_args=count;
    if( script_id!=4029 ) return TORIRS_CONTRACT_PENDING;
    out->int_count=1;out->ints[0]=args[0].int_value==1?2:0;
    out->string_count=1;snprintf(out->strings[0],sizeof(out->strings[0]),"done");
    return TORIRS_CONTRACT_OK;
}

static int qh_varc_read,qh_varc_str_ok;static size_t qh_varc_required;static char qh_varc_copy[64];
static enum ToriRS_Result qh_set_ok,qh_set_bad_id,qh_setstr_ok;
static enum ToriRS_AssetState qh_enum_state,qh_enum_pending,qh_enum_count_state,qh_npc_state,qh_npc_missing,qh_loc_state,qh_loc_pending;
static struct ToriRS_EnumDef qh_enum;static struct ToriRS_EnumEntry qh_entries[2];
static struct ToriRS_NpcDef qh_npc;static int qh_npc_transforms[2];static int qh_npc_models[4];
static struct ToriRS_LocDef qh_loc;
static enum ToriRS_Result qh_chat_ok,qh_chat_bad,qh_tile_ok,qh_npc_ok,qh_npc_absent,qh_clear_ok,qh_tile_bad;
static bool qh_arrow_tile_read,qh_arrow_npc_read,qh_arrow_cleared_read;
static struct ToriRS_HintArrowInfo qh_arrow_tile_info,qh_arrow_npc_info;
static enum ToriRS_Result qh_sound_ok,qh_sound_bad;
static int qh_weight,qh_world_types,qh_tick;
static enum ToriRS_ContractResult qh_run_ok,qh_run_too_many,qh_run_pending,qh_run_draw,qh_run_script_cb;
static struct ToriRS_ScriptResult qh_run_result;
static enum ToriRS_Result qh_draw_set,qh_draw_chat,qh_draw_arrow,qh_draw_sound;
static int qh_draw_read;

static void qh_tick_cb(struct ToriRS_Api* api,void* state,struct ToriRS_TickEvent const* ev)
{
    struct ToriRS_GameApi const* game=api->game;
    struct ToriRS_ScriptArg args[TORIRS_SCRIPT_RUN_ARGS_MAX+1];
    (void)state;(void)ev;
    qh_set_ok=api->cache.set_varc_int(api,2,55);
    qh_set_bad_id=api->cache.set_varc_int(api,TORIRS_VARC_ID_MAX,1);
    qh_varc_read=api->cache.varc_int(api,2);
    qh_setstr_ok=api->cache.set_varc_str(api,3,"quest search");
    qh_varc_str_ok=api->cache.varc_str(api,3,qh_varc_copy,sizeof(qh_varc_copy),&qh_varc_required);

    qh_enum_state=api->cache.enum_def(api,5,&qh_enum,qh_entries,2);
    qh_enum_pending=api->cache.enum_def(api,6,&qh_enum,NULL,0);
    qh_enum_count_state=api->cache.enum_def(api,-1,&qh_enum,NULL,0);
    memset(&qh_npc,0,sizeof(qh_npc));
    qh_npc.transforms=(struct ToriRS_DefList){qh_npc_transforms,2,0};
    qh_npc.models=(struct ToriRS_DefList){qh_npc_models,4,0};
    qh_npc_state=api->cache.npc_def(api,10,&qh_npc);
    qh_npc_missing=api->cache.npc_def(api,11,&qh_npc);
    memset(&qh_loc,0,sizeof(qh_loc));
    qh_loc_state=api->cache.loc_def(api,20,&qh_loc);
    qh_loc_pending=api->cache.loc_def(api,21,&qh_loc);

    qh_chat_ok=game->chat_add(api,0,"","Quest Helper: started");
    qh_chat_bad=game->chat_add(api,TORIRS_PLUGIN_CHAT_TYPE_MAX,"","nope");
    qh_tile_ok=game->hint_arrow_tile(api,3222,3218,0);
    qh_arrow_tile_read=game->hint_arrow(api,&qh_arrow_tile_info);
    qh_tile_bad=game->hint_arrow_tile(api,3222,3218,4);
    qh_npc_absent=game->hint_arrow_npc(api,8);
    qh_npc_ok=game->hint_arrow_npc(api,7);
    qh_arrow_npc_read=game->hint_arrow(api,&qh_arrow_npc_info);
    qh_clear_ok=game->hint_arrow_clear(api);
    {
        struct ToriRS_HintArrowInfo none;
        qh_arrow_cleared_read=game->hint_arrow(api,&none);
    }
    qh_sound_ok=game->sound(api,2266,1,0);
    qh_sound_bad=game->sound(api,-1,1,0);
    qh_weight=game->weight(api);qh_world_types=game->world_types(api);qh_tick=game->tick(api);

    memset(args,0,sizeof(args));
    args[0].int_value=1;
    qh_run_ok=api->scripts.run(api->scripts.context,4029,args,1,&qh_run_result);
    {
        /* Its own result: every run clears what it is handed first. */
        struct ToriRS_ScriptResult scratch;
        qh_run_too_many=api->scripts.run(api->scripts.context,4029,args,TORIRS_SCRIPT_RUN_ARGS_MAX+1,&scratch);
        qh_run_pending=api->scripts.run(api->scripts.context,4030,args,1,&scratch);
    }
}
static void qh_draw_cb(struct ToriRS_Api* api,void* state,struct ToriRS_Graphics* graphics)
{
    struct ToriRS_ScriptResult result;
    (void)state;(void)graphics;
    qh_draw_set=api->cache.set_varc_int(api,2,99);
    qh_draw_read=api->cache.varc_int(api,2);
    qh_draw_chat=api->game->chat_add(api,0,"","in a frame");
    qh_draw_arrow=api->game->hint_arrow_clear(api);
    qh_draw_sound=api->game->sound(api,2266,1,0);
    qh_run_draw=api->scripts.run(api->scripts.context,4029,NULL,0,&result);
}
static void qh_script_cb(struct ToriRS_Api* api,void* state,struct ToriRS_ScriptEvent const* event)
{
    struct ToriRS_ScriptResult result;
    (void)state;(void)event;
    qh_run_script_cb=api->scripts.run(api->scripts.context,4029,NULL,0,&result);
}
static int qh_bare_varc_int,qh_bare_tick;static bool qh_bare_varc_str,qh_bare_arrow;
static enum ToriRS_Result qh_bare_set,qh_bare_chat;static enum ToriRS_AssetState qh_bare_enum;
static enum ToriRS_ContractResult qh_bare_run;
static void qh_bare_tick_cb(struct ToriRS_Api* api,void* state,struct ToriRS_TickEvent const* ev)
{
    struct ToriRS_EnumDef e;struct ToriRS_HintArrowInfo arrow;struct ToriRS_ScriptResult result;size_t required;
    (void)state;(void)ev;
    qh_bare_varc_int=api->cache.varc_int(api,1);
    qh_bare_varc_str=api->cache.varc_str(api,1,NULL,0,&required);
    qh_bare_set=api->cache.set_varc_int(api,1,1);
    qh_bare_enum=api->cache.enum_def(api,1,&e,NULL,0);
    qh_bare_chat=api->game->chat_add(api,0,"","x");
    qh_bare_arrow=api->game->hint_arrow(api,&arrow);
    qh_bare_tick=api->game->tick(api);
    qh_bare_run=api->scripts.run(api->scripts.context,1,NULL,0,&result);
}

/* ------------------------------------------------------------------------ */
/* Quest Helper port: widget detail / child lists / native text / items /     */
/* listeners, and the three engine-news events.                               */
/* ------------------------------------------------------------------------ */

static int qhw_kind = -1, qhw_requests;
static char qhw_text[64];
static int qhw_a, qhw_b, qhw_id;
static enum ToriRS_ContractResult
qhw_request(void* user, uint64_t owner, struct PluginWidgetRequest* r)
{
    (void)user;(void)owner;
    qhw_kind = (int)r->kind;
    ++qhw_requests;
    switch( r->kind )
    {
    case PLUGIN_WIDGET_DETAIL:
        r->detail->id = 0x00e70001; r->detail->index = 3; r->detail->type = 4;
        snprintf(r->detail->text, sizeof(r->detail->text), "%s", "Yes please");
        snprintf(r->detail->actions[0], sizeof(r->detail->actions[0]), "%s", "Continue");
        r->detail->action_count = 1;
        return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_DYNAMIC_CHILDREN:
    case PLUGIN_WIDGET_STATIC_CHILDREN:
    case PLUGIN_WIDGET_NESTED_CHILDREN:
    {
        size_t const n = r->kind == PLUGIN_WIDGET_DYNAMIC_CHILDREN ? 2 : 1;
        for( size_t i = 0; i < n && i < r->capacity; i++ )
            r->refs[i] = (struct ToriRS_WidgetRef){{77, 50 + i, 1}};
        *r->count = n;
        return n > r->capacity ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
    }
    case PLUGIN_WIDGET_LISTENER:
        if( strcmp(r->name, "onLoad") == 0 ) return TORIRS_CONTRACT_UNAVAILABLE;
        *r->count = 3;
        if( r->capacity < 3 ) return TORIRS_CONTRACT_BUDGET_EXCEEDED;
        r->listener_args[0] = (struct ToriRS_WidgetListenerArg){.int_value = 277};
        r->listener_args[1] = (struct ToriRS_WidgetListenerArg){.int_value = -2147483645};
        r->listener_args[2] = (struct ToriRS_WidgetListenerArg){.is_string = true, .string_value = "Search"};
        return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_NATIVE_TEXT:
        snprintf(qhw_text, sizeof(qhw_text), "%s", r->name);
        return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_NATIVE_TEXT_COLOR:
    case PLUGIN_WIDGET_SET_SPRITE:
        qhw_a = r->a;
        return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_CREATE_ITEM:
        *r->refs = (struct ToriRS_WidgetRef){{77, 60, 1}};
        return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_SET_ITEM:
        qhw_a = r->a; qhw_b = r->b; qhw_id = r->id;
        return TORIRS_CONTRACT_PENDING;
    default:
        return TORIRS_CONTRACT_OK;
    }
}

static void qhw_start(struct ToriRS_Api* api, void* state)
{
    struct ToriRS_WidgetApi* ui = &api->widgets;
    struct ToriRS_WidgetRef const w = {{77, 2, 3}};
    struct ToriRS_WidgetRef refs[4], item;
    struct ToriRS_WidgetDetail d = {.struct_size = sizeof(d)};
    struct ToriRS_WidgetListenerArg args[4];
    size_t count = 99;
    (void)state;

    CHECK(ui->detail(ui->context, w, &d) == TORIRS_CONTRACT_OK && qhw_kind == PLUGIN_WIDGET_DETAIL &&
          d.id == 0x00e70001 && d.index == 3 && strcmp(d.text, "Yes please") == 0 && d.action_count == 1 &&
          d.struct_size == sizeof(d),
          "detail reaches the adapter and keeps the caller's struct_size");
    CHECK(ui->dynamic_children(ui->context, w, NULL, 0, &count) == TORIRS_CONTRACT_BUDGET_EXCEEDED && count == 2,
          "a zero-capacity dynamic list answers its count");
    CHECK(ui->dynamic_children(ui->context, w, refs, 4, &count) == TORIRS_CONTRACT_OK && count == 2 &&
          qhw_kind == PLUGIN_WIDGET_DYNAMIC_CHILDREN && refs[1].opaque[1] == 51,
          "dynamic children are the dynamic list");
    CHECK(ui->static_children(ui->context, w, refs, 4, &count) == TORIRS_CONTRACT_OK && count == 1 &&
          qhw_kind == PLUGIN_WIDGET_STATIC_CHILDREN, "static children are the static list");
    CHECK(ui->nested_children(ui->context, w, refs, 4, &count) == TORIRS_CONTRACT_OK && count == 1 &&
          qhw_kind == PLUGIN_WIDGET_NESTED_CHILDREN, "nested children are the nested list");

    CHECK(ui->set_native_text(ui->context, w, "<col=00ff00>Yes please") == TORIRS_CONTRACT_OK &&
          qhw_kind == PLUGIN_WIDGET_NATIVE_TEXT && strcmp(qhw_text, "<col=00ff00>Yes please") == 0,
          "a native text edit reaches the adapter");
    {
        static char too_long[TORIRS_WIDGET_NATIVE_TEXT_MAX + 2];
        int const before = qhw_requests;
        memset(too_long, 'a', sizeof(too_long) - 1);
        CHECK(ui->set_native_text(ui->context, w, too_long) == TORIRS_CONTRACT_BUDGET_EXCEEDED &&
              qhw_requests == before, "an over-long native text never reaches the adapter");
    }
    CHECK(ui->set_native_text_color(ui->context, w, 0x1000000) == TORIRS_CONTRACT_INVALID_ARGUMENT,
          "a native text colour is 0xRRGGBB");
    CHECK(ui->set_native_text_color(ui->context, w, 0x00ff00) == TORIRS_CONTRACT_OK && qhw_a == 0x00ff00 &&
          qhw_kind == PLUGIN_WIDGET_NATIVE_TEXT_COLOR, "a native text colour reaches the adapter");

    CHECK(ui->create_item(ui->context, w, "", &item) == TORIRS_CONTRACT_INVALID_ARGUMENT, "an owned item needs a key");
    CHECK(ui->create_item(ui->context, w, "bank-1", &item) == TORIRS_CONTRACT_OK && item.opaque[1] == 60 &&
          qhw_kind == PLUGIN_WIDGET_CREATE_ITEM, "owned item creation returns the adapter's child");
    CHECK(ui->set_item(ui->context, item, -2, 1, -1) == TORIRS_CONTRACT_INVALID_ARGUMENT, "an obj id below -1 is refused");
    CHECK(ui->set_item(ui->context, item, 995, -1, -1) == TORIRS_CONTRACT_INVALID_ARGUMENT, "a negative quantity is refused");
    CHECK(ui->set_item(ui->context, item, 995, 1000, -1) == TORIRS_CONTRACT_PENDING && qhw_a == 995 && qhw_b == 1000 &&
          qhw_id == -1, "set_item passes the adapter's pending through");
    CHECK(ui->set_item(ui->context, item, 995, 1, 3) == TORIRS_CONTRACT_INVALID_ARGUMENT &&
          ui->set_item(ui->context, item, 995, 1, -2) == TORIRS_CONTRACT_INVALID_ARGUMENT,
          "a quantity mode outside -1..2 is refused");
    CHECK(ui->set_item(ui->context, item, 995, 1, 1) == TORIRS_CONTRACT_PENDING && qhw_id == 1,
          "the quantity mode (ItemQuantityMode.ALWAYS) reaches the adapter");
    CHECK(ui->set_sprite(ui->context, w, -1) == TORIRS_CONTRACT_INVALID_ARGUMENT, "a sprite id is never negative");
    CHECK(ui->set_sprite(ui->context, w, 1453) == TORIRS_CONTRACT_OK && qhw_a == 1453 &&
          qhw_kind == PLUGIN_WIDGET_SET_SPRITE, "a sprite reaches the adapter");

    CHECK(ui->listener(ui->context, w, "", args, 4, &count) == TORIRS_CONTRACT_INVALID_ARGUMENT,
          "a listener needs a name");
    CHECK(ui->listener(ui->context, w, "onInvTransmit", args, 4, &count) == TORIRS_CONTRACT_OK && count == 3 &&
          args[0].int_value == 277 && args[1].int_value == -2147483645 && args[2].is_string &&
          strcmp(args[2].string_value, "Search") == 0, "a listener answers RuneLite's Object[] order");
    CHECK(ui->listener(ui->context, w, "onLoad", args, 4, &count) == TORIRS_CONTRACT_UNAVAILABLE,
          "an absent listener is unavailable");
}
static void qhw_draw(struct ToriRS_Api* api, void* state, struct ToriRS_Graphics* graphics)
{
    struct ToriRS_WidgetApi* ui = &api->widgets;
    struct ToriRS_WidgetRef const w = {{77, 2, 3}};
    struct ToriRS_WidgetDetail d = {.struct_size = sizeof(d)};
    size_t count = 0;
    (void)state;(void)graphics;
    CHECK(ui->detail(ui->context, w, &d) == TORIRS_CONTRACT_OK, "paint may read a widget's detail");
    CHECK(ui->dynamic_children(ui->context, w, NULL, 0, &count) == TORIRS_CONTRACT_BUDGET_EXCEEDED && count == 2,
          "paint may read the child lists");
    CHECK(ui->set_native_text(ui->context, w, "x") == TORIRS_CONTRACT_WRONG_CONTEXT, "paint cannot edit native text");
    CHECK(ui->set_native_text_color(ui->context, w, 1) == TORIRS_CONTRACT_WRONG_CONTEXT, "paint cannot recolour native text");
    CHECK(ui->set_item(ui->context, w, 1, 1, -1) == TORIRS_CONTRACT_WRONG_CONTEXT, "paint cannot set an item");
    CHECK(ui->set_sprite(ui->context, w, 1) == TORIRS_CONTRACT_WRONG_CONTEXT, "paint cannot set a sprite");
}
static void qhw_stop(struct ToriRS_Api* api, void* state)
{
    struct ToriRS_WidgetRef out;
    (void)state;
    CHECK(api->widgets.create_item(api->widgets.context, (struct ToriRS_WidgetRef){{77, 2, 3}}, "late", &out) ==
              TORIRS_CONTRACT_WRONG_CONTEXT, "shutdown cannot create an owned item");
}

/* The engine-news events. */
static struct ToriRS_PluginHost* qhe_host;
static int32_t qhe_values[8];
static int qhe_count, qhe_value_calls;
static int qhe_var_events, qhe_last_varp, qhe_last_value, qhe_last_old;
static char qhe_trace[128];
static int qhe_varp_values(void* user, int32_t const** out)
{
    (void)user;
    ++qhe_value_calls;
    *out = qhe_values;
    return qhe_count;
}
static void qhe_var(struct ToriRS_Api* api, void* state, struct ToriRS_VarChangedEvent const* ev)
{
    (void)api;(void)state;
    ++qhe_var_events;
    qhe_last_varp = ev->varp; qhe_last_value = ev->value; qhe_last_old = ev->old;
}
static void qhe_interface(struct ToriRS_Api* api, void* state, struct ToriRS_InterfaceEvent const* ev)
{
    char step[24];
    (void)state;
    snprintf(step, sizeof(step), "%c%d ", ev->open ? '+' : '-', ev->group);
    strncat(qhe_trace, step, sizeof(qhe_trace) - strlen(qhe_trace) - 1);
    /* A mount raised from a handler waits for the next frame. */
    if( ev->group == 999 )
        PluginHost_InterfaceChanged(qhe_host, 1000, true);
    (void)api;
}
static void qhe_inventory(struct ToriRS_Api* api, void* state, struct ToriRS_InventoryChangedEvent const* ev)
{
    char step[24];
    (void)api;(void)state;
    snprintf(step, sizeof(step), "i%d ", ev->inv);
    strncat(qhe_trace, step, sizeof(qhe_trace) - strlen(qhe_trace) - 1);
}
/* The fix round's seams: cache.sprite_image, panel.show, on_loc_changed and
 * on_map_anim. */
static uint32_t qhf_sprite[4] = { 0xFF102030u, 0x00000000u, 0xFF405060u, 0xFF708090u };
static int qhf_sprite_calls;
static int qhf_sprite_pixels(void* user, int id, int frame, uint32_t const** out, int* w, int* h)
{
    (void)user;
    ++qhf_sprite_calls;
    if( id == 5 && frame == 0 ) { *out = qhf_sprite; *w = 2; *h = 2; return 1; }
    if( id == 6 ) return 0;
    return -1;
}
static enum ToriRS_AssetState qhf_s5, qhf_s6, qhf_s7, qhf_s5f1, qhf_bad;
static uint32_t qhf_s5_ref;
static char qhf_trace[256];
static void qhf_tick(struct ToriRS_Api* api, void* state, struct ToriRS_TickEvent const* ev)
{
    struct ToriRS_ImageRef r = { 0 };
    (void)state;(void)ev;
    qhf_s5 = api->cache.sprite_image(api, 5, 0, &r); qhf_s5_ref = (uint32_t)r.value;
    qhf_s6 = api->cache.sprite_image(api, 6, 0, &r);
    qhf_s7 = api->cache.sprite_image(api, 7, 0, &r);
    qhf_s5f1 = api->cache.sprite_image(api, 5, 1, &r);
    qhf_bad = api->cache.sprite_image(api, -1, 0, &r);
}
static void qhf_start(struct ToriRS_Api* api, void* state)
{
    struct ToriRS_PanelDescriptor panel = { .preferred_width = 320 };
    (void)state;
    CHECK(api->panel.request(api, &panel) == TORIRS_RESULT_OK, "the fix fixture registers a page");
}
static void qhf_frame(struct ToriRS_Api* api, void* state, struct ToriRS_FrameEvent const* ev)
{
    (void)state;(void)ev;
    if( qhf_trace[0] == 'S' )
    {
        qhf_trace[0] = 0;
        api->panel.show(api);
    }
}
static void qhf_loc(struct ToriRS_Api* api, void* state, struct ToriRS_LocChangedEvent const* ev)
{
    char step[48];
    (void)api;(void)state;
    snprintf(step, sizeof(step), "L%d:%d>%d ", ev->tile_x, ev->old_loc_id, ev->loc_id);
    strncat(qhf_trace, step, sizeof(qhf_trace) - strlen(qhf_trace) - 1);
}
static void qhf_map_anim(struct ToriRS_Api* api, void* state, struct ToriRS_MapAnimEvent const* ev)
{
    char step[48];
    (void)api;(void)state;
    snprintf(step, sizeof(step), "G%d@%d ", ev->spotanim, ev->tile_x);
    strncat(qhf_trace, step, sizeof(qhf_trace) - strlen(qhf_trace) - 1);
}
static void qhf_item_search(struct ToriRS_Api* api, void* state, struct ToriRS_ItemSearchEvent* ev)
{
    (void)api;(void)state;
    if( strcmp(ev->query, "quest-helper") != 0 ) return;
    ev->consumed = true;
    ev->count = 3; ev->ids[0] = 952; ev->ids[1] = 995; ev->ids[2] = 1925;
}
/* ---- script hooks: scripts.watch + on_script_pre / on_script_post ---- */
static int sh_watch_calls[4];   /* [0] on 505, [1] off 505, [2] on 514, [3] off 514 */
static char sh_trace[256];
static int sh_run_result;
static bool sh_engine_watch(void* user, int script_id, bool on)
{
    (void)user;
    if( script_id == 505 ) sh_watch_calls[on ? 0 : 1]++;
    if( script_id == 514 ) sh_watch_calls[on ? 2 : 3]++;
    return script_id >= 0 && script_id <= 65535;
}
static int sh_engine_run(void* user, int script_id, struct ToriRS_ScriptArg const* args, int arg_count,
                         struct ToriRS_ScriptResult* out)
{
    (void)user;(void)script_id;(void)args;(void)arg_count;(void)out;
    return TORIRS_CONTRACT_OK;
}
struct ShStack { int32_t ints[3]; };
static int32_t sh_get_int(void* user, size_t i) { return ((struct ShStack*)user)->ints[2 - i]; }
static void sh_set_int(void* user, size_t i, int32_t v) { ((struct ShStack*)user)->ints[2 - i] = v; }
static char const* sh_get_string(void* user, size_t i) { (void)user;(void)i; return ""; }
static bool sh_set_string(void* user, size_t i, char const* v) { (void)user;(void)i;(void)v; return false; }
static void sh_hook(struct ToriRS_Api* api, struct ToriRS_ScriptHookEvent const* ev, char tag)
{
    size_t ints = 0, strings = 0;
    int32_t top = -1;
    char step[48];
    CHECK(api->scripts.counts(api->scripts.context, ev->ref, &ints, &strings) == TORIRS_CONTRACT_OK,
          "a hook's ref answers counts");
    (void)api->scripts.get_int(api->scripts.context, ev->ref, 0, &top);
    snprintf(step, sizeof(step), "%c%d:%zu:%d%s ", tag, ev->script_id, ints, top, ev->invoked ? "i" : "");
    strncat(sh_trace, step, sizeof(sh_trace) - strlen(sh_trace) - 1);
    if( tag == 'Q' && ev->script_id == 514 )
        (void)api->scripts.set_int(api->scripts.context, ev->ref, 0, 1);
    {
        struct ToriRS_ScriptResult result;
        sh_run_result = api->scripts.run(api->scripts.context, 77, NULL, 0, &result);
    }
}
static void sh_a_pre(struct ToriRS_Api* api, void* state, struct ToriRS_ScriptHookEvent const* ev) { (void)state; sh_hook(api, ev, 'a'); }
static void sh_a_post(struct ToriRS_Api* api, void* state, struct ToriRS_ScriptHookEvent const* ev) { (void)state; sh_hook(api, ev, 'A'); }
static void sh_b_post(struct ToriRS_Api* api, void* state, struct ToriRS_ScriptHookEvent const* ev) { (void)state; sh_hook(api, ev, 'Q'); }
static void sh_a_start(struct ToriRS_Api* api, void* state)
{
    (void)state;
    CHECK(api->scripts.watch(api->scripts.context, 505, true) == TORIRS_CONTRACT_OK, "a plugin watches a script");
    CHECK(api->scripts.watch(api->scripts.context, 505, true) == TORIRS_CONTRACT_OK, "restating a watch is fine");
    CHECK(api->scripts.watch(api->scripts.context, 70000, true) == TORIRS_CONTRACT_INVALID_ARGUMENT,
          "an id past 65535 is refused");
}
static void sh_b_start(struct ToriRS_Api* api, void* state)
{
    (void)state;
    CHECK(api->scripts.watch(api->scripts.context, 505, true) == TORIRS_CONTRACT_OK, "a second plugin watches the same id");
    CHECK(api->scripts.watch(api->scripts.context, 514, true) == TORIRS_CONTRACT_OK, "and another");
}
static void test_script_hooks(void)
{
    struct ToriRS_PluginEngine engine = fake_engine();
    engine.script_watch = sh_engine_watch;
    engine.script_run = sh_engine_run;
    struct ToriRS_PluginHost* host = PluginHost_New(&engine);
    struct ToriRS_PluginDef a = {.struct_size=sizeof(a),.id="sh-a",.title="SH A",.version="1",
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_start=sh_a_start,
                    .on_script_pre=sh_a_pre,.on_script_post=sh_a_post}};
    struct ToriRS_PluginDef b = {.struct_size=sizeof(b),.id="sh-b",.title="SH B",.version="1",
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_start=sh_b_start,
                    .on_script_post=sh_b_post}};
    struct ShStack values = {{ 10, 20, 0 }};
    struct PluginScriptStack stack = {.user=&values,.int_count=3,.string_count=0,.writable_ints=7,
        .get_int=sh_get_int,.set_int=sh_set_int,.get_string=sh_get_string,.set_string=sh_set_string};
    int const ia = PluginHost_Register(host, &a);
    int const ib = PluginHost_Register(host, &b);
    memset(sh_watch_calls, 0, sizeof(sh_watch_calls));
    sh_trace[0] = 0;
    CHECK(ia >= 0 && ib >= 0, "both hook plugins register");
    PluginHost_Start(host);
    CHECK(sh_watch_calls[0] == 1 && sh_watch_calls[2] == 1,
          "the engine hears each watched id once, however many plugins watch it");

    PluginHost_ScriptHook(host, false, 505, true, &stack);
    PluginHost_ScriptHook(host, true, 505, true, &stack);
    PluginHost_ScriptHook(host, true, 514, true, &stack);
    PluginHost_ScriptHook(host, false, 777, false, &stack);
    CHECK(strcmp(sh_trace, "a505:3:0i A505:3:0i Q505:3:0i Q514:3:0i ") == 0,
          "each hook reaches only the plugins watching that script, with its values");
    CHECK(values.ints[2] == 1, "a post hook writes the value the script returned");
    CHECK(sh_run_result == TORIRS_CONTRACT_WRONG_CONTEXT, "no script can be run from inside a hook");

    PluginHost_SetEnabled(host, ia, false);
    CHECK(sh_watch_calls[1] == 0, "an id another plugin still watches stays watched");
    PluginHost_SetEnabled(host, ib, false);
    CHECK(sh_watch_calls[1] == 1 && sh_watch_calls[3] == 1, "the last plugin to stop takes its watches with it");
    sh_trace[0] = 0;
    PluginHost_ScriptHook(host, false, 505, true, &stack);
    CHECK(sh_trace[0] == 0, "a stopped plugin hears no hook");
    PluginHost_Free(host);
}

static void test_quest_helper_fix_seams(void)
{
    struct ToriRS_PluginEngine engine = fake_engine();
    engine.sprite_pixels = qhf_sprite_pixels;
    struct ToriRS_PluginHost* host = PluginHost_New(&engine);
    struct ToriRS_PluginDef def = {.struct_size=sizeof(def),.id="qh-fix",.title="QH fix",.version="3",
        .flags=TORIRS_PLUGIN_DISABLED_BY_DEFAULT,
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_start=qhf_start,.on_server_tick=qhf_tick,
                    .on_frame_start=qhf_frame,.on_loc_changed=qhf_loc,.on_map_anim=qhf_map_anim,
                    .on_item_search=qhf_item_search}};
    struct ToriRS_LocChangedEvent door = { 3226, 3240, 0, 0, 1, 1531, 1530, 0, 0 };
    struct ToriRS_MapAnimEvent puff = { 3227, 3241, 0, 86, 0, 0, 5 };
    int const index = PluginHost_Register(host, &def);
    CHECK(index >= 0, "fix fixture registers");
    PluginHost_Start(host);
    qhf_trace[0] = 0;

    {
        int ids[4] = { -7, -7, -7, -7 };
        CHECK(PluginHost_ItemSearch(host, "quest-helper", true, ids, 4) == -1 && ids[0] == -7,
              "a switched-off subscriber is not asked the GE search");
    }
    /* Switched off: nothing queued. */
    PluginHost_LocChanged(host, &door);
    PluginHost_MapAnim(host, &puff);
    PluginHost_FrameStart(host, 0, 0);
    CHECK(qhf_trace[0] == 0, "a switched-off subscriber queues no loc change or graphic");

    PluginHost_SetEnabled(host, index, true);
    PluginHost_ServerTick(host, 1);
    {
        int ids[4] = { 0 };
        int two[2] = { 0 };
        CHECK(PluginHost_ItemSearch(host, "spade", true, ids, 4) == -1, "an unanswered GE search is the client's");
        CHECK(PluginHost_ItemSearch(host, "quest-helper", true, ids, 4) == 3 && ids[0] == 952 && ids[2] == 1925,
              "an answered GE search hands back the plugin's ids");
        CHECK(PluginHost_ItemSearch(host, "quest-helper", true, two, 2) == 2 && two[1] == 995,
              "the answer is cut to the caller's room");
    }
    CHECK(qhf_s5 == TORIRS_ASSET_READY && qhf_s5_ref != 0, "a resident sprite frame is an image");
    CHECK(g_composed.w == 2 && g_composed.h == 2 && g_composed.argb[2] == 0xFF405060u,
          "the sprite's own pixels are what was composed");
    CHECK(qhf_s6 == TORIRS_ASSET_PENDING, "a sprite still loading is PENDING");
    CHECK(qhf_s7 == TORIRS_ASSET_MISSING && qhf_s5f1 == TORIRS_ASSET_MISSING,
          "no such sprite, or a frame past its last, is MISSING");
    CHECK(qhf_bad == TORIRS_ASSET_INVALID, "a negative sprite id is bad input");

    PluginHost_LocChanged(host, &door);
    door.old_loc_id = 1531; door.loc_id = -1; door.tile_x = 3227;
    PluginHost_LocChanged(host, &door);
    PluginHost_MapAnim(host, &puff);
    CHECK(qhf_trace[0] == 0, "loc changes wait for the frame boundary");
    PluginHost_FrameStart(host, 0, 0);
    CHECK(strcmp(qhf_trace, "L3226:1530>1531 L3227:1531>-1 G86@3227 ") == 0,
          "loc changes arrive in landing order, then the graphics");
    qhf_trace[0] = 0;
    for( int i = 0; i < PLUGIN_HOST_LOC_QUEUE_MAX + 3; i++ )
    {
        door.tile_x = i;
        PluginHost_LocChanged(host, &door);
    }
    PluginHost_FrameStart(host, 0, 0);
    CHECK(strncmp(qhf_trace, "L3:", 3) == 0, "past the queue's room the oldest loc changes are dropped");
    qhf_trace[0] = 0;

    CHECK(PluginHost_PanelActive(host) != index, "the page is not up before panel.show");
    qhf_trace[0] = 'S'; qhf_trace[1] = 0;
    PluginHost_FrameStart(host, 0, 0);
    CHECK(PluginHost_PanelActive(host) != index, "panel.show waits for the next frame start");
    PluginHost_FrameStart(host, 0, 0);
    CHECK(PluginHost_PanelActive(host) == index, "panel.show opens the plugin's page");
    qhf_trace[0] = 'S'; qhf_trace[1] = 0;
    PluginHost_FrameStart(host, 0, 0);
    PluginHost_FrameStart(host, 0, 0);
    CHECK(PluginHost_PanelActive(host) == index, "panel.show never collapses a page that is up");
    PluginHost_Free(host);
}

static void test_quest_helper_widgets_events(void)
{
    {
        struct ToriRS_PluginEngine engine = fake_engine();
        engine.widget_request = qhw_request;
        struct ToriRS_PluginHost* host = PluginHost_New(&engine);
        struct ToriRS_PluginDef def = {.struct_size=sizeof(def),.id="qh-widgets",.title="QH widgets",.version="3",
            .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_start=qhw_start,.on_stop=qhw_stop,
                        .on_draw_canvas=qhw_draw}};
        CHECK(PluginHost_Register(host, &def) >= 0, "widget fixture registers");
        PluginHost_Start(host);
        PluginHost_DrawCanvas(host, 765, 503);
        PluginHost_Free(host);
    }
    {
        struct ToriRS_PluginEngine engine = fake_engine();
        engine.varp_values = qhe_varp_values;
        struct ToriRS_PluginHost* host = PluginHost_New(&engine);
        qhe_host = host;
        struct ToriRS_PluginDef listener = {.struct_size=sizeof(listener),.id="qh-events",.title="QH events",.version="3",
            .flags=TORIRS_PLUGIN_DISABLED_BY_DEFAULT,
            .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_var_changed=qhe_var,
                        .on_interface=qhe_interface,.on_inventory_changed=qhe_inventory}};
        struct ToriRS_PluginDef bystander = {.struct_size=sizeof(bystander),.id="qh-bystander",.title="Bystander",.version="3",
            .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks)}};
        int const index = PluginHost_Register(host, &listener);
        CHECK(index >= 0 && PluginHost_Register(host, &bystander) >= 0, "event fixtures register");
        PluginHost_Start(host);
        qhe_count = 4; qhe_values[0] = 1; qhe_values[1] = 2; qhe_values[2] = 3; qhe_values[3] = 5;
        qhe_value_calls = qhe_var_events = 0; qhe_trace[0] = 0;

        /* Disabled: nothing is asked, nothing is queued. */
        PluginHost_ServerTick(host, 1);
        PluginHost_InterfaceChanged(host, 162, true);
        PluginHost_InventoryChanged(host, 93);
        PluginHost_FrameStart(host, 0, 0);
        CHECK(qhe_value_calls == 0 && qhe_trace[0] == 0,
              "a subscriber that is switched off costs no varp read and queues no news");

        PluginHost_SetEnabled(host, index, true);
        PluginHost_ServerTick(host, 2);
        CHECK(qhe_value_calls == 1 && qhe_var_events == 0, "the first tick only records the values");
        qhe_values[3] = 7;
        PluginHost_ServerTick(host, 3);
        CHECK(qhe_var_events == 1 && qhe_last_varp == 3 && qhe_last_value == 7 && qhe_last_old == 5,
              "a changed varp is one event with its old value");
        PluginHost_ServerTick(host, 4);
        CHECK(qhe_var_events == 1, "an unchanged tick raises nothing");
        qhe_values[1] = 9; qhe_values[2] = 4;
        qhe_count = 6; qhe_values[4] = 11; qhe_values[5] = 12;
        PluginHost_ServerTick(host, 5);
        CHECK(qhe_var_events == 3 && qhe_last_varp == 2 && qhe_last_old == 3,
              "every changed varp is reported once, and a grown table only records its new varps");
        qhe_values[5] = 13;
        PluginHost_ServerTick(host, 6);
        CHECK(qhe_var_events == 4 && qhe_last_varp == 5 && qhe_last_old == 12, "a recorded new varp reports its next change");

        PluginHost_InterfaceChanged(host, 162, true);
        PluginHost_InterfaceChanged(host, 231, true);
        PluginHost_InventoryChanged(host, 93);
        PluginHost_InventoryChanged(host, 94);
        PluginHost_InventoryChanged(host, 93);
        PluginHost_InterfaceChanged(host, 231, false);
        PluginHost_InterfaceChanged(host, 999, true);
        CHECK(qhe_trace[0] == 0, "news waits for the frame boundary");
        PluginHost_FrameStart(host, 0, 0);
        CHECK(strcmp(qhe_trace, "+162 +231 -231 +999 i93 i94 ") == 0,
              "mounts arrive in order, then each changed inventory once");
        qhe_trace[0] = 0;
        PluginHost_FrameStart(host, 0, 0);
        CHECK(strcmp(qhe_trace, "+1000 ") == 0, "a mount raised from a handler arrives the next frame");

        PluginHost_SetEnabled(host, index, false);
        int const calls = qhe_value_calls;
        PluginHost_ServerTick(host, 7);
        CHECK(qhe_value_calls == calls, "switching the subscriber off stops the varp reads");
        PluginHost_SetEnabled(host, index, true);
        qhe_values[0] = 40;
        PluginHost_ServerTick(host, 8);
        CHECK(qhe_var_events == 4, "a restarted subscriber is re-seeded, never handed an old change");
        PluginHost_Free(host);
        qhe_host = NULL;
    }
}

static void test_quest_helper_cache_game_scripts(void)
{
    struct ToriRS_PluginEngine engine=fake_engine();
    engine.capability=script_test_capability;
    engine.varc_int=qh_fake_varc_int;engine.varc_str=qh_fake_varc_str;
    engine.set_varc_int=qh_fake_set_varc_int;engine.set_varc_str=qh_fake_set_varc_str;
    engine.enum_def=qh_fake_enum_def;engine.npc_def=qh_fake_npc_def;engine.loc_def=qh_fake_loc_def;
    engine.chat_add=qh_fake_chat_add;engine.hint_arrow_set=qh_fake_arrow_set;engine.hint_arrow_get=qh_fake_arrow_get;
    engine.sound=qh_fake_sound;engine.weight=qh_fake_weight;engine.world_types=qh_fake_world_types;engine.tick=qh_fake_tick;
    engine.script_run=qh_fake_script_run;
    struct ToriRS_PluginHost* host=PluginHost_New(&engine);
    struct ToriRS_PluginDef def={.struct_size=sizeof(def),.id="qh-seams",.title="QH",.version="3",
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_server_tick=qh_tick_cb,
            .on_draw_canvas=qh_draw_cb,.on_script_callback=qh_script_cb}};
    memset(qh_varc_ints,0xff,sizeof(qh_varc_ints));qh_varc_str[0]=0;
    qh_chat_calls=0;qh_sound_calls=0;qh_run_calls=0;qh_arrow_kind=TORIRS_HINT_ARROW_NONE;
    CHECK(PluginHost_Register(host,&def)>=0,"cache/game/scripts fixture registers");
    PluginHost_Start(host);
    PluginHost_ServerTick(host,1);

    CHECK(qh_set_ok==TORIRS_RESULT_OK && qh_varc_read==55,"a varc int write lands in the store a read sees");
    CHECK(qh_set_bad_id==TORIRS_RESULT_INVALID,"a varc id past TORIRS_VARC_ID_MAX is refused");
    CHECK(qh_setstr_ok==TORIRS_RESULT_OK && qh_varc_str_ok && strcmp(qh_varc_copy,"quest search")==0 &&
          qh_varc_required==strlen("quest search")+1,
          "a varc string is copied out with its size");
    CHECK(qh_enum_state==TORIRS_ASSET_READY && qh_enum.count==3 && qh_enum.key_type=='i' &&
          qh_entries[0].key==10 && qh_entries[1].int_value==101,
          "an enum fills its header and as many entries as there is room for");
    CHECK(qh_enum_pending==TORIRS_ASSET_PENDING,"a loading enum answers PENDING");
    CHECK(qh_enum_count_state==TORIRS_ASSET_INVALID,"a negative enum id is bad input");
    CHECK(qh_npc_state==TORIRS_ASSET_READY && strcmp(qh_npc.name,"Cook")==0 && qh_npc.impostor==12 &&
          strcmp(qh_npc.actions[0],"Talk-to")==0,
          "an npc def comes through");
    CHECK(qh_npc.transforms.count==3 && qh_npc_transforms[0]==11 && qh_npc_transforms[1]==12,
          "a def list states its true count past the caller's room");
    CHECK(qh_npc.models.count==2 && qh_npc_models[1]==101,"a list with room is filled whole");
    CHECK(qh_npc.recolor_from.count==1,"a NULL zero-capacity list only counts");
    CHECK(qh_npc_missing==TORIRS_ASSET_MISSING,"an npc the cache has none of is MISSING");
    CHECK(qh_loc_state==TORIRS_ASSET_READY && qh_loc.size_z==2 && qh_loc_pending==TORIRS_ASSET_PENDING,
          "a loc def comes through, and a loading one answers PENDING");

    CHECK(qh_chat_ok==TORIRS_RESULT_OK && qh_chat_calls==1 && qh_chat_type==0 &&
          strcmp(qh_chat_text,"Quest Helper: started")==0,
          "a chat line reaches the client store");
    CHECK(qh_chat_bad==TORIRS_RESULT_INVALID && qh_chat_calls==1,"a chat type past the store's rings is refused");
    CHECK(qh_tile_ok==TORIRS_RESULT_OK && qh_arrow_tile_read && qh_arrow_tile_info.kind==TORIRS_HINT_ARROW_TILE &&
          qh_arrow_tile_info.tile_x==3222 && qh_arrow_tile_info.tile_z==3218 && qh_arrow_tile_info.npc_slot==-1,
          "a tile arrow is set and read back");
    CHECK(qh_tile_bad==TORIRS_RESULT_INVALID,"a level no map has is refused");
    CHECK(qh_npc_absent==TORIRS_RESULT_NOT_FOUND,"an arrow at a slot no npc holds is NOT_FOUND");
    CHECK(qh_npc_ok==TORIRS_RESULT_OK && qh_arrow_npc_read && qh_arrow_npc_info.npc_slot==7,"an npc arrow is set");
    CHECK(qh_clear_ok==TORIRS_RESULT_OK && !qh_arrow_cleared_read,"a cleared arrow reads as none");
    CHECK(qh_sound_ok==TORIRS_RESULT_OK && qh_sound_id==2266 && qh_sound_loops==1 && qh_sound_bad==TORIRS_RESULT_INVALID &&
          qh_sound_calls==1,
          "a sound reaches the engine and a negative id does not");
    CHECK(qh_weight==42 && qh_world_types==1 && qh_tick==777,"weight, world types and tick are the engine's");

    CHECK(qh_run_ok==TORIRS_CONTRACT_OK && qh_run_result.int_count==1 && qh_run_result.ints[0]==2 &&
          strcmp(qh_run_result.strings[0],"done")==0 && qh_run_args==1,
          "scripts.run hands the engine the arguments and the caller the stacks");
    CHECK(qh_run_too_many==TORIRS_CONTRACT_INVALID_ARGUMENT,"more arguments than a run carries is refused by the host");
    CHECK(qh_run_pending==TORIRS_CONTRACT_PENDING,"the engine's PENDING comes through");

    qh_run_calls=0;
    PluginHost_DrawCanvas(host,765,503);
    CHECK(qh_draw_set==TORIRS_RESULT_UNSUPPORTED && qh_draw_read==55,"a draw callback cannot write a varc, but reads one");
    CHECK(qh_draw_chat==TORIRS_RESULT_UNSUPPORTED && qh_draw_arrow==TORIRS_RESULT_UNSUPPORTED &&
          qh_draw_sound==TORIRS_RESULT_UNSUPPORTED,
          "a draw callback cannot add a chat line, move the arrow or play a sound");
    CHECK(qh_run_draw==TORIRS_CONTRACT_WRONG_CONTEXT && qh_run_calls==0,"scripts.run is refused in a draw callback");

    {
        struct PluginScriptStack stack={.int_count=2,.string_count=1,.writable_ints=1,.writable_strings=1,
            .get_int=script_test_get_int,.set_int=script_test_set_int,
            .get_string=script_test_get_string,.set_string=script_test_set_string};
        qh_run_script_cb=TORIRS_CONTRACT_OK;
        PluginHost_ScriptCallback(host,"caption",99,&stack);
        CHECK(qh_run_script_cb==TORIRS_CONTRACT_WRONG_CONTEXT && qh_run_calls==0,
              "scripts.run is refused inside on_script_callback");
    }
    PluginHost_Free(host);

    /* An engine without the seams: readers say "not here", writers UNSUPPORTED. */
    {
        struct ToriRS_PluginEngine bare=fake_engine();
        struct ToriRS_PluginHost* h=PluginHost_New(&bare);
        struct ToriRS_PluginDef bare_def={.struct_size=sizeof(bare_def),.id="qh-bare",.title="QH bare",.version="3",
            .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_server_tick=qh_bare_tick_cb}};
        CHECK(PluginHost_Register(h,&bare_def)>=0,"bare-engine fixture registers");
        PluginHost_Start(h);
        PluginHost_ServerTick(h,1);
        CHECK(qh_bare_varc_int==-1 && !qh_bare_varc_str && qh_bare_set==TORIRS_RESULT_UNSUPPORTED &&
              qh_bare_enum==TORIRS_ASSET_MISSING && qh_bare_chat==TORIRS_RESULT_UNSUPPORTED &&
              !qh_bare_arrow && qh_bare_tick==0 && qh_bare_run==TORIRS_CONTRACT_UNAVAILABLE,
              "an engine without the seams answers 'not here', never a made-up value");
        PluginHost_Free(h);
    }
}

/* ------------------------------------------------------------------------ *
 * The Quest Helper port's client-APIs round, through the host: the menu
 * edit plan (set_order / set_text / shown), on_command, cache.set_varbit /
 * seq_def, client.open_url / worldmap_jump. The menu is a fake of strings so
 * the order the host hands the engine is visible.
 *
 * MUTATION 1: apply the plan before the entity holders' drops (or never
 *   update menu_payload_at on a drop).  Red: "a holder's drop does not move
 *   the plan's rows".
 * MUTATION 2: let set_order name another plugin's tag.  Red: "a tag is this
 *   plugin's own".
 * MUTATION 3: drop plugin_ctx_may_mutate from api_set_varbit.  Red: "a draw
 *   callback cannot write a varbit".
 * MUTATION 4: dispatch on_command past a consumer.  Red: "a consumed command
 *   reaches no later plugin".
 * ------------------------------------------------------------------------ */
static char qc_rows[32][128];
static int qc_actions[32];
static int qc_count, qc_permutes, qc_texts;
static int qc_menu_add(void* u, void* cursor, char const* text, int action)
{
    (void)u; (void)cursor;
    snprintf(qc_rows[qc_count], sizeof(qc_rows[0]), "%s", text);
    qc_actions[qc_count++] = action;
    return 1;
}
static int qc_menu_drop(void* u, void* cursor, int index)
{
    (void)u; (void)cursor;
    if( index < 0 || index >= qc_count ) return 0;
    for( int i = index + 1; i < qc_count; i++ ) { memcpy(qc_rows[i - 1], qc_rows[i], sizeof(qc_rows[0])); qc_actions[i - 1] = qc_actions[i]; }
    qc_count--;
    return 1;
}
static int qc_menu_count(void* u, void* cursor) { (void)u; (void)cursor; return qc_count; }
static void qc_menu_set_text(void* u, void* cursor, int index, char const* text)
{ (void)u; (void)cursor; qc_texts++; snprintf(qc_rows[index], sizeof(qc_rows[0]), "%s", text); }
static void qc_menu_permute(void* u, void* cursor, int const* order, int count)
{
    char old[32][128]; int old_actions[32];
    (void)u; (void)cursor;
    qc_permutes++;
    memcpy(old, qc_rows, sizeof(old)); memcpy(old_actions, qc_actions, sizeof(old_actions));
    for( int i = 0; i < count; i++ ) { memcpy(qc_rows[i], old[order[i]], sizeof(qc_rows[0])); qc_actions[i] = old_actions[order[i]]; }
    qc_count = count;
}
static void qc_menu_row(void* u, void* cursor, int index, struct ToriRS_MenuRow* out)
{
    (void)u; (void)cursor;
    memset(out, 0, sizeof(*out));
    out->text = qc_rows[index];
    out->action = qc_actions[index];
    out->npc_slot = out->player_pid = out->target_id = out->component_id = out->slot = -1;
}
static int qc_menu_shown(void* u, struct ToriRS_Rect* out)
{ (void)u; *out = (struct ToriRS_Rect){ 100, 50, 120, 70 }; return 1; }
static int qc_varbits[4];
static int qc_set_varbit(void* u, int id, int value) { (void)u; if( id < 0 || id >= 4 ) return 0; qc_varbits[id] = value; return 1; }
static int qc_seq_def(void* u, int id, struct ToriRS_SeqDef* out)
{
    static int const lengths[3] = { 4, 5, 6 };
    (void)u;
    if( id == 8 ) return TORIRS_ASSET_PENDING;
    if( id != 7 ) return TORIRS_ASSET_MISSING;
    out->id = 7; out->frame_count = 3; out->duration = 15; out->frame_step = -1; out->skeletal = false;
    qh_fake_list(&out->frame_lengths, lengths, 3);
    return TORIRS_ASSET_READY;
}
static char qc_url[512];
static int qc_url_calls;
static int qc_open_url(void* u, char const* url)
{ (void)u; qc_url_calls++; snprintf(qc_url, sizeof(qc_url), "%s", url); return TORIRS_RESULT_OK; }
static int qc_jump_x, qc_jump_z;
static int qc_worldmap_jump(void* u, int x, int z, int level)
{ (void)u; (void)level; if( x > 9000 ) return TORIRS_RESULT_NOT_FOUND; qc_jump_x = x; qc_jump_z = z; return TORIRS_RESULT_OK; }

static int qc_mode;
static enum ToriRS_Result qc_r_order, qc_r_bad_row, qc_r_dup, qc_r_bad_tag, qc_r_text, qc_r_long;
static enum ToriRS_CallbackResult qc_build(struct ToriRS_Api* api, void* state, struct ToriRS_MenuBuildEvent* ev)
{
    (void)state;
    if( qc_mode == 1 )
    {
        /* RuneLite's "Start Quest Helper" at length-1, Walk here deprioritized,
         * the npc row's text replaced, Cancel removed. */
        struct ToriRS_MenuRowRef order[4] = {
            { 1, 0, false },          /* Examine */
            { 2, 0, true },           /* Walk here, deprioritized */
            { -1, 41, false },        /* our row */
            { 3, 0, false },          /* Talk-to (top) */
        };
        static char long_text[200];
        CHECK(api->menu.add(api, ev, "Start Quest Helper", 41), "the plugin's row is added");
        qc_r_bad_row = api->menu.set_order(api, ev, (struct ToriRS_MenuRowRef[]){ { 9, 0, false } }, 1);
        qc_r_dup = api->menu.set_order(api, ev, (struct ToriRS_MenuRowRef[]){ { 1, 0, false }, { 1, 0, false } }, 2);
        qc_r_bad_tag = api->menu.set_order(api, ev, (struct ToriRS_MenuRowRef[]){ { -1, 99, false } }, 1);
        qc_r_order = api->menu.set_order(api, ev, order, 4);
        qc_r_text = api->menu.set_text(api, ev, 3, "Talk-to @yel@Fake Veos");
        memset(long_text, 'x', sizeof(long_text) - 1);
        qc_r_long = api->menu.set_text(api, ev, 3, long_text);
    }
    return TORIRS_CALLBACK_CONTINUE;
}
static enum ToriRS_CallbackResult qc_build_other(struct ToriRS_Api* api, void* state, struct ToriRS_MenuBuildEvent* ev)
{
    (void)state;
    if( qc_mode == 1 )
    {
        CHECK(api->menu.add(api, ev, "Other plugin", 7), "another plugin adds a row");
        qc_r_bad_tag = api->menu.set_order(api, ev, (struct ToriRS_MenuRowRef[]){ { -1, 41, false } }, 1) ==
                               TORIRS_RESULT_INVALID ? qc_r_bad_tag : TORIRS_RESULT_OK;
    }
    return TORIRS_CALLBACK_CONTINUE;
}
static int qc_commands_a, qc_commands_b;
static char qc_cmd_name[64], qc_cmd_args[64];
static enum ToriRS_CallbackResult qc_command_a(struct ToriRS_Api* api, void* state, struct ToriRS_CommandEvent const* ev)
{
    (void)api; (void)state;
    qc_commands_a++;
    snprintf(qc_cmd_name, sizeof(qc_cmd_name), "%s", ev->name);
    snprintf(qc_cmd_args, sizeof(qc_cmd_args), "%s", ev->args);
    return strcmp(ev->name, "questhelperdebug") == 0 ? TORIRS_CALLBACK_CONSUME : TORIRS_CALLBACK_CONTINUE;
}
static enum ToriRS_CallbackResult qc_command_b(struct ToriRS_Api* api, void* state, struct ToriRS_CommandEvent const* ev)
{ (void)api; (void)state; (void)ev; qc_commands_b++; return TORIRS_CALLBACK_CONTINUE; }
static enum ToriRS_Result qc_v_ok, qc_v_bad, qc_u_ok, qc_u_bad, qc_u_ftp, qc_j_ok, qc_j_far, qc_j_level;
static enum ToriRS_AssetState qc_s_ready, qc_s_pending, qc_s_missing;
static struct ToriRS_SeqDef qc_seq;
static int qc_seq_lengths[2];
static bool qc_shown;
static struct ToriRS_Rect qc_shown_box;
/* container_slot/container_size: a shop (516) the fake holds, 3 slots, slot 1
 * empty; any other id is not held. */
static int qc_container_slot(void* u, int container, int slot, int* id, int* count)
{
    (void)u;
    if( container != 516 || slot < 0 || slot >= 3 )
        return 0;
    *id = slot == 1 ? -1 : 1925 + slot;
    *count = slot == 1 ? 0 : 10 + slot;
    return 1;
}
static int qc_container_size(void* u, int container) { (void)u; return container == 516 ? 3 : 0; }
static int qc_c_size, qc_c_size_none, qc_c_id[3], qc_c_count[3];
static bool qc_c_ok[3], qc_c_past, qc_c_none;
/* world_entity_next / world_entity_to_main: two boats (views 3 and 7). */
static int qc_wev_next(void* u, int iter, struct ToriRS_WorldEntitySnapshot* out)
{
    (void)u;
    int const next = iter < 3 ? 3 : iter < 7 ? 7 : -1;
    if( next < 0 ) return -1;
    memset(out, 0, sizeof(*out));
    out->id = next; out->config_id = next == 3 ? 9 : 12; out->fine_x = 3200 * 128; out->fine_z = 3200 * 128;
    out->base_x = 6400 + next * 16; out->base_z = 6400; out->size_x = 16; out->size_z = 8; out->deck_level = 1;
    return next;
}
static int qc_wev_to_main(void* u, int id, int x, int z, int* ox, int* oz)
{ (void)u; if( id != 3 ) return 0; *ox = 3200 * 128 + x; *oz = 3200 * 128 + z; return 1; }
static int qc_w_ids[4], qc_w_configs[4], qc_w_count, qc_w_mx, qc_w_mz;
static bool qc_w_main_ok, qc_w_main_gone;
/* world_entity_loc_next: boat 3's own world holds two hull locs. */
static int qc_wev_loc_next(void* u, int id, int iter, struct ToriRS_ScenerySnapshot* out)
{
    (void)u;
    if( id != 3 || iter >= 1 ) return -1;
    memset(out, 0, sizeof(*out));
    out->loc_id = iter < 0 ? 59001 : 59002; out->tile_x = 6448 + (iter < 0 ? 0 : 1); out->tile_z = 6400;
    out->level = 1; out->element_id = iter < 0 ? 700 : 701; out->size_x = 1; out->size_z = 1;
    return iter < 0 ? 0 : 1;
}
static int qc_wl_ids[4], qc_wl_elements[4], qc_wl_count, qc_wl_gone;
static void qc_tick(struct ToriRS_Api* api, void* state, struct ToriRS_TickEvent const* ev)
{
    (void)state; (void)ev;
    qc_v_ok = api->cache.set_varbit(api, 2, 9);
    qc_v_bad = api->cache.set_varbit(api, 5, 1);
    memset(&qc_seq, 0, sizeof(qc_seq));
    qc_seq.frame_lengths = (struct ToriRS_DefList){ qc_seq_lengths, 2, 0 };
    qc_s_ready = api->cache.seq_def(api, 7, &qc_seq);
    qc_s_pending = api->cache.seq_def(api, 8, &qc_seq);
    qc_s_missing = api->cache.seq_def(api, 9, &qc_seq);
    qc_u_bad = api->client->open_url(api, "https://example.org/a b");
    qc_u_ftp = api->client->open_url(api, "ftp://example.org/");
    qc_u_ok = api->client->open_url(api, "https://github.com/Zoinkwiz/quest-helper");
    qc_j_ok = api->client->worldmap_jump(api, 3222, 3218, 0);
    qc_j_far = api->client->worldmap_jump(api, 9999, 3218, 0);
    qc_j_level = api->client->worldmap_jump(api, 3222, 3218, 4);
    qc_shown = api->menu.shown(api, &qc_shown_box);
    qc_c_size = api->game->container_size(api, 516);
    qc_c_size_none = api->game->container_size(api, 659);
    for( int i = 0; i < 3; i++ )
        qc_c_ok[i] = api->game->container_slot(api, 516, i, &qc_c_id[i], &qc_c_count[i]);
    {
        int id = 0, count = 0;
        qc_c_past = api->game->container_slot(api, 516, 3, &id, &count);
        qc_c_none = api->game->container_slot(api, 659, 0, &id, &count);
    }
    {
        struct ToriRS_WorldEntitySnapshot w;
        int it = -1;
        qc_w_count = 0;
        while( (it = api->game->world_entity_next(api, it, &w)) >= 0 && qc_w_count < 4 )
        {
            qc_w_ids[qc_w_count] = w.id; qc_w_configs[qc_w_count] = w.config_id; qc_w_count++;
        }
        qc_w_main_ok = api->game->world_entity_to_main(api, 3, 64, 192, &qc_w_mx, &qc_w_mz);
        {
            struct ToriRS_ScenerySnapshot loc;
            int at = -1;
            qc_wl_count = 0;
            while( (at = api->game->world_entity_loc_next(api, 3, at, &loc)) >= 0 && qc_wl_count < 4 )
            {
                qc_wl_ids[qc_wl_count] = loc.loc_id; qc_wl_elements[qc_wl_count] = loc.element_id; qc_wl_count++;
            }
            qc_wl_gone = api->game->world_entity_loc_next(api, 5, -1, &loc);
        }
        {
            int x = 0, z = 0;
            qc_w_main_gone = api->game->world_entity_to_main(api, 5, 64, 64, &x, &z);
        }
    }
}
static enum ToriRS_Result qc_d_varbit, qc_d_url, qc_d_jump;
static void qc_draw(struct ToriRS_Api* api, void* state, struct ToriRS_Graphics* graphics)
{
    (void)state; (void)graphics;
    qc_d_varbit = api->cache.set_varbit(api, 2, 1);
    qc_d_url = api->client->open_url(api, "https://example.org/");
    qc_d_jump = api->client->worldmap_jump(api, 3222, 3218, 0);
}

static void qc_menu_reset(void)
{
    static char const* const rows[4] = { "Cancel", "Examine @yel@Veos", "Walk here", "Talk-to @yel@Veos" };
    qc_count = 0;
    for( int i = 0; i < 4; i++ ) { snprintf(qc_rows[i], sizeof(qc_rows[0]), "%s", rows[i]); qc_actions[i] = 1000 + i; }
    qc_count = 4;
}

/* ------------------------------------------------------------------------ *
 * menu.set_order across plugins (the panel/menu round): the payload is the
 * WHOLE menu, and two plugins that order it compose by event_priority --
 * each handed the menu as the higher-priority ones left it, their rows
 * included -- instead of the last caller winning.
 *
 * MUTATION 1: dispatch on_menu_build with plugin_dispatch (plans applied at
 *   the end, last wins).  Red: "the later plugin is handed the earlier one's
 *   row" and "the two orders compose".
 * MUTATION 2: cap the bridge payload at 16 again.  Red: "a 20-row menu is
 *   handed whole" (host side: ROWS_MAX).
 * ------------------------------------------------------------------------ */
static int qm_mode;
static int qm_low_rows, qm_high_rows;
static char qm_low_row3[128];
static enum ToriRS_Result qm_r_high, qm_r_low;
static enum ToriRS_CallbackResult qm_high(struct ToriRS_Api* api, void* state, struct ToriRS_MenuBuildEvent* ev)
{
    (void)state;
    qm_high_rows = ev->row_count;
    if( qm_mode == 1 )
    {
        /* Its row just below the top (QuestMenuHandler's "Start Quest Helper"). */
        struct ToriRS_MenuRowRef order[5] = { { 0, 0, false }, { 1, 0, false }, { 2, 0, false }, { -1, 5, false }, { 3, 0, false } };
        CHECK(api->menu.add(api, ev, "High row", 5), "the high-priority plugin adds its row");
        qm_r_high = api->menu.set_order(api, ev, order, 5);
    }
    return TORIRS_CALLBACK_CONTINUE;
}
static enum ToriRS_CallbackResult qm_low(struct ToriRS_Api* api, void* state, struct ToriRS_MenuBuildEvent* ev)
{
    (void)state;
    qm_low_rows = ev->row_count;
    qm_low_row3[0] = '\0';
    if( ev->row_count > 3 && ev->rows[3].text )
        snprintf(qm_low_row3, sizeof(qm_low_row3), "%s", ev->rows[3].text);
    if( qm_mode == 1 )
    {
        /* Walk here (payload row 2) to the top; every other row where it is. */
        struct ToriRS_MenuRowRef order[5] = { { 0, 0, false }, { 1, 0, false }, { 3, 0, false }, { 4, 0, false }, { 2, 0, false } };
        qm_r_low = api->menu.set_order(api, ev, order, 5);
    }
    else if( qm_mode == 2 )
    {
        /* The whole 20-row menu, reversed: rows past the old 16 are named. */
        struct ToriRS_MenuRowRef order[20];
        for( int i = 0; i < 20; i++ )
            order[i] = (struct ToriRS_MenuRowRef){ 19 - i, 0, false };
        qm_r_low = api->menu.set_order(api, ev, order, 20);
    }
    return TORIRS_CALLBACK_CONTINUE;
}

static void test_menu_order_composes(void)
{
    struct ToriRS_PluginEngine engine = fake_engine();
    struct ToriRS_MenuBuildEvent ev;
    int cursor = 0;
    bool ordered;
    engine.menu_add = qc_menu_add; engine.menu_drop = qc_menu_drop; engine.menu_count = qc_menu_count;
    engine.menu_set_text = qc_menu_set_text; engine.menu_permute = qc_menu_permute; engine.menu_row = qc_menu_row;
    struct ToriRS_PluginHost* host = PluginHost_New(&engine);
    /* Registered low first: the dispatch order is the priority's, not the
     * registration's. */
    struct ToriRS_PluginDef low = { .struct_size = sizeof(low), .id = "qm-low", .title = "QM low", .version = "1",
        .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks), .on_menu_build = qm_low } };
    struct ToriRS_PluginDef high = { .struct_size = sizeof(high), .id = "qm-high", .title = "QM high", .version = "1",
        .event_priority = 10,
        .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks), .on_menu_build = qm_high } };
    CHECK(PluginHost_Register(host, &low) >= 0 && PluginHost_Register(host, &high) >= 0, "compose fixtures register");
    PluginHost_Start(host);

    qm_mode = 1;
    qc_menu_reset();
    qc_permutes = 0;
    memset(&ev, 0, sizeof(ev));
    ev.row_count = 4;
    ordered = PluginHost_MenuBuild(host, &cursor, &ev, false);
    CHECK(qm_r_high == TORIRS_RESULT_OK && qm_r_low == TORIRS_RESULT_OK, "both orders are accepted");
    CHECK(qm_high_rows == 4, "the higher-priority plugin runs first, on the native rows");
    CHECK(qm_low_rows == 5 && strcmp(qm_low_row3, "High row") == 0,
          "the later plugin is handed the earlier one's row, where it put it");
    CHECK(ordered && qc_count == 5 && strcmp(qc_rows[0], "Cancel") == 0 && strcmp(qc_rows[1], "Examine @yel@Veos") == 0 &&
              strcmp(qc_rows[2], "High row") == 0 && strcmp(qc_rows[3], "Talk-to @yel@Veos") == 0 &&
              strcmp(qc_rows[4], "Walk here") == 0,
          "the two orders compose: the high row below the old top, then Walk here moved over both");
    CHECK(qc_permutes == 2, "each plugin's order applied once");

    /* A menu larger than the old 16-row payload is handed whole. */
    qm_mode = 2;
    qc_count = 0;
    for( int i = 0; i < 20; i++ )
    {
        snprintf(qc_rows[i], sizeof(qc_rows[0]), "Row %d", i);
        qc_actions[i] = 2000 + i;
    }
    qc_count = 20;
    memset(&ev, 0, sizeof(ev));
    ev.row_count = 20;
    ordered = PluginHost_MenuBuild(host, &cursor, &ev, false);
    CHECK(qm_low_rows == 20 && qm_r_low == TORIRS_RESULT_OK, "a 20-row menu is handed whole and ordered whole");
    CHECK(ordered && qc_count == 20 && strcmp(qc_rows[0], "Row 19") == 0 && strcmp(qc_rows[19], "Row 0") == 0 &&
              strcmp(qc_rows[3], "Row 16") == 0,
          "rows past the old 16 move too");
    PluginHost_Free(host);
}

/* ------------------------------------------------------------------------ */
/* Quest Helper port: sub-tile instance placement, the instance's element and */
/* its hit test (RuneLiteObject.setLocation / getClickbox).                    */
/* ------------------------------------------------------------------------ */

static int qs_fine_x, qs_fine_z, qs_fine_level, qs_fine_calls, qs_hit_object = -1;
static void qs_fine(void* u, int object, int fine_x, int fine_z, int level, int height, int yaw)
{ (void)u;(void)object;(void)height;(void)yaw; qs_fine_x = fine_x; qs_fine_z = fine_z; qs_fine_level = level; qs_fine_calls++; }
static int qs_element(void* u, int object) { (void)u; return object == qs_hit_object ? 900 + object : -1; }
static int qs_hit(void* u, int object) { (void)u; return object == qs_hit_object; }
static enum ToriRS_Result qs_place_ok, qs_place_negative, qs_place_level, qs_place_yaw, qs_place_stale;
static int qs_element_drawn, qs_element_stale;
static bool qs_hit_on, qs_hit_stale;
static void qs_tick(struct ToriRS_Api* api, void* state, struct ToriRS_TickEvent const* ev)
{
    struct ToriRS_SceneInstanceRef inst = { 0 }, gone = { 0 };
    int was_in_use[FAKE_OBJECTS_MAX];
    (void)state; (void)ev;
    for( int i = 0; i < FAKE_OBJECTS_MAX; i++ )
        was_in_use[i] = g_engine.objects[i].in_use;
    (void)api->scene.instance_create(api, &inst);
    for( int i = 0; i < FAKE_OBJECTS_MAX; i++ )
        if( g_engine.objects[i].in_use && !was_in_use[i] ) qs_hit_object = i;
    (void)api->scene.instance_create(api, &gone);
    api->scene.instance_destroy(api, gone);
    qs_place_ok = api->scene.instance_place(api, inst, 3222 * 128 + 17, 3218 * 128 + 100, 0, 0, 512);
    qs_place_negative = api->scene.instance_place(api, inst, -1, 3218 * 128, 0, 0, 0);
    qs_place_level = api->scene.instance_place(api, inst, 3222 * 128, 3218 * 128, 4, 0, 0);
    qs_place_yaw = api->scene.instance_place(api, inst, 3222 * 128, 3218 * 128, 0, 0, 2048);
    qs_place_stale = api->scene.instance_place(api, gone, 3222 * 128, 3218 * 128, 0, 0, 0);
    qs_element_drawn = api->scene.instance_element(api, inst);
    qs_element_stale = api->scene.instance_element(api, gone);
    qs_hit_on = api->scene.instance_hit(api, inst);
    qs_hit_stale = api->scene.instance_hit(api, gone);
    api->scene.instance_destroy(api, inst);
}
static enum ToriRS_Result qs_bare_place;
static int qs_bare_element;
static bool qs_bare_hit;
static void qs_bare_tick(struct ToriRS_Api* api, void* state, struct ToriRS_TickEvent const* ev)
{
    struct ToriRS_SceneInstanceRef inst = { 0 };
    (void)state; (void)ev;
    (void)api->scene.instance_create(api, &inst);
    qs_bare_place = api->scene.instance_place(api, inst, 3222 * 128, 3218 * 128, 0, 0, 0);
    qs_bare_element = api->scene.instance_element(api, inst);
    qs_bare_hit = api->scene.instance_hit(api, inst);
    api->scene.instance_destroy(api, inst);
}
static void test_scene_instance_place(void)
{
    {
        struct ToriRS_PluginEngine engine = fake_engine();
        engine.object_set_fine_position = qs_fine; engine.object_element = qs_element; engine.object_hit = qs_hit;
        struct ToriRS_PluginHost* host = PluginHost_New(&engine);
        struct ToriRS_PluginDef def = { .struct_size = sizeof(def), .id = "qs-place", .title = "QS", .version = "1",
            .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks), .on_server_tick = qs_tick } };
        CHECK(PluginHost_Register(host, &def) >= 0, "instance-place fixture registers");
        PluginHost_Start(host);
        PluginHost_ServerTick(host, 1);
        CHECK(qs_place_ok == TORIRS_RESULT_OK && qs_fine_calls == 1 && qs_fine_x == 3222 * 128 + 17 &&
              qs_fine_z == 3218 * 128 + 100 && qs_fine_level == 0,
              "instance_place hands an absolute sub-tile point to the engine");
        CHECK(qs_place_negative == TORIRS_RESULT_INVALID && qs_place_level == TORIRS_RESULT_INVALID &&
              qs_place_yaw == TORIRS_RESULT_INVALID && qs_place_stale == TORIRS_RESULT_INVALID && qs_fine_calls == 1,
              "a negative point, a level past 3, a yaw past 2047 and a destroyed instance never reach it");
        CHECK(qs_element_drawn == 900 + qs_hit_object && qs_element_stale == -1,
              "instance_element answers the drawn element, -1 for a destroyed instance");
        CHECK(qs_hit_on && !qs_hit_stale, "instance_hit is the engine's pick; a destroyed instance is never hit");
        PluginHost_Free(host);
    }
    {
        struct ToriRS_PluginEngine engine = fake_engine();
        struct ToriRS_PluginHost* host = PluginHost_New(&engine);
        struct ToriRS_PluginDef def = { .struct_size = sizeof(def), .id = "qs-bare", .title = "QS", .version = "1",
            .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks), .on_server_tick = qs_bare_tick } };
        CHECK(PluginHost_Register(host, &def) >= 0, "bare-engine fixture registers");
        PluginHost_Start(host);
        PluginHost_ServerTick(host, 1);
        CHECK(qs_bare_place == TORIRS_RESULT_UNSUPPORTED && qs_bare_element == -1 && !qs_bare_hit,
              "an engine without the hooks: place UNSUPPORTED, no element, never hit");
        PluginHost_Free(host);
    }
}

static void test_quest_helper_client_apis(void)
{
    struct ToriRS_PluginEngine engine = fake_engine();
    struct ToriRS_MenuBuildEvent ev;
    int cursor = 0;
    bool ordered;
    engine.menu_add = qc_menu_add; engine.menu_drop = qc_menu_drop; engine.menu_count = qc_menu_count;
    engine.menu_set_text = qc_menu_set_text; engine.menu_permute = qc_menu_permute; engine.menu_shown = qc_menu_shown;
    engine.menu_row = qc_menu_row;
    engine.set_varbit = qc_set_varbit; engine.seq_def = qc_seq_def; engine.open_url = qc_open_url;
    engine.worldmap_jump = qc_worldmap_jump;
    engine.container_slot = qc_container_slot; engine.container_size = qc_container_size;
    engine.world_entity_next = qc_wev_next; engine.world_entity_to_main = qc_wev_to_main;
    engine.world_entity_loc_next = qc_wev_loc_next;
    struct ToriRS_PluginHost* host = PluginHost_New(&engine);
    struct ToriRS_PluginDef def = { .struct_size = sizeof(def), .id = "qc-apis", .title = "QC", .version = "3",
        .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks), .on_menu_build = qc_build,
            .on_server_tick = qc_tick, .on_draw_canvas = qc_draw, .on_command = qc_command_a } };
    struct ToriRS_PluginDef other = { .struct_size = sizeof(other), .id = "qc-other", .title = "QC2", .version = "3",
        .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks), .on_menu_build = qc_build_other,
            .on_command = qc_command_b } };
    CHECK(PluginHost_Register(host, &def) >= 0 && PluginHost_Register(host, &other) >= 0, "client-API fixtures register");
    PluginHost_Start(host);

    /* No plan: the host leaves the order to the caller's sort. */
    qc_mode = 0;
    qc_menu_reset();
    memset(&ev, 0, sizeof(ev));
    ev.row_count = 4;
    ordered = PluginHost_MenuBuild(host, &cursor, &ev, false);
    CHECK(!ordered && qc_permutes == 0 && qc_texts == 0 && qc_count == 4, "a build nobody edits is left alone");

    qc_mode = 1;
    qc_menu_reset();
    memset(&ev, 0, sizeof(ev));
    ev.row_count = 4;
    ordered = PluginHost_MenuBuild(host, &cursor, &ev, false);
    CHECK(qc_r_bad_row == TORIRS_RESULT_INVALID, "a row past the payload is refused");
    CHECK(qc_r_dup == TORIRS_RESULT_INVALID, "a row listed twice is refused");
    CHECK(qc_r_bad_tag == TORIRS_RESULT_INVALID, "a tag is this plugin's own");
    CHECK(qc_r_order == TORIRS_RESULT_OK && qc_r_text == TORIRS_RESULT_OK, "the plan is accepted");
    CHECK(qc_r_long == TORIRS_RESULT_BUDGET, "a row text past the menu's row size is refused");
    CHECK(ordered && qc_permutes == 2, "an ordered build tells the caller not to re-sort (the plan, then the sink)");
    CHECK(qc_count == 5, "Cancel is gone; the other plugin's row stays");
    /* The other plugin ran after the plan was applied, so its row landed on
     * the ordered menu's top (createMenuEntry(-1)); the deprioritized walk
     * sank below every row once the build was done. */
    CHECK(qc_count == 5 && strcmp(qc_rows[0], "Walk here") == 0 && strcmp(qc_rows[1], "Examine @yel@Veos") == 0 &&
              strcmp(qc_rows[2], "Start Quest Helper") == 0 && strcmp(qc_rows[3], "Talk-to @yel@Fake Veos") == 0 &&
              strcmp(qc_rows[4], "Other plugin") == 0,
          "bottom first: the deprioritized walk, the plan in order with the new text, then the later plugin's row");

    qc_shown = false;
    PluginHost_ServerTick(host, 1);
    CHECK(qc_v_ok == TORIRS_RESULT_OK && qc_varbits[2] == 9, "set_varbit reaches the client's varbit write");
    CHECK(qc_v_bad == TORIRS_RESULT_INVALID, "a varbit the client does not know is INVALID");
    CHECK(qc_s_ready == TORIRS_ASSET_READY && qc_seq.frame_count == 3 && qc_seq.duration == 15 &&
              qc_seq.frame_lengths.count == 3 && qc_seq_lengths[0] == 4 && qc_seq_lengths[1] == 5,
          "a seq states its frames, its true length count past the caller's room, and its duration");
    CHECK(qc_s_pending == TORIRS_ASSET_PENDING && qc_s_missing == TORIRS_ASSET_MISSING, "seq_def's PENDING and MISSING come through");
    CHECK(qc_u_bad == TORIRS_RESULT_INVALID && qc_u_ftp == TORIRS_RESULT_INVALID, "a url with a space or another scheme is refused");
    CHECK(qc_u_ok == TORIRS_RESULT_OK && qc_url_calls == 1 && strcmp(qc_url, "https://github.com/Zoinkwiz/quest-helper") == 0,
          "an https url reaches the platform, and only it");
    CHECK(qc_j_ok == TORIRS_RESULT_OK && qc_jump_x == 3222 && qc_jump_z == 3218, "worldmap_jump reaches the map");
    CHECK(qc_j_far == TORIRS_RESULT_NOT_FOUND && qc_j_level == TORIRS_RESULT_INVALID, "an uncovered tile and a bad level");
    CHECK(qc_shown && qc_shown_box.x == 100 && qc_shown_box.height == 70, "menu.shown answers the open menu's box");
    CHECK(qc_c_size == 3 && qc_c_size_none == 0, "container_size: a held shop by its id, 0 for one never sent");
    CHECK(qc_c_ok[0] && qc_c_id[0] == 1925 && qc_c_count[0] == 10 && qc_c_ok[2] && qc_c_id[2] == 1927 && qc_c_count[2] == 12,
          "container_slot reads any container by the id on_inventory_changed reports");
    CHECK(qc_c_ok[1] && qc_c_id[1] == -1, "an empty slot of a held container is true with -1");
    CHECK(!qc_c_past && !qc_c_none, "a slot past the end, and a container not held, are false");
    CHECK(qc_w_count == 2 && qc_w_ids[0] == 3 && qc_w_ids[1] == 7 && qc_w_configs[0] == 9 && qc_w_configs[1] == 12,
          "world_entity_next walks every live boat with its config id");
    CHECK(qc_w_main_ok && qc_w_mx == 3200 * 128 + 64 && qc_w_mz == 3200 * 128 + 192,
          "world_entity_to_main carries a deck point through the engine's transform");
    CHECK(!qc_w_main_gone, "world_entity_to_main of an entity that is not live is false");
    CHECK(qc_wl_count == 2 && qc_wl_ids[0] == 59001 && qc_wl_ids[1] == 59002 && qc_wl_elements[1] == 701,
          "world_entity_loc_next walks the boat's own locs with their elements");
    CHECK(qc_wl_gone < 0, "world_entity_loc_next of an entity that is not live ends at once");

    PluginHost_DrawCanvas(host, 765, 503);
    CHECK(qc_d_varbit == TORIRS_RESULT_UNSUPPORTED && qc_varbits[2] == 9, "a draw callback cannot write a varbit");
    CHECK(qc_d_url == TORIRS_RESULT_UNSUPPORTED && qc_url_calls == 1 && qc_d_jump == TORIRS_RESULT_UNSUPPORTED,
          "a draw callback cannot open a url or move the map");

    CHECK(PluginHost_Command(host, "qh-inv") == 0 && qc_commands_a == 1 && qc_commands_b == 1 &&
              strcmp(qc_cmd_name, "qh-inv") == 0 && qc_cmd_args[0] == '\0',
          "an unconsumed command reaches every subscriber");
    CHECK(PluginHost_Command(host, "questhelperdebug a b") == 1 && qc_commands_a == 2 && qc_commands_b == 1 &&
              strcmp(qc_cmd_name, "questhelperdebug") == 0 && strcmp(qc_cmd_args, "a b") == 0,
          "a consumed command reaches no later plugin and is reported consumed");
    PluginHost_Free(host);

    /* An engine without the hooks: edits UNSUPPORTED, the caller sorts. */
    {
        struct ToriRS_PluginEngine bare = fake_engine();
        struct ToriRS_PluginHost* h = PluginHost_New(&bare);
        CHECK(PluginHost_Command(h, "x") == 0, "with no subscriber a command is never consumed");
        PluginHost_Free(h);
    }
}

int
main(void)
{
    struct ToriRS_PluginEngine engine;

    /* ---- V2 registration and lifecycle ---------------------------------- */
    {
        struct ToriRS_PluginHost* hv2;
        struct ToriRS_PanelWidget const* widget;
        uint32_t generation;
        int draw_before;
        int a2;
        int b2;
        int c2;
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
        c2 = PluginHost_Register(hv2, &V2_PROBE_C);
        prefix2 = PluginHost_Register(hv2, &V2_PREFIX_ONLY);
        CHECK(a2 == 0 && b2 == 1 && c2 == 2, "v2 registration shares host indexing");
        CHECK(prefix2 == 3, "a definition ending in a shorter callback-table prefix registers");
        CHECK(
            strcmp(PluginHost_Name(hv2, a2), "v2-probe-a") == 0 &&
                strcmp(PluginHost_Title(hv2, a2), "V2 Probe A") == 0 &&
                PluginHost_ConfigCount(hv2, a2) == 2,
            "v2 identity, title, and config schema normalize into host metadata");
        CHECK(PluginHost_IsRuntimeHost(hv2, b2), "v2 runtime-host flag normalizes");

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
            ToriMem_Free(ToriMem_Plugins(), encoded);
        }
        CHECK(
            g_v2_typed_calls == 3 && g_engine.objects_live == 3,
            "typed module calls execute for every live instance");
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

        CHECK(PluginHost_PanelHasPage(hv2, a2), "v2 on_start panel registration is retained");
        CHECK(PluginHost_PanelSelect(hv2, a2), "v2 panel can be selected");
        generation = PluginHost_PanelSelectionGeneration(hv2);
        CHECK(
            g_v2_panel_builds == 1 && PluginHost_PanelWidgetCount(hv2, generation) == 7,
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
        /* A changed option COUNT applies in place: the catalogue a settings
         * page offers grows and shrinks (a provider appears, a lane lacks
         * one) and used to force a full rebuild through INVALID. */
        {
            struct ToriRS_PanelWidget const* second = PluginHost_PanelWidgetAt(hv2, generation, 5);
            CHECK(second && second->structured_select && second->select_option_count == 2 &&
                      strcmp(second->select_options[1].value, "two") == 0,
                  "the second select holds its own two-option slice behind the first");
            CHECK(g_v2_api[1]->panel.set_options(g_v2_api[1], "frame", "c/frame", V2_PANEL_OPTIONS_FOUR, 4) ==
                      TORIRS_RESULT_OK,
                  "a select grows from three options to four in place");
            widget = PluginHost_PanelWidgetAt(hv2, generation, 2);
            CHECK(widget && widget->select_option_count == 4 && widget->selected == 3 &&
                      strcmp(widget->select_options[3].value, "c/frame") == 0 &&
                      !widget->select_options[2].enabled,
                  "the grown slice carries all four options and the selection");
            second = PluginHost_PanelWidgetAt(hv2, generation, 5);
            CHECK(second && second->select_option_count == 2 &&
                      strcmp(second->select_options[0].value, "one") == 0 &&
                      strcmp(second->select_options[1].value, "two") == 0 &&
                      second->select_options == widget->select_options + 4,
                  "the slice behind it slid by one and its owner still points at it");
            CHECK(g_v2_api[1]->panel.set_options(g_v2_api[1], "frame", "two", V2_PANEL_OPTIONS_SECOND, 2) ==
                      TORIRS_RESULT_OK,
                  "a select shrinks from four options to two in place");
            widget = PluginHost_PanelWidgetAt(hv2, generation, 2);
            second = PluginHost_PanelWidgetAt(hv2, generation, 5);
            CHECK(widget && widget->select_option_count == 2 && widget->selected == 1 &&
                      second && strcmp(second->select_options[1].value, "two") == 0 &&
                      second->select_options == widget->select_options + 2,
                  "the shrunk slice and the slid neighbour are both intact");
            CHECK(g_v2_api[1]->panel.set_options(g_v2_api[1], "frame", "auto", V2_PANEL_OPTIONS, 3) ==
                      TORIRS_RESULT_OK &&
                      PluginHost_PanelWidgetAt(hv2, generation, 2)->select_option_count == 3,
                  "the original three come back");
            widget = PluginHost_PanelWidgetAt(hv2, generation, 2);
        }
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
        /* A collapsed well (TORIRS_PANEL_CUSTOM_HEIGHT_COLLAPSED) is a height
         * the host keeps -- the presenter hides the row -- while any other
         * height under the minimum is still raised to it. */
        CHECK(
            g_v2_api[1]->panel.set_height(g_v2_api[1], "labelled_chart", TORIRS_PANEL_CUSTOM_HEIGHT_COLLAPSED) ==
                    TORIRS_RESULT_OK &&
                PluginHost_PanelWidgetAt(hv2, generation, 4)->preferred_height ==
                    TORIRS_PANEL_CUSTOM_HEIGHT_COLLAPSED,
            "a custom well collapses in place");
        CHECK(
            g_v2_api[1]->panel.set_height(g_v2_api[1], "labelled_chart", -5) == TORIRS_RESULT_OK &&
                PluginHost_PanelWidgetAt(hv2, generation, 4)->preferred_height ==
                    TORIRS_PANEL_CUSTOM_HEIGHT_MIN,
            "any other height under the minimum is raised to it");
        CHECK(
            g_v2_api[1]->panel.set_height(g_v2_api[1], "labelled_chart", 72) == TORIRS_RESULT_OK &&
                PluginHost_PanelWidgetAt(hv2, generation, 4)->preferred_height == 72,
            "and a collapsed well grows back by a setter");

        /* ---- a button the page cannot service does not fire ------------ */
        widget = PluginHost_PanelWidgetAt(hv2, generation, 6);
        CHECK(
            widget && widget->kind == TORIRS_PANEL_WIDGET_BUTTON &&
                strcmp(widget->id, "commit") == 0 && widget->value == 0,
            "a button built disabled reports itself disabled");
        CHECK(
            widget && !PluginHost_PanelDispatch(
                          hv2,
                          generation,
                          widget->serial,
                          10,
                          "commit",
                          TORIRS_PANEL_ACTION_ACTIVATE,
                          0,
                          NULL,
                          0,
                          0) &&
                g_v2_button_actions == 0,
            "and its action is refused rather than delivered anyway");
        CHECK(
            g_v2_api[1]->panel.set_value(g_v2_api[1], "commit", 1) ==
                    TORIRS_RESULT_OK &&
                PluginHost_PanelWidgetAt(hv2, generation, 6)->value == 1,
            "enabling it in place is a value change, not a rebuild");
        CHECK(
            widget && PluginHost_PanelDispatch(
                          hv2,
                          generation,
                          widget->serial,
                          11,
                          "commit",
                          TORIRS_PANEL_ACTION_ACTIVATE,
                          0,
                          NULL,
                          0,
                          0) &&
                g_v2_button_actions == 1,
            "and the same click now reaches the plugin");

        /* ---- one row is reidentified; the page around it is not -------- */
        {
            struct ToriRS_PluginPanelChange change;
            uint32_t before[8];
            uint32_t was;
            int const count = PluginHost_PanelWidgetCount(hv2, generation);
            int kept = 0;

            /* A presenter that has just built the page has consumed the
             * structural declaration; without this the journal is still in
             * "rebuild" and no per-row change would be recorded at all. */
            PluginHost_PanelChangesAcknowledge(hv2, generation);
            CHECK(count == 7, "the page starts with its seven declared rows");
            for( int i = 0; i < count; i++ )
                before[i] = PluginHost_PanelWidgetAt(hv2, generation, i)->serial;
            was = before[3];

            CHECK(
                g_v2_api[1]->panel.reidentify(g_v2_api[1], "chart") ==
                    TORIRS_RESULT_OK,
                "one named row takes a new identity");
            widget = PluginHost_PanelWidgetAt(hv2, generation, 3);
            CHECK(
                PluginHost_PanelWidgetCount(hv2, generation) == count && widget &&
                    strcmp(widget->id, "chart") == 0 && widget->serial != was,
                "the row count and its order are untouched; only its serial moved");
            for( int i = 0; i < count; i++ )
                if( i != 3 &&
                    PluginHost_PanelWidgetAt(hv2, generation, i)->serial ==
                        before[i] )
                    kept++;
            CHECK(
                kept == count - 1,
                "every other row keeps the identity it was built with");
            CHECK(
                PluginHost_PanelChangeNext(hv2, generation, &change) == 1 &&
                    change.widget_index == 3 &&
                    change.flags == TORIRS_PLUGIN_PANEL_CHANGE_IDENTITY &&
                    change.widget_serial == widget->serial,
                "the journal carries one identity change and names the new serial");
            CHECK(
                PluginHost_PanelChangeNext(hv2, generation, &change) == 0,
                "and nothing else -- a reidentify is not a page rebuild");
            CHECK(
                PluginHost_PanelNeedsDraw(hv2, generation, widget->serial) &&
                    !PluginHost_PanelNeedsDraw(hv2, generation, was),
                "the well is dirty under its new identity and absent under the old");
            CHECK(
                !PluginHost_PanelDispatch(
                    hv2, generation, was, 12, "chart",
                    TORIRS_PANEL_ACTION_ACTIVATE, 0, NULL, 0, 0) &&
                    g_v2_custom_actions == 0,
                "a click authored against the row's old serial is refused");
            CHECK(
                PluginHost_PanelDispatch(
                    hv2, generation, widget->serial, 13, "chart",
                    TORIRS_PANEL_ACTION_ACTIVATE, 0, NULL, 0, 0) &&
                    g_v2_custom_actions == 1,
                "while the same click against the new serial is delivered");
        }

        /* ---- the row's NAME, and the reader's place ------------------- */
        {
            struct ToriRS_PluginPanelChange change;

            PluginHost_PanelChangesAcknowledge(hv2, generation);

            /*
             * Renaming a row that HAS a name is a setter.
             *
             * KEY_VALUE, TOGGLE, SELECT and ACTION_ROW are built from `label`
             * and carry their value in a second string, and the patch path had
             * no arm for one at all -- so renaming one cost a whole page
             * rebuild, which throws the scroll away and retires every retained
             * custom run. It was the last unnecessary rebuild in the row model.
             */
            CHECK(
                g_v2_api[1]->panel.set_label(g_v2_api[1], "frame", "Game frame") ==
                    TORIRS_RESULT_OK,
                "a SELECT takes a new name");
            widget = PluginHost_PanelWidgetAt(hv2, generation, 2);
            CHECK(
                widget && strcmp(widget->label, "Game frame") == 0,
                "which lands on the row the host already holds");
            CHECK(
                PluginHost_PanelChangeNext(hv2, generation, &change) == 1 &&
                    change.widget_index == 2 &&
                    change.flags == TORIRS_PLUGIN_PANEL_CHANGE_LABEL,
                "journalled as a LABEL change and not as a rebuild");
            CHECK(
                PluginHost_PanelChangeNext(hv2, generation, &change) == 0,
                "and as nothing else: a name is not a value");

            /*
             * And a kind whose ONE string already travels as `text` is refused
             * rather than silently given a second spelling -- two spellings for
             * one string is how a later set_text reverts a rename with nothing
             * to say it happened.
             */
            CHECK(
                g_v2_api[1]->panel.set_label(g_v2_api[1], "commit", "Send") !=
                    TORIRS_RESULT_OK,
                "a BUTTON, whose caption is its text, refuses a label");
            CHECK(
                PluginHost_PanelChangeNext(hv2, generation, &change) == 0,
                "and journals nothing for the refusal");

            /*
             * The reader's place. -1 and not 0 with no page up, because 0 IS
             * an answer -- the top of one -- and a plugin that could not tell
             * the two apart would restore a place nobody ever took.
             */
            PluginHost_PanelSetScroll(hv2, generation, 48);
            CHECK(
                g_v2_api[1]->panel.scroll(g_v2_api[1]) == 48,
                "a plugin reads where its page is scrolled to");
            {
                int wanted = -1;
                CHECK(
                    g_v2_api[1]->panel.scroll_to(g_v2_api[1], 96) == TORIRS_RESULT_OK,
                    "and can ask for it to move");
                CHECK(
                    PluginHost_PanelTakeScrollRequest(hv2, generation, &wanted) &&
                        wanted == 96,
                    "which the presenter picks up exactly once");
                CHECK(
                    !PluginHost_PanelTakeScrollRequest(hv2, generation, &wanted),
                    "and not twice");
                CHECK(
                    !PluginHost_PanelTakeScrollRequest(hv2, generation + 1000, &wanted),
                    "a request for a selection that is gone is dropped, not kept");
            }
            /*
             * With no page up the answer is -1, not 0.
             *
             * 0 is a legitimate answer -- the top of a page that IS up -- so a
             * plugin that could not tell the two apart would restore a place
             * nobody ever took, on a page nobody is reading.
             */
            (void)PluginHost_PanelLayout(
                hv2, generation, 0, 0, 1000, TORIRS_PANEL_SIZE_MEDIUM, false, true);
            CHECK(
                g_v2_api[1]->panel.scroll(g_v2_api[1]) == -1,
                "no page up is -1, not the top of one that is");
            CHECK(
                g_v2_api[1]->panel.scroll_to(g_v2_api[1], 10) != TORIRS_RESULT_OK,
                "and a reader who is not there cannot be moved");
            (void)PluginHost_PanelLayout(
                hv2, generation, 320, 400, 1000, TORIRS_PANEL_SIZE_MEDIUM, true, true);
        }

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
        g_stat_xp_unstated = 1;
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
            g_v2_seam.skill_stated_returned && g_v2_seam.skill_stated.stated &&
                g_v2_seam.skill_stated.index == 0 &&
                strcmp(g_v2_seam.skill_stated.name, "Attack") == 0 &&
                g_v2_seam.skill_stated.xp == 1154,
            "a stated skill answers true and says so in the snapshot");
        CHECK(
            !g_v2_seam.skill_unstated_returned &&
                !g_v2_seam.skill_unstated.stated &&
                g_v2_seam.skill_unstated.index == 1 &&
                strcmp(g_v2_seam.skill_unstated.name, "Defence") == 0 &&
                g_v2_seam.skill_unstated.xp == 0 &&
                g_v2_seam.skill_unstated.current_level == 0 &&
                g_v2_seam.skill_unstated.base_level == 0,
            "a skill with no reading yet names itself and states nothing");
        CHECK(
            !g_v2_seam.skill_missing_returned &&
                !g_v2_seam.skill_missing.stated &&
                g_v2_seam.skill_missing.index == -1 &&
                g_v2_seam.skill_missing.name[0] == '\0',
            "an index this client has no skill for is told apart from no reading");
        g_stat_xp_unstated = -1;
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

        data = ToriMem_Alloc(ToriMem_Net(), 4); /* an IO payload */
        memcpy(data, "DATA", 4);
        PluginHost_AssetDeliver(seam_host, "v2-seam-probe", "raw.bin", data, 4);
        data = ToriMem_Alloc(ToriMem_Net(), 3); /* an IO payload */
        memcpy(data, "IMG", 3);
        PluginHost_AssetDeliver(seam_host, "v2-seam-probe", "image.bin", data, 3);
        data = ToriMem_Alloc(ToriMem_Net(), 5); /* an IO payload */
        memcpy(data, "MODEL", 5);
        PluginHost_AssetDeliver(seam_host, "v2-seam-probe", "model.bin", data, 5);
        PluginHost_AssetDeliver(seam_host, "v2-seam-probe", "missing.bin", NULL, 0);
        data = ToriMem_Alloc(ToriMem_Net(), 4); /* an IO payload */
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
        unsigned char* data;

        memset(&g_engine, 0, sizeof(g_engine));
        memset(&g_v2_aba, 0, sizeof(g_v2_aba));
        g_v2_aba_starts = 0;
        g_screen_now = TORIRS_SCREEN_GAME;
        engine = fake_engine();
        aba_host = PluginHost_New(&engine);
        CHECK(
            PluginHost_Register(aba_host, &V2_ABA_PROBE) == 0,
            "the V2 resource-incarnation probe registers");
        PluginHost_Start(aba_host);

        data = ToriMem_Alloc(ToriMem_Net(), 3); /* an IO payload */
        memcpy(data, "IMG", 3);
        PluginHost_AssetDeliver(aba_host, "v2-aba", "aba-old.png", data, 3);
        data = ToriMem_Alloc(ToriMem_Net(), 5); /* an IO payload */
        memcpy(data, "MODEL", 5);
        PluginHost_AssetDeliver(aba_host, "v2-aba", "aba-old.model", data, 5);
        PluginHost_FrameStart(aba_host, 1, 0);
        PluginHost_LogicTick(aba_host, 1);
        PluginHost_LogicTick(aba_host, 2);
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

        data = ToriMem_Alloc(ToriMem_Net(), 3); /* an IO payload */
        memcpy(data, "NEW", 3);
        PluginHost_AssetDeliver(aba_host, "v2-aba", "aba-new.png", data, 3);
        data = ToriMem_Alloc(ToriMem_Net(), 5); /* an IO payload */
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
    test_widget_states();
    test_widget_operations();
    test_widget_images();
    test_world_draw_context();
    test_draw_polygon_and_maps();
    test_quest_helper_cache_game_scripts();
    test_quest_helper_client_apis();
    test_scene_instance_place();
    test_menu_order_composes();
    test_quest_helper_widgets_events();
    test_quest_helper_fix_seams();
    test_script_hooks();
    test_world_hull_answers_its_refusals();
    test_world_tile_answers_its_refusal();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
