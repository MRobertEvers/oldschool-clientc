#include "content_test_sailing.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(TORIRS_EMBED_SERVER) && TORIRS_EMBED_SERVER
#include "app.h"
#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_content.h"
#include "torirsserver/torirs_server_container.h"
#include "torirsserver/torirs_server_friends.h"
#include "torirsserver/torirs_server_embed.h"
#include "torirsserver/torirs_server_ids.h"
#include "torirsserver/torirs_server_mapinstance.h"
#include "serverscript/ssvm.h"
#include "serverscript/ssvm_provider.h"
#include "world/world.h"
#include "world/entity_scenery.h"
#include "world/entity_player.h"
#include "net/rev/pktnames.h"
#include "render/torirs_world_projection.h"
#include "toridraw_animation.h"

#define SAILING_CHECKPOINT_MAX 8
#define SAILING_CHECKPOINT_NAME_MAX 64
#define SAILING_CHECKPOINT_INVS 6
#define SAILING_CHECKPOINT_REGS 111
#define SAILING_CHECKPOINT_ANIMS 512

/* Numeric client motion only: no config, world, model or view pointers. */
struct SailingWevMotion
{
    int x, y, z, angle, queue_count, interp_armed;
    int interp_from_x, interp_from_z, interp_from_angle;
    int interp_to_x, interp_to_z, interp_to_angle;
    double interp_start_cycle, interp_end_cycle;
    struct WevTarget queue[WEV_TARGET_QUEUE_SLOTS];
};

#define SAILING_MOTION_FIELDS(X) \
    X(x) X(y) X(z) X(angle) X(queue_count) X(interp_armed) \
    X(interp_from_x) X(interp_from_z) X(interp_from_angle) \
    X(interp_to_x) X(interp_to_z) X(interp_to_angle) \
    X(interp_start_cycle) X(interp_end_cycle)

static void save_motion(struct SailingWevMotion* saved, const struct Wev* wev)
{
#define SAVE_MOTION(field) saved->field = wev->field;
    SAILING_MOTION_FIELDS(SAVE_MOTION)
#undef SAVE_MOTION
    memcpy(saved->queue, wev->queue, sizeof(saved->queue));
}

static void restore_motion(struct Wev* wev, const struct SailingWevMotion* saved,
                           double cycle_delta)
{
#define RESTORE_MOTION(field) wev->field = saved->field;
    SAILING_MOTION_FIELDS(RESTORE_MOTION)
#undef RESTORE_MOTION
    memcpy(wev->queue, saved->queue, sizeof(wev->queue));
    for( int i = 0; i <= wev->queue_count; ++i )
        wev->queue[i].enqueue_cycle += cycle_delta;
    wev->interp_start_cycle += cycle_delta;
    wev->interp_end_cycle += cycle_delta;
}
#undef SAILING_MOTION_FIELDS

struct SailingCheckpoint
{
    char name[SAILING_CHECKPOINT_NAME_MAX];
    int pid;
    int server_tick;
    uint32_t login_generation;
    int x, z, level, navigating_vessel, navigating_vessel_serial;
    struct ToriRSServerVessel vessels[TORIRSSERVER_VESSEL_MAX];
    int instance_vars[TORIRSSERVER_VESSEL_MAX][SAILING_CHECKPOINT_REGS];
    int crew_clocks[TORIRSSERVER_VESSEL_MAX][5];
    int32_t varps[TORIRSSERVER_VARP_COUNT];
    int stat_level[TORIRSSERVER_STAT_COUNT];
    int stat_boosted[TORIRSSERVER_STAT_COUNT];
    int stat_xp_tenths[TORIRSSERVER_STAT_COUNT];
    int hitpoints, max_hitpoints;
    struct ToriRSServerItem inv[TORIRSSERVER_INV_SLOTS];
    struct ToriRSServerItem worn[TORIRSSERVER_WORN_SLOTS];
    struct ToriRSServerQueued queue[TORIRSSERVER_QUEUE_MAX];
    struct ToriRSServerQueued engine_queue[TORIRSSERVER_ENGINE_QUEUE_MAX];
    struct ToriRSServerTimer timers[TORIRSSERVER_TIMER_MAX];
    struct { int inv_id, slots; struct ToriRSServerItem* items; } cargo[SAILING_CHECKPOINT_INVS];
    struct ToriDraw_Camera camera;
    struct ToriDraw_Position camera_position;
    int base_x, base_z;
    int yaw, pitch, yaw_velocity, pitch_velocity, pitch_clamp, zoom, unlocked;
    float orbit_x, orbit_z;
    int side_tab;
    int aboard_view;
    int selected_heading;
    uint64_t selected_remaining;
    double wev_clock;
    struct SailingWevMotion motion[WORLDVIEW_MAX];
    int scenery_count;
    struct
    {
        int view, x, z, level, shape, loc_id;
        int seq_id, frame, cycle, loop;
    } scenery[SAILING_CHECKPOINT_ANIMS];
    struct
    {
        int live, seq_id, seq_delay, bob_y;
        double anim_age, seq_age;
    } bob[WORLDVIEW_MAX];
};

static struct SailingCheckpoint* checkpoints[SAILING_CHECKPOINT_MAX];
static int restore_slot = -1;
static uint32_t restore_receipts[WORLDVIEW_MAX];

static int fail(char* error, size_t cap, const char* message)
{
    snprintf(error, cap, "%s", message);
    return 0;
}

/* A second real server player with no external transport. It reaches the
 * primary renderer exclusively through ordinary PLAYER_INFO packets. */
static int peer_pid = -1;
static uint32_t peer_generation;
static int peer_query_varbit = -1;

/* The crew-shell role varbit, by NAME. It was written here as the literal
 * 19233, which is the id this cache happens to assign
 * `sailing_sidepanel_player_role`; a content rebuild that moves it would have
 * left this reading somebody else's varbit and reporting a plausible wrong
 * role. Resolved once and cached, because the symbol table is fixed for the
 * life of the process. */
static int peer_role_varbit(void)
{
    static int resolved = -2;

    if( resolved == -2 )
        resolved = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT,
                                              "sailing_sidepanel_player_role");
    return resolved;
}

static struct ToriRSServerPlayer* sailing_peer(struct ToriRSServerEmbed* embed)
{
    struct ToriRSServer* srv = embed ? ToriRSServer_EmbedWorld(embed) : NULL;
    if( !srv || peer_pid < 0 || peer_pid >= TORIRSSERVER_PLAYER_MAX ) return NULL;
    struct ToriRSServerPlayer* peer = &srv->players[peer_pid];
    return peer->active && peer->world == srv && peer->login_generation == peer_generation ? peer : NULL;
}

