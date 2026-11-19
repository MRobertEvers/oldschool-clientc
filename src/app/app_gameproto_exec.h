#ifndef SRC_APP_APP_GAMEPROTO_EXEC_H
#define SRC_APP_APP_GAMEPROTO_EXEC_H

/*
 * The real client's half of a server packet (app/app_gameproto_exec.c): the
 * model (RS_ServerState_Apply), then what only this client does with it, in
 * that order. A client with no App runs game/rs_gameproto_exec.h instead.
 */

struct App;
struct RevPacket;
struct RS_ServerState;
struct PktZoneSubPacket;

/** Execute one parsed packet, or a control frame. `state` is the App's
 *  records (App_ServerStateBind). Never fatal: unmapped opcodes are logged. */
void
App_GameProto_Exec(
    struct App* app,
    struct RS_ServerState const* state,
    struct RevPacket* packet);

/** A zone sub-packet held for a view that was not built yet
 *  (App_ZonePacketHold), now that it is and its turn has come: applied at the
 *  zone base, plane and view as of its arrival. A FULL_FOLLOWS reset is one
 *  of them (name UPDATE_ZONE_FULL_FOLLOWS, no payload). */
void
App_GameProto_ApplyHeldZoneOp(
    struct App* app,
    struct RS_ServerState const* state,
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
App_GameProto_ReplayZoneOp(
    struct App* app,
    struct RS_ServerState const* state,
    struct PktZoneSubPacket const* pkt,
    int base_x,
    int base_z,
    int level);

/** Every zone of the loaded root scene back to the map, as
 *  UPDATE_ZONE_FULL_FOLLOWS puts one back: the client's loc changes restored
 *  and its ground items gone. */
void
App_GameProto_ReplayClearScene(struct App* app);

#endif
