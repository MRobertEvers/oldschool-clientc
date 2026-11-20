#include "torirsserver/wire/osrs239/osrs239_inbound.h"

#include "net/rev/pktnames.h"
#include "torirsserver/wire/core/wire_in.h"

#include "packets/friendchat_setrank.h"
#include "packets/opnpc6.h"
#include "packets/opworldentity1.h"
#include "packets/opworldentity2.h"
#include "packets/opworldentity3.h"
#include "packets/opworldentity4.h"
#include "packets/opworldentity5.h"
#include "packets/opworldentity6.h"
#include "packets/oploc6.h"
#include "packets/opnpc1_v2.h"
#include "packets/opnpc2_v2.h"
#include "packets/opnpc3_v2.h"
#include "packets/opnpc4_v2.h"
#include "packets/opnpc5_v2.h"
#include "packets/oploc1_v2.h"
#include "packets/oploc2_v2.h"
#include "packets/oploc3_v2.h"
#include "packets/oploc4_v2.h"
#include "packets/oploc5_v2.h"
#include "packets/opobj1_v2.h"
#include "packets/opobj2_v2.h"
#include "packets/opobj3_v2.h"
#include "packets/opobj4_v2.h"
#include "packets/opobj5_v2.h"
#include "packets/opplayer1.h"
#include "packets/opplayer2.h"
#include "packets/opplayer3.h"
#include "packets/opplayer4.h"
#include "packets/opplayer5.h"
#include "packets/opnpct.h"
#include "packets/oploct.h"
#include "packets/opobjt.h"
#include "packets/opplayert.h"
#include "packets/if_buttont.h"
#include "packets/if_buttond.h"
#include "packets/move_gameclick.h"
#include "packets/move_minimapclick.h"
#include "packets/resume_pausebutton.h"
#include "packets/clickworldmap.h"
#include "packets/set_heading.h"
#include "packets/if_script_trigger.h"

#include <assert.h>
#include "rsprot_buf.h"

/*
 * Revision 239's client -> server bodies, into the canonical ones the world
 * handlers read (wire/core/wire_in.h). Each packet is decoded with rsprot's
 * revision-239 codec -- the function the client writes it with -- so this file
 * says only which canonical body each one becomes. See osrs239_inbound.h for
 * why inbound needs a translation at all.
 *
 * OPPLAYER6..8 are not taken at 239: the 239 table names them, but no
 * handler has been asked to accept them from a 239 client yet.
 */

/* Decode the payload with one codec into `M`; drop the packet unless it read
 * cleanly and exactly. */
#define DECODE(CODEC, M)                                  \
    do                                                    \
    {                                                     \
        CODEC(wire_in_reader(&r, in, in_len), &(M));      \
        if( !wire_in_read_all(&r) )                       \
            return PKTOUT_NAME_NONE;                      \
    } while( 0 )

