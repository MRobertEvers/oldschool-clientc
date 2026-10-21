#include "torirs_server_wire.h"

#include "mock239_runclientscript.h"
#include "mock241_inbound.h"
#include "torirs_server_scene.h"
#include "torirs_server_zone.h"

#include "net/rev/osrs241/packetin.h"
#include "net/rev/osrs241/packetout.h"
#include "net/rev/osrs241/zoneprot.h"

#include "rsprot_buf.h"
#include "rsprot_exec.h"

#include "packets/ambientsound_start.h"
#include "packets/ambientsound_stop.h"
#include "packets/cam_lookat_v3.h"
#include "packets/cam_moveto_v3.h"
#include "packets/cam_moveto_cycles_v3.h"
#include "packets/cam_rotateto_coordinate_v3.h"
#include "packets/cam_shake.h"
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
#include "packets/loc_merge.h"
#include "packets/map_anim.h"
#include "packets/map_projanim_v2.h"
#include "packets/message_game.h"
#include "packets/midi_jingle.h"
#include "packets/midi_song_stop.h"
#include "packets/midi_song_v2.h"
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
#include <string.h>

/*
 * The revision-241 wire: a COPY of torirs_server_wire.c's osrs239 wire,
 * changed to 241.
 *
 * WHERE THE BYTES COME FROM. 239's writers are hand transcriptions of RSProt's
 * encoders. RSProt has no 241, so this file does not transcribe anything: every
 * fixed-layout server prot is written by running the GENERATED codec in
 * 3rd/rsprot/packets/ at revision 241 in the encode direction -- the very
 * function the 241 client's bridge (src/net/rev/rsprot_bridge.c) runs to read
 * it. Those 241 rows were derived from RSProx's osrs-241 decoders by
 * tools/rsprox_codec_overlay.py and checked field for field against the 239
 * pairing, so the server's writer and the client's reader are one statement of
 * the layout and cannot drift.
 *
 * Writers whose 241 body is identical to 239's (friend/ignore entries,
 * RUNCLIENTSCRIPT, the inventory header) are the same bytes 239 writes; the
 * only hand-written ones here say which RSProx decoder they mirror.
 */

/* ------------------------------------------------------------------ */
/* Running a codec into an RSAreaBuf                                   */
/* ------------------------------------------------------------------ */

static void
w241_run(struct RSAreaBuf* buf, RsprotVersionRange const* ranges, int count, void* msg)
{
    uint8_t tmp[1024];
    RsprotBuf b;
    RsprotExec x;
    RsprotCodecFn fn = rsprot_version_pick(ranges, count, 241);

    /* Every table this file names has a 241 row (the overlay's report lists
     * them); a NULL here is a generator regression, not a runtime case. */
    assert(fn);
    rsprot_buf_wrap(&b, tmp, (int32_t)sizeof(tmp));
    rsprot_exec_encode(&x, &b);
    fn(&x, msg);
    assert(rsprot_exec_ok(&x));
    assert(!b.err);
    rsab_pdata(buf, tmp, (size_t)b.wpos);
}

#define W241(TABLE, MSG) w241_run(buf, TABLE, TABLE##_count, &(MSG))

/* ------------------------------------------------------------------ */
/* Opcode resolution                                                   */
/* ------------------------------------------------------------------ */

static int
osrs241_opcode(int pkt_name)
{
    return packetin_wire_osrs241(pkt_name);
}

static int
osrs241_size(int wire_opcode)
{
    return packetin_size_osrs241(wire_opcode);
}

static char const*
osrs241_prot_name(int wire_opcode)
{
    return packetin_protname_osrs241(wire_opcode);
}

static int
osrs241_out_size(int wire_opcode)
{
    return osrs241_packetout_size(wire_opcode);
}

static int
osrs241_out_name(int wire_opcode)
{
    return osrs241_packetout_name(wire_opcode);
}

static char const*
osrs241_out_prot_name(int wire_opcode)
{
    return osrs241_packetout_protname(wire_opcode);
}

/* The enclosed sub-packet ordinal: RSProx 241's IndexedZoneProtDecoder order,
 * which is a different permutation from 239's (zoneprot.h). */