int ContentTestSailing_PeerCommand(struct ToriRSServerEmbed* embed, const char* command,
                                  int* changed, char* error, size_t cap)
{
    assert(command);
    assert(changed);
    assert(error);
    assert(cap);
    *changed = 0;
    error[0] = 0;
    struct ToriRSServer* srv = embed ? ToriRSServer_EmbedWorld(embed) : NULL;
    struct ToriRSServerPlayer* captain = embed ? ToriRSServer_EmbedPlayer(embed, 0) : NULL;
    if( !srv || !captain || !captain->active ) return fail(error, cap, "peer requires an online embedded world");
    while( *command == ' ' ) ++command;
    struct ToriRSServerPlayer* peer = sailing_peer(embed);
    if( !*command ) return 1;
    if( !strncmp(command, "create", 6) && (!command[6] || command[6] == ' ') )
    {
        if( peer ) return fail(error, cap, "remove the current peer before creating another");
        char name[32] = "HarnessMate";
        sscanf(command + 6, "%31s", name);
        for( int i = 0; name[i]; ++i )
            if( !((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') ||
                  (name[i] >= '0' && name[i] <= '9') || name[i] == '_') )
                return fail(error, cap, "peer name uses letters, digits and underscores");
        peer = ToriRSServer_WorldAddPlayer(srv, NULL);
        if( !peer ) return fail(error, cap, "no free player slot");
        ToriRSServer_WorldPlayerInit(peer);
        ToriRSServer_WorldSetDisplayName(peer, name);
        if( !ToriRSServer_FriendsLogin(peer->name37, 0, TORIRSSERVER_CHAT_PRIVATE_ON, 0, 0) )
        {
            ToriRSServer_WorldPlayerFree(srv, peer->pid);
            ToriRSServer_WorldPlayerReap(srv);
            return fail(error, cap, "no free social presence slot");
        }
        peer_pid = peer->pid;
        peer_generation = peer->login_generation;
        peer_query_varbit = -1;
        struct ToriRSServerPlayer* was = srv->active_player;
        ToriRSServer_WorldSetActive(srv, peer);
        struct ToriRSServerVessel* boat = ToriRSServer_VesselAtTile(srv, captain->x, captain->z);
        if( !boat || !ToriRSServer_VesselBoardPlayer(srv, peer, boat) )
            ToriRSServer_WorldTeleport(srv, captain->level, captain->x, captain->z);
        ToriRSServer_WorldSetActive(srv, was);
        *changed = 1;
        return 1;
    }
    if( !peer ) return fail(error, cap, "create a peer first");
    if( !strcmp(command, "remove") )
    {
        ToriRSServer_WorldRemovePlayer(srv, peer);
        ToriRSServer_WorldPlayerReap(srv);
        peer_pid = -1;
        peer_generation = 0;
        *changed = 1;
        return 1;
    }
    int level, x, z, run = 0, handle = 0, op, group, component, slot;
    char symbol[128];
    struct ToriRSServerPlayer* was = srv->active_player;
    int ok = 1;
    ToriRSServer_WorldSetActive(srv, peer);
    if( sscanf(command, "place %d %d %d", &level, &x, &z) == 3 &&
        level >= 0 && level < 4 && x >= 0 && x < 16384 && z >= 0 && z < 16384 )
        ToriRSServer_WorldTeleport(srv, level, x, z);
    else if( !strncmp(command, "board", 5) && (!command[5] || command[5] == ' ') )
    {
        sscanf(command + 5, "%d", &handle);
        struct ToriRSServerVessel* boat = handle ? ToriRSServer_VesselGet(srv, handle)
            : ToriRSServer_VesselAtTile(srv, captain->x, captain->z);
        if( !boat || !ToriRSServer_VesselBoardPlayer(srv, peer, boat) )
            ok = fail(error, cap, "peer boarding requires a live boat with walkable deck");
    }
    else if( sscanf(command, "walk %d %d %d", &x, &z, &run) >= 2 &&
             x >= 0 && x < 16384 && z >= 0 && z < 16384 && (run == 0 || run == 1) )
    {
        ToriRSServer_WorldSetVarp(srv, ToriRSServer_WorldVarp("option_run"), run);
        peer->run_energy = TORIRSSERVER_RUN_ENERGY_MAX;
        ToriRSServer_WorldWalkTo(srv, x, z);
    }
    else if( sscanf(command, "varbit %127s", symbol) == 1 )
    {
        peer_query_varbit = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT, symbol);
        if( peer_query_varbit < 0 ) ok = fail(error, cap, "unknown peer varbit");
        else if( sscanf(command, "varbit %*s %d", &x) == 1 ) ToriRSServer_VarbitSet(srv, peer_query_varbit, x);
        else { ToriRSServer_WorldSetActive(srv, was); return 1; }
    }
    else if( sscanf(command, "private %d", &x) == 1 && x >= 0 && x <= 2 )
    {
        uint8_t payload[3] = {0, x, 0};
        ToriRSServer_WorldHandle(peer, PKTOUT_NAME_CHAT_SETMODE, payload, 3);
    }
    else if( !strncmp(command, "cheat ", 6) && strlen(command + 6) < 127 )
    {
        uint8_t payload[128];
        size_t n = strlen(command + 6);
        memcpy(payload, command + 6, n);
        payload[n++] = '\n';
        ToriRSServer_WorldHandle(peer, PKTOUT_NAME_CLIENT_CHEAT, payload, (int)n);
    }
    else if( sscanf(command, "button %d %d %d %d", &group, &component, &slot, &op) == 4 &&
             group >= 0 && group < 65536 && component >= 0 && component < 65536 &&
             slot >= -1 && slot < 32768 && op >= 1 && op <= 10 )
    {
        uint32_t uid = ((uint32_t)group << 16) | component;
        uint8_t payload[9] = {uid >> 24, uid >> 16, uid >> 8, uid,
                             (unsigned)slot >> 8, slot, 255, 255, op};
        ToriRSServer_WorldHandle(peer, PKTOUT_NAME_IF_BUTTONX, payload, 9);
    }
    /* The same button by content symbol, so a test names the component the
     * script names ("sailing_boat_cargohold:items") instead of two numbers. */
    else if( sscanf(command, "button %127s %d %d", symbol, &slot, &op) == 3 &&
             slot >= -1 && slot < 32768 && op >= 1 && op <= 10 )
    {
        int com = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, symbol);
        if( com <= 0 ) ok = fail(error, cap, "unknown peer button component");
        else
        {
            uint8_t payload[9] = {com >> 24, com >> 16, com >> 8, com,
                                 (unsigned)slot >> 8, slot, 255, 255, op};
            ToriRSServer_WorldHandle(peer, PKTOUT_NAME_IF_BUTTONX, payload, 9);
        }
    }
    /* The CAPTAIN's half of a player-targeted interface op: the OPPLAYERT an
     * already-armed native click puts on the wire, aimed at the peer. It exists
     * to separate the two halves of a target op — arming, which is the mouse's
     * job, and the grant, which is the server's — and it is not a substitute
     * for the click: an acceptance run that uses it must say the arming was
     * exercised (or reported broken) separately. */
    else if( sscanf(command, "target %127s", symbol) == 1 )
    {
        int com = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_COMPONENT, symbol);
        if( com <= 0 ) ok = fail(error, cap, "unknown peer target component");
        else
        {
            /* OPPLAYERT carries the published GPI index, not the pool pid. */
            int wire = ToriRSServer_WirePlayerIndex(peer->pid);
            uint8_t payload[6] = {wire >> 8, wire,
                                 com >> 24, com >> 16, com >> 8, com};
            ToriRSServer_WorldHandle(captain, PKTOUT_NAME_OPPLAYERT, payload, 6);
        }
    }
    /* A p_namedialog wait belongs to a client that types a name. The peer has
     * no client, so the harness supplies the same RESUME_P_NAMEDIALOG the real
     * one would send; ~sailing_board_friend is unreachable without it. */
    else if( !strncmp(command, "namedialog ", 11) )
    {
        const unsigned char* text = (const unsigned char*)command + 11;
        size_t length = strlen((const char*)text);
        int printable = length > 0 && length <= 63;
        for( size_t i = 0; i < length; ++i )
            if( text[i] < 32 || text[i] > 126 ) printable = 0;
        if( !printable ) ok = fail(error, cap, "peer namedialog requires 1..63 printable ASCII characters");
        else
        {
            uint8_t payload[64];
            memcpy(payload, text, length);
            payload[length] = 0;
            ToriRSServer_WorldHandle(peer, PKTOUT_NAME_RESUME_P_NAMEDIALOG, payload, (int)length + 1);
        }
    }
    /* The peer's own click on a world loc: OPLOC<n> is p2 x, p2 z, p2 locId,
     * exactly what a second client would put on the wire, so the server walks
     * it there and runs the bound [oploc<n>] trigger. */
    else if( sscanf(command, "oploc %d %d %d %127s", &op, &x, &z, symbol) == 4 &&
             op >= 1 && op <= 5 && x >= 0 && x < 16384 && z >= 0 && z < 16384 )
    {
        int loc_id = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC, symbol);
        if( loc_id < 0 ) ok = fail(error, cap, "unknown peer loc");
        else
        {
            uint8_t payload[6] = {x >> 8, x, z >> 8, z, loc_id >> 8, loc_id};
            ToriRSServer_WorldHandle(peer, PKTOUT_NAME_OPLOC1 + (op - 1), payload, 6);
        }
    }
    else if( sscanf(command, "proc %127s", symbol) == 1 )
    {
        int args[4];
        int count = sscanf(command, "proc %*s %d %d %d %d", &args[0], &args[1], &args[2], &args[3]);
        if( count < 0 ) count = 0;
        char name[160];
        snprintf(name, sizeof(name), "[proc,%s]", symbol);
        if( !ToriRSServer_ScriptsRunProc(srv, name, args, count) ) ok = fail(error, cap, "peer content proc unavailable or invalid arguments");
    }
    else ok = fail(error, cap, "peer supports create/name, place/level/x/z, board/handle, walk/x/z/run, remove, varbit, private, cheat, proc, namedialog, oploc/op/x/z/loc, target/component and button/component-or-group+component/slot/op");
    ToriRSServer_WorldSetActive(srv, was);
    *changed = ok;
    return ok;
}

