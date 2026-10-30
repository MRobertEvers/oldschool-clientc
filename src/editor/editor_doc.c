#include "editor_doc.h"
#include "editor_cfg.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- square ------------------------------------------------------------- */

void
Editor_SquareInit(
    struct Editor_Square* square,
    int map_x,
    int map_z)
{
    assert(square);

    memset(square, 0, sizeof(*square));
    square->map_x = map_x;
    square->map_z = map_z;
}

void
Editor_SquareFree(struct Editor_Square* square)
{
    if( !square )
        return;

    ToriMem_Free(ToriMem_Editor(), square->locs);
    ToriMem_Free(ToriMem_Editor(), square->trailing);
    ToriMem_Free(ToriMem_Editor(), square->foreign);
    memset(square, 0, sizeof(*square));
}

int
Editor_SquareLocAdd(
    struct Editor_Square* square,
    const struct Editor_Loc* loc)
{
    assert(square);
    assert(loc);

    if( square->loc_count == square->loc_capacity )
    {
        int next = square->loc_capacity ? square->loc_capacity * 2 : 256;
        square->locs =
            ToriMem_Realloc(ToriMem_Editor(), square->locs, (size_t)next * sizeof(*square->locs));
        square->loc_capacity = next;
    }

    square->locs[square->loc_count] = *loc;
    return square->loc_count++;
}

void
Editor_SquareLocRemoveAt(
    struct Editor_Square* square,
    int index)
{
    assert(square);
    assert(index >= 0);
    assert(index < square->loc_count);

    /* Order matters: the cache lists a square's locs sorted, and the client
     * walks them in file order. Compacting rather than swapping the tail in
     * keeps a save from reshuffling placements the user did not touch. */
    memmove(
        &square->locs[index],
        &square->locs[index + 1],
        (size_t)(square->loc_count - index - 1) * sizeof(*square->locs));
    square->loc_count--;
}

int
Editor_SquareLocFind(
    const struct Editor_Square* square,
    int level,
    int x,
    int z,
    int shape)
{
    assert(square);

    for( int i = 0; i < square->loc_count; i++ )
    {
        const struct Editor_Loc* loc = &square->locs[i];
        if( loc->level == level && loc->x == x && loc->z == z && loc->shape == shape )
            return i;
    }
    return -1;
}

/* ---- keys --------------------------------------------------------------- */

/** A path-ish tail: segments of [A-Za-z0-9_.-], no empty segment, no "..". */
static int
relative_path_valid(const char* path)
{
    const char* segment = path;

    if( !path[0] )
        return 0;
    for( const char* at = path;; at++ )
    {
        if( *at == '/' || *at == '\0' )
        {
            size_t length = (size_t)(at - segment);
            if( length == 0 )
                return 0;
            if( length == 2 && segment[0] == '.' && segment[1] == '.' )
                return 0;
            if( length == 1 && segment[0] == '.' )
                return 0;
            if( *at == '\0' )
                return 1;
            segment = at + 1;
            continue;
        }
        if( !isalnum((unsigned char)*at) && *at != '_' && *at != '-' && *at != '.' )
            return 0;
    }
}

static int
name_valid(const char* name)
{
    if( !name[0] )
        return 0;
    for( const char* at = name; *at; at++ )
    {
        if( !isalnum((unsigned char)*at) && *at != '_' && *at != '-' && *at != '.' )
            return 0;
    }
    return strcmp(name, "..") != 0 && strcmp(name, ".") != 0;
}

/** A config type or namespace: [a-z0-9_]+. */
static int
config_type_valid(
    const char* type,
    size_t length)
{
    if( length == 0 || length >= 32 )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        if( !islower((unsigned char)type[i]) && !isdigit((unsigned char)type[i]) && type[i] != '_' )
            return 0;
    }
    return 1;
}

/** "<ns>.alloc", "<ns>.client", "<ns>.server" or "<ns>.pack". */
static int
pack_file_valid(const char* file)
{
    static const char* const suffixes[] = { ".alloc", ".client", ".server", ".pack" };
    size_t length = strlen(file);

    for( size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++ )
    {
        size_t suffix_length = strlen(suffixes[i]);

        if( length > suffix_length && strcmp(file + length - suffix_length, suffixes[i]) == 0 )
            return config_type_valid(file, length - suffix_length);
    }
    return 0;
}

