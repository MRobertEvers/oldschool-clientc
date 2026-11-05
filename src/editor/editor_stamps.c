/*
 * The STAMPS documents (editor_stamps.h): the line-preserving codec, the
 * stamp in memory and its quarter turn, and the splices that write one.
 */

#include "editor_stamps.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char k_stamp_comments[] = "# ToriRSMapEd stamp (editor/stamps/<name>.stamp).\n"
                                       "# stamp <name> [| note <text>]\n"
                                       "# loc <dx> <dz> <dlevel> <loc> <shape> <rot> [<w>x<h>]\n"
                                       "# tile <dx> <dz> <dlevel> [h<raw>] [u<id>] [o<id>/<shape>/<rot>] [f<bits>]\n"
                                       "# spawn <npc|obj> <name> <dx> <dz> <dlevel> [<count>]\n";

static const char k_index_comments[] = "# ToriRSMapEd stamps (editor/stamps/index.txt): one line per stamp.\n"
                                       "# stamp <name> [| note <text>]\n";

/* ---- names ------------------------------------------------------------------ */

int
Editor_StampNameValid(const char* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= EDITOR_STAMP_NAME_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') )
            return 0;
    }
    return 1;
}

void
Editor_StampDocKey(
    char out[EDITOR_DOC_KEY_MAX],
    const char* name)
{
    assert(out);
    assert(name);
    assert(Editor_StampNameValid(name));
    snprintf(out, EDITOR_DOC_KEY_MAX, "%s%s", EDITOR_STAMP_KEY_PREFIX, name);
}

/* ---- one line ------------------------------------------------------------------ */

/** The next whitespace-separated word of [*at, end) into `out` (NUL-terminated,
 *  at most `size` - 1 bytes). 0 at the end, or when the word is too long. */
static int
next_word(
    const char** at,
    const char* end,
    char* out,
    size_t size)
{
    const char* p = *at;
    size_t n = 0;

    while( p < end && (*p == ' ' || *p == '\t') )
        p++;
    if( p >= end )
        return 0;
    while( p < end && *p != ' ' && *p != '\t' )
    {
        if( n + 1 >= size )
            return 0;
        out[n++] = *p++;
    }
    out[n] = '\0';
    *at = p;
    return 1;
}

/** A whole decimal integer, optionally signed, within [lo, hi]. */
static int
parse_int(
    const char* word,
    int lo,
    int hi,
    int* out)
{
    long value = 0;
    int negative = 0;
    const char* p = word;

    if( *p == '-' )
    {
        negative = 1;
        p++;
    }
    if( *p == '\0' )
        return 0;
    for( ; *p; p++ )
    {
        if( *p < '0' || *p > '9' )
            return 0;
        value = value * 10 + (*p - '0');
        if( value > 1000000 )
            return 0;
    }
    if( negative )
        value = -value;
    if( value < lo || value > hi )
        return 0;
    *out = (int)value;
    return 1;
}

/** The three offsets every piece starts with. */
static int
parse_offsets(
    const char** at,
    const char* end,
    struct Editor_StampPiece* piece)
{
    char word[16];
    return next_word(at, end, word, sizeof(word))
           && parse_int(word, -EDITOR_STAMP_REACH, EDITOR_STAMP_REACH, &piece->dx)
           && next_word(at, end, word, sizeof(word))
           && parse_int(word, -EDITOR_STAMP_REACH, EDITOR_STAMP_REACH, &piece->dz)
           && next_word(at, end, word, sizeof(word)) && parse_int(word, -3, 3, &piece->dlevel);
}

