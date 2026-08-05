#include "mock230_wire.h"

#include "mock239_inbound.h"

#include "net/rev/osrs230/packetin.h"
#include "net/rev/osrs230/packetout.h"
#include "net/rev/osrs239/packetin.h"
#include "net/rev/osrs239/packetout.h"
#include "net/rev/osrs239/zoneprot.h"

#include "mock230_zone.h"

#include <rsareabuf.h>

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Opcode resolution                                                   */
/* ------------------------------------------------------------------ */

/*
 * Both revisions resolve through the SAME tables the client reads
 * (`src/net/rev/<rev>/packetin.h`). That is the whole reason the numbers cannot
 * drift: there is one statement of "IF_OPENTOP is 96 at revision 239" and both
 * ends include it. The old file-local enum in mock230_encode.c was a second
 * copy, and a second copy of a number is a number that will disagree.
 */

static int
osrs230_opcode(int pkt_name)
{
    return packetin_wire_osrs230(pkt_name);
}

static int
osrs230_size(int wire_opcode)
{
    return packetin_size_osrs230(wire_opcode);
}

/*
 * Revision 230 addresses a zone sub-packet by its top-level opcode -- the
 * client resolves it in the same table -- so this is the same lookup.
 */
static int
osrs230_out_size(int wire_opcode)
{
    return osrs230_packetout_size(wire_opcode);
}

static int
osrs230_out_name(int wire_opcode)
{
    return osrs230_packetout_name(wire_opcode);
}

static int
osrs239_out_size(int wire_opcode)
{
    return osrs239_packetout_size(wire_opcode);
}

static int
osrs239_out_name(int wire_opcode)
{
    return osrs239_packetout_name(wire_opcode);
}

static char const*
osrs239_out_prot_name(int wire_opcode)
{
    return osrs239_packetout_protname(wire_opcode);
}

static int
osrs230_zone_sub(int pkt_name)
{
    return packetin_wire_osrs230(pkt_name);
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
    case PKT_NAME_LOC_DEL: return OSRS239_ZONE_LOC_DEL;
    case PKT_NAME_LOC_MERGE: return OSRS239_ZONE_LOC_MERGE;
    case PKT_NAME_OBJ_COUNT: return OSRS239_ZONE_OBJ_COUNT;
    case PKT_NAME_MAP_ANIM: return OSRS239_ZONE_MAP_ANIM;
    case PKT_NAME_LOC_ADD_CHANGE: return OSRS239_ZONE_LOC_ADD_CHANGE_V2;
    case PKT_NAME_OBJ_ADD: return OSRS239_ZONE_OBJ_ADD;
    case PKT_NAME_LOC_ANIM: return OSRS239_ZONE_LOC_ANIM;
    case PKT_NAME_OBJ_DEL: return OSRS239_ZONE_OBJ_DEL;
    case PKT_NAME_OBJ_REVEAL: return OSRS239_ZONE_OBJ_ENABLED_OPS;
    case PKT_NAME_MAP_PROJANIM: return OSRS239_ZONE_MAP_PROJANIM_V2;
    default: return -1;
    }
}

static int
osrs239_opcode(int pkt_name)
{
    return packetin_wire_osrs239(pkt_name);
}

static int
osrs239_size(int wire_opcode)
{
    return packetin_size_osrs239(wire_opcode);
}

