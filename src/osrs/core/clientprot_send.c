#include "clientprot_send.h"

#include "osrs/core/clientprot_move_path.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/rsbuf_isaac.h"

#include <string.h>

/* Pure serializers: bytes only, zero GGame knowledge.
 * The opcode value is supplied by the caller (gamenet_core_send_*_v1),
 * which receives it from the revision-specific gateway. */

/* ── Keepalives ──────────────────────────────────────────────────────────── */

void
clientprot_send_no_timeout_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode)
{
    rsbuf_p1isaac(b, isaac, opcode);
}

void
clientprot_send_idle_timer_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode)
{
    rsbuf_p1isaac(b, isaac, opcode);
}

void
clientprot_send_event_tracking_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode)
{
    rsbuf_p1isaac(b, isaac, opcode);
}

void
clientprot_send_close_modal_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode)
{
    rsbuf_p1isaac(b, isaac, opcode);
}

void
clientprot_send_resume_pausebutton_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode)
{
    rsbuf_p1isaac(b, isaac, opcode);
}

void
clientprot_send_resume_p_countdialog_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_ResumeCntDlg_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p4(b, w->value);
}

/* ── Movement ────────────────────────────────────────────────────────────── */

void
clientprot_send_move_gameclick_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_MoveGameClick_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    clientprot_move_path_write_subpacket(
        b, w->run ? 1 : 0, w->base_x, w->base_z, w->path_x, w->path_z, w->path_len);
}

void
clientprot_send_move_opclick_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_MoveOpClick_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    clientprot_move_path_write_subpacket(
        b, w->run ? 1 : 0, w->base_x, w->base_z, w->path_x, w->path_z, w->path_len);
}

void
clientprot_send_move_minimapclick_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_MoveMinimapClick_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    int mark = (int)b->position;
    p1(b, 0);
    p1(b, w->run ? 1 : 0);
    p2(b, w->x);
    p2(b, w->z);
    if( w->cam && w->cam_n > 0 )
        rsbuf_pwrite(b, w->cam, w->cam_n);
    psize1(b, mark);
}

/* ── Chat ────────────────────────────────────────────────────────────────── */

void
clientprot_send_chat_setmode_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_ChatSetMode_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p1(b, w->pub);
    p1(b, w->priv);
    p1(b, w->trade);
}

void
clientprot_send_message_public_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_MessagePublic_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    int mark = (int)b->position;
    p1(b, 0);
    p1(b, w->color);
    p1(b, w->effect);
    if( w->wp && w->n > 0 )
        rsbuf_pwrite(b, w->wp, w->n);
    psize1(b, mark);
}

void
clientprot_send_message_private_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_MessagePrivate_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    int mark = (int)b->position;
    p1(b, 0);
    if( w->user )
        pjstr(b, w->user, 0);
    else
        p1(b, 0);
    if( w->wp && w->n > 0 )
        rsbuf_pwrite(b, w->wp, w->n);
    psize1(b, mark);
}

void
clientprot_send_client_cheat_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_ClientCheat_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    /* Client.ts: p1(chatInput.length - 2 + 1) then pjstr(chatInput.substring(2)).
     * w->line is the full "::body" string; body starts at +2.
     * pjstr appends a newline (byte 10), so wire length = strlen(body) + 1. */
    const char* body = (w->line && strlen(w->line) >= 2) ? w->line + 2 : "";
    p1(b, (int)(strlen(body) + 1));
    pjstr(b, body, 10);
}

/* ── Interface buttons ───────────────────────────────────────────────────── */

void
clientprot_send_inv_button_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_InvButton_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->obj_id);
    p2(b, w->slot);
    p2(b, w->comp_id);
}

void
clientprot_send_inv_button_d_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_InvButtonD_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->comp_id);
    p2(b, w->from_slot);
    p2(b, w->to_slot);
    p1(b, w->mode & 0xff);
}

void
clientprot_send_if_button_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_IfButton_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->comp_id);
}

void
clientprot_send_if_playerdesign_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_PlayerDesign_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    if( w->payload && w->n > 0 )
        rsbuf_pwrite(b, w->payload, w->n);
}

void
clientprot_send_tutorial_clickside_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_TutorialClick_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p1(b, w->tab);
}

/* ── World ops: held ─────────────────────────────────────────────────────── */

void
clientprot_send_opheld_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpHeld_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->obj_id);
    p2(b, w->slot);
    p2(b, w->comp_id);
}