/** One tile token: h<raw>, u<id>, o<id>/<shape>/<rot>, f<bits>. Each once. */
static int
parse_tile_token(
    const char* word,
    struct Editor_Tile* tile,
    unsigned* seen)
{
    int value;
    unsigned const bit = word[0] == 'h' ? 1u : word[0] == 'u' ? 2u : word[0] == 'o' ? 4u : word[0] == 'f' ? 8u : 0u;

    if( bit == 0 || (*seen & bit) )
        return 0;
    *seen |= bit;
    if( word[0] == 'o' )
    {
        char copy[32];
        char* first;
        char* second;
        int id;
        int shape;
        int rotation;
        if( strlen(word + 1) >= sizeof(copy) )
            return 0;
        snprintf(copy, sizeof(copy), "%s", word + 1);
        first = strchr(copy, '/');
        if( !first )
            return 0;
        *first++ = '\0';
        second = strchr(first, '/');
        if( !second )
            return 0;
        *second++ = '\0';
        if( !parse_int(copy, 0, 65535, &id) || !parse_int(first, 0, 255, &shape) || !parse_int(second, 0, 3, &rotation) )
            return 0;
        tile->has_overlay = 1;
        tile->overlay_id = (uint16_t)id;
        tile->shape = (uint8_t)shape;
        tile->rotation = (uint8_t)rotation;
        return 1;
    }
    if( !parse_int(word + 1, 0, 255, &value) )
        return 0;
    if( word[0] == 'h' )
    {
        /* The file spells an explicit zero as h1; a raw 0 cannot be authored. */
        if( value == 0 )
            return 0;
        tile->has_height = 1;
        tile->height = (uint8_t)value;
    }
    else if( word[0] == 'u' )
    {
        if( value == 0 )
            return 0;
        tile->underlay_id = (uint8_t)value;
    }
    else
    {
        if( value == 0 )
            return 0;
        tile->settings = (uint8_t)value;
    }
    return 1;
}

/** [begin, end) without a trailing CR or spaces. */
static void
trim_end(
    const char* begin,
    const char** end)
{
    while( *end > begin && ((*end)[-1] == '\r' || (*end)[-1] == ' ' || (*end)[-1] == '\t') )
        (*end)--;
}

static int
parse_header(
    const char* at,
    const char* end,
    struct Editor_StampsLine* out)
{
    char name[EDITOR_STAMP_NAME_MAX];
    const char* bar = memchr(at, '|', (size_t)(end - at));
    const char* name_end = bar ? bar : end;

    while( name_end > at && name_end[-1] == ' ' )
        name_end--;
    while( at < name_end && *at == ' ' )
        at++;
    if( name_end <= at || (size_t)(name_end - at) >= sizeof(name) )
        return 0;
    memcpy(name, at, (size_t)(name_end - at));
    name[name_end - at] = '\0';
    if( !Editor_StampNameValid(name) )
        return 0;
    snprintf(out->name, sizeof(out->name), "%s", name);
    out->note[0] = '\0';
    if( bar )
    {
        const char* note = bar + 1;
        while( note < end && *note == ' ' )
            note++;
        if( (size_t)(end - note) < 5 || memcmp(note, "note ", 5) != 0 )
            return 0;
        note += 5;
        if( (size_t)(end - note) >= sizeof(out->note) || memchr(note, '|', (size_t)(end - note)) )
            return 0;
        memcpy(out->note, note, (size_t)(end - note));
        out->note[end - note] = '\0';
    }
    return 1;
}

int
Editor_StampsParseLine(
    const char* text,
    struct Editor_StampsLine* out_line)
{
    struct Editor_StampsLine scratch;
    struct Editor_StampsLine* out = out_line ? out_line : &scratch;
    const char* at = text;
    const char* end;
    char word[EDITOR_CMD_NAME_MAX];
    struct Editor_StampPiece* piece = &out->piece;

