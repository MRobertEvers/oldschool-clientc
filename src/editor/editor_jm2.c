#include "editor_jm2.h"

#include "editor_doc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* ---- emit helpers -------------------------------------------------------- *
 *
 * snprintf's contract, carried across a sequence of appends: `written` counts
 * what the full text WOULD be, and bytes land only while they fit. A caller
 * probes with (NULL, 0), allocates, and emits again — so the emitters below
 * never need to know how big the answer is before they start.
 */

struct emit_cursor
{
    char* out;
    size_t capacity;
    size_t written;
};

static void
emit_char(
    struct emit_cursor* cursor,
    char ch)
{
    assert(cursor);

    if( cursor->out && cursor->written + 1 < cursor->capacity )
        cursor->out[cursor->written] = ch;
    cursor->written++;
}

static void
emit_str(
    struct emit_cursor* cursor,
    const char* text)
{
    assert(cursor);
    assert(text);

    for( const char* p = text; *p; p++ )
        emit_char(cursor, *p);
}

static void
emit_int(
    struct emit_cursor* cursor,
    int value)
{
    char digits[16];
    int length = 0;
    unsigned magnitude;

    assert(cursor);

    if( value < 0 )
    {
        emit_char(cursor, '-');
        magnitude = (unsigned)(-(long)value);
    }
    else
    {
        magnitude = (unsigned)value;
    }

    do
    {
        digits[length++] = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    } while( magnitude );

    while( length > 0 )
        emit_char(cursor, digits[--length]);
}

/** NUL-terminate whatever fit. Safe when nothing did: capacity 0 writes nothing. */
static size_t
emit_finish(struct emit_cursor* cursor)
{
    assert(cursor);

    if( cursor->out && cursor->capacity > 0 )
    {
        size_t at = cursor->written < cursor->capacity - 1 ? cursor->written : cursor->capacity - 1;
        cursor->out[at] = '\0';
    }
    return cursor->written;
}

/* ---- parse helpers ------------------------------------------------------- */

struct parse_cursor
{
    const char* p;
    const char* end;
};

static int
parse_at_end(const struct parse_cursor* cursor)
{
    assert(cursor);
    return cursor->p >= cursor->end;
}

static void
parse_skip_spaces(struct parse_cursor* cursor)
{
    assert(cursor);
    while( !parse_at_end(cursor) && (*cursor->p == ' ' || *cursor->p == '\t') )
        cursor->p++;
}

/** Read a non-negative decimal. Returns 0 when no digit was there. */
static int
parse_uint(
    struct parse_cursor* cursor,
    int* out_value)
{
    long value = 0;
    int digits = 0;

    assert(cursor);
    assert(out_value);

    while( !parse_at_end(cursor) && *cursor->p >= '0' && *cursor->p <= '9' )
    {
        value = value * 10 + (*cursor->p - '0');
        if( value > 0x7FFFFFF )
            return 0;
        cursor->p++;
        digits++;
    }
    if( digits == 0 )
        return 0;
    *out_value = (int)value;
    return 1;
}

/** The line the cursor sits in, as [start, line_end), advancing past its \n. */
static void
parse_take_line(
    struct parse_cursor* cursor,
    const char** out_start,
    const char** out_end)
{
    const char* start;
    const char* scan;

    assert(cursor);
    assert(out_start);
    assert(out_end);

    start = cursor->p;
    scan = start;
    while( scan < cursor->end && *scan != '\n' )
        scan++;

    *out_start = start;
    /* A \r\n file must not leave the \r inside the line. */
    *out_end = (scan > start && scan[-1] == '\r') ? scan - 1 : scan;

    cursor->p = (scan < cursor->end) ? scan + 1 : cursor->end;
}

static int
line_is_banner(
    const char* start,
    const char* end)
{
    assert(start);
    assert(end);
    return end > start && start[0] == '=';
}

/** cachepack's rule: MAP and LOC banners are the codec's, everything else is
 *  the server's. Spelled the same way so the two tools agree about a file. */
static int
banner_is_ours(
    const char* start,
    const char* end)
{
    size_t length;

    assert(start);
    assert(end);
    assert(end >= start);

    length = (size_t)(end - start);
    for( size_t i = 0; i + 3 <= length; i++ )
    {
        if( memcmp(start + i, "MAP", 3) == 0 )
            return 1;
        if( memcmp(start + i, "LOC", 3) == 0 )
            return 1;
    }
    return 0;
}

static int
hex_nibble(char ch)
{
    if( ch >= '0' && ch <= '9' )
        return ch - '0';
    if( ch >= 'a' && ch <= 'f' )
        return 10 + (ch - 'a');
    if( ch >= 'A' && ch <= 'F' )
        return 10 + (ch - 'A');
    return -1;
}

