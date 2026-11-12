#include "editor_cfg.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---- lines ---------------------------------------------------------------- */

size_t
Editor_CfgLineParse(
    const char* text,
    size_t length,
    size_t at,
    struct Editor_CfgLine* out_line)
{
    const char* newline;
    size_t stop;
    size_t content_end;
    size_t next;
    size_t i;

    assert(text);
    assert(out_line);
    assert(at < length);

    newline = memchr(text + at, '\n', length - at);
    memset(out_line, 0, sizeof(*out_line));
    if( newline )
    {
        stop = (size_t)(newline - text);
        next = stop + 1;
        if( stop > at && text[stop - 1] == '\r' )
        {
            content_end = stop - 1;
            out_line->eol = EDITOR_CFG_EOL_CRLF;
        }
        else
        {
            content_end = stop;
            out_line->eol = EDITOR_CFG_EOL_LF;
        }
    }
    else
    {
        content_end = length;
        next = length;
        out_line->eol = EDITOR_CFG_EOL_NONE;
    }
    out_line->start = at;
    out_line->length = content_end - at;

    if( content_end > at && text[at] == '[' )
    {
        const char* close = memchr(text + at + 1, ']', content_end - at - 1);

        out_line->kind = EDITOR_CFG_LINE_HEADER;
        if( close )
        {
            out_line->part = (size_t)(close - (text + at + 1));
            out_line->closed = 1;
        }
        else
            out_line->part = content_end - at - 1;
        return next;
    }

    for( i = at; i < content_end && (text[i] == ' ' || text[i] == '\t'); i++ )
    {
    }
    if( i == content_end )
        out_line->kind = EDITOR_CFG_LINE_BLANK;
    else if( text[i] == '/' && i + 1 < content_end && text[i + 1] == '/' )
        out_line->kind = EDITOR_CFG_LINE_COMMENT;
    else
    {
        const char* equals = memchr(text + at, '=', content_end - at);

        if( equals )
        {
            out_line->kind = EDITOR_CFG_LINE_FIELD;
            out_line->part = (size_t)(equals - (text + at));
        }
        else
            out_line->kind = EDITOR_CFG_LINE_OTHER;
    }
    return next;
}

static size_t
emit_bytes(
    char* out,
    size_t at,
    const char* bytes,
    size_t length)
{
    if( out && length > 0 )
        memcpy(out + at, bytes, length);
    return at + length;
}

static size_t
emit_eol(
    char* out,
    size_t at,
    unsigned char eol)
{
    if( eol == EDITOR_CFG_EOL_CRLF )
        return emit_bytes(out, at, "\r\n", 2);
    if( eol == EDITOR_CFG_EOL_LF )
        return emit_bytes(out, at, "\n", 1);
    return at;
}

size_t
Editor_CfgEmitLines(
    const char* text,
    const struct Editor_CfgLine* lines,
    int count,
    char* out)
{
    size_t at = 0;

    assert(text);
    assert(lines || count == 0);

    for( int i = 0; i < count; i++ )
    {
        const struct Editor_CfgLine* line = &lines[i];
        const char* base = text + line->start;

        if( line->kind == EDITOR_CFG_LINE_HEADER )
        {
            size_t used = 1 + line->part + (line->closed ? 1 : 0);

            at = emit_bytes(out, at, "[", 1);
            at = emit_bytes(out, at, base + 1, line->part);
            if( line->closed )
                at = emit_bytes(out, at, "]", 1);
            at = emit_bytes(out, at, base + used, line->length - used);
        }
        else if( line->kind == EDITOR_CFG_LINE_FIELD )
        {
            at = emit_bytes(out, at, base, line->part);
            at = emit_bytes(out, at, "=", 1);
            at = emit_bytes(out, at, base + line->part + 1, line->length - line->part - 1);
        }
        else
            at = emit_bytes(out, at, base, line->length);
        at = emit_eol(out, at, line->eol);
    }
    return at;
}

/** A header line naming exactly `name`: "[name]" and nothing after it. */
static int
header_names(
    const char* text,
    const struct Editor_CfgLine* line,
    const char* name,
    size_t name_length)
{
    return line->kind == EDITOR_CFG_LINE_HEADER && line->closed && line->part == name_length
           && line->length == name_length + 2
           && memcmp(text + line->start + 1, name, name_length) == 0;
}

static int
key_valid(
    const char* key,
    size_t length)
{
    size_t i;

    if( length == 0 || key[0] == '[' )
        return 0;
    for( i = 0; i < length; i++ )
    {
        if( key[i] == '=' || key[i] == '#' || key[i] == '@' || key[i] == '\n' || key[i] == '\r'
            || key[i] == '\0' )
            return 0;
    }
    /* A key whose first non-blank bytes are "//" would read back as a comment,
     * one of only blanks as a blank line. */
    for( i = 0; i < length && (key[i] == ' ' || key[i] == '\t'); i++ )
    {
    }
    if( i == length )
        return 0;
    return !(key[i] == '/' && i + 1 < length && key[i + 1] == '/');
}

static int
text_one_line(const char* value)
{
    return strchr(value, '\n') == NULL && strchr(value, '\r') == NULL;
}

static int
parse_count(
    const char* at,
    const char* stop,
    int* out)
{
    int value = 0;

    if( at == stop )
        return 0;
    for( ; at < stop; at++ )
    {
        if( *at < '0' || *at > '9' )
            return 0;
        value = value * 10 + (*at - '0');
        if( value > 1000000 )
            return 0;
    }
    *out = value;
    return 1;
}

int
Editor_CfgFieldSpecParse(
    const char* spec,
    char* out_key,
    size_t key_size,
    int* out_occurrence,
    int* out_line)
{
    const char* key_end;
    const char* hash;
    const char* at_sign;
    const char* end;
    size_t key_length;

    assert(spec);
    assert(out_key);
    assert(out_occurrence);
    assert(out_line);

    end = spec + strlen(spec);
    hash = strchr(spec, '#');
    at_sign = strchr(spec, '@');
    key_end = hash ? hash : at_sign ? at_sign : end;
    if( at_sign && hash && at_sign < hash )
        return 0;

    key_length = (size_t)(key_end - spec);
    if( !key_valid(spec, key_length) || key_length + 1 > key_size )
        return 0;

    *out_occurrence = 0;
    *out_line = -1;
    if( hash && !parse_count(hash + 1, at_sign ? at_sign : end, out_occurrence) )
        return 0;
    if( at_sign && !parse_count(at_sign + 1, end, out_line) )
        return 0;

    memcpy(out_key, spec, key_length);
    out_key[key_length] = '\0';
    return 1;
}

int
Editor_CfgFieldSpecFormat(
    const char* key,
    int occurrence,
    int line,
    char* out,
    size_t out_size)
{
    int written;

    assert(key);
    assert(out);
    assert(occurrence >= 0);

    if( occurrence > 0 && line >= 0 )
        written = snprintf(out, out_size, "%s#%d@%d", key, occurrence, line);
    else if( occurrence > 0 )
        written = snprintf(out, out_size, "%s#%d", key, occurrence);
    else if( line >= 0 )
        written = snprintf(out, out_size, "%s@%d", key, line);
    else
        written = snprintf(out, out_size, "%s", key);
    return written > 0 && (size_t)written < out_size;
}

/* ---- the file index -------------------------------------------------------- */