static int
osrs241_zone_sub(int pkt_name)
{
    switch( pkt_name )
    {
    case PKT_NAME_LOC_DEL: return OSRS241_ZONE_LOC_DEL;
    case PKT_NAME_LOC_MERGE: return OSRS241_ZONE_LOC_MERGE;
    case PKT_NAME_OBJ_COUNT: return OSRS241_ZONE_OBJ_COUNT;
    case PKT_NAME_MAP_ANIM: return OSRS241_ZONE_MAP_ANIM;
    case PKT_NAME_SOUND_AREA: return OSRS241_ZONE_SOUND_AREA;
    case PKT_NAME_LOC_ADD_CHANGE: return OSRS241_ZONE_LOC_ADD_CHANGE_V2;
    case PKT_NAME_OBJ_ADD: return OSRS241_ZONE_OBJ_ADD;
    case PKT_NAME_LOC_ANIM: return OSRS241_ZONE_LOC_ANIM;
    case PKT_NAME_OBJ_DEL: return OSRS241_ZONE_OBJ_DEL;
    case PKT_NAME_OBJ_REVEAL: return OSRS241_ZONE_OBJ_ENABLED_OPS;
    case PKT_NAME_MAP_PROJANIM: return OSRS241_ZONE_MAP_PROJANIM_V2;
    default: return -1;
    }
}

/* ------------------------------------------------------------------ */
/* Interfaces                                                          */
/* ------------------------------------------------------------------ */

static void
w241_if_opentop(struct RSAreaBuf* buf, int interface_id)
{
    MsgIfOpenTop m = { 0 };
    m.interface_id = interface_id;
    W241(rsprot_if_opentop_out, m);
}

static void
w241_if_opensub(struct RSAreaBuf* buf, int interface_id, int dest_uid, int type)
{
    MsgIfOpenSub m = { 0 };
    m.interface_id = interface_id;
    m.destination_combined_id = dest_uid;
    m.type = type;
    W241(rsprot_if_opensub_out, m);
}

static void
w241_if_closesub(struct RSAreaBuf* buf, int dest_uid)
{
    MsgIfCloseSub m = { 0 };
    m.combined_id = dest_uid;
    W241(rsprot_if_closesub_out, m);
}

static void
w241_if_movesub(struct RSAreaBuf* buf, int source_uid, int dest_uid)
{
    MsgIfMoveSub m = { 0 };
    m.source_combined_id = source_uid;
    m.destination_combined_id = dest_uid;
    W241(rsprot_if_movesub_out, m);
}

/* Two masks, the button ops in the second -- see w239_if_setevents. */
static void
w241_if_setevents(
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
    W241(rsprot_if_setevents_v2_out, m);
}

static void
w241_if_setcolour(struct RSAreaBuf* buf, int component_uid, int colour)
{
    MsgIfSetColour m = { 0 };
    m.combined_id = component_uid;
    m.colour15_bit_packed = colour;
    W241(rsprot_if_setcolour_out, m);
}

static void
w241_if_sethide(struct RSAreaBuf* buf, int component_uid, int hide)
{
    MsgIfSetHide m = { 0 };
    m.combined_id = component_uid;
    m.hidden = hide ? 1 : 0;
    W241(rsprot_if_sethide_out, m);
}

static void
w241_if_setmodel(struct RSAreaBuf* buf, int component_uid, int model_id)
{
    MsgIfSetModelV2 m = { 0 };
    m.combined_id = component_uid;
    m.model = model_id;
    W241(rsprot_if_setmodel_v2_out, m);
}

static void
w241_if_setobject(struct RSAreaBuf* buf, int component_uid, int obj_id, int value)
{
    MsgIfSetObject m = { 0 };
    m.combined_id = component_uid;
    m.obj = obj_id;
    m.count = value;
    W241(rsprot_if_setobject_out, m);
}

static void
w241_if_setposition(struct RSAreaBuf* buf, int component_uid, int x, int y)
{
    MsgIfSetPosition m = { 0 };
    m.combined_id = component_uid;
    m.x = x;
    m.y = y;
    W241(rsprot_if_setposition_out, m);
}

