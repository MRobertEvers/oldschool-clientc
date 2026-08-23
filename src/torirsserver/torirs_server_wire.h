#ifndef SRC_TORIRSSERVER_TORIRS_SERVER_WIRE_H
#define SRC_TORIRSSERVER_TORIRS_SERVER_WIRE_H

/*
 * The outbound wire adapter: which revision's bytes this server writes.
 *
 * This is the fourth vtable seam in the net stack and it is deliberately shaped
 * like the other three -- `ToriRSServerTransport` (where the bytes go),
 * `NetLoginVTable` (how the handshake runs), `GameProtoRevTable` (what the
 * client reads). Same contract in all four: a struct of function pointers, one
 * instance per implementation, a `_by_name` resolver, and NULL slots meaning
 * "the classic behaviour". Nothing above the seam asks which revision it is.
 *
 * ## Why this could not stay an enum
 *
 * `torirs_server_encode.c` used to hold its 50 opcodes in a file-local `enum` of
 * literals. That was honest while there was one revision, and it hid three
 * things the moment there were two:
 *
 *  1. **Opcodes move.** IF_OPENTOP is 60 at revision 230 and 96 at 239.
 *  2. **Opcodes disappear.** There is no UPDATE_PID and no P_COUNTDIALOG at
 *     239 -- the local player's index rides in the login response and the count
 *     dialog is a clientscript. A literal cannot express "absent", so an enum
 *     forces every revision to have every packet.
 *  3. **Payloads move even when the size does not.** This is the one that costs
 *     debugging days. IF_OPENTOP is a 2-byte interface id at both revisions and
 *     is written `p2Alt1` at 230, `p2Alt2` at 239. IF_OPENSUB is 7 bytes at
 *     both and writes its three fields in the opposite ORDER. A packet like
 *     that frames perfectly, passes every length assert, and arrives as a
 *     different interface.
 *
 * Point 3 is why `payload` is not a sparse list of overrides over a shared
 * default. A revision's writer set is *whole*: if `payload` is non-NULL, every
 * packet it does not name is REFUSED rather than written with the other
 * revision's layout. Refusing is visible; a wrong layout is not. This is the
 * same choice `osrs239_parse.c` makes on the way in.
 *
 * ## What "the 230 payloads" actually are
 *
 * Not RSProt's. `src/net/rev/osrs230/` is a hybrid: about a third of its
 * opcodes were assigned by this project and its payloads are lc254-shaped,
 * because it is paired with this repo's own client and the two only have to
 * agree with each other. So the 230 wire adapter's `payload` is NULL, meaning
 * "whatever torirs_server_encode.c has always written", and that is correct for the
 * only client it talks to. The 239 adapter's payloads are transcribed from
 * RSProt because its client is not ours.
 */

#include "net/rev/pktnames.h"

#include <stdint.h>

struct RSAreaBuf;
struct ToriRSServerZoneEvent;

/* ------------------------------------------------------------------ */
/* Payload writers                                                     */
/* ------------------------------------------------------------------ */

/*
 * A payload writer receives an opened buffer and the packet's fields, and
 * writes the body only -- no opcode, no length. Framing is the adapter's job.
 *
 * The argument lists are the union of what the revisions need, which is why
 * some carry a field a given revision ignores (239's REBUILD_NORMAL has no
 * XTEA keys, but 230's does). A writer that ignores an argument is stating
 * that its revision does not carry it; a writer that is absent is stating that
 * nobody has transcribed it yet.
 */