void
Editor_CfgFileIndex(
    struct Editor_CfgFile* file,
    const char* text,
    size_t length)
{
    size_t at = 0;
    int have_header = 0;

    assert(file);
    assert(text || length == 0);

    memset(file, 0, sizeof(*file));
    file->text = text;
    file->length = length;
    file->preamble_end = length;

    while( at < length )
    {
        const char* newline = memchr(text + at, '\n', length - at);
        size_t stop = newline ? (size_t)(newline - text) : length;
        size_t next = newline ? stop + 1 : length;

        if( text[at] == '[' )
        {
            struct Editor_CfgBlockRef* ref;
            size_t content_end = stop;
            const char* close;

            if( content_end > at && text[content_end - 1] == '\r' )
                content_end--;
            if( !have_header )
            {
                file->preamble_end = at;
                have_header = 1;
            }
            else
                file->blocks[file->block_count - 1].end = at;

            if( file->block_count == file->block_capacity )
            {
                int next_capacity = file->block_capacity ? file->block_capacity * 2 : 256;
                file->blocks = ToriMem_Realloc(
                    ToriMem_Editor(), file->blocks, sizeof(*file->blocks) * (size_t)next_capacity);
                assert(file->blocks);
                file->block_capacity = next_capacity;
            }
            ref = &file->blocks[file->block_count++];
            ref->start = at;
            ref->end = length;
            ref->name_start = at + 1;
            close = content_end > at + 1 ? memchr(text + at + 1, ']', content_end - at - 1) : NULL;
            ref->name_length = close ? (size_t)(close - (text + at + 1)) : content_end - at - 1;
        }
        at = next;
    }
}

void
Editor_CfgFileFree(struct Editor_CfgFile* file)
{
    if( !file )
        return;
    ToriMem_Free(ToriMem_Editor(), file->blocks);
    memset(file, 0, sizeof(*file));
}

int
Editor_CfgFileFind(
    const struct Editor_CfgFile* file,
    const char* name)
{
    size_t name_length;

    assert(file);
    assert(name);

    name_length = strlen(name);
    for( int i = 0; i < file->block_count; i++ )
    {
        const struct Editor_CfgBlockRef* ref = &file->blocks[i];

        if( ref->name_length == name_length
            && memcmp(file->text + ref->name_start, name, name_length) == 0 )
        {
            struct Editor_CfgLine line;

            Editor_CfgLineParse(file->text, file->length, ref->start, &line);
            if( header_names(file->text, &line, name, name_length) )
                return i;
        }
    }
    return -1;
}

int
Editor_CfgBlockExtent(
    const char* text,
    size_t length,
    const char* name,
    size_t* out_start,
    size_t* out_end)
{
    size_t name_length;
    size_t at = 0;

    assert(text || length == 0);
    assert(name);
    assert(out_start);
    assert(out_end);

    name_length = strlen(name);
    while( at < length )
    {
        const char* newline = memchr(text + at, '\n', length - at);
        size_t stop = newline ? (size_t)(newline - text) : length;
        size_t next = newline ? stop + 1 : length;
        size_t content_end = stop;

        if( content_end > at && text[content_end - 1] == '\r' )
            content_end--;
        if( text[at] == '[' && content_end - at == name_length + 2 && text[content_end - 1] == ']'
            && memcmp(text + at + 1, name, name_length) == 0 )
        {
            while( next < length && text[next] != '[' )
            {
                const char* line_end = memchr(text + next, '\n', length - next);
                next = line_end ? (size_t)(line_end - text) + 1 : length;
            }
            *out_start = at;
            *out_end = next;
            return 1;
        }
        at = next;
    }
    return 0;
}

char*
Editor_CfgFileReplaceBlock(
    const char* text,
    size_t length,
    const char* name,
    const char* block,
    size_t block_length,
    size_t* out_length)
{
    size_t start = length;
    size_t end = length;
    size_t at;
    int found;
    char* merged;

    assert(text || length == 0);
    assert(name);
    assert(block || block_length == 0);
    assert(out_length);

    found = Editor_CfgBlockExtent(text, length, name, &start, &end);
    merged = ToriMem_Alloc(ToriMem_Editor(), length - (end - start) + block_length + 3);
    assert(merged);

    if( start > 0 )
        memcpy(merged, text, start);
    at = start;
    if( !found && block_length > 0 && length > 0 )
    {
        /* A new record goes at the end, after a blank separator line. */
        if( text[length - 1] != '\n' )
            merged[at++] = '\n';
        if( length < 2 || text[length - 2] != '\n' || text[length - 1] != '\n' )
            merged[at++] = '\n';
    }
    if( block_length > 0 )
    {
        memcpy(merged + at, block, block_length);
        at += block_length;
    }
    if( length > end )
    {
        memcpy(merged + at, text + end, length - end);
        at += length - end;
    }
    merged[at] = '\0';
    *out_length = at;
    return merged;
}

/* ---- one block -------------------------------------------------------------- */

void
Editor_CfgBlockInit(struct Editor_CfgBlock* block)
{
    assert(block);
    memset(block, 0, sizeof(*block));
}

void
Editor_CfgBlockFree(struct Editor_CfgBlock* block)
{
    if( !block )
        return;
    ToriMem_Free(ToriMem_Editor(), block->text);
    ToriMem_Free(ToriMem_Editor(), block->lines);
    memset(block, 0, sizeof(*block));
}

static void
block_reparse(struct Editor_CfgBlock* block)
{
    size_t at = 0;

    block->line_count = 0;
    while( at < block->length )
    {
        if( block->line_count == block->line_capacity )
        {
            int next = block->line_capacity ? block->line_capacity * 2 : 16;
            block->lines = ToriMem_Realloc(
                ToriMem_Editor(), block->lines, sizeof(*block->lines) * (size_t)next);
            assert(block->lines);
            block->line_capacity = next;
        }
        at = Editor_CfgLineParse(block->text, block->length, at, &block->lines[block->line_count]);
        block->line_count++;
    }
}

static void
block_reserve(
    struct Editor_CfgBlock* block,
    size_t length)
{
    size_t next;

    /* `length < capacity`, not `length + 1 <= capacity`: the +1 wraps at
     * SIZE_MAX, and that wrapped path (an empty block "already big enough")
     * is one the optimiser follows into the caller's memcpy. */
    if( length < block->capacity )
        return;
    next = block->capacity ? block->capacity : 64;
    while( next <= length )
        next *= 2;
    block->text = ToriMem_Realloc(ToriMem_Editor(), block->text, next);
    assert(block->text);
    block->capacity = next;
}

/** Replace `remove` bytes at `offset` with the concatenation of up to three
 *  pieces, then reparse. */
static void
block_splice(
    struct Editor_CfgBlock* block,
    size_t offset,
    size_t remove,
    const char* a,
    const char* b,
    const char* c)
{
    size_t la = a ? strlen(a) : 0;
    size_t lb = b ? strlen(b) : 0;
    size_t lc = c ? strlen(c) : 0;
    size_t insert = la + lb + lc;
    size_t next_length;

    assert(offset <= block->length);
    assert(remove <= block->length - offset);

    next_length = block->length - remove + insert;
    block_reserve(block, next_length);
    memmove(
        block->text + offset + insert,
        block->text + offset + remove,
        block->length - offset - remove + 1);
    if( la )
        memcpy(block->text + offset, a, la);
    if( lb )
        memcpy(block->text + offset + la, b, lb);
    if( lc )
        memcpy(block->text + offset + la + lb, c, lc);
    block->length = next_length;
    block_reparse(block);
}

void
Editor_CfgBlockSet(
    struct Editor_CfgBlock* block,
    const char* text,
    size_t length)
{
    assert(block);
    assert(text || length == 0);

    block_reserve(block, length);
    if( length > 0 )
        memcpy(block->text, text, length);
    block->text[length] = '\0';
    block->length = length;
    block_reparse(block);
}

int
Editor_CfgBlockHeader(const struct Editor_CfgBlock* block)
{
    assert(block);

    for( int i = 0; i < block->line_count; i++ )
    {
        if( block->lines[i].kind == EDITOR_CFG_LINE_HEADER )
            return i;
    }
    return -1;
}

int
Editor_CfgBlockNameIs(
    const struct Editor_CfgBlock* block,
    const char* name)
{
    int header;

    assert(block);
    assert(name);

    header = Editor_CfgBlockHeader(block);
    return header >= 0 && header_names(block->text, &block->lines[header], name, strlen(name));
}

