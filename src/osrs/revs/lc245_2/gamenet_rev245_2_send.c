#include "gamenet_rev245_2_send.h"

#include "osrs/core/gamenet_core_send.h"
#include "osrs/revs/lc245_2/packetout_lc245_2.h"

/* Revision gateway: the only file that knows PKTOUT_LC245_2_* constants.
 * Each function resolves the wire opcode and forwards to gamenet_core_send_*_v1. */

/* ── Keepalives ──────────────────────────────────────────────────────────── */

void
gamenet_rev245_2_send_no_timeout(struct GGame* g)
{
    gamenet_core_send_no_timeout_v1(g, PKTOUT_LC245_2_NO_TIMEOUT);
}

void
gamenet_rev245_2_send_idle_timer(struct GGame* g)
{
    gamenet_core_send_idle_timer_v1(g, PKTOUT_LC245_2_IDLE_TIMER);
}
void
gamenet_rev245_2_send_event_tracking(struct GGame* g)
{
    gamenet_core_send_event_tracking_v1(g, PKTOUT_LC245_2_EVENT_TRACKING);
}
void
gamenet_rev245_2_send_close_modal(struct GGame* g)
{
    gamenet_core_send_close_modal_v1(g, PKTOUT_LC245_2_CLOSE_MODAL);
}
void
gamenet_rev245_2_send_resume_pausebutton(struct GGame* g, int comp_id)
{
    gamenet_core_send_resume_pausebutton_v1(g, PKTOUT_LC245_2_RESUME_PAUSEBUTTON, comp_id);
}
void
gamenet_rev245_2_send_resume_p_countdialog(
    struct GGame* g,
    int value)
{
    gamenet_core_send_resume_p_countdialog_v1(g, PKTOUT_LC245_2_RESUME_P_COUNTDIALOG, value);
}

/* ── Movement ────────────────────────────────────────────────────────────── */

void
gamenet_rev245_2_send_move_gameclick(
    struct GGame* g,
    int run,
    const int* px,
    const int* pz,
    int n)
{
    gamenet_core_send_move_gameclick_v1(g, PKTOUT_LC245_2_MOVE_GAMECLICK, run, px, pz, n);
}
void
gamenet_rev245_2_send_move_opclick(
    struct GGame* g,
    int run,
    const int* px,
    const int* pz,
    int n)
{
    gamenet_core_send_move_opclick_v1(g, PKTOUT_LC245_2_MOVE_OPCLICK, run, px, pz, n);
}
void
gamenet_rev245_2_send_move_minimapclick(
    struct GGame* g,
    int x,
    int z,
    int run,
    const uint8_t* cam,
    int cam_n)
{
    gamenet_core_send_move_minimapclick_v1(
        g, PKTOUT_LC245_2_MOVE_MINIMAPCLICK, x, z, run, cam, cam_n);
}

/* ── Chat ────────────────────────────────────────────────────────────────── */

void
gamenet_rev245_2_send_chat_setmode(
    struct GGame* g,
    int pub,
    int priv,
    int trade)
{
    gamenet_core_send_chat_setmode_v1(g, PKTOUT_LC245_2_CHAT_SETMODE, pub, priv, trade);
}
void
gamenet_rev245_2_send_message_public(
    struct GGame* g,
    int color,
    int effect,
    const uint8_t* wp,
    int n)
{
    gamenet_core_send_message_public_v1(g, PKTOUT_LC245_2_MESSAGE_PUBLIC, color, effect, wp, n);
}
void
gamenet_rev245_2_send_message_private(
    struct GGame* g,
    const char* user,
    const uint8_t* wp,
    int n)
{
    gamenet_core_send_message_private_v1(g, PKTOUT_LC245_2_MESSAGE_PRIVATE, user, wp, n);
}
void
gamenet_rev245_2_send_client_cheat(
    struct GGame* g,
    const char* line)
{
    gamenet_core_send_client_cheat_v1(g, PKTOUT_LC245_2_CLIENT_CHEAT, line);
}

