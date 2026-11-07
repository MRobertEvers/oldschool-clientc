#include "torirs_server_wire.h"

#include "mock239_runclientscript.h"
#include "mock242_inbound.h"
#include "torirs_server_scene.h"
#include "torirs_server_zone.h"

#include "torirsserver/wire/osrs242/packetin.h"
#include "torirsserver/wire/osrs242/packetout.h"
#include "torirsserver/wire/osrs242/zoneprot.h"

#include "rsprot_buf.h"
#include "rsprot_exec.h"

#include "packets/ambientsound_start.h"
#include "packets/ambientsound_stop.h"
#include "packets/cam_lookat_v3.h"
#include "packets/cam_moveto_v3.h"
#include "packets/cam_moveto_cycles_v3.h"
#include "packets/cam_rotateto_coordinate_v3.h"
#include "packets/cam_shake.h"
#include "packets/cam_unlock.h"
#include "packets/chat_filter_settings.h"
#include "packets/if_closesub.h"
#include "packets/if_movesub.h"
#include "packets/if_opensub.h"
#include "packets/if_opentop.h"
#include "packets/if_setangle.h"
#include "packets/if_setanim.h"
#include "packets/if_setcolour.h"
#include "packets/if_setevents_v2.h"
#include "packets/if_sethide.h"
#include "packets/if_setmodel_v2.h"
#include "packets/if_setnpchead.h"
#include "packets/if_setnpchead_active.h"
#include "packets/if_setobject.h"
#include "packets/if_setplayerhead.h"
#include "packets/if_setplayermodel_basecolour.h"
#include "packets/if_setplayermodel_bodytype.h"
#include "packets/if_setplayermodel_obj.h"
#include "packets/if_setplayermodel_self.h"
#include "packets/if_setposition.h"
#include "packets/if_setrotatespeed.h"
#include "packets/if_setscrollpos.h"
#include "packets/if_settext.h"
#include "packets/loc_add_change_v2.h"
#include "packets/loc_anim.h"
#include "packets/loc_del.h"
#include "packets/map_anim.h"
#include "packets/map_projanim_v2.h"
#include "packets/scriptedproj_add.h"
#include "packets/message_game.h"
#include "packets/midi_jingle.h"
#include "packets/update_stockmarket_slot_v2.h"
#include "packets/varp_long.h"
#include "packets/midi_song_stop.h"
#include "packets/midi_song_v2.h"
#include "packets/midi_song_withsecondary.h"
#include "packets/obj_add.h"
#include "packets/obj_count.h"
#include "packets/obj_del.h"
#include "packets/obj_enabled_ops.h"
#include "packets/rebuild_normal_v2.h"
#include "packets/rebuild_region_v2.h"
#include "packets/set_map_flag_v2.h"
#include "packets/sound_area.h"
#include "packets/synth_sound.h"
#include "packets/update_inv_stoptransmit.h"
#include "packets/update_runenergy.h"
#include "packets/update_runweight.h"
#include "packets/update_stat_v2.h"
#include "packets/update_zone_full_follows.h"
#include "packets/update_zone_partial_enclosed.h"
#include "packets/update_zone_partial_follows.h"
#include "packets/varp_large.h"
#include "packets/varp_small.h"

#include <assert.h>
#include <rsareabuf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The revision-242 wire: a copy of torirs_server_wire241.c, changed to 242.
 *
 * Every fixed-layout server prot is written by running the GENERATED codec in
 * 3rd/rsprot/packets/ at revision 242 in the encode direction, the function the
 * 242 client's executor runs to read it (RSProt's 242 rows, from RSProx
 * 8c514346's Kotlin 242). Most of them moved transforms at 242; the codecs
 * carry that, so the writers below did not change.
 *
 * What did: the zone sub-packet ordinals, the UPDATE_INV_FULL slot, the
 * world-entity spawn record and flag bits, and LOC_MERGE, which has no 242
 * layout in either Kotlin library and is not sent. Writers RSProt's codecs write
 * by hand (friend and ignore entries, RUNCLIENTSCRIPT, UPDATE_INV_PARTIAL)
 * state one layout for 240..242 there, so they keep 241's bytes.
 */

/* ------------------------------------------------------------------ */
/* Running a codec into an RSAreaBuf                                   */
/* ------------------------------------------------------------------ */

/* The largest packet a client accepts. This was 1024, and a REBUILD_REGION for
 * an instance of two map squares (320 zones in its window, ~1130 bytes) came
 * out cut at 1024: the client could not parse it, never rebuilt, and the
 * player could not walk in the copy (Children of the Sun's tail, 2026-10-29).
 * The overflow is checked below without assert(), which OPT builds drop. */
#define W242_PACKET_MAX 40000

static void
w242_run(struct RSAreaBuf* buf, RsprotVersionRange const* ranges, int count, void* msg)
{
    uint8_t tmp[W242_PACKET_MAX];
    RsprotBuf b;
    RsprotExec x;
    RsprotCodecFn fn = rsprot_version_pick(ranges, count, rsprot_osrs(242));

    /* Every table this file names has a 242 row (the overlay's report lists
     * them); a NULL here is a generator regression, not a runtime case. */
    assert(fn);
    rsprot_buf_wrap(&b, tmp, (int32_t)sizeof(tmp));
    rsprot_exec_encode(&x, &b);
    fn(&x, msg);
    assert(rsprot_exec_ok(&x));
    if( b.err )
    {
        fprintf(stderr, "torirsserver: a 242 packet outgrew %d bytes\n", W242_PACKET_MAX);
        abort();
    }
    rsab_pdata(buf, tmp, (size_t)b.wpos);
}

#define W242(TABLE, MSG) w242_run(buf, TABLE, TABLE##_count, &(MSG))

/* ------------------------------------------------------------------ */
/* Opcode resolution                                                   */
/* ------------------------------------------------------------------ */

static int
osrs242_opcode(int pkt_name)
{
    /* No 242 layout: neither as its own prot nor inside a zone. */
    if( pkt_name == PKT_NAME_LOC_MERGE )
        return -1;
    return packetin_wire_osrs242(pkt_name);
}