int ContentTestSailing_PrimaryProc(struct ToriRSServerEmbed* embed, const char* command,
                                  char* error, size_t cap)
{
    assert(command);
    assert(error);
    assert(cap);
    error[0] = 0;
    struct ToriRSServer* srv = embed ? ToriRSServer_EmbedWorld(embed) : NULL;
    struct ToriRSServerPlayer* captain = embed ? ToriRSServer_EmbedPlayer(embed, 0) : NULL;
    if( !srv || !captain || !captain->active ) return fail(error, cap, "proc requires an online embedded world");
    char symbol[128];
    if( sscanf(command, "%127s", symbol) != 1 ) return fail(error, cap, "proc requires a content proc name");
    int args[4];
    int count = sscanf(command, "%*s %d %d %d %d", &args[0], &args[1], &args[2], &args[3]);
    if( count < 0 ) count = 0;
    char name[160];
    snprintf(name, sizeof(name), "[proc,%s]", symbol);
    struct ToriRSServerPlayer* was = srv->active_player;
    ToriRSServer_WorldSetActive(srv, captain);
    int ok = ToriRSServer_ScriptsRunProc(srv, name, args, count);
    ToriRSServer_WorldSetActive(srv, was);
    if( !ok ) return fail(error, cap, "content proc unavailable or invalid arguments");
    return 1;
}

/* app_world_height_in's rule, for the harness only: an out-of-scene column has
 * no heightmap and a LINK_BELOW tile samples the level under it. */
static int sailing_ground_height(struct World* world, int fine_x, int fine_z, int level)
{
    assert(world);
    if( !world->heightmap ) return 0;
    int tile_x = fine_x >> 7, tile_z = fine_z >> 7;
    if( tile_x < 0 || tile_z < 0 || tile_x >= world->_scene_size || tile_z >= world->_scene_size )
        return 0;
    int real_level = level;
    if( level < WORLD_MAP_TERRAIN_LEVELS - 1 &&
        (World_TileFlagGet(world, tile_x, tile_z, 1) & RSCACHE_FLOFLAG_LINK_BELOW) != 0 )
        real_level = level + 1;
    return heightmap_get_interpolated(world->heightmap, fine_x, fine_z, real_level);
}

/*
 * Where the peer's model stands on the captain's screen, so an acceptance test
 * can aim a real mouse at another player the way a user does. This mirrors
 * app.c's app_world_project_actor, which is static; it is a harness reading,
 * never a render input, and the reply also carries the client's OWN pick-set
 * verdict, so a drift from app.c shows up as a failed proof rather than a
 * confidently wrong coordinate.
 */
static int peer_screen(struct App* app, const struct WorldEntity_Player* actor,
                       int* out_x, int* out_y)
{
    assert(app);
    assert(actor);
    assert(out_x);
    assert(out_y);
    if( !app->world || !app->world_view_valid ) return 0;
    int fine_x = (int)actor->draw_position.x;
    int fine_z = (int)actor->draw_position.z;
    int view_id = actor->view_placement.view_id;
    int min_y = 0;
    struct ToriDraw_SceneElement* element = actor->element_id >= 0
        ? ToriDraw_SceneElementGet(app->scene, actor->element_id) : NULL;
    const struct ToriDraw_Model* model = element && ToriDraw_ModelKindIsFull(element->model.kind)
        ? ToriDraw_ModelRead(element->model) : NULL;
    for( int i = 0; model && i < model->vertex_count; ++i )
        if( model->vertices_y[i] < min_y ) min_y = model->vertices_y[i];
    /* Half the model's height: mid-body, where a user aims and where the pick
     * has the most of the actor under it. */
    int height = -min_y / 2;
    int ground_y;
    if( view_id > 0 && Wevs_IsLive(&app->wevs, view_id) &&
        WorldviewRegistry_IsLive(&app->worldviews, view_id) )
    {
        struct Wev* wev = Wevs_Get(&app->wevs, view_id);
        struct Worldview* view = WorldviewRegistry_Get(&app->worldviews, view_id);
        struct WevDeckBox box;
        int root_fx, root_fz, level;
        assert(wev->config);
        if( wev->flattened || !wev->render_visible ) return 0;
        if( wev->parent_view_id != WORLDVIEW_ROOT ) return 0;
        if( actor->view_placement.home_view == 0 )
        {
            fine_x = actor->view_placement.x;
            fine_z = actor->view_placement.z;
        }
        box.pos_x = wev->x - (app->world->_base_tile_x << 7);
        box.pos_z = wev->z - (app->world->_base_tile_z << 7);
        box.angle = wev->angle;
        box.recenter_x = -(view->size_x_tiles * 64) - wev->config->pivot_x;
        box.recenter_z = -(view->size_z_tiles * 64) - wev->config->pivot_z;
        box.size_x_tiles = view->size_x_tiles;
        box.size_z_tiles = view->size_z_tiles;
        Wev_ParentFromDeck(&box, fine_x, fine_z, &root_fx, &root_fz);
        level = wev->config->plane < 0 ? 0 : wev->config->plane;
        if( level >= COLLISION_LEVELS ) level = COLLISION_LEVELS - 1;
        ground_y = wev->y + wev->bob_y + sailing_ground_height(view->world, fine_x, fine_z, level);
        fine_x = root_fx;
        fine_z = root_fz;
    }
    else
    {
        int level = actor->grid_position.level;
        if( level < 0 ) level = 0;
        if( level >= COLLISION_LEVELS ) level = COLLISION_LEVELS - 1;
        ground_y = sailing_ground_height(app->world, fine_x, fine_z, level);
    }
    if( fine_x < 128 || fine_z < 128 ) return 0;
    return ToriRS_WorldProjectPoint(&app->world_camera, &app->world_camera_pos,
        app->world_emit_desc.x, app->world_emit_desc.y,
        app->world_emit_desc.w, app->world_emit_desc.h, 50,
        fine_x, ground_y - height, fine_z, out_x, out_y);
}

