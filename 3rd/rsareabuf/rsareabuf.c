#include "rsareabuf.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Arena                                                               */
/* ------------------------------------------------------------------ */

void
rsab_arena_init(
    struct RSArena* arena,
    void* memory,
    size_t capacity)
{
    arena->base = (uint8_t*)memory;
    arena->capacity = memory ? capacity : 0;
    arena->used = 0;
    arena->high_water = 0;
    arena->exhausted = 0;
}

void
rsab_arena_reset(struct RSArena* arena)
{
    if( arena->used > arena->high_water )
        arena->high_water = arena->used;
    arena->used = 0;
}

void*
rsab_arena_alloc(
    struct RSArena* arena,
    size_t bytes)
{
    /* Pointer alignment keeps a block usable for any struct a caller might
     * place in it, not just the byte arrays this file hands out. */
    const size_t align = sizeof(void*);
    size_t start = (arena->used + (align - 1)) & ~(align - 1);

    if( !arena->base || bytes > arena->capacity || start > arena->capacity - bytes )
    {
        arena->exhausted = 1;
        return NULL;
    }

    arena->used = start + bytes;
    if( arena->used > arena->high_water )
        arena->high_water = arena->used;
    memset(arena->base + start, 0, bytes);
    return arena->base + start;
}

/* ------------------------------------------------------------------ */
/* Buffer lifecycle                                                    */
/* ------------------------------------------------------------------ */

bool
rsab_open_arena(
    struct RSAreaBuf* buf,
    struct RSArena* arena,
    size_t capacity)
{
    void* mem = rsab_arena_alloc(arena, capacity);
    memset(buf, 0, sizeof(*buf));
    if( !mem )
        return false;
    buf->data = (uint8_t*)mem;
    buf->capacity = capacity;
    return true;
}

bool
rsab_open_heap(
    struct RSAreaBuf* buf,
    size_t capacity)
{
    void* mem = calloc(capacity ? capacity : 1, 1);
    memset(buf, 0, sizeof(*buf));
    if( !mem )
        return false;
    buf->data = (uint8_t*)mem;
    buf->capacity = capacity;
    buf->owns_data = 1;
    return true;
}

void
rsab_wrap(
    struct RSAreaBuf* buf,
    void* data,
    size_t length)
{
    memset(buf, 0, sizeof(*buf));
    buf->data = (uint8_t*)data;
    buf->capacity = data ? length : 0;
}

void
rsab_close(struct RSAreaBuf* buf)
{
    if( buf->owns_data )
        free(buf->data);
    memset(buf, 0, sizeof(*buf));
}

void
rsab_rewind(struct RSAreaBuf* buf)
{
    buf->pos = 0;
    buf->bit_pos = 0;
}

/* ------------------------------------------------------------------ */
/* Byte primitives                                                     */
/* ------------------------------------------------------------------ */

/* Every writer funnels through here, so the bounds check exists once. A write
 * past the end drops the byte and latches `overflow`; the cursor still
 * advances so the reported length stays the length the caller intended and the
 * mismatch is visible rather than silently short. */
static void
put(
    struct RSAreaBuf* buf,
    int32_t value)
{
    if( buf->pos < buf->capacity )
        buf->data[buf->pos] = (uint8_t)value;
    else
        buf->overflow = 1;
    buf->pos++;
}

static int32_t
get(struct RSAreaBuf* buf)
{
    if( buf->pos >= buf->capacity )
    {
        buf->overflow = 1;
        return 0;
    }
    return buf->data[buf->pos++];
}

/* ------------------------------------------------------------------ */
/* Writers                                                             */
/* ------------------------------------------------------------------ */

void
rsab_p1(
    struct RSAreaBuf* buf,
    int32_t value)
{
    put(buf, value);
}

void
rsab_p1_alt1(
    struct RSAreaBuf* buf,
    int32_t value)
{
    put(buf, value + 128);
}

