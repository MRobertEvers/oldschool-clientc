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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond, msg)                                                                      \
    do                                                                                        \
    {                                                                                         \
        g_checks++;                                                                           \
        if( !(cond) )                                                                         \
        {                                                                                     \
            g_failures++;                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));                   \
        }                                                                                     \
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
};

static struct FakeEngine g_engine;

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
static int
fake_local_player(void* u, struct ToriRS_PluginPlayerSnap* out)
{
    (void)u;
    memset(out, 0, sizeof(*out));
    out->true_x = 3200;
    out->true_z = 3200;
    return 1;
}
static int
fake_npc_next(void* u, int iter, struct ToriRS_PluginNpcSnap* out)
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
fake_npc_by_slot(void* u, int slot, struct ToriRS_PluginNpcSnap* out)
{
    (void)u;
    memset(out, 0, sizeof(*out));
    out->server_slot = slot;
    return slot >= 0 ? 1 : 0;
}
static int
fake_player_next(void* u, int iter, struct ToriRS_PluginPlayerSnap* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_loc_next(void* u, int iter, struct ToriRS_PluginLocSnap* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_highlight_next(void* u, int iter, struct ToriRS_PluginHighlightItem* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static void
fake_notify(void* u, char const* text)
{
    (void)u;
    snprintf(g_engine.last_notify, sizeof(g_engine.last_notify), "%s", text);
    g_engine.notifies++;
}
static int
fake_key_held(void* u, int key)
{
    (void)u;
    return key == 42;
}
static int
fake_hover_tile(void* u, int* ox, int* oz, int* olevel)
{
    (void)u;
    *ox = 3200;
    *oz = 3200;
    *olevel = 0;
    return 1;
}
static int
fake_hover_entity(void* u, struct ToriRS_PluginHoverEntity* out)
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
fake_element_height(void* u, int element_id)
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
    { "draw_distance", "Draw distance", TORIRS_PLUGIN_FEATURE_INT, 25, 90, NULL,
      { 0, 0 }, 0, 25, 25 },
    { "camera_zoom", "Camera zoom", TORIRS_PLUGIN_FEATURE_ENUM, 0, 0, "Adjustable|Fixed",
      { 0, 1 }, 2, 0, 0 },
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
fake_feature_next(void* u, int i, struct ToriRS_PluginFeature* o)
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
fake_feature_get(void* u, char const* k)
{
    (void)u;

    struct FakeFeature const* f = fake_feature_find(k);
    return f ? f->value : TORIRS_PLUGIN_FEATURE_UNSET;
}

static int
fake_feature_set(void* u, char const* k, int v)
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
fake_varbit(void* u, int id)
{
    (void)u;
    return id == 12977 ? 1 : 0;
}
static int
fake_varp(void* u, int id)
{
    (void)u;
    /* The colour rows store `colour + 1`; 0x00FF00 + 1 here. */
    return id == 3108 ? 0x00FF01 : 0;
}
static int
fake_project(void* u, int fx, int fz, int h, int* ox, int* oy)
{
    (void)u;
    (void)h;
    *ox = fx / 128;
    *oy = fz / 128;
    return 1;
}
static int
fake_draw_tile(void* u, int a, int b, int c, uint32_t d, uint32_t e, int f)
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
fake_draw_hull(void* u, int a, uint32_t b, int c, int d)
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
fake_draw_line(void* u, int a, int b, int c, int d, uint32_t e)
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
fake_draw_text(void* u, int a, int b, char const* s, uint32_t c)
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
fake_draw_rect(void* u, int a, int b, int c, int d, uint32_t e, int f)
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
fake_obj_next(void* u, int iter, struct ToriRS_PluginObjSnap* out)
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
fake_asset_read(void* u, char const* plugin, char const* name)
{
    struct FakeEngine* e = u;
    e->asset_reads++;
    snprintf(e->last_asset_plugin, sizeof(e->last_asset_plugin), "%s", plugin);
    snprintf(e->last_asset_name, sizeof(e->last_asset_name), "%s", name);
    return 1;
}

static int
fake_asset_write(void* u, char const* plugin, char const* name, void const* data, int size)
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
        out_path,
        (size_t)out_path_size,
        "%s%s%s",
        dir ? dir : "",
        (dir && *dir) ? "/" : "",
        name);
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
fake_model_publish(void* u, int model, void const* data, int size)
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
fake_model_release(void* u, int model)
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
fake_mesh_destroy(void* u, int mesh)
{
    struct FakeEngine* e = u;
    (void)mesh;
    e->meshes_live--;
}

static void
fake_mesh_clear(void* u, int mesh)
{
    struct FakeEngine* e = u;
    (void)mesh;
    e->mesh_clears++;
}

static int
fake_mesh_vertex(void* u, int mesh, int x, int y, int z)
{
    struct FakeEngine* e = u;
    (void)mesh;
    (void)x;
    (void)y;
    (void)z;
    return e->mesh_vertices++;
}

static int
fake_mesh_face(void* u, int mesh, int a, int b, int c, int hsl, int alpha)
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
fake_object_destroy(void* u, int object)
{
    struct FakeEngine* e = u;
    if( object < 0 || object >= FAKE_OBJECTS_MAX || !e->objects[object].in_use )
        return;
    memset(&e->objects[object], 0, sizeof(e->objects[object]));
    e->objects_live--;
}

static void
fake_object_set_model(void* u, int object, int source, int id)
{
    struct FakeEngine* e = u;
    e->objects[object].source = source;
    e->objects[object].model_id = id;
}

static void
fake_object_recolor(void* u, int object, int from, int to)
{
    struct FakeEngine* e = u;
    (void)from;
    (void)to;
    e->objects[object].recolors++;
}

static void
fake_object_clear_recolors(void* u, int object)
{
    struct FakeEngine* e = u;
    e->objects[object].recolors = 0;
}

static void
fake_object_set_anim(void* u, int object, int seq_id, int loop)
{
    struct FakeEngine* e = u;
    (void)loop;
    e->objects[object].seq_id = seq_id;
}

static void
fake_object_set_light(void* u, int object, int ambient, int contrast)
{
    (void)u;
    (void)object;
    (void)ambient;
    (void)contrast;
}

static void
fake_object_set_position(void* u, int object, int x, int z, int level, int height, int yaw)
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
fake_object_set_active(void* u, int object, int active)
{
    struct FakeEngine* e = u;
    e->objects[object].active = active;
}

static int
fake_object_ready(void* u, int object)
{
    struct FakeEngine* e = u;
    return e->objects[object].model_id >= 0;
}

static int
fake_hsl_from_rgb(void* u, uint32_t rgb)
{
    (void)u;
    return (int)(rgb & 0xffff);
}

static uint32_t
fake_hsl_to_rgb(void* u, int hsl)
{
    (void)u;
    return (uint32_t)hsl;
}

static int
fake_menu_add(void* u, void* cursor, char const* text, int action)
{
    (void)u;
    (void)cursor;
    g_engine.menu_rows++;
    g_engine.last_action = action;
    snprintf(g_engine.last_text, sizeof(g_engine.last_text), "%s", text);
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
fake_mouse_pos(void* u, int* x, int* y)
{
    (void)u;
    if( x )
        *x = 0;
    if( y )
        *y = 0;
    return 1;
}
static int
fake_minimap_rect(void* u, int* x, int* y, int* w, int* h)
{
    (void)u;
    if( x )
        *x = 550;
    if( y )
        *y = 4;
    if( w )
        *w = 146;
    if( h )
        *h = 151;
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
fake_layout_set(void* u, int owned, int canvas, int fixed_w, int fixed_h)
{
    struct FakeEngine* e = u;
    e->layout_sets++;
    e->layout_owned = owned;
    e->layout_canvas = canvas;
    e->layout_fixed_w = fixed_w;
    e->layout_fixed_h = fixed_h;
}
static int
fake_layout_slot(void* u, int slot, int member, int x, int y, int w, int h)
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
fake_layout_slot_skin(void* u, int slot, int art, int mask)
{
    (void)u;
    (void)slot;
    (void)art;
    (void)mask;
    return 0;
}
static int
fake_layout_slot_overlay(void* u, int slot, int image, int x, int y, int trans)
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
fake_layout_scrollbar(void* u, int const* images, int count)
{
    (void)u;
    (void)images;
    (void)count;
    return 0;
}
static int
fake_display_setting(void* u, int setting, int* out_value, int* out_min, int* out_max)
{
    (void)u;
    (void)setting;
    (void)out_value;
    (void)out_min;
    (void)out_max;
    return 0;
}
static int
fake_display_setting_set(void* u, int setting, int value)
{
    (void)u;
    (void)setting;
    (void)value;
    return 0;
}
static int
fake_tab_active(void* u)
{
    (void)u;
    return -1;
}
static int
fake_tab_select(void* u, int tabno)
{
    (void)u;
    (void)tabno;
    return 0;
}
static int
fake_obj_info(void* u, int obj_id, struct ToriRS_PluginObjInfo* out)
{
    (void)u;
    (void)obj_id;
    (void)out;
    return 0;
}
static int
fake_inv_slot(void* u, int inv, int slot, int* out_obj_id, int* out_count)
{
    (void)u;
    (void)inv;
    (void)slot;
    (void)out_obj_id;
    (void)out_count;
    return 0;
}
static int
fake_inv_size(void* u, int inv)
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

static int
fake_slot_rect(void* u, int slot, int* x, int* y, int* w, int* h)
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
fake_slot_member_rect(void* u, int slot, int member, int* x, int* y, int* w, int* h)
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
fake_component_rect(void* u, int component_id, int* x, int* y, int* w, int* h)
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
static int g_role_anchor_calls;
static int g_role_anchor_resets;
static int g_role_anchor_invalids;
static int g_role_anchor_current_plugin = -1;
static int g_role_anchor_replace = -1;
static int g_role_anchor_last_replace = -1;

static int
role_is(char const* role)
{
    return g_role_name && strcmp(role, g_role_name) == 0;
}

static int
fake_role_rect(void* u, char const* role, int* x, int* y, int* w, int* h)
{
    (void)u;
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
fake_role_visible(void* u, char const* role)
{
    (void)u;
    return role_is(role) ? g_role_visible : 0;
}

static int
fake_role_click(void* u, char const* role, int op)
{
    (void)u;
    if( !role_is(role) )
        return 0;
    g_role_clicked = role;
    g_role_clicked_op = op;
    return 1;
}

static int
fake_role_id(void* u, char const* role)
{
    (void)u;
    return role_is(role) ? g_role_component_id : -1;
}

static int
fake_role_replace(void* u, int plugin, char const* role, int enabled)
{
    (void)u;
    g_role_replace_calls++;
    g_role_replace_plugin = plugin;
    g_role_replace_enabled = enabled;
    snprintf(g_role_replace_name, sizeof(g_role_replace_name), "%s", role ? role : "");
    return role && role_is(role);
}

static int
fake_role_anchor(void* u, int plugin, char const* role, int replace)
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
    return role_is(role);
}

static int
fake_stat(void* u, int skill, int* cur, int* base)
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
fake_stat_xp(void* u, int skill, int* xp, int* level_xp, int* next_xp)
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
fake_skill_name(void* u, int skill)
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
fake_draw_select_canvas(void* u, int canvas)
{
    (void)u;
    (void)canvas;
}
static int
fake_image_publish(void* u, int slot, void const* data, int size, int* w, int* h)
{
    (void)u;
    (void)slot;
    (void)data;
    (void)size;
    if( w )
        *w = 26;
    if( h )
        *h = 26;
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
fake_image_publish_argb(void* u, int slot, int w, int h, uint32_t const* argb)
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
fake_image_read(void* u, int slot, uint32_t* out, int max)
{
    (void)u;
    int const pixels = g_composed.w * g_composed.h;

    if( slot != g_composed.slot || pixels <= 0 || pixels > max )
        return 0;
    memcpy(out, g_composed.argb, (size_t)pixels * sizeof(uint32_t));
    return pixels;
}
static void
fake_image_release(void* u, int slot)
{
    (void)u;
    (void)slot;
}
static int
fake_draw_image(
    void* u, int slot, int x, int y, int w, int h, int cx, int cy, int cw, int ch, int trans)
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
    (void)plugin;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)ops;
    (void)op_count;
    (void)tag;
    return 1;
}
static int
fake_if_click(void* u, int component_id, int op)
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
fake_lane(void* u, struct ToriRS_PluginLane* o)
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
    e.world_cycle = fake_world_cycle;
    e.frame_ms = fake_frame_ms;
    e.frame_work_us = fake_frame_work_us;
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
    e.minimap_rect = fake_minimap_rect;
    e.slot_rect = fake_slot_rect;
    e.slot_member_rect = fake_slot_member_rect;
    e.component_rect = fake_component_rect;
    e.role_rect = fake_role_rect;
    e.role_visible = fake_role_visible;
    e.role_click = fake_role_click;
    e.role_id = fake_role_id;
    e.role_replace = fake_role_replace;
    e.role_anchor = fake_role_anchor;
    e.layout_set = fake_layout_set;
    e.layout_slot = fake_layout_slot;
    e.layout_slot_skin = fake_layout_slot_skin;
    e.layout_slot_overlay = fake_layout_slot_overlay;
    e.layout_scrollbar = fake_layout_scrollbar;
    e.display_setting = fake_display_setting;
    e.display_setting_set = fake_display_setting_set;
    e.tab_active = fake_tab_active;
    e.tab_select = fake_tab_select;
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
    e.draw_image = fake_draw_image;
    e.hit_region = fake_hit_region;
    e.if_click = fake_if_click;
    e.menu_add = fake_menu_add;
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

static enum ToriRS_PluginVerdict
alpha_tick(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_alpha_ticks++;
    if( g_order_count < 8 )
        g_order[g_order_count++] = 1;
    return TORIRS_PLUGIN_PASS;
}

static struct ToriRS_PluginApi const* g_api;

static enum ToriRS_PluginVerdict
alpha_menu_add(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ud;
    g_api->menu_add(ctx, (struct ToriRS_PluginEvMenuBuild*)ev, "Tag Goblin", 7u);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
alpha_select(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ctx;
    (void)ud;
    struct ToriRS_PluginEvMenuSelect* sel = ev;
    g_select_calls++;
    g_last_tag = sel->plugin_tag;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
alpha_packet_in(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ctx;
    (void)ud;
    struct ToriRS_PluginEvPacketIn* p = ev;
    if( p->name == 99 )
        p->drop = true;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
alpha_draw(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
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
alpha_canvas_anchor(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ev;
    (void)ud;
    (void)g_api->role_anchor(ctx, "report_button");
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
alpha_canvas_after_anchor(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_alpha_anchor_second_saw_reset = g_role_anchor_current_plugin < 0;
    return TORIRS_PLUGIN_PASS;
}

static void
alpha_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LOGIC_TICK, alpha_tick, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_MENU_BUILD, alpha_menu_add, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_MENU_SELECT, alpha_select, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_PACKET_IN, alpha_packet_in, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_WORLD, alpha_draw, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_CANVAS, alpha_canvas_anchor, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_CANVAS, alpha_canvas_after_anchor, NULL);
}

static struct ToriRS_PluginConfigItem const ALPHA_CONFIG[] = {
    { "colour", TORIRS_PLUGIN_CFG_COLOR, "Colour", "#00FF00", 0, 0, NULL },
    { "level", TORIRS_PLUGIN_CFG_INT, "Level", "3", 0, 10, NULL },
    { "on", TORIRS_PLUGIN_CFG_BOOL, "On", "1", 0, 0, NULL },
    { "hidden", TORIRS_PLUGIN_CFG_STRING, NULL, "", 0, 0, NULL },
    { NULL, TORIRS_PLUGIN_CFG_BOOL, NULL, NULL, 0, 0, NULL },
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
gamma_chat(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ctx;
    (void)ud;
    struct ToriRS_PluginEvChat* c = ev;
    g_gamma_chats++;
    snprintf(g_gamma_chat_text, sizeof(g_gamma_chat_text), "%s", c->text);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
gamma_game_event(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
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
gamma_asset(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
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
gamma_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
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
win_build(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
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
win_ui(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
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
winner_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
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
reloader_start(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
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
reloader_stop(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_reload_stops++;
    return TORIRS_PLUGIN_PASS;
}

static struct ToriRS_PluginConfigItem const RELOADER_CFG[] = {
    { .key = "colour", .label = "colour", .type = TORIRS_PLUGIN_CFG_STRING,
      .default_value = "#000000" },
    { 0 },
};

static void
reloader_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
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
beta_tick(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    if( g_order_count < 8 )
        g_order[g_order_count++] = 2;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
beta_key_consume(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
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
beta_canvas_competing_anchor(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    struct ToriRS_PluginEvDraw* draw = ev;

    (void)ud;
    if( !g_beta_anchor_attempt )
        return TORIRS_PLUGIN_PASS;
    g_beta_anchor_retarget_invalid =
        !g_api->role_anchor(ctx, "report_button") &&
        g_role_anchor_current_plugin >= 0 && g_role_anchor_replace == -2;
    g_beta_draw_saw_invalid = g_role_anchor_replace == -2;
    g_api->draw_rect(ctx, draw->surface, 1, 1, 2, 2, 0xffffffu, 0);
    return TORIRS_PLUGIN_PASS;
}

static void
beta_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LOGIC_TICK, beta_tick, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_KEY, beta_key_consume, NULL);
    api->subscribe(
        ctx, TORIRS_PLUGIN_EV_DRAW_CANVAS, beta_canvas_competing_anchor, NULL);
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
standoff_start(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_standoff_starts++;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
standoff_stop(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_standoff_stops++;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
standoff_tick(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_standoff_ticks++;
    return TORIRS_PLUGIN_PASS;
}

static void
standoff_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
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
latecomer_stop(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ctx;
    (void)ev;
    (void)ud;
    g_latecomer_stops++;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
latecomer_start(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
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
    { .key = "colour", .label = "colour", .type = TORIRS_PLUGIN_CFG_STRING,
      .default_value = "#000000" },
    { 0 },
};

static void
latecomer_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
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

/* Releasing a layout from inside its declaration abandons that transaction.
 * Reclaiming it immediately is deliberately the hard case: comparing only the
 * owner before and after dispatch mistakes the new claim for the old one and
 * commits the half-built declaration. */
static struct ToriRS_PluginApi const* g_layout_api;
static int g_layout_initial_claim_ok;
static int g_layout_reclaim_ok;
static int g_layout_events;
static int g_layout_width;
static int g_layout_height;
static int g_layout_canvas;

static enum ToriRS_PluginVerdict
layout_reclaimer_start(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    (void)ev;
    (void)ud;
    g_layout_initial_claim_ok =
        g_layout_api->layout_claim(ctx, TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW, 0, 0);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
layout_reclaimer_layout(struct ToriRS_PluginCtx* ctx, void* ev, void* ud)
{
    struct ToriRS_PluginEvLayout const* layout = ev;
    (void)ud;
    g_layout_events++;
    g_layout_width = layout->width;
    g_layout_height = layout->height;
    g_layout_canvas = layout->canvas;
    if( g_layout_events == 1 )
    {
        g_layout_api->layout_release(ctx);
        g_layout_reclaim_ok =
            g_layout_api->layout_claim(ctx, TORIRS_PLUGIN_CANVAS_FIXED, 765, 503);
    }
    return TORIRS_PLUGIN_PASS;
}

static void
layout_reclaimer_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    g_layout_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, layout_reclaimer_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LAYOUT, layout_reclaimer_layout, NULL);
}

static struct ToriRS_PluginDef const LAYOUT_RECLAIMER = {
    .name = "layout-reclaimer",
    .version = "1",
    .init = layout_reclaimer_init,
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
        CHECK(strcmp(PluginHost_Title(host, a), "Alpha The Plugin") == 0,
              "a declared title is what the panel gets");
        CHECK(strcmp(PluginHost_Name(host, a), "alpha") == 0,
              "and the name is untouched by it");
        CHECK(strcmp(PluginHost_Title(host, t), "Ground Items 2") == 0,
              "a title-less plugin gets one derived from its id");
        CHECK(strcmp(PluginHost_Name(host, t), "ground-items_2") == 0,
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
        CHECK(PluginHost_IsEnabled(host, e),
              "a saved enabled=0 from an older build is ignored too");
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

        CHECK(api->feature_get(ctx, "pathing_mode") == TORIRS_PLUGIN_FEATURE_UNSET,
              "an unpublished flag reads as unset");
        CHECK(!api->feature_set(ctx, "pathing_mode", 1),
              "and cannot be set, which is the whole server-agreement rule");

        CHECK(api->feature_set(ctx, "draw_distance", 60), "a value in range is taken");
        CHECK(api->feature_get(ctx, "draw_distance") == 60, "and is what reads back");
        CHECK(!api->feature_set(ctx, "draw_distance", 4000), "a value out of range is not");
        CHECK(api->feature_get(ctx, "draw_distance") == 60, "and leaves the flag alone");

        CHECK(api->feature_set(ctx, "draw_distance", TORIRS_PLUGIN_FEATURE_UNSET),
              "the sentinel is accepted");
        CHECK(api->feature_get(ctx, "draw_distance") == 25,
              "and restores what the boot resolved");

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

        CHECK(api->slot_rect(ctx, TORIRS_PLUGIN_SLOT_CHAT_BUTTONS, &x, &y, &w, &h),
              "the role answers");
        CHECK(x == 6, "with the box the frame gave the role");

        x = y = w = h = -1;
        CHECK(api->slot_member_rect(ctx, TORIRS_PLUGIN_SLOT_CHAT_BUTTONS, 3, &x, &y, &w, &h),
              "and the member answers for the number it was asked for");
        CHECK(x == 408 && y == 467 && w == 56 && h == 19,
              "with that member's own box, not the role's");

        x = y = w = h = -1;
        CHECK(!api->slot_member_rect(ctx, TORIRS_PLUGIN_SLOT_CHAT_BUTTONS, 2, &x, &y, &w, &h),
              "a member this frame does not declare is 0");
        CHECK(x == -1 && y == -1 && w == -1 && h == -1, "and leaves the outputs untouched");

        CHECK(!api->slot_member_rect(ctx, TORIRS_PLUGIN_SLOT_SAFE, 0, &x, &y, &w, &h),
              "SAFE is derived and has no members to number");
        CHECK(!api->slot_member_rect(ctx, TORIRS_PLUGIN_SLOT_CHAT_BUTTONS, -1, &x, &y, &w, &h),
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
        CHECK(api->component_rect(ctx, (162 << 16) | 31, &x, &y, &w, &h),
              "a mounted component answers by id");
        CHECK(x == 437 && y == 480 && w == 79 && h == 23, "with its own box");

        x = y = w = h = -1;
        CHECK(!api->component_rect(ctx, (162 << 16) | 33, &x, &y, &w, &h),
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
        CHECK(!api->role_rect(ctx, "report_button", &x, &y, &w, &h),
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
        CHECK(!api->role_rect(ctx, "logout_screen", &x, &y, &w, &h),
              "a role this profile did not bind is still absent");

        g_role_visible = 0;
        CHECK(!api->role_visible(ctx, "report_button"), "a hidden element is not visible");
        CHECK(api->role_rect(ctx, "report_button", &x, &y, &w, &h),
              "…but it still has a box, which is why the two are separate verbs");

        g_role_clicked = NULL;
        g_role_clicked_op = -1;
        CHECK(api->role_click(ctx, "report_button", 1), "a bound role presses");
        CHECK(g_role_clicked && strcmp(g_role_clicked, "report_button") == 0,
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
        CHECK(api->role_replace(ctx, "temporarily_absent", 1),
              "an absent role can be claimed persistently");
        CHECK(g_role_replace_calls == 1 && g_role_replace_enabled == 1,
              "the accepted claim is published even before it resolves");
        CHECK(api->role_replace(ctx, "temporarily_absent", 0),
              "the absent standing claim releases idempotently");

        g_role_replace_calls = 0;
        CHECK(api->role_replace(ctx, "report_button", 1),
              "the first plugin claims a semantic replacement");
        CHECK(g_role_replace_plugin == a && g_role_replace_enabled == 1,
              "the engine receives the claim with its owner");
        CHECK(!api->role_replace(
                  PluginHost_Ctx(host, b), "report_button", 1),
              "a second plugin cannot replace the same role");
        CHECK(g_role_replace_calls == 1,
              "a refused competing claim never reaches the engine");

        PluginHost_ReconcileRoleReplacements(host);
        CHECK(g_role_replace_calls == 2 && g_role_replace_enabled == 1,
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
        CHECK(g_role_anchor_invalids == 1 && g_beta_anchor_retarget_invalid,
              "a competing replacement anchor enters active-invalid state");
        CHECK(g_beta_draw_saw_invalid,
              "draws after the failed retarget cannot inherit a prior/global anchor");
        CHECK(g_role_anchor_calls == 1 && g_role_anchor_last_replace == 1,
              "the replacement owner can anchor its canvas declarations at the tombstone");
        CHECK(g_alpha_anchor_second_saw_reset,
              "a later canvas subscriber cannot inherit the prior subscriber's anchor");
        CHECK(g_role_anchor_resets >= 4 && g_role_anchor_current_plugin < 0,
              "the host resets the anchor on both sides of each subscriber");

        CHECK(api->role_replace(ctx, "report_button", 0),
              "the owner can release its replacement");
        CHECK(g_role_replace_enabled == 0,
              "release is published to reveal the native subtree");
        CHECK(api->role_replace(
                  PluginHost_Ctx(host, b), "report_button", 1),
              "another plugin may claim after release");
        g_role_replace_calls = 0;
        PluginHost_SetEnabled(host, b, false);
        CHECK(g_role_replace_calls == 1 && g_role_replace_plugin == b &&
                  g_role_replace_enabled == 0,
              "teardown automatically releases every replacement the plugin owns");
        PluginHost_SetEnabled(host, b, true);

        CHECK(!api->role_replace(ctx, "safe", 1) &&
                  !api->role_replace(ctx, "canvas", 1),
              "derived rectangles cannot be claimed as component replacements");

        /*
         * `safe` never reaches the engine at all: it is derived from the
         * regions and the host's own reservation table, so it has to answer
         * with exactly what the region enum answers.
         */
        {
            int sx, sy, sw, sh, rx, ry, rw, rh;
            int by_slot = api->slot_rect(ctx, TORIRS_PLUGIN_SLOT_SAFE, &sx, &sy, &sw, &sh);
            int by_role = api->role_rect(ctx, "safe", &rx, &ry, &rw, &rh);
            CHECK(by_slot == by_role, "the safe role and the safe region agree that it exists");
            if( by_slot && by_role )
                CHECK(sx == rx && sy == ry && sw == rw && sh == rh,
                      "and agree on the rectangle");
        }

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
        PluginHost_FrameStart(host, 1);
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
        PluginHost_FrameStart(host, 2);
        PluginHost_DrawWorld(host);
        CHECK(g_engine.draw_items > first, "the budget resets each frame");
    }

    /* Config: defaults, typed reads, and an ini round-trip that keeps what was
     * changed and omits what was not. */
    {
        void* data = NULL;
        int size = 0;

        CHECK(strcmp(PluginHost_ConfigGet(host, a, "colour"), "#00FF00") == 0,
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
        CHECK(g_api->asset_load(ctx, "../../plugin_prefs.ini") == 0, "a path asset name is refused");
        CHECK(g_api->asset_load(ctx, "sub/dir.txt") == 0, "so is a separator");
        CHECK(g_engine.asset_reads == 0, "and neither reaches the engine");

        /* The ordinary read: queued once, delivered once, readable after. */
        CHECK(g_api->asset_load(ctx, "prices.txt") == 0, "a first load reports 'queued', not 'here'");
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
        CHECK(
            g_api->asset_data(ctx, "missing.txt", NULL) == NULL,
            "and leaves nothing resident");

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
                g_api->screenshot(ctx, NULL, "levelup.png", shot_path, (int)sizeof(shot_path)) ==
                    1,
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
                strcmp(g_engine.last_shot_dir, "shots/levels") == 0,
                "and is forwarded unchanged");
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
                    ctx, "shots/../../etc", "levelup.png", shot_path, (int)sizeof(shot_path)) ==
                    0,
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
                strcmp(g_gamma_chat_text, "Your Zulrah kill count is: 122.") == 0,
                "with its text");

            PluginHost_GameEvent(host3, "boss_kill", "Zulrah", 122, "Your Zulrah kill count is: 122.");
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

    /* ---- release and reclaim during EV_LAYOUT ----------------------------- */
    {
        struct ToriRS_PluginHost* hl = PluginHost_New(&engine);
        int const l = PluginHost_Register(hl, &LAYOUT_RECLAIMER);

        g_engine.layout_begins = 0;
        g_engine.layout_ends = 0;
        g_engine.layout_sets = 0;
        g_engine.layout_owned = 0;
        g_engine.layout_canvas = -1;
        g_engine.layout_fixed_w = -1;
        g_engine.layout_fixed_h = -1;
        g_layout_initial_claim_ok = 0;
        g_layout_reclaim_ok = 0;
        g_layout_events = 0;
        g_layout_width = 0;
        g_layout_height = 0;
        g_layout_canvas = -1;

        PluginHost_Start(hl);
        CHECK(g_layout_initial_claim_ok, "the test plugin claims the resizable frame at start");
        CHECK(PluginHost_LayoutOwner(hl) == l, "the initial layout claim has an owner");
        CHECK(
            g_engine.layout_sets == 1 && g_engine.layout_owned == 1 &&
                g_engine.layout_canvas == TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW,
            "the initial claim is published to the engine");

        PluginHost_Layout(hl, 1200, 800);
        CHECK(g_layout_events == 1, "the initial claim receives its layout event");
        CHECK(
            g_layout_width == 1200 && g_layout_height == 800 &&
                g_layout_canvas == TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW,
            "the initial event uses the window canvas");
        CHECK(g_layout_reclaim_ok, "the handler can release and reclaim its layout");
        CHECK(g_engine.layout_begins == 1, "the abandoned declaration began once");
        CHECK(g_engine.layout_ends == 0, "the abandoned declaration is not committed");
        CHECK(PluginHost_LayoutOwner(hl) == l, "the replacement claim keeps the same owner");
        CHECK(
            g_engine.layout_sets == 3 && g_engine.layout_owned == 1 &&
                g_engine.layout_canvas == TORIRS_PLUGIN_CANVAS_FIXED &&
                g_engine.layout_fixed_w == 765 && g_engine.layout_fixed_h == 503,
            "the replacement fixed-canvas claim is published after the release");

        PluginHost_Layout(hl, 1200, 800);
        CHECK(g_layout_events == 2, "the replacement claim receives a later layout event");
        CHECK(
            g_layout_width == 765 && g_layout_height == 503 &&
                g_layout_canvas == TORIRS_PLUGIN_CANVAS_FIXED,
            "the later event uses the replacement claim's fixed canvas");
        CHECK(g_engine.layout_begins == 2, "the replacement declaration starts fresh");
        CHECK(g_engine.layout_ends == 1, "the replacement declaration commits exactly once");
        CHECK(PluginHost_LayoutOwner(hl) == l, "the replacement claim remains owned");

        PluginHost_Free(hl);
    }

    PluginHost_Free(host);

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