struct ToriRSServerWirePayload
{
    void (*if_opentop)(struct RSAreaBuf* buf, int interface_id);
    void (*if_opensub)(struct RSAreaBuf* buf, int interface_id, int dest_uid, int type);
    void (*if_closesub)(struct RSAreaBuf* buf, int dest_uid);
    void (*if_movesub)(struct RSAreaBuf* buf, int source_uid, int dest_uid);
    void (*if_setevents)(
        struct RSAreaBuf* buf,
        int component_uid,
        int start,
        int end,
        uint32_t events1,
        uint32_t events2);
    void (*if_setcolour)(struct RSAreaBuf* buf, int component_uid, int colour);
    void (*if_sethide)(struct RSAreaBuf* buf, int component_uid, int hide);
    void (*if_setmodel)(struct RSAreaBuf* buf, int component_uid, int model_id);
    void (*if_setobject)(struct RSAreaBuf* buf, int component_uid, int obj_id, int value);
    void (*if_setposition)(struct RSAreaBuf* buf, int component_uid, int x, int y);
    void (*if_setscroll)(struct RSAreaBuf* buf, int component_uid, int position);
    void (*if_setrotatespeed)(
        struct RSAreaBuf* buf,
        int component_uid,
        int x_speed,
        int y_speed);
    void (*if_setangle)(
        struct RSAreaBuf* buf,
        int component_uid,
        int zoom,
        int angle_x,
        int angle_y);
    void (*if_setnpchead_active)(struct RSAreaBuf* buf, int component_uid, int index);
    void (*if_setplayermodel_basecolour)(
        struct RSAreaBuf* buf,
        int component_uid,
        int index,
        int colour);
    void (*if_setplayermodel_bodytype)(
        struct RSAreaBuf* buf,
        int component_uid,
        int body_type);
    void (*if_setplayermodel_obj)(
        struct RSAreaBuf* buf,
        int component_uid,
        int obj_id);
    void (*if_setplayermodel_self)(
        struct RSAreaBuf* buf,
        int component_uid,
        int copy_objs);
    void (*varp_small)(struct RSAreaBuf* buf, int varp, int value);
    void (*varp_large)(struct RSAreaBuf* buf, int varp, int value);
    void (*update_runenergy)(struct RSAreaBuf* buf, int hundredths);
    void (*update_runweight)(struct RSAreaBuf* buf, int kilograms);
    void (*update_stat)(
        struct RSAreaBuf* buf,
        int stat,
        int level,
        int xp,
        int boosted);
    void (*cam_moveto)(
        struct RSAreaBuf* buf,
        int x,
        int z,
        int height,
        int rate,
        int rate2);
    void (*cam_lookat)(
        struct RSAreaBuf* buf,
        int x,
        int z,
        int height,
        int rate,
        int rate2);
    void (*cam_shake)(
        struct RSAreaBuf* buf,
        int axis,
        int random,
        int amplitude,
        int rate);
    /**
     * One zone sub-packet's payload, by canonical name.
     *
     * A single slot rather than one per event because they share a shape --
     * a packed coord-in-zone byte, a packed loc-properties byte, an id -- and
     * differ only in field order and byte order. Returns 1 when it wrote the
     * event, 0 when this revision has no writer for that kind, which the caller
     * turns into a dropped sub-packet rather than a mis-encoded one.
     *
     * `props` is (shape << 2) | angle and `pos` is (x << 4) | z within the
     * zone; both are already packed by the caller, identically at both
     * revisions.
     */
    int (*zone_payload)(
        struct RSAreaBuf* buf,
        int pkt_name,
        const struct ToriRSServerZoneEvent* event,
        int pos,
        int props);

    /**
     * A zone header: which zone the sub-packets that follow apply to.
     *
     * Three bytes at revision 239 against the hybrid's two — the level is a
     * field the 230 table has no room for, so a 239 client reading our old
     * header takes the first sub-packet's opcode as the level and everything
     * after it is one byte out.
     */
    void (*zone_header)(struct RSAreaBuf* buf, int pkt_name, int zone_x, int zone_z,
                        int level);