int
Editor_DocKeyValid(
    enum Editor_DocKind kind,
    const char* key)
{
    int map_x;
    int map_z;
    const char* colon;

    assert(key);

    if( strlen(key) >= EDITOR_DOC_KEY_MAX )
        return 0;

    switch( kind )
    {
    case EDITOR_DOC_SQUARE:
    case EDITOR_DOC_SPAWN:
        return Editor_SquareKeyParse(key, &map_x, &map_z);
    case EDITOR_DOC_CFG:
    {
        char type[32];
        size_t type_length;

        colon = strchr(key, ':');
        if( !colon )
            return 0;
        type_length = (size_t)(colon - key);
        if( type_length == 0 || type_length >= sizeof(type) )
            return 0;
        memcpy(type, key, type_length);
        type[type_length] = '\0';
        for( size_t i = 0; i < type_length; i++ )
        {
            if( !islower((unsigned char)type[i]) && !isdigit((unsigned char)type[i])
                && type[i] != '_' )
                return 0;
        }
        return name_valid(colon + 1);
    }
    case EDITOR_DOC_IFACE:
        return strncmp(key, "if:", 3) == 0 && name_valid(key + 3);
    case EDITOR_DOC_TEXT:
        if( strncmp(key, "rs2:", 4) == 0 )
            return relative_path_valid(key + 4);
        if( strncmp(key, "cs2:", 4) == 0 )
            return name_valid(key + 4);
        /* The id ledgers and membership lists an allocation appends to
         * (editor_alloc.h): pack/<ns>.{alloc,client,server,pack}. */
        if( strncmp(key, "pack:", 5) == 0 )
            return pack_file_valid(key + 5);
        /* A config type's member index, configs/all.<type>.compack. */
        if( strncmp(key, "compack:", 8) == 0 )
            return config_type_valid(key + 8, strlen(key + 8));
        return 0;
    }
    return 0;
}

int
Editor_DocStorageValid(const char* storage)
{
    size_t length;
    const char* lane;

    assert(storage);

    if( storage[0] == '\0' || strcmp(storage, "maps") == 0 )
        return 1;
    length = strlen(storage);
    if( length >= EDITOR_DOC_STORAGE_MAX || strncmp(storage, "ported/", 7) != 0 || length < 13 )
        return 0;
    if( strcmp(storage + length - 5, "/maps") != 0 )
        return 0;

    /* The lane: one name between "ported/" and "/maps". */
    lane = storage + 7;
    for( const char* at = lane; at < storage + length - 5; at++ )
    {
        if( !isalnum((unsigned char)*at) && *at != '_' && *at != '-' )
            return 0;
    }
    return storage + length - 5 > lane;
}

int
Editor_DocSplitSquareKey(
    const char* full_key,
    char out_storage[EDITOR_DOC_STORAGE_MAX],
    char out_key[EDITOR_DOC_KEY_MAX])
{
    const char* slash;
    size_t storage_length;

    assert(full_key);
    assert(out_storage);
    assert(out_key);

    out_storage[0] = '\0';
    out_key[0] = '\0';
    slash = strrchr(full_key, '/');
    if( !slash )
    {
        if( strlen(full_key) >= EDITOR_DOC_KEY_MAX )
            return 0;
        snprintf(out_key, EDITOR_DOC_KEY_MAX, "%s", full_key);
        return Editor_DocKeyValid(EDITOR_DOC_SQUARE, out_key);
    }

    storage_length = (size_t)(slash - full_key);
    if( storage_length >= EDITOR_DOC_STORAGE_MAX || strlen(slash + 1) >= EDITOR_DOC_KEY_MAX )
        return 0;
    memcpy(out_storage, full_key, storage_length);
    out_storage[storage_length] = '\0';
    snprintf(out_key, EDITOR_DOC_KEY_MAX, "%s", slash + 1);
    if( strcmp(out_storage, "maps") == 0 )
        out_storage[0] = '\0';
    return Editor_DocStorageValid(out_storage) && Editor_DocKeyValid(EDITOR_DOC_SQUARE, out_key);
}

