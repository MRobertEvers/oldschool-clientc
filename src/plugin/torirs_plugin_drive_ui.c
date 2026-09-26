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
#include <stdlib.h>
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

/*
 * The pose a MODEL component is drawn with right now: the three angles, the
 * zoom and the spin speeds IF_SETANGLE / IF_SETROTATESPEED (and the cache's
 * modelxan/modelyan/modelzoom before them) left in the tree.
 *
 * The read the server's if_setangle needed a witness for: the packet reaching
 * the wire proves the server half, and only the client's own component proves
 * the apply (rs_gameproto_exec.c -> UITree_ApplyModelAngle). A component that
 * is not a model has no pose, and says so -- REFUSED, not a row of zeroes.
 */
struct DriveUiModelPose
{
    int component_id;
    int model;
    int xan;
    int yan;
    int zan;
    int zoom;
    int rotate_x_speed;
    int rotate_y_speed;
};

static enum DriveResult
drive_ui_model_pose(
    struct App* app,
    char const* symbol,
    int sub,
    struct DriveUiModelPose* out)
{
    struct UITreeComponent const* c;
    int component_id;
    int32_t idx;

    assert(app);
    assert(symbol);
    assert(out);

    memset(out, 0, sizeof(*out));
    if( DriveSymbol_Lookup(DRIVE_SYMBOL_COMPONENT, symbol, &component_id) != DRIVE_OK )
        return DRIVE_NO_ROW;
    if( !app->tree )
        return DRIVE_NOT_VISIBLE;
    idx = UITree_FindByComponentId(app->tree, component_id);
    if( idx >= 0 && sub >= 0 )
        idx = UITree_FindChildBySubid(app->tree, idx, component_id, sub);
    if( idx < 0 )
        return DRIVE_NOT_VISIBLE;
    c = &app->tree->components[idx];
    if( c->type != UIELEM_RS_MODEL )
        return DRIVE_REFUSED;
    out->component_id = c->component_id;
    out->model = c->u.rs_model.gamecache_model_id;
    out->xan = c->u.rs_model.xan;
    out->yan = c->u.rs_model.yan;
    out->zan = c->u.rs_model.zan;
    out->zoom = c->u.rs_model.zoom;
    out->rotate_x_speed = c->u.rs_model.rotate_x_speed;
    out->rotate_y_speed = c->u.rs_model.rotate_y_speed;
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
DriveUi_TabByName(struct App* app, char const* name, int* out_tab_number)
{
    int number;

    assert(app);
    assert(name);
    assert(out_tab_number);

    *out_tab_number = -1;
    /* RevConfigRefs_Get answers -1 for "this revision does not have that",
     * which folds the two sources plan 5.8 names -- an explicit [tabs] row
     * and the [role:panel_<name>] derivation -- into one table already
     * built at boot (RevConfigRefs_AddItems, revconfig_refs.c:refs_add_tab_
     * from_panel_role): nothing here re-derives either. */
    number = RevConfigRefs_Get(&app->revconfig_refs, "tab", name);
    if( number < 0 )
        return DRIVE_NO_ROW;
    *out_tab_number = number;
    return DRIVE_OK;
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

/*
 * The combat half of a row: the overhead health bar and the newest live
 * hitsplat.  See struct DriveNpcRow for why a RATIO is the only hitpoints
 * reading a client has, and why `end_fill` is the one that reads 0 on death.
 *
 * Mirrors app_plugin_fill_npc_for_world's health pair (the plugin ABI's own
 * answer to the same question) rather than inventing a second scale, and
 * app_overlay_entities.c's two liveness tests -- `healthbar_end_cycle >
 * cycle` for the bar, `start <= cycle < end` for a splat -- rather than
 * guessing how long either lives.  A driver that read an expired splat slot
 * would see the same "fresh hit" forever, and a combat verb built on it would
 * never re-engage a fight that had stopped.
 */
static void
drive_ui_fill_npc_combat(
    struct App* app, struct WorldEntity_NPC const* npc, struct DriveNpcRow* out)
{
    struct WorldEntityFacet_Combat const* combat;
    int cycle;
    int i;

    assert(app);
    assert(app->world);
    assert(npc);
    assert(out);

    combat = &npc->combat;
    cycle = app->world->cycle;

    if( combat->healthbar_type >= 0 )
    {
        struct RS_HealthbarType const* type =
            RS_Healthbars_TypeFor(&app->healthbars, combat->healthbar_type);
        out->health_ratio = combat->healthbar_end_fill;
        out->health_scale = type->width > 0 ? type->width : RS_HEALTHBAR_DEFAULT_WIDTH;
        out->health_active = combat->healthbar_end_cycle > cycle;
    }
    else
    {
        out->health_ratio = -1;
        out->health_scale = -1;
        out->health_active = 0;
    }

    out->hit_damage = -1;
    out->hit_cycle = 0;
    for( i = 0; i < WORLD_ENTITY_DAMAGE_SLOTS; i++ )
    {
        if( combat->damage_start_cycles[i] > cycle || combat->damage_cycles[i] <= cycle )
            continue;
        if( combat->damage_start_cycles[i] < out->hit_cycle )
            continue;
        out->hit_cycle = combat->damage_start_cycles[i];
        out->hit_damage = (int)combat->damage_values[i];
    }
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
        drive_ui_fill_npc_combat(app, npc, &out[j]);
        drive_ui_strip_tags(npc->name, out[j].name, sizeof(out[j].name));
        /* Overhead SAY (DriveNpcRow.overhead's banner for why a test needs
         * it).  The facet clears `message` itself the cycle the timer hits 0
         * (world_cycle.c), so the two agree already; reading the timer first
         * keeps them agreeing even if a future decode leaves a stale string
         * behind, and costs nothing. */
        if( npc->chat.timer > 0 )
        {
            drive_ui_strip_tags(npc->chat.message, out[j].overhead, sizeof(out[j].overhead));
            out[j].overhead_timer = npc->chat.timer;
        }
        else
        {
            out[j].overhead[0] = '\0';
            out[j].overhead_timer = 0;
        }
        if( count < cap )
            count++;
    }
    *out_count = count;
    return DRIVE_OK;
}

/*
 * The multiloc child a placement currently draws as.
 *
 * The client's scenery entity keeps the id the map or the packet named and
 * resolves the transform table on the way to the model
 * (app_varp_refresh_loc_transforms, app_world_spawn.c's APP_SPAWN_LOC_CHANGE
 * arm), so this is the only place a reader can ask "what IS that thing".
 * Answers `loc_id` for the overwhelming majority of defs, which carry no
 * transform table at all; -1 is the table's own "hidden" and is passed
 * through rather than folded into `loc_id`, because a hidden loc and a plain
 * loc are different answers to a test asking why it cannot click something.
 */
static int
drive_ui_loc_resolved(struct App* app, int loc_id)
{
    struct ToriRS_Location* cfg;

    assert(app);
    if( loc_id < 0 || !app->provider )
        return loc_id;
    cfg = CacheProvider_LocationGet(app->provider, loc_id);
    if( !cfg || cfg->transform_count <= 0 || !cfg->transforms )
        return loc_id;
    return VarPManager_ResolveTransform(
        &app->varps, cfg->transforms, cfg->transform_count, cfg->transform_varbit,
        cfg->transform_varp);
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
    if( getenv("TORIRS_DRIVE_DEBUG") )
        fprintf(stderr,
            "drive_locs: world=%p scenery_pool=%d head=%d have_player=%d at %d,%d base=%d,%d "
            "radius=%d\n",
            (void*)app->world, pool->count, World_EntityPoolHead(pool), have_player, px, pz,
            base_x, base_z, radius);
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
        if( getenv("TORIRS_DRIVE_DEBUG") )
        {
            char sym[128] = "?";
            DriveSymbol_Name(DRIVE_SYMBOL_LOC, sc->loc_id, sym, (int)sizeof(sym));
            fprintf(stderr, "drive_locs: index %d loc_id=%d(%s) at %d,%d\n", i, sc->loc_id, sym,
                tile_x, tile_z);
        }
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
        out[j].resolved_loc_id = drive_ui_loc_resolved(app, sc->loc_id);
        out[j].tile_x = tile_x;
        out[j].tile_z = tile_z;
        out[j].level = sc->grid_position.level;
        out[j].element_id = sc->element_id;
        out[j].shape = sc->shape;
        if( count < cap )
            count++;
    }
    *out_count = count;
    return DRIVE_OK;
}