    assert(text);
    end = text + strlen(text);
    trim_end(text, &end);
    while( at < end && (*at == ' ' || *at == '\t') )
        at++;
    if( at == end || *at == '#' || (end - at >= 2 && at[0] == '/' && at[1] == '/') )
        return EDITOR_STAMPS_LINE_TEXT;
    if( !next_word(&at, end, word, sizeof(word)) )
        return -1;
    memset(piece, 0, sizeof(*piece));
    if( strcmp(word, "stamp") == 0 )
        return parse_header(at, end, out) ? EDITOR_STAMPS_LINE_HEADER : -1;
    if( strcmp(word, "loc") == 0 )
    {
        piece->kind = EDITOR_STAMP_LOC;
        piece->size_x = 1;
        piece->size_z = 1;
        if( !parse_offsets(&at, end, piece) || !next_word(&at, end, word, sizeof(word))
            || !parse_int(word, 0, 1 << 20, &piece->loc_id) || !next_word(&at, end, word, sizeof(word))
            || !parse_int(word, 0, 22, &piece->shape) || !next_word(&at, end, word, sizeof(word))
            || !parse_int(word, 0, 3, &piece->rotation) )
            return -1;
        if( next_word(&at, end, word, sizeof(word)) )
        {
            char* x = strchr(word, 'x');
            if( !x )
                return -1;
            *x = '\0';
            if( !parse_int(word, 1, 16, &piece->size_x) || !parse_int(x + 1, 1, 16, &piece->size_z) )
                return -1;
            if( next_word(&at, end, word, sizeof(word)) )
                return -1;
        }
        return EDITOR_STAMPS_LINE_PIECE;
    }
    if( strcmp(word, "tile") == 0 )
    {
        unsigned seen = 0;
        piece->kind = EDITOR_STAMP_TILE;
        if( !parse_offsets(&at, end, piece) )
            return -1;
        while( next_word(&at, end, word, sizeof(word)) )
            if( !parse_tile_token(word, &piece->tile, &seen) )
                return -1;
        /* A token too long for the word buffer stops next_word early. */
        while( at < end && (*at == ' ' || *at == '\t') )
            at++;
        return at == end ? EDITOR_STAMPS_LINE_PIECE : -1;
    }
    if( strcmp(word, "spawn") == 0 )
    {
        piece->kind = EDITOR_STAMP_SPAWN;
        piece->count = 1;
        if( !next_word(&at, end, word, sizeof(word)) )
            return -1;
        if( strcmp(word, "npc") == 0 )
            piece->is_obj = 0;
        else if( strcmp(word, "obj") == 0 )
            piece->is_obj = 1;
        else
            return -1;
        if( !next_word(&at, end, piece->name, sizeof(piece->name)) || !parse_offsets(&at, end, piece) )
            return -1;
        if( next_word(&at, end, word, sizeof(word)) )
        {
            if( !parse_int(word, 1, 1 << 30, &piece->count) || next_word(&at, end, word, sizeof(word)) )
                return -1;
        }
        return EDITOR_STAMPS_LINE_PIECE;
    }
    return -1;
}

int
Editor_StampFormatPiece(
    const struct Editor_StampPiece* piece,
    char* out,
    size_t out_capacity)
{
    int written = 0;

    assert(piece);
    assert(out || out_capacity == 0);
    if( piece->kind == EDITOR_STAMP_LOC )
    {
        if( piece->size_x == 1 && piece->size_z == 1 )
            written = snprintf(out, out_capacity, "loc %d %d %d %d %d %d", piece->dx, piece->dz, piece->dlevel,
                piece->loc_id, piece->shape, piece->rotation);
        else
            written = snprintf(out, out_capacity, "loc %d %d %d %d %d %d %dx%d", piece->dx, piece->dz,
                piece->dlevel, piece->loc_id, piece->shape, piece->rotation, piece->size_x, piece->size_z);
    }
    else if( piece->kind == EDITOR_STAMP_TILE )
    {
        const struct Editor_Tile* t = &piece->tile;
        char tokens[96];
        int n = 0;
        tokens[0] = '\0';
        if( t->has_height )
            n += snprintf(tokens + n, sizeof(tokens) - (size_t)n, " h%d", t->height);
        if( t->underlay_id )
            n += snprintf(tokens + n, sizeof(tokens) - (size_t)n, " u%d", t->underlay_id);
        if( t->has_overlay )
            n += snprintf(tokens + n, sizeof(tokens) - (size_t)n, " o%d/%d/%d", t->overlay_id, t->shape, t->rotation);
        if( t->settings )
            n += snprintf(tokens + n, sizeof(tokens) - (size_t)n, " f%d", t->settings);
        written = snprintf(out, out_capacity, "tile %d %d %d%s", piece->dx, piece->dz, piece->dlevel, tokens);
    }
    else
    {
        assert(piece->kind == EDITOR_STAMP_SPAWN);
        if( piece->count > 1 )
            written = snprintf(out, out_capacity, "spawn %s %s %d %d %d %d", piece->is_obj ? "obj" : "npc", piece->name,
                piece->dx, piece->dz, piece->dlevel, piece->count);
        else
            written = snprintf(out, out_capacity, "spawn %s %s %d %d %d", piece->is_obj ? "obj" : "npc", piece->name,
                piece->dx, piece->dz, piece->dlevel);
    }
    return written;
}

