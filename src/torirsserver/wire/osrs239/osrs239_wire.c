#include "torirsserver/torirs_server_wire.h"

#include "net/rev/pktnames.h"
#include "torirsserver/torirs_server_interface_state.h"
#include "torirsserver/torirs_server_zone.h"
#include "torirsserver/wire/core/wire_runclientscript.h"
#include "torirsserver/wire/osrs239/osrs239_inbound.h"
#include "torirsserver/wire/osrs239/osrs239_interface_setters.h"
#include "streams/player_info_write.h"
#include "torirsserver/wire/osrs239/osrs239_varp.h"
#include "torirsserver/wire/osrs239/osrs239_prots.h"

#include "rsprot_buf.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/*
 * Revision 239's wire: its opcode tables and its whole writer set. The
 * default revision (ToriRSServer_WireDefault).
 */

/* ------------------------------------------------------------------ */
/* Opcode resolution                                                   */
/* ------------------------------------------------------------------ */

static int
osrs239_out_size(int wire_opcode)
{
    return wire_prot_out_size(&g_osrs239_prots, wire_opcode);
}

static int
osrs239_out_name(int wire_opcode)
{
    return wire_prot_out_name(&g_osrs239_prots, wire_opcode);
}

static char const*
osrs239_out_prot_name(int wire_opcode)
{
    return wire_prot_out_protname(&g_osrs239_prots, wire_opcode);
}

/*
 * Revision 239 addresses it by the ORDINAL of RSProt's IndexedZoneProtEncoder,
 * which is declaration order and matches a table inside the client. RSProt's
 * own OldSchoolZoneProt ids are a THIRD numbering and are not it.
 */
static int
osrs239_zone_sub(int pkt_name)
{
    switch( pkt_name )
    {
    case PKT_NAME_LOC_DEL: return RSPROT_OSRS239_ZONE_LOC_DEL;
    case PKT_NAME_LOC_MERGE: return RSPROT_OSRS239_ZONE_LOC_MERGE;
    case PKT_NAME_OBJ_COUNT: return RSPROT_OSRS239_ZONE_OBJ_COUNT;
    case PKT_NAME_MAP_ANIM: return RSPROT_OSRS239_ZONE_MAP_ANIM;
    case PKT_NAME_SOUND_AREA: return RSPROT_OSRS239_ZONE_SOUND_AREA;
    case PKT_NAME_LOC_ADD_CHANGE: return RSPROT_OSRS239_ZONE_LOC_ADD_CHANGE_V2;
    case PKT_NAME_OBJ_ADD: return RSPROT_OSRS239_ZONE_OBJ_ADD;
    case PKT_NAME_LOC_ANIM: return RSPROT_OSRS239_ZONE_LOC_ANIM;
    case PKT_NAME_OBJ_DEL: return RSPROT_OSRS239_ZONE_OBJ_DEL;
    case PKT_NAME_OBJ_REVEAL: return RSPROT_OSRS239_ZONE_OBJ_ENABLED_OPS;
    case PKT_NAME_MAP_PROJANIM: return RSPROT_OSRS239_ZONE_MAP_PROJANIM_V2;
    default: return -1;
    }
}

static int
osrs239_opcode(int pkt_name)
{
    return wire_prot_in_code(&g_osrs239_prots, pkt_name);
}

static int
osrs239_size(int wire_opcode)
{
    return wire_prot_in_size(&g_osrs239_prots, wire_opcode);
}

static char const*
osrs239_prot_name(int wire_opcode)
{
    return wire_prot_in_protname(&g_osrs239_prots, wire_opcode);
}

/* ------------------------------------------------------------------ */
/* Revision 239 payload writers                                        */
/* ------------------------------------------------------------------ */

/*
 * Transcribed one-for-one from RSProt's osrs-239 encoders
 * (osrs-239-desktop/.../game/outgoing/codec/). Each writer names the encoder
 * it came from so a disagreement is a diff rather than an argument.
 *
 * The byte-order suffixes are load-bearing and are the reason these exist at
 * all. IF_OPENTOP carries the same 2-byte interface id at 230 and 239 and
 * writes it `p2Alt1` at one and `p2Alt2` at the other; a client reading the
 * wrong one opens a different interface with no error anywhere.
 *
 * `pCombinedId` is RSProt's name for a packed (interface << 16) | child uid,
 * written as a 4-byte value in one of the alt orders.
 */

/* IfOpenTopEncoder: p2Alt2(interfaceId) */
static void
w239_if_opentop(struct RsprotBuf* buf, int interface_id)
{
    rsprot_p2_12_add128(buf, interface_id);
}

/* IfOpenSubEncoder: p2Alt3(interfaceId), pCombinedIdAlt3(dest), p1(type).
 * Note the ORDER against 230's p1(type) first -- same seven bytes, reversed. */
static void
w239_if_opensub(struct RsprotBuf* buf, int interface_id, int dest_uid, int type)
{
    rsprot_p2_21_add128(buf, interface_id);
    rsprot_p4_2143(buf, dest_uid);
    rsprot_p1(buf, type);
}

/* IfCloseSubEncoder: pCombinedId(dest) */
static void
w239_if_closesub(struct RsprotBuf* buf, int dest_uid)
{
    rsprot_p4_1234(buf, dest_uid);
}

/*
 * The remaining interface layouts are transcribed from the authoritative
 * client's handlers, not inferred from neighbouring packets. In the golden
 * deob these are class243 opcodes 123/55/63/142/106/102/82 in client.java:
 * every method13xxx call below has been matched to class617's concrete byte
 * reader. Most fields use a different transform despite all addressing the
 * same packed component uid.
 */

/* opcode 123: dest g4Alt1, source g4. */
static void
w239_if_movesub(struct RsprotBuf* buf, int source_uid, int dest_uid)
{
    rsprot_p4_4321(buf, dest_uid);
    rsprot_p4_1234(buf, source_uid);
}

/* opcode 55: colour g2, uid g4Alt2. */
static void
w239_if_setcolour(struct RsprotBuf* buf, int component_uid, int colour)
{
    rsprot_p2_12(buf, colour);
    rsprot_p4_3412(buf, component_uid);
}

/* opcode 63: uid g4Alt3, hidden g1Alt1. */
static void
w239_if_sethide(struct RsprotBuf* buf, int component_uid, int hide)
{
    rsprot_p4_2143(buf, component_uid);
    rsprot_p1_add128(buf, hide ? 1 : 0);
}

/* opcode 142 (IF_SETMODEL_V2): model g4Alt2, uid g4Alt1. */
static void
w239_if_setmodel(struct RsprotBuf* buf, int component_uid, int model_id)
{
    rsprot_p4_3412(buf, model_id);
    rsprot_p4_4321(buf, component_uid);
}

/* opcode 106: uid g4Alt2, obj g2Alt2, object value g4. */
static void
w239_if_setobject(struct RsprotBuf* buf, int component_uid, int obj_id, int value)
{
    rsprot_p4_3412(buf, component_uid);
    rsprot_p2_12_add128(buf, obj_id);
    rsprot_p4_1234(buf, value);
}

