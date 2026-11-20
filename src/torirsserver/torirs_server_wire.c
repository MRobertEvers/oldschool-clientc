#include "torirs_server_wire.h"
#include <assert.h>

#include "torirs_server_interface_state.h"

#include "rsprot_buf.h"

#include <stdio.h>
#include <string.h>

/*
 * The revision-independent half of the wire adapter: resolving one by name,
 * the once-only reports, and the readers tests use to take a captured payload
 * apart in whichever layout a wire writes. Each revision's adapter -- its
 * opcode tables and its writer set -- is its own file:
 *
 *   wire/osrs230/osrs230_wire.c   the hybrid; payloads are torirs_server_encode.c's
 *   wire/osrs239/osrs239_wire.c
 *   wire/osrs241/osrs241_wire.c
 *   wire/osrs242/osrs242_wire.c
 *
 * The readers below tell 239's layouts from 230's by asking whether `wire`
 * carries 239's writer set.
 */

/*
 * READING an UPDATE_INV_FULL / UPDATE_INV_PARTIAL header.
 *
 * 230 writes `p4 component, p2 container[, p2 capacity]`. 239 writes the same
 * three fields but deliberately puts `OSRS239_INV_COMBINED_ID_NONE` where the
 * component goes: at that revision the INVENTORY ID is the address and the
 * component is not carried at all (see `w239_inv_stop_transmit`, where the
 * golden client's handler indexes by inventory).
 *
 * `*out_component` is therefore set to -1 when the revision does not carry one,
 * and callers must not read it as "component 0". An assertion that counts one
 * flush per listening COMPONENT cannot be made at 239 by construction; the
 * honest form there is per container.
 */
int
ToriRSServer_WireReadInvHeader(
    const struct ToriRSServerWire* wire,
    int pkt_name,
    const uint8_t* data,
    int len,
    int* out_component,
    int* out_container,
    int* out_capacity)
{
    struct RsprotBuf buf;

    assert(wire);
    assert(data);
    assert(out_component);
    assert(out_container);
    assert(out_capacity);

    *out_capacity = -1;
    rsprot_buf_wrap_read(&buf, (uint8_t*)data, (size_t)(len < 0 ? 0 : len));
    if( wire->payload == ToriRSServer_Wire239()->payload )
    {
        (void)rsprot_g4_1234(&buf); /* the NONE sentinel, not a component */
        *out_component = -1;
        *out_container = rsprot_g2_12(&buf);
    }
    else
    {
        *out_component = rsprot_g4_1234(&buf);
        *out_container = rsprot_g2_12(&buf);
    }
    if( pkt_name == PKT_NAME_UPDATE_INV_FULL )
        *out_capacity = rsprot_g2_12(&buf);
    return rsprot_buf_ok(&buf);
}

/*
 * READING a VARP_SMALL / VARP_LARGE back.
 *
 * The two forms do not even agree with each other at 239: VARP_SMALL writes
 * `p1Alt1 value` and THEN `p2Alt3 varp`, while VARP_LARGE writes `p2Alt2 varp`
 * first and `p4Alt1 value` after. 230 puts a plain `p2 varp` first in both. A
 * stanza that reads two bytes off the front and calls it a varp id therefore
 * reads a value-plus-half-an-id at 239 and matches nothing — "the open should
 * transmit slayer_misc (4844)" about an open that transmitted it.
 */
int
ToriRSServer_WireReadVarp(
    const struct ToriRSServerWire* wire,
    int pkt_name,
    const uint8_t* data,
    int len,
    int* out_varp,
    int* out_value)
{
    struct RsprotBuf buf;
    int large = pkt_name == PKT_NAME_VARP_LARGE;

    assert(wire);
    assert(data);
    assert(out_varp);
    assert(out_value);

    rsprot_buf_wrap_read(&buf, (uint8_t*)data, (size_t)(len < 0 ? 0 : len));
    if( wire->payload == ToriRSServer_Wire239()->payload )
    {
        if( large )
        {
            *out_varp = rsprot_g2_12_add128(&buf);
            *out_value = rsprot_g4_4321(&buf);
        }
        else
        {
            *out_value = rsprot_g1_add128(&buf);
            *out_varp = rsprot_g2_21_add128(&buf);
        }
    }
    else
    {
        *out_varp = rsprot_g2_12(&buf);
        *out_value = large ? rsprot_g4_1234(&buf) : rsprot_g1(&buf);
    }
    return rsprot_buf_ok(&buf);
}

