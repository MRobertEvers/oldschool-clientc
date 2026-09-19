/*
 * quest-driver: interfaces, tabs, the npc/loc/obj pools, keys, text and screenshots.
 *
 * Owner: verbs-ui (docs/ARCHITECT.md). Nobody else defines a function in this
 * file, and this file defines no function declared under another owner's
 * block in src/plugin/torirs_plugin_drive.h.
 *
 * D3: mount liveness is UITree_GroupPresent. interface_parents is CS2-only
 * and the dat1 lane mounts through CreateTask_SlotMount without ever writing
 * it. D4: modal_host_uid is initialised to -1, set on open and NEVER cleared
 * on close, so DriveUi_ModalLive re-verifies rather than testing non-zero --
 * here that re-verify is UITree_InterfaceParentFind on the exact uid
 * modal_host_uid names (D3's objection is to using interface_parents to find
 * a mount by NAME, which it cannot do; keyed by the uid we already hold, it
 * is exactly what the table is for).
 * D2: ui.invoke is app_plugin_if_click, not app->host.trigger_op.
 * This group also owns App_SetCameraPose, App_LocalPlayerIdle and the mount
 * liveness helper in src/app/app_plugin_api.c, which verbs-pointer calls.
 *
 * Four seams here reach outside this file, and all four are wired now:
 * DriveUi_IfClick/DriveUi_Tab go through App_PluginDriveIfClick /
 * App_PluginDriveTabSelect (app.h), the test-only exports of the raw
 * dispatchers -- the gated api_* path refuses a coroutine resumed on a bare
 * frame tick, and plan 5.8/U9 says the test module may call the seam directly.
 * DriveUi_Key/DriveUi_Text push onto the bus PluginDriveCore_CmdBus() hands
 * back, which content_test.c sets once a frame beside the embed pointer.
 */

#include "plugin/torirs_plugin_drive.h"

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER

#include "app.h"
#include "cmd/cmdbus.h"
#include "input/torirs_keymap.h"
#include "plugin/torirs_plugin_lua.h"

#include "lauxlib.h"
#include "lua.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum DriveResult
DriveUi_GroupPresent(struct App* app, int interface_id, int* out_present)
{
    assert(app);
    assert(out_present);
    *out_present = app->tree ? UITree_GroupPresent(app->tree, interface_id) : 0;
    return DRIVE_OK;
}

enum DriveResult
DriveUi_Component(struct App* app, char const* symbol, int sub, int* out_component_id)
{
    enum DriveResult symbol_result;
    int component_id;
    int32_t idx;

    assert(app);
    assert(symbol);
    assert(out_component_id);

    *out_component_id = -1;
    /* An unknown content symbol is the test author's typo or a symbol this
     * pack does not have -- DRIVE_NO_ROW, matching ui.widget's own declared
     * result set (plan 5.8: ok / no_row / not_visible). */
    symbol_result = DriveSymbol_Lookup(DRIVE_SYMBOL_COMPONENT, symbol, &component_id);
    if( symbol_result != DRIVE_OK )
        return DRIVE_NO_ROW;
    if( !app->tree )
        return DRIVE_NOT_VISIBLE;
    /* content_test.c:138-146 verbatim (the `widget()` helper), minus the
     * cache lookup this function's caller already did through DriveSymbol_Lookup. */
    idx = UITree_FindByComponentId(app->tree, component_id);
    if( idx >= 0 && sub >= 0 )
        idx = UITree_FindChildBySubid(app->tree, idx, component_id, sub);
    if( idx < 0 )
        return DRIVE_NOT_VISIBLE;
    *out_component_id = app->tree->components[idx].component_id;
    return DRIVE_OK;
}