static int
line_is_key(
    const struct Editor_CfgBlock* block,
    int index,
    const char* key,
    size_t key_length)
{
    const struct Editor_CfgLine* line = &block->lines[index];

    return line->kind == EDITOR_CFG_LINE_FIELD && line->part == key_length
           && memcmp(block->text + line->start, key, key_length) == 0;
}

int
Editor_CfgBlockFieldCount(
    const struct Editor_CfgBlock* block,
    const char* key)
{
    size_t key_length;
    int count = 0;

    assert(block);
    assert(key);

    key_length = strlen(key);
    for( int i = 0; i < block->line_count; i++ )
        count += line_is_key(block, i, key, key_length);
    return count;
}

int
Editor_CfgBlockFieldFind(
    const struct Editor_CfgBlock* block,
    const char* key,
    int occurrence)
{
    size_t key_length;
    int seen = 0;

    assert(block);
    assert(key);

    key_length = strlen(key);
    for( int i = 0; i < block->line_count; i++ )
    {
        if( !line_is_key(block, i, key, key_length) )
            continue;
        if( seen == occurrence )
            return i;
        seen++;
    }
    return -1;
}

const char*
Editor_CfgBlockValue(
    const struct Editor_CfgBlock* block,
    int line_index,
    size_t* out_length)
{
    const struct Editor_CfgLine* line;

    assert(block);
    assert(out_length);
    assert(line_index >= 0);
    assert(line_index < block->line_count);

    line = &block->lines[line_index];
    assert(line->kind == EDITOR_CFG_LINE_FIELD);
    *out_length = line->length - line->part - 1;
    return block->text + line->start + line->part + 1;
}

int
Editor_CfgBlockFieldIs(
    const struct Editor_CfgBlock* block,
    const char* key,
    int occurrence,
    const char* value)
{
    int index;
    size_t length;
    const char* current;

    assert(block);
    assert(key);
    assert(value);

    index = Editor_CfgBlockFieldFind(block, key, occurrence);
    if( index < 0 )
        return 0;
    current = Editor_CfgBlockValue(block, index, &length);
    return length == strlen(value) && memcmp(current, value, length) == 0;
}

int
Editor_CfgBlockFieldSet(
    struct Editor_CfgBlock* block,
    const char* key,
    int occurrence,
    const char* value)
{
    int index;
    size_t length;
    const char* current;

    assert(block);
    assert(key);
    assert(value);

    if( !key_valid(key, strlen(key)) || !text_one_line(value) )
        return 0;
    index = Editor_CfgBlockFieldFind(block, key, occurrence);
    if( index < 0 )
        return 0;
    current = Editor_CfgBlockValue(block, index, &length);
    block_splice(block, (size_t)(current - block->text), length, value, NULL, NULL);
    return 1;
}

static const char*
eol_text(unsigned char eol)
{
    return eol == EDITOR_CFG_EOL_CRLF ? "\r\n" : "\n";
}

/** Offset just past line `index`'s ending. */
static size_t
line_after(
    const struct Editor_CfgBlock* block,
    int index)
{
    const struct Editor_CfgLine* line = &block->lines[index];
    return line->start + line->length + Editor_CfgEolLength(line->eol);
}

/** Insert "key=value" so it becomes line `position` (1..line_count). */
static void
insert_line_at(
    struct Editor_CfgBlock* block,
    int position,
    const char* key,
    const char* value,
    const char* eol)
{
    char field[512];
    const struct Editor_CfgLine* before = &block->lines[position - 1];

    snprintf(field, sizeof(field), "%s=", key);
    if( before->eol != EDITOR_CFG_EOL_NONE )
    {
        /* Lines below `position` (if any) keep their bytes: the new line
         * carries its own ending. */
        char tail[512];

        snprintf(tail, sizeof(tail), "%s%s", value, eol);
        block_splice(block, line_after(block, position - 1), 0, field, tail, NULL);
        return;
    }
    /* After a last line with no ending: the ending goes between them and the
     * new line is the last, with none. */
    block_splice(block, before->start + before->length, 0, eol, field, value);
}

int
Editor_CfgBlockFieldInsert(
    struct Editor_CfgBlock* block,
    const char* key,
    int occurrence,
    int line,
    const char* value)
{
    int header;
    int count;
    int position;
    size_t key_length;
    const char* eol;

    assert(block);
    assert(key);
    assert(value);

    key_length = strlen(key);
    if( !key_valid(key, key_length) || !text_one_line(value) || occurrence < 0 )
        return 0;
    if( key_length + strlen(value) + 4 > 500 )
        return 0;
    header = Editor_CfgBlockHeader(block);
    if( header < 0 )
        return 0;
    count = Editor_CfgBlockFieldCount(block, key);
    if( occurrence > count )
        return 0;
    eol = eol_text(block->lines[header].eol);

    if( line >= 0 )
    {
        int earlier = 0;

        if( line <= header || line > block->line_count )
            return 0;
        for( int i = 0; i < line; i++ )
            earlier += line_is_key(block, i, key, key_length);
        if( earlier != occurrence )
            return 0;
        position = line;
    }
    else if( occurrence < count )
        position = Editor_CfgBlockFieldFind(block, key, occurrence);
    else
    {
        int last = -1;
        int last_field = -1;

        for( int i = header + 1; i < block->line_count; i++ )
        {
            if( line_is_key(block, i, key, key_length) )
                last = i;
            if( block->lines[i].kind == EDITOR_CFG_LINE_FIELD )
                last_field = i;
        }
        position = (last >= 0 ? last : last_field >= 0 ? last_field : header) + 1;
    }

    if( position <= header )
        return 0; /* a field above the header is not this record's */
    insert_line_at(block, position, key, value, eol);
    return 1;
}

int
Editor_CfgBlockFieldRemove(
    struct Editor_CfgBlock* block,
    const char* key,
    int occurrence,
    int line)
{
    int index;
    const struct Editor_CfgLine* found;

    assert(block);
    assert(key);

    index = Editor_CfgBlockFieldFind(block, key, occurrence);
    if( index < 0 || (line >= 0 && index != line) )
        return 0;
    found = &block->lines[index];
    if( found->eol != EDITOR_CFG_EOL_NONE || index == 0 )
    {
        block_splice(block, found->start, line_after(block, index) - found->start, NULL, NULL, NULL);
        return 1;
    }
    {
        const struct Editor_CfgLine* before = &block->lines[index - 1];
        size_t from = before->start + before->length;

        block_splice(block, from, found->start + found->length - from, NULL, NULL, NULL);
    }
    return 1;
}

static int
name_valid(const char* name)
{
    return name[0] != '\0' && strchr(name, ']') == NULL && text_one_line(name);
}

int
Editor_CfgBlockRename(
    struct Editor_CfgBlock* block,
    const char* new_name)
{
    int header;
    const struct Editor_CfgLine* line;

    assert(block);
    assert(new_name);

    if( !name_valid(new_name) )
        return 0;
    header = Editor_CfgBlockHeader(block);
    if( header < 0 || !block->lines[header].closed )
        return 0;
    line = &block->lines[header];
    block_splice(block, line->start + 1, line->part, new_name, NULL, NULL);
    return 1;
}

int
Editor_CfgBlockAddHeader(
    struct Editor_CfgBlock* block,
    const char* name)
{
    assert(block);
    assert(name);

    if( !name_valid(name) || Editor_CfgBlockHeader(block) >= 0 )
        return 0;
    block_splice(block, 0, 0, "[", name, "]\n");
    return 1;
}

int
Editor_CfgBlockRemoveHeader(
    struct Editor_CfgBlock* block,
    const char* name)
{
    assert(block);
    assert(name);

    if( block->line_count == 0 || !header_names(block->text, &block->lines[0], name, strlen(name)) )
        return 0;
    for( int i = 1; i < block->line_count; i++ )
    {
        if( block->lines[i].kind == EDITOR_CFG_LINE_FIELD
            || block->lines[i].kind == EDITOR_CFG_LINE_OTHER
            || block->lines[i].kind == EDITOR_CFG_LINE_HEADER )
            return 0;
    }
    /* Only an LF-ended header is what AddHeader puts back byte for byte. */
    if( block->lines[0].eol != EDITOR_CFG_EOL_LF )
        return 0;
    block_splice(block, 0, line_after(block, 0), NULL, NULL, NULL);
    return 1;
}