/*
 * READING an IF_SETTEXT back.
 *
 * The two fields swap: 230 writes `p4 uid` then a NEWLINE-terminated string,
 * 239 writes a NUL-terminated string then `pCombinedIdAlt2 uid`. A stanza that
 * reads the uid from the front therefore reads the first four characters of the
 * text at 239, and its string comparison starts four bytes into the message.
 */
int
ToriRSServer_WireReadIfSettext(
    const struct ToriRSServerWire* wire,
    const uint8_t* data,
    int len,
    int* out_uid,
    char* out_text,
    int text_cap)
{
    struct RsprotBuf buf;
    int n = 0;
    int terminator;
    int trailing_uid;

    assert(wire);
    assert(data);
    assert(out_uid);
    assert(out_text);
    assert(text_cap > 0);

    out_text[0] = '\0';
    *out_uid = -1;
    trailing_uid = (wire->payload == ToriRSServer_Wire239()->payload);
    terminator = trailing_uid ? 0 : '\n';
    rsprot_buf_wrap_read(&buf, (uint8_t*)data, (size_t)(len < 0 ? 0 : len));
    if( !trailing_uid )
        *out_uid = rsprot_g4_1234(&buf);
    for( ;; )
    {
        int c = rsprot_g1(&buf);

        if( !rsprot_buf_ok(&buf) )
            return 0;
        if( c == terminator )
            break;
        if( n >= text_cap - 1 )
            return 0;
        out_text[n++] = (char)c;
    }
    out_text[n] = '\0';
    if( trailing_uid )
        *out_uid = rsprot_g4_3412(&buf);
    return rsprot_buf_ok(&buf);
}

/*
 * READING a MESSAGE_GAME back — the text, which is what every stanza wants.
 *
 * 230 writes `p1 type` then the string; 239 writes `psmart1or2 type`, then a
 * p1 sender-name flag, then the string. Sixty-eight stanzas skipped a single
 * byte and read from there, so at 239 they read from the sender flag — a zero
 * — and every `mes` came back as the empty string. That is silent: the capture
 * loop simply matches nothing, and a content walkthrough that reports through
 * `mes` reads as "::whatever-run never reached its OK line" while the script
 * ran perfectly.
 *
 * Copies into the caller's buffer rather than pointing into the packet: the two
 * revisions start the string at different offsets and a caller holding a
 * pointer would have to know which.
 */
int
ToriRSServer_WireReadMessageGame(
    const struct ToriRSServerWire* wire,
    const uint8_t* data,
    int len,
    char* out_text,
    int text_cap)
{
    struct RsprotBuf buf;
    int n = 0;

    assert(wire);
    assert(data);
    assert(out_text);
    assert(text_cap > 0);

    out_text[0] = '\0';
    rsprot_buf_wrap_read(&buf, (uint8_t*)data, (size_t)(len < 0 ? 0 : len));
    /* 239 and 241 both write the smart type plus the name_present flag
     * (w241_message_game: "as at 239"); only 230 writes a bare g1 type. */
    if( wire->revision >= 239 )
    {
        (void)rsprot_gsmart1or2(&buf); /* message type */
        (void)rsprot_g1(&buf);     /* sender-name present flag */
    }
    else
    {
        (void)rsprot_g1(&buf); /* message type */
    }
    for( ;; )
    {
        int c = rsprot_g1(&buf);

        if( !rsprot_buf_ok(&buf) )
            return 0;
        if( c == 0 )
            break;
        if( n >= text_cap - 1 )
            return 0;
        out_text[n++] = (char)c;
    }
    out_text[n] = '\0';
    return 1;
}