int
Editor_StampFormatHeader(
    const char* name,
    const char* note,
    char* out,
    size_t out_capacity)
{
    assert(name);
    assert(out || out_capacity == 0);
    if( note && note[0] )
        return snprintf(out, out_capacity, "stamp %s | note %s", name, note);
    return snprintf(out, out_capacity, "stamp %s", name);
}

/* ---- the file ------------------------------------------------------------------ */

void
Editor_StampsInit(struct Editor_StampsFile* file)
{
    assert(file);
    memset(file, 0, sizeof(*file));
}

void
Editor_StampsFree(struct Editor_StampsFile* file)
{
    if( !file )
        return;
    for( int i = 0; i < file->line_count; i++ )
        ToriMem_Free(ToriMem_Editor(), file->lines[i].text);
    ToriMem_Free(ToriMem_Editor(), file->lines);
    memset(file, 0, sizeof(*file));
}

static struct Editor_StampsLine*
stamps_append(struct Editor_StampsFile* file)
{
    if( file->line_count == file->line_capacity )
    {
        int const capacity = file->line_capacity ? file->line_capacity * 2 : 16;
        file->lines = ToriMem_Realloc(ToriMem_Editor(), file->lines, sizeof(file->lines[0]) * (size_t)capacity);
        assert(file->lines);
        file->line_capacity = capacity;
    }
    memset(&file->lines[file->line_count], 0, sizeof(file->lines[0]));
    return &file->lines[file->line_count++];
}

struct Editor_StampsParseResult
Editor_StampsParse(
    struct Editor_StampsFile* file,
    const char* text,
    size_t length)
{
    struct Editor_StampsParseResult result = { 1, 0 };
    size_t at = 0;
    int line_number = 0;
    int headers = 0;

    assert(file);
    assert(text || length == 0);

    Editor_StampsFree(file);
    while( at < length )
    {
        const char* begin = text + at;
        const char* newline = memchr(begin, '\n', length - at);
        size_t const line_length = newline ? (size_t)(newline - begin) : length - at;
        struct Editor_StampsLine* line = stamps_append(file);
        int kind;

        line_number++;
        line->text = ToriMem_Alloc(ToriMem_Editor(), line_length + 1);
        assert(line->text);
        memcpy(line->text, begin, line_length);
        line->text[line_length] = '\0';
        kind = Editor_StampsParseLine(line->text, line);
        /* A stamp file names itself once; the index names each stamp once,
         * and holds nothing else. A piece outside a stamp belongs to none. */
        /* (This line's own kind is still TEXT: only earlier headers match.) */
        if( kind == EDITOR_STAMPS_LINE_HEADER && Editor_StampsFindHeader(file, line->name) >= 0 )
            kind = -1;
        if( kind == EDITOR_STAMPS_LINE_PIECE && headers == 0 )
            kind = -1;
        if( kind == EDITOR_STAMPS_LINE_HEADER )
            headers++;
        if( kind < 0 )
        {
            line->kind = EDITOR_STAMPS_LINE_TEXT;
            if( result.ok )
            {
                result.ok = 0;
                result.line = line_number;
            }
        }
        else
            line->kind = (enum Editor_StampsLineKind)kind;
        at += line_length;
        if( newline )
        {
            at++;
            file->final_newline = at == length;
        }
        else
            file->final_newline = 0;
    }
    return result;
}