/* opcode 102: y g2Alt3, uid g4Alt2, x g2Alt3. */
static void
w239_if_setposition(struct RsprotBuf* buf, int component_uid, int x, int y)
{
    osrs239_encode_if_setposition(buf, component_uid, x, y);
}

/* opcode 82: uid g4Alt3, scroll position g2. */
static void
w239_if_setscroll(struct RsprotBuf* buf, int component_uid, int position)
{
    rsprot_p4_2143(buf, component_uid);
    rsprot_p2_12(buf, position);
}

/*
 * IfSetEventsV2Encoder:
 *   p2Alt2(end), p4(events2), p4Alt2(events1), p2(start), pCombinedIdAlt3(uid)
 *
 * V2 carries TWO 32-bit masks where 230 carried one, and they are NOT a mask
 * and a spare -- the button ops MOVED OUT of the first into the second:
 *
 *   events1  "the bitpacked events. Note that ifbutton is no longer included
 *             in this, so bits 1..10 are ignored."
 *   events2  "the bitpacked ifbutton events. Each bit corresponds to the
 *             respective button, including the sign bit."
 *
 * So the classic mask's bits 1..10 -- which are exactly IF_BUTTON1..10, and are
 * what arms a component's ops -- are DISCARDED by a 239 client if they are left
 * where they are. Writing 0 for events2 and the whole classic mask for events1
 * is a well-formed packet that arms nothing: every component the content arms
 * still resolves its verb, still runs its own onop clientscript, and then has
 * nothing to send, because the bit the client checks is in the other word.
 *
 * Nothing downstream reports it. The server sees no IF_BUTTON at all, which is
 * indistinguishable from a player who did not click.
 */
static void
w239_if_setevents(
    struct RsprotBuf* buf,
    int component_uid,
    int start,
    int end,
    uint32_t events1,
    uint32_t events2)
{
    ToriRSServer_IfStateEncodeSeteventsV2(
        buf, component_uid, start, end, events1, events2);
}

/*
 * VarpSmallEncoder: p1Alt1(value), p2Alt3(id)
 * VarpLargeEncoder: p2Alt2(id), p4Alt1(value)
 *
 * These two are worth pausing on, because a first pass at this file wrote
 * `p2Alt1` for the small id and put the large one's value first -- both
 * plausible, both wrong, and neither detectable downstream: every varp write
 * would have landed on a different varp at full speed with no error. The
 * lesson is in the shape of this file rather than in these two lines: each
 * writer is transcribed from its own encoder and none is inferred from its
 * neighbour, because "the id comes first" is not a rule this protocol has.
 */
static void
w239_varp_small(struct RsprotBuf* buf, int varp, int value)
{
    osrs239_encode_varp_small(buf, varp, value);
}

static void
w239_varp_large(struct RsprotBuf* buf, int varp, int value)
{
    rsprot_p2_12_add128(buf, varp);
    rsprot_p4_4321(buf, value);
}

/* UpdateRunEnergyEncoder: p2(energy) -- hundredths of a percent */
static void
w239_update_runenergy(struct RsprotBuf* buf, int hundredths)
{
    rsprot_p2_12(buf, hundredths);
}

/* UpdateRunWeightEncoder: p2(weight) -- kilograms, signed */
static void
w239_update_runweight(struct RsprotBuf* buf, int kilograms)
{
    rsprot_p2_12(buf, kilograms);
}

/*
 * UpdateStatV2Encoder:
 *   p1(invisibleBoostedLevel), p1(currentLevel), p1Alt1(stat), p4Alt2(experience)
 *
 * Seven bytes at both revisions and not one field in the same place. The 230
 * writer next door is `p1 stat, p1 level, p4 xp, p1 boosted`; reading that as
 * this would report the stat id as a level and the level as a stat.
 */
static void
w239_update_stat(struct RsprotBuf* buf, int stat, int level, int xp, int boosted)
{
    /*
     * `currentLevel` is the BOOSTED level -- "boosted or drained", in RSProt's
     * own words -- and `invisibleBoostedLevel` is the same number with invisible
     * boosts folded in. The base level is not on this wire at all: the client
     * derives it from the experience.
     *
     * Written the other way round (base into currentLevel), every stat orb and
     * every skill-tab number reads the base level, so a boost or a drain is
     * simply invisible -- and the packet is the right length with plausible
     * values in it. This server models no invisible boost, so both fields carry
     * the same value rather than one carrying a base level that has no slot.
     */
    (void)level;
    rsprot_p1(buf, boosted);
    rsprot_p1(buf, boosted);
    rsprot_p1_add128(buf, stat);
    rsprot_p4_3412(buf, xp);
}

/*
 * RebuildNormalV2Encoder: p2Alt1(worldArea), p2Alt2(zoneZ), p2Alt2(zoneX).
 *
 * The XTEA key array is GONE at V2 -- OldSchool stores map archives in the
 * clear from revision 237 -- so `keys` and `key_squares` are ignored here on
 * purpose rather than by omission. Writing 230's trailing key block would add
 * hundreds of bytes the client does not read, and since the packet is
 * var-short-framed it would not even fail a length check; the client would just
 * treat the next packet's opcode as map data.
 */
static void
w239_rebuild_normal(
    struct RsprotBuf* buf,
    int world_area,
    int zone_x,
    int zone_z,
    const int32_t* keys,
    int key_squares)
{
    (void)keys;
    (void)key_squares;
    rsprot_p2_21(buf, world_area);
    rsprot_p2_12_add128(buf, zone_z);
    rsprot_p2_12_add128(buf, zone_x);
}

/*
 * CamMoveToV2Encoder: p2Alt1 x, p1Alt2 rate, p2Alt1 height, p1Alt2 rate2, p2 z.
 *
 * Eight bytes at this revision against the classic six, and the coordinates are
 * two bytes each rather than one — the classic packs a scene-local tile into a
 * byte, which cannot address a coordinate outside the 104-tile scene.
 */
static void
w239_cam_moveto(struct RsprotBuf* buf, int x, int z, int height, int rate, int rate2)
{
    rsprot_p2_21(buf, x);
    rsprot_p1_neg(buf, rate);
    rsprot_p2_21(buf, height);
    rsprot_p1_neg(buf, rate2);
    rsprot_p2_12(buf, z);
}

/* CamLookAtV2Encoder: p2 z, p2Alt2 height, p1 rate2, p1Alt3 rate, p2Alt1 x.
 * Same five fields as the move, in a different order and different orders. */
static void
w239_cam_lookat(struct RsprotBuf* buf, int x, int z, int height, int rate, int rate2)
{
    rsprot_p2_12(buf, z);
    rsprot_p2_12_add128(buf, height);
    rsprot_p1(buf, rate2);
    rsprot_p1_sub128(buf, rate);
    rsprot_p2_21(buf, x);
}

/* CamShakeEncoder: p1 axis, p1 random, p1 amplitude, p1 rate. Unchanged shape,
 * but it still needs a writer: an untranscribed packet is refused. */
static void
w239_cam_shake(struct RsprotBuf* buf, int axis, int random, int amplitude, int rate)
{
    rsprot_p1(buf, axis);
    rsprot_p1(buf, random);
    rsprot_p1(buf, amplitude);
    rsprot_p1(buf, rate);
}