/* ---- document ----------------------------------------------------------- */

void
Editor_DocInit(
    struct Editor_Doc* doc,
    const struct RSCache* profile)
{
    assert(doc);
    /* No profile assert: NULL means "this document never derives" — the
     * server's authoritative doc. See the field's comment in editor_doc.h;
     * Editor_SquareDeriveTerrain asserts its own profile argument. */

    memset(doc, 0, sizeof(*doc));
    doc->profile = profile;
}

void
Editor_DocFree(struct Editor_Doc* doc)
{
    if( !doc )
        return;

    for( int i = 0; i < doc->square_count; i++ )
        Editor_SquareFree(&doc->squares[i]);
    doc->square_count = 0;
    memset(doc->square_storage, 0, sizeof(doc->square_storage));
    memset(doc->square_created, 0, sizeof(doc->square_created));

    for( int i = 0; i < doc->spawn_count; i++ )
    {
        Editor_SpawnFree(&doc->spawns[i]->file);
        ToriMem_Free(ToriMem_Editor(), doc->spawns[i]);
    }
    ToriMem_Free(ToriMem_Editor(), doc->spawns);
    doc->spawns = NULL;
    doc->spawn_count = 0;
    doc->spawn_capacity = 0;

    for( int i = 0; i < doc->text_count; i++ )
    {
        ToriMem_Free(ToriMem_Editor(), doc->texts[i]->text);
        ToriMem_Free(ToriMem_Editor(), doc->texts[i]);
    }
    ToriMem_Free(ToriMem_Editor(), doc->texts);
    doc->texts = NULL;
    doc->text_count = 0;
    doc->text_capacity = 0;
}

struct Editor_Square*
Editor_DocFindSquare(
    struct Editor_Doc* doc,
    int map_x,
    int map_z)
{
    assert(doc);

    for( int i = 0; i < doc->square_count; i++ )
    {
        if( doc->squares[i].map_x == map_x && doc->squares[i].map_z == map_z )
            return &doc->squares[i];
    }
    return NULL;
}

struct Editor_Square*
Editor_DocOpenSquare(
    struct Editor_Doc* doc,
    int map_x,
    int map_z)
{
    struct Editor_Square* square;

    assert(doc);

    square = Editor_DocFindSquare(doc, map_x, map_z);
    if( square )
        return square;

    if( doc->square_count == EDITOR_DOC_MAX_SQUARES )
        return NULL;

    doc->square_storage[doc->square_count][0] = '\0';
    doc->square_created[doc->square_count] = 0;
    square = &doc->squares[doc->square_count++];
    Editor_SquareInit(square, map_x, map_z);
    return square;
}

static int
square_index(
    const struct Editor_Doc* doc,
    const struct Editor_Square* square)
{
    int index;

    assert(doc);
    assert(square);

    index = (int)(square - doc->squares);
    assert(index >= 0);
    assert(index < doc->square_count);
    return index;
}

const char*
Editor_DocSquareStorage(
    const struct Editor_Doc* doc,
    const struct Editor_Square* square)
{
    return doc->square_storage[square_index(doc, square)];
}

void
Editor_DocSquareSetStorage(
    struct Editor_Doc* doc,
    const struct Editor_Square* square,
    const char* storage)
{
    assert(storage);
    assert(strlen(storage) < EDITOR_DOC_STORAGE_MAX);

    snprintf(
        doc->square_storage[square_index(doc, square)],
        EDITOR_DOC_STORAGE_MAX,
        "%s",
        strcmp(storage, "maps") == 0 ? "" : storage);
}

int
Editor_DocFind(
    struct Editor_Doc* doc,
    enum Editor_DocKind kind,
    const char* key,
    struct Editor_DocHandle* out)
{
    assert(doc);
    assert(key);
    assert(out);

    memset(out, 0, sizeof(*out));
    out->kind = kind;