/* ── Interface buttons ───────────────────────────────────────────────────── */

void
gamenet_rev245_2_send_inv_button(
    struct GGame* g,
    int which,
    int comp_id,
    int slot,
    int obj_id)
{
    static const int ops[5] = { PKTOUT_LC245_2_INV_BUTTON1,
                                PKTOUT_LC245_2_INV_BUTTON2,
                                PKTOUT_LC245_2_INV_BUTTON3,
                                PKTOUT_LC245_2_INV_BUTTON4,
                                PKTOUT_LC245_2_INV_BUTTON5 };
    int idx = which - 1;
    if( idx < 0 || idx > 4 )
        return;
    gamenet_core_send_inv_button_v1(g, ops[idx], comp_id, slot, obj_id);
}
void
gamenet_rev245_2_send_inv_button_d(
    struct GGame* g,
    int comp_id,
    int from_slot,
    int to_slot,
    int mode)
{
    gamenet_core_send_inv_button_d_v1(
        g, PKTOUT_LC245_2_INV_BUTTOND, comp_id, from_slot, to_slot, mode);
}
void
gamenet_rev245_2_send_if_button(
    struct GGame* g,
    int comp_id)
{
    gamenet_core_send_if_button_v1(g, PKTOUT_LC245_2_IF_BUTTON, comp_id);
}
void
gamenet_rev245_2_send_if_playerdesign(
    struct GGame* g,
    const uint8_t* payload,
    int n)
{
    gamenet_core_send_if_playerdesign_v1(g, PKTOUT_LC245_2_IF_PLAYERDESIGN, payload, n);
}
void
gamenet_rev245_2_send_tutorial_clickside(
    struct GGame* g,
    int tab)
{
    gamenet_core_send_tutorial_clickside_v1(g, PKTOUT_LC245_2_TUTORIAL_CLICKSIDE, tab);
}

/* ── World ops: held ─────────────────────────────────────────────────────── */

void
gamenet_rev245_2_send_opheld(
    struct GGame* g,
    int which,
    int obj_id,
    int slot,
    int comp_id)
{
    static const int ops[5] = { PKTOUT_LC245_2_OPHELD1,
                                PKTOUT_LC245_2_OPHELD2,
                                PKTOUT_LC245_2_OPHELD3,
                                PKTOUT_LC245_2_OPHELD4,
                                PKTOUT_LC245_2_OPHELD5 };
    int idx = which - 1;
    if( idx < 0 || idx > 4 )
        return;
    gamenet_core_send_opheld_v1(g, ops[idx], obj_id, slot, comp_id);
}
void
gamenet_rev245_2_send_opheldt(
    struct GGame* g,
    int obj_id,
    int slot,
    int comp_id,
    int target_comp_id)
{
    gamenet_core_send_opheldt_v1(g, PKTOUT_LC245_2_OPHELDT, obj_id, slot, comp_id, target_comp_id);
}
void
gamenet_rev245_2_send_opheldu(
    struct GGame* g,
    int obj_id,
    int slot,
    int comp_id,
    int src_obj_id,
    int src_slot,
    int src_comp_id)
{
    gamenet_core_send_opheldu_v1(
        g, PKTOUT_LC245_2_OPHELDU, obj_id, slot, comp_id, src_obj_id, src_slot, src_comp_id);
}

/* ── World ops: loc ──────────────────────────────────────────────────────── */

