/*
 * What the two trackers actually LOOK like, as pixels.
 *
 * Separate from tracker_test.c, which is about the arithmetic and the page
 * model: this one composes the real strips out of the real shipped art and
 * writes them to PNGs, because "does it look like the cache's own tracker" is
 * a question no assertion answers. The counts and colours it does pin are the
 * ones a layout regression moves -- the strip's height, the box pitch, the
 * bar's fill -- and everything else is in the picture.
 *
 *   TRACKER_RENDER_XP_PNG    where the xp strip goes   (build/xp_strip.png)
 *   TRACKER_RENDER_LOOT_PNG  where the loot strip goes (build/loot_strip.png)
 *
 * Run it from `src/`: the asset reads below resolve `../script/plugins/assets`
 * the same way item_stats_test does.
 *
 * The scenarios are the reference screenshots' own -- virtual-level 120
 * attack plus level 110 hitpoints, and a Guard/Cerberus/Werewolf loot log --
 * so the two pictures can be put beside the captures they are trying to match.
 */

#include "plugin/torirs_plugin_v2.h"

#include "engine/png_decode.h"
#include "miniz.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond, ...)                                                                       \
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

/* ---------------------------------------------------------------- the client */

#define FAKE_SKILLS 25
#define FAKE_ASSETS 32
/* One per distinct (obj, quantity) the grid asks for, plus the art. The real
 * host keeps a bounded LRU of 48 and re-rasterises past it; this one simply
 * has room, because running out here shows up as blank cells that look like a
 * plugin bug and are not one. */
#define FAKE_IMAGES 512

static char const* const SKILL_NAME[FAKE_SKILLS] = {
    "Attack", "Defence", "Strength", "Hitpoints", "Ranged", "Prayer", "Magic",
    "Cooking", "Woodcutting", "Fletching", "Fishing", "Firemaking", "Crafting",
    "Smithing", "Mining", "Herblore", "Agility", "Thieving", "Slayer", "Farming",
    "Runecraft", "Hunter", "Construction", "Sailing", "Summoning"
};

/* Enough of the client's xp table for the levels the scenario uses. */
static int const LEVEL_XP[100] = {
    0, 0, 83, 174, 276, 388, 512, 650, 801, 969, 1154, 1358, 1584, 1833, 2107,
    2411, 2746, 3115, 3523, 3973, 4470, 5018, 5624, 6291, 7028, 7842, 8740,
    9730, 10824, 12031, 13363, 14833, 16456, 18247, 20224, 22406, 24815, 27473,
    30408, 33648, 37224, 41171, 45529, 50339, 55649, 61512, 67983, 75127, 83014,
    91721, 101333, 111945, 123660, 136594, 150872, 166636, 184040, 203254,
    224466, 247886, 273742, 302288, 333804, 368599, 407015, 449428, 496254,
    547953, 605032, 668051, 737627, 814445, 899257, 992895, 1096278, 1210421,
    1336443, 1475581, 1629200, 1798808, 1986068, 2192818, 2421087, 2673114,
    2951373, 3258594, 3597792, 3972294, 4385776, 4842295, 5346332, 5902831,
    6517253, 7195629, 7944614, 8771558, 9684577, 10692629, 11805606, 13034431
};

struct FakeAsset
{
    char name[64];
    unsigned char* bytes;
    int size;
};

struct FakeImage
{
    int used;
    int w;
    int h;
    uint32_t* px;
};

static struct
{
    uint64_t now_ms;
    int xp[FAKE_SKILLS];
    int level[FAKE_SKILLS];

    /** Which plugin's asset folder a read resolves against. */
    char const* asset_dir;

    struct FakeAsset asset[FAKE_ASSETS];
    int asset_count;
    struct FakeImage image[FAKE_IMAGES];

    /** The last composed strip, which is the picture under test. */
    char composed[32];
    int comp_w;
    int comp_h;
    uint32_t* comp_px;
    int compose_calls;
    int obj_image_calls;

    /** What the plugin registered for its popout-rail entry. */
    char panel_icon[64];
    int panel_preferred_width;

    /* config, as a flat key/value list */
    struct { char k[32]; char v[192]; } cfg[24];
    int cfg_count;
} g_c;

/* ------------------------------------------------------------------- verbs */

static uint64_t fake_frame_ms(void* c) { (void)c; return g_c.now_ms; }

static int
fake_local_player(void* ctx, struct ToriRS_PlayerSnapshot* out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    out->true_x = 3200;
    out->true_z = 3200;
    return 1;
}

/* ---- config ---- */

static char const*
fake_cfg_str(void* ctx, char const* key)
{
    (void)ctx;
    for( int i = 0; i < g_c.cfg_count; i++ )
        if( strcmp(g_c.cfg[i].k, key) == 0 )
            return g_c.cfg[i].v;
    return "";
}
static int fake_cfg_bool(void* c, char const* k) { return atoi(fake_cfg_str(c, k)) != 0; }
static int fake_cfg_int(void* c, char const* k) { return atoi(fake_cfg_str(c, k)); }

static void
cfg_set(char const* key, char const* value)
{
    for( int i = 0; i < g_c.cfg_count; i++ )
        if( strcmp(g_c.cfg[i].k, key) == 0 )
        {
            snprintf(g_c.cfg[i].v, sizeof(g_c.cfg[i].v), "%s", value);
            return;
        }
    assert(g_c.cfg_count < (int)(sizeof(g_c.cfg) / sizeof(g_c.cfg[0])));
    snprintf(g_c.cfg[g_c.cfg_count].k, sizeof(g_c.cfg[0].k), "%s", key);
    snprintf(g_c.cfg[g_c.cfg_count].v, sizeof(g_c.cfg[0].v), "%s", value);
    g_c.cfg_count++;
}

static void
fake_cfg_set(void* ctx, char const* k, char const* v)
{
    (void)ctx;
    cfg_set(k, v);
}

/* ---- assets: the REAL shipped files ---- */

static struct FakeAsset*
asset_find(char const* name)
{
    for( int i = 0; i < g_c.asset_count; i++ )
        if( strcmp(g_c.asset[i].name, name) == 0 )
            return &g_c.asset[i];
    return NULL;
}

static int
fake_asset_load(void* ctx, char const* name)
{
    char path[512];
    FILE* f;
    struct FakeAsset* a;
    long size;

    (void)ctx;
    if( asset_find(name) )
        return 1;
    assert(g_c.asset_count < FAKE_ASSETS);
    snprintf(path, sizeof(path), "../script/plugins/assets/%s/%s", g_c.asset_dir, name);
    f = fopen(path, "rb");
    if( !f )
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    a = &g_c.asset[g_c.asset_count++];
    snprintf(a->name, sizeof(a->name), "%s", name);
    a->bytes = malloc((size_t)size);
    assert(a->bytes);
    a->size = (int)fread(a->bytes, 1, (size_t)size, f);
    fclose(f);
    return 1;
}

static void const*
fake_asset_data(void* ctx, char const* name, int* out_size)
{
    struct FakeAsset* a;
    (void)ctx;
    a = asset_find(name);
    if( !a )
        return NULL;
    if( out_size )
        *out_size = a->size;
    return a->bytes;
}

static int
fake_asset_save(void* c, char const* n, void const* d, int s)
{ (void)c; (void)n; (void)d; (void)s; return 1; }
static void fake_asset_release(void* c, char const* n) { (void)c; (void)n; }

/* ---- images ---- */

