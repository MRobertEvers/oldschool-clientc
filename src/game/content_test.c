#include "content_test.h"
#include "app.h"
#include "cmd/cmdbus.h"
#include "platform/net_transport.h"
#include "platform/platform_window.h"
#include "ui/uitree_layout.h"
#include "content_test_sailing.h"
#include "game/rs_cs2_dispatch.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int ContentTest_Enabled(void)
{
    const char* dir = getenv("TORIRS_CONTENT_TEST");
    return dir && *dir;
}

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER
#include "torirsserver/torirs_server_embed.h"
#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_content.h"
#include "torirsserver/torirs_server_scene.h"
#include "world/world.h"
#include <toridraw_scene.h>
#include "varp/varp_manager.h"
#include "varc/varc_manager.h"

/* One warm process, one virtual clock. IO/picking/UI continue at frozen time.
 * A step never waits out the 600 ms server tick in wall-clock time. */
static uint64_t test_now = 1000, last_real, started_us;
static int remaining, active, finishing, running, publish_pending, restore_pending;
static int observe_requested, observe_drawn, observe_x, observe_z, observe_bit;
static char observe_widget[512];
static int click_phase = -1, click_x, click_y, click_button;
static char command[2048], result[16384], capture_path[1024];

int ContentTest_DrawRequested(struct App* app)
{
    if( !ContentTest_Enabled() || !App_FrameSettled(app) || App_IsBooting(app, NULL) || app->world_load_inflight ) return 0;
    if( active && observe_requested && !remaining && !observe_drawn )
    { observe_drawn = 1; return 1; }
    return (active && (capture_path[0] || click_phase >= 0)) || running;
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
    snprintf(out, capacity, "{\"vertices\":%d,\"hash\":%u,\"min_y\":%d,\"max_y\":%d,\"posed_frame\":%d}",
        count, hash, min_y, max_y, e ? e->posed_frame : -1);
}

static void path(char* out, size_t cap, const char* file)
{
    snprintf(out, cap, "%s/%s", getenv("TORIRS_CONTENT_TEST"), file);
}

static int settled(struct App* app)
{
    return !App_AsyncPending(app) && App_FrameSettled(app) && !app->world_load_inflight;
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

        snprintf(result, sizeof(result),
            "{\"ok\":true,\"online\":%s,\"paused\":%s,\"time_ms\":%llu,\"server_x\":%d,\"server_z\":%d,\"level\":%d,\"aboard_view\":%d,\"logic_cycle\":%llu,\"server_tick\":%d,\"camera_yaw\":%d,\"camera_pitch\":%d,\"camera_zoom\":%d,\"world_ready\":%d,\"renderer\":%d}",
            server ? "true" : "false", running ? "false" : "true", (unsigned long long)test_now,
            server ? server->x : -1, server ? server->z : -1, server ? server->level : -1,
            app->aboard_view, (unsigned long long)app->logic_cycle,
            server ? ToriRSServer_EmbedWorld(embed)->tick : -1,
            app->orbit_yaw, app->orbit_pitch, app->world_cam_zoom,
            app->world ? app->world->load_complete : 0, app->world_render_mode);
        struct WorldEntity_Player* player = app->world ? World_PlayerGetByServerPid(app->world, app->esync.local_pid) : NULL;
        size_t client_end = strlen(result);
        snprintf(result + client_end - 1, sizeof(result) - client_end + 1,
            ",\"x\":%d,\"z\":%d,\"camera\":%d,\"animation\":%d,\"anim_frame\":%d,\"chat_blocked\":%d}",
            player ? app->world->_base_tile_x + player->grid_position.x : -1,
            player ? app->world->_base_tile_z + player->grid_position.z : -1,
            app->cam_script.scripted, player ? player->animation.primary.anim_id : -1,
            player ? player->animation.primary.frame : -1, VarCManager_GetInt(&app->varcs, 11));
        char rig[256];
        geometry_json(app, player ? player->element_id : -1, rig, sizeof(rig));
        size_t rig_end = strlen(result);
        snprintf(result + rig_end - 1, sizeof(result) - rig_end + 1, ",\"rig\":%s}", rig);
        char sailing[4096];
        ContentTestSailing_State(app, embed, sailing, sizeof(sailing));
        size_t n = strlen(result);
        snprintf(result + n - 1, sizeof(result) - n + 1, ",\"sailing\":%s}", sailing);

    snprintf(out, capacity, "%s", result);
}

