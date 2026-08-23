/*
 * The item-stats plugin, run against a hand-built api table.
 *
 * No plugin host here, and that is the point: this plugin's whole behaviour is
 * "given a hovered item, an objtype and a set of levels, what does the panel
 * say", and every one of those is an api verb. Answering them directly makes
 * the test a table of cases rather than a client, and keeps it out of the way
 * of the host's own harness.
 *
 * What is asserted is the panel's SHAPE -- how many rows it has, which is what
 * the composed image's height states -- because that is the part an assertion
 * can hold. A shark heals one stat; a super combat potion boosts three; a super
 * restore at full health restores only prayer, and at a drained one restores
 * every skill that is down. Getting the arithmetic wrong changes that count.
 *
 * The other half of the result is whether the picture is legible, which no
 * assertion can state: ITEM_STATS_TEST_PNG names a file to write the last
 * composed panel to.
 */

#include "plugin/torirs_plugin.h"

#include "engine/png_decode.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define TEST_ASSERT(cond, ...)                                                                 \
    do                                                                                         \
    {                                                                                          \
        g_checks++;                                                                            \
        if( !(cond) )                                                                          \
        {                                                                                      \
            g_failures++;                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                        \
            printf(__VA_ARGS__);                                                               \
            printf("\n");                                                                      \
        }                                                                                      \
    } while( 0 )

/* ------------------------------------------------------------- the client */

#define FAKE_SKILLS 25
#define FAKE_WORN_SLOTS 14

static struct
{
    int current[FAKE_SKILLS];
    int base[FAKE_SKILLS];
    int run_energy;
    /** obj id per worn slot, or -1. */
    int worn[FAKE_WORN_SLOTS];
    /** The one hovered item's record. */
    struct ToriRS_PluginObjInfo objs[8];
    int obj_count;

    /* what the plugin did */
    int compose_w;
    int compose_h;
    int compose_count;
    int draw_count;
    uint32_t* compose_px;
} g_client;

static char const* const FAKE_SKILL_NAME[FAKE_SKILLS] = {
    "Attack", "Defence", "Strength", "Hitpoints", "Ranged", "Prayer", "Magic",
    "Cooking", "Woodcutting", "Fletching", "Fishing", "Firemaking", "Crafting",
    "Smithing", "Mining", "Herblore", "Agility", "Thieving", "Slayer", "Farming",
    "Runecraft", "Hunter", "Construction", "Sailing", "Summoning"
};

/* The handler table the plugin builds by subscribing. */
static ToriRS_PluginHandler g_handler[TORIRS_PLUGIN_EV_COUNT];

static void
fake_subscribe(
    struct ToriRS_PluginCtx* ctx,
    enum ToriRS_PluginEvent event,
    ToriRS_PluginHandler handler,
    void* userdata)
{
    (void)ctx;
    (void)userdata;
    assert(event >= 0 && event < TORIRS_PLUGIN_EV_COUNT);
    g_handler[event] = handler;
}

static void
fake_log(struct ToriRS_PluginCtx* ctx, char const* fmt, ...)
{
    (void)ctx;
    (void)fmt;
}

static int
fake_stat(struct ToriRS_PluginCtx* ctx, int skill, int* out_current, int* out_base)
{
    (void)ctx;
    if( skill < 0 || skill >= FAKE_SKILLS )
        return 0;
    if( out_current )
        *out_current = g_client.current[skill];
    if( out_base )
        *out_base = g_client.base[skill];
    return 1;
}

static char const*
fake_skill_name(struct ToriRS_PluginCtx* ctx, int skill)
{
    (void)ctx;
    return (skill >= 0 && skill < FAKE_SKILLS) ? FAKE_SKILL_NAME[skill] : NULL;
}

static int
fake_run_energy(struct ToriRS_PluginCtx* ctx)
{
    (void)ctx;
    return g_client.run_energy;
}

static struct ToriRS_PluginObjInfo const*
fake_obj_find(int obj_id)
{
    for( int i = 0; i < g_client.obj_count; i++ )
        if( g_client.objs[i].obj_id == obj_id )
            return &g_client.objs[i];
    return NULL;
}