/** Slot 1 is reserved for the composed strip; everything else allocates above it. */
#define STRIP_SLOT 1

static int
image_alloc(int w, int h)
{
    for( int i = STRIP_SLOT + 1; i < FAKE_IMAGES; i++ )
        if( !g_c.image[i].used )
        {
            g_c.image[i].used = 1;
            g_c.image[i].w = w;
            g_c.image[i].h = h;
            free(g_c.image[i].px);
            g_c.image[i].px = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
            assert(g_c.image[i].px);
            return i;
        }
    return -1;
}

/**
 * Decode the real PNG behind `name`, the way the host's image_load does.
 *
 * The composed strips are also "loaded" through here, which is what lets the
 * plugin's own draw path (compose, then image_load, then draw_image) run
 * unchanged -- the compose below simply keeps the pixels under the same name.
 */
static int
fake_image_load(void* ctx, char const* name)
{
    struct FakeAsset* a;
    int w = 0;
    int h = 0;
    uint32_t* px = NULL;
    int slot;

    /* The composed strip is not a file: the plugin composes it and then loads
     * it back by the same name, so that name resolves to the reserved slot. */
    if( g_c.composed[0] && strcmp(g_c.composed, name) == 0 && g_c.image[STRIP_SLOT].used )
        return STRIP_SLOT;

    if( !fake_asset_load(ctx, name) )
        return -1;
    a = asset_find(name);
    if( !a || !PngDecode_Argb(a->bytes, a->size, &w, &h, &px) )
        return -1;
    slot = image_alloc(w, h);
    if( slot < 0 )
    {
        free(px);
        return -1;
    }
    memcpy(g_c.image[slot].px, px, (size_t)w * (size_t)h * sizeof(uint32_t));
    free(px);
    return slot;
}

static int
fake_image_size(void* ctx, int im, int* w, int* h)
{
    (void)ctx;
    if( im <= 0 || im >= FAKE_IMAGES || !g_c.image[im].used )
        return 0;
    if( w )
        *w = g_c.image[im].w;
    if( h )
        *h = g_c.image[im].h;
    return 1;
}

static int
fake_image_pixels(void* ctx, int im, uint32_t* out, int max)
{
    int n;
    (void)ctx;
    if( im <= 0 || im >= FAKE_IMAGES || !g_c.image[im].used )
        return 0;
    n = g_c.image[im].w * g_c.image[im].h;
    if( n > max )
        return 0;
    memcpy(out, g_c.image[im].px, (size_t)n * sizeof(uint32_t));
    return n;
}

static void fake_image_release(void* c, int i) { (void)c; (void)i; }

/** Keep the composed strip; this is the picture the test writes out. */
static int
fake_image_compose(
    void* ctx, char const* name, int w, int h, uint32_t const* argb)
{
    (void)ctx;
    g_c.compose_calls++;
    snprintf(g_c.composed, sizeof(g_c.composed), "%s", name);
    free(g_c.comp_px);
    g_c.comp_px = malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    assert(g_c.comp_px);
    memcpy(g_c.comp_px, argb, (size_t)w * (size_t)h * sizeof(uint32_t));
    g_c.comp_w = w;
    g_c.comp_h = h;
    /* And into the reserved slot, so the plugin's own image_load of the name it
     * just composed finds these pixels. */
    g_c.image[STRIP_SLOT].used = 1;
    g_c.image[STRIP_SLOT].w = w;
    g_c.image[STRIP_SLOT].h = h;
    free(g_c.image[STRIP_SLOT].px);
    g_c.image[STRIP_SLOT].px = malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    assert(g_c.image[STRIP_SLOT].px);
    memcpy(g_c.image[STRIP_SLOT].px, argb, (size_t)w * (size_t)h * sizeof(uint32_t));
    return 1;
}

/**
 * An item icon, as the client would hand one over.
 *
 * Synthetic, and deliberately so: the real icons are a lit render of the
 * objtype's inventory model and nothing outside a booted client can produce
 * one. What this has to be right about is the SHAPE of the answer -- 36x32,
 * bordered, opaque where the item is -- because that is what the grid lays out
 * against. The colour is keyed off the obj id so two different drops are two
 * different swatches and a mis-indexed grid is visible.
 */
static int
fake_obj_image(void* ctx, int obj_id, int count, int style)
{
    int const slot = image_alloc(36, 32);
    uint32_t rgb;

    (void)ctx;
    (void)style;
    g_c.obj_image_calls++;
    if( slot < 0 )
        return -1;
    rgb = 0xFF000000u | (uint32_t)(((obj_id * 2654435761u) >> 8) & 0x00FFFFFFu);
    for( int y = 0; y < 32; y++ )
        for( int x = 0; x < 36; x++ )
        {
            int const edge = x < 3 || y < 3 || x > 32 || y > 28;
            g_c.image[slot].px[y * 36 + x] = edge ? 0u : rgb;
        }
    /* A stack number would be baked in by the client; a bright corner stands
     * in for it so a quantity that fails to reach the icon is visible. */
    if( count > 1 )
        for( int y = 2; y < 8; y++ )
            for( int x = 2; x < 12; x++ )
                g_c.image[slot].px[y * 36 + x] = 0xFF6FE96Fu;
    return slot;
}

/* ---- the client's loot store ----------------------------------------------
 *
 * The plugin no longer reconstructs kills from despawns: it reads the store
 * the game itself keeps (game/rs_loot_store.c), which is the same one the
 * cache's tracker reads. So the harness stands in for the STORE, and the
 * scenario below is the reference capture's own log.
 */

#define FAKE_LOOT_SOURCES 12
#define FAKE_LOOT_ROWS 16

static struct FakeLootSource
{
    int id;
    char name[64];
    int kills;
    int row_count;
    struct ToriRS_LootRow row[FAKE_LOOT_ROWS];
} g_loot[FAKE_LOOT_SOURCES];
static int g_loot_count;
static uint64_t g_loot_revision = 1;

static void
loot_add(char const* name, int kills, int const* obj, int const* qty, int const* val, int n)
{
    struct FakeLootSource* s;
    g_loot_revision++;
    assert(g_loot_count < FAKE_LOOT_SOURCES);
    s = &g_loot[g_loot_count];
    s->id = g_loot_count + 1;
    snprintf(s->name, sizeof(s->name), "%s", name);
    s->kills = kills;
    s->row_count = n < FAKE_LOOT_ROWS ? n : FAKE_LOOT_ROWS;
    for( int i = 0; i < s->row_count; i++ )
    {
        s->row[i].obj_id = obj[i];
        s->row[i].quantity = qty[i];
        s->row[i].value = val[i];
    }
    g_loot_count++;
}

static int
fake_loot_source_next(
    void* ctx, int iter, struct ToriRS_LootSource* out)
{
    int const next = iter + 1;
    (void)ctx;
    if( next < 0 || next >= g_loot_count )
        return -1;
    memset(out, 0, sizeof(*out));
    out->id = g_loot[next].id;
    snprintf(out->name, sizeof(out->name), "%s", g_loot[next].name);
    out->row_count = g_loot[next].row_count;
    out->kill_count = g_loot[next].kills;
    return next;
}

static int
fake_loot_row_next(
    void* ctx, int source_id, int iter,
    struct ToriRS_LootRow* out)
{
    int const next = iter + 1;
    (void)ctx;
    for( int i = 0; i < g_loot_count; i++ )
        if( g_loot[i].id == source_id )
        {
            if( next < 0 || next >= g_loot[i].row_count )
                return -1;
            *out = g_loot[i].row[next];
            return next;
        }
    return -1;
}

