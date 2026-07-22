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

#endif