void
clientprot_send_opheldt_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpHeldT_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->obj_id);
    p2(b, w->slot);
    p2(b, w->comp_id);
    p2(b, w->target_comp_id);
}

void
clientprot_send_opheldu_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpHeldU_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->obj_id);
    p2(b, w->slot);
    p2(b, w->comp_id);
    p2(b, w->src_obj_id);
    p2(b, w->src_slot);
    p2(b, w->src_comp_id);
}

/* ── World ops: loc ──────────────────────────────────────────────────────── */

void
clientprot_send_oploc_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpLoc_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->loc_id);
    (void)w->ctrl;
}

void
clientprot_send_oploct_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpLoc_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->loc_id);
    (void)w->ctrl;
}

void
clientprot_send_oplocu_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpLoc_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->loc_id);
}

/* ── World ops: npc ──────────────────────────────────────────────────────── */

void
clientprot_send_opnpc_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpNpc_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->npc_slot);
}

void
clientprot_send_opnpct_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpNpc_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->npc_slot);
}

void
clientprot_send_opnpcu_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpNpc_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->npc_slot);
}

/* ── World ops: obj ──────────────────────────────────────────────────────── */

void
clientprot_send_opobj_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpObj_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->obj_id);
    p1(b, w->ctrl ? 1 : 0);
}

void
clientprot_send_opobjt_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpObj_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->obj_id);
    p1(b, w->ctrl ? 1 : 0);
}

void
clientprot_send_opobju_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpObj_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->obj_id);
}

/* ── World ops: player ───────────────────────────────────────────────────── */

void
clientprot_send_opplayer_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpPlayer_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->player_slot);
}

void
clientprot_send_opplayert_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpPlayer_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->player_slot);
}

void
clientprot_send_opplayeru_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpPlayer_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->player_slot);
}

void
clientprot_send_oploct_target_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpLoc_v1* w,
    int target_comp_id)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->loc_id);
    (void)w->ctrl;
    p2(b, target_comp_id);
}

void
clientprot_send_oplocu_use_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpLoc_v1* w,
    const struct PktClientProt_OpHeldSourceInv_v1* src)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->loc_id);
    p2(b, src->obj_comp_id);
    p2(b, src->slot);
    p2(b, src->sel_comp_id);
}

void
clientprot_send_opnpct_target_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpNpc_v1* w,
    int target_comp_id)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->npc_slot);
    p2(b, target_comp_id);
}

void
clientprot_send_opnpcu_use_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpNpc_v1* w,
    const struct PktClientProt_OpHeldSourceInv_v1* src)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->npc_slot);
    p2(b, src->obj_comp_id);
    p2(b, src->slot);
    p2(b, src->sel_comp_id);
}

void
clientprot_send_opobjt_spell_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    int wire_x,
    int wire_z,
    int obj_id,
    int target_comp_id)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, wire_x);
    p2(b, wire_z);
    p2(b, obj_id);
    p2(b, target_comp_id);
}

void
clientprot_send_opobju_use_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpObj_v1* w,
    const struct PktClientProt_OpHeldSourceInv_v1* src)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->obj_id);
    p2(b, src->obj_comp_id);
    p2(b, src->slot);
    p2(b, src->sel_comp_id);
}

void
clientprot_send_opplayert_target_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpPlayer_v1* w,
    int target_comp_id)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->player_slot);
    p2(b, target_comp_id);
}

void
clientprot_send_opplayeru_use_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_OpPlayer_v1* w,
    const struct PktClientProt_OpHeldSourceInv_v1* src)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p2(b, w->player_slot);
    p2(b, src->obj_comp_id);
    p2(b, src->slot);
    p2(b, src->sel_comp_id);
}

/* ── Social ──────────────────────────────────────────────────────────────── */

void
clientprot_send_friend_add_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_FriendIgnore_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p8(b, w->userhash);
}

void
clientprot_send_friend_del_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_FriendIgnore_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p8(b, w->userhash);
}

void
clientprot_send_ignore_add_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_FriendIgnore_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p8(b, w->userhash);
}

void
clientprot_send_ignore_del_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_FriendIgnore_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p8(b, w->userhash);
}

void
clientprot_send_report_abuse_v1(
    struct RSBuffer* b,
    struct Isaac* isaac,
    int opcode,
    const struct PktClientProt_ReportAbuse_v1* w)
{
    rsbuf_p1isaac(b, isaac, opcode);
    p8(b, w->userhash);
    p1(b, w->type);
}
