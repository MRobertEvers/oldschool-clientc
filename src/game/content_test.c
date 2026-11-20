#include "content_test.h"
#include "app.h"
#include "cmd/cmdbus.h"
#include "net/net.h"
#include "input/torirs_keymap.h"
#include "platform/net_transport.h"
#include "platform/platform_window.h"
#include "ui/uitree_layout.h"
#include "painters/painters.h"
#include "content_test_sailing.h"
#include "plugin/torirs_plugin_host.h"
#include "game/rs_cs2_dispatch.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Asked several times a frame by the main loop and the drive plugin; the
 * environment is fixed before the first frame, so it is read once. */
int ContentTest_Enabled(void)
{
    static int enabled = -1;
    if( enabled < 0 )
    {
        const char* dir = getenv("TORIRS_CONTENT_TEST");
        const char* defer_ms = getenv("TORIRS_IO_DEFER_MS");
        enabled = dir && *dir;
        /* A wall-clock read deferral against the test's virtual clock: the
         * frame budget is spent in a wall second while the first read is
         * still out, and the run ends with no row. Refused, by name, for the
         * knob that counts in the frames this clock does have. */
        if( enabled && defer_ms && strtol(defer_ms, NULL, 0) > 0 )
        {
            fprintf(stderr,
                    "content_test: TORIRS_IO_DEFER_MS=%s cannot run under the content test: its frames run "
                    "on a virtual clock and cost microseconds of wall time, so a deferral in wall "
                    "milliseconds outlasts the whole frame budget. Use TORIRS_IO_DEFER_FRAMES=n (with "
                    "TORIRS_CONTENT_TEST_RACE=1 or TORIRS_CONTENT_TEST_HOLD=1).\n",
                    defer_ms);
            exit(2);
        }
    }
    return enabled;
}

int ContentTest_BackgroundRebuild(void)
{
    static int on = -1;
    if( on < 0 )
    {
        const char* race = getenv("TORIRS_CONTENT_TEST_RACE");
        const char* background = getenv("TORIRS_CONTENT_TEST_BACKGROUND");
        on = ContentTest_Enabled() && race && atoi(race) != 0 && background && atoi(background) != 0;
    }
    return on;
}

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER
/* TORIRS_CONTENT_TEST_TICK_ONLY, read once (asked every frame of a test run).
 * Its readers sit in the embedded-server block below, so it lives behind the
 * same guard: a client without the embedded server has no caller for it. */
static int content_test_tick_only(void)
{
    static int on = -1;
    if( on < 0 )
        on = getenv("TORIRS_CONTENT_TEST_TICK_ONLY") != NULL;
    return on;
}
#endif /* TORIRS_EMBED_SERVER */

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER
#include "torirsserver/torirs_server_embed.h"
#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_content.h"
#include "torirsserver/torirs_server_scene.h"
#include "serverscript/ssvm_provider.h"
#include "world/world.h"
#include <toridraw_scene.h>
#include "varp/varp_manager.h"
#include "varc/varc_manager.h"
#include "plugin/torirs_plugin_drive.h"

/* One warm process, one virtual clock. IO/picking/UI continue at frozen time.
 * A step never waits out the 600 ms server tick in wall-clock time. */
struct ContentTestRun
{
    uint64_t test_now, last_real, started_us;
    int remaining, active, finishing, running, publish_pending, restore_pending;
    int observe_requested, observe_drawn, observe_x, observe_z, observe_bit;
    char observe_widget[512], observe_npcs[512];
    int click_phase, click_x, click_y, click_button;
    int hover_requested;
    char command[2048], result[16384], capture_path[1024];
};
static struct ContentTestRun g_content_test = { .test_now = 1000, .click_phase = -1 };

static int reload_scripts(struct ToriRSServer* srv, const char* directory)
{
    /* Queue/timer ids are compiler allocations. Preserve work by its declared
     * name and argument signature across a warm reload, never by the old id. */
    enum { MAX_BINDINGS=TORIRSSERVER_PLAYER_MAX *
        (TORIRSSERVER_QUEUE_MAX+TORIRSSERVER_ENGINE_QUEUE_MAX+TORIRSSERVER_TIMER_MAX) };
    struct Binding { int* id; int* active; int argc; char* name; };
    struct Binding* bindings=calloc(MAX_BINDINGS,sizeof(*bindings));
    assert(bindings);
    int count=0;
    for( int p=0; p<TORIRSSERVER_PLAYER_MAX; ++p )
    {
        struct ToriRSServerPlayer* player=&srv->players[p];
        for( int kind=0; kind<3; ++kind )
        {
            int length=kind==0 ? TORIRSSERVER_QUEUE_MAX : kind==1 ? TORIRSSERVER_ENGINE_QUEUE_MAX : TORIRSSERVER_TIMER_MAX;
            for( int n=0; n<length; ++n )
            {
                int *id, *enabled, argc=0;
                if( kind==2 ) { id=&player->timers[n].script_id; enabled=&player->timers[n].active; }
                else
                {
                    struct ToriRSServerQueued* queue=kind ? &player->engine_queue[n] : &player->queue[n];
                    id=&queue->script_id; enabled=&queue->active; argc=queue->argc;
                }
                if( !*enabled ) continue;
                const struct SSVM_Script* script=srv->scripts ? SSVM_ProviderGet(srv->scripts,*id) : NULL;
                if( !script || !script->name ) { *enabled=0; continue; }
                struct Binding* binding=&bindings[count++];
                binding->id=id; binding->active=enabled; binding->argc=argc;
                size_t length=strlen(script->name)+1;
                binding->name=malloc(length);
                assert(binding->name);
                memcpy(binding->name,script->name,length);
            }
        }
    }
    ToriRSServer_ScriptsFree(srv);
    int ok=ToriRSServer_ScriptsLoad(srv,directory);
    for( int n=0; n<count; ++n )
    {
        struct Binding* binding=&bindings[n];
        const struct SSVM_Script* script=ok ? SSVM_ProviderGetByName(srv->scripts,binding->name) : NULL;
        if( script && script->int_arg_count==binding->argc && script->string_arg_count==0 )
            *binding->id=script->id;
        else *binding->active=0;
        free(binding->name);
    }
    free(bindings);
    ContentTestSailing_Clear();
    return ok;
}

int ContentTest_DrawRequested(struct App* app)
{
    if( !ContentTest_Enabled() || !App_FrameSettled(app) || App_IsBooting(app, NULL) || !App_WorldSettled(app) ) return 0;
    if( g_content_test.active && g_content_test.observe_requested && !g_content_test.remaining && !g_content_test.observe_drawn )
    { g_content_test.observe_drawn = 1; return 1; }
    {
        int forced = (g_content_test.active && (g_content_test.capture_path[0] || g_content_test.click_phase >= 0 || g_content_test.hover_requested)) || g_content_test.running;
        /* Quest mode (docs/QUEST_DRIVER_REMAINING.md B0): PluginDrive_
         * QuestScriptPath() runs entirely below the `if( !active )` mailbox
         * block above (ContentTest_Begin returns before it), so `active` and
         * `running` are unconditionally 0 for the whole run and `forced`
         * above is always 0 -- this file's own forced-draw branch does
         * nothing in quest mode.
         *
         * Measured (B0 spike, a throwaway click_minimenu run traced frame by
         * frame): a quest-mode world renders exactly ONCE, early after
         * login, and then never again once nothing else raises the redraw request
         * (no animation, no camera drift) -- app->world_views.frame->world_pickset and the
         * per-entity screen-position caches freeze at that one frame's
         * contents for the rest of the run. A click verb that lands after
         * that point waits out its whole deadline against a pickset that
         * will never update, and answers `covered` on a target that is
         * plainly on screen.
         *
         * The fix mirrors `hover_requested` above rather than inventing a
         * new mechanism: while the quest coroutine has a LEVEL await armed
         * (PluginDriveCore_LevelAwaitPending -- pick_holds, menu_visible,
         * player_idle, ... every one of them reads live rendered/engine
         * state on each poll, per QUEST_DRIVER_DESIGN.md's EDGE+LEVEL rule),
         * force a draw so that poll has a current frame to read. An
         * edge-only await (event= with no level=) is left alone: it
         * resolves off a discrete event, not off rendered state, and
         * forcing a draw for it would only cost frames for nothing.
         *
         * Not under render skip (App::render_skip): there every await is a
         * level await -- t.wait is one -- so this would draw every frame of
         * the run. The reads that DO need a drawn frame (the pickset) ask for
         * one themselves through App_RenderSkipRequestDraw. */
        if( PluginDrive_QuestScriptPath() && PluginDriveCore_LevelAwaitPending() &&
            !App_RenderSkipEnabled(app) )
            forced = 1;
        /* TEMPORARY, env-gated like the existing TORIRS_DRIVE_DEBUG
         * precedent (torirs_plugin_drive_ui.c): confirms the fix above by
         * showing the pickset actually staying current instead of freezing.
         * Remove once B1-B3 are green and this stops earning its keep. */
        static int pickset_trace = -1;
        if( pickset_trace < 0 )
            pickset_trace = getenv("TORIRS_QD_PICKSET_TRACE") != NULL;
        if( pickset_trace )
            fprintf(
                stderr,
                "pickset_trace: now=%llu forced=%d active=%d running=%d pickset_count=%d\n",
                (unsigned long long)g_content_test.test_now,
                forced,
                g_content_test.active,
                g_content_test.running,
                app->world_views.frame->world_pickset.count);
        return forced;
    }
}

