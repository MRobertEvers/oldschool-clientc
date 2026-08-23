/*
 * Feature Flags plugin tests.
 *
 * The plugin has no static schema -- its rows are whatever the engine
 * publishes -- so everything worth checking is a round trip through that
 * seam: does the page carry one control per flag, does a pick reach the
 * engine, does the ini keep the words the user chose rather than an ordinal,
 * and does "Revision default" put a flag back rather than merely stop
 * changing it.
 *
 * The engine here is a FAKE with three flags, one of each shape. It is not a
 * copy of app.c's table on purpose: which flags the client publishes is the
 * client's statement about what is client-only, and a test that restated it
 * would only be checking that two lists were typed the same way.
 */

#include "plugin/torirs_plugin_host.h"

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

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_FEATURE_FLAGS;

/* ------------------------------------------------------------ fake engine */

struct FakeFlag
{
    char const* key;
    char const* label;
    int kind;
    int min;
    int max;
    char const* choices;
    int values[2];
    int value_count;
    /** What this "boot" resolved, i.e. what the UNSET sentinel restores. */
    int boot;
    int value;
};

static struct FakeFlag g_flags[] = {
    { "draw_distance",
     "Draw distance",   TORIRS_PLUGIN_FEATURE_INT,
     25, 90,
     NULL,                           { 0, 0 },
     0, 25,
     25   },
    { "camera_zoom",
     "Camera zoom",     TORIRS_PLUGIN_FEATURE_ENUM,
     0,  0,
     "Adjustable|Fixed",             { 0, 1 },
     2, 0,
     0    },
    { "target_mask_held",
     "Held target bit", TORIRS_PLUGIN_FEATURE_ENUM,
     0,  0,
     "0x10 (2004)|0x20 (OldSchool)", { 0x10, 0x20 },
     2, 0x10,
     0x10 },
};

#define FLAG_COUNT ((int)(sizeof(g_flags) / sizeof(g_flags[0])))

static void
flags_reset(void)
{
    for( int i = 0; i < FLAG_COUNT; i++ )
        g_flags[i].value = g_flags[i].boot;
}

