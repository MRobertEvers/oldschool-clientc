/* Replay production PLAYER_INFO packets through the actual rev239 client.
 * One decoder is stateful, so each case selects one observer for its full
 * consecutive stream; recipient_pid keeps packets from the other clients out. */
#include <rscache.h>

#include "net/rev/packets/pkt_player_info.h"
#include "net/rev/packets/pkt_npc_info.h"

void osrs239_playerinfo_set_local(int local_index);
void osrs239_playerinfo_init(const uint8_t* data, int len);
int osrs239_player_info_read(const uint8_t* data, int len,
                            struct PktPlayerInfoOp* ops, int cap);

int osrs239_npc_info_read(const uint8_t* data, int len, struct PktNpcInfoOp* ops, int cap);

struct SailingClientProbe
{
    int local_index;
    int present[MOCK239_PLAYER_SLOTS];
    int x[MOCK239_PLAYER_SLOTS], z[MOCK239_PLAYER_SLOTS], level[MOCK239_PLAYER_SLOTS];
    int speed[MOCK239_PLAYER_SLOTS], jump[MOCK239_PLAYER_SLOTS];
    int appearances[MOCK239_PLAYER_SLOTS], removals[MOCK239_PLAYER_SLOTS];
};

static void
sailing_client_decode(struct ToriRSServerCapture* capture,
                      struct ToriRSServerPlayer* viewer, struct SailingClientProbe* client)
{
    int packets = 0;
    SELFTEST_CHECK(!capture->overflow, "the multiplayer packet capture did not overflow");
    for( int p = 0; p < capture->count; ++p )
    {
        const struct ToriRSServerCapturedPacket* packet = &capture->packets[p];
        if( packet->recipient_pid != viewer->pid || packet->name != PKT_NAME_PLAYER_INFO ) continue;
        struct PktPlayerInfoOp ops[256];
        int target = -1;
        SELFTEST_CHECK(!packet->truncated, "the production PLAYER_INFO is fully captured");
        int count = osrs239_player_info_read(packet->data, packet->len, ops, 256);
        SELFTEST_CHECK(count >= 0 && count < 256, "the actual client decodes the production packet (%d operations)", count);
        packets++;
        for( int i = 0; i < count; ++i )
        {
            struct PktPlayerInfoOp* op = &ops[i];
            if( op->kind == PKT_PLAYER_INFO_OP_SET_LOCAL_PLAYER ) target = client->local_index;
            else if( op->kind == PKT_PLAYER_INFO_OP_ADD_PLAYER_NEW_OPBITS_PID )
            { target = (int)op->_bitvalue; client->present[target] = 1; }
            else if( op->kind == PKT_PLAYER_INFO_OP_REMOVE_PLAYER_PID )
            { client->present[op->_bitvalue] = 0; client->removals[op->_bitvalue]++; }
            else if( op->kind == PKT_PLAYER_INFO_OP_ABS_XZLEVEL && target >= 0 )
            {
                client->x[target] = op->_local_xz_level.x;
                client->z[target] = op->_local_xz_level.z;
                client->level[target] = op->_local_xz_level.level;
                client->speed[target] = op->_local_xz_level.move_speed;
                client->jump[target] = op->_local_xz_level.jump;
            }
            else if( op->kind == PKT_PLAYER_INFO_OP_APPEARANCE )
            {
                if( target >= 0 ) client->appearances[target]++;
                free(op->_appearance.appearance);
            }
            else if( op->kind == PKT_PLAYER_INFO_OP_SAY ) free(op->_say.text);
            else if( op->kind == PKT_PLAYER_INFO_OP_CHAT ) free(op->_chat.data);
        }
    }
    SELFTEST_CHECK(packets == 1, "one consecutive PLAYER_INFO reaches selected observer %d, got %d", viewer->pid, packets);
}

static void
sailing_client_seed(struct ToriRSServer* srv, struct ToriRSServerPlayer* viewer,
                    struct SailingClientProbe* client)
{
    uint8_t init[8192];
    struct RSAreaBuf buf;
    static struct ToriRSServerCapture capture;
    int index = ToriRSServer_WirePlayerIndex(viewer->pid);
    int32_t coord = (viewer->level << 28) | (viewer->x << 14) | viewer->z;
    memset(client, 0, sizeof(*client));
    client->local_index = index;
    client->present[index] = 1;
    client->x[index] = viewer->x;
    client->z[index] = viewer->z;
    client->level[index] = viewer->level;
    /* The init block has no observer-dependent fields beyond this coordinate.
     * Seed exactly as REBUILD_LOGIN does, then use only real packet captures. */
    rsab_wrap(&buf, init, sizeof(init));
    mock239_playerinfo_write_init(&buf, index, coord);
    osrs239_playerinfo_set_local(index);
    osrs239_playerinfo_init(init, (int)rsab_len(&buf));
    mock239_playerinfo_state_init(&viewer->v5_gpi, index, coord);
    memset(viewer->v5_player_generation, 0, sizeof(viewer->v5_player_generation));
    viewer->v5_playerinfo_sent = 0;
    viewer->v5_last_x = viewer->x;
    viewer->v5_last_z = viewer->z;
    viewer->v5_last_level = viewer->level;
    int saved_masks = viewer->masks;
    viewer->masks |= TORIRSSERVER_PMASK_APPEARANCE;
    ToriRSServer_CaptureBegin(srv, &capture);
    ToriRSServer_SendPlayerInfo(viewer);
    ToriRSServer_CaptureEnd(srv);
    sailing_client_decode(&capture, viewer, client);
    viewer->masks = saved_masks;
}

