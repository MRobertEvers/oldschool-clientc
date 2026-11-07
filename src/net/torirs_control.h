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
};

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
    return "?";
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

#endif