    if( kind == EDITOR_DOC_SQUARE )
    {
        int map_x;
        int map_z;
        if( !Editor_SquareKeyParse(key, &map_x, &map_z) )
            return 0;
        out->square = Editor_DocFindSquare(doc, map_x, map_z);
        return out->square != NULL;
    }
    if( kind == EDITOR_DOC_SPAWN )
    {
        for( int i = 0; i < doc->spawn_count; i++ )
        {
            if( strcmp(doc->spawns[i]->key, key) == 0 )
            {
                out->spawn = doc->spawns[i];
                return 1;
            }
        }
        return 0;
    }
    for( int i = 0; i < doc->text_count; i++ )
    {
        if( doc->texts[i]->kind == kind && strcmp(doc->texts[i]->key, key) == 0 )
        {
            out->text = doc->texts[i];
            return 1;
        }
    }
    return 0;
}

int
Editor_DocOpen(
    struct Editor_Doc* doc,
    enum Editor_DocKind kind,
    const char* key,
    struct Editor_DocHandle* out)
{
    assert(doc);
    assert(key);
    assert(out);

    if( !Editor_DocKeyValid(kind, key) )
    {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    if( Editor_DocFind(doc, kind, key, out) )
        return 1;

    if( kind == EDITOR_DOC_SQUARE )
    {
        int map_x;
        int map_z;
        Editor_SquareKeyParse(key, &map_x, &map_z);
        out->square = Editor_DocOpenSquare(doc, map_x, map_z);
        return out->square != NULL;
    }
    if( kind == EDITOR_DOC_SPAWN )
    {
        struct Editor_SpawnDoc* spawn;

        if( doc->spawn_count == doc->spawn_capacity )
        {
            int next = doc->spawn_capacity ? doc->spawn_capacity * 2 : 16;
            doc->spawns = ToriMem_Realloc(
                ToriMem_Editor(), doc->spawns, (size_t)next * sizeof(*doc->spawns));
            assert(doc->spawns);
            doc->spawn_capacity = next;
        }
        spawn = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*spawn));
        assert(spawn);
        snprintf(spawn->key, sizeof(spawn->key), "%s", key);
        Editor_SquareKeyParse(key, &spawn->map_x, &spawn->map_z);
        Editor_SpawnInit(&spawn->file);
        doc->spawns[doc->spawn_count++] = spawn;
        out->spawn = spawn;
        return 1;
    }

    {
        struct Editor_TextDoc* text;

        if( doc->text_count == doc->text_capacity )
        {
            int next = doc->text_capacity ? doc->text_capacity * 2 : 16;
            doc->texts = ToriMem_Realloc(
                ToriMem_Editor(), doc->texts, (size_t)next * sizeof(*doc->texts));
            assert(doc->texts);
            doc->text_capacity = next;
        }
        text = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*text));
        assert(text);
        text->kind = kind;
        snprintf(text->key, sizeof(text->key), "%s", key);
        text->text = ToriMem_Alloc(ToriMem_Editor(), 1);
        assert(text->text);
        text->text[0] = '\0';
        text->capacity = 1;
        doc->texts[doc->text_count++] = text;
        out->text = text;
        return 1;
    }
}

int
Editor_DocHandleLoaded(const struct Editor_DocHandle* handle)
{
    assert(handle);

    if( handle->square )
        return handle->square->loaded;
    if( handle->spawn )
        return handle->spawn->loaded;
    assert(handle->text);
    return handle->text->loaded;
}

int
Editor_DocHandleDirty(const struct Editor_DocHandle* handle)
{
    assert(handle);

    if( handle->square )
        return handle->square->dirty_map || handle->square->dirty_loc;
    if( handle->spawn )
        return handle->spawn->dirty;
    assert(handle->text);
    return handle->text->dirty;
}

void
Editor_DocHandleClearDirty(struct Editor_DocHandle* handle)
{
    assert(handle);

    if( handle->square )
    {
        handle->square->dirty_map = 0;
        handle->square->dirty_loc = 0;
        return;
    }
    if( handle->spawn )
    {
        handle->spawn->dirty = 0;
        return;
    }
    assert(handle->text);
    handle->text->dirty = 0;
}

