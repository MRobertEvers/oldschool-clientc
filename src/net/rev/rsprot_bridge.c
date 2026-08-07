/* See rsprot_bridge.h — the rsprot codecs, wired into the inbound path. */
#include "rsprot_bridge.h"

#include "rsprot_exec.h"

#include "packets/cam_lookat_v2.h"
#include "packets/if_resync_v2.h"
#include "packets/cam_moveto_v2.h"
#include "packets/cam_shake.h"
#include "packets/runclientscript.h"
#include "packets/set_map_flag_v2.h"
#include "packets/update_friendlist.h"
#include "packets/update_ignorelist.h"
#include "packets/update_inv_full.h"
#include "packets/update_inv_partial.h"
#include "packets/update_inv_stoptransmit.h"
#include "packets/set_npc_update_origin.h"
#include "packets/chat_filter_settings.h"
#include "packets/friendlist_loaded.h"
#include "packets/if_sethide.h"
#include "packets/if_setplayermodel_basecolour.h"
#include "packets/if_setplayermodel_bodytype.h"
#include "packets/if_setplayermodel_obj.h"
#include "packets/if_setplayermodel_self.h"
#include "packets/friendlist_loaded.h"
#include "packets/if_clearinv.h"
#include "packets/if_closesub.h"
#include "packets/if_movesub.h"
#include "packets/if_opensub.h"
#include "packets/if_opentop.h"
#include "packets/if_setanim.h"
#include "packets/if_setangle.h"
#include "packets/if_setcolour.h"
#include "packets/if_setevents_v2.h"
#include "packets/if_setmodel_v2.h"
#include "packets/if_setnpchead.h"
#include "packets/if_setnpchead_active.h"
#include "packets/if_setobject.h"
#include "packets/if_setplayerhead.h"
#include "packets/if_setposition.h"
#include "packets/if_setrotatespeed.h"
#include "packets/if_setscrollpos.h"
#include "packets/if_settext.h"
#include "packets/message_game.h"
#include "packets/midi_song_v2.h"
#include "packets/server_tick_end.h"
#include "packets/synth_sound.h"
#include "packets/update_runenergy.h"
#include "packets/update_stat_v2.h"
#include "packets/update_zone_full_follows.h"
#include "packets/update_zone_partial_follows.h"
#include "packets/varp_large.h"
#include "packets/varp_small.h"

#include "net/jbase37.h"

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

/* ------------------------------------------------------------------ */
/* Interface setters                                                   */
/* ------------------------------------------------------------------ */

/*
 * Almost every one of these is "a combined id plus one or two scalars", and the
 * only interesting thing about them is which byte order each field uses -- which
 * is now the codec's problem rather than this file's. What is left here is the
 * rename from RSProt's field names to the canonical struct's.
 */

static int
bridge_if_clearinv(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_clearinv_out, MsgIfClearInv)
    out->_if_clearinv.component_id = msg.combined_id;
    return 1;
}

static int
bridge_if_movesub(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_movesub_out, MsgIfMoveSub)
    out->_if_movesub.dest_uid = msg.destination_combined_id;
    out->_if_movesub.source_uid = msg.source_combined_id;
    return 1;
}

static int
bridge_if_setangle(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setangle_out, MsgIfSetAngle)
    out->_if_setangle.component_id = msg.combined_id;
    out->_if_setangle.zoom = msg.zoom;
    out->_if_setangle.angle_x = msg.angle_x;
    out->_if_setangle.angle_y = msg.angle_y;
    return 1;
}

static int
bridge_if_setcolour(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setcolour_out, MsgIfSetColour)
    out->_if_setcolour.component_id = msg.combined_id;
    out->_if_setcolour.colour = msg.colour15_bit_packed;
    return 1;
}

static int
bridge_if_setmodel(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setmodel_v2_out, MsgIfSetModelV2)
    out->_if_setmodel.component_id = msg.combined_id;
    out->_if_setmodel.model_id = msg.model;
    return 1;
}

static int
bridge_if_setnpchead(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setnpchead_out, MsgIfSetNpcHead)
    out->_if_setnpchead.component_id = msg.combined_id;
    out->_if_setnpchead.npc_id = msg.npc;
    return 1;
}