/* The client's own answer to "is that actor under the cursor?", from the last
 * completed pick pass. A `hover` at the projected point proves the reading. */
static int peer_picked(struct App* app, int element_id)
{
    assert(app);
    for( int i = 0; i < app->world_pickset.count; ++i )
        if( app->world_pickset.items[i].element_id == element_id &&
            app->world_pickset.items[i].type == WORLD_PICK_PLAYER )
            return 1;
    return 0;
}

void ContentTestSailing_PeerState(struct App* app, struct ToriRSServerEmbed* embed,
                                 char* json, size_t cap)
{
    assert(app);
    assert(json);
    assert(cap);
    struct ToriRSServerPlayer* peer = sailing_peer(embed);
    if( !peer ) { snprintf(json, cap, "{\"ok\":true,\"present\":false}"); return; }
    int wire = ToriRSServer_WirePlayerIndex(peer->pid);
    struct WorldEntity_Player* actor = World_PlayerGetByServerPid(app->world, wire);
    for( int view = 1; !actor && view < WORLDVIEW_MAX; ++view )
        if( WorldviewRegistry_IsLive(&app->worldviews, view) )
            actor = World_PlayerGetByServerPid(WorldviewRegistry_Get(&app->worldviews, view)->world, wire);
    int screen_x = -1, screen_y = -1;
    int projected = actor && peer_screen(app, actor, &screen_x, &screen_y);
    /* The server's own verdicts on the peer, read from the captain's hull:
     * "can this guest steer?" and "can this guest reach the hold?". These are
     * the two rules the social/permission proofs are about. */
    struct ToriRSServer* srv = ToriRSServer_EmbedWorld(embed);
    struct ToriRSServerPlayer* captain = ToriRSServer_EmbedPlayer(embed, 0);
    struct ToriRSServerVessel* boat = srv && captain && captain->active
        ? ToriRSServer_VesselAtTile(srv, captain->x, captain->z) : NULL;
    int can_navigate = boat && ToriRSServer_VesselCanNavigate(srv, peer, boat);
    int can_cargo = boat && ToriRSServer_VesselCargoAllowed(srv, peer, boat);
    snprintf(json, cap,
        "{\"ok\":true,\"present\":true,\"pid\":%d,\"wire_pid\":%d,\"name\":\"%s\","
        "\"server\":{\"x\":%d,\"z\":%d,\"level\":%d,\"obs_x\":%d,\"obs_z\":%d,\"obs_level\":%d,"
        "\"navigating\":%d,\"running\":%d,\"mainmodal\":%d,\"sidemodal\":%d,\"role\":%d,\"varbit\":%d,\"value\":%d,"
        "\"script\":%d,\"script_wait\":%d},"
        "\"client\":{\"present\":%s,\"element\":%d,\"view\":%d,\"home_view\":%d,\"fine_x\":%d,\"fine_z\":%d,\"view_x\":%d,\"view_z\":%d,\"route_length\":%d,\"route_run\":%d},"
        "\"screen\":{\"projected\":%s,\"x\":%d,\"y\":%d,\"picked\":%s},"
        "\"permissions\":{\"captain_boat\":%d,\"navigate\":%s,\"cargo\":%s}}",
        peer->pid, wire, peer->display_name, peer->x, peer->z, peer->level,
        peer->obs_x, peer->obs_z, peer->obs_level, peer->navigating_vessel, peer->running,
        peer->mainmodal_group, peer->sidemodal_group,
        peer_role_varbit() >= 0 ? ToriRSServer_VarbitGet(peer, peer_role_varbit()) : -1,
        peer_query_varbit, peer_query_varbit >= 0 ? ToriRSServer_VarbitGet(peer, peer_query_varbit) : -1,
        peer->active_script != NULL,
        peer->active_script ? (int)peer->active_script->execution : -1,
        actor ? "true" : "false", actor ? actor->element_id : -1,
        actor ? actor->view_placement.view_id : -1, actor ? actor->view_placement.home_view : -1,
        actor ? (int)actor->draw_position.x : -1, actor ? (int)actor->draw_position.z : -1,
        actor ? actor->view_placement.x : -1, actor ? actor->view_placement.z : -1,
        actor ? actor->pathing.route_length : -1,
        actor && actor->pathing.route_length ? actor->pathing.route_run[actor->pathing.route_length-1] : -1,
        projected ? "true" : "false", projected ? screen_x : -1, projected ? screen_y : -1,
        actor && peer_picked(app, actor->element_id) ? "true" : "false",
        boat ? boat->index : 0, can_navigate ? "true" : "false",
        can_cargo ? "true" : "false");
}

static int ready(struct App* app, struct ToriRSServerEmbed* embed,
                 int require_aboard, char* error, size_t cap)
{
    if( !embed || !ToriRSServer_EmbedOnline(embed, 0) || !app->world ||
        !app->world->load_complete )
        return fail(error, cap, "checkpoint requires a loaded embedded world");
    struct ToriRSServerPlayer* player = ToriRSServer_EmbedPlayer(embed, 0);
    struct ToriRSServer* srv = ToriRSServer_EmbedWorld(embed);
    assert(player);
    assert(srv);
    if( sailing_peer(embed) )
        return fail(error, cap, "remove the peer before checkpointing; checkpoints cover the primary player");
    if( player->active_script || player->action_locked || player->dying ||
        player->combat_target >= 0 || player->delayed_until > srv->tick ||
        player->remote_view_active || app->cam_script.scripted )
        return fail(error, cap, "checkpoint requires an idle player without an active script or encounter");
    for( int i = 0; i < TORIRSSERVER_WORLD_QUEUE_MAX; ++i )
        if( srv->world_queue[i].active )
            return fail(error, cap, "checkpoint cannot rewind a suspended world script");
    /* Scheduled player work contains scalar script ids/arguments and relative
     * delays; snapshot it below. Unlike active_script/world_queue it owns no
     * suspended VM. Layout changes legitimately queue gameframe_apply_mode. */
    for( int i=0; i<TORIRSSERVER_VESSEL_MAX; ++i )
        if( srv->vessels[i].in_use && srv->vessels[i].owner_uid==player->pid+1 )
            for( int hotspot=0; hotspot<13; ++hotspot )
                if( ToriRSServer_MapInstanceVarGet(srv->vessels[i].instance,8+hotspot*7) )
                    return fail(error,cap,"stop active facility operations before checkpointing; world resources are not rewound");
    if( require_aboard && !ToriRSServer_VesselAtTile(srv, player->x, player->z) )
        return fail(error, cap, "sailing checkpoints require the player aboard a built vessel");
    return 1;
}