int
Editor_DocHasUnsaved(const struct Editor_Doc* doc)
{
    assert(doc);

    for( int i = 0; i < doc->square_count; i++ )
    {
        if( doc->squares[i].dirty_map || doc->squares[i].dirty_loc )
            return 1;
    }
    for( int i = 0; i < doc->spawn_count; i++ )
    {
        if( doc->spawns[i]->dirty )
            return 1;
    }
    for( int i = 0; i < doc->text_count; i++ )
    {
        if( doc->texts[i]->dirty )
            return 1;
    }
    return 0;
}

/* ---- text documents ----------------------------------------------------- */

static void
text_reserve(
    struct Editor_TextDoc* text,
    size_t length)
{
    if( length + 1 <= text->capacity )
        return;
    {
        size_t next = text->capacity ? text->capacity : 64;
        while( next < length + 1 )
            next *= 2;
        text->text = ToriMem_Realloc(ToriMem_Editor(), text->text, next);
        assert(text->text);
        text->capacity = next;
    }
}

void
Editor_TextDocSet(
    struct Editor_TextDoc* text,
    const char* data,
    size_t length)
{
    assert(text);
    assert(data || length == 0);

    text_reserve(text, length);
    if( length > 0 )
        memcpy(text->text, data, length);
    text->text[length] = '\0';
    text->length = length;
    text->loaded = 1;
    text->dirty = 0;
}

void
Editor_TextDocSplice(
    struct Editor_TextDoc* text,
    size_t offset,
    size_t remove_length,
    const char* insert,
    size_t insert_length)
{
    size_t next_length;

    assert(text);
    assert(insert || insert_length == 0);
    assert(offset <= text->length);
    assert(remove_length <= text->length - offset);

    next_length = text->length - remove_length + insert_length;
    text_reserve(text, next_length);
    memmove(
        text->text + offset + insert_length,
        text->text + offset + remove_length,
        text->length - offset - remove_length + 1);
    if( insert_length > 0 )
        memcpy(text->text + offset, insert, insert_length);
    text->length = next_length;
}

/* ---- IFACE as text splices (phase 3 replaces), CFG through editor_cfg ---- */

/** End of the line starting at `at` (the '\n' or `end`). */
static size_t
line_end(
    const char* text,
    size_t at,
    size_t end)
{
    while( at < end && text[at] != '\n' )
        at++;
    return at;
}

/** Past the line's newline, or `end`. */
static size_t
next_line(
    const char* text,
    size_t at,
    size_t end)
{
    at = line_end(text, at, end);
    return at < end ? at + 1 : end;
}

static int
line_blank(
    const char* text,
    size_t start,
    size_t stop)
{
    for( size_t i = start; i < stop; i++ )
    {
        if( (unsigned char)text[i] > ' ' )
            return 0;
    }
    return 1;
}

/** The line's length without a trailing '\r'. */
static size_t
content_end(
    const char* text,
    size_t start,
    size_t stop)
{
    if( stop > start && text[stop - 1] == '\r' )
        return stop - 1;
    return stop;
}

/**
 * One `key=value` edit inside [range_start, range_end): the line must hold
 * `expect` ("" = must be absent) and ends up holding `want` ("" = removed).
 */