static void
w241_if_setscroll(struct RSAreaBuf* buf, int component_uid, int position)
{
    MsgIfSetScrollPos m = { 0 };
    m.combined_id = component_uid;
    m.scroll_pos = position;
    W241(rsprot_if_setscrollpos_out, m);
}

static void
w241_if_setrotatespeed(struct RSAreaBuf* buf, int component_uid, int x_speed, int y_speed)
{
    MsgIfSetRotateSpeed m = { 0 };
    m.combined_id = component_uid;
    m.x_speed = x_speed;
    m.y_speed = y_speed;
    W241(rsprot_if_setrotatespeed_out, m);
}

static void
w241_if_setangle(struct RSAreaBuf* buf, int component_uid, int zoom, int angle_x, int angle_y)
{
    MsgIfSetAngle m = { 0 };
    m.combined_id = component_uid;
    m.zoom = zoom;
    m.angle_x = angle_x;
    m.angle_y = angle_y;
    W241(rsprot_if_setangle_out, m);
}

static void
w241_if_setnpchead_active(struct RSAreaBuf* buf, int component_uid, int index)
{
    MsgIfSetNpcHeadActive m = { 0 };
    m.combined_id = component_uid;
    m.index = index;
    W241(rsprot_if_setnpchead_active_out, m);
}

static void
w241_if_setplayermodel_basecolour(struct RSAreaBuf* buf, int component_uid, int index, int colour)
{
    MsgIfSetPlayerModelBaseColour m = { 0 };
    m.combined_id = component_uid;
    m.index = index;
    m.colour = colour;
    W241(rsprot_if_setplayermodel_basecolour_out, m);
}

static void
w241_if_setplayermodel_bodytype(struct RSAreaBuf* buf, int component_uid, int body_type)
{
    MsgIfSetPlayerModelBodyType m = { 0 };
    m.combined_id = component_uid;
    m.body_type = body_type;
    W241(rsprot_if_setplayermodel_bodytype_out, m);
}

static void
w241_if_setplayermodel_obj(struct RSAreaBuf* buf, int component_uid, int obj_id)
{
    MsgIfSetPlayerModelObj m = { 0 };
    m.combined_id = component_uid;
    m.obj = obj_id;
    W241(rsprot_if_setplayermodel_obj_out, m);
}

static void
w241_if_setplayermodel_self(struct RSAreaBuf* buf, int component_uid, int copy_objs)
{
    MsgIfSetPlayerModelSelf m = { 0 };
    m.combined_id = component_uid;
    m.copy_objs = copy_objs;
    W241(rsprot_if_setplayermodel_self_out, m);
}

static void
w241_if_settext(struct RSAreaBuf* buf, int uid, const char* text)
{
    MsgIfSetText m = { 0 };
    m.combined_id = uid;
    m.text = text ? text : "";
    W241(rsprot_if_settext_out, m);
}

static void
w241_if_setnpchead(struct RSAreaBuf* buf, int uid, int npc_id)
{
    MsgIfSetNpcHead m = { 0 };
    m.combined_id = uid;
    m.npc = npc_id;
    W241(rsprot_if_setnpchead_out, m);
}

static void
w241_if_setanim(struct RSAreaBuf* buf, int uid, int anim_id)
{
    MsgIfSetAnim m = { 0 };
    m.combined_id = uid;
    m.anim = anim_id;
    W241(rsprot_if_setanim_out, m);
}

static void
w241_if_setplayerhead(struct RSAreaBuf* buf, int uid)
{
    MsgIfSetPlayerHead m = { 0 };
    m.combined_id = uid;
    W241(rsprot_if_setplayerhead_out, m);
}

/* ------------------------------------------------------------------ */
/* Player state                                                        */
/* ------------------------------------------------------------------ */

static void
w241_message_game(struct RSAreaBuf* buf, int type, const char* text)
{
    MsgMessageGame m = { 0 };
    m.type = type;
    m.name_present = 0; /* no sender name, as at 239 */
    m.message = text ? text : "";
    W241(rsprot_message_game_out, m);
}

static void
w241_varp_small(struct RSAreaBuf* buf, int varp, int value)
{
    MsgVarpSmall m = { 0 };
    m.id = varp;
    m.value = value;
    W241(rsprot_varp_small_out, m);
}