/* Hash the posed mesh actually handed to the renderer, not just its frame
 * counter. This catches a bound sequence whose geometry never deforms. */
static void geometry_json(struct App* app, int id, char* out, size_t capacity)
{
    struct ToriDraw_SceneElement* e = id >= 0 ? ToriDraw_SceneElementGet(app->scene, id) : NULL;
    const struct ToriDraw_Model* model = e && ToriDraw_ModelKindIsFull(e->model.kind) ? ToriDraw_ModelRead(e->model) : NULL;
    uint32_t hash = 2166136261u;
    int count = model ? model->vertex_count : 0, min_y = 0, max_y = 0;
    for( int i = 0; i < count; i++ )
    {
        hash = (hash ^ (uint32_t)model->vertices_x[i]) * 16777619u;
        hash = (hash ^ (uint32_t)model->vertices_y[i]) * 16777619u;
        hash = (hash ^ (uint32_t)model->vertices_z[i]) * 16777619u;
        if( !i || model->vertices_y[i] < min_y ) min_y = model->vertices_y[i];
        if( !i || model->vertices_y[i] > max_y ) max_y = model->vertices_y[i];
    }
    snprintf(out, capacity, "{\"vertices\":%d,\"hash\":%u,\"min_y\":%d,\"max_y\":%d,\"posed_frame\":%d,\"yaw\":%d}",
        count, hash, min_y, max_y, e ? e->posed_frame : -1, e ? e->world_position.yaw : -1);
}

static void path(char* out, size_t cap, const char* file)
{
    snprintf(out, cap, "%s/%s", getenv("TORIRS_CONTENT_TEST"), file);
}

static int settled(struct App* app)
{
    return !App_AsyncPending(app) && App_FrameSettled(app) && App_WorldSettled(app);
}

static int widget(struct App* app, const char* name, int sub)
{
    int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, name);
    if( id < 0 || !app->tree ) return -1;
    int idx = UITree_FindByComponentId(app->tree, id);
    if( idx >= 0 && sub >= 0 )
        idx = UITree_FindChildBySubid(app->tree, idx, id, sub);
    return idx;
}

static void state_json(struct App* app, struct ToriRSServerEmbed* embed, char* out, size_t capacity)
{
    char result[16384];
    struct ToriRSServerPlayer* server = embed ? ToriRSServer_EmbedPlayer(embed, 0) : NULL;

    snprintf(
        result,
        sizeof(result),
        "{\"ok\":true,\"online\":%s,\"paused\":%s,\"time_ms\":%llu,\"server_x\":%d,\"server_z\":%d,"
        "\"level\":%d,\"aboard_view\":%d,\"logic_cycle\":%llu,\"server_tick\":%d,\"camera_yaw\":%d,"
        "\"camera_pitch\":%d,\"camera_zoom\":%d,\"world_ready\":%d,\"renderer\":%d}",
        server ? "true" : "false",
        g_content_test.running ? "false" : "true",
        (unsigned long long)g_content_test.test_now,
        server ? server->x : -1,
        server ? server->z : -1,
        server ? server->level : -1,
        Wevs_AboardView(&app->wevs),
        (unsigned long long)FrameClockSerial_LogicCycle(&app->frame_clock.serial),
        server ? ToriRSServer_EmbedWorld(embed)->tick : -1,
        app->world_views.frame->orbit.yaw,
        app->world_views.frame->orbit.pitch,
        app->world_views.frame->world_cam_zoom,
        app->world ? app->world->load_complete : 0,
        RenderMode_World(&app->render_mode));
    struct WorldEntity_Player* player =
        app->world ? World_PlayerGetByServerPid(app->world, app->esync.local_pid) : NULL;
    int base_x = app->world ? app->world->_base_tile_x : 0;
    int base_z = app->world ? app->world->_base_tile_z : 0;
    if( Wevs_AboardView(&app->wevs) > 0 )
    {
        struct Worldview* view = WorldviewRegistry_Get(&app->worldviews, Wevs_AboardView(&app->wevs));
        if( view )
        {
            base_x = view->base_x;
            base_z = view->base_z;
        }
        }
        size_t client_end = strlen(result);
        snprintf(result + client_end - 1, sizeof(result) - client_end + 1,
            ",\"x\":%d,\"z\":%d,\"camera\":%d,\"animation\":%d,\"anim_frame\":%d,\"chat_blocked\":%d}",
            player ? base_x + player->grid_position.x : -1,
            player ? base_z + player->grid_position.z : -1,
            CamScript_IsScripted(&app->cam_script), player ? player->animation.primary.anim_id : -1,
            player ? player->animation.primary.frame : -1, VarCManager_GetInt(&app->varcs, 11));
        size_t facing_end = strlen(result);
        snprintf(result + facing_end - 1, sizeof(result) - facing_end + 1,
            ",\"player_yaw\":%d}", player ? player->orientation.yaw : -1);
        int cutscene_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "varb542_cutscene_status");
        int fov_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, "varb4606_fov_clamp");
        int visible_orbs = 0;
        /* The minimap-orbs plugin owns one image control per orb, created
         * under these keys; an orb counts when its control exists and no
         * ancestor hides it. */
        const char* orb_keys[] = { "orb_hitpoints", "orb_prayer", "orb_run", "orb_special" };
        /* tree-walk-exempt: content-test harness, not the client */
        for( uint32_t i = 0; app->tree && i < app->tree->component_count; ++i )
        {
            struct UITreeComponent const* c = &app->tree->components[i];
            if( c->freed || !c->plugin_owner || !c->plugin_key )
                continue;
            for( int k = 0; k < 4; ++k )
                if( strcmp(c->plugin_key, orb_keys[k]) == 0 && !UITree_NodeOrAncestorDisplayHidden(app->tree, (int32_t)i) )
                    ++visible_orbs;
        }
        int move_lx, move_lz, move_height, look_lx, look_lz, look_height;
        CamScript_MoveTarget(&app->cam_script, &move_lx, &move_lz, &move_height);
        CamScript_LookTarget(&app->cam_script, &look_lx, &look_lz, &look_height);
        size_t camera_end = strlen(result);
        snprintf(
            result + camera_end - 1,
            sizeof(result) - camera_end + 1,
            ",\"cinematic\":{\"eye_x\":%d,\"eye_z\":%d,\"eye_height\":%d,"
            "\"look_x\":%d,\"look_z\":%d,\"look_height\":%d,"
            "\"render_yaw\":%d,\"render_pitch\":%d,\"render_x\":%d,\"render_y\":%d,\"render_z\":%d,"
            "\"hide_panels\":%d,\"fov_mode\":%d,\"visible_orbs\":%d}}",
            move_lx + base_x,
            move_lz + base_z,
            move_height,
            look_lx + base_x,
            look_lz + base_z,
            look_height,
            app->world_views.frame->world_camera.yaw,
            app->world_views.frame->world_camera.pitch,
            app->world_views.frame->world_camera_pos.x + base_x * 128,
            app->world_views.frame->world_camera_pos.y,
            app->world_views.frame->world_camera_pos.z + base_z * 128,
            cutscene_bit >= 0 ? VarPManager_GetVarbit(&app->varps, cutscene_bit) : -1,
            fov_bit >= 0 ? VarPManager_GetVarbit(&app->varps, fov_bit) : -1,
            visible_orbs);
        char rig[256];
        geometry_json(app, player ? player->element_id : -1, rig, sizeof(rig));
        size_t rig_end = strlen(result);
        snprintf(result + rig_end - 1, sizeof(result) - rig_end + 1, ",\"rig\":%s}", rig);
        char sailing[4096];
        ContentTestSailing_State(app, embed, sailing, sizeof(sailing));
        size_t n = strlen(result);
        snprintf(result + n - 1, sizeof(result) - n + 1, ",\"sailing\":%s}", sailing);
        if( server )
        {
            n=strlen(result);
            snprintf(result+n-1,sizeof(result)-n+1,
                ",\"server_activity\":{\"active_script\":%d,\"action_locked\":%d,\"delay_until\":%d,\"waypoint\":%d,\"kind\":%d,\"op\":%d,\"target\":%d,\"x\":%d,\"z\":%d,\"use_on\":%d,\"last_useitem\":%d,\"last_useslot\":%d,\"client_selected_obj\":%d}}",
                server->active_script!=NULL,server->action_locked,server->delayed_until,
                server->waypoint_index,server->interaction.kind,server->interaction.op,
                server->interaction.target_id,server->interaction.x,server->interaction.z,
                server->interaction.use_on,server->last_useitem,server->last_useslot,
                app->target_select.obj.active ? app->target_select.obj.obj_id : -1);
        }

    snprintf(out, capacity, "%s", result);
}