/* ---- keys -------------------------------------------------------------------- */

int
Editor_CfgKeySplit(
    const char* key,
    char* out_type,
    size_t type_size,
    char* out_name,
    size_t name_size)
{
    const char* colon;
    size_t type_length;
    size_t name_length;

    assert(key);
    assert(out_type);
    assert(out_name);

    colon = strchr(key, ':');
    if( !colon )
        return 0;
    type_length = (size_t)(colon - key);
    /* An overlay key's name stops at its '@' (Editor_CfgKeyOverlay). */
    name_length = strchr(colon + 1, '@') ? (size_t)(strchr(colon + 1, '@') - (colon + 1)) : strlen(colon + 1);
    if( type_length == 0 || type_length + 1 > type_size || name_length == 0
        || name_length + 1 > name_size )
        return 0;
    memcpy(out_type, key, type_length);
    out_type[type_length] = '\0';
    memcpy(out_name, colon + 1, name_length);
    out_name[name_length] = '\0';
    return 1;
}

const char*
Editor_CfgKeyOverlay(const char* key)
{
    const char* colon;
    const char* at;

    assert(key);
    colon = strchr(key, ':');
    if( !colon )
        return NULL;
    at = strchr(colon + 1, '@');
    return at ? at + 1 : NULL;
}

int
Editor_CfgOverlayKey(
    char out[128],
    const char* type,
    const char* name,
    const char* overlay)
{
    int written;

    assert(out);
    assert(type);
    assert(name);
    if( !overlay || !overlay[0] )
        written = snprintf(out, 128, "%s:%s", type, name);
    else
        written = snprintf(out, 128, "%s:%s@%s", type, name, overlay);
    return written > 0 && written < 128;
}

/* ---- a record across its layers ------------------------------------------- */

static void
trim_span(
    const char** begin,
    const char** end)
{
    while( *begin < *end && (**begin == ' ' || **begin == '\t') )
        (*begin)++;
    while( *end > *begin && ((*end)[-1] == ' ' || (*end)[-1] == '\t' || (*end)[-1] == '\r') )
        (*end)--;
}

int
Editor_CfgRegisterParse(
    const char* type,
    const char* text,
    size_t length,
    struct Editor_CfgRegisterField* out,
    int max)
{
    const char* p = text;
    const char* end = text + length;
    size_t type_length;
    int count = 0;
    int current = -1;

    assert(type);
    assert(text || length == 0);
    assert(out || max == 0);

    type_length = strlen(type);
    while( p < end )
    {
        const char* nl = memchr(p, '\n', (size_t)(end - p));
        const char* le = nl ? nl : end;
        const char* b = p;
        const char* e = le;

        trim_span(&b, &e);
        if( b < e && *b == '[' )
        {
            const char* close = memchr(b, ']', (size_t)(e - b));
            current = -1;
            if( close && (size_t)(close - b - 1) > type_length + 1 && memcmp(b + 1, type, type_length) == 0
                && b[1 + type_length] == '.' )
            {
                const char* k = b + 2 + type_length;
                size_t klen = (size_t)(close - k);
                if( klen > 0 && klen < EDITOR_CMD_NAME_MAX )
                {
                    for( int i = 0; i < count; i++ )
                        if( strlen(out[i].key) == klen && memcmp(out[i].key, k, klen) == 0 )
                            current = i;
                    if( current < 0 && count < max )
                    {
                        current = count++;
                        memset(&out[current], 0, sizeof(out[current]));
                        memcpy(out[current].key, k, klen);
                        out[current].key[klen] = '\0';
                        snprintf(out[current].scope, sizeof(out[current].scope), "server");
                    }
                }
            }
        }
        else if( current >= 0 && b < e && *b != ';' && *b != '#' )
        {
            const char* eq = memchr(b, '=', (size_t)(e - b));
            if( eq )
            {
                const char* kb = b;
                const char* ke = eq;
                const char* vb = eq + 1;
                const char* ve = e;
                const char* semi;
                trim_span(&kb, &ke);
                semi = memchr(vb, ';', (size_t)(ve - vb));
                if( semi )
                    ve = semi;
                trim_span(&vb, &ve);
                if( (size_t)(ke - kb) == 5 && memcmp(kb, "scope", 5) == 0 && (size_t)(ve - vb) < sizeof(out[0].scope) )
                    snprintf(out[current].scope, sizeof(out[current].scope), "%.*s", (int)(ve - vb), vb);
                else if( (size_t)(ke - kb) == 4 && memcmp(kb, "text", 4) == 0 )
                {
                    out[current].text_param = (size_t)(ve - vb) == 5 && memcmp(vb, "param", 5) == 0;
                    out[current].text_indexed = (size_t)(ve - vb) == 7 && memcmp(vb, "indexed", 7) == 0;
                }
                else if( (size_t)(ke - kb) == 6 && memcmp(kb, "values", 6) == 0
                         && (size_t)(ve - vb) < sizeof(out[0].values) )
                    snprintf(out[current].values, sizeof(out[current].values), "%.*s", (int)(ve - vb), vb);
                else if( (size_t)(ke - kb) == 4 && memcmp(kb, "type", 4) == 0 && (size_t)(ve - vb) < sizeof(out[0].type) )
                    snprintf(out[current].type, sizeof(out[current].type), "%.*s", (int)(ve - vb), vb);
                else if( (size_t)(ke - kb) == 3 && memcmp(kb, "ref", 3) == 0 && (size_t)(ve - vb) < sizeof(out[0].ref) )
                    snprintf(out[current].ref, sizeof(out[current].ref), "%.*s", (int)(ve - vb), vb);
                else if( (size_t)(ke - kb) == 6 && memcmp(kb, "client", 6) == 0
                         && (size_t)(ve - vb) < sizeof(out[0].client) )
                    snprintf(out[current].client, sizeof(out[current].client), "%.*s", (int)(ve - vb), vb);
                else if( (size_t)(ke - kb) == 6 && memcmp(kb, "server", 6) == 0
                         && (size_t)(ve - vb) < sizeof(out[0].server) )
                    snprintf(out[current].server, sizeof(out[current].server), "%.*s", (int)(ve - vb), vb);
                else if( (size_t)(ke - kb) == 5 && memcmp(kb, "param", 5) == 0
                         && (size_t)(ve - vb) < sizeof(out[0].param) )
                    snprintf(out[current].param, sizeof(out[current].param), "%.*s", (int)(ve - vb), vb);
            }
        }
        p = nl ? nl + 1 : end;
    }
    return count;
}

/* ---- the register's types (workbench 6.6) ---------------------------------- */

const char*
Editor_CfgFieldKindName(int kind)
{
    static const char* const k_names[] = { "text", "number", "bool", "name", "enum", "param" };
    assert(kind >= 0);
    assert(kind <= EDITOR_CFG_FIELD_PARAM);
    return k_names[kind];
}

int
Editor_CfgRegisterFind(
    const struct Editor_CfgRegisterField* fields,
    int field_count,
    const char* key)
{
    assert(fields || field_count == 0);
    assert(key);
    for( int i = 0; i < field_count; i++ )
        if( strcmp(fields[i].key, key) == 0 )
            return i;
    return -1;
}

/** The config types a `type =` (or `ref =`) may name that the catalog
 *  indexes as records (torirs_maped_catalog.h's config kinds). */
static int
cfg_config_type(const char* type)
{
    static const char* const k_types[] = { "loc", "npc", "obj", "seq", "spotanim", "varp", "varbit", "varc", "varn",
        "vars", "struct", "enum", "param", "inv", "dbrow", "dbtable", "idk", "underlay", "overlay", "hitsplat",
        "healthbar", "mapelement", "category", "model", "synth", "interface", "stat" };
    for( size_t i = 0; i < sizeof(k_types) / sizeof(k_types[0]); i++ )
        if( strcmp(type, k_types[i]) == 0 )
            return 1;
    return 0;
}