enum DriveResult
DriveUi_IfClick(struct App* app, int component_id, int op)
{
    assert(app);
    /*
     * The ONE dispatcher every real click reaches, via the test-only export
     * App_PluginDriveIfClick (app.h). Reimplementing app_plugin_click_node's
     * ninety lines here would give the driver a second, divergent copy of the
     * press path, which is the one thing a test harness must never have: it
     * would pass while the real click broke.
     */
    if( op < 0 || op > 10 )
        return DRIVE_REFUSED;
    return App_PluginDriveIfClick(app, component_id, op) ? DRIVE_OK : DRIVE_REFUSED;
}

enum DriveResult
DriveUi_Tab(struct App* app, int tab_number)
{
    assert(app);
    /* app_plugin_tab_select already branches CS2 vs dat1 internally, so this
     * needs no lane handling of its own. It answers 0 for a tab this frame has
     * nothing on, which is a refusal a test should see rather than a silent
     * no-op. */
    if( tab_number < 0 )
        return DRIVE_REFUSED;
    return App_PluginDriveTabSelect(app, tab_number) ? DRIVE_OK : DRIVE_REFUSED;
}

enum DriveResult
DriveUi_ModalLive(struct App* app, int* out_live)
{
    assert(app);
    assert(out_live);
    *out_live = 0;
    if( app->modal_host_uid >= 0 && app->tree &&
        UITree_InterfaceParentFind(app->tree, app->modal_host_uid) >= 0 )
        *out_live = 1;
    return DRIVE_OK;
}

/* ------------------------------------------------------------- pool walks */

/* <col=RRGGBB>...</col> comes off WorldEntity_NPC.name wholesale: strip every
 * bracketed run rather than special-casing "col", matching the one grammar
 * this field is documented to carry (entity_npc.h:41-43). An unterminated
 * '<' is left as plain text -- the same rule game/rs_game_events.c's private
 * strip_markup uses, restated here because that function is not exported and
 * this group owns no file that could export it. */
static void
drive_ui_strip_tags(char const* in, char* out, size_t out_cap)
{
    size_t at = 0;

    assert(in);
    assert(out);
    assert(out_cap > 0);

    while( *in && at < out_cap - 1 )
    {
        if( *in == '<' )
        {
            char const* close = strchr(in, '>');
            if( close )
            {
                in = close + 1;
                continue;
            }
        }
        out[at++] = *in++;
    }
    out[at] = '\0';
}

/* Chebyshev tile distance: the "radius" a click/target verb states is a
 * square the way the client's own interaction range checks are (route
 * approach, not line-of-sight), and it is cheap to filter and to reason
 * about from a test. radius <= 0 is "the whole pool", per this group's own
 * header banner. */
static int
drive_ui_within_radius(int ax, int az, int bx, int bz, int radius)
{
    int dx = ax - bx;
    int dz = az - bz;
    if( dx < 0 )
        dx = -dx;
    if( dz < 0 )
        dz = -dz;
    return radius <= 0 || (dx <= radius && dz <= radius);
}

static long
drive_ui_distance2(int ax, int az, int bx, int bz)
{
    long dx = ax - bx;
    long dz = az - bz;
    return dx * dx + dz * dz;
}

