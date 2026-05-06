#ifndef OSRS_GAMENET_SEND_H
#define OSRS_GAMENET_SEND_H

/* gamenet_send — outbound intent layer.
 *
 * Callers use gamenet_send_xxx() directly; no knowledge of opcodes, wire
 * formats, or revision is required.  Each function dispatches through:
 *
 *   gamenet_send_xxx(game, ...)
 *     -> gamenet_rev245_2_send_xxx(game, ...)   [net-rev gateway]
 *     -> gamenet_core_send_xxx_v1(game, ...)    [hydrator: GGame -> wire struct]
 *     -> clientprot_send_xxx_v1(buf, isaac, &w) [pure serializer: struct -> bytes]
 */

#include "osrs/core/revision.h"
#include "osrs/revs/lc245_2/gamenet_rev245_2_send.h"

#include <stdint.h>
#include <assert.h>

struct GGame;

/* Dispatch helper — used inline functions below call into per-rev gateway. */
static inline void
gamenet_send__dispatch_lc245_2_only_void(void (*fn_lc245_2)(struct GGame*), struct GGame* g)
{
    switch( revision_active()->kind )
    {
    case REVISION_KIND_LC245_2: fn_lc245_2(g); break;
    default: break;
    }
}

/* ── Keepalives ──────────────────────────────────────────────────────────── */

static inline void gamenet_send_no_timeout(struct GGame* g)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_no_timeout(g); break;
    default: break;
    }
}

static inline void gamenet_send_idle_timer(struct GGame* g)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_idle_timer(g); break;
    default: break;
    }
}

static inline void gamenet_send_event_tracking(struct GGame* g)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_event_tracking(g); break;
    default: break;
    }
}

static inline void gamenet_send_close_modal(struct GGame* g)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_close_modal(g); break;
    default: break;
    }
}

static inline void gamenet_send_resume_pausebutton(struct GGame* g)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_resume_pausebutton(g); break;
    default: break;
    }
}

static inline void gamenet_send_resume_p_countdialog(struct GGame* g, int value)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_resume_p_countdialog(g, value); break;
    default: break;
    }
}

/* ── Movement ────────────────────────────────────────────────────────────── */

void
gamenet_send_move_gameclick(
    struct GGame* g,
    int           run,
    const int*    path_x,
    const int*    path_z,
    int           path_len);

void
gamenet_send_move_opclick(
    struct GGame* g,
    int           run,
    const int*    path_x,
    const int*    path_z,
    int           path_len);

static inline void
gamenet_send_move_minimapclick(
    struct GGame*   g,
    int             x,
    int             z,
    int             run,
    const uint8_t*  cam,
    int             cam_n)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2:
        gamenet_rev245_2_send_move_minimapclick(g, x, z, run, cam, cam_n); break;
    default: break;
    }
}

/* ── Chat ────────────────────────────────────────────────────────────────── */

static inline void gamenet_send_chat_setmode(struct GGame* g, int pub, int priv, int trade)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_chat_setmode(g, pub, priv, trade); break;
    default: break;
    }
}

static inline void
gamenet_send_message_public(struct GGame* g, int color, int effect, const uint8_t* wp, int n)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2:
        gamenet_rev245_2_send_message_public(g, color, effect, wp, n); break;
    default: break;
    }
}

static inline void
gamenet_send_message_private(struct GGame* g, const char* user, const uint8_t* wp, int n)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2:
        gamenet_rev245_2_send_message_private(g, user, wp, n); break;
    default: break;
    }
}

static inline void gamenet_send_client_cheat(struct GGame* g, const char* line)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_client_cheat(g, line); break;
    default: break;
    }
}

/* ── Interface buttons ───────────────────────────────────────────────────── */

static inline void
gamenet_send_inv_button(struct GGame* g, int which, int comp_id, int slot, int obj_id)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2:
        gamenet_rev245_2_send_inv_button(g, which, comp_id, slot, obj_id); break;
    default: break;
    }
}

static inline void
gamenet_send_inv_button_d(struct GGame* g, int comp_id, int from_slot, int to_slot, int mode)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2:
        gamenet_rev245_2_send_inv_button_d(g, comp_id, from_slot, to_slot, mode); break;
    default: break;
    }
}

static inline void gamenet_send_if_button(struct GGame* g, int comp_id)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_if_button(g, comp_id); break;
    default: break;
    }
}

static inline void gamenet_send_if_playerdesign(struct GGame* g, const uint8_t* payload, int n)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_if_playerdesign(g, payload, n); break;
    default: break;
    }
}