/*
 * READING an IF_SETEVENTS back.
 *
 * 230 writes `p4Alt3 uid, p2Alt2 start, p4Alt1 events, p2 end`; 239's
 * IfSetEventsV2 writes `p2Alt2 end, p4 events2, p4Alt2 events1, p2 start,
 * p4Alt3 uid` — a different order, different transforms, and a second event
 * word 230 does not have. A stanza that scans for its component by decoding
 * the first field found nothing at all at 239, which is how "the login burst
 * arms every skill-guide cell" came to report an unarmed cell for all 24.
 *
 * `out_events` is the CLASSIC mask, recombined. 239 does not carry one:
 * `ToriRSServer_IfStateSplitClassicEvents` moves classic bits 1..10 into a
 * second word shifted down by one and leaves the rest in the first, so op 2 —
 * classic bit 2, value 4 — arrives as events1 = 0 and events2 = 2. Returning
 * events1 raw therefore reports "unarmed" for every op an interface actually
 * has. This undoes the split so the assertions keep speaking classic.
 */
int
ToriRSServer_WireReadIfSetevents(
    const struct ToriRSServerWire* wire,
    const uint8_t* data,
    int len,
    int* out_uid,
    int* out_start,
    int* out_end,
    uint32_t* out_events)
{
    struct RsprotBuf buf;

    assert(wire);
    assert(data);
    assert(out_uid);
    assert(out_start);
    assert(out_end);
    assert(out_events);

    rsprot_buf_wrap_read(&buf, (uint8_t*)data, (size_t)(len < 0 ? 0 : len));
    if( wire->payload == ToriRSServer_Wire239()->payload )
    {
        uint32_t events2;
        uint32_t events1;

        *out_end = rsprot_g2_12_add128(&buf);
        events2 = (uint32_t)rsprot_g4_1234(&buf);
        events1 = (uint32_t)rsprot_g4_3412(&buf);
        *out_start = rsprot_g2_12(&buf);
        *out_uid = rsprot_g4_2143(&buf);
        *out_events = (events1 & ~UINT32_C(0x7fe)) | ((events2 & UINT32_C(0x3ff)) << 1);
    }
    else
    {
        *out_uid = rsprot_g4_2143(&buf);
        *out_start = rsprot_g2_12_add128(&buf);
        *out_events = (uint32_t)rsprot_g4_4321(&buf);
        *out_end = rsprot_g2_12(&buf);
    }
    return rsprot_buf_ok(&buf);
}

/*
 * READING a RUNCLIENTSCRIPT back, all-int arguments only.
 *
 * The two layouts differ in exactly one byte and it is a silent one: the type
 * string is NEWLINE-terminated at 230 and NUL-terminated at 239 (see
 * `w239_run_clientscript`'s note). A reader that scans for the wrong terminator
 * does not fail — it swallows the terminator as a type character and then reads
 * three bytes of the first argument as more of them, so `types` still compares
 * equal to "iiii" up to its embedded NUL while every argument and the script id
 * come out shifted. That is what made the skill guide's 24 cells all report the
 * same skill index at 239.
 *
 * Only 'i' is decoded, because that is the shape every payload assertion in the
 * suite inspects; anything else returns 0 rather than guessing.
 */
int
ToriRSServer_WireReadRunClientscript(
    const struct ToriRSServerWire* wire,
    const uint8_t* data,
    int len,
    char* out_types,
    int types_cap,
    int* out_argv,
    int argv_cap,
    int* out_argc,
    int* out_script_id)
{
    struct RsprotBuf buf;
    int terminator;
    int argc = 0;

    assert(wire);
    assert(data);
    assert(out_types);
    assert(out_argv);
    assert(out_argc);
    assert(out_script_id);
    assert(types_cap > 0);

    terminator = (wire->payload == ToriRSServer_Wire239()->payload)
                     ? 0
                     : '\n';
    rsprot_buf_wrap_read(&buf, (uint8_t*)data, (size_t)(len < 0 ? 0 : len));
    for( ;; )
    {
        int c = rsprot_g1(&buf);

        if( !rsprot_buf_ok(&buf) )
            return 0;
        if( c == terminator )
            break;
        if( c != 'i' || argc >= types_cap - 1 || argc >= argv_cap )
            return 0;
        out_types[argc++] = (char)c;
    }
    out_types[argc] = '\0';

    /* Reversed on the wire at both revisions: the client pops them off a
     * stack. */
    for( int i = argc - 1; i >= 0; i-- )
        out_argv[i] = rsprot_g4_1234(&buf);
    *out_script_id = rsprot_g4_1234(&buf);
    *out_argc = argc;
    return rsprot_buf_ok(&buf);
}

