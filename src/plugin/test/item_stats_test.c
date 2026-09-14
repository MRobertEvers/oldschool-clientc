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
 *
 * SINCE THE PORCELAIN PORT there is a second subject, and it needs a fake the
 * old table of cases did not: the WIDGET TREE. The plugin itself still calls
 * no widget verb -- the layer does, walking a hovered cell up to a panel it
 * can name -- so the fake below is three nodes and a role table, and a test
 * without one would abort on the first hover.
 *
 * And a third subject, which is COST. "The tooltip restates itself whenever
 * what it says would change" is only an improvement if "and never otherwise"
 * is also true, so the compose count, the layer's own counters and the number
 * of reads of the game are all pinned rather than described. Those numbers
 * are written out exactly, not bounded: a fifth engine call a frame should be
 * a decision somebody made and not a drift nobody noticed.
 */

#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_api.h"

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
    struct ToriRS_ItemInfo objs[8];
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

static struct ToriRS_ItemInfo const*
fake_obj_find(int obj_id)
{
    for( int i = 0; i < g_client.obj_count; i++ )
        if( g_client.objs[i].obj_id == obj_id )
            return &g_client.objs[i];
    return NULL;
}

static int
fake_obj_info(int obj_id, struct ToriRS_ItemInfo* out)
{
    struct ToriRS_ItemInfo const* found = fake_obj_find(obj_id);
    if( !found )
        return 0;
    *out = *found;
    return 1;
}

static int
fake_inv_slot(
    int inv,
    int slot,
    int* out_obj_id,
    int* out_count)
{
    if( inv != TORIRS_INVENTORY_WORN || slot < 0 || slot >= FAKE_WORN_SLOTS )
        return 0;
    if( out_obj_id )
        *out_obj_id = g_client.worn[slot];
    if( out_count )
        *out_count = g_client.worn[slot] >= 0 ? 1 : 0;
    return 1;
}

static int
fake_inv_size(int inv)
{
    return inv == TORIRS_INVENTORY_WORN ? FAKE_WORN_SLOTS : 0;
}

static int g_mouse_x = 100, g_mouse_y = 100;
static int
fake_mouse_pos(int* out_x, int* out_y)
{
    if( out_x )
        *out_x = g_mouse_x;
    if( out_y )
        *out_y = g_mouse_y;
    return 1;
}

static int
fake_cfg_bool(char const* key)
{
    /* The plugin's own defaults, which is what a fresh install runs with. */
    if( strcmp(key, "show_theoretical") == 0 )
        return 0;
    if( strcmp(key, "always_show_base_stats") == 0 )
        return 0;
    return 1;
}

/* The plugin's own defaults, so the written BMP shows the colour scheme a
 * fresh install draws with rather than a wall of white. */
static uint32_t g_color_better = 0x33EE33u;