enum DriveResult
DriveUi_Npcs(struct App* app, int radius, struct DriveNpcRow* out, int cap, int* out_count)
{
    int px = 0, pz = 0, plevel, dest_x, dest_z, flag_x, flag_z, draw_x, draw_z;
    int have_player;
    int base_x, base_z;
    int count = 0;
    struct World_EntityPool* pool;
    int i;

    assert(app);
    assert(out);
    assert(cap > 0);
    assert(out_count);

    *out_count = 0;
    if( !app->world )
        return DRIVE_OK;

    have_player = App_LocalPlayerTiles(
        app, &px, &pz, &plevel, &dest_x, &dest_z, &flag_x, &flag_z, &draw_x, &draw_z);
    base_x = app->world->_base_tile_x;
    base_z = app->world->_base_tile_z;

    pool = &app->world->entities.npc;
    if( getenv("TORIRS_DRIVE_DEBUG") )
        fprintf(stderr,
            "drive_npcs: world=%p npc_pool=%d head=%d player_pool=%d scenery_pool=%d "
            "load_complete=%d have_player=%d at %d,%d base=%d,%d radius=%d\n",
            (void*)app->world, pool->count, World_EntityPoolHead(pool),
            app->world->entities.player.count, app->world->entities.scenery.count,
            app->world->load_complete, have_player, px, pz, base_x, base_z, radius);
    for( i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC const* npc = World_EntityPoolGet(pool, i);
        int tile_x, tile_z;
        long distance;
        int j, insert_at;

        if( !npc || npc->server_slot < 0 )
        {
            if( getenv("TORIRS_DRIVE_DEBUG") )
                fprintf(stderr,
                    "drive_npcs: index %d dropped (npc=%p slot=%d)\n",
                    i, (void const*)npc, npc ? npc->server_slot : -1);
            continue;
        }
        tile_x = base_x + npc->grid_position.x;
        tile_z = base_z + npc->grid_position.z;
        if( have_player && !drive_ui_within_radius(tile_x, tile_z, px, pz, radius) )
        {
            if( getenv("TORIRS_DRIVE_DEBUG") )
                fprintf(stderr,
                    "drive_npcs: slot %d id %d at %d,%d outside radius %d of %d,%d\n",
                    npc->server_slot, npc->npc_id, tile_x, tile_z, radius, px, pz);
            continue;
        }

        distance = have_player ? drive_ui_distance2(tile_x, tile_z, px, pz) : 0;

        /* Insertion into a bounded nearest-`cap` array: cap is small (a test
         * asks for a handful of candidates), the pool is a few thousand, and
         * this keeps the whole walk to one pass with no second buffer. */
        insert_at = count < cap ? count : cap - 1;
        if( count >= cap )
        {
            long worst = have_player
                ? drive_ui_distance2(out[cap - 1].tile_x, out[cap - 1].tile_z, px, pz)
                : 0;
            if( have_player && distance >= worst )
                continue;
        }
        for( j = insert_at; j > 0; j-- )
        {
            long prev_distance = have_player
                ? drive_ui_distance2(out[j - 1].tile_x, out[j - 1].tile_z, px, pz)
                : 0;
            if( !have_player || prev_distance <= distance )
                break;
            out[j] = out[j - 1];
        }
        out[j].slot = npc->server_slot;
        out[j].npc_id = npc->npc_id;
        out[j].base_npc_id = npc->base_npc_id;
        out[j].tile_x = tile_x;
        out[j].tile_z = tile_z;
        out[j].level = npc->grid_position.level;
        out[j].element_id = npc->element_id;
        drive_ui_strip_tags(npc->name, out[j].name, sizeof(out[j].name));
        if( count < cap )
            count++;
    }
    *out_count = count;
    return DRIVE_OK;
}