static void
sailing_client_tick(struct ToriRSServer* srv, struct ToriRSServerPlayer* viewer,
                    struct SailingClientProbe* client)
{
    static struct ToriRSServerCapture capture;
    ToriRSServer_CaptureBegin(srv, &capture);
    selftest_tick(srv);
    ToriRSServer_CaptureEnd(srv);
    sailing_client_decode(&capture, viewer, client);
}

/* Decode only packets inside one actual observer/view sandwich using the
 * suite's independent rev239 zone decoder. */
static int
sailing_view_zone_count(const struct ToriRSServerCapture* capture, int pid, int view,
                        enum GameProtoPktName name, int id)
{
    static struct ToriRSServerCapture selected;
    int active = 0;
    ToriRSServer_CaptureReset(&selected);
    for( int i = 0; i < capture->count; ++i )
    {
        const struct ToriRSServerCapturedPacket* packet = &capture->packets[i];
        if( packet->recipient_pid != pid ) continue;
        if( packet->name == PKT_NAME_SET_ACTIVE_WORLD && packet->len == 3 )
            active = (packet->data[0] << 8) | packet->data[1];
        else if( active == view && selected.count < TORIRSSERVER_CAPTURE_MAX )
            selected.packets[selected.count++] = *packet;
    }
    return selftest_rev239_zone_scan(&selected, name, id, NULL, NULL, NULL);
}

static void
sailing_decode_moving_npc(struct ToriRSServer* srv, struct ToriRSServerPlayer* viewer,
                          int npc_slot, int old_x, int old_z)
{
    static struct ToriRSServerCapture capture;
    int old[TORIRSSERVER_TRACKED_NPC_MAX];
    int old_count = viewer->tracked_count;
    memcpy(old, viewer->tracked, sizeof(old));
    ToriRSServer_CaptureBegin(srv, &capture);
    ToriRSServer_SendNpcInfo(viewer);
    ToriRSServer_CaptureEnd(srv);
    int at = ToriRSServer_CaptureFindNamed(&capture, PKT_NAME_NPC_INFO, 0);
    SELFTEST_CHECK(at >= 0 && !capture.overflow, "moving deck NPC produces an actual NPC_INFO for observer %d", viewer->pid);
    if( at < 0 ) return;
    struct PktNpcInfoOp ops[2048];
    const struct ToriRSServerCapturedPacket* packet = &capture.packets[at];
    int count = osrs239_npc_info_read(packet->data, packet->len, ops, 2048);
    int target = -1, present = 0, x = old_x, z = old_z;
    static const int dx[8] = {-1,0,1,-1,1,-1,0,1};
    static const int dz[8] = {-1,-1,-1,0,0,1,1,1};
    SELFTEST_CHECK(!packet->truncated && count > 0 && count < 2048,
                   "actual client decodes the complete NPC movement packet");
    for( int i = 0; i < count; ++i )
    {
        struct PktNpcInfoOp* op = &ops[i];
        if( op->kind == PKT_NPC_INFO_OP_ADD_NPC_NEW_OPBITS_PID )
            target = ToriRSServer_SlotMapWorld(viewer, (int)op->_bitvalue);
        else if( op->kind == PKT_NPC_INFO_OP_ADD_NPC_OLD_OPBITS_IDX )
            target = op->_bitvalue < (unsigned)old_count ? old[op->_bitvalue] : -1;
        else if( op->kind == PKT_NPC_INFO_OP_SET_NPC_OPBITS_IDX ) target = -1;
        if( target == npc_slot )
        {
            present = 1;
            if( op->kind == PKT_NPC_INFO_OP_DELTA_XZ )
            { x = viewer->obs_x + op->_delta_xz.dx; z = viewer->obs_z + op->_delta_xz.dz; }
            else if( op->kind == PKT_NPC_INFO_OPBITS_WALKDIR || op->kind == PKT_NPC_INFO_OPBITS_RUNDIR )
            { x += dx[op->_bitvalue & 7]; z += dz[op->_bitvalue & 7]; }
        }
        if( op->kind == PKT_NPC_INFO_OP_SAY ) free(op->_say.text);
        else if( op->kind == PKT_NPC_INFO_OP_NAME_CHANGE ) free(op->_name_change.name);
    }
    SELFTEST_CHECK(present && viewer->npc_tracked[npc_slot] &&
                   x == srv->npcs[npc_slot].obs_x && z == srv->npcs[npc_slot].obs_z,
                   "observer %d decodes the NPC's step plus hull motion at %d,%d (expected %d,%d)",
                   viewer->pid, x, z, srv->npcs[npc_slot].obs_x, srv->npcs[npc_slot].obs_z);
}

/*
 * The crew NPC shell's real transform table, read out of the boot cache.
 *
 * Every crew member drawn on a deck is a `multivarbit` shell whose rungs are
 * indexed BY THE ROLE VARBIT'S VALUE (this client's
 * `VarPManager_ResolveTransform`, and the reference's class393: `configs[v]`
 * while `v < count - 1`, else the last entry). So the value the server
 * publishes for a role is only correct if the cache has a real npc at that
 * index -- which is a fact of the cache, not of this server, and is read from
 * the cache the run was pointed at rather than restated here.
 *
 * Returns the rung count, 0 when the cache has no such record.
 */