static void
w241_varp_large(struct RSAreaBuf* buf, int varp, int value)
{
    MsgVarpLarge m = { 0 };
    m.id = varp;
    m.value = value;
    W241(rsprot_varp_large_out, m);
}

static void
w241_update_runenergy(struct RSAreaBuf* buf, int hundredths)
{
    MsgUpdateRunEnergy m = { 0 };
    m.runenergy = hundredths;
    W241(rsprot_update_runenergy_out, m);
}

static void
w241_update_runweight(struct RSAreaBuf* buf, int kilograms)
{
    MsgUpdateRunWeight m = { 0 };
    m.runweight = kilograms;
    W241(rsprot_update_runweight_out, m);
}

/* The boosted level in both level fields, as w239_update_stat explains. */
static void
w241_update_stat(struct RSAreaBuf* buf, int stat, int level, int xp, int boosted)
{
    MsgUpdateStatV2 m = { 0 };
    (void)level;
    m.stat = stat;
    m.current_level = boosted;
    m.invisible_boosted_level = boosted;
    m.experience = xp;
    W241(rsprot_update_stat_v2_out, m);
}

static void
w241_set_map_flag(struct RSAreaBuf* buf, int level, int x, int z)
{
    MsgSetMapFlagV2 m = { 0 };
    m.coord_grid_packed = ((level & 0x3) << 28) | ((x & 0x3fff) << 14) | (z & 0x3fff);
    W241(rsprot_set_map_flag_v2_out, m);
}

/* Two bytes; the private filter has its own packet (see w239_chat_filter). */
static void
w241_chat_filter(struct RSAreaBuf* buf, int public_, int private_, int trade)
{
    MsgChatFilterSettings m = { 0 };
    (void)private_;
    m.public_chat_filter = public_;
    m.trade_chat_filter = trade;
    W241(rsprot_chat_filter_settings_out, m);
}

/* ------------------------------------------------------------------ */
/* Sound                                                               */
/* ------------------------------------------------------------------ */

static void
w241_synth_sound(struct RSAreaBuf* buf, int id, int loops, int delay)
{
    MsgSynthSound m = { 0 };
    m.id = id;
    m.loops = loops;
    m.delay = delay;
    W241(rsprot_synth_sound_out, m);
}

static void
w241_midi_song(
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
    W241(rsprot_midi_song_v2_out, m);
}

static void
w241_midi_song_stop(struct RSAreaBuf* buf, int fade_out_delay, int fade_out_speed)
{
    MsgMidiSongStop m = { 0 };
    m.fade_out_delay = fade_out_delay;
    m.fade_out_speed = fade_out_speed;
    W241(rsprot_midi_song_stop_out, m);
}

static void
w241_midi_jingle(struct RSAreaBuf* buf, int id, int length_ms)
{
    MsgMidiJingle m = { 0 };
    m.id = id;
    m.length_in_millis = length_ms;
    W241(rsprot_midi_jingle_out, m);
}

static void
w241_ambientsound_start(struct RSAreaBuf* buf, int id, int fade)
{
    MsgAmbientSoundStart m = { 0 };
    m.id = id;
    m.fade = fade ? 1 : 0;
    W241(rsprot_ambientsound_start_out, m);
}

static void
w241_ambientsound_stop(struct RSAreaBuf* buf, int fade)
{
    MsgAmbientSoundStop m = { 0 };
    m.fade = fade ? 1 : 0;
    W241(rsprot_ambientsound_stop_out, m);
}

/* ------------------------------------------------------------------ */
/* Social and inventories -- unchanged bodies                          */
/* ------------------------------------------------------------------ */

/* FriendListLoaded: zero bytes, as at 239. */
static void
w241_friendlist_loaded(struct RSAreaBuf* buf, int status)
{
    (void)buf;
    (void)status;
}

/* UpdateFriendListDecoder is unchanged at 241: the 239 entry, byte for byte. */
static void
w241_friend_entry(struct RSAreaBuf* buf, const char* name, int world)
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

/* UpdateIgnoreListDecoder is unchanged at 241. */
static void
w241_ignore_entry(struct RSAreaBuf* buf, const char* name)
{
    rsab_p1(buf, 0x1);
    rsab_pjstr(buf, name ? name : "", RSAB_JSTR_NUL);
    rsab_pjstr(buf, "", RSAB_JSTR_NUL);
    rsab_pjstr(buf, "", RSAB_JSTR_NUL);
}