/* CamUnlockEncoder at 239: one boolean byte (rsprot cam_unlock v2). */
static void
w239_cam_unlock(struct RsprotBuf* buf, int unlock)
{
    rsprot_p1(buf, unlock);
}

/*
 * RebuildRegionV2Encoder: p2 zoneZ, p2 zoneX, p1Alt1 reload.
 *
 * Three differences from the normal rebuild and from revision 230, all of them
 * silent if missed: zoneZ comes FIRST, both are plain p2 rather than alt
 * orders, and there is a `reload` flag the older packet has no field for. The
 * zone descriptor grid follows this header and the XTEA key block that used to
 * trail it is gone, as it is for REBUILD_NORMAL. `encodeRegionV2` back-patches
 * the distinct source-square count in front of the bit grid.
 */
static void
w239_rebuild_region(
    struct RsprotBuf* buf,
    int zone_x,
    int zone_z,
    int reload,
    int32_t const* zones,
    int distinct_squares)
{
    int const count = TORIRSSERVER_MAPINSTANCE_LEVELS * TORIRSSERVER_MAPINSTANCE_SCENE_ZONES *
                      TORIRSSERVER_MAPINSTANCE_SCENE_ZONES;

    rsprot_p2_12(buf, zone_z);
    rsprot_p2_12(buf, zone_x);
    rsprot_p1_add128(buf, reload ? 1 : 0);
    rsprot_p2_12(buf, distinct_squares);
    rsprot_buf_bit_begin(buf);
    for( int i = 0; i < count; i++ )
    {
        rsprot_pbits(buf, 1, zones[i] != -1);
        if( zones[i] != -1 )
            rsprot_pbits(buf, 26, zones[i]);
    }
    rsprot_buf_bit_end(buf);
}

/*
 * REBUILD_WORLDENTITY_V4 (op 109) and WORLDENTITY_INFO_V7 (op 122).
 *
 * These are the exact inverses of the committed client decoders
 * (src/net/rev/osrs239/osrs239_parse.c) rather than transcriptions of an
 * RSProt encoder: RSProt's generator excludes the whole info family, so the
 * decoder is the only statement of the layout that exists on both ends.
 *
 * The alt transforms are the part that fails silently when it is wrong: a
 * size byte written plain reads back as `(-b) & 0xFF`, i.e. a 1x1 deck
 * arrives as a 15x15 one and the rebuild grid the client then expects is 225
 * zones long instead of 1.
 */
static void
w239_rebuild_worldentity(struct RsprotBuf* buf, int base_x, int base_z, int source_squares)
{
    rsprot_p2_12(buf, base_x);
    rsprot_p2_12(buf, base_z);
    rsprot_p2_12(buf, source_squares);
}

static void
w239_wev_spawn_scalars(struct RsprotBuf* buf, int size_byte, int priority_group, int config_id)
{
    rsprot_p1_neg(buf, size_byte);      /* method13137: (-b) & 0xFF */
    rsprot_p1_add128(buf, priority_group); /* method13164: (b - 128) & 0xFF */
    rsprot_p2_21(buf, config_id);       /* method13178: signed little-endian */
}

static void
w239_wev_op_mask(struct RsprotBuf* buf, int op_mask)
{
    rsprot_p1_sub128(buf, op_mask); /* method13166: (128 - b) & 0xFF */
}

/*
 * The zone sub-packets, transcribed from RSProt's zone/payload encoders.
 *
 * Every one of these is the same handful of fields in a different order and a
 * different byte order from revision 230's, which is the failure mode this
 * whole adapter exists for: they frame to the right length either way and
 * decode to a different loc on a different tile.
 */
/*
 * MessageGameEncoder: pSmart1or2 type, p1 hasName, [pjstr name], pjstr text.
 *
 * The type is a smart rather than the classic single byte, and the name is an
 * optional field the classic packet does not have at all -- so a 239 client
 * reading the old layout takes the first character of the message as its
 * "has name" flag.
 */
static void
w239_message_game(struct RsprotBuf* buf, int type, const char* text)
{
    rsprot_psmart1or2(buf, type);
    rsprot_p1(buf, 0); /* no sender name */
    rsprot_pjstr(buf, text ? text : "");
}

/* IfSetTextEncoder: pjstr text, pCombinedIdAlt2 uid. The text comes FIRST at
 * this revision and second at 230. */
static void
w239_if_settext(struct RsprotBuf* buf, int uid, const char* text)
{
    rsprot_pjstr(buf, text ? text : "");
    rsprot_p4_3412(buf, uid);
}

/* IfSetNpcHeadEncoder: pCombinedId uid, p2Alt3 npc. */
static void
w239_if_setnpchead(struct RsprotBuf* buf, int uid, int npc_id)
{
    rsprot_p4_1234(buf, uid);
    rsprot_p2_21_add128(buf, npc_id);
}

/* IfSetAnimEncoder: pCombinedIdAlt1 uid, p2Alt1 anim. */
static void
w239_if_setanim(struct RsprotBuf* buf, int uid, int anim_id)
{
    rsprot_p4_4321(buf, uid);
    rsprot_p2_21(buf, anim_id);
}

/* IfSetPlayerHeadEncoder: pCombinedIdAlt2 uid, and nothing else. */
static void
w239_if_setplayerhead(struct RsprotBuf* buf, int uid)
{
    rsprot_p4_3412(buf, uid);
}

/*
 * SetMapFlagV2Encoder: p4 packed coord.
 *
 * Four bytes carrying an ABSOLUTE coord where the classic sends two
 * scene-local tile bytes with 255,255 meaning "clear". There is no
 * clear sentinel here; a cleared flag is coord 0.
 */
static void
w239_set_map_flag(struct RsprotBuf* buf, int level, int x, int z)
{
    rsprot_p4_1234(buf, ((level & 0x3) << 28) | ((x & 0x3fff) << 14) | (z & 0x3fff));
}

static void
w239_unset_map_flag(struct RsprotBuf* buf)
{
    rsprot_p4_1234(buf, -1);
}

/*
 * ChatFilterSettingsEncoder: p1Alt1 public, p1Alt3 trade.
 *
 * TWO bytes, not the classic three -- the private-chat filter moved to its own
 * packet (CHAT_FILTER_SETTINGS_PRIVATECHAT). Writing three desynchronises the
 * next packet by one byte.
 */
static void
w239_chat_filter(struct RsprotBuf* buf, int public_, int private_, int trade)
{
    (void)private_;
    rsprot_p1_add128(buf, public_);
    rsprot_p1_sub128(buf, trade);
}

/* SynthSoundEncoder: p2 id, p1 loops, p2 delay. */
static void
w239_synth_sound(struct RsprotBuf* buf, int id, int loops, int delay)
{
    rsprot_p2_12(buf, id);
    rsprot_p1(buf, loops);
    rsprot_p2_12(buf, delay);
}

/* MidiSongV2Encoder v14. The five p2 transforms are independently confirmed
 * by the golden client's field2969 handler (client.java) and RSProt 239. */