static int
fake_obj_info(void* ctx, int obj_id, struct ToriRS_ItemInfo* out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    out->obj_id = obj_id;
    snprintf(out->name, sizeof(out->name), "Item %d", obj_id);
    out->cert_link = -1;
    out->wearpos = -1;
    out->wearpos2 = -1;
    out->wearpos3 = -1;
    out->attack_rate = -1;
    return 1;
}

/* ---- the panel model (only what the strips need) ---- */

struct FakeWidget { char id[32]; int kind; int height; int live; };
static struct FakeWidget g_w[48];
static int g_w_count;
static int g_building;

static bool
fake_panel_request(void* c, struct ToriRS_PanelDescriptor const* d)
{
    (void)c;
    if( !d )
        return false;
    snprintf(g_c.panel_icon, sizeof(g_c.panel_icon), "%s", d->icon_asset ? d->icon_asset : "");
    g_c.panel_preferred_width = d->preferred_width;
    /* panel.request owns this load in the real host. Mirror it so the visual
     * harness verifies the rail asset as well as art loaded by the page. */
    if( d->icon_asset && d->icon_asset[0] )
        (void)fake_asset_load(NULL, d->icon_asset);
    return true;
}

static bool
fake_panel_widget(void* c, int kind, char const* id, char const* label)
{
    (void)c;
    (void)label;
    if( !g_building || g_w_count >= 48 )
        return false;
    for( int i = 0; i < g_w_count; i++ )
        if( strcmp(g_w[i].id, id) == 0 )
            return true;
    snprintf(g_w[g_w_count].id, sizeof(g_w[0].id), "%s", id);
    g_w[g_w_count].kind = kind;
    g_w[g_w_count].live = 1;
    g_w_count++;
    return true;
}

static struct FakeWidget*
w_find(char const* id)
{
    for( int i = 0; i < g_w_count; i++ )
        if( g_w[i].live && strcmp(g_w[i].id, id) == 0 )
            return &g_w[i];
    return NULL;
}

static bool fake_panel_set_text(void* c, char const* i, char const* t)
{ (void)c; (void)t; return w_find(i) != NULL; }
static bool fake_panel_set_value(void* c, char const* i, int v)
{ (void)c; (void)v; return w_find(i) != NULL; }
static bool fake_panel_set_attention(void* c, bool o) { (void)c; (void)o; return true; }
static void fake_panel_clear(void* c) { (void)c; g_w_count = 0; }
static void fake_panel_invalidate(void* c, char const* i) { (void)c; (void)i; }

static bool
fake_panel_set_height(void* c, char const* id, int px)
{
    (void)c;
    {
        struct FakeWidget* w = w_find(id);
        if( !w )
            return false;
        w->height = px;
        return true;
    }
}

static struct ToriRS_ApiV2 g_api;
static struct ToriRS_GameApiV2 g_game_api;
static struct ToriRS_PluginDefV2 const* g_plugin;
static void* g_plugin_state;
static int g_draw_w;
static int g_draw_h;

static void v2_log(struct ToriRS_ApiV2* api, char const* format, ...)
{ (void)api; (void)format; }
static void v2_notify(struct ToriRS_ApiV2* api, char const* text)
{ (void)api; (void)text; }
static uint64_t v2_frame_ms(struct ToriRS_ApiV2* api)
{ (void)api; return fake_frame_ms(NULL); }
static bool v2_local_player(
    struct ToriRS_ApiV2* api, struct ToriRS_PlayerSnapshot* out)
{ (void)api; return fake_local_player(NULL, out) != 0; }
static bool v2_cfg_has(struct ToriRS_ApiV2* api, char const* key)
{ (void)api; return fake_cfg_str(NULL, key)[0] != '\0'; }
static bool v2_cfg_bool(struct ToriRS_ApiV2* api, char const* key, bool* out)
{ (void)api; *out = fake_cfg_bool(NULL, key) != 0; return true; }
static bool v2_cfg_int(struct ToriRS_ApiV2* api, char const* key, int* out)
{ (void)api; *out = fake_cfg_int(NULL, key); return true; }
static bool v2_cfg_string(
    struct ToriRS_ApiV2* api, char const* key, char const** out)
{ (void)api; *out = fake_cfg_str(NULL, key); return true; }
static enum ToriRS_Result v2_cfg_set(
    struct ToriRS_ApiV2* api, char const* key, char const* value)
{
    (void)api;
    fake_cfg_set(NULL, key, value);
    if( g_plugin && g_plugin_state && g_plugin->callbacks.on_config_changed )
        g_plugin->callbacks.on_config_changed(&g_api, g_plugin_state, key);
    return TORIRS_RESULT_OK;
}
static enum ToriRS_AssetState v2_asset_request(
    struct ToriRS_ApiV2* api, char const* name)
{ (void)api; return fake_asset_load(NULL, name) ? TORIRS_ASSET_READY : TORIRS_ASSET_MISSING; }
static bool v2_asset_bytes(
    struct ToriRS_ApiV2* api, char const* name, void const** out, size_t* size)
{
    int n = 0;
    (void)api;
    *out = fake_asset_data(NULL, name, &n);
    *size = n > 0 ? (size_t)n : 0;
    return *out != NULL;
}
static enum ToriRS_Result v2_asset_save(
    struct ToriRS_ApiV2* api, char const* name, void const* data, size_t size)
{ (void)api; return fake_asset_save(NULL, name, data, (int)size) ? TORIRS_RESULT_OK : TORIRS_RESULT_ERROR; }
static void v2_asset_release(struct ToriRS_ApiV2* api, char const* name)
{ (void)api; fake_asset_release(NULL, name); }
static enum ToriRS_AssetState v2_image(
    struct ToriRS_ApiV2* api, char const* name, struct ToriRS_ImageRef* out)
{
    int const image = fake_image_load(NULL, name);
    (void)api;
    out->value = image >= 0 ? (uint32_t)image + 1u : 0;
    return image >= 0 ? TORIRS_ASSET_READY : TORIRS_ASSET_MISSING;
}
static bool v2_image_size(
    struct ToriRS_ApiV2* api, struct ToriRS_ImageRef image, int* w, int* h)
{ (void)api; return image.value && fake_image_size(NULL, (int)image.value - 1, w, h) != 0; }
static bool v2_image_pixels(
    struct ToriRS_ApiV2* api, struct ToriRS_ImageRef image,
    uint32_t* out, size_t capacity, size_t* count)
{
    int const n = image.value
                      ? fake_image_pixels(NULL, (int)image.value - 1, out, (int)capacity)
                      : 0;
    (void)api;
    *count = n > 0 ? (size_t)n : 0;
    return n > 0;
}
static enum ToriRS_AssetState v2_image_compose(
    struct ToriRS_ApiV2* api, char const* name, int w, int h,
    uint32_t const* pixels, struct ToriRS_ImageRef* out)
{
    int const image = fake_image_compose(NULL, name, w, h, pixels);
    (void)api;
    out->value = image >= 0 ? (uint32_t)image + 1u : 0;
    return image >= 0 ? TORIRS_ASSET_READY : TORIRS_ASSET_ERROR;
}
static void v2_image_release(
    struct ToriRS_ApiV2* api, struct ToriRS_ImageRef image)
{ (void)api; fake_image_release(NULL, image.value ? (int)image.value - 1 : -1); }
static bool v2_skill(
    struct ToriRS_ApiV2* api, int skill, struct ToriRS_SkillSnapshot* out)
{
    (void)api;
    if( skill < 0 || skill >= FAKE_SKILLS ) return false;
    memset(out, 0, sizeof(*out));
    out->struct_size = sizeof(*out);
    out->index = skill;
    snprintf(out->name, sizeof(out->name), "%s", SKILL_NAME[skill]);
    out->current_level = g_c.level[skill];
    out->base_level = g_c.level[skill];
    out->xp = g_c.xp[skill];
    out->level_xp = LEVEL_XP[g_c.level[skill]];
    out->next_level_xp = g_c.level[skill] >= 99 ? 0 : LEVEL_XP[g_c.level[skill] + 1];
    return true;
}
static bool v2_item_info(
    struct ToriRS_ApiV2* api, int obj, struct ToriRS_ItemInfo* out)
{ (void)api; return fake_obj_info(NULL, obj, out) != 0; }
static enum ToriRS_AssetState v2_item_image(
    struct ToriRS_ApiV2* api, int obj, int count, int style,
    struct ToriRS_ImageRef* out)
{
    int const image = fake_obj_image(NULL, obj, count, style);
    (void)api;
    out->value = image >= 0 ? (uint32_t)image + 1u : 0;
    return image >= 0 ? TORIRS_ASSET_READY : TORIRS_ASSET_BUDGET;
}
static int v2_loot_source_next(
    struct ToriRS_ApiV2* api, int iter, struct ToriRS_LootSource* out)
{ (void)api; return fake_loot_source_next(NULL, iter, out); }
static int v2_loot_row_next(
    struct ToriRS_ApiV2* api, int source, int iter, struct ToriRS_LootRow* out)
{ (void)api; return fake_loot_row_next(NULL, source, iter, out); }
static uint64_t v2_loot_revision(struct ToriRS_ApiV2* api)
{ (void)api; return g_loot_revision; }
static bool v2_loot_source_clear(struct ToriRS_ApiV2* api, int source)
{ (void)api; (void)source; return false; }

