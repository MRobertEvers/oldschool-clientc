/*
 * The Activities builtins, against a fake engine.
 *
 * Every one of these plugins is invisible: it has no roster row, no config
 * page and no log line, and the only thing that decides whether it does
 * anything is a varbit somebody set in a panel this test cannot open. So a
 * broken one does not fail loudly -- it draws nothing, exactly like a setting
 * that is switched off, which is also what it looked like before any of this
 * existed. That is the failure this file is here to catch.
 *
 * The engine is a vtable, so the whole family runs here with no client: the
 * fake below answers the four questions they ask (what is the varbit, what is
 * under the pointer, what npcs are there, is shift down) and counts what came
 * back out.
 */

#include "plugin/plugins/nxt_activities.h"
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

#define FAKE_VARS_MAX 20000
#define FAKE_NPCS_MAX 4
#define FAKE_LOCS_MAX 8
#define FAKE_ASSET_MAX 4096

struct FakeEngine
{
    int varbit[FAKE_VARS_MAX];
    int varp[FAKE_VARS_MAX];

    int shift_held;
    int hover_ok;
    int hover_x;
    int hover_z;
    int hover_level;
    int hover_entity_ok;

    struct ToriRS_PluginNpcSnap npcs[FAKE_NPCS_MAX];
    int npc_count;

    struct ToriRS_PluginLocSnap locs[FAKE_LOCS_MAX];
    int loc_count;

    /* What the engine says the CACHE asked to be marked. In the client these
     * come from the HIGHLIGHT_* opcodes; here they are set by hand, because
     * what is under test is the drawing and not the recording (that is
     * `make -C src test-highlight`). */
    struct ToriRS_PluginHighlightItem highlights[FAKE_LOCS_MAX];
    int highlight_count;
    int highlight_walks;

    /* What the plugins drew this pass, by primitive, so a test can say which
     * row produced it rather than only that something happened. */
    int tiles;
    int hulls;
    int texts;
    uint32_t last_tile_rgb;
    int last_tile_fill_alpha;
    char last_text[64];

    /* api->notify: what the player was told, and how often. */
    char last_notify[200];
    int notifies;
    int menu_rows;
    char last_menu_text[128];
    int last_menu_action;

    /* The asset store, one file, which is all these plugins use. */
    char asset_name[64];
    char asset_bytes[FAKE_ASSET_MAX];
    int asset_size;
    int asset_writes;
};

static struct FakeEngine g_engine;

/* In game: these harnesses exercise behaviour that is gated on it.
 * @see ToriRS_PluginApi::screen. */
static int
fake_plugin_screen(void* u)
{
    (void)u;
    return TORIRS_PLUGIN_SCREEN_GAME;
}

