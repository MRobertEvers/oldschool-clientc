#ifndef OSRS_CORE_CLIENTPROT_CORE_H
#define OSRS_CORE_CLIENTPROT_CORE_H

/* Revision-agnostic client → server packet emission facade.
 * Callers build a typed CPArgs_* struct and call clientprot_core_emit().
 * No function pointers anywhere; the revision dispatch uses a switch on
 * enum ClientProtOpKind exactly like inbound uses a switch on packet_type. */

#include <stddef.h>
#include <stdint.h>

struct GGame;

/** Max tiles in one Client.ts tryMove / MOVE_* path segment (matches GAME_TRY_MOVE_MAX_PATH). */
#define CLIENTPROT_MOVE_PATH_MAX 25

/* ── Op kinds ────────────────────────────────────────────────────────────── */

enum ClientProtOpKind
{
    CLIENTPROT_OP_NO_TIMEOUT = 0,
    CLIENTPROT_OP_IDLE_TIMER,
    CLIENTPROT_OP_EVENT_TRACKING,
    CLIENTPROT_OP_MOVE_GAMECLICK,
    CLIENTPROT_OP_MOVE_MINIMAPCLICK,
    CLIENTPROT_OP_MOVE_OPCLICK,
    CLIENTPROT_OP_CHAT_SETMODE,
    CLIENTPROT_OP_MESSAGE_PUBLIC,
    CLIENTPROT_OP_MESSAGE_PRIVATE,
    CLIENTPROT_OP_CLIENT_CHEAT,
    CLIENTPROT_OP_RESUME_P_COUNTDIALOG,
    CLIENTPROT_OP_RESUME_PAUSEBUTTON,
    CLIENTPROT_OP_INV_BUTTON,
    CLIENTPROT_OP_INV_BUTTOND,
    CLIENTPROT_OP_IF_BUTTON,
    CLIENTPROT_OP_CLOSE_MODAL,
    CLIENTPROT_OP_OPHELD,
    CLIENTPROT_OP_OPHELDT,
    CLIENTPROT_OP_OPHELDU,
    CLIENTPROT_OP_OPLOC,
    CLIENTPROT_OP_OPLOCT,
    CLIENTPROT_OP_OPLOCU,
    CLIENTPROT_OP_OPNPC,
    CLIENTPROT_OP_OPNPCT,
    CLIENTPROT_OP_OPNPCU,
    CLIENTPROT_OP_OPOBJ,
    CLIENTPROT_OP_OPOBJT,
    CLIENTPROT_OP_OPOBJU,
    CLIENTPROT_OP_OPPLAYER,
    CLIENTPROT_OP_OPPLAYERT,
    CLIENTPROT_OP_OPPLAYERU,
    CLIENTPROT_OP_FRIEND_ADD,
    CLIENTPROT_OP_FRIEND_DEL,
    CLIENTPROT_OP_IGNORE_ADD,
    CLIENTPROT_OP_IGNORE_DEL,
    CLIENTPROT_OP_IF_PLAYERDESIGN,
    CLIENTPROT_OP_TUTORIAL_CLICKSIDE,
    CLIENTPROT_OP_REPORT_ABUSE,
};

/* ── Argument structs (one per op needing payload) ───────────────────────── */

struct CPArgs_ChatSetMode      { int pub, priv, trade; };
/** Scene-local tile path: path[0] = first step, path[path_len-1] = destination. */
struct CPArgs_MovGameClick
{
    int run;
    int path_len;
    int path_x[CLIENTPROT_MOVE_PATH_MAX];
    int path_z[CLIENTPROT_MOVE_PATH_MAX];
};
struct CPArgs_MovMinimapClick  { int x, z, run; const uint8_t* cam; int cam_n; };
struct CPArgs_InvButton        { int which; int comp_id; int slot; int obj_id; };
/** Client.ts INV_BUTTOND: p2(compId), p2(fromSlot), p2(toSlot), p1(mode). Same component for bank arrange. */
struct CPArgs_InvButtonD       { int comp_id; int from_slot; int to_slot; int mode; };
struct CPArgs_IfButton         { int comp_id; };
struct CPArgs_MessagePublic    { int color, effect; const uint8_t* wp; int n; };
struct CPArgs_MessagePrivate   { const char* user; const uint8_t* wp; int n; };
struct CPArgs_ClientCheat      { const char* line; };
struct CPArgs_OpHeld           { int which; int obj_id, slot, comp_id; };
/** Client.ts TGT_HELD: OPHELDT + 4×p2 (obj, slot, comp, targetComId). */
struct CPArgs_OpHeldT          { int obj_id, slot, comp_id, target_comp_id; };
/** Client.ts USEHELD_ONHELD: OPHELDU + 6×p2 (dest obj/slot/comp, src obj/slot/comp). */
struct CPArgs_OpHeldU          { int obj_id, slot, comp_id, src_obj_id, src_slot, src_comp_id; };
struct CPArgs_OpLoc            { int which; int x, z; int loc_id; int ctrl; };
struct CPArgs_OpNpc            { int which; int npc_slot; };
struct CPArgs_OpObj            { int which; int x, z; int obj_id; int ctrl; };
struct CPArgs_OpPlayer         { int which; int player_slot; };
struct CPArgs_FriendIgnore     { int64_t userhash; };
struct CPArgs_TutorialClick    { int tab; };
struct CPArgs_ResumeCountDlg   { int value; };
struct CPArgs_PlayerDesign     { const uint8_t* payload; int n; };
struct CPArgs_MovOpClick
{
    int run;
    int path_len;
    int path_x[CLIENTPROT_MOVE_PATH_MAX];
    int path_z[CLIENTPROT_MOVE_PATH_MAX];
};
struct CPArgs_ReportAbuse      { int64_t userhash; int type; };