enum DriveResult
DriveUi_Locs(struct App* app, int radius, struct DriveLocRow* out, int cap, int* out_count)
{
    int px = 0, pz = 0, plevel, dest_x, dest_z, flag_x, flag_z, draw_x, draw_z;
    int have_player;
    int base_x, base_z;
    int count = 0;
    struct World_EntityPool* pool;
    int i;

    assert(app);
    assert(out);
    assert(cap > 0);
    assert(out_count);

    *out_count = 0;
    if( !app->world )
        return DRIVE_OK;

    have_player = App_LocalPlayerTiles(
        app, &px, &pz, &plevel, &dest_x, &dest_z, &flag_x, &flag_z, &draw_x, &draw_z);
    base_x = app->world->_base_tile_x;
    base_z = app->world->_base_tile_z;

    /* Root worldview only (content_test.c's own scenery_json walks every
     * live worldview; this group has not settled U12 -- non-root worldviews
     * -- so this matches npc_json's narrower, root-only scope instead of
     * guessing at the aboard-view base tile math). */
    pool = &app->world->entities.scenery;
    for( i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery const* sc = World_EntityPoolGet(pool, i);
        int tile_x, tile_z;
        long distance;
        int j, insert_at;

        if( !sc )
            continue;
        tile_x = base_x + sc->grid_position.x;
        tile_z = base_z + sc->grid_position.z;
        if( have_player && !drive_ui_within_radius(tile_x, tile_z, px, pz, radius) )
            continue;

        distance = have_player ? drive_ui_distance2(tile_x, tile_z, px, pz) : 0;
        insert_at = count < cap ? count : cap - 1;
        if( count >= cap )
        {
            long worst = have_player
                ? drive_ui_distance2(out[cap - 1].tile_x, out[cap - 1].tile_z, px, pz)
                : 0;
            if( have_player && distance >= worst )
                continue;
        }
        for( j = insert_at; j > 0; j-- )
        {
            long prev_distance = have_player
                ? drive_ui_distance2(out[j - 1].tile_x, out[j - 1].tile_z, px, pz)
                : 0;
            if( !have_player || prev_distance <= distance )
                break;
            out[j] = out[j - 1];
        }
        out[j].loc_id = sc->loc_id;
        out[j].tile_x = tile_x;
        out[j].tile_z = tile_z;
        out[j].level = sc->grid_position.level;
        out[j].element_id = sc->element_id;
        if( count < cap )
            count++;
    }
    *out_count = count;
    return DRIVE_OK;
}

enum DriveResult
DriveUi_Objs(struct App* app, int radius, struct DriveObjRow* out, int cap, int* out_count)
{
    int px = 0, pz = 0, plevel, dest_x, dest_z, flag_x, flag_z, draw_x, draw_z;
    int have_player;
    int base_x, base_z;
    int count = 0;
    struct World_EntityPool* pool;
    int i;

    assert(app);
    assert(out);
    assert(cap > 0);
    assert(out_count);

    *out_count = 0;
    if( !app->world )
        return DRIVE_OK;

    have_player = App_LocalPlayerTiles(
        app, &px, &pz, &plevel, &dest_x, &dest_z, &flag_x, &flag_z, &draw_x, &draw_z);
    base_x = app->world->_base_tile_x;
    base_z = app->world->_base_tile_z;

    pool = &app->world->entities.obj_stack;
    for( i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_ObjStack const* stack = World_EntityPoolGet(pool, i);
        int tile_x, tile_z;
        long distance;
        int j, insert_at;

        if( !stack )
            continue;
        tile_x = base_x + stack->grid_position.x;
        tile_z = base_z + stack->grid_position.z;
        if( have_player && !drive_ui_within_radius(tile_x, tile_z, px, pz, radius) )
            continue;

        distance = have_player ? drive_ui_distance2(tile_x, tile_z, px, pz) : 0;
        insert_at = count < cap ? count : cap - 1;
        if( count >= cap )
        {
            long worst = have_player
                ? drive_ui_distance2(out[cap - 1].tile_x, out[cap - 1].tile_z, px, pz)
                : 0;
            if( have_player && distance >= worst )
                continue;
        }
        for( j = insert_at; j > 0; j-- )
        {
            long prev_distance = have_player
                ? drive_ui_distance2(out[j - 1].tile_x, out[j - 1].tile_z, px, pz)
                : 0;
            if( !have_player || prev_distance <= distance )
                break;
            out[j] = out[j - 1];
        }
        out[j].obj_id = stack->obj_id;
        out[j].count = stack->count;
        out[j].tile_x = tile_x;
        out[j].tile_z = tile_z;
        out[j].level = stack->grid_position.level;
        out[j].element_id = stack->element_id;
        if( count < cap )
            count++;
    }
    *out_count = count;
    return DRIVE_OK;
}