static enum ToriRS_Result v2_panel_request(
    struct ToriRS_ApiV2* api, struct ToriRS_PanelDescriptor const* desc)
{ (void)api; return fake_panel_request(NULL, desc) ? TORIRS_RESULT_OK : TORIRS_RESULT_ERROR; }
static void v2_panel_invalidate(struct ToriRS_ApiV2* api)
{ (void)api; fake_panel_clear(NULL); }
static void v2_panel_attention(struct ToriRS_ApiV2* api, bool wanted)
{ (void)api; (void)fake_panel_set_attention(NULL, wanted); }
static enum ToriRS_Result v2_panel_text(
    struct ToriRS_ApiV2* api, char const* id, char const* text)
{ (void)api; return fake_panel_set_text(NULL, id, text) ? TORIRS_RESULT_OK : TORIRS_RESULT_NOT_FOUND; }
static enum ToriRS_Result v2_panel_value(
    struct ToriRS_ApiV2* api, char const* id, int value)
{ (void)api; return fake_panel_set_value(NULL, id, value) ? TORIRS_RESULT_OK : TORIRS_RESULT_NOT_FOUND; }
static enum ToriRS_Result v2_panel_height(
    struct ToriRS_ApiV2* api, char const* id, int height)
{ (void)api; return fake_panel_set_height(NULL, id, height) ? TORIRS_RESULT_OK : TORIRS_RESULT_NOT_FOUND; }
static void v2_panel_redraw(struct ToriRS_ApiV2* api, char const* id)
{ (void)api; fake_panel_invalidate(NULL, id); }
static void v2_build_heading(struct ToriRS_PanelBuilder* panel, char const* text)
{ (void)panel; (void)text; }
static void v2_build_paragraph(struct ToriRS_PanelBuilder* panel, char const* text)
{ (void)panel; (void)text; }
static void v2_build_toggle(
    struct ToriRS_PanelBuilder* panel, char const* id, char const* label, bool value)
{ (void)panel; (void)value; (void)fake_panel_widget(NULL, TORIRS_PANEL_TOGGLE, id, label); }
static void v2_build_select(
    struct ToriRS_PanelBuilder* panel, char const* id, char const* label,
    char const* value, struct ToriRS_SelectOption const* options, int count)
{ (void)panel; (void)value; (void)options; (void)count; (void)fake_panel_widget(NULL, TORIRS_PANEL_SELECT, id, label); }
static void v2_build_button(
    struct ToriRS_PanelBuilder* panel, char const* id, char const* label, bool enabled)
{ (void)panel; (void)enabled; (void)fake_panel_widget(NULL, TORIRS_PANEL_BUTTON, id, label); }
static void v2_build_custom(
    struct ToriRS_PanelBuilder* panel, char const* id, int height)
{ (void)panel; (void)fake_panel_widget(NULL, TORIRS_PANEL_CUSTOM, id, ""); (void)fake_panel_set_height(NULL, id, height); }
static void v2_build_label(
    struct ToriRS_PanelBuilder* panel, char const* id, char const* text)
{ (void)panel; (void)fake_panel_widget(NULL, TORIRS_PANEL_LABEL, id, text); }
static void v2_build_key_value(
    struct ToriRS_PanelBuilder* panel, char const* id, char const* label, char const* value)
{ (void)panel; (void)value; (void)fake_panel_widget(NULL, TORIRS_PANEL_KEY_VALUE, id, label); }
static enum ToriRS_Result v2_build_node(
    struct ToriRS_PanelBuilder* panel, struct ToriRS_PanelNode const* node)
{ (void)panel; return fake_panel_widget(NULL, TORIRS_PANEL_HEADING, node->id, node->text) ? TORIRS_RESULT_OK : TORIRS_RESULT_BUDGET; }
static void v2_draw_image(
    struct ToriRS_DrawBuilder* draw, struct ToriRS_ImageRef image, int x, int y, int alpha)
{ (void)draw; (void)image; (void)x; (void)y; (void)alpha; }
static bool v2_draw_context(
    struct ToriRS_DrawBuilder* draw, struct ToriRS_DrawContext* out)
{
    (void)draw;
    out->bounds = (struct ToriRS_Rect){ 0, 0, g_draw_w, g_draw_h };
    out->clip = out->bounds;
    return true;
}