static void
w239_midi_song(
    struct RsprotBuf* buf,
    int id,
    int fade_out_delay,
    int fade_out_speed,
    int fade_in_delay,
    int fade_in_speed)
{
    rsprot_p2_21(buf, fade_in_delay);
    rsprot_p2_12(buf, fade_out_delay);
    rsprot_p2_21_add128(buf, id);
    rsprot_p2_12(buf, fade_in_speed);
    rsprot_p2_21_add128(buf, fade_out_speed);
}

/*
 * MidiSongStopEncoder v12. Both fields are p2Alt3, which is the writer side of
 * the two `rsprot_g2_21_add128` reads the client's own parse does
 * (osrs239_parse.c, PKT_NAME_MIDI_SONG_STOP) — so the pair is confirmed from
 * both ends rather than transcribed once.
 *
 * A separate packet and not `midi_song(-1)` on the wire: MIDI_SONG's id field
 * is two bytes with 65535 meaning "no track", and the 239 client maps that to
 * -1 and then *starts* nothing — it does not stop what is already playing.
 * Stopping is this packet.
 */
static void
w239_midi_song_stop(struct RsprotBuf* buf, int fade_out_delay, int fade_out_speed)
{
    rsprot_p2_21_add128(buf, fade_out_delay);
    rsprot_p2_21_add128(buf, fade_out_speed);
}

/*
 * MidiJingleEncoder v16 (revs 239). `length_in_millis` is a big-endian p3, id
 * is a p2Alt3 with 65535 meaning "no id" -- the same sentinel MIDI_SONG uses.
 *
 * Confirmed two independent ways rather than transcribed once:
 *   - the vendored generated codec, 3rd/rsprot/packets/midi_jingle.c
 *     (`packet_midi_jingle_osrs239_out`, table row `{239, 239, ...}`)
 *   - the rev-239 deob: `class243(118, 5)` and the g3/g2Alt3 read pair in
 *     `client.java`'s handler for opcode 118.
 *
 * The client ignores the length once decoded (rs_audio.c's RS_Audio_Jingle
 * comment: "trusting it would cut a jingle short on a slow load"), so a wrong
 * value here would not be visible by ear -- only the wire-layout tests catch
 * it, which is why they exist.
 */
static void
w239_midi_jingle(struct RsprotBuf* buf, int id, int length_ms)
{
    rsprot_p3_123(buf, length_ms);
    rsprot_p2_21_add128(buf, id);
}

/*
 * AmbientSoundStartEncoder / AmbientSoundStopEncoder.
 *
 * The client reads the first byte as a boolean ("fade rather than cut"), not as
 * a duration -- `boolean var = g1Add() == 1` in the 239 deob. The id that
 * follows is a soundscape config id.
 */
static void
w239_ambientsound_start(struct RsprotBuf* buf, int id, int fade)
{
    rsprot_p1_add128(buf, fade ? 1 : 0);
    rsprot_p2_12(buf, id);
}

static void
w239_ambientsound_stop(struct RsprotBuf* buf, int fade)
{
    rsprot_p1(buf, fade ? 1 : 0);
}

/* FriendListLoadedEncoder writes nothing: the packet is zero-length at this
 * revision where the classic carries a status byte. */
static void
w239_friendlist_loaded(struct RsprotBuf* buf, int status)
{
    (void)buf;
    (void)status;
}

/*
 * UpdateFriendListDecoder in the authoritative 239 deob:
 * p1 renamed, pjstr name, pjstr previousName, p2 world, p1 rank, p1 flags;
 * an online-only pjstr/g1/g4 block; then a trailing pjstr note.
 */
static void
w239_friend_entry(struct RsprotBuf* buf, const char* name, const char* previous,
                 int match_previous, int world, int rank)
{
    const char* safe = name ? name : "";
    rsprot_p1(buf, match_previous ? 1 : 0);
    rsprot_pjstr(buf, safe);
    /* "" when the friend was never renamed: Jagex's rows carry one only
     * after a rename (RSProx index 236 [3]: 7 of the 94 in the login dump,
     * e.g. "Tianzhen" after "00sanguine"); the name twice reads as one. */
    rsprot_pjstr(buf, previous ? previous : "");
    rsprot_p2_12(buf, (uint16_t)world);
    rsprot_p1(buf, rank & 0xff);         /* rank */
    rsprot_p1(buf, 0);                 /* flags */
    if( world > 0 )
    {
        rsprot_pjstr(buf, "");
        rsprot_p1(buf, 0);
        rsprot_p4_1234(buf, 0);
    }
    rsprot_pjstr(buf, ""); /* note */
}

/*
 * UpdateInvFull:    pCombinedId uid, p2 container, p2 capacity, then per slot
 *                   p1Alt3 count (capped at 255), [p4Alt3 count if >= 255],
 *                   p2Alt1 id+1.
 * UpdateInvPartial: pCombinedId uid, p2 container, then per changed slot
 *                   pSmart1or2 slot, p2 id+1, p1 count, [p4 count if >= 255].
 *
 * The `+1` on the id is the wire's null encoding -- 0 means empty -- and it is
 * the same at both revisions. What differs is everything around it: the full
 * form puts the COUNT first and the id second, the partial form the other way
 * round, and their overflow escapes use different byte orders.
 */
/*
 * UpdateInvFullEncoder / UpdateInvPartialEncoder: pCombinedId, p2 inventoryId,
 * and (full only) p2 capacity.
 *
 * THE COMBINED ID IS DISCARDED, and that is the packet's contract at this
 * revision rather than a simplification. RSProt states it twice, once as a
 * runtime check and once as a deprecation on the only constructor that can
 * produce a positive value:
 *
 *     require(combinedId < 0) {
 *         "Positive combined id will always lead to crashing as the client no
 *          longer supports it."
 *     }
 *     @Deprecated("Interface Id/Component Id are no longer supported by the
 *                  client, guaranteed crashing.")
 *
 * An IF1-era client painted an inventory by writing the objs INTO the named
 * component's item array. IF3 components have no such array, so the client
 * dereferences a null field and dies -- which is exactly what a real client did
 * here, every login, on the first UPDATE_INV_FULL:
 *
 *     Client error: 22,97,12,14,...
 *     NullPointerException: Cannot read the array length because "<local9>.fv"
 *     is null
 *
 * -- opcode 22 (UPDATE_INV_FULL), 14 bytes, whose body began `00 95 00 00`:
 * interface 149, component 0. It reads as a disconnect rather than a crash,
 * because the client's answer to a fatal packet is to drop the connection and
 * return to the login screen; nothing in the server log looks wrong.
 *
 * -1 is the value for a normal IF3 interface. (Values below -70000 mark a
 * MIRRORED inventory -- the trade/partner case, where two components share one
 * inv id. Nothing here mirrors, and if something does the caller will have to
 * say so, because this layer cannot tell.)
 *
 * The binding a positive id used to carry is not lost: at IF3 the interface
 * says which inv it paints, and the server's part is only to keep that inv's
 * contents current.
 */
