#ifndef OSRS_CORE_GAMENET_CORE_SEND_H
#define OSRS_CORE_GAMENET_CORE_SEND_H

/* gamenet_core_send — outbound data-hydration layer.
 *
 * Per-op _v1 functions gather all data needed for the wire packet from GGame
 * (world base tiles, player state, ISAAC cipher, etc.), build a typed
 * PktClientProt_*_v1 struct, and call the matching clientprot_send_xxx_v1().
 *
 * The first `opcode` argument to each _v1 entry point is the revision-specific
 * obfuscated client packet id (e.g. PKTOUT_LC245_2_*).  The revision gateway
 * (gamenet_rev245_2_send.c) resolves it; core and clientprot never include
 * packetout headers or hardcode wire opcodes.
 *
 * Rules:
 *  - May read from GGame / World freely.
 *  - Must NOT call clientprot_core_emit() or use ClientProtOpKind.
 *  - Must NOT write network bytes directly; delegate to clientprot_send_xxx_v1.
 */

#include <stdint.h>

struct GGame;

/* ── Wire payload structs (replace CPArgs_*) ─────────────────────────────── */

#define GAMENET_CORE_SEND_MOVE_PATH_MAX 25

struct PktClientProt_MoveGameClick_v1
{
    int run;
    int path_len;
    int path_x[GAMENET_CORE_SEND_MOVE_PATH_MAX];
    int path_z[GAMENET_CORE_SEND_MOVE_PATH_MAX];
    int base_x;
    int base_z;
};

struct PktClientProt_MoveMinimapClick_v1
{
    int x;
    int z;
    int run;
    const uint8_t* cam;
    int cam_n;
};

struct PktClientProt_MoveOpClick_v1
{
    int run;
    int path_len;
    int path_x[GAMENET_CORE_SEND_MOVE_PATH_MAX];
    int path_z[GAMENET_CORE_SEND_MOVE_PATH_MAX];
    int base_x;
    int base_z;
};

struct PktClientProt_ChatSetMode_v1
{
    int pub;
    int priv;
    int trade;
};
struct PktClientProt_MessagePublic_v1
{
    int color;
    int effect;
    const uint8_t* wp;
    int n;
};
struct PktClientProt_MessagePrivate_v1
{
    const char* user;
    const uint8_t* wp;
    int n;
};
struct PktClientProt_ClientCheat_v1
{
    const char* line;
};
struct PktClientProt_ResumeCntDlg_v1
{
    int value;
};
/** Payload after opcode for RESUME_PAUSEBUTTON (Client.ts: pIsaac then p2(component)). */
struct PktClientProt_ResumePauseButton_v1
{
    int comp_id;
};

struct PktClientProt_InvButton_v1
{
    int comp_id;
    int slot;
    int obj_id;
};
struct PktClientProt_InvButtonD_v1
{
    int comp_id;
    int from_slot;
    int to_slot;
    int mode;
};
struct PktClientProt_IfButton_v1
{
    int comp_id;
};
struct PktClientProt_PlayerDesign_v1
{
    const uint8_t* payload;
    int n;
};
struct PktClientProt_TutorialClick_v1
{
    int tab;
};

struct PktClientProt_OpHeld_v1
{
    int obj_id;
    int slot;
    int comp_id;
};
struct PktClientProt_OpHeldT_v1
{
    int obj_id;
    int slot;
    int comp_id;
    int target_comp_id;
};
struct PktClientProt_OpHeldU_v1
{
    int obj_id;
    int slot;
    int comp_id;
    int src_obj_id;
    int src_slot;
    int src_comp_id;
};

struct PktClientProt_OpLoc_v1
{
    int wire_x;
    int wire_z;
    int loc_id;
    int ctrl;
};
struct PktClientProt_OpNpc_v1
{
    int npc_slot;
};
struct PktClientProt_OpObj_v1
{
    int wire_x;
    int wire_z;
    int obj_id;
};
struct PktClientProt_OpPlayer_v1
{
    int player_slot;
};

/** Extra payload after OPLOCU base (Client.ts USEHELD_ONLOC): held component triple. */
struct PktClientProt_OpHeldSourceInv_v1
{
    int obj_comp_id;
    int slot;
    int sel_comp_id;
};

struct PktClientProt_FriendIgnore_v1
{
    int64_t userhash;
};
struct PktClientProt_ReportAbuse_v1
{
    int64_t userhash;
    int type;
};

/* ── Hydrator entry points ───────────────────────────────────────────────── */

/* Keepalives */
void
gamenet_core_send_no_timeout_v1(
    struct GGame* g,
    int opcode);
void
gamenet_core_send_idle_timer_v1(
    struct GGame* g,
    int opcode);
void
gamenet_core_send_event_tracking_v1(
    struct GGame* g,
    int opcode);
void
gamenet_core_send_close_modal_v1(
    struct GGame* g,
    int opcode);
void
gamenet_core_send_resume_pausebutton_v1(
    struct GGame* g,
    int opcode,
    int comp_id);
void
gamenet_core_send_resume_p_countdialog_v1(
    struct GGame* g,
    int opcode,
    int value);

/* Movement */
void
gamenet_core_send_move_gameclick_v1(
    struct GGame* g,
    int opcode,
    int run,
    const int* px,
    const int* pz,
    int n);