static int
osrs242_size(int wire_opcode)
{
    return packetin_size_osrs242(wire_opcode);
}

static char const*
osrs242_prot_name(int wire_opcode)
{
    return packetin_protname_osrs242(wire_opcode);
}

static int
osrs242_out_size(int wire_opcode)
{
    return osrs242_packetout_size(wire_opcode);
}

static int
osrs242_out_name(int wire_opcode)
{
    return osrs242_packetout_name(wire_opcode);
}

static char const*
osrs242_out_prot_name(int wire_opcode)
{
    return osrs242_packetout_protname(wire_opcode);
}

/* The enclosed sub-packet ordinal: RSProx 242's IndexedZoneProtDecoder order
 * (zoneprot.h), another permutation. LOC_MERGE has none to send. */
static int
osrs242_zone_sub(int pkt_name)
{
    switch( pkt_name )
    {
    case PKT_NAME_LOC_DEL: return OSRS242_ZONE_LOC_DEL;
    case PKT_NAME_OBJ_COUNT: return OSRS242_ZONE_OBJ_COUNT;
    case PKT_NAME_MAP_ANIM: return OSRS242_ZONE_MAP_ANIM;
    case PKT_NAME_SOUND_AREA: return OSRS242_ZONE_SOUND_AREA;
    case PKT_NAME_LOC_ADD_CHANGE: return OSRS242_ZONE_LOC_ADD_CHANGE_V2;
    case PKT_NAME_OBJ_ADD: return OSRS242_ZONE_OBJ_ADD;
    case PKT_NAME_LOC_ANIM: return OSRS242_ZONE_LOC_ANIM;
    case PKT_NAME_OBJ_DEL: return OSRS242_ZONE_OBJ_DEL;
    case PKT_NAME_OBJ_REVEAL: return OSRS242_ZONE_OBJ_ENABLED_OPS;
    case PKT_NAME_MAP_PROJANIM: return OSRS242_ZONE_MAP_PROJANIM_V2;
    case PKT_NAME_SCRIPTEDPROJ_ADD: return OSRS242_ZONE_SCRIPTEDPROJ_ADD;
    default: return -1;
    }
}

/* ------------------------------------------------------------------ */
/* Interfaces                                                          */
/* ------------------------------------------------------------------ */

static void
w242_if_opentop(struct RSAreaBuf* buf, int interface_id)
{
    MsgIfOpenTop m = { 0 };
    m.interface_id = interface_id;
    W242(rsprot_if_opentop_out, m);
}

static void
w242_if_opensub(struct RSAreaBuf* buf, int interface_id, int dest_uid, int type)
{
    MsgIfOpenSub m = { 0 };
    m.interface_id = interface_id;
    m.destination_combined_id = dest_uid;
    m.type = type;
    W242(rsprot_if_opensub_out, m);
}

static void
w242_if_closesub(struct RSAreaBuf* buf, int dest_uid)
{
    MsgIfCloseSub m = { 0 };
    m.combined_id = dest_uid;
    W242(rsprot_if_closesub_out, m);
}

static void
w242_if_movesub(struct RSAreaBuf* buf, int source_uid, int dest_uid)
{
    MsgIfMoveSub m = { 0 };
    m.source_combined_id = source_uid;
    m.destination_combined_id = dest_uid;
    W242(rsprot_if_movesub_out, m);
}

/* Two masks, the button ops in the second -- see w239_if_setevents. */
static void
w242_if_setevents(
    struct RSAreaBuf* buf,
    int component_uid,
    int start,
    int end,
    uint32_t events1,
    uint32_t events2)
{
    MsgIfSetEventsV2 m = { 0 };
    m.combined_id = component_uid;
    m.start = start;
    m.end = end;
    m.events1 = (int32_t)events1;
    m.events2 = (int32_t)events2;
    W242(rsprot_if_setevents_v2_out, m);
}

static void
w242_if_setcolour(struct RSAreaBuf* buf, int component_uid, int colour)
{
    MsgIfSetColour m = { 0 };
    m.combined_id = component_uid;
    m.colour15_bit_packed = colour;
    W242(rsprot_if_setcolour_out, m);
}

static void
w242_if_sethide(struct RSAreaBuf* buf, int component_uid, int hide)
{
    MsgIfSetHide m = { 0 };
    m.combined_id = component_uid;
    m.hidden = hide ? 1 : 0;
    W242(rsprot_if_sethide_out, m);
}

static void
w242_if_setmodel(struct RSAreaBuf* buf, int component_uid, int model_id)
{
    MsgIfSetModelV2 m = { 0 };
    m.combined_id = component_uid;
    m.model = model_id;
    W242(rsprot_if_setmodel_v2_out, m);
}

static void
w242_if_setobject(struct RSAreaBuf* buf, int component_uid, int obj_id, int value)
{
    MsgIfSetObject m = { 0 };
    m.combined_id = component_uid;
    m.obj = obj_id;
    m.count = value;
    W242(rsprot_if_setobject_out, m);
}

static void
w242_if_setposition(struct RSAreaBuf* buf, int component_uid, int x, int y)
{
    MsgIfSetPosition m = { 0 };
    m.combined_id = component_uid;
    m.x = x;
    m.y = y;
    W242(rsprot_if_setposition_out, m);
}

static void
w242_if_setscroll(struct RSAreaBuf* buf, int component_uid, int position)
{
    MsgIfSetScrollPos m = { 0 };
    m.combined_id = component_uid;
    m.scroll_pos = position;
    W242(rsprot_if_setscrollpos_out, m);
}

static void
w242_if_setrotatespeed(struct RSAreaBuf* buf, int component_uid, int x_speed, int y_speed)
{
    MsgIfSetRotateSpeed m = { 0 };
    m.combined_id = component_uid;
    m.x_speed = x_speed;
    m.y_speed = y_speed;
    W242(rsprot_if_setrotatespeed_out, m);
}

static void
w242_if_setangle(struct RSAreaBuf* buf, int component_uid, int zoom, int angle_x, int angle_y)
{
    MsgIfSetAngle m = { 0 };
    m.combined_id = component_uid;
    m.zoom = zoom;
    m.angle_x = angle_x;
    m.angle_y = angle_y;
    W242(rsprot_if_setangle_out, m);
}