static void scenery_json(struct App* app, int x, int z, char* out, size_t capacity)
{
    char result[16384];
    if( !app->world ) { snprintf(out, capacity, "{\"ok\":true,\"items\":[]}"); return; }
            strcpy(result, "{\"ok\":true,\"items\":[");
            int count = 0;
            for( int view_id=0; view_id<WORLDVIEW_MAX; ++view_id )
            {
            if( !WorldviewRegistry_IsLive(&app->worldviews,view_id) ) continue;
            struct Worldview* view=WorldviewRegistry_Get(&app->worldviews,view_id);
            struct World* world=view->world;
            int base_x=view_id ? view->base_x : world->_base_tile_x;
            int base_z=view_id ? view->base_z : world->_base_tile_z;
            struct World_EntityPool* pool = &world->entities.scenery;
            for( int i = World_EntityPoolHead(pool); i >= 0; i = World_EntityPoolNext(pool, i) )
            {
                struct WorldEntity_Scenery* sc = World_EntityPoolGet(pool, i);
                if( !sc || sc->grid_position.x + base_x != x ||
                    sc->grid_position.z + base_z != z ) continue;
                struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(app->scene, sc->element_id);
                char rig[256];
                geometry_json(app, sc->element_id, rig, sizeof(rig));
                size_t used = strlen(result);
                snprintf(result + used, sizeof(result) - used,
                    "%s{\"loc\":%d,\"seq\":%d,\"frame\":%d,\"cycle\":%d,\"angle\":%d,\"element\":%d,\"rig\":%s,\"view\":%d}",
                    count++ ? "," : "", sc->loc_id, element ? element->anim_seq_id : -1, element ? element->anim_frame : -1,
                    element ? element->anim_cycle : -1, sc->angle, sc->element_id, rig, view_id);
            }
            }
            size_t used = strlen(result);
            snprintf(result + used, sizeof(result) - used, "]}");
    snprintf(out, capacity, "%s", result);
}

static void npc_json(struct App* app, struct ToriRSServerEmbed* embed,
                     const char* name, char* out, size_t capacity)
{
    char result[16384];
    struct ToriRSServerPlayer* server = embed ? ToriRSServer_EmbedPlayer(embed, 0) : NULL;
    int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, name);
    int count = 0, x = -1, z = -1, truncated = 0;
    int server_count = 0, server_x = -1, server_z = -1;
    struct ToriRSServer* srv = server ? ToriRSServer_EmbedWorld(embed) : NULL;
    if( srv && id >= 0 )
        for( int i = 0; i < TORIRSSERVER_NPC_MAX; ++i )
            if( srv->npcs[i].active && srv->npcs[i].type == id )
            { ++server_count; server_x = srv->npcs[i].x; server_z = srv->npcs[i].z; }
    snprintf(result, sizeof(result), "{\"ok\":%s,\"items\":[", id >= 0 ? "true" : "false");
    if( app->world && id >= 0 )
    {
        struct World_EntityPool* pool = &app->world->entities.npc;
        for( int i = World_EntityPoolHead(pool); i >= 0; i = World_EntityPoolNext(pool, i) )
        {
            struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
            if( !npc || (npc->npc_id != id && npc->base_npc_id != id) ) continue;
            int world_slot = server ? ToriRSServer_SlotMapWorld(server, npc->server_slot) : -1;
            struct ToriRSServerNpc* source = srv && world_slot >= 0 &&
                world_slot < TORIRSSERVER_NPC_MAX ? &srv->npcs[world_slot] : NULL;
            struct ToriDraw_SceneElement* e = npc->element_id >= 0 ?
                ToriDraw_SceneElementGet(app->scene, npc->element_id) : NULL;
            char rig[256];
            geometry_json(app, npc->element_id, rig, sizeof(rig));
            size_t used = strlen(result);
            if( sizeof(result) - used < 1024 ) { truncated = 1; break; }
            x = npc->draw_position.x; z = npc->draw_position.z;
            snprintf(result + used, sizeof(result) - used,
                "%s{\"slot\":%d,\"generation\":%d,\"x\":%d,\"z\":%d,\"yaw\":%d,"
                "\"render_x\":%d,\"render_y\":%d,\"render_z\":%d,\"render_yaw\":%d,\"size\":%d,"
                "\"server_x\":%d,\"server_z\":%d,\"mode\":%d,\"waypoint\":%d,\"rig\":%s}",
                count++ ? "," : "", npc->server_slot, source ? source->generation : -1,
                x + app->world->_base_tile_x * 128, z + app->world->_base_tile_z * 128,
                npc->orientation.yaw, e ? e->world_position.x : -1,
                e ? e->world_position.y : -1, e ? e->world_position.z : -1,
                e ? e->world_position.yaw : -1, npc->size,
                source ? source->x : -1, source ? source->z : -1,
                source ? source->mode : -1, source ? source->waypoint_index : -1, rig);
        }
    }
    size_t used = strlen(result);
    snprintf(result + used, sizeof(result) - used,
        "],\"count\":%d,\"x\":%d,\"z\":%d,\"server_count\":%d,\"server_x\":%d,\"server_z\":%d}",
        count, x, z, server_count, server_x, server_z);
    if( truncated ) snprintf(result, sizeof(result), "{\"ok\":false,\"error\":\"too many NPCs for one observation\"}");
    snprintf(out, capacity, "%s", result);
}

static void widget_json(struct App* app, const char* name, int sub, char* out, size_t capacity)
{
    char result[16384];

        int idx = widget(app, name, sub);
        int x=0,y=0,w=0,h=0,hidden=1;
        if( idx >= 0 )
        {
            UITree_LayoutGetBounds(&app->tree->components[idx].position, &x, &y, &w, &h);
            hidden = app->tree->components[idx].behavior.hide;
        }
        snprintf(result, sizeof(result), "{\"ok\":true,\"exists\":%s,\"index\":%d,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"hidden\":%d,\"trans\":%d}",
                 idx >= 0 ? "true" : "false", idx,x,y,w,h,hidden, idx >= 0 ? app->tree->components[idx].trans : 0);
    snprintf(out, capacity, "%s", result);
}

static void error(const char* message)
{
    snprintf(g_content_test.result, sizeof(g_content_test.result), "{\"ok\":false,\"error\":\"%s\"}", message);
}

static void capture(struct App* app, const char* filename)
{
    char dir[1024], name[256];
    const char* slash = strrchr(filename, '/');
    if( !slash || filename[0] != '/' || strlen(filename) >= sizeof(dir) )
    {
        error("shot requires an absolute PNG path");
        return;
    }
    size_t len = (size_t)(slash - filename);
    memcpy(dir, filename, len);
    dir[len] = 0;
    snprintf(name, sizeof(name), "%s", slash + 1);
    if( !name[0] || strchr(name, '\"') || strchr(filename, '\\') )
    {
        error("invalid screenshot filename");
        return;
    }
    if( !App_RequestScreenshot(app, dir, name, g_content_test.capture_path, sizeof(g_content_test.capture_path)) )
        error("renderer screenshot request refused");
    else
    {
        remove(g_content_test.capture_path);
        RedrawRequest_Raise(&app->redraw_request);
    }
}

/* Counts come from the last completed native painter pass. Actor lookups
 * include root-owned passengers borrowed into a deck's dynamic scene. */