static int
fake_world_cycle(void* u)
{
    (void)u;
    return 1;
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
    out->level = 0;
    out->element_id = 1;
    /* Walking east: dest differs from true, and the flag is set, which is what
     * the destination marker requires. */
    out->dest_x = 3204;
    out->dest_z = 3200;
    out->flag_x = 3204;
    out->flag_z = 3200;
    return 1;
}
static int
fake_npc_next(void* u, int iter, struct ToriRS_PluginNpcSnap* out)
{
    (void)u;
    int const next = iter + 1;
    if( next >= g_engine.npc_count )
        return -1;
    *out = g_engine.npcs[next];
    return next;
}
static int
fake_npc_by_slot(void* u, int slot, struct ToriRS_PluginNpcSnap* out)
{
    (void)u;
    for( int i = 0; i < g_engine.npc_count; i++ )
        if( g_engine.npcs[i].server_slot == slot )
        {
            *out = g_engine.npcs[i];
            return 1;
        }
    return 0;
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
fake_obj_next(void* u, int iter, struct ToriRS_PluginObjSnap* out)
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
    int const next = iter + 1;
    if( next >= g_engine.loc_count )
        return -1;
    *out = g_engine.locs[next];
    return next;
}
static int
fake_highlight_next(void* u, int iter, struct ToriRS_PluginHighlightItem* out)
{
    (void)u;
    if( iter < 0 )
        g_engine.highlight_walks++;
    int const next = iter + 1;
    if( next >= g_engine.highlight_count )
        return -1;
    *out = g_engine.highlights[next];
    return next;
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
    return key == TORIRS_PLUGIN_KEY_SHIFT && g_engine.shift_held;
}
static int
fake_hover_tile(void* u, int* ox, int* oz, int* olevel)
{
    (void)u;
    if( !g_engine.hover_ok )
        return 0;
    *ox = g_engine.hover_x;
    *oz = g_engine.hover_z;
    *olevel = g_engine.hover_level;
    return 1;
}
static int
fake_hover_entity(void* u, struct ToriRS_PluginHoverEntity* out)
{
    (void)u;
    if( !g_engine.hover_entity_ok )
        return 0;
    memset(out, 0, sizeof(*out));
    out->kind = TORIRS_PLUGIN_HOVER_NPC;
    out->element_id = 7;
    out->tile_x = g_engine.hover_x;
    out->tile_z = g_engine.hover_z;
    return 1;
}
static int
fake_element_height(void* u, int element_id)
{
    (void)u;
    return element_id >= 0 ? 200 : 0;
}
static int
fake_feature_next(void* u, int i, struct ToriRS_PluginFeature* o)
{
    (void)u;
    (void)i;
    (void)o;
    return -1;
}
static int
fake_feature_get(void* u, char const* k)
{
    (void)u;
    (void)k;
    return TORIRS_PLUGIN_FEATURE_UNSET;
}
static int
fake_feature_set(void* u, char const* k, int v)
{
    (void)u;
    (void)k;
    (void)v;
    return 0;
}
static int
fake_varbit(void* u, int id)
{
    (void)u;
    return (id >= 0 && id < FAKE_VARS_MAX) ? g_engine.varbit[id] : 0;
}
static int
fake_varp(void* u, int id)
{
    (void)u;
    return (id >= 0 && id < FAKE_VARS_MAX) ? g_engine.varp[id] : 0;
}
/*
 * The boot profile, as a fixture.
 *
 * The plugins now ask for rows by NAME (api->cache_id), so a test that wants to
 * set one has to answer the same question the profile does. These are
 * revconfig/osrs239's numbers; the point of listing them here is that the test
 * still drives REAL ids -- if it invented its own, it would pass equally well
 * against a plugin that resolved nothing.
 */
static struct
{
    char const* kind;
    char const* name;
    int id;
} const k_fake_cache_ids[] = {
    { "varbit", "bird_nest", 13087 },
    { "varbit", "cannon_low_notify", 14175 },
    { "varbit", "cannon_low_amount", 14176 },
    { "varbit", "cannon_no_ammo_notify", 14177 },
    { "varp", "cannon_ammo", 3 },
    { "varp", "cannon_coord", 3551 },
};

static int
fake_cache_id(void* u, char const* kind, char const* name)
{
    (void)u;
    assert(kind);
    assert(name);
    for( size_t i = 0; i < sizeof(k_fake_cache_ids) / sizeof(k_fake_cache_ids[0]); i++ )
    {
        if( strcmp(k_fake_cache_ids[i].kind, kind) == 0 &&
            strcmp(k_fake_cache_ids[i].name, name) == 0 )
            return k_fake_cache_ids[i].id;
    }
    return -1;
}

/** The id this fixture gives `name`; asserts, because a typo would silently
 *  set a var nothing reads and the test would pass for the wrong reason. */