static struct FakeFlag*
flag_find(char const* key)
{
    for( int i = 0; i < FLAG_COUNT; i++ )
    {
        if( strcmp(g_flags[i].key, key) == 0 )
            return &g_flags[i];
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
    if( at >= FLAG_COUNT )
        return -1;

    struct FakeFlag const* f = &g_flags[at];
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
    struct FakeFlag const* f = flag_find(k);
    return f ? f->value : TORIRS_PLUGIN_FEATURE_UNSET;
}

static int
fake_feature_set(
    void* u,
    char const* k,
    int v)
{
    (void)u;

    struct FakeFlag* f = flag_find(k);
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

/* Everything else the host asserts on, answered flatly. */

static int
fake_world_cycle(void* u)
{
    (void)u;
    return 0;
}
static uint64_t
fake_frame_ms(void* u)
{
    (void)u;
    return 0;
}
static int
fake_local_player(
    void* u,
    struct ToriRS_PluginPlayerSnap* o)
{
    (void)u;
    (void)o;
    return 0;
}
static int
fake_npc_next(
    void* u,
    int i,
    struct ToriRS_PluginNpcSnap* o)
{
    (void)u;
    (void)i;
    (void)o;
    return -1;
}
static int
fake_npc_by_slot(
    void* u,
    int s,
    struct ToriRS_PluginNpcSnap* o)
{
    (void)u;
    (void)s;
    (void)o;
    return 0;
}
static int
fake_player_next(
    void* u,
    int i,
    struct ToriRS_PluginPlayerSnap* o)
{
    (void)u;
    (void)i;
    (void)o;
    return -1;
}
static int
fake_obj_next(
    void* u,
    int i,
    struct ToriRS_PluginObjSnap* o)
{
    (void)u;
    (void)i;
    (void)o;
    return -1;
}
static int
fake_loc_next(
    void* u,
    int i,
    struct ToriRS_PluginLocSnap* o)
{
    (void)u;
    (void)i;
    (void)o;
    return -1;
}
static int
fake_highlight_next(
    void* u,
    int i,
    struct ToriRS_PluginHighlightItem* o)
{
    (void)u;
    (void)i;
    (void)o;
    return -1;
}
static void
fake_notify(
    void* u,
    char const* t)
{
    (void)u;
    (void)t;
}
static int
fake_key_held(
    void* u,
    int k)
{
    (void)u;
    (void)k;
    return 0;
}
static int
fake_hover_tile(
    void* u,
    int* x,
    int* z,
    int* l)
{
    (void)u;
    (void)x;
    (void)z;
    (void)l;
    return 0;
}
static int
fake_hover_entity(
    void* u,
    struct ToriRS_PluginHoverEntity* o)
{
    (void)u;
    (void)o;
    return 0;
}
static int
fake_element_height(
    void* u,
    int e)
{
    (void)u;
    (void)e;
    return 0;
}
static int
fake_varbit(
    void* u,
    int i)
{
    (void)u;
    (void)i;
    return 0;
}
static int
fake_varp(
    void* u,
    int i)
{
    (void)u;
    (void)i;
    return 0;
}
static int
fake_cache_id(
    void* u,
    char const* k,
    char const* n)
{
    (void)u;
    (void)k;
    (void)n;
    return -1;
}
static int
fake_project(
    void* u,
    int a,
    int b,
    int c,
    int* x,
    int* y)
{
    (void)u;
    (void)a;
    (void)b;
    (void)c;
    (void)x;
    (void)y;
    return 0;
}
static int
fake_draw_tile(
    void* u,
    int x,
    int z,
    int l,
    uint32_t c,
    uint32_t f,
    int a)
{
    (void)u;
    (void)x;
    (void)z;
    (void)l;
    (void)c;
    (void)f;
    (void)a;
    return 0;
}
static int
fake_draw_hull(
    void* u,
    int e,
    uint32_t c,
    int a,
    int s)
{
    (void)u;
    (void)e;
    (void)c;
    (void)a;
    (void)s;
    return 0;
}
static int
fake_draw_line(
    void* u,
    int a,
    int b,
    int c,
    int d,
    uint32_t r)
{
    (void)u;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)r;
    return 0;
}
static int
fake_draw_text(
    void* u,
    int x,
    int y,
    char const* t,
    uint32_t r)
{
    (void)u;
    (void)x;
    (void)y;
    (void)t;
    (void)r;
    return 0;
}
static int
fake_draw_rect(
    void* u,
    int x,
    int y,
    int w,
    int h,
    uint32_t c,
    int a)
{
    (void)u;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)c;
    (void)a;
    return 0;
}
static void
fake_draw_select_canvas(
    void* u,
    int c)
{
    (void)u;
    (void)c;
}
static int
fake_mouse_pos(
    void* u,
    int* x,
    int* y)
{
    (void)u;
    (void)x;
    (void)y;
    return 0;
}
static int
fake_minimap_rect(
    void* u,
    int* x,
    int* y,
    int* w,
    int* h)
{
    (void)u;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return 0;
}
static int
fake_anchor_rect(
    void* u,
    int a,
    int* x,
    int* y,
    int* w,
    int* h)
{
    (void)u;
    (void)a;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return 0;
}
static int
fake_stat(
    void* u,
    int s,
    int* c,
    int* b)
{
    (void)u;
    (void)s;
    (void)c;
    (void)b;
    return 0;
}
static int
fake_stat_xp(
    void* u,
    int s,
    int* a,
    int* b,
    int* c)
{
    (void)u;
    (void)s;
    (void)a;
    (void)b;
    (void)c;
    return 0;
}
static char const*
fake_skill_name(
    void* u,
    int s)
{
    (void)u;
    (void)s;
    return NULL;
}
static int
fake_run_energy(void* u)
{
    (void)u;
    return 0;
}
static int
fake_menu_add(
    void* u,
    void* c,
    char const* t,
    int a)
{
    (void)u;
    (void)c;
    (void)t;
    (void)a;
    return 0;
}
static int
fake_if_click(
    void* u,
    int c,
    int o)
{
    (void)u;
    (void)c;
    (void)o;
    return 0;
}
static int
fake_asset_write(
    void* u,
    char const* p,
    char const* n,
    void const* d,
    int s)
{
    (void)u;
    (void)p;
    (void)n;
    (void)d;
    (void)s;
    return 1;
}
static int
fake_screenshot(
    void* u,
    char const* p,
    char const* d,
    char const* n)
{
    (void)u;
    (void)p;
    (void)d;
    (void)n;
    return 1;
}
static int
fake_model_publish(
    void* u,
    int m,
    void const* d,
    int size)
{
    (void)u;
    (void)m;
    (void)d;
    (void)size;
    return 0;
}
static void
fake_model_release(
    void* u,
    int m)
{
    (void)u;
    (void)m;
}
static int
fake_obj_info(
    void* u,
    int id,
    struct ToriRS_PluginObjInfo* o)
{
    (void)u;
    (void)id;
    (void)o;
    return 0;
}
static int
fake_inv_slot(
    void* u,
    int inv,
    int slot,
    int* id,
    int* n)
{
    (void)u;
    (void)inv;
    (void)slot;
    (void)id;
    (void)n;
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
static int
fake_mesh_create(void* u)
{
    (void)u;
    return -1;
}
static void
fake_mesh_destroy(
    void* u,
    int m)
{
    (void)u;
    (void)m;
}
static void
fake_mesh_clear(
    void* u,
    int m)
{
    (void)u;
    (void)m;
}
static int
fake_mesh_vertex(
    void* u,
    int m,
    int x,
    int y,
    int z)
{
    (void)u;
    (void)m;
    (void)x;
    (void)y;
    (void)z;
    return -1;
}
static int
fake_mesh_face(
    void* u,
    int m,
    int a,
    int b,
    int c,
    int h,
    int t)
{
    (void)u;
    (void)m;
    (void)a;
    (void)b;
    (void)c;
    (void)h;
    (void)t;
    return -1;
}
static int
fake_object_create(void* u)
{
    (void)u;
    return -1;
}
static void
fake_object_destroy(
    void* u,
    int o)
{
    (void)u;
    (void)o;
}
static void
fake_object_set_model(
    void* u,
    int o,
    int s,
    int i)
{
    (void)u;
    (void)o;
    (void)s;
    (void)i;
}
static void
fake_object_recolor(
    void* u,
    int o,
    int a,
    int b)
{
    (void)u;
    (void)o;
    (void)a;
    (void)b;
}
static void
fake_object_clear_recolors(
    void* u,
    int o)
{
    (void)u;
    (void)o;
}
static void
fake_object_set_anim(
    void* u,
    int o,
    int s,
    int l)
{
    (void)u;
    (void)o;
    (void)s;
    (void)l;
}
static void
fake_object_set_light(
    void* u,
    int o,
    int a,
    int c)
{
    (void)u;
    (void)o;
    (void)a;
    (void)c;
}
static void
fake_object_set_position(
    void* u,
    int o,
    int x,
    int z,
    int l,
    int h,
    int y)
{
    (void)u;
    (void)o;
    (void)x;
    (void)z;
    (void)l;
    (void)h;
    (void)y;
}
static void
fake_object_set_active(
    void* u,
    int o,
    int a)
{
    (void)u;
    (void)o;
    (void)a;
}
static int
fake_object_ready(
    void* u,
    int o)
{
    (void)u;
    (void)o;
    return 0;
}
static int
fake_hsl_from_rgb(
    void* u,
    uint32_t r)
{
    (void)u;
    (void)r;
    return 0;
}
static uint32_t
fake_hsl_to_rgb(
    void* u,
    int h)
{
    (void)u;
    (void)h;
    return 0;
}

/* ------------------------------------------------------------------ tests */

static int
fake_display_setting(
    void* u,
    int s,
    int* v,
    int* mn,
    int* mx)
{
    (void)u;
    (void)s;
    (void)v;
    (void)mn;
    (void)mx;
    return 0;
}
static int
fake_display_setting_set(
    void* u,
    int s,
    int v)
{
    (void)u;
    (void)s;
    (void)v;
    return 0;
}
static void
fake_layout_set(
    void* u,
    int o,
    int c,
    int w,
    int h)
{
    (void)u;
    (void)o;
    (void)c;
    (void)w;
    (void)h;
}
static void
fake_layout_begin(void* u)
{
    (void)u;
}
static void
fake_layout_end(void* u)
{
    (void)u;
}
static int
fake_layout_slot(
    void* u,
    int s,
    int m,
    int x,
    int y,
    int w,
    int h)
{
    (void)u;
    (void)s;
    (void)m;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return 0;
}
static int
fake_layout_slot_skin(
    void* u,
    int s,
    int a,
    int m)
{
    (void)u;
    (void)s;
    (void)a;
    (void)m;
    return 0;
}
static int
fake_tab_active(void* u)
{
    (void)u;
    return 0;
}
static int
fake_tab_select(
    void* u,
    int t)
{
    (void)u;
    (void)t;
    return 0;
}
static int
fake_asset_read(
    void* u,
    char const* p,
    char const* n)
{
    (void)u;
    (void)p;
    (void)n;
    return 0;
}
static int
fake_image_publish(
    void* u,
    int s,
    void const* d,
    int n,
    int* w,
    int* h)
{
    (void)u;
    (void)s;
    (void)d;
    (void)n;
    (void)w;
    (void)h;
    return 0;
}
static int
fake_image_publish_argb(
    void* u,
    int s,
    int w,
    int h,
    uint32_t const* a)
{
    (void)u;
    (void)s;
    (void)w;
    (void)h;
    (void)a;
    return 0;
}
static int
fake_image_read(
    void* u,
    int s,
    uint32_t* o,
    int m)
{
    (void)u;
    (void)s;
    (void)o;
    (void)m;
    return 0;
}
static void
fake_image_release(
    void* u,
    int s)
{
    (void)u;
    (void)s;
}
static int
fake_draw_image(
    void* u,
    int s,
    int x,
    int y,
    int w,
    int h,
    int cx,
    int cy,
    int cw,
    int ch,
    int t)
{
    (void)u;
    (void)s;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)cx;
    (void)cy;
    (void)cw;
    (void)ch;
    (void)t;
    return 0;
}
static int
fake_hit_region(
    void* u,
    int p,
    int x,
    int y,
    int w,
    int h,
    char const* const* o,
    int c,
    uint32_t t)
{
    (void)u;
    (void)p;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)o;
    (void)c;
    (void)t;
    return 0;
}