const char*
Editor_CfgRegisterRefType(const struct Editor_CfgRegisterField* field)
{
    assert(field);
    if( field->ref[0] && cfg_config_type(field->ref) )
        return field->ref;
    if( field->type[0] && !strchr(field->type, ',') && cfg_config_type(field->type) )
        return field->type;
    return "";
}

/** The band width of `server = opcode:<n>:<w>`: 1, 2, 4, or 0. */
static int
cfg_server_width(const char* server)
{
    const char* last;

    if( strncmp(server, "opcode:", 7) != 0 )
        return 0;
    last = strrchr(server, ':');
    if( strcmp(last, ":u1") == 0 )
        return 1;
    if( strcmp(last, ":u2") == 0 )
        return 2;
    if( strcmp(last, ":u4") == 0 )
        return 4;
    return 0;
}

int
Editor_CfgRegisterValueKind(const struct Editor_CfgRegisterField* field)
{
    assert(field);
    if( field->values[0] )
        return EDITOR_CFG_FIELD_ENUM;
    if( strcmp(field->type, "boolean") == 0 )
        return EDITOR_CFG_FIELD_BOOL;
    if( Editor_CfgRegisterRefType(field)[0] )
        return EDITOR_CFG_FIELD_NAME;
    if( strcmp(field->type, "int") == 0 || cfg_server_width(field->server) )
        return EDITOR_CFG_FIELD_NUMBER;
    return EDITOR_CFG_FIELD_TEXT;
}

int
Editor_CfgRegisterRange(
    const struct Editor_CfgRegisterField* field,
    long long* out_min,
    long long* out_max)
{
    int width;

    assert(field);
    assert(out_min);
    assert(out_max);
    width = cfg_server_width(field->server);
    *out_min = 0;
    if( width == 1 )
        *out_max = 255;
    else if( width == 2 )
        *out_max = 65535;
    else if( width == 4 )
        *out_max = 4294967295LL;
    else if( strcmp(field->type, "int") == 0 )
    {
        *out_min = -2147483648LL;
        *out_max = 2147483647LL;
    }
    else
        return 0;
    return 1;
}

int
Editor_CfgRegisterWords(
    const struct Editor_CfgRegisterField* field,
    char out[][32],
    int max)
{
    const char* p;
    int count = 0;

    assert(field);
    assert(out || max == 0);
    p = field->values;
    while( *p && count < max )
    {
        const char* b = p;
        const char* e;
        const char* comma = strchr(p, ',');
        const char* colon;

        e = comma ? comma : p + strlen(p);
        p = comma ? comma + 1 : e;
        trim_span(&b, &e);
        colon = b < e ? memchr(b, ':', (size_t)(e - b)) : NULL;
        if( colon )
            e = colon;
        trim_span(&b, &e);
        if( b < e && (size_t)(e - b) < 32 )
        {
            memcpy(out[count], b, (size_t)(e - b));
            out[count][e - b] = '\0';
            count++;
        }
    }
    return count;
}

int
Editor_CfgRegisterClientSide(const struct Editor_CfgRegisterField* field)
{
    assert(field);
    return strcmp(field->scope, "client") == 0 || strcmp(field->client, "native") == 0
           || strncmp(field->client, "param:", 6) == 0;
}

const char*
Editor_CfgRowIssueText(int issue)
{
    static const char* const k_texts[] = { "", "unknown key", "unknown param", "not a number", "out of range",
        "not yes or no", "not one of its words" };
    assert(issue >= 0);
    assert(issue <= EDITOR_CFG_ISSUE_NOT_WORD);
    return k_texts[issue];
}

/** `text` is a whole decimal integer (an optional '-'). */
static int
cfg_integer(
    const char* text,
    long long* out)
{
    const char* p = text;
    long long value = 0;
    int negative = 0;

    if( *p == '-' )
    {
        negative = 1;
        p++;
    }
    if( *p < '0' || *p > '9' )
        return 0;
    while( *p >= '0' && *p <= '9' )
    {
        if( value > 999999999999LL )
            return 0;
        value = value * 10 + (*p - '0');
        p++;
    }
    if( *p )
        return 0;
    *out = negative ? -value : value;
    return 1;
}

static int
cfg_bool_word(const char* text)
{
    return strcmp(text, "yes") == 0 || strcmp(text, "no") == 0 || strcmp(text, "true") == 0
           || strcmp(text, "false") == 0 || strcmp(text, "1") == 0 || strcmp(text, "0") == 0;
}

int
Editor_CfgValueIssue(
    const struct Editor_CfgRegisterField* field,
    int kind,
    const char* value)
{
    long long number;

    assert(field);
    assert(value);
    if( kind == EDITOR_CFG_FIELD_NUMBER )
    {
        long long lo;
        long long hi;
        if( !cfg_integer(value, &number) )
            return EDITOR_CFG_ISSUE_NOT_NUMBER;
        if( Editor_CfgRegisterRange(field, &lo, &hi) && (number < lo || number > hi) )
            return EDITOR_CFG_ISSUE_OUT_OF_RANGE;
        return EDITOR_CFG_ISSUE_NONE;
    }
    if( kind == EDITOR_CFG_FIELD_BOOL )
        return cfg_bool_word(value) ? EDITOR_CFG_ISSUE_NONE : EDITOR_CFG_ISSUE_NOT_BOOL;
    if( kind == EDITOR_CFG_FIELD_ENUM )
    {
        char words[16][32];
        int const count = Editor_CfgRegisterWords(field, words, 16);
        if( cfg_integer(value, &number) )
            return EDITOR_CFG_ISSUE_NONE;
        for( int i = 0; i < count; i++ )
            if( strcmp(words[i], value) == 0 )
                return EDITOR_CFG_ISSUE_NONE;
        return EDITOR_CFG_ISSUE_NOT_WORD;
    }
    return EDITOR_CFG_ISSUE_NONE;
}

/** The register field an INDEXED key (patrol2) belongs to, or -1. */
static int
cfg_indexed_field(
    const struct Editor_CfgRegisterField* fields,
    int field_count,
    const char* key)
{
    size_t length = strlen(key);
    size_t digits = 0;

    while( digits < length && key[length - 1 - digits] >= '0' && key[length - 1 - digits] <= '9' )
        digits++;
    if( digits == 0 || digits == length )
        return -1;
    for( int i = 0; i < field_count; i++ )
        if( fields[i].text_indexed && strlen(fields[i].key) == length - digits
            && memcmp(fields[i].key, key, length - digits) == 0 )
            return i;
    return -1;
}

/** The register field that names param `sub`: a field of that key, or one
 *  whose param binding (`param = p`, `client = param:p`) is `sub`. */
static int
cfg_param_field(
    const struct Editor_CfgRegisterField* fields,
    int field_count,
    const char* sub)
{
    int at = Editor_CfgRegisterFind(fields, field_count, sub);
    if( at >= 0 )
        return at;
    for( int i = 0; i < field_count; i++ )
        if( strcmp(fields[i].param, sub) == 0
            || (strncmp(fields[i].client, "param:", 6) == 0 && strcmp(fields[i].client + 6, sub) == 0) )
            return i;
    return -1;
}

/** A value typed by what it holds (a key the register does not list). */
static int
cfg_guess_kind(const char* value)
{
    long long number;

    if( cfg_integer(value, &number) )
        return EDITOR_CFG_FIELD_NUMBER;
    if( strcmp(value, "yes") == 0 || strcmp(value, "no") == 0 || strcmp(value, "true") == 0
        || strcmp(value, "false") == 0 )
        return EDITOR_CFG_FIELD_BOOL;
    return EDITOR_CFG_FIELD_TEXT;
}

