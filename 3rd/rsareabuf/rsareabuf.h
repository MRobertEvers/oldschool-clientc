#ifndef RSAREABUF_H
#define RSAREABUF_H

/*
 * rsareabuf — an **a**rena-backed **RS** packet **buf**fer.
 *
 * A C port of LostCity's `rsbuf` (github.com/2004scape/rsbuf, `src/packet.rs`;
 * the same surface its TypeScript engine reaches through `Packet.ts`), which is
 * what a RuneScape server encodes wire packets with. Everything the Jagex
 * protocols need is here and nothing else: the byte writers/readers, the four
 * byte-order variants each multi-byte value can be written in, the "smart"
 * variable-width integers, newline/NUL terminated strings, a bit-addressed mode
 * for the player/npc info streams, and length back-patching for variable-size
 * packets.
 *
 * Two things differ deliberately from the Rust original:
 *
 *  - **Bounds are checked.** rsbuf writes through `get_unchecked_mut` and makes
 *    capacity the caller's problem. Here every accessor clamps and sets the
 *    sticky `overflow` flag instead, so a mis-sized packet is a wrong length
 *    you can assert on, not memory corruption. Check `rsab_ok()` before you
 *    send.
 *  - **Allocation is an arena** (the "area" in the name), which is what
 *    `Packet.alloc()`'s size-class pool buys in the TS engine: a server builds
 *    a burst of packets per tick and drops them all at once. `rsab_arena_reset`
 *    at the top of a tick is the whole memory-management story. Buffers can
 *    also come from the heap (`rsab_open_heap`) or wrap caller memory
 *    (`rsab_wrap`, the decode path).
 *
 * Byte-order naming follows the Jagex/RSProt convention, where "alt" numbering
 * is the transform applied to the plain big-endian form:
 *
 *   p1        v                     p2        [v>>8, v]
 *   p1_alt1   v+128                 p2_alt1   [v, v>>8]              (LE)
 *   p1_alt2   -v                    p2_alt2   [v>>8, v+128]
 *   p1_alt3   128-v                 p2_alt3   [v+128, v>>8]
 *
 *   p3        [v>>16, v>>8, v]
 *   p3_alt1   [v, v>>8, v>>16]               (LE)
 *   p3_alt2   [v>>16, v, v>>8]
 *   p3_alt3   [v>>8, v>>16, v]
 *
 *   p4        [v>>24, v>>16, v>>8, v]
 *   p4_alt1   [v, v>>8, v>>16, v>>24]        (LE)
 *   p4_alt2   [v>>8, v, v>>24, v>>16]        (ME)
 *   p4_alt3   [v>>16, v>>24, v, v>>8]        (ME reversed)
 *
 * Every reader is the exact inverse of its writer over the range the encoding
 * represents; `rsareabuf_test.c` asserts that pairing.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* String terminators. Older revisions newline-terminate, newer ones NUL. */
#define RSAB_JSTR_NEWLINE 0x0A
#define RSAB_JSTR_NUL 0x00

/* ------------------------------------------------------------------ */
/* Arena                                                               */
/* ------------------------------------------------------------------ */

/**
 * A bump allocator over caller memory. Hands out zeroed, pointer-aligned
 * blocks; frees nothing individually. `high_water` records the peak `used`
 * across every reset, which is the number to size the backing store from.
 */
struct RSArena
{
    uint8_t* base;
    size_t capacity;
    size_t used;
    size_t high_water;
    /** Set once an allocation did not fit. Survives a reset so an undersized
     *  arena is still visible after the fact. */
    int exhausted;
};

/** Wrap `capacity` bytes of caller memory. */
void
rsab_arena_init(
    struct RSArena* arena,
    void* memory,
    size_t capacity);

/** Drop every allocation at once. Keeps `high_water` and `exhausted`. */
void
rsab_arena_reset(struct RSArena* arena);

/** Bump `bytes` (zeroed, pointer-aligned) off the arena, or NULL when it does
 *  not fit — which also latches `exhausted`. */
void*
rsab_arena_alloc(
    struct RSArena* arena,
    size_t bytes);

/* ------------------------------------------------------------------ */
/* Buffer                                                              */
/* ------------------------------------------------------------------ */

