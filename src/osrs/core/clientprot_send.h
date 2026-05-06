#ifndef OSRS_CORE_CLIENTPROT_SEND_H
#define OSRS_CORE_CLIENTPROT_SEND_H

/* clientprot_send — pure outbound serializer layer.
 *
 * Each clientprot_send_xxx_v1() takes a fully-populated WireOut_Xxx_v1 struct
 * (produced by gamenet_core_send_xxx_v1) and writes the rev-245_2 wire bytes
 * into `out` using ISAAC cipher `isaac`.
 *
 * Rules:
 *  - ZERO GGame knowledge.  No GGame*, World*, or game state reads.
 *  - No LibToriRS_NetSend calls — the caller (gamenet_core_send_xxx_v1) does that.
 *  - Returns number of bytes written, or -1 on error.
 *
 * Populated in Phase 2 (outbound layer split).
 */

#include "osrs/core/gamenet_core_send.h"

struct RSBuffer;
struct Isaac;

struct Isaac;

/* ── Per-op serializers ──────────────────────────────────────────────────── */

/* Keepalives */
void clientprot_send_no_timeout_v1(struct RSBuffer* b, struct Isaac* isaac);
void clientprot_send_idle_timer_v1(struct RSBuffer* b, struct Isaac* isaac);
void clientprot_send_event_tracking_v1(struct RSBuffer* b, struct Isaac* isaac);
void clientprot_send_close_modal_v1(struct RSBuffer* b, struct Isaac* isaac);
void clientprot_send_resume_pausebutton_v1(struct RSBuffer* b, struct Isaac* isaac);
void clientprot_send_resume_p_countdialog_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_ResumeCntDlg_v1* w);

/* Movement */
void clientprot_send_move_gameclick_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_MoveGameClick_v1* w);
void clientprot_send_move_opclick_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_MoveOpClick_v1* w);
void clientprot_send_move_minimapclick_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_MoveMinimapClick_v1* w);

/* Chat */
void clientprot_send_chat_setmode_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_ChatSetMode_v1* w);
void clientprot_send_message_public_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_MessagePublic_v1* w);
void clientprot_send_message_private_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_MessagePrivate_v1* w);
void clientprot_send_client_cheat_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_ClientCheat_v1* w);

/* Interface buttons */
void clientprot_send_inv_button_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_InvButton_v1* w);
void clientprot_send_inv_button_d_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_InvButtonD_v1* w);
void clientprot_send_if_button_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_IfButton_v1* w);
void clientprot_send_if_playerdesign_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_PlayerDesign_v1* w);
void clientprot_send_tutorial_clickside_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_TutorialClick_v1* w);

/* World ops: held */
void clientprot_send_opheld_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpHeld_v1* w);
void clientprot_send_opheldt_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpHeldT_v1* w);
void clientprot_send_opheldu_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpHeldU_v1* w);

/* World ops: loc */
void clientprot_send_oploc_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpLoc_v1* w);
void clientprot_send_oploct_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpLoc_v1* w);
void clientprot_send_oplocu_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpLoc_v1* w);

/* World ops: npc */
void clientprot_send_opnpc_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpNpc_v1* w);
void clientprot_send_opnpct_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpNpc_v1* w);
void clientprot_send_opnpcu_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpNpc_v1* w);

/* World ops: obj */
void clientprot_send_opobj_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpObj_v1* w);
void clientprot_send_opobjt_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpObj_v1* w);
void clientprot_send_opobju_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpObj_v1* w);

/* World ops: player */
void clientprot_send_opplayer_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpPlayer_v1* w);
void clientprot_send_opplayert_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpPlayer_v1* w);
void clientprot_send_opplayeru_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpPlayer_v1* w);

/* Social */
void clientprot_send_friend_add_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_FriendIgnore_v1* w);
void clientprot_send_friend_del_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_FriendIgnore_v1* w);
void clientprot_send_ignore_add_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_FriendIgnore_v1* w);
void clientprot_send_ignore_del_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_FriendIgnore_v1* w);
void clientprot_send_report_abuse_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_ReportAbuse_v1* w);

#endif /* OSRS_CORE_CLIENTPROT_SEND_H */
