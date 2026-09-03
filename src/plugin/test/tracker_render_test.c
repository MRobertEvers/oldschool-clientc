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
 * The scenario is the reference screenshots' own -- a defence/hitpoints/slayer
 * trip, and a Guard/Cerberus/Werewolf loot log -- so the two pictures can be
 * put beside the captures they are trying to match.
 */

#include "plugin/torirs_plugin.h"

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
#define FAKE_ASSETS 16
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

    /* config, as a flat key/value list */
    struct { char k[32]; char v[192]; } cfg[24];
    int cfg_count;
} g_c;

static ToriRS_PluginHandler g_handler[TORIRS_PLUGIN_EV_COUNT];

/* ------------------------------------------------------------------- verbs */

static void
fake_subscribe(
    struct ToriRS_PluginCtx* ctx, enum ToriRS_PluginEvent ev,
    ToriRS_PluginHandler fn, void* ud)
{
    (void)ctx;
    (void)ud;
    assert(ev >= 0 && ev < TORIRS_PLUGIN_EV_COUNT);
    g_handler[ev] = fn;
}

static void fake_log(struct ToriRS_PluginCtx* c, char const* f, ...) { (void)c; (void)f; }
static void fake_notify(struct ToriRS_PluginCtx* c, char const* t) { (void)c; (void)t; }
static uint64_t fake_frame_ms(struct ToriRS_PluginCtx* c) { (void)c; return g_c.now_ms; }

static int
fake_local_player(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginPlayerSnap* out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    out->true_x = 3200;
    out->true_z = 3200;
    return 1;
}

static char const*
fake_skill_name(struct ToriRS_PluginCtx* ctx, int s)
{
    (void)ctx;
    return (s >= 0 && s < FAKE_SKILLS) ? SKILL_NAME[s] : NULL;
}

static int
fake_stat(struct ToriRS_PluginCtx* ctx, int s, int* cur, int* base)
{
    (void)ctx;
    if( s < 0 || s >= FAKE_SKILLS )
        return 0;
    if( cur )
        *cur = g_c.level[s];
    if( base )
        *base = g_c.level[s];
    return 1;
}

static int
fake_stat_xp(struct ToriRS_PluginCtx* ctx, int s, int* xp, int* lvl_xp, int* next_xp)
{
    (void)ctx;
    if( s < 0 || s >= FAKE_SKILLS )
        return 0;
    if( xp )
        *xp = g_c.xp[s];
    if( lvl_xp )
        *lvl_xp = LEVEL_XP[g_c.level[s]];
    if( next_xp )
        *next_xp = g_c.level[s] >= 99 ? 0 : LEVEL_XP[g_c.level[s] + 1];
    return 1;
}

/* ---- config ---- */

static char const*
fake_cfg_str(struct ToriRS_PluginCtx* ctx, char const* key)
{
    (void)ctx;
    for( int i = 0; i < g_c.cfg_count; i++ )
        if( strcmp(g_c.cfg[i].k, key) == 0 )
            return g_c.cfg[i].v;
    return "";
}
static int fake_cfg_bool(struct ToriRS_PluginCtx* c, char const* k) { return atoi(fake_cfg_str(c, k)) != 0; }
static int fake_cfg_int(struct ToriRS_PluginCtx* c, char const* k) { return atoi(fake_cfg_str(c, k)); }

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
fake_cfg_set(struct ToriRS_PluginCtx* ctx, char const* k, char const* v)
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
fake_asset_load(struct ToriRS_PluginCtx* ctx, char const* name)
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
fake_asset_data(struct ToriRS_PluginCtx* ctx, char const* name, int* out_size)
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
fake_asset_save(struct ToriRS_PluginCtx* c, char const* n, void const* d, int s)
{ (void)c; (void)n; (void)d; (void)s; return 1; }
static void fake_asset_release(struct ToriRS_PluginCtx* c, char const* n) { (void)c; (void)n; }

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
fake_image_load(struct ToriRS_PluginCtx* ctx, char const* name)
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
fake_image_size(struct ToriRS_PluginCtx* ctx, int im, int* w, int* h)
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
fake_image_pixels(struct ToriRS_PluginCtx* ctx, int im, uint32_t* out, int max)
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

static void fake_image_release(struct ToriRS_PluginCtx* c, int i) { (void)c; (void)i; }

