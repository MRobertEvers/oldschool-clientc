#include "clientprot_rev245_2.h"

#include "osrs/core/clientprot_move_path.h"
#include "osrs/game.h"
#include "osrs/packetout.h"
#include "osrs/rscache/rsbuf.h"
#include "osrs/rscache/rsbuf_isaac.h"

#include <assert.h>
#include <string.h>

static int
clientprot_wire_tile_x(
    struct GGame* g,
    int scene_x)
{
    return (g && g->world) ? scene_x + g->world->_base_tile_x : scene_x;
}

static int
clientprot_wire_tile_z(
    struct GGame* g,
    int scene_z)
{
    return (g && g->world) ? scene_z + g->world->_base_tile_z : scene_z;
}

int
clientprot_rev245_2_emit(
    struct RSBuffer* b,
    struct GGame* g,
    enum ClientProtOpKind kind,
    void* args)
{
    switch( kind )
    {
        /* ── Keepalives ─────────────────────────────────────────────────── */

    case CLIENTPROT_OP_NO_TIMEOUT:
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_NO_TIMEOUT);
        return 0;

    case CLIENTPROT_OP_IDLE_TIMER:
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_IDLE_TIMER);
        return 0;

    case CLIENTPROT_OP_EVENT_TRACKING:
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_EVENT_TRACKING);
        return 0;

        /* ── Movement ───────────────────────────────────────────────────── */

    case CLIENTPROT_OP_MOVE_GAMECLICK:
    {
        struct CPArgs_MovGameClick* a = (struct CPArgs_MovGameClick*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_MOVE_GAMECLICK);
        int bx = g->world ? g->world->_base_tile_x : 0;
        int bz = g->world ? g->world->_base_tile_z : 0;
        clientprot_move_path_write_subpacket(
            b, a->run ? 1 : 0, bx, bz, a->path_x, a->path_z, a->path_len);
        return 0;
    }

    case CLIENTPROT_OP_MOVE_MINIMAPCLICK:
    {
        struct CPArgs_MovMinimapClick* a = (struct CPArgs_MovMinimapClick*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_MOVE_MINIMAPCLICK);
        int mark = (int)b->position;
        p1(b, 0);
        p1(b, a->run ? 1 : 0);
        p2(b, a->x);
        p2(b, a->z);
        if( a->cam && a->cam_n > 0 )
            rsbuf_pwrite(b, a->cam, a->cam_n);
        psize1(b, mark);
        return 0;
    }

    case CLIENTPROT_OP_MOVE_OPCLICK:
    {
        struct CPArgs_MovOpClick* a = (struct CPArgs_MovOpClick*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_MOVE_OPCLICK);
        int bx = g->world ? g->world->_base_tile_x : 0;
        int bz = g->world ? g->world->_base_tile_z : 0;
        clientprot_move_path_write_subpacket(
            b, a->run ? 1 : 0, bx, bz, a->path_x, a->path_z, a->path_len);
        return 0;
    }

        /* ── Chat ───────────────────────────────────────────────────────── */

    case CLIENTPROT_OP_CHAT_SETMODE:
    {
        struct CPArgs_ChatSetMode* a = (struct CPArgs_ChatSetMode*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_CHAT_SETMODE);
        p1(b, a->pub);
        p1(b, a->priv);
        p1(b, a->trade);
        return 0;
    }

    case CLIENTPROT_OP_MESSAGE_PUBLIC:
    {
        struct CPArgs_MessagePublic* a = (struct CPArgs_MessagePublic*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_MESSAGE_PUBLIC);
        int mark = (int)b->position;
        p2(b, 0); /* varu16 size placeholder */
        p1(b, a->color);
        p1(b, a->effect);
        if( a->wp && a->n > 0 )
            rsbuf_pwrite(b, a->wp, a->n);
        psize2(b, mark);
        return 0;
    }

    case CLIENTPROT_OP_MESSAGE_PRIVATE:
    {
        struct CPArgs_MessagePrivate* a = (struct CPArgs_MessagePrivate*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_MESSAGE_PRIVATE);
        int mark = (int)b->position;
        p2(b, 0);
        /* username as null-terminated string */
        if( a->user )
            pjstr(b, a->user, 0);
        else
            p1(b, 0);
        if( a->wp && a->n > 0 )
            rsbuf_pwrite(b, a->wp, a->n);
        psize2(b, mark);
        return 0;
    }

    case CLIENTPROT_OP_CLIENT_CHEAT:
    {
        struct CPArgs_ClientCheat* a = (struct CPArgs_ClientCheat*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_CLIENT_CHEAT);
        int mark = (int)b->position;
        p2(b, 0);
        if( a->line )
            pjstr(b, a->line, 0);
        else
            p1(b, 0);
        psize2(b, mark);
        return 0;
    }

        /* ── Interface buttons ──────────────────────────────────────────── */

    case CLIENTPROT_OP_INV_BUTTON:
    {
        struct CPArgs_InvButton* a = (struct CPArgs_InvButton*)args;
        static const int ops[5] = { PKTOUT_LC245_2_INV_BUTTON1,
                                    PKTOUT_LC245_2_INV_BUTTON2,
                                    PKTOUT_LC245_2_INV_BUTTON3,
                                    PKTOUT_LC245_2_INV_BUTTON4,
                                    PKTOUT_LC245_2_INV_BUTTON5 };
        int idx = a->which - 1;
        if( idx < 0 || idx > 4 )
            idx = 0;
        rsbuf_p1isaac(b, g->random_out, ops[idx]);
        p2(b, a->obj_id);
        p2(b, a->slot);
        p2(b, a->comp_id);
        return 0;
    }

    case CLIENTPROT_OP_INV_BUTTOND:
    {
        struct CPArgs_InvButtonD* a = (struct CPArgs_InvButtonD*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_INV_BUTTOND);
        p2(b, a->comp_id);
        p2(b, a->from_slot);
        p2(b, a->to_slot);
        p1(b, a->mode & 0xff);
        return 0;
    }

    case CLIENTPROT_OP_IF_BUTTON:
    {
        struct CPArgs_IfButton* a = (struct CPArgs_IfButton*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_IF_BUTTON);
        p2(b, a->comp_id);
        return 0;
    }

    case CLIENTPROT_OP_CLOSE_MODAL:
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_CLOSE_MODAL);
        return 0;

    case CLIENTPROT_OP_RESUME_PAUSEBUTTON:
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_RESUME_PAUSEBUTTON);
        return 0;

    case CLIENTPROT_OP_RESUME_P_COUNTDIALOG:
    {
        struct CPArgs_ResumeCountDlg* a = (struct CPArgs_ResumeCountDlg*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_RESUME_P_COUNTDIALOG);
        p4(b, a->value);
        return 0;
    }

        /* ── World interactions ──────────────────────────────────────────── */

    case CLIENTPROT_OP_OPHELD:
    {
        struct CPArgs_OpHeld* a = (struct CPArgs_OpHeld*)args;
        static const int ops[5] = { PKTOUT_LC245_2_OPHELD1,
                                    PKTOUT_LC245_2_OPHELD2,
                                    PKTOUT_LC245_2_OPHELD3,
                                    PKTOUT_LC245_2_OPHELD4,
                                    PKTOUT_LC245_2_OPHELD5 };
        int idx = a->which - 1;
        if( idx < 0 || idx > 4 )
            idx = 0;
        rsbuf_p1isaac(b, g->random_out, ops[idx]);
        p2(b, a->obj_id);
        p2(b, a->slot);
        p2(b, a->comp_id);
        return 0;
    }

    case CLIENTPROT_OP_OPHELDT:
    {
        struct CPArgs_OpHeldT* a = (struct CPArgs_OpHeldT*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_OPHELDT);
        p2(b, a->obj_id);
        p2(b, a->slot);
        p2(b, a->comp_id);
        p2(b, a->target_comp_id);
        return 0;
    }

    case CLIENTPROT_OP_OPHELDU:
    {
        struct CPArgs_OpHeldU* a = (struct CPArgs_OpHeldU*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_OPHELDU);
        p2(b, a->obj_id);
        p2(b, a->slot);
        p2(b, a->comp_id);
        p2(b, a->src_obj_id);
        p2(b, a->src_slot);
        p2(b, a->src_comp_id);
        return 0;
    }

    case CLIENTPROT_OP_OPLOC:
    {
        struct CPArgs_OpLoc* a = (struct CPArgs_OpLoc*)args;
        static const int ops[5] = { PKTOUT_LC245_2_OPLOC1,
                                    PKTOUT_LC245_2_OPLOC2,
                                    PKTOUT_LC245_2_OPLOC3,
                                    PKTOUT_LC245_2_OPLOC4,
                                    PKTOUT_LC245_2_OPLOC5 };
        int idx = a->which - 1;
        if( idx < 0 || idx > 4 )
            idx = 0;
        rsbuf_p1isaac(b, g->random_out, ops[idx]);
        p2(b, clientprot_wire_tile_x(g, a->x));
        p2(b, clientprot_wire_tile_z(g, a->z));
        p2(b, a->loc_id);
        p1(b, a->ctrl ? 1 : 0);
        return 0;
    }

    case CLIENTPROT_OP_OPLOCT:
    {
        struct CPArgs_OpLoc* a = (struct CPArgs_OpLoc*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_OPLOCT);
        p2(b, clientprot_wire_tile_x(g, a->x));
        p2(b, clientprot_wire_tile_z(g, a->z));
        p2(b, a->loc_id);
        p1(b, a->ctrl ? 1 : 0);
        return 0;
    }

    case CLIENTPROT_OP_OPLOCU:
    {
        struct CPArgs_OpLoc* a = (struct CPArgs_OpLoc*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_OPLOCU);
        p2(b, clientprot_wire_tile_x(g, a->x));
        p2(b, clientprot_wire_tile_z(g, a->z));
        p2(b, a->loc_id);
        return 0;
    }

    case CLIENTPROT_OP_OPNPC:
    {
        struct CPArgs_OpNpc* a = (struct CPArgs_OpNpc*)args;
        static const int ops[5] = { PKTOUT_LC245_2_OPNPC1,
                                    PKTOUT_LC245_2_OPNPC2,
                                    PKTOUT_LC245_2_OPNPC3,
                                    PKTOUT_LC245_2_OPNPC4,
                                    PKTOUT_LC245_2_OPNPC5 };
        int idx = a->which - 1;
        if( idx < 0 || idx > 4 )
            idx = 0;
        rsbuf_p1isaac(b, g->random_out, ops[idx]);
        p2(b, a->npc_slot);
        return 0;
    }

    case CLIENTPROT_OP_OPNPCT:
    {
        struct CPArgs_OpNpc* a = (struct CPArgs_OpNpc*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_OPNPCT);
        p2(b, a->npc_slot);
        return 0;
    }

    case CLIENTPROT_OP_OPNPCU:
    {
        struct CPArgs_OpNpc* a = (struct CPArgs_OpNpc*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_OPNPCU);
        p2(b, a->npc_slot);
        return 0;
    }

    case CLIENTPROT_OP_OPOBJ:
    {
        struct CPArgs_OpObj* a = (struct CPArgs_OpObj*)args;
        static const int ops[5] = { PKTOUT_LC245_2_OPOBJ1,
                                    PKTOUT_LC245_2_OPOBJ2,
                                    PKTOUT_LC245_2_OPOBJ3,
                                    PKTOUT_LC245_2_OPOBJ4,
                                    PKTOUT_LC245_2_OPOBJ5 };
        int idx = a->which - 1;
        if( idx < 0 || idx > 4 )
            idx = 0;
        rsbuf_p1isaac(b, g->random_out, ops[idx]);
        p2(b, clientprot_wire_tile_x(g, a->x));
        p2(b, clientprot_wire_tile_z(g, a->z));
        p2(b, a->obj_id);
        p1(b, a->ctrl ? 1 : 0);
        return 0;
    }

    case CLIENTPROT_OP_OPOBJT:
    {
        struct CPArgs_OpObj* a = (struct CPArgs_OpObj*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_OPBJT);
        p2(b, clientprot_wire_tile_x(g, a->x));
        p2(b, clientprot_wire_tile_z(g, a->z));
        p2(b, a->obj_id);
        p1(b, a->ctrl ? 1 : 0);
        return 0;
    }

    case CLIENTPROT_OP_OPOBJU:
    {
        struct CPArgs_OpObj* a = (struct CPArgs_OpObj*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_OPOBJU);
        p2(b, clientprot_wire_tile_x(g, a->x));
        p2(b, clientprot_wire_tile_z(g, a->z));
        p2(b, a->obj_id);
        return 0;
    }

    case CLIENTPROT_OP_OPPLAYER:
    {
        struct CPArgs_OpPlayer* a = (struct CPArgs_OpPlayer*)args;
        static const int ops[4] = { PKTOUT_LC245_2_OPPLAYER1,
                                    PKTOUT_LC245_2_OPPLAYER2,
                                    PKTOUT_LC245_2_OPPLAYER3,
                                    PKTOUT_LC245_2_OPPLAYER4 };
        if( a->which < 1 || a->which > 4 )
            return 0;
        rsbuf_p1isaac(b, g->random_out, ops[a->which - 1]);
        p2(b, a->player_slot);
        return 0;
    }

    case CLIENTPROT_OP_OPPLAYERT:
    {
        struct CPArgs_OpPlayer* a = (struct CPArgs_OpPlayer*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_OPPLAYERT);
        p2(b, a->player_slot);
        return 0;
    }

    case CLIENTPROT_OP_OPPLAYERU:
    {
        struct CPArgs_OpPlayer* a = (struct CPArgs_OpPlayer*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_OPPLAYERU);
        p2(b, a->player_slot);
        return 0;
    }

        /* ── Social ─────────────────────────────────────────────────────── */

    case CLIENTPROT_OP_FRIEND_ADD:
    {
        struct CPArgs_FriendIgnore* a = (struct CPArgs_FriendIgnore*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_FRIENDLIST_ADD);
        p8(b, a->userhash);
        return 0;
    }

    case CLIENTPROT_OP_FRIEND_DEL:
    {
        struct CPArgs_FriendIgnore* a = (struct CPArgs_FriendIgnore*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_FRIENDLIST_DEL);
        p8(b, a->userhash);
        return 0;
    }

    case CLIENTPROT_OP_IGNORE_ADD:
    {
        struct CPArgs_FriendIgnore* a = (struct CPArgs_FriendIgnore*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_IGNORELIST_ADD);
        p8(b, a->userhash);
        return 0;
    }

    case CLIENTPROT_OP_IGNORE_DEL:
    {
        struct CPArgs_FriendIgnore* a = (struct CPArgs_FriendIgnore*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_IGNORELIST_DEL);
        p8(b, a->userhash);
        return 0;
    }

        /* ── Misc ───────────────────────────────────────────────────────── */

    case CLIENTPROT_OP_TUTORIAL_CLICKSIDE:
    {
        struct CPArgs_TutorialClick* a = (struct CPArgs_TutorialClick*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_TUTORIAL_CLICKSIDE);
        p1(b, a->tab);
        return 0;
    }

    case CLIENTPROT_OP_IF_PLAYERDESIGN:
    {
        struct CPArgs_PlayerDesign* a = (struct CPArgs_PlayerDesign*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_IF_PLAYERDESIGN);
        if( a->payload && a->n > 0 )
            rsbuf_pwrite(b, a->payload, a->n);
        return 0;
    }

    case CLIENTPROT_OP_REPORT_ABUSE:
    {
        struct CPArgs_ReportAbuse* a = (struct CPArgs_ReportAbuse*)args;
        rsbuf_p1isaac(b, g->random_out, PKTOUT_LC245_2_REPORT_ABUSE);
        p8(b, a->userhash);
        p1(b, a->type);
        return 0;
    }

    default:
        assert(0 && "clientprot_rev245_2_emit: unknown ClientProtOpKind");
        return -1;
    }
}