static void scenery_json(struct App* app, int x, int z, char* out, size_t capacity)
{
    char result[16384];
    if( !app->world ) { snprintf(out, capacity, "{\"ok\":true,\"items\":[]}"); return; }
            strcpy(result, "{\"ok\":true,\"items\":[");
            int count = 0;
            struct World_EntityPool* pool = &app->world->entities.scenery;
            for( int i = World_EntityPoolHead(pool); i >= 0; i = World_EntityPoolNext(pool, i) )
            {
                struct WorldEntity_Scenery* sc = World_EntityPoolGet(pool, i);
                if( !sc || sc->grid_position.x + app->world->_base_tile_x != x ||
                    sc->grid_position.z + app->world->_base_tile_z != z ) continue;
                struct ToriDraw_SceneElement* element = ToriDraw_SceneElementGet(app->scene, sc->element_id);
                char rig[256];
                geometry_json(app, sc->element_id, rig, sizeof(rig));
                size_t used = strlen(result);
                snprintf(result + used, sizeof(result) - used,
                    "%s{\"loc\":%d,\"seq\":%d,\"frame\":%d,\"cycle\":%d,\"angle\":%d,\"element\":%d,\"rig\":%s}",
                    count++ ? "," : "", sc->loc_id, element ? element->anim_seq_id : -1, element ? element->anim_frame : -1,
                    element ? element->anim_cycle : -1, sc->angle, sc->element_id, rig);
            }
            size_t used = strlen(result);
            snprintf(result + used, sizeof(result) - used, "]}");
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
    snprintf(result, sizeof(result), "{\"ok\":false,\"error\":\"%s\"}", message);
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
    if( !App_RequestScreenshot(app, dir, name, capture_path, sizeof(capture_path)) )
        error("renderer screenshot request refused");
    else
    {
        remove(capture_path);
        app->need_redraw = 1;
    }
}