/* The one loc def DriveUi_LocVariants is currently fetching, and how many
 * times it has been asked since -- see the retry note in the body. */
static int g_drive_ui_loc_load_id = -1;
static int g_drive_ui_loc_load_polls;

/*
 * Flatten a multiloc family.  Breadth-first over `out_slots` itself: each id
 * appended is visited in turn, so a table whose child is itself a multiloc
 * (a farming patch's growth chain) comes out whole, and the dedup that guards
 * the append is also what terminates a table pointing back at an ancestor.
 *
 * A child whose own def is not resident contributes no grandchildren -- it is
 * still IN the list, which is all a symbol match needs -- and no load is
 * queued for it: the one id worth waiting for is the one the caller named,
 * and fanning loads across a whole family would queue hundreds of reads for a
 * question already answered.
 */
enum DriveResult
DriveUi_LocVariants(
    struct App* app, int loc_id, int* out_resolved, int* out_slots, int cap, int* out_count)
{
    struct ToriRS_Location* cfg;
    int head = 0;
    int count = 0;

    assert(app);
    assert(out_resolved);
    assert(out_slots);
    assert(cap > 0);
    assert(out_count);
    assert(loc_id >= 0);

    *out_count = 0;
    *out_resolved = loc_id;
    if( !app->provider )
        return DRIVE_NO_ROW;