    void (*message_game)(struct RSAreaBuf* buf, int type, const char* text);
    void (*if_settext)(struct RSAreaBuf* buf, int uid, const char* text);
    void (*if_setnpchead)(struct RSAreaBuf* buf, int uid, int npc_id);
    void (*if_setanim)(struct RSAreaBuf* buf, int uid, int anim_id);
    void (*if_setplayerhead)(struct RSAreaBuf* buf, int uid);
    void (*set_map_flag)(struct RSAreaBuf* buf, int level, int x, int z);
    void (*chat_filter)(struct RSAreaBuf* buf, int public_, int private_, int trade);
    void (*synth_sound)(struct RSAreaBuf* buf, int id, int loops, int delay);
    void (*midi_song)(
        struct RSAreaBuf* buf,
        int id,
        int fade_out_delay,
        int fade_out_speed,
        int fade_in_delay,
        int fade_in_speed);
    /** MidiSongStopEncoder v12: p2Alt3 delay, p2Alt3 speed. Silence, as a
     *  packet of its own — MIDI_SONG cannot express it, because every id it can
     *  carry is a track. */
    void (*midi_song_stop)(
        struct RSAreaBuf* buf,
        int fade_out_delay,
        int fade_out_speed);
    /** MidiJingleEncoder v16 (revs 239): p3 length_in_millis, p2Alt3 id.
     *  Confirmed against the vendored rsprot codec, the 239 Kotlin
     *  transcription, and the rev-239 deob -- see torirs_server_wire.c. */
    void (*midi_jingle)(struct RSAreaBuf* buf, int id, int length_ms);
    /** AmbientSoundStartEncoder v2: p1Add fade flag, p2 soundscape id. The id
     *  is a config-group-15 record, not a sound effect. */
    void (*ambientsound_start)(struct RSAreaBuf* buf, int id, int fade);
    void (*ambientsound_stop)(struct RSAreaBuf* buf, int fade);
    void (*friendlist_loaded)(struct RSAreaBuf* buf, int status);
    /** One UPDATE_FRIENDLIST row. Rev 239 is a name/string record rather than
     *  the classic p8 base37 + p1 world tuple. */
    void (*friend_entry)(struct RSAreaBuf* buf, const char* name, int world);
    /** The header of an inventory update; the slot loop follows in the caller,
     *  which is the only thing that knows the container. */
    void (*inv_header)(struct RSAreaBuf* buf, int pkt_name, int uid, int container,
                       int capacity);
    /** One inventory slot. `slot` is ignored by the FULL form, which is
     *  positional, and leads the PARTIAL form. */
    void (*inv_slot)(struct RSAreaBuf* buf, int pkt_name, int slot, int obj_id,
                     int count);
    /** Stop one inventory transmit. Rev 230 addresses the component; rev 239
     *  removes the client-side container by inventory id. */
    void (*inv_stop_transmit)(struct RSAreaBuf* buf, int component_uid, int inv_id);

    /**
     * RUNCLIENTSCRIPT's argument block.
     *
     * The type string is NUL-terminated at this revision rather than
     * newline-terminated, and the script id is a plain p4 after the arguments
     * (which are still written in reverse). `types` is one char per argument.
     */
    void (*run_clientscript)(
        struct RSAreaBuf* buf,
        int script_id,
        const char* types,
        int const* intv,
        const char* const* strv,
        int argc);

    /** One ignore-list entry. The list is name-based at this revision rather
     *  than a flat array of base-37 longs. */
    void (*ignore_entry)(struct RSAreaBuf* buf, const char* name);

    /** REBUILD_REGION's header. The zone descriptor grid follows and is written
     *  by the caller, which already knows how to bit-pack it. */
    void (*rebuild_region)(struct RSAreaBuf* buf, int zone_x, int zone_z, int reload);
    void (*rebuild_normal)(
        struct RSAreaBuf* buf,
        int world_area,
        int zone_x,
        int zone_z,
        const int32_t* keys,
        int key_squares);
};

/* ------------------------------------------------------------------ */
/* The adapter                                                         */
/* ------------------------------------------------------------------ */

struct ToriRSServerWire
{
    char const* name;
    int revision;

    /** Canonical PKT_NAME_* -> wire opcode. **-1 means the revision has no
     *  such packet**, which is a real answer and not an error: the caller
     *  drops it (loudly, once) instead of writing a negative opcode. */
    int (*opcode)(int pkt_name);

    /** Wire opcode -> framing size: >=0 fixed, -1 var-u8, -2 var-u16. Used by
     *  the length check that catches an encoder disagreeing with its table. */
    int (*payload_size)(int wire_opcode);

    /** Wire opcode -> the revision's own name for it, for logs. A trace can
     *  then be grepped straight back into RSProt. NULL-safe. */
    char const* (*prot_name)(int wire_opcode);

    /**
     * The byte that identifies a sub-packet INSIDE
     * UPDATE_ZONE_PARTIAL_ENCLOSED. -1 when the revision cannot carry it there.
     *
     * This is a separate question from `opcode` and the two revisions answer it
     * differently, which is exactly why it is a slot rather than a reuse:
     *
     *   230  the same top-level opcode. A zone sub-packet is looked up in the
     *        same table as a normal packet, which is why this repo's 230 table
     *        had to pick sub-packet numbers that collide with nothing.
     *   239  the ORDINAL of RSProt's IndexedZoneProtEncoder enum -- neither the
     *        top-level opcode nor RSProt's own OldSchoolZoneProt id, both of
     *        which are different numbers that would decode as a different
     *        sub-packet. See src/net/rev/osrs239/zoneprot.h.
     *
     * Getting this wrong is invisible at the frame level: the enclosed blob is
     * length-prefixed as a whole, so a wrong sub-code produces a well-formed
     * packet whose contents are read as some other event.
     */
    int (*zone_sub_code)(int pkt_name);