int
osrs239_inbound_translate(
    int wire_opcode,
    int name,
    uint8_t const* in,
    int in_len,
    uint8_t* out,
    int out_cap,
    int* out_len)
{
    struct WireInReader r;
    struct RsprotBuf w;

    (void)wire_opcode;
    assert(in || in_len == 0);
    assert(out);
    assert(out_len);
    *out_len = 0;
    rsprot_buf_wrap(&w, out, (size_t)out_cap);

    switch( name )
    {
    /* Examine (OPNPC6 / OPLOC6): the npc's type, the loc's id, passed on as a
     * plain g4 for the world's examine reply (MESSAGE_GAME 28 / 29). */
    /* A boat hull's ops (OPWORLDENTITY1..5): its world-entity index, passed
     * on as a g4 for the world's vessel op; OPWORLDENTITY6 its examine, by
     * type (RSProx index 4128, rev 238). */
    case PKTOUT_NAME_OPWORLDENTITY1:
    {
        MsgOpWorldEntity1 m = {0};
        DECODE(packet_opworldentity1_osrs239_in, m);
        rsprot_p4_1234(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPWORLDENTITY2:
    {
        MsgOpWorldEntity2 m = {0};
        DECODE(packet_opworldentity2_osrs239_in, m);
        rsprot_p4_1234(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPWORLDENTITY3:
    {
        MsgOpWorldEntity3 m = {0};
        DECODE(packet_opworldentity3_osrs239_in, m);
        rsprot_p4_1234(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPWORLDENTITY4:
    {
        MsgOpWorldEntity4 m = {0};
        DECODE(packet_opworldentity4_osrs239_in, m);
        rsprot_p4_1234(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPWORLDENTITY5:
    {
        MsgOpWorldEntity5 m = {0};
        DECODE(packet_opworldentity5_osrs239_in, m);
        rsprot_p4_1234(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPWORLDENTITY6:
    {
        MsgOpWorldEntity6 m = {0};
        DECODE(packet_opworldentity6_osrs239_in, m);
        rsprot_p4_1234(&w, m.id);
        break;
    }
    case PKTOUT_NAME_OPNPC6:
    {
        MsgOpNpc6 m = {0};
        DECODE(packet_opnpc6_osrs239_in, m);
        rsprot_p4_1234(&w, m.id);
        break;
    }
    case PKTOUT_NAME_OPLOC6:
    {
        MsgOpLoc6 m = {0};
        DECODE(packet_oploc6_osrs239_in, m);
        rsprot_p4_1234(&w, m.id);
        break;
    }
    case PKTOUT_NAME_OPNPC1:
    {
        MsgOpNpc1V2 m = {0};
        DECODE(packet_opnpc1_v2_osrs239_in, m);
        wire_in_index(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPNPC2:
    {
        MsgOpNpc2V2 m = {0};
        DECODE(packet_opnpc2_v2_osrs239_in, m);
        wire_in_index(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPNPC3:
    {
        MsgOpNpc3V2 m = {0};
        DECODE(packet_opnpc3_v2_osrs239_in, m);
        wire_in_index(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPNPC4:
    {
        MsgOpNpc4V2 m = {0};
        DECODE(packet_opnpc4_v2_osrs239_in, m);
        wire_in_index(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPNPC5:
    {
        MsgOpNpc5V2 m = {0};
        DECODE(packet_opnpc5_v2_osrs239_in, m);
        wire_in_index(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPLOC1:
    {
        MsgOpLoc1V2 m = {0};
        DECODE(packet_oploc1_v2_osrs239_in, m);
        wire_in_tile(&w, m.x, m.z, m.id);
        break;
    }
    case PKTOUT_NAME_OPLOC2:
    {
        MsgOpLoc2V2 m = {0};
        DECODE(packet_oploc2_v2_osrs239_in, m);
        wire_in_tile(&w, m.x, m.z, m.id);
        break;
    }
    case PKTOUT_NAME_OPLOC3:
    {
        MsgOpLoc3V2 m = {0};
        DECODE(packet_oploc3_v2_osrs239_in, m);
        wire_in_tile(&w, m.x, m.z, m.id);
        break;
    }
    case PKTOUT_NAME_OPLOC4:
    {
        MsgOpLoc4V2 m = {0};
        DECODE(packet_oploc4_v2_osrs239_in, m);
        wire_in_tile(&w, m.x, m.z, m.id);
        break;
    }
    case PKTOUT_NAME_OPLOC5:
    {
        MsgOpLoc5V2 m = {0};
        DECODE(packet_oploc5_v2_osrs239_in, m);
        wire_in_tile(&w, m.x, m.z, m.id);
        break;
    }
    case PKTOUT_NAME_OPOBJ1:
    {
        MsgOpObj1V2 m = {0};
        DECODE(packet_opobj1_v2_osrs239_in, m);
        wire_in_tile(&w, m.x, m.z, m.id);
        break;
    }
    case PKTOUT_NAME_OPOBJ2:
    {
        MsgOpObj2V2 m = {0};
        DECODE(packet_opobj2_v2_osrs239_in, m);
        wire_in_tile(&w, m.x, m.z, m.id);
        break;
    }
    case PKTOUT_NAME_OPOBJ3:
    {
        MsgOpObj3V2 m = {0};
        DECODE(packet_opobj3_v2_osrs239_in, m);
        wire_in_tile(&w, m.x, m.z, m.id);
        break;
    }
    case PKTOUT_NAME_OPOBJ4:
    {
        MsgOpObj4V2 m = {0};
        DECODE(packet_opobj4_v2_osrs239_in, m);
        wire_in_tile(&w, m.x, m.z, m.id);
        break;
    }
    case PKTOUT_NAME_OPOBJ5:
    {
        MsgOpObj5V2 m = {0};
        DECODE(packet_opobj5_v2_osrs239_in, m);
        wire_in_tile(&w, m.x, m.z, m.id);
        break;
    }
    case PKTOUT_NAME_OPPLAYER1:
    {
        MsgOpPlayer1 m = {0};
        DECODE(packet_opplayer1_osrs239_in, m);
        wire_in_index(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPPLAYER2:
    {
        MsgOpPlayer2 m = {0};
        DECODE(packet_opplayer2_osrs239_in, m);
        wire_in_index(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPPLAYER3:
    {
        MsgOpPlayer3 m = {0};
        DECODE(packet_opplayer3_osrs239_in, m);
        wire_in_index(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPPLAYER4:
    {
        MsgOpPlayer4 m = {0};
        DECODE(packet_opplayer4_osrs239_in, m);
        wire_in_index(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPPLAYER5:
    {
        MsgOpPlayer5 m = {0};
        DECODE(packet_opplayer5_osrs239_in, m);
        wire_in_index(&w, m.index);
        break;
    }
    case PKTOUT_NAME_OPNPCT:
    {
        MsgOpNpcT m = {0};
        DECODE(packet_opnpct_osrs239_in, m);
        name = wire_in_index_target(&w, PKTOUT_NAME_OPNPCT, PKTOUT_NAME_OPNPCU, m.index,
                                    m.selected_obj, m.selected_sub, m.selected_combined_id);
        break;
    }
    case PKTOUT_NAME_OPLOCT:
    {
        MsgOpLocT m = {0};
        DECODE(packet_oploct_osrs239_in, m);
        name = wire_in_tile_target(&w, PKTOUT_NAME_OPLOCT, PKTOUT_NAME_OPLOCU, m.x, m.z, m.id,
                                   m.selected_obj, m.selected_sub, m.selected_combined_id);
        break;
    }
    case PKTOUT_NAME_OPOBJT:
    {
        MsgOpObjT m = {0};
        DECODE(packet_opobjt_osrs239_in, m);
        name = wire_in_tile_target(&w, PKTOUT_NAME_OPOBJT, PKTOUT_NAME_OPOBJU, m.x, m.z, m.id,
                                   m.selected_obj, m.selected_sub, m.selected_combined_id);
        break;
    }
    case PKTOUT_NAME_OPPLAYERT:
    {
        MsgOpPlayerT m = {0};
        DECODE(packet_opplayert_osrs239_in, m);
        name = wire_in_index_target(&w, PKTOUT_NAME_OPPLAYERT, PKTOUT_NAME_OPPLAYERU, m.index,
                                    m.selected_obj, m.selected_sub, m.selected_combined_id);
        break;
    }
    case PKTOUT_NAME_IF_BUTTONT:
    {
        MsgIfButtonT m = {0};
        DECODE(packet_if_buttont_osrs239_in, m);
        name = wire_in_if_buttont(&w, m.target_combined_id, m.target_obj, m.target_sub,
                                  m.selected_combined_id, m.selected_obj, m.selected_sub);
        break;
    }
    case PKTOUT_NAME_INV_BUTTOND:
    {
        MsgIfButtonD m = {0};
        DECODE(packet_if_buttond_osrs239_in, m);
        wire_in_inv_buttond(&w, m.selected_combined_id, m.selected_obj, m.selected_sub,
                            m.target_combined_id, m.target_obj, m.target_sub);
        break;
    }
    case PKTOUT_NAME_MOVE_GAMECLICK:
    {
        MsgMoveGameClick m = {0};
        DECODE(packet_move_gameclick_osrs239_in, m);
        wire_in_move(&w, m.key_combination, m.x, m.z);
        break;
    }
    case PKTOUT_NAME_MOVE_MINIMAPCLICK:
    {
        MsgMoveMinimapClick m = {0};
        DECODE(packet_move_minimapclick_osrs239_in, m);
        wire_in_move(&w, m.key_combination, m.x, m.z);
        name = PKTOUT_NAME_MOVE_GAMECLICK;
        break;
    }
    case PKTOUT_NAME_RESUME_PAUSEBUTTON:
    {
        MsgResumePauseButton m = {0};
        DECODE(packet_resume_pausebutton_osrs239_in, m);
        wire_in_resume_pausebutton(&w, m.combined_id, m.sub);
        break;
    }
    case PKTOUT_NAME_CLICK_WORLD_MAP:
    {
        MsgClickworldmap m = {0};
        DECODE(packet_clickworldmap_osrs239_in, m);
        wire_in_clickworldmap(&w, m.packed);
        break;
    }
    /* FRIENDCHAT_SETRANK: the name and the rank in this revision's order and
     * transform (RSProt FriendChatSetRank), normalised to p1 rank + name. */
    case PKTOUT_NAME_FRIENDCHAT_SETRANK:
    {
        MsgFriendChatSetRank m = {0};
        DECODE(packet_friendchat_setrank_osrs239_in, m);
        if( !wire_in_friendchat_setrank(&w, m.name, m.rank) )
            return PKTOUT_NAME_NONE;
        break;
    }
    case PKTOUT_NAME_SET_HEADING:
    {
        MsgSetHeading m = {0};
        DECODE(packet_set_heading_osrs239_in, m);
        wire_in_set_heading(&w, m.heading);
        break;
    }
    case PKTOUT_NAME_IF_SCRIPT_TRIGGER:
    {
        MsgIfScriptTrigger m = {0};

        packet_if_script_trigger_osrs239_in(wire_in_reader(&r, in, in_len), &m);
        if( !wire_in_read_ok(&r) ||
            !wire_in_if_script_trigger(&w, m.sub, m.obj, m.combined_id, m.crc, m.copy,
                                       m.copy_length) )
            return PKTOUT_NAME_NONE;
        break;
    }

    /* BUG_REPORT is not routed. Everything else is a body that is the same at
     * 239..242, or is dropped: passing an untranslated body to a handler is
     * the failure this file exists to remove -- it frames, it validates, and
     * it acts on numbers that came out of the wrong bytes. */
    default:
        return wire_in_body(name, in, in_len, out, out_cap, out_len);
    }
    return wire_in_done(&w, name, out_len);
}