#define OSRS239_INV_COMBINED_ID_NONE (-1)
static void
w239_inv_header(struct RsprotBuf* buf, int pkt_name, int uid, int container,
                int capacity)
{
    (void)uid;
    rsprot_p4_1234(buf, (uint32_t)OSRS239_INV_COMBINED_ID_NONE);
    rsprot_p2_12(buf, container);
    if( pkt_name == PKT_NAME_UPDATE_INV_FULL )
        rsprot_p2_12(buf, capacity);
}

static void
w239_inv_slot(struct RsprotBuf* buf, int pkt_name, int slot, int obj_id, int count)
{
    if( pkt_name == PKT_NAME_UPDATE_INV_FULL )
    {
        if( obj_id < 0 )
        {
            rsprot_p1_sub128(buf, 0);
            rsprot_p2_21(buf, 0);
            return;
        }
        rsprot_p1_sub128(buf, count > 0xff ? 0xff : count);
        if( count >= 255 )
            rsprot_p4_2143(buf, count);
        rsprot_p2_21(buf, obj_id + 1);
        return;
    }
    rsprot_psmart1or2(buf, slot);
    /* UpdateInvPartialEncoder stops after the zero object sentinel. An empty
     * slot has no count byte; emitting one shifts every following record and
     * makes the client decode plausible but unrelated item ids/counts. */
    if( obj_id < 0 )
    {
        rsprot_p2_12(buf, 0);
        return;
    }
    rsprot_p2_12(buf, obj_id + 1);
    rsprot_p1(buf, count > 0xff ? 0xff : count);
    if( count >= 0xff )
        rsprot_p4_1234(buf, count);
}

/* UPDATE_INV_STOPTRANSMIT, opcode 45: inventory id g2Alt2. The golden
 * client's field2979 handler removes class36.field209[var0], proving this is
 * the inventory id rather than the component uid rev 230 carries. */
static void
w239_inv_stop_transmit(struct RsprotBuf* buf, int component_uid, int inv_id)
{
    (void)component_uid;
    rsprot_p2_12_add128(buf, inv_id);
}

/*
 * RunClientScriptEncoder: pjstr types, then the arguments in REVERSE order,
 * then p4 the script id.
 *
 * Two differences from the classic packet, both silent. The type string is
 * NUL-terminated here and newline-terminated at 230 -- a client reading the
 * wrong terminator consumes the first argument as part of the string. And 's'
 * arguments are NUL-terminated strings rather than newline-terminated ones.
 *
 * The reverse ordering is the same at both revisions and is not a mistake: the
 * client pops arguments off a stack.
 */
static void
w239_run_clientscript(
    struct RsprotBuf* buf,
    int script_id,
    const char* types,
    int const* intv,
    const char* const* strv,
    int argc)
{
    struct WireClientScriptArg args[WIRE_RUNCLIENTSCRIPT_ARG_MAX] = { 0 };

    if( argc < 0 || argc > WIRE_RUNCLIENTSCRIPT_ARG_MAX )
    {
        /* Preserve the vtable's void contract while making invalid input
         * visible to the caller's ordinary rsprot_buf_ok()/drop path. */
        buf->err |= RSPROT_BUF_ERR_RANGE;
        return;
    }

    for( int i = 0; i < argc; i++ )
    {
        args[i].int_value = intv ? intv[i] : 0;
        args[i].string_value = strv ? strv[i] : NULL;
    }
    if( !wire_encode_runclientscript(buf, script_id, types, args, argc) )
        buf->err |= RSPROT_BUF_ERR_RANGE;
}

/*
 * UpdateIgnoreListEncoder, added-entry form: p1 kind, pjstr name,
 * pjstr previousName, pjstr note.
 *
 * The classic packet is a flat array of 8-byte base-37 names; this one is
 * per-entry and text. Kind 0x1 is "added", 0x4 is "removed"; only the added
 * form is written here because this server only ever sends a whole list.
 */
static void
w239_ignore_entry(struct RsprotBuf* buf, const char* name, const char* previous)
{
    rsprot_p1(buf, 0x1);
    rsprot_pjstr(buf, name ? name : "");
    rsprot_pjstr(buf, previous ? previous : ""); /* previousName */
    rsprot_pjstr(buf, ""); /* note */
}

/*
 * Zone headers. Three fields, three different orders, one per packet:
 *
 *   FULL_FOLLOWS      p1Alt3 zoneZ, p1Alt2 zoneX, p1     level
 *   PARTIAL_FOLLOWS   p1Alt1 zoneZ, p1     zoneX, p1Alt2 level
 *   PARTIAL_ENCLOSED  p1Alt2 level, p1Alt1 zoneZ, p1Alt1 zoneX
 */
static void
w239_zone_header(struct RsprotBuf* buf, int pkt_name, int zone_x, int zone_z, int level)
{
    switch( pkt_name )
    {
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
        rsprot_p1_sub128(buf, zone_z);
        rsprot_p1_neg(buf, zone_x);
        rsprot_p1(buf, level);
        break;
    case PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS:
        rsprot_p1_add128(buf, zone_z);
        rsprot_p1(buf, zone_x);
        rsprot_p1_neg(buf, level);
        break;
    default: /* PARTIAL_ENCLOSED */
        rsprot_p1_neg(buf, level);
        rsprot_p1_add128(buf, zone_z);
        rsprot_p1_add128(buf, zone_x);
        break;
    }
}

/** OpFlags.ofOps(true x5) -- every right-click option the loctype declares. */
#define OSRS239_LOC_OPS_ALL_SHOWN 0x1f