int
Editor_CfgRecordType(
    struct Editor_CfgRecord* record,
    const struct Editor_CfgRegisterField* fields,
    int field_count,
    const char* const* known_params,
    int known_count)
{
    int issues = 0;

    assert(record);
    assert(fields || field_count == 0);
    assert(known_params || known_count == 0);
    for( int r = 0; r < record->row_count; r++ )
    {
        struct Editor_CfgRecordRow* row = &record->rows[r];
        const struct Editor_CfgRegisterField* field;
        int const param = row->sub[0] != '\0';

        row->reg = param ? cfg_param_field(fields, field_count, row->sub)
                         : Editor_CfgRegisterFind(fields, field_count, row->key);
        if( row->reg < 0 && !param )
            row->reg = cfg_indexed_field(fields, field_count, row->key);
        field = row->reg >= 0 ? &fields[row->reg] : NULL;
        if( field )
            row->value_kind = (unsigned char)Editor_CfgRegisterValueKind(field);
        else
            row->value_kind = (unsigned char)(row->layer < 0 ? EDITOR_CFG_FIELD_TEXT : cfg_guess_kind(row->value));
        row->kind = param ? (unsigned char)EDITOR_CFG_FIELD_PARAM : row->value_kind;
        row->client_side = (row->layer >= 0 && record->layers[row->layer].rank == 0)
                           || (field && Editor_CfgRegisterClientSide(field));
        row->issue = EDITOR_CFG_ISSUE_NONE;
        if( row->in_overlay && !field && !param )
            row->issue = EDITOR_CFG_ISSUE_UNKNOWN_KEY;
        else if( row->in_overlay && !field )
        {
            int known = 0;
            for( int k = 0; k < known_count && !known; k++ )
                known = strcmp(known_params[k], row->sub) == 0;
            if( !known )
                row->issue = EDITOR_CFG_ISSUE_UNKNOWN_PARAM;
        }
        else if( row->layer >= 0 && field )
            row->issue = (unsigned char)Editor_CfgValueIssue(field, row->value_kind, row->value);
        issues += row->issue != EDITOR_CFG_ISSUE_NONE;
    }
    return issues;
}

void
Editor_CfgRecordRowName(
    const struct Editor_CfgRecordRow* row,
    char* out,
    size_t out_size)
{
    assert(row);
    assert(out);
    assert(out_size > 0);
    if( row->sub[0] )
        snprintf(out, out_size, "param:%s", row->sub);
    else if( row->occurrence > 0 )
        snprintf(out, out_size, "%s#%d", row->key, row->occurrence);
    else
        snprintf(out, out_size, "%s", row->key);
}

/** Case-insensitive: the `length` bytes at `needle` somewhere in `hay`. */
static int
cfg_contains_ci(
    const char* hay,
    const char* needle,
    size_t length)
{
    for( const char* h = hay; *h; h++ )
    {
        size_t i = 0;
        while( i < length && h[i] )
        {
            char a = h[i];
            char b = needle[i];
            if( a >= 'A' && a <= 'Z' )
                a = (char)(a - 'A' + 'a');
            if( b >= 'A' && b <= 'Z' )
                b = (char)(b - 'A' + 'a');
            if( a != b )
                break;
            i++;
        }
        if( i == length )
            return 1;
    }
    return 0;
}

/**
 * The words a person looks a field up by that its key does not spell: a
 * clone's examine text, its right-click options, its colours. Typing
 * "examine" in the Record panel's Find listed nothing -- the examine text is
 * `desc` (2026-11-11). "" for a key with none.
 */
static const char*
cfg_field_words(const char* key)
{
    if( strcmp(key, "desc") == 0 )
        return "examine description";
    if( strcmp(key, "name") == 0 )
        return "display";
    /* op1..op5, iop1..iop5: not `opcode129` nor `subop`. */
    if( (key[0] == 'o' && key[1] == 'p' && key[2] >= '1' && key[2] <= '9')
        || (key[0] == 'i' && key[1] == 'o' && key[2] == 'p' && key[3] >= '1' && key[3] <= '9') )
        return "option action menu";
    if( strncmp(key, "recol", 5) == 0 )
        return "recolour colour color";
    if( strncmp(key, "retex", 5) == 0 )
        return "retexture texture";
    return "";
}

int
Editor_CfgRecordRowMatches(
    const struct Editor_CfgRecord* record,
    int row,
    const char* query)
{
    const struct Editor_CfgRecordRow* r;
    char name[EDITOR_CMD_NAME_MAX * 2];
    const char* layer = "";
    const char* p;

    assert(record);
    assert(row >= 0);
    assert(row < record->row_count);
    assert(query);
    r = &record->rows[row];
    Editor_CfgRecordRowName(r, name, sizeof(name));
    if( r->layer >= 0 )
    {
        const char* slash = strrchr(record->layers[r->layer].label, '/');
        layer = slash ? slash + 1 : record->layers[r->layer].label;
    }
    p = query;
    for( ;; )
    {
        const char* b;
        size_t length;

        while( *p == ' ' || *p == '\t' )
            p++;
        if( !*p )
            return 1;
        b = p;
        while( *p && *p != ' ' && *p != '\t' )
            p++;
        length = (size_t)(p - b);
        if( !cfg_contains_ci(name, b, length) && !cfg_contains_ci(r->value, b, length)
            && !cfg_contains_ci(layer, b, length) && !cfg_contains_ci(Editor_CfgFieldKindName(r->kind), b, length)
            && !cfg_contains_ci(cfg_field_words(r->key), b, length) )
            return 0;
    }
}

int
Editor_CfgRecordRowChanged(
    const struct Editor_CfgRecord* record,
    int row,
    const struct Editor_CfgRecord* base)
{
    const struct Editor_CfgRecordRow* r;
    int at;

    assert(record);
    assert(base);
    assert(row >= 0);
    assert(row < record->row_count);
    r = &record->rows[row];
    at = Editor_CfgRecordFindRow(base, r->key, r->sub, r->occurrence);
    if( at < 0 || base->rows[at].layer < 0 )
        return r->layer >= 0;
    if( r->layer < 0 )
        return 1;
    return strcmp(base->rows[at].value, r->value) != 0
           || strcmp(base->layers[base->rows[at].layer].doc_key, record->layers[r->layer].doc_key) != 0;
}

int
Editor_CfgRecordRemovedSince(
    const struct Editor_CfgRecord* record,
    const struct Editor_CfgRecord* base)
{
    int count = 0;

    assert(record);
    assert(base);
    for( int i = 0; i < base->row_count; i++ )
        if( base->rows[i].layer >= 0
            && Editor_CfgRecordFindRow(record, base->rows[i].key, base->rows[i].sub, base->rows[i].occurrence) < 0 )
            count++;
    return count;
}

void
Editor_CfgRecordInit(
    struct Editor_CfgRecord* record,
    const char* type,
    const char* name)
{
    assert(record);
    assert(type);
    assert(name);
    memset(record, 0, sizeof(*record));
    snprintf(record->type, sizeof(record->type), "%s", type);
    snprintf(record->name, sizeof(record->name), "%s", name);
}

void
Editor_CfgRecordFree(struct Editor_CfgRecord* record)
{
    if( !record )
        return;
    for( int i = 0; i < record->layer_count; i++ )
        Editor_CfgBlockFree(&record->layers[i].block);
    ToriMem_Free(ToriMem_Editor(), record->rows);
    memset(record, 0, sizeof(*record));
}

int
Editor_CfgLayerRank(const char* overlay)
{
    const char* base;

    if( !overlay || !overlay[0] )
        return 0;
    base = strrchr(overlay, '/');
    base = base ? base + 1 : overlay;
    if( strstr(overlay, "ported/") || strstr(base, ".generated.") )
        return 1;
    return 2;
}

