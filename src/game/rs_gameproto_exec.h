#ifndef SRC_GAME_RS_GAMEPROTO_EXEC_H
#define SRC_GAME_RS_GAMEPROTO_EXEC_H

/*
 * Server-packet execution against src App state -- the seam ToriRS_Network's
 * parsed packets flow into. Context-struct pattern like RS_MinimenuBuildCtx so
 * it is testable without a full App.
 *
 * Two halves per packet. The MODEL half is RS_ServerState_Apply
 * (game/rs_server_state.h): varps, stats, inventories, the interface stores,
 * chat, social, the session records and the per-player entity setters, written
 * whether or not anything can be shown. RS_GameProto_Exec runs it first and
 * then the PRESENTATION that packet has here: tree pokes, CS2 transmits,
 * redraws, queued asset tasks, client scripts. The families whose record still
 * lives in the scene or the tree (zones, locs, ground items, projectiles,
 * interface mounts, world entities) are applied here whole.
 */

struct RevPacket;
struct App;
struct UITree;
struct RS_ServerState;

struct RS_GameProtoCtx
{
    /** The packet model's records (RS_ServerState_Apply). Required. */
    struct RS_ServerState const* state;
    struct UITree* tree;
    /** Optional: the presentation and the view-held families need the App.
     *  When NULL those are skipped (unit tests pass a NULL app). */
    struct App* app;
};

/** Execute one parsed packet. Never fatal — unmapped opcodes are logged. */
void
RS_GameProto_Exec(
    struct RS_GameProtoCtx const* ctx,
    struct RevPacket* packet);

struct PktZoneSubPacket;
struct TorirsControlFrame;

/** A zone sub-packet held for a view that was not built yet
 *  (App_ZonePacketHold), now that it is and its turn has come: applied at the
 *  zone base, plane and view as of its arrival. A FULL_FOLLOWS reset is one
 *  of them (name UPDATE_ZONE_FULL_FOLLOWS, no payload). */
void
RS_GameProto_ApplyHeldZoneOp(
    struct RS_GameProtoCtx const* ctx,
    struct PktZoneSubPacket const* pkt,
    int base_x,
    int base_z,
    int level,
    int view);

/* A replay seek's world keyframe (replay/replay_world_apply.c), through the
 * arms the zone packets themselves run through. */

/** One zone op, at the zone base (scene-local) and plane it was decoded for,
 *  in the root view; held like a live one while the scene is loading. */
void
RS_GameProto_ReplayZoneOp(
    struct RS_GameProtoCtx const* ctx,
    struct PktZoneSubPacket const* pkt,
    int base_x,
    int base_z,
    int level);

/** Every zone of the loaded root scene back to the map, as
 *  UPDATE_ZONE_FULL_FOLLOWS puts one back: the client's loc changes restored
 *  and its ground items gone. */
void
RS_GameProto_ReplayClearScene(struct App* app);

#endif