static int
apply_field(
    struct Editor_TextDoc* text,
    size_t range_start,
    size_t range_end,
    const char* key,
    const char* expect,
    const char* want)
{
    size_t key_length = strlen(key);
    size_t found_start = (size_t)-1;
    size_t found_stop = 0;
    size_t insert_at = range_start;

    if( key_length == 0 || (!expect[0] && !want[0]) )
        return 0;

    for( size_t at = range_start; at < range_end; at = next_line(text->text, at, range_end) )
    {
        size_t stop = line_end(text->text, at, range_end);

        if( !line_blank(text->text, at, stop) )
            insert_at = stop < range_end ? stop + 1 : stop;
        if( found_start == (size_t)-1 && stop - at > key_length
            && memcmp(text->text + at, key, key_length) == 0 && text->text[at + key_length] == '=' )
        {
            found_start = at;
            found_stop = stop;
        }
    }

    if( expect[0] )
    {
        size_t value_at;
        size_t value_end;

        if( found_start == (size_t)-1 )
            return 0;
        value_at = found_start + key_length + 1;
        value_end = content_end(text->text, found_start, found_stop);
        if( value_end - value_at != strlen(expect)
            || memcmp(text->text + value_at, expect, value_end - value_at) != 0 )
            return 0;
        if( want[0] )
            Editor_TextDocSplice(text, value_at, value_end - value_at, want, strlen(want));
        else
        {
            size_t remove_end = found_stop < text->length ? found_stop + 1 : found_stop;
            Editor_TextDocSplice(text, found_start, remove_end - found_start, NULL, 0);
        }
        return 1;
    }

    if( found_start != (size_t)-1 )
        return 0;
    {
        char line[EDITOR_CMD_NAME_MAX + EDITOR_CMD_VALUE_MAX + 4];
        int length;

        if( insert_at == text->length && text->length > 0 && text->text[text->length - 1] != '\n' )
            length = snprintf(line, sizeof(line), "\n%s=%s\n", key, want);
        else
            length = snprintf(line, sizeof(line), "%s=%s\n", key, want);
        Editor_TextDocSplice(text, insert_at, 0, line, (size_t)length);
    }
    return 1;
}

/** Offset of the `index`th line beginning with '[' at or after `from`, or
 *  (size_t)-1. */
static size_t
find_header(
    const struct Editor_TextDoc* text,
    int index)
{
    int seen = 0;

    for( size_t at = 0; at < text->length; at = next_line(text->text, at, text->length) )
    {
        if( text->text[at] == '[' )
        {
            if( seen == index )
                return at;
            seen++;
        }
    }
    return (size_t)-1;
}

static int
count_headers(const struct Editor_TextDoc* text)
{
    int count = 0;

    for( size_t at = 0; at < text->length; at = next_line(text->text, at, text->length) )
    {
        if( text->text[at] == '[' )
            count++;
    }
    return count;
}

/** End of the block whose header starts at `header`: the next header or EOF. */
static size_t
block_end(
    const struct Editor_TextDoc* text,
    size_t header)
{
    for( size_t at = next_line(text->text, header, text->length); at < text->length;
         at = next_line(text->text, at, text->length) )
    {
        if( text->text[at] == '[' )
            return at;
    }
    return text->length;
}

static struct Editor_TextDoc*
find_text(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command)
{
    struct Editor_DocHandle handle;

    if( !Editor_DocFind(doc, command->doc_kind, command->doc_key, &handle) || !handle.text
        || !handle.text->loaded )
        return NULL;
    return handle.text;
}

/** A fixed-size string field off the wire: terminated inside its array. */
static int
field_terminated(
    const char* field,
    size_t size)
{
    return memchr(field, '\0', size) != NULL;
}

/** Make the document's text the block's, as one splice of the bytes that
 *  differ: the edit touched one line, and the rest is not rewritten. */
static void
cfg_commit(
    struct Editor_TextDoc* text,
    const struct Editor_CfgBlock* block)
{
    size_t old_length = text->length;
    size_t new_length = block->length;
    size_t prefix = 0;
    size_t suffix = 0;

    while( prefix < old_length && prefix < new_length && text->text[prefix] == block->text[prefix] )
        prefix++;
    while( suffix < old_length - prefix && suffix < new_length - prefix
           && text->text[old_length - 1 - suffix] == block->text[new_length - 1 - suffix] )
        suffix++;
    Editor_TextDocSplice(
        text,
        prefix,
        old_length - prefix - suffix,
        block->text + prefix,
        new_length - prefix - suffix);
    text->dirty = 1;
}

