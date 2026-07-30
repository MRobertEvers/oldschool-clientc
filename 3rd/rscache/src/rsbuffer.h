#ifndef RSCACHE_BUFFER_H
#define RSCACHE_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

/** Wire type byte for an embedded params map entry (opcode 249). */
enum RSCache_ParamKind
{
    RSCACHE_PARAM_INT = 0,
    RSCACHE_PARAM_STRING = 1,
    /** Rev 237+: 8-byte value. Pre-237 caches never emit this. */
    RSCACHE_PARAM_LONG = 2,
};

struct RSCache_Params
{
    int* keys;
    void** values;
    /** Per-entry `enum RSCache_ParamKind`. Replaces the old bool is_string. */
    uint8_t* kinds;
    int count;
    int capacity;
};

/**
 * A cursor over a byte range, used for both decode ("g", get) and encode
 * ("p", put).
 *
 * Two ownership modes:
 *
 *  - **Borrowed** (`RSCache_BufferInit`, or a designated initialiser naming
 *    only data/size/position). `data` belongs to the caller, `size` is a hard
 *    capacity, and a write past it is a bug — the "p" functions assert. Every
 *    decoder and the whole net stack use this mode.
 *  - **Owned** (`RSCache_BufferInitAlloc`). `data` is heap memory belonging to
 *    the buffer and `size` is the current allocation, which grows on demand as
 *    "p" functions run. Release with `RSCache_BufferRelease`, or hand the bytes
 *    off with `RSCache_BufferDetach`.
 *
 * For an owned buffer `position` is the number of bytes written so far — the
 * payload length — and `size` is only the allocation. Do not copy an owned
 * buffer by value; the copy would alias the same allocation.
 */
struct RSCache_Buffer
{
    uint8_t* data;
    uint32_t size;
    uint32_t position;
    /* Trailing so existing `{ .data = .., .size = .., .position = .. }`
     * initialisers keep working and default to borrowed. */
    bool owns_data;
};

/** Borrowed mode: wrap caller memory. `size` is a hard capacity. */
void
RSCache_BufferInit(
    struct RSCache_Buffer* buffer,
    uint8_t* data,
    uint32_t size);

/** Owned mode: allocate `initial_capacity` bytes (0 picks a small default) that
 *  grow as needed. Returns false if the allocation failed, leaving the buffer
 *  zeroed. */
bool
RSCache_BufferInitAlloc(
    struct RSCache_Buffer* buffer,
    uint32_t initial_capacity);

/** Free an owned buffer's memory and zero it. No-op when borrowed. */
void
RSCache_BufferRelease(struct RSCache_Buffer* buffer);

/** Hand the owned allocation to the caller, who must free() it. Writes the
 *  payload length (i.e. `position`) to *out_size. The buffer is left zeroed.
 *  Returns NULL when the buffer is borrowed or empty. */
uint8_t*
RSCache_BufferDetach(
    struct RSCache_Buffer* buffer,
    uint32_t* out_size);

/** Make room for `extra` more bytes at `position`. Grows an owned buffer;
 *  for a borrowed one, just reports whether the room already exists. */
bool
RSCache_BufferEnsure(
    struct RSCache_Buffer* buffer,
    uint32_t extra);

/** Bytes written so far — the payload length of an encode. */
static inline uint32_t
RSCache_BufferLength(const struct RSCache_Buffer* buffer)
{
    return buffer->position;
}

/** Bytes left to read before `size`. */
static inline uint32_t
RSCache_BufferRemaining(const struct RSCache_Buffer* buffer)
{
    return buffer->position >= buffer->size ? 0u : buffer->size - buffer->position;
}

/*
 * Reader / writer pairs. Every "g" below has a "p" that is its exact inverse:
 * p(g(x)) reproduces the bytes and g(p(v)) reproduces the value, over the
 * value range the encoding can represent. The one documented exception is the
 * string pair — see RSCache_BufferPjstr.
 */

