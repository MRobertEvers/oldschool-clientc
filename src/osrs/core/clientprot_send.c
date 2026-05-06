#include "clientprot_send.h"

#include "osrs/core/clientprot_move_path.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/rsbuf_isaac.h"
#include "osrs/revs/lc245_2/packetout_lc245_2.h"

/* Pure serializers: bytes only, zero GGame knowledge. */

/* ── Keepalives ──────────────────────────────────────────────────────────── */

void clientprot_send_no_timeout_v1(struct RSBuffer* b, struct Isaac* isaac)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_NO_TIMEOUT);
}

void clientprot_send_idle_timer_v1(struct RSBuffer* b, struct Isaac* isaac)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_IDLE_TIMER);
}

void clientprot_send_event_tracking_v1(struct RSBuffer* b, struct Isaac* isaac)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_EVENT_TRACKING);
}

void clientprot_send_close_modal_v1(struct RSBuffer* b, struct Isaac* isaac)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_CLOSE_MODAL);
}

void clientprot_send_resume_pausebutton_v1(struct RSBuffer* b, struct Isaac* isaac)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_RESUME_PAUSEBUTTON);
}

void clientprot_send_resume_p_countdialog_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_ResumeCntDlg_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_RESUME_P_COUNTDIALOG);
    p4(b, w->value);
}

/* ── Movement ────────────────────────────────────────────────────────────── */

void clientprot_send_move_gameclick_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_MoveGameClick_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_MOVE_GAMECLICK);
    clientprot_move_path_write_subpacket(
        b, w->run ? 1 : 0, w->base_x, w->base_z, w->path_x, w->path_z, w->path_len);
}

void clientprot_send_move_opclick_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_MoveOpClick_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_MOVE_OPCLICK);
    clientprot_move_path_write_subpacket(
        b, w->run ? 1 : 0, w->base_x, w->base_z, w->path_x, w->path_z, w->path_len);
}

void clientprot_send_move_minimapclick_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_MoveMinimapClick_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_MOVE_MINIMAPCLICK);
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

void clientprot_send_chat_setmode_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_ChatSetMode_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_CHAT_SETMODE);
    p1(b, w->pub);
    p1(b, w->priv);
    p1(b, w->trade);
}

void clientprot_send_message_public_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_MessagePublic_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_MESSAGE_PUBLIC);
    int mark = (int)b->position;
    p2(b, 0);
    p1(b, w->color);
    p1(b, w->effect);
    if( w->wp && w->n > 0 )
        rsbuf_pwrite(b, w->wp, w->n);
    psize2(b, mark);
}

void clientprot_send_message_private_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_MessagePrivate_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_MESSAGE_PRIVATE);
    int mark = (int)b->position;
    p2(b, 0);
    if( w->user )
        pjstr(b, w->user, 0);
    else
        p1(b, 0);
    if( w->wp && w->n > 0 )
        rsbuf_pwrite(b, w->wp, w->n);
    psize2(b, mark);
}

void clientprot_send_client_cheat_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_ClientCheat_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_CLIENT_CHEAT);
    int mark = (int)b->position;
    p2(b, 0);
    if( w->line )
        pjstr(b, w->line, 0);
    else
        p1(b, 0);
    psize2(b, mark);
}

/* ── Interface buttons ───────────────────────────────────────────────────── */

void clientprot_send_inv_button_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_InvButton_v1* w)
{
    static const int ops[5] = { PKTOUT_LC245_2_INV_BUTTON1,
                                PKTOUT_LC245_2_INV_BUTTON2,
                                PKTOUT_LC245_2_INV_BUTTON3,
                                PKTOUT_LC245_2_INV_BUTTON4,
                                PKTOUT_LC245_2_INV_BUTTON5 };
    int idx = w->which - 1;
    if( idx < 0 || idx > 4 ) idx = 0;
    rsbuf_p1isaac(b, isaac, ops[idx]);
    p2(b, w->obj_id);
    p2(b, w->slot);
    p2(b, w->comp_id);
}

void clientprot_send_inv_button_d_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_InvButtonD_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_INV_BUTTOND);
    p2(b, w->comp_id);
    p2(b, w->from_slot);
    p2(b, w->to_slot);
    p1(b, w->mode & 0xff);
}

void clientprot_send_if_button_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_IfButton_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_IF_BUTTON);
    p2(b, w->comp_id);
}

void clientprot_send_if_playerdesign_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_PlayerDesign_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_IF_PLAYERDESIGN);
    if( w->payload && w->n > 0 )
        rsbuf_pwrite(b, w->payload, w->n);
}

void clientprot_send_tutorial_clickside_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_TutorialClick_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_TUTORIAL_CLICKSIDE);
    p1(b, w->tab);
}

/* ── World ops: held ─────────────────────────────────────────────────────── */

void clientprot_send_opheld_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpHeld_v1* w)
{
    static const int ops[5] = { PKTOUT_LC245_2_OPHELD1, PKTOUT_LC245_2_OPHELD2,
                                PKTOUT_LC245_2_OPHELD3, PKTOUT_LC245_2_OPHELD4,
                                PKTOUT_LC245_2_OPHELD5 };
    int idx = w->which - 1;
    if( idx < 0 || idx > 4 ) idx = 0;
    rsbuf_p1isaac(b, isaac, ops[idx]);
    p2(b, w->obj_id);
    p2(b, w->slot);
    p2(b, w->comp_id);
}