static struct ToriRS_PluginEngine
fake_engine(void)
{
    struct ToriRS_PluginEngine e;

    memset(&e, 0, sizeof(e));
    e.world_cycle = fake_world_cycle;
    e.frame_ms = fake_frame_ms;
    e.local_player = fake_local_player;
    e.npc_next = fake_npc_next;
    e.npc_by_slot = fake_npc_by_slot;
    e.player_next = fake_player_next;
    e.obj_next = fake_obj_next;
    e.loc_next = fake_loc_next;
    e.highlight_next = fake_highlight_next;
    e.notify = fake_notify;
    e.key_held = fake_key_held;
    e.hover_tile = fake_hover_tile;
    e.hover_entity = fake_hover_entity;
    e.element_height = fake_element_height;
    e.varbit = fake_varbit;
    e.varp = fake_varp;
    e.cache_id = fake_cache_id;
    e.project = fake_project;
    e.draw_tile = fake_draw_tile;
    e.draw_hull = fake_draw_hull;
    e.draw_line = fake_draw_line;
    e.draw_text = fake_draw_text;
    e.draw_rect = fake_draw_rect;
    e.draw_select_canvas = fake_draw_select_canvas;
    e.mouse_pos = fake_mouse_pos;
    e.minimap_rect = fake_minimap_rect;
    e.anchor_rect = fake_anchor_rect;
    e.layout_set = fake_layout_set;
    e.layout_begin = fake_layout_begin;
    e.layout_end = fake_layout_end;
    e.layout_slot = fake_layout_slot;
    e.layout_slot_skin = fake_layout_slot_skin;
    e.tab_active = fake_tab_active;
    e.tab_select = fake_tab_select;
    e.stat = fake_stat;
    e.stat_xp = fake_stat_xp;
    e.skill_name = fake_skill_name;
    e.run_energy = fake_run_energy;
    e.menu_add = fake_menu_add;
    e.image_publish = fake_image_publish;
    e.image_publish_argb = fake_image_publish_argb;
    e.image_read = fake_image_read;
    e.image_release = fake_image_release;
    e.draw_image = fake_draw_image;
    e.hit_region = fake_hit_region;
    e.if_click = fake_if_click;
    e.asset_read = fake_asset_read;
    e.asset_write = fake_asset_write;
    e.screenshot = fake_screenshot;
    e.model_publish = fake_model_publish;
    e.model_release = fake_model_release;
    e.menu_add = fake_menu_add;
    e.obj_info = fake_obj_info;
    e.inv_slot = fake_inv_slot;
    e.inv_size = fake_inv_size;
    e.screenshot = fake_screenshot;
    e.model_publish = fake_model_publish;
    e.model_release = fake_model_release;
    e.obj_info = fake_obj_info;
    e.inv_slot = fake_inv_slot;
    e.inv_size = fake_inv_size;
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
    e.feature_next = fake_feature_next;
    e.feature_get = fake_feature_get;
    e.feature_set = fake_feature_set;
    e.display_setting = fake_display_setting;
    e.display_setting_set = fake_display_setting_set;
    e.asset_read = fake_asset_read;
    e.asset_write = fake_asset_write;
    return e;
}