size_t
Editor_StampsEmit(
    const struct Editor_StampsFile* file,
    char* out,
    size_t out_capacity)
{
    size_t total = 0;

    assert(file);
    assert(out || out_capacity == 0);
    for( int i = 0; i < file->line_count; i++ )
    {
        const char* text = file->lines[i].text;
        size_t const length = strlen(text);
        int const newline = i + 1 < file->line_count || file->final_newline;
        if( out && total < out_capacity )
        {
            size_t const room = out_capacity - total - 1;
            memcpy(out + total, text, length < room ? length : room);
            if( newline && length < room )
                out[total + length] = '\n';
        }
        total += length + (size_t)newline;
    }
    if( out && out_capacity > 0 )
        out[total < out_capacity ? total : out_capacity - 1] = '\0';
    return total;
}

int
Editor_StampsFindHeader(
    const struct Editor_StampsFile* file,
    const char* name)
{
    assert(file);
    assert(name);
    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].kind == EDITOR_STAMPS_LINE_HEADER && strcmp(file->lines[i].name, name) == 0 )
            return i;
    return -1;
}

int
Editor_StampsHeaderCount(const struct Editor_StampsFile* file)
{
    int count = 0;

    assert(file);
    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].kind == EDITOR_STAMPS_LINE_HEADER )
            count++;
    return count;
}

const char*
Editor_StampsHeaderName(
    const struct Editor_StampsFile* file,
    int nth)
{
    assert(file);
    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].kind == EDITOR_STAMPS_LINE_HEADER && nth-- == 0 )
            return file->lines[i].name;
    return NULL;
}

/* ---- the stamp ----------------------------------------------------------------- */

void
Editor_StampInit(struct Editor_Stamp* stamp)
{
    assert(stamp);
    memset(stamp, 0, sizeof(*stamp));
}

void
Editor_StampFree(struct Editor_Stamp* stamp)
{
    if( !stamp )
        return;
    ToriMem_Free(ToriMem_Editor(), stamp->pieces);
    memset(stamp, 0, sizeof(*stamp));
}

void
Editor_StampClear(struct Editor_Stamp* stamp)
{
    assert(stamp);
    stamp->name[0] = '\0';
    stamp->note[0] = '\0';
    stamp->count = 0;
    stamp->turned = 0;
}

int
Editor_StampAdd(
    struct Editor_Stamp* stamp,
    const struct Editor_StampPiece* piece)
{
    assert(stamp);
    assert(piece);
    if( stamp->count >= EDITOR_STAMP_PIECES_MAX )
        return 0;
    if( piece->dx < -EDITOR_STAMP_REACH || piece->dx > EDITOR_STAMP_REACH || piece->dz < -EDITOR_STAMP_REACH
        || piece->dz > EDITOR_STAMP_REACH )
        return 0;
    if( stamp->count == stamp->capacity )
    {
        int capacity = stamp->capacity ? stamp->capacity * 2 : 64;
        if( capacity > EDITOR_STAMP_PIECES_MAX )
            capacity = EDITOR_STAMP_PIECES_MAX;
        stamp->pieces = ToriMem_Realloc(ToriMem_Editor(), stamp->pieces, sizeof(stamp->pieces[0]) * (size_t)capacity);
        assert(stamp->pieces);
        stamp->capacity = capacity;
    }
    stamp->pieces[stamp->count++] = *piece;
    return 1;
}

void
Editor_StampCopy(
    struct Editor_Stamp* dst,
    const struct Editor_Stamp* src)
{
    assert(dst);
    assert(src);
    assert(dst != src);
    Editor_StampClear(dst);
    snprintf(dst->name, sizeof(dst->name), "%s", src->name);
    snprintf(dst->note, sizeof(dst->note), "%s", src->note);
    for( int i = 0; i < src->count; i++ )
        (void)Editor_StampAdd(dst, &src->pieces[i]);
    dst->turned = src->turned;
}

int
Editor_StampFromFile(
    const struct Editor_StampsFile* file,
    struct Editor_Stamp* stamp)
{
    int header = -1;

    assert(file);
    assert(stamp);
    Editor_StampClear(stamp);
    for( int i = 0; i < file->line_count; i++ )
    {
        const struct Editor_StampsLine* line = &file->lines[i];
        if( line->kind == EDITOR_STAMPS_LINE_HEADER && header < 0 )
        {
            header = i;
            snprintf(stamp->name, sizeof(stamp->name), "%s", line->name);
            snprintf(stamp->note, sizeof(stamp->note), "%s", line->note);
        }
        else if( line->kind == EDITOR_STAMPS_LINE_PIECE )
            (void)Editor_StampAdd(stamp, &line->piece);
    }
    return header >= 0;
}