/** Keep the composed strip; this is the picture the test writes out. */
static int
fake_image_compose(
    struct ToriRS_PluginCtx* ctx, char const* name, int w, int h, uint32_t const* argb)
{
    (void)ctx;
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
fake_obj_image(struct ToriRS_PluginCtx* ctx, int obj_id, int count, int style)
{
    int const slot = image_alloc(36, 32);
    uint32_t rgb;

    (void)ctx;
    (void)style;
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
    struct ToriRS_PluginLootRow row[FAKE_LOOT_ROWS];
} g_loot[FAKE_LOOT_SOURCES];
static int g_loot_count;

static void
loot_add(char const* name, int kills, int const* obj, int const* qty, int const* val, int n)
{
    struct FakeLootSource* s;
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
    struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginLootSource* out)
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
    struct ToriRS_PluginCtx* ctx, int source_id, int iter,
    struct ToriRS_PluginLootRow* out)
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
fake_obj_info(struct ToriRS_PluginCtx* ctx, int obj_id, struct ToriRS_PluginObjInfo* out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    out->obj_id = obj_id;
    out->cert_link = -1;
    out->wearpos = -1;
    out->wearpos2 = -1;
    out->wearpos3 = -1;
    out->attack_rate = -1;
    return 1;
}

static void
fake_draw_image(
    struct ToriRS_PluginCtx* c, void* s, int im, int x, int y,
    int cx, int cy, int cw, int ch, int t)
{ (void)c; (void)s; (void)im; (void)x; (void)y; (void)cx; (void)cy; (void)cw; (void)ch; (void)t; }

/* ---- the panel model (only what the strips need) ---- */

struct FakeWidget { char id[32]; int kind; int height; int live; };
static struct FakeWidget g_w[48];
static int g_w_count;
static int g_building;

static bool
fake_panel_request(struct ToriRS_PluginCtx* c, struct ToriRS_PluginPanelDesc const* d)
{ (void)c; (void)d; return true; }

static bool
fake_panel_widget(struct ToriRS_PluginCtx* c, int kind, char const* id, char const* label)
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

static bool fake_panel_set_text(struct ToriRS_PluginCtx* c, char const* i, char const* t)
{ (void)c; (void)t; return w_find(i) != NULL; }
static bool fake_panel_set_value(struct ToriRS_PluginCtx* c, char const* i, int v)
{ (void)c; (void)v; return w_find(i) != NULL; }
static bool fake_panel_set_options(struct ToriRS_PluginCtx* c, char const* i, char const* o, int s)
{ (void)c; (void)o; (void)s; return w_find(i) != NULL; }
static bool fake_panel_set_badge(struct ToriRS_PluginCtx* c, char const* t) { (void)c; (void)t; return true; }
static bool fake_panel_set_attention(struct ToriRS_PluginCtx* c, bool o) { (void)c; (void)o; return true; }
static void fake_panel_clear(struct ToriRS_PluginCtx* c) { (void)c; g_w_count = 0; }
static void fake_panel_invalidate(struct ToriRS_PluginCtx* c, char const* i) { (void)c; (void)i; }

static bool
fake_panel_set_height(struct ToriRS_PluginCtx* c, char const* id, int px)
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

static struct ToriRS_PluginApi g_api;