enum DriveResult
DriveUi_PlayerTile(struct App* app, int* out_x, int* out_z, int* out_level)
{
    int dest_x, dest_z, flag_x, flag_z, draw_x, draw_z;

    assert(app);
    assert(out_x);
    assert(out_z);
    assert(out_level);

    if( !App_LocalPlayerTiles(
            app, out_x, out_z, out_level, &dest_x, &dest_z, &flag_x, &flag_z, &draw_x, &draw_z) )
    {
        *out_x = 0;
        *out_z = 0;
        *out_level = 0;
        return DRIVE_NOT_VISIBLE;
    }
    return DRIVE_OK;
}

enum DriveResult
DriveUi_Key(struct App* app, char const* name, int down)
{
    struct ToriRS_CmdBus* bus;
    int key;

    assert(app);
    assert(name);

    /* The bus main.c drains every loop iteration, handed to the driver once a
     * frame by content_test.c the same way the embed pointer is. A push from
     * inside on_frame_start is drained on the NEXT iteration, which is the
     * extra frame every cadence in this driver budgets for. */
    bus = PluginDriveCore_CmdBus();
    if( !bus )
        return DRIVE_UNSUPPORTED;

    key = LibToriRS_OsrsKeyFromName(name);
    /* A name this keymap does not know is the test author's typo, not a
     * contract violation: answer refused and let the ledger name it. */
    if( key < 0 )
        return DRIVE_REFUSED;

    /* content_test.c:610-624's "key " cheat, which is the proven shape: the
     * down edge carries its character echo, because one real client frame's
     * input would carry both. */
    if( down )
    {
        CmdBus_PushOsrsKey(bus, key, 1, 1);
        CmdBus_PushKeyEvent(bus, key, 0, 0);
    }
    else
    {
        CmdBus_PushOsrsKey(bus, key, 0, 0);
    }
    return DRIVE_OK;
}

enum DriveResult
DriveUi_Text(struct App* app, char const* text)
{
    struct ToriRS_CmdBus* bus;
    size_t length;
    size_t i;

    assert(app);
    assert(text);

    bus = PluginDriveCore_CmdBus();
    if( !bus )
        return DRIVE_UNSUPPORTED;

    /* A test author's typo -- an empty string, a control character, something
     * longer than the chat line holds -- is a legitimate refusal, not a
     * contract violation. Same bounds as content_test.c:596-608. */
    length = strlen(text);
    if( length < 1 || length > 63 )
        return DRIVE_REFUSED;
    for( i = 0; i < length; i++ )
        if( (unsigned char)text[i] < 0x20 || (unsigned char)text[i] > 0x7e )
            return DRIVE_REFUSED;

    for( i = 0; i < length; i++ )
        CmdBus_PushKeyEvent(bus, -1, (int)(unsigned char)text[i], 0);
    return DRIVE_OK;
}

/* One capture outstanding at a time: t.shot is awaited synchronously by the
 * coroutine that requested it (core.lua's flush happens before the next
 * step), so a second name can only arrive after this one settles. */
static char g_drive_ui_shot_path[1024];

/*
 * QD-11: the slot leaving app->plugin_screenshots is not the same moment as
 * the file landing on disk, and this counts the polls spent in that gap.
 *
 * The write-back is Task_PluginAssetWrite (app_plugin_assets.c:390,
 * task_plugin_io.c:550-563): queued the frame the slot is released, and run
 * -- write-then-rename, platform_x_io.c:508-556 -- on a LATER pass of the
 * async task budget (app_frame.c's step loop). App_AsyncPending(app) looked
 * like the right gate here (it is what content_test.c's own equivalent poll
 * effectively waits behind, content_test.c:700 before its stat at :973), but
 * it is measured wrong for this call site: drive_pump_once runs from
 * on_frame_start (ARCHITECT.md A1), which is BEFORE this frame's async
 * budget loop, so the flag this function would read is the ONE FRAME STALE
 * value left by the frame before -- the one that queued the write, not the
 * one running it. Traced live (TORIRS_STDERR_UNBUFFERED=1, a t.shot smoke
 * script): the first poll after the slot clears sees async_pending=0 and
 * stat()=-1 in the same breath, every time -- the exact false refusal the
 * finding reported (4 of 4 shots). A bounded retry count sidesteps the
 * staleness instead of chasing it: the write is at most a couple of frames
 * behind the slot clearing (one to run the write, one for this poll to see
 * it), so a small bound clears the ordinary case in 1-2 polls and still
 * refuses a genuine failure (a write error logs and returns with no file,
 * task_plugin_io.c:558-560) well inside ui.lua's outer 3-SERVER-TICK
 * deadline (ui.lua:186-195), rather than leaving this static stuck for the
 * rest of the run.
 */