static int
fake_obj_info(struct ToriRS_PluginCtx* ctx, int obj_id, struct ToriRS_PluginObjInfo* out)
{
    struct ToriRS_PluginObjInfo const* found = fake_obj_find(obj_id);
    (void)ctx;
    if( !found )
        return 0;
    *out = *found;
    return 1;
}

static int
fake_inv_slot(
    struct ToriRS_PluginCtx* ctx,
    int inv,
    int slot,
    int* out_obj_id,
    int* out_count)
{
    (void)ctx;
    if( inv != TORIRS_PLUGIN_INV_WORN || slot < 0 || slot >= FAKE_WORN_SLOTS )
        return 0;
    if( out_obj_id )
        *out_obj_id = g_client.worn[slot];
    if( out_count )
        *out_count = g_client.worn[slot] >= 0 ? 1 : 0;
    return 1;
}

static int
fake_inv_size(struct ToriRS_PluginCtx* ctx, int inv)
{
    (void)ctx;
    return inv == TORIRS_PLUGIN_INV_WORN ? FAKE_WORN_SLOTS : 0;
}

static int
fake_mouse_pos(struct ToriRS_PluginCtx* ctx, int* out_x, int* out_y)
{
    (void)ctx;
    if( out_x )
        *out_x = 100;
    if( out_y )
        *out_y = 100;
    return 1;
}

static int
fake_cfg_bool(struct ToriRS_PluginCtx* ctx, char const* key)
{
    (void)ctx;
    /* The plugin's own defaults, which is what a fresh install runs with. */
    if( strcmp(key, "show_theoretical") == 0 )
        return 0;
    if( strcmp(key, "always_show_base_stats") == 0 )
        return 0;
    return 1;
}

/* The plugin's own defaults, so the written BMP shows the colour scheme a
 * fresh install draws with rather than a wall of white. */
static uint32_t
fake_cfg_color(struct ToriRS_PluginCtx* ctx, char const* key)
{
    (void)ctx;
    if( strcmp(key, "color_better") == 0 )
        return 0x33EE33u;
    if( strcmp(key, "color_better_some_capped") == 0 )
        return 0x9CEE33u;
    if( strcmp(key, "color_better_capped") == 0 )
        return 0xEEEE33u;
    if( strcmp(key, "color_worse") == 0 )
        return 0xEE3333u;
    if( strcmp(key, "color_header") == 0 )
        return 0xFF981Fu;
    return 0xEEEEEEu;
}

/* -- the atlas, read off disk exactly as the client would hand it over -- */

/*
 * The shipped assets, read off disk by name.
 *
 * By NAME and not one slot, because the plugin ships two of them now -- the
 * glyph metrics and the equipment table for revisions whose cache states no
 * bonuses -- and a fake that answered the second request with the first file's
 * bytes would have the plugin parse a font atlas as a bonus table.
 */
#define FAKE_ASSETS_MAX 4

static struct
{
    char name[32];
    char* bytes;
    int size;
} g_asset[FAKE_ASSETS_MAX];
static int g_asset_count;

static int
fake_asset_find(char const* name)
{
    for( int i = 0; i < g_asset_count; i++ )
        if( strcmp(g_asset[i].name, name) == 0 )
            return i;
    return -1;
}

static int
fake_asset_load(struct ToriRS_PluginCtx* ctx, char const* name)
{
    char path[512];
    FILE* f;
    long size;
    int at;

    (void)ctx;
    if( fake_asset_find(name) >= 0 )
        return 1;
    if( g_asset_count >= FAKE_ASSETS_MAX )
        return 0;
    snprintf(path, sizeof(path), "../script/plugins/assets/item-stats/%s", name);
    f = fopen(path, "rb");
    if( !f )
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    at = g_asset_count++;
    snprintf(g_asset[at].name, sizeof(g_asset[at].name), "%s", name);
    g_asset[at].bytes = malloc((size_t)size);
    assert(g_asset[at].bytes);
    g_asset[at].size = (int)fread(g_asset[at].bytes, 1, (size_t)size, f);
    fclose(f);
    return g_asset[at].size > 0;
}