static int name_valid(const char* name)
{
    size_t n = strlen(name);
    if( n == 0 || n >= SAILING_CHECKPOINT_NAME_MAX ) return 0;
    for( size_t i = 0; i < n; ++i )
        if( !((name[i] >= 'a' && name[i] <= 'z') ||
              (name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= '0' && name[i] <= '9') ||
              name[i] == '_' || name[i] == '-') ) return 0;
    return 1;
}

int ContentTestSailing_Save(struct App* app, struct ToriRSServerEmbed* embed,
                           const char* name, char* error, size_t cap)
{
    assert(app);
    assert(name);
    assert(error);
    assert(cap > 0);
    error[0] = 0;
    if( !name_valid(name) ) return fail(error, cap, "invalid checkpoint name");
    if( !ready(app, embed, 1, error, cap) ) return 0;
    struct ToriRSServer* world = ToriRSServer_EmbedWorld(embed);
    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; ++i )
    {
        const struct ToriRSServerVessel* boat = &world->vessels[i];
        int view = boat->view_id;
        if( !boat->in_use || view < 1 || view >= WORLDVIEW_MAX ||
            !Wevs_IsLive(&app->wevs, view) ) continue;
        const struct Wev* wev = Wevs_Get(&app->wevs, view);
        if( wev->queue[0].x != boat->fine_x || wev->queue[0].z != boat->fine_z ||
            wev->queue[0].angle != boat->angle )
            return fail(error, cap, "checkpoint requires delivered vessel targets");
    }
    int slot = -1;
    for( int i = 0; i < SAILING_CHECKPOINT_MAX; ++i )
    {
        if( checkpoints[i] && strcmp(checkpoints[i]->name, name) == 0 )
        {
            slot = i;
            break;
        }
        if( !checkpoints[i] && slot < 0 ) slot = i;
    }
    if( slot < 0 ) return fail(error, cap, "all eight checkpoint slots are occupied");
    if( !checkpoints[slot] )
    {
        checkpoints[slot] = calloc(1, sizeof(*checkpoints[slot]));
        assert(checkpoints[slot]);
    }
    struct SailingCheckpoint* saved = checkpoints[slot];
    struct ToriRSServerPlayer* player = ToriRSServer_EmbedPlayer(embed, 0);
    struct ToriRSServer* srv = ToriRSServer_EmbedWorld(embed);
    snprintf(saved->name, sizeof(saved->name), "%s", name);
    saved->pid = player->pid;
    saved->server_tick = srv->tick;
    saved->login_generation = player->login_generation;
    saved->x = player->x; saved->z = player->z; saved->level = player->level;
    saved->navigating_vessel = player->navigating_vessel;
    saved->navigating_vessel_serial = player->navigating_vessel_serial;
    memcpy(saved->vessels, srv->vessels, sizeof(saved->vessels));
    for( int i=0; i<TORIRSSERVER_VESSEL_MAX; ++i )
        for( int key=0; key<SAILING_CHECKPOINT_REGS; ++key )
            saved->instance_vars[i][key]=srv->vessels[i].in_use
                ? ToriRSServer_MapInstanceVarGet(srv->vessels[i].instance,key) : 0;
    for( int i=0; i<TORIRSSERVER_VESSEL_MAX; ++i )
        for( int slot=0; slot<5; ++slot )
            saved->crew_clocks[i][slot]=srv->vessels[i].in_use
                ? ToriRSServer_MapInstanceVarGet(srv->vessels[i].instance,117+slot) : 0;
    memcpy(saved->varps, player->varps, sizeof(saved->varps));
    memcpy(saved->stat_level,player->stat_level,sizeof(saved->stat_level));
    memcpy(saved->stat_boosted,player->stat_boosted,sizeof(saved->stat_boosted));
    memcpy(saved->stat_xp_tenths,player->stat_xp_tenths,sizeof(saved->stat_xp_tenths));
    saved->hitpoints=player->hitpoints;
    saved->max_hitpoints=player->max_hitpoints;
    memcpy(saved->inv, player->inv, sizeof(saved->inv));
    memcpy(saved->worn, player->worn, sizeof(saved->worn));
    memcpy(saved->queue, player->queue, sizeof(saved->queue));
    memcpy(saved->engine_queue, player->engine_queue, sizeof(saved->engine_queue));
    memcpy(saved->timers, player->timers, sizeof(saved->timers));
    const struct ToriRSServerIds* ids = ToriRSServer_Ids();
    int cargo_ids[SAILING_CHECKPOINT_INVS] = {ids->inv_sailing_cargo_1,ids->inv_sailing_cargo_2,
        ids->inv_sailing_cargo_3,ids->inv_sailing_cargo_4,ids->inv_sailing_cargo_5,
        ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_INV,"sailing_trawling_net")};
    for( int i=0; i<SAILING_CHECKPOINT_INVS; ++i )
    {
        free(saved->cargo[i].items);
        saved->cargo[i].items=NULL;
        saved->cargo[i].slots=0;
        saved->cargo[i].inv_id=cargo_ids[i];
        if( cargo_ids[i]<0 ) continue;
        struct ToriRSServerContainer* cargo=ToriRSServer_ContainerResolve(srv,player,cargo_ids[i]);
        assert(cargo);
        saved->cargo[i].slots=cargo->slots;
        saved->cargo[i].items=malloc((size_t)cargo->slots*sizeof(*cargo->items));
        assert(saved->cargo[i].items);
        memcpy(saved->cargo[i].items,cargo->items,(size_t)cargo->slots*sizeof(*cargo->items));
    }
    saved->camera = app->world_camera;
    saved->camera_position = app->world_camera_pos;
    saved->base_x = app->world->_base_tile_x;
    saved->base_z = app->world->_base_tile_z;
    saved->yaw = app->orbit_yaw; saved->pitch = app->orbit_pitch;
    saved->yaw_velocity = app->orbit_yaw_vel;
    saved->pitch_velocity = app->orbit_pitch_vel;
    saved->orbit_x = app->orbit_x; saved->orbit_z = app->orbit_z;
    saved->pitch_clamp = app->camera_pitch_clamp;
    saved->zoom = app->world_cam_zoom;
    saved->unlocked = app->camera_unlocked;
    saved->side_tab = app->slots.side_tab;
    saved->aboard_view = app->aboard_view;
    saved->selected_heading=app->sailing_selected_heading;
    saved->selected_remaining=app->sailing_selected_until>app->logic_cycle
        ? app->sailing_selected_until-app->logic_cycle : 0;
    saved->wev_clock = app->wevs.clock;
    saved->scenery_count=0;
    for( int view_id=1; view_id<WORLDVIEW_MAX; ++view_id )
    {
        if( !WorldviewRegistry_IsLive(&app->worldviews,view_id) ) continue;
        struct World* view=WorldviewRegistry_Get(&app->worldviews,view_id)->world;
        struct World_EntityPool* pool=&view->entities.scenery;
        for( int id=World_EntityPoolHead(pool); id>=0; id=World_EntityPoolNext(pool,id) )
        {
            struct WorldEntity_Scenery* sc=World_EntityPoolGet(pool,id);
            struct ToriDraw_SceneElement* el=ToriDraw_SceneElementGet(app->scene,sc->element_id);
            if( !el || el->anim_seq_id<0 ) continue;
            assert(saved->scenery_count<SAILING_CHECKPOINT_ANIMS);
            int n=saved->scenery_count++;
            saved->scenery[n].view=view_id;
            saved->scenery[n].x=sc->grid_position.x;
            saved->scenery[n].z=sc->grid_position.z;
            saved->scenery[n].level=sc->grid_position.level;
            saved->scenery[n].shape=sc->shape;
            saved->scenery[n].loc_id=sc->loc_id;
            saved->scenery[n].seq_id=el->anim_seq_id;
            saved->scenery[n].frame=el->anim_frame;
            saved->scenery[n].cycle=el->anim_cycle;
            saved->scenery[n].loop=el->anim_loop;
        }
    }
    for( int i = 1; i < WORLDVIEW_MAX; ++i )
    {
        saved->bob[i].live = Wevs_IsLive(&app->wevs, i);
        if( !saved->bob[i].live ) continue;
        struct Wev* wev = Wevs_Get(&app->wevs, i);
        save_motion(&saved->motion[i], wev);
        saved->bob[i].seq_id = wev->seq_id;
        saved->bob[i].seq_delay = wev->seq_delay;
        saved->bob[i].bob_y = wev->bob_y;
        saved->bob[i].anim_age = app->wevs.clock - wev->anim_start_cycle;
        saved->bob[i].seq_age = app->wevs.clock - wev->seq_start_cycle;
    }
    return 1;
}