static void wev_json(struct App* app, char* out, size_t capacity)
{
    int marker[WORLDVIEW_MAX]={0}, draws[WORLDVIEW_MAX]={0}, order[WORLDVIEW_MAX]={0}, next_order=0;
    int terrain[WORLDVIEW_MAX]={0}, actors[WORLDVIEW_MAX]={0}, picks[WORLDVIEW_MAX]={0};
    int stack[WORLDVIEW_MAX+1]={0}, depth=0;
    if( app->painter_buffer )
        for( int n=0; n<app->painter_buffer->command_count; ++n )
        {
            const struct PaintersElementCommand* cmd=&app->painter_buffer->commands[n];
            int id=(int)cmd->_entity._bf_entity;
            if( cmd->_bf_kind==PNTR_CMD_BEGIN_WORLD )
            {
                assert(id>0 && id<WORLDVIEW_MAX);
                assert(depth<WORLDVIEW_MAX);
                stack[++depth]=id; ++marker[id]; order[id]=++next_order;
            }
            else if( cmd->_bf_kind==PNTR_CMD_END_WORLD )
            { assert(depth>0 && stack[depth]==id); --depth; }
            else
            {
                int view=stack[depth];
                if( cmd->_bf_kind==PNTR_CMD_TERRAIN || cmd->_bf_kind==PNTR_CMD_TERRAIN_PICK_ONLY )
                    ++terrain[view];
                else if( cmd->_bf_kind==PNTR_CMD_ELEMENT )
                {
                    id=painter_command_element_id(cmd);
                    ++draws[view];
                    struct World* own=WorldviewRegistry_IsLive(&app->worldviews,view)
                        ? WorldviewRegistry_Get(&app->worldviews,view)->world : NULL;
                    if( (app->world && (World_NpcGetByElementId(app->world,id,NULL) ||
                                         World_PlayerGetByElementId(app->world,id))) ||
                        (own && own!=app->world && (World_NpcGetByElementId(own,id,NULL) ||
                                                   World_PlayerGetByElementId(own,id))) )
                        ++actors[view];
                }
            }
        }
    assert(depth==0);
    for( int n = 0; n < app->world_views.frame->world_pickset.count; ++n )
    {
        int id = app->world_views.frame->world_pickset.items[n].view_id;
        if( id>=0 && id<WORLDVIEW_MAX ) ++picks[id];
    }
    int used=snprintf(out,capacity,"{\"ok\":true,\"limit\":%d,\"aboard\":%d,\"views\":[",
                      app->host.world_entity_draw_limit,Wevs_AboardView(&app->wevs));
    int count=0;
    for( int id=1; id<WORLDVIEW_MAX; ++id )
        if( Wevs_IsLive(&app->wevs,id) )
        {
            const struct Wev* w=Wevs_Get(&app->wevs,id);
            int bounds[4]; Wev_RenderBounds(w,bounds);
            used+=snprintf(out+used,capacity-(size_t)used,
                "%s{\"id\":%d,\"group\":%d,\"visible\":%s,\"flat\":%s,\"fine\":[%d,%d],\"yaw\":%d,\"bounds\":[%d,%d,%d,%d],\"markers\":%d,\"draw_order\":%d,\"model_commands\":%d,\"terrain_commands\":%d,\"actor_commands\":%d,\"picked\":%d}",
                count++ ? "," : "",id,w->priority_group,w->render_visible ? "true":"false",
                w->flattened ? "true":"false",w->x,w->z,w->angle,
                bounds[0],bounds[1],bounds[2],bounds[3],marker[id],order[id],draws[id],terrain[id],actors[id],picks[id]);
            assert(used>=0 && (size_t)used<capacity);
        }
    snprintf(out+used,capacity-(size_t)used,"]}");
}

/*
 * The quest clock's step: 20 ms a frame, the frame-locked clock every proof
 * runs on -- which puts each server tick exactly 30 client cycles after the
 * last. A WATCHED client (a Scripts-tab Play) runs on the wall clock, where
 * frames are uneven and a tick is 29 or 31 cycles as often as 30; a driver
 * reader that counted the client's own cycle drifted from scriptrun there and
 * on no proof lane (the Olm crystal shadows, coxolmso17, 2026-10-13).
 * TORIRS_CONTENT_TEST_JITTER=1 gives that unevenness deterministically: the
 * same 20 ms on average, in steps of 0 to 40, so ticks fall 29 to 31 frames
 * apart, and a jittered run's tick log must equal the locked run's -- any
 * difference is a reader on the client's clock.
 */
static uint64_t quest_clock_step_ms(void)
{
    /* (seven steps, 140 ms: a period that divides 30 frames would put every
     * tick 30 frames apart again and perturb nothing) */
    static const uint64_t steps[] = { 20, 40, 0, 30, 10, 20, 20 };
    static int jitter = -1;
    static unsigned at;

    if( jitter < 0 )
    {
        const char* knob = getenv("TORIRS_CONTENT_TEST_JITTER");
        jitter = knob && knob[0] && atoi(knob) != 0;
    }
    if( !jitter )
        return 20;
    at = (at + 1) % (unsigned)(sizeof(steps) / sizeof(steps[0]));
    return steps[at];
}

/*
 * Is the virtual clock held this frame?
 *
 * By default while the client has async work pending (App_AsyncPending): the
 * report of the frame-start pump, which does not see what the frame's packets
 * queue after it, nor what the main queue waits on. At disk speed that is
 * nearly nothing; with slow reads (TORIRS_IO_DEFER_FRAMES) the server runs
 * ahead of the client's loads through it.
 *
 * TORIRS_CONTENT_TEST_HOLD=1, the ORACLE: also while the client has any work
 * it will finish without the server (App_ClientBusy). The clock then steps
 * only with every queue done or waiting on game time or on the server, so a
 * slow read can change nothing a test observes: any tick-log row that differs
 * from the undeferred run under the same hold is something that depends on
 * when an asset lands.
 *
 * TORIRS_CONTENT_TEST_RACE=1, the RACE: held only for what the MAIN queue
 * holds, the one thing the frame contract (app.h) lets hold the game: the
 * boot, a packet task waiting on work (a REBUILD's gather included;
 * App_MainQueueWaitsOnWork -- a READY one runs in the next logic cycle, so
 * holding the clock for it would hold it forever), and the client scripts'
 * latches. Not the whole settle gate: an open server tick is closed by the
 * server's fence, which a held clock never sends (a logout left one open and
 * held the clock for good). A parallel task waiting on
 * its read does not hold it, so the server's next packets arrive while it
 * waits: the target despawns, the scene rebuilds, the interface closes, the
 * key is written again -- what a real client on a slow cache meets.
 */
static int content_test_knob(const char* name)
{
    const char* knob = getenv(name);
    return knob && knob[0] && atoi(knob) != 0;
}

/* 0 default, 1 TORIRS_CONTENT_TEST_HOLD, 2 TORIRS_CONTENT_TEST_RACE; read once. */
static int content_test_mode(void)
{
    static int mode = -1;
    if( mode < 0 )
    {
        int const race = content_test_knob("TORIRS_CONTENT_TEST_RACE");
        int const hold = content_test_knob("TORIRS_CONTENT_TEST_HOLD");
        /* One mode at a time: the race holds less than the default, the
         * oracle more. */
        if( race )
            assert(!hold);
        mode = race ? 2 : hold ? 1 : 0;
    }
    return mode;
}

static int g_content_test_driver_held;

int ContentTest_DriverHeld(void)
{
    return g_content_test_driver_held;
}

static int content_test_clock_held(struct App* app)
{
    int const mode = content_test_mode();
    if( mode == 2 )
        return RS_ClientPhase_Booting(&app->phase) || App_MainQueueWaitsOnWork(app) ||
               TaskRunner_HadWork(&app->frame_runner) || app->frame_runner.frame_settle_pending;
    if( mode == 1 )
        return App_AsyncPending(app) || App_ClientBusy(app);
    return App_AsyncPending(app);
}