static void const*
fake_asset_data(struct ToriRS_PluginCtx* ctx, char const* name, int* out_size)
{
    int const at = fake_asset_find(name);

    (void)ctx;
    if( at < 0 )
    {
        if( out_size )
            *out_size = 0;
        return NULL;
    }
    if( out_size )
        *out_size = g_asset[at].size;
    return g_asset[at].bytes;
}

/* Dropping the resident copy is the plugin's own housekeeping once it has
 * parsed a table; the file itself is untouched, so a later load reads it
 * again. */
static void
fake_asset_release(struct ToriRS_PluginCtx* ctx, char const* name)
{
    int const at = fake_asset_find(name);

    (void)ctx;
    if( at < 0 )
        return;
    free(g_asset[at].bytes);
    g_asset[at] = g_asset[--g_asset_count];
}

/*
 * The glyph sheet: the REAL text.png, decoded.
 *
 * The metrics come from text.ini and decide the layout, which is what the row
 * counts below assert; the pixels decide whether the letters are letters, which
 * only the written BMP can show. Decoding the file for real is what makes that
 * picture worth looking at -- and it is a second check on the pairing, since an
 * atlas whose glyph boxes fall outside its own image draws nothing.
 */
static uint32_t* g_atlas_px;
static int g_atlas_w;
static int g_atlas_h;

static int
fake_image_load(struct ToriRS_PluginCtx* ctx, char const* name)
{
    char path[512];
    FILE* f;
    long size;
    void* bytes;

    (void)ctx;
    if( g_atlas_px )
        return 1;
    snprintf(path, sizeof(path), "../script/plugins/assets/item-stats/%s", name);
    f = fopen(path, "rb");
    if( !f )
        return -1;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bytes = malloc((size_t)size);
    assert(bytes);
    size = (long)fread(bytes, 1, (size_t)size, f);
    fclose(f);
    if( !PngDecode_Argb(bytes, (int)size, &g_atlas_w, &g_atlas_h, &g_atlas_px) )
    {
        free(bytes);
        return -1;
    }
    free(bytes);
    return 1;
}

static int
fake_image_size(struct ToriRS_PluginCtx* ctx, int image, int* out_w, int* out_h)
{
    (void)ctx;
    if( image != 1 || !g_atlas_px )
        return 0;
    if( out_w )
        *out_w = g_atlas_w;
    if( out_h )
        *out_h = g_atlas_h;
    return 1;
}

static int
fake_image_pixels(struct ToriRS_PluginCtx* ctx, int image, uint32_t* out, int max)
{
    int const count = g_atlas_w * g_atlas_h;
    (void)ctx;
    if( image != 1 || !g_atlas_px || max < count )
        return 0;
    memcpy(out, g_atlas_px, (size_t)count * sizeof(uint32_t));
    return count;
}

static int
fake_image_compose(
    struct ToriRS_PluginCtx* ctx,
    char const* name,
    int w,
    int h,
    uint32_t const* argb)
{
    (void)ctx;
    (void)name;
    g_client.compose_w = w;
    g_client.compose_h = h;
    g_client.compose_count++;
    free(g_client.compose_px);
    g_client.compose_px = malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    assert(g_client.compose_px);
    memcpy(g_client.compose_px, argb, (size_t)w * (size_t)h * sizeof(uint32_t));
    return 2;
}

static void
fake_image_release(struct ToriRS_PluginCtx* ctx, int image)
{
    (void)ctx;
    (void)image;
}

static void
fake_draw_image(
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    int image,
    int x,
    int y,
    int clip_x,
    int clip_y,
    int clip_w,
    int clip_h,
    int trans)
{
    (void)ctx;
    (void)surface;
    (void)image;
    (void)x;
    (void)y;
    (void)clip_x;
    (void)clip_y;
    (void)clip_w;
    (void)clip_h;
    (void)trans;
    g_client.draw_count++;
}

static struct ToriRS_PluginApi g_api;