int ContentTestSailing_Restore(struct App* app, struct ToriRSServerEmbed* embed,
                              const char* name, char* error, size_t cap)
{
    assert(app);
    assert(name);
    assert(error);
    assert(cap > 0);
    error[0] = 0;
    restore_slot = -1;
    if( !ready(app, embed, 0, error, cap) ) return 0;
    struct SailingCheckpoint* saved = NULL;
    int selected_slot = -1;
    for( int i = 0; i < SAILING_CHECKPOINT_MAX; ++i )
        if( checkpoints[i] && strcmp(checkpoints[i]->name, name) == 0 )
        {
            saved = checkpoints[i];
            selected_slot = i;
        }
    if( !saved ) return fail(error, cap, "unknown checkpoint");
    struct ToriRSServerPlayer* player = ToriRSServer_EmbedPlayer(embed, 0);
    struct ToriRSServer* srv = ToriRSServer_EmbedWorld(embed);
    if( saved->pid != player->pid || saved->login_generation != player->login_generation )
        return fail(error, cap, "checkpoint belongs to a previous player session");
    /* Preflight everything before writing: rejected restores are atomic. Deck
     * scenes and facilities are retained, so identity/layout changes require a
     * new fixture, never copying an obsolete instance/window handle back. */
    int facility_changed[TORIRSSERVER_VESSEL_MAX] = {0};
    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; ++i )
    {
        const struct ToriRSServerVessel* live = &srv->vessels[i];
        const struct ToriRSServerVessel* old = &saved->vessels[i];
        if( live->in_use != old->in_use )
            return fail(error, cap, "vessel topology changed since checkpoint");
        if( !live->in_use ) continue;
        if( live->serial != old->serial || live->instance != old->instance ||
            live->view_id != old->view_id || live->config_id != old->config_id ||
            live->deck_window != old->deck_window || live->deck_src_x != old->deck_src_x ||
            live->deck_src_z != old->deck_src_z || live->size_x_tiles != old->size_x_tiles ||
            live->size_z_tiles != old->size_z_tiles )
            return fail(error, cap, "deck identity changed since checkpoint");
        facility_changed[i]=memcmp(live->facility,old->facility,sizeof(live->facility))!=0 ||
            live->anchored!=old->anchored;
        for( int key=0; key<SAILING_CHECKPOINT_REGS && !facility_changed[i]; ++key )
            facility_changed[i]=ToriRSServer_MapInstanceVarGet(live->instance,key)!=saved->instance_vars[i][key];
        if( facility_changed[i] && (!srv->scripts ||
            !SSVM_ProviderGetByName(srv->scripts,"[proc,sailing_facilities_restore]")) )
            return fail(error,cap,"facility restore requires the sailing content proc");
    }
    for( int i=0; i<SAILING_CHECKPOINT_INVS; ++i )
        if( saved->cargo[i].items )
        {
            struct ToriRSServerContainer* cargo=ToriRSServer_ContainerResolve(srv,player,saved->cargo[i].inv_id);
            if( !cargo || cargo->slots!=saved->cargo[i].slots )
                return fail(error,cap,"cargo container layout changed since checkpoint");
        }
    for( int view = 1; view < WORLDVIEW_MAX; ++view )
        restore_receipts[view] = Wevs_IsLive(&app->wevs, view)
            ? Wevs_Get(&app->wevs, view)->teleport_serial : 0;
    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; ++i )
    {
        struct ToriRSServerVessel* live = &srv->vessels[i];
        if( !live->in_use ) continue;
        int seq_stamp = live->seq_stamp + 1;
        int teleport_stamp = live->teleport_stamp + 1;
        *live = saved->vessels[i];
        live->seq_stamp = seq_stamp;
        live->teleport_stamp = teleport_stamp;
    }
    struct ToriRSServerPlayer* previous_active = srv->active_player;
    ToriRSServer_WorldSetActive(srv, player);
    ToriRSServer_WorldInteractionClearAt(player);
    ToriRSServer_WorldTeleport(srv, saved->level, saved->x, saved->z);
    player->navigating_vessel = saved->navigating_vessel;
    player->navigating_vessel_serial = saved->navigating_vessel_serial;
    for( int i = 0; i < TORIRSSERVER_VARP_COUNT; ++i )
        if( player->varps[i] != saved->varps[i] )
            ToriRSServer_WorldSetVarpOn(srv, player, i, saved->varps[i]);
    for( int i=0; i<TORIRSSERVER_STAT_COUNT; ++i )
    {
        if( player->stat_level[i]!=saved->stat_level[i] ||
            player->stat_boosted[i]!=saved->stat_boosted[i] ||
            player->stat_xp_tenths[i]!=saved->stat_xp_tenths[i] ) player->stat_dirty|=1u<<i;
        player->stat_level[i]=saved->stat_level[i];
        player->stat_boosted[i]=saved->stat_boosted[i];
        player->stat_xp_tenths[i]=saved->stat_xp_tenths[i];
    }
    player->hitpoints=saved->hitpoints;
    player->max_hitpoints=saved->max_hitpoints;
    memcpy(player->inv, saved->inv, sizeof(saved->inv));
    memcpy(player->worn, saved->worn, sizeof(saved->worn));
    for( int i=0; i<SAILING_CHECKPOINT_INVS; ++i )
        if( saved->cargo[i].items )
        {
            struct ToriRSServerContainer* cargo=ToriRSServer_ContainerResolve(srv,player,saved->cargo[i].inv_id);
            assert(cargo);
            memcpy(cargo->items,saved->cargo[i].items,(size_t)cargo->slots*sizeof(*cargo->items));
            ToriRSServer_ContainerMarkAll(cargo);
        }
    for( int i=0; i<TORIRSSERVER_VESSEL_MAX; ++i )
        if( srv->vessels[i].in_use )
        {
            int32_t handle=srv->vessels[i].index;
            for( int key=0; key<SAILING_CHECKPOINT_REGS; ++key )
                ToriRSServer_MapInstanceVarSet(srv->vessels[i].instance,key,saved->instance_vars[i][key]);
            for( int slot=0; slot<5; ++slot )
                ToriRSServer_MapInstanceVarSet(srv->vessels[i].instance,117+slot,saved->crew_clocks[i][slot]);
            if( facility_changed[i] )
            {
                int ran=ToriRSServer_ScriptsRunProc(srv,"[proc,sailing_facilities_restore]",&handle,1);
                assert(ran);
                (void)ran;
                if( SSVM_ProviderGetByName(srv->scripts,"[proc,sailing_wind_publish]") )
                    ToriRSServer_ScriptsRunProc(srv,"[proc,sailing_wind_publish]",&handle,1);
            }
            else if( srv->scripts && SSVM_ProviderGetByName(srv->scripts,"[proc,sailing_sail_visual_on]") )
                ToriRSServer_ScriptsRunProc(srv,"[proc,sailing_sail_visual_on]",&handle,1);
        }
    if( srv->scripts && SSVM_ProviderGetByName(srv->scripts,"[proc,sailing_crew_sync]") )
        ToriRSServer_ScriptsRunProc(srv,"[proc,sailing_crew_sync]",NULL,0);
    memcpy(player->queue, saved->queue, sizeof(saved->queue));
    memcpy(player->engine_queue, saved->engine_queue, sizeof(saved->engine_queue));
    memcpy(player->timers, saved->timers, sizeof(saved->timers));
    for( int i = 0; i < TORIRSSERVER_TIMER_MAX; ++i )
        if( player->timers[i].active )
            player->timers[i].clock += srv->tick - saved->server_tick;
    for( int i = 0; i < TORIRSSERVER_CONTAINER_MAX; ++i )
    {
        struct ToriRSServerContainer* row = &player->containers[i];
        if( row->used && (row->items == player->inv || row->items == player->worn) )
            ToriRSServer_ContainerMarkAll(row);
    }
    ToriRSServer_WorldSetActive(srv, previous_active);
    restore_slot = selected_slot;
    return 1;
}