static struct Editor_ParseResult
parse_ok(void)
{
    struct Editor_ParseResult result = { EDITOR_PARSE_OK, 0 };
    return result;
}

static struct Editor_ParseResult
parse_fail(
    enum Editor_ParseStatus status,
    int line)
{
    struct Editor_ParseResult result;
    result.status = status;
    result.line = line;
    return result;
}

/* ---- .jm2 ---------------------------------------------------------------- */

/**
 * Read `<level> <x> <z>:` off the front of a line.
 *
 * Returns 0 for a shape failure (no coords / no colon) and writes
 * EDITOR_PARSE_BAD_COORD into `out_status` when the numbers parsed but named a
 * tile outside the square — the caller reports one or the other.
 */
static int
parse_tile_key(
    struct parse_cursor* cursor,
    int* out_level,
    int* out_x,
    int* out_z,
    enum Editor_ParseStatus* out_status)
{
    assert(cursor);
    assert(out_level);
    assert(out_x);
    assert(out_z);
    assert(out_status);

    *out_status = EDITOR_PARSE_BAD_LINE;

    parse_skip_spaces(cursor);
    if( !parse_uint(cursor, out_level) )
        return 0;
    parse_skip_spaces(cursor);
    if( !parse_uint(cursor, out_x) )
        return 0;
    parse_skip_spaces(cursor);
    if( !parse_uint(cursor, out_z) )
        return 0;
    parse_skip_spaces(cursor);
    if( parse_at_end(cursor) || *cursor->p != ':' )
        return 0;
    cursor->p++;

    if( *out_level < 0 || *out_level >= EDITOR_SQUARE_LEVELS || *out_x < 0 ||
        *out_x >= EDITOR_SQUARE_X || *out_z < 0 || *out_z >= EDITOR_SQUARE_Z )
    {
        *out_status = EDITOR_PARSE_BAD_COORD;
        return 0;
    }
    return 1;
}

/** One `h` / `o` / `f` / `u` token into the tile. */
static int
parse_tile_token(
    struct parse_cursor* cursor,
    struct Editor_Tile* tile)
{
    char kind;
    int value = 0;

    assert(cursor);
    assert(tile);

    kind = *cursor->p;
    cursor->p++;
    if( !parse_uint(cursor, &value) )
        return 0;

    switch( kind )
    {
    case 'h':
        if( value > 0xFF )
            return 0;
        tile->has_height = 1;
        tile->height = (uint8_t)value;
        return 1;

    case 'o':
    {
        int shape = 0;
        int rotation = 0;
        if( value > 0xFFFF )
            return 0;
        /* `o<id>;<shape>;<rotation>` — the two sub-fields are written whenever
         * the token is, because both are recoverable from the attribute opcode
         * and dropping either would make the tile unencodable. */
        if( !parse_at_end(cursor) && *cursor->p == ';' )
        {
            cursor->p++;
            if( !parse_uint(cursor, &shape) )
                return 0;
            if( !parse_at_end(cursor) && *cursor->p == ';' )
            {
                cursor->p++;
                if( !parse_uint(cursor, &rotation) )
                    return 0;
            }
        }
        /* The attribute opcode is shape*4 + rotation + 2 and must stay inside
         * the overlay band (2..49), so shape tops out at 11 and rotation at 3. */
        if( shape > 11 || rotation > 3 )
            return 0;
        tile->has_overlay = 1;
        tile->overlay_id = (uint16_t)value;
        tile->shape = (uint8_t)shape;
        tile->rotation = (uint8_t)rotation;
        return 1;
    }

    case 'f':
        /* Settings ride opcodes 50..81, so the field holds 1..32. */
        if( value < 1 || value > 32 )
            return 0;
        tile->settings = (uint8_t)value;
        return 1;

    case 'u':
        /* Underlay is `attribute - 81`, and the attribute is read at the era's
         * width — u8 before OldSchool 209, u16 after. So the opcode range does
         * not bound this; the stored field does, and it is a byte. */
        if( value < 1 || value > 0xFF )
            return 0;
        tile->underlay_id = (uint8_t)value;
        return 1;

    default:
        return 0;
    }
}