/*
 * READING one back, which the selftest needs and nothing on the wire does.
 *
 * Every assertion in the suite that inspects an IF_OPENSUB payload was written
 * against the 230 layout — `p1(type)`, `p2Alt2(interfaceId)`,
 * `pCombinedIdAlt3(dest)` — and 239 writes the same three fields in the
 * opposite order with a different transform on the id. A stanza that decodes by
 * hand is therefore a 230-only assertion wearing a revision-neutral name, which
 * is how the skill guide's payload checks came to assert nothing at 239.
 *
 * So the decode lives beside the encoders, one branch per revision, and the
 * callers ask for fields rather than for bytes. Mirrors `w239_if_opensub` and
 * `ToriRSServer_SendIfOpensub`'s else-branch exactly; if either moves this must
 * move with it.
 */
int
ToriRSServer_WireReadIfOpensub(
    const struct ToriRSServerWire* wire,
    const uint8_t* data,
    int len,
    int* out_group,
    int* out_uid,
    int* out_type)
{
    struct RsprotBuf buf;

    assert(wire);
    assert(data);
    assert(out_group);
    assert(out_uid);
    assert(out_type);

    rsprot_buf_wrap_read(&buf, (uint8_t*)data, (size_t)(len < 0 ? 0 : len));
    if( wire->payload == ToriRSServer_Wire239()->payload )
    {
        *out_group = rsprot_g2_21_add128(&buf);
        *out_uid = rsprot_g4_2143(&buf);
        *out_type = rsprot_g1(&buf);
    }
    else
    {
        *out_type = rsprot_g1(&buf);
        *out_group = rsprot_g2_12_add128(&buf);
        *out_uid = rsprot_g4_2143(&buf);
    }
    return rsprot_buf_ok(&buf);
}

void
ToriRSServer_WireMessagePrivateOsrs(struct RsprotBuf* buf, const char* from, int world,
                                    int counter, int crown, uint8_t const* text, int text_len)
{
    assert(buf);
    assert(from);
    assert(text_len >= 0);
    rsprot_pjstr(buf, from);
    rsprot_p2_12(buf, world);
    rsprot_p3_123(buf, counter & 0xFFFFFF);
    rsprot_p1(buf, crown);
    if( text_len > 0 )
        rsprot_pdata(buf, text, (size_t)text_len);
}

/*
 * Friends chat, as every OSRS wire writes it. RSProt has one row for each of
 * the three across 222..242 (3rd/rsprot/packets/update_friendchat_channel_
 * full_v2.c, update_friendchat_channel_singleuser.c, message_friendchannel.c:
 * the 242 client's readers), so the three wires share one writer each, as they
 * share MESSAGE_PRIVATE's above.
 *
 * The world name is written empty, as UPDATE_FRIENDLIST's is on every wire
 * here. Jagex sends "Old School 67" (RSProx index 761 [11]); no clientscript
 * the chat-channel panel runs reads it (`friendschat_getchatuserworld` is the
 * number), and this server's world has no name to send.
 */
void
ToriRSServer_WireFriendchatFullOsrs(struct RsprotBuf* buf, const char* owner, int64_t channel37,
                                    int kick_rank, const char* const* names, const int* worlds,
                                    const int* ranks, int count)
{
    assert(buf);
    assert(owner);
    assert(count >= 0);
    assert(count == 0 || names);
    assert(count == 0 || worlds);
    assert(count == 0 || ranks);
    rsprot_pjstr(buf, owner);
    rsprot_p8_12345678(buf, channel37);
    rsprot_p1(buf, kick_rank & 0xff);
    rsprot_psmart1or2null(buf, count);
    for( int i = 0; i < count; i++ )
    {
        assert(names[i]);
        rsprot_pjstr(buf, names[i]);
        rsprot_p2_12(buf, worlds[i]);
        rsprot_p1(buf, ranks[i] & 0xff);
        rsprot_pjstr(buf, "");
    }
}

