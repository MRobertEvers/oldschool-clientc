/* See rsprot_bridge.h — the rsprot codecs, wired into the inbound path. */
#include "rsprot_bridge.h"

#include "rsprot_exec.h"

#include "packets/if_closesub.h"
#include "packets/if_opensub.h"
#include "packets/if_opentop.h"
#include "packets/if_setanim.h"
#include "packets/if_settext.h"
#include "packets/message_game.h"
#include "packets/server_tick_end.h"
#include "packets/update_runenergy.h"
#include "packets/varp_large.h"
#include "packets/varp_small.h"

#include <stdlib.h>
#include <string.h>

int
rsprot_bridge_revision(struct GameProtoRevTable const* rev)
{
    if( !rev )
        return 0;
    switch( rev->revision )
    {
    case GAMEPROTO_REVISION_OSRS230:
        return 230;
    case GAMEPROTO_REVISION_OSRS239:
        return 239;
    /*
     * lc254, lc245_2 and xrsps233 are NOT RSProt revisions — LostCity and a
     * private server respectively — so there is no revision number to look up
     * and no codec to find. Returning 0 makes every lookup miss and the caller
     * fall through to the hand-written parsers, which is the correct answer
     * rather than a degraded one.
     */
    default:
        return 0;
    }
}

/*
 * The string copy. rsprot hands back a borrow into the payload; RevPacket owns
 * its strings. NULL in stays NULL out — "absent" is a value gjstrnull can
 * return and is not the same as an empty string.
 */
static char*
dup_borrowed(char const* s)
{
    size_t n;
    char* out;

    if( !s )
        return NULL;
    n = strlen(s);
    out = (char*)malloc(n + 1);
    if( !out )
        return NULL;
    memcpy(out, s, n + 1);
    return out;
}

/*
 * One adapter per packet: run the codec, then copy into the canonical arm.
 *
 * Each is written out rather than generated because each one states a mapping
 * decision — which canonical field a wire field lands in, and what happens to a
 * wire field with no canonical home. A generator would have to be told those
 * anyway, and stating them here keeps them where someone reading the packet
 * looks.
 */

#define BRIDGE_RUN(TABLE, MSGTYPE)                                                \
    MSGTYPE msg;                                                                  \
    RsprotExec x;                                                                 \
    RsprotBuf buf;                                                                \
    RsprotCodecFn fn = rsprot_version_pick(TABLE, TABLE##_count, revision);        \
    if( !fn )                                                                     \
        return -1;                                                                \
    memset(&msg, 0, sizeof(msg));                                                 \
    rsprot_buf_wrap_read(&buf, data, len);                                        \
    rsprot_exec_decode(&x, &buf);                                                 \
    fn(&x, &msg);                                                                 \
    if( !rsprot_exec_ok(&x) || buf.err )                                          \
        return 0;

static int
bridge_varp_small(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_varp_small_out, MsgVarpSmall)
    out->_varp_small.variable = msg.id;
    /*
     * Sign-extend. The wire field is one byte and the canonical struct's
     * comment already says "g1b signed byte": varps carry negative values and
     * reading this unsigned turns -1 into 255, which is a valid different
     * setting rather than an error.
     */
    out->_varp_small.value = (int8_t)msg.value;
    return 1;
}

static int
bridge_varp_large(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_varp_large_out, MsgVarpLarge)
    out->_varp_large.variable = msg.id;
    out->_varp_large.value = msg.value;
    return 1;
}

static int
bridge_update_runenergy(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_update_runenergy_out, MsgUpdateRunEnergy)
    /*
     * UNIT CHANGE, and it is the whole content of this adapter.
     *
     * The wire carries HUNDREDTHS of a percent in two bytes; the canonical
     * `run_energy` field is a percent, 0-100, and osrs230_parse.c has always
     * divided by 100 on the way in. Handing the raw value over instead reads as
     * a working decode -- the bytes are right, the packet frames, nothing
     * errors -- and pegs the run-energy orb at 100x.
     *
     * Caught by src/net/rev/test/rsprot_bridge_test.c, which is the argument
     * for that test existing: a codec can be byte-perfect and the ADAPTER still
     * wrong, and only running both parsers over the same payload sees it.
     */
    out->_update_run_energy.run_energy = msg.runenergy / 100;
    return 1;
}