/* ── Facade ──────────────────────────────────────────────────────────────── */

/** Build the outbound packet and send it via LibToriRS_NetSend.
 * @param args  Points to the matching CPArgs_* struct; NULL for no-payload ops.
 * By default logs each `[pkout]` line (kind, inferred logical opcode vs PKTOUT_LC245_2_*,
 * wire0, ISAAC low byte, payload hex — same masking as Client-TS `Packet.pIsaac`).
 * Set env `TORI_DEBUG_PKTOUT=0` to disable. */
void
clientprot_core_emit(struct GGame* game, enum ClientProtOpKind kind, void* args);

/** Encode Client.ts MOVE_GAMECLICK from a BFS path (scene tiles). No-op if path_len < 1. */
void
clientprot_emit_move_gameclick_path(
    struct GGame* game,
    int run,
    const int* path_x,
    const int* path_z,
    int path_len);

/** Encode Client.ts MOVE_OPCLICK from a BFS path (scene tiles). */
void
clientprot_emit_move_opclick_path(
    struct GGame* game,
    int run,
    const int* path_x,
    const int* path_z,
    int path_len);

/* ── Convenience inline wrappers ─────────────────────────────────────────── */

static inline void
clientprot_no_timeout(struct GGame* g)
{
    clientprot_core_emit(g, CLIENTPROT_OP_NO_TIMEOUT, NULL);
}

static inline void
clientprot_idle_timer(struct GGame* g)
{
    clientprot_core_emit(g, CLIENTPROT_OP_IDLE_TIMER, NULL);
}

static inline void
clientprot_close_modal(struct GGame* g)
{
    clientprot_core_emit(g, CLIENTPROT_OP_CLOSE_MODAL, NULL);
}

static inline void
clientprot_chat_setmode(struct GGame* g, int pub, int priv, int trade)
{
    struct CPArgs_ChatSetMode a = { pub, priv, trade };
    clientprot_core_emit(g, CLIENTPROT_OP_CHAT_SETMODE, &a);
}

static inline void
clientprot_message_public(
    struct GGame* g, int color, int effect, const uint8_t* wp, int n)
{
    struct CPArgs_MessagePublic a = { color, effect, wp, n };
    clientprot_core_emit(g, CLIENTPROT_OP_MESSAGE_PUBLIC, &a);
}

static inline void
clientprot_message_private(
    struct GGame* g, const char* user, const uint8_t* wp, int n)
{
    struct CPArgs_MessagePrivate a = { user, wp, n };
    clientprot_core_emit(g, CLIENTPROT_OP_MESSAGE_PRIVATE, &a);
}

static inline void
clientprot_client_cheat(struct GGame* g, const char* line)
{
    struct CPArgs_ClientCheat a = { line };
    clientprot_core_emit(g, CLIENTPROT_OP_CLIENT_CHEAT, &a);
}

static inline void
clientprot_inv_button(
    struct GGame* g, int which, int comp_id, int slot, int obj_id)
{
    struct CPArgs_InvButton a = { which, comp_id, slot, obj_id };
    clientprot_core_emit(g, CLIENTPROT_OP_INV_BUTTON, &a);
}

static inline void
clientprot_inv_button_d(struct GGame* g, int comp_id, int from_slot, int to_slot, int mode)
{
    struct CPArgs_InvButtonD a = { comp_id, from_slot, to_slot, mode };
    clientprot_core_emit(g, CLIENTPROT_OP_INV_BUTTOND, &a);
}

static inline void
clientprot_if_button(struct GGame* g, int comp_id)
{
    struct CPArgs_IfButton a = { comp_id };
    clientprot_core_emit(g, CLIENTPROT_OP_IF_BUTTON, &a);
}

static inline void
clientprot_friend_add(struct GGame* g, int64_t userhash)
{
    struct CPArgs_FriendIgnore a = { userhash };
    clientprot_core_emit(g, CLIENTPROT_OP_FRIEND_ADD, &a);
}

static inline void
clientprot_friend_del(struct GGame* g, int64_t userhash)
{
    struct CPArgs_FriendIgnore a = { userhash };
    clientprot_core_emit(g, CLIENTPROT_OP_FRIEND_DEL, &a);
}

static inline void
clientprot_ignore_add(struct GGame* g, int64_t userhash)
{
    struct CPArgs_FriendIgnore a = { userhash };
    clientprot_core_emit(g, CLIENTPROT_OP_IGNORE_ADD, &a);
}

static inline void
clientprot_ignore_del(struct GGame* g, int64_t userhash)
{
    struct CPArgs_FriendIgnore a = { userhash };
    clientprot_core_emit(g, CLIENTPROT_OP_IGNORE_DEL, &a);
}

static inline void
clientprot_tutorial_clickside(struct GGame* g, int tab)
{
    struct CPArgs_TutorialClick a = { tab };
    clientprot_core_emit(g, CLIENTPROT_OP_TUTORIAL_CLICKSIDE, &a);
}

static inline void
clientprot_resume_p_countdialog(struct GGame* g, int value)
{
    struct CPArgs_ResumeCountDlg a = { value };
    clientprot_core_emit(g, CLIENTPROT_OP_RESUME_P_COUNTDIALOG, &a);
}

static inline void
clientprot_resume_pausebutton(struct GGame* g)
{
    clientprot_core_emit(g, CLIENTPROT_OP_RESUME_PAUSEBUTTON, NULL);
}

#endif /* OSRS_CORE_CLIENTPROT_CORE_H */
