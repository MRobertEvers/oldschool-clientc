#ifndef OSRS_REVS_LC245_2_GAMENET_REV245_2_SEND_H
#define OSRS_REVS_LC245_2_GAMENET_REV245_2_SEND_H

/* gamenet_rev245_2_send — outbound net-revision gateway for LC245_2.
 *
 * Each function is a one-line shim that calls gamenet_core_send_xxx_v1().
 * This is the single source of truth for which v1 hydrator is used for each
 * op on this network revision.
 *
 * If a future revision uses a different wire format, add gamenet_rev254_send_xxx
 * that calls gamenet_core_send_xxx_v2() instead; no other files change.
 */

#include <stdint.h>

struct GGame;

/* ── Keepalives ──────────────────────────────────────────────────────────── */
void gamenet_rev245_2_send_no_timeout(struct GGame* g);
void gamenet_rev245_2_send_idle_timer(struct GGame* g);
void gamenet_rev245_2_send_event_tracking(struct GGame* g);
void gamenet_rev245_2_send_close_modal(struct GGame* g);
void gamenet_rev245_2_send_resume_pausebutton(struct GGame* g);
void gamenet_rev245_2_send_resume_p_countdialog(struct GGame* g, int value);

/* ── Movement ────────────────────────────────────────────────────────────── */
void gamenet_rev245_2_send_move_gameclick(
    struct GGame* g, int run, const int* px, const int* pz, int n);
void gamenet_rev245_2_send_move_opclick(
    struct GGame* g, int run, const int* px, const int* pz, int n);
void gamenet_rev245_2_send_move_minimapclick(
    struct GGame* g, int x, int z, int run, const uint8_t* cam, int cam_n);

/* ── Chat ────────────────────────────────────────────────────────────────── */
void gamenet_rev245_2_send_chat_setmode(struct GGame* g, int pub, int priv, int trade);
void gamenet_rev245_2_send_message_public(
    struct GGame* g, int color, int effect, const uint8_t* wp, int n);
void gamenet_rev245_2_send_message_private(
    struct GGame* g, const char* user, const uint8_t* wp, int n);
void gamenet_rev245_2_send_client_cheat(struct GGame* g, const char* line);

/* ── Interface buttons ───────────────────────────────────────────────────── */
void gamenet_rev245_2_send_inv_button(
    struct GGame* g, int which, int comp_id, int slot, int obj_id);
void gamenet_rev245_2_send_inv_button_d(
    struct GGame* g, int comp_id, int from_slot, int to_slot, int mode);
void gamenet_rev245_2_send_if_button(struct GGame* g, int comp_id);
void gamenet_rev245_2_send_if_playerdesign(struct GGame* g, const uint8_t* payload, int n);
void gamenet_rev245_2_send_tutorial_clickside(struct GGame* g, int tab);

/* ── World ops: held ─────────────────────────────────────────────────────── */
void gamenet_rev245_2_send_opheld(
    struct GGame* g, int which, int obj_id, int slot, int comp_id);
void gamenet_rev245_2_send_opheldt(
    struct GGame* g, int obj_id, int slot, int comp_id, int target_comp_id);
void gamenet_rev245_2_send_opheldu(
    struct GGame* g,
    int obj_id, int slot, int comp_id,
    int src_obj_id, int src_slot, int src_comp_id);

/* ── World ops: loc ──────────────────────────────────────────────────────── */
void gamenet_rev245_2_send_oploc(
    struct GGame* g, int which, int x, int z, int loc_id, int ctrl);
void gamenet_rev245_2_send_oploct(struct GGame* g, int x, int z, int loc_id, int ctrl);
void gamenet_rev245_2_send_oplocu(struct GGame* g, int x, int z, int loc_id);

/* ── World ops: npc ──────────────────────────────────────────────────────── */
void gamenet_rev245_2_send_opnpc(struct GGame* g, int which, int npc_slot);
void gamenet_rev245_2_send_opnpct(struct GGame* g, int npc_slot);
void gamenet_rev245_2_send_opnpcu(struct GGame* g, int npc_slot);

/* ── World ops: obj ──────────────────────────────────────────────────────── */
void gamenet_rev245_2_send_opobj(
    struct GGame* g, int which, int x, int z, int obj_id, int ctrl);
void gamenet_rev245_2_send_opobjt(struct GGame* g, int x, int z, int obj_id, int ctrl);
void gamenet_rev245_2_send_opobju(struct GGame* g, int x, int z, int obj_id);

/* ── World ops: player ───────────────────────────────────────────────────── */
void gamenet_rev245_2_send_opplayer(struct GGame* g, int which, int player_slot);
void gamenet_rev245_2_send_opplayert(struct GGame* g, int player_slot);
void gamenet_rev245_2_send_opplayeru(struct GGame* g, int player_slot);

/* ── Social ──────────────────────────────────────────────────────────────── */
void gamenet_rev245_2_send_friend_add(struct GGame* g, int64_t userhash);
void gamenet_rev245_2_send_friend_del(struct GGame* g, int64_t userhash);
void gamenet_rev245_2_send_ignore_add(struct GGame* g, int64_t userhash);
void gamenet_rev245_2_send_ignore_del(struct GGame* g, int64_t userhash);
void gamenet_rev245_2_send_report_abuse(struct GGame* g, int64_t userhash, int type);

#endif /* OSRS_REVS_LC245_2_GAMENET_REV245_2_SEND_H */