static int
bridge_if_opensub(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_opensub_out, MsgIfOpenSub)
    out->_if_opensub.target_uid = msg.destination_combined_id;
    out->_if_opensub.interface_id = msg.interface_id;
    out->_if_opensub.type = msg.type;
    return 1;
}

static int
bridge_if_closesub(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_closesub_out, MsgIfCloseSub)
    out->_if_closesub.target_uid = msg.combined_id;
    return 1;
}

static int
bridge_if_settext(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_settext_out, MsgIfSetText)
    out->_if_settext.component_id = msg.combined_id;
    out->_if_settext.text = dup_borrowed(msg.text);
    if( !out->_if_settext.text )
        return 0;
    return 1;
}

static int
bridge_if_setanim(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setanim_out, MsgIfSetAnim)
    out->_if_setanim.component_id = msg.combined_id;
    out->_if_setanim.anim_id = msg.anim;
    return 1;
}

static int
bridge_message_game(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_message_game_out, MsgMessageGame)
    /*
     * `type` (the chat filter tab) and `name` (the speaker, present only when
     * name_present is set) are DROPPED: struct PktMessageGame carries only
     * `text`. That is the same loss the hand-written osrs239_parse.c already
     * takes — it reads the name and frees it on the spot — so this is not a
     * regression, but it is a real gap and belongs in the canonical struct
     * rather than being read and discarded twice.
     */
    out->_message_game.text = dup_borrowed(msg.message);
    if( !out->_message_game.text )
        return 0;
    return 1;
}

static int
bridge_server_tick_end(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_server_tick_end_out, MsgServerTickEnd)
    /* Zero payload: the packet IS its arrival. `packet_type` is already set by
     * the caller, and RevPacket has no arm for it because there is nothing to
     * carry. Running the codec is still worth it — it is what asserts the
     * payload really was empty at this revision. */
    (void)msg;
    (void)out;
    return 1;
}

static int
bridge_if_opentop(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_opentop_out, MsgIfOpenTop)
    out->_if_opentop.interface_id = msg.interface_id;
    return 1;
}

struct BridgeRow
{
    int pkt_name;
    int (*fn)(int revision, uint8_t const* data, int len, struct RevPacket* out);
};

/*
 * The migration ledger, in table form: a packet is on rsprot exactly when it
 * has a row here. Adding one is the whole cutover step for that packet, and the
 * arm it replaces in osrs239_parse.c becomes dead code to delete.
 */
static struct BridgeRow const k_rows[] = {
    { PKT_NAME_IF_OPENTOP, bridge_if_opentop },
    { PKT_NAME_IF_OPENSUB, bridge_if_opensub },
    { PKT_NAME_IF_CLOSESUB, bridge_if_closesub },
    { PKT_NAME_IF_SETTEXT, bridge_if_settext },
    { PKT_NAME_IF_SETANIM, bridge_if_setanim },
    { PKT_NAME_VARP_SMALL, bridge_varp_small },
    { PKT_NAME_VARP_LARGE, bridge_varp_large },
    { PKT_NAME_UPDATE_RUNENERGY, bridge_update_runenergy },
    { PKT_NAME_MESSAGE_GAME, bridge_message_game },
    { PKT_NAME_SERVER_TICK_END, bridge_server_tick_end },
};

int
rsprot_bridge_parse(
    struct GameProtoRevTable const* rev,
    int pkt_name,
    uint8_t const* data,
    int len,
    struct RevPacket* out)
{
    int revision = rsprot_bridge_revision(rev);
    size_t i;

    if( revision == 0 )
        return -1;

    for( i = 0; i < sizeof(k_rows) / sizeof(k_rows[0]); i++ )
    {
        if( k_rows[i].pkt_name != pkt_name )
            continue;
        out->packet_type = (enum GameProtoPktName)pkt_name;
        return k_rows[i].fn(revision, data, len, out);
    }
    return -1;
}