/**
 * A cursor over a byte range, in byte mode or bit mode.
 *
 * `pos` is the byte cursor: after an encode it is the payload length. `bit_pos`
 * is the bit cursor, meaningful between `rsab_bits()` and `rsab_bytes()`. The
 * two are synchronised by those calls, never concurrently — exactly as in
 * rsbuf, where `bits()` seeds `bit_pos = pos << 3` and `bytes()` rounds
 * `bit_pos` back up into `pos`.
 */
struct RSAreaBuf
{
    uint8_t* data;
    size_t capacity;
    size_t pos;
    size_t bit_pos;
    /** Sticky: a read or write that did not fit. The value is still defined
     *  (writes drop, reads return 0) so decoding continues safely. */
    int overflow;
    /** Non-zero when `data` came from malloc and `rsab_close` must free it. */
    int owns_data;
};

/** Take `capacity` zeroed bytes from `arena`. Returns false (and leaves the
 *  buffer zeroed) when the arena is exhausted. */
bool
rsab_open_arena(
    struct RSAreaBuf* buf,
    struct RSArena* arena,
    size_t capacity);

/** malloc `capacity` zeroed bytes. Pair with `rsab_close`. */
bool
rsab_open_heap(
    struct RSAreaBuf* buf,
    size_t capacity);

/** Wrap caller memory — the decode path. Never freed by `rsab_close`. */
void
rsab_wrap(
    struct RSAreaBuf* buf,
    void* data,
    size_t length);

/** Free a heap buffer and zero it. No-op for arena/wrapped buffers. */
void
rsab_close(struct RSAreaBuf* buf);

/** Rewind both cursors; keeps the bytes. */
void
rsab_rewind(struct RSAreaBuf* buf);

/** Bytes written (encode) or the wrapped length's consumed prefix (decode). */
static inline size_t
rsab_len(const struct RSAreaBuf* buf)
{
    return buf->pos;
}

/** Bytes between `pos` and `capacity`. */
static inline size_t
rsab_remaining(const struct RSAreaBuf* buf)
{
    return buf->pos >= buf->capacity ? 0u : buf->capacity - buf->pos;
}

/** False once any accessor ran out of room. Check before sending. */
static inline bool
rsab_ok(const struct RSAreaBuf* buf)
{
    return buf->overflow == 0;
}

/* ------------------------------------------------------------------ */
/* Writers                                                             */
/* ------------------------------------------------------------------ */

void
rsab_p1(
    struct RSAreaBuf* buf,
    int32_t value);
void
rsab_p1_alt1(
    struct RSAreaBuf* buf,
    int32_t value);
void
rsab_p1_alt2(
    struct RSAreaBuf* buf,
    int32_t value);
void
rsab_p1_alt3(
    struct RSAreaBuf* buf,
    int32_t value);

void
rsab_p2(
    struct RSAreaBuf* buf,
    int32_t value);
void
rsab_p2_alt1(
    struct RSAreaBuf* buf,
    int32_t value);
void
rsab_p2_alt2(
    struct RSAreaBuf* buf,
    int32_t value);
void
rsab_p2_alt3(
    struct RSAreaBuf* buf,
    int32_t value);

void
rsab_p3(
    struct RSAreaBuf* buf,
    int32_t value);
void
rsab_p3_alt1(
    struct RSAreaBuf* buf,
    int32_t value);
void
rsab_p3_alt2(
    struct RSAreaBuf* buf,
    int32_t value);
void
rsab_p3_alt3(
    struct RSAreaBuf* buf,
    int32_t value);

void
rsab_p4(
    struct RSAreaBuf* buf,
    int32_t value);
void
rsab_p4_alt1(
    struct RSAreaBuf* buf,
    int32_t value);
void
rsab_p4_alt2(
    struct RSAreaBuf* buf,
    int32_t value);
void
rsab_p4_alt3(
    struct RSAreaBuf* buf,
    int32_t value);

void
rsab_p8(
    struct RSAreaBuf* buf,
    int64_t value);

/** `str` (windows-1252 bytes, passed through) followed by `terminator`. */
void
rsab_pjstr(
    struct RSAreaBuf* buf,
    const char* str,
    int terminator);