static void
api_init(void)
{
    memset(&g_api, 0, sizeof(g_api));
    g_api.abi_version = TORIRS_PLUGIN_ABI;
    g_api.subscribe = fake_subscribe;
    g_api.log = fake_log;
    g_api.stat = fake_stat;
    g_api.skill_name = fake_skill_name;
    g_api.run_energy = fake_run_energy;
    g_api.obj_info = fake_obj_info;
    g_api.inv_slot = fake_inv_slot;
    g_api.inv_size = fake_inv_size;
    g_api.mouse_pos = fake_mouse_pos;
    g_api.cfg_bool = fake_cfg_bool;
    g_api.cfg_color = fake_cfg_color;
    g_api.asset_load = fake_asset_load;
    g_api.asset_data = fake_asset_data;
    g_api.asset_release = fake_asset_release;
    g_api.image_load = fake_image_load;
    g_api.image_size = fake_image_size;
    g_api.image_pixels = fake_image_pixels;
    g_api.image_compose = fake_image_compose;
    g_api.image_release = fake_image_release;
    g_api.draw_image = fake_draw_image;
}

/* ---------------------------------------------------------------- driving */

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_ITEM_STATS;

/* The plugin asserts its context is real and never looks inside it -- the host
 * owns that struct -- so any address will do, and using one keeps the plugin's
 * own contract checks live rather than tiptoeing around them. */
static struct ToriRS_PluginCtx* const CTX = (struct ToriRS_PluginCtx*)&g_client;

/* The plugin's own line box: four pixels of margin top and bottom, and a
 * twelve-pixel line. Restated here rather than shared, so a change to either
 * shows up as a failing count instead of as two constants moving together. */
#define TIP_BORDER 4
#define TIP_PITCH 12

static int
tip_rows(void)
{
    if( g_client.compose_h <= 0 )
        return 0;
    return (g_client.compose_h - TIP_BORDER * 2) / TIP_PITCH;
}

/** One frame: the hover pass names the item, then the canvas draws. */
static void
frame(int hovered_obj_id)
{
    struct ToriRS_PluginEvMenuBuild menu;
    struct ToriRS_PluginEvDrawCanvas draw;
    struct ToriRS_PluginEvFrame frame_ev;

    memset(&frame_ev, 0, sizeof(frame_ev));
    if( g_handler[TORIRS_PLUGIN_EV_FRAME_START] )
        g_handler[TORIRS_PLUGIN_EV_FRAME_START](CTX, &frame_ev, NULL);

    memset(&menu, 0, sizeof(menu));
    menu.hover_pass = true;
    if( hovered_obj_id >= 0 )
    {
        menu.row_count = 1;
        menu.rows[0].text = "Eat";
        menu.rows[0].pick_kind = 2; /* UI_MINIMENU_PICK_INV_SLOT */
        menu.rows[0].npc_slot = -1;
        menu.rows[0].player_pid = -1;
        menu.rows[0].target_id = hovered_obj_id;
        menu.rows[0].component_id = (149 << 16) | 0;
        menu.rows[0].slot = 3;
    }
    if( g_handler[TORIRS_PLUGIN_EV_MENU_BUILD] )
        g_handler[TORIRS_PLUGIN_EV_MENU_BUILD](CTX, &menu, NULL);

    memset(&draw, 0, sizeof(draw));
    draw.surface = (void*)0x1;
    draw.width = 765;
    draw.height = 503;
    g_client.compose_w = 0;
    g_client.compose_h = 0;
    g_client.draw_count = 0;
    if( g_handler[TORIRS_PLUGIN_EV_DRAW_CANVAS] )
        g_handler[TORIRS_PLUGIN_EV_DRAW_CANVAS](CTX, &draw, NULL);
}

/** Register an objtype the fake client can answer for. */
static struct ToriRS_PluginObjInfo*
obj_add(int obj_id, char const* name)
{
    struct ToriRS_PluginObjInfo* out;
    assert(g_client.obj_count < (int)(sizeof(g_client.objs) / sizeof(g_client.objs[0])));
    out = &g_client.objs[g_client.obj_count++];
    memset(out, 0, sizeof(*out));
    out->obj_id = obj_id;
    snprintf(out->name, sizeof(out->name), "%s", name);
    out->cert_link = -1;
    out->wearpos = -1;
    out->wearpos2 = -1;
    out->wearpos3 = -1;
    out->attack_rate = -1;
    return out;
}

