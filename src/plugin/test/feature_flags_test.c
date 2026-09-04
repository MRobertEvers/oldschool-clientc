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

extern struct ToriRS_PluginDefV2 const TORIRS_FEATURE_FLAGS;

/* ------------------------------------------------------------ fake engine */

struct FakeFlag
{
    char const* key;
    char const* label;
    char const* section;
    int kind;
    int min;
    int max;
    char const* choices;
    int values[4];
    int value_count;
    /** What this "boot" resolved, i.e. what the UNSET sentinel restores. */
    int boot;
    int value;
};

/*
 * One flag of each shape: a NUMBER whose named values are suggestions, an
 * ENUM whose values are 0..n, and an ENUM whose values are not (0x10/0x20 is
 * the case that catches a panel writing a choice INDEX where a bit belongs).
 * Two sections between them, because the headings are part of what the page
 * has to get right.
 */
static struct FakeFlag g_flags[] = {
    { "draw_distance",
     "Draw distance", "Scene",
     TORIRS_FEATURE_INT,  25,
     90, "25 tiles|40 tiles|60 tiles|90 tiles",
     { 25, 40, 60, 90 },
     4, 25,
     25   },
    { "camera_zoom",
     "Zoom",          "Camera",
     TORIRS_FEATURE_ENUM, 0,
     0,  "Adjustable|Fixed",
     { 0, 1 },
     2, 0,
     0    },
    { "target_mask_held",
     "Held bit",      "",
     TORIRS_FEATURE_ENUM, 0,
     0,  "0x10 (2004)|0x20 (OldSchool)",
     { 0x10, 0x20 },
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
    struct ToriRS_FeatureInfo* o)
{
    (void)u;

    int const at = i < 0 ? 0 : i + 1;
    if( at >= FLAG_COUNT )
        return -1;

    struct FakeFlag const* f = &g_flags[at];
    memset(o, 0, sizeof(*o));
    snprintf(o->key, sizeof(o->key), "%s", f->key);
    snprintf(o->label, sizeof(o->label), "%s", f->label);
    snprintf(o->section, sizeof(o->section), "%s", f->section);
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
    return f ? f->value : TORIRS_FEATURE_UNSET;
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
    if( v == TORIRS_FEATURE_UNSET )
    {
        f->value = f->boot;
        return 1;
    }
    if( f->kind == TORIRS_FEATURE_ENUM )
    {
        /* As the engine does: an enum is its list, an INT is its range. */
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

/* In game: these harnesses exercise behaviour that is gated on it.
 * @see ToriRS_CoreApiV2::screen. */
static int
fake_plugin_screen(void* u)
{
    (void)u;
    return TORIRS_SCREEN_GAME;
}

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
static uint64_t
fake_frame_work_us(void* u)
{
    (void)u;
    return 0;
}
static int
fake_local_player(
    void* u,
    struct ToriRS_PlayerSnapshot* o)
{
    (void)u;
    (void)o;
    return 0;
}
static int
fake_npc_next(
    void* u,
    int i,
    struct ToriRS_NpcSnapshot* o)
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
    struct ToriRS_NpcSnapshot* o)
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
    struct ToriRS_PlayerSnapshot* o)
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
    struct ToriRS_GroundItemSnapshot* o)
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
    struct ToriRS_ScenerySnapshot* o)
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
    struct ToriRS_HighlightItem* o)
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
    struct ToriRS_HoverTarget* o)
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
/* Regions, by role. `w` of 0 means "this gameframe has no such region", which
 * is how the fallback chain in slot_rect's contract gets exercised. */
static int g_slot_x[TORIRS_HOST_SURFACE_COUNT];
static int g_slot_y[TORIRS_HOST_SURFACE_COUNT];
static int g_slot_w[TORIRS_HOST_SURFACE_COUNT];
static int g_slot_h[TORIRS_HOST_SURFACE_COUNT];

static int
fake_slot_rect(void* u, int slot, int* x, int* y, int* w, int* h)
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

/* No frame under test declares MEMBERS of a role, so the honest answer is
 * "this gameframe has no such member" -- @see
 * the host's surface-member query, where that is an answer and not a
 * fault. */
/** The lane states no size for any surface, so a caller falls back to its own.
 *  @see ToriRS_FrameApiV2::surface_native_size. */
static int
fake_slot_native_size(void* u, int slot, int* w, int* h)
{
    (void)u;
    (void)slot;
    (void)w;
    (void)h;
    return 0;
}

static int
fake_slot_member_rect(void* u, int slot, int member, int* x, int* y, int* w, int* h)
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

/* Nothing under test mounts a component tree, so every id answers "not
 * here" -- @see ToriRS_CacheApiV2::component_rect, where that is an answer. */
