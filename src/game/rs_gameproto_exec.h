#ifndef SRC_GAME_RS_GAMEPROTO_EXEC_H
#define SRC_GAME_RS_GAMEPROTO_EXEC_H

/*
 * A server packet in a client with no App: the model, and the writes that go
 * to a UI tree alone.
 *
 * The MODEL half of every packet is RS_ServerState_Apply
 * (game/rs_server_state.h): varps, stats, inventories, the interface stores,
 * chat, social, the session records and the per-player entity setters,
 * written whether or not anything can be shown. Both clients run it.
 *
 * What only the real client does with a packet -- CS2 transmits and hooks,
 * redraws, queued mounts, client scripts, audio, the scene, and the parallel
 * tasks for what must not hold the packet pump -- is the App layer's,
 * App_GameProto_Exec (app/app_gameproto_exec.h). This file names no App: it is
 * what the unit tests of game state drive, and the tree-only arms the App
 * calls for the same packets.
 */

struct RevPacket;
struct UITree;
struct RS_ServerState;

struct RS_GameProtoCtx
{
    /** The packet model's records (RS_ServerState_Apply). Required. */
    struct RS_ServerState const* state;
    /** The tree the interface writes go to. Required. */
    struct UITree* tree;
};

/** One parsed packet with no App: RS_ServerState_Apply, then the interface
 *  setters written to `tree` (a text, hide, colour, model or animation goes
 *  onto the node when it exists; the store already holds it) and the
 *  tree-only arms (RS_GameProto_PresentTree). A control frame is the App's
 *  and is never handed here. */
void
RS_GameProto_Exec(
    struct RS_GameProtoCtx const* ctx,
    struct RevPacket* packet);

/** The arms that write only to a UI tree and are the same in both clients:
 *  IF_SETSCROLLPOS, IF_SETPOSITION, IF_SETROTATESPEED, IF_SETANGLE and
 *  IF_CLEARINV. Each relies on the mount task having been queued ahead of it.
 *  Any other packet is left alone. */
void
RS_GameProto_PresentTree(
    struct UITree* tree,
    struct RevPacket const* packet);

#endif