int
Editor_CfgRecordAddLayer(
    struct Editor_CfgRecord* record,
    const char* overlay,
    const char* text,
    size_t length)
{
    struct Editor_CfgRecordLayer layer;
    int at;

    assert(record);
    assert(text || length == 0);

    if( record->layer_count >= EDITOR_CFG_RECORD_LAYERS_MAX )
        return 0;
    memset(&layer, 0, sizeof(layer));
    if( !Editor_CfgOverlayKey(layer.doc_key, record->type, record->name, overlay) )
        return 0;
    if( overlay && overlay[0] )
        snprintf(layer.label, sizeof(layer.label), "%s", overlay);
    else
        snprintf(layer.label, sizeof(layer.label), "configs/all.%s", record->type);
    layer.rank = Editor_CfgLayerRank(overlay);
    Editor_CfgBlockInit(&layer.block);
    Editor_CfgBlockSet(&layer.block, text ? text : "", length);
    layer.present = Editor_CfgBlockNameIs(&layer.block, record->name);

    /* Merge order: rank, then path (the walk is sorted). */
    at = record->layer_count;
    while( at > 0 )
    {
        const struct Editor_CfgRecordLayer* prev = &record->layers[at - 1];
        if( prev->rank < layer.rank || (prev->rank == layer.rank && strcmp(prev->label, layer.label) <= 0) )
            break;
        at--;
    }
    memmove(&record->layers[at + 1], &record->layers[at],
        (size_t)(record->layer_count - at) * sizeof(record->layers[0]));
    record->layers[at] = layer;
    record->layer_count++;
    return 1;
}

int
Editor_CfgRecordFindLayer(
    const struct Editor_CfgRecord* record,
    const char* doc_key)
{
    assert(record);
    assert(doc_key);
    for( int i = 0; i < record->layer_count; i++ )
        if( strcmp(record->layers[i].doc_key, doc_key) == 0 )
            return i;
    return -1;
}

/** A FIELD line's key, value core (no trailing comment) and param name. */
static void
record_line_parts(
    const struct Editor_CfgBlock* block,
    int line,
    char* key,
    char* sub,
    char* value)
{
    const struct Editor_CfgLine* l = &block->lines[line];
    const char* text = block->text + l->start;
    size_t value_length = 0;
    const char* v = Editor_CfgBlockValue(block, line, &value_length);
    const char* ve = v + value_length;
    const char* vb = v;
    size_t klen = l->part < EDITOR_CMD_NAME_MAX ? l->part : EDITOR_CMD_NAME_MAX - 1;

    memcpy(key, text, klen);
    key[klen] = '\0';
    for( const char* c = vb; c + 1 < ve; c++ )
        if( c[0] == '/' && c[1] == '/' && (c == vb || c[-1] == ' ' || c[-1] == '\t') )
        {
            ve = c;
            break;
        }
    trim_span(&vb, &ve);
    sub[0] = '\0';
    if( strcmp(key, "param") == 0 )
    {
        const char* comma = memchr(vb, ',', (size_t)(ve - vb));
        if( comma && (size_t)(comma - vb) < EDITOR_CMD_NAME_MAX )
        {
            memcpy(sub, vb, (size_t)(comma - vb));
            sub[comma - vb] = '\0';
            vb = comma + 1;
        }
    }
    snprintf(value, EDITOR_CMD_VALUE_MAX, "%.*s", (int)(ve - vb), vb);
}

int
Editor_CfgRecordFindRow(
    const struct Editor_CfgRecord* record,
    const char* key,
    const char* sub,
    int occurrence)
{
    assert(record);
    assert(key);
    assert(sub);
    for( int i = 0; i < record->row_count; i++ )
    {
        const struct Editor_CfgRecordRow* row = &record->rows[i];
        if( row->occurrence == occurrence && strcmp(row->key, key) == 0 && strcmp(row->sub, sub) == 0 )
            return i;
    }
    return -1;
}

static struct Editor_CfgRecordRow*
record_row_add(struct Editor_CfgRecord* record)
{
    if( record->row_count == record->row_capacity )
    {
        int next = record->row_capacity ? record->row_capacity * 2 : 128;
        record->rows = ToriMem_Realloc(ToriMem_Editor(), record->rows, (size_t)next * sizeof(*record->rows));
        assert(record->rows);
        record->row_capacity = next;
    }
    memset(&record->rows[record->row_count], 0, sizeof(record->rows[0]));
    return &record->rows[record->row_count++];
}

/** Statement `line` of a layer is which occurrence of its (key, sub)? */
static int
record_occurrence(
    const struct Editor_CfgBlock* block,
    int line,
    const char* key,
    const char* sub)
{
    int n = 0;
    char k[EDITOR_CMD_NAME_MAX];
    char s[EDITOR_CMD_NAME_MAX];
    char v[EDITOR_CMD_VALUE_MAX];

    for( int i = 0; i < line; i++ )
    {
        if( block->lines[i].kind != EDITOR_CFG_LINE_FIELD )
            continue;
        record_line_parts(block, i, k, s, v);
        n += strcmp(k, key) == 0 && strcmp(s, sub) == 0;
    }
    return n;
}

void
Editor_CfgRecordResolve(
    struct Editor_CfgRecord* record,
    const struct Editor_CfgRegisterField* fields,
    int field_count)
{
    assert(record);
    assert(fields || field_count == 0);

    record->row_count = 0;
    for( int l = 0; l < record->layer_count; l++ )
    {
        const struct Editor_CfgBlock* block = &record->layers[l].block;
        if( !record->layers[l].present )
            continue;
        for( int i = 0; i < block->line_count; i++ )
        {
            char key[EDITOR_CMD_NAME_MAX];
            char sub[EDITOR_CMD_NAME_MAX];
            char value[EDITOR_CMD_VALUE_MAX];
            int occurrence;
            int at;

            if( block->lines[i].kind != EDITOR_CFG_LINE_FIELD )
                continue;
            record_line_parts(block, i, key, sub, value);
            occurrence = record_occurrence(block, i, key, sub);
            at = Editor_CfgRecordFindRow(record, key, sub, occurrence);
            if( at < 0 )
            {
                struct Editor_CfgRecordRow* row = record_row_add(record);
                snprintf(row->key, sizeof(row->key), "%s", key);
                snprintf(row->sub, sizeof(row->sub), "%s", sub);
                row->occurrence = occurrence;
                snprintf(row->value, sizeof(row->value), "%s", value);
                row->layer = l;
                row->in_overlay = record->layers[l].rank > 0;
            }
            else
            {
                /* Layers run in merge order: a later statement wins. */
                struct Editor_CfgRecordRow* row = &record->rows[at];
                snprintf(row->value, sizeof(row->value), "%s", value);
                if( row->layer != l )
                    row->shadowed++;
                row->layer = l;
                row->in_overlay |= record->layers[l].rank > 0;
            }
        }
    }
    for( int f = 0; f < field_count; f++ )
    {
        const struct Editor_CfgRegisterField* field = &fields[f];
        const char* key = field->text_param ? "param" : field->key;
        const char* sub = field->text_param ? field->key : "";
        int at = Editor_CfgRecordFindRow(record, key, sub, 0);
        if( field->text_indexed )
        {
            /* patrol1..N: the record's own statements are rows already; the
             * register's row is the first one, to add from. */
            char first[EDITOR_CMD_NAME_MAX];
            snprintf(first, sizeof(first), "%s1", field->key);
            if( Editor_CfgRecordFindRow(record, first, "", 0) < 0 && at < 0 )
                at = -2;
            if( at == -2 )
            {
                struct Editor_CfgRecordRow* row = record_row_add(record);
                snprintf(row->key, sizeof(row->key), "%s", first);
                row->layer = -1;
                snprintf(row->scope, sizeof(row->scope), "%s", field->scope);
                continue;
            }
        }
        if( at < 0 )
        {
            struct Editor_CfgRecordRow* row = record_row_add(record);
            snprintf(row->key, sizeof(row->key), "%s", key);
            snprintf(row->sub, sizeof(row->sub), "%s", sub);
            row->layer = -1;
            at = record->row_count - 1;
        }
        snprintf(record->rows[at].scope, sizeof(record->rows[at].scope), "%s", field->scope);
    }
    (void)Editor_CfgRecordType(record, fields, field_count, NULL, 0);
}