uint64_t ContentTest_Begin(struct App* app, struct NetTransport* transport,
                          struct ToriRS_CmdBus* bus, uint64_t real_now)
{
    if( !ContentTest_Enabled() ) return real_now;
    assert(app);
    assert(bus);
    struct ToriRSServerEmbed* embed = NetTransport_TestClock(transport, test_now);
    uint64_t elapsed = last_real ? real_now - last_real : 0;
    last_real = real_now;
    if( !active )
    {
        char request[4096];
        path(request, sizeof(request), "request");
        FILE* f = fopen(request, "r");
        if( f )
        {
            if( !fgets(command, sizeof(command), f) ) command[0] = 0;
            fclose(f);
            remove(request);
            command[strcspn(command, "\r\n")] = 0;
            active = 1;
            finishing = remaining = publish_pending = 0;
            observe_requested = observe_drawn = 0;
            capture_path[0] = 0;
            started_us = PlatformWindow_TicksUs();
            strcpy(result, "{\"ok\":true}");
            int frames, op, x, z, sub;
            char name[512];
            if( sscanf(command, "step %d", &frames) == 1 && frames >= 0 && frames <= 30000 )
            {
                running = 0;
                remaining = frames;
            }
            else if( sscanf(command, "observe %d %d %d %511s %511s", &frames, &observe_x, &observe_z, name, observe_widget) == 5 )
            {
                observe_bit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
                if( frames < 0 || frames > 30000 || observe_bit < 0 ) error("invalid observation frames or varbit");
                else { remaining = frames; running = 0; observe_requested = 1; }
            }
            else if( !strcmp(command, "pause") ) running = 0;
            else if( !strcmp(command, "resume") ) running = 1;
            else if( strncmp(command, "cheat ", 6) == 0 )
            {
                if( !App_SendCommand(app, command + 6) ) error("client is not online");
                else publish_pending = 1;
            }
            else if( sscanf(command, "click %d %d %511s", &click_x, &click_y, name) >= 2 )
            {
                char button[16] = "left";
                sscanf(command, "click %d %d %15s", &click_x, &click_y, button);
                click_button = !strcmp(button, "right") ? 3 : !strcmp(button, "middle") ? 2 : 1;
                click_phase = 0;
            }
            else if( sscanf(command, "pointer %d %d", &x, &z) == 2 )
                CmdBus_PushMouseMove(bus, x, z);
            else if( sscanf(command, "camera %d %d %d", &x, &z, &sub) == 3 )
            {
                if( z < 128 || z > 383 || sub < -1000 || sub > 10000 )
                    error("camera pitch must be 128..383; zoom -1000..10000");
                else
                {
                    app->orbit_yaw = app->world_camera.yaw = x & 2047;
                    app->orbit_pitch = app->world_camera.pitch = z;
                    app->world_cam_zoom = sub;
                    app->orbit_yaw_vel = app->orbit_pitch_vel = 0;
                    app->need_redraw = 1;
                }
            }
            else if( sscanf(command, "loc %d %d %d %511s", &op, &x, &z, name) == 4 )
            {
                int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, name);
                if( id < 0 ) error("unknown loc");
                else { App_SimulateLocOp(app, op, x, z, id); publish_pending = 1; }
            }
            else if( sscanf(command, "button %511s %d %d", name, &sub, &op) == 3 )
            {
                int idx = widget(app, name, sub);
                if( idx < 0 || app->host.trigger_op_count >= RS_CS2_HOST_TRIGGER_OP_MAX )
                    error("button missing or queue full");
                else
                {
                    int slot = (app->host.trigger_op_head + app->host.trigger_op_count++) % RS_CS2_HOST_TRIGGER_OP_MAX;
                    app->host.trigger_op[slot] = (struct RS_CS2TriggerOp){app->tree->components[idx].component_id, op};
                    app->runner.frame_settle_pending = 1;
                    app->runner.frame_settle_pending = 1;
                    app->need_redraw = 1;
                    publish_pending = 1;
                }
            }
            else if( sscanf(command, "resume %511s", name) == 1 )
            {
                int idx = widget(app, name, -1);
                if( idx < 0 ) error("resume widget missing");
                else { app->host.resume_pausebutton_component_id = app->tree->components[idx].component_id; app->runner.frame_settle_pending = 1; publish_pending = 1; }
            }
            else if( !strcmp(command, "close") )
            { app->host.close_modal_requested = true; app->runner.frame_settle_pending = 1; publish_pending = 1; }
            else if( (!strcmp(command, "reload") || !strncmp(command, "reload ", 7)) && embed )
            {
                struct ToriRSServer* srv = ToriRSServer_EmbedWorld(embed);
                const char* scripts = command[6] == ' ' ? command + 7 : getenv("TORIRSSERVER_SCRIPTS");
                if( !scripts || !srv ) error("set TORIRSSERVER_SCRIPTS and log in first");
                else
                {
                    ToriRSServer_ScriptsFree(srv);
                    int ok = ToriRSServer_ScriptsLoad(srv, scripts);
                    snprintf(result, sizeof(result), "{\"ok\":%s}", ok ? "true" : "false");
                }
            }
            /* Queue readback only after input/network/UI settlement in End. */
            else if( !strncmp(command, "shot ", 5) ) { }
            else if( strcmp(command, "state") && strncmp(command, "varbit ", 7) &&
                     strcmp(command, "threats") && strncmp(command, "scenery ", 8) && strncmp(command, "npc ", 4) && strncmp(command, "widget ", 7) && strncmp(command, "save ", 5) &&
                     strncmp(command, "restore ", 8) && strncmp(command, "collision", 9) &&
                     strncmp(command, "varp ", 5) && strncmp(command, "inventory ", 10) )
                error("unknown command or invalid arguments");
        }
    }
    /* Input edges cross the same bus and picking pipeline as physical clicks.
     * Let one settled render publish hover hits before pressing. */
    if( click_phase >= 0 && settled(app) )
    {
        if( click_phase == 0 ) CmdBus_PushMouseMove(bus, click_x, click_y);
        else if( click_phase == 2 )
            CmdBus_PushMouseButton(bus, TORIRS_CMD_INPUT_MOUSE_DOWN, click_button, click_x, click_y);
        else if( click_phase == 3 )
        {
            CmdBus_PushMouseButton(bus, TORIRS_CMD_INPUT_MOUSE_UP, click_button, click_x, click_y);
            click_phase = -2;
            publish_pending = 1;
        }
        click_phase++;
        app->need_redraw = 1;
    }
    if( remaining > 0 && !App_AsyncPending(app) )
    {
        test_now += 20;
        remaining--;
    }
    else if( running && settled(app) ) test_now += elapsed > 100 ? 100 : elapsed;
    NetTransport_TestClock(transport, test_now);
    return test_now;
}