void
gamenet_core_send_move_opclick_v1(
    struct GGame* g,
    int opcode,
    int run,
    const int* px,
    const int* pz,
    int n);
void
gamenet_core_send_move_minimapclick_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int run,
    const uint8_t* cam,
    int cam_n);

/* Chat */
void
gamenet_core_send_chat_setmode_v1(
    struct GGame* g,
    int opcode,
    int pub,
    int priv,
    int trade);
void
gamenet_core_send_message_public_v1(
    struct GGame* g,
    int opcode,
    int color,
    int effect,
    const uint8_t* wp,
    int n);
void
gamenet_core_send_message_private_v1(
    struct GGame* g,
    int opcode,
    const char* user,
    const uint8_t* wp,
    int n);
void
gamenet_core_send_client_cheat_v1(
    struct GGame* g,
    int opcode,
    const char* line);

/* Interface buttons */
void
gamenet_core_send_inv_button_v1(
    struct GGame* g,
    int opcode,
    int comp_id,
    int slot,
    int obj_id);
void
gamenet_core_send_inv_button_d_v1(
    struct GGame* g,
    int opcode,
    int comp_id,
    int from_slot,
    int to_slot,
    int mode);
void
gamenet_core_send_if_button_v1(
    struct GGame* g,
    int opcode,
    int comp_id);
void
gamenet_core_send_if_playerdesign_v1(
    struct GGame* g,
    int opcode,
    const uint8_t* payload,
    int n);
void
gamenet_core_send_tutorial_clickside_v1(
    struct GGame* g,
    int opcode,
    int tab);

/* World ops: held */
void
gamenet_core_send_opheld_v1(
    struct GGame* g,
    int opcode,
    int obj_id,
    int slot,
    int comp_id);
void
gamenet_core_send_opheldt_v1(
    struct GGame* g,
    int opcode,
    int obj_id,
    int slot,
    int comp_id,
    int target_comp_id);
void
gamenet_core_send_opheldu_v1(
    struct GGame* g,
    int opcode,
    int obj_id,
    int slot,
    int comp_id,
    int src_obj_id,
    int src_slot,
    int src_comp_id);

/* World ops: loc */
void
gamenet_core_send_oploc_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int loc_id,
    int ctrl);
void
gamenet_core_send_oploct_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int loc_id,
    int ctrl);
void
gamenet_core_send_oplocu_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int loc_id);
void
gamenet_core_send_oploct_target_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int loc_id,
    int ctrl,
    int target_comp_id);
void
gamenet_core_send_oplocu_use_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int loc_id,
    int obj_comp_id,
    int slot,
    int sel_comp_id);

/* World ops: npc */
void
gamenet_core_send_opnpc_v1(
    struct GGame* g,
    int opcode,
    int npc_slot);
void
gamenet_core_send_opnpct_v1(
    struct GGame* g,
    int opcode,
    int npc_slot);
void
gamenet_core_send_opnpcu_v1(
    struct GGame* g,
    int opcode,
    int npc_slot);
void
gamenet_core_send_opnpct_target_v1(
    struct GGame* g,
    int opcode,
    int npc_slot,
    int target_comp_id);
void
gamenet_core_send_opnpcu_use_v1(
    struct GGame* g,
    int opcode,
    int npc_slot,
    int obj_comp_id,
    int slot,
    int sel_comp_id);

/* World ops: obj */
void
gamenet_core_send_opobj_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int obj_id);
void
gamenet_core_send_opobjt_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int obj_id);
void
gamenet_core_send_opobju_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int obj_id);
void
gamenet_core_send_opobjt_spell_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int obj_id,
    int target_comp_id);
void
gamenet_core_send_opobju_use_v1(
    struct GGame* g,
    int opcode,
    int x,
    int z,
    int obj_id,
    int obj_comp_id,
    int slot,
    int sel_comp_id);

/* World ops: player */
void
gamenet_core_send_opplayer_v1(
    struct GGame* g,
    int opcode,
    int player_slot);
void
gamenet_core_send_opplayert_v1(
    struct GGame* g,
    int opcode,
    int player_slot);
void
gamenet_core_send_opplayeru_v1(
    struct GGame* g,
    int opcode,
    int player_slot);
void
gamenet_core_send_opplayert_target_v1(
    struct GGame* g,
    int opcode,
    int player_slot,
    int target_comp_id);
void
gamenet_core_send_opplayeru_use_v1(
    struct GGame* g,
    int opcode,
    int player_slot,
    int obj_comp_id,
    int slot,
    int sel_comp_id);

/* Social */
void
gamenet_core_send_friend_add_v1(
    struct GGame* g,
    int opcode,
    int64_t userhash);
void
gamenet_core_send_friend_del_v1(
    struct GGame* g,
    int opcode,
    int64_t userhash);
void
gamenet_core_send_ignore_add_v1(
    struct GGame* g,
    int opcode,
    int64_t userhash);
void
gamenet_core_send_ignore_del_v1(
    struct GGame* g,
    int opcode,
    int64_t userhash);
void
gamenet_core_send_report_abuse_v1(
    struct GGame* g,
    int opcode,
    int64_t userhash,
    int type);

#endif /* OSRS_CORE_GAMENET_CORE_SEND_H */