uint64_t ContentTest_Begin(struct App* app, struct NetTransport* transport,
                          struct ToriRS_CmdBus* bus, uint64_t real_now)
{
    if( !ContentTest_Enabled() ) return real_now;
    assert(app);
    assert(bus);
    /* Decided below, every frame; a frame that decides nothing holds nothing. */
    App_LogicClockHold(0);
    g_content_test_driver_held = 0;
    struct ToriRSServerEmbed* embed = NetTransport_TestClock(transport, g_content_test.test_now);
    /* t.cheat needs the embedded struct ToriRSServer* to call a debugproc
     * in-process, with no packet; this is the one place a NetTransport is
     * already turned into one. Handed over every frame, quest mode or not,
     * since a driver-loaded-but-idle run (no TORIRS_QUEST_SCRIPT) is still a
     * legitimate way to drive ::commands from the ordinary mailbox below. */
    PluginDriveCore_SetEmbed(embed);
    /* verbs-pointer's DrivePointer_MouseMove/MouseButton push onto this bus
     * (torirs_plugin_drive_pointer.c's own banner reports the seam); handed
     * over every frame, same as g_embed above, for the same reason. */
    PluginDriveCore_SetCmdBus(bus);
    if( PluginDrive_QuestScriptPath() )
    {
        /*
         * Quest-script mode (docs/QUEST_DRIVER_PLAN.md 5.1 / ARCHITECT.md A1):
         * no mailbox. TORIRS_QUEST_SCRIPT drives itself through api.drive's
         * scheduler (torirs_plugin_drive.c), polled from the driver plugin's
         * own on_frame_start -- there is no `request` file to read and no
         * `command`/`active` state machine to run below.
         *
         * The one thing this file still owns is the virtual clock.
         * PluginDrive_ClockWantsStep answers whether the coroutine still has
         * a quest to run; capture_path is THIS file's own state (a
         * screenshot request in flight, `t.shot`'s job, verbs-ui) and is the
         * hold a step must not race -- checked here rather than threaded
         * through the driver because nothing outside this file has any
         * business knowing that variable's name.
         */
        int const wants = !g_content_test.capture_path[0] && PluginDrive_ClockWantsStep(app);
        int const held = wants && content_test_clock_held(app);
        /* Held: the client's frame-locked cycles stand still with the server
         * (App_LogicClockHold). In the oracle and race modes they stand still
         * on EVERY frame the clock does not step -- a screenshot's, a frame
         * before the driver wants time -- so the client runs exactly one
         * cycle per step and its clock cannot drift from the server's however
         * the frames fall; by default only a hold stops them, as before. */
        App_LogicClockHold(content_test_mode() > 0 ? !(wants && !held) : held);
        g_content_test_driver_held = held && content_test_mode() > 0;
        if( wants && !held )
            g_content_test.test_now += quest_clock_step_ms();
        NetTransport_TestClock(transport, g_content_test.test_now);
        return g_content_test.test_now;
    }
    uint64_t elapsed = g_content_test.last_real ? real_now - g_content_test.last_real : 0;
    g_content_test.last_real = real_now;
    if( !g_content_test.active )
    {
        char request[4096];
        path(request, sizeof(request), "request");
        FILE* f = fopen(request, "r");
        if( f )
        {
            if( !fgets(g_content_test.command, sizeof(g_content_test.command), f) ) g_content_test.command[0] = 0;
            fclose(f);
            remove(request);
            g_content_test.command[strcspn(g_content_test.command, "\r\n")] = 0;
            g_content_test.active = 1;
            g_content_test.finishing = g_content_test.remaining = g_content_test.publish_pending = 0;
            g_content_test.observe_requested = g_content_test.observe_drawn = 0;
            g_content_test.observe_npcs[0] = 0;
            g_content_test.hover_requested=0;
            g_content_test.capture_path[0] = 0;
            g_content_test.started_us = PlatformWindow_TicksUs();
            strcpy(g_content_test.result, "{\"ok\":true}");
            int frames, op, x, z, sub;
            char name[512];
            if( sscanf(g_content_test.command, "step %d", &frames) == 1 && frames >= 0 && frames <= 30000 )
            {
                g_content_test.running = 0;
                g_content_test.remaining = frames;
            }
            else if( sscanf(g_content_test.command, "observe %d %d %d %511s %511s %511s", &frames, &g_content_test.observe_x, &g_content_test.observe_z, name, g_content_test.observe_widget, g_content_test.observe_npcs) >= 5 )
            {
                g_content_test.observe_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
                if( frames < 0 || frames > 30000 || g_content_test.observe_bit < 0 ) error("invalid observation frames or varbit");
                else { g_content_test.remaining = frames; g_content_test.running = 0; g_content_test.observe_requested = 1; }
            }
            else if( sscanf(g_content_test.command,"wevlimit %d",&x)==1 )
            {
                app->host.world_entity_draw_limit=x<0 ? 0:x;
                g_content_test.hover_requested=1; RedrawRequest_Raise(&app->redraw_request);
            }
            else if( sscanf(g_content_test.command,"wevgroup %d %d",&x,&z)==2 )
            {
                if( x<=0 || x>=WORLDVIEW_MAX || !Wevs_IsLive(&app->wevs,x) || z<0 || z>2 )
                    error("wevgroup requires a live view and group0..2");
                else
                {
                    Wevs_Get(&app->wevs,x)->priority_group=z;
                    g_content_test.hover_requested=1; RedrawRequest_Raise(&app->redraw_request);
                }
            }
            else if( !strcmp(g_content_test.command,"peer") || !strncmp(g_content_test.command,"peer ",5) )
            {
                char message[256]; int changed=0;
                if( !ContentTestSailing_PeerCommand(embed,g_content_test.command+4,&changed,message,sizeof(message)) ) error(message);
                else if( changed ) { g_content_test.publish_pending=1; RedrawRequest_Raise(&app->redraw_request); }
            }
            else if( !strncmp(g_content_test.command, "proc ", 5) )
            {
                char message[256];
                if( !ContentTestSailing_PrimaryProc(embed, g_content_test.command + 5, message, sizeof(message)) ) error(message);
                else { g_content_test.publish_pending = 1; RedrawRequest_Raise(&app->redraw_request); }
            }
            else if( !strcmp(g_content_test.command, "pause") ) g_content_test.running = 0;
            else if( !strcmp(g_content_test.command, "resume") ) g_content_test.running = 1;
            else if( sscanf(g_content_test.command,"hover %d %d",&x,&z)==2 )
            {
                CmdBus_PushMouseMove(bus,x,z);
                g_content_test.hover_requested=1;
                RedrawRequest_Raise(&app->redraw_request);
            }
            else if( strncmp(g_content_test.command, "cheat ", 6) == 0 )
            {
                if( !App_SendCommand(app, g_content_test.command + 6) ) error("client is not online");
                else g_content_test.publish_pending = 1;
            }
            else if( sscanf(g_content_test.command, "click %d %d %511s", &g_content_test.click_x, &g_content_test.click_y, name) >= 2 )
            {
                char button[16] = "left";
                sscanf(g_content_test.command, "click %d %d %15s", &g_content_test.click_x, &g_content_test.click_y, button);
                g_content_test.click_button = !strcmp(button, "right") ? 3 : !strcmp(button, "middle") ? 2 : 1;
                g_content_test.click_phase = 0;
            }
            else if( sscanf(g_content_test.command, "pointer %d %d", &x, &z) == 2 )
                CmdBus_PushMouseMove(bus, x, z);
            else if( sscanf(g_content_test.command, "camera %d %d %d", &x, &z, &sub) == 3 )
            {
                if( z < 128 || z > 383 || sub < -1000 || sub > 10000 )
                    error("camera pitch must be 128..383; zoom -1000..10000");
                else
                {
                    app->world_views.frame->orbit.yaw = app->world_views.frame->world_camera.yaw = x & 2047;
                    app->world_views.frame->orbit.pitch = app->world_views.frame->world_camera.pitch = z;
                    app->world_views.frame->world_cam_zoom = sub;
                    app->world_views.frame->orbit.yaw_velocity = app->world_views.frame->orbit.pitch_velocity = 0;
                    RedrawRequest_Raise(&app->redraw_request);
                }
            }
            else if( sscanf(g_content_test.command, "loc %d %d %d %511s", &op, &x, &z, name) == 4 )
            {
                int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, name);
                if( id < 0 ) error("unknown loc");
                else { App_SimulateLocOp(app, op, x, z, id); g_content_test.publish_pending = 1; }
            }
            else if( sscanf(g_content_test.command, "button %511s %d %d", name, &sub, &op) == 3 )
            {
                int idx = widget(app, name, sub);
                if( idx < 0 || app->host.trigger_op_count >= RS_CS2_HOST_TRIGGER_OP_MAX )
                    error("button missing or queue full");
                else
                {
                    int slot = (app->host.trigger_op_head + app->host.trigger_op_count++) % RS_CS2_HOST_TRIGGER_OP_MAX;
                    /* Field by field: `ref` sits between the two ints, so a
                     * positional {component_id, op} puts the op index inside
                     * the ref and leaves op_index 0. @see rs_cs2_trigger_op_push. */
                    app->host.trigger_op[slot].component_id =
                        app->tree->components[idx].component_id;
                    app->host.trigger_op[slot].ref = UITree_RefAt(app->tree, idx);
                    app->host.trigger_op[slot].op_index = op;
                    app->frame_runner.frame_settle_pending = 1;
                    RedrawRequest_Raise(&app->redraw_request);
                    g_content_test.publish_pending = 1;
                }
            }
            else if( sscanf(g_content_test.command, "resume %511s", name) == 1 )
            {
                int idx = widget(app, name, -1);
                if( idx < 0 ) error("resume widget missing");
                else { app->host.resume_pausebutton_component_id = app->tree->components[idx].component_id; app->frame_runner.frame_settle_pending = 1; g_content_test.publish_pending = 1; }
            }
            else if( !strcmp(g_content_test.command, "close") )
            { app->host.close_modal_requested = true; app->frame_runner.frame_settle_pending = 1; g_content_test.publish_pending = 1; }
            else if( !strncmp(g_content_test.command, "text ", 5) )
            {
                const unsigned char* text=(const unsigned char*)g_content_test.command+5;
                size_t length=strlen((const char*)text);
                int valid=length>0 && length<=63;
                for( size_t i=0; i<length; ++i )
                    if( text[i]<32 || text[i]>126 ) valid=0;
                if( !valid ) error("text requires 1..63 printable ASCII characters");
                else
                {
                    for( size_t i=0; i<length; ++i )
                        CmdBus_PushKeyEvent(bus,-1,text[i],0);
                    g_content_test.publish_pending=1;
                }
            }
            else if( !strncmp(g_content_test.command, "key ", 4) )
            {
                int key=-1;
                if( !strcmp(g_content_test.command+4,"enter") ) key=TORIRS_OSRSKEY_ENTER;
                else if( !strcmp(g_content_test.command+4,"escape") ) key=TORIRS_OSRSKEY_ESCAPE;
                else if( !strcmp(g_content_test.command+4,"backspace") ) key=TORIRS_OSRSKEY_BACKSPACE;
                else if( !strcmp(g_content_test.command+4,"tab") ) key=TORIRS_OSRSKEY_TAB;
                if( key<0 ) error("key requires enter, escape, backspace or tab");
                else
                {
                    CmdBus_PushOsrsKey(bus,key,1,1);
                    CmdBus_PushKeyEvent(bus,key,0,0);
                    CmdBus_PushOsrsKey(bus,key,0,0);
                    g_content_test.publish_pending=1;
                }
            }
            else if( !strcmp(g_content_test.command, "logout") )
            {
                if( !app->net || app->net->state != TORIRS_NET_GAME ) error("not logged in");
                else
                {
                    /* Use the production transport close: embedded and socket
                     * servers both save through their ordinary disconnect. */
                    ToriRS_Network_Logout(app->net);
                    g_content_test.publish_pending = 1;
                }
            }
            else if( (!strcmp(g_content_test.command, "reload") || !strncmp(g_content_test.command, "reload ", 7)) && embed )
            {
                struct ToriRSServer* srv = ToriRSServer_EmbedWorld(embed);
                const char* scripts = g_content_test.command[6] == ' ' ? g_content_test.command + 7 : getenv("TORIRSSERVER_SCRIPTS");
                if( !scripts || !srv ) error("set TORIRSSERVER_SCRIPTS and log in first");
                else
                {
                    int ok = reload_scripts(srv, scripts);
                    snprintf(g_content_test.result, sizeof(g_content_test.result), "{\"ok\":%s,\"checkpoints_cleared\":true}", ok ? "true" : "false");
                }
            }
            /* Queue readback only after input/network/UI settlement in End. */
            else if( !strncmp(g_content_test.command, "shot ", 5) ) { }
            else if( strcmp(g_content_test.command, "wev") && strcmp(g_content_test.command, "state") && strncmp(g_content_test.command, "varbit ", 7) &&
                     strcmp(g_content_test.command, "threats") && strncmp(g_content_test.command, "scenery ", 8) && strncmp(g_content_test.command, "npc ", 4) && strncmp(g_content_test.command, "widget ", 7) && strncmp(g_content_test.command, "save ", 5) &&
                     strncmp(g_content_test.command, "restore ", 8) && strncmp(g_content_test.command, "collision", 9) &&
                     strncmp(g_content_test.command, "varp ", 5) && strncmp(g_content_test.command, "inventory ", 10) &&
                     strncmp(g_content_test.command, "activity ", 9) )
                error("unknown command or invalid arguments");
        }
    }
    /* Input edges cross the same bus and picking pipeline as physical clicks.
     * Let one settled render publish hover hits before pressing. */
    if( g_content_test.click_phase >= 0 && settled(app) )
    {
        if( g_content_test.click_phase == 0 ) CmdBus_PushMouseMove(bus, g_content_test.click_x, g_content_test.click_y);
        else if( g_content_test.click_phase == 2 )
            CmdBus_PushMouseButton(bus, TORIRS_CMD_INPUT_MOUSE_DOWN, g_content_test.click_button, g_content_test.click_x, g_content_test.click_y);
        else if( g_content_test.click_phase == 3 )
        {
            CmdBus_PushMouseButton(bus, TORIRS_CMD_INPUT_MOUSE_UP, g_content_test.click_button, g_content_test.click_x, g_content_test.click_y);
            g_content_test.click_phase = -2;
            g_content_test.publish_pending = 1;
        }
        g_content_test.click_phase++;
        RedrawRequest_Raise(&app->redraw_request);
    }
    App_LogicClockHold(g_content_test.remaining > 0 && content_test_clock_held(app));
    if( g_content_test.remaining > 0 && !content_test_clock_held(app) )
    {
        g_content_test.test_now += 20;
        g_content_test.remaining--;
    }
    else if( g_content_test.running && settled(app) ) g_content_test.test_now += elapsed > 100 ? 100 : elapsed;
    NetTransport_TestClock(transport, g_content_test.test_now);
    return g_content_test.test_now;
}

