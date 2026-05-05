#ifndef OSRS_CORE_CLIENTPROT_CORE_H
#define OSRS_CORE_CLIENTPROT_CORE_H

/* Revision-agnostic client → server packet emission facade.
 * Callers build a typed CPArgs_* struct and call clientprot_core_emit().
 * No function pointers anywhere; the revision dispatch uses a switch on
 * enum ClientProtOpKind exactly like inbound uses a switch on packet_type. */

#include <stddef.h>
#include <stdint.h>

struct GGame;

/* ── Op kinds ────────────────────────────────────────────────────────────── */

enum ClientProtOpKind
{
    CLIENTPROT_OP_NO_TIMEOUT = 0,
    CLIENTPROT_OP_IDLE_TIMER,
    CLIENTPROT_OP_EVENT_MOUSE_MOVE,
    CLIENTPROT_OP_EVENT_MOUSE_CLICK,
    CLIENTPROT_OP_EVENT_APPLET_FOCUS,
    CLIENTPROT_OP_EVENT_CAMERA_POSITION,
    CLIENTPROT_OP_EVENT_TRACKING,
    CLIENTPROT_OP_MAP_BUILD_COMPLETE,
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
    CLIENTPROT_OP_SEND_SNAPSHOT,
    CLIENTPROT_OP_TUTORIAL_CLICKSIDE,
    CLIENTPROT_OP_LOGOUT,
    CLIENTPROT_OP_REPORT_ABUSE,
};

/* ── Argument structs (one per op needing payload) ───────────────────────── */

struct CPArgs_ChatSetMode      { int pub, priv, trade; };
struct CPArgs_MovGameClick     { int x, z, run; };
struct CPArgs_MovMinimapClick  { int x, z, run; const uint8_t* cam; int cam_n; };
struct CPArgs_InvButton        { int which; int comp_id; int slot; int obj_id; };
struct CPArgs_InvButtonD       { int from_comp, from_slot, to_comp, to_slot; };
struct CPArgs_IfButton         { int comp_id; };
struct CPArgs_MessagePublic    { int color, effect; const uint8_t* wp; int n; };
struct CPArgs_MessagePrivate   { const char* user; const uint8_t* wp; int n; };
struct CPArgs_ClientCheat      { const char* line; };
struct CPArgs_OpHeld           { int which; int obj_id, slot, comp_id; };
struct CPArgs_OpHeldT          { int obj_id, slot, comp_id; int sel_obj_id, sel_slot, sel_comp_id; };
struct CPArgs_OpHeldU          { int obj_id, slot, comp_id; int g_obj_id; };
struct CPArgs_OpLoc            { int which; int x, z; int loc_id; int ctrl; };
struct CPArgs_OpNpc            { int which; int npc_slot; };
struct CPArgs_OpObj            { int which; int x, z; int obj_id; int ctrl; };
struct CPArgs_OpPlayer         { int which; int player_slot; };
struct CPArgs_FriendIgnore     { int64_t userhash; };
struct CPArgs_EventMouseClick  { int button, x, y, cycle; };
struct CPArgs_EventCamera      { int yaw, pitch; };
struct CPArgs_EventBuf         { const uint8_t* buf; int n; };
struct CPArgs_EventFocus       { int focused; };
struct CPArgs_TutorialClick    { int tab; };
struct CPArgs_ResumeCountDlg   { int value; };
struct CPArgs_PlayerDesign     { const uint8_t* payload; int n; };
struct CPArgs_Snapshot         { const uint8_t* payload; int n; };
struct CPArgs_MovOpClick       { int x, z, run; };
struct CPArgs_ReportAbuse      { int64_t userhash; int type; };

/* ── Facade ──────────────────────────────────────────────────────────────── */

/** Build the outbound packet and send it via LibToriRS_NetSend.
 * @param args  Points to the matching CPArgs_* struct; NULL for no-payload ops.
 * With env `TORI_DEBUG_PKTOUT` set (non-empty, not `0`), logs each packet: kind name,
 * inferred logical opcode (packetout.h PKTOUT_LC245_2_*), wire byte 0, ISAAC term,
 * size and payload hex — same masking as Client-TS `Packet.pIsaac`. */
void
clientprot_core_emit(struct GGame* game, enum ClientProtOpKind kind, void* args);

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
clientprot_map_build_complete(struct GGame* g)
{
    clientprot_core_emit(g, CLIENTPROT_OP_MAP_BUILD_COMPLETE, NULL);
}

static inline void
clientprot_close_modal(struct GGame* g)
{
    clientprot_core_emit(g, CLIENTPROT_OP_CLOSE_MODAL, NULL);
}

static inline void
clientprot_logout(struct GGame* g)
{
    clientprot_core_emit(g, CLIENTPROT_OP_LOGOUT, NULL);
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
clientprot_if_button(struct GGame* g, int comp_id)
{
    struct CPArgs_IfButton a = { comp_id };
    clientprot_core_emit(g, CLIENTPROT_OP_IF_BUTTON, &a);
}

static inline void
clientprot_move_gameclick(struct GGame* g, int x, int z, int run)
{
    struct CPArgs_MovGameClick a = { x, z, run };
    clientprot_core_emit(g, CLIENTPROT_OP_MOVE_GAMECLICK, &a);
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

static inline void
clientprot_event_camera(struct GGame* g, int yaw, int pitch)
{
    struct CPArgs_EventCamera a = { yaw, pitch };
    clientprot_core_emit(g, CLIENTPROT_OP_EVENT_CAMERA_POSITION, &a);
}

static inline void
clientprot_event_focus(struct GGame* g, int focused)
{
    struct CPArgs_EventFocus a = { focused };
    clientprot_core_emit(g, CLIENTPROT_OP_EVENT_APPLET_FOCUS, &a);
}

static inline void
clientprot_event_mouse_click(struct GGame* g, int button, int x, int y, int cycle)
{
    struct CPArgs_EventMouseClick a = { button, x, y, cycle };
    clientprot_core_emit(g, CLIENTPROT_OP_EVENT_MOUSE_CLICK, &a);
}

#endif /* OSRS_CORE_CLIENTPROT_CORE_H */