int
RSCache_BufferG1(struct RSCache_Buffer* buffer);
void
RSCache_BufferP1(
    struct RSCache_Buffer* buffer,
    int value);
// signed
int8_t
RSCache_BufferG1b(struct RSCache_Buffer* buffer);
void
RSCache_BufferP1b(
    struct RSCache_Buffer* buffer,
    int value);
int
RSCache_BufferG2(struct RSCache_Buffer* buffer);
void
RSCache_BufferP2(
    struct RSCache_Buffer* buffer,
    int value);
// signed
int
RSCache_BufferG2b(struct RSCache_Buffer* buffer);
void
RSCache_BufferP2b(
    struct RSCache_Buffer* buffer,
    int value);
int
RSCache_BufferG3(struct RSCache_Buffer* buffer);
void
RSCache_BufferP3(
    struct RSCache_Buffer* buffer,
    int value);
int
RSCache_BufferG4(struct RSCache_Buffer* buffer);
void
RSCache_BufferP4(
    struct RSCache_Buffer* buffer,
    int value);

int64_t
RSCache_BufferG8(struct RSCache_Buffer* buffer);
void
RSCache_BufferP8(
    struct RSCache_Buffer* buffer,
    int64_t value);

/**
 * Read a big-endian IEEE 754 single-precision float.
 * Equivalent to Java's DataInputStream.readFloat / ByteBuffer.readFloat.
 */
float
RSCache_BufferReadFloat(struct RSCache_Buffer* buffer);
void
RSCache_BufferWriteFloat(
    struct RSCache_Buffer* buffer,
    float value);

int
RSCache_BufferReadUsmart(struct RSCache_Buffer* buffer);
/** u16 when the value fits in 15 bits, else u32 with the top bit set. */
void
RSCache_BufferWriteUsmart(
    struct RSCache_Buffer* buffer,
    int value);

int
RSCache_BufferReadBigSmart(struct RSCache_Buffer* buffer);
/** -1 encodes as the reserved 2-byte 32767. */
void
RSCache_BufferWriteBigSmart(
    struct RSCache_Buffer* buffer,
    int value);

char*
RSCache_BufferReadStringNullTerminated(struct RSCache_Buffer* buffer);
char*
RSCache_BufferReadStringNewlineTerminated(struct RSCache_Buffer* buffer);

int
RSCache_BufferReadUnsignedIntSmartShortCompat(struct RSCache_Buffer* buffer);
/** Emits as many 32767 escapes as the magnitude needs, then the remainder.
 *  A value that is an exact multiple of 32767 still needs the trailing
 *  remainder byte(s) or the reader would keep consuming. */
void
RSCache_BufferWriteUnsignedIntSmartShortCompat(
    struct RSCache_Buffer* buffer,
    int value);

int
RSCache_BufferReadShortSmart(struct RSCache_Buffer* buffer);
/** 1 byte over [-64, 63], else 2 bytes over [-16384, 16383]. */
void
RSCache_BufferWriteShortSmart(
    struct RSCache_Buffer* buffer,
    int value);

int
RSCache_BufferReadUnsignedShortSmart(struct RSCache_Buffer* buffer);
/** 1 byte over [0, 127], else 2 bytes over [0, 32767]. */
void
RSCache_BufferWriteUnsignedShortSmart(
    struct RSCache_Buffer* buffer,
    int value);

/* LEB128 little-endian base-128 varint (`readVarInt2` in the reference client):
 * 7 data bits per byte, low byte first, high bit set means "more bytes follow".
 * Used by the DBTABLEINDEX (cache table 21) and DBROW tableId (config kind 38). */
int
RSCache_BufferReadVarInt2(struct RSCache_Buffer* buffer);
void
RSCache_BufferWriteVarInt2(
    struct RSCache_Buffer* buffer,
    int value);