static int
fake_id(char const* kind, char const* name)
{
    int id = fake_cache_id(NULL, kind, name);
    assert(id >= 0);
    return id;
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
fake_draw_tile(
    void* u,
    int tx,
    int tz,
    int level,
    uint32_t rgb,
    uint32_t fill_rgb,
    int fill_alpha)
{
    (void)u;
    (void)tx;
    (void)tz;
    (void)level;
    (void)fill_rgb;
    g_engine.tiles++;
    g_engine.last_tile_rgb = rgb;
    g_engine.last_tile_fill_alpha = fill_alpha;
    return 1;
}
static int
fake_draw_hull(void* u, int element_id, uint32_t rgb, int fill_alpha, int shape)
{
    (void)u;
    (void)element_id;
    (void)rgb;
    (void)fill_alpha;
    (void)shape;
    g_engine.hulls++;
    return 1;
}
static int
fake_draw_line(void* u, int x0, int y0, int x1, int y1, uint32_t rgb)
{
    (void)u;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)rgb;
    return 1;
}
static int
fake_draw_text(void* u, int x, int y, char const* text, uint32_t rgb)
{
    (void)u;
    (void)x;
    (void)y;
    (void)rgb;
    g_engine.texts++;
    snprintf(g_engine.last_text, sizeof(g_engine.last_text), "%s", text);
    return 1;
}
static int
fake_draw_rect(void* u, int x, int y, int w, int h, uint32_t rgb, int fill_alpha)
{
    (void)u;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)rgb;
    (void)fill_alpha;
    return 1;
}
static int
fake_menu_add(void* u, void* cursor, char const* text, int action)
{
    (void)u;
    (void)cursor;
    g_engine.menu_rows++;
    g_engine.last_menu_action = action;
    snprintf(g_engine.last_menu_text, sizeof(g_engine.last_menu_text), "%s", text);
    return 1;
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
fake_asset_read(void* u, char const* plugin, char const* name)
{
    (void)u;
    (void)plugin;
    (void)name;
    /* Nothing on disk: the plugins have to survive a first run with no saved
     * list, which is the state every fresh install is in. */
    return 0;
}
static int
fake_asset_write(void* u, char const* plugin, char const* name, void const* data, int size)
{
    (void)u;
    (void)plugin;
    snprintf(g_engine.asset_name, sizeof(g_engine.asset_name), "%s", name);
    if( size > FAKE_ASSET_MAX )
        size = FAKE_ASSET_MAX;
    memcpy(g_engine.asset_bytes, data, (size_t)size);
    g_engine.asset_size = size;
    g_engine.asset_writes++;
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
    (void)u;
    (void)plugin;
    (void)dir;
    snprintf(out_path, (size_t)out_path_size, "%s", name);
    return 1;
}
/*
 * The engine entry points this suite does not exercise.
 *
 * PluginHost_New asserts every one of them, so a seam that grows a callback
 * aborts the suite on its first line until the fake catches up -- which is the
 * point of the assert, and is why these are stubs with honest answers rather
 * than omissions. Each returns the "this frame has none" answer its contract
 * defines.
 */
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
static void
fake_layout_set(void* u, int owned, int canvas, int fixed_w, int fixed_h)
{
    (void)u;
    (void)owned;
    (void)canvas;
    (void)fixed_w;
    (void)fixed_h;
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
fake_role_replace(void* u, int plugin, char const* role, int enabled)
{
    (void)u; (void)plugin; (void)role; (void)enabled;
    return 1;
}

static int
fake_role_anchor(void* u, int plugin, char const* role, int replace, int place)
{
    (void)place; (void)u; (void)plugin; (void)replace;
    return role ? 0 : 1;
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
fake_tab_enabled(void* u, int tabno)
{
    (void)u;
    (void)tabno;
    return 1;
}
static int
fake_model_publish(void* u, int m, void const* d, int size)
{
    (void)u;
    (void)m;
    (void)d;
    (void)size;
    return 0;
}
static void
fake_model_release(void* u, int m)
{
    (void)u;
    (void)m;
}
static int
fake_mesh_create(void* u)
{
    (void)u;
    return -1;
}
static void
fake_mesh_destroy(void* u, int m)
{
    (void)u;
    (void)m;
}
static void
fake_mesh_clear(void* u, int m)
{
    (void)u;
    (void)m;
}
static int
fake_mesh_vertex(void* u, int m, int x, int y, int z)
{
    (void)u;
    (void)m;
    (void)x;
    (void)y;
    (void)z;
    return -1;
}
static int
fake_mesh_face(void* u, int m, int a, int b, int c, int hsl, int alpha)
{
    (void)u;
    (void)m;
    (void)a;
    (void)b;
    (void)c;
    (void)hsl;
    (void)alpha;
    return -1;
}
static int
fake_object_create(void* u)
{
    (void)u;
    return -1;
}
static void
fake_object_destroy(void* u, int o)
{
    (void)u;
    (void)o;
}
static void
fake_object_set_model(void* u, int o, int s, int i)
{
    (void)u;
    (void)o;
    (void)s;
    (void)i;
}
static void
fake_object_recolor(void* u, int o, int a, int b)
{
    (void)u;
    (void)o;
    (void)a;
    (void)b;
}
static void
fake_object_clear_recolors(void* u, int o)
{
    (void)u;
    (void)o;
}
static void
fake_object_set_anim(void* u, int o, int s, int l)
{
    (void)u;
    (void)o;
    (void)s;
    (void)l;
}
static void
fake_object_set_light(void* u, int o, int a, int c)
{
    (void)u;
    (void)o;
    (void)a;
    (void)c;
}
static void
fake_object_set_position(void* u, int o, int x, int z, int l, int h, int y)
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
fake_object_set_active(void* u, int o, int a)
{
    (void)u;
    (void)o;
    (void)a;
}
static int
fake_object_ready(void* u, int o)
{
    (void)u;
    (void)o;
    return 0;
}
static int
fake_hsl_from_rgb(void* u, uint32_t rgb)
{
    (void)u;
    return (int)rgb;
}
static uint32_t
fake_hsl_to_rgb(void* u, int hsl)
{
    (void)u;
    return (uint32_t)hsl;
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

/* No frame under test declares MEMBERS of a role, so the honest answer is
 * "this gameframe has no such member" -- @see
 * ToriRS_PluginApi::slot_member_rect, where that is an answer and not a
 * fault. */
/** The lane states no size for any surface, so a caller falls back to its own.
 *  @see ToriRS_PluginApi::slot_native_size. */
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
 * here" -- @see ToriRS_PluginApi::component_rect, where that is an answer. */
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
fake_stat_xp(void* u, int skill, int* xp, int* level_xp, int* next_xp)
{
    (void)u;
    (void)skill;
    if( xp )
        *xp = 0;
    if( level_xp )
        *level_xp = 0;
    if( next_xp )
        *next_xp = 83;
    return 1;
}
static int
fake_image_publish_argb(void* u, int slot, int w, int h, uint32_t const* argb)
{
    (void)u;
    (void)slot;
    (void)argb;
    return w > 0 && h > 0;
}
static int
fake_image_read(void* u, int slot, uint32_t* out, int max)
{
    (void)u;
    (void)slot;
    (void)out;
    (void)max;
    return 0;
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
static void
fake_image_release(void* u, int slot)
{
    (void)u;
    (void)slot;
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
fake_loot_source_next(void* u, int iter, struct ToriRS_PluginLootSource* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_loot_row_next(
    void* u, int source_id, int iter, struct ToriRS_PluginLootRow* out)
{
    (void)u;
    (void)source_id;
    (void)iter;
    (void)out;
    return -1;
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
    e.feature_next = fake_feature_next;
    e.feature_get = fake_feature_get;
    e.feature_set = fake_feature_set;
    e.varbit = fake_varbit;
    e.varp = fake_varp;
    e.cache_id = fake_cache_id;
    e.project = fake_project;
    e.draw_tile = fake_draw_tile;
    e.draw_hull = fake_draw_hull;
    e.draw_line = fake_draw_line;
    e.draw_text = fake_draw_text;
    e.draw_rect = fake_draw_rect;
    e.mouse_pos = fake_mouse_pos;
    e.slot_rect = fake_slot_rect;
    e.slot_member_rect = fake_slot_member_rect;
    e.slot_native_size = fake_slot_native_size;
    e.component_rect = fake_component_rect;
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
    e.asset_read = fake_asset_read;
    e.asset_write = fake_asset_write;
    e.screenshot = fake_screenshot;
    e.obj_info = fake_obj_info;
    e.inv_slot = fake_inv_slot;
    e.inv_size = fake_inv_size;
    e.layout_set = fake_layout_set;
    e.layout_begin = fake_layout_begin;
    e.layout_end = fake_layout_end;
    e.role_rect = fake_role_rect;
    e.role_visible = fake_role_visible;
    e.role_click = fake_role_click;
    e.role_id = fake_role_id;
    e.role_slot = fake_role_slot;
    e.role_replace = fake_role_replace;
    e.role_anchor = fake_role_anchor;
    e.layout_slot = fake_layout_slot;
    e.layout_slot_skin = fake_layout_slot_skin;
    e.layout_slot_overlay = fake_layout_slot_overlay;
    e.layout_scrollbar = fake_layout_scrollbar;
    e.display_setting = fake_display_setting;
    e.display_setting_set = fake_display_setting_set;
    e.tab_active = fake_tab_active;
    e.tab_select = fake_tab_select;
    e.tab_enabled = fake_tab_enabled;
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

/* ------------------------------------------------------------ the plugins */

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_HIGHLIGHT;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_BIRD_NEST;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_CANNON_AMMO;

static void
draw_reset(void)
{
    g_engine.tiles = 0;
    g_engine.hulls = 0;
    g_engine.texts = 0;
    g_engine.menu_rows = 0;
    g_engine.last_text[0] = '\0';
    g_engine.last_menu_text[0] = '\0';
}

int
main(void)
{
    struct ToriRS_PluginEngine engine = fake_engine();
    struct ToriRS_PluginHost* host = PluginHost_New(&engine);
    int p_hl;
    int p_nest;
    int p_cannon;

    memset(&g_engine, 0, sizeof(g_engine));
    g_engine.hover_ok = 1;
    g_engine.hover_x = 3210;
    g_engine.hover_z = 3220;

    p_hl = PluginHost_Register(host, &TORIRS_PLUGIN_NXT_HIGHLIGHT);
    p_nest = PluginHost_Register(host, &TORIRS_PLUGIN_NXT_BIRD_NEST);
    p_cannon = PluginHost_Register(host, &TORIRS_PLUGIN_NXT_CANNON_AMMO);
    CHECK(
        p_hl >= 0 && p_nest >= 0 && p_cannon >= 0,
        "all three register");
    PluginHost_Start(host);

    /* ---- the roster must not show any of them --------------------------- */
    {
        CHECK(PluginHost_IsHidden(host, p_hl), "the cache-highlight renderer is hidden");
        CHECK(PluginHost_IsHidden(host, p_nest), "the bird nest notice is hidden");
        CHECK(PluginHost_IsHidden(host, p_cannon), "the cannon notices are hidden");
        /* Hidden is not disabled: the feature is always running and the varbit
         * is what decides whether it does anything. A builtin that shipped
         * switched off would need a switch to turn it on, and there is none. */
    }

    /* ---- 453: the poll booths -------------------------------------------
     *
     * Not tested here any more, because they are not drawn here any more.
     *
     * `nxt-poll-booths` matched booths BY NAME, on the reasoning that no id
     * list stays complete across revisions. The cache had the answer all along
     * and it is better than a name match: loc CATEGORY 761 is exactly the
     * thirty-four votable booths and nothing else. The two records it leaves
     * out are `clanwars_tournament_pollbooth_blue` and `pollbooth_green_noop`,
     * which are a prop and a dead booth -- both of which the name match
     * highlighted.
     *
     * What made it unreachable was not the data but the dispatch: clientscript
     * 8320 is bound to that category by client trigger 37, and this client
     * raised no triggers. It does now (game/rs_client_trigger.h), so the row is
     * the cache's.
     *
     * One behaviour changed with it, deliberately. The builtin lit booths
     * unconditionally; clientscript 8319 gates on `%varbit4337`, "there is an
     * active poll", which this server never writes -- so the row is now inert
     * here. That is the truthful state. A booth that lights up forever teaches
     * the user to ignore it, and the missing half is a server feature.
     */

    /* ---- the cache's own highlights, drawn as the group described them --
     *
     * Everything below was decided by a clientscript: the colour came from the
     * user's colour row, the flags from the setting's varbit. The renderer's
     * whole job is to turn each flag into the draw call it names, and its
     * whole failure mode is having an opinion of its own.
     */
    {
        /* An Agility obstacle, as clientscript 1854 sets one up:
         * `_7015(11, 65280, 1, 30, 5)` -- flags 5 = model outline + model
         * fill, opacity 30%. */
        g_engine.highlight_count = 1;
        memset(&g_engine.highlights[0], 0, sizeof(g_engine.highlights[0]));
        g_engine.highlights[0].kind = TORIRS_PLUGIN_HL_LOC;
        g_engine.highlights[0].element_id = 41;
        g_engine.highlights[0].tile_x = 3200;
        g_engine.highlights[0].tile_z = 3200;
        g_engine.highlights[0].size_x = 1;
        g_engine.highlights[0].size_z = 1;
        g_engine.highlights[0].rgb = 0x00FF00;
        g_engine.highlights[0].opacity = 30;
        g_engine.highlights[0].outline_width = 1;
        g_engine.highlights[0].flags = 1 | 4;

        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.hulls == 1, "a model-flagged item is outlined");
        CHECK(g_engine.tiles == 0, "and its tile is not marked -- no tile flag");

        /* A hovered tile, as clientscript 5198 sets one up:
         * `_7035(5, colour, 0, 70, 10)` -- flags 10 = tile outline + tile
         * fill, and element_id -1, because a tile is a place and not a thing. */
        g_engine.highlights[0].kind = TORIRS_PLUGIN_HL_TILE;
        g_engine.highlights[0].element_id = -1;
        g_engine.highlights[0].rgb = 0xBEBA6E;
        g_engine.highlights[0].opacity = 70;
        g_engine.highlights[0].outline_width = 1;
        g_engine.highlights[0].flags = 2 | 8;

        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.hulls == 0, "a tile item has no model to outline");
        CHECK(g_engine.tiles == 1, "its tile is marked");
        CHECK(g_engine.last_tile_rgb == 0xBEBA6E, "in the colour the script chose");
        CHECK(
            g_engine.last_tile_fill_alpha == 70,
            "and the opacity is passed straight through -- it is already 0..255");

        /* Thickness 0 with the outline flag set draws no outline: the
         * reference's predicate is `(flags & bit) && thickness != 0`, and this
         * is clientscript 5198's hovered tile exactly. */
        g_engine.highlights[0].outline_width = 0;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.tiles == 1, "a fill with no border still draws its tile");
        CHECK(
            g_engine.last_tile_fill_alpha == 70,
            "as a wash -- the fill half is what makes it live");
        g_engine.highlights[0].outline_width = 1;

        /* Outline without fill: the wash is the fill flag's, not the
         * opacity's. A renderer that keyed the wash off opacity alone would
         * fill every outline-only group in the cache. */
        g_engine.highlights[0].flags = 2;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.last_tile_fill_alpha == 0, "no tile-fill flag means no wash");

        /* ...and an outline flag whose thickness is zero draws nothing at all,
         * so the item stops producing a tile. */
        g_engine.highlights[0].outline_width = 0;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.tiles == 0, "an outline flag with no thickness draws nothing");
        g_engine.highlights[0].outline_width = 1;

        /* A 2x2 subject is marked over its whole footprint. */
        g_engine.highlights[0].size_x = 2;
        g_engine.highlights[0].size_z = 2;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.tiles == 4, "a 2x2 footprint is four tiles, not one");

        /* The walk restarts each frame, which is what makes the engine
         * re-resolve; a renderer that cached the cursor would draw one frame
         * and then nothing. */
        g_engine.highlight_walks = 0;
        draw_reset();
        PluginHost_DrawWorld(host);
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.highlight_walks == 2, "the list is walked from the top each frame");

        g_engine.highlight_count = 0;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.tiles == 0 && g_engine.hulls == 0, "an empty list draws nothing");
    }

    /* ---- 258 / 263 / 264 / 266: the npc name rows ------------------------
     *
     * Not tested here any more, because they are not drawn here any more.
     *
     * There used to be an `nxt-npc-names` builtin faking these four in the
     * hitsplat font, because the cache draws them through the `_7200` family
     * and this client implemented none of it. It does now
     * (game/rs_entity_overlay.h), and the cache's own clientscript 6698 builds
     * a real text component in the row's own colour and the row's own font
     * (495 normal / 496 bold) -- which the faux-bold second pass could only
     * approximate. The builtin is deleted rather than left switched off: two
     * things reading one varbit and both drawing is a doubled name, not a
     * fallback.
     *
     * What replaced these checks: ui/test/uitree_test_scripted_overlay.c pins
     * that an overlay's children reach the screen, are hoisted under the
     * panels and are clipped to the world.
     */

    /* ---- 189: the bird nest notification ---------------------------------
     *
     * INVERTED, like most of the Skills section: the feature is ON at 0.
     */
    {
        struct ToriRS_PluginObjSnap obj;

        memset(&obj, 0, sizeof(obj));
        obj.obj_id = 5073; /* bird_nest_seeds */
        obj.tile_x = 3200;
        obj.tile_z = 3200;
        obj.level = 0;

        g_engine.varbit[fake_id("varbit", NXT_VARBIT_BIRD_NEST)] = 1; /* inverted: 1 is OFF */
        g_engine.notifies = 0;
        PluginHost_ObjSpawn(host, &obj);
        CHECK(g_engine.notifies == 0, "varbit 1 is the OFF state for setting 189");

        g_engine.varbit[fake_id("varbit", NXT_VARBIT_BIRD_NEST)] = 0;
        PluginHost_ObjSpawn(host, &obj);
        CHECK(g_engine.notifies == 1, "a nest under the player is announced");
        CHECK(
            strstr(g_engine.last_notify, "nest") != NULL,
            "and the line says what happened");

        /* Somebody else's nest, across the clearing. A notification for that
         * is noise every time a crowd chops. */
        g_engine.notifies = 0;
        obj.tile_x = 3210;
        PluginHost_ObjSpawn(host, &obj);
        CHECK(g_engine.notifies == 0, "a nest on another tile is not yours");

        /* And an ordinary drop on your own tile is not a nest. */
        obj.tile_x = 3200;
        obj.obj_id = 1511; /* logs */
        PluginHost_ObjSpawn(host, &obj);
        CHECK(g_engine.notifies == 0, "logs under the player are not a nest");

        g_engine.varbit[fake_id("varbit", NXT_VARBIT_BIRD_NEST)] = 1;
    }

    /* ---- 248 / 249 / 250: the cannon ammunition rows ---------------------
     *
     * varp 3 is the count (`rockthrower`) and varp 3551 your cannon's coord
     * (`ownedmcannon_temp`). Everything below is an EDGE -- a count that is
     * already low when you look at it is a state, not an event.
     */
    {
        int tick = 0;
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_LOW_NOTIFY)] = 1;
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_NO_AMMO_NOTIFY)] = 1;
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_LOW_AMOUNT)] = 10;
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_COORD)] = 0; /* no cannon */
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 30;
        g_engine.notifies = 0;

        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 0, "no cannon, nothing to say");

        /* Place one. Its starting load is a state, not a drop. */
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_COORD)] = 0x0C800C80;
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 0, "the first tick with a cannon announces nothing");

        /* Firing down towards the line, but not across it. */
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 12;
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 0, "above the threshold is not low");

        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 9;
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 1, "crossing the threshold says so once");
        CHECK(strstr(g_engine.last_notify, "low") != NULL, "and says what happened");

        /* Still below it, and silent -- a line every tick would bury the
         * chatbox, which is the failure this check exists for. */
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 8;
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 1, "staying below the line is not a second event");

        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 0;
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 2, "empty says so");
        CHECK(strstr(g_engine.last_notify, "run out") != NULL, "as running out");

        /* Reloading is not news, and it re-arms the low notice. */
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 30;
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 2, "loading it says nothing");
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 5;
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 3, "and the threshold arms again");

        /* Threshold 0 is "the user has not chosen an amount": no low notice,
         * but empty still reports. */
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_LOW_AMOUNT)] = 0;
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 30;
        PluginHost_ServerTick(host, ++tick);
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 3;
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 3, "threshold 0 never calls anything low");
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 0;
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 4, "but empty is still empty");

        /* Both rows off. */
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_NO_AMMO_NOTIFY)] = 0;
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 30;
        PluginHost_ServerTick(host, ++tick);
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 0;
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 4, "setting 250 off is silent at empty");

        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_COORD)] = 0;
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 0;
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_LOW_NOTIFY)] = 0;
    }

    PluginHost_Free(host);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