    /*
     * The INBOUND table: what the client sends us.
     *
     * Separate slots because it is a separate table with its own numbering —
     * MOVE_GAMECLICK is opcode 86 at revision 230 and 114 at 239 — and because
     * forgetting it is a silent, total failure rather than a partial one. The
     * outbound path was made revision-driven first and this was left reading
     * the 230 table; every packet a 239 client sent was then framed with the
     * wrong length, so the inbound stream desynchronised on the client's first
     * word and the server misread everything after it. Nothing logs an error:
     * unknown opcodes frame as zero-length by design.
     */
    /** Wire opcode -> payload size: >=0 fixed, -1 var-u8, -2 var-u16. */
    int (*packetout_size)(int wire_opcode);
    /** Wire opcode -> canonical PKTOUT_NAME_*, or PKTOUT_NAME_NONE. */
    int (*packetout_name)(int wire_opcode);

    /** Wire opcode -> the revision's own INBOUND name, for logs. A separate
     *  slot from `prot_name` because the two directions are separate tables
     *  with separate numbering: opcode 114 is MOVE_GAMECLICK arriving and
     *  RUNCLIENTSCRIPT leaving, so naming one with the other's table produces
     *  a trace that is not merely unhelpful but actively misleading. */
    char const* (*packetout_prot_name)(int wire_opcode);

    /**
     * Rewrite an inbound body into the one `torirs_server_world.c` reads, or NULL
     * when the revision's client already sends it.
     *
     * A THIRD slot rather than a smarter `packetout_name`, because inbound the
     * client writes the bytes and we do not get to choose them. Two things a
     * name lookup cannot express turn up at 239:
     *
     *  - the fields inside a packet moved. `OPLOC1_V2` is
     *    `z, x, ctrl, subop, id` where the 230 body is `x, z, id`: the same
     *    length, the same opcode after the table maps it, and not one field in
     *    the same place. The packet frames, so the stream never desynchronises
     *    and nothing is logged; the interaction just names a loc that is not
     *    there;
     *
     *  - a family collapsed into one opcode. IF_BUTTON1..10, OPHELD1..5 and
     *    INV_BUTTON1..5 are all `IF_BUTTONX` at 239 with the op number in the
     *    payload, so ONE wire opcode has to resolve to one of twenty canonical
     *    names -- which is why this returns a name and not a length.
     *
     * NULL for 230, whose client is this repo's own and writes what the
     * handlers read by construction.
     */
    int (*translate_in)(
        int wire_opcode,
        int name,
        uint8_t const* in,
        int in_len,
        uint8_t* out,
        int out_cap,
        int* out_len);

    /**
     * 1 = opcodes >= 0x80 are written as TWO stream-cipher bytes
     * (`(op >> 8) | 0x80`, then `op & 0xFF`) -- RSProt's pSmart1Or2Enc.
     *
     * Not optional at 239, whose opcodes reach 148. A server that leaves this
     * 0 and sends a high opcode does not lose one packet; the client reads the
     * second byte as the next opcode and never resynchronises.
     */
    int opcode_smart2;

    /** NULL = write the payloads torirs_server_encode.c has always written (the
     *  lc254-shaped 230 set). Non-NULL = a whole writer set; anything it does
     *  not name is refused. See the header comment. */
    const struct ToriRSServerWirePayload* payload;

    /**
     * The canonical packets this revision's writer set covers.
     *
     * NULL means "all of them", which is the 230 case: its payloads are
     * whatever the encoders already write and there is nothing to be missing.
     * When `payload` is non-NULL this list is the whitelist, and a packet
     * outside it is REFUSED at send rather than written with the other
     * revision's bytes.
     *
     * This exists because the alternative -- letting an un-transcribed packet
     * fall through to the 230 writer -- produces a packet that frames
     * correctly, passes the length check, and means something else. There is no
     * downstream symptom of that; there is a very obvious symptom of a packet
     * that never arrives.
     */
    const int* transcribed;
    int transcribed_count;
};