static int
fake_component_rect(void* u, int component_id, int* x, int* y, int* w, int* h)
{
    (void)u;
    (void)component_id;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return 0;
}

/* No role table under test either: every name answers "this revision does not
 * have that", which is the contract's own reading of an unbound role. */
static int
fake_role_rect(void* u, char const* role, int* x, int* y, int* w, int* h)
{
    (void)u;
    (void)role;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return 0;
}

static int
fake_role_visible(void* u, char const* role)
{
    (void)u;
    (void)role;
    return 0;
}

static int
fake_role_click(void* u, char const* role, int op)
{
    (void)u;
    (void)role;
    (void)op;
    return 0;
}

static int
fake_role_id(void* u, char const* role)
{
    (void)u;
    (void)role;
    return -1;
}

/* No role in these fakes binds to a frame slot: the tests that care about
 * chrome parts drive them through the slot verbs directly. */
static int
fake_role_slot(void* user, char const* role, int* out_slot, int* out_member)
{
    (void)user;
    (void)role;
    (void)out_slot;
    (void)out_member;
    return 0;
}


static int
fake_role_suppress_facets(void* u, char const* role, int paint, int input, int subtree)
{
    (void)u; (void)role; (void)paint; (void)input; (void)subtree;
    return 1;
}

static int
fake_ui_boundary(void* u, char const* role, int place)
{
    (void)place; (void)u;
    return role ? 0 : 1;
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
fake_menu_drop(void* u, void* cursor, int index)
{
    (void)u;
    (void)cursor;
    (void)index;
    return 1;
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
    char const* n,
    char* out_path,
    int out_path_size)
{
    (void)u;
    (void)p;
    (void)d;
    snprintf(out_path, (size_t)out_path_size, "%s", n);
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
    struct ToriRS_ItemInfo* o)
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

/* -- everything the plugin does not use, answered flatly -- */

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
fake_frame_activate(
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
fake_tab_enabled(
    void* u,
    int t)
{
    (void)u;
    (void)t;
    return 1;
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

/* The icon cache's engine end. A fake objtype has no inventory model, so the
 * honest answer here is the same one a real client gives before one is
 * resident: not yet. Tests that want an icon override this. */
static int
fake_obj_image(void* u, int slot, int obj_id, int count, int style, int* out_w, int* out_h)
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
fake_loot_source_next(void* u, int iter, struct ToriRS_LootSource* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_loot_row_next(
    void* u, int source_id, int iter, struct ToriRS_LootRow* out)
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
    e.screen = fake_plugin_screen;
    e.world_cycle = fake_world_cycle;
    e.frame_ms = fake_frame_ms;
    e.frame_work_us = fake_frame_work_us;
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
    e.frame_activate = fake_frame_activate;
    e.layout_begin = fake_layout_begin;
    e.layout_end = fake_layout_end;
    e.layout_slot = fake_layout_slot;
    e.layout_slot_skin = fake_layout_slot_skin;
    e.layout_slot_overlay = fake_layout_slot_overlay;
    e.layout_scrollbar = fake_layout_scrollbar;
    e.tab_active = fake_tab_active;
    e.tab_select = fake_tab_select;
    e.tab_enabled = fake_tab_enabled;
    e.slot_rect = fake_slot_rect;
    e.slot_member_rect = fake_slot_member_rect;
    e.slot_native_size = fake_slot_native_size;
    e.component_rect = fake_component_rect;
    e.role_rect = fake_role_rect;
    e.role_visible = fake_role_visible;
    e.role_click = fake_role_click;
    e.role_suppress_facets = fake_role_suppress_facets;
    e.ui_boundary = fake_ui_boundary;
    e.stat = fake_stat;
    e.stat_xp = fake_stat_xp;
    e.skill_name = fake_skill_name;
    e.run_energy = fake_run_energy;
    e.menu_add = fake_menu_add;
    e.menu_drop = fake_menu_drop;
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

static struct ToriRS_PanelWidget const*
widget_named(
    struct ToriRS_PluginHost* host,
    int p,
    char const* id)
{
    uint32_t const generation = PluginHost_PanelSelectionGeneration(host);
    (void)p;
    for( int i = 0; i < PluginHost_PanelWidgetCount(host, generation); i++ )
    {
        struct ToriRS_PanelWidget const* w =
            PluginHost_PanelWidgetAt(host, generation, i);
        if( w && strcmp(w->id, id) == 0 )
            return w;
    }
    return NULL;
}

/** Index of a control's choice whose text is `text`, or -1. */
static int
choice_index(
    struct ToriRS_PanelWidget const* w,
    char const* text)
{
    if( w->structured_select )
    {
        for( int i = 0; i < w->select_option_count; i++ )
            if( strcmp(w->select_options[i].label, text) == 0 ||
                strcmp(w->select_options[i].value, text) == 0 )
                return i;
        return -1;
    }
    char const* at = w->choices;
    int i = 0;

    for( ;; i++ )
    {
        char const* end = strchr(at, '|');
        size_t const len = end ? (size_t)(end - at) : strlen(at);
        if( strlen(text) == len && strncmp(at, text, len) == 0 )
            return i;
        if( !end )
            return -1;
        at = end + 1;
    }
}

/** Do the same thing the panel does when a dropdown row is used. */
static void
pick(
    struct ToriRS_PluginHost* host,
    int p,
    char const* id,
    char const* text)
{
    static uint64_t sequence;
    struct ToriRS_PanelWidget const* w = widget_named(host, p, id);
    int const index = w ? choice_index(w, text) : -1;

    CHECK(index >= 0, text);
    if( index >= 0 )
    {
        uint32_t const generation = PluginHost_PanelSelectionGeneration(host);
        char const* value = w->structured_select
                                ? w->select_options[index].value
                                : text;
        (void)PluginHost_PanelDispatch(
            host, generation, w->serial, ++sequence, id,
            TORIRS_PANEL_ACTION_PICK, index, value, 0, 0);
        (void)PluginHost_PanelEnsureBuilt(host, generation);
    }
}

int
main(void)
{
    struct ToriRS_PluginEngine engine = fake_engine();
    struct ToriRS_PluginHost* host = PluginHost_New(&engine);
    int const p = PluginHost_RegisterV2(host, &TORIRS_FEATURE_FLAGS);

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

    CHECK(PluginHost_PanelSelect(host, p), "the feature page selects");
    CHECK(
        PluginHost_PanelLayout(
            host, PluginHost_PanelSelectionGeneration(host),
            320, 480, 1000, TORIRS_PANEL_SIZE_MEDIUM, true, true),
        "the selected page receives a live layout");

    /*
     * NOT ONE TEXT FIELD.
     *
     * The row this page is built out of is a list of named values, and that is
     * the point rather than a detail: a number typed into a box is a value
     * nobody has checked, in a caption that did not fit beside it. A control
     * of any other kind here is the regression.
     */
    for( int i = 0;
         i < PluginHost_PanelWidgetCount(
                 host, PluginHost_PanelSelectionGeneration(host));
         i++ )
    {
        struct ToriRS_PanelWidget const* w = PluginHost_PanelWidgetAt(
            host, PluginHost_PanelSelectionGeneration(host), i);
        CHECK(w && w->kind != TORIRS_PANEL_WIDGET_INPUT, "no row on the page is a text field");
    }

    /* Two sections and one row per flag. */
    CHECK(
        PluginHost_PanelWidgetCount(
            host, PluginHost_PanelSelectionGeneration(host)) == FLAG_COUNT + 2,
        "the page carries a row per flag and a heading per section");
    {
        struct ToriRS_PanelWidget const* w = PluginHost_PanelWidgetAt(
            host, PluginHost_PanelSelectionGeneration(host), 0);
        CHECK(
            w && w->kind == TORIRS_PANEL_WIDGET_SECTION && strcmp(w->label, "Scene") == 0,
            "the first section's heading comes before its rows");
        CHECK(
            widget_named(host, p, "_v2_heading_1") != NULL,
            "and the second section gets its own heading");
    }

    {
        struct ToriRS_PanelWidget const* w = widget_named(host, p, "draw_distance");
        CHECK(w && w->kind == TORIRS_PANEL_WIDGET_DROPDOWN, "a number flag is a dropdown");
        CHECK(w && strcmp(w->label, "Draw distance") == 0, "captioned short enough to fit");
        /*
         * The default entry NAMES the value in force. A page of untouched rows
         * that all say "Revision default" tells a reader nothing about what
         * the client is actually doing, which is the one question they opened
         * it to answer.
         */
        CHECK(
            w && w->structured_select && w->select_option_count == 5 &&
                strcmp(w->select_options[0].label,
                       "Revision default (25 tiles)") == 0,
            "and its default entry carries the value in force");
        CHECK(w && w->selected == 0, "opening on it when nothing is stored");
    }
    {
        struct ToriRS_PanelWidget const* w = widget_named(host, p, "camera_zoom");
        CHECK(
            w && w->structured_select && w->select_option_count == 3 &&
                strcmp(w->select_options[0].label,
                       "Revision default (Adjustable)") == 0,
            "an enum's default entry names its value the same way");
    }

    /* A pick reaches the engine. An ENUM stores the choice TEXT, so a settings
     * file survives the list gaining an entry. */
    {
        struct ToriRS_PanelWidget const* camera =
            widget_named(host, p, "camera_zoom");
        struct ToriRS_PanelWidget const* distance =
            widget_named(host, p, "draw_distance");
        uint32_t const camera_serial = camera ? camera->serial : 0;
        uint32_t const distance_serial = distance ? distance->serial : 0;

        pick(host, p, "camera_zoom", "Fixed");
        camera = widget_named(host, p, "camera_zoom");
        distance = widget_named(host, p, "draw_distance");
        CHECK(camera && camera->serial == camera_serial,
            "changing a flag patches its retained row without rebuilding it");
        CHECK(distance && distance->serial == distance_serial,
            "changing a flag does not rebuild an unrelated row");
    }
    CHECK(g_flags[1].value == 1, "picking a choice applies it");
    CHECK(
        strcmp(PluginHost_ConfigGet(host, p, "camera_zoom"), "Fixed") == 0,
        "and an enum stores the choice text, not its index");
    {
        struct ToriRS_PanelWidget const* w = widget_named(host, p, "camera_zoom");
        CHECK(w && w->selected == 2, "the row comes back showing what was chosen");
        CHECK(
            w && strcmp(w->select_options[0].label, "Revision default") == 0,
            "and the default entry drops the value it no longer names");
    }

    /* The values are the flag's own, not the choice index -- 0x20 is 32. */
    pick(host, p, "target_mask_held", "0x20 (OldSchool)");
    CHECK(g_flags[2].value == 0x20, "an enum whose values are not 0..n still applies");

    /* A NUMBER stores the number, because its named values are suggestions
     * rather than its legal set. */
    pick(host, p, "draw_distance", "60 tiles");
    CHECK(g_flags[0].value == 60, "picking a named number applies it");
    CHECK(
        strcmp(PluginHost_ConfigGet(host, p, "draw_distance"), "60") == 0,
        "and a number stores the number, not the words around it");

    /*
     * Revision default is a RESTORE, not merely a stop: the flag goes back to
     * what the boot resolved, without this plugin having to know the number.
     *
     * The entry reads plainly here rather than "(60 tiles)": a row that is
     * OVERRIDING its default does not also name the default, because naming
     * both would be two answers to "what is this set to" on one line.
     */
    {
        struct ToriRS_PanelWidget const* w = widget_named(host, p, "draw_distance");
        CHECK(
            w && strcmp(w->select_options[0].label, "Revision default") == 0,
            "an overridden row's default entry names nothing");
    }
    pick(host, p, "draw_distance", "Revision default");
    CHECK(g_flags[0].value == 25, "picking the default puts the flag back");

    /* A saved ini is re-applied at the next start, both halves: the key that
     * carries a choice, and the key that carries the default. */
    {
        struct ToriRS_PluginEngine e2 = fake_engine();
        struct ToriRS_PluginHost* host2 = PluginHost_New(&e2);
        int const p2 = PluginHost_RegisterV2(host2, &TORIRS_FEATURE_FLAGS);

        flags_reset();
        PluginHost_ConfigApply(host2, "feature-flags", "camera_zoom", "Fixed");
        PluginHost_ConfigApply(host2, "feature-flags", "draw_distance", "80");
        PluginHost_ConfigApply(host2, "feature-flags", "target_mask_held", "Revision default");
        PluginHost_Start(host2);

        CHECK(g_flags[1].value == 1, "a saved choice is in force after a start");
        CHECK(g_flags[0].value == 80, "and so is a saved number");
        CHECK(g_flags[2].value == 0x10, "a saved default leaves the flag where it was");
        PluginHost_Free(host2);
        (void)p2;
    }

    /*
     * A hand-edited number the list does not name is KEPT and shown.
     *
     * Opening a settings page must not quietly rewrite a file somebody edited
     * on purpose, and a dropdown with no entry for the value in force would
     * read as "Revision default" over a client that is not at its default. So
     * the value gets an entry of its own, after the ones the engine named.
     */
    {
        struct ToriRS_PluginEngine e3 = fake_engine();
        struct ToriRS_PluginHost* host3 = PluginHost_New(&e3);
        int const p3 = PluginHost_RegisterV2(host3, &TORIRS_FEATURE_FLAGS);
        struct ToriRS_PanelWidget const* w;

        flags_reset();
        PluginHost_ConfigApply(host3, "feature-flags", "draw_distance", "63");
        PluginHost_Start(host3);
        CHECK(g_flags[0].value == 63, "an ini value the list does not name still applies");

        CHECK(PluginHost_PanelSelect(host3, p3), "the custom-value page selects");
        w = widget_named(host3, p3, "draw_distance");
        CHECK(w && choice_index(w, "63") == 5, "and is appended after the named ones");
        CHECK(w && w->selected == 5, "with the row showing it");
        PluginHost_Free(host3);
    }

    PluginHost_Free(host);
    printf("feature_flags_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