int ContentTestSailing_RestoreDelivered(struct App* app)
{
    assert(app);
    if( restore_slot < 0 ) return 1;
    const struct SailingCheckpoint* saved = checkpoints[restore_slot];
    assert(saved);
    if( !app->world || app->aboard_view != saved->aboard_view ) return 0;
    for( int i = 0; i < TORIRSSERVER_VESSEL_MAX; ++i )
    {
        const struct ToriRSServerVessel* boat = &saved->vessels[i];
        int view = boat->view_id;
        if( !boat->in_use || view < 1 || view >= WORLDVIEW_MAX ||
            !saved->bob[view].live ) continue;
        if( !Wevs_IsLive(&app->wevs, view) ) return 0;
        const struct Wev* wev = Wevs_Get(&app->wevs, view);
        if( wev->teleport_serial == restore_receipts[view] ||
            wev->config_id != boat->config_id ||
            wev->queue[0].x != boat->fine_x ||
            wev->queue[0].z != boat->fine_z ||
            wev->queue[0].angle != boat->angle )
        {
            static unsigned retries;
            if( getenv("TORIRS_SAILING_RESTORE_DEBUG") && (retries++ % 4096) == 0 )
                fprintf(stderr, "sailing restore waiting: view%d receipt%u/%u config%d/%d target%d,%d,%d expected%d,%d,%d\n",
                        view, wev->teleport_serial, restore_receipts[view],
                        wev->config_id, boat->config_id,
                        wev->queue[0].x, wev->queue[0].z, wev->queue[0].angle,
                        boat->fine_x, boat->fine_z, boat->angle);
            return 0;
        }
    }
    return 1;
}

void ContentTestSailing_SettleRestore(struct App* app)
{
    assert(app);
    if( restore_slot < 0 ) return;
    assert(ContentTestSailing_RestoreDelivered(app));
    struct SailingCheckpoint* saved = checkpoints[restore_slot];
    assert(saved);
    assert(app->world);
    for( int n=0; n<saved->scenery_count; ++n )
    {
        int view_id=saved->scenery[n].view;
        assert(WorldviewRegistry_IsLive(&app->worldviews,view_id));
        struct World* view=WorldviewRegistry_Get(&app->worldviews,view_id)->world;
        int id=World_SceneryFindAt(view,saved->scenery[n].x,saved->scenery[n].z,
            saved->scenery[n].level,saved->scenery[n].shape);
        struct WorldEntity_Scenery* sc=id>=0 ? World_EntityPoolGet(&view->entities.scenery,id) : NULL;
        assert(sc);
        assert(sc->loc_id==saved->scenery[n].loc_id);
        struct ToriDraw_Animation* anim=ToriDraw_SceneAnimationGet(app->scene,saved->scenery[n].seq_id);
        assert(anim);
        ToriDraw_SceneElementSetAnimationSeq(app->scene,sc->element_id,saved->scenery[n].seq_id);
        ToriDraw_SceneElementSetAnimation(app->scene,sc->element_id,anim,true);
        struct ToriDraw_SceneElement* el=ToriDraw_SceneElementGet(app->scene,sc->element_id);
        assert(el);
        el->is_skeletal=anim->skeletal!=NULL;
        el->skeletal_animation=anim->skeletal;
        el->skeletal_play_frames=el->is_skeletal ? anim->frame_count : 0;
        el->anim_loop=saved->scenery[n].loop;
        el->anim_frame=saved->scenery[n].frame;
        el->anim_cycle=saved->scenery[n].cycle;
        ToriDraw_SceneElementPoseInvalidate(app->scene,sc->element_id);
        ToriDraw_SceneElementApplyAnimation(app->scene,sc->element_id,true,el->anim_frame);
    }
    for( int i = 1; i < WORLDVIEW_MAX; ++i )
        if( Wevs_IsLive(&app->wevs, i) )
        {
            struct Wev* wev = Wevs_Get(&app->wevs, i);
            if( saved->bob[i].live )
            {
                /* Delivery was checked first. Restore the paused visual pose
                 * and interpolation phase while retaining the same received
                 * newest target from which future server deltas chain. */
                restore_motion(wev, &saved->motion[i], app->wevs.clock - saved->wev_clock);
                wev->seq_id = saved->bob[i].seq_id;
                wev->seq_delay = saved->bob[i].seq_delay;
                wev->bob_y = saved->bob[i].bob_y;
                wev->anim_start_cycle = app->wevs.clock - saved->bob[i].anim_age;
                wev->seq_start_cycle = app->wevs.clock - saved->bob[i].seq_age;
            }
        }
    /* Camera positions are root-scene local. Retain the absolute viewpoint
     * even when sailing has moved the root scene window since save. */
    int dx = (saved->base_x - app->world->_base_tile_x) * 128;
    int dz = (saved->base_z - app->world->_base_tile_z) * 128;
    app->world_camera = saved->camera;
    app->world_camera_pos = saved->camera_position;
    app->world_camera_pos.x += dx;
    app->world_camera_pos.z += dz;
    app->orbit_yaw = saved->yaw; app->orbit_pitch = saved->pitch;
    app->orbit_yaw_vel = saved->yaw_velocity;
    app->orbit_pitch_vel = saved->pitch_velocity;
    app->orbit_x = saved->orbit_x + dx; app->orbit_z = saved->orbit_z + dz;
    app->camera_pitch_clamp = saved->pitch_clamp;
    app->world_cam_zoom = saved->zoom;
    app->sailing_selected_heading=saved->selected_heading;
    app->sailing_selected_until=app->logic_cycle+saved->selected_remaining;
    app->camera_unlocked = saved->unlocked;
    app->slots.side_tab = saved->side_tab;
    app->need_redraw = 1;
    restore_slot = -1;
}