static void
w242_if_setnpchead_active(struct RSAreaBuf* buf, int component_uid, int index)
{
    MsgIfSetNpcHeadActive m = { 0 };
    m.combined_id = component_uid;
    m.index = index;
    W242(rsprot_if_setnpchead_active_out, m);
}

static void
w242_if_setplayermodel_basecolour(struct RSAreaBuf* buf, int component_uid, int index, int colour)
{
    MsgIfSetPlayerModelBaseColour m = { 0 };
    m.combined_id = component_uid;
    m.index = index;
    m.colour = colour;
    W242(rsprot_if_setplayermodel_basecolour_out, m);
}

static void
w242_if_setplayermodel_bodytype(struct RSAreaBuf* buf, int component_uid, int body_type)
{
    MsgIfSetPlayerModelBodyType m = { 0 };
    m.combined_id = component_uid;
    m.body_type = body_type;
    W242(rsprot_if_setplayermodel_bodytype_out, m);
}

static void
w242_if_setplayermodel_obj(struct RSAreaBuf* buf, int component_uid, int obj_id)
{
    MsgIfSetPlayerModelObj m = { 0 };
    m.combined_id = component_uid;
    m.obj = obj_id;
    W242(rsprot_if_setplayermodel_obj_out, m);
}

static void
w242_if_setplayermodel_self(struct RSAreaBuf* buf, int component_uid, int copy_objs)
{
    MsgIfSetPlayerModelSelf m = { 0 };
    m.combined_id = component_uid;
    m.copy_objs = copy_objs;
    W242(rsprot_if_setplayermodel_self_out, m);
}

static void
w242_if_settext(struct RSAreaBuf* buf, int uid, const char* text)
{
    MsgIfSetText m = { 0 };
    m.combined_id = uid;
    m.text = text ? text : "";
    W242(rsprot_if_settext_out, m);
}

static void
w242_if_setnpchead(struct RSAreaBuf* buf, int uid, int npc_id)
{
    MsgIfSetNpcHead m = { 0 };
    m.combined_id = uid;
    m.npc = npc_id;
    W242(rsprot_if_setnpchead_out, m);
}

static void
w242_if_setanim(struct RSAreaBuf* buf, int uid, int anim_id)
{
    MsgIfSetAnim m = { 0 };
    m.combined_id = uid;
    m.anim = anim_id;
    W242(rsprot_if_setanim_out, m);
}

static void
w242_if_setplayerhead(struct RSAreaBuf* buf, int uid)
{
    MsgIfSetPlayerHead m = { 0 };
    m.combined_id = uid;
    W242(rsprot_if_setplayerhead_out, m);
}

/* ------------------------------------------------------------------ */
/* Player state                                                        */
/* ------------------------------------------------------------------ */

static void
w242_message_game(struct RSAreaBuf* buf, int type, const char* text)
{
    MsgMessageGame m = { 0 };
    m.type = type;
    m.name_present = 0; /* no sender name, as at 239 */
    m.message = text ? text : "";
    W242(rsprot_message_game_out, m);
}

static void
w242_varp_small(struct RSAreaBuf* buf, int varp, int value)
{
    MsgVarpSmall m = { 0 };
    m.id = varp;
    m.value = value;
    W242(rsprot_varp_small_out, m);
}

static void
w242_varp_large(struct RSAreaBuf* buf, int varp, int value)
{
    MsgVarpLarge m = { 0 };
    m.id = varp;
    m.value = value;
    W242(rsprot_varp_large_out, m);
}

static void
w242_update_runenergy(struct RSAreaBuf* buf, int hundredths)
{
    MsgUpdateRunEnergy m = { 0 };
    m.runenergy = hundredths;
    W242(rsprot_update_runenergy_out, m);
}

static void
w242_update_runweight(struct RSAreaBuf* buf, int kilograms)
{
    MsgUpdateRunWeight m = { 0 };
    m.runweight = kilograms;
    W242(rsprot_update_runweight_out, m);
}

/* The boosted level in both level fields, as w239_update_stat explains. */
static void
w242_update_stat(struct RSAreaBuf* buf, int stat, int level, int xp, int boosted)
{
    MsgUpdateStatV2 m = { 0 };
    (void)level;
    m.stat = stat;
    m.current_level = boosted;
    m.invisible_boosted_level = boosted;
    m.experience = xp;
    W242(rsprot_update_stat_v2_out, m);
}

static void
w242_set_map_flag(struct RSAreaBuf* buf, int level, int x, int z)
{
    MsgSetMapFlagV2 m = { 0 };
    m.coord_grid_packed = ((level & 0x3) << 28) | ((x & 0x3fff) << 14) | (z & 0x3fff);
    W242(rsprot_set_map_flag_v2_out, m);
}

/* Two bytes; the private filter has its own packet (see w239_chat_filter). */
static void
w242_chat_filter(struct RSAreaBuf* buf, int public_, int private_, int trade)
{
    MsgChatFilterSettings m = { 0 };
    (void)private_;
    m.public_chat_filter = public_;
    m.trade_chat_filter = trade;
    W242(rsprot_chat_filter_settings_out, m);
}

/* ------------------------------------------------------------------ */
/* Sound                                                               */
/* ------------------------------------------------------------------ */

static void
w242_synth_sound(struct RSAreaBuf* buf, int id, int loops, int delay)
{
    MsgSynthSound m = { 0 };
    m.id = id;
    m.loops = loops;
    m.delay = delay;
    W242(rsprot_synth_sound_out, m);
}

static void
w242_midi_song(
    struct RSAreaBuf* buf,
    int id,
    int fade_out_delay,
    int fade_out_speed,
    int fade_in_delay,
    int fade_in_speed)
{
    MsgMidiSongV2 m = { 0 };
    m.id = id;
    m.fade_out_delay = fade_out_delay;
    m.fade_out_speed = fade_out_speed;
    m.fade_in_delay = fade_in_delay;
    m.fade_in_speed = fade_in_speed;
    W242(rsprot_midi_song_v2_out, m);
}

