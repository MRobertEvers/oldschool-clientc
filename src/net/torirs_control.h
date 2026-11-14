#ifndef SRC_NET_TORIRS_CONTROL_H
#define SRC_NET_TORIRS_CONTROL_H

/*
 * The control escape: how the game server reaches a ToriRS client's control
 * channel on the game connection, with nothing added to any opcode table.
 *
 * WIRE, between two game packets (never inside one):
 *
 *   escape    the opcode value TORIRS_CONTROL_ESCAPE, written through the
 *             session's ISAAC cipher like any opcode -- as pSmart1Or2, two
 *             cipher bytes, so both ends step the cipher twice
 *   magic     the four bytes 'T' 'O' 'R' 'I', plain
 *   type      u32, little-endian
 *   length    u32, little-endian
 *   payload   `length` bytes
 *
 * After the magic the frame is the editor control link's own
 * (src/torirsserver/torirs_server_control.h): little-endian u32s, strings
 * `u32 len, bytes`. One control protocol, two ways in.
 *
 * Why an escape value and not the magic alone: every opcode byte is encrypted,
 * so the first bytes of a packet are effectively random. A bare 32-bit magic
 * would match an ordinary packet about once in 2^32 and lose the stream, and
 * the reader would have to hold every packet until four bytes had arrived --
 * SERVER_TICK_END, which has no payload and ends each tick, would wait for the
 * next tick's bytes. The escape is decided where the opcode is already decoded,
 * with one comparison. The magic after it is a guard: an escape not followed by
 * TORI means the stream is out of step, and the reader aborts.
 *
 * Why 0x7FFF: it is the largest value pSmart1Or2 can carry, and no OSRS
 * revision's server table comes near it (osrs242's last opcode is 152;
 * 3rd/rsprot/test/test_tables.c checks every table). The escape is NOT in
 * RSProt, the revision tables, pktnames.h or the server's wire tables.
 *
 * Only revisions whose opcodes are pSmart1Or2 (`opcode_smart2`) carry it. The
 * LostCity revisions (rs245_2, rs254, rs289) frame opcodes as one byte up to
 * 255 and talk to LostCity servers, which never send it, so their reader never
 * looks for it.
 *
 * Only a client that asked gets one: our login writes subVersion =
 * TORIRS_CONTROL_LOGIN_SUBVERSION (src/net/login/login_osrs.c), a value no
 * stock client sends, and the server sends control frames only to a session
 * whose login carried it (torirs_server_session.c). A stock OSRS client would
 * read the escape as an unknown opcode and lose the stream.
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

/** The opcode value that announces a control frame (pSmart1Or2's largest). */
#define TORIRS_CONTROL_ESCAPE 0x7FFF

/** The frame's first four bytes after the escape. */
#define TORIRS_CONTROL_MAGIC_0 'T'
#define TORIRS_CONTROL_MAGIC_1 'O'
#define TORIRS_CONTROL_MAGIC_2 'R'
#define TORIRS_CONTROL_MAGIC_3 'I'

/** magic, type, length. */
#define TORIRS_CONTROL_HEADER_BYTES 12

/** The largest payload a reader accepts; a larger one is a stream out of step. */
#define TORIRS_CONTROL_PAYLOAD_MAX (1024 * 1024)

/** The login block's subVersion from a ToriRS client: "TORI", big-endian. */
#define TORIRS_CONTROL_LOGIN_SUBVERSION 0x544F5249

enum TorirsControlType
{
    /**
     * The live reload rewrote archives of one client cache index: forget them.
     * Payload `u32 index, u32 n, n x u32 archive`. Sent before the REBUILD that
     * a square or loc-record reload owes, on the same connection.
     */
    TORIRS_CONTROL_CACHE_INVALIDATE = 1,

    /*
     * The replay's four (2..5). No server sends them: the RSProx replay
     * transport (src/platform/net_transport_replay.c), which plays a recording
     * as the server inside the client's own process, writes them into the
     * stream it enciphers, at the byte where they belong among the recorded
     * packets -- so the stream's order is their order, and nothing waits for
     * the bytes before them to drain. A client on any other transport that
     * reads one aborts (rs_gameproto_exec.c exec_control): it is a stream out
     * of step, not a message to skip. torirs_control_type_is_replay.
     */