#define DRIVE_UI_SHOT_MISS_BUDGET 60
static int g_drive_ui_shot_misses;

enum DriveResult
DriveUi_Shot(struct App* app, char const* name, char* out_path, int out_cap)
{
    char const* session_dir;
    char dir[900];
    char filename[164];
    struct stat info;
    int i;

    assert(app);
    assert(name);
    assert(out_path);
    assert(out_cap > 0);

    out_path[0] = '\0';

    if( g_drive_ui_shot_path[0] )
    {
        /* A request is already in flight: poll it, exactly as
         * content_test.c:964-971 does -- still registered under
         * app->plugin_screenshots means the renderer has not written it yet;
         * DRIVE_TIMEOUT here is this function's own private "not yet"
         * signal, re-checked by ui.lua's await loop, and is never the value
         * a test sees (t.shot's declared result set is ok/refused). */
        for( i = 0; i < APP_PLUGIN_SCREENSHOTS_MAX; i++ )
            if( app->plugin_screenshots[i].in_use &&
                strcmp(app->plugin_screenshots[i].path, g_drive_ui_shot_path) == 0 )
                return DRIVE_TIMEOUT;
        if( stat(g_drive_ui_shot_path, &info) == 0 && info.st_size > 0 )
        {
            snprintf(out_path, (size_t)out_cap, "%s", g_drive_ui_shot_path);
            g_drive_ui_shot_path[0] = '\0';
            g_drive_ui_shot_misses = 0;
            return DRIVE_OK;
        }
        /* See the banner above: the slot is clear but the write-back has not
         * landed on disk yet, ordinarily for one or two more polls. */
        if( ++g_drive_ui_shot_misses < DRIVE_UI_SHOT_MISS_BUDGET )
            return DRIVE_TIMEOUT;
        g_drive_ui_shot_path[0] = '\0';
        g_drive_ui_shot_misses = 0;
        return DRIVE_REFUSED;
    }

    session_dir = DriveCore_SessionDir();
    if( !session_dir )
        return DRIVE_REFUSED;
    snprintf(dir, sizeof(dir), "%s/shots", session_dir);
    snprintf(filename, sizeof(filename), "%s.png", name);
    if( !App_RequestScreenshot(app, dir, filename, g_drive_ui_shot_path, sizeof(g_drive_ui_shot_path)) )
    {
        g_drive_ui_shot_path[0] = '\0';
        return DRIVE_REFUSED;
    }
    /* content_test.c:355's own capture() removes any stale file at this path
     * first, so a leftover from an earlier run cannot be mistaken for this
     * one's completion. */
    remove(g_drive_ui_shot_path);
    g_drive_ui_shot_misses = 0;
    app->need_redraw = 1;
    return DRIVE_TIMEOUT; /* queued; the caller polls again next frame */
}

/* -------------------------------------------------------------- Lua thunks */

static int
lua_drive_group_present(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int interface_id = PluginDrive_ArgInt(L, 1);
    int present = 0;
    enum DriveResult result;

    assert(app);
    result = DriveUi_GroupPresent(app, interface_id, &present);
    lua_pushstring(L, DriveResultName(result));
    lua_pushboolean(L, present);
    return 2;
}