static void
api_init(void)
{
    memset(&g_api, 0, sizeof(g_api));
    memset(&g_game_api, 0, sizeof(g_game_api));
    g_api.struct_size = sizeof(g_api);
    g_api.major_version = TORIRS_PLUGIN_API_V2_MAJOR;
    g_api.minor_version = TORIRS_PLUGIN_API_V2_MINOR;
    g_api.core.log = v2_log;
    g_api.core.notify = v2_notify;
    g_api.core.frame_ms = v2_frame_ms;
    g_api.config.has = v2_cfg_has;
    g_api.config.get_bool = v2_cfg_bool;
    g_api.config.get_int = v2_cfg_int;
    g_api.config.get_string = v2_cfg_string;
    g_api.config.set = v2_cfg_set;
    g_api.world.local_player = v2_local_player;
    g_api.assets.request = v2_asset_request;
    g_api.assets.bytes = v2_asset_bytes;
    g_api.assets.save = v2_asset_save;
    g_api.assets.release = v2_asset_release;
    g_api.assets.image = v2_image;
    g_api.assets.image_size = v2_image_size;
    g_api.assets.image_pixels = v2_image_pixels;
    g_api.assets.image_compose = v2_image_compose;
    g_api.assets.image_release = v2_image_release;
    g_api.panel.request = v2_panel_request;
    g_api.panel.invalidate = v2_panel_invalidate;
    g_api.panel.attention = v2_panel_attention;
    g_api.panel.set_text = v2_panel_text;
    g_api.panel.set_value = v2_panel_value;
    g_api.panel.set_height = v2_panel_height;
    g_api.panel.redraw = v2_panel_redraw;
    g_game_api.struct_size = sizeof(g_game_api);
    g_game_api.skill = v2_skill;
    g_game_api.item_info = v2_item_info;
    g_game_api.item_image = v2_item_image;
    g_game_api.loot_source_next = v2_loot_source_next;
    g_game_api.loot_row_next = v2_loot_row_next;
    g_game_api.loot_revision = v2_loot_revision;
    g_game_api.loot_source_clear = v2_loot_source_clear;
    g_api.game = &g_game_api;
}

/* ---------------------------------------------------------------- the png */

static void
write_png(char const* path, int w, int h, uint32_t const* argb)
{
    unsigned char* raw;
    mz_ulong raw_size = (unsigned long)(w * 4 + 1) * (unsigned long)h;
    mz_ulong comp_size;
    unsigned char* comp;
    unsigned char head[8 + 25 + 12];
    FILE* f;
    int at = 0;

    raw = malloc(raw_size);
    assert(raw);
    for( int y = 0; y < h; y++ )
    {
        raw[at++] = 0;
        for( int x = 0; x < w; x++ )
        {
            uint32_t const p = argb[y * w + x];
            raw[at++] = (unsigned char)(p >> 16);
            raw[at++] = (unsigned char)(p >> 8);
            raw[at++] = (unsigned char)p;
            raw[at++] = (unsigned char)(p >> 24);
        }
    }
    comp_size = mz_compressBound(raw_size);
    comp = malloc(comp_size);
    assert(comp);
    mz_compress2(comp, &comp_size, raw, raw_size, 9);

    f = fopen(path, "wb");
    if( !f )
    {
        free(raw);
        free(comp);
        return;
    }
    {
        static unsigned char const sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
        unsigned char ihdr[25];
        unsigned long crc;
        int n = 0;
        memcpy(head, sig, 8);
        fwrite(head, 1, 8, f);

        ihdr[n++] = 0; ihdr[n++] = 0; ihdr[n++] = 0; ihdr[n++] = 13;
        ihdr[n++] = 'I'; ihdr[n++] = 'H'; ihdr[n++] = 'D'; ihdr[n++] = 'R';
        ihdr[n++] = (unsigned char)(w >> 24); ihdr[n++] = (unsigned char)(w >> 16);
        ihdr[n++] = (unsigned char)(w >> 8);  ihdr[n++] = (unsigned char)w;
        ihdr[n++] = (unsigned char)(h >> 24); ihdr[n++] = (unsigned char)(h >> 16);
        ihdr[n++] = (unsigned char)(h >> 8);  ihdr[n++] = (unsigned char)h;
        ihdr[n++] = 8; ihdr[n++] = 6; ihdr[n++] = 0; ihdr[n++] = 0; ihdr[n++] = 0;
        crc = mz_crc32(MZ_CRC32_INIT, ihdr + 4, 17);
        ihdr[n++] = (unsigned char)(crc >> 24); ihdr[n++] = (unsigned char)(crc >> 16);
        ihdr[n++] = (unsigned char)(crc >> 8);  ihdr[n++] = (unsigned char)crc;
        fwrite(ihdr, 1, (size_t)n, f);

        {
            unsigned char len[4] = {
                (unsigned char)(comp_size >> 24), (unsigned char)(comp_size >> 16),
                (unsigned char)(comp_size >> 8), (unsigned char)comp_size };
            unsigned char* chunk = malloc(comp_size + 4);
            assert(chunk);
            memcpy(chunk, "IDAT", 4);
            memcpy(chunk + 4, comp, comp_size);
            crc = mz_crc32(MZ_CRC32_INIT, chunk, comp_size + 4);
            fwrite(len, 1, 4, f);
            fwrite(chunk, 1, comp_size + 4, f);
            {
                unsigned char c4[4] = {
                    (unsigned char)(crc >> 24), (unsigned char)(crc >> 16),
                    (unsigned char)(crc >> 8), (unsigned char)crc };
                fwrite(c4, 1, 4, f);
            }
            free(chunk);
        }
        {
            static unsigned char const iend[12] = {
                0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82 };
            fwrite(iend, 1, 12, f);
        }
    }
    fclose(f);
    free(raw);
    free(comp);
}

/* -------------------------------------------------------------- driving */

extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_XP_TRACKER;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_LOOT_TRACKER;

static void
plugin_prepare(struct ToriRS_PluginDefV2 const* definition)
{
    assert(definition);
    assert(!g_plugin_state);
    g_plugin = definition;
    g_plugin_state = calloc(1, definition->state_size);
    assert(g_plugin_state);
}

static void
dispatch_start(void)
{
    assert(g_plugin && g_plugin_state);
    if( g_plugin->callbacks.on_start )
        g_plugin->callbacks.on_start(&g_api, g_plugin_state);
}

static void
dispatch_stop(void)
{
    assert(g_plugin);
    if( g_plugin_state && g_plugin->callbacks.on_stop )
        g_plugin->callbacks.on_stop(&g_api, g_plugin_state);
    free(g_plugin_state);
    g_plugin_state = NULL;
    g_plugin = NULL;
}

static void
dispatch_logic_tick(struct ToriRS_TickEvent const* event)
{
    assert(g_plugin && g_plugin_state && event);
    if( g_plugin->callbacks.on_logic_tick )
        g_plugin->callbacks.on_logic_tick(&g_api, g_plugin_state, event);
}

static void
dispatch_panel_layout(struct ToriRS_PanelLayoutEvent const* event)
{
    assert(g_plugin && g_plugin_state && event);
    if( g_plugin->callbacks.on_ui_layout )
        g_plugin->callbacks.on_ui_layout(&g_api, g_plugin_state, event);
}

static void
panel_build(void)
{
    struct ToriRS_PanelLayoutEvent lay;
    struct ToriRS_PanelBuilder panel = {
        .struct_size = sizeof(panel),
        .heading = v2_build_heading,
        .paragraph = v2_build_paragraph,
        .toggle = v2_build_toggle,
        .select = v2_build_select,
        .button = v2_build_button,
        .custom = v2_build_custom,
        .label = v2_build_label,
        .key_value = v2_build_key_value,
        .node = v2_build_node,
    };

    g_w_count = 0;
    g_building = 1;
    if( g_plugin->callbacks.on_ui_build )
        g_plugin->callbacks.on_ui_build(
            &g_api, g_plugin_state, &panel, TORIRS_PANEL_VIEW_PAGE);
    g_building = 0;

    memset(&lay, 0, sizeof(lay));
    lay.width = 264;
    lay.height = 491;
    lay.scale_milli = 1000;
    lay.size_class = TORIRS_PANEL_SIZE_MEDIUM;
    lay.visible = true;
    lay.game_visible = true;
    lay.selection_generation = 1;
    dispatch_panel_layout(&lay);
}