static int
bridge_if_setnpchead_active(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setnpchead_active_out, MsgIfSetNpcHeadActive)
    out->_if_setnpchead_active.component_id = msg.combined_id;
    out->_if_setnpchead_active.index = msg.index;
    return 1;
}

/*
 * `count` on the wire is the model ZOOM, not a quantity. RSProt names the field
 * after the classic obj-count slot it reuses; the canonical struct names it for
 * what the client does with it.
 */
static int
bridge_if_setobject(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setobject_out, MsgIfSetObject)
    out->_if_setobject.component_id = msg.combined_id;
    out->_if_setobject.obj_id = msg.obj;
    out->_if_setobject.zoom = msg.count;
    return 1;
}

static int
bridge_if_setplayerhead(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setplayerhead_out, MsgIfSetPlayerHead)
    out->_if_setplayerhead.component_id = msg.combined_id;
    return 1;
}

static int
bridge_if_setposition(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setposition_out, MsgIfSetPosition)
    out->_if_setposition.component_id = msg.combined_id;
    /* Signed, for the same reason as IF_SETROTATESPEED: a position offset is a
     * displacement and goes both ways. */
    out->_if_setposition.x = (int16_t)msg.x;
    /* RSProt's `y` is a screen offset on the vertical axis; the canonical
     * struct calls that axis z, matching the rest of this tree's 2D naming. */
    out->_if_setposition.z = (int16_t)msg.y;
    return 1;
}

static int
bridge_if_setrotatespeed(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setrotatespeed_out, MsgIfSetRotateSpeed)
    out->_if_setrotatespeed.component_id = msg.combined_id;
    /*
     * SIGNED. A rotate speed is a per-tick delta and a negative one spins the
     * model the other way; read unsigned, -1 becomes 65535 and the model
     * whips around instead of drifting back. The codec hands back the raw
     * 16-bit field, so the sign is this adapter's business.
     *
     * Caught by the differential test — it was the one packet of eighteen
     * where the mapping was right and the type was not.
     */
    out->_if_setrotatespeed.x_speed = (int16_t)msg.x_speed;
    out->_if_setrotatespeed.y_speed = (int16_t)msg.y_speed;
    return 1;
}

static int
bridge_if_setscrollpos(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setscrollpos_out, MsgIfSetScrollPos)
    out->_if_setscrollpos.component_id = msg.combined_id;
    out->_if_setscrollpos.pos = msg.scroll_pos;
    return 1;
}

/*
 * V2 carries TWO 32-bit masks where the older packet carried one, and the
 * button ops MOVED between them: events1 no longer includes ifbutton, and
 * events2 holds one bit per button. `events` is the classic single mask the
 * menu and input code still read, reconstructed exactly as the 239 client does.
 * That reconstruction is client behaviour, not wire layout, which is why it
 * lives here and not in the codec.
 */
static int
bridge_if_setevents(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    struct PktIfSetEvents* p = &out->_if_setevents;

    BRIDGE_RUN(rsprot_if_setevents_v2_out, MsgIfSetEventsV2)
    p->component_id = msg.combined_id;
    p->from = (int16_t)msg.start;
    p->to = (int16_t)msg.end;
    p->events1 = (uint32_t)msg.events1;
    p->events2 = (uint32_t)msg.events2;
    p->events = (int)(p->events1 | ((p->events2 & UINT32_C(0x3ff)) << 1));
    return 1;
}

/* ------------------------------------------------------------------ */
/* Zone headers, stats, sound                                          */
/* ------------------------------------------------------------------ */

static int
bridge_update_zone_full_follows(
    int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_update_zone_full_follows_out, MsgUpdateZoneFullFollows)
    out->_update_zone_full_follows.base_x = msg.zone_x;
    out->_update_zone_full_follows.base_z = msg.zone_z;
    out->_update_zone_full_follows.level = msg.level;
    return 1;
}