static uint32_t
fake_cfg_color(char const* key)
{
    if( strcmp(key, "color_better") == 0 )
        return g_color_better;
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

/* A file this install does not have, and how many times it was asked for.
 * An absent shipped table is a legitimate install; asking for it once is the
 * difference between that and a retry storm nobody can see. */
static char g_asset_denied[32];
static int g_asset_requests;

static int
fake_asset_load(char const* name)
{
    char path[512];
    FILE* f;
    long size;
    int at;

    g_asset_requests++;
    if( g_asset_denied[0] && strcmp(g_asset_denied, name) == 0 )
        return 0;
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
fake_asset_data(char const* name, int* out_size)
{
    int const at = fake_asset_find(name);

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
fake_asset_release(char const* name)
{
    int const at = fake_asset_find(name);

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
fake_image_load(char const* name)
{
    char path[512];
    FILE* f;
    long size;
    void* bytes;

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
fake_image_size(int image, int* out_w, int* out_h)
{
    if( image != 1 || !g_atlas_px )
        return 0;
    if( out_w )
        *out_w = g_atlas_w;
    if( out_h )
        *out_h = g_atlas_h;
    return 1;
}

static int
fake_image_pixels(int image, uint32_t* out, int max)
{
    int const count = g_atlas_w * g_atlas_h;
    if( image != 1 || !g_atlas_px || max < count )
        return 0;
    memcpy(out, g_atlas_px, (size_t)count * sizeof(uint32_t));
    return count;
}

static int
fake_image_compose(
    char const* name,
    int w,
    int h,
    uint32_t const* argb)
{
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
fake_image_release(int image)
{
    (void)image;
}

static struct ToriRS_Api g_api;
static struct ToriRS_ClientApi g_client_api;
static struct ToriRS_GameApi g_game_api;
static void* g_plugin_state;

/* ------------------------------------------------------- the widget tree */

/*
 * Three nodes, which is all the tree this plugin's one widget question needs.
 *
 * "Which container is the hovered cell in" is answered by walking the cell up
 * to a panel the portable vocabulary can name, so the fake has to be a TREE
 * and not a lookup: node 1 is the inventory panel, node 2 the bank's, node 3
 * the root both hang under. A cell reports the panel's own component id,
 * which is what a minimenu row carries.
 *
 * `panel_equipment` is deliberately a role nothing answers, because that is
 * the shape a real lane has -- no profile in this tree declares a
 * `panel_bank` at all -- and it is what makes the COST of the container
 * question visible: an unresolved watch is re-asked at every fence, and
 * test_container_watch_costs_one_find_a_frame is where that number is pinned.
 */
#define FAKE_NODE_INVENTORY 1
#define FAKE_NODE_BANK 2
#define FAKE_NODE_ROOT 3

#define FAKE_COMPONENT_INVENTORY ((149 << 16) | 0)
#define FAKE_COMPONENT_BANK ((12 << 16) | 13)

static int g_widget_finds;
static int g_widget_gets;

static struct ToriRS_WidgetRef
fake_ref(int node)
{
    struct ToriRS_WidgetRef ref;
    memset(&ref, 0, sizeof(ref));
    if( node <= 0 )
        return ref;
    ref.opaque[0] = (uint64_t)node;
    ref.opaque[1] = 1;
    ref.opaque[2] = 1;
    return ref;
}

static int
fake_node(struct ToriRS_WidgetRef ref)
{
    return ToriRS_WidgetRefValid(ref) ? (int)ref.opaque[0] : 0;
}

/** Which node a role names, or 0 for a role this lane does not have. */
static int
fake_role_node(char const* role)
{
    if( strcmp(role, "panel_inventory") == 0 )
        return FAKE_NODE_INVENTORY;
    if( strcmp(role, "panel_bank") == 0 )
        return FAKE_NODE_BANK;
    return 0;
}

static enum ToriRS_ContractResult
fake_widget_find(void* ctx, char const* role, struct ToriRS_WidgetRef* out)
{
    int const node = fake_role_node(role);
    (void)ctx;
    g_widget_finds++;
    if( !node )
        return TORIRS_CONTRACT_UNAVAILABLE;
    *out = fake_ref(node);
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_widget_get(void* ctx, int32_t component_id, struct ToriRS_WidgetRef* out)
{
    (void)ctx;
    g_widget_gets++;
    if( component_id == FAKE_COMPONENT_INVENTORY )
        *out = fake_ref(FAKE_NODE_INVENTORY);
    else if( component_id == FAKE_COMPONENT_BANK )
        *out = fake_ref(FAKE_NODE_BANK);
    else
        return TORIRS_CONTRACT_UNAVAILABLE;
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_widget_parent(void* ctx, struct ToriRS_WidgetRef ref, struct ToriRS_WidgetRef* out)
{
    int const node = fake_node(ref);
    (void)ctx;
    memset(out, 0, sizeof(*out));
    if( node == FAKE_NODE_INVENTORY || node == FAKE_NODE_BANK )
        *out = fake_ref(FAKE_NODE_ROOT);
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_widget_bounds(void* ctx, struct ToriRS_WidgetRef ref, struct ToriRS_WidgetBounds* out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    if( !fake_node(ref) )
        return TORIRS_CONTRACT_UNAVAILABLE;
    out->width = 765;
    out->height = 503;
    return TORIRS_CONTRACT_OK;
}

static enum ToriRS_ContractResult
fake_widget_state(void* ctx, struct ToriRS_WidgetRef ref, struct ToriRS_WidgetState* out)
{
    (void)ctx;
    if( !fake_node(ref) )
        return TORIRS_CONTRACT_UNAVAILABLE;
    out->bounds.width = 765;
    out->bounds.height = 503;
    out->presented = true;
    return TORIRS_CONTRACT_OK;
}

/* One subscriber per role, which is the host's own keying. A new subscription
 * receives BOUND straight away where the role resolves, exactly as the
 * contract says it must. */
#define FAKE_WATCHES_MAX 8
static struct
{
    char role[64];
    ToriRS_WidgetListener listener;
    void* user;
} g_watch[FAKE_WATCHES_MAX];

static enum ToriRS_ContractResult
fake_widget_watch_state(
    void* ctx, char const* role, ToriRS_WidgetListener listener, void* user)
{
    int free_slot = -1;
    (void)ctx;
    for( int i = 0; i < FAKE_WATCHES_MAX; i++ )
    {
        if( g_watch[i].listener && strcmp(g_watch[i].role, role) == 0 )
        {
            if( !listener )
                memset(&g_watch[i], 0, sizeof(g_watch[i]));
            else
            {
                g_watch[i].listener = listener;
                g_watch[i].user = user;
            }
            return TORIRS_CONTRACT_OK;
        }
        if( !g_watch[i].listener && free_slot < 0 )
            free_slot = i;
    }
    if( !listener )
        return TORIRS_CONTRACT_OK;
    if( free_slot < 0 )
        return TORIRS_CONTRACT_BUDGET_EXCEEDED;
    snprintf(g_watch[free_slot].role, sizeof(g_watch[free_slot].role), "%s", role);
    g_watch[free_slot].listener = listener;
    g_watch[free_slot].user = user;
    {
        int const node = fake_role_node(role);
        if( node )
        {
            struct ToriRS_WidgetEvent event;
            memset(&event, 0, sizeof(event));
            event.type = TORIRS_WIDGET_BOUND;
            event.widget = fake_ref(node);
            listener(&g_api, user, &event);
        }
    }
    return TORIRS_CONTRACT_OK;
}

/*
 * The `@tree` subscription, and this lane never publishes again.
 *
 * Which is the whole point of the cost cases below: this fake's tree is a
 * constant, so after the opening notification the layer has no reason to
 * re-ask about anything and every settled frame here is what a settled frame
 * in the client is -- nothing happening. A layer that polled on a clock
 * instead would show up as four role lookups a frame.
 */
static ToriRS_WidgetListener g_tree_listener;
static void* g_tree_user;
static int g_tree_subscribes;

static enum ToriRS_ContractResult
fake_widget_watch_tree(void* ctx, ToriRS_WidgetListener listener, void* user)
{
    struct ToriRS_WidgetEvent event;
    (void)ctx;
    g_tree_listener = listener;
    g_tree_user = user;
    if( !listener )
        return TORIRS_CONTRACT_OK;
    g_tree_subscribes++;
    memset(&event, 0, sizeof(event));
    event.type = TORIRS_WIDGET_TREE_CHANGED;
    listener(&g_api, user, &event);
    return TORIRS_CONTRACT_OK;
}

static void
fake_watches_reset(void)
{
    memset(g_watch, 0, sizeof(g_watch));
    g_tree_listener = NULL;
    g_tree_user = NULL;
    g_tree_subscribes = 0;
}

/*
 * The one capability this plugin asks: does the OPEN CACHE state equipment
 * bonuses at all? A fake that answered true unconditionally would make the
 * dat1 case below untestable, and that is the case the shipped table exists
 * for.
 */
static int g_lane_states_bonuses = 1;
static bool
fake_capability(struct ToriRS_Api* api, char const* name)
{
    (void)api;
    if( strcmp(name, "item_bonuses") == 0 )
        return g_lane_states_bonuses != 0;
    return false;
}

static bool v2_cfg_bool(struct ToriRS_Api* api, char const* key, bool* out)
{ (void)api; *out = fake_cfg_bool(key) != 0; return true; }
static bool v2_cfg_color(struct ToriRS_Api* api, char const* key, uint32_t* out)
{ (void)api; *out = fake_cfg_color(key); return true; }
/* Reads of the GAME, which are not layer calls and so are invisible to the
 * Porcelain counters. The freshness fix moved this half from once per thirty
 * frames to once per hovering frame, and that is a trade worth a number. */
static int g_game_reads;

static bool v2_skill(
    struct ToriRS_Api* api, int skill, struct ToriRS_SkillSnapshot* out)
{
    (void)api;
    g_game_reads++;
    if( skill < 0 || skill >= FAKE_SKILLS ) return false;
    memset(out, 0, sizeof(*out));
    out->struct_size = sizeof(*out);
    out->index = skill;
    snprintf(out->name, sizeof(out->name), "%s", FAKE_SKILL_NAME[skill]);
    out->current_level = g_client.current[skill];
    out->base_level = g_client.base[skill];
    return true;
}
static int v2_run_energy(struct ToriRS_Api* api)
{ (void)api; g_game_reads++; return g_client.run_energy; }
static int v2_inv_size(struct ToriRS_Api* api, int inv)
{ (void)api; return fake_inv_size(inv); }
static bool v2_inv_slot(
    struct ToriRS_Api* api, int inv, int slot, int* obj, int* count)
{ (void)api; g_game_reads++; return fake_inv_slot(inv, slot, obj, count) != 0; }
static bool v2_item_info(
    struct ToriRS_Api* api, int obj, struct ToriRS_ItemInfo* out)
{ (void)api; g_game_reads++; return fake_obj_info(obj, out) != 0; }
static bool v2_pointer(struct ToriRS_Api* api, int* x, int* y)
{ (void)api; return fake_mouse_pos(x, y) != 0; }
static enum ToriRS_AssetState v2_asset_request(
    struct ToriRS_Api* api, char const* name)
{ (void)api; return fake_asset_load(name) ? TORIRS_ASSET_READY : TORIRS_ASSET_MISSING; }
static bool v2_asset_bytes(
    struct ToriRS_Api* api, char const* name, void const** data, size_t* size)
{
    int n = 0;
    (void)api;
    *data = fake_asset_data(name, &n);
    *size = n > 0 ? (size_t)n : 0;
    return *data != NULL;
}
static void v2_asset_release(struct ToriRS_Api* api, char const* name)
{ (void)api; fake_asset_release(name); }
static enum ToriRS_AssetState v2_image(
    struct ToriRS_Api* api, char const* name, struct ToriRS_ImageRef* out)
{
    int const image = fake_image_load(name);
    (void)api;
    out->value = image;
    return image > 0 ? TORIRS_ASSET_READY : TORIRS_ASSET_MISSING;
}
static bool v2_image_size(
    struct ToriRS_Api* api, struct ToriRS_ImageRef image, int* w, int* h)
{ (void)api; return fake_image_size(image.value, w, h) != 0; }
static bool v2_image_pixels(
    struct ToriRS_Api* api, struct ToriRS_ImageRef image,
    uint32_t* out, size_t capacity, size_t* count)
{
    int const n = fake_image_pixels(image.value, out, (int)capacity);
    (void)api;
    *count = n > 0 ? (size_t)n : 0;
    return n > 0;
}
static enum ToriRS_AssetState v2_image_compose(
    struct ToriRS_Api* api, char const* name, int w, int h,
    uint32_t const* pixels, struct ToriRS_ImageRef* out)
{
    (void)api;
    out->value = fake_image_compose(name, w, h, pixels);
    return out->value > 0 ? TORIRS_ASSET_READY : TORIRS_ASSET_ERROR;
}
static void v2_image_release(struct ToriRS_Api* api, struct ToriRS_ImageRef image)
{ (void)api; fake_image_release(image.value); }
/* The canvas this paint callback may draw on, as the plugin reads it from the
 * graphics context's draw_context bounds -- the only area the API offers. */
static struct ToriRS_Rect g_canvas_bounds = { 0, 0, 765, 503 };
static bool v2_draw_context(struct ToriRS_Graphics* draw, struct ToriRS_DrawContext* out)
{
    (void)draw;
    if( out->struct_size < sizeof(*out) ) return false;
    out->bounds = g_canvas_bounds;
    return true;
}
static int g_draw_x, g_draw_y;
static void v2_draw_image(
    struct ToriRS_Graphics* draw, struct ToriRS_ImageRef image, int x, int y, int alpha)
{ (void)draw; (void)image; (void)alpha; g_draw_x = x; g_draw_y = y; g_client.draw_count++; }

static void
api_init(void)
{
    memset(&g_api, 0, sizeof(g_api));
    memset(&g_client_api, 0, sizeof(g_client_api));
    memset(&g_game_api, 0, sizeof(g_game_api));
    g_api.struct_size = sizeof(g_api);
    g_api.major_version = TORIRS_PLUGIN_API_MAJOR;
    g_api.config.get_bool = v2_cfg_bool;
    g_api.config.get_color = v2_cfg_color;
    g_api.input.pointer = v2_pointer;
    g_api.assets.request = v2_asset_request;
    g_api.assets.bytes = v2_asset_bytes;
    g_api.assets.release = v2_asset_release;
    g_api.assets.image = v2_image;
    g_api.assets.image_size = v2_image_size;
    g_api.assets.image_pixels = v2_image_pixels;
    g_api.assets.image_compose = v2_image_compose;
    g_api.assets.image_release = v2_image_release;
    /*
     * The widget verbs, which this plugin never calls and the LAYER does: the
     * container a hovered cell belongs to is answered by walking the cell up
     * to a panel, through a watch that the layer subscribes and the host
     * answers. A test with no widget table would abort the first time a cell
     * was hovered, which is the whole of the port.
     */
    g_api.widgets.find = fake_widget_find;
    g_api.widgets.get_widget = fake_widget_get;
    g_api.widgets.parent = fake_widget_parent;
    g_api.widgets.bounds = fake_widget_bounds;
    g_api.widgets.state = fake_widget_state;
    g_api.widgets.watch_state = fake_widget_watch_state;
    g_api.widgets.watch_tree = fake_widget_watch_tree;
    g_api.core.capability = fake_capability;
    g_game_api.struct_size = sizeof(g_game_api);
    g_game_api.skill = v2_skill;
    g_game_api.run_energy = v2_run_energy;
    g_game_api.inventory_size = v2_inv_size;
    g_game_api.inventory_slot = v2_inv_slot;
    g_game_api.item_info = v2_item_info;
    g_api.game = &g_game_api;
}

/* ---------------------------------------------------------------- driving */

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_ITEM_STATS;

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

/*
 * The instance the host allocates, and the layer handle inside it.
 *
 * The handle is the FIRST field of the plugin's state, which is the one thing
 * this test knows about that state -- and knowing it is what lets the findings
 * and the counters below be read at all. The same assumption, for the same
 * reason, as tileind_v2_test.c's TileindStateHead.
 */
struct ItemStatsStateHead
{
    struct Porcelain* porcelain;
};

static struct Porcelain*
handle(void)
{
    return ((struct ItemStatsStateHead*)g_plugin_state)->porcelain;
}

/** Findings this plugin has recorded that it did NOT declare expected. */
static int
undeclared_findings(void)
{
    struct PorcelainFinding found[PORCELAIN_FINDINGS_MAX];
    int const count = Porcelain_Findings(handle(), found, PORCELAIN_FINDINGS_MAX);
    int undeclared = 0;

    for( int i = 0; i < count; i++ )
        if( !found[i].expected )
            undeclared++;
    return undeclared;
}

/** How many times a finding with this detail was recorded, coalesced. */
static int
finding_count(char const* detail)
{
    struct PorcelainFinding found[PORCELAIN_FINDINGS_MAX];
    int const count = Porcelain_Findings(handle(), found, PORCELAIN_FINDINGS_MAX);

    for( int i = 0; i < count; i++ )
        if( found[i].detail && strcmp(found[i].detail, detail) == 0 )
            return (int)found[i].count;
    return 0;
}

/** One frame: the hover pass names the item, then the canvas draws. */
static void
frame_in(int hovered_obj_id, int component_id)
{
    struct ToriRS_MenuBuildEvent menu;
    struct ToriRS_FrameEvent frame_ev;
    struct ToriRS_Graphics draw;

    memset(&frame_ev, 0, sizeof(frame_ev));
    TORIRS_PLUGIN_ITEM_STATS.callbacks.on_frame_start(&g_api, g_plugin_state, &frame_ev);

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
        menu.rows[0].component_id = component_id;
        menu.rows[0].slot = 3;
    }
    TORIRS_PLUGIN_ITEM_STATS.callbacks.on_menu_build(&g_api, g_plugin_state, &menu);

    memset(&draw, 0, sizeof(draw));
    draw.struct_size = sizeof(draw);
    draw.image = v2_draw_image;
    draw.context = v2_draw_context;
    g_client.compose_w = 0;
    g_client.compose_h = 0;
    g_client.draw_count = 0;
    TORIRS_PLUGIN_ITEM_STATS.callbacks.on_draw_canvas(&g_api, g_plugin_state, &draw);
}

static void
frame(int hovered_obj_id)
{
    frame_in(hovered_obj_id, FAKE_COMPONENT_INVENTORY);
}

/** Register an objtype the fake client can answer for. */
static struct ToriRS_ItemInfo*
obj_add(int obj_id, char const* name)
{
    struct ToriRS_ItemInfo* out;
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
    if( g_plugin_state )
    {
        TORIRS_PLUGIN_ITEM_STATS.callbacks.on_stop(&g_api, g_plugin_state);
        free(g_plugin_state);
        g_plugin_state = NULL;
    }
    memset(&g_client, 0, sizeof(g_client));
    for( int i = 0; i < FAKE_SKILLS; i++ )
    {
        g_client.current[i] = 99;
        g_client.base[i] = 99;
    }
    for( int i = 0; i < FAKE_WORN_SLOTS; i++ )
        g_client.worn[i] = -1;
    g_client.run_energy = 100;
    g_lane_states_bonuses = 1;
    g_color_better = 0x33EE33u;
    /* The host keys a subscription by ROLE NAME, and a closed handle's watches
     * are the host's to forget. Dropping them here is what stops one case's
     * listener being called with the next case's freed handle. */
    fake_watches_reset();
    g_widget_finds = 0;
    g_widget_gets = 0;
    /* Every case below is about a panel appearing, so the plugin is restarted
     * with it: the composed panel is cached against what it SAYS, and a case
     * that reused it would be measuring the previous one. */
    g_plugin_state = calloc(1, TORIRS_PLUGIN_ITEM_STATS.state_size);
    assert(g_plugin_state);
    TORIRS_PLUGIN_ITEM_STATS.callbacks.on_start(&g_api, g_plugin_state);
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

/*
 * An open right-click menu stops the tooltip, which is the reference client's
 * isMenuOpen() gate arriving for free.
 *
 * A right-click build is not a hover: the pointer is over the MENU by then,
 * and a tooltip that kept following it would be pinned to a cell nobody is
 * pointing at. The rebuild stops running while the menu is up, so the stash
 * goes stale inside a frame and the panel stops drawing.
 */
static void
test_an_open_menu_stops_the_tooltip(void)
{
    struct ToriRS_MenuBuildEvent menu;
    struct ToriRS_FrameEvent frame_ev;
    struct ToriRS_Graphics draw;

    client_reset();
    obj_add(385, "Shark");
    g_client.current[3] = 50;
    frame(385);
    TEST_ASSERT(g_client.draw_count == 1, "the hover draws");

    /* Two frames of a right-click build and nothing else: the first is still
     * inside the one-frame liveness window, the second is not. */
    for( int i = 0; i < 2; i++ )
    {
        memset(&frame_ev, 0, sizeof(frame_ev));
        TORIRS_PLUGIN_ITEM_STATS.callbacks.on_frame_start(&g_api, g_plugin_state, &frame_ev);
        memset(&menu, 0, sizeof(menu));
        menu.hover_pass = false;
        menu.row_count = 1;
        menu.rows[0].text = "Eat";
        menu.rows[0].pick_kind = 2;
        menu.rows[0].target_id = 385;
        menu.rows[0].component_id = FAKE_COMPONENT_INVENTORY;
        menu.rows[0].slot = 3;
        TORIRS_PLUGIN_ITEM_STATS.callbacks.on_menu_build(&g_api, g_plugin_state, &menu);
        memset(&draw, 0, sizeof(draw));
        draw.struct_size = sizeof(draw);
        draw.image = v2_draw_image;
        draw.context = v2_draw_context;
        g_client.draw_count = 0;
        TORIRS_PLUGIN_ITEM_STATS.callbacks.on_draw_canvas(&g_api, g_plugin_state, &draw);
    }
    TEST_ASSERT(
        g_client.draw_count == 0,
        "and a right-click build is not one, so the panel goes (drew %d)",
        g_client.draw_count);
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

/* The tooltip stays inside the graphics context's canvas bounds, not inside a
 * placement area: a pointer near the canvas corner flips the panel up and left
 * of the pointer, and a smaller canvas moves that edge. An inventory hover is
 * outside the 3D viewport and must NOT flip: the canvas, not the viewport, is
 * the bound. */
static void
test_tooltip_clamps_to_draw_canvas(void)
{
    client_reset();
    obj_add(385, "Shark");
    g_client.current[3] = 50;
    g_mouse_x = 100; g_mouse_y = 100;
    g_canvas_bounds = (struct ToriRS_Rect){ 0, 0, 765, 503 };
    frame(385);
    TEST_ASSERT(g_client.draw_count == 1 && g_draw_x == 112 && g_draw_y == 116,
        "away from the edge the panel sits right and below the pointer (%d,%d)", g_draw_x, g_draw_y);
    g_mouse_x = 618; g_mouse_y = 235; /* inventory slot 1 on the fixed frame */
    frame(385);
    TEST_ASSERT(g_draw_x == 630 && g_draw_y == 251,
        "an inventory hover outside the 3D viewport keeps the panel beside the pointer (%d,%d)", g_draw_x, g_draw_y);
    g_mouse_x = 760; g_mouse_y = 495;
    frame(385);
    TEST_ASSERT(g_draw_x < 760 && g_draw_y < 495,
        "at the canvas corner the panel flips up and left (%d,%d)", g_draw_x, g_draw_y);
    g_mouse_x = 250; g_mouse_y = 190;
    g_canvas_bounds = (struct ToriRS_Rect){ 0, 0, 300, 200 };
    frame(385);
    TEST_ASSERT(g_draw_x < 250 && g_draw_y < 190,
        "a smaller canvas moves the flip edge with it (%d,%d)", g_draw_x, g_draw_y);
    g_mouse_x = 100; g_mouse_y = 100;
    g_canvas_bounds = (struct ToriRS_Rect){ 0, 0, 765, 503 };
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
    struct ToriRS_ItemInfo* note;

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
    struct ToriRS_ItemInfo* whip;

    client_reset();
    whip = obj_add(4151, "Abyssal whip");
    whip->wearpos = 3;
    whip->has_bonuses = 1;
    whip->attack_rate = 4;
    whip->bonus[TORIRS_EQUIPMENT_BONUS_ATTACK_SLASH] = 82;
    whip->bonus[TORIRS_EQUIPMENT_BONUS_STRENGTH] = 82;
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
    struct ToriRS_ItemInfo* whip;

    client_reset();
    whip = obj_add(4151, "Abyssal whip");
    whip->wearpos = 3;
    whip->has_bonuses = 1;
    whip->attack_rate = 4;
    whip->bonus[TORIRS_EQUIPMENT_BONUS_ATTACK_SLASH] = 82;
    whip->bonus[TORIRS_EQUIPMENT_BONUS_STRENGTH] = 82;
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
    struct ToriRS_ItemInfo* bow;
    struct ToriRS_ItemInfo* shield;
    int with_shield;
    int without_shield;

    client_reset();
    bow = obj_add(11785, "Armadyl crossbow");
    bow->wearpos = 3;
    bow->wearpos2 = 5;
    bow->has_bonuses = 1;
    bow->attack_rate = 6;
    bow->bonus[TORIRS_EQUIPMENT_BONUS_ATTACK_RANGE] = 100;
    frame(11785);
    without_shield = tip_rows();

    client_reset();
    bow = obj_add(11785, "Armadyl crossbow");
    bow->wearpos = 3;
    bow->wearpos2 = 5;
    bow->has_bonuses = 1;
    bow->attack_rate = 6;
    bow->bonus[TORIRS_EQUIPMENT_BONUS_ATTACK_RANGE] = 100;
    shield = obj_add(1540, "Anti-dragon shield");
    shield->wearpos = 5;
    shield->has_bonuses = 1;
    shield->bonus[TORIRS_EQUIPMENT_BONUS_DEFENCE_MAGIC] = 10;
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
    struct ToriRS_ItemInfo* scimitar;

    client_reset();
    /* The lane states nothing, which is the ONLY condition under which the
     * shipped table may answer. It used to be asked per ITEM, so an
     * OldSchool record that happened to carry no params fell through to a
     * table baked from a different cache. */
    g_lane_states_bonuses = 0;
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
    struct ToriRS_ItemInfo* scimitar;

    client_reset();
    scimitar = obj_add(1333, "Rune scimitar");
    scimitar->has_bonuses = 1;
    scimitar->wearpos = 3;
    scimitar->attack_rate = 4;
    scimitar->bonus[TORIRS_EQUIPMENT_BONUS_ATTACK_SLASH] = 1;
    frame(1333);
    TEST_ASSERT(
        tip_rows() == 2,
        "the record's one bonus and its heading, not the table's four; got %d",
        tip_rows());
}

/* ------------------------------------------------- what the port changed */

/*
 * A bank cell and an inventory cell are two hovers.
 *
 * `component_id` and `slot` were captured from the menu row and then never
 * read once, so both containers keyed on the obj id alone and the second one
 * was handed the first one's picture. The container is part of the derived
 * key now; moving between the two composes again.
 */
static void
test_bank_and_inventory_are_two_tooltips(void)
{
    client_reset();
    obj_add(385, "Shark");
    g_client.current[3] = 50;

    frame_in(385, FAKE_COMPONENT_INVENTORY);
    TEST_ASSERT(g_client.compose_count == 1, "the inventory hover composes once");
    frame_in(385, FAKE_COMPONENT_INVENTORY);
    TEST_ASSERT(
        g_client.compose_count == 1,
        "and a second frame of the same hover composes nothing (composed %d)",
        g_client.compose_count);
    frame_in(385, FAKE_COMPONENT_BANK);
    TEST_ASSERT(
        g_client.compose_count == 2,
        "the same shark in the BANK is a different tooltip (composed %d)",
        g_client.compose_count);
    TEST_ASSERT(g_client.draw_count == 1, "and it is drawn");
}

/*
 * The tooltip restates itself when what it SAYS changes, under a pointer that
 * has not moved.
 *
 * This is the row the ledger opened against the thirty-frame TTL: a heal
 * printed against hitpoints that have since drained was wrong for up to half a
 * second and then flickered to the right answer at a moment unrelated to the
 * change. Nothing about the hover moves here; only the player does.
 */
static void
test_a_drained_stat_restates_the_tooltip(void)
{
    int width_before;

    client_reset();
    obj_add(385, "Shark");
    g_client.current[3] = 99;

    frame(385);
    TEST_ASSERT(g_client.compose_count == 1, "the first frame composes");
    width_before = g_client.compose_w;
    for( int i = 0; i < 40; i++ )
        frame(385);
    TEST_ASSERT(
        g_client.compose_count == 1,
        "forty frames of a stationary pointer over an unchanging item compose "
        "nothing more (composed %d)",
        g_client.compose_count);

    g_client.current[3] = 50; /* the heal lands differently now */
    frame(385);
    TEST_ASSERT(
        g_client.compose_count == 2,
        "a hitpoint that moved restates the tooltip on the NEXT frame "
        "(composed %d)",
        g_client.compose_count);

    /*
     * And a change that moves NO glyph at all.
     *
     * The case above would still pass with the rows left out of the derived
     * key entirely, because a different number of digits is a different panel
     * WIDTH and the layer compares the size separately. A colour moves what
     * the tooltip says and nothing else, so it is the one that pins the rows
     * themselves -- and it is the ledger's "config change repaints" row,
     * which used to need a callback to throw the picture away by hand.
     */
    width_before = g_client.compose_w;
    for( int i = 0; i < 10; i++ )
        frame(385);
    TEST_ASSERT(g_client.compose_count == 2, "and settles again");
    g_color_better = 0x00FFFFu;
    TORIRS_PLUGIN_ITEM_STATS.callbacks.on_config_changed(&g_api, g_plugin_state, "color_better");
    frame(385);
    TEST_ASSERT(
        g_client.compose_count == 3,
        "an edited colour repaints with no glyph having moved (composed %d)",
        g_client.compose_count);
    TEST_ASSERT(
        g_client.compose_w == width_before,
        "and the panel is exactly the size it was (%d -> %d)",
        width_before,
        g_client.compose_w);
}

/*
 * Nothing changing costs nothing.
 *
 * Read off the layer's own counters rather than believed: a stand-in the test
 * wrote itself would pin the stand-in. This plugin owns no control and
 * describes nothing, so every one of these is zero by construction -- and the
 * number that is NOT zero by construction is engine_calls, which is what a
 * per-frame asset re-request or a per-frame capability scan would show up in.
 */
static void
test_steady_state_costs_nothing(void)
{
    struct PorcelainCounters counters;

    client_reset();
    obj_add(385, "Shark");
    g_client.current[3] = 50;
    frame(385);
    TEST_ASSERT(g_client.compose_count == 1, "the picture exists");

    Porcelain_CountersReset(handle());
    g_widget_finds = 0;
    for( int i = 0; i < 60; i++ )
        frame(385);
    Porcelain_CountersRead(handle(), &counters);
    TEST_ASSERT(g_client.compose_count == 1, "sixty settled frames compose nothing more");
    TEST_ASSERT(counters.describe_runs == 0, "this plugin describes nothing");
    TEST_ASSERT(counters.setters == 0, "it owns no control to move");
    TEST_ASSERT(counters.revalidates == 0, "and asks for no layout");
    TEST_ASSERT(counters.allocations == 0,
        "and allocates nothing once the picture is painted (allocated %u)",
        counters.allocations);
    /*
     * THREE engine calls per frame while a tooltip is up, and the number is
     * written out rather than bounded so that a fourth is a decision.
     *
     *   1  draw->context      -- the pass's own drawable rect, which is the
     *                            one call the plugin made before the port too
     *   1  widgets.bounds     -- the CANVAS box, which is what decides
     *                            `canvas_space`: a tooltip clamped against a
     *                            panel well's local rectangle is the retired
     *                            placement bug that flipped it over the
     *                            minimap
     *   1  widgets.get_widget -- the hovered cell, for the container question
     *
     * A frame with nothing hovered costs none of them: the menu note returns
     * before the container walk and the draw callback returns before the
     * context.
     *
     * It was FOUR when this port landed, plus two for a one-off frame-root
     * walk, and the fourth was a second widgets.bounds: Porcelain_DrawContext
     * derived the USABLE rect for every caller as well. This caller wants a
     * pass to clamp to, not a placement area to lay out in, and so does every
     * other caller of the verb -- nothing in the tree read that field. It is
     * gone from the struct rather than left answering zero, and with it goes
     * the reason a tooltip ever asked whether this lane has a docked strip.
     * The stray two went with it: they were the frame-root cache being
     * invalidated when the four strip watches settled to ABSENT.
     */
    TEST_ASSERT(
        counters.engine_calls == 60 * 3,
        "a settled hovering frame costs the draw region, the canvas box and "
        "one cell lookup -- nothing else (%u over 60 frames)",
        counters.engine_calls);

    Porcelain_CountersReset(handle());
    g_game_reads = 0;
    for( int i = 0; i < 60; i++ )
        frame(-1);
    Porcelain_CountersRead(handle(), &counters);
    /*
     * ZERO, and this is the layer's central claim stated as a number.
     *
     * It was four a frame for one release: Porcelain_Usable was fixed to
     * CREATE its lane-chrome watches rather than do a read-only lookup that
     * always answered nothing -- which it had to be, a frame provider cannot
     * subtract a docked strip without it -- and Porcelain_DrawContext derived
     * the usable rect for every caller. So every plugin that took a draw
     * context left four unresolved watches behind on a lane with no docked
     * strip, and porcelain_resolve_pending re-asked all four on every fence
     * for the rest of the session.
     *
     * Two halves to the fix, and only the second one is general. This plugin
     * no longer creates those four watches at all, because the usable rect
     * left the draw context. The one below is the half that holds for a
     * plugin which really does leave an unresolved watch behind:
     * An element that is not in the tree can only appear when the tree
     * changes, so that is when the layer re-asks now. This fake's tree never
     * publishes after the opening notification, which is exactly a client
     * with nothing happening in it.
     */
    TEST_ASSERT(
        counters.engine_calls == 0,
        "and a frame with nothing hovered costs nothing at all -- an "
        "unresolved watch is re-asked when the TREE moves, not on a clock "
        "(%u over 60)",
        counters.engine_calls);
    TEST_ASSERT(
        g_game_reads == 0,
        "not one read of the game either (%d over 60)",
        g_game_reads);

    /*
     * What the freshness rule COSTS, and it is not an engine call.
     *
     * Saying what the tooltip would say is arithmetic over one sample of the
     * player, and the sample is the twenty-three skills plus the run meter
     * plus the hovered record. Before the port that ran once every thirty frames and
     * the panel was wrong in between; now it runs on every frame the pointer
     * is over a cell, and the EXPENSIVE half -- setting twenty rows of
     * glyphs into a buffer -- is the one behind the hash. This is the number
     * that trade is made of, so it is written out rather than described.
     */
    client_reset();
    obj_add(385, "Shark");
    g_client.current[3] = 50;
    frame(385);
    g_game_reads = 0;
    for( int i = 0; i < 10; i++ )
        frame(385);
    TEST_ASSERT(
        g_game_reads == 10 * 25,
        "a hovering frame reads the twenty-three skills, the run meter and the "
        "hovered record: twenty-five (%d over 10 frames)",
        g_game_reads);
}

/*
 * What the container question COSTS, said as two numbers.
 *
 * This is the whole of the lane-read growth the port carries, and it is here
 * so that it is something a reader can weigh rather than a sentence in a
 * report. An INVENTORY hover is cheap because the inventory is the first
 * panel the walk asks about. A BANK hover walks past `panel_equipment` on the
 * way and leaves an unresolved watch behind. That watch is re-asked for
 * exactly as long as the layer is still waiting on it -- the two fences of
 * the absence clock -- and then never again: once an element is ABSENT the
 * only thing that can change the answer is a new tree, and this lane does not
 * publish one. TWO role lookups for the life of the session, not one a frame.
 */
static void
test_what_the_container_question_costs(void)
{
    client_reset();
    obj_add(385, "Shark");
    g_client.current[3] = 50;

    TEST_ASSERT(g_widget_gets == 0, "before any hover, not one widget is asked for");
    frame(385);
    g_widget_gets = 0;
    g_widget_finds = 0;
    for( int i = 0; i < 20; i++ )
        frame(385);
    TEST_ASSERT(
        g_widget_gets == 20,
        "an inventory hover is one component lookup a frame (%d over 20)",
        g_widget_gets);
    TEST_ASSERT(
        g_widget_finds == 0,
        "and no role lookup for the CONTAINER: the panel that answers is the "
        "first one "
        "the walk asks about (%d)",
        g_widget_finds);

    client_reset();
    obj_add(385, "Shark");
    g_client.current[3] = 50;
    frame_in(385, FAKE_COMPONENT_BANK);
    g_widget_finds = 0;
    for( int i = 0; i < 20; i++ )
        frame_in(385, FAKE_COMPONENT_BANK);
    TEST_ASSERT(
        g_widget_finds == 2,
        "a bank hover leaves an unresolved panel watch behind, and it costs "
        "the two fences of the absence clock and then nothing at all -- not "
        "one role lookup a frame for ever (%d over 20)",
        g_widget_finds);
}

/*
 * A worn item neither source can name is SAID, and the comparison stops.
 *
 * On a dat1 world the worn side resolves by name too, and a miss used to
 * contribute zero -- so an equipped weapon the table has never heard of was
 * silently treated as bare skin and every delta against it was wrong, with
 * nothing anywhere to say so.
 */
static void
test_worn_miss_is_a_finding_and_no_rows(void)
{
    struct ToriRS_ItemInfo* hovered;
    struct ToriRS_ItemInfo* worn;

    client_reset();
    g_lane_states_bonuses = 0;
    hovered = obj_add(1333, "Rune scimitar");
    hovered->has_bonuses = 0;
    hovered->wearpos = -1;
    worn = obj_add(60001, "Blade of unheardof");
    worn->has_bonuses = 0;
    worn->wearpos = -1;
    g_client.worn[3] = 60001;

    frame(1333);
    TEST_ASSERT(
        g_client.draw_count == 0,
        "no panel is drawn against a worn item nobody can name (drew %d)",
        g_client.draw_count);
    TEST_ASSERT(
        finding_count("Blade of unheardof") == 1,
        "and the miss is one finding naming the item (%d)",
        finding_count("Blade of unheardof"));

    for( int i = 0; i < 10; i++ )
        frame(1333);
    TEST_ASSERT(
        finding_count("Blade of unheardof") == 11,
        "the finding coalesces rather than multiplying into the table (%d)",
        finding_count("Blade of unheardof"));
}

/*
 * An absent shipped file is asked for ONCE.
 *
 * The tri-state that was supposed to say so had a branch that could not run:
 * "absent" was only reachable after the asset had already answered READY, so a
 * text.ini that was simply not there was re-requested on every frame the
 * pointer sat over a cell, for the life of the session, silently.
 */
static void
test_a_missing_file_is_one_finding_not_a_retry_storm(void)
{
    client_reset();
    snprintf(g_asset_denied, sizeof(g_asset_denied), "%s", "text.ini");
    obj_add(385, "Shark");
    g_client.current[3] = 50;
    g_asset_requests = 0;

    for( int i = 0; i < 20; i++ )
        frame(385);
    TEST_ASSERT(
        g_client.draw_count == 0, "with no metrics nothing is drawn (drew %d)", g_client.draw_count);
    TEST_ASSERT(
        finding_count("text.ini") == 1,
        "the absence is one finding (%d)",
        finding_count("text.ini"));
    TEST_ASSERT(
        g_asset_requests == 1,
        "and the file is asked for once, not twenty times (%d requests)",
        g_asset_requests);
    g_asset_denied[0] = '\0';
}

/*
 * The four things this client cannot do, said where a capture reads them.
 *
 * Each is expected=1, so the clean-findings gate ignores it -- and each stops
 * being true loudly rather than quietly, which is the half the paragraph at
 * the top of item_stats.c cannot manage on its own.
 */
static void
test_the_four_absences_are_declared(void)
{
    client_reset();
    TEST_ASSERT(
        undeclared_findings() == 0,
        "a plugin that has done nothing yet has nothing to report (%d)",
        undeclared_findings());
    TEST_ASSERT(finding_count("item weight") == 1, "weight is declared");
    TEST_ASSERT(finding_count("magic damage") == 1, "magic damage is declared");
    TEST_ASSERT(finding_count("potion durations") == 1, "potion durations are declared");
    TEST_ASSERT(finding_count("spicy stew boost") == 1, "spicy stew is declared");
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
    test_an_open_menu_stops_the_tooltip();
    test_unknown_item();
    test_food_heals();
    test_tooltip_clamps_to_draw_canvas();
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
    test_bank_and_inventory_are_two_tooltips();
    test_a_drained_stat_restates_the_tooltip();
    test_steady_state_costs_nothing();
    test_what_the_container_question_costs();
    test_worn_miss_is_a_finding_and_no_rows();
    test_a_missing_file_is_one_finding_not_a_retry_storm();
    test_the_four_absences_are_declared();
    test_render_sample();
    write_png_stub();

    if( g_plugin_state )
    {
        TORIRS_PLUGIN_ITEM_STATS.callbacks.on_stop(&g_api, g_plugin_state);
        free(g_plugin_state);
        g_plugin_state = NULL;
    }

    printf("item_stats_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