/**
 * The revision-239 wire index for a player pool slot.
 *
 * The pool is 0-based; the client's player table is **1..2047, with index 0
 * unused**. Those are different numbering schemes and conflating them is not a
 * cosmetic error:
 *
 *  - the GPI init block writes one low-resolution entry per index 1..2047
 *    EXCEPT the local one, so a local index of 0 skips nothing and writes 2047
 *    entries where the client reads 2046 — every entry after the first lands on
 *    the wrong slot and the block is two bytes too long;
 *  - PLAYER_INFO's high-resolution section is keyed on the same index;
 *  - and the login response has to state it, because the client learns which
 *    slot is itself from there and nowhere else.
 *
 * This is not merely the local-login index. Every revision-239 field that
 * names a player in the GPI namespace uses the same mapping, including
 * PLAYER_INFO updates and NPC Face.Entity blocks. One function keeps those
 * fields from disagreeing.
 */
static inline int
ToriRSServer_WirePlayerIndex(int pool_pid)
{
    return pool_pid + 1;
}

/** "osrs230" or "osrs239"; NULL when unknown. */
const struct ToriRSServerWire*
ToriRSServer_WireByName(char const* name);

/** The default when nothing selects one: revision 230, the hybrid this repo's
 *  own client speaks and every existing test asserts against. */
const struct ToriRSServerWire*
ToriRSServer_WireDefault(void);

/**
 * Resolve `pkt_name` to a wire opcode, reporting an absent packet ONCE per
 * (revision, packet) rather than per send.
 *
 * The once-ness is the point. A packet the revision lacks is usually emitted
 * every tick, so a plain warning would bury the log and a silent drop would
 * hide a real gap. Returns -1 when the packet does not exist here.
 */
int
ToriRSServer_WireOpcode(const struct ToriRSServerWire* wire, int pkt_name);

/**
 * Decode a captured IF_OPENSUB payload in whichever layout this wire writes.
 *
 * Revisions disagree about both the order and the transforms of its three
 * fields, so a caller that reads the bytes itself is asserting about one
 * revision. Returns non-zero when the payload was long enough.
 */
/**
 * Decode an UPDATE_INV_FULL / UPDATE_INV_PARTIAL header.
 *
 * `*out_component` is -1 when the revision does not carry one — 239 addresses
 * these by inventory id and writes a sentinel in the component's place. Do not
 * read that -1 as a component. `*out_capacity` is -1 for the partial form.
 */
int
ToriRSServer_WireReadInvHeader(
    const struct ToriRSServerWire* wire,
    int pkt_name,
    const uint8_t* data,
    int len,
    int* out_component,
    int* out_container,
    int* out_capacity);

/**
 * Decode a captured IF_SETTEXT: the uid and the string swap places between
 * revisions, and the string's terminator differs with them.
 */
int
ToriRSServer_WireReadIfSettext(
    const struct ToriRSServerWire* wire,
    const uint8_t* data,
    int len,
    int* out_uid,
    char* out_text,
    int text_cap);

/**
 * Copy a captured MESSAGE_GAME's text out, in whichever layout this wire
 * writes. The string starts at a different offset per revision. Returns
 * non-zero on success.
 */
int
ToriRSServer_WireReadMessageGame(
    const struct ToriRSServerWire* wire,
    const uint8_t* data,
    int len,
    char* out_text,
    int text_cap);

/**
 * Decode a captured IF_SETEVENTS payload in this wire's layout.
 *
 * `out_events` is the CLASSIC mask: 239 splits it across two words and this
 * puts it back, so callers keep speaking the numbers the assertions use.
 */
int
ToriRSServer_WireReadIfSetevents(
    const struct ToriRSServerWire* wire,
    const uint8_t* data,
    int len,
    int* out_uid,
    int* out_start,
    int* out_end,
    uint32_t* out_events);

/**
 * Decode a captured RUNCLIENTSCRIPT payload, all-int arguments only.
 *
 * The type string's terminator differs by revision (newline at 230, NUL at
 * 239) and reading the wrong one shifts every argument silently. Returns
 * non-zero on success; 0 for a short payload or a non-int argument.
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
    int* out_script_id);

int
ToriRSServer_WireReadIfOpensub(
    const struct ToriRSServerWire* wire,
    const uint8_t* data,
    int len,
    int* out_group,
    int* out_uid,
    int* out_type);

/** Human-readable name for a canonical packet, for those same diagnostics. */
char const*
ToriRSServer_WirePktName(int pkt_name);

/**
 * 1 when this revision's writer set covers `pkt_name`.
 *
 * Always 1 for a wire with no `payload` (revision 230). For one with a payload
 * set, a 0 here reports once and then drops -- same policy and same reasoning
 * as an absent opcode.
 */
int
ToriRSServer_WireCanWrite(const struct ToriRSServerWire* wire, int pkt_name);

#endif
