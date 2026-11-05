/*
 * The FAVOURITES document's codec and its splices (editor_favourites.h).
 */

#include "editor_favourites.h"

#include "editor_palette.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
Editor_FavouritesInit(struct Editor_FavouritesFile* file)
{
    assert(file);
    memset(file, 0, sizeof(*file));
}

void
Editor_FavouritesFree(struct Editor_FavouritesFile* file)
{
    if( !file )
        return;
    ToriMem_Free(ToriMem_Editor(), file->lines);
    ToriMem_Free(ToriMem_Editor(), file->text);
    memset(file, 0, sizeof(*file));
}

static int
fav_is_space(char c)
{
    return c == ' ' || c == '\t';
}

int
Editor_FavouritesParseLine(
    const char* text,
    size_t length,
    struct Editor_FavouriteRow* out_row)
{
    struct Editor_FavouriteRow row;
    size_t at = 0;
    size_t start;
    char kind[8];
    static const char k_word[] = "favourite";

    assert(text);
    while( at < length && fav_is_space(text[at]) )
        at++;
    if( at == length || text[at] == '#' || (at + 1 < length && text[at] == '/' && text[at + 1] == '/') )
        return 0;
    if( length - at < sizeof(k_word) || memcmp(text + at, k_word, sizeof(k_word) - 1) != 0
        || !fav_is_space(text[at + sizeof(k_word) - 1]) )
        return -1;
    at += sizeof(k_word) - 1;
    while( at < length && fav_is_space(text[at]) )
        at++;

    memset(&row, 0, sizeof(row));
    row.shape = -1;
    /* <kind>:<record> */
    start = at;
    while( at < length && text[at] != ':' && !fav_is_space(text[at]) )
        at++;
    if( at == length || text[at] != ':' || at - start == 0 || at - start >= sizeof(kind) )
        return -1;
    memcpy(kind, text + start, at - start);
    kind[at - start] = '\0';
    row.kind = Editor_PaletteKindFromName(kind);
    if( row.kind < 0 )
        return -1;
    at++;
    start = at;
    while( at < length && !fav_is_space(text[at]) && text[at] != '|' )
        at++;
    if( at - start == 0 || at - start >= sizeof(row.record) )
        return -1;
    memcpy(row.record, text + start, at - start);
    row.record[at - start] = '\0';
    if( !Editor_PaletteRecordValid(row.record) )
        return -1;
    while( at < length && fav_is_space(text[at]) )
        at++;
    /* [| shape S rot R] */
    if( at < length )
    {
        char tail[64];
        int shape;
        int rotation;
        int used = 0;
        if( text[at] != '|' || length - at >= sizeof(tail) )
            return -1;
        memcpy(tail, text + at + 1, length - at - 1);
        tail[length - at - 1] = '\0';
        if( sscanf(tail, " shape %d rot %d %n", &shape, &rotation, &used) != 2 || tail[used] != '\0' )
            return -1;
        if( row.kind != EDITOR_PALETTE_LOC || shape < 0 || shape > EDITOR_PALETTE_SHAPE_MAX || rotation < 0
            || rotation > 3 )
            return -1;
        row.shape = shape;
        row.rotation = rotation;
    }
    if( out_row )
        *out_row = row;
    return 1;
}

int
Editor_FavouriteRowValid(const struct Editor_FavouriteRow* row)
{
    assert(row);
    if( row->kind < EDITOR_PALETTE_LOC || row->kind > EDITOR_PALETTE_OBJ || !Editor_PaletteRecordValid(row->record) )
        return 0;
    if( row->shape == -1 )
        return row->rotation == 0;
    return row->kind == EDITOR_PALETTE_LOC && row->shape >= 0 && row->shape <= EDITOR_PALETTE_SHAPE_MAX
           && row->rotation >= 0 && row->rotation <= 3;
}

int
Editor_FavouriteFormat(
    const struct Editor_FavouriteRow* row,
    char* out,
    size_t out_capacity)
{
    assert(row);
    assert(out || out_capacity == 0);
    if( row->shape >= 0 )
        return snprintf(out, out_capacity, "favourite %s:%s | shape %d rot %d", Editor_PaletteKindName(row->kind),
            row->record, row->shape, row->rotation);
    return snprintf(out, out_capacity, "favourite %s:%s", Editor_PaletteKindName(row->kind), row->record);
}

struct Editor_FavouritesParseResult
Editor_FavouritesParse(
    struct Editor_FavouritesFile* file,
    const char* text,
    size_t length)
{
    struct Editor_FavouritesParseResult result;
    size_t at = 0;
    int line_number = 0;

    assert(file);
    if( length > 0 )
        assert(text);
    Editor_FavouritesFree(file);
    result.ok = 1;
    result.line = 0;
    file->text = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(file->text);
    if( length > 0 )
        memcpy(file->text, text, length);
    file->text[length] = '\0';
    file->length = length;

    while( at < length )
    {
        const char* newline = memchr(file->text + at, '\n', length - at);
        size_t const stop = newline ? (size_t)(newline - file->text) : length;
        size_t content = stop;
        struct Editor_FavouritesLine* line;
        int kind;

        line_number++;
        if( content > at && file->text[content - 1] == '\r' )
            content--;
        if( file->line_count == file->line_capacity )
        {
            int const capacity = file->line_capacity ? file->line_capacity * 2 : 16;
            file->lines = ToriMem_Realloc(ToriMem_Editor(), file->lines, sizeof(*file->lines) * (size_t)capacity);
            assert(file->lines);
            file->line_capacity = capacity;
        }
        line = &file->lines[file->line_count++];
        memset(line, 0, sizeof(*line));
        line->offset = at;
        line->length = (newline ? stop + 1 : length) - at;
        kind = Editor_FavouritesParseLine(file->text + at, content - at, &line->row);
        if( kind < 0 && result.ok )
        {
            result.ok = 0;
            result.line = line_number;
        }
        line->is_row = kind == 1;
        if( line->is_row && result.ok )
            for( int i = 0; i < file->line_count - 1; i++ )
                if( file->lines[i].is_row && file->lines[i].row.kind == line->row.kind
                    && strcmp(file->lines[i].row.record, line->row.record) == 0 )
                {
                    /* One line per record. */
                    result.ok = 0;
                    result.line = line_number;
                    break;
                }
        at = newline ? stop + 1 : length;
    }
    return result;
}