static int
bridge_update_zone_partial_follows(
    int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_update_zone_partial_follows_out, MsgUpdateZonePartialFollows)
    out->_update_zone_partial_follows.base_x = msg.zone_x;
    out->_update_zone_partial_follows.base_z = msg.zone_z;
    out->_update_zone_partial_follows.level = msg.level;
    return 1;
}

/*
 * `invisibleBoostedLevel` is dropped: the canonical struct carries one level,
 * and the client wants the real one. The hand-written arm dropped the same
 * field, so this is not a regression -- but a boosted level the UI could show
 * is being thrown away at both revisions, which is worth someone deciding
 * rather than inheriting.
 */
static int
bridge_update_stat(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_update_stat_v2_out, MsgUpdateStatV2)
    out->_update_stat.stat = msg.stat;
    out->_update_stat.level = msg.current_level;
    out->_update_stat.xp = msg.experience;
    return 1;
}

static int
bridge_synth_sound(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_synth_sound_out, MsgSynthSound)
    out->_synth_sound.id = msg.id;
    out->_synth_sound.loops = msg.loops;
    out->_synth_sound.delay = msg.delay;
    return 1;
}

/* Only the id survives: the canonical struct has no home for the four fade
 * timings V2 added, and the client's music player does not use them. */
static int
bridge_midi_song(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_midi_song_v2_out, MsgMidiSongV2)
    out->_midi_song.id = msg.id;
    return 1;
}

static int
bridge_friendlist_loaded(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_friendlist_loaded_out, MsgFriendListLoaded)
    (void)msg;
    /* 1, matching the hand-written arm. An earlier version of this adapter
     * said 2 and nothing caught it: FRIENDLIST_LOADED is zero-payload, so it
     * was never added to the differential test's generic cases. It is now. */
    out->_friendlist_loaded.status = 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Camera, chat filters, player-model setters                          */
/* ------------------------------------------------------------------ */

static int
bridge_cam_shake(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_cam_shake_out, MsgCamShake)
    /*
     * MsgCamShake holds EIGHT fields for a four-byte packet, because the struct
     * is the union across versions and RSProt renamed all four between them:
     * older revisions call them type/randomAmount/sineAmount/sineFrequency and
     * 239 calls them axis/random/amplitude/rate. Mapping the wrong set compiles
     * fine and reads four zeroes.
     *
     * `grep rev239 3rd/rsprot/packets/cam_shake.h` says which codec 239 picks,
     * and that codec names the fields it uses. The differential test caught
     * this one; the union is exactly where that mistake is easy to make.
     */
    out->_cam_shake.axis = msg.axis;
    out->_cam_shake.amplitude = msg.random;
    out->_cam_shake.frequency = msg.amplitude;
    out->_cam_shake.speed = msg.rate;
    return 1;
}

/*
 * `chat_private_mode` is stated as 0, not left alone.
 *
 * The rev-239 packet carries only the public and trade filters -- private chat
 * moved to its own CHAT_FILTER_SETTINGS_PRIVATECHAT packet -- so there is no
 * value on the wire for it. Writing 0 makes that explicit; the hand-written arm
 * did the same.
 */
static int
bridge_chat_filter_settings(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_chat_filter_settings_out, MsgChatFilterSettings)
    out->_chat_filter_settings.chat_public_mode = msg.public_chat_filter;
    out->_chat_filter_settings.chat_trade_mode = msg.trade_chat_filter;
    out->_chat_filter_settings.chat_private_mode = 0;
    return 1;
}

static int
bridge_if_sethide(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_sethide_out, MsgIfSetHide)
    out->_if_sethide.component_id = msg.combined_id;
    out->_if_sethide.hide = msg.hidden;
    return 1;
}

/*
 * Four packets, one canonical arm, and `index` is the discriminator.
 *
 * PktIfSetPlayerModel holds (component_id, index, value). BASECOLOUR names a
 * colour slot and so carries a real index; the other three set a single
 * property and use -1 to say "no slot". That -1 is load-bearing -- it is how
 * the consumer tells a base-colour write from a bodytype write -- so each
 * adapter states it rather than leaving the field at whatever memset left.
 */