void
ToriRSServer_WireFriendchatUserOsrs(struct RsprotBuf* buf, const char* name, int world, int rank)
{
    assert(buf);
    assert(name);
    rsprot_pjstr(buf, name);
    rsprot_p2_12(buf, world);
    rsprot_p1(buf, rank & 0xff);
    if( rank != -128 )
        rsprot_pjstr(buf, "");
}

void
ToriRSServer_WireMessageFriendchannelOsrs(struct RsprotBuf* buf, const char* sender,
                                          int64_t channel37, int world, int counter, int crown,
                                          uint8_t const* text, int text_len)
{
    assert(buf);
    assert(sender);
    assert(text_len >= 0);
    rsprot_pjstr(buf, sender);
    rsprot_p8_12345678(buf, channel37);
    rsprot_p2_12(buf, world);
    rsprot_p3_123(buf, counter & 0xFFFFFF);
    rsprot_p1(buf, crown);
    if( text_len > 0 )
        rsprot_pdata(buf, text, (size_t)text_len);
}

const struct ToriRSServerWire*
ToriRSServer_WireByName(char const* name)
{
    assert(name);
    if( strcmp(name, "osrs230") == 0 )
        return ToriRSServer_Wire230();
    if( strcmp(name, "osrs239") == 0 )
        return ToriRSServer_Wire239();
    if( strcmp(name, "osrs241") == 0 )
        return ToriRSServer_Wire241();
    if( strcmp(name, "osrs242") == 0 )
        return ToriRSServer_Wire242();
    return NULL;
}