void
gamenet_rev245_2_send_oploc(
    struct GGame* g,
    int which,
    int x,
    int z,
    int loc_id,
    int ctrl)
{
    static const int ops[5] = { PKTOUT_LC245_2_OPLOC1,
                                PKTOUT_LC245_2_OPLOC2,
                                PKTOUT_LC245_2_OPLOC3,
                                PKTOUT_LC245_2_OPLOC4,
                                PKTOUT_LC245_2_OPLOC5 };
    int idx = which - 1;
    if( idx < 0 || idx > 4 )
        return;
    gamenet_core_send_oploc_v1(g, ops[idx], x, z, loc_id, ctrl);
}
void
gamenet_rev245_2_send_oploct(
    struct GGame* g,
    int x,
    int z,
    int loc_id,
    int ctrl)
{
    gamenet_core_send_oploct_v1(g, PKTOUT_LC245_2_OPLOCT, x, z, loc_id, ctrl);
}
void
gamenet_rev245_2_send_oplocu(
    struct GGame* g,
    int x,
    int z,
    int loc_id)
{
    gamenet_core_send_oplocu_v1(g, PKTOUT_LC245_2_OPLOCU, x, z, loc_id);
}
void
gamenet_rev245_2_send_oploct_target(
    struct GGame* g,
    int x,
    int z,
    int loc_id,
    int ctrl,
    int target_comp_id)
{
    gamenet_core_send_oploct_target_v1(
        g, PKTOUT_LC245_2_OPLOCT, x, z, loc_id, ctrl, target_comp_id);
}
void
gamenet_rev245_2_send_oplocu_use(
    struct GGame* g,
    int x,
    int z,
    int loc_id,
    int obj_comp_id,
    int slot,
    int sel_comp_id)
{
    gamenet_core_send_oplocu_use_v1(
        g, PKTOUT_LC245_2_OPLOCU, x, z, loc_id, obj_comp_id, slot, sel_comp_id);
}

/* ── World ops: npc ──────────────────────────────────────────────────────── */

void
gamenet_rev245_2_send_opnpc(
    struct GGame* g,
    int which,
    int npc_slot)
{
    static const int ops[5] = { PKTOUT_LC245_2_OPNPC1,
                                PKTOUT_LC245_2_OPNPC2,
                                PKTOUT_LC245_2_OPNPC3,
                                PKTOUT_LC245_2_OPNPC4,
                                PKTOUT_LC245_2_OPNPC5 };
    int idx = which - 1;
    if( idx < 0 || idx > 4 )
        return;
    gamenet_core_send_opnpc_v1(g, ops[idx], npc_slot);
}
void
gamenet_rev245_2_send_opnpct(
    struct GGame* g,
    int npc_slot)
{
    gamenet_core_send_opnpct_v1(g, PKTOUT_LC245_2_OPNPCT, npc_slot);
}
void
gamenet_rev245_2_send_opnpcu(
    struct GGame* g,
    int npc_slot)
{
    gamenet_core_send_opnpcu_v1(g, PKTOUT_LC245_2_OPNPCU, npc_slot);
}
void
gamenet_rev245_2_send_opnpct_target(
    struct GGame* g,
    int npc_slot,
    int target_comp_id)
{
    gamenet_core_send_opnpct_target_v1(g, PKTOUT_LC245_2_OPNPCT, npc_slot, target_comp_id);
}
void
gamenet_rev245_2_send_opnpcu_use(
    struct GGame* g,
    int npc_slot,
    int obj_comp_id,
    int slot,
    int sel_comp_id)
{
    gamenet_core_send_opnpcu_use_v1(
        g, PKTOUT_LC245_2_OPNPCU, npc_slot, obj_comp_id, slot, sel_comp_id);
}

/* ── World ops: obj ──────────────────────────────────────────────────────── */