static int
w239_zone_payload(
    struct RsprotBuf* buf,
    int pkt_name,
    const struct ToriRSServerZoneEvent* event,
    int pos,
    int props)
{
    int const id = event->id;
    int const count = event->count;
    int const old_count = event->old_count;

    switch( pkt_name )
    {
    /*
     * LocAddChangeV2Encoder: p1Alt1 opCount, [ops], p1Alt3 opFlags,
     * p1Alt1 locProperties, p1Alt2 coordInZone, p2Alt3 id.
     *
     * TWO fields V1 did not have, and each one is silent when wrong.
     *
     * `opCount` leads with zero, which means "do not override the ops" -- the
     * client then uses the loctype's own from the cache. A non-zero count is
     * followed by that many `p1 op-1, pjstr text` pairs and REPLACES all five.
     *
     * `opFlags` is a five-bit mask of which right-click options are SHOWN
     * (OpFlags.ofOps: bit 0 is op1). Zero is a legal value meaning "hide every
     * option", so a loc placed with 0 appears, animates, blocks movement and
     * cannot be clicked -- which reads as the loc change half-working rather
     * than as a protocol error. LOC_DEL's own reference call passes 31 for the
     * same field; ALL_SHOWN is what a server that has nothing to hide sends.
     */
    case PKT_NAME_LOC_ADD_CHANGE:
    {
        /*
         * The override list, and it is what makes a door with only one cache
         * record able to say "Close".
         *
         * The key on the wire is the ZERO-based slot index. RSProt keys its map
         * by the 1-based op number and writes `p1 key - 1`
         * (LocAddChangeV2Encoder; the generated codec spells the same thing as
         * `RSPROT_XFORM(x, U1, elem->key, -1)`), and the 239 gamepack indexes
         * its five-slot array with the byte it read, unadjusted. Writing the
         * 1-based number here would rename the slot one along, silently.
         *
         * Only slots that carry a replacement are sent. The client keeps the
         * loctype's own label for every slot the list does not mention, so an
         * empty list is exactly "use the cache's menu" — which is why a
         * placement with nothing to override still encodes as count 0 rather
         * than as five empty strings.
         */
        int count = 0;

        for( int i = 0; i < 5; i++ )
        {
            if( event->ops.name[i][0] != '\0' )
                count++;
        }
        rsprot_p1_add128(buf, count);
        for( int i = 0; i < 5; i++ )
        {
            if( event->ops.name[i][0] == '\0' )
                continue;
            rsprot_p1(buf, i);
            rsprot_pjstr(buf, event->ops.name[i]);
        }
        rsprot_p1_sub128(buf, event->ops.flags & OSRS239_LOC_OPS_ALL_SHOWN);
        rsprot_p1_add128(buf, props);
        rsprot_p1_neg(buf, pos);
        rsprot_p2_21_add128(buf, id);
        return 1;
    }

    /* LocDelEncoder: p1 locProperties, p1Alt2 coordInZone. */
    case PKT_NAME_LOC_DEL:
        rsprot_p1(buf, props);
        rsprot_p1_neg(buf, pos);
        return 1;

    /* LocAnimEncoder: p1 locProperties, p1Alt3 coordInZone, p2Alt3 id. */
    case PKT_NAME_LOC_ANIM:
        rsprot_p1(buf, props);
        rsprot_p1_sub128(buf, pos);
        rsprot_p2_21_add128(buf, id);
        return 1;

    /* MapAnimEncoder: p1 height, p2Alt1 id, p2Alt1 delay, p1 coordInZone.
     * `count` carries the delay and `old_count` the height, which is what the
     * caller already packs into them. */
    case PKT_NAME_MAP_ANIM:
        rsprot_p1(buf, event->src_height);
        rsprot_p2_21(buf, id);
        rsprot_p2_21(buf, event->start_delay);
        rsprot_p1(buf, pos);
        return 1;

    /* SoundAreaEncoder v19 (3rd/rsprot/packets/sound_area.c, the client's own
     * reader): p1Alt3 coordInZone, p1Alt2 dropOffRange, p1 range, p1Alt2
     * delay, p1Alt1 loops, p2 id. `count` carries the range, `old_count` the
     * drop-off and `shape` the loops (torirs_server_zone.h). */
    case PKT_NAME_SOUND_AREA:
        rsprot_p1_sub128(buf, pos);
        rsprot_p1_neg(buf, event->old_count);
        rsprot_p1(buf, event->count);
        rsprot_p1_neg(buf, event->start_delay);
        rsprot_p1_add128(buf, event->shape);
        rsprot_p2_12(buf, id);
        return 1;

    /*
     * LocMergeEncoder: p1 minX, p2Alt1 index, p1 locProperties, p1 minZ,
     * p2Alt1 id, p2 end, p2Alt3 start, p1Alt1 maxX, p1Alt3 coordInZone,
     * p1Alt3 maxZ.
     *
     * Fourteen bytes at both revisions and every field in a different place --
     * the classic order is coord, properties, id, start, end, pid, then the
     * four bounds. `index` is the player the merged loc belongs to.
     *
     * In the CLIENT's numbering, the same way MAP_PROJANIM's target below is:
     * the event carries the pool pid and this revision's player table is
     * 1..2047 with 0 unused (ToriRSServer_WirePlayerIndex). Written raw, an
     * agility obstacle's merged loc names client index 0 -- the slot that is
     * never occupied -- so the loc hides on schedule and rides with nobody.
     * Nothing about it looks like an off-by-one: the packet is well formed, the
     * loc disappears exactly when content said, and the player simply climbs
     * through empty air.
     */
    case PKT_NAME_LOC_MERGE:
        rsprot_p1(buf, event->west);
        rsprot_p2_21(buf, ToriRSServer_WirePlayerIndex(event->player_pid));
        rsprot_p1(buf, props);
        rsprot_p1(buf, event->south);
        rsprot_p2_21(buf, id);
        rsprot_p2_12(buf, event->end_cycle);
        rsprot_p2_21_add128(buf, event->start_cycle);
        rsprot_p1_add128(buf, event->east);
        rsprot_p1_sub128(buf, pos);
        rsprot_p1_sub128(buf, event->north);
        return 1;

    /*
     * MapProjAnimV2Encoder: p2 startTime, p1 coordInZone, p2Alt2 progress,
     * p2Alt2 id, p2Alt1 endTime, p4Alt1 end.packed, p2Alt3 endHeight,
     * p3Alt2 sourceIndex, p1Alt2 angle, p2 startHeight, p3 targetIndex.
     *
     * Twenty-four bytes against the classic fifteen, and the shape changed
     * rather than just the order: the destination is an absolute PACKED COORD
     * (p4) where the classic sends a pair of tile offsets, and the source and
     * target indices are three bytes each where the classic uses two. Those
     * widths are the id space growing, and a two-byte write here truncates a
     * real index into a different entity.
     *
     * The two flight-shape fields are `angle` and `progress`, and they are the
     * content-facing `$angle` and `$offset` of `projanim_pl`/`projanim_npc` --
     * the same pair the classic packet carries as `peak` and `arc`, in the same
     * order. `progress` is documented in fine coords (128 per game square),
     * which is exactly the unit `World_ProjectileSpawn`'s `startpos` already
     * uses, so neither needs scaling.
     */
    case PKT_NAME_MAP_PROJANIM:
    {
        /*
         * `end` is an ABSOLUTE CoordGrid -- (level << 28) | (x << 14) | z --
         * not the source-relative offsets the classic packet carries. The two
         * are the same field conceptually and nothing distinguishes them on the
         * wire, so the offsets written here put the arc's landing point a few
         * tiles from (0,0) and the shot flies off the map.
         */
        int end_packed = ((event->dst_level & 3) << 28) | ((event->dst_x & 0x3fff) << 14) |
                         (event->dst_z & 0x3fff);
        rsprot_p2_12(buf, event->start_delay);
        rsprot_p1(buf, pos);
        rsprot_p2_12_add128(buf, event->arc);
        rsprot_p2_12_add128(buf, id);
        rsprot_p2_21(buf, event->end_delay);
        rsprot_p4_4321(buf, end_packed);
        /*
         * Heights, times four.
         *
         * "the startHeight and endHeight variables no longer come with an
         * implicit * 4 multiplier in the client" -- the classic client scaled
         * them, so content states them in the old units and this revision has
         * to scale them here instead. Unscaled, every projectile flies at a
         * quarter of its intended height, which for a typical arc means along
         * the ground: it renders, it just does not look like a projectile.
         */
        rsprot_p2_21_add128(buf, event->heights_raw ? event->dst_height : event->dst_height * 4);
        /* sourceIndex: 0 = not locked to a source entity (`projanim_*`, a shot
         * from a tile); an npc_projanim_* names the npc, in the same client
         * numbering and player form as the target below. */
        rsprot_p3_132(buf, event->source < 0 ? event->source - 1 : event->source);
        rsprot_p1_neg(buf, event->peak);
        rsprot_p2_12(buf, event->heights_raw ? event->src_height : event->src_height * 4);
        /*
         * The target index, in the CLIENT's numbering.
         *
         * "If the target avatar is a player, set the value as -(index + 1)" --
         * and `index` is the index the client knows that player by, which at
         * this revision is `pool pid + 1` (ToriRSServer_WirePlayerIndex: the
         * client's table is 1..2047 with 0 unused). The event carries the
         * classic form, `-pid - 1`, so a player target is one further from zero
         * here. Npc targets need no adjustment *at this layer*: they arrive
         * already stated in the recipient's own npc numbering, which is a
         * per-client translation and so belongs where the event is fanned out
         * to each client (`torirs_server_zone.c` projanim_target_for_client) rather
         * than in a writer that is handed one event and no recipient.
         *
         * Sent unadjusted, a shot aimed at the only player in the world names
         * client index 0 -- the slot that is never occupied -- so the
         * projectile has no target to lock onto and is not drawn. The packet is
         * well-formed and the server logs a send.
         */
        rsprot_p3_123(buf, event->target < 0 ? event->target - 1 : event->target);
        return 1;
    }

    /* ObjEnabledOpsEncoder: p1Alt1 coordInZone, p2Alt3 id, p1 opFlags. */
    case PKT_NAME_OBJ_REVEAL:
        rsprot_p1_add128(buf, pos);
        rsprot_p2_21_add128(buf, id);
        rsprot_p1(buf, 0xff);
        return 1;

    /* ObjDelEncoder: p2 id, p1 coordInZone, p4Alt2 quantity. */
    case PKT_NAME_OBJ_DEL:
        rsprot_p2_12(buf, id);
        rsprot_p1(buf, pos);
        rsprot_p4_3412(buf, count);
        return 1;

    /* ObjCountEncoder: p4Alt3 oldQuantity, p1Alt1 coordInZone,
     * p4Alt1 newQuantity, p2Alt3 id. Both quantities are four bytes at this
     * revision; the classic packet carries two. */
    case PKT_NAME_OBJ_COUNT:
        rsprot_p4_2143(buf, old_count);
        rsprot_p1_add128(buf, pos);
        rsprot_p4_4321(buf, count);
        rsprot_p2_21_add128(buf, id);
        return 1;

    /*
     * ObjAddEncoder: p1Alt1 coordInZone, p2 timeUntilPublic, p1 ownershipType,
     * p2Alt2 id, p1 neverBecomesPublic, p1Alt2 opFlags, p2Alt3 timeUntilDespawn,
     * p4Alt1 quantity.
     *
     * Fourteen bytes against the classic five: ownership and the two timers are
     * new fields with no older equivalent, so they are stated rather than
     * carried over. Public immediately, never despawning, all ops enabled.
     */
    case PKT_NAME_OBJ_ADD:
        rsprot_p1_add128(buf, pos);
        /* timeUntilPublic / ownershipType / neverBecomesPublic: this server has
         * no per-player loot ownership, so every obj is public from the moment
         * it lands. Stated rather than inherited -- the classic packet has no
         * field for any of the three. */
        rsprot_p2_12(buf, 0);
        rsprot_p1(buf, 0);
        rsprot_p2_12_add128(buf, id);
        rsprot_p1(buf, 0);
        rsprot_p1_neg(buf, 0xff); /* every op shown */
        rsprot_p2_21_add128(buf, event->despawn_ticks & 0xffff);
        rsprot_p4_4321(buf, count);
        return 1;

    default:
        return 0;
    }
}