/** Unsigned smart: 1 byte over [0,127], else 2 bytes over [0,32767] biased by
 *  0x8000. Out-of-range latches `overflow` rather than truncating. */
void
rsab_psmart(
    struct RSAreaBuf* buf,
    int32_t value);

/** Signed smart: 1 byte over [-64,63] biased by 64, else 2 bytes over
 *  [-16384,16383] biased by 0xC000. */
void
rsab_psmarts(
    struct RSAreaBuf* buf,
    int32_t value);

/* ------------------------------------------------------------------ */
/* Byte-order-explicit names for the alt orders                        */
/* ------------------------------------------------------------------ */

/*
 * `_alt1`/`_alt2`/`_alt3` are ordinals: nothing in `rsab_p4_alt2` says which
 * four bytes come out, so checking a call site means reading the
 * implementation. These names say it, using the scheme
 * `docs/BUFFER_ACCESSOR_AUDIT.md` derives and `3rd/rsprot/src/rsprot_buffer.h`
 * documents in full — digits are byte significance in write order, 1 = most
 * significant, `be`/`le` when the order is standard, and a transform suffix
 * (`add128` / `neg` / `sub128`) when the value is biased rather than permuted.
 *
 * Aliases, not reimplementations: one body per encoding, two spellings. The
 * `_alt` names stay because they are what a transcription from RSProt's Kotlin
 * reads as, and both sets are checked by the same tests.
 *
 * The width-1 forms carry no order at all — they are pure value transforms, so
 * naming them `_be` would give four different encodings one name.
 */
#define rsab_p1_add128 rsab_p1_alt1
#define rsab_p1_neg rsab_p1_alt2
#define rsab_p1_sub128 rsab_p1_alt3
#define rsab_g1_add128 rsab_g1_alt1
#define rsab_g1_neg rsab_g1_alt2
#define rsab_g1_sub128 rsab_g1_alt3

/* Width 2 carries BOTH an order and, for two of the four, a low-byte bias —
 * so a purely positional name would give p2_be and p2_be_add128 one name for
 * different bytes. */
#define rsab_p2_be rsab_p2
#define rsab_p2_le rsab_p2_alt1
#define rsab_p2_be_add128 rsab_p2_alt2
#define rsab_p2_le_add128 rsab_p2_alt3
#define rsab_g2_be rsab_g2
#define rsab_g2_le rsab_g2_alt1
#define rsab_g2_be_add128 rsab_g2_alt2
#define rsab_g2_le_add128 rsab_g2_alt3

#define rsab_p3_be rsab_p3
#define rsab_p3_le rsab_p3_alt1
#define rsab_p3_132 rsab_p3_alt2
#define rsab_p3_213 rsab_p3_alt3
#define rsab_g3_be rsab_g3
#define rsab_g3_le rsab_g3_alt1
#define rsab_g3_132 rsab_g3_alt2
#define rsab_g3_213 rsab_g3_alt3

#define rsab_p4_be rsab_p4
#define rsab_p4_le rsab_p4_alt1
#define rsab_p4_3412 rsab_p4_alt2
#define rsab_p4_2143 rsab_p4_alt3
#define rsab_g4_be rsab_g4
#define rsab_g4_le rsab_g4_alt1
#define rsab_g4_3412 rsab_g4_alt2
#define rsab_g4_2143 rsab_g4_alt3

void
rsab_pdata(
    struct RSAreaBuf* buf,
    const void* src,
    size_t length);

/** Repeat `value` `count` times — padding and reserved regions. */
void
rsab_pfill(
    struct RSAreaBuf* buf,
    int32_t value,
    size_t count);

/* ------------------------------------------------------------------ */
/* Readers                                                             */
/* ------------------------------------------------------------------ */

int32_t
rsab_g1(struct RSAreaBuf* buf);
int32_t
rsab_g1s(struct RSAreaBuf* buf);
int32_t
rsab_g1_alt1(struct RSAreaBuf* buf);
int32_t
rsab_g1_alt2(struct RSAreaBuf* buf);
int32_t
rsab_g1_alt3(struct RSAreaBuf* buf);