struct Editor_ParseResult
Editor_Jm2Parse(
    struct Editor_Square* square,
    const char* text,
    size_t length)
{
    struct parse_cursor cursor;
    int line_number = 0;
    int seen_header = 0;
    int keeping_foreign = 0;
    size_t foreign_length = 0;
    size_t foreign_capacity = 0;
    char* foreign = NULL;

    assert(square);
    assert(text || length == 0);

    memset(square->tiles, 0, sizeof(square->tiles));
    free(square->trailing);
    square->trailing = NULL;
    square->trailing_size = 0;
    free(square->foreign);
    square->foreign = NULL;

    cursor.p = text;
    cursor.end = text + length;

    while( !parse_at_end(&cursor) )
    {
        const char* start;
        const char* end;
        struct parse_cursor line;
        int level = 0;
        int x = 0;
        int z = 0;
        enum Editor_ParseStatus key_status;

        parse_take_line(&cursor, &start, &end);
        line_number++;

        if( line_is_banner(start, end) )
        {
            keeping_foreign = !banner_is_ours(start, end);
            if( !keeping_foreign )
                seen_header = 1;
        }

        if( keeping_foreign )
        {
            /* Verbatim, newline included — this text is the server's and the
             * editor is only its custodian. */
            size_t raw_length = (size_t)(end - start) + 1;
            if( foreign_length + raw_length + 1 > foreign_capacity )
            {
                size_t next = foreign_capacity ? foreign_capacity * 2 : 4096;
                while( next < foreign_length + raw_length + 1 )
                    next *= 2;
                foreign = realloc(foreign, next);
                assert(foreign);
                foreign_capacity = next;
            }
            memcpy(foreign + foreign_length, start, raw_length - 1);
            foreign_length += raw_length - 1;
            foreign[foreign_length++] = '\n';
            foreign[foreign_length] = '\0';
            continue;
        }

        if( start == end )
            continue;
        if( line_is_banner(start, end) )
            continue;

        line.p = start;
        line.end = end;

        /* `trailing=<hex>` — bytes past the last tile, carried so a re-encode
         * is the same length as the file it came from. */
        if( (size_t)(end - start) >= 9 && memcmp(start, "trailing=", 9) == 0 )
        {
            const char* hex = start + 9;
            size_t hex_length = (size_t)(end - hex);
            if( hex_length % 2 != 0 )
            {
                free(foreign);
                return parse_fail(EDITOR_PARSE_BAD_TOKEN, line_number);
            }
            square->trailing_size = (int)(hex_length / 2);
            if( square->trailing_size > 0 )
            {
                square->trailing = malloc((size_t)square->trailing_size);
                assert(square->trailing);
                for( int i = 0; i < square->trailing_size; i++ )
                {
                    int high = hex_nibble(hex[i * 2]);
                    int low = hex_nibble(hex[i * 2 + 1]);
                    if( high < 0 || low < 0 )
                    {
                        free(foreign);
                        return parse_fail(EDITOR_PARSE_BAD_TOKEN, line_number);
                    }
                    square->trailing[i] = (uint8_t)((high << 4) | low);
                }
            }
            continue;
        }

        if( !parse_tile_key(&line, &level, &x, &z, &key_status) )
        {
            free(foreign);
            return parse_fail(key_status, line_number);
        }

        {
            struct Editor_Tile* tile = &square->tiles[Editor_TileIndex(x, z, level)];
            for( ;; )
            {
                parse_skip_spaces(&line);
                if( parse_at_end(&line) )
                    break;
                if( !parse_tile_token(&line, tile) )
                {
                    free(foreign);
                    return parse_fail(EDITOR_PARSE_BAD_TOKEN, line_number);
                }
            }
        }
    }

    if( !seen_header )
    {
        free(foreign);
        return parse_fail(EDITOR_PARSE_BAD_HEADER, 0);
    }

    square->foreign = foreign;
    square->loaded = 1;
    return parse_ok();
}