size_t
Editor_FavouritesEmit(
    const struct Editor_FavouritesFile* file,
    char* out,
    size_t out_capacity)
{
    assert(file);
    assert(out || out_capacity == 0);
    if( out_capacity > 0 )
    {
        size_t const n = file->length < out_capacity - 1 ? file->length : out_capacity - 1;
        if( n > 0 )
            memcpy(out, file->text, n);
        out[n] = '\0';
    }
    return file->length;
}

int
Editor_FavouritesCount(const struct Editor_FavouritesFile* file)
{
    int count = 0;

    assert(file);
    for( int i = 0; i < file->line_count; i++ )
        count += file->lines[i].is_row;
    return count;
}

const struct Editor_FavouriteRow*
Editor_FavouritesAt(
    const struct Editor_FavouritesFile* file,
    int nth)
{
    assert(file);
    assert(nth >= 0);
    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].is_row && nth-- == 0 )
            return &file->lines[i].row;
    assert(!"Editor_FavouritesAt: past the last favourite");
    return NULL;
}

int
Editor_FavouritesFind(
    const struct Editor_FavouritesFile* file,
    int kind,
    const char* record)
{
    assert(file);
    assert(record);
    for( int i = 0; i < file->line_count; i++ )
        if( file->lines[i].is_row && file->lines[i].row.kind == kind && strcmp(file->lines[i].row.record, record) == 0 )
            return i;
    return -1;
}

/** One splice: `before_len` bytes of the text at `offset` become `after`. */
static int
fav_splice(
    struct Editor_Cmd* out,
    const struct Editor_FavouritesFile* file,
    size_t offset,
    size_t before_len,
    const char* after,
    size_t after_len)
{
    if( before_len + after_len > EDITOR_CMD_SPLICE_MAX )
        return 0;
    Editor_CmdInit(out, EDITOR_DOC_TEXT, EDITOR_FAVOURITES_DOC_KEY, EDITOR_CMD_TEXT_SPLICE);
    out->u.text_splice.offset = (uint32_t)offset;
    out->u.text_splice.before_len = (uint32_t)before_len;
    out->u.text_splice.after_len = (uint32_t)after_len;
    if( before_len > 0 )
        memcpy(out->u.text_splice.text, file->text + offset, before_len);
    if( after_len > 0 )
        memcpy(out->u.text_splice.text + before_len, after, after_len);
    return 1;
}

int
Editor_FavouritesAddCommand(
    struct Editor_Cmd* out,
    const struct Editor_FavouritesFile* file,
    const struct Editor_FavouriteRow* row)
{
    char text[EDITOR_CMD_SPLICE_MAX];
    int line;
    int written;
    size_t at = 0;

    assert(out);
    assert(file);
    assert(row);
    if( !Editor_FavouriteRowValid(row) )
        return 0;
    line = Editor_FavouritesFind(file, row->kind, row->record);
    if( line >= 0 )
    {
        /* Re-posed: the line rewritten in place, its own line ending kept. */
        const struct Editor_FavouritesLine* l = &file->lines[line];
        size_t ending = 0;
        if( l->row.shape == row->shape && l->row.rotation == row->rotation )
            return 0;
        if( l->length > 0 && file->text[l->offset + l->length - 1] == '\n' )
            ending = l->length > 1 && file->text[l->offset + l->length - 2] == '\r' ? 2 : 1;
        written = Editor_FavouriteFormat(row, text, sizeof(text));
        if( written <= 0 || (size_t)written + ending >= sizeof(text) )
            return 0;
        memcpy(text + written, file->text + l->offset + l->length - ending, ending);
        return fav_splice(out, file, l->offset, l->length, text, (size_t)written + ending);
    }
    if( file->length == 0 )
    {
        written = snprintf(text, sizeof(text), "%s", EDITOR_FAVOURITES_HEADER);
        at = (size_t)written;
    }
    else if( file->text[file->length - 1] != '\n' )
        text[at++] = '\n';
    written = Editor_FavouriteFormat(row, text + at, sizeof(text) - at);
    if( written <= 0 || at + (size_t)written + 1 >= sizeof(text) )
        return 0;
    at += (size_t)written;
    text[at++] = '\n';
    return fav_splice(out, file, file->length, 0, text, at);
}

int
Editor_FavouritesRemoveCommand(
    struct Editor_Cmd* out,
    const struct Editor_FavouritesFile* file,
    int kind,
    const char* record)
{
    int line;

    assert(out);
    assert(file);
    assert(record);
    line = Editor_FavouritesFind(file, kind, record);
    if( line < 0 )
        return 0;
    /* The last favourite of a file that is the header and it: the header
     * goes too, as it came (the emptied document is deleted on save). */
    {
        size_t const header = sizeof(EDITOR_FAVOURITES_HEADER) - 1;
        if( file->lines[line].offset == header && file->lines[line].offset + file->lines[line].length == file->length
            && memcmp(file->text, EDITOR_FAVOURITES_HEADER, header) == 0 )
            return fav_splice(out, file, 0, file->length, NULL, 0);
    }
    return fav_splice(out, file, file->lines[line].offset, file->lines[line].length, NULL, 0);
}