int32_t
rsab_g2(struct RSAreaBuf* buf);
int32_t
rsab_g2s(struct RSAreaBuf* buf);
int32_t
rsab_g2_alt1(struct RSAreaBuf* buf);
int32_t
rsab_g2_alt2(struct RSAreaBuf* buf);
int32_t
rsab_g2_alt3(struct RSAreaBuf* buf);

int32_t
rsab_g3(struct RSAreaBuf* buf);
int32_t
rsab_g3_alt1(struct RSAreaBuf* buf);
int32_t
rsab_g3_alt2(struct RSAreaBuf* buf);
int32_t
rsab_g3_alt3(struct RSAreaBuf* buf);

int32_t
rsab_g4(struct RSAreaBuf* buf);
int32_t
rsab_g4_alt1(struct RSAreaBuf* buf);
int32_t
rsab_g4_alt2(struct RSAreaBuf* buf);
int32_t
rsab_g4_alt3(struct RSAreaBuf* buf);

int64_t
rsab_g8(struct RSAreaBuf* buf);

/** Copy up to `out_capacity-1` bytes of a `terminator`-terminated string into
 *  `out` and NUL-terminate it. The cursor always advances past the terminator,
 *  so an over-long string truncates the copy but not the parse. Returns the
 *  number of bytes written to `out`, excluding the NUL. */
int
rsab_gjstr(
    struct RSAreaBuf* buf,
    char* out,
    size_t out_capacity,
    int terminator);

int32_t
rsab_gsmart(struct RSAreaBuf* buf);
int32_t
rsab_gsmarts(struct RSAreaBuf* buf);

void
rsab_gdata(
    struct RSAreaBuf* buf,
    void* dest,
    size_t length);

/** Borrow `length` bytes at the cursor and step past them; NULL on overflow. */
const uint8_t*
rsab_gpeek(
    struct RSAreaBuf* buf,
    size_t length);

/* ------------------------------------------------------------------ */
/* Bit mode                                                            */
/* ------------------------------------------------------------------ */

/** Enter bit mode: `bit_pos = pos * 8`. */
void
rsab_bits(struct RSAreaBuf* buf);

/** Leave bit mode: `pos = ceil(bit_pos / 8)` — the byte-align every info
 *  stream does before its extended-info section. */
void
rsab_bytes(struct RSAreaBuf* buf);

/** Write the low `count` bits of `value`, most significant first. */
void
rsab_pbit(
    struct RSAreaBuf* buf,
    int count,
    int32_t value);

/** Read `count` bits, most significant first. */
int32_t
rsab_gbit(
    struct RSAreaBuf* buf,
    int count);

/* ------------------------------------------------------------------ */
/* Variable-length framing                                             */
/* ------------------------------------------------------------------ */

/**
 * Reserve a 1/2/4-byte length slot and return a mark to patch it with.
 *
 * The pairing is the ergonomic half of rsbuf's `psize1`/`psize2`/`psize4`,
 * which take the already-known size and reach back `size + n` bytes. Here the
 * mark remembers where the slot is, so the body can be written without the
 * caller tracking its length:
 *
 *     size_t mark = rsab_psize1_begin(&buf);
 *     rsab_p1(&buf, kind);
 *     rsab_pjstr(&buf, text, RSAB_JSTR_NUL);
 *     rsab_psize1_end(&buf, mark);
 */
size_t
rsab_psize1_begin(struct RSAreaBuf* buf);
size_t
rsab_psize2_begin(struct RSAreaBuf* buf);
size_t
rsab_psize4_begin(struct RSAreaBuf* buf);

/** Patch the reserved slot with the byte count written since `mark`. A body
 *  too large for the slot latches `overflow`. */
void
rsab_psize1_end(
    struct RSAreaBuf* buf,
    size_t mark);
void
rsab_psize2_end(
    struct RSAreaBuf* buf,
    size_t mark);
void
rsab_psize4_end(
    struct RSAreaBuf* buf,
    size_t mark);

/** rsbuf-compatible form: patch the slot `size + n` bytes back from the
 *  cursor, where the caller already knows `size`. */
void
rsab_psize1(
    struct RSAreaBuf* buf,
    uint32_t size);
void
rsab_psize2(
    struct RSAreaBuf* buf,
    uint32_t size);
void
rsab_psize4(
    struct RSAreaBuf* buf,
    uint32_t size);

#endif