    cfg = CacheProvider_LocationGet(app->provider, loc_id);
    if( !cfg )
    {
        /* One load in flight per id, not one per poll.  The caller polls this
         * from an await's `level`, which runs once a FRAME, and
         * CreateTask_Dat2LocLoad dedups only against residency (it has no
         * in-flight check) -- so asking plainly would queue thirty group
         * reads a tick for one answer.  The retry every DRIVE_UI_LOC_LOAD_
         * RETRY polls is there because a dropped task would otherwise hang
         * the await for its whole deadline with nothing left fetching. */
        enum
        {
            DRIVE_UI_LOC_LOAD_RETRY = 30
        };
        struct ToriRS_Task* load = NULL;

        if( loc_id != g_drive_ui_loc_load_id )
        {
            g_drive_ui_loc_load_id = loc_id;
            g_drive_ui_loc_load_polls = 0;
            load = CreateTask_LocLoad(app->provider, loc_id);
        }
        else if( ++g_drive_ui_loc_load_polls % DRIVE_UI_LOC_LOAD_RETRY == 0 )
            load = CreateTask_LocLoad(app->provider, loc_id);
        else
            return DRIVE_TIMEOUT;
        /* CreateTask_LocLoad answers NULL when there is nothing left to fetch;
         * with the config still absent after that, this provider cannot
         * produce one and no amount of polling will change that. */
        if( !load )
            return DRIVE_NOT_FOUND;
        ToriRS_TaskQueue_Add(app->runner.queue, load);
        return DRIVE_TIMEOUT;
    }
    if( loc_id == g_drive_ui_loc_load_id )
        g_drive_ui_loc_load_id = -1;
    *out_resolved = drive_ui_loc_resolved(app, loc_id);