static void
client_reset(void)
{
    memset(&g_client, 0, sizeof(g_client));
    for( int i = 0; i < FAKE_SKILLS; i++ )
    {
        g_client.current[i] = 99;
        g_client.base[i] = 99;
    }
    for( int i = 0; i < FAKE_WORN_SLOTS; i++ )
        g_client.worn[i] = -1;
    g_client.run_energy = 100;
    /* Every case below is about a panel appearing, so the plugin is restarted
     * with it: the composed panel is cached against the item it was built
     * from, and a case that reused it would be measuring the previous one. */
    memset(g_handler, 0, sizeof(g_handler));
    TORIRS_PLUGIN_ITEM_STATS.init(CTX, &g_api);
}

/* ------------------------------------------------------------- the cases */

static void
test_no_hover(void)
{
    client_reset();
    obj_add(385, "Shark");
    frame(-1);
    TEST_ASSERT(g_client.draw_count == 0, "nothing hovered draws nothing");
}

static void
test_unknown_item(void)
{
    client_reset();
    obj_add(1512, "Logs");
    frame(1512);
    TEST_ASSERT(
        g_client.draw_count == 0,
        "an item with no effect and no bonuses gets no panel (drew %d)",
        g_client.draw_count);
}

static void
test_food_heals(void)
{
    client_reset();
    obj_add(385, "Shark");
    g_client.current[3] = 50; /* hitpoints, badly down */
    frame(385);
    TEST_ASSERT(g_client.draw_count == 1, "a shark gets a panel");
    TEST_ASSERT(tip_rows() == 1, "one stat changes; got %d rows", tip_rows());
}

static void
test_food_at_full_health(void)
{
    client_reset();
    obj_add(385, "Shark");
    frame(385);
    /* Still one row: the reference prints the change even when it is zero,
     * because "this would be wasted" is the answer the tooltip exists to
     * give. */
    TEST_ASSERT(tip_rows() == 1, "a full-health shark still says so; got %d", tip_rows());
}

static void
test_dose_suffix_is_stripped(void)
{
    client_reset();
    obj_add(2436, "Super attack(4)");
    g_client.current[0] = 99;
    frame(2436);
    TEST_ASSERT(g_client.draw_count == 1, "a dosed potion resolves");
    TEST_ASSERT(tip_rows() == 1, "one stat boosted; got %d rows", tip_rows());
}

static void
test_combo_potion(void)
{
    client_reset();
    obj_add(12695, "Super combat potion(4)");
    frame(12695);
    TEST_ASSERT(tip_rows() == 3, "attack, strength and defence; got %d", tip_rows());
}

static void
test_super_restore_only_lists_what_is_down(void)
{
    int full_rows;
    int drained_rows;

    client_reset();
    obj_add(3024, "Super restore(4)");
    frame(3024);
    full_rows = tip_rows();

    client_reset();
    obj_add(3024, "Super restore(4)");
    g_client.current[0] = 60; /* attack */
    g_client.current[2] = 70; /* strength */
    g_client.current[10] = 1; /* fishing */
    frame(3024);
    drained_rows = tip_rows();

    TEST_ASSERT(full_rows == 1, "at full stats only prayer is listed; got %d", full_rows);
    TEST_ASSERT(
        drained_rows == 4,
        "prayer plus the three drained skills; got %d",
        drained_rows);
}

static void
test_mature_ale_is_its_own_drink(void)
{
    client_reset();
    obj_add(5751, "Dwarven stout(m)");
    frame(5751);
    /* food, mining, smithing, and the three drains. */
    TEST_ASSERT(tip_rows() == 6, "the mature stout's six rows; got %d", tip_rows());
}

static void
test_noted_item_reads_the_base(void)
{
    struct ToriRS_PluginObjInfo* note;

    client_reset();
    obj_add(385, "Shark");
    note = obj_add(386, "Shark");
    note->cert_link = 385;
    g_client.current[3] = 50;
    frame(386);
    TEST_ASSERT(g_client.draw_count == 1, "a noted shark answers as a shark");
    TEST_ASSERT(tip_rows() == 1, "and with the same one row; got %d", tip_rows());
}