static void
w242_midi_song_withsecondary(
    struct RSAreaBuf* buf,
    int primary_id,
    int secondary_id,
    int fade_out_delay,
    int fade_out_speed,
    int fade_in_delay,
    int fade_in_speed)
{
    MsgMidiSongWithSecondary m = { 0 };
    m.primary_id = primary_id;
    m.secondary_id = secondary_id;
    m.fade_out_delay = fade_out_delay;
    m.fade_out_speed = fade_out_speed;
    m.fade_in_delay = fade_in_delay;
    m.fade_in_speed = fade_in_speed;
    W242(rsprot_midi_song_withsecondary_out, m);
}

static void
w242_midi_song_stop(struct RSAreaBuf* buf, int fade_out_delay, int fade_out_speed)
{
    MsgMidiSongStop m = { 0 };
    m.fade_out_delay = fade_out_delay;
    m.fade_out_speed = fade_out_speed;
    W242(rsprot_midi_song_stop_out, m);
}

static void
w242_midi_jingle(struct RSAreaBuf* buf, int id, int length_ms)
{
    MsgMidiJingle m = { 0 };
    m.id = id;
    m.length_in_millis = length_ms;
    W242(rsprot_midi_jingle_out, m);
}

static void
w242_varp_long(struct RSAreaBuf* buf, int id, int64_t value)
{
    MsgVarpLong m = { 0 };
    m.id = id;
    m.high = (int32_t)(value >> 32);
    m.low = (int32_t)(value & 0xFFFFFFFF);
    W242(rsprot_varp_long_out, m);
}

/* The escaped form every 241 slot takes (first_state 7, version 2): `status`
 * after it, 8-byte price and gold, no extension. RSProx index 4426 [97]:
 * "slot=3, version=2, status=1, id=hunting_ojibway_bird_snare (10006),
 * price=21, count=6"; the empty slot [106] is status 0 with nothing set. */
static void
w242_update_stockmarket_slot(
    struct RSAreaBuf* buf,
    int slot,
    int status,
    int obj,
    int64_t price,
    int count,
    int completed_count,
    int64_t completed_gold)
{
    MsgUpdateStockmarketSlotV2 m = { 0 };
    m.slot = slot;
    m.first_state = 7;
    m.version = 2;
    m.status = status;
    m.obj = obj;
    m.price = price;
    m.count = count;
    m.completed_count = completed_count;
    m.completed_gold = completed_gold;
    W242(rsprot_update_stockmarket_slot_v2_out, m);
}

static void
w242_ambientsound_start(struct RSAreaBuf* buf, int id, int fade)
{
    MsgAmbientSoundStart m = { 0 };
    m.id = id;
    m.fade = fade ? 1 : 0;
    W242(rsprot_ambientsound_start_out, m);
}

static void
w242_ambientsound_stop(struct RSAreaBuf* buf, int fade)
{
    MsgAmbientSoundStop m = { 0 };
    m.fade = fade ? 1 : 0;
    W242(rsprot_ambientsound_stop_out, m);
}

/* ------------------------------------------------------------------ */
/* Social and inventories -- unchanged bodies                          */
/* ------------------------------------------------------------------ */

/* FriendListLoaded: zero bytes, as at 239. */
static void
w242_friendlist_loaded(struct RSAreaBuf* buf, int status)
{
    (void)buf;
    (void)status;
}

/* UPDATE_FRIENDLIST: 241's entry (RSProt's update_friendlist.c: one layout for 240..242). */
static void
w242_friend_entry(struct RSAreaBuf* buf, const char* name, int world)
{
    const char* safe = name ? name : "";
    rsab_p1(buf, 0);
    rsab_pjstr(buf, safe, RSAB_JSTR_NUL);
    rsab_pjstr(buf, safe, RSAB_JSTR_NUL);
    rsab_p2(buf, (uint16_t)world);
    rsab_p1(buf, 0);
    rsab_p1(buf, 0);
    if( world > 0 )
    {
        rsab_pjstr(buf, "", RSAB_JSTR_NUL);
        rsab_p1(buf, 0);
        rsab_p4(buf, 0);
    }
    rsab_pjstr(buf, "", RSAB_JSTR_NUL);
}

/* UPDATE_IGNORELIST: 241's entry (RSProt: one layout for 240..242). */
static void
w242_ignore_entry(struct RSAreaBuf* buf, const char* name)
{
    rsab_p1(buf, 0x1);
    rsab_pjstr(buf, name ? name : "", RSAB_JSTR_NUL);
    rsab_pjstr(buf, "", RSAB_JSTR_NUL);
    rsab_pjstr(buf, "", RSAB_JSTR_NUL);
}

/* The inventory header is 241's (gCombinedId -1, g2 inventory, g2 capacity for
 * the full form): RSProt's osrs242 UPDATE_INV_FULL head is COM_1234 + U2_12. */
static void
w242_inv_header(struct RSAreaBuf* buf, int pkt_name, int uid, int container, int capacity)
{
    (void)uid;
    rsab_p4(buf, (uint32_t)-1);
    rsab_p2(buf, container);
    if( pkt_name == PKT_NAME_UPDATE_INV_FULL )
        rsab_p2(buf, capacity);
}

/*
 * One slot. UPDATE_INV_FULL moved again at 242 (RSProt update_inv_full, osrs242):
 * the count leads, g1Alt1, escaping at 255 to a g4Alt3 count, then the id+1 as
 * g2Alt3. The PARTIAL form is 241's.
 */
static void
w242_inv_slot(struct RSAreaBuf* buf, int pkt_name, int slot, int obj_id, int count)
{
    if( pkt_name == PKT_NAME_UPDATE_INV_FULL )
    {
        if( obj_id < 0 )
        {
            rsab_p1_alt1(buf, 0);
            rsab_p2_alt3(buf, 0);
            return;
        }
        rsab_p1_alt1(buf, count > 0xff ? 0xff : count);
        if( count >= 255 )
            rsab_p4_alt3(buf, count);
        rsab_p2_alt3(buf, obj_id + 1);
        return;
    }
    rsab_psmart(buf, slot);
    if( obj_id < 0 )
    {
        rsab_p2(buf, 0);
        return;
    }
    rsab_p2(buf, obj_id + 1);
    rsab_p1(buf, count > 0xff ? 0xff : count);
    if( count >= 0xff )
        rsab_p4(buf, count);
}