void
rsab_p1_alt2(
    struct RSAreaBuf* buf,
    int32_t value)
{
    put(buf, -value);
}

void
rsab_p1_alt3(
    struct RSAreaBuf* buf,
    int32_t value)
{
    put(buf, 128 - value);
}

void
rsab_p2(
    struct RSAreaBuf* buf,
    int32_t value)
{
    put(buf, value >> 8);
    put(buf, value);
}

void
rsab_p2_alt1(
    struct RSAreaBuf* buf,
    int32_t value)
{
    put(buf, value);
    put(buf, value >> 8);
}

void
rsab_p2_alt2(
    struct RSAreaBuf* buf,
    int32_t value)
{
    put(buf, value >> 8);
    put(buf, value + 128);
}

void
rsab_p2_alt3(
    struct RSAreaBuf* buf,
    int32_t value)
{
    put(buf, value + 128);
    put(buf, value >> 8);
}

void
rsab_p3(
    struct RSAreaBuf* buf,
    int32_t value)
{
    put(buf, value >> 16);
    put(buf, value >> 8);
    put(buf, value);
}

void
rsab_p4(
    struct RSAreaBuf* buf,
    int32_t value)
{
    put(buf, value >> 24);
    put(buf, value >> 16);
    put(buf, value >> 8);
    put(buf, value);
}

void
rsab_p4_alt1(
    struct RSAreaBuf* buf,
    int32_t value)
{
    put(buf, value);
    put(buf, value >> 8);
    put(buf, value >> 16);
    put(buf, value >> 24);
}

void
rsab_p4_alt2(
    struct RSAreaBuf* buf,
    int32_t value)
{
    put(buf, value >> 8);
    put(buf, value);
    put(buf, value >> 24);
    put(buf, value >> 16);
}

void
rsab_p4_alt3(
    struct RSAreaBuf* buf,
    int32_t value)
{
    put(buf, value >> 16);
    put(buf, value >> 24);
    put(buf, value);
    put(buf, value >> 8);
}

void
rsab_p8(
    struct RSAreaBuf* buf,
    int64_t value)
{
    rsab_p4(buf, (int32_t)((uint64_t)value >> 32));
    rsab_p4(buf, (int32_t)((uint64_t)value & 0xffffffffu));
}

void
rsab_pjstr(
    struct RSAreaBuf* buf,
    const char* str,
    int terminator)
{
    if( str )
        for( const char* c = str; *c; c++ )
            put(buf, (uint8_t)*c);
    put(buf, terminator);
}

void
rsab_psmart(
    struct RSAreaBuf* buf,
    int32_t value)
{
    if( value >= 0 && value < 128 )
    {
        put(buf, value);
        return;
    }
    if( value >= 0 && value < 32768 )
    {
        rsab_p2(buf, value + 0x8000);
        return;
    }
    /* rsbuf panics here. Latch instead: the caller sees a bad packet rather
     * than the process dying mid-tick. */
    buf->overflow = 1;
}

void
rsab_psmarts(
    struct RSAreaBuf* buf,
    int32_t value)
{
    if( value >= -64 && value < 64 )
    {
        put(buf, value + 64);
        return;
    }
    if( value >= -16384 && value < 16384 )
    {
        rsab_p2(buf, value + 0xC000);
        return;
    }
    buf->overflow = 1;
}

void
rsab_pdata(
    struct RSAreaBuf* buf,
    const void* src,
    size_t length)
{
    const uint8_t* bytes = (const uint8_t*)src;
    if( !bytes )
    {
        buf->pos += length;
        if( length )
            buf->overflow = 1;
        return;
    }
    if( buf->pos + length <= buf->capacity )
    {
        memcpy(buf->data + buf->pos, bytes, length);
        buf->pos += length;
        return;
    }
    for( size_t i = 0; i < length; i++ )
        put(buf, bytes[i]);
}

void
rsab_pfill(
    struct RSAreaBuf* buf,
    int32_t value,
    size_t count)
{
    for( size_t i = 0; i < count; i++ )
        put(buf, value);
}