static void
test_equipment_against_nothing(void)
{
    struct ToriRS_PluginObjInfo* whip;

    client_reset();
    whip = obj_add(4151, "Abyssal whip");
    whip->wearpos = 3;
    whip->has_bonuses = 1;
    whip->attack_rate = 4;
    whip->bonus[TORIRS_PLUGIN_BONUS_ATTACK_SLASH] = 82;
    whip->bonus[TORIRS_PLUGIN_BONUS_STRENGTH] = 82;
    frame(4151);
    TEST_ASSERT(g_client.draw_count == 1, "a weapon gets a panel");
    /* Melee Str, the Attack bonus heading, and Slash. Speed does not appear:
     * the whip swings at unarmed's rate, so equipping it changes nothing
     * there, which is exactly the row the reference suppresses. */
    TEST_ASSERT(tip_rows() == 3, "str, heading, slash; got %d", tip_rows());
}

static void
test_equipment_against_the_same_item(void)
{
    struct ToriRS_PluginObjInfo* whip;

    client_reset();
    whip = obj_add(4151, "Abyssal whip");
    whip->wearpos = 3;
    whip->has_bonuses = 1;
    whip->attack_rate = 4;
    whip->bonus[TORIRS_PLUGIN_BONUS_ATTACK_SLASH] = 82;
    whip->bonus[TORIRS_PLUGIN_BONUS_STRENGTH] = 82;
    g_client.worn[3] = 4151;
    frame(4151);
    TEST_ASSERT(
        g_client.draw_count == 0,
        "swapping a whip for the same whip changes nothing, so there is no "
        "panel (drew %d)",
        g_client.draw_count);
}

static void
test_two_handed_takes_the_shield_off(void)
{
    struct ToriRS_PluginObjInfo* bow;
    struct ToriRS_PluginObjInfo* shield;
    int with_shield;
    int without_shield;

    client_reset();
    bow = obj_add(11785, "Armadyl crossbow");
    bow->wearpos = 3;
    bow->wearpos2 = 5;
    bow->has_bonuses = 1;
    bow->attack_rate = 6;
    bow->bonus[TORIRS_PLUGIN_BONUS_ATTACK_RANGE] = 100;
    frame(11785);
    without_shield = tip_rows();

    client_reset();
    bow = obj_add(11785, "Armadyl crossbow");
    bow->wearpos = 3;
    bow->wearpos2 = 5;
    bow->has_bonuses = 1;
    bow->attack_rate = 6;
    bow->bonus[TORIRS_PLUGIN_BONUS_ATTACK_RANGE] = 100;
    shield = obj_add(1540, "Anti-dragon shield");
    shield->wearpos = 5;
    shield->has_bonuses = 1;
    shield->bonus[TORIRS_PLUGIN_BONUS_DEFENCE_MAGIC] = 10;
    g_client.worn[5] = 1540;
    frame(11785);
    with_shield = tip_rows();

    TEST_ASSERT(
        with_shield > without_shield,
        "wearing a shield adds the defence it would cost (%d vs %d rows)",
        with_shield,
        without_shield);
}

/*
 * A cache that states no bonuses at all -- every dat1 world.
 *
 * The record has no params and no wearpos, exactly as a 2004 objtype decodes,
 * so everything the panel says has to come from the shipped table. This is the
 * case that made the plugin look broken on a LostCity world: the hover was
 * found, the name was read, and there was nothing to say about a scimitar.
 */
static void
test_dat1_falls_back_to_the_shipped_table(void)
{
    struct ToriRS_PluginObjInfo* scimitar;

    client_reset();
    scimitar = obj_add(1333, "Rune scimitar");
    scimitar->has_bonuses = 0;
    scimitar->wearpos = -1;
    frame(1333);
    TEST_ASSERT(
        g_client.draw_count == 1,
        "a dat1 weapon still gets a panel (drew %d)",
        g_client.draw_count);
    /*
     * Melee Str, the Attack heading with Stab/Slash/Crush, and the Defence
     * heading with the one point of slash defence a scimitar carries. No Speed
     * row: it swings at unarmed's rate, so equipping it changes nothing there,
     * which is exactly the row the reference suppresses.
     */
    TEST_ASSERT(tip_rows() == 7, "str, and both bonus groups; got %d", tip_rows());
}

