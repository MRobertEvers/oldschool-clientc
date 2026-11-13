#ifndef SRC_GAME_RS_GAMEPROTO_EXEC_H
#define SRC_GAME_RS_GAMEPROTO_EXEC_H

/*
 * Server-packet execution against src App state — the seam ToriRS_Network's
 * parsed packets flow into (analogous to v0 gameproto_exec, rewritten for
 * src's managers). Context-struct pattern like RS_MinimenuBuildCtx so it is
 * testable without a full App.
 *
 * Mappings: VARP to VarPManager, UPDATE_INV to InvManager, UPDATE_STAT /
 * RUNENERGY / RUNWEIGHT to RS_PlayerStats, IF_* mutations to UITree, the
 * IF_OPEN / SETTAB family to RS_UISlots (needs App), MESSAGE_* to RS_Chat.
 */

struct RevPacket;
struct App;
struct UITree;
struct InvManager;
struct VarPManager;
struct RS_PlayerStats;
struct RS_Chat;

struct RS_GameProtoCtx
{
    struct UITree* tree;
    struct InvManager* invs;
    struct VarPManager* varps;
    struct RS_PlayerStats* stats;
    struct RS_Chat* chat;
    /** Optional: interface-open packets and world load need the App. When
     *  NULL those handlers are skipped (unit tests pass a NULL app). */
    struct App* app;
};

/** Execute one parsed packet. Never fatal — unmapped opcodes are logged. */
void
RS_GameProto_Exec(
    struct RS_GameProtoCtx const* ctx,
    struct RevPacket* packet);

/* Replay zone sub-packets that arrived while the world was async-loading
 * (App::pending_zone), in arrival order. No-op until load_complete. Called
 * lazily before any live zone sub-packet, and from the app tick so a queue
 * with no follow-up traffic still drains. */
void
RS_GameProto_FlushPendingZone(struct RS_GameProtoCtx const* ctx);

struct PktZoneSubPacket;
struct TorirsControlFrame;

/** A replay control frame (net/torirs_control.h, TORIRS_CONTROL_REPLAY_*) is
 *  only ever written by the RSProx replay transport (App.net_replay): on any
 *  other connection this says so and aborts. Returns when it is expected. */
void
RS_GameProto_ReplayControlExpected(
    struct App const* app,
    struct TorirsControlFrame const* frame);

/* A replay seek's world keyframe (replay/replay_world_apply.c), through the
 * arms the zone packets themselves run through. */

/** One zone op, at the zone base (scene-local) and plane it was decoded for,
 *  in the root view; queued like a live one while the scene is loading. */
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
