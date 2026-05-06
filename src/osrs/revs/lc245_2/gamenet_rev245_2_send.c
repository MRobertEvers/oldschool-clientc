#include "gamenet_rev245_2_send.h"

#include "osrs/core/gamenet_core_send.h"

/* Each function is a one-line shim: bind the network revision to the v1 hydrator. */

void gamenet_rev245_2_send_no_timeout(struct GGame* g) {
    gamenet_core_send_no_timeout_v1(g);
}
void gamenet_rev245_2_send_idle_timer(struct GGame* g) {
    gamenet_core_send_idle_timer_v1(g);
}
void gamenet_rev245_2_send_event_tracking(struct GGame* g) {
    gamenet_core_send_event_tracking_v1(g);
}
void gamenet_rev245_2_send_close_modal(struct GGame* g) {
    gamenet_core_send_close_modal_v1(g);
}
void gamenet_rev245_2_send_resume_pausebutton(struct GGame* g) {
    gamenet_core_send_resume_pausebutton_v1(g);
}
void gamenet_rev245_2_send_resume_p_countdialog(struct GGame* g, int value) {
    gamenet_core_send_resume_p_countdialog_v1(g, value);
}

void gamenet_rev245_2_send_move_gameclick(
    struct GGame* g, int run, const int* px, const int* pz, int n) {
    gamenet_core_send_move_gameclick_v1(g, run, px, pz, n);
}
void gamenet_rev245_2_send_move_opclick(
    struct GGame* g, int run, const int* px, const int* pz, int n) {
    gamenet_core_send_move_opclick_v1(g, run, px, pz, n);
}
void gamenet_rev245_2_send_move_minimapclick(
    struct GGame* g, int x, int z, int run, const uint8_t* cam, int cam_n) {
    gamenet_core_send_move_minimapclick_v1(g, x, z, run, cam, cam_n);
}

void gamenet_rev245_2_send_chat_setmode(struct GGame* g, int pub, int priv, int trade) {
    gamenet_core_send_chat_setmode_v1(g, pub, priv, trade);
}
void gamenet_rev245_2_send_message_public(
    struct GGame* g, int color, int effect, const uint8_t* wp, int n) {
    gamenet_core_send_message_public_v1(g, color, effect, wp, n);
}
void gamenet_rev245_2_send_message_private(
    struct GGame* g, const char* user, const uint8_t* wp, int n) {
    gamenet_core_send_message_private_v1(g, user, wp, n);
}
void gamenet_rev245_2_send_client_cheat(struct GGame* g, const char* line) {
    gamenet_core_send_client_cheat_v1(g, line);
}

void gamenet_rev245_2_send_inv_button(
    struct GGame* g, int which, int comp_id, int slot, int obj_id) {
    gamenet_core_send_inv_button_v1(g, which, comp_id, slot, obj_id);
}
void gamenet_rev245_2_send_inv_button_d(
    struct GGame* g, int comp_id, int from_slot, int to_slot, int mode) {
    gamenet_core_send_inv_button_d_v1(g, comp_id, from_slot, to_slot, mode);
}
void gamenet_rev245_2_send_if_button(struct GGame* g, int comp_id) {
    gamenet_core_send_if_button_v1(g, comp_id);
}
void gamenet_rev245_2_send_if_playerdesign(struct GGame* g, const uint8_t* payload, int n) {
    gamenet_core_send_if_playerdesign_v1(g, payload, n);
}
void gamenet_rev245_2_send_tutorial_clickside(struct GGame* g, int tab) {
    gamenet_core_send_tutorial_clickside_v1(g, tab);
}

void gamenet_rev245_2_send_opheld(
    struct GGame* g, int which, int obj_id, int slot, int comp_id) {
    gamenet_core_send_opheld_v1(g, which, obj_id, slot, comp_id);
}
void gamenet_rev245_2_send_opheldt(
    struct GGame* g, int obj_id, int slot, int comp_id, int target_comp_id) {
    gamenet_core_send_opheldt_v1(g, obj_id, slot, comp_id, target_comp_id);
}
void gamenet_rev245_2_send_opheldu(
    struct GGame* g,
    int obj_id, int slot, int comp_id,
    int src_obj_id, int src_slot, int src_comp_id) {
    gamenet_core_send_opheldu_v1(g, obj_id, slot, comp_id, src_obj_id, src_slot, src_comp_id);
}

void gamenet_rev245_2_send_oploc(
    struct GGame* g, int which, int x, int z, int loc_id, int ctrl) {
    gamenet_core_send_oploc_v1(g, which, x, z, loc_id, ctrl);
}
void gamenet_rev245_2_send_oploct(struct GGame* g, int x, int z, int loc_id, int ctrl) {
    gamenet_core_send_oploct_v1(g, x, z, loc_id, ctrl);
}
void gamenet_rev245_2_send_oplocu(struct GGame* g, int x, int z, int loc_id) {
    gamenet_core_send_oplocu_v1(g, x, z, loc_id);
}

void gamenet_rev245_2_send_opnpc(struct GGame* g, int which, int npc_slot) {
    gamenet_core_send_opnpc_v1(g, which, npc_slot);
}
void gamenet_rev245_2_send_opnpct(struct GGame* g, int npc_slot) {
    gamenet_core_send_opnpct_v1(g, npc_slot);
}
void gamenet_rev245_2_send_opnpcu(struct GGame* g, int npc_slot) {
    gamenet_core_send_opnpcu_v1(g, npc_slot);
}

void gamenet_rev245_2_send_opobj(
    struct GGame* g, int which, int x, int z, int obj_id, int ctrl) {
    gamenet_core_send_opobj_v1(g, which, x, z, obj_id, ctrl);
}
void gamenet_rev245_2_send_opobjt(struct GGame* g, int x, int z, int obj_id, int ctrl) {
    gamenet_core_send_opobjt_v1(g, x, z, obj_id, ctrl);
}
void gamenet_rev245_2_send_opobju(struct GGame* g, int x, int z, int obj_id) {
    gamenet_core_send_opobju_v1(g, x, z, obj_id);
}

void gamenet_rev245_2_send_opplayer(struct GGame* g, int which, int player_slot) {
    gamenet_core_send_opplayer_v1(g, which, player_slot);
}
void gamenet_rev245_2_send_opplayert(struct GGame* g, int player_slot) {
    gamenet_core_send_opplayert_v1(g, player_slot);
}
void gamenet_rev245_2_send_opplayeru(struct GGame* g, int player_slot) {
    gamenet_core_send_opplayeru_v1(g, player_slot);
}

void gamenet_rev245_2_send_friend_add(struct GGame* g, int64_t userhash) {
    gamenet_core_send_friend_add_v1(g, userhash);
}
void gamenet_rev245_2_send_friend_del(struct GGame* g, int64_t userhash) {
    gamenet_core_send_friend_del_v1(g, userhash);
}
void gamenet_rev245_2_send_ignore_add(struct GGame* g, int64_t userhash) {
    gamenet_core_send_ignore_add_v1(g, userhash);
}
void gamenet_rev245_2_send_ignore_del(struct GGame* g, int64_t userhash) {
    gamenet_core_send_ignore_del_v1(g, userhash);
}
void gamenet_rev245_2_send_report_abuse(struct GGame* g, int64_t userhash, int type) {
    gamenet_core_send_report_abuse_v1(g, userhash, type);
}