static int
cfg_apply_field(
    struct Editor_CfgBlock* block,
    const struct Editor_Cmd* command,
    int forward)
{
    char key[EDITOR_CMD_NAME_MAX];
    int occurrence;
    int line;
    int positioned;
    const char* expect;
    const char* want;

    if( !field_terminated(command->u.cfg_field.key, sizeof(command->u.cfg_field.key))
        || !field_terminated(command->u.cfg_field.before, sizeof(command->u.cfg_field.before))
        || !field_terminated(command->u.cfg_field.after, sizeof(command->u.cfg_field.after)) )
        return 0;
    if( !Editor_CfgFieldSpecParse(command->u.cfg_field.key, key, sizeof(key), &occurrence, &line) )
        return 0;
    if( Editor_CfgBlockHeader(block) < 0 )
        return 0;
    positioned = strchr(command->u.cfg_field.key, '#') || strchr(command->u.cfg_field.key, '@');
    expect = forward ? command->u.cfg_field.before : command->u.cfg_field.after;
    want = forward ? command->u.cfg_field.after : command->u.cfg_field.before;

    if( !expect[0] && !want[0] )
        return 0;
    if( expect[0] )
    {
        if( !Editor_CfgBlockFieldIs(block, key, occurrence, expect) )
            return 0;
        if( line >= 0 && Editor_CfgBlockFieldFind(block, key, occurrence) != line )
            return 0;
        if( want[0] )
            return Editor_CfgBlockFieldSet(block, key, occurrence, want);
        return Editor_CfgBlockFieldRemove(block, key, occurrence, line);
    }
    /* A plain "key" must be absent outright; "key#N" / "key@L" name the
     * occurrence or the line it becomes. */
    if( !positioned && Editor_CfgBlockFieldCount(block, key) != 0 )
        return 0;
    return Editor_CfgBlockFieldInsert(block, key, occurrence, line, want);
}

static int
cfg_apply_block(
    struct Editor_CfgBlock* block,
    const struct Editor_Cmd* command,
    int forward)
{
    const char* name = command->u.cfg_block.name;
    int adds = command->has_after && !command->has_before;
    int removes = command->has_before && !command->has_after;

    if( !field_terminated(command->u.cfg_block.name, sizeof(command->u.cfg_block.name))
        || !field_terminated(command->u.cfg_block.new_name, sizeof(command->u.cfg_block.new_name)) )
        return 0;

    if( command->has_before && command->has_after )
    {
        const char* from = forward ? command->u.cfg_block.name : command->u.cfg_block.new_name;
        const char* to = forward ? command->u.cfg_block.new_name : command->u.cfg_block.name;

        if( !Editor_CfgBlockNameIs(block, from) )
            return 0;
        return Editor_CfgBlockRename(block, to);
    }
    if( !adds && !removes )
        return 0;
    /* add forward == remove inverse: no header -> "[name]\n" at the top. */
    if( adds == forward )
        return Editor_CfgBlockAddHeader(block, name);
    return Editor_CfgBlockRemoveHeader(block, name);
}

int
Editor_CfgApply(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction)
{
    struct Editor_TextDoc* text;
    struct Editor_CfgBlock block;
    int forward = direction == EDITOR_CMD_FORWARD;
    int changed = 0;

    assert(doc);
    assert(command);

    if( command->kind != EDITOR_CMD_CFG_FIELD && command->kind != EDITOR_CMD_CFG_BLOCK )
        return 0;
    text = find_text(doc, command);
    if( !text )
        return 0;

    Editor_CfgBlockInit(&block);
    Editor_CfgBlockSet(&block, text->text, text->length);
    if( command->kind == EDITOR_CMD_CFG_FIELD )
        changed = cfg_apply_field(&block, command, forward);
    else
        changed = cfg_apply_block(&block, command, forward);
    if( changed )
        cfg_commit(text, &block);
    Editor_CfgBlockFree(&block);
    return changed;
}

/** The canonical text IF_COMPONENT add inserts and remove takes back. */
static int
component_text(
    char* out,
    size_t out_size,
    int index,
    int parent)
{
    if( parent >= 0 )
        return snprintf(out, out_size, "[component%d]\nlayer=%d\n\n", index, parent);
    return snprintf(out, out_size, "[component%d]\n\n", index);
}