static int
bridge_if_setplayermodel_basecolour(
    int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setplayermodel_basecolour_out, MsgIfSetPlayerModelBaseColour)
    out->_if_setplayermodel.component_id = msg.combined_id;
    out->_if_setplayermodel.index = msg.index;
    out->_if_setplayermodel.value = msg.colour;
    return 1;
}

static int
bridge_if_setplayermodel_bodytype(
    int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setplayermodel_bodytype_out, MsgIfSetPlayerModelBodyType)
    out->_if_setplayermodel.component_id = msg.combined_id;
    out->_if_setplayermodel.index = -1;
    out->_if_setplayermodel.value = msg.body_type;
    return 1;
}

static int
bridge_if_setplayermodel_obj(
    int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setplayermodel_obj_out, MsgIfSetPlayerModelObj)
    out->_if_setplayermodel.component_id = msg.combined_id;
    out->_if_setplayermodel.index = -1;
    out->_if_setplayermodel.value = msg.obj;
    return 1;
}

/* `copy_objs` is a flag; the canonical arm carries it in `value` as 0/1, which
 * is what the hand-written arm's `!= 0` produced. */
static int
bridge_if_setplayermodel_self(
    int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_if_setplayermodel_self_out, MsgIfSetPlayerModelSelf)
    out->_if_setplayermodel.component_id = msg.combined_id;
    out->_if_setplayermodel.index = -1;
    out->_if_setplayermodel.value = msg.copy_objs != 0;
    return 1;
}

static int
bridge_set_npc_update_origin(
    int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_set_npc_update_origin_out, MsgSetNpcUpdateOrigin)
    out->_set_npc_update_origin.x = msg.origin_x;
    out->_set_npc_update_origin.z = msg.origin_z;
    return 1;
}

/*
 * Four bytes carrying an ABSOLUTE packed CoordGrid, where the classic packet
 * sends two scene-local tile bytes with 255,255 meaning "clear". Three of the
 * four canonical fields are unpacked here rather than read off the wire:
 *
 *   - x/z come out of the packed coord;
 *   - `clear` has no sentinel at this revision -- a cleared flag is coord 0;
 *   - `absolute` tells the executor to subtract the scene origin, which this
 *     layer does not know.
 *
 * RSProt calls this SET_MAP_FLAG_V2; the client's canonical name is
 * UNSET_MAP_FLAG, and src/net/rev/osrs239/packetin.h has recorded the mapping
 * all along. It was reported as "not vendored in RSProt" twice before anyone
 * read that row.
 */
static int
bridge_unset_map_flag(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_set_map_flag_v2_out, MsgSetMapFlagV2)
    out->_set_map_flag.x = (msg.coord_grid_packed >> 14) & 0x3fff;
    out->_set_map_flag.z = msg.coord_grid_packed & 0x3fff;
    out->_set_map_flag.clear = msg.coord_grid_packed == 0;
    out->_set_map_flag.absolute = 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Containers, social lists, clientscript                              */
/* ------------------------------------------------------------------ */

/*
 * These five run HAND-WRITTEN codecs (3rd/rsprot/packets/update_inv_full.c and
 * friends) rather than generated ones. They are still rsprot codecs -- same
 * vocabulary, same executor, same bidirectionality -- so the packet layout
 * lives in one place per packet exactly as it does for the generated 215.
 * What differs is only who transcribed them; each file's header says which
 * construct defeated the generator.
 *
 * All five decode arrays into caller-supplied buffers. The codec bounds every
 * loop and fails the exec past the capacity rather than writing on, so a
 * malformed payload cannot overrun these -- see RSPROT_MORE and rsprot_xcount.
 */