static void
w242_inv_stop_transmit(struct RSAreaBuf* buf, int component_uid, int inv_id)
{
    MsgUpdateInvStopTransmit m = { 0 };
    (void)component_uid;
    m.inventory_id = inv_id;
    W242(rsprot_update_inv_stoptransmit_out, m);
}

/* RUNCLIENTSCRIPT: 241's body (RSProt's runclientscript.c: one layout for 240..242). */
static void
w242_run_clientscript(
    struct RSAreaBuf* buf,
    int script_id,
    const char* types,
    int const* intv,
    const char* const* strv,
    int argc)
{
    struct Mock239ClientScriptArg args[MOCK239_RUNCLIENTSCRIPT_ARG_MAX] = { 0 };

    if( argc < 0 || argc > MOCK239_RUNCLIENTSCRIPT_ARG_MAX )
    {
        buf->overflow = 1;
        return;
    }
    for( int i = 0; i < argc; i++ )
    {
        args[i].int_value = intv ? intv[i] : 0;
        args[i].string_value = strv ? strv[i] : NULL;
    }
    if( !mock239_encode_runclientscript(buf, script_id, types, args, argc) )
        buf->overflow = 1;
}

/* ------------------------------------------------------------------ */
/* Camera                                                              */
/* ------------------------------------------------------------------ */

/* CAM_MOVETO_V3 / CAM_LOOKAT_V3 (the V2 prots went at 241). The extra
 * heightRelative byte is 0 -- the height is the V2 meaning. */
static void
w242_cam_moveto(struct RSAreaBuf* buf, int x, int z, int height, int rate, int rate2)
{
    MsgCamMoveToV3 m = { 0 };
    m.x = x;
    m.z = z;
    m.height = height;
    m.rate = rate;
    m.rate2 = rate2;
    m.height_relative = 0;
    W242(rsprot_cam_moveto_v3_out, m);
}

/* The eased pair. height_relative is 0 (an absolute height) and
 * track_target 0, as every recorded send has them (RSProx 4293 [174..205]). */
static void
w242_cam_moveto_cycles(struct RSAreaBuf* buf, int x, int z, int height, int cycles,
                       int ignore_terrain, int easing)
{
    MsgCamMoveToCyclesV3 m = { 0 };
    m.x = x;
    m.z = z;
    m.height = height;
    m.cycles = cycles;
    m.ignore_terrain = ignore_terrain;
    m.easing_id = easing;
    m.height_relative = 0;
    W242(rsprot_cam_moveto_cycles_v3_out, m);
}

static void
w242_cam_rotateto_coordinate(struct RSAreaBuf* buf, int x, int z, int height, int cycles, int easing)
{
    MsgCamRotateToCoordinateV3 m = { 0 };
    m.x = x;
    m.z = z;
    m.height = height;
    m.cycles = cycles;
    m.easing_id = easing;
    m.height_relative = 0;
    m.track_target = 0;
    W242(rsprot_cam_rotateto_coordinate_v3_out, m);
}

static void
w242_cam_lookat(struct RSAreaBuf* buf, int x, int z, int height, int rate, int rate2)
{
    MsgCamLookAtV3 m = { 0 };
    m.x = x;
    m.z = z;
    m.height = height;
    m.rate = rate;
    m.rate2 = rate2;
    m.height_relative = 0;
    W242(rsprot_cam_lookat_v3_out, m);
}

static void
w242_cam_shake(struct RSAreaBuf* buf, int axis, int random, int amplitude, int rate)
{
    MsgCamShake m = { 0 };
    m.axis = axis;
    m.random = random;
    m.amplitude = amplitude;
    m.rate = rate;
    W242(rsprot_cam_shake_out, m);
}

/* CAM_UNLOCK: RSProt cam_unlock's 242 row. */
static void
w242_cam_unlock(struct RSAreaBuf* buf, int unlock)
{
    MsgCamUnlock m = { 0 };
    m.unlock = unlock;
    W242(rsprot_cam_unlock_out, m);
}

/* ------------------------------------------------------------------ */
/* Map                                                                 */
/* ------------------------------------------------------------------ */

static void
w242_rebuild_normal(
    struct RSAreaBuf* buf,
    int world_area,
    int zone_x,
    int zone_z,
    const int32_t* keys,
    int key_squares)
{
    MsgRebuildNormalV2 m = { 0 };
    (void)keys;
    (void)key_squares;
    m.world_area = world_area;
    m.zone_x = zone_x;
    m.zone_z = zone_z;
    W242(rsprot_rebuild_normal_v2_out, m);
}

static void
w242_rebuild_region(
    struct RSAreaBuf* buf,
    int zone_x,
    int zone_z,
    int reload,
    int32_t const* zones,
    int distinct_squares)
{
    static MsgRebuildRegionV2 m;

    _Static_assert(sizeof(m.zones) / sizeof(m.zones[0]) ==
                       TORIRSSERVER_MAPINSTANCE_LEVELS * TORIRSSERVER_MAPINSTANCE_SCENE_ZONES *
                           TORIRSSERVER_MAPINSTANCE_SCENE_ZONES,
                   "the codec's grid is the instance window");
    memset(&m, 0, sizeof(m));
    m.zone_x = zone_x;
    m.zone_z = zone_z;
    m.reload = reload ? 1 : 0;
    m.distinct_mapsquare_count = distinct_squares;
    memcpy(m.zones, zones, sizeof(m.zones));
    W242(rsprot_rebuild_region_v2_out, m);
}

/* REBUILD_WORLDENTITY_V4: RSProt states 241's layout for 242. */
static void
w242_rebuild_worldentity(struct RSAreaBuf* buf, int base_x, int base_z, int source_squares)
{
    rsab_p2(buf, base_x);
    rsab_p2(buf, base_z);
    rsab_p2(buf, source_squares);
}