static char const*
osrs239_prot_name(int wire_opcode)
{
    return packetin_protname_osrs239(wire_opcode);
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
w239_if_opentop(struct RSAreaBuf* buf, int interface_id)
{
    rsab_p2_alt2(buf, interface_id);
}

/* IfOpenSubEncoder: p2Alt3(interfaceId), pCombinedIdAlt3(dest), p1(type).
 * Note the ORDER against 230's p1(type) first -- same seven bytes, reversed. */
static void
w239_if_opensub(struct RSAreaBuf* buf, int interface_id, int dest_uid, int type)
{
    rsab_p2_alt3(buf, interface_id);
    rsab_p4_alt3(buf, dest_uid);
    rsab_p1(buf, type);
}

/* IfCloseSubEncoder: pCombinedId(dest) */
static void
w239_if_closesub(struct RSAreaBuf* buf, int dest_uid)
{
    rsab_p4(buf, dest_uid);
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
    struct RSAreaBuf* buf,
    int component_uid,
    int start,
    int end,
    uint32_t events)
{
    /* Bits 1..10 of the classic mask are buttons 1..10; events2 bit N is
     * button N+1. They are left set in events1 as well -- the client ignores
     * them there, and clearing them would make the two revisions' masks differ
     * by more than where the buttons live. */
    uint32_t const buttons = (events >> 1) & 0x3ff;

    rsab_p2_alt2(buf, end);
    rsab_p4(buf, (int32_t)buttons);
    rsab_p4_alt2(buf, (int32_t)events);
    rsab_p2(buf, start);
    rsab_p4_alt3(buf, component_uid);
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
w239_varp_small(struct RSAreaBuf* buf, int varp, int value)
{
    rsab_p1_alt1(buf, value);
    rsab_p2_alt3(buf, varp);
}

static void
w239_varp_large(struct RSAreaBuf* buf, int varp, int value)
{
    rsab_p2_alt2(buf, varp);
    rsab_p4_alt1(buf, value);
}

/* UpdateRunEnergyEncoder: p2(energy) -- hundredths of a percent */
static void
w239_update_runenergy(struct RSAreaBuf* buf, int hundredths)
{
    rsab_p2(buf, hundredths);
}

/* UpdateRunWeightEncoder: p2(weight) -- kilograms, signed */
static void
w239_update_runweight(struct RSAreaBuf* buf, int kilograms)
{
    rsab_p2(buf, kilograms);
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
w239_update_stat(struct RSAreaBuf* buf, int stat, int level, int xp, int boosted)
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
    rsab_p1(buf, boosted);
    rsab_p1(buf, boosted);
    rsab_p1_alt1(buf, stat);
    rsab_p4_alt2(buf, xp);
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
    struct RSAreaBuf* buf,
    int world_area,
    int zone_x,
    int zone_z,
    const int32_t* keys,
    int key_squares)
{
    (void)keys;
    (void)key_squares;
    rsab_p2_alt1(buf, world_area);
    rsab_p2_alt2(buf, zone_z);
    rsab_p2_alt2(buf, zone_x);
}

/*
 * CamMoveToV2Encoder: p2Alt1 x, p1Alt2 rate, p2Alt1 height, p1Alt2 rate2, p2 z.
 *
 * Eight bytes at this revision against the classic six, and the coordinates are
 * two bytes each rather than one — the classic packs a scene-local tile into a
 * byte, which cannot address a coordinate outside the 104-tile scene.
 */
static void
w239_cam_moveto(struct RSAreaBuf* buf, int x, int z, int height, int rate, int rate2)
{
    rsab_p2_alt1(buf, x);
    rsab_p1_alt2(buf, rate);
    rsab_p2_alt1(buf, height);
    rsab_p1_alt2(buf, rate2);
    rsab_p2(buf, z);
}

/* CamLookAtV2Encoder: p2 z, p2Alt2 height, p1 rate2, p1Alt3 rate, p2Alt1 x.
 * Same five fields as the move, in a different order and different orders. */
static void
w239_cam_lookat(struct RSAreaBuf* buf, int x, int z, int height, int rate, int rate2)
{
    rsab_p2(buf, z);
    rsab_p2_alt2(buf, height);
    rsab_p1(buf, rate2);
    rsab_p1_alt3(buf, rate);
    rsab_p2_alt1(buf, x);
}

/* CamShakeEncoder: p1 axis, p1 random, p1 amplitude, p1 rate. Unchanged shape,
 * but it still needs a writer: an untranscribed packet is refused. */
static void
w239_cam_shake(struct RSAreaBuf* buf, int axis, int random, int amplitude, int rate)
{
    rsab_p1(buf, axis);
    rsab_p1(buf, random);
    rsab_p1(buf, amplitude);
    rsab_p1(buf, rate);
}

/*
 * RebuildRegionV2Encoder: p2 zoneZ, p2 zoneX, p1Alt1 reload.
 *
 * Three differences from the normal rebuild and from revision 230, all of them
 * silent if missed: zoneZ comes FIRST, both are plain p2 rather than alt
 * orders, and there is a `reload` flag the older packet has no field for. The
 * zone descriptor grid follows this header and the XTEA key block that used to
 * trail it is gone, as it is for REBUILD_NORMAL.
 */
static void
w239_rebuild_region(struct RSAreaBuf* buf, int zone_x, int zone_z, int reload)
{
    rsab_p2(buf, zone_z);
    rsab_p2(buf, zone_x);
    rsab_p1_alt1(buf, reload ? 1 : 0);
}

/*
 * The zone sub-packets, transcribed from RSProt's zone/payload encoders.
 *
 * Every one of these is the same handful of fields in a different order and a
 * different byte order from revision 230's, which is the failure mode this
 * whole adapter exists for: they frame to the right length either way and
 * decode to a different loc on a different tile.
 */
/* pSmart1or2: one byte below 0x80, else a short with 0x8000 set. */
static void
w239_psmart1or2(struct RSAreaBuf* buf, int v)
{
    if( v >= 0 && v <= 0x7f )
        rsab_p1(buf, v);
    else
        rsab_p2(buf, 0x8000 | (v & 0x7fff));
}

/*
 * MessageGameEncoder: pSmart1or2 type, p1 hasName, [pjstr name], pjstr text.
 *
 * The type is a smart rather than the classic single byte, and the name is an
 * optional field the classic packet does not have at all -- so a 239 client
 * reading the old layout takes the first character of the message as its
 * "has name" flag.
 */
static void
w239_message_game(struct RSAreaBuf* buf, int type, const char* text)
{
    w239_psmart1or2(buf, type);
    rsab_p1(buf, 0); /* no sender name */
    rsab_pjstr(buf, text ? text : "", RSAB_JSTR_NUL);
}

/* IfSetTextEncoder: pjstr text, pCombinedIdAlt2 uid. The text comes FIRST at
 * this revision and second at 230. */
static void
w239_if_settext(struct RSAreaBuf* buf, int uid, const char* text)
{
    rsab_pjstr(buf, text ? text : "", RSAB_JSTR_NUL);
    rsab_p4_alt2(buf, uid);
}

/* IfSetNpcHeadEncoder: pCombinedId uid, p2Alt3 npc. */
static void
w239_if_setnpchead(struct RSAreaBuf* buf, int uid, int npc_id)
{
    rsab_p4(buf, uid);
    rsab_p2_alt3(buf, npc_id);
}

/* IfSetAnimEncoder: pCombinedIdAlt1 uid, p2Alt1 anim. */
static void
w239_if_setanim(struct RSAreaBuf* buf, int uid, int anim_id)
{
    rsab_p4_alt1(buf, uid);
    rsab_p2_alt1(buf, anim_id);
}

/* IfSetPlayerHeadEncoder: pCombinedIdAlt2 uid, and nothing else. */
static void
w239_if_setplayerhead(struct RSAreaBuf* buf, int uid)
{
    rsab_p4_alt2(buf, uid);
}

/*
 * SetMapFlagV2Encoder: p4 packed coord.
 *
 * Four bytes carrying an ABSOLUTE coord where the classic sends two
 * scene-local tile bytes with 255,255 meaning "clear". There is no
 * clear sentinel here; a cleared flag is coord 0.
 */
static void
w239_set_map_flag(struct RSAreaBuf* buf, int level, int x, int z)
{
    rsab_p4(buf, ((level & 0x3) << 28) | ((x & 0x3fff) << 14) | (z & 0x3fff));
}

/*
 * ChatFilterSettingsEncoder: p1Alt1 public, p1Alt3 trade.
 *
 * TWO bytes, not the classic three -- the private-chat filter moved to its own
 * packet (CHAT_FILTER_SETTINGS_PRIVATECHAT). Writing three desynchronises the
 * next packet by one byte.
 */
static void
w239_chat_filter(struct RSAreaBuf* buf, int public_, int private_, int trade)
{
    (void)private_;
    rsab_p1_alt1(buf, public_);
    rsab_p1_alt3(buf, trade);
}

/* SynthSoundEncoder: p2 id, p1 loops, p2 delay. */
static void
w239_synth_sound(struct RSAreaBuf* buf, int id, int loops, int delay)
{
    rsab_p2(buf, id);
    rsab_p1(buf, loops);
    rsab_p2(buf, delay);
}

/* FriendListLoadedEncoder writes nothing: the packet is zero-length at this
 * revision where the classic carries a status byte. */
static void
w239_friendlist_loaded(struct RSAreaBuf* buf, int status)
{
    (void)buf;
    (void)status;
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
w239_inv_header(struct RSAreaBuf* buf, int pkt_name, int uid, int container,
                int capacity)
{
    (void)uid;
    rsab_p4(buf, (uint32_t)OSRS239_INV_COMBINED_ID_NONE);
    rsab_p2(buf, container);
    if( pkt_name == PKT_NAME_UPDATE_INV_FULL )
        rsab_p2(buf, capacity);
}

static void
w239_inv_slot(struct RSAreaBuf* buf, int pkt_name, int slot, int obj_id, int count)
{
    if( pkt_name == PKT_NAME_UPDATE_INV_FULL )
    {
        if( obj_id < 0 )
        {
            rsab_p1_alt3(buf, 0);
            rsab_p2_alt1(buf, 0);
            return;
        }
        rsab_p1_alt3(buf, count > 0xff ? 0xff : count);
        if( count >= 255 )
            rsab_p4_alt3(buf, count);
        rsab_p2_alt1(buf, obj_id + 1);
        return;
    }
    w239_psmart1or2(buf, slot);
    rsab_p2(buf, obj_id < 0 ? 0 : obj_id + 1);
    rsab_p1(buf, count > 0xff ? 0xff : count);
    if( count >= 0xff )
        rsab_p4(buf, count);
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
    struct RSAreaBuf* buf,
    int script_id,
    const char* types,
    int const* intv,
    const char* const* strv,
    int argc)
{
    for( int i = 0; i < argc; i++ )
        rsab_p1(buf, types && types[i] ? (uint8_t)types[i] : (uint8_t)'i');
    rsab_p1(buf, 0); /* NUL, not the classic newline */
    for( int i = argc - 1; i >= 0; i-- )
    {
        if( types && types[i] == 's' )
            rsab_pjstr(buf, strv && strv[i] ? strv[i] : "", RSAB_JSTR_NUL);
        else
            rsab_p4(buf, intv ? intv[i] : 0);
    }
    rsab_p4(buf, script_id);
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
w239_ignore_entry(struct RSAreaBuf* buf, const char* name)
{
    rsab_p1(buf, 0x1);
    rsab_pjstr(buf, name ? name : "", RSAB_JSTR_NUL);
    rsab_pjstr(buf, "", RSAB_JSTR_NUL); /* previousName */
    rsab_pjstr(buf, "", RSAB_JSTR_NUL); /* note */
}

/*
 * p3Alt2: [v>>16, v, v>>8].
 *
 * rsareabuf has p3 (big-endian medium) but not this permutation, and it is the
 * only place a 24-bit alt order is needed, so it lives here rather than
 * widening the buffer library for one caller.
 */
static void
w239_p3_alt2(struct RSAreaBuf* buf, int v)
{
    rsab_p1(buf, (v >> 16) & 0xff);
    rsab_p1(buf, v & 0xff);
    rsab_p1(buf, (v >> 8) & 0xff);
}

/*
 * Zone headers. Three fields, three different orders, one per packet:
 *
 *   FULL_FOLLOWS      p1Alt3 zoneZ, p1Alt2 zoneX, p1     level
 *   PARTIAL_FOLLOWS   p1Alt1 zoneZ, p1     zoneX, p1Alt2 level
 *   PARTIAL_ENCLOSED  p1Alt2 level, p1Alt1 zoneZ, p1Alt1 zoneX
 */
static void
w239_zone_header(struct RSAreaBuf* buf, int pkt_name, int zone_x, int zone_z, int level)
{
    switch( pkt_name )
    {
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS:
        rsab_p1_alt3(buf, zone_z);
        rsab_p1_alt2(buf, zone_x);
        rsab_p1(buf, level);
        break;
    case PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS:
        rsab_p1_alt1(buf, zone_z);
        rsab_p1(buf, zone_x);
        rsab_p1_alt2(buf, level);
        break;
    default: /* PARTIAL_ENCLOSED */
        rsab_p1_alt2(buf, level);
        rsab_p1_alt1(buf, zone_z);
        rsab_p1_alt1(buf, zone_x);
        break;
    }
}

/** OpFlags.ofOps(true x5) -- every right-click option the loctype declares. */
#define OSRS239_LOC_OPS_ALL_SHOWN 0x1f

static int
w239_zone_payload(
    struct RSAreaBuf* buf,
    int pkt_name,
    const struct Mock230ZoneEvent* event,
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
        rsab_p1_alt1(buf, 0);
        rsab_p1_alt3(buf, OSRS239_LOC_OPS_ALL_SHOWN);
        rsab_p1_alt1(buf, props);
        rsab_p1_alt2(buf, pos);
        rsab_p2_alt3(buf, id);
        return 1;

    /* LocDelEncoder: p1 locProperties, p1Alt2 coordInZone. */
    case PKT_NAME_LOC_DEL:
        rsab_p1(buf, props);
        rsab_p1_alt2(buf, pos);
        return 1;

    /* LocAnimEncoder: p1 locProperties, p1Alt3 coordInZone, p2Alt3 id. */
    case PKT_NAME_LOC_ANIM:
        rsab_p1(buf, props);
        rsab_p1_alt3(buf, pos);
        rsab_p2_alt3(buf, id);
        return 1;

    /* MapAnimEncoder: p1 height, p2Alt1 id, p2Alt1 delay, p1 coordInZone.
     * `count` carries the delay and `old_count` the height, which is what the
     * caller already packs into them. */
    case PKT_NAME_MAP_ANIM:
        rsab_p1(buf, event->src_height);
        rsab_p2_alt1(buf, id);
        rsab_p2_alt1(buf, event->start_delay);
        rsab_p1(buf, pos);
        return 1;

    /*
     * LocMergeEncoder: p1 minX, p2Alt1 index, p1 locProperties, p1 minZ,
     * p2Alt1 id, p2 end, p2Alt3 start, p1Alt1 maxX, p1Alt3 coordInZone,
     * p1Alt3 maxZ.
     *
     * Fourteen bytes at both revisions and every field in a different place --
     * the classic order is coord, properties, id, start, end, pid, then the
     * four bounds. `index` is the player the merged loc belongs to.
     */
    case PKT_NAME_LOC_MERGE:
        rsab_p1(buf, event->west);
        rsab_p2_alt1(buf, event->player_pid);
        rsab_p1(buf, props);
        rsab_p1(buf, event->south);
        rsab_p2_alt1(buf, id);
        rsab_p2(buf, event->end_cycle);
        rsab_p2_alt3(buf, event->start_cycle);
        rsab_p1_alt1(buf, event->east);
        rsab_p1_alt3(buf, pos);
        rsab_p1_alt3(buf, event->north);
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
        rsab_p2(buf, event->start_delay);
        rsab_p1(buf, pos);
        rsab_p2_alt2(buf, event->arc);
        rsab_p2_alt2(buf, id);
        rsab_p2_alt1(buf, event->end_delay);
        rsab_p4_alt1(buf, end_packed);
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
        rsab_p2_alt3(buf, event->dst_height * 4);
        /* sourceIndex: 0 = not locked to a source entity. The content-facing
         * `projanim` op names a source COORD, not an entity, so there is no
         * index to give -- the shot already starts from the right tile. */
        w239_p3_alt2(buf, 0);
        rsab_p1_alt2(buf, event->peak);
        rsab_p2(buf, event->src_height * 4);
        /*
         * The target index, in the CLIENT's numbering.
         *
         * "If the target avatar is a player, set the value as -(index + 1)" --
         * and `index` is the index the client knows that player by, which at
         * this revision is `pool pid + 1` (mock230_wire_local_index: the
         * client's table is 1..2047 with 0 unused). The event carries the
         * classic form, `-pid - 1`, so a player target is one further from zero
         * here. Npc targets need no adjustment: NPC_INFO writes the raw slot,
         * so the two numberings already agree.
         *
         * Sent unadjusted, a shot aimed at the only player in the world names
         * client index 0 -- the slot that is never occupied -- so the
         * projectile has no target to lock onto and is not drawn. The packet is
         * well-formed and the server logs a send.
         */
        rsab_p3(buf, event->target < 0 ? event->target - 1 : event->target);
        return 1;
    }

    /* ObjEnabledOpsEncoder: p1Alt1 coordInZone, p2Alt3 id, p1 opFlags. */
    case PKT_NAME_OBJ_REVEAL:
        rsab_p1_alt1(buf, pos);
        rsab_p2_alt3(buf, id);
        rsab_p1(buf, 0xff);
        return 1;

    /* ObjDelEncoder: p2 id, p1 coordInZone, p4Alt2 quantity. */
    case PKT_NAME_OBJ_DEL:
        rsab_p2(buf, id);
        rsab_p1(buf, pos);
        rsab_p4_alt2(buf, count);
        return 1;

    /* ObjCountEncoder: p4Alt3 oldQuantity, p1Alt1 coordInZone,
     * p4Alt1 newQuantity, p2Alt3 id. Both quantities are four bytes at this
     * revision; the classic packet carries two. */
    case PKT_NAME_OBJ_COUNT:
        rsab_p4_alt3(buf, old_count);
        rsab_p1_alt1(buf, pos);
        rsab_p4_alt1(buf, count);
        rsab_p2_alt3(buf, id);
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
        rsab_p1_alt1(buf, pos);
        /* timeUntilPublic / ownershipType / neverBecomesPublic: this server has
         * no per-player loot ownership, so every obj is public from the moment
         * it lands. Stated rather than inherited -- the classic packet has no
         * field for any of the three. */
        rsab_p2(buf, 0);
        rsab_p1(buf, 0);
        rsab_p2_alt2(buf, id);
        rsab_p1(buf, 0);
        rsab_p1_alt2(buf, 0xff); /* every op shown */
        rsab_p2_alt3(buf, event->despawn_ticks & 0xffff);
        rsab_p4_alt1(buf, count);
        return 1;

    default:
        return 0;
    }
}

static const struct Mock230WirePayload k_payload_osrs239 = {
    .message_game = w239_message_game,
    .if_settext = w239_if_settext,
    .if_setnpchead = w239_if_setnpchead,
    .if_setanim = w239_if_setanim,
    .if_setplayerhead = w239_if_setplayerhead,
    .set_map_flag = w239_set_map_flag,
    .chat_filter = w239_chat_filter,
    .synth_sound = w239_synth_sound,
    .friendlist_loaded = w239_friendlist_loaded,
    .inv_header = w239_inv_header,
    .inv_slot = w239_inv_slot,
    .run_clientscript = w239_run_clientscript,
    .ignore_entry = w239_ignore_entry,
    .zone_header = w239_zone_header,
    .zone_payload = w239_zone_payload,
    .cam_moveto = w239_cam_moveto,
    .cam_lookat = w239_cam_lookat,
    .cam_shake = w239_cam_shake,
    .rebuild_region = w239_rebuild_region,
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
    /* Everything else is deliberately absent: see mock230_wire.h. PLAYER_INFO
     * and NPC_INFO have no entry here and are not missing -- their v5 streams
     * are a codec rather than a field list, so mock230_encode.c forks to a
     * whole writer instead of filling one of these slots. */
};

/* ------------------------------------------------------------------ */
/* The adapters                                                        */
/* ------------------------------------------------------------------ */

/*
 * The packets revision 239's writer set covers. Everything else is refused —
 * see `transcribed` in mock230_wire.h for why refusing beats falling through.
 *
 * What is missing from this list is the honest measure of how far the 239
 * server is. PLAYER_INFO and NPC_INFO are now here; what is not is the
 * long tail -- OBJ_ADD, P_COUNTDIALOG and UPDATE_PID among them, and those
 * three because revision 239 has no such packet at all.
 */
static const int k_transcribed_osrs239[] = {
    PKT_NAME_IF_OPENTOP,       PKT_NAME_IF_OPENSUB,      PKT_NAME_IF_CLOSESUB,
    PKT_NAME_IF_SETEVENTS,     PKT_NAME_VARP_SMALL,      PKT_NAME_VARP_LARGE,
    PKT_NAME_UPDATE_RUNENERGY, PKT_NAME_UPDATE_RUNWEIGHT, PKT_NAME_UPDATE_STAT,
    PKT_NAME_REBUILD_NORMAL,   PKT_NAME_REBUILD_REGION,
    PKT_NAME_CAM_MOVETO,       PKT_NAME_CAM_LOOKAT,      PKT_NAME_CAM_SHAKE,
    PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS, PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS,
    PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED,
    PKT_NAME_LOC_ADD_CHANGE,   PKT_NAME_LOC_DEL,         PKT_NAME_LOC_ANIM,
    PKT_NAME_MAP_ANIM,         PKT_NAME_OBJ_ADD,         PKT_NAME_OBJ_DEL,
    PKT_NAME_OBJ_COUNT,        PKT_NAME_LOC_MERGE,       PKT_NAME_MAP_PROJANIM,
    PKT_NAME_OBJ_REVEAL,       PKT_NAME_MESSAGE_GAME,    PKT_NAME_IF_SETTEXT,
    PKT_NAME_IF_SETNPCHEAD,    PKT_NAME_IF_SETANIM,      PKT_NAME_IF_SETPLAYERHEAD,
    PKT_NAME_UNSET_MAP_FLAG,   PKT_NAME_CHAT_FILTER_SETTINGS,
    PKT_NAME_SYNTH_SOUND,      PKT_NAME_FRIENDLIST_LOADED,
    PKT_NAME_UPDATE_INV_FULL,  PKT_NAME_UPDATE_INV_PARTIAL,
    PKT_NAME_RUNCLIENTSCRIPT,  PKT_NAME_UPDATE_IGNORELIST,

    /* PLAYER_INFO has no `payload` writer because it is not a field list — the
     * whole packet is built by mock239_playerinfo.c, which mock230_encode.c
     * forks to before writing a bit. It is listed here so the send is allowed;
     * NPC_INFO is deliberately still absent. */
    PKT_NAME_PLAYER_INFO,
    /* Also built whole by mock230_encode.c rather than by a field writer: the
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

    PKT_NAME_SERVER_TICK_END, PKT_NAME_VARP_RESET, PKT_NAME_VARP_SYNC,
    PKT_NAME_CAM_RESET,       PKT_NAME_RESET_ANIMS,
};

static const struct Mock230Wire k_wire_osrs230 = {
    .name = "osrs230",
    .revision = 230,
    .opcode = osrs230_opcode,
    .payload_size = osrs230_size,
    .prot_name = NULL,
    .zone_sub_code = osrs230_zone_sub,
    .packetout_size = osrs230_out_size,
    .packetout_name = osrs230_out_name,
    .opcode_smart2 = 0, /* no opcode in this table reaches 0x80 */
    .payload = NULL,    /* the lc254-shaped set mock230_encode.c writes */
};

static const struct Mock230Wire k_wire_osrs239 = {
    .name = "osrs239",
    .revision = 239,
    .opcode = osrs239_opcode,
    .payload_size = osrs239_size,
    .prot_name = osrs239_prot_name,
    .zone_sub_code = osrs239_zone_sub,
    .packetout_size = osrs239_out_size,
    .packetout_name = osrs239_out_name,
    .packetout_prot_name = osrs239_out_prot_name,
    .translate_in = mock239_inbound_translate,
    .opcode_smart2 = 1,
    .payload = &k_payload_osrs239,
    .transcribed = k_transcribed_osrs239,
    .transcribed_count = (int)(sizeof(k_transcribed_osrs239) /
                               sizeof(k_transcribed_osrs239[0])),
};

const struct Mock230Wire*
mock230_wire_by_name(char const* name)
{
    if( !name )
        return NULL;
    if( strcmp(name, "osrs230") == 0 )
        return &k_wire_osrs230;
    if( strcmp(name, "osrs239") == 0 )
        return &k_wire_osrs239;
    return NULL;
}

const struct Mock230Wire*
mock230_wire_default(void)
{
    return &k_wire_osrs230;
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/*
 * One report per (revision, packet) for an absent packet.
 *
 * Keyed on the canonical name and cleared never: a packet a revision lacks is
 * typically emitted every tick, so per-send reporting drowns the log and a
 * silent drop hides the gap entirely. Reporting once is the only version of
 * this anyone reads.
 */
static uint8_t g_absent_reported[PKT_NAME_COUNT];
static uint8_t g_untranscribed_reported[PKT_NAME_COUNT];

char const*
mock230_wire_pkt_name(int pkt_name)
{
    switch( pkt_name )
    {
    case PKT_NAME_IF_OPENTOP: return "IF_OPENTOP";
    case PKT_NAME_IF_OPENSUB: return "IF_OPENSUB";
    case PKT_NAME_IF_CLOSESUB: return "IF_CLOSESUB";
    case PKT_NAME_IF_MOVESUB: return "IF_MOVESUB";
    case PKT_NAME_IF_SETEVENTS: return "IF_SETEVENTS";
    case PKT_NAME_IF_SETTEXT: return "IF_SETTEXT";
    case PKT_NAME_IF_SETHIDE: return "IF_SETHIDE";
    case PKT_NAME_IF_SETANIM: return "IF_SETANIM";
    case PKT_NAME_IF_SETNPCHEAD: return "IF_SETNPCHEAD";
    case PKT_NAME_IF_SETPLAYERHEAD: return "IF_SETPLAYERHEAD";
    case PKT_NAME_IF_SETMODEL: return "IF_SETMODEL";
    case PKT_NAME_IF_SETOBJECT: return "IF_SETOBJECT";
    case PKT_NAME_IF_SETCOLOUR: return "IF_SETCOLOUR";
    case PKT_NAME_IF_SETPOSITION: return "IF_SETPOSITION";
    case PKT_NAME_IF_SETSCROLLPOS: return "IF_SETSCROLLPOS";
    case PKT_NAME_UPDATE_INV_FULL: return "UPDATE_INV_FULL";
    case PKT_NAME_UPDATE_INV_PARTIAL: return "UPDATE_INV_PARTIAL";
    case PKT_NAME_UPDATE_INV_STOP_TRANSMIT: return "UPDATE_INV_STOP_TRANSMIT";
    case PKT_NAME_VARP_SMALL: return "VARP_SMALL";
    case PKT_NAME_VARP_LARGE: return "VARP_LARGE";
    case PKT_NAME_VARP_RESET: return "VARP_RESET";
    case PKT_NAME_VARP_SYNC: return "VARP_SYNC";
    case PKT_NAME_UPDATE_STAT: return "UPDATE_STAT";
    case PKT_NAME_UPDATE_RUNENERGY: return "UPDATE_RUNENERGY";
    case PKT_NAME_UPDATE_RUNWEIGHT: return "UPDATE_RUNWEIGHT";
    case PKT_NAME_UPDATE_PID: return "UPDATE_PID";
    case PKT_NAME_REBUILD_NORMAL: return "REBUILD_NORMAL";
    case PKT_NAME_REBUILD_REGION: return "REBUILD_REGION";
    case PKT_NAME_PLAYER_INFO: return "PLAYER_INFO";
    case PKT_NAME_NPC_INFO: return "NPC_INFO";
    case PKT_NAME_MESSAGE_GAME: return "MESSAGE_GAME";
    case PKT_NAME_MESSAGE_PRIVATE: return "MESSAGE_PRIVATE";
    case PKT_NAME_RUNCLIENTSCRIPT: return "RUNCLIENTSCRIPT";
    case PKT_NAME_P_COUNTDIALOG: return "P_COUNTDIALOG";
    case PKT_NAME_SET_PLAYER_OP: return "SET_PLAYER_OP";
    case PKT_NAME_UNSET_MAP_FLAG: return "SET_MAP_FLAG";
    case PKT_NAME_CHAT_FILTER_SETTINGS: return "CHAT_FILTER_SETTINGS";
    case PKT_NAME_FRIENDLIST_LOADED: return "FRIENDLIST_LOADED";
    case PKT_NAME_UPDATE_FRIENDLIST: return "UPDATE_FRIENDLIST";
    case PKT_NAME_UPDATE_IGNORELIST: return "UPDATE_IGNORELIST";
    case PKT_NAME_CAM_RESET: return "CAM_RESET";
    case PKT_NAME_CAM_MOVETO: return "CAM_MOVETO";
    case PKT_NAME_CAM_LOOKAT: return "CAM_LOOKAT";
    case PKT_NAME_CAM_SHAKE: return "CAM_SHAKE";
    case PKT_NAME_SYNTH_SOUND: return "SYNTH_SOUND";
    case PKT_NAME_UPDATE_ZONE_FULL_FOLLOWS: return "UPDATE_ZONE_FULL_FOLLOWS";
    case PKT_NAME_UPDATE_ZONE_PARTIAL_FOLLOWS: return "UPDATE_ZONE_PARTIAL_FOLLOWS";
    case PKT_NAME_UPDATE_ZONE_PARTIAL_ENCLOSED: return "UPDATE_ZONE_PARTIAL_ENCLOSED";
    case PKT_NAME_LOC_ADD_CHANGE: return "LOC_ADD_CHANGE";
    case PKT_NAME_LOC_DEL: return "LOC_DEL";
    case PKT_NAME_LOC_ANIM: return "LOC_ANIM";
    case PKT_NAME_LOC_MERGE: return "LOC_MERGE";
    case PKT_NAME_OBJ_ADD: return "OBJ_ADD";
    case PKT_NAME_OBJ_DEL: return "OBJ_DEL";
    case PKT_NAME_OBJ_COUNT: return "OBJ_COUNT";
    case PKT_NAME_OBJ_REVEAL: return "OBJ_REVEAL";
    case PKT_NAME_MAP_ANIM: return "MAP_ANIM";
    case PKT_NAME_MAP_PROJANIM: return "MAP_PROJANIM";
    case PKT_NAME_SERVER_TICK_END: return "SERVER_TICK_END";
    case PKT_NAME_MIDI_SONG: return "MIDI_SONG";
    case PKT_NAME_MIDI_JINGLE: return "MIDI_JINGLE";
    case PKT_NAME_LOGOUT: return "LOGOUT";
    case PKT_NAME_RESET_ANIMS: return "RESET_ANIMS";
    case PKT_NAME_UPDATE_REBOOT_TIMER: return "UPDATE_REBOOT_TIMER";
    case PKT_NAME_HINT_ARROW: return "HINT_ARROW";
    /* A `?` here is not cosmetic: this switch is what the "not transcribed"
     * report prints, so a name missing from it turns a work item into an
     * anonymous one. */
    default: return "?";
    }
}

int
mock230_wire_opcode(const struct Mock230Wire* wire, int pkt_name)
{
    int op;
    if( !wire || !wire->opcode )
        return -1;
    op = wire->opcode(pkt_name);
    if( op >= 0 )
        return op;

    if( pkt_name > 0 && pkt_name < PKT_NAME_COUNT && !g_absent_reported[pkt_name] )
    {
        g_absent_reported[pkt_name] = 1;
        fprintf(stderr, "mock230: %s has no %s -- dropping it (reported once)\n",
                wire->name, mock230_wire_pkt_name(pkt_name));
    }
    return -1;
}

int
mock230_wire_can_write(const struct Mock230Wire* wire, int pkt_name)
{
    if( !wire || !wire->payload || !wire->transcribed )
        return 1;
    for( int i = 0; i < wire->transcribed_count; i++ )
        if( wire->transcribed[i] == pkt_name )
            return 1;

    if( pkt_name > 0 && pkt_name < PKT_NAME_COUNT &&
        !g_untranscribed_reported[pkt_name] )
    {
        g_untranscribed_reported[pkt_name] = 1;
        fprintf(stderr,
                "mock230: %s payload for %s is not transcribed -- refusing to send "
                "it with another revision's layout (reported once)\n",
                wire->name, mock230_wire_pkt_name(pkt_name));
    }
    return 0;
}
