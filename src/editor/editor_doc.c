#include "editor_doc.h"
#include "editor_cfg.h"
#include "editor_if.h"
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
        assert(square->locs);
        square->loc_capacity = next;
    }

    square->locs[square->loc_count] = *loc;
    return square->loc_count++;
}

/* The cache's order for a square's locs: loc id, then the packed position
 * (level, x, z), then -- among one loc's layers on one tile -- shape and
 * rotation. Every .jl2 in the content tree is in it (2,949 squares). */
static int
loc_cache_order(
    const struct Editor_Loc* a,
    const struct Editor_Loc* b)
{
    if( a->loc_id != b->loc_id )
        return a->loc_id < b->loc_id ? -1 : 1;
    if( a->level != b->level )
        return a->level < b->level ? -1 : 1;
    if( a->x != b->x )
        return a->x < b->x ? -1 : 1;
    if( a->z != b->z )
        return a->z < b->z ? -1 : 1;
    if( a->shape != b->shape )
        return a->shape < b->shape ? -1 : 1;
    if( a->rotation != b->rotation )
        return a->rotation < b->rotation ? -1 : 1;
    return 0;
}

int
Editor_SquareLocInsert(
    struct Editor_Square* square,
    const struct Editor_Loc* loc)
{
    int index;
    int at;

    assert(square);
    assert(loc);

    /* Before the first placement that sorts after it: in a file in cache
     * order (all of them) that is exactly where a deleted loc stood, so an
     * undone delete saves byte for byte. A hand-ordered file still gets a
     * stable, deterministic place -- the same on the authority and on every
     * mirror. */
    at = square->loc_count;
    for( int i = 0; i < square->loc_count; i++ )
        if( loc_cache_order(&square->locs[i], loc) > 0 )
        {
            at = i;
            break;
        }
    index = Editor_SquareLocAdd(square, loc);
    assert(index == square->loc_count - 1);
    memmove(&square->locs[at + 1], &square->locs[at], (size_t)(index - at) * sizeof(*square->locs));
    square->locs[at] = *loc;
    return at;
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

int
Editor_SquareLocFindExact(
    const struct Editor_Square* square,
    const struct Editor_Loc* loc)
{
    assert(square);
    assert(loc);

    for( int i = 0; i < square->loc_count; i++ )
        if( loc_cache_order(&square->locs[i], loc) == 0 )
            return i;
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
        /* "<type>:<name>@<overlay>": the record's block in a SERVER OVERLAY,
         * a path under server/scripts whose extension is the type
         * ("npc:goblin@areas/lumbridge/configs/lumbridge.npc"). Without the
         * suffix the key means the block of configs/all.<type>, as before. */
        {
            const char* at = strchr(colon + 1, '@');
            char name[EDITOR_DOC_KEY_MAX];
            const char* overlay;
            size_t overlay_length;

            if( !at )
                return name_valid(colon + 1);
            if( (size_t)(at - (colon + 1)) >= sizeof(name) )
                return 0;
            memcpy(name, colon + 1, (size_t)(at - (colon + 1)));
            name[at - (colon + 1)] = '\0';
            overlay = at + 1;
            overlay_length = strlen(overlay);
            if( !name_valid(name) || !relative_path_valid(overlay) )
                return 0;
            if( overlay_length < type_length + 2 || overlay[overlay_length - type_length - 1] != '.'
                || memcmp(overlay + overlay_length - type_length, type, type_length) != 0 )
                return 0;
            return 1;
        }
    }
    case EDITOR_DOC_IFACE:
        return strncmp(key, "if:", 3) == 0 && name_valid(key + 3);
    case EDITOR_DOC_AREAS:
        /* One per content tree. */
        return strcmp(key, "areas") == 0;
    case EDITOR_DOC_PALETTES:
        /* One per content tree (editor_palette.h). */
        return strcmp(key, "palettes") == 0;
    case EDITOR_DOC_SCENES:
        /* One per content tree (editor_scenes.h). */
        return strcmp(key, "scenes") == 0;
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
        /* A type's field register, fields/<type>.ini (what the record
         * editor offers to add); read, never edited. */
        if( strncmp(key, "fields:", 7) == 0 )
        {
            size_t const length = strlen(key + 7);
            return length > 4 && strcmp(key + 7 + length - 4, ".ini") == 0
                   && config_type_valid(key + 7, length - 4);
        }
        /* The catalog favourites, editor/favourites.txt (editor_favourites.h):
         * one per content tree, a text document edited by splices. */
        if( strcmp(key, "editor:favourites.txt") == 0 )
            return 1;
        /* The encounter sets, editor/encounters.txt (editor_encounters.h). */
        if( strcmp(key, "editor:encounters.txt") == 0 )
            return 1;
        /* A square's rows from the .spawn files of quests and skills, made
         * by the server (editor_host_local.c); read, never edited. */
        if( strncmp(key, "spawns:", 7) == 0 )
        {
            int map_x;
            int map_z;
            char tail;
            return sscanf(key + 7, "m%d_%d%c", &map_x, &map_z, &tail) == 2 && map_x >= 0 && map_z >= 0;
        }
        /* An npc's stats ledger, npc_stats/<shard>/<gameval>.stats
         * (editor_npcstats.h): one shard byte, a slash, a gameval. */
        if( strncmp(key, "stats:", 6) == 0 )
        {
            const char* name = key + 8;
            size_t const length = strlen(key + 6);
            if( length < 3 || key[7] != '/' )
                return 0;
            for( const char* at = name; *at; at++ )
                if( !isalnum((unsigned char)*at) && *at != '_' )
                    return 0;
            /* The shard is the name's: gen_npc_stats.py's shard_of. */
            return strlen(name) < 64
                   && key[6] == (isalnum((unsigned char)name[0]) ? (char)tolower((unsigned char)name[0]) : '_');
        }
        /* An npc's movement ledger, npc_movement/<shard>/<name>.move
         * (editor_npc.h), shaped as the stats one. */
        if( strncmp(key, "move:", 5) == 0 )
        {
            const char* name = key + 7;
            size_t const length = strlen(key + 5);
            if( length < 3 || key[6] != '/' )
                return 0;
            for( const char* at = name; *at; at++ )
                if( !isalnum((unsigned char)*at) && *at != '_' )
                    return 0;
            return strlen(name) < 64
                   && key[5] == (isalnum((unsigned char)name[0]) ? (char)tolower((unsigned char)name[0]) : '_');
        }
        /* A stamp and the stamps' list (editor_stamps.h): editor/stamps/
         * <name>.stamp and editor/stamps/index.txt. Names are [A-Za-z0-9_-]
         * under 48 bytes, so "index" can never be confused with the list. */
        if( strncmp(key, "stamp:", 6) == 0 )
        {
            size_t const length = strlen(key + 6);
            return length > 0 && length < 48 && name_valid(key + 6) && !strchr(key + 6, '.');
        }
        if( strcmp(key, "stamps:index") == 0 )
            return 1;
        /* A world-map area file, worldmap/areas/<name>.wma (editor_wma.h;
         * the instance helper writes one): [a-z0-9_] under 48 bytes, the
         * name the catalog's reader keys the file by. */
        if( strncmp(key, "wma:", 4) == 0 )
        {
            size_t const length = strlen(key + 4);
            if( length == 0 || length >= 48 )
                return 0;
            for( size_t i = 0; i < length; i++ )
            {
                char const c = key[4 + i];
                if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') )
                    return 0;
            }
            return 1;
        }
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

    if( storage[0] == '\0' || strcmp(storage, "maps") == 0 || Editor_DocStorageIsScratch(storage) )
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
Editor_DocStorageIsScratch(const char* storage)
{
    assert(storage);
    return strcmp(storage, EDITOR_DOC_SCRATCH_STORAGE) == 0;
}

void
Editor_DocScratchSlot(
    int slot,
    int* out_map_x,
    int* out_map_z)
{
    assert(slot >= 0);
    assert(slot < EDITOR_DOC_SCRATCH_SLOTS);
    assert(out_map_x);
    assert(out_map_z);
    /* East of the content and of every shipped square, and below map x 100:
     * that is the server's instance pool, whose login rescue teleports a
     * character standing there to the respawn tile. */
    *out_map_x = 90 + 2 * (slot % 4);
    *out_map_z = 240 + 2 * (slot / 4);
}

int
Editor_DocIsScratchSlot(
    int map_x,
    int map_z)
{
    for( int slot = 0; slot < EDITOR_DOC_SCRATCH_SLOTS; slot++ )
    {
        int x;
        int z;
        Editor_DocScratchSlot(slot, &x, &z);
        if( x == map_x && z == map_z )
            return 1;
    }
    return 0;
}

int
Editor_SquareIsBlank(const struct Editor_Square* square)
{
    assert(square);
    if( square->loc_count > 0 )
        return 0;
    /* Field by field: a tile that came off the wire need not have its
     * padding byte zeroed, so a memcmp against the zero tile can miss. */
    for( int i = 0; i < EDITOR_SQUARE_TILES; i++ )
    {
        struct Editor_Tile const* tile = &square->tiles[i];
        if( tile->has_height || tile->height || tile->has_overlay || tile->overlay_id || tile->shape
            || tile->rotation || tile->settings || tile->underlay_id )
            return 0;
    }
    return 1;
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

/** Square `i` holds edits a save (or a Place) is owed: a blank scratch
 *  square -- the slot a Place cleared -- owes nothing. */
static int
doc_square_unsaved(
    const struct Editor_Doc* doc,
    int i)
{
    const struct Editor_Square* square = &doc->squares[i];

    if( !square->dirty_map && !square->dirty_loc )
        return 0;
    return !(Editor_DocStorageIsScratch(doc->square_storage[i]) && Editor_SquareIsBlank(square));
}

int
Editor_DocHasUnsaved(const struct Editor_Doc* doc)
{
    assert(doc);

    for( int i = 0; i < doc->square_count; i++ )
    {
        if( doc_square_unsaved(doc, i) )
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

int
Editor_DocUnsavedCount(const struct Editor_Doc* doc)
{
    int count = 0;

    assert(doc);
    for( int i = 0; i < doc->square_count; i++ )
        if( doc_square_unsaved(doc, i) )
            count++;
    for( int i = 0; i < doc->spawn_count; i++ )
        if( doc->spawns[i]->dirty )
            count++;
    for( int i = 0; i < doc->text_count; i++ )
        if( doc->texts[i]->dirty )
            count++;
    return count;
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
    text->revision++;
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
    text->revision++;
}

/* ---- CFG through editor_cfg; IFACE through editor_if ---- */

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

int
Editor_IfApply(
    struct Editor_Doc* doc,
    const struct Editor_Cmd* command,
    enum Editor_CmdDirection direction)
{
    /* The interface codec (editor_if.h). editor_cmd.c calls it directly; this
     * keeps the fixed entry point for anyone else. */
    return Editor_IfDocApply(doc, command, direction);
}