static inline void gamenet_send_tutorial_clickside(struct GGame* g, int tab)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_tutorial_clickside(g, tab); break;
    default: break;
    }
}

/* ── World ops: held ─────────────────────────────────────────────────────── */

static inline void
gamenet_send_opheld(struct GGame* g, int which, int obj_id, int slot, int comp_id)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2:
        gamenet_rev245_2_send_opheld(g, which, obj_id, slot, comp_id); break;
    default: break;
    }
}

static inline void
gamenet_send_opheldt(struct GGame* g, int obj_id, int slot, int comp_id, int target_comp_id)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2:
        gamenet_rev245_2_send_opheldt(g, obj_id, slot, comp_id, target_comp_id); break;
    default: break;
    }
}

static inline void
gamenet_send_opheldu(
    struct GGame* g,
    int obj_id, int slot, int comp_id,
    int src_obj_id, int src_slot, int src_comp_id)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2:
        gamenet_rev245_2_send_opheldu(g, obj_id, slot, comp_id,
                                      src_obj_id, src_slot, src_comp_id); break;
    default: break;
    }
}

/* ── World ops: loc ──────────────────────────────────────────────────────── */

static inline void
gamenet_send_oploc(struct GGame* g, int which, int x, int z, int loc_id, int ctrl)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2:
        gamenet_rev245_2_send_oploc(g, which, x, z, loc_id, ctrl); break;
    default: break;
    }
}

static inline void gamenet_send_oploct(struct GGame* g, int x, int z, int loc_id, int ctrl)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_oploct(g, x, z, loc_id, ctrl); break;
    default: break;
    }
}

static inline void gamenet_send_oplocu(struct GGame* g, int x, int z, int loc_id)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_oplocu(g, x, z, loc_id); break;
    default: break;
    }
}

/* ── World ops: npc ──────────────────────────────────────────────────────── */

static inline void gamenet_send_opnpc(struct GGame* g, int which, int npc_slot)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_opnpc(g, which, npc_slot); break;
    default: break;
    }
}

static inline void gamenet_send_opnpct(struct GGame* g, int npc_slot)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_opnpct(g, npc_slot); break;
    default: break;
    }
}

static inline void gamenet_send_opnpcu(struct GGame* g, int npc_slot)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_opnpcu(g, npc_slot); break;
    default: break;
    }
}

/* ── World ops: obj ──────────────────────────────────────────────────────── */

static inline void
gamenet_send_opobj(struct GGame* g, int which, int x, int z, int obj_id, int ctrl)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2:
        gamenet_rev245_2_send_opobj(g, which, x, z, obj_id, ctrl); break;
    default: break;
    }
}

static inline void gamenet_send_opobjt(struct GGame* g, int x, int z, int obj_id, int ctrl)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_opobjt(g, x, z, obj_id, ctrl); break;
    default: break;
    }
}

static inline void gamenet_send_opobju(struct GGame* g, int x, int z, int obj_id)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_opobju(g, x, z, obj_id); break;
    default: break;
    }
}

/* ── World ops: player ───────────────────────────────────────────────────── */

static inline void gamenet_send_opplayer(struct GGame* g, int which, int player_slot)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_opplayer(g, which, player_slot); break;
    default: break;
    }
}

static inline void gamenet_send_opplayert(struct GGame* g, int player_slot)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_opplayert(g, player_slot); break;
    default: break;
    }
}

static inline void gamenet_send_opplayeru(struct GGame* g, int player_slot)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_opplayeru(g, player_slot); break;
    default: break;
    }
}

/* ── Social ──────────────────────────────────────────────────────────────── */

static inline void gamenet_send_friend_add(struct GGame* g, int64_t userhash)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_friend_add(g, userhash); break;
    default: break;
    }
}

static inline void gamenet_send_friend_del(struct GGame* g, int64_t userhash)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_friend_del(g, userhash); break;
    default: break;
    }
}

static inline void gamenet_send_ignore_add(struct GGame* g, int64_t userhash)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_ignore_add(g, userhash); break;
    default: break;
    }
}

static inline void gamenet_send_ignore_del(struct GGame* g, int64_t userhash)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_ignore_del(g, userhash); break;
    default: break;
    }
}

static inline void gamenet_send_report_abuse(struct GGame* g, int64_t userhash, int type)
{
    switch( revision_active()->kind ) {
    case REVISION_KIND_LC245_2: gamenet_rev245_2_send_report_abuse(g, userhash, type); break;
    default: break;
    }
}

#endif /* OSRS_GAMENET_SEND_H */