void clientprot_send_opheldt_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpHeldT_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_OPHELDT);
    p2(b, w->obj_id);
    p2(b, w->slot);
    p2(b, w->comp_id);
    p2(b, w->target_comp_id);
}

void clientprot_send_opheldu_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpHeldU_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_OPHELDU);
    p2(b, w->obj_id);
    p2(b, w->slot);
    p2(b, w->comp_id);
    p2(b, w->src_obj_id);
    p2(b, w->src_slot);
    p2(b, w->src_comp_id);
}

/* ── World ops: loc ──────────────────────────────────────────────────────── */

void clientprot_send_oploc_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpLoc_v1* w)
{
    static const int ops[5] = { PKTOUT_LC245_2_OPLOC1, PKTOUT_LC245_2_OPLOC2,
                                PKTOUT_LC245_2_OPLOC3, PKTOUT_LC245_2_OPLOC4,
                                PKTOUT_LC245_2_OPLOC5 };
    int idx = w->which - 1;
    if( idx < 0 || idx > 4 ) idx = 0;
    rsbuf_p1isaac(b, isaac, ops[idx]);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->loc_id);
    p1(b, w->ctrl ? 1 : 0);
}

void clientprot_send_oploct_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpLoc_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_OPLOCT);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->loc_id);
    p1(b, w->ctrl ? 1 : 0);
}

void clientprot_send_oplocu_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpLoc_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_OPLOCU);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->loc_id);
}

/* ── World ops: npc ──────────────────────────────────────────────────────── */

void clientprot_send_opnpc_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpNpc_v1* w)
{
    static const int ops[5] = { PKTOUT_LC245_2_OPNPC1, PKTOUT_LC245_2_OPNPC2,
                                PKTOUT_LC245_2_OPNPC3, PKTOUT_LC245_2_OPNPC4,
                                PKTOUT_LC245_2_OPNPC5 };
    int idx = w->which - 1;
    if( idx < 0 || idx > 4 ) idx = 0;
    rsbuf_p1isaac(b, isaac, ops[idx]);
    p2(b, w->npc_slot);
}

void clientprot_send_opnpct_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpNpc_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_OPNPCT);
    p2(b, w->npc_slot);
}

void clientprot_send_opnpcu_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpNpc_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_OPNPCU);
    p2(b, w->npc_slot);
}

/* ── World ops: obj ──────────────────────────────────────────────────────── */

void clientprot_send_opobj_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpObj_v1* w)
{
    static const int ops[5] = { PKTOUT_LC245_2_OPOBJ1, PKTOUT_LC245_2_OPOBJ2,
                                PKTOUT_LC245_2_OPOBJ3, PKTOUT_LC245_2_OPOBJ4,
                                PKTOUT_LC245_2_OPOBJ5 };
    int idx = w->which - 1;
    if( idx < 0 || idx > 4 ) idx = 0;
    rsbuf_p1isaac(b, isaac, ops[idx]);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->obj_id);
    p1(b, w->ctrl ? 1 : 0);
}

void clientprot_send_opobjt_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpObj_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_OPBJT);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->obj_id);
    p1(b, w->ctrl ? 1 : 0);
}

void clientprot_send_opobju_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpObj_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_OPOBJU);
    p2(b, w->wire_x);
    p2(b, w->wire_z);
    p2(b, w->obj_id);
}

/* ── World ops: player ───────────────────────────────────────────────────── */

void clientprot_send_opplayer_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpPlayer_v1* w)
{
    static const int ops[4] = { PKTOUT_LC245_2_OPPLAYER1, PKTOUT_LC245_2_OPPLAYER2,
                                PKTOUT_LC245_2_OPPLAYER3, PKTOUT_LC245_2_OPPLAYER4 };
    if( w->which < 1 || w->which > 4 ) return;
    rsbuf_p1isaac(b, isaac, ops[w->which - 1]);
    p2(b, w->player_slot);
}

void clientprot_send_opplayert_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpPlayer_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_OPPLAYERT);
    p2(b, w->player_slot);
}

void clientprot_send_opplayeru_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_OpPlayer_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_OPPLAYERU);
    p2(b, w->player_slot);
}

/* ── Social ──────────────────────────────────────────────────────────────── */

void clientprot_send_friend_add_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_FriendIgnore_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_FRIENDLIST_ADD);
    p8(b, w->userhash);
}

void clientprot_send_friend_del_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_FriendIgnore_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_FRIENDLIST_DEL);
    p8(b, w->userhash);
}

void clientprot_send_ignore_add_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_FriendIgnore_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_IGNORELIST_ADD);
    p8(b, w->userhash);
}

void clientprot_send_ignore_del_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_FriendIgnore_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_IGNORELIST_DEL);
    p8(b, w->userhash);
}

void clientprot_send_report_abuse_v1(
    struct RSBuffer* b, struct Isaac* isaac,
    const struct WireOut_ReportAbuse_v1* w)
{
    rsbuf_p1isaac(b, isaac, PKTOUT_LC245_2_REPORT_ABUSE);
    p8(b, w->userhash);
    p1(b, w->type);
}
