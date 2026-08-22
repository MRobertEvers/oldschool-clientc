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
#define FAKE_LOCS_MAX 4
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

    /* What the plugins drew this pass, by primitive, so a test can say which
     * row produced it rather than only that something happened. */
    int tiles;
    int hulls;
    int texts;
    uint32_t last_tile_rgb;
    int last_tile_fill_alpha;
    char last_text[64];

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
fake_screenshot(void* u, char const* plugin, char const* dir, char const* name)
{
    (void)u;
    (void)plugin;
    (void)dir;
    (void)name;
    return 1;
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

static struct ToriRS_PluginEngine
fake_engine(void)
{
    struct ToriRS_PluginEngine e;
    memset(&e, 0, sizeof(e));
    e.user = &g_engine;
    e.world_cycle = fake_world_cycle;
    e.frame_ms = fake_frame_ms;
    e.local_player = fake_local_player;
    e.npc_next = fake_npc_next;
    e.npc_by_slot = fake_npc_by_slot;
    e.player_next = fake_player_next;
    e.obj_next = fake_obj_next;
    e.loc_next = fake_loc_next;
    e.key_held = fake_key_held;
    e.hover_tile = fake_hover_tile;
    e.hover_entity = fake_hover_entity;
    e.element_height = fake_element_height;
    e.varbit = fake_varbit;
    e.varp = fake_varp;
    e.project = fake_project;
    e.draw_tile = fake_draw_tile;
    e.draw_hull = fake_draw_hull;
    e.draw_line = fake_draw_line;
    e.draw_text = fake_draw_text;
    e.draw_rect = fake_draw_rect;
    e.menu_add = fake_menu_add;
    e.asset_read = fake_asset_read;
    e.asset_write = fake_asset_write;
    e.screenshot = fake_screenshot;
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

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_TILE_INDICATOR;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_TILE_MARKERS;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_ENTITY_HOVER;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_NPC_HIGHLIGHT;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_NXT_POLL_BOOTHS;

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

/** Build a right-click menu carrying one npc row, the way app.c's bridge does
 *  for a click on an npc. */
static void
menu_with_npc(struct ToriRS_PluginEvMenuBuild* ev, int slot)
{
    memset(ev, 0, sizeof(*ev));
    ev->row_count = 1;
    ev->rows[0].text = "Attack Goblin";
    ev->rows[0].action = 9;
    ev->rows[0].npc_slot = slot;
    ev->rows[0].player_pid = -1;
    ev->rows[0].target_id = -1;
    ev->hover_pass = false;
}

int
main(void)
{
    struct ToriRS_PluginEngine engine = fake_engine();
    struct ToriRS_PluginHost* host = PluginHost_New(&engine);
    int p_tile;
    int p_mark;
    int p_hover;
    int p_npc;
    int p_booth;

    memset(&g_engine, 0, sizeof(g_engine));
    g_engine.hover_ok = 1;
    g_engine.hover_x = 3210;
    g_engine.hover_z = 3220;

    p_tile = PluginHost_Register(host, &TORIRS_PLUGIN_NXT_TILE_INDICATOR);
    p_mark = PluginHost_Register(host, &TORIRS_PLUGIN_NXT_TILE_MARKERS);
    p_hover = PluginHost_Register(host, &TORIRS_PLUGIN_NXT_ENTITY_HOVER);
    p_npc = PluginHost_Register(host, &TORIRS_PLUGIN_NXT_NPC_HIGHLIGHT);
    p_booth = PluginHost_Register(host, &TORIRS_PLUGIN_NXT_POLL_BOOTHS);
    CHECK(
        p_tile >= 0 && p_mark >= 0 && p_hover >= 0 && p_npc >= 0 && p_booth >= 0,
        "all five register");
    PluginHost_Start(host);

    /* ---- the roster must not show any of them --------------------------- */
    {
        CHECK(PluginHost_IsHidden(host, p_tile), "tile indicator is hidden");
        CHECK(PluginHost_IsHidden(host, p_mark), "tile markers are hidden");
        CHECK(PluginHost_IsHidden(host, p_hover), "entity hover is hidden");
        CHECK(PluginHost_IsHidden(host, p_npc), "npc highlight is hidden");
        CHECK(PluginHost_IsHidden(host, p_booth), "poll booths are hidden");
        /* Hidden is not disabled: the feature is always running and the varbit
         * is what decides whether it does anything. A builtin that shipped
         * switched off would need a switch to turn it on, and there is none. */
        CHECK(PluginHost_IsEnabled(host, p_tile), "and still enabled");
        CHECK(PluginHost_IsEnabled(host, p_npc), "and still enabled");
    }

    /* ---- 172 / 175 / 178: nothing is drawn until the varbit says so ----- */
    {
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.tiles == 0, "every setting off draws no tile");

        g_engine.varbit[NXT_VARBIT_HOVER_TILE] = 1;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.tiles == 1, "the hovered tile alone draws one");
        CHECK(
            g_engine.last_tile_rgb == NXT_COL_HOVER_TILE,
            "and in the row's own default colour, not white");
        CHECK(g_engine.last_tile_fill_alpha == 0, "outline only");

        g_engine.varbit[NXT_VARBIT_CURRENT_TILE] = 1;
        g_engine.varbit[NXT_VARBIT_DEST_TILE] = 1;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.tiles == 3, "hovered + current + destination");

        /* A colour the user picked, stored as `colour + 1`. Reading it without
         * the offset gives a colour one unit out, which nothing on a screen
         * can show -- so it is checked here or nowhere. */
        g_engine.varp[NXT_VARP_HOVER_TILE_COLOR] = 0xFF0000 + 1;
        g_engine.varbit[NXT_VARBIT_CURRENT_TILE] = 0;
        g_engine.varbit[NXT_VARBIT_DEST_TILE] = 0;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.last_tile_rgb == 0xFF0000u, "a chosen colour drops the +1 offset");

        g_engine.varbit[NXT_VARBIT_HOVER_TILE] = 0;
        g_engine.varp[NXT_VARP_HOVER_TILE_COLOR] = 0;
    }

    /* ---- 190: the hover highlight ---------------------------------------- */
    {
        g_engine.hover_entity_ok = 1;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.hulls == 0, "no hull while setting 190 is off");

        g_engine.varbit[NXT_VARBIT_HOVER_ENTITY] = 1;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.hulls == 1, "setting 190 on outlines the hovered entity");

        g_engine.hover_entity_ok = 0;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.hulls == 0, "and nothing when the pointer is on open ground");
        g_engine.varbit[NXT_VARBIT_HOVER_ENTITY] = 0;
    }

    /* ---- 112 / 113 / 117: the tile markers ------------------------------- */
    {
        struct ToriRS_PluginEvMenuBuild menu;
        struct ToriRS_PluginMenuRow row;

        memset(&menu, 0, sizeof(menu));
        menu.hover_pass = false;

        /* The row is gated on BOTH the setting and shift. Either one alone
         * putting "Mark tile" on every right-click would be the bug. */
        draw_reset();
        PluginHost_MenuBuild(host, &g_engine, &menu, false);
        CHECK(g_engine.menu_rows == 0, "no Mark tile with the setting off");

        g_engine.varbit[NXT_VARBIT_TILE_MARKERS] = 1;
        draw_reset();
        PluginHost_MenuBuild(host, &g_engine, &menu, false);
        CHECK(g_engine.menu_rows == 0, "no Mark tile without shift");

        g_engine.shift_held = 1;
        draw_reset();
        PluginHost_MenuBuild(host, &g_engine, &menu, false);
        CHECK(g_engine.menu_rows == 1, "shift + the setting offers the row");
        CHECK(strcmp(g_engine.last_menu_text, "Mark tile") == 0, "an unmarked tile says Mark");

        /* Choosing it marks the tile the pointer is on. */
        memset(&row, 0, sizeof(row));
        row.action = g_engine.last_menu_action;
        row.text = g_engine.last_menu_text;
        row.npc_slot = -1;
        row.player_pid = -1;
        row.target_id = -1;
        PluginHost_MenuSelect(host, &row, 0, 0);

        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.tiles == 1, "the marked tile is drawn");
        CHECK(
            g_engine.last_tile_rgb == NXT_COL_TILE_MARKER,
            "in setting 113's default green");

        draw_reset();
        PluginHost_MenuBuild(host, &g_engine, &menu, false);
        CHECK(
            strcmp(g_engine.last_menu_text, "Unmark tile") == 0,
            "and the row now offers to take it away");

        /* Saved, and saved as a list a later run can read back. */
        g_engine.asset_writes = 0;
        PluginHost_FrameStart(host, 2000);
        CHECK(g_engine.asset_writes == 1, "the list is written once, not every frame");
        CHECK(strcmp(g_engine.asset_name, "tiles.txt") == 0, "under its own name");
        CHECK(strncmp(g_engine.asset_bytes, "3210 3220 0", 11) == 0, "holding the tile");
        PluginHost_FrameStart(host, 2020);
        CHECK(g_engine.asset_writes == 1, "and not again while nothing has changed");

        /* 117, the button. It has no varbit at all -- EV_SETTING is the only
         * way this can hear it. */
        PluginHost_Setting(host, NXT_SETTING_CLEAR_TILE_MARKERS, -1);
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.tiles == 0, "\"Clear your highlighted tiles\" clears them");
        PluginHost_FrameStart(host, 2040);
        CHECK(g_engine.asset_size == 0, "and the saved list with them");

        /* A setting id this plugin does not own must not clear anything. */
        PluginHost_MenuBuild(host, &g_engine, &menu, false);
        PluginHost_MenuSelect(host, &row, 0, 0);
        PluginHost_Setting(host, 999, -1);
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.tiles == 1, "another row's button leaves the markers alone");

        PluginHost_Setting(host, NXT_SETTING_CLEAR_TILE_MARKERS, -1);
        g_engine.shift_held = 0;
        g_engine.varbit[NXT_VARBIT_TILE_MARKERS] = 0;
        PluginHost_FrameStart(host, 2060);
    }

    /* ---- 261 and its qualifiers ----------------------------------------- */
    {
        struct ToriRS_PluginEvMenuBuild menu;
        struct ToriRS_PluginMenuRow row;

        g_engine.npc_count = 2;
        g_engine.npcs[0].server_slot = 11;
        g_engine.npcs[0].npc_id = 3029;
        g_engine.npcs[0].base_npc_id = 3029;
        g_engine.npcs[0].size = 1;
        g_engine.npcs[0].element_id = 21;
        g_engine.npcs[0].true_x = 3202;
        g_engine.npcs[0].true_z = 3202;
        snprintf(g_engine.npcs[0].name, sizeof(g_engine.npcs[0].name), "Goblin");
        g_engine.npcs[1].server_slot = 12;
        g_engine.npcs[1].npc_id = 2042;
        g_engine.npcs[1].base_npc_id = 2042;
        g_engine.npcs[1].size = 2;
        g_engine.npcs[1].element_id = 22;
        g_engine.npcs[1].true_x = 3210;
        g_engine.npcs[1].true_z = 3210;
        snprintf(g_engine.npcs[1].name, sizeof(g_engine.npcs[1].name), "Zulrah");

        /* Tagging is its own row (416), separate from the highlight itself. */
        menu_with_npc(&menu, 11);
        g_engine.shift_held = 1;
        draw_reset();
        PluginHost_MenuBuild(host, &g_engine, &menu, false);
        CHECK(g_engine.menu_rows == 0, "no Tag row while setting 416 is off");

        g_engine.varbit[NXT_VARBIT_NPC_TAGGING] = 1;
        menu_with_npc(&menu, 11);
        draw_reset();
        PluginHost_MenuBuild(host, &g_engine, &menu, false);
        CHECK(g_engine.menu_rows == 1, "setting 416 offers it");
        CHECK(strcmp(g_engine.last_menu_text, "Tag Goblin") == 0, "named after the npc");

        memset(&row, 0, sizeof(row));
        row.action = g_engine.last_menu_action;
        row.text = g_engine.last_menu_text;
        row.npc_slot = 11;
        row.player_pid = -1;
        row.target_id = -1;
        PluginHost_MenuSelect(host, &row, 0, 0);

        /* Tagged, but 261 is still off, so nothing is drawn. The two rows are
         * genuinely independent: tagging while the highlight is off is how a
         * list is built before it is switched on. */
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.hulls == 0 && g_engine.tiles == 0, "a tag alone draws nothing");

        g_engine.varbit[NXT_VARBIT_NPC_HIGHLIGHT] = 1;
        g_engine.varbit[NXT_VARBIT_NPC_OUTLINE] = 1;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.hulls == 1, "261 + 260 outline the tagged npc and only it");

        g_engine.varbit[NXT_VARBIT_NPC_TILE] = NXT_TILE_OUTLINE;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.tiles == 1, "259 marks its one tile");
        CHECK(g_engine.last_tile_fill_alpha == 0, "\"outline only\" has no wash");

        g_engine.varbit[NXT_VARBIT_NPC_TILE] = NXT_TILE_OUTLINE_FILL;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.last_tile_fill_alpha > 0, "\"outline and fill\" has one");

        g_engine.varbit[NXT_VARBIT_NPC_NAME] = NXT_NAME_NORMAL;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.texts == 1, "258 names it once");
        CHECK(strcmp(g_engine.last_text, "Goblin") == 0, "with its name");

        g_engine.varbit[NXT_VARBIT_NPC_NAME] = NXT_NAME_BOLD;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.texts == 2, "bold is the same name struck twice");

        /* 264 names everything -- but must not double up on the npc 258 has
         * already named, in a second colour. */
        g_engine.varbit[NXT_VARBIT_NPC_NAMES_ALL] = NXT_NAME_NORMAL;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.texts == 3, "264 adds the untagged npc and no more");

        g_engine.varbit[NXT_VARBIT_NPC_NAME] = NXT_NAME_OFF;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.texts == 2, "with 258 off, 264 names both");

        /* A 2x2 npc owns four tiles, and true_x/true_z is the SW corner. */
        g_engine.varbit[NXT_VARBIT_NPC_NAMES_ALL] = NXT_NAME_OFF;
        menu_with_npc(&menu, 12);
        draw_reset();
        PluginHost_MenuBuild(host, &g_engine, &menu, false);
        row.npc_slot = 12;
        row.action = g_engine.last_menu_action;
        PluginHost_MenuSelect(host, &row, 0, 0);
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.tiles == 1 + 4, "a 2x2 npc is marked over its whole footprint");

        /* 267, the other button. */
        PluginHost_Setting(host, NXT_SETTING_CLEAR_NPC_TAGS, -1);
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(
            g_engine.hulls == 0 && g_engine.tiles == 0,
            "\"Clear your highlighted NPCs\" clears them");
    }

    /* ---- 453: the poll booths ------------------------------------------- */
    {
        g_engine.loc_count = 3;
        g_engine.locs[0].loc_id = 8720;
        g_engine.locs[0].element_id = 31;
        g_engine.locs[0].interactive = 1;
        snprintf(g_engine.locs[0].name, sizeof(g_engine.locs[0].name), "Poll booth");
        /* A different poll booth loc id, same name: this cache holds dozens of
         * them and the whole reason the plugin matches on the name is that no
         * id list stays complete. */
        g_engine.locs[1].loc_id = 7817;
        g_engine.locs[1].element_id = 32;
        g_engine.locs[1].interactive = 1;
        snprintf(g_engine.locs[1].name, sizeof(g_engine.locs[1].name), "Poll booth");
        g_engine.locs[2].loc_id = 1234;
        g_engine.locs[2].element_id = 33;
        g_engine.locs[2].interactive = 1;
        snprintf(g_engine.locs[2].name, sizeof(g_engine.locs[2].name), "Bank booth");

        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.hulls == 0, "no booth is marked while setting 453 is off");

        g_engine.varbit[NXT_VARBIT_POLL_BOOTHS] = 1;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.hulls == 2, "both poll booths, and not the bank booth");

        /* A loc nothing can click must not be marked, however it is named. */
        g_engine.locs[1].interactive = 0;
        draw_reset();
        PluginHost_DrawWorld(host);
        CHECK(g_engine.hulls == 1, "a non-interactive loc is left alone");
        g_engine.varbit[NXT_VARBIT_POLL_BOOTHS] = 0;
        g_engine.loc_count = 0;
    }

    PluginHost_Free(host);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