static void
tick(uint64_t ms)
{
    struct ToriRS_TickEvent ev;
    memset(&ev, 0, sizeof(ev));
    g_c.now_ms += ms;
    dispatch_logic_tick(&ev);
}

/** Run one draw pass over the named custom well and keep what it composed. */
static void
draw_well(char const* id, int width)
{
    struct ToriRS_DrawBuilder draw = {
        .struct_size = sizeof(draw),
        .image = v2_draw_image,
        .context = v2_draw_context,
    };
    g_draw_w = width;
    g_draw_h = 4096;
    if( g_plugin->callbacks.on_ui_draw )
        g_plugin->callbacks.on_ui_draw(&g_api, g_plugin_state, id, &draw);
}

static void
activate_well(char const* id, int x, int y)
{
    struct ToriRS_PanelActionEvent event;

    memset(&event, 0, sizeof(event));
    event.id = id;
    event.action = TORIRS_PANEL_ACTION_ACTIVATE;
    event.x = x;
    event.y = y;
    event.selection_generation = 1;
    if( g_plugin->callbacks.on_ui_action )
        g_plugin->callbacks.on_ui_action(&g_api, g_plugin_state, &event);
}

static void
reset(char const* asset_dir)
{
    if( g_plugin_state ) dispatch_stop();
    for( int i = 0; i < g_c.asset_count; i++ )
        free(g_c.asset[i].bytes);
    for( int i = 0; i < FAKE_IMAGES; i++ )
    {
        free(g_c.image[i].px);
        g_c.image[i].px = NULL;
        g_c.image[i].used = 0;
    }
    free(g_c.comp_px);
    memset(&g_c, 0, sizeof(g_c));
    g_w_count = 0;
    g_loot_revision = 1;
    g_c.asset_dir = asset_dir;
    g_c.now_ms = 1000000;
}

/* ------------------------------------------------------------- the cases */

static int
dominant_colour_in_rect(
    uint32_t const* pixels,
    int width,
    int height,
    int x0,
    int y0,
    int x1,
    int y1,
    int channel)
{
    if( !pixels || width <= 0 || height <= 0 )
        return 0;
    if( x0 < 0 ) x0 = 0;
    if( y0 < 0 ) y0 = 0;
    if( x1 > width ) x1 = width;
    if( y1 > height ) y1 = height;
    for( int y = y0; y < y1; y++ )
        for( int x = x0; x < x1; x++ )
        {
            uint32_t const pixel = pixels[y * width + x];
            int const rgb[3] = {
                (int)((pixel >> 16) & 0xff),
                (int)((pixel >> 8) & 0xff),
                (int)(pixel & 0xff),
            };
            int const other0 = (channel + 1) % 3;
            int const other1 = (channel + 2) % 3;
            if( (pixel >> 24) != 0 && rgb[channel] > rgb[other0] + 20 &&
                rgb[channel] > rgb[other1] + 20 )
                return 1;
        }
    return 0;
}