    /**
     * A replay seek's chase tick begins (replay/replay_scrub.h, CHASE): its
     * world packets are withheld, so the local player's tile is stated here
     * for the scripts that read `coord`, and the client charges its clock
     * the cycles the chase did not run. Ahead of the tick's packets.
     * Payload `u32 coord, u32 cycle, u32 len, len bytes name`: `coord` packed
     * as the coord type is (level << 28 | x << 14 | z); `cycle` the client
     * cycle, counted from the chase's start, in which a chase of 30 cycles a
     * tick executes this tick (bits 0..30), and bit 31 set on a chase's first
     * tick, which the count starts from (the client's arithmetic is
     * exec_control_replay_coord's); `name` the local player's name on a chase's
     * first tick (len 0 on the others; never NUL-terminated, at most
     * TORIRS_CONTROL_REPLAY_NAME_MAX - 1 bytes).
     */
    TORIRS_CONTROL_REPLAY_COORD = 2,

    /**
     * A replay seek's world keyframe at tick P (replay/replay_world.h): the
     * world the ledger says a client arriving at P is sent, applied in its
     * place in the stream (replay/replay_world_apply.c) after the chase's
     * packets and before the pre-roll's.
     * Payload `u32 tick, u32 serial`. The keyframe itself -- megabytes of
     * tables and op lists -- does NOT travel in the frame: the transport and
     * the client are one process, and it waits in the replay world mailbox
     * (ReplayWorld_Post), one slot. `serial` is the mailbox serial it was
     * posted under and `tick` its tick; the handler asserts that the mailbox
     * holds exactly that delivery, so a frame and a keyframe that disagree
     * stop the client where they part.
     */
    TORIRS_CONTROL_REPLAY_WORLD = 3,

    /**
     * A public chat line of a PLAYER_INFO a replay seek's chase withheld: the
     * chatbox keeps it, so it goes where the PLAYER_INFO stood.
     * Payload `u32 say, u32 crown, u32 colour_effect, u32 name_len, name_len
     * bytes name, u32 data_len, data_len bytes data`: `say` 1 for a SAY (data
     * is its text, not NUL-terminated) and 0 for a CHAT (data is the
     * revision's packed text, for chat_unpack); `name` the speaker's name as
     * its appearance states it (name_len 0: none; at most
     * TORIRS_CONTROL_REPLAY_NAME_MAX - 1 bytes); data_len at most
     * TORIRS_CONTROL_REPLAY_CHAT_DATA_MAX.
     */
    TORIRS_CONTROL_REPLAY_CHAT = 4,

    /**
     * The recorder's client answered its chatbox prompt here: a client packet
     * RSProx recorded among the server's (RESUME_P_OBJDIALOG, _COUNTDIALOG,
     * _COUNTDIALOG_LONG, _NAMEDIALOG, _STRINGDIALOG), staged where it was
     * sent. The official client closes the prompt itself as it sends the
     * answer and no server packet says so; a replay drops client packets, so
     * the executor runs that close here (rs_gameproto_exec.c
     * exec_control_replay_dialog).
     * Payload `u32 prot`: the answer's GameProtoPktOutName (net/rev/pktnames.h),
     * which names the kind of prompt answered.
     */
    TORIRS_CONTROL_REPLAY_DIALOG = 5,
};

/** Room for a replay name, its NUL included (a name is at most 12 characters;
 *  the room is the scrubber's). */
#define TORIRS_CONTROL_REPLAY_NAME_MAX 32

/** The longest chat data a REPLAY_CHAT carries (a SAY's text or a CHAT's packed
 *  bytes: both are a u8-length field on the wire, well inside this). */
#define TORIRS_CONTROL_REPLAY_CHAT_DATA_MAX 1024

/** Room for each replay frame's payload. */
#define TORIRS_CONTROL_REPLAY_COORD_BYTES_MAX (12 + TORIRS_CONTROL_REPLAY_NAME_MAX)
#define TORIRS_CONTROL_REPLAY_WORLD_BYTES 8
#define TORIRS_CONTROL_REPLAY_DIALOG_BYTES 4
#define TORIRS_CONTROL_REPLAY_CHAT_BYTES_MAX                                                       \
    (20 + TORIRS_CONTROL_REPLAY_NAME_MAX + TORIRS_CONTROL_REPLAY_CHAT_DATA_MAX)

/** Archives one CACHE_INVALIDATE frame names; a longer list is several frames. */
#define TORIRS_CONTROL_INVALIDATE_PER_FRAME 4096