static void
api_init(void)
{
    memset(&g_api, 0, sizeof(g_api));
    g_api.abi_version = TORIRS_PLUGIN_ABI;
    g_api.subscribe = fake_subscribe;
    g_api.log = fake_log;
    g_api.notify = fake_notify;
    g_api.frame_ms = fake_frame_ms;
    g_api.local_player = fake_local_player;
    g_api.skill_name = fake_skill_name;
    g_api.stat = fake_stat;
    g_api.stat_xp = fake_stat_xp;
    g_api.cfg_bool = fake_cfg_bool;
    g_api.cfg_int = fake_cfg_int;
    g_api.cfg_str = fake_cfg_str;
    g_api.cfg_set = fake_cfg_set;
    g_api.asset_load = fake_asset_load;
    g_api.asset_data = fake_asset_data;
    g_api.asset_save = fake_asset_save;
    g_api.asset_release = fake_asset_release;
    g_api.image_load = fake_image_load;
    g_api.image_size = fake_image_size;
    g_api.image_pixels = fake_image_pixels;
    g_api.image_compose = fake_image_compose;
    g_api.image_release = fake_image_release;
    g_api.draw_image = fake_draw_image;
    g_api.obj_image = fake_obj_image;
    g_api.obj_info = fake_obj_info;
    g_api.loot_source_next = fake_loot_source_next;
    g_api.loot_row_next = fake_loot_row_next;
    g_api.panel_request = fake_panel_request;
    g_api.panel_widget = fake_panel_widget;
    g_api.panel_set_text = fake_panel_set_text;
    g_api.panel_set_value = fake_panel_set_value;
    g_api.panel_set_options = fake_panel_set_options;
    g_api.panel_set_badge = fake_panel_set_badge;
    g_api.panel_set_attention = fake_panel_set_attention;
    g_api.panel_clear = fake_panel_clear;
    g_api.panel_invalidate = fake_panel_invalidate;
    g_api.panel_set_height = fake_panel_set_height;
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

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_XP_TRACKER;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_LOOT_TRACKER;

static struct ToriRS_PluginCtx* const CTX = (struct ToriRS_PluginCtx*)&g_c;

static void
dispatch(enum ToriRS_PluginEvent ev, void* payload)
{
    if( g_handler[ev] )
        g_handler[ev](CTX, payload, NULL);
}

static void
panel_build(void)
{
    struct ToriRS_PluginEvPanelBuild ev;
    struct ToriRS_PluginEvPanelLayout lay;

    memset(&ev, 0, sizeof(ev));
    ev.selection_generation = 1;
    ev.view = TORIRS_PLUGIN_PANEL_VIEW_PAGE;
    g_w_count = 0;
    g_building = 1;
    dispatch(TORIRS_PLUGIN_EV_PANEL_BUILD, &ev);
    g_building = 0;

    memset(&lay, 0, sizeof(lay));
    lay.width = 264;
    lay.height = 491;
    lay.scale_milli = 1000;
    lay.size_class = TORIRS_PLUGIN_PANEL_MEDIUM;
    lay.visible = true;
    lay.game_visible = true;
    lay.selection_generation = 1;
    dispatch(TORIRS_PLUGIN_EV_PANEL_LAYOUT, &lay);
}

static void
tick(uint64_t ms)
{
    struct ToriRS_PluginEvTick ev;
    memset(&ev, 0, sizeof(ev));
    g_c.now_ms += ms;
    dispatch(TORIRS_PLUGIN_EV_LOGIC_TICK, &ev);
}

/** Run one draw pass over the named custom well and keep what it composed. */
static void
draw_well(char const* id, int width)
{
    struct ToriRS_PluginEvPanelDraw ev;
    memset(&ev, 0, sizeof(ev));
    ev.id = id;
    ev.surface = (void*)0x1;
    ev.width = width;
    ev.height = 4096;
    ev.scale_milli = 1000;
    ev.selection_generation = 1;
    dispatch(TORIRS_PLUGIN_EV_PANEL_DRAW, &ev);
}

static void
reset(char const* asset_dir)
{
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
    memset(g_handler, 0, sizeof(g_handler));
    g_w_count = 0;
    g_c.asset_dir = asset_dir;
    g_c.now_ms = 1000000;
}

/* ------------------------------------------------------------- the cases */

/**
 * The xp strip, against the reference capture's own trip: a defence/hitpoints/
 * slayer session with a few hundred thousand xp on each.
 */
static void
render_xp(void)
{
    char const* path = getenv("TRACKER_RENDER_XP_PNG");
    int const DEF = 1, HP = 3, SLAY = 18;

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

    g_c.level[DEF] = 97;  g_c.xp[DEF] = LEVEL_XP[97] + 400000;
    g_c.level[HP] = 92;   g_c.xp[HP] = LEVEL_XP[92] + 200000;
    g_c.level[SLAY] = 98; g_c.xp[SLAY] = LEVEL_XP[98] + 90000;

    TORIRS_PLUGIN_XP_TRACKER.init(CTX, &g_api);
    dispatch(TORIRS_PLUGIN_EV_START, NULL);
    panel_build();
    tick(20);

    /* An hour and a half of training, so the rates are the reference's order
     * of magnitude rather than a first-minute extrapolation. */
    for( int i = 0; i < 90; i++ )
    {
        g_c.xp[DEF] += 4150;
        g_c.xp[HP] += 2030;
        g_c.xp[SLAY] += 2040;
        tick(60000);
    }
    panel_build();
    draw_well("boxes", 264);

    CHECK(g_c.comp_px != NULL, "the xp strip composed");
    CHECK(g_c.comp_w == 264, "at the well's width (got %d)", g_c.comp_w);
    CHECK(
        g_c.comp_h >= 3 * 50, "and is tall enough for three boxes (got %d)", g_c.comp_h);
    if( g_c.comp_px )
        write_png(path ? path : "build/xp_strip.png", g_c.comp_w, g_c.comp_h, g_c.comp_px);
    printf("xp strip: %dx%d -> %s\n", g_c.comp_w, g_c.comp_h,
        path ? path : "build/xp_strip.png");
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

    TORIRS_PLUGIN_LOOT_TRACKER.init(CTX, &g_api);
    dispatch(TORIRS_PLUGIN_EV_START, NULL);
    panel_build();
    tick(1000);
    panel_build();
    draw_well("strip", 264);

    CHECK(g_c.comp_px != NULL, "the loot strip composed");
    CHECK(g_c.comp_w == 264, "at the well's width (got %d)", g_c.comp_w);
    if( g_c.comp_px )
        write_png(path ? path : "build/loot_strip.png", g_c.comp_w, g_c.comp_h, g_c.comp_px);
    printf("loot strip: %dx%d -> %s\n", g_c.comp_w, g_c.comp_h,
        path ? path : "build/loot_strip.png");
}

int
main(void)
{
    api_init();
    render_xp();
    render_loot();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