static int
bridge_update_inv_full(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    struct PktUpdateInvFull* p = &out->_update_inv_full;
    static MsgUpdateInvFullSlot slots[MSG_UPDATE_INV_FULL_MAX_SLOTS];
    MsgUpdateInvFull msg;
    RsprotExec x;
    RsprotBuf buf;
    RsprotCodecFn fn = rsprot_version_pick(
        rsprot_update_inv_full_out, rsprot_update_inv_full_out_count, revision);

    if( !fn )
        return -1;
    memset(&msg, 0, sizeof(msg));
    msg.slots = slots;
    rsprot_buf_wrap_read(&buf, data, len);
    rsprot_exec_decode(&x, &buf);
    fn(&x, &msg);
    if( !rsprot_exec_ok(&x) || buf.err )
        return 0;

    memset(p, 0, sizeof(*p));
    p->component_id = msg.combined_id;
    p->inv_id = msg.inventory_id;
    p->size = msg.capacity;
    p->obj_ids = (int*)malloc((size_t)msg.capacity * sizeof(int) + 1);
    p->obj_counts = (int*)malloc((size_t)msg.capacity * sizeof(int) + 1);
    if( !p->obj_ids || !p->obj_counts )
    {
        free(p->obj_ids);
        free(p->obj_counts);
        p->obj_ids = NULL;
        p->obj_counts = NULL;
        p->size = 0;
        return 0;
    }
    for( int i = 0; i < msg.capacity; i++ )
    {
        p->obj_ids[i] = slots[i].id;
        p->obj_counts[i] = slots[i].count;
    }
    return 1;
}

static int
bridge_update_inv_partial(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    struct PktUpdateInvPartial* p = &out->_update_inv_partial;
    static MsgUpdateInvPartialSlot slots[MSG_UPDATE_INV_PARTIAL_MAX_SLOTS];
    MsgUpdateInvPartial msg;
    RsprotExec x;
    RsprotBuf buf;
    RsprotCodecFn fn = rsprot_version_pick(
        rsprot_update_inv_partial_out, rsprot_update_inv_partial_out_count, revision);

    if( !fn )
        return -1;
    memset(&msg, 0, sizeof(msg));
    msg.slots = slots;
    rsprot_buf_wrap_read(&buf, data, len);
    rsprot_exec_decode(&x, &buf);
    fn(&x, &msg);
    if( !rsprot_exec_ok(&x) || buf.err )
        return 0;

    memset(p, 0, sizeof(*p));
    p->component_id = msg.combined_id;
    p->inv_id = msg.inventory_id;
    p->count = msg.count;
    p->entries = (struct PktUpdateInvPartialEntry*)malloc(
        (size_t)msg.count * sizeof(*p->entries) + 1);
    if( !p->entries )
    {
        p->count = 0;
        return 0;
    }
    for( int i = 0; i < msg.count; i++ )
    {
        p->entries[i].slot = slots[i].slot;
        p->entries[i].obj_id = slots[i].id;
        /* An emptied slot carries no count on the wire; the canonical struct
         * wants a number, and 0 is what "nothing here" means to the consumer. */
        p->entries[i].count = slots[i].id < 0 ? 0 : slots[i].count;
    }
    return 1;
}

/*
 * The canonical struct keeps only base-37 names, so the note, the previous
 * name and the world metadata are read and dropped. That is the same loss the
 * hand-written arm took -- it decoded all four strings and freed three -- but
 * it IS a loss, and an ignore list that cannot show a note is a feature gap
 * living in PktUpdateIgnoreList rather than in the wire.
 */
static int
bridge_update_ignorelist(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    struct PktUpdateIgnoreList* p = &out->_update_ignorelist;
    static MsgUpdateIgnoreListEntry entries[MSG_UPDATE_IGNORELIST_MAX_ENTRIES];
    MsgUpdateIgnoreList msg;
    RsprotExec x;
    RsprotBuf buf;
    RsprotCodecFn fn = rsprot_version_pick(
        rsprot_update_ignorelist_out, rsprot_update_ignorelist_out_count, revision);

    if( !fn )
        return -1;
    memset(&msg, 0, sizeof(msg));
    msg.entries = entries;
    rsprot_buf_wrap_read(&buf, data, len);
    rsprot_exec_decode(&x, &buf);
    fn(&x, &msg);
    if( !rsprot_exec_ok(&x) || buf.err )
        return 0;

    memset(p, 0, sizeof(*p));
    p->count = msg.count;
    p->names37 = (int64_t*)malloc((size_t)msg.count * sizeof(int64_t) + 1);
    if( !p->names37 )
    {
        p->count = 0;
        return 0;
    }
    for( int i = 0; i < msg.count; i++ )
        p->names37[i] = entries[i].name ? (int64_t)strtobase37(entries[i].name) : 0;
    return 1;
}