static int
sailing_crew_shell_transforms(int npc_id, int* out, int cap, int* out_varbit)
{
    struct RSCache profile = RSCache_ProfileZero();
    struct RSCache_Dat2Disk* disk;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    const char* cache_env = getenv("TORIRSSERVER_CACHE");
    const char* cache_dir = cache_env && cache_env[0] ? cache_env : TORIRSSERVER_CACHE_DIR_DEFAULT;
    int table;
    int count = 0;

    assert(out);
    assert(out_varbit);
    assert(cap > 0);
    if( npc_id < 0 )
        return 0;

    profile.game = RSCACHE_GAME_OLDSCHOOL;
    profile.epoch = RSCACHE_EPOCH_DAT2;
    profile.revision = TORIRSSERVER_CACHE_REVISION;

    *out_varbit = -1;
    disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        char fallback[512];

        snprintf(fallback, sizeof(fallback), "../%s", cache_dir);
        disk = RSCache_Dat2DiskNewFromDirectory(fallback);
    }
    if( !disk )
        return 0;
    RSCache_Dat2DiskSetProfile(disk, &profile);
    table = RSCache_Dat2DiskTableId(disk, RSCACHE_DAT2_TABLE_CONFIGS);
    archive = RSCache_Dat2DiskArchiveNewLoad(disk, table, RSCACHE_DAT2_CONFIG_KIND_NPC);
    if( !archive )
    {
        RSCache_Dat2DiskFree(disk);
        return 0;
    }
    RSCache_Dat2DiskArchiveInitMetadata(disk, archive);
    RSCache_ProfileSetGroupRevision(&profile, RSCACHE_TYPE_NPC, archive->revision);
    files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( files )
    {
        for( int i = 0; i < archive->file_count && !count; i++ )
        {
            struct RSCache_Dat2ConfigNpc* npc;

            if( archive->file_ids[i] != npc_id )
                continue;
            npc = RSCache_Dat2ConfigNpcNewDecodeProfile(
                &profile, files->files[i], files->file_sizes[i]);
            if( !npc )
                continue;
            *out_varbit = npc->varbit_id;
            for( int rung = 0; rung < npc->configs_count && rung < cap; rung++ )
                out[count++] = npc->configs[rung];
            RSCache_Dat2ConfigNpcFree(npc);
        }
        RSCache_FileListFree(files);
    }
    RSCache_Dat2DiskArchiveFree(archive);
    RSCache_Dat2DiskFree(disk);
    return count;
}

/* The resolver both clients use, restated so the assertion below reads the
 * table exactly as a viewer's client would. */
static int
sailing_crew_shell_rung(const int* transforms, int count, int role)
{
    assert(transforms);
    if( count <= 0 )
        return -1;
    if( role >= 0 && role < count - 1 )
        return transforms[role];
    return transforms[count - 1];
}