static const struct ToriRSServerWirePayload k_payload_osrs239 = {
    .player_info = player_info_osrs239_write,
    .message_private = ToriRSServer_WireMessagePrivateOsrs,
    .if_movesub = w239_if_movesub,
    .if_setcolour = w239_if_setcolour,
    .if_sethide = w239_if_sethide,
    .if_setmodel = w239_if_setmodel,
    .if_setobject = w239_if_setobject,
    .if_setposition = w239_if_setposition,
    .if_setscroll = w239_if_setscroll,
    .if_setrotatespeed = osrs239_encode_if_setrotatespeed,
    .if_setangle = osrs239_encode_if_setangle,
    .if_setnpchead_active = osrs239_encode_if_setnpchead_active,
    .if_setplayermodel_basecolour = osrs239_encode_if_setplayermodel_basecolour,
    .if_setplayermodel_bodytype = osrs239_encode_if_setplayermodel_bodytype,
    .if_setplayermodel_obj = osrs239_encode_if_setplayermodel_obj,
    .if_setplayermodel_self = osrs239_encode_if_setplayermodel_self,
    .message_game = w239_message_game,
    .if_settext = w239_if_settext,
    .if_setnpchead = w239_if_setnpchead,
    .if_setanim = w239_if_setanim,
    .if_setplayerhead = w239_if_setplayerhead,
    .set_map_flag = w239_set_map_flag,
    .unset_map_flag = w239_unset_map_flag,
    .chat_filter = w239_chat_filter,
    .synth_sound = w239_synth_sound,
    .midi_song = w239_midi_song,
    .midi_song_stop = w239_midi_song_stop,
    .midi_jingle = w239_midi_jingle,
    .ambientsound_start = w239_ambientsound_start,
    .ambientsound_stop = w239_ambientsound_stop,
    .friendlist_loaded = w239_friendlist_loaded,
    .friend_entry = w239_friend_entry,
    .friendchat_full = ToriRSServer_WireFriendchatFullOsrs,
    .friendchat_user = ToriRSServer_WireFriendchatUserOsrs,
    .message_friendchannel = ToriRSServer_WireMessageFriendchannelOsrs,
    .inv_header = w239_inv_header,
    .inv_slot = w239_inv_slot,
    .inv_stop_transmit = w239_inv_stop_transmit,
    .run_clientscript = w239_run_clientscript,
    .ignore_entry = w239_ignore_entry,
    .zone_header = w239_zone_header,
    .zone_payload = w239_zone_payload,
    .cam_moveto = w239_cam_moveto,
    .cam_lookat = w239_cam_lookat,
    .cam_shake = w239_cam_shake,
    .cam_unlock = w239_cam_unlock,
    .rebuild_region = w239_rebuild_region,
    .rebuild_worldentity = w239_rebuild_worldentity,
    .wev_spawn_scalars = w239_wev_spawn_scalars,
    .wev_op_mask = w239_wev_op_mask,
    .if_opentop = w239_if_opentop,
    .if_opensub = w239_if_opensub,
    .if_closesub = w239_if_closesub,
    .if_setevents = w239_if_setevents,
    .varp_small = w239_varp_small,
    .varp_large = w239_varp_large,
    .update_runenergy = w239_update_runenergy,
    .update_runweight = w239_update_runweight,
    .update_stat = w239_update_stat,
    .rebuild_normal = w239_rebuild_normal,
    /* Everything else is deliberately absent: see torirs_server_wire.h. NPC_INFO
     * has no entry here and is not missing -- its v5 stream is a codec rather
     * than a field list, so torirs_server_encode.c writes it whole. */
};