void ContentTestSailing_State(struct App* app, struct ToriRSServerEmbed* embed,
                             char* json, size_t cap)
{
    assert(app);
    (void)app;
    assert(json);
    assert(cap > 0);
    struct ToriRSServerPlayer* player = embed ? ToriRSServer_EmbedPlayer(embed, 0) : NULL;
    struct ToriRSServer* srv = embed ? ToriRSServer_EmbedWorld(embed) : NULL;
    struct ToriRSServerVessel* vessel = player && srv
        ? ToriRSServer_VesselAtTile(srv, player->x, player->z) : NULL;
    int count = 0;
    for( int i = 0; i < SAILING_CHECKPOINT_MAX; ++i ) count += checkpoints[i] != NULL;
    if( !vessel )
    {
        snprintf(json, cap, "{\"ok\":true,\"aboard\":false,\"checkpoints\":%d}", count);
        return;
    }
    snprintf(json, cap,
        "{\"ok\":true,\"aboard\":true,\"checkpoints\":%d,"
        "\"checkpoint_scope\":\"movement,stats,varps,inventories,facilities,timers,camera,deck animation; unchanged vessel topology\","
        "\"server_tick\":%d,\"player\":{\"x\":%d,\"z\":%d,\"level\":%d,\"navigating\":%d},"
        "\"vessel\":{\"id\":%d,\"serial\":%d,\"view\":%d,\"config\":%d,\"instance\":%d,"
        "\"fine_x\":%d,\"fine_z\":%d,\"angle\":%d,\"heading\":%d,\"state\":%d,"
        "\"speed_tier\":%d,\"sails_set\":%d,\"reversing\":%d,\"hp\":%d,\"hp_max\":%d,"
        "\"owner_uid\":%d,\"cargo_slot\":%d,\"navigator_mask\":%u,"
        "\"facilities\":[%d,%d,%d]}}",
        count, srv->tick, player->x, player->z, player->level, player->navigating_vessel,
        vessel->index, vessel->serial, vessel->view_id, vessel->config_id, vessel->instance,
        vessel->fine_x, vessel->fine_z, vessel->angle, vessel->heading, vessel->state,
        vessel->speed_tier, vessel->sails_set, vessel->reversing, vessel->hp, vessel->hp_max,
        vessel->owner_uid, vessel->cargo_slot,
        (unsigned)ToriRSServer_VesselNavigatorMask(srv, vessel),
        vessel->facility[0], vessel->facility[1], vessel->facility[2]);
    const struct Wev* wev = vessel->view_id > 0 &&
        Wevs_IsLive(&app->wevs, vessel->view_id) ? Wevs_Get(&app->wevs, vessel->view_id) : NULL;
    /* World-entity transforms are already absolute root fine coordinates. */
    size_t end = strlen(json);
    if( end && end < cap )
        snprintf(json + end - 1, cap - end + 1,
            ",\"client_vessel\":{\"present\":%s,\"fine_x\":%d,\"fine_z\":%d,"
            "\"angle\":%d,\"target_fine_x\":%d,\"target_fine_z\":%d,\"target_angle\":%d,"
            "\"queue_count\":%d}}",
            wev ? "true" : "false", wev ? wev->x : -1,
            wev ? wev->z : -1, wev ? wev->angle : -1,
            wev ? wev->queue[0].x : -1, wev ? wev->queue[0].z : -1,
            wev ? wev->queue[0].angle : -1, wev ? wev->queue_count : -1);
}

void ContentTestSailing_Clear(void)
{
    restore_slot = -1;
    for( int i = 0; i < SAILING_CHECKPOINT_MAX; ++i )
    {
        if( checkpoints[i] )
            for( int j=0; j<SAILING_CHECKPOINT_INVS; ++j ) free(checkpoints[i]->cargo[j].items);
        free(checkpoints[i]);
        checkpoints[i] = NULL;
    }
}
#else
int ContentTestSailing_PeerCommand(struct ToriRSServerEmbed* embed, const char* command,
                                  int* changed, char* error, size_t cap)
{ (void)embed; (void)command; *changed=0; snprintf(error,cap,"embedded server required"); return 0; }
void ContentTestSailing_PeerState(struct App* app, struct ToriRSServerEmbed* embed, char* json, size_t cap)
{ (void)app; (void)embed; snprintf(json,cap,"{\"ok\":false}"); }
int ContentTestSailing_PrimaryProc(struct ToriRSServerEmbed* embed, const char* command,
                                  char* error, size_t cap)
{
    (void)embed; (void)command;
    assert(error);
    assert(cap > 0);
    snprintf(error, cap, "content procs require EMBED_SERVER=1");
    return 0;
}
int ContentTestSailing_Save(struct App* app, struct ToriRSServerEmbed* embed,
                           const char* name, char* error, size_t cap)
{
    (void)app; (void)embed; (void)name;
    assert(error);
    assert(cap > 0);
    snprintf(error, cap, "sailing checkpoints require EMBED_SERVER=1");
    return 0;
}
int ContentTestSailing_Restore(struct App* app, struct ToriRSServerEmbed* embed,
                              const char* name, char* error, size_t cap)
{
    return ContentTestSailing_Save(app, embed, name, error, cap);
}
void ContentTestSailing_State(struct App* app, struct ToriRSServerEmbed* embed,
                             char* json, size_t cap)
{
    (void)app; (void)embed;
    assert(json);
    assert(cap > 0);
    snprintf(json, cap, "{\"ok\":false,\"error\":\"embedded server required\"}");
}
void ContentTestSailing_Clear(void) {}
int ContentTestSailing_RestoreDelivered(struct App* app) { (void)app; return 1; }
void ContentTestSailing_SettleRestore(struct App* app) { (void)app; }
#endif