size_t
Editor_Jm2Emit(
    const struct Editor_Square* square,
    char* out,
    size_t out_capacity)
{
    struct emit_cursor cursor;

    assert(square);
    assert(out || out_capacity == 0);

    cursor.out = out;
    cursor.capacity = out_capacity;
    cursor.written = 0;

    emit_str(&cursor, "==== MAP ====\n");

    if( square->trailing_size > 0 )
    {
        static const char hex[] = "0123456789abcdef";
        assert(square->trailing);
        emit_str(&cursor, "trailing=");
        for( int i = 0; i < square->trailing_size; i++ )
        {
            emit_char(&cursor, hex[(square->trailing[i] >> 4) & 0xF]);
            emit_char(&cursor, hex[square->trailing[i] & 0xF]);
        }
        emit_char(&cursor, '\n');
    }

    for( int level = 0; level < EDITOR_SQUARE_LEVELS; level++ )
    {
        for( int x = 0; x < EDITOR_SQUARE_X; x++ )
        {
            for( int z = 0; z < EDITOR_SQUARE_Z; z++ )
            {
                const struct Editor_Tile* tile = &square->tiles[Editor_TileIndex(x, z, level)];
                int wrote_token = 0;

                if( !tile->has_height && !tile->has_overlay && !tile->settings &&
                    !tile->underlay_id )
                    continue;

                emit_int(&cursor, level);
                emit_char(&cursor, ' ');
                emit_int(&cursor, x);
                emit_char(&cursor, ' ');
                emit_int(&cursor, z);
                emit_str(&cursor, ": ");

                /* Token order is h, o, f, u — the order cachepack writes, which
                 * is what keeps an untouched square byte-identical through a
                 * load/save cycle. The decoder accepts any order. */
                if( tile->has_height )
                {
                    emit_char(&cursor, 'h');
                    emit_int(&cursor, tile->height);
                    wrote_token = 1;
                }
                if( tile->has_overlay )
                {
                    if( wrote_token )
                        emit_char(&cursor, ' ');
                    emit_char(&cursor, 'o');
                    emit_int(&cursor, tile->overlay_id);
                    emit_char(&cursor, ';');
                    emit_int(&cursor, tile->shape);
                    emit_char(&cursor, ';');
                    emit_int(&cursor, tile->rotation);
                    wrote_token = 1;
                }
                if( tile->settings )
                {
                    if( wrote_token )
                        emit_char(&cursor, ' ');
                    emit_char(&cursor, 'f');
                    emit_int(&cursor, tile->settings);
                    wrote_token = 1;
                }
                if( tile->underlay_id )
                {
                    if( wrote_token )
                        emit_char(&cursor, ' ');
                    emit_char(&cursor, 'u');
                    emit_int(&cursor, tile->underlay_id);
                }
                emit_char(&cursor, '\n');
            }
        }
    }

    if( square->foreign && square->foreign[0] )
    {
        emit_char(&cursor, '\n');
        emit_str(&cursor, square->foreign);
    }

    return emit_finish(&cursor);
}

/* ---- .jl2 ---------------------------------------------------------------- */

struct Editor_ParseResult
Editor_Jl2Parse(
    struct Editor_Square* square,
    const char* text,
    size_t length)
{
    struct parse_cursor cursor;
    int line_number = 0;
    int seen_header = 0;

    assert(square);
    assert(text || length == 0);

    square->loc_count = 0;

    cursor.p = text;
    cursor.end = text + length;

    while( !parse_at_end(&cursor) )
    {
        const char* start;
        const char* end;
        struct parse_cursor line;
        struct Editor_Loc loc;
        int level = 0;
        int x = 0;
        int z = 0;
        enum Editor_ParseStatus key_status;

        parse_take_line(&cursor, &start, &end);
        line_number++;

        if( line_is_banner(start, end) )
        {
            seen_header = 1;
            continue;
        }
        if( start == end )
            continue;

        line.p = start;
        line.end = end;

        if( !parse_tile_key(&line, &level, &x, &z, &key_status) )
            return parse_fail(key_status, line_number);

        memset(&loc, 0, sizeof(loc));
        loc.level = level;
        loc.x = x;
        loc.z = z;

        parse_skip_spaces(&line);
        if( !parse_uint(&line, &loc.loc_id) )
            return parse_fail(EDITOR_PARSE_BAD_LINE, line_number);
        parse_skip_spaces(&line);
        if( !parse_uint(&line, &loc.shape) )
            return parse_fail(EDITOR_PARSE_BAD_LINE, line_number);

        /* Rotation is omitted when 0 — the common case, and the reason the
         * emitter has two forms. */
        parse_skip_spaces(&line);
        if( !parse_at_end(&line) && !parse_uint(&line, &loc.rotation) )
            return parse_fail(EDITOR_PARSE_BAD_LINE, line_number);

        if( loc.shape > 22 || loc.rotation > 3 )
            return parse_fail(EDITOR_PARSE_BAD_TOKEN, line_number);

        Editor_SquareLocAdd(square, &loc);
    }

    if( !seen_header )
        return parse_fail(EDITOR_PARSE_BAD_HEADER, 0);

    return parse_ok();
}

size_t
Editor_Jl2Emit(
    const struct Editor_Square* square,
    char* out,
    size_t out_capacity)
{
    struct emit_cursor cursor;

    assert(square);
    assert(out || out_capacity == 0);

    cursor.out = out;
    cursor.capacity = out_capacity;
    cursor.written = 0;

    emit_str(&cursor, "==== LOC ====\n");

    for( int i = 0; i < square->loc_count; i++ )
    {
        const struct Editor_Loc* loc = &square->locs[i];

        emit_int(&cursor, loc->level);
        emit_char(&cursor, ' ');
        emit_int(&cursor, loc->x);
        emit_char(&cursor, ' ');
        emit_int(&cursor, loc->z);
        emit_str(&cursor, ": ");
        emit_int(&cursor, loc->loc_id);
        emit_char(&cursor, ' ');
        emit_int(&cursor, loc->shape);
        if( loc->rotation != 0 )
        {
            emit_char(&cursor, ' ');
            emit_int(&cursor, loc->rotation);
        }
        emit_char(&cursor, '\n');
    }

    return emit_finish(&cursor);
}