int
Editor_CfgRecordAddLayerFor(const struct Editor_CfgRecord* record)
{
    assert(record);
    for( int l = record->layer_count - 1; l >= 0; l-- )
        if( record->layers[l].present )
            return l;
    return 0;
}

int
Editor_CfgRecordWriteLayerFor(
    const struct Editor_CfgRecord* record,
    int row)
{
    assert(record);
    if( row >= 0 && row < record->row_count )
        for( int l = record->layer_count - 1; l >= 0; l-- )
            if( record->layers[l].rank == 2 && Editor_CfgRecordLayerStates(record, l, row, NULL, 0) >= 0 )
                return l;
    for( int l = record->layer_count - 1; l >= 0; l-- )
        if( record->layers[l].rank == 2 && record->layers[l].present )
            return l;
    for( int l = record->layer_count - 1; l >= 0; l-- )
        if( record->layers[l].rank == 2 )
            return l;
    return -1;
}

/** The line of (key, sub, occurrence) in a block, its index among the
 *  block's `key` lines, and its raw value; -1 when absent. */
static int
record_find_line(
    const struct Editor_CfgBlock* block,
    const char* key,
    const char* sub,
    int occurrence,
    int* out_key_index,
    char* out_raw,
    size_t raw_size)
{
    int key_index = 0;
    int seen = 0;

    for( int i = 0; i < block->line_count; i++ )
    {
        char k[EDITOR_CMD_NAME_MAX];
        char s[EDITOR_CMD_NAME_MAX];
        char v[EDITOR_CMD_VALUE_MAX];
        if( block->lines[i].kind != EDITOR_CFG_LINE_FIELD )
            continue;
        record_line_parts(block, i, k, s, v);
        if( strcmp(k, key) != 0 )
            continue;
        if( strcmp(s, sub) == 0 && seen++ == occurrence )
        {
            size_t length = 0;
            const char* raw = Editor_CfgBlockValue(block, i, &length);
            if( length >= raw_size )
                return -2;
            memcpy(out_raw, raw, length);
            out_raw[length] = '\0';
            *out_key_index = key_index;
            return i;
        }
        key_index++;
    }
    *out_key_index = key_index;
    return -1;
}

int
Editor_CfgRecordLayerStates(
    const struct Editor_CfgRecord* record,
    int layer,
    int row,
    char* out_value,
    size_t value_size)
{
    const struct Editor_CfgRecordRow* r;
    const struct Editor_CfgBlock* block;
    int seen = 0;

    assert(record);
    assert(layer >= 0);
    assert(layer < record->layer_count);
    assert(row >= 0);
    assert(row < record->row_count);
    assert(out_value || value_size == 0);
    r = &record->rows[row];
    if( !record->layers[layer].present )
        return -1;
    block = &record->layers[layer].block;
    for( int i = 0; i < block->line_count; i++ )
    {
        char k[EDITOR_CMD_NAME_MAX];
        char s[EDITOR_CMD_NAME_MAX];
        char v[EDITOR_CMD_VALUE_MAX];
        if( block->lines[i].kind != EDITOR_CFG_LINE_FIELD )
            continue;
        record_line_parts(block, i, k, s, v);
        if( strcmp(k, r->key) != 0 || strcmp(s, r->sub) != 0 || seen++ != r->occurrence )
            continue;
        if( value_size > 0 )
            snprintf(out_value, value_size, "%s", v);
        return i - (Editor_CfgBlockHeader(block) > 0 ? Editor_CfgBlockHeader(block) : 0);
    }
    return -1;
}

int
Editor_CfgRecordFieldCommands(
    const struct Editor_CfgRecord* record,
    const char* key,
    const char* sub,
    int occurrence,
    const char* value,
    int layer,
    struct Editor_Cmd* out,
    int max)
{
    const struct Editor_CfgRecordLayer* target;
    char raw[EDITOR_CMD_VALUE_MAX];
    char want[EDITOR_CMD_VALUE_MAX];
    char spec[EDITOR_CMD_NAME_MAX];
    int key_index = 0;
    int line;
    int count = 0;
    int written;

    assert(record);
    assert(key);
    assert(sub);
    assert(value);
    assert(out || max == 0);

    if( layer < 0 )
    {
        int row = Editor_CfgRecordFindRow(record, key, sub, occurrence);
        layer = row >= 0 && record->rows[row].layer >= 0 ? record->rows[row].layer
                                                         : Editor_CfgRecordAddLayerFor(record);
    }
    if( layer >= record->layer_count || !key[0] || strchr(value, '\n') || strchr(value, '\r') )
        return -1;
    if( strcmp(key, "param") == 0 && !sub[0] )
        return -1;
    target = &record->layers[layer];

    line = target->present ? record_find_line(&target->block, key, sub, occurrence, &key_index, raw, sizeof(raw))
                           : -1;
    if( line == -2 )
        return -1;

    /* The new raw value: a param keeps its name, a line keeps its comment. */
    if( value[0] )
    {
        const char* comment = "";
        if( line >= 0 )
        {
            const char* c = strstr(raw, "//");
            while( c && c > raw && c[-1] != ' ' && c[-1] != '\t' )
                c = strstr(c + 2, "//");
            if( c )
            {
                while( c > raw && (c[-1] == ' ' || c[-1] == '\t') )
                    c--;
                comment = c;
            }
        }
        written = sub[0] ? snprintf(want, sizeof(want), "%s,%s%s", sub, value, comment)
                         : snprintf(want, sizeof(want), "%s%s", value, comment);
        if( written < 0 || (size_t)written >= sizeof(want) )
            return -1;
    }
    else
        want[0] = '\0';

    if( line >= 0 )
    {
        if( strcmp(raw, want) == 0 )
            return 0;
        if( max < 1 )
            return -1;
        /* A removal names its line, so the undo puts it back there. */
        if( want[0] )
            written = key_index ? snprintf(spec, sizeof(spec), "%s#%d", key, key_index)
                                : snprintf(spec, sizeof(spec), "%s", key);
        else
            written = snprintf(spec, sizeof(spec), "%s#%d@%d", key, key_index, line);
        if( written < 0 || (size_t)written >= sizeof(spec) )
            return -1;
        Editor_CmdInit(&out[0], EDITOR_DOC_CFG, target->doc_key, EDITOR_CMD_CFG_FIELD);
        out[0].has_before = 1;
        out[0].has_after = want[0] != '\0';
        snprintf(out[0].u.cfg_field.key, sizeof(out[0].u.cfg_field.key), "%s", spec);
        snprintf(out[0].u.cfg_field.before, sizeof(out[0].u.cfg_field.before), "%s", raw);
        snprintf(out[0].u.cfg_field.after, sizeof(out[0].u.cfg_field.after), "%s", want);
        return 1;
    }

    if( !want[0] )
        return 0;
    if( max < (target->present ? 1 : 2) )
        return -1;
    if( !target->present )
    {
        /* "Override in <layer>": the record's block opens there first. */
        Editor_CmdInit(&out[count], EDITOR_DOC_CFG, target->doc_key, EDITOR_CMD_CFG_BLOCK);
        out[count].has_after = 1;
        snprintf(out[count].u.cfg_block.name, sizeof(out[count].u.cfg_block.name), "%s", record->name);
        count++;
        key_index = 0;
    }
    /* Appended as the key's next statement. */
    written = key_index ? snprintf(spec, sizeof(spec), "%s#%d", key, key_index)
                        : snprintf(spec, sizeof(spec), "%s", key);
    if( written < 0 || (size_t)written >= sizeof(spec) )
        return -1;
    Editor_CmdInit(&out[count], EDITOR_DOC_CFG, target->doc_key, EDITOR_CMD_CFG_FIELD);
    out[count].has_after = 1;
    snprintf(out[count].u.cfg_field.key, sizeof(out[count].u.cfg_field.key), "%s", spec);
    snprintf(out[count].u.cfg_field.after, sizeof(out[count].u.cfg_field.after), "%s", want);
    count++;
    return count;
}