static void
selftest_sailing_multiplayer(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    if( srv->wire->revision < 239 ) return;
    fprintf(stderr, "ToriRSServer selftest: production sailing multiplayer/client decoder\n");
    int saved_x = player->x, saved_z = player->z, saved_level = player->level;
    int saved_rng = srv->rng;
    ToriRSServer_WorldSetActive(srv, player);
    ToriRSServer_WorldTeleport(srv, 0, 3080, 3160);
    int handle = ToriRSServer_VesselSpawn(srv, 9, 6, 12, 0, 3072, 3160, 0);
    struct ToriRSServerVessel* boat = ToriRSServer_VesselGet(srv, handle);
    SELFTEST_CHECK(boat != NULL, "multiplayer test has an actual ocean hull");
    if( !boat ) return;
    int bx, bz;
    ToriRSServer_MapInstanceBase(boat->instance, &bx, &bz);
    for( int zz = 0; zz < 2; ++zz )
        ToriRSServer_MapInstanceSetchunk(boat->instance, 0, 0, zz, 3216, 3216, 0, 0);
    ToriRSServer_MapInstanceBuild(boat->instance);
    ToriRSServer_WorldMapInstanceBuilt(srv, boat->instance);
    struct ToriRSServerPlayer* rider = ToriRSServer_WorldAddPlayer(srv, NULL);
    struct ToriRSServerPlayer* guest = ToriRSServer_WorldAddPlayer(srv, NULL);
    SELFTEST_CHECK(rider && guest, "a runner and same-deck observer join alongside the shore observer");
    if( !rider || !guest ) goto done;
    ToriRSServer_WorldPlayerInit(rider);
    ToriRSServer_WorldPlayerInit(guest);
    int rx = -1, rz = -1;
    ToriRSServer_WorldSetActive(srv, rider);
    ToriRSServer_WorldTeleport(srv, 0, bx + 3, bz + 6);
    /* Find a real, unblocked three-tile eastward corridor. No terrain stamps. */
    for( int x = bx + 1; x < bx + 5 && rx < 0; ++x )
        for( int z = bz + 3; z < bz + 11 && rx < 0; ++z )
            if( !ToriRSServer_SceneWalkBlocked(0, x, z) &&
                ToriRSServer_SceneCanStep(0, x, z, 4) &&
                ToriRSServer_SceneCanStep(0, x + 1, z, 4) ) { rx = x; rz = z; }
    SELFTEST_CHECK(rx >= 0, "the native source map provides three consecutive walkable deck tiles");
    if( rx < 0 ) goto done;
    ToriRSServer_WorldTeleport(srv, 0, rx, rz);
    ToriRSServer_WorldSetActive(srv, guest);
    ToriRSServer_WorldTeleport(srv, 0, rx, rz + 1);
    ToriRSServer_WorldSetActive(srv, player);
    selftest_tick(srv);
    /* A second hull is viewed from shore and from a DIFFERENT vessel. Its
     * dynamic facility exists before first publication, then animates later. */
    {
        static struct ToriRSServerCapture capture;
        int other_handle = ToriRSServer_VesselSpawn(srv, 2, 2, 5, 0, 3077, 3160, 0);
        struct ToriRSServerVessel* other = ToriRSServer_VesselGet(srv, other_handle);
        int mast = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_LOC,
                     "sailing_boat_sail_kandarin_2x5_wood");
        SELFTEST_CHECK(other && mast >= 0, "second hull and actual native mast resolve");
        if( other && mast >= 0 )
        {
            int ox, oz;
            ToriRSServer_MapInstanceBase(other->instance, &ox, &oz);
            ToriRSServer_MapInstanceSetchunk(other->instance, 0, 0, 0, 3216, 3216, 0, 0);
            ToriRSServer_MapInstanceBuild(other->instance);
            ToriRSServer_WorldMapInstanceBuilt(srv, other->instance);
            struct ToriRSServerLocOps ops;
            ToriRSServer_LocOpsDefault(&ops);
            ToriRSServer_ZoneLocChanged(srv, ox + 4, oz + 4, 0, 10, mast, 0, -1, 0, 0, &ops);
            ToriRSServer_CaptureBegin(srv, &capture);
            selftest_tick(srv);
            ToriRSServer_CaptureEnd(srv);
            SELFTEST_CHECK(!capture.overflow && sailing_view_zone_count(&capture, player->pid,
                           other->view_id, PKT_NAME_LOC_ADD_CHANGE, mast) == 1,
                           "shore observer receives the other hull's initial native facility snapshot");
            SELFTEST_CHECK(sailing_view_zone_count(&capture, guest->pid, other->view_id,
                           PKT_NAME_LOC_ADD_CHANGE, mast) == 1,
                           "passenger receives facilities on a different vessel's published view");
            SELFTEST_CHECK(sailing_view_zone_count(&capture, player->pid, 0,
                           PKT_NAME_LOC_ADD_CHANGE, mast) == 0,
                           "deck facility snapshot never mutates the root world");
            ToriRSServer_ZoneLocAnim(srv, ox + 4, oz + 4, 0, 10, 0, 808);
            ToriRSServer_CaptureBegin(srv, &capture);
            selftest_tick(srv);
            ToriRSServer_CaptureEnd(srv);
            SELFTEST_CHECK(sailing_view_zone_count(&capture, player->pid, other->view_id,
                           PKT_NAME_LOC_ANIM, 808) == 1 &&
                           sailing_view_zone_count(&capture, guest->pid, other->view_id,
                           PKT_NAME_LOC_ANIM, 808) == 1,
                           "later facility animations reach shore and another hull's passenger");
            ToriRSServer_CaptureBegin(srv, &capture);
            selftest_tick(srv);
            ToriRSServer_CaptureEnd(srv);
            SELFTEST_CHECK(sailing_view_zone_count(&capture, player->pid, other->view_id,
                           PKT_NAME_LOC_ADD_CHANGE, mast) == 0 &&
                           sailing_view_zone_count(&capture, player->pid, other->view_id,
                           PKT_NAME_LOC_ANIM, 808) == 0,
                           "quiet published decks do not replay their state or expired animations");
            ToriRSServer_VesselFree(srv, other_handle);
            selftest_tick(srv);
        }
    }
    struct ToriRSServerPlayer* viewers[] = {player, guest, rider};
    int rider_index = ToriRSServer_WirePlayerIndex(rider->pid);
    for( int pass = 0; pass < 3; ++pass )
    {
        struct ToriRSServerPlayer* viewer = viewers[pass];
        struct SailingClientProbe client;
        ToriRSServer_VesselStop(boat);
        boat->fine_x = 3072 * 128 + 64;
        boat->fine_z = 3160 * 128 + 64;
        ToriRSServer_WorldSetActive(srv, rider);
        ToriRSServer_WorldTeleport(srv, 0, rx, rz);
        ToriRSServer_WorldSetActive(srv, player);
        selftest_tick(srv);
        sailing_client_seed(srv, viewer, &client);
        SELFTEST_CHECK(client.present[rider_index] && client.appearances[rider_index] == 1,
                       "observer %d receives the rider and a native appearance", pass);
        ToriRSServer_WorldSetActive(srv, rider);
        ToriRSServer_WorldSetVarp(srv, ToriRSServer_WorldVarp("option_run"), 1);
        rider->run_energy = TORIRSSERVER_RUN_ENERGY_MAX;
        ToriRSServer_WorldWalkTo(srv, rx + 2, rz);
        ToriRSServer_VesselSetHeading(boat, 0);
        ToriRSServer_VesselSetSpeed(boat, 1);
        boat->sails_set = 1;
        int fz = boat->fine_z;
        ToriRSServer_WorldSetActive(srv, player);
        sailing_client_tick(srv, viewer, &client);
        SELFTEST_CHECK(rider->x == rx + 2 && rider->z == rz,
                       "RUN takes exactly two deck tiles while the hull moves (observer %d): %d,%d", pass, rider->x, rider->z);
        SELFTEST_CHECK(boat->fine_z != fz, "the hull actually translates during RUN (observer %d)", pass);
        int want_x = rider->x;
        int want_z = rider->z;
        SELFTEST_CHECK(client.x[rider_index] == want_x && client.z[rider_index] == want_z,
                       "actual decoder preserves authoritative deck coordinates for observer %d: %d,%d expected %d,%d", pass,
                       client.x[rider_index], client.z[rider_index], want_x, want_z);
        SELFTEST_CHECK(client.speed[rider_index] == PKT_PLAYER_TRAVERSAL_RUN && !client.jump[rider_index],
                       "the real packet preserves RUN traversal without a snap for observer %d", pass);
        SELFTEST_CHECK(client.removals[rider_index] == 0 && client.appearances[rider_index] == 1,
                       "moving aboard keeps the same remote player without remove/re-add (observer %d)", pass);
        ToriRSServer_VesselStop(boat);
    }
    {
        int chicken = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC, "chicken");
        int hand_slot = chicken >= 0 ? ToriRSServer_WorldNpcSpawn(srv, chicken, rx, rz, 0) : -1;
        SELFTEST_CHECK(hand_slot >= 0, "real movable deck NPC spawns for shore and same-deck observer checks");
        if( hand_slot >= 0 )
        {
            struct ToriRSServerNpc* hand = &srv->npcs[hand_slot];
            hand->mode = TORIRSSERVER_NPCMODE_NONE;
            selftest_tick(srv);
            int old_x = hand->obs_x, old_z = hand->obs_z;
            int hull_z = boat->fine_z;
            ToriRSServer_WorldSetActive(srv, rider);
            SELFTEST_CHECK(ToriRSServer_WorldNpcWalkTo(hand, rx + 3, rz),
                           "native NPC movement takes a legal step on the retained deck");
            ToriRSServer_VesselSetHeading(boat, 0);
            ToriRSServer_VesselSetSpeed(boat, 1);
            boat->sails_set = 1;
            ToriRSServer_VesselTickAll(srv);
            ToriRSServer_WorldRefreshObservation(srv);
            SELFTEST_CHECK(hand->x == rx + 1 && hand->z == rz && boat->fine_z != hull_z,
                           "NPC walks one deck tile while its hull translates: %d,%d -> %d,%d; hullZ %d -> %d",
                           rx, rz, hand->x, hand->z, hull_z, boat->fine_z);
            ToriRSServer_WorldSetActive(srv, player);
            sailing_decode_moving_npc(srv, player, hand_slot, old_x, old_z);
            sailing_decode_moving_npc(srv, guest, hand_slot, old_x, old_z);
            ToriRSServer_VesselStop(boat);
            ToriRSServer_WorldNpcFree(srv, hand_slot);
            selftest_tick(srv);
        }
    }
    {
        struct SailingClientProbe client;
        sailing_client_seed(srv,player,&client);
        int deck_x=rider->x, deck_z=rider->z;
        int before_x=client.x[rider_index], before_z=client.z[rider_index];
        int old_obs_z=rider->obs_z;
        ToriRSServer_VesselSetHeading(boat,0);
        ToriRSServer_VesselSetSpeed(boat,1);
        boat->sails_set=1;
        sailing_client_tick(srv,player,&client);
        sailing_client_tick(srv,player,&client);
        SELFTEST_CHECK(rider->x==deck_x && rider->z==deck_z && rider->obs_z!=old_obs_z,
                       "standing passenger stays on its deck tile while root visibility travels with the hull");
        SELFTEST_CHECK(client.x[rider_index]==before_x && client.z[rider_index]==before_z &&
                       client.removals[rider_index]==0 && client.appearances[rider_index]==1,
                       "a standing passenger gets no fake PLAYER_INFO walk, respawn or placement as the hull moves");
        ToriRSServer_VesselStop(boat);
    }
    /* Moving with the hull can compose to THREE root tiles in one tick,
     * exceeding the run opcode's geometry while still being ordinary RUN. */
    {
        int start_x = -1, start_z = -1;
        ToriRSServer_WorldSetActive(srv, rider);
        for( int x = bx + 1; x < bx + 7 && start_x < 0; ++x )
            for( int z = bz + 5; z < bz + 13 && start_x < 0; ++z )
                if( !ToriRSServer_SceneWalkBlocked(0,x,z) &&
                    ToriRSServer_SceneCanStep(0,x,z,6) &&
                    ToriRSServer_SceneCanStep(0,x,z-1,6) ) { start_x=x; start_z=z; }
        SELFTEST_CHECK(start_x >= 0, "the retained deck provides a longitudinal RUN corridor");
        if( start_x >= 0 )
        {
            ToriRSServer_WorldTeleport(srv,0,start_x,start_z);
            boat->fine_z = 3160 * 128 + 64;
            ToriRSServer_WorldSetActive(srv,player);
            selftest_tick(srv);
            struct SailingClientProbe client;
            sailing_client_seed(srv,player,&client);
            int before_z = rider->obs_z;
            ToriRSServer_WorldSetActive(srv,rider);
            ToriRSServer_WorldWalkTo(srv,start_x,start_z-2);
            ToriRSServer_VesselSetHeading(boat,0);
            ToriRSServer_VesselSetSpeed(boat,2);
            boat->sails_set=1;
            ToriRSServer_WorldSetActive(srv,player);
            sailing_client_tick(srv,player,&client);
            SELFTEST_CHECK(rider->x==start_x && rider->z==start_z-2 && rider->obs_z==before_z-3,
                           "two deck RUN tiles plus hull movement compose to three root tiles");
            SELFTEST_CHECK(client.z[rider_index]==rider->z && client.level[rider_index]==rider->level &&
                           client.speed[rider_index]==PKT_PLAYER_TRAVERSAL_RUN && !client.jump[rider_index],
                           "root visibility moves three tiles while high-resolution staging retains a two-tile RUN");
            ToriRSServer_VesselStop(boat);
        }
        ToriRSServer_WorldSetActive(srv,player);
    }
    /* Permission policy is independent of the observer's projected position.
     * A passenger does not acquire helm/cargo rights just by sharing a deck. */
    {
        int captain_x=player->x, captain_z=player->z, captain_level=player->level;
        int old_owner=boat->owner_uid;
        int privacy_bit=ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_VARBIT,"settings_cargo_hold_privacy");
        int privacy=privacy_bit>=0 ? ToriRSServer_VarbitGet(player,privacy_bit) : 0;
        boat->owner_uid=player->pid+1;
        ToriRSServer_WorldSetActive(srv,player);
        ToriRSServer_WorldTeleport(srv,0,rx+2,rz+1);
        SELFTEST_CHECK(privacy_bit>=0, "native cargo privacy setting resolves");
        if( privacy_bit>=0 ) ToriRSServer_VarbitSet(srv,privacy_bit,0);
        SELFTEST_CHECK(ToriRSServer_VesselCanNavigate(srv,player,boat) &&
                       !ToriRSServer_VesselCanNavigate(srv,guest,boat) &&
                       !ToriRSServer_VesselCargoAllowed(srv,guest,boat),
                       "owner starts with helm/cargo access while an ungranted passenger has neither");
        uint32_t grant=1u<<guest->pid;
        SELFTEST_CHECK(!ToriRSServer_VesselSetNavigators(srv,rider,boat,1u<<rider->pid),
                       "another passenger cannot promote themselves to navigator");
        SELFTEST_CHECK(ToriRSServer_VesselSetNavigators(srv,player,boat,grant) &&
                       ToriRSServer_VesselCanNavigate(srv,guest,boat) &&
                       ToriRSServer_VesselCargoAllowed(srv,guest,boat),
                       "captain grants the selected guest navigation and default navigator-only cargo");
        /*
         * The role each of the three publishes (varbit 19233), and the crew
         * NPCs that value has to draw.
         *
         * Native values are 10 captain, 6 navigator, 3 aboard guest, 0 not
         * aboard: cs2 8732 torirs_sailing_facility_row_state tests 10 and 6,
         * the captain-only affordances test 10 alone, and the crew shells'
         * cache transform tables hold a real npc only at 0, 3, 6 and 10. A
         * passenger used to be sent 2, which resolves to the shells' -1 rung
         * -- the guest saw a deck with no crew on it at all.
         */
        {
            const struct ToriRSServerIds* role_ids = ToriRSServer_Ids();
            int role_bit = role_ids->varbit_sailing_player_role;
            SELFTEST_CHECK(role_bit >= 0, "the native sidepanel role varbit resolves");
            selftest_tick(srv);
            int captain_role = role_bit >= 0 ? ToriRSServer_VarbitGet(player, role_bit) : -1;
            int navigator_role = role_bit >= 0 ? ToriRSServer_VarbitGet(guest, role_bit) : -1;
            int passenger_role = role_bit >= 0 ? ToriRSServer_VarbitGet(rider, role_bit) : -1;
            SELFTEST_CHECK(captain_role == 10 && navigator_role == 6 && passenger_role == 3,
                           "captain/navigator/passenger publish native roles 10/6/3, got %d/%d/%d",
                           captain_role, navigator_role, passenger_role);
            int shell = ToriRSServer_ContentSymbol(TORIRSSERVER_PACK_NPC,
                                                   "sailing_crew_generic_1_ship");
            int transforms[64];
            int shell_varbit = -1;
            int rungs = shell >= 0 ?
                sailing_crew_shell_transforms(shell, transforms, 64, &shell_varbit) : 0;
            SELFTEST_CHECK(rungs > 0 && shell_varbit == role_bit,
                           "the crew npc shell (%d) switches on this very varbit in the cache: "
                           "%d rungs on varbit %d", shell, rungs, shell_varbit);
            if( rungs > 0 )
            {
                int roles[3] = {captain_role, navigator_role, passenger_role};
                const char* names[3] = {"captain", "navigator", "passenger"};
                for( int r = 0; r < 3; ++r )
                {
                    int drawn = sailing_crew_shell_rung(transforms, rungs, roles[r]);
                    SELFTEST_CHECK(drawn >= 0 && ToriRSServer_NpcInfoKnown(drawn),
                                   "a %s (role %d) resolves the crew shell to a real npc, got %d",
                                   names[r], roles[r], drawn);
                }
                SELFTEST_CHECK(sailing_crew_shell_rung(transforms, rungs, 2) < 0,
                               "and the old published role 2 is exactly the hidden rung "
                               "that left a passenger's deck empty");
            }
        }
        SELFTEST_CHECK(!ToriRSServer_VesselSetNavigators(srv,player,boat,1u<<TORIRSSERVER_PLAYER_MAX) &&
                       ToriRSServer_VesselNavigatorMask(srv,boat)==grant,
                       "invalid navigator masks are rejected without changing the existing grant");
        if( privacy_bit>=0 ) ToriRSServer_VarbitSet(srv,privacy_bit,1);
        SELFTEST_CHECK(ToriRSServer_VesselCargoAllowed(srv,rider,boat),
                       "All players cargo privacy admits the ordinary passenger");
        if( privacy_bit>=0 ) ToriRSServer_VarbitSet(srv,privacy_bit,2);
        SELFTEST_CHECK(!ToriRSServer_VesselCargoAllowed(srv,guest,boat) &&
                       ToriRSServer_VesselCargoAllowed(srv,player,boat),
                       "No players cargo privacy excludes the navigator but retains the owner");
        guest->navigating_vessel=boat->index; guest->navigating_vessel_serial=boat->serial;
        SELFTEST_CHECK(ToriRSServer_VesselSetNavigators(srv,player,boat,0) &&
                       !guest->navigating_vessel && !guest->navigating_vessel_serial,
                       "revoking navigation immediately releases a guest's held helm");
        SELFTEST_CHECK(ToriRSServer_VesselSetNavigators(srv,player,boat,grant), "captain can grant again before departure");
        ToriRSServer_WorldSetActive(srv,guest);
        ToriRSServer_WorldTeleport(srv,0,3079,3161);
        SELFTEST_CHECK(!ToriRSServer_VesselCanNavigate(srv,guest,boat), "leaving the hull removes navigation rights");
        ToriRSServer_VesselBoardPlayer(srv,guest,boat);
        SELFTEST_CHECK(!ToriRSServer_VesselCanNavigate(srv,guest,boat), "boarding again cannot revive an old navigator grant");
        ToriRSServer_WorldSetActive(srv,player);
        SELFTEST_CHECK(ToriRSServer_VesselSetNavigators(srv,player,boat,grant), "regrant before player-slot reuse test");
        int old_pid=guest->pid; uint32_t old_generation=guest->login_generation;
        ToriRSServer_WorldSetDisplayName(guest,"SailLeaseTest");
        ToriRSServer_WorldRemovePlayer(srv,guest);
        ToriRSServer_WorldPlayerReap(srv);
        SELFTEST_CHECK(!boat->navigator_generation[old_pid], "logout clears the actual navigator lease");
        guest=ToriRSServer_WorldAddPlayer(srv,NULL);
        SELFTEST_CHECK(guest && guest->pid==old_pid, "a new guest reuses the released player slot");
        if( guest )
        {
            ToriRSServer_WorldPlayerInit(guest);
            ToriRSServer_VesselBoardPlayer(srv,guest,boat);
            boat->navigator_generation[guest->pid]=old_generation;
            SELFTEST_CHECK(guest->login_generation!=old_generation &&
                           !ToriRSServer_VesselCanNavigate(srv,guest,boat),
                           "even a stale saved grant cannot authorize a different login in the same pid");
            boat->navigator_generation[guest->pid]=0;
        }
        ToriRSServer_WorldSetActive(srv,player);
        if( privacy_bit>=0 ) ToriRSServer_VarbitSet(srv,privacy_bit,privacy);
        boat->owner_uid=old_owner;
        ToriRSServer_WorldTeleport(srv,captain_level,captain_x,captain_z);
        selftest_tick(srv);
    }
    /* Shore replay continues through transition, disappearance and re-entry. */
    {
        struct SailingClientProbe client;
        sailing_client_seed(srv, player, &client);
        ToriRSServer_WorldSetActive(srv, rider);
        ToriRSServer_WorldTeleport(srv, 0, 3079, 3161);
        ToriRSServer_WorldSetActive(srv, player);
        sailing_client_tick(srv, player, &client);
        SELFTEST_CHECK(client.present[rider_index] && client.x[rider_index] == 3079 &&
                       client.z[rider_index] == 3161 && client.jump[rider_index],
                       "shore observer decodes the disembark placement without stale deck offsets");
        ToriRSServer_WorldSetActive(srv, rider);
        ToriRSServer_WorldTeleport(srv, 2, 3222, 3218);
        ToriRSServer_WorldSetActive(srv, player);
        sailing_client_tick(srv, player, &client);
        SELFTEST_CHECK(!client.present[rider_index] && client.removals[rider_index] == 1,
                       "different-plane distant rider leaves the actual client's high-resolution table");
        ToriRSServer_WorldSetActive(srv, rider);
        ToriRSServer_WorldTeleport(srv, 0, rx, rz);
        ToriRSServer_WorldSetActive(srv, player);
        sailing_client_tick(srv, player, &client);
        SELFTEST_CHECK(client.present[rider_index] && client.appearances[rider_index] == 2 &&
                       client.x[rider_index] == rider->x && client.z[rider_index] == rider->z &&
                       client.level[rider_index] == rider->level,
                       "coarse-region re-entry promotes the same rider into its real deck space and appearance");
    }
    {
        const struct ToriRSServerWire* wire = srv->wire;
        int tracked = player->wev_tracked_count;
        static struct ToriRSServerCapture capture;
        srv->wire = ToriRSServer_WireByName("osrs230");
        SELFTEST_CHECK(srv->wire && srv->wire->revision == 230, "resolve the actual revision230 adapter");
        ToriRSServer_CaptureBegin(srv, &capture);
        ToriRSServer_SendWorldEntityInfo(player);
        ToriRSServer_SendSetActiveWorldId(player, boat->view_id, 0);
        ToriRSServer_SendRebuildWorldEntity(player, boat);
        ToriRSServer_CaptureEnd(srv);
        SELFTEST_CHECK(capture.count == 0 && player->wev_tracked_count == tracked,
                       "revision230 refuses vessel packets and does not mutate v239 observer tracking");
        srv->wire = wire;
    }