/** Append `length` bytes to out[0..capacity) at *total, snprintf-style. */
static void
emit_bytes(
    char* out,
    size_t capacity,
    size_t* total,
    const char* bytes,
    size_t length)
{
    if( out && *total < capacity )
    {
        size_t const room = capacity - *total - 1;
        memcpy(out + *total, bytes, length < room ? length : room);
    }
    *total += length;
}

size_t
Editor_StampEmitNew(
    const struct Editor_Stamp* stamp,
    char* out,
    size_t out_capacity)
{
    char line[EDITOR_STAMP_LINE_MAX + 2];
    size_t total = 0;
    int n;

    assert(stamp);
    assert(out || out_capacity == 0);
    assert(Editor_StampNameValid(stamp->name));
    emit_bytes(out, out_capacity, &total, k_stamp_comments, sizeof(k_stamp_comments) - 1);
    n = Editor_StampFormatHeader(stamp->name, stamp->note, line, sizeof(line) - 1);
    assert(n > 0);
    assert((size_t)n < sizeof(line) - 1);
    line[n] = '\n';
    emit_bytes(out, out_capacity, &total, line, (size_t)n + 1);
    for( int i = 0; i < stamp->count; i++ )
    {
        n = Editor_StampFormatPiece(&stamp->pieces[i], line, sizeof(line) - 1);
        assert(n > 0);
        assert((size_t)n < sizeof(line) - 1);
        line[n] = '\n';
        emit_bytes(out, out_capacity, &total, line, (size_t)n + 1);
    }
    if( out && out_capacity > 0 )
        out[total < out_capacity ? total : out_capacity - 1] = '\0';
    return total;
}

void
Editor_StampPieceRotate(
    struct Editor_StampPiece* piece,
    int quarters)
{
    assert(piece);
    quarters = ((quarters % 4) + 4) % 4;
    for( int q = 0; q < quarters; q++ )
    {
        /* Clockwise seen from above (z north): (x, z) -> (z, -x). A
         * footprint's SW corner is where its NW corner went. */
        int const w = piece->kind == EDITOR_STAMP_LOC ? piece->size_x : 1;
        int const h = piece->kind == EDITOR_STAMP_LOC ? piece->size_z : 1;
        int const dx = piece->dz;
        int const dz = -(piece->dx + w - 1);
        piece->dx = dx;
        piece->dz = dz;
        if( piece->kind == EDITOR_STAMP_LOC )
        {
            piece->size_x = h;
            piece->size_z = w;
            piece->rotation = (piece->rotation + 1) & 3;
        }
        else if( piece->kind == EDITOR_STAMP_TILE && piece->tile.has_overlay )
            piece->tile.rotation = (uint8_t)((piece->tile.rotation + 1) & 3);
    }
}

void
Editor_StampRotate(
    struct Editor_Stamp* stamp,
    int quarters)
{
    assert(stamp);
    for( int i = 0; i < stamp->count; i++ )
        Editor_StampPieceRotate(&stamp->pieces[i], quarters);
    stamp->turned = (((stamp->turned + quarters) % 4) + 4) % 4;
}

int
Editor_StampCount(
    const struct Editor_Stamp* stamp,
    enum Editor_StampPieceKind kind)
{
    int count = 0;

    assert(stamp);
    for( int i = 0; i < stamp->count; i++ )
        if( stamp->pieces[i].kind == kind )
            count++;
    return count;
}

void
Editor_StampSummary(
    const struct Editor_Stamp* stamp,
    char* out,
    size_t out_size)
{
    int const locs = Editor_StampCount(stamp, EDITOR_STAMP_LOC);
    int const tiles = Editor_StampCount(stamp, EDITOR_STAMP_TILE);
    int const spawns = Editor_StampCount(stamp, EDITOR_STAMP_SPAWN);

    assert(out);
    snprintf(out, out_size, "%s: %d loc%s, %d tile%s, %d spawn%s", stamp->name[0] ? stamp->name : "(unnamed)", locs,
        locs == 1 ? "" : "s", tiles, tiles == 1 ? "" : "s", spawns, spawns == 1 ? "" : "s");
}