/* ------------------------------------------------------------------ */
/* Readers                                                             */
/* ------------------------------------------------------------------ */

int32_t
rsab_g1(struct RSAreaBuf* buf)
{
    return get(buf);
}

int32_t
rsab_g1s(struct RSAreaBuf* buf)
{
    return (int8_t)get(buf);
}

int32_t
rsab_g1_alt1(struct RSAreaBuf* buf)
{
    return (get(buf) - 128) & 0xff;
}

int32_t
rsab_g1_alt2(struct RSAreaBuf* buf)
{
    return (-get(buf)) & 0xff;
}

int32_t
rsab_g1_alt3(struct RSAreaBuf* buf)
{
    return (128 - get(buf)) & 0xff;
}

int32_t
rsab_g2(struct RSAreaBuf* buf)
{
    int32_t hi = get(buf);
    int32_t lo = get(buf);
    return (hi << 8) | lo;
}

int32_t
rsab_g2s(struct RSAreaBuf* buf)
{
    return (int16_t)rsab_g2(buf);
}

int32_t
rsab_g2_alt1(struct RSAreaBuf* buf)
{
    int32_t lo = get(buf);
    int32_t hi = get(buf);
    return (hi << 8) | lo;
}

int32_t
rsab_g2_alt2(struct RSAreaBuf* buf)
{
    int32_t hi = get(buf);
    int32_t lo = (get(buf) - 128) & 0xff;
    return (hi << 8) | lo;
}

int32_t
rsab_g2_alt3(struct RSAreaBuf* buf)
{
    int32_t lo = (get(buf) - 128) & 0xff;
    int32_t hi = get(buf);
    return (hi << 8) | lo;
}

int32_t
rsab_g3(struct RSAreaBuf* buf)
{
    int32_t b0 = get(buf);
    int32_t b1 = get(buf);
    int32_t b2 = get(buf);
    return (b0 << 16) | (b1 << 8) | b2;
}

int32_t
rsab_g4(struct RSAreaBuf* buf)
{
    int32_t b0 = get(buf);
    int32_t b1 = get(buf);
    int32_t b2 = get(buf);
    int32_t b3 = get(buf);
    return (int32_t)(((uint32_t)b0 << 24) | ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) |
                     (uint32_t)b3);
}

int32_t
rsab_g4_alt1(struct RSAreaBuf* buf)
{
    int32_t b0 = get(buf);
    int32_t b1 = get(buf);
    int32_t b2 = get(buf);
    int32_t b3 = get(buf);
    return (int32_t)(((uint32_t)b3 << 24) | ((uint32_t)b2 << 16) | ((uint32_t)b1 << 8) |
                     (uint32_t)b0);
}

int32_t
rsab_g4_alt2(struct RSAreaBuf* buf)
{
    int32_t b0 = get(buf);
    int32_t b1 = get(buf);
    int32_t b2 = get(buf);
    int32_t b3 = get(buf);
    return (int32_t)(((uint32_t)b2 << 24) | ((uint32_t)b3 << 16) | ((uint32_t)b0 << 8) |
                     (uint32_t)b1);
}

int32_t
rsab_g4_alt3(struct RSAreaBuf* buf)
{
    int32_t b0 = get(buf);
    int32_t b1 = get(buf);
    int32_t b2 = get(buf);
    int32_t b3 = get(buf);
    return (int32_t)(((uint32_t)b1 << 24) | ((uint32_t)b0 << 16) | ((uint32_t)b3 << 8) |
                     (uint32_t)b2);
}

int64_t
rsab_g8(struct RSAreaBuf* buf)
{
    uint32_t hi = (uint32_t)rsab_g4(buf);
    uint32_t lo = (uint32_t)rsab_g4(buf);
    return (int64_t)(((uint64_t)hi << 32) | lo);
}