/*
 * WORLDENTITY_INFO_V7's spawn record at 242 (RSProt worldentity_info osrs242,
 * from RSProx's WorldEntityInfoClient): g2 index (written by the caller),
 * g2s config id, g1 priority, g1Alt2 packed size, g1Alt3 flags.
 */
static void
w242_wev_spawn_header(
    struct RSAreaBuf* buf,
    int flags,
    int size_byte,
    int priority_group,
    int config_id)
{
    rsab_p2(buf, config_id);
    rsab_p1(buf, priority_group);
    rsab_p1_alt2(buf, size_byte);
    rsab_p1_alt3(buf, flags);
}

/* Unused at 242 -- the spawn head is one hook (above) -- but the slot must not
 * be NULL: the encoder treats a NULL as "this wire has no world entities". */
static void
w242_wev_spawn_scalars(struct RSAreaBuf* buf, int size_byte, int priority_group, int config_id)
{
    w242_wev_spawn_header(buf, 0, size_byte, priority_group, config_id);
}

/* Enabled ops: flag 0x1 at 242 (wev_ops_flag), g1Alt1, after the sequence. */
static void
w242_wev_op_mask(struct RSAreaBuf* buf, int op_mask)
{
    rsab_p1_alt1(buf, op_mask);
}

/* The sequence block: flag 0x2 at 242 (wev_seq_flag), g2 id, g1Alt3 delay. */
static void
w242_wev_seq(struct RSAreaBuf* buf, int seq_id, int delay)
{
    rsab_p2(buf, seq_id & 0xffff);
    rsab_p1_alt3(buf, delay & 0xff);
}

/* ------------------------------------------------------------------ */
/* Zones                                                               */
/* ------------------------------------------------------------------ */

static void
w242_zone_header(struct RSAreaBuf* buf, int pkt_name, int zone_x, int zone_z, int level)
{
    switch( pkt_name )
    {
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
    {
        MsgUpdateZoneFullFollows m = { 0 };
        m.zone_x = zone_x;
        m.zone_z = zone_z;
        m.level = level;
        W242(rsprot_update_zone_full_follows_out, m);
        break;
    }
    case PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS:
    {
        MsgUpdateZonePartialFollows m = { 0 };
        m.zone_x = zone_x;
        m.zone_z = zone_z;
        m.level = level;
        W242(rsprot_update_zone_partial_follows_out, m);
        break;
    }
    default: /* PARTIAL_ENCLOSED */
    {
        MsgDesktopUpdateZonePartialEnclosed m = { 0 };
        m.zone_x = zone_x;
        m.zone_z = zone_z;
        m.level = level;
        W242(rsprot_update_zone_partial_enclosed_out, m);
        break;
    }
    }
}

#define OSRS242_LOC_OPS_ALL_SHOWN 0x1f

/*
 * The zone sub-packets, by codec. The value conversions are w239_zone_payload's
 * and are explained there: the 0-based op slot (key - 1 on the wire), the
 * client player index for LOC_MERGE and the projectile ends, the heights
 * times four, the absolute packed destination.
 */