static uint64_t
argb_hash(uint32_t const* pixels, int count)
{
    uint64_t hash = 14695981039346656037ULL;

    assert(pixels || count == 0);
    for( int i = 0; i < count; i++ )
    {
        hash ^= pixels[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

/** Pin native cache art by decoded pixels, independent of PNG compression. */
static void
check_png_contract(
    char const* name,
    int expected_width,
    int expected_height,
    uint64_t expected_hash)
{
    struct FakeAsset* asset;
    uint32_t* pixels = NULL;
    uint64_t hash;
    int width = 0;
    int height = 0;
    bool decoded;

    CHECK(fake_asset_load(NULL, name), "%s is shipped", name);
    asset = asset_find(name);
    if( !asset )
        return;
    decoded = PngDecode_Argb(asset->bytes, asset->size, &width, &height, &pixels);
    CHECK(decoded, "%s decodes", name);
    if( !decoded )
        return;
    CHECK(
        width == expected_width && height == expected_height,
        "%s keeps the cache's %dx%d logical canvas (got %dx%d)",
        name, expected_width, expected_height, width, height);
    hash = argb_hash(pixels, width * height);
    CHECK(
        hash == expected_hash,
        "%s pixels are the exact osrs239 cache sprite (got %016llx)",
        name, (unsigned long long)hash);
    free(pixels);
}

/** Compare only a sprite's fully opaque ink. Transparent pixels correctly
 * reveal the totals plate beneath them and therefore cannot equal the PNG's
 * zero words after composition. */
static bool
sprite_opaque_matches(char const* name, int dst_x, int dst_y)
{
    struct FakeAsset* asset;
    uint32_t* pixels = NULL;
    int width = 0;
    int height = 0;
    int opaque = 0;

    if( !fake_asset_load(NULL, name) )
        return false;
    asset = asset_find(name);
    if( !asset ||
        !PngDecode_Argb(asset->bytes, asset->size, &width, &height, &pixels) )
        return false;
    for( int y = 0; y < height; y++ )
        for( int x = 0; x < width; x++ )
        {
            uint32_t const p = pixels[y * width + x];
            if( (p >> 24) != 0xffu )
                continue;
            opaque++;
            if( !g_c.comp_px || dst_x + x < 0 || dst_y + y < 0 ||
                dst_x + x >= g_c.comp_w || dst_y + y >= g_c.comp_h ||
                g_c.comp_px[(dst_y + y) * g_c.comp_w + dst_x + x] != p )
            {
                free(pixels);
                return false;
            }
        }
    free(pixels);
    return opaque > 0;
}

static uint32_t
png_pixel(char const* name, int x, int y)
{
    struct FakeAsset* asset;
    uint32_t* pixels = NULL;
    uint32_t answer = 0;
    int width = 0;
    int height = 0;

    if( !fake_asset_load(NULL, name) )
        return 0;
    asset = asset_find(name);
    if( asset && PngDecode_Argb(asset->bytes, asset->size, &width, &height, &pixels) &&
        x >= 0 && y >= 0 && x < width && y < height )
        answer = pixels[y * width + x];
    free(pixels);
    return answer;
}

/**
 * The XP strip against the reference capture: attack at virtual level 120 and
 * hitpoints at virtual level 110, gaining 20 and 7 XP over one minute.
 */
static void
render_xp(void)
{
    char const* path = getenv("TRACKER_RENDER_XP_PNG");
    int const ATT = 0, HP = 3;
    /* Final XP values spell the reference's 84.82% / 9.16%, with remaining
     * values that compact to 1,647.1k and 3,662.5k respectively. */
    int const ATT_FINAL_XP = 113479738;
    int const HP_FINAL_XP = 39107301;

    reset("xp-tracker");
    cfg_set("save_state", "0");
    cfg_set("hide_maxed", "0");
    cfg_set("pause_on_logout", "1");
    cfg_set("pause_skill_after", "0");
    cfg_set("reset_rate_after", "0");
    cfg_set("label_top_left", "XP/hr");
    cfg_set("label_top_right", "XP Gained");
    cfg_set("label_bottom_left", "XP Left");
    cfg_set("label_bottom_right", "Actions");

    /* The protocol's real base-level field stops at 99. The CS2 derives the
     * displayed 120/110 from XP, which is the behavior under test. */
    g_c.level[ATT] = 99; g_c.xp[ATT] = ATT_FINAL_XP - 20;
    g_c.level[HP] = 99;  g_c.xp[HP] = HP_FINAL_XP - 7;

    plugin_prepare(&TORIRS_PLUGIN_XP_TRACKER);
    dispatch_start();
    panel_build();
    tick(20);

    g_c.xp[ATT] = ATT_FINAL_XP;
    g_c.xp[HP] = HP_FINAL_XP;
    tick(60000);
    panel_build();
    draw_well("boxes", 264);

    CHECK(g_c.comp_px != NULL, "the xp strip composed");
    CHECK(g_c.comp_w == 264, "at the well's width (got %d)", g_c.comp_w);
    CHECK(g_c.comp_h == 3 * 50, "and holds overview plus two boxes (got %d)", g_c.comp_h);
    CHECK(
        strcmp(g_c.panel_icon, "panel_icon.png") == 0,
        "the XP rail requests its dedicated popout icon (got '%s')", g_c.panel_icon);
    CHECK(
        asset_find("overview_icon.png") != NULL,
        "the overall card loads its distinct CS2 tracker icon");
    check_png_contract("panel_icon.png", 30, 30, 0x0935136D8AFD5058ULL);
    check_png_contract("overview_icon.png", 25, 25, 0xEB43A3FF292CB579ULL);
    CHECK(
        dominant_colour_in_rect(g_c.comp_px, g_c.comp_w, g_c.comp_h, 0, 0, 31, 48, 0) &&
            dominant_colour_in_rect(g_c.comp_px, g_c.comp_w, g_c.comp_h, 0, 0, 31, 48, 1) &&
            dominant_colour_in_rect(g_c.comp_px, g_c.comp_w, g_c.comp_h, 0, 0, 31, 48, 2),
        "the overview card draws the red/green/blue stats-bars icon");
    CHECK(
        g_c.comp_px[40 * g_c.comp_w + 250] == 0x7F000000u &&
            g_c.comp_px[48 * g_c.comp_w + 250] == 0,
        "cc_settrans(128) stays alpha 127 over the chrome, with a transparent row gap");
    CHECK(
        g_c.comp_px[50 * g_c.comp_w] == 0xFF000000u,
        "the skill box outline is the CS2 rectangle's default opaque black");
    CHECK(
        dominant_colour_in_rect(g_c.comp_px, g_c.comp_w, g_c.comp_h, 1, 77, 263, 92, 1) &&
            dominant_colour_in_rect(g_c.comp_px, g_c.comp_w, g_c.comp_h, 1, 127, 263, 142, 1),
        "99-cap snapshots continue as green virtual-level 120 and 110 bars");
    CHECK(
        argb_hash(g_c.comp_px, g_c.comp_w * g_c.comp_h) == 0x11BCF2CBC800F2F1ULL,
        "the reference strip retains its exact totals, virtual labels, bars, and alpha");
    {
        uint64_t const before = argb_hash(g_c.comp_px, g_c.comp_w * g_c.comp_h);
        int const composed = g_c.compose_calls;
        activate_well("boxes", 10, 51);
        panel_build();
        draw_well("boxes", 264);
        CHECK(
            g_c.compose_calls == composed &&
                argb_hash(g_c.comp_px, g_c.comp_w * g_c.comp_h) == before &&
                g_c.comp_px[50 * g_c.comp_w] == 0xFF000000u,
            "opening a detail row does not invent a selected border colour");
    }
    if( g_c.comp_px )
        write_png(path ? path : "build/xp_strip.png", g_c.comp_w, g_c.comp_h, g_c.comp_px);
    printf("xp strip: %dx%d -> %s\n", g_c.comp_w, g_c.comp_h,
        path ? path : "build/xp_strip.png");
}

/**
 * A configured TTL is a clock even while every event-derived value is still.
 * During the first minute XP/hr is intentionally held at its 60-second floor,
 * so two one-second frames below differ only in the rendered TTL. The compose
 * cache must observe that difference instead of reusing the first picture.
 */
static void
test_xp_ttl_advances_inside_rate_floor(void)
{
    int const WC = 8;
    int composed;

    reset("xp-tracker");
    cfg_set("save_state", "0");
    cfg_set("hide_maxed", "0");
    cfg_set("pause_on_logout", "1");
    cfg_set("pause_skill_after", "0");
    cfg_set("reset_rate_after", "0");
    cfg_set("label_top_left", "TTL");
    cfg_set("label_top_right", "XP Gained");
    cfg_set("label_bottom_left", "XP Left");
    cfg_set("label_bottom_right", "Actions");
    g_c.level[WC] = 40;
    g_c.xp[WC] = LEVEL_XP[40];

    plugin_prepare(&TORIRS_PLUGIN_XP_TRACKER);
    dispatch_start();
    panel_build();
    tick(20); /* seed the first reading */
    g_c.xp[WC] += 100;
    tick(1000); /* one action and one elapsed second */
    panel_build();
    draw_well("boxes", 264);
    composed = g_c.compose_calls;

    tick(1000); /* no XP/action change; XP/hr is still on the same floor */
    draw_well("boxes", 264);
    CHECK(
        g_c.compose_calls == composed + 1,
        "a TTL slot recomposes as elapsed seconds advance inside the XP/hr floor");
}

/** The loot strip, seeded with the reference capture's own log. */
static void
render_loot(void)
{
    char const* path = getenv("TRACKER_RENDER_LOOT_PNG");
    static int const guard_o[] = { 995, 526, 1291, 1381, 561, 562, 1163, 2353, 314, 1265 };
    static int const guard_q[] = { 198, 49, 3, 1, 2, 16, 18, 23, 12, 3 };
    static int const guard_v[] = { 1, 60, 180, 40, 25, 110, 90, 210, 15, 70 };
    static int const cerb_o[] = { 13227, 565, 561, 3049, 2434, 385, 1149, 1215, 892,
                                  560, 4151, 11235, 6685 };
    static int const cerb_q[] = { 15, 120, 20, 600, 60, 200, 20, 1, 18, 3, 1, 2, 19 };
    static int const cerb_v[] = { 1200, 300, 110, 90, 640, 420, 1500, 9000, 30, 250,
                                  180000, 240000, 3400 };
    static int const ever_o[] = { 1511, 1521, 1519 };
    static int const ever_q[] = { 1, 1, 1 };
    static int const ever_v[] = { 40, 120, 113 };
    static int const wolf_o[] = { 526, 995, 1275 };
    static int const wolf_q[] = { 3, 640, 1 };
    static int const wolf_v[] = { 60, 1, 560 };
    static int const gob_o[] = { 526, 995, 1381 };
    static int const gob_q[] = { 1, 12, 1 };
    static int const gob_v[] = { 60, 1, 52 };
    static int const ork_o[] = { 526, 995 };
    static int const ork_q[] = { 4, 114 };
    static int const ork_v[] = { 60, 1 };
    static int const vamp_o[] = { 526, 995 };
    static int const vamp_q[] = { 3, 28 };
    static int const vamp_v[] = { 60, 1 };
    static int const hell_o[] = { 526 };
    static int const hell_q[] = { 1 };
    static int const hell_v[] = { 0 };
    static int const imp_o[] = { 995, 1957 };
    static int const imp_q[] = { 2, 1 };
    static int const imp_v[] = { 1, 40 };
    static int const sara_o[] = { 526, 995 };
    static int const sara_q[] = { 2, 0 };
    static int const sara_v[] = { 0, 1 };

    reset("loot-tracker");
    cfg_set("remember_loot", "0");
    cfg_set("price_source", "Cache value");
    cfg_set("kill_chat_message", "0");
    cfg_set("chat_value_threshold", "0");
    cfg_set("ignored_items", "");
    cfg_set("ignored_sources", "");

    g_loot_count = 0;
    loot_add("Guard", 49, guard_o, guard_q, guard_v, 10);
    loot_add("Cerberus", 19, cerb_o, cerb_q, cerb_v, 13);
    loot_add("Evergreen", 1, ever_o, ever_q, ever_v, 3);
    loot_add("Werewolf", 3, wolf_o, wolf_q, wolf_v, 3);
    loot_add("Goblin", 1, gob_o, gob_q, gob_v, 3);
    loot_add("Ork", 4, ork_o, ork_q, ork_v, 2);
    loot_add("Feral Vampyre", 3, vamp_o, vamp_q, vamp_v, 2);
    loot_add("Hellhound", 1, hell_o, hell_q, hell_v, 1);
    loot_add("Imp", 1, imp_o, imp_q, imp_v, 2);
    loot_add("Saradomin priest", 2, sara_o, sara_q, sara_v, 2);

    plugin_prepare(&TORIRS_PLUGIN_LOOT_TRACKER);
    dispatch_start();
    panel_build();
    tick(1000);
    panel_build();
    draw_well("strip", 264);

    CHECK(g_c.comp_px != NULL, "the loot strip composed");
    CHECK(g_c.comp_w == 264, "at the well's width (got %d)", g_c.comp_w);
    CHECK(
        g_c.comp_h == 972,
        "source headers and their thinbox bodies keep the native pitch (got %d)",
        g_c.comp_h);
    CHECK(g_c.obj_image_calls > 0, "loot cells request and draw item sprites");
    CHECK(
        strcmp(g_c.panel_icon, "panel_icon.png") == 0,
        "the Loot rail requests its dedicated CS2 popout icon (got '%s')",
        g_c.panel_icon);
    check_png_contract("panel_icon.png", 30, 30, 0x7DC96DACDD9F74B6ULL);
    CHECK(
        asset_find("btn_dropview.png") && asset_find("btn_sourceview.png") &&
            asset_find("btn_alch.png") && asset_find("btn_cache.png") &&
            asset_find("btn_collapse.png") && asset_find("btn_expand.png") &&
            asset_find("btn_ignored.png") && asset_find("btn_ignored_hide.png"),
        "the totals card loads both state faces for all four action icons");
    CHECK(
        g_c.comp_px[44 * g_c.comp_w] == 0xFF0E0E0Cu &&
            g_c.comp_px[45 * g_c.comp_w + 1] == 0xFF474745u &&
            g_c.comp_px[162 * g_c.comp_w] == 0xFF0E0E0Cu,
        "source headers and item bodies retain the two-colour CS2 thinbox outlines");
    CHECK(
        (g_c.comp_px[83 * g_c.comp_w + 49] >> 24) == 0 &&
            (g_c.comp_px[83 * g_c.comp_w + 59] >> 24) != 0,
        "script3042 leaves the first inter-cell gap and starts column two at x=58");
    CHECK(
        argb_hash(g_c.comp_px, g_c.comp_w * g_c.comp_h) == 0x862D6CF5E77B88D0ULL,
        "the Loot Tools reference strip retains its exact plates, text, grid, and controls "
        "(got %016llx)",
        (unsigned long long)argb_hash(g_c.comp_px, g_c.comp_w * g_c.comp_h));
    if( g_c.comp_px )
        write_png(path ? path : "build/loot_strip.png", g_c.comp_w, g_c.comp_h, g_c.comp_px);
    printf("loot strip: %dx%d -> %s\n", g_c.comp_w, g_c.comp_h,
        path ? path : "build/loot_strip.png");
}

/** Every fixed overview control changes both behavior and its cache-authored
 * face. This is the part a still image cannot cover. */
static void
test_loot_stateful_controls(void)
{
    static int const obj[] = { 526 };
    static int const qty[] = { 1 };
    static int const val[] = { 60 };
    int const ignored_x = 264 - 4 - 30;
    int const collapse_x = 264 - 35 - 30;
    int const value_x = 264 - 66 - 30;

    reset("loot-tracker");
    cfg_set("price_source", "Cache value");
    cfg_set("kill_chat_message", "0");
    cfg_set("chat_value_threshold", "0");
    cfg_set("ignored_items", "");
    cfg_set("ignored_sources", "");
    g_loot_count = 0;
    loot_add("Goblin", 1, obj, qty, val, 1);

    plugin_prepare(&TORIRS_PLUGIN_LOOT_TRACKER);
    dispatch_start();
    panel_build();
    tick(1000);
    panel_build();
    draw_well("strip", 264);
    CHECK(
        sprite_opaque_matches("btn_dropview.png", 4, 6) &&
            sprite_opaque_matches("btn_alch.png", value_x, 6) &&
            sprite_opaque_matches("btn_collapse.png", collapse_x, 6) &&
            sprite_opaque_matches("btn_ignored.png", ignored_x, 6),
        "the initial totals controls show the actions they will perform");

    activate_well("strip", collapse_x + 5, 10);
    panel_build();
    draw_well("strip", 264);
    CHECK(
        g_c.comp_h == 81 && sprite_opaque_matches("btn_expand.png", collapse_x, 6),
        "Collapse all closes the source and changes to graphic4919 Expand");
    activate_well("strip", collapse_x + 5, 10);
    panel_build();
    draw_well("strip", 264);
    CHECK(
        g_c.comp_h == 126 && sprite_opaque_matches("btn_collapse.png", collapse_x, 6),
        "Expand all restores the grid and changes back to graphic4917 Collapse");

    activate_well("strip", value_x + 5, 10);
    draw_well("strip", 264);
    CHECK(
        strcmp(fake_cfg_str(NULL, "price_source"), "High alchemy") == 0 &&
            sprite_opaque_matches("btn_cache.png", value_x, 6),
        "the value action switches basis and changes to graphic4911 Cache value");

    activate_well("strip", 9, 10);
    panel_build();
    draw_well("strip", 264);
    CHECK(
        sprite_opaque_matches("btn_sourceview.png", 4, 6),
        "the drop view changes its left action to the source-view graphic");
    activate_well("strip", 9, 10);
    panel_build();
    draw_well("strip", 264);

    (void)v2_cfg_set(&g_api, "ignored_sources", "Goblin");
    (void)v2_cfg_set(&g_api, "ignored_items", "Item 526");
    panel_build();
    draw_well("strip", 264);
    CHECK(
        g_c.comp_h == 77 && sprite_opaque_matches("btn_ignored.png", ignored_x, 6),
        "ignored data is retained but hidden in ordinary mode");

    activate_well("strip", ignored_x + 5, 10);
    panel_build();
    draw_well("strip", 264);
    CHECK(
        g_c.comp_h == 126 &&
            sprite_opaque_matches("btn_ignored_hide.png", ignored_x, 6),
        "Show ignored restores retained rows and changes to graphic4914 Hide");
    CHECK(
        g_c.comp_px[48 * g_c.comp_w + 100] ==
                png_pixel("cat_spine_ignored.png", 10, 2) &&
            g_c.comp_px[83 * g_c.comp_w + 6] ==
                png_pixel("cell_ignored.png", 1, 1),
        "ignored sources and items use their exact alternate cache plates");
}

int
main(void)
{
    api_init();
    render_xp();
    test_xp_ttl_advances_inside_rate_floor();
    render_loot();
    test_loot_stateful_controls();
    if( g_plugin_state ) dispatch_stop();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