static int
move_block(
    struct Editor_TextDoc* text,
    int from,
    int to)
{
    size_t header = find_header(text, from);
    size_t stop;
    size_t length;
    char* block;
    size_t insert_at;

    if( header == (size_t)-1 )
        return 0;
    stop = block_end(text, header);
    length = stop - header;
    /* A block with no newline at EOF would run into its new neighbour. */
    if( length == 0 || text->text[stop - 1] != '\n' )
        return 0;

    block = ToriMem_Alloc(ToriMem_Editor(), length);
    assert(block);
    memcpy(block, text->text + header, length);
    Editor_TextDocSplice(text, header, length, NULL, 0);

    if( to >= count_headers(text) )
        insert_at = text->length;
    else
        insert_at = find_header(text, to);
    if( to < 0 || insert_at == (size_t)-1 )
    {
        /* Put it back where it was: the move does not fit this text. */
        Editor_TextDocSplice(text, header, 0, block, length);
        ToriMem_Free(ToriMem_Editor(), block);
        return 0;
    }
    Editor_TextDocSplice(text, insert_at, 0, block, length);
    ToriMem_Free(ToriMem_Editor(), block);
    return 1;
}

int
Editor_IfApply(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction)
{
    struct Editor_TextDoc* text;
    int forward = direction == EDITOR_CMD_FORWARD;

    assert(doc);
    assert(command);

    text = find_text(doc, command);
    if( !text )
        return 0;

    if( command->kind == EDITOR_CMD_IF_FIELD )
    {
        size_t header = find_header(text, command->u.if_field.component);
        int changed;

        if( command->u.if_field.component < 0 || header == (size_t)-1 )
            return 0;
        changed = apply_field(
            text,
            next_line(text->text, header, text->length),
            block_end(text, header),
            command->u.if_field.key,
            forward ? command->u.if_field.before : command->u.if_field.after,
            forward ? command->u.if_field.after : command->u.if_field.before);
        if( changed )
            text->dirty = 1;
        return changed;
    }

    if( command->kind == EDITOR_CMD_IF_COMPONENT )
    {
        const int index_before = command->u.if_component.index_before;
        const int index_after = command->u.if_component.index_after;
        int adds = command->has_after && !command->has_before;
        int removes = command->has_before && !command->has_after;

        if( command->has_before && command->has_after )
        {
            char before[16] = "";
            char after[16] = "";
            int parent_before = command->u.if_component.parent_before;
            int parent_after = command->u.if_component.parent_after;

            if( parent_before >= 0 )
                snprintf(before, sizeof(before), "%d", parent_before);
            if( parent_after >= 0 )
                snprintf(after, sizeof(after), "%d", parent_after);

            if( forward )
            {
                if( parent_before != parent_after )
                {
                    size_t header = find_header(text, index_before);
                    if( header == (size_t)-1
                        || !apply_field(
                            text,
                            next_line(text->text, header, text->length),
                            block_end(text, header),
                            "layer",
                            before,
                            after) )
                        return 0;
                }
                if( index_before != index_after && !move_block(text, index_before, index_after) )
                    return 0;
            }
            else
            {
                if( index_before != index_after && !move_block(text, index_after, index_before) )
                    return 0;
                if( parent_before != parent_after )
                {
                    size_t header = find_header(text, index_before);
                    if( header == (size_t)-1
                        || !apply_field(
                            text,
                            next_line(text->text, header, text->length),
                            block_end(text, header),
                            "layer",
                            after,
                            before) )
                        return 0;
                }
            }
            text->dirty = 1;
            return 1;
        }
        if( !adds && !removes )
            return 0;

        {
            int index = adds ? index_after : index_before;
            int parent = adds ? command->u.if_component.parent_after
                              : command->u.if_component.parent_before;
            char block[64];
            size_t length = (size_t)component_text(block, sizeof(block), index, parent);

            if( index < 0 )
                return 0;
            if( adds == forward )
            {
                size_t insert_at;
                if( index > count_headers(text) )
                    return 0;
                insert_at = index == count_headers(text) ? text->length : find_header(text, index);
                Editor_TextDocSplice(text, insert_at, 0, block, length);
            }
            else
            {
                size_t header = find_header(text, index);
                if( header == (size_t)-1 || block_end(text, header) - header != length
                    || memcmp(text->text + header, block, length) != 0 )
                    return 0;
                Editor_TextDocSplice(text, header, length, NULL, 0);
            }
            text->dirty = 1;
            return 1;
        }
    }

    return 0;
}