/* ------------------------------------------------------------------ tests */

static struct ToriRS_PluginWinWidget const*
widget_named(
    struct ToriRS_PluginHost* host,
    int p,
    char const* id)
{
    for( int i = 0; i < PluginHost_WinWidgetCount(host, p); i++ )
    {
        struct ToriRS_PluginWinWidget const* w = PluginHost_WinWidgetAt(host, p, i);
        if( w && strcmp(w->id, id) == 0 )
            return w;
    }
    return NULL;
}

int
main(void)
{
    struct ToriRS_PluginEngine engine = fake_engine();
    struct ToriRS_PluginHost* host = PluginHost_New(&engine);
    int const p = PluginHost_Register(host, &TORIRS_PLUGIN_FEATURE_FLAGS);

    flags_reset();

    CHECK(p == 0, "the plugin registers");
    CHECK(PluginHost_IsEssential(host, p), "and is essential, so the roster locks its row");
    CHECK(!PluginHost_IsHidden(host, p), "but not hidden: it has a page to reach");
    CHECK(PluginHost_IsEnabled(host, p), "and starts on");

    PluginHost_Start(host);

    /* Nothing stored, so nothing moved: a fresh install is the revision's own
     * client, which is the one thing a settings page must not change. */
    CHECK(
        g_flags[0].value == 25 && g_flags[1].value == 0 && g_flags[2].value == 0x10,
        "a start with no saved overrides leaves every flag at its boot value");

    PluginHost_WinBuild(host, p);
    CHECK(
        PluginHost_WinWidgetCount(host, p) == FLAG_COUNT,
        "the page carries one control per published flag");

    {
        struct ToriRS_PluginWinWidget const* w = widget_named(host, p, "camera_zoom");
        CHECK(w != NULL, "an enum flag gets a control named for its key");
        CHECK(w && w->kind == TORIRS_PLUGIN_W_DROPDOWN, "and it is a dropdown");
        CHECK(
            w && strcmp(w->choices, "Revision default|Adjustable|Fixed") == 0,
            "whose first choice is the default the engine never states");
        CHECK(w && w->selected == 0, "and which opens on it when nothing is stored");
    }
    {
        struct ToriRS_PluginWinWidget const* w = widget_named(host, p, "draw_distance");
        CHECK(w && w->kind == TORIRS_PLUGIN_W_INPUT, "an int flag gets a field");
        CHECK(w && strstr(w->label, "25..90") != NULL, "labelled with the range it takes");
        CHECK(
            w && strcmp(w->text, "Revision default") == 0,
            "and showing the default in words rather than as a bare -1");
    }

    /* A pick reaches the engine and is stored as the WORDS, so the ini reads
     * as what was chosen and survives the choice list growing an entry. */
    PluginHost_WinDispatch(host, p, "camera_zoom", TORIRS_PLUGIN_UI_PICK, 2, "Fixed");
    CHECK(g_flags[1].value == 1, "picking a choice applies it");
    CHECK(
        strcmp(PluginHost_ConfigGet(host, p, "camera_zoom"), "Fixed") == 0,
        "and stores the choice text, not its index");

    /* The values are the flag's own, not the choice index -- 0x20 is 32. */
    PluginHost_WinDispatch(
        host, p, "target_mask_held", TORIRS_PLUGIN_UI_PICK, 2, "0x20 (OldSchool)");
    CHECK(g_flags[2].value == 0x20, "an enum whose values are not 0..n still applies");

    PluginHost_WinDispatch(host, p, "draw_distance", TORIRS_PLUGIN_UI_TEXT, -1, "60");
    CHECK(g_flags[0].value == 60, "a typed number in range applies");

    /* Out of range: refused, said out loud, and PUT BACK -- a field left
     * holding a number the engine did not take is the failure mode. */
    PluginHost_WinDispatch(host, p, "draw_distance", TORIRS_PLUGIN_UI_TEXT, -1, "4000");
    CHECK(g_flags[0].value == 25, "a typed number out of range falls back to the default");
    CHECK(
        strcmp(PluginHost_ConfigGet(host, p, "draw_distance"), "Revision default") == 0,
        "and the store says so rather than keeping the refused value");
    {
        struct ToriRS_PluginWinWidget const* w = widget_named(host, p, "draw_distance");
        CHECK(w && strcmp(w->text, "Revision default") == 0, "as does the field");
    }

    /* Revision default is a RESTORE, not merely a stop: the flag goes back to
     * what the boot resolved, without this plugin having to know the number. */
    PluginHost_WinDispatch(host, p, "camera_zoom", TORIRS_PLUGIN_UI_PICK, 0, "Revision default");
    CHECK(g_flags[1].value == 0, "picking the default puts the flag back");

    /* A saved ini is re-applied at the next start, both halves: the key that
     * carries a choice, and the key that carries the default. */
    {
        struct ToriRS_PluginEngine e2 = fake_engine();
        struct ToriRS_PluginHost* host2 = PluginHost_New(&e2);
        (void)PluginHost_Register(host2, &TORIRS_PLUGIN_FEATURE_FLAGS);

        flags_reset();
        PluginHost_ConfigApply(host2, "feature-flags", "camera_zoom", "Fixed");
        PluginHost_ConfigApply(host2, "feature-flags", "draw_distance", "80");
        PluginHost_ConfigApply(host2, "feature-flags", "target_mask_held", "Revision default");
        PluginHost_Start(host2);

        CHECK(g_flags[1].value == 1, "a saved choice is in force after a start");
        CHECK(g_flags[0].value == 80, "and so is a saved number");
        CHECK(g_flags[2].value == 0x10, "a saved default leaves the flag where it was");
        PluginHost_Free(host2);
    }

    PluginHost_Free(host);
    printf("feature_flags_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
