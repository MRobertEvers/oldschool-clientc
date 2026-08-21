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
    struct FakeObject objects[FAKE_OBJECTS_MAX];
    int objects_live;
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
fake_key_held(void* u, int key)
{
    (void)u;
    return key == 42;
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
fake_draw_tile(void* u, int a, int b, int c, uint32_t d, int e)
{
    (void)u;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    g_engine.draw_items += 5;
    return 5;
}
static int
fake_draw_hull(void* u, int a, uint32_t b, int c)
{
    (void)u;
    (void)a;
    (void)b;
    (void)c;
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
    e.key_held = fake_key_held;
    e.project = fake_project;
    e.draw_tile = fake_draw_tile;
    e.draw_hull = fake_draw_hull;
    e.draw_line = fake_draw_line;
    e.draw_text = fake_draw_text;
    e.draw_rect = fake_draw_rect;
    e.menu_add = fake_menu_add;
    e.obj_next = fake_obj_next;
    e.asset_read = fake_asset_read;
    e.asset_write = fake_asset_write;
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
    /* Well past the budget, to prove the host stops handing calls through. */
    for( int i = 0; i < 400; i++ )
        g_api->draw_tile(ctx, d->surface, 1, 1, 0, 0xffffffu, 0);
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

static void
gamma_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    api->subscribe(ctx, TORIRS_PLUGIN_EV_ASSET, gamma_asset, NULL);

    g_gamma_object_count = 0;
    for( int i = 0; i < 3; i++ )
    {
        int const handle = api->object_create(ctx);
        if( handle >= 0 )
            g_gamma_objects[g_gamma_object_count++] = handle;
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

static struct ToriRS_PluginDef const GAMMA = {
    .name = "gamma",
    .version = "1",
    .priority = 0,
    .config = NULL,
    .init = gamma_init,
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

static void
beta_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LOGIC_TICK, beta_tick, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_KEY, beta_key_consume, NULL);
}

static struct ToriRS_PluginDef const BETA = {
    .name = "beta",
    .version = "1",
    .priority = 10,
    .config = NULL,
    .init = beta_init,
};

static struct ToriRS_PluginDef const OFF_BY_DEFAULT = {
    .name = "sleeper",
    .version = "1",
    .disabled_by_default = true,
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

        /* Stopping the plugin takes its geometry and its bytes with it. */
        PluginHost_SetEnabled(host3, g, false);
        CHECK(g_engine.objects_live == 0, "a stopped plugin's world objects are destroyed");
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
        }

        PluginHost_Free(host3);
    }

    PluginHost_Free(host);

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
