#ifndef OSRS_CORE_CLIENTPROT_SEND_H
#define OSRS_CORE_CLIENTPROT_SEND_H

/* clientprot_send — pure outbound serializer layer.
 *
 * Each clientprot_send_xxx_v1() accepts a pre-resolved wire opcode plus a
 * fully-populated PktClientProt_Xxx_v1 struct and writes the bytes into `b` using
 * the ISAAC cipher.  The opcode value comes from the rev-specific caller
 * (gamenet_core_send_*_v1) which receives it from gamenet_rev245_2_send.c.
 *
 * Rules:
 *  - ZERO GGame knowledge.
 *  - ZERO revision-specific header includes (no packetout_lc245_2.h).
 *  - The `opcode` parameter is the only revision-specific value; everything
 *    else is revision-agnostic payload.
 */

#include "osrs/core/gamenet_core_send.h"

struct RSBuffer;
struct Isaac;

/* ── Per-op serializers ──────────────────────────────────────────────────── */

/* Keepalives */
void
clientprot_send_no_timeout_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode);
void
clientprot_send_idle_timer_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode);
void
clientprot_send_event_tracking_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode);
void
clientprot_send_close_modal_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode);
void
clientprot_send_resume_pausebutton_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_ResumePauseButton_v1* w);
void
clientprot_send_resume_p_countdialog_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_ResumeCntDlg_v1* w);

/* Movement */
void
clientprot_send_move_gameclick_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_MoveGameClick_v1* w);
void
clientprot_send_move_opclick_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_MoveOpClick_v1* w);
void
clientprot_send_move_minimapclick_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_MoveMinimapClick_v1* w);

/* Chat */
void
clientprot_send_chat_setmode_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_ChatSetMode_v1* w);
void
clientprot_send_message_public_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_MessagePublic_v1* w);
void
clientprot_send_message_private_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_MessagePrivate_v1* w);
void
clientprot_send_client_cheat_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_ClientCheat_v1* w);

/* Interface buttons */
void
clientprot_send_inv_button_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_InvButton_v1* w);
void
clientprot_send_inv_button_d_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_InvButtonD_v1* w);
void
clientprot_send_if_button_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_IfButton_v1* w);
void
clientprot_send_if_playerdesign_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_PlayerDesign_v1* w);
void
clientprot_send_tutorial_clickside_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_TutorialClick_v1* w);

/* World ops: held */
void
clientprot_send_opheld_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpHeld_v1* w);
void
clientprot_send_opheldt_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpHeldT_v1* w);
void
clientprot_send_opheldu_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpHeldU_v1* w);

/* World ops: loc */
void
clientprot_send_oploc_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpLoc_v1* w);
void
clientprot_send_oploct_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpLoc_v1* w);
void
clientprot_send_oplocu_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpLoc_v1* w);

/* World ops: npc */
void
clientprot_send_opnpc_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpNpc_v1* w);
void
clientprot_send_opnpct_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpNpc_v1* w);
void
clientprot_send_opnpcu_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpNpc_v1* w);

/* World ops: obj */
void
clientprot_send_opobj_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpObj_v1* w);
void
clientprot_send_opobjt_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpObj_v1* w);
void
clientprot_send_opobju_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpObj_v1* w);

/* World ops: player */
void
clientprot_send_opplayer_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpPlayer_v1* w);
void
clientprot_send_opplayert_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpPlayer_v1* w);
void
clientprot_send_opplayeru_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpPlayer_v1* w);

/* Extended world ops (Client.ts spell / use-on-target trailing fields). */
void
clientprot_send_oploct_target_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpLoc_v1* w,
    int target_comp_id);
void
clientprot_send_oplocu_use_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpLoc_v1* w,
    const struct PktClientProt_OpHeldSourceInv_v1* src);
void
clientprot_send_opnpct_target_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpNpc_v1* w,
    int target_comp_id);
void
clientprot_send_opnpcu_use_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpNpc_v1* w,
    const struct PktClientProt_OpHeldSourceInv_v1* src);
void
clientprot_send_opobjt_spell_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    int wire_x,
    int wire_z,
    int obj_id,
    int target_comp_id);
void
clientprot_send_opobju_use_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpObj_v1* w,
    const struct PktClientProt_OpHeldSourceInv_v1* src);
void
clientprot_send_opplayert_target_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpPlayer_v1* w,
    int target_comp_id);
void
clientprot_send_opplayeru_use_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpPlayer_v1* w,
    const struct PktClientProt_OpHeldSourceInv_v1* src);

/* Social */
void
clientprot_send_friend_add_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_FriendIgnore_v1* w);
void
clientprot_send_friend_del_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_FriendIgnore_v1* w);
void
clientprot_send_ignore_add_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_FriendIgnore_v1* w);
void
clientprot_send_ignore_del_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_FriendIgnore_v1* w);
void
clientprot_send_report_abuse_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_ReportAbuse_v1* w);

#endif /* OSRS_CORE_CLIENTPROT_SEND_H */