void
gamenet_rev245_2_send_opobj(
    struct GGame* g,
    int which,
    int x,
    int z,
    int obj_id)
{
    static const int ops[5] = { PKTOUT_LC245_2_OPOBJ1,
                                PKTOUT_LC245_2_OPOBJ2,
                                PKTOUT_LC245_2_OPOBJ3,
                                PKTOUT_LC245_2_OPOBJ4,
                                PKTOUT_LC245_2_OPOBJ5 };
    int idx = which - 1;
    if( idx < 0 || idx > 4 )
        return;
    gamenet_core_send_opobj_v1(g, ops[idx], x, z, obj_id);
}
void
gamenet_rev245_2_send_opobjt(
    struct GGame* g,
    int x,
    int z,
    int obj_id)
{
    gamenet_core_send_opobjt_v1(g, PKTOUT_LC245_2_OPBJT, x, z, obj_id);
}
void
gamenet_rev245_2_send_opobju(
    struct GGame* g,
    int x,
    int z,
    int obj_id)
{
    gamenet_core_send_opobju_v1(g, PKTOUT_LC245_2_OPOBJU, x, z, obj_id);
}
void
gamenet_rev245_2_send_opobjt_spell(
    struct GGame* g,
    int x,
    int z,
    int obj_id,
    int target_comp_id)
{
    gamenet_core_send_opobjt_spell_v1(g, PKTOUT_LC245_2_OPBJT, x, z, obj_id, target_comp_id);
}
void
gamenet_rev245_2_send_opobju_use(
    struct GGame* g,
    int x,
    int z,
    int obj_id,
    int obj_comp_id,
    int slot,
    int sel_comp_id)
{
    gamenet_core_send_opobju_use_v1(
        g, PKTOUT_LC245_2_OPOBJU, x, z, obj_id, obj_comp_id, slot, sel_comp_id);
}

/* ── World ops: player ───────────────────────────────────────────────────── */

void
gamenet_rev245_2_send_opplayer(
    struct GGame* g,
    int which,
    int player_slot)
{
    static const int ops[5] = { PKTOUT_LC245_2_OPPLAYER1,
                                PKTOUT_LC245_2_OPPLAYER2,
                                PKTOUT_LC245_2_OPPLAYER3,
                                PKTOUT_LC245_2_OPPLAYER4,
                                PKTOUT_LC245_2_OPPLAYER5 };
    if( which < 1 || which > 5 )
        return;
    gamenet_core_send_opplayer_v1(g, ops[which - 1], player_slot);
}
void
gamenet_rev245_2_send_opplayert(
    struct GGame* g,
    int player_slot)
{
    gamenet_core_send_opplayert_v1(g, PKTOUT_LC245_2_OPPLAYERT, player_slot);
}
void
gamenet_rev245_2_send_opplayeru(
    struct GGame* g,
    int player_slot)
{
    gamenet_core_send_opplayeru_v1(g, PKTOUT_LC245_2_OPPLAYERU, player_slot);
}
void
gamenet_rev245_2_send_opplayert_target(
    struct GGame* g,
    int player_slot,
    int target_comp_id)
{
    gamenet_core_send_opplayert_target_v1(g, PKTOUT_LC245_2_OPPLAYERT, player_slot, target_comp_id);
}
void
gamenet_rev245_2_send_opplayeru_use(
    struct GGame* g,
    int player_slot,
    int obj_comp_id,
    int slot,
    int sel_comp_id)
{
    gamenet_core_send_opplayeru_use_v1(
        g, PKTOUT_LC245_2_OPPLAYERU, player_slot, obj_comp_id, slot, sel_comp_id);
}

/* ── Social ──────────────────────────────────────────────────────────────── */

void
gamenet_rev245_2_send_friend_add(
    struct GGame* g,
    int64_t userhash)
{
    gamenet_core_send_friend_add_v1(g, PKTOUT_LC245_2_FRIENDLIST_ADD, userhash);
}
void
gamenet_rev245_2_send_friend_del(
    struct GGame* g,
    int64_t userhash)
{
    gamenet_core_send_friend_del_v1(g, PKTOUT_LC245_2_FRIENDLIST_DEL, userhash);
}
void
gamenet_rev245_2_send_ignore_add(
    struct GGame* g,
    int64_t userhash)
{
    gamenet_core_send_ignore_add_v1(g, PKTOUT_LC245_2_IGNORELIST_ADD, userhash);
}
void
gamenet_rev245_2_send_ignore_del(
    struct GGame* g,
    int64_t userhash)
{
    gamenet_core_send_ignore_del_v1(g, PKTOUT_LC245_2_IGNORELIST_DEL, userhash);
}
void
gamenet_rev245_2_send_report_abuse(
    struct GGame* g,
    int64_t userhash,
    int type)
{
    gamenet_core_send_report_abuse_v1(g, PKTOUT_LC245_2_REPORT_ABUSE, userhash, type);
}