/*
 * PktUpdateFriendList holds ONE friend, not a list, and the hand-written arm
 * required the payload to hold exactly one entry. Keeping that: the first entry
 * wins and `present` says whether there was one, which is what the consumer
 * already reads. A multi-entry payload now decodes without error where it used
 * to be rejected, and the extra entries are dropped -- a gap in the canonical
 * struct, not in this decode.
 */
static int
bridge_update_friendlist(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    struct PktUpdateFriendList* p = &out->_update_friendlist;
    static MsgUpdateFriendListEntry entries[MSG_UPDATE_FRIENDLIST_MAX_ENTRIES];
    MsgUpdateFriendList msg;
    RsprotExec x;
    RsprotBuf buf;
    RsprotCodecFn fn = rsprot_version_pick(
        rsprot_update_friendlist_out, rsprot_update_friendlist_out_count, revision);

    if( !fn )
        return -1;
    memset(&msg, 0, sizeof(msg));
    msg.entries = entries;
    rsprot_buf_wrap_read(&buf, data, len);
    rsprot_exec_decode(&x, &buf);
    fn(&x, &msg);
    if( !rsprot_exec_ok(&x) || buf.err )
        return 0;

    memset(p, 0, sizeof(*p));
    /* An empty payload is legal and completes initial sync. */
    if( msg.count <= 0 )
        return 1;
    if( !entries[0].name )
        return 0;
    p->name37 = (int64_t)strtobase37(entries[0].name);
    p->world = entries[0].world_id;
    p->present = 1;
    return 1;
}

static int
bridge_runclientscript(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    struct PktRunClientScript* p = &out->_runclientscript;
    static MsgRunClientScriptArg args[MSG_RUNCLIENTSCRIPT_MAX_ARGS];
    static int32_t ints[MSG_RUNCLIENTSCRIPT_MAX_ELEMS];
    static const char* strs[MSG_RUNCLIENTSCRIPT_MAX_ELEMS];
    MsgRunClientScript msg;
    RsprotExec x;
    RsprotBuf buf;
    int argc;
    RsprotCodecFn fn = rsprot_version_pick(
        rsprot_runclientscript_out, rsprot_runclientscript_out_count, revision);

    if( !fn )
        return -1;
    memset(&msg, 0, sizeof(msg));
    memset(args, 0, sizeof(args));
    for( int i = 0; i < MSG_RUNCLIENTSCRIPT_MAX_ARGS; i++ )
    {
        args[i].ints = ints;
        args[i].strs = strs;
    }
    msg.args = args;
    rsprot_buf_wrap_read(&buf, data, len);
    rsprot_exec_decode(&x, &buf);
    fn(&x, &msg);
    if( !rsprot_exec_ok(&x) || buf.err || !msg.types )
        return 0;

    memset(p, 0, sizeof(*p));
    p->script_id = msg.id;
    argc = (int)strlen(msg.types);
    if( argc > PKT_RUNCLIENTSCRIPT_ARG_MAX )
        return 0;
    p->argc = argc;
    for( int i = 0; i < argc; i++ )
    {
        if( msg.types[i] == 's' )
        {
            /* The canonical struct owns fixed-size string storage; the codec
             * hands back a borrow into the payload, so this copies. */
            char const* s = args[i].sval ? args[i].sval : "";
            size_t n = strlen(s);

            if( n >= PKT_RUNCLIENTSCRIPT_STR_LEN )
                n = PKT_RUNCLIENTSCRIPT_STR_LEN - 1;
            memcpy(p->strv[i], s, n);
            p->strv[i][n] = '\0';
            p->str_mask |= 1u << i;
        }
        else
        {
            /*
             * The array and 64-bit forms have no home in PktRunClientScript,
             * which carries one int or one string per argument. They decode
             * correctly and are then narrowed; the same narrowing the
             * hand-written arm did by only ever reading p4 and pjstr.
             */
            p->intv[i] = args[i].ival;
        }
    }
    return 1;
}

