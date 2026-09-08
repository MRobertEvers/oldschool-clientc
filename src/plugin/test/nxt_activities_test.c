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

#include <assert.h>
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
#define FAKE_TILE_MARKS 16

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

    struct ToriRS_NpcSnapshot npcs[FAKE_NPCS_MAX];
    int npc_count;

    struct ToriRS_ScenerySnapshot locs[FAKE_LOCS_MAX];
    int loc_count;

    /* What the engine says the CACHE asked to be marked. In the client these
     * come from the HIGHLIGHT_* opcodes; here they are set by hand, because
     * what is under test is the drawing and not the recording (that is
     * `make -C src test-highlight`). */
    struct ToriRS_HighlightItem highlights[FAKE_LOCS_MAX];
    int highlight_count;
    int highlight_walks;

    /* What the plugins drew this pass, by primitive, so a test can say which
     * row produced it rather than only that something happened. */
    int tiles;
    int hulls;
    int texts;
    uint32_t last_tile_rgb;
    uint32_t last_tile_fill_rgb;
    int last_tile_fill_alpha;
    int last_tile_width;
    uint32_t last_tile_flags;
    int last_hull_width;
    int last_hull_alpha;
    uint32_t last_hull_flags;
    int draw_refusal_mode;
    /* WHICH tiles, not just how many: a footprint loop that transposed its
     * two axes draws the right number of the wrong tiles, and every
     * non-square subject in the cache is marked beside itself. */
    int tile_x[FAKE_TILE_MARKS];
    int tile_z[FAKE_TILE_MARKS];
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
 * @see ToriRS_CoreApi::screen. */
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
fake_local_player(void* u, struct ToriRS_PlayerSnapshot* out)
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
fake_npc_next(void* u, int iter, struct ToriRS_NpcSnapshot* out)
{
    (void)u;
    int const next = iter + 1;
    if( next >= g_engine.npc_count )
        return -1;
    *out = g_engine.npcs[next];
    return next;
}
static int
fake_npc_by_slot(void* u, int slot, struct ToriRS_NpcSnapshot* out)
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
fake_player_next(void* u, int iter, struct ToriRS_PlayerSnapshot* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_obj_next(void* u, int iter, struct ToriRS_GroundItemSnapshot* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_loc_next(void* u, int iter, struct ToriRS_ScenerySnapshot* out)
{
    (void)u;
    int const next = iter + 1;
    if( next >= g_engine.loc_count )
        return -1;
    *out = g_engine.locs[next];
    return next;
}
static int
fake_highlight_next(void* u, int iter, struct ToriRS_HighlightItem* out)
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
    return key == TORIRS_KEY_SHIFT && g_engine.shift_held;
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
fake_hover_entity(void* u, struct ToriRS_HoverTarget* out)
{
    (void)u;
    if( !g_engine.hover_entity_ok )
        return 0;
    memset(out, 0, sizeof(*out));
    out->kind = TORIRS_HOVER_NPC;
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
fake_feature_next(void* u, int i, struct ToriRS_FeatureInfo* o)
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
    return TORIRS_FEATURE_UNSET;
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
    (void)level;
    if( g_engine.tiles < FAKE_TILE_MARKS )
    {
        g_engine.tile_x[g_engine.tiles] = tx;
        g_engine.tile_z[g_engine.tiles] = tz;
    }
    g_engine.tiles++;
    g_engine.last_tile_rgb = rgb;
    g_engine.last_tile_fill_rgb = fill_rgb;
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
fake_draw_tile_stroke(void* u, int x, int z, int level, uint32_t rgb,
                      uint32_t fill, int alpha, int width, int item_budget)
{
    CHECK(item_budget > 0, "stroke receives the remaining host draw budget");
    g_engine.last_tile_width = width;
    if( g_engine.draw_refusal_mode )
        return g_engine.draw_refusal_mode == 3 ? 0 : -g_engine.draw_refusal_mode;
    return fake_draw_tile(u, x, z, level, rgb, fill, alpha);
}
static int
fake_draw_hull_stroke(void* u, int element, uint32_t rgb, int alpha, int shape,
                      int width, int item_budget)
{
    CHECK(item_budget > 0, "hull receives the remaining host draw budget");
    g_engine.last_hull_width = width;
    g_engine.last_hull_alpha = alpha;
    if( g_engine.draw_refusal_mode )
        return g_engine.draw_refusal_mode == 3 ? 0 : -g_engine.draw_refusal_mode;
    return fake_draw_hull(u, element, rgb, alpha, shape);
}
static int
fake_draw_tile_styled(void* u,int x,int z,int level,uint32_t rgb,uint32_t fill,
                     int alpha,int width,uint32_t flags,int item_budget)
{
    g_engine.last_tile_flags=flags;
    return fake_draw_tile_stroke(u,x,z,level,rgb,fill,alpha,width,item_budget);
}
static int
fake_draw_hull_styled(void* u,int element,uint32_t rgb,int alpha,int shape,
                     int width,uint32_t flags,int item_budget)
{
    g_engine.last_hull_flags=flags;
    return fake_draw_hull_stroke(u,element,rgb,alpha,shape,width,item_budget);
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
fake_obj_info(void* u, int obj_id, struct ToriRS_ItemInfo* out)
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
fake_frame_activate(void* u, int active, int canvas, int fixed_w, int fixed_h)
{
    (void)u;
    (void)active;
    (void)canvas;
    (void)fixed_w;
    (void)fixed_h;
}

static void
fake_frame_provide(void* u, uint64_t owner)
{
    (void)u;
    (void)owner;
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
/** The lane states no size for any surface, so a caller falls back to its own.
 *  @see ToriRS_FrameApi::surface_native_size. */
static int
fake_slot_native_size(void* u, int slot, int* w, int* h)
{
    (void)u;
    (void)slot;
    (void)w;
    (void)h;
    return 0;
}

/* Nothing under test mounts a component tree, so every id answers "not
 * here" -- @see ToriRS_CacheApi::component_rect, where that is an answer. */
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
    e.draw_tile_stroke = fake_draw_tile_stroke;
    e.draw_hull_stroke = fake_draw_hull_stroke;
    e.draw_hull_styled = fake_draw_hull_styled;
    e.draw_tile_styled = fake_draw_tile_styled;
    e.draw_line = fake_draw_line;
    e.draw_text = fake_draw_text;
    e.draw_rect = fake_draw_rect;
    e.mouse_pos = fake_mouse_pos;
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
    e.if_click = fake_if_click;
    e.menu_add = fake_menu_add;
    e.menu_drop = fake_menu_drop;
    e.asset_read = fake_asset_read;
    e.asset_write = fake_asset_write;
    e.screenshot = fake_screenshot;
    e.obj_info = fake_obj_info;
    e.inv_slot = fake_inv_slot;
    e.inv_size = fake_inv_size;
    e.frame_activate = fake_frame_activate;
    e.frame_provide = fake_frame_provide;
    e.display_setting = fake_display_setting;
    e.display_setting_set = fake_display_setting_set;
    e.tab_active = fake_tab_active;
    e.tab_select = fake_tab_select;
    e.tab_enabled = fake_tab_enabled;
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
    memset(g_engine.tile_x, 0, sizeof(g_engine.tile_x));
    memset(g_engine.tile_z, 0, sizeof(g_engine.tile_z));
    g_engine.menu_rows = 0;
    g_engine.last_text[0] = '\0';
    g_engine.last_menu_text[0] = '\0';
}

static enum ToriRS_Result draw_results[6];

static void
draw_result_probe(struct ToriRS_Api* api, void* state, struct ToriRS_Graphics* draw)
{
    (void)api;
    (void)state;
    CHECK(draw->struct_size >= TORIRS_GRAPHICS_STROKE_SIZE, "graphics advertises its optional stroke tail");
    draw_results[0] = draw->world_tile(draw, 3200, 3200, 0, 0x123456, 0x654321, 70);
    draw_results[1] = draw->world_hull(draw, 41, 0x123456, 70, TORIRS_HULL_MESH);
    draw_results[2] = draw->world_tile_stroke(draw, 3200, 3200, 0, 0x123456, 0x654321, 70, 0);
    draw_results[3] = draw->world_hull_stroke(draw, 41, 0x123456, 70, TORIRS_HULL_MESH, 2);
    draw_results[4] = draw->world_tile_styled(draw,3200,3200,0,0x123456,0x654321,70,0,0);
    draw_results[5] = draw->world_hull_styled(draw,41,0x123456,70,TORIRS_HULL_MESH,2,16);
    CHECK(draw->world_hull_styled(draw,41,1,0,TORIRS_HULL_BOUNDS,1,0)==TORIRS_RESULT_UNSUPPORTED,
          "bounds have no mesh depth and refuse a false occlusion promise");
    CHECK(draw->world_tile_stroke(draw, 0, 0, 0, 0, 0, 0, -1) == TORIRS_RESULT_INVALID,
          "negative stroke width is rejected before the engine call");
}

static void
draw_wrong_scope_probe(struct ToriRS_Api* api,void* state,struct ToriRS_Graphics* draw)
{
    (void)api;(void)state;
    CHECK(draw->world_tile_stroke(draw,3200,3200,0,1,1,0,1)==TORIRS_RESULT_INVALID,
          "tile stroke on canvas returns a scope error without reaching the engine assertion");
    CHECK(draw->world_hull_stroke(draw,41,1,0,TORIRS_HULL_MESH,1)==TORIRS_RESULT_INVALID,
          "mesh stroke on canvas returns a scope error");
    CHECK(draw->world_tile_styled(draw,3200,3200,0,1,1,0,1,0)==TORIRS_RESULT_INVALID,
          "tile surface on canvas returns a scope error");
    CHECK(draw->world_hull_styled(draw,41,1,0,TORIRS_HULL_MESH,1,0)==TORIRS_RESULT_INVALID,
          "styled mesh on canvas returns a scope error");
}

static void
test_draw_capacity_results(void)
{
    static struct ToriRS_PluginDef const probe = {
        .struct_size = sizeof(probe), .id = "stroke-result-probe", .title = "Stroke results",
        .version = "1.0", .callbacks = { .struct_size = sizeof(struct ToriRS_PluginCallbacks),
                                        .on_draw_world = draw_result_probe,
                                        .on_draw_canvas = draw_wrong_scope_probe },
    };
    for( int mode = 0; mode <= 3; mode++ )
    {
        struct ToriRS_PluginEngine engine = fake_engine();
        struct ToriRS_PluginHost* host = PluginHost_New(&engine);
        g_engine.draw_refusal_mode = mode;
        CHECK(PluginHost_Register(host, &probe) >= 0, "stroke result probe registers");
        PluginHost_Start(host);
        PluginHost_DrawWorld(host);
        for( int i = 0; i < 6; i++ )
            CHECK(draw_results[i] == (mode == 1 || mode == 2 ? TORIRS_RESULT_BUDGET : TORIRS_RESULT_OK),
                  "old and appended drawing verbs report capacity refusal but accept offscreen no-ops");
        int const old_tiles=g_engine.tiles,old_hulls=g_engine.hulls;
        PluginHost_DrawCanvas(host,765,503);
        CHECK(g_engine.tiles==old_tiles && g_engine.hulls==old_hulls,
              "wrong-context world calls do not reach the renderer");
        PluginHost_Free(host);
    }
    g_engine.draw_refusal_mode = 0;
}

static int minimap_calls,minimap_canvas,minimap_alpha,minimap_width;
static int minimap_x[8],minimap_z[8],minimap_budget;
static void minimap_select(void* u,int canvas) { (void)u;minimap_canvas=canvas; }
static int minimap_tile(void* u,int x,int z,int level,uint32_t outline,uint32_t fill,
    int alpha,int width,int budget)
{
    (void)u;
    if( x<0 ) return 0;
    if( budget<=0 ) return -2;
    CHECK(minimap_canvas==TORIRS_PLUGIN_ENGINE_DRAW_MINIMAP,"minimap primitive uses its own surface");
    CHECK(level==0 && outline==0x00abcdu && fill==0x00abcdu,"minimap retains native level and colors");
    CHECK(budget>0,"minimap receives the remaining plugin budget");
    minimap_budget=budget;minimap_alpha=alpha;minimap_width=width;
    if( minimap_calls<8 ) { minimap_x[minimap_calls]=x;minimap_z[minimap_calls]=z; }
    ++minimap_calls;
    return 1;
}
static void minimap_budget_probe(struct ToriRS_Api* api,void* state,struct ToriRS_Graphics* draw)
{
    (void)api;(void)state;
    for( int i=0;i<TORIRS_PLUGIN_DRAW_BUDGET;++i )
        CHECK(draw->minimap_tile(draw,3200,3210,0,0x00abcd,0x00abcd,50,0)==TORIRS_RESULT_OK,
            "map tiles fit the declared per-plugin budget");
    CHECK(draw->minimap_tile(draw,3200,3210,0,0x00abcd,0x00abcd,50,0)==TORIRS_RESULT_BUDGET,
        "map capacity refusal is reported, not a successful partial draw");
    CHECK(draw->minimap_tile(draw,-1,0,0,0,0,50,0)==TORIRS_RESULT_OK,
        "off-map no-op remains OK after visible draws fill the budget");
    CHECK(draw->minimap_tile(draw,3200,3210,0,0,0,50,256)==TORIRS_RESULT_INVALID,
        "invalid minimap width never reaches the engine");
}
static void minimap_scope_probe(struct ToriRS_Api* api,void* state,struct ToriRS_Graphics* draw)
{
    (void)api;(void)state;
    CHECK(draw->minimap_tile(draw,3200,3210,0,0,0,50,0)==TORIRS_RESULT_INVALID,
        "world callback cannot issue a map command through the wrong scope");
}
static void test_minimap_highlights(void)
{
    memset(&g_engine,0,sizeof(g_engine));
    struct ToriRS_PluginEngine engine=fake_engine();
    engine.draw_minimap_tile=minimap_tile;engine.draw_select_canvas=minimap_select;
    struct ToriRS_PluginHost* host=PluginHost_New(&engine);
    int plugin=PluginHost_Register(host,&TORIRS_PLUGIN_NXT_HIGHLIGHT);
    PluginHost_Start(host);
    g_engine.highlight_count=1;
    struct ToriRS_HighlightItem* item=&g_engine.highlights[0];
    *item=(struct ToriRS_HighlightItem){.kind=TORIRS_HIGHLIGHT_TILE,.element_id=-1,
        .tile_x=3200,.tile_z=3210,.level=0,.size_x=2,.size_z=3,
        .rgb=0x00abcd,.opacity=50,.outline_width=2,.flags=2|8|16|64};
    PluginHost_FrameStart(host,0,0);
    minimap_calls=0;PluginHost_DrawMinimap(host);
    CHECK(minimap_calls==6 && minimap_alpha==50 && minimap_width==2,
        "MINIMAP64 sends the complete2x3footprint through the independent draw pass");
    CHECK(minimap_x[0]==3200 && minimap_z[0]==3210 && minimap_x[5]==3201 && minimap_z[5]==3212,
        "map footprints preserve every absolute tile coordinate");
    CHECK(minimap_budget==TORIRS_PLUGIN_DRAW_BUDGET-5,"each accepted map tile charges once");
    CHECK(minimap_canvas==TORIRS_PLUGIN_ENGINE_DRAW_WORLD,"minimap scope restores normal overlay surface");
    item->flags=8|64;item->outline_width=0;
    PluginHost_FrameStart(host,20,0);minimap_calls=0;PluginHost_DrawMinimap(host);
    CHECK(minimap_calls==6 && minimap_width==0,"fill-only minimap marks request no outline");
    item->flags=2|8|16;
    PluginHost_FrameStart(host,40,0);minimap_calls=0;PluginHost_DrawMinimap(host);
    CHECK(minimap_calls==0,"identical group without MINIMAP64 draws no map marks");
    item->flags=2|8|16|64;
    PluginHost_SetEnabled(host,plugin,false);PluginHost_FrameStart(host,60,0);PluginHost_DrawMinimap(host);
    CHECK(minimap_calls==0,"disabled highlight plugin contributes no minimap marks");
    struct ToriRS_PluginDef probe={.struct_size=sizeof(probe),.id="minimap-budget",.title="Minimap budget",.version="3",
        .callbacks={.struct_size=sizeof(struct ToriRS_PluginCallbacks),.on_draw_minimap=minimap_budget_probe,
            .on_draw_world=minimap_scope_probe}};
    CHECK(PluginHost_Register(host,&probe)>=0,"minimap budget probe registers");
    PluginHost_Start(host);PluginHost_FrameStart(host,80,0);
    PluginHost_DrawWorld(host);CHECK(minimap_calls==0,"wrong-scope map request makes no engine call");
    PluginHost_DrawMinimap(host);CHECK(minimap_calls==TORIRS_PLUGIN_DRAW_BUDGET,"budget accepts exactly512complete map records");
    PluginHost_Free(host);
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
        g_engine.highlights[0].kind = TORIRS_HIGHLIGHT_LOC;
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
        CHECK(g_engine.last_hull_width == 1, "a one-pixel group keeps its exact width");
        CHECK(g_engine.last_hull_flags==0,"ordinary mesh highlights respect scene visibility");
        g_engine.highlights[0].flags|=TORIRS_WORLD_DRAW_ALWAYS_ON_TOP;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.last_hull_flags==TORIRS_WORLD_DRAW_ALWAYS_ON_TOP,
              "cache always-on-top reaches the renderer without losing the draw bits");
        g_engine.highlights[0].flags&=~TORIRS_WORLD_DRAW_ALWAYS_ON_TOP;
        g_engine.highlights[0].outline_width = 2;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.last_hull_width == 2, "two-pixel model outlines remain distinct from one-pixel outlines");
        g_engine.highlights[0].outline_width = 0;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.last_hull_width == 0 && g_engine.last_hull_alpha == 30,
              "a fill-only model suppresses its border while preserving the wash");

        /* A hovered tile, as clientscript 5198 sets one up:
         * `_7035(5, colour, 0, 70, 10)` -- flags 10 = tile outline + tile
         * fill, and element_id -1, because a tile is a place and not a thing. */
        g_engine.highlights[0].kind = TORIRS_HIGHLIGHT_TILE;
        g_engine.highlights[0].element_id = -1;
        g_engine.highlights[0].rgb = 0xBEBA6E;
        g_engine.highlights[0].opacity = 70;
        g_engine.highlights[0].outline_width = 1;
        g_engine.highlights[0].flags = 2 | 8;

        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.hulls == 0, "a tile item has no model to outline");
        CHECK(g_engine.tiles == 1, "its tile is marked");
        CHECK(g_engine.last_tile_width == 1, "the tile's one-pixel width reaches the engine");
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
        CHECK(g_engine.last_tile_width == 0, "fill-only tiles emit no outline stroke");
        CHECK(
            g_engine.last_tile_fill_alpha == 70,
            "as a wash -- the fill half is what makes it live");
        g_engine.highlights[0].outline_width = 1;

        g_engine.highlights[0].outline_width = 2;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.last_tile_width == 2, "a two-pixel tile width reaches the engine unchanged");
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
        CHECK(
            g_engine.tile_x[0] == 3200 && g_engine.tile_z[0] == 3200,
            "anchored at the SW corner the engine reported");

        /*
         * ...and the footprint is not square.
         *
         * A 2x2 is symmetric: a loop that ran dx over size_z and dz over
         * size_x passes the check above and misplaces every oblong subject in
         * the cache -- a 3x1 fence marked as a 1x3 one, three tiles of the
         * right colour beside the thing they are for. Only a non-square
         * footprint tells the two loops apart, so the tiles themselves are
         * named here and not just counted.
         */
        g_engine.highlights[0].size_x = 3;
        g_engine.highlights[0].size_z = 1;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.tiles == 3, "a 3x1 footprint is three tiles");
        CHECK(
            g_engine.tile_x[2] == 3202 && g_engine.tile_z[2] == 3200,
            "and they run along X, which is the axis size_x names");
        g_engine.highlights[0].size_x = 1;
        g_engine.highlights[0].size_z = 1;

        /* The wash and the border are the same colour here because the group
         * has one, but they are two arguments and the renderer must not send
         * the group's colour as only one of them. */
        g_engine.highlights[0].flags = 2 | 8;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.last_tile_flags==0,"ordinary native tile respects foreground geometry");
        g_engine.highlights[0].flags|=TORIRS_WORLD_DRAW_ALWAYS_ON_TOP;
        draw_reset();PluginHost_DrawWorld(host);
        CHECK(g_engine.last_tile_flags==TORIRS_WORLD_DRAW_ALWAYS_ON_TOP,
              "native tile always-on-top flag reaches the surface renderer");
        g_engine.highlights[0].flags&=~TORIRS_WORLD_DRAW_ALWAYS_ON_TOP;
        CHECK(
            g_engine.last_tile_fill_rgb == 0xBEBA6E &&
                g_engine.last_tile_rgb == 0xBEBA6E,
            "the group's colour reaches both the border and the wash");
        g_engine.highlights[0].flags = 2;

        /*
         * ALWAYS_ON_TOP (16) and MINIMAP (64) are qualifiers on a draw, not a
         * draw. On their own they must produce nothing: a renderer that read
         * "the group has flags" as "the group wants marking" would paint every
         * on-top group the cache declares, whether or not it asked for a
         * shape.
         *
         * Model depth is transported by world_hull_styled. MINIMAP still has
         * no draw verb here. Neither qualifier invents a requested shape.
         */
        g_engine.highlights[0].flags = 16 | 64;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(
            g_engine.tiles == 0 && g_engine.hulls == 0,
            "a qualifier flag with no draw flag draws nothing");
        g_engine.highlights[0].flags = 2;

        /* Every item in the list, not the head of it. A group resolves to as
         * many subjects as the world holds and the renderer walks until the
         * engine stops. */
        g_engine.highlights[1] = g_engine.highlights[0];
        g_engine.highlights[1].kind = TORIRS_HIGHLIGHT_LOC;
        g_engine.highlights[1].element_id = 77;
        g_engine.highlights[1].tile_x = 3300;
        g_engine.highlights[1].flags = 1;
        g_engine.highlight_count = 2;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(
            g_engine.tiles == 1 && g_engine.hulls == 1,
            "a two-item list draws both, each as its own flags name");
        g_engine.highlight_count = 1;

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
        struct ToriRS_GroundItemSnapshot obj;

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

        /* A genuine stack-count increase is another observable drop edge. */
        obj.obj_id = 5073;
        obj.tile_x = 3200;
        obj.count = 1;
        g_engine.notifies = 0;
        PluginHost_ObjSpawn(host, &obj);
        CHECK(g_engine.notifies == 1, "the first nest of a kind is a spawn");

        obj.count = 2;
        PluginHost_ObjCount(host, &obj);
        CHECK(
            g_engine.notifies == 2,
            "and the second, which merged into that stack, is announced too");

        /* Taking one back off the stack is the same callback with the same
         * fields and a smaller number. Only the previous count tells the two
         * apart, which is why the plugin keeps one. */
        obj.count = 1;
        PluginHost_ObjCount(host, &obj);
        CHECK(g_engine.notifies == 2, "a stack getting smaller is not a drop");

        /* However it grows, somebody else's tile is not yours. */
        obj.tile_x = 3210;
        obj.count = 1;
        PluginHost_ObjSpawn(host, &obj);
        obj.count = 2;
        PluginHost_ObjCount(host, &obj);
        CHECK(
            g_engine.notifies == 2,
            "a stack growing across the clearing is still not yours");

        /* A count change on a stack this plugin never saw arrive -- it was
         * switched on mid-session, or the tile was already loaded. There is no
         * baseline, so there is no edge: an unknown state is not an event, the
         * same rule the cannon builtin states as `last_ammo = -1`. */
        obj.obj_id = 5074; /* bird_nest_ring, never spawned above */
        obj.tile_x = 3200;
        obj.count = 5;
        PluginHost_ObjCount(host, &obj);
        CHECK(
            g_engine.notifies == 2,
            "a count with no remembered baseline announces nothing");
        obj.count = 6;
        PluginHost_ObjCount(host, &obj);
        CHECK(
            g_engine.notifies == 3,
            "and the baseline it took makes the next growth an event");

        /* The nest that lands after you have picked the last one up is a new
         * one, not a stack coming back. */
        obj.obj_id = 5073;
        obj.count = 1;
        PluginHost_ObjDespawn(host, &obj);
        PluginHost_ObjSpawn(host, &obj);
        CHECK(g_engine.notifies == 4, "a nest landing on a cleared tile is announced");

        /* Switched off, the count change is as silent as the spawn. */
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_BIRD_NEST)] = 1;
        obj.count = 2;
        PluginHost_ObjCount(host, &obj);
        CHECK(g_engine.notifies == 4, "varbit 1 silences the second nest too");

        /* Original live repro: first nest arrives while the plugin is off;
         * after restart, a second same-id OBJ_ADD still carries count1. An
         * arrival is not inferred from a quantity difference or old baseline. */
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_BIRD_NEST)] = 0;
        g_engine.notifies = 0;
        obj.obj_id = 5070;
        obj.count = 1;
        PluginHost_SetEnabled(host, p_nest, false);
        PluginHost_ObjSpawn(host, &obj);
        CHECK(g_engine.notifies == 0, "a nest received while disabled stays silent");
        PluginHost_SetEnabled(host, p_nest, true);
        PluginHost_ObjSpawn(host, &obj);
        CHECK(g_engine.notifies == 1, "same-count OBJ_ADD after restart is a new nest arrival");
        PluginHost_ObjSpawn(host, &obj);
        CHECK(g_engine.notifies == 2, "another same-count OBJ_ADD is independently announced");
        PluginHost_ObjCount(host, &obj);
        CHECK(g_engine.notifies == 2, "an unchanged OBJ_COUNT is not an arrival");
        obj.count = 0;
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

        /* A new cannon coordinate can arrive between two plugin ticks,
         * without an observed zero in between. Its starting load is not a
         * threshold crossing from the previous cannon. */
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_COORD)] = 0x0C800C81;
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 5;
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 0, "replacement cannon does not inherit the previous ammo edge");
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 30;
        PluginHost_ServerTick(host, ++tick);

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

        /* Enabling with an already empty cannon cannot replay a decrease
         * observed while the owner was disabled. */
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 30;
        PluginHost_ServerTick(host, ++tick);
        PluginHost_SetEnabled(host, p_cannon, false);
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 0;
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_NO_AMMO_NOTIFY)] = 1;
        PluginHost_ServerTick(host, ++tick);
        PluginHost_SetEnabled(host, p_cannon, true);
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 4, "reenable does not replay disabled cannon ammo changes");

        /* Native pickup publishes null (-1), while initial/absent adapters
         * read zero. Neither sentinel represents an owned cannon. */
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_NO_AMMO_NOTIFY)] = 1;
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_COORD)] = -1;
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 30;
        PluginHost_ServerTick(host, ++tick);
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 0;
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 4, "native null coordinate has no ammo events");

        /*
         * The drop that empties the cannon in one tick, with 250 unticked.
         *
         * 15 -> 0 with the amount at 10 is a textbook crossing of setting
         * 248's line: the previous count was above it and the new one is at or
         * below. "Out of ammo wins over low on ammo" is a rule about two lines
         * arriving together -- with the out-of-ammo row off there is no second
         * line, and swallowing the low one leaves the user who ticked 248 and
         * set an amount with no warning at all for the worst drop there is.
         */
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_LOW_NOTIFY)] = 1;
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_LOW_AMOUNT)] = 10;
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_NO_AMMO_NOTIFY)] = 0;
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_COORD)] = 0x0C800C82;
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 15;
        PluginHost_ServerTick(host, ++tick);
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 0;
        PluginHost_ServerTick(host, ++tick);
        CHECK(
            g_engine.notifies == 5,
            "15 -> 0 with 250 off still warns -- it crossed 248's line");
        CHECK(
            strstr(g_engine.last_notify, "low") != NULL,
            "and it is the low-on-ammo line, the row that is switched on");

        /* With 250 ticked the very same drop is ONE line and it is the empty
         * one: two lines for one event is what the precedence rule is for. */
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_NO_AMMO_NOTIFY)] = 1;
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 15;
        PluginHost_ServerTick(host, ++tick);
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 0;
        PluginHost_ServerTick(host, ++tick);
        CHECK(
            g_engine.notifies == 6,
            "with both rows on the same drop is one line, not two");
        CHECK(
            strstr(g_engine.last_notify, "run out") != NULL,
            "and out of ammo is the one that speaks");

        /* Both rows off is still silence: the low line is not a fallback for
         * a row the user unticked. */
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_LOW_NOTIFY)] = 0;
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_NO_AMMO_NOTIFY)] = 0;
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 15;
        PluginHost_ServerTick(host, ++tick);
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 0;
        PluginHost_ServerTick(host, ++tick);
        CHECK(g_engine.notifies == 6, "both rows off says nothing at all");

        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_NO_AMMO_NOTIFY)] = 1;
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_LOW_AMOUNT)] = 0;
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_COORD)] = 0;
        g_engine.varp[fake_id("varp", NXT_VARP_CANNON_AMMO)] = 0;
        g_engine.varbit[fake_id("varbit", NXT_VARBIT_CANNON_LOW_NOTIFY)] = 0;
    }

    PluginHost_Free(host);
    test_draw_capacity_results();
    test_minimap_highlights();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