/** Room for one CACHE_INVALIDATE frame's payload. */
#define TORIRS_CONTROL_INVALIDATE_BYTES_MAX (8 + 4 * TORIRS_CONTROL_INVALIDATE_PER_FRAME)

/**
 * A control frame the client read, in arrival order with the packets around
 * it. Owned by the RevPacket that carries it (revpacket.h `_control`) and
 * freed by gameproto_free.
 */
struct TorirsControlFrame
{
    int type;
    int length;
    uint8_t* payload;
};

static inline char const*
torirs_control_type_name(int type)
{
    if( type == TORIRS_CONTROL_CACHE_INVALIDATE )
        return "CACHE_INVALIDATE";
    if( type == TORIRS_CONTROL_REPLAY_COORD )
        return "REPLAY_COORD";
    if( type == TORIRS_CONTROL_REPLAY_WORLD )
        return "REPLAY_WORLD";
    if( type == TORIRS_CONTROL_REPLAY_CHAT )
        return "REPLAY_CHAT";
    if( type == TORIRS_CONTROL_REPLAY_DIALOG )
        return "REPLAY_DIALOG";
    return "?";
}

/** 1 for the types only the replay transport writes (never a server). */
static inline int
torirs_control_type_is_replay(int type)
{
    return type == TORIRS_CONTROL_REPLAY_COORD || type == TORIRS_CONTROL_REPLAY_WORLD ||
           type == TORIRS_CONTROL_REPLAY_CHAT || type == TORIRS_CONTROL_REPLAY_DIALOG;
}

static inline void
torirs_control_put_u32(
    uint8_t* out,
    uint32_t v)
{
    out[0] = (uint8_t)v;
    out[1] = (uint8_t)(v >> 8);
    out[2] = (uint8_t)(v >> 16);
    out[3] = (uint8_t)(v >> 24);
}