void ContentTest_End(struct App* app, struct NetTransport* transport)
{
    if( !ContentTest_Enabled() ) return;
    if( !active )
    {
        /* Keep idle overhead small without adding a frame's latency to input. */
        if( !running && settled(app) ) PlatformWindow_SleepUntil(PlatformWindow_Ticks64() + 1);
        return;
    }
    /* Immediate IF_/varp replies may open a UI transaction between scheduled
     * ticks. A paused clock cannot reach the next tick's fence by itself.
     * Seal only those already-sent bytes; do not publish player/NPC masks or
     * advance scripts, since that would change the animation start timing. */
    if( !remaining && getenv("TORIRS_CONTENT_TEST_TICK_ONLY") && app->server_tick_open )
    {
        struct ToriRSServerEmbed* pending_embed = NetTransport_TestClock(transport, test_now);
        struct ToriRSServerPlayer* pending_player = pending_embed ? ToriRSServer_EmbedPlayer(pending_embed, 0) : NULL;
        if( pending_player ) { ToriRSServer_SendTickEnd(pending_player); return; }
    }
    if( remaining || click_phase >= 0 || App_AsyncPending(app) || app->world_load_inflight )
    { observe_drawn = 0; return; }
    /* Two complete polls consume client output and apply its server response. */
    int observation = observe_requested || !strcmp(command, "state") || !strcmp(command, "threats") ||
        !strncmp(command, "varbit ", 7) || !strncmp(command, "scenery ", 8) ||
        !strncmp(command, "npc ", 4) || !strncmp(command, "widget ", 7);
    if( !observation && finishing++ < 2 ) return;
    struct ToriRSServerEmbed* embed = NetTransport_TestClock(transport, test_now);
    struct ToriRSServerPlayer* server = embed ? ToriRSServer_EmbedPlayer(embed, 0) : NULL;
    if( getenv("TORIRS_CONTENT_TEST_TICK_ONLY") ) publish_pending = 0;
    if( publish_pending && server )
    {
        ToriRSServer_WorldPublish(ToriRSServer_EmbedWorld(embed));
        publish_pending = 0;
        finishing = 0;
        return;
    }
    if( RS_CS2_TransmitsPending(&app->host) )
    {
        RS_CS2_PumpTransmits(&app->host, &app->runner);
        app->runner.frame_settle_pending = 1;
        app->need_redraw = 1;
        finishing = 0;
        return;
    }
    if( !App_FrameSettled(app) ) { observe_drawn = 0; return; }
    if( observe_requested && !observe_drawn ) return;
    if( !strncmp(command, "shot ", 5) && !capture_path[0] )
    {
        capture(app, command + 5);
        if( capture_path[0] ) { finishing = 0; return; }
    }
    if( restore_pending )
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
            finishing = 0;
            app->need_redraw = 1;
            return;
        }
        ContentTestSailing_SettleRestore(app);
        restore_pending = 0;
        finishing = 0;
        return;
    }
    char name[512];
    int sub;
    if( observe_requested )
    {
        char state[8192], scenery[4096], widget_state[1024];
        state_json(app, embed, state, sizeof(state));
        scenery_json(app, observe_x, observe_z, scenery, sizeof(scenery));
        widget_json(app, observe_widget, -1, widget_state, sizeof(widget_state));
        snprintf(result, sizeof(result), "{\"ok\":true,\"state\":%s,\"station\":{\"client\":%d,\"server\":%d},\"scenery\":%s,\"fade\":%s}",
            state, VarPManager_GetVarbit(&app->varps, observe_bit),
            server ? ToriRSServer_VarbitGet(server, observe_bit) : -1, scenery, widget_state);
        observe_requested = 0;
    }
    else if( strcmp(command, "state") == 0 )
    {
        state_json(app, embed, result, sizeof(result));
    }
    else if( sscanf(command, "varbit %511s", name) == 1 )
    {
        int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, name);
        snprintf(result, sizeof(result), "{\"ok\":%s,\"client\":%d,\"server\":%d}",
            id >= 0 && server ? "true" : "false", id >= 0 ? VarPManager_GetVarbit(&app->varps, id) : -1,
            id >= 0 && server ? ToriRSServer_VarbitGet(server, id) : -1);
    }
    else if( sscanf(command, "varp %d", &sub) == 1 )
    {
        if( sub < 0 || sub >= TORIRSSERVER_VARP_COUNT ) error("varp index outside server table");
        else snprintf(result,sizeof(result),"{\"ok\":true,\"client\":%d,\"server\":%d}",
            VarPManager_GetVarp(&app->varps,sub),server ? server->varps[sub] : -1);
    }
    else if( sscanf(command,"inventory %d",&sub)==1 )
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
            int offset=snprintf(result,sizeof(result),"{\"ok\":true,\"id\":%d,\"client_slots\":%d,\"other_slots\":%d,\"server_slots\":%d",
                sub,local ? local->slot_count : 0,other ? other->slot_count : 0,cargo ? cargo->slots : 0);
            const char* labels[]={"client","other","server"};
            for( int which=0; which<3; ++which )
            {
                offset+=snprintf(result+offset,sizeof(result)-offset,",\"%s\":[",labels[which]);
                const struct InvContainer* container=which==0 ? local : other;
                int slots=which==2 ? (cargo ? cargo->slots : 0) : (container ? container->slot_count : 0);
                int entries=0;
                for( int slot=0; slot<slots && entries<32; ++slot )
                {
                    int obj=which==2 ? cargo->items[slot].obj_id : container->slots[slot].obj_id;
                    int count=which==2 ? cargo->items[slot].count : container->slots[slot].obj_count;
                    if( obj<=0 || count<=0 ) continue;
                    offset+=snprintf(result+offset,sizeof(result)-offset,"%s[%d,%d,%d]",entries++ ? ",":"",slot,obj,count);
                }
                offset+=snprintf(result+offset,sizeof(result)-offset,"]");
            }
            snprintf(result+offset,sizeof(result)-offset,"}");
        }
    }
    else if( !strcmp(command, "threats") && server )
    {
        strcpy(result, "{\"ok\":true,\"npcs\":[");
        struct ToriRSServer* srv = ToriRSServer_EmbedWorld(embed);
        int count = 0;
        for( int i = 0; i < TORIRSSERVER_NPC_MAX && count < 64; i++ )
        {
            struct ToriRSServerNpc* npc = &srv->npcs[i];
            if( !npc->active || npc->combat_target != server->pid ) continue;
            size_t used = strlen(result);
            snprintf(result + used, sizeof(result) - used, "%s{\"type\":%d,\"x\":%d,\"z\":%d}",
                count++ ? "," : "", npc->type, npc->x, npc->z);
        }
        size_t used = strlen(result);
        snprintf(result + used, sizeof(result) - used, "]}");
    }
    else if( !strncmp(command, "scenery ", 8) )
    {
        int x, z;
        if( sscanf(command, "scenery %d %d", &x, &z) != 2 || !app->world ) error("scenery requires world X Z");
        else
        {
            scenery_json(app, x, z, result, sizeof(result));
        }
    }
    else if( sscanf(command, "npc %511s", name) == 1 )
    {
        int id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, name);
        int count = 0, x = -1, z = -1;
        if( app->world && id >= 0 )
        {
            struct World_EntityPool* pool = &app->world->entities.npc;
            for( int i = World_EntityPoolHead(pool); i >= 0; i = World_EntityPoolNext(pool, i) )
            {
                struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
                if( npc && npc->npc_id == id )
                { count++; x = npc->draw_position.x; z = npc->draw_position.z; }
            }
        }
        snprintf(result, sizeof(result), "{\"ok\":%s,\"count\":%d,\"x\":%d,\"z\":%d}", id >= 0 ? "true" : "false", count, x, z);
    }
    else if( sscanf(command, "widget %511s %d", name, &sub) == 2 )
    {
        widget_json(app, name, sub, result, sizeof(result));
    }
    else if( !strncmp(command, "collision", 9) && server )
    {
        int radius = 20;
        sscanf(command, "collision %d", &radius);
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
            snprintf(result,sizeof(result),
                     "{\"ok\":true,\"x\":%d,\"z\":%d,\"level\":%d,\"width\":%d,\"order\":\"north_to_south\",\"boat_open\":\"%s\",\"player_open\":\"%s\"}",
                     cx,cz,level,radius*2+1,water,walking);
        }
    }
    else if( !strncmp(command, "save ", 5) || !strncmp(command, "restore ", 8) )
    {
        int restoring = !strncmp(command, "restore ", 8);
        char message[512];
        int ok = restoring
            ? ContentTestSailing_Restore(app, embed, command + 8, message, sizeof(message))
            : ContentTestSailing_Save(app, embed, command + 5, message, sizeof(message));
        if( !ok ) error(message);
        else
        {
            strcpy(result, "{\"ok\":true,\"scope\":\"movement,varps,backpack,worn,cargo,facilities,camera; unchanged vessel topology\"}");
            if( restoring )
            {
                /* Execute the restore once. Subsequent frames only drain its
                 * normal network update and restore the visual checkpoint. */
                command[0] = 0;
                publish_pending = restore_pending = 1;
                finishing = 0;
                return;
            }
        }
    }
    if( capture_path[0] )
    {
        struct stat info;
        for( int i = 0; i < APP_PLUGIN_SCREENSHOTS_MAX; i++ )
            if( app->plugin_screenshots[i].in_use && !strcmp(app->plugin_screenshots[i].path, capture_path) )
            {
                app->need_redraw = 1;
                return;
            }
        if( stat(capture_path, &info) || info.st_size == 0 ) error("renderer capture did not produce a file");
        else snprintf(result, sizeof(result), "{\"ok\":true,\"path\":\"%s\",\"width\":%d,\"height\":%d,\"renderer\":%d}",
                      capture_path, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, app->world_render_mode);
    }
    size_t len = strlen(result);
    if( len && result[len - 1] == '}' )
        snprintf(result + len - 1, sizeof(result) - len + 1, ",\"runtime_ms\":%.3f}",
                 (PlatformWindow_TicksUs() - started_us) / 1000.0);
    char temp[4096], response[4096];
    path(temp, sizeof(temp), "response.tmp"); path(response, sizeof(response), "response");
    FILE* f = fopen(temp, "w");
    if( !f ) { perror(temp); abort(); }
    fprintf(f, "%s\n", result);
    if( fclose(f) || rename(temp, response) ) { perror(response); abort(); }
    active = 0;
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
#endif
