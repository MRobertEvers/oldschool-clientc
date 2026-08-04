#include "mock230_wire.h"

#include "net/rev/osrs230/packetin.h"
#include "net/rev/osrs239/packetin.h"
#include "net/rev/osrs239/zoneprot.h"

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
 * V2 carries TWO 32-bit masks where 230 carried one, which is where the extra
 * four bytes (12 -> 16) went. This server has never had a second mask to say,
 * so it writes 0 for events2 -- an explicit "no high-range events" rather than
 * a field left off, which would shorten the packet and desync the frame.
 */
static void
w239_if_setevents(
    struct RSAreaBuf* buf,
    int component_uid,
    int start,
    int end,
    uint32_t events)
{
    rsab_p2_alt2(buf, end);
    rsab_p4(buf, 0);
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
    rsab_p1(buf, boosted);
    rsab_p1(buf, level);
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

static const struct Mock230WirePayload k_payload_osrs239 = {
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
    /* Everything else is deliberately absent: see mock230_wire.h. The two that
     * matter most and are the largest work item are PLAYER_INFO and NPC_INFO,
     * whose v5 high/low-resolution streams are a different codec rather than a
     * different field order. */
};

/* ------------------------------------------------------------------ */
/* The adapters                                                        */
/* ------------------------------------------------------------------ */

/*
 * The packets revision 239's writer set covers. Everything else is refused —
 * see `transcribed` in mock230_wire.h for why refusing beats falling through.
 *
 * What is missing from this list is the honest measure of how far the 239
 * server is: PLAYER_INFO and NPC_INFO above all, whose v5 high/low-resolution
 * streams are a different codec rather than a different field order, and
 * without which no OldSchool client reaches the world.
 */
static const int k_transcribed_osrs239[] = {
    PKT_NAME_IF_OPENTOP,       PKT_NAME_IF_OPENSUB,      PKT_NAME_IF_CLOSESUB,
    PKT_NAME_IF_SETEVENTS,     PKT_NAME_VARP_SMALL,      PKT_NAME_VARP_LARGE,
    PKT_NAME_UPDATE_RUNENERGY, PKT_NAME_UPDATE_RUNWEIGHT, PKT_NAME_UPDATE_STAT,
    PKT_NAME_REBUILD_NORMAL,

    /* PLAYER_INFO has no `payload` writer because it is not a field list — the
     * whole packet is built by mock239_playerinfo.c, which mock230_encode.c
     * forks to before writing a bit. It is listed here so the send is allowed;
     * NPC_INFO is deliberately still absent. */
    PKT_NAME_PLAYER_INFO,
    /* Empty form only — mock239_npcinfo_write_empty. */
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