static inline uint32_t
torirs_control_get_u32(uint8_t const* in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

/** 1 when the first four header bytes are the magic. */
static inline int
torirs_control_magic_ok(uint8_t const* header)
{
    return header[0] == TORIRS_CONTROL_MAGIC_0 && header[1] == TORIRS_CONTROL_MAGIC_1 &&
           header[2] == TORIRS_CONTROL_MAGIC_2 && header[3] == TORIRS_CONTROL_MAGIC_3;
}

/** The 12 header bytes for a frame of `type` carrying `length` bytes. */
static inline void
torirs_control_put_header(
    uint8_t* out,
    int type,
    int length)
{
    assert(out);
    assert(length >= 0);
    assert(length <= TORIRS_CONTROL_PAYLOAD_MAX);
    out[0] = TORIRS_CONTROL_MAGIC_0;
    out[1] = TORIRS_CONTROL_MAGIC_1;
    out[2] = TORIRS_CONTROL_MAGIC_2;
    out[3] = TORIRS_CONTROL_MAGIC_3;
    torirs_control_put_u32(out + 4, (uint32_t)type);
    torirs_control_put_u32(out + 8, (uint32_t)length);
}

/**
 * CACHE_INVALIDATE's payload for at most TORIRS_CONTROL_INVALIDATE_PER_FRAME
 * archives of `index`, into `out` (8 + 4 x count bytes). Returns the length.
 */
static inline int
torirs_control_invalidate_encode(
    int index,
    int const* archives,
    int count,
    uint8_t* out)
{
    assert(index >= 0);
    assert(index < 255);
    assert(count >= 0);
    assert(count <= TORIRS_CONTROL_INVALIDATE_PER_FRAME);
    assert(out);
    if( count > 0 )
        assert(archives);
    torirs_control_put_u32(out, (uint32_t)index);
    torirs_control_put_u32(out + 4, (uint32_t)count);
    for( int i = 0; i < count; i++ )
        torirs_control_put_u32(out + 8 + 4 * i, (uint32_t)archives[i]);
    return 8 + 4 * count;
}

/**
 * Read a CACHE_INVALIDATE payload: its index and archive count, and archive
 * `i` with torirs_control_invalidate_archive. Returns 0 when the payload's
 * length does not match its count -- a frame the server cannot have written.
 */
static inline int
torirs_control_invalidate_decode(
    struct TorirsControlFrame const* frame,
    int* index,
    int* count)
{
    uint32_t n;

    assert(frame);
    assert(index);
    assert(count);
    if( frame->length < 8 )
        return 0;
    n = torirs_control_get_u32(frame->payload + 4);
    if( n > TORIRS_CONTROL_INVALIDATE_PER_FRAME || (int)(8 + 4 * n) != frame->length )
        return 0;
    *index = (int)torirs_control_get_u32(frame->payload);
    *count = (int)n;
    return 1;
}

static inline int
torirs_control_invalidate_archive(
    struct TorirsControlFrame const* frame,
    int i)
{
    return (int)torirs_control_get_u32(frame->payload + 8 + 4 * i);
}

/* --- the replay's frames (TORIRS_CONTROL_REPLAY_*) ------------------------- */

/** A `u32 len, bytes` string at `out`; returns the bytes written. */
static inline int
torirs_control_put_str(
    uint8_t* out,
    char const* s,
    int len)
{
    assert(out);
    assert(len >= 0);
    if( len > 0 )
        assert(s);
    torirs_control_put_u32(out, (uint32_t)len);
    if( len > 0 )
        memcpy(out + 4, s, (size_t)len);
    return 4 + len;
}

/**
 * A `u32 len, bytes` string at `*at` of `frame`, into `out` (room `cap`, NUL
 * added). 0 when it runs past the payload or does not fit.
 */
static inline int
torirs_control_get_str(
    struct TorirsControlFrame const* frame,
    int* at,
    char* out,
    int cap)
{
    uint32_t len;

    assert(frame);
    assert(at);
    assert(out);
    assert(cap > 0);
    if( *at + 4 > frame->length )
        return 0;
    len = torirs_control_get_u32(frame->payload + *at);
    if( len >= (uint32_t)cap || (int)len > frame->length - *at - 4 )
        return 0;
    memcpy(out, frame->payload + *at + 4, len);
    out[len] = '\0';
    *at += 4 + (int)len;
    return 1;
}

struct TorirsControlReplayCoord
{
    int coord;
    /** The full-rate chase's cycle for the tick, and whether the chase
     *  starts here. */
    int cycle;
    int first;
    /** "" on every chase tick but the first. */
    char name[TORIRS_CONTROL_REPLAY_NAME_MAX];
};

struct TorirsControlReplayWorld
{
    int tick;
    uint32_t serial;
};

struct TorirsControlReplayChat
{
    int say;
    int crown;
    int colour_effect;
    char name[TORIRS_CONTROL_REPLAY_NAME_MAX];
    /** Borrowed from the frame: `length` bytes, not NUL-terminated. */
    uint8_t const* data;
    int length;
};

/** REPLAY_COORD's payload, into `out` (TORIRS_CONTROL_REPLAY_COORD_BYTES_MAX).
 *  Returns the length. */
static inline int
torirs_control_replay_coord_encode(
    int coord,
    int cycle,
    int first,
    char const* name,
    uint8_t* out)
{
    int name_len;

    assert(cycle >= 0);
    assert(name);
    assert(out);
    name_len = (int)strlen(name);
    assert(name_len < TORIRS_CONTROL_REPLAY_NAME_MAX);
    torirs_control_put_u32(out, (uint32_t)coord);
    torirs_control_put_u32(out + 4, (uint32_t)cycle | (first ? 0x80000000u : 0u));
    return 8 + torirs_control_put_str(out + 8, name, name_len);
}

/** 0 when the payload is not one the transport can have written. */
static inline int
torirs_control_replay_coord_decode(
    struct TorirsControlFrame const* frame,
    struct TorirsControlReplayCoord* out)
{
    int at = 8;

    assert(frame);
    assert(out);
    assert(frame->type == TORIRS_CONTROL_REPLAY_COORD);
    if( frame->length < 12 )
        return 0;
    out->coord = (int)torirs_control_get_u32(frame->payload);
    out->cycle = (int)(torirs_control_get_u32(frame->payload + 4) & 0x7fffffffu);
    out->first = (torirs_control_get_u32(frame->payload + 4) & 0x80000000u) != 0;
    if( !torirs_control_get_str(frame, &at, out->name, (int)sizeof(out->name)) )
        return 0;
    return at == frame->length;
}

/** REPLAY_WORLD's payload, into `out` (TORIRS_CONTROL_REPLAY_WORLD_BYTES).
 *  Returns the length. */
static inline int
torirs_control_replay_world_encode(
    int tick,
    uint32_t serial,
    uint8_t* out)
{
    assert(tick >= 0);
    assert(serial != 0);
    assert(out);
    torirs_control_put_u32(out, (uint32_t)tick);
    torirs_control_put_u32(out + 4, serial);
    return TORIRS_CONTROL_REPLAY_WORLD_BYTES;
}

static inline int
torirs_control_replay_world_decode(
    struct TorirsControlFrame const* frame,
    struct TorirsControlReplayWorld* out)
{
    assert(frame);
    assert(out);
    assert(frame->type == TORIRS_CONTROL_REPLAY_WORLD);
    if( frame->length != TORIRS_CONTROL_REPLAY_WORLD_BYTES )
        return 0;
    out->tick = (int)torirs_control_get_u32(frame->payload);
    out->serial = torirs_control_get_u32(frame->payload + 4);
    return out->tick >= 0 && out->serial != 0;
}

/** REPLAY_CHAT's payload, into `out` (TORIRS_CONTROL_REPLAY_CHAT_BYTES_MAX).
 *  `name` "" for none. Returns the length. */
static inline int
torirs_control_replay_chat_encode(
    int say,
    int crown,
    int colour_effect,
    char const* name,
    uint8_t const* data,
    int length,
    uint8_t* out)
{
    int name_len;
    int at = 0;

    assert(say == 0 || say == 1);
    assert(name);
    name_len = (int)strlen(name);
    assert(name_len < TORIRS_CONTROL_REPLAY_NAME_MAX);
    assert(length >= 0);
    assert(length <= TORIRS_CONTROL_REPLAY_CHAT_DATA_MAX);
    assert(out);
    torirs_control_put_u32(out + at, (uint32_t)say);
    at += 4;
    torirs_control_put_u32(out + at, (uint32_t)crown);
    at += 4;
    torirs_control_put_u32(out + at, (uint32_t)colour_effect);
    at += 4;
    at += torirs_control_put_str(out + at, name, name_len);
    at += torirs_control_put_str(out + at, (char const*)data, length);
    return at;
}

static inline int
torirs_control_replay_chat_decode(
    struct TorirsControlFrame const* frame,
    struct TorirsControlReplayChat* out)
{
    int at = 12;
    uint32_t len;

    assert(frame);
    assert(out);
    assert(frame->type == TORIRS_CONTROL_REPLAY_CHAT);
    if( frame->length < 12 )
        return 0;
    out->say = (int)torirs_control_get_u32(frame->payload);
    out->crown = (int)torirs_control_get_u32(frame->payload + 4);
    out->colour_effect = (int)torirs_control_get_u32(frame->payload + 8);
    if( out->say != 0 && out->say != 1 )
        return 0;
    if( !torirs_control_get_str(frame, &at, out->name, (int)sizeof(out->name)) )
        return 0;
    if( at + 4 > frame->length )
        return 0;
    len = torirs_control_get_u32(frame->payload + at);
    if( len > TORIRS_CONTROL_REPLAY_CHAT_DATA_MAX || (int)len != frame->length - at - 4 )
        return 0;
    out->data = frame->payload + at + 4;
    out->length = (int)len;
    return 1;
}

/** REPLAY_DIALOG's payload, into `out` (TORIRS_CONTROL_REPLAY_DIALOG_BYTES):
 *  the answer's GameProtoPktOutName. Returns the length. */
static inline int
torirs_control_replay_dialog_encode(
    int pkt_out_name,
    uint8_t* out)
{
    assert(pkt_out_name > 0);
    assert(out);
    torirs_control_put_u32(out, (uint32_t)pkt_out_name);
    return TORIRS_CONTROL_REPLAY_DIALOG_BYTES;
}

/** The answer's GameProtoPktOutName into `*pkt_out_name`; 0 when the payload
 *  is not one the transport can have written. Which names are answers is the
 *  executor's to check. */
static inline int
torirs_control_replay_dialog_decode(
    struct TorirsControlFrame const* frame,
    int* pkt_out_name)
{
    assert(frame);
    assert(pkt_out_name);
    assert(frame->type == TORIRS_CONTROL_REPLAY_DIALOG);
    if( frame->length != TORIRS_CONTROL_REPLAY_DIALOG_BYTES )
        return 0;
    *pkt_out_name = (int)torirs_control_get_u32(frame->payload);
    return *pkt_out_name > 0;
}

#endif