static int
w242_zone_payload(
    struct RSAreaBuf* buf,
    int pkt_name,
    const struct ToriRSServerZoneEvent* event,
    int pos,
    int props)
{
    switch( pkt_name )
    {
    case PKT_NAME_LOC_ADD_CHANGE:
    {
        MsgLocAddChangeV2 m = { 0 };
        Rsprot_MsgLocAddChangeV2_opsElem ops[5];
        int count = 0;

        for( int i = 0; i < 5; i++ )
        {
            if( event->ops.name[i][0] == '\0' )
                continue;
            ops[count].key = i + 1;
            ops[count].value = event->ops.name[i];
            count++;
        }
        m.ops = ops;
        m.ops_count = count;
        m.op_flags = event->ops.flags & OSRS242_LOC_OPS_ALL_SHOWN;
        m.loc_properties_packed = props;
        m.coord_in_zone_packed = pos;
        m.id = event->id;
        W242(rsprot_loc_add_change_v2_out, m);
        return 1;
    }
    case PKT_NAME_LOC_DEL:
    {
        MsgLocDel m = { 0 };
        m.loc_properties_packed = props;
        m.coord_in_zone_packed = pos;
        W242(rsprot_loc_del_out, m);
        return 1;
    }
    case PKT_NAME_LOC_ANIM:
    {
        MsgLocAnim m = { 0 };
        m.loc_properties_packed = props;
        m.coord_in_zone_packed = pos;
        m.id = event->id;
        W242(rsprot_loc_anim_out, m);
        return 1;
    }
    case PKT_NAME_MAP_ANIM:
    {
        MsgMapAnim m = { 0 };
        m.height = event->src_height;
        m.id = event->id;
        m.delay = event->start_delay;
        m.coord_in_zone_packed = pos;
        W242(rsprot_map_anim_out, m);
        return 1;
    }
    case PKT_NAME_SOUND_AREA:
    {
        MsgSoundArea m = { 0 };
        m.coord_in_zone_packed = pos;
        m.drop_off_range = event->old_count;
        m.range = event->count;
        m.delay = event->start_delay;
        m.loops = event->shape;
        m.id = event->id;
        W242(rsprot_sound_area_out, m);
        return 1;
    }
    case PKT_NAME_MAP_PROJANIM:
    {
        MsgMapProjAnimV2 m = { 0 };
        m.start_time = event->start_delay;
        m.coord_in_zone_packed = pos;
        m.progress = event->arc;
        m.id = event->id;
        m.end_time = event->end_delay;
        m.end_packed = ((event->dst_level & 3) << 28) | ((event->dst_x & 0x3fff) << 14) |
                       (event->dst_z & 0x3fff);
        m.end_height = event->heights_raw ? event->dst_height : event->dst_height * 4;
        m.source_index = event->source < 0 ? event->source - 1 : event->source;
        m.angle = event->peak;
        m.start_height = event->heights_raw ? event->src_height : event->src_height * 4;
        m.target_index = event->target < 0 ? event->target - 1 : event->target;
        W242(rsprot_map_projanim_v2_out, m);
        return 1;
    }
    /*
     * ScriptedProjAddDecoder (RSProx osrs-241; RSProt carries 242's row). The source is the shot's own
     * tile (`pos`) and the target an absolute CoordGrid, each with an actor
     * index the client prefers when it can find it (deob class56.method1476);
     * the index convention is MAP_PROJANIM_V2's. Heights are raw -- the deob
     * subtracts them from the ground as they are -- and the four sub-tile
     * offsets are 0 in every recorded shot.
     */
    case PKT_NAME_SCRIPTEDPROJ_ADD:
    {
        MsgScriptedProjAdd m = { 0 };
        m.curve_script_a = event->curve_a;
        m.source_index = event->source < 0 ? event->source - 1 : event->source;
        m.source_height = event->src_height;
        m.target_height = event->dst_height;
        m.id = event->id;
        m.target_index = event->target < 0 ? event->target - 1 : event->target;
        m.end_time = event->end_delay;
        m.slot = event->slot;
        m.start_time = event->start_delay;
        m.curve_script_t = event->curve_t;
        m.target_coord_packed = ((event->dst_level & 3) << 28) |
                                ((event->dst_x & 0x3fff) << 14) | (event->dst_z & 0x3fff);
        m.curve_script_h = event->curve_h;
        m.coord_in_zone_packed = pos;
        W242(rsprot_scriptedproj_add_out, m);
        return 1;
    }
    case PKT_NAME_OBJ_REVEAL:
    {
        MsgObjEnabledOps m = { 0 };
        m.coord_in_zone_packed = pos;
        m.id = event->id;
        m.op_flags = 0xff;
        W242(rsprot_obj_enabled_ops_out, m);
        return 1;
    }
    case PKT_NAME_OBJ_DEL:
    {
        MsgObjDel m = { 0 };
        m.id = event->id;
        m.coord_in_zone_packed = pos;
        m.quantity = event->count;
        W242(rsprot_obj_del_out, m);
        return 1;
    }
    case PKT_NAME_OBJ_COUNT:
    {
        MsgObjCount m = { 0 };
        m.old_quantity = event->old_count;
        m.coord_in_zone_packed = pos;
        m.new_quantity = event->count;
        m.id = event->id;
        W242(rsprot_obj_count_out, m);
        return 1;
    }
    case PKT_NAME_OBJ_ADD:
    {
        MsgObjAdd m = { 0 };
        m.coord_in_zone_packed = pos;
        m.time_until_public = 0;
        m.ownership_type = 0;
        m.id = event->id;
        m.never_becomes_public = 0;
        m.op_flags = 0xff;
        m.time_until_despawn = event->despawn_ticks & 0xffff;
        m.quantity = event->count;
        W242(rsprot_obj_add_out, m);
        return 1;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* The tables                                                          */
/* ------------------------------------------------------------------ */

static const struct ToriRSServerWirePayload k_payload_osrs242 = {
    .message_private = ToriRSServer_WireMessagePrivateOsrs,
    .if_movesub = w242_if_movesub,
    .if_setcolour = w242_if_setcolour,
    .if_sethide = w242_if_sethide,
    .if_setmodel = w242_if_setmodel,
    .if_setobject = w242_if_setobject,
    .if_setposition = w242_if_setposition,
    .if_setscroll = w242_if_setscroll,
    .if_setrotatespeed = w242_if_setrotatespeed,
    .if_setangle = w242_if_setangle,
    .if_setnpchead_active = w242_if_setnpchead_active,
    .if_setplayermodel_basecolour = w242_if_setplayermodel_basecolour,
    .if_setplayermodel_bodytype = w242_if_setplayermodel_bodytype,
    .if_setplayermodel_obj = w242_if_setplayermodel_obj,
    .if_setplayermodel_self = w242_if_setplayermodel_self,
    .message_game = w242_message_game,
    .if_settext = w242_if_settext,
    .if_setnpchead = w242_if_setnpchead,
    .if_setanim = w242_if_setanim,
    .if_setplayerhead = w242_if_setplayerhead,
    .set_map_flag = w242_set_map_flag,
    .chat_filter = w242_chat_filter,
    .synth_sound = w242_synth_sound,
    .midi_song = w242_midi_song,
    .midi_song_stop = w242_midi_song_stop,
    .midi_song_withsecondary = w242_midi_song_withsecondary,
    .midi_jingle = w242_midi_jingle,
    .update_stockmarket_slot = w242_update_stockmarket_slot,
    .varp_long = w242_varp_long,
    .ambientsound_start = w242_ambientsound_start,
    .ambientsound_stop = w242_ambientsound_stop,
    .friendlist_loaded = w242_friendlist_loaded,
    .friend_entry = w242_friend_entry,
    .inv_header = w242_inv_header,
    .inv_slot = w242_inv_slot,
    .inv_stop_transmit = w242_inv_stop_transmit,
    .run_clientscript = w242_run_clientscript,
    .ignore_entry = w242_ignore_entry,
    .zone_header = w242_zone_header,
    .zone_payload = w242_zone_payload,
    .cam_moveto = w242_cam_moveto,
    .cam_lookat = w242_cam_lookat,
    .cam_moveto_cycles = w242_cam_moveto_cycles,
    .cam_rotateto_coordinate = w242_cam_rotateto_coordinate,
    .cam_shake = w242_cam_shake,
    .cam_unlock = w242_cam_unlock,
    .rebuild_region = w242_rebuild_region,
    .rebuild_worldentity = w242_rebuild_worldentity,
    .wev_spawn_scalars = w242_wev_spawn_scalars,
    .wev_spawn_header = w242_wev_spawn_header,
    .wev_op_mask = w242_wev_op_mask,
    .wev_seq = w242_wev_seq,
    .wev_seq_flag = 0x2,
    .wev_ops_flag = 0x1,
    .if_opentop = w242_if_opentop,
    .if_opensub = w242_if_opensub,
    .if_closesub = w242_if_closesub,
    .if_setevents = w242_if_setevents,
    .varp_small = w242_varp_small,
    .varp_large = w242_varp_large,
    .update_runenergy = w242_update_runenergy,
    .update_runweight = w242_update_runweight,
    .update_stat = w242_update_stat,
    .rebuild_normal = w242_rebuild_normal,
};

/* 241's list less LOC_MERGE: every one of these has a 242 writer above or a
 * 242 branch at its send site in torirs_server_encode.c. */
static const int k_transcribed_osrs242[] = {
    /* MessagePrivateEncoder through the `message_private` hook: display name,
     * world, 24-bit counter, crown, then the packed text. */
    PKT_NAME_MESSAGE_PRIVATE,
    PKT_NAME_IF_OPENTOP,       PKT_NAME_IF_OPENSUB,      PKT_NAME_IF_CLOSESUB,
    PKT_NAME_IF_MOVESUB,       PKT_NAME_IF_RESYNC_V2,    PKT_NAME_IF_CLEARINV,
    PKT_NAME_IF_SETEVENTS,     PKT_NAME_IF_SETCOLOUR,
    PKT_NAME_IF_SETHIDE,       PKT_NAME_IF_SETMODEL,     PKT_NAME_IF_SETOBJECT,
    PKT_NAME_IF_SETPOSITION,   PKT_NAME_IF_SETSCROLLPOS,
    PKT_NAME_IF_SETROTATESPEED, PKT_NAME_IF_SETANGLE,
    PKT_NAME_IF_SETNPCHEAD_ACTIVE,
    PKT_NAME_IF_SETPLAYERMODEL_BASECOLOUR,
    PKT_NAME_IF_SETPLAYERMODEL_BODYTYPE,
    PKT_NAME_IF_SETPLAYERMODEL_OBJ,
    PKT_NAME_IF_SETPLAYERMODEL_SELF,
    PKT_NAME_VARP_SMALL,       PKT_NAME_VARP_LARGE,
    PKT_NAME_UPDATE_RUNENERGY, PKT_NAME_UPDATE_RUNWEIGHT, PKT_NAME_UPDATE_STAT,
    PKT_NAME_REBUILD_NORMAL,   PKT_NAME_REBUILD_REGION,
    PKT_NAME_CAM_MOVETO_CYCLES, PKT_NAME_CAM_ROTATETO_COORDINATE,
    PKT_NAME_CAM_MOVETO,       PKT_NAME_CAM_LOOKAT,      PKT_NAME_CAM_SHAKE,
    PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS, PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS,
    PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED,
    PKT_NAME_LOC_ADD_CHANGE,   PKT_NAME_LOC_DEL,         PKT_NAME_LOC_ANIM,
    PKT_NAME_MAP_ANIM,         PKT_NAME_OBJ_ADD,         PKT_NAME_OBJ_DEL,
    PKT_NAME_OBJ_COUNT,        PKT_NAME_MAP_PROJANIM,
    PKT_NAME_SOUND_AREA,
    PKT_NAME_SCRIPTEDPROJ_ADD,
    PKT_NAME_MAP_ANIM_SPECIFIC,
    PKT_NAME_OBJ_REVEAL,       PKT_NAME_MESSAGE_GAME,    PKT_NAME_IF_SETTEXT,
    PKT_NAME_IF_SETNPCHEAD,    PKT_NAME_IF_SETANIM,      PKT_NAME_IF_SETPLAYERHEAD,
    PKT_NAME_UNSET_MAP_FLAG,   PKT_NAME_CHAT_FILTER_SETTINGS,
    PKT_NAME_MINIMAP_TOGGLE,
    PKT_NAME_CAM_UNLOCK,
    PKT_NAME_HIDENPCOPS,       PKT_NAME_HIDELOCOPS,      PKT_NAME_HIDEOBJOPS,
    PKT_NAME_HINT_ARROW,
    PKT_NAME_SYNTH_SOUND,      PKT_NAME_MIDI_SONG,          PKT_NAME_MIDI_SONG_STOP,
    PKT_NAME_MIDI_SONG_WITHSECONDARY,
    PKT_NAME_MIDI_JINGLE,
    PKT_NAME_UPDATE_STOCKMARKET_SLOT,
    PKT_NAME_VARP_LONG,
    PKT_NAME_FRIENDLIST_LOADED,
    PKT_NAME_AMBIENTSOUND_START, PKT_NAME_AMBIENTSOUND_STOP,
    PKT_NAME_UPDATE_INV_FULL,  PKT_NAME_UPDATE_INV_PARTIAL,
    PKT_NAME_UPDATE_INV_STOP_TRANSMIT,
    PKT_NAME_RUNCLIENTSCRIPT,  PKT_NAME_UPDATE_FRIENDLIST,
    PKT_NAME_UPDATE_IGNORELIST,
    PKT_NAME_PLAYER_INFO,
    PKT_NAME_NPC_INFO,
    PKT_NAME_SET_NPC_UPDATE_ORIGIN,
    PKT_NAME_SET_ACTIVE_WORLD,
    PKT_NAME_REBUILD_WORLDENTITY,
    PKT_NAME_WORLDENTITY_INFO,
    PKT_NAME_SERVER_TICK_END, PKT_NAME_VARP_RESET, PKT_NAME_VARP_SYNC,
    PKT_NAME_CAM_RESET,       PKT_NAME_RESET_ANIMS,
    PKT_NAME_TRIGGER_ONDIALOGABORT,
    /* SET_PLAYER_OP is written by its send site (242 branch). */
    PKT_NAME_SET_PLAYER_OP,
};

static const struct ToriRSServerWire k_wire_osrs242 = {
    .name = "osrs242",
    .revision = 242,
    .opcode = osrs242_opcode,
    .payload_size = osrs242_size,
    .prot_name = osrs242_prot_name,
    .zone_sub_code = osrs242_zone_sub,
    .packetout_size = osrs242_out_size,
    .packetout_name = osrs242_out_name,
    .packetout_prot_name = osrs242_out_prot_name,
    .translate_in = mock242_inbound_translate,
    .opcode_smart2 = 1, /* server opcodes reach 150 */
    .payload = &k_payload_osrs242,
    .transcribed = k_transcribed_osrs242,
    .transcribed_count = (int)(sizeof(k_transcribed_osrs242) / sizeof(k_transcribed_osrs242[0])),
};

const struct ToriRSServerWire*
ToriRSServer_Wire242(void)
{
    return &k_wire_osrs242;
}