int
rsab_gjstr(
    struct RSAreaBuf* buf,
    char* out,
    size_t out_capacity,
    int terminator)
{
    size_t written = 0;
    for( ;; )
    {
        if( buf->pos >= buf->capacity )
        {
            buf->overflow = 1;
            break;
        }
        int c = buf->data[buf->pos++];
        if( c == (terminator & 0xff) )
            break;
        if( out && written + 1 < out_capacity )
            out[written++] = (char)c;
    }
    if( out && out_capacity )
        out[written] = '\0';
    return (int)written;
}

int32_t
rsab_gsmart(struct RSAreaBuf* buf)
{
    if( buf->pos >= buf->capacity )
    {
        buf->overflow = 1;
        return 0;
    }
    if( buf->data[buf->pos] < 128 )
        return get(buf);
    return rsab_g2(buf) - 0x8000;
}

int32_t
rsab_gsmarts(struct RSAreaBuf* buf)
{
    if( buf->pos >= buf->capacity )
    {
        buf->overflow = 1;
        return 0;
    }
    if( buf->data[buf->pos] < 128 )
        return get(buf) - 64;
    return rsab_g2(buf) - 0xC000;
}

void
rsab_gdata(
    struct RSAreaBuf* buf,
    void* dest,
    size_t length)
{
    uint8_t* out = (uint8_t*)dest;
    for( size_t i = 0; i < length; i++ )
    {
        int32_t v = get(buf);
        if( out )
            out[i] = (uint8_t)v;
    }
}

const uint8_t*
rsab_gpeek(
    struct RSAreaBuf* buf,
    size_t length)
{
    if( buf->pos + length > buf->capacity )
    {
        buf->overflow = 1;
        return NULL;
    }
    const uint8_t* at = buf->data + buf->pos;
    buf->pos += length;
    return at;
}

/* ------------------------------------------------------------------ */
/* Bit mode                                                            */
/* ------------------------------------------------------------------ */

void
rsab_bits(struct RSAreaBuf* buf)
{
    buf->bit_pos = buf->pos << 3;
}

void
rsab_bytes(struct RSAreaBuf* buf)
{
    buf->pos = (buf->bit_pos + 7) >> 3;
}

void
rsab_pbit(
    struct RSAreaBuf* buf,
    int count,
    int32_t value)
{
    size_t byte_pos = buf->bit_pos >> 3;
    int remaining = 8 - (int)(buf->bit_pos & 7);

    if( count <= 0 || count > 32 )
    {
        buf->overflow = 1;
        return;
    }
    buf->bit_pos += (size_t)count;
    if( (buf->bit_pos + 7) >> 3 > buf->capacity )
    {
        buf->overflow = 1;
        return;
    }

    /* Read-modify-write, most significant bit of `value` first, spilling into
     * as many bytes as the run crosses (rsbuf Packet::pbit). */
    while( count > remaining )
    {
        int mask = (1 << remaining) - 1;
        buf->data[byte_pos] =
            (uint8_t)((buf->data[byte_pos] & ~mask) | ((value >> (count - remaining)) & mask));
        byte_pos++;
        count -= remaining;
        remaining = 8;
    }

    {
        int shift = remaining - count;
        int mask = (1 << count) - 1;
        buf->data[byte_pos] =
            (uint8_t)((buf->data[byte_pos] & ~(mask << shift)) | ((value & mask) << shift));
    }
}

int32_t
rsab_gbit(
    struct RSAreaBuf* buf,
    int count)
{
    size_t byte_pos = buf->bit_pos >> 3;
    int remaining = 8 - (int)(buf->bit_pos & 7);
    int32_t result = 0;

    if( count <= 0 || count > 32 )
    {
        buf->overflow = 1;
        return 0;
    }
    buf->bit_pos += (size_t)count;
    if( (buf->bit_pos + 7) >> 3 > buf->capacity )
    {
        buf->overflow = 1;
        return 0;
    }

    while( count > remaining )
    {
        result |= (int32_t)((buf->data[byte_pos] & ((1 << remaining) - 1)) << (count - remaining));
        byte_pos++;
        count -= remaining;
        remaining = 8;
    }

    if( count == remaining )
        result |= (int32_t)(buf->data[byte_pos] & ((1 << remaining) - 1));
    else
        result |= (int32_t)((buf->data[byte_pos] >> (remaining - count)) & ((1 << count) - 1));
    return result;
}