done:
    ToriRSServer_WorldSetActive(srv, player);
    if( rider ) ToriRSServer_WorldPlayerFree(srv, rider->pid);
    if( guest ) ToriRSServer_WorldPlayerFree(srv, guest->pid);
    ToriRSServer_WorldPlayerReap(srv);
    ToriRSServer_VesselFree(srv, handle);
    ToriRSServer_WorldTeleport(srv, saved_level, saved_x, saved_z);
    srv->rng = saved_rng;
}

/* Exhaust every actual published identity and keep non-boat instances usable. */
static void
selftest_sailing_capacity(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{
    int handles[TORIRSSERVER_WEV_VIEW_MAX] = {0};
    int other_instances[TORIRSSERVER_PLAYER_MAX] = {0};
    int before = ToriRSServer_MapInstanceLiveCount();
    unsigned views = 0, windows = 0;
    SELFTEST_CHECK(ToriRSServer_VesselLiveCount(srv) == 0, "capacity fixture starts with no live hulls");
    for( int i = 0; i < TORIRSSERVER_WEV_VIEW_MAX; ++i )
    {
        handles[i] = ToriRSServer_VesselSpawn(srv, 1, 1, 3, 0, 3072, 3160, 0);
        struct ToriRSServerVessel* vessel = ToriRSServer_VesselGet(srv, handles[i]);
        SELFTEST_CHECK(vessel && vessel->view_id > 0 && vessel->deck_window > 0,
                       "live hull %d reserves a visible view and an independent deck window", i + 1);
        if( vessel )
        {
            unsigned view = 1u << vessel->view_id;
            unsigned window = 1u << (vessel->deck_window - TORIRSSERVER_SCENE_VESSEL_WINDOW_BASE);
            SELFTEST_CHECK(!(views & view) && !(windows & window), "hull %d owns unique view/window resources", i + 1);
            views |= view; windows |= window;
        }
    }
    SELFTEST_CHECK(ToriRSServer_VesselLiveCount(srv) == 15 && ToriRSServer_MapInstanceLiveCount() == before + 15,
                   "fifteen real vessel instances coexist within the sixteen-view client bound");
    for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; ++i )
    {
        other_instances[i] = ToriRSServer_MapInstanceAlloc(ToriRSServer_WorldCacheDir(), 1, 1);
        SELFTEST_CHECK(other_instances[i] > 0, "player activity instance %d remains available alongside fifteen boats", i + 1);
    }
    int allocated = ToriRSServer_MapInstanceLiveCount();
    SELFTEST_CHECK(ToriRSServer_VesselSpawn(srv, 1, 1, 3, 0, 3072, 3160, 0) == 0,
                   "sixteenth hull is rejected cleanly before acquiring any deck resource");
    SELFTEST_CHECK(ToriRSServer_VesselLiveCount(srv) == 15 && ToriRSServer_MapInstanceLiveCount() == allocated,
                   "refused spawn leaks neither a hull nor a private map instance");
    if( srv->wire->revision >= 239 )
    {
        static struct ToriRSServerCapture capture;
        struct selftest_wev_packet decoded;
        player->wev_tracked_count = 0;
        ToriRSServer_CaptureBegin(srv, &capture);
        ToriRSServer_SendWorldEntityInfo(player);
        ToriRSServer_CaptureEnd(srv);
        int at = ToriRSServer_CaptureFindNamed(&capture, PKT_NAME_WORLDENTITY_INFO, 0);
        SELFTEST_CHECK(at >= 0 && !capture.overflow && !capture.packets[at].truncated,
                       "all fifteen world-entity spawns fit a complete actual packet");
        if( at >= 0 )
        {
            selftest_wev_decode(capture.packets[at].data, capture.packets[at].len, &decoded);
            SELFTEST_CHECK(decoded.spawn_count == 15 && decoded.trailing == 0 && player->wev_tracked_count == 15,
                           "the packet publishes all fifteen actual views, with no invisible hull");
        }
    }
    if( handles[7] )
    {
        int view = ToriRSServer_VesselGet(srv, handles[7])->view_id;
        int serial = ToriRSServer_VesselGet(srv, handles[7])->serial;
        ToriRSServer_VesselFree(srv, handles[7]);
        handles[7] = ToriRSServer_VesselSpawn(srv, 1, 1, 3, 0, 3072, 3160, 0);
        struct ToriRSServerVessel* replacement = ToriRSServer_VesselGet(srv, handles[7]);
        SELFTEST_CHECK(replacement && replacement->view_id == view && replacement->serial != serial && replacement->deck_window > 0,
                       "freeing one hull permits a visible replacement with a new serial");
    }
    for( int i = 0; i < TORIRSSERVER_WEV_VIEW_MAX; ++i )
        if( handles[i] ) ToriRSServer_VesselFree(srv, handles[i]);
    for( int i = 0; i < TORIRSSERVER_PLAYER_MAX; ++i )
        if( other_instances[i] ) ToriRSServer_WorldMapInstanceFree(srv, other_instances[i]);
    SELFTEST_CHECK(ToriRSServer_VesselLiveCount(srv) == 0 && ToriRSServer_MapInstanceLiveCount() == before,
                   "all boat and activity resources are returned after capacity testing");
    selftest_tick(srv);
}