static int
lua_drive_component(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    char const* symbol = PluginDrive_ArgString(L, 1);
    int sub = PluginDrive_ArgOptInt(L, 2, -1);
    int component_id = -1;
    enum DriveResult result;

    assert(app);
    result = DriveUi_Component(app, symbol, sub, &component_id);
    lua_pushstring(L, DriveResultName(result));
    if( result == DRIVE_OK )
        lua_pushinteger(L, component_id);
    else
        lua_pushnil(L);
    return 2;
}

static int
lua_drive_if_click(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int component_id = PluginDrive_ArgInt(L, 1);
    int op = PluginDrive_ArgInt(L, 2);
    enum DriveResult result;

    assert(app);
    result = DriveUi_IfClick(app, component_id, op);
    return PluginDrive_PushResult(L, result, NULL);
}

static int
lua_drive_tab(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int tab_number = PluginDrive_ArgInt(L, 1);
    enum DriveResult result;

    assert(app);
    result = DriveUi_Tab(app, tab_number);
    return PluginDrive_PushResult(L, result, NULL);
}

static int
lua_drive_modal_live(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int live = 0;
    enum DriveResult result;

    assert(app);
    result = DriveUi_ModalLive(app, &live);
    lua_pushstring(L, DriveResultName(result));
    lua_pushboolean(L, live);
    return 2;
}

static void
drive_ui_push_npc_row(struct lua_State* L, struct DriveNpcRow const* row)
{
    lua_newtable(L);
    lua_pushinteger(L, row->slot);
    lua_setfield(L, -2, "slot");
    lua_pushinteger(L, row->npc_id);
    lua_setfield(L, -2, "npc_id");
    lua_pushinteger(L, row->base_npc_id);
    lua_setfield(L, -2, "base_npc_id");
    lua_pushinteger(L, row->tile_x);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, row->tile_z);
    lua_setfield(L, -2, "z");
    lua_pushinteger(L, row->level);
    lua_setfield(L, -2, "level");
    lua_pushinteger(L, row->element_id);
    lua_setfield(L, -2, "element_id");
    lua_pushstring(L, row->name);
    lua_setfield(L, -2, "name");
}

static int
lua_drive_npcs(struct lua_State* L)
{
    enum
    {
        DRIVE_UI_POOL_CAP = 64
    };
    struct App* app = PluginDrive_App();
    int radius = PluginDrive_ArgOptInt(L, 1, 0);
    struct DriveNpcRow rows[DRIVE_UI_POOL_CAP];
    int count = 0;
    enum DriveResult result;
    int i;

    assert(app);
    result = DriveUi_Npcs(app, radius, rows, DRIVE_UI_POOL_CAP, &count);
    lua_pushstring(L, DriveResultName(result));
    lua_newtable(L);
    for( i = 0; i < count; i++ )
    {
        drive_ui_push_npc_row(L, &rows[i]);
        lua_rawseti(L, -2, i + 1);
    }
    return 2;
}

static int
lua_drive_locs(struct lua_State* L)
{
    enum
    {
        DRIVE_UI_POOL_CAP = 64
    };
    struct App* app = PluginDrive_App();
    int radius = PluginDrive_ArgOptInt(L, 1, 0);
    struct DriveLocRow rows[DRIVE_UI_POOL_CAP];
    int count = 0;
    enum DriveResult result;
    int i;

    assert(app);
    result = DriveUi_Locs(app, radius, rows, DRIVE_UI_POOL_CAP, &count);
    lua_pushstring(L, DriveResultName(result));
    lua_newtable(L);
    for( i = 0; i < count; i++ )
    {
        lua_newtable(L);
        lua_pushinteger(L, rows[i].loc_id);
        lua_setfield(L, -2, "loc_id");
        lua_pushinteger(L, rows[i].tile_x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, rows[i].tile_z);
        lua_setfield(L, -2, "z");
        lua_pushinteger(L, rows[i].level);
        lua_setfield(L, -2, "level");
        lua_pushinteger(L, rows[i].element_id);
        lua_setfield(L, -2, "element_id");
        lua_rawseti(L, -2, i + 1);
    }
    return 2;
}