static int
bridge_update_inv_stop_transmit(
    int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_update_inv_stoptransmit_out, MsgUpdateInvStopTransmit)
    /* Two bytes, and they are the CONTAINER id, not a component. This revision
     * addresses the container directly; component_id is stated -1 to say the
     * packet carries none, which is what the consumer tests. */
    out->_update_inv_stop_transmit.component_id = -1;
    out->_update_inv_stop_transmit.inv_id = msg.inventory_id;
    return 1;
}

/*
 * Both camera packets carry an ABSOLUTE coordinate at this revision where the
 * classic pair sent scene-local tiles, so `absolute` is set for the executor to
 * subtract the scene origin -- which this layer does not know. RSProt names the
 * axes x/z; the canonical struct calls them local_x/local_z and the name is now
 * a slight lie, kept because every consumer reads it.
 */
static int
bridge_cam_lookat(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_cam_lookat_v2_out, MsgCamLookAtV2)
    out->_cam_lookat.local_x = msg.x;
    out->_cam_lookat.local_z = msg.z;
    out->_cam_lookat.height = msg.height;
    out->_cam_lookat.rate = msg.rate;
    out->_cam_lookat.rate2 = msg.rate2;
    out->_cam_lookat.absolute = 1;
    return 1;
}

static int
bridge_cam_moveto(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    BRIDGE_RUN(rsprot_cam_moveto_v2_out, MsgCamMoveToV2)
    out->_cam_moveto.local_x = msg.x;
    out->_cam_moveto.local_z = msg.z;
    out->_cam_moveto.height = msg.height;
    out->_cam_moveto.rate = msg.rate;
    out->_cam_moveto.rate2 = msg.rate2;
    out->_cam_moveto.absolute = 1;
    return 1;
}

/*
 * Two element arrays, and they are bounded differently on purpose.
 *
 * `sub_interfaces` has a two-byte count on the wire. `events` does NOT -- it
 * runs to the end of the payload -- which is why the codec loops it with
 * RSPROT_MORE and why decoding learns its length rather than being told.
 *
 * Both decode into static buffers sized at the codec's own caps, so a
 * malformed payload fails the exec rather than writing past them.
 */