/* ------------------------------------------------------------------ */
/* Variable-length framing                                             */
/* ------------------------------------------------------------------ */

static size_t
size_begin(
    struct RSAreaBuf* buf,
    int width)
{
    size_t mark = buf->pos;
    rsab_pfill(buf, 0, (size_t)width);
    return mark;
}

/* The slot lives at `mark`; the body is everything after it. */
static uint32_t
size_body(
    struct RSAreaBuf* buf,
    size_t mark,
    int width)
{
    size_t slot_end = mark + (size_t)width;
    if( buf->pos < slot_end || slot_end > buf->capacity )
    {
        buf->overflow = 1;
        return 0;
    }
    return (uint32_t)(buf->pos - slot_end);
}

size_t
rsab_psize1_begin(struct RSAreaBuf* buf)
{
    return size_begin(buf, 1);
}

size_t
rsab_psize2_begin(struct RSAreaBuf* buf)
{
    return size_begin(buf, 2);
}

size_t
rsab_psize4_begin(struct RSAreaBuf* buf)
{
    return size_begin(buf, 4);
}

void
rsab_psize1_end(
    struct RSAreaBuf* buf,
    size_t mark)
{
    uint32_t size = size_body(buf, mark, 1);
    if( buf->overflow )
        return;
    if( size > 0xff )
    {
        buf->overflow = 1;
        return;
    }
    buf->data[mark] = (uint8_t)size;
}

void
rsab_psize2_end(
    struct RSAreaBuf* buf,
    size_t mark)
{
    uint32_t size = size_body(buf, mark, 2);
    if( buf->overflow )
        return;
    if( size > 0xffff )
    {
        buf->overflow = 1;
        return;
    }
    buf->data[mark] = (uint8_t)(size >> 8);
    buf->data[mark + 1] = (uint8_t)size;
}

void
rsab_psize4_end(
    struct RSAreaBuf* buf,
    size_t mark)
{
    uint32_t size = size_body(buf, mark, 4);
    if( buf->overflow )
        return;
    buf->data[mark] = (uint8_t)(size >> 24);
    buf->data[mark + 1] = (uint8_t)(size >> 16);
    buf->data[mark + 2] = (uint8_t)(size >> 8);
    buf->data[mark + 3] = (uint8_t)size;
}

void
rsab_psize1(
    struct RSAreaBuf* buf,
    uint32_t size)
{
    if( size + 1 > buf->pos || buf->pos > buf->capacity || size > 0xff )
    {
        buf->overflow = 1;
        return;
    }
    buf->data[buf->pos - size - 1] = (uint8_t)size;
}

void
rsab_psize2(
    struct RSAreaBuf* buf,
    uint32_t size)
{
    size_t at;
    if( size + 2 > buf->pos || size > 0xffff )
    {
        buf->overflow = 1;
        return;
    }
    at = buf->pos - size - 2;
    buf->data[at] = (uint8_t)(size >> 8);
    buf->data[at + 1] = (uint8_t)size;
}

void
rsab_psize4(
    struct RSAreaBuf* buf,
    uint32_t size)
{
    size_t at;
    if( size + 4 > buf->pos )
    {
        buf->overflow = 1;
        return;
    }
    at = buf->pos - size - 4;
    buf->data[at] = (uint8_t)(size >> 24);
    buf->data[at + 1] = (uint8_t)(size >> 16);
    buf->data[at + 2] = (uint8_t)(size >> 8);
    buf->data[at + 3] = (uint8_t)size;
}