/* ---- writing a document ----------------------------------------------------------- */

int
Editor_StampSplices(
    const char* doc_key,
    const char* old_text,
    size_t old_length,
    const char* new_text,
    size_t new_length,
    struct Editor_Cmd* out,
    int max)
{
    size_t head = 0;
    size_t tail = 0;
    size_t done;
    int count = 0;

    assert(doc_key);
    assert(old_text || old_length == 0);
    assert(new_text || new_length == 0);
    assert(out || max == 0);

    while( head < old_length && head < new_length && old_text[head] == new_text[head] )
        head++;
    while( tail < old_length - head && tail < new_length - head
           && old_text[old_length - 1 - tail] == new_text[new_length - 1 - tail] )
        tail++;
    if( head == old_length && head == new_length )
        return 0;

    /* The old middle out, a chunk at a time from the same offset... */
    done = 0;
    while( done < old_length - head - tail )
    {
        size_t chunk = old_length - head - tail - done;
        if( chunk > EDITOR_CMD_SPLICE_MAX )
            chunk = EDITOR_CMD_SPLICE_MAX;
        if( count == max )
            return -1;
        Editor_CmdInit(&out[count], EDITOR_DOC_TEXT, doc_key, EDITOR_CMD_TEXT_SPLICE);
        out[count].has_before = 1;
        out[count].u.text_splice.offset = (uint32_t)head;
        out[count].u.text_splice.before_len = (uint32_t)chunk;
        out[count].u.text_splice.after_len = 0;
        memcpy(out[count].u.text_splice.text, old_text + head + done, chunk);
        done += chunk;
        count++;
    }
    /* ...then the new one in, each chunk after the last. */
    done = 0;
    while( done < new_length - head - tail )
    {
        size_t chunk = new_length - head - tail - done;
        if( chunk > EDITOR_CMD_SPLICE_MAX )
            chunk = EDITOR_CMD_SPLICE_MAX;
        if( count == max )
            return -1;
        Editor_CmdInit(&out[count], EDITOR_DOC_TEXT, doc_key, EDITOR_CMD_TEXT_SPLICE);
        out[count].has_after = 1;
        out[count].u.text_splice.offset = (uint32_t)(head + done);
        out[count].u.text_splice.before_len = 0;
        out[count].u.text_splice.after_len = (uint32_t)chunk;
        memcpy(out[count].u.text_splice.text, new_text + head + done, chunk);
        done += chunk;
        count++;
    }
    return count;
}

size_t
Editor_StampsIndexWith(
    const char* old_text,
    size_t old_length,
    const char* name,
    char* out,
    size_t out_capacity)
{
    struct Editor_StampsFile file;
    char line[EDITOR_STAMP_LINE_MAX + 1];
    size_t total = 0;
    int listed;
    int n;

    assert(old_text || old_length == 0);
    assert(name);
    assert(Editor_StampNameValid(name));
    assert(out || out_capacity == 0);

    Editor_StampsInit(&file);
    (void)Editor_StampsParse(&file, old_text, old_length);
    listed = Editor_StampsFindHeader(&file, name) >= 0;
    Editor_StampsFree(&file);

    if( old_length == 0 )
        emit_bytes(out, out_capacity, &total, k_index_comments, sizeof(k_index_comments) - 1);
    else
        emit_bytes(out, out_capacity, &total, old_text, old_length);
    if( !listed )
    {
        if( old_length > 0 && old_text[old_length - 1] != '\n' )
            emit_bytes(out, out_capacity, &total, "\n", 1);
        n = Editor_StampFormatHeader(name, NULL, line, sizeof(line));
        assert(n > 0);
        assert((size_t)n < sizeof(line));
        emit_bytes(out, out_capacity, &total, line, (size_t)n);
        emit_bytes(out, out_capacity, &total, "\n", 1);
    }
    if( out && out_capacity > 0 )
        out[total < out_capacity ? total : out_capacity - 1] = '\0';
    return total;
}