    for( ;; )
    {
        int i;

        if( cfg && cfg->transforms )
            for( i = 0; i < cfg->transform_count && count < cap; i++ )
            {
                int child = cfg->transforms[i];
                int seen = child < 0 || child == loc_id;
                int j;

                for( j = 0; !seen && j < count; j++ )
                    seen = out_slots[j] == child;
                if( seen )
                    continue;
                out_slots[count++] = child;
            }
        if( head >= count )
            break;
        cfg = CacheProvider_LocationGet(app->provider, out_slots[head++]);
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

/*
 * The unchanged frame, and why a picture of it is not written.
 *
 * t.exec/t.check shoot after EVERY verb, including the many that change
 * nothing on screen -- an await that was already satisfied, a count read out
 * of the backpack, a walk to a tile the player is standing on. Four
 * consecutive shots of Sheep Herder (equip-trousers, sheep1-present,
 * walk-near-sheep1, blocked) came out BYTE-IDENTICAL, and gate.py's
 * duplicate-MD5 rule -- which exists to catch a driver that drove nothing --
 * then failed the quest for it. The author had done nothing wrong; the
 * harness had photographed the same frame four times and the gate called
 * that a dead driver.
 *
 * So the driver stops writing the same picture twice in a row of its own
 * accord: a capture whose bytes are identical to the LAST SHOT THIS RUN
 * ACTUALLY WROTE is deleted again and answered ("ok", "unchanged since
 * <that shot's name>"), the Lua side drops the name from the row's `shots`
 * column, and the row's detail says `[frame unchanged]` instead. The gate's
 * duplicate-MD5 rule is left exactly as it was: it can no longer fire on
 * this class at all, and it still catches a driver whose every INTERACTION
 * produced the same frame, because those shots are the ones that are
 * suppressed rather than duplicated -- a quest that really drove nothing
 * ends up with one PNG and a ledger full of `[frame unchanged]`, which is
 * far more legible than N copies of one picture.
 *
 * The comparison is the written PNG's own bytes, not the framebuffer: the
 * pixels this capture is made of live in app_plugin_screenshots_write
 * (src/app/app_plugin_assets.c), one frame shared by every plugin's pending
 * request and encoded there once -- reaching into that shared path to hash
 * it for the quest driver alone is a bigger change, in a file this owner
 * does not own, for the same answer. Encoding is deterministic (one
 * tdefl_write_image_to_png_file_in_memory_ex call with fixed settings), so
 * equal frames are equal files and "byte-identical PNG" is precisely the
 * equivalence gate.py's MD5 rule already measures.
 *
 * `keep` is the one way past it, and t.exec's `<name>-FAIL` capture is what
 * it is for: a FAIL row must keep its picture even when the failing verb
 * changed nothing, because that picture is the first thing a human opens.
 * A kept shot still becomes the new "last written", so the next capture is
 * compared against what is really on disk.
 */
static unsigned char* g_drive_ui_shot_last_bytes;
static long g_drive_ui_shot_last_size;
static char g_drive_ui_shot_last_name[192];
/* The in-flight request's own name and keep flag: the poll branch below is
 * re-entered a frame or two later and only the REQUEST knew them. */
static char g_drive_ui_shot_name[192];
static int g_drive_ui_shot_keep;

static unsigned char*
drive_ui_shot_read(char const* path, long* out_size)
{
    FILE* file;
    unsigned char* bytes;
    long size;

    assert(path);
    assert(out_size);

    *out_size = 0;
    file = fopen(path, "rb");
    if( !file )
        return NULL;
    if( fseek(file, 0, SEEK_END) != 0 )
    {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if( size <= 0 )
    {
        fclose(file);
        return NULL;
    }
    rewind(file);
    bytes = malloc((size_t)size);
    assert(bytes);
    if( fread(bytes, 1, (size_t)size, file) != (size_t)size )
    {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *out_size = size;
    return bytes;
}

/* 1 when `path` is byte-identical to the last shot this run wrote -- in
 * which case `path` is removed again and the cache is left naming that
 * earlier shot. 0 otherwise, and then `path` IS the last shot from here on. */
static int
drive_ui_shot_dedupe(char const* path, char const* name, int keep)
{
    unsigned char* bytes;
    long size = 0;

    assert(path);
    assert(name);

    bytes = drive_ui_shot_read(path, &size);
    if( !bytes )
    {
        /* stat() said there was a file and it could not be read back. Forget
         * what the last frame was rather than compare the NEXT shot against
         * a cache that no longer describes anything on disk: an unreadable
         * capture costs one duplicate picture at worst, a stale cache
         * silently drops a frame that really did change. */
        free(g_drive_ui_shot_last_bytes);
        g_drive_ui_shot_last_bytes = NULL;
        g_drive_ui_shot_last_size = 0;
        g_drive_ui_shot_last_name[0] = '\0';
        return 0;
    }
    if( !keep && g_drive_ui_shot_last_bytes && size == g_drive_ui_shot_last_size &&
        memcmp(bytes, g_drive_ui_shot_last_bytes, (size_t)size) == 0 )
    {
        free(bytes);
        remove(path);
        return 1;
    }
    free(g_drive_ui_shot_last_bytes);
    g_drive_ui_shot_last_bytes = bytes;
    g_drive_ui_shot_last_size = size;
    snprintf(g_drive_ui_shot_last_name, sizeof(g_drive_ui_shot_last_name), "%s", name);
    return 0;
}

static enum DriveResult
drive_ui_shot(
    struct App* app,
    char const* name,
    int keep,
    char* out_path,
    int out_cap,
    int* out_unchanged)
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
    if( out_unchanged )
        *out_unchanged = 0;

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
            /* The banner above: an identical picture is deleted again and
             * the answer names the shot it would have duplicated, so the
             * detail a test sees is never a path that is not there. */
            int unchanged = drive_ui_shot_dedupe(
                g_drive_ui_shot_path, g_drive_ui_shot_name, g_drive_ui_shot_keep);
            if( unchanged )
                snprintf(out_path, (size_t)out_cap, "unchanged since %s",
                         g_drive_ui_shot_last_name);
            else
                snprintf(out_path, (size_t)out_cap, "%s", g_drive_ui_shot_path);
            if( out_unchanged )
                *out_unchanged = unchanged;
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
    snprintf(g_drive_ui_shot_name, sizeof(g_drive_ui_shot_name), "%s", name);
    g_drive_ui_shot_keep = keep;
    g_drive_ui_shot_misses = 0;
    app->need_redraw = 1;
    return DRIVE_TIMEOUT; /* queued; the caller polls again next frame */
}

/* The declared seam (torirs_plugin_drive.h), unchanged: a caller that has no
 * opinion about duplicate frames gets the ordinary suppressing capture. */
enum DriveResult
DriveUi_Shot(struct App* app, char const* name, char* out_path, int out_cap)
{
    return drive_ui_shot(app, name, 0, out_path, out_cap, NULL);
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

/* api_drive.model_pose(symbol [, sub]) -> result, {component, model, xan,
 * yan, zan, zoom, x_speed, y_speed} | nil. */
static int
lua_drive_model_pose(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    char const* symbol = PluginDrive_ArgString(L, 1);
    int sub = PluginDrive_ArgOptInt(L, 2, -1);
    struct DriveUiModelPose pose;
    enum DriveResult result;

    assert(app);
    result = drive_ui_model_pose(app, symbol, sub, &pose);
    lua_pushstring(L, DriveResultName(result));
    if( result != DRIVE_OK )
    {
        lua_pushnil(L);
        return 2;
    }
    lua_createtable(L, 0, 8);
    lua_pushinteger(L, pose.component_id);
    lua_setfield(L, -2, "component");
    lua_pushinteger(L, pose.model);
    lua_setfield(L, -2, "model");
    lua_pushinteger(L, pose.xan);
    lua_setfield(L, -2, "xan");
    lua_pushinteger(L, pose.yan);
    lua_setfield(L, -2, "yan");
    lua_pushinteger(L, pose.zan);
    lua_setfield(L, -2, "zan");
    lua_pushinteger(L, pose.zoom);
    lua_setfield(L, -2, "zoom");
    lua_pushinteger(L, pose.rotate_x_speed);
    lua_setfield(L, -2, "x_speed");
    lua_pushinteger(L, pose.rotate_y_speed);
    lua_setfield(L, -2, "y_speed");
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
lua_drive_tab_by_name(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    char const* name = PluginDrive_ArgString(L, 1);
    int tab_number = -1;
    enum DriveResult result;

    assert(app);
    result = DriveUi_TabByName(app, name, &tab_number);
    lua_pushstring(L, DriveResultName(result));
    if( result == DRIVE_OK )
        lua_pushinteger(L, tab_number);
    else
        lua_pushnil(L);
    return 2;
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
    /* The combat half (struct DriveNpcRow): a RATIO out of the healthbar
     * type's own denominator, never hitpoints -- the client is never told an
     * npc's hitpoints.  -1/-1 is "no bar has ever been sent for this npc". */
    lua_pushinteger(L, row->health_ratio);
    lua_setfield(L, -2, "health_ratio");
    lua_pushinteger(L, row->health_scale);
    lua_setfield(L, -2, "health_scale");
    lua_pushboolean(L, row->health_active);
    lua_setfield(L, -2, "health_active");
    lua_pushinteger(L, row->hit_damage);
    lua_setfield(L, -2, "hit_damage");
    lua_pushinteger(L, row->hit_cycle);
    lua_setfield(L, -2, "hit_cycle");
    lua_pushstring(L, row->name);
    lua_setfield(L, -2, "name");
    /* The overhead half: `npc_say`'s SAY mask, which never reaches the
     * chatbox (SS_OP_NPC_SAY) and so is invisible to api_drive.messages.
     * Always a string here: "" is "nothing overhead".  A `nil` therefore
     * means one thing only -- a binary built before this field -- which is
     * what pointer.lua's reader tests for, so a quest run on the shared
     * torirs_questtest degrades to the old behaviour instead of lying. */
    lua_pushstring(L, row->overhead);
    lua_setfield(L, -2, "overhead");
    lua_pushinteger(L, row->overhead_timer);
    lua_setfield(L, -2, "overhead_timer");
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
    /*
     * R-B (locs): 64 was the npc-pool figure, carried over without checking
     * the scenery pool's own scale. A live Lumbridge walk (TORIRS_DRIVE_DEBUG=1)
     * found 8,515 scenery entities in the world and 7,479 inside radius 60 of
     * the player -- walls, fences and floor decor alone dwarf 64, so the
     * nearest-K insertion above filled entirely with those and every "tree"
     * (53 within that same radius) ranked outside the kept window and never
     * reached DriveUi_Locs's caller. world.loc_near's own linear scan for a
     * matching loc_id then saw a truncated table and answered not_found on a
     * symbol the map is "full of" (test/quests/_conformance.lua's own
     * comment on LOC_SYMBOL) -- this cap, not the symbol table or the pool
     * walk, was R-B's remaining bug. 8192 covers the observed pool with
     * headroom; `static` (like g_drive_ui_shot_path above) keeps a buffer
     * this size off the stack. */
    enum
    {
        DRIVE_UI_POOL_CAP = 8192
    };
    struct App* app = PluginDrive_App();
    int radius = PluginDrive_ArgOptInt(L, 1, 0);
    static struct DriveLocRow rows[DRIVE_UI_POOL_CAP];
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
        lua_pushinteger(L, rows[i].resolved_loc_id);
        lua_setfield(L, -2, "resolved_loc_id");
        lua_pushinteger(L, rows[i].tile_x);
        lua_setfield(L, -2, "x");
        lua_pushinteger(L, rows[i].tile_z);
        lua_setfield(L, -2, "z");
        lua_pushinteger(L, rows[i].level);
        lua_setfield(L, -2, "level");
        lua_pushinteger(L, rows[i].element_id);
        lua_setfield(L, -2, "element_id");
        lua_pushinteger(L, rows[i].shape);
        lua_setfield(L, -2, "shape");
        lua_rawseti(L, -2, i + 1);
    }
    return 2;
}

/* (result, {resolved = <id>, slots = {<id>, ...}}).  `timeout` means the def
 * is being fetched -- poll again next frame; the second return is nil, the way
 * every non-ok read in this module answers. */
static int
lua_drive_loc_variants(struct lua_State* L)
{
    enum
    {
        DRIVE_UI_VARIANT_CAP = 64
    };
    struct App* app = PluginDrive_App();
    int loc_id = PluginDrive_ArgInt(L, 1);
    int slots[DRIVE_UI_VARIANT_CAP];
    int resolved = loc_id;
    int count = 0;
    enum DriveResult result;
    int i;

    assert(app);
    if( loc_id < 0 )
        return luaL_error(L, "drive.loc_variants: loc id %d is not a resolved symbol", loc_id);
    result = DriveUi_LocVariants(app, loc_id, &resolved, slots, DRIVE_UI_VARIANT_CAP, &count);
    lua_pushstring(L, DriveResultName(result));
    if( result != DRIVE_OK )
    {
        lua_pushnil(L);
        return 2;
    }
    lua_newtable(L);
    lua_pushinteger(L, resolved);
    lua_setfield(L, -2, "resolved");
    lua_newtable(L);
    for( i = 0; i < count; i++ )
    {
        lua_pushinteger(L, slots[i]);
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "slots");
    return 2;
}

static int
lua_drive_objs(struct lua_State* L)
{
    /* Same audit as lua_drive_locs above (R-B): ground-item stacks are far
     * fewer than static scenery in the areas this driver has actually
     * walked, so 64 has not been observed to truncate one, but the failure
     * mode is identical if a quest test ever drops enough loot nearby --
     * raised alongside locs' fix rather than leaving a same-shaped trap. */
    enum
    {
        DRIVE_UI_POOL_CAP = 2048
    };
    struct App* app = PluginDrive_App();
    int radius = PluginDrive_ArgOptInt(L, 1, 0);
    static struct DriveObjRow rows[DRIVE_UI_POOL_CAP];
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

/* 1 while the in-flight capture is still registered under
 * app->plugin_screenshots, i.e. the renderer has not taken its pixels yet. */
static int
drive_ui_shot_slot_busy(struct App const* app)
{
    int i;

    assert(app);
    if( !g_drive_ui_shot_path[0] )
        return 0;
    for( i = 0; i < APP_PLUGIN_SCREENSHOTS_MAX; i++ )
        if( app->plugin_screenshots[i].in_use &&
            strcmp(app->plugin_screenshots[i].path, g_drive_ui_shot_path) == 0 )
            return 1;
    return 0;
}

/* api.drive.shot(name, keep) -> (result, detail, unchanged, captured).
 *
 * A THIRD return value, and ui.lua's QD.shot is the only reader: on an
 * `unchanged` capture the detail is "unchanged since <name>" rather than a
 * path, and the two are told apart by this boolean instead of by parsing
 * that sentence. `keep` (t.exec's `-FAIL` shot) writes the picture whether
 * it changed or not.
 *
 * The FOURTH, `captured`, is true once the renderer has taken this
 * capture's pixels (its slot has left app->plugin_screenshots) -- including
 * on a `timeout` answer, while the file is still being written.  QD.shot
 * aims the camera for a photograph and must not put the press pose back
 * before the frame it is taken from has been drawn: a draw the frame pacer
 * skipped defers the capture by a frame or more, so "the next poll" is not
 * that moment and this is. */
static int
lua_drive_shot(struct lua_State* L)
{
    struct App* app = PluginDrive_App();
    char const* name = PluginDrive_ArgString(L, 1);
    int keep = lua_toboolean(L, 2);
    char path[1024];
    int unchanged = 0;
    enum DriveResult result;

    assert(app);
    result = drive_ui_shot(app, name, keep, path, (int)sizeof(path), &unchanged);
    PluginDrive_PushResult(L, result, result == DRIVE_OK ? path : NULL);
    lua_pushboolean(L, unchanged);
    lua_pushboolean(L, result != DRIVE_TIMEOUT || !drive_ui_shot_slot_busy(app));
    return 4;
}


static struct LuaFn const LUA_DRIVE_UI_FNS[] = {
    {"group_present", lua_drive_group_present},
    {"component", lua_drive_component},
    {"model_pose", lua_drive_model_pose},
    {"if_click", lua_drive_if_click},
    {"tab", lua_drive_tab},
    {"tab_by_name", lua_drive_tab_by_name},
    {"modal_live", lua_drive_modal_live},
    {"npcs", lua_drive_npcs},
    {"locs", lua_drive_locs},
    {"loc_variants", lua_drive_loc_variants},
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