void ContentTest_End(struct App* app, struct NetTransport* transport)
{
    if( !ContentTest_Enabled() ) return;
    if( !g_content_test.active )
    {
        /* Keep idle overhead small without adding a frame's latency to input.
         *
         * A quest-script run under render skip is never idle: its virtual
         * clock steps every frame and nothing waits on the mailbox, so the nap
         * is pure wall time -- sampled (macOS `sample`, druidspirit, skip on)
         * at a quarter of the main thread, more than the frames it no longer
         * draws. --render-every-frame keeps it, so that A/B is today's run. */
        if( !g_content_test.running && settled(app) &&
            !(PluginDrive_QuestScriptPath() && App_RenderSkipEnabled(app)) )
            PlatformWindow_SleepUntil(PlatformWindow_Ticks64() + 1);
        return;
    }
    /* Immediate IF_/varp replies may open a UI transaction between scheduled
     * ticks. A paused clock cannot reach the next tick's fence by itself.
     * Seal only those already-sent bytes; do not publish player/NPC masks or
     * advance scripts, since that would change the animation start timing. */
    if( !g_content_test.remaining && content_test_tick_only() && RS_ServerTick_IsOpen(&app->server_tick) )
    {
        struct ToriRSServerEmbed* pending_embed = NetTransport_TestClock(transport, g_content_test.test_now);
        struct ToriRSServerPlayer* pending_player = pending_embed ? ToriRSServer_EmbedPlayer(pending_embed, 0) : NULL;
        if( pending_player ) { ToriRSServer_SendTickEnd(pending_player); return; }
    }
    if( g_content_test.remaining || g_content_test.click_phase >= 0 || App_AsyncPending(app) || !App_WorldSettled(app) )
    { g_content_test.observe_drawn = 0; return; }
    /* Two complete polls consume client output and apply its server response. */
    int observation = g_content_test.observe_requested || !strcmp(g_content_test.command, "state") || !strcmp(g_content_test.command, "threats") ||
        !strncmp(g_content_test.command, "varbit ", 7) || !strncmp(g_content_test.command, "scenery ", 8) ||
        !strncmp(g_content_test.command, "npc ", 4) || !strncmp(g_content_test.command, "widget ", 7);
    if( !observation && g_content_test.finishing++ < 2 ) return;
    struct ToriRSServerEmbed* embed = NetTransport_TestClock(transport, g_content_test.test_now);
    struct ToriRSServerPlayer* server = embed ? ToriRSServer_EmbedPlayer(embed, 0) : NULL;
    if( content_test_tick_only() ) g_content_test.publish_pending = 0;
    if( g_content_test.publish_pending && server )
    {
        ToriRSServer_WorldPublish(ToriRSServer_EmbedWorld(embed));
        g_content_test.publish_pending = 0;
        g_content_test.finishing = 0;
        return;
    }
    if( RS_CS2_TransmitsPending(&app->host) )
    {
        RS_CS2_PumpTransmits(&app->host, &app->frame_runner);
        app->frame_runner.frame_settle_pending = 1;
        RedrawRequest_Raise(&app->redraw_request);
        g_content_test.finishing = 0;
        return;
    }
    if( !App_FrameSettled(app) ) { g_content_test.observe_drawn = 0; return; }
    if( g_content_test.observe_requested && !g_content_test.observe_drawn ) return;
    if( !strncmp(g_content_test.command, "shot ", 5) && !g_content_test.capture_path[0] )
    {
        capture(app, g_content_test.command + 5);
        if( g_content_test.capture_path[0] ) { g_content_test.finishing = 0; return; }
    }
    if( g_content_test.restore_pending )
    {
        /* A root rebuild can publish before WORLDENTITY_INFO, and the next
         * publish is deferred until MAP_BUILD_COMPLETE. Drain that real
         * roundtrip before snapping the received interpolation target. */
        if( !ContentTestSailing_RestoreDelivered(app) )
        {
            if( server )
            {
                ToriRSServer_WorldPublish(ToriRSServer_EmbedWorld(embed));
                static unsigned retries;
                if( getenv("TORIRS_SAILING_RESTORE_DEBUG") && (retries++ % 4096) == 0 )
                    fprintf(stderr, "sailing restore server: tracked%d sent%d,%d stamp%d login%d rebuild%d pending%d pipe%d\n",
                            server->wev_tracked_count, server->wev_last_fine_x[0],
                            server->wev_last_fine_z[0], server->wev_teleport_stamps[0],
                            server->login_scene_pending, server->rebuild_scene_pending,
                            server->rebuild_pending, ToriRSServer_EmbedPending(embed, 0));
            }
            g_content_test.finishing = 0;
            RedrawRequest_Raise(&app->redraw_request);
            return;
        }
        ContentTestSailing_SettleRestore(app);
        g_content_test.restore_pending = 0;
        g_content_test.finishing = 0;
        return;
    }
    char name[512];
    int sub;
    if( g_content_test.observe_requested )
    {
        char state[8192], scenery[4096], widget_state[1024];
        state_json(app, embed, state, sizeof(state));
        scenery_json(app, g_content_test.observe_x, g_content_test.observe_z, scenery, sizeof(scenery));
        widget_json(app, g_content_test.observe_widget, -1, widget_state, sizeof(widget_state));
        snprintf(g_content_test.result, sizeof(g_content_test.result), "{\"ok\":true,\"state\":%s,\"station\":{\"client\":%d,\"server\":%d},\"scenery\":%s,\"fade\":%s}",
            state, VarPManager_GetVarbit(&app->varps, g_content_test.observe_bit),
            server ? ToriRSServer_VarbitGet(server, g_content_test.observe_bit) : -1, scenery, widget_state);
        /* Optional comma-separated NPC types share the same rendered frame
         * and mailbox response. No additional simulation or rendering occurs. */
        char actors[8192] = "{";
        int count = 0, failed = 0;
        for( char* next = g_content_test.observe_npcs; *next; )
        {
            char* end = strchr(next, ',');
            if( end ) *end = 0;
            char actor[16384];
            npc_json(app, embed, next, actor, sizeof(actor));
            size_t used = strlen(actors);
            if( strstr(actor, "\"ok\":false") ||
                used + strlen(next) + strlen(actor) + 8 >= sizeof(actors) )
            { failed = 1; break; }
            snprintf(actors + used, sizeof(actors) - used, "%s\"%s\":%s", count++ ? "," : "", next, actor);
            if( !end ) break;
            next = end + 1;
        }
        strcat(actors, "}");
        size_t used = strlen(g_content_test.result);
        if( failed || used + strlen(actors) + 12 >= sizeof(g_content_test.result) )
            error("invalid or oversized NPC observation");
        else
            snprintf(g_content_test.result + used - 1, sizeof(g_content_test.result) - used + 1, ",\"npcs\":%s}", actors);
        g_content_test.observe_requested = 0;
    }
    else if( !strcmp(g_content_test.command,"wev") || !strncmp(g_content_test.command,"wevlimit ",9) || !strncmp(g_content_test.command,"wevgroup ",9) )
    {
        if( !strstr(g_content_test.result,"\"ok\":false") ) wev_json(app,g_content_test.result,sizeof(g_content_test.result));
    }
    else if( !strcmp(g_content_test.command,"peer") || !strncmp(g_content_test.command,"peer ",5) )
    {
        if( !strstr(g_content_test.result,"\"ok\":false") ) ContentTestSailing_PeerState(app,embed,g_content_test.result,sizeof(g_content_test.result));
    }
    else if( strcmp(g_content_test.command, "state") == 0 )
    {
        state_json(app, embed, g_content_test.result, sizeof(g_content_test.result));
    }
    else if( sscanf(g_content_test.command, "varbit %511s", name) == 1 )
    {
        int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
        snprintf(g_content_test.result, sizeof(g_content_test.result), "{\"ok\":%s,\"client\":%d,\"server\":%d}",
            id >= 0 && server ? "true" : "false", id >= 0 ? VarPManager_GetVarbit(&app->varps, id) : -1,
            id >= 0 && server ? ToriRSServer_VarbitGet(server, id) : -1);
    }
    else if( sscanf(g_content_test.command, "varp %d", &sub) == 1 )
    {
        if( sub < 0 || sub >= TORIRSSERVER_VARP_COUNT ) error("varp index outside server table");
        else snprintf(g_content_test.result,sizeof(g_content_test.result),"{\"ok\":true,\"client\":%d,\"server\":%d}",
            VarPManager_GetVarp(&app->varps,sub),server ? ToriRSServer_VarpsGet(&server->varps, sub) : -1);
    }
    else if( sscanf(g_content_test.command,"activity %d",&sub)==1 && server )
    {
        struct ToriRSServer* srv=ToriRSServer_EmbedWorld(embed);
        struct ToriRSServerVessel* boat=ToriRSServer_VesselAtTile(srv,server->x,server->z);
        int handle;
        if( sscanf(g_content_test.command,"activity %*d %d",&handle)==1 )
        {
            boat=ToriRSServer_VesselGet(srv,handle);
            if( boat && boat->owner_uid!=server->pid+1 ) boat=NULL;
        }
        if( !boat || sub<0 || sub>=13 ) error("activity requires a boat and hotspot0..12");
        else
        {
            int reg[7],wind[8];
            for( int i=0; i<7; ++i ) reg[i]=ToriRSServer_MapInstanceVarGet(boat->instance,8+sub*7+i);
            for( int i=0; i<8; ++i ) wind[i]=ToriRSServer_MapInstanceVarGet(boat->instance,i);
            uint32_t uid=(uint32_t)reg[4]-1;
            int target=(int)(uid&65535), hp=-1, frozen=0, slow_token=0, burn_token=0;
            if( reg[4] && target<TORIRSSERVER_NPC_MAX && srv->npcs[target].active &&
                srv->npcs[target].generation==(uid>>16) )
            {
                hp=srv->npcs[target].hitpoints;
                frozen=srv->npcs[target].frozen_ticks;
                slow_token=srv->npcs[target].script_vars[62];
                burn_token=srv->npcs[target].script_vars[63];
            }
            snprintf(g_content_test.result,sizeof(g_content_test.result),
                "{\"ok\":true,\"vessel\":%d,\"slot\":%d,\"registers\":[%d,%d,%d,%d,%d,%d,%d],\"wind\":[%d,%d,%d,%d,%d,%d,%d,%d],\"sailing_xp_tenths\":%d,\"fishing_xp_tenths\":%d,\"ranged_xp_tenths\":%d,\"construction_xp_tenths\":%d,\"target_hp\":%d,\"target_frozen\":%d,\"target_slow_token\":%d,\"target_burn_token\":%d}",
                boat->index,sub,reg[0],reg[1],reg[2],reg[3],reg[4],reg[5],reg[6],wind[0],wind[1],wind[2],wind[3],wind[4],wind[5],wind[6],wind[7],
                server->stat_xp_tenths[23],server->stat_xp_tenths[10],server->stat_xp_tenths[4],server->stat_xp_tenths[22],hp,frozen,slow_token,burn_token);
        }
    }
    else if( sscanf(g_content_test.command,"inventory %d",&sub)==1 )
    {
        if( sub<0 || sub>=32768 ) error("inventory requires a native base ID below32768");
        else
        {
            const struct InvContainer* local=InvManager_FindContainer(&app->invs,sub);
            const struct InvContainer* other=InvManager_FindContainer(&app->invs,sub+32768);
            struct ToriRSServerContainer* cargo=NULL;
            if( server )
                for( int i=0; i<TORIRSSERVER_CONTAINER_MAX; ++i )
                    if( server->containers[i].used && server->containers[i].inv_id==sub ) cargo=&server->containers[i];
            int offset=snprintf(g_content_test.result,sizeof(g_content_test.result),"{\"ok\":true,\"id\":%d,\"client_slots\":%d,\"other_slots\":%d,\"server_slots\":%d",
                sub,local ? local->slot_count : 0,other ? other->slot_count : 0,cargo ? cargo->slots : 0);
            const char* labels[]={"client","other","server"};
            for( int which=0; which<3; ++which )
            {
                offset+=snprintf(g_content_test.result+offset,sizeof(g_content_test.result)-offset,",\"%s\":[",labels[which]);
                const struct InvContainer* container=which==0 ? local : other;
                int slots=which==2 ? (cargo ? cargo->slots : 0) : (container ? container->slot_count : 0);
                int entries=0;
                for( int slot=0; slot<slots && entries<32; ++slot )
                {
                    int obj=which==2 ? cargo->items[slot].obj_id : container->slots[slot].obj_id;
                    int count=which==2 ? cargo->items[slot].count : container->slots[slot].obj_count;
                    if( obj<0 || count<=0 ) continue;
                    offset+=snprintf(g_content_test.result+offset,sizeof(g_content_test.result)-offset,"%s[%d,%d,%d]",entries++ ? ",":"",slot,obj,count);
                }
                offset+=snprintf(g_content_test.result+offset,sizeof(g_content_test.result)-offset,"]");
            }
            snprintf(g_content_test.result+offset,sizeof(g_content_test.result)-offset,"}");
        }
    }
    else if( !strcmp(g_content_test.command, "threats") && server )
    {
        strcpy(g_content_test.result, "{\"ok\":true,\"npcs\":[");
        struct ToriRSServer* srv = ToriRSServer_EmbedWorld(embed);
        int count = 0;
        for( int i = 0; i < TORIRSSERVER_NPC_MAX && count < 64; i++ )
        {
            struct ToriRSServerNpc* npc = &srv->npcs[i];
            if( !npc->active || npc->combat_target != server->pid ) continue;
            size_t used = strlen(g_content_test.result);
            snprintf(g_content_test.result + used, sizeof(g_content_test.result) - used, "%s{\"type\":%d,\"x\":%d,\"z\":%d}",
                count++ ? "," : "", npc->type, npc->x, npc->z);
        }
        size_t used = strlen(g_content_test.result);
        snprintf(g_content_test.result + used, sizeof(g_content_test.result) - used, "]}");
    }
    else if( !strncmp(g_content_test.command, "scenery ", 8) )
    {
        int x, z;
        if( sscanf(g_content_test.command, "scenery %d %d", &x, &z) != 2 || !app->world ) error("scenery requires world X Z");
        else
        {
            scenery_json(app, x, z, g_content_test.result, sizeof(g_content_test.result));
        }
    }
    else if( sscanf(g_content_test.command, "npc %511s", name) == 1 )
        npc_json(app, embed, name, g_content_test.result, sizeof(g_content_test.result));
    else if( sscanf(g_content_test.command, "widget %511s %d", name, &sub) == 2 )
    {
        widget_json(app, name, sub, g_content_test.result, sizeof(g_content_test.result));
    }
    else if( !strncmp(g_content_test.command, "collision", 9) && server )
    {
        int radius = 20;
        sscanf(g_content_test.command, "collision %d", &radius);
        if( radius < 1 || radius > 24 ) error("collision radius must be 1..24");
        else
        {
            struct ToriRSServer* srv = ToriRSServer_EmbedWorld(embed);
            struct ToriRSServerVessel* boat = ToriRSServer_VesselAtTile(srv, server->x, server->z);
            int cx = boat ? boat->fine_x / 128 : server->x;
            int cz = boat ? boat->fine_z / 128 : server->z;
            int level = boat ? boat->level : server->level;
            char water[2402], walking[2402];
            int n = 0;
            for( int z = cz + radius; z >= cz - radius; --z )
                for( int x = cx - radius; x <= cx + radius; ++x )
                {
                    water[n] = ToriRSServer_VesselTileSailable(level,x,z) ? '1' : '0';
                    walking[n++] = ToriRSServer_SceneWalkBlocked(level,x,z) ? '0' : '1';
                }
            water[n] = walking[n] = 0;
            snprintf(g_content_test.result,sizeof(g_content_test.result),
                     "{\"ok\":true,\"x\":%d,\"z\":%d,\"level\":%d,\"width\":%d,\"order\":\"north_to_south\",\"boat_open\":\"%s\",\"player_open\":\"%s\"}",
                     cx,cz,level,radius*2+1,water,walking);
        }
    }
    else if( !strncmp(g_content_test.command, "save ", 5) || !strncmp(g_content_test.command, "restore ", 8) )
    {
        int restoring = !strncmp(g_content_test.command, "restore ", 8);
        char message[512];
        int ok = restoring
            ? ContentTestSailing_Restore(app, embed, g_content_test.command + 8, message, sizeof(message))
            : ContentTestSailing_Save(app, embed, g_content_test.command + 5, message, sizeof(message));
        if( !ok ) error(message);
        else
        {
            strcpy(g_content_test.result, "{\"ok\":true,\"scope\":\"movement,stats,varps,inventories,facilities,timers,camera,deck animation; unchanged vessel topology\"}");
            if( restoring )
            {
                /* Execute the restore once. Subsequent frames only drain its
                 * normal network update and restore the visual checkpoint. */
                g_content_test.command[0] = 0;
                g_content_test.publish_pending = g_content_test.restore_pending = 1;
                g_content_test.finishing = 0;
                return;
            }
        }
    }
    if( g_content_test.capture_path[0] )
    {
        struct stat info;
        if( PluginScreenshots_IsWaiting(&app->plugin_screenshots, g_content_test.capture_path) )
        {
            RedrawRequest_Raise(&app->redraw_request);
            return;
        }
        if( stat(g_content_test.capture_path, &info) || info.st_size == 0 ) error("renderer capture did not produce a file");
        else snprintf(g_content_test.result, sizeof(g_content_test.result), "{\"ok\":true,\"path\":\"%s\",\"width\":%d,\"height\":%d,\"renderer\":%d}",
                      g_content_test.capture_path, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, RenderMode_World(&app->render_mode));
    }
    size_t len = strlen(g_content_test.result);
    if( len && g_content_test.result[len - 1] == '}' )
        snprintf(g_content_test.result + len - 1, sizeof(g_content_test.result) - len + 1, ",\"runtime_ms\":%.3f}",
                 (PlatformWindow_TicksUs() - g_content_test.started_us) / 1000.0);
    char temp[4096], response[4096];
    path(temp, sizeof(temp), "response.tmp"); path(response, sizeof(response), "response");
    FILE* f = fopen(temp, "w");
    if( !f ) { perror(temp); abort(); }
    fprintf(f, "%s\n", g_content_test.result);
    if( fclose(f) || rename(temp, response) ) { perror(response); abort(); }
    g_content_test.active = 0;
}
#else
int ContentTest_DrawRequested(struct App* app) { (void)app; return 0; }
uint64_t ContentTest_Begin(struct App* app, struct NetTransport* t,
                          struct ToriRS_CmdBus* bus, uint64_t now)
{
    (void)app; (void)t; (void)bus;
    if( ContentTest_Enabled() ) { fprintf(stderr, "content test requires EMBED_SERVER=1\n"); exit(2); }
    return now;
}
void ContentTest_End(struct App* app, struct NetTransport* t) { (void)app; (void)t; }
int ContentTest_DriverHeld(void) { return 0; }
#endif