void
RSCache_BufferReadParams(
    struct RSCache_Buffer* buffer,
    struct RSCache_Params* params);
/**
 * Exactly the bytes `RSCache_BufferWriteParams` will write.
 *
 * Derived from the writer rather than guessed: the count byte, then per entry a
 * kind byte, a 3-byte key and a payload of 4 (int), 8 (long) or `strlen + 1`
 * (string). Types carrying params used to allow a flat constant instead — `inv`
 * reserved 4096 — which is a bound that does not depend on what it bounds and
 * silently truncates the first record that outgrows it.
 */
uint32_t
RSCache_BufferParamsBound(const struct RSCache_Params* params);

/** Inverse of RSCache_BufferReadParams. Param order is preserved, so a decode
 *  followed by an encode reproduces the original bytes. */
void
RSCache_BufferWriteParams(
    struct RSCache_Buffer* buffer,
    const struct RSCache_Params* params);

int
RSCache_BufferReadto(
    struct RSCache_Buffer* buffer,
    char* out,
    int out_size,
    int len);

/**
 * Older revs use newline terminated strings. Newer revs use null terminated strings.
 */
#define RSCACHE_JSTR_TERMINATOR_NEWLINE 0x0A
#define RSCACHE_JSTR_TERMINATOR_NULL 0x00
/**
 * Write `str` followed by `terminator`.
 *
 * Byte-transparent: both this writer and the matching readers
 * (`RSCache_BufferReadStringNullTerminated` /
 * `RSCache_BufferReadStringNewlineTerminated`) treat cache strings as
 * windows-1252 wire bytes and pass them through unchanged. Decode then encode
 * reproduces every byte in 0x01..0xFF. Unicode is a consumer concern — use
 * `RSCache_Cp1252ToUtf8` / `RSCache_Utf8ToCp1252` when a real Unicode string is
 * needed (tools, logs, display outside the client's byte-indexed font).
 */
void
RSCache_BufferPjstr(
    struct RSCache_Buffer* buffer,
    const char* str,
    int terminator);

/**
 * Convert a NUL-terminated windows-1252 string to UTF-8.
 *
 * Returns the UTF-8 byte length needed excluding the NUL. Always NUL-terminates
 * `dst` when `dst_size > 0` (truncating if necessary). The five bytes
 * windows-1252 leaves undefined (0x81, 0x8D, 0x8F, 0x90, 0x9D) map to the C1
 * controls U+0081..U+009D so this and `RSCache_Utf8ToCp1252` form a bijection
 * over all 256 byte values.
 */
int
RSCache_Cp1252ToUtf8(
    const char* src,
    char* dst,
    int dst_size);

/**
 * Convert a NUL-terminated UTF-8 string to windows-1252.
 *
 * Returns the cp1252 byte length needed excluding the NUL. Always NUL-terminates
 * `dst` when `dst_size > 0`. Codepoints that windows-1252 cannot represent (and
 * malformed UTF-8) become `'?'`.
 */
int
RSCache_Utf8ToCp1252(
    const char* src,
    char* dst,
    int dst_size);

void
RSCache_BufferPwrite(
    struct RSCache_Buffer* buffer,
    const uint8_t* data,
    int data_size);

/* Raw-pointer + offset readers (for multi-cursor decode, e.g. model format) */
int
RSCache_BufferG1At(
    const uint8_t* data,
    int* offset);
int
RSCache_BufferG2At(
    const uint8_t* data,
    int* offset);
int
RSCache_BufferG4At(
    const uint8_t* data,
    int* offset);
int
RSCache_BufferReadShortSmartAt(
    const uint8_t* data,
    int* offset);

/* Raw-pointer + offset writers. Mirror the "At" readers for multi-cursor
 * encode; the caller owns the bounds check. */
void
RSCache_BufferP1At(
    uint8_t* data,
    int* offset,
    int value);
void
RSCache_BufferP2At(
    uint8_t* data,
    int* offset,
    int value);