const struct ToriRSServerWire*
ToriRSServer_WireDefault(void)
{
    return ToriRSServer_Wire239();
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
ToriRSServer_WirePktName(int pkt_name)
{
    switch( pkt_name )
    {
    case PKT_NAME_IF_OPENTOP: return "IF_OPENTOP";
    case PKT_NAME_IF_OPENSUB: return "IF_OPENSUB";
    case PKT_NAME_IF_CLOSESUB: return "IF_CLOSESUB";
    case PKT_NAME_IF_MOVESUB: return "IF_MOVESUB";
    case PKT_NAME_IF_RESYNC_V2: return "IF_RESYNC_V2";
    case PKT_NAME_IF_CLEARINV: return "IF_CLEARINV";
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
    case PKT_NAME_IF_SETROTATESPEED: return "IF_SETROTATESPEED";
    case PKT_NAME_IF_SETANGLE: return "IF_SETANGLE";
    case PKT_NAME_IF_SETNPCHEAD_ACTIVE: return "IF_SETNPCHEAD_ACTIVE";
    case PKT_NAME_IF_SETPLAYERMODEL_BASECOLOUR: return "IF_SETPLAYERMODEL_BASECOLOUR";
    case PKT_NAME_IF_SETPLAYERMODEL_BODYTYPE: return "IF_SETPLAYERMODEL_BODYTYPE";
    case PKT_NAME_IF_SETPLAYERMODEL_OBJ: return "IF_SETPLAYERMODEL_OBJ";
    case PKT_NAME_IF_SETPLAYERMODEL_SELF: return "IF_SETPLAYERMODEL_SELF";
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
    case PKT_NAME_TRIGGER_ONDIALOGABORT: return "TRIGGER_ONDIALOGABORT";
    case PKT_NAME_SET_PLAYER_OP: return "SET_PLAYER_OP";
    case PKT_NAME_UNSET_MAP_FLAG: return "SET_MAP_FLAG";
    case PKT_NAME_CHAT_FILTER_SETTINGS: return "CHAT_FILTER_SETTINGS";
    case PKT_NAME_MINIMAP_TOGGLE: return "MINIMAP_TOGGLE";
    case PKT_NAME_HIDENPCOPS: return "HIDENPCOPS";
    case PKT_NAME_HIDELOCOPS: return "HIDELOCOPS";
    case PKT_NAME_HIDEOBJOPS: return "HIDEOBJOPS";
    case PKT_NAME_FRIENDLIST_LOADED: return "FRIENDLIST_LOADED";
    case PKT_NAME_UPDATE_FRIENDLIST: return "UPDATE_FRIENDLIST";
    case PKT_NAME_UPDATE_IGNORELIST: return "UPDATE_IGNORELIST";
    case PKT_NAME_CAM_RESET: return "CAM_RESET";
    case PKT_NAME_CAM_UNLOCK: return "CAM_UNLOCK";
    case PKT_NAME_CAM_MOVETO: return "CAM_MOVETO";
    case PKT_NAME_CAM_MOVETO_CYCLES: return "CAM_MOVETO_CYCLES";
    case PKT_NAME_CAM_ROTATETO_COORDINATE: return "CAM_ROTATETO_COORDINATE";
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
    case PKT_NAME_MAP_ANIM_SPECIFIC: return "MAP_ANIM_SPECIFIC";
    case PKT_NAME_MAP_PROJANIM: return "MAP_PROJANIM";
    case PKT_NAME_SCRIPTEDPROJ_ADD: return "SCRIPTEDPROJ_ADD";
    case PKT_NAME_SERVER_TICK_END: return "SERVER_TICK_END";
    case PKT_NAME_MIDI_SONG: return "MIDI_SONG";
    case PKT_NAME_MIDI_SONG_WITHSECONDARY: return "MIDI_SONG_WITHSECONDARY";
    case PKT_NAME_AMBIENTSOUND_START: return "AMBIENTSOUND_START";
    case PKT_NAME_AMBIENTSOUND_STOP: return "AMBIENTSOUND_STOP";
    case PKT_NAME_MIDI_JINGLE: return "MIDI_JINGLE";
    case PKT_NAME_LOGOUT: return "LOGOUT";
    case PKT_NAME_RESET_ANIMS: return "RESET_ANIMS";
    case PKT_NAME_UPDATE_REBOOT_TIMER: return "UPDATE_REBOOT_TIMER";
    case PKT_NAME_HINT_ARROW: return "HINT_ARROW";
    case PKT_NAME_SET_ACTIVE_WORLD: return "SET_ACTIVE_WORLD";
    case PKT_NAME_REBUILD_WORLDENTITY: return "REBUILD_WORLDENTITY";
    case PKT_NAME_WORLDENTITY_INFO: return "WORLDENTITY_INFO";
    /* A `?` here is not cosmetic: this switch is what the "not transcribed"
     * report prints, so a name missing from it turns a work item into an
     * anonymous one. */
    default: return "?";
    }
}

int
ToriRSServer_WireOpcode(const struct ToriRSServerWire* wire, int pkt_name)
{
    int op;
    assert(wire);
    if( !wire->opcode )
        return -1;
    op = wire->opcode(pkt_name);
    if( op >= 0 )
        return op;

    if( pkt_name > 0 && pkt_name < PKT_NAME_COUNT && !g_absent_reported[pkt_name] )
    {
        g_absent_reported[pkt_name] = 1;
        fprintf(stderr, "torirsserver: %s has no %s -- dropping it (reported once)\n",
                wire->name, ToriRSServer_WirePktName(pkt_name));
    }
    return -1;
}

int
ToriRSServer_WireCanWrite(const struct ToriRSServerWire* wire, int pkt_name)
{
    assert(wire);
    if( !wire->payload || !wire->transcribed )
        return 1;
    for( int i = 0; i < wire->transcribed_count; i++ )
        if( wire->transcribed[i] == pkt_name )
            return 1;

    if( pkt_name > 0 && pkt_name < PKT_NAME_COUNT &&
        !g_untranscribed_reported[pkt_name] )
    {
        g_untranscribed_reported[pkt_name] = 1;
        fprintf(stderr,
                "torirsserver: %s payload for %s is not transcribed -- refusing to send "
                "it with another revision's layout (reported once)\n",
                wire->name, ToriRSServer_WirePktName(pkt_name));
    }
    return 0;
}