static int
bridge_if_resync(int revision, uint8_t const* data, int len, struct RevPacket* out)
{
    struct PktIfResync* p = &out->_if_resync;
    static Rsprot_MsgIfResyncV2_sub_interfacesElem subs[4096];
    static Rsprot_MsgIfResyncV2_eventsElem evts[256];
    MsgIfResyncV2 msg;
    RsprotExec x;
    RsprotBuf buf;
    RsprotCodecFn fn = rsprot_version_pick(
        rsprot_if_resync_v2_out, rsprot_if_resync_v2_out_count, revision);

    if( !fn )
        return -1;
    memset(&msg, 0, sizeof(msg));
    msg.sub_interfaces = subs;
    msg.events = evts;
    rsprot_buf_wrap_read(&buf, data, len);
    rsprot_exec_decode(&x, &buf);
    fn(&x, &msg);
    if( !rsprot_exec_ok(&x) || buf.err )
        return 0;
    if( msg.sub_interfaces_count < 0 ||
        msg.sub_interfaces_count > (int32_t)(sizeof(subs) / sizeof(subs[0])) )
        return 0;

    memset(p, 0, sizeof(*p));
    /* 65535 is "no root"; the canonical struct says that with -1. */
    p->root_interface_id = msg.top_level_interface == 65535 ? -1 : msg.top_level_interface;
    p->mount_count = msg.sub_interfaces_count;
    p->event_count = msg.events_count;
    if( msg.sub_interfaces_count > 0 )
    {
        p->mounts = (struct PktIfResyncMount*)malloc(
            (size_t)msg.sub_interfaces_count * sizeof(*p->mounts));
        if( !p->mounts )
            return 0;
        for( int i = 0; i < msg.sub_interfaces_count; i++ )
        {
            p->mounts[i].target_uid = subs[i].destination_combined_id;
            p->mounts[i].interface_id = subs[i].interface_id;
            p->mounts[i].type = subs[i].type;
        }
    }
    if( msg.events_count > 0 )
    {
        p->events = (struct PktIfResyncEvent*)malloc(
            (size_t)msg.events_count * sizeof(*p->events));
        if( !p->events )
        {
            free(p->mounts);
            p->mounts = NULL;
            p->mount_count = 0;
            return 0;
        }
        for( int i = 0; i < msg.events_count; i++ )
        {
            p->events[i].component_id = evts[i].combined_id;
            p->events[i].from = (int16_t)evts[i].start;
            p->events[i].to = (int16_t)evts[i].end;
            p->events[i].events1 = (uint32_t)evts[i].events1;
            p->events[i].events2 = (uint32_t)evts[i].events2;
        }
    }
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
    { PKT_NAME_IF_CLEARINV, bridge_if_clearinv },
    { PKT_NAME_IF_MOVESUB, bridge_if_movesub },
    { PKT_NAME_IF_SETANGLE, bridge_if_setangle },
    { PKT_NAME_IF_SETCOLOUR, bridge_if_setcolour },
    { PKT_NAME_IF_SETEVENTS, bridge_if_setevents },
    { PKT_NAME_IF_SETMODEL, bridge_if_setmodel },
    { PKT_NAME_IF_SETNPCHEAD, bridge_if_setnpchead },
    { PKT_NAME_IF_SETNPCHEAD_ACTIVE, bridge_if_setnpchead_active },
    { PKT_NAME_IF_SETOBJECT, bridge_if_setobject },
    { PKT_NAME_IF_SETPLAYERHEAD, bridge_if_setplayerhead },
    { PKT_NAME_IF_SETPOSITION, bridge_if_setposition },
    { PKT_NAME_IF_SETROTATESPEED, bridge_if_setrotatespeed },
    { PKT_NAME_IF_SETSCROLLPOS, bridge_if_setscrollpos },
    { PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS, bridge_update_zone_full_follows },
    { PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS, bridge_update_zone_partial_follows },
    { PKT_NAME_UPDATE_STAT, bridge_update_stat },
    { PKT_NAME_SYNTH_SOUND, bridge_synth_sound },
    { PKT_NAME_MIDI_SONG, bridge_midi_song },
    { PKT_NAME_CAM_SHAKE, bridge_cam_shake },
    { PKT_NAME_IF_RESYNC_V2, bridge_if_resync },
    { PKT_NAME_CAM_LOOKAT, bridge_cam_lookat },
    { PKT_NAME_CAM_MOVETO, bridge_cam_moveto },
    { PKT_NAME_UPDATE_INV_FULL, bridge_update_inv_full },
    { PKT_NAME_UPDATE_INV_PARTIAL, bridge_update_inv_partial },
    { PKT_NAME_UPDATE_INV_STOP_TRANSMIT, bridge_update_inv_stop_transmit },
    { PKT_NAME_UPDATE_IGNORELIST, bridge_update_ignorelist },
    { PKT_NAME_UPDATE_FRIENDLIST, bridge_update_friendlist },
    { PKT_NAME_RUNCLIENTSCRIPT, bridge_runclientscript },
    { PKT_NAME_UNSET_MAP_FLAG, bridge_unset_map_flag },
    { PKT_NAME_SET_NPC_UPDATE_ORIGIN, bridge_set_npc_update_origin },
    { PKT_NAME_CHAT_FILTER_SETTINGS, bridge_chat_filter_settings },
    { PKT_NAME_IF_SETHIDE, bridge_if_sethide },
    { PKT_NAME_IF_SETPLAYERMODEL_BASECOLOUR, bridge_if_setplayermodel_basecolour },
    { PKT_NAME_IF_SETPLAYERMODEL_BODYTYPE, bridge_if_setplayermodel_bodytype },
    { PKT_NAME_IF_SETPLAYERMODEL_OBJ, bridge_if_setplayermodel_obj },
    { PKT_NAME_IF_SETPLAYERMODEL_SELF, bridge_if_setplayermodel_self },
    { PKT_NAME_FRIENDLIST_LOADED, bridge_friendlist_loaded },
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