void
RSCache_BufferP4At(
    uint8_t* data,
    int* offset,
    int value);
void
RSCache_BufferWriteShortSmartAt(
    uint8_t* data,
    int* offset,
    int value);

#define g1(buffer) RSCache_BufferG1(buffer)
#define g1b(buffer) RSCache_BufferG1b(buffer)
#define p1(buffer, value) RSCache_BufferP1(buffer, value)
#define p1b(buffer, value) RSCache_BufferP1b(buffer, value)

#define g2(buffer) RSCache_BufferG2(buffer)
#define g2b(buffer) RSCache_BufferG2b(buffer)
#define p2(buffer, value) RSCache_BufferP2(buffer, value)
#define p2b(buffer, value) RSCache_BufferP2b(buffer, value)

#define g3(buffer) RSCache_BufferG3(buffer)
#define p3(buffer, value) RSCache_BufferP3(buffer, value)
#define g4(buffer) RSCache_BufferG4(buffer)
#define p4(buffer, value) RSCache_BufferP4(buffer, value)
#define g8(buffer) RSCache_BufferG8(buffer)
#define p8(buffer, value) RSCache_BufferP8(buffer, value)
#define gf(buffer) RSCache_BufferReadFloat(buffer)
#define pf(buffer, value) RSCache_BufferWriteFloat(buffer, value)
#define gusmart(buffer) RSCache_BufferReadUsmart(buffer)
#define pusmart(buffer, value) RSCache_BufferWriteUsmart(buffer, value)
#define gbigsmart(buffer) RSCache_BufferReadBigSmart(buffer)
#define pbigsmart(buffer, value) RSCache_BufferWriteBigSmart(buffer, value)
#define gushortsmart(buffer) RSCache_BufferReadUnsignedShortSmart(buffer)
#define pushortsmart(buffer, value) RSCache_BufferWriteUnsignedShortSmart(buffer, value)
#define gshortsmart(buffer) RSCache_BufferReadShortSmart(buffer)
#define pshortsmart(buffer, value) RSCache_BufferWriteShortSmart(buffer, value)
#define guintsmartshortcompat(buffer) RSCache_BufferReadUnsignedIntSmartShortCompat(buffer)
#define puintsmartshortcompat(buffer, value)                                                       \
    RSCache_BufferWriteUnsignedIntSmartShortCompat(buffer, value)
#define gvarint2(buffer) RSCache_BufferReadVarInt2(buffer)
#define pvarint2(buffer, value) RSCache_BufferWriteVarInt2(buffer, value)

#define gcstring(buffer) RSCache_BufferReadStringNullTerminated(buffer)
#define gstringnewline(buffer) RSCache_BufferReadStringNewlineTerminated(buffer)
#define pjstr(buffer, str, terminator) RSCache_BufferPjstr(buffer, str, terminator)

#define gparams(buffer, params) RSCache_BufferReadParams(buffer, params)
#define pparams(buffer, params) RSCache_BufferWriteParams(buffer, params)
#define greadto(buffer, out, out_size, len) RSCache_BufferReadto(buffer, out, out_size, len)
#define pbuf(buffer, data, data_size) RSCache_BufferPwrite(buffer, data, data_size)

#define g1at(data, offset) RSCache_BufferG1At(data, offset)
#define g2at(data, offset) RSCache_BufferG2At(data, offset)
#define g4at(data, offset) RSCache_BufferG4At(data, offset)
#define gshortsmartat(data, offset) RSCache_BufferReadShortSmartAt(data, offset)
#define p1at(data, offset, value) RSCache_BufferP1At(data, offset, value)
#define p2at(data, offset, value) RSCache_BufferP2At(data, offset, value)
#define p4at(data, offset, value) RSCache_BufferP4At(data, offset, value)
#define pshortsmartat(data, offset, value) RSCache_BufferWriteShortSmartAt(data, offset, value)

#endif