/*
 * The cache wins where it has an answer.
 *
 * Same item, same name, but the record states its own bonuses -- and they are
 * deliberately not the table's. An OldSchool session must read its own record,
 * or the plugin would be telling a player about a different game's balance.
 */
static void
test_cache_params_beat_the_table(void)
{
    struct ToriRS_PluginObjInfo* scimitar;

    client_reset();
    scimitar = obj_add(1333, "Rune scimitar");
    scimitar->has_bonuses = 1;
    scimitar->wearpos = 3;
    scimitar->attack_rate = 4;
    scimitar->bonus[TORIRS_PLUGIN_BONUS_ATTACK_SLASH] = 1;
    frame(1333);
    TEST_ASSERT(
        tip_rows() == 2,
        "the record's one bonus and its heading, not the table's four; got %d",
        tip_rows());
}

/*
 * The sample the BMP is written from: a saradomin brew drunk at 40/99 with the
 * attacking stats up, which is the busiest consumable panel there is -- a heal,
 * a defence boost, and a drain on each of the four stats it takes from.
 */
static void
test_render_sample(void)
{
    client_reset();
    obj_add(6685, "Saradomin brew(4)");
    g_client.current[3] = 40; /* hitpoints */
    frame(6685);
    TEST_ASSERT(
        tip_rows() == 6,
        "hitpoints, defence and the four drains; got %d",
        tip_rows());
}

static void
write_png_stub(void)
{
    /* The picture, for the half no assertion states. Written as a raw BMP so
     * the test links nothing to produce it. */
    char const* path = getenv("ITEM_STATS_TEST_BMP");
    FILE* f;
    int w = g_client.compose_w;
    int h = g_client.compose_h;
    int row_bytes;
    int size;

    if( !path || !g_client.compose_px || w <= 0 || h <= 0 )
        return;
    f = fopen(path, "wb");
    if( !f )
        return;
    row_bytes = (w * 3 + 3) & ~3;
    size = 54 + row_bytes * h;
    {
        unsigned char header[54];
        memset(header, 0, sizeof(header));
        header[0] = 'B';
        header[1] = 'M';
        header[2] = (unsigned char)(size & 0xFF);
        header[3] = (unsigned char)((size >> 8) & 0xFF);
        header[4] = (unsigned char)((size >> 16) & 0xFF);
        header[5] = (unsigned char)((size >> 24) & 0xFF);
        header[10] = 54;
        header[14] = 40;
        header[18] = (unsigned char)(w & 0xFF);
        header[19] = (unsigned char)((w >> 8) & 0xFF);
        header[22] = (unsigned char)(h & 0xFF);
        header[23] = (unsigned char)((h >> 8) & 0xFF);
        header[26] = 1;
        header[28] = 24;
        fwrite(header, 1, sizeof(header), f);
    }
    for( int y = h - 1; y >= 0; y-- )
    {
        int written = 0;
        for( int x = 0; x < w; x++ )
        {
            uint32_t px = g_client.compose_px[y * w + x];
            unsigned char bgr[3] = {
                (unsigned char)(px & 0xFF),
                (unsigned char)((px >> 8) & 0xFF),
                (unsigned char)((px >> 16) & 0xFF)
            };
            fwrite(bgr, 1, 3, f);
            written += 3;
        }
        for( ; written < row_bytes; written++ )
            fputc(0, f);
    }
    fclose(f);
    printf("wrote %s (%dx%d)\n", path, w, h);
}

int
main(void)
{
    api_init();

    test_no_hover();
    test_unknown_item();
    test_food_heals();
    test_food_at_full_health();
    test_dose_suffix_is_stripped();
    test_combo_potion();
    test_super_restore_only_lists_what_is_down();
    test_mature_ale_is_its_own_drink();
    test_noted_item_reads_the_base();
    test_equipment_against_nothing();
    test_equipment_against_the_same_item();
    test_two_handed_takes_the_shield_off();
    test_dat1_falls_back_to_the_shipped_table();
    test_cache_params_beat_the_table();
    test_render_sample();
    write_png_stub();

    printf("item_stats_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