/*
 * The packets revision 239's writer set covers. Everything else is refused —
 * see `transcribed` in torirs_server_wire.h for why refusing beats falling through.
 *
 * What is missing from this list is the honest measure of how far the 239
 * server is. PLAYER_INFO and NPC_INFO are now here; what is not is the
 * long tail -- OBJ_ADD, P_COUNTDIALOG and UPDATE_PID among them, and those
 * three because revision 239 has no such packet at all.
 */
static const int k_transcribed_osrs239[] = {
    /* Friends chat: the shared writers in torirs_server_wire.c (RSProt's one
     * 222..242 row each). */
    PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_FULL, PKT_NAME_UPDATE_FRIENDCHAT_CHANNEL_SINGLEUSER,
    PKT_NAME_MESSAGE_FRIENDCHANNEL,
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
    PKT_NAME_CAM_MOVETO,       PKT_NAME_CAM_LOOKAT,      PKT_NAME_CAM_SHAKE,
    PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS, PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS,
    PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED,
    PKT_NAME_LOC_ADD_CHANGE,   PKT_NAME_LOC_DEL,         PKT_NAME_LOC_ANIM,
    PKT_NAME_MAP_ANIM,         PKT_NAME_OBJ_ADD,         PKT_NAME_OBJ_DEL,
    PKT_NAME_OBJ_COUNT,        PKT_NAME_LOC_MERGE,       PKT_NAME_MAP_PROJANIM,
    PKT_NAME_SOUND_AREA,
    /* MapAnimSpecificEncoder, written whole by ToriRSServer_SendMapAnimSpecific
     * the way HINT_ARROW is (no `payload` hook): eight bytes. */
    PKT_NAME_MAP_ANIM_SPECIFIC,
    PKT_NAME_OBJ_REVEAL,       PKT_NAME_MESSAGE_GAME,    PKT_NAME_IF_SETTEXT,
    PKT_NAME_IF_SETNPCHEAD,    PKT_NAME_IF_SETANIM,      PKT_NAME_IF_SETPLAYERHEAD,
    PKT_NAME_UNSET_MAP_FLAG,   PKT_NAME_CHAT_FILTER_SETTINGS,
    PKT_NAME_MINIMAP_TOGGLE, /* MinimapToggleEncoder: U1, verified for rev239. */
    PKT_NAME_CAM_UNLOCK, /* CamUnlockEncoder: one boolean, the payload hook. */
    /* HideNpcOps/HideLocOps/HideObjOpsEncoder: one boolean byte each, 223-239
     * (3rd/rsprot/packets/hidenpcops.c); opcodes 75 / 21 / 73 at 239. */
    PKT_NAME_HIDENPCOPS,       PKT_NAME_HIDELOCOPS,      PKT_NAME_HIDEOBJOPS,
    /* HINT_ARROW's six bytes are the same at every OldSchool revision that has
     * it, so `ToriRSServer_SendHintArrow` writes them directly rather than
     * through a `payload` hook -- listed here so the send is allowed, the same
     * way PLAYER_INFO is below. Revision 230 deliberately does NOT list it: its
     * prot table has no HINT_ARROW at all, and the drop is the truthful
     * behaviour there. */
    PKT_NAME_HINT_ARROW,
    PKT_NAME_SYNTH_SOUND,      PKT_NAME_MIDI_SONG,          PKT_NAME_MIDI_SONG_STOP,
    PKT_NAME_MIDI_JINGLE,
    PKT_NAME_FRIENDLIST_LOADED,
    PKT_NAME_AMBIENTSOUND_START, PKT_NAME_AMBIENTSOUND_STOP,
    PKT_NAME_UPDATE_INV_FULL,  PKT_NAME_UPDATE_INV_PARTIAL,
    PKT_NAME_UPDATE_INV_STOP_TRANSMIT,
    PKT_NAME_RUNCLIENTSCRIPT,  PKT_NAME_UPDATE_FRIENDLIST,
    PKT_NAME_UPDATE_IGNORELIST,

    /* PLAYER_INFO has no `payload` writer because it is not a field list — the
     * whole packet is built by player_info_write.c, which torirs_server_encode.c
     * forks to before writing a bit. It is listed here so the send is allowed;
     * NPC_INFO is deliberately still absent. */
    PKT_NAME_PLAYER_INFO,
    /* Also built whole by torirs_server_encode.c rather than by a field writer: the
     * v5 stream is one bit section, not a list of fields. */
    PKT_NAME_NPC_INFO,

    /*
     * Payload-free packets. A packet with no body has no layout to get wrong,
     * so listing them costs nothing and is not a claim about anything -- their
     * only per-revision fact is the opcode, which the table already answers.
     *
     * FRIENDLIST_LOADED is deliberately NOT here even though it looks like one:
     * it is 0 bytes at 239 and 1 at 230, and this server writes the 1-byte
     * form. The length check catches that ("wrote 1 bytes, client frames it as
     * 0"), which is the guard working rather than a packet to wave through.
     */
    PKT_NAME_SET_NPC_UPDATE_ORIGIN,
    PKT_NAME_SET_ACTIVE_WORLD,
    PKT_NAME_PACKET_GROUP_START,

    /* Sailing (docs/SAILING_PLAN.md S2). Deliberately absent from revision
     * 230's list: that prot table has no world-entity packet at all, so the
     * opcode lookup drops them there — the refusal the wire convention wants,
     * rather than a plausible frame at some other revision's opcode. */
    PKT_NAME_REBUILD_WORLDENTITY,
    PKT_NAME_WORLDENTITY_INFO,

    PKT_NAME_SERVER_TICK_END, PKT_NAME_VARP_RESET, PKT_NAME_VARP_SYNC,
    PKT_NAME_CAM_RESET,       PKT_NAME_RESET_ANIMS,
    /* No body (size 0 in every spec): the server ending a session, which the
     * client answers by closing and returning to the title, never reconnecting
     * (torirs_server_control.c CTL_LOGOUT). */
    PKT_NAME_LOGOUT,
    PKT_NAME_TRIGGER_ONDIALOGABORT,
};

static const struct ToriRSServerWire k_wire_osrs239 = {
    .name = "osrs239",
    .revision = 239,
    .opcode = osrs239_opcode,
    .payload_size = osrs239_size,
    .prot_name = osrs239_prot_name,
    .zone_sub_code = osrs239_zone_sub,
    .packetout_size = osrs239_out_size,
    .packetout_name = osrs239_out_name,
    .packetout_prot_name = osrs239_out_prot_name,
    .translate_in = osrs239_inbound_translate,
    .opcode_smart2 = 1,
    .payload = &k_payload_osrs239,
    .transcribed = k_transcribed_osrs239,
    .transcribed_count = (int)(sizeof(k_transcribed_osrs239) /
                               sizeof(k_transcribed_osrs239[0])),
};

const struct ToriRSServerWire*
ToriRSServer_Wire239(void)
{
    return &k_wire_osrs239;
}