static int
lua_drive_objs(struct lua_State* L)
{
    enum
    {
        DRIVE_UI_POOL_CAP = 64
    };
    struct App* app = PluginDrive_App();
    int radius = PluginDrive_ArgOptInt(L, 1, 0);
    struct DriveObjRow rows[DRIVE_UI_POOL_CAP];
    int count = 0;
    enum DriveResult result;
    int i;

    assert(app);
    result = DriveUi_Objs(app, radius, rows, DRIVE_UI_POOL_CAP, &count);
    lua_pushstring(L, DriveResultName(result));
    lua_newtable(L);
    for( i = 0; i < count; i++ )
    {
        lua_newtable(L);
        lua_pushinteger(L, rows[i].obj_id);
        lua_setfield(L, -2, "obj_id");
        lua_pushinteger(L, rows[i].count);
        lua_setfield(L, -2, "count");
        lua_pushinteger(L, rows[i].tile_x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, rows[i].tile_z);
        lua_setfield(L, -2, "z");
        lua_pushinteger(L, rows[i].level);
        lua_setfield(L, -2, "level");
        lua_pushinteger(L, rows[i].element_id);
        lua_setfield(L, -2, "element_id");
        lua_rawseti(L, -2, i + 1);
    }
    return 2;
}

static int
lua_drive_player_tile(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    int x = 0, z = 0, level = 0;
    enum DriveResult result;

    assert(app);
    result = DriveUi_PlayerTile(app, &x, &z, &level);
    lua_pushstring(L, DriveResultName(result));
    if( result == DRIVE_OK )
    {
        lua_newtable(L);
        lua_pushinteger(L, x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, z);
        lua_setfield(L, -2, "z");
        lua_pushinteger(L, level);
        lua_setfield(L, -2, "level");
    }
    else
        lua_pushnil(L);
    return 2;
}

static int
lua_drive_key(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    char const* name = PluginDrive_ArgString(L, 1);
    int down;
    enum DriveResult result;

    assert(app);
    luaL_checktype(L, 2, LUA_TBOOLEAN);
    down = lua_toboolean(L, 2);
    result = DriveUi_Key(app, name, down);
    return PluginDrive_PushResult(L, result, NULL);
}

static int
lua_drive_text(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    char const* text = PluginDrive_ArgString(L, 1);
    enum DriveResult result;

    assert(app);
    result = DriveUi_Text(app, text);
    return PluginDrive_PushResult(L, result, NULL);
}

static int
lua_drive_shot(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    char const* name = PluginDrive_ArgString(L, 1);
    char path[1024];
    enum DriveResult result;

    assert(app);
    result = DriveUi_Shot(app, name, path, (int)sizeof(path));
    return PluginDrive_PushResult(L, result, result == DRIVE_OK ? path : NULL);
}


static struct LuaFn const LUA_DRIVE_UI_FNS[] = {
    {"group_present", lua_drive_group_present},
    {"component", lua_drive_component},
    {"if_click", lua_drive_if_click},
    {"tab", lua_drive_tab},
    {"modal_live", lua_drive_modal_live},
    {"npcs", lua_drive_npcs},
    {"locs", lua_drive_locs},
    {"objs", lua_drive_objs},
    {"player_tile", lua_drive_player_tile},
    {"key", lua_drive_key},
    {"text", lua_drive_text},
    {"shot", lua_drive_shot},
    {NULL, NULL},
};

void
PluginDriveUi_RegisterLua(struct lua_State* L, void* script)
{
    assert(L);
    assert(script);
    PluginLua_AppendModule(L, script, LUA_DRIVE_UI_FNS);
}

#else /* !TORIRS_EMBED_SERVER */

/* No embedded server, no driver. The registrar stays so the module assembly
 * in torirs_plugin_drive.c needs no second spelling. */
void
PluginDriveUi_RegisterLua(struct lua_State* L, void* script)
{
    (void)L;
    (void)script;
}

#endif /* TORIRS_EMBED_SERVER */