/* The inventory header is unchanged at 241 (gCombinedId -1, g2 inventory,
 * g2 capacity for the full form). */
static void
w241_inv_header(struct RSAreaBuf* buf, int pkt_name, int uid, int container, int capacity)
{
    (void)uid;
    rsab_p4(buf, (uint32_t)-1);
    rsab_p2(buf, container);
    if( pkt_name == PKT_NAME_UPDATE_INV_FULL )
        rsab_p2(buf, capacity);
}

/*
 * One slot. UPDATE_INV_FULL moved at 241 (RSProx UpdateInvFullDecoder): the id
 * LEADS -- g2Alt1 id+1 -- then the g1Alt3 count, escaping at 255 to a g4Alt2
 * count (239: count first, escape g4Alt3, id last). The PARTIAL form is
 * unchanged.
 */
static void
w241_inv_slot(struct RSAreaBuf* buf, int pkt_name, int slot, int obj_id, int count)
{
    if( pkt_name == PKT_NAME_UPDATE_INV_FULL )
    {
        if( obj_id < 0 )
        {
            rsab_p2_alt1(buf, 0);
            rsab_p1_alt3(buf, 0);
            return;
        }
        rsab_p2_alt1(buf, obj_id + 1);
        rsab_p1_alt3(buf, count > 0xff ? 0xff : count);
        if( count >= 255 )
            rsab_p4_alt2(buf, count);
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
w241_inv_stop_transmit(struct RSAreaBuf* buf, int component_uid, int inv_id)
{
    MsgUpdateInvStopTransmit m = { 0 };
    (void)component_uid;
    m.inventory_id = inv_id;
    W241(rsprot_update_inv_stoptransmit_out, m);
}

/* RunClientScriptDecoder is unchanged at 241. */
static void
w241_run_clientscript(
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

/* CAM_MOVETO_V3 / CAM_LOOKAT_V3: the V2 prots are gone at 241. The extra
 * heightRelative byte is 0 -- the height is the V2 meaning. */
static void
w241_cam_moveto(struct RSAreaBuf* buf, int x, int z, int height, int rate, int rate2)
{
    MsgCamMoveToV3 m = { 0 };
    m.x = x;
    m.z = z;
    m.height = height;
    m.rate = rate;
    m.rate2 = rate2;
    m.height_relative = 0;
    W241(rsprot_cam_moveto_v3_out, m);
}

/* The eased pair. height_relative is 0 (an absolute height) and
 * track_target 0, as every recorded send has them (RSProx 4293 [174..205]). */
static void
w241_cam_moveto_cycles(struct RSAreaBuf* buf, int x, int z, int height, int cycles,
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
    W241(rsprot_cam_moveto_cycles_v3_out, m);
}

static void
w241_cam_rotateto_coordinate(struct RSAreaBuf* buf, int x, int z, int height, int cycles, int easing)
{
    MsgCamRotateToCoordinateV3 m = { 0 };
    m.x = x;
    m.z = z;
    m.height = height;
    m.cycles = cycles;
    m.easing_id = easing;
    m.height_relative = 0;
    m.track_target = 0;
    W241(rsprot_cam_rotateto_coordinate_v3_out, m);
}

static void
w241_cam_lookat(struct RSAreaBuf* buf, int x, int z, int height, int rate, int rate2)
{
    MsgCamLookAtV3 m = { 0 };
    m.x = x;
    m.z = z;
    m.height = height;
    m.rate = rate;
    m.rate2 = rate2;
    m.height_relative = 0;
    W241(rsprot_cam_lookat_v3_out, m);
}

static void
w241_cam_shake(struct RSAreaBuf* buf, int axis, int random, int amplitude, int rate)
{
    MsgCamShake m = { 0 };
    m.axis = axis;
    m.random = random;
    m.amplitude = amplitude;
    m.rate = rate;
    W241(rsprot_cam_shake_out, m);
}

/* ------------------------------------------------------------------ */
/* Map                                                                 */
/* ------------------------------------------------------------------ */

static void
w241_rebuild_normal(
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
    W241(rsprot_rebuild_normal_v2_out, m);
}

static void
w241_rebuild_region(struct RSAreaBuf* buf, int zone_x, int zone_z, int reload)
{
    MsgRebuildRegionV2 m = { 0 };
    m.zone_x = zone_x;
    m.zone_z = zone_z;
    m.reload = reload ? 1 : 0;
    W241(rsprot_rebuild_region_v2_out, m);
}

/* RebuildWorldEntityV4Decoder is unchanged at 241. */
static void
w241_rebuild_worldentity(struct RSAreaBuf* buf, int base_x, int base_z, int source_squares)
{
    rsab_p2(buf, base_x);
    rsab_p2(buf, base_z);
    rsab_p2(buf, source_squares);
}

/*
 * WORLDENTITY_INFO_V7's spawn record at 241 (RSProx decodeLowResolutionV5):
 * g2 index (written by the caller), g2sAlt1 config id, g1Alt3 flags,
 * g1Alt2 priority, g1Alt3 packed size. The whole head after the index is
 * written here, flags included -- 239 wrote the flags byte plain between the
 * index and the scalars.
 */
static void
w241_wev_spawn_header(
    struct RSAreaBuf* buf,
    int flags,
    int size_byte,
    int priority_group,
    int config_id)
{
    rsab_p2_alt1(buf, config_id);
    rsab_p1_alt3(buf, flags);
    rsab_p1_alt2(buf, priority_group);
    rsab_p1_alt3(buf, size_byte);
}

/* Unused at 241 -- the spawn head is one hook (above) -- but the slot must not
 * be NULL: the encoder treats a NULL as "this wire has no world entities". */
static void
w241_wev_spawn_scalars(struct RSAreaBuf* buf, int size_byte, int priority_group, int config_id)
{
    w241_wev_spawn_header(buf, 0, size_byte, priority_group, config_id);
}

/* Enabled ops: g1Alt2 at 241 (239: g1Alt3), and AFTER the sequence block. */
static void
w241_wev_op_mask(struct RSAreaBuf* buf, int op_mask)
{
    rsab_p1_alt2(buf, op_mask);
}

/* The 0x1 sequence block: g2 id, g1Alt2 delay (239: plain delay). */
static void
w241_wev_seq(struct RSAreaBuf* buf, int seq_id, int delay)
{
    rsab_p2(buf, seq_id & 0xffff);
    rsab_p1_alt2(buf, delay & 0xff);
}

/* ------------------------------------------------------------------ */
/* Zones                                                               */
/* ------------------------------------------------------------------ */

static void
w241_zone_header(struct RSAreaBuf* buf, int pkt_name, int zone_x, int zone_z, int level)
{
    switch( pkt_name )
    {
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
    {
        MsgUpdateZoneFullFollows m = { 0 };
        m.zone_x = zone_x;
        m.zone_z = zone_z;
        m.level = level;
        W241(rsprot_update_zone_full_follows_out, m);
        break;
    }
    case PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS:
    {
        MsgUpdateZonePartialFollows m = { 0 };
        m.zone_x = zone_x;
        m.zone_z = zone_z;
        m.level = level;
        W241(rsprot_update_zone_partial_follows_out, m);
        break;
    }
    default: /* PARTIAL_ENCLOSED */
    {
        MsgDesktopUpdateZonePartialEnclosed m = { 0 };
        m.zone_x = zone_x;
        m.zone_z = zone_z;
        m.level = level;
        W241(rsprot_update_zone_partial_enclosed_out, m);
        break;
    }
    }
}

#define OSRS241_LOC_OPS_ALL_SHOWN 0x1f

/*
 * The zone sub-packets, by codec. The value conversions are w239_zone_payload's
 * and are explained there: the 0-based op slot (key - 1 on the wire), the
 * client player index for LOC_MERGE and the projectile ends, the heights
 * times four, the absolute packed destination.
 */
static int
w241_zone_payload(
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
        m.op_flags = event->ops.flags & OSRS241_LOC_OPS_ALL_SHOWN;
        m.loc_properties_packed = props;
        m.coord_in_zone_packed = pos;
        m.id = event->id;
        W241(rsprot_loc_add_change_v2_out, m);
        return 1;
    }
    case PKT_NAME_LOC_DEL:
    {
        MsgLocDel m = { 0 };
        m.loc_properties_packed = props;
        m.coord_in_zone_packed = pos;
        W241(rsprot_loc_del_out, m);
        return 1;
    }
    case PKT_NAME_LOC_ANIM:
    {
        MsgLocAnim m = { 0 };
        m.loc_properties_packed = props;
        m.coord_in_zone_packed = pos;
        m.id = event->id;
        W241(rsprot_loc_anim_out, m);
        return 1;
    }
    case PKT_NAME_MAP_ANIM:
    {
        MsgMapAnim m = { 0 };
        m.height = event->src_height;
        m.id = event->id;
        m.delay = event->start_delay;
        m.coord_in_zone_packed = pos;
        W241(rsprot_map_anim_out, m);
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
        W241(rsprot_sound_area_out, m);
        return 1;
    }
    case PKT_NAME_LOC_MERGE:
    {
        MsgLocMerge m = { 0 };
        m.min_x = event->west;
        m.min_z = event->south;
        m.max_x = event->east;
        m.max_z = event->north;
        m.index = ToriRSServer_WirePlayerIndex(event->player_pid);
        m.loc_properties_packed = props;
        m.id = event->id;
        m.start = event->start_cycle;
        m.end = event->end_cycle;
        m.coord_in_zone_packed = pos;
        W241(rsprot_loc_merge_out, m);
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
        m.end_height = event->dst_height * 4;
        m.source_index = event->source < 0 ? event->source - 1 : event->source;
        m.angle = event->peak;
        m.start_height = event->src_height * 4;
        m.target_index = event->target < 0 ? event->target - 1 : event->target;
        W241(rsprot_map_projanim_v2_out, m);
        return 1;
    }
    case PKT_NAME_OBJ_REVEAL:
    {
        MsgObjEnabledOps m = { 0 };
        m.coord_in_zone_packed = pos;
        m.id = event->id;
        m.op_flags = 0xff;
        W241(rsprot_obj_enabled_ops_out, m);
        return 1;
    }
    case PKT_NAME_OBJ_DEL:
    {
        MsgObjDel m = { 0 };
        m.id = event->id;
        m.coord_in_zone_packed = pos;
        m.quantity = event->count;
        W241(rsprot_obj_del_out, m);
        return 1;
    }
    case PKT_NAME_OBJ_COUNT:
    {
        MsgObjCount m = { 0 };
        m.old_quantity = event->old_count;
        m.coord_in_zone_packed = pos;
        m.new_quantity = event->count;
        m.id = event->id;
        W241(rsprot_obj_count_out, m);
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
        W241(rsprot_obj_add_out, m);
        return 1;
    }
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* The tables                                                          */
/* ------------------------------------------------------------------ */

static const struct ToriRSServerWirePayload k_payload_osrs241 = {
    .if_movesub = w241_if_movesub,
    .if_setcolour = w241_if_setcolour,
    .if_sethide = w241_if_sethide,
    .if_setmodel = w241_if_setmodel,
    .if_setobject = w241_if_setobject,
    .if_setposition = w241_if_setposition,
    .if_setscroll = w241_if_setscroll,
    .if_setrotatespeed = w241_if_setrotatespeed,
    .if_setangle = w241_if_setangle,
    .if_setnpchead_active = w241_if_setnpchead_active,
    .if_setplayermodel_basecolour = w241_if_setplayermodel_basecolour,
    .if_setplayermodel_bodytype = w241_if_setplayermodel_bodytype,
    .if_setplayermodel_obj = w241_if_setplayermodel_obj,
    .if_setplayermodel_self = w241_if_setplayermodel_self,
    .message_game = w241_message_game,
    .if_settext = w241_if_settext,
    .if_setnpchead = w241_if_setnpchead,
    .if_setanim = w241_if_setanim,
    .if_setplayerhead = w241_if_setplayerhead,
    .set_map_flag = w241_set_map_flag,
    .chat_filter = w241_chat_filter,
    .synth_sound = w241_synth_sound,
    .midi_song = w241_midi_song,
    .midi_song_stop = w241_midi_song_stop,
    .midi_jingle = w241_midi_jingle,
    .ambientsound_start = w241_ambientsound_start,
    .ambientsound_stop = w241_ambientsound_stop,
    .friendlist_loaded = w241_friendlist_loaded,
    .friend_entry = w241_friend_entry,
    .inv_header = w241_inv_header,
    .inv_slot = w241_inv_slot,
    .inv_stop_transmit = w241_inv_stop_transmit,
    .run_clientscript = w241_run_clientscript,
    .ignore_entry = w241_ignore_entry,
    .zone_header = w241_zone_header,
    .zone_payload = w241_zone_payload,
    .cam_moveto = w241_cam_moveto,
    .cam_lookat = w241_cam_lookat,
    .cam_moveto_cycles = w241_cam_moveto_cycles,
    .cam_rotateto_coordinate = w241_cam_rotateto_coordinate,
    .cam_shake = w241_cam_shake,
    .rebuild_region = w241_rebuild_region,
    .rebuild_worldentity = w241_rebuild_worldentity,
    .wev_spawn_scalars = w241_wev_spawn_scalars,
    .wev_spawn_header = w241_wev_spawn_header,
    .wev_op_mask = w241_wev_op_mask,
    .wev_seq = w241_wev_seq,
    .if_opentop = w241_if_opentop,
    .if_opensub = w241_if_opensub,
    .if_closesub = w241_if_closesub,
    .if_setevents = w241_if_setevents,
    .varp_small = w241_varp_small,
    .varp_large = w241_varp_large,
    .update_runenergy = w241_update_runenergy,
    .update_runweight = w241_update_runweight,
    .update_stat = w241_update_stat,
    .rebuild_normal = w241_rebuild_normal,
};

/* The 239 list, packet for packet: every one of these has a 241 writer above
 * or a 241 branch at its send site in torirs_server_encode.c. */
static const int k_transcribed_osrs241[] = {
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
    PKT_NAME_OBJ_COUNT,        PKT_NAME_LOC_MERGE,       PKT_NAME_MAP_PROJANIM,
    PKT_NAME_SOUND_AREA,
    PKT_NAME_MAP_ANIM_SPECIFIC,
    PKT_NAME_OBJ_REVEAL,       PKT_NAME_MESSAGE_GAME,    PKT_NAME_IF_SETTEXT,
    PKT_NAME_IF_SETNPCHEAD,    PKT_NAME_IF_SETANIM,      PKT_NAME_IF_SETPLAYERHEAD,
    PKT_NAME_UNSET_MAP_FLAG,   PKT_NAME_CHAT_FILTER_SETTINGS,
    PKT_NAME_MINIMAP_TOGGLE,
    PKT_NAME_HIDENPCOPS,       PKT_NAME_HIDELOCOPS,      PKT_NAME_HIDEOBJOPS,
    PKT_NAME_HINT_ARROW,
    PKT_NAME_SYNTH_SOUND,      PKT_NAME_MIDI_SONG,          PKT_NAME_MIDI_SONG_STOP,
    PKT_NAME_MIDI_JINGLE,
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
    /* SetPlayerOpDecoder at 241 is written by its send site (241 branch). */
    PKT_NAME_SET_PLAYER_OP,
};

static const struct ToriRSServerWire k_wire_osrs241 = {
    .name = "osrs241",
    .revision = 241,
    .opcode = osrs241_opcode,
    .payload_size = osrs241_size,
    .prot_name = osrs241_prot_name,
    .zone_sub_code = osrs241_zone_sub,
    .packetout_size = osrs241_out_size,
    .packetout_name = osrs241_out_name,
    .packetout_prot_name = osrs241_out_prot_name,
    .translate_in = mock241_inbound_translate,
    .opcode_smart2 = 1, /* server opcodes reach 150 */
    .payload = &k_payload_osrs241,
    .transcribed = k_transcribed_osrs241,
    .transcribed_count = (int)(sizeof(k_transcribed_osrs241) / sizeof(k_transcribed_osrs241[0])),
};

const struct ToriRSServerWire*
ToriRSServer_Wire241(void)
{
    return &k_wire_osrs241;
}
