/*
 * A loc / npc / obj record from its text block: editor_recdef.h has the
 * contract. The key tables below follow cachepack's packers line for line
 * (3rd/rscache/tools/cachepack/config/cp_loc.c cp_pack_loc, cp_npc.c
 * cp_pack_npc, cp_obj.c cp_pack_obj): a key that packer reads into a struct
 * field is read into the same field here; a key that is only an opcode's
 * presence there (`blockwalk=0`, `shadow=no`) sets what the cache decoder
 * sets when it meets that opcode (3rd/rscache/src/datatypes/dat2_config_*.c).
 */

#include "editor_recdef.h"

#include "engine/torirs_location_from_rscache.h"
#include "engine/torirs_npctype_from_rscache.h"
#include "engine/torirs_objtype_from_rscache.h"
#include "mem/torirs_mem.h"

#include <rscache.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- the block as key / value lines -------------------------------------- */

struct recdef_field
{
    char* key;
    char* value;
};

struct recdef_block
{
    /** The block's text, cut into NUL-terminated keys and values in place. */
    char* text;
    struct recdef_field* fields;
    int count;
};

/** Cut `text` into its field lines. Comments, blank lines, the header and a
 *  line with no '=' are not fields. Spaces around a key and a value go, as
 *  the config reader drops them. */
static void
recdef_block_read(
    struct recdef_block* block,
    const char* text,
    size_t length)
{
    char* p;
    char* end;
    int lines = 1;

    memset(block, 0, sizeof(*block));
    block->text = ToriMem_Alloc(ToriMem_Editor(), length + 1);
    assert(block->text);
    memcpy(block->text, text, length);
    block->text[length] = '\0';
    for( size_t i = 0; i < length; i++ )
        lines += text[i] == '\n';
    block->fields = ToriMem_Alloc(ToriMem_Editor(), (size_t)lines * sizeof(*block->fields));
    assert(block->fields);

    p = block->text;
    end = block->text + length;
    while( p < end )
    {
        char* line_end = memchr(p, '\n', (size_t)(end - p));
        char* next = line_end ? line_end + 1 : end;
        char* eq;
        char* key = p;
        char* key_end;
        char* value;
        char* value_end;

        if( !line_end )
            line_end = end;
        if( line_end > p && line_end[-1] == '\r' )
            line_end--;
        *line_end = '\0';
        while( *key == ' ' || *key == '\t' )
            key++;
        eq = strchr(key, '=');
        if( key[0] != '[' && key[0] != '/' && key[0] != '\0' && eq )
        {
            key_end = eq;
            while( key_end > key && (key_end[-1] == ' ' || key_end[-1] == '\t') )
                key_end--;
            *key_end = '\0';
            value = eq + 1;
            while( *value == ' ' || *value == '\t' )
                value++;
            value_end = line_end;
            while( value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t') )
                value_end--;
            *value_end = '\0';
            if( key[0] )
            {
                block->fields[block->count].key = key;
                block->fields[block->count].value = value;
                block->count++;
            }
        }
        p = next;
    }
}

static void
recdef_block_free(struct recdef_block* block)
{
    ToriMem_Free(ToriMem_Editor(), block->fields);
    ToriMem_Free(ToriMem_Editor(), block->text);
    memset(block, 0, sizeof(*block));
}

/* ---- values --------------------------------------------------------------- */

/** A list of ints that grows, settable by index (unset slots are 0). */
struct recdef_ints
{
    int* items;
    int count;
    int capacity;
};

static void
recdef_ints_set(
    struct recdef_ints* list,
    int index,
    int value)
{
    assert(index >= 0);
    if( index >= list->capacity )
    {
        int next = list->capacity ? list->capacity : 8;
        while( next <= index )
            next *= 2;
        list->items = ToriMem_Realloc(ToriMem_Editor(), list->items, (size_t)next * sizeof(int));
        assert(list->items);
        memset(list->items + list->capacity, 0, (size_t)(next - list->capacity) * sizeof(int));
        list->capacity = next;
    }
    list->items[index] = value;
    if( index >= list->count )
        list->count = index + 1;
}

static void
recdef_ints_push(
    struct recdef_ints* list,
    int value)
{
    recdef_ints_set(list, list->count, value);
}

static void
recdef_ints_free(struct recdef_ints* list)
{
    ToriMem_Free(ToriMem_Editor(), list->items);
    memset(list, 0, sizeof(*list));
}

/** cachepack's cp_parse_int: the whole text, any base strtol reads. */
static int
recdef_int(
    const char* text,
    int* out)
{
    char* end = NULL;
    long v;

    if( !text[0] )
        return 0;
    errno = 0;
    v = strtol(text, &end, 0);
    if( errno != 0 || end == text || *end != '\0' || v < INT32_MIN || v > INT32_MAX )
        return 0;
    *out = (int)v;
    return 1;
}

/** `prefix<N>` -> N - 1 (cp_indexed_key), else -1. */
static int
recdef_indexed(
    const char* key,
    const char* prefix)
{
    size_t const plen = strlen(prefix);
    int value = 0;

    if( strncmp(key, prefix, plen) != 0 || !key[plen] )
        return -1;
    for( const char* p = key + plen; *p; p++ )
    {
        if( *p < '0' || *p > '9' )
            return -1;
        value = value * 10 + (*p - '0');
        if( value > 100000 )
            return -1;
    }
    return value > 0 ? value - 1 : -1;
}

/** A text value as the record holds it: `\n`, `\r` and `\<c>` unescaped
 *  (cp_unescape). Owned by the caller (ToriMem_Editor). */
static char*
recdef_text(const char* value)
{
    size_t const n = strlen(value);
    char* out = ToriMem_Alloc(ToriMem_Editor(), n + 1);
    size_t w = 0;

    assert(out);
    for( size_t i = 0; i < n; i++ )
    {
        if( value[i] == '\\' && value[i + 1] )
        {
            char const c = value[++i];
            out[w++] = c == 'n' ? '\n' : c == 'r' ? '\r' : c;
        }
        else
            out[w++] = value[i];
    }
    out[w] = '\0';
    return out;
}

/** A reference: the record's name, the wire's "none" (`-1`, `null`), or a
 *  bare id. 0 when nobody has that name. */
static int
recdef_ref(
    const struct Editor_RecDefContext* ctx,
    const char* type,
    const char* value,
    int* out)
{
    if( strcmp(value, "-1") == 0 || strcmp(value, "null") == 0 )
    {
        *out = -1;
        return 1;
    }
    /* A category is a number in the machine export and a name in an authored
     * overlay; every other reference is a name first. */
    if( strcmp(type, "category") == 0 && recdef_int(value, out) )
        return 1;
    if( ctx->resolve )
    {
        int const id = ctx->resolve(ctx->user, type, value);
        if( id >= 0 )
        {
            *out = id;
            return 1;
        }
    }
    return recdef_int(value, out);
}

/** `<prefix><N>s` / `<prefix><N>d` pairs (cp_collect_pairs): 0 when a value
 *  is not a number or the two sides differ in length. */
static int
recdef_pairs(
    const struct recdef_block* block,
    const char* prefix,
    struct recdef_ints* from,
    struct recdef_ints* to)
{
    size_t const plen = strlen(prefix);

    for( int i = 0; i < block->count; i++ )
    {
        const char* key = block->fields[i].key;
        size_t const klen = strlen(key);
        char index_text[16];
        char side;
        int index = 0;
        int value = 0;

        if( klen < plen + 2 || strncmp(key, prefix, plen) != 0 )
            continue;
        side = key[klen - 1];
        if( (side != 's' && side != 'd') || klen - plen - 1 >= sizeof(index_text) )
            continue;
        memcpy(index_text, key + plen, klen - plen - 1);
        index_text[klen - plen - 1] = '\0';
        if( !recdef_int(index_text, &index) || index <= 0 )
            continue;
        if( strcmp(block->fields[i].value, "default") == 0 )
            continue;
        if( !recdef_int(block->fields[i].value, &value) )
            return 0;
        recdef_ints_set(side == 's' ? from : to, index - 1, value);
    }
    return from->count == to->count;
}

static void
recdef_note_bad(
    struct Editor_RecDefReport* report,
    const char* key,
    const char* value)
{
    if( report->bad == 0 )
        snprintf(report->first_bad, sizeof(report->first_bad), "%.40s=%.50s", key, value);
    report->bad++;
}

/** A comma list of ints into `out` (at most `max`); the count, or -1 when a
 *  part is not a number. */
static int
recdef_int_list(
    const char* value,
    int* out,
    int max)
{
    int count = 0;
    const char* p = value;

    while( *p )
    {
        char part[32];
        const char* comma = strchr(p, ',');
        size_t const n = comma ? (size_t)(comma - p) : strlen(p);

        if( n == 0 || n >= sizeof(part) || count >= max )
            return -1;
        memcpy(part, p, n);
        part[n] = '\0';
        if( !recdef_int(part, &out[count++]) )
            return -1;
        p += n;
        if( *p == ',' )
            p++;
    }
    return count;
}

/* ---- the ledger ------------------------------------------------------------ */

int
Editor_RecDefLedgerName(
    const char* text,
    size_t length,
    int id,
    char* name,
    size_t name_size)
{
    const char* p = text;
    const char* end;

    assert(text || length == 0);
    assert(name);
    assert(name_size > 0);
    assert(id >= 0);
    name[0] = '\0';
    end = text + length;
    while( p < end )
    {
        const char* line_end = memchr(p, '\n', (size_t)(end - p));
        const char* next = line_end ? line_end + 1 : end;
        const char* eq;
        long value = 0;
        int digits = 0;

        if( !line_end )
            line_end = end;
        while( p < line_end && *p >= '0' && *p <= '9' && digits < 9 )
        {
            value = value * 10 + (*p - '0');
            digits++;
            p++;
        }
        eq = p;
        if( digits > 0 && eq < line_end && *eq == '=' && value == id )
        {
            const char* name_end = eq + 1;
            while( name_end < line_end && *name_end != ' ' && *name_end != '\t' && *name_end != '\r' )
                name_end++;
            if( name_end > eq + 1 && (size_t)(name_end - (eq + 1)) < name_size )
            {
                memcpy(name, eq + 1, (size_t)(name_end - (eq + 1)));
                name[name_end - (eq + 1)] = '\0';
                return 1;
            }
        }
        p = next;
    }
    return 0;
}

/* ---- loc ------------------------------------------------------------------- */

#define RECDEF_LOC_MODELS_MAX 255

struct ToriRS_Location*
Editor_RecDefLoc(
    const struct Editor_RecDefContext* ctx,
    int id,
    const char* text,
    size_t length,
    struct Editor_RecDefReport* report)
{
    struct Editor_RecDefReport local;
    struct recdef_block block;
    struct RSCache_Dat2ConfigLoc loc;
    struct ToriRS_Location* out;
    /* shape<N>=<shape>,<model>...: one row per N. */
    struct recdef_ints shape_of = { 0 };
    int** shape_models = NULL;
    int* shape_lengths = NULL;
    int shape_rows = 0;
    struct recdef_ints flat = { 0 };
    int flat_stated = 0;
    struct recdef_ints transforms = { 0 };
    int multi_stated = 0;
    struct recdef_ints sounds = { 0 };
    struct recdef_ints recol_s = { 0 };
    struct recdef_ints recol_d = { 0 };
    struct recdef_ints retex_s = { 0 };
    struct recdef_ints retex_d = { 0 };
    char* owned_text[2 + 10] = { 0 };
    int* flat_models_ptr;
    int flat_length;

    assert(ctx);
    assert(ctx->profile);
    assert(id >= 0);
    assert(text || length == 0);
    if( !report )
        report = &local;
    memset(report, 0, sizeof(*report));
    recdef_block_read(&block, text, length);
    if( block.count == 0 )
    {
        recdef_block_free(&block);
        return NULL;
    }

    RSCache_Dat2ConfigLocInit(&loc);
    for( int i = 0; i < block.count; i++ )
    {
        const char* key = block.fields[i].key;
        const char* value = block.fields[i].value;
        int ok = 1;
        int index;

        if( strcmp(value, "default") == 0 )
            continue;
        report->stated++;
        if( strcmp(key, "name") == 0 )
        {
            ToriMem_Free(ToriMem_Editor(), owned_text[0]);
            loc.name = owned_text[0] = recdef_text(value);
        }
        else if( strcmp(key, "desc") == 0 )
        {
            ToriMem_Free(ToriMem_Editor(), owned_text[1]);
            loc.desc = owned_text[1] = recdef_text(value);
        }
        else if( strcmp(key, "shape") == 0 )
            ok = strcmp(value, "empty") == 0;
        else if( (index = recdef_indexed(key, "shape")) >= 0 )
        {
            int parts[1 + RECDEF_LOC_MODELS_MAX];
            int const n = recdef_int_list(value, parts, 1 + RECDEF_LOC_MODELS_MAX);
            if( n < 1 )
                ok = 0;
            else
            {
                if( index >= shape_rows )
                {
                    shape_models = ToriMem_Realloc(ToriMem_Editor(), shape_models,
                        (size_t)(index + 1) * sizeof(*shape_models));
                    shape_lengths = ToriMem_Realloc(ToriMem_Editor(), shape_lengths,
                        (size_t)(index + 1) * sizeof(*shape_lengths));
                    assert(shape_models);
                    assert(shape_lengths);
                    for( int r = shape_rows; r <= index; r++ )
                    {
                        shape_models[r] = NULL;
                        shape_lengths[r] = 0;
                    }
                    shape_rows = index + 1;
                }
                recdef_ints_set(&shape_of, index, parts[0]);
                ToriMem_Free(ToriMem_Editor(), shape_models[index]);
                shape_models[index] = ToriMem_Alloc(ToriMem_Editor(), (size_t)n * sizeof(int));
                assert(shape_models[index]);
                memcpy(shape_models[index], parts + 1, (size_t)(n - 1) * sizeof(int));
                shape_lengths[index] = n - 1;
            }
        }
        else if( strcmp(key, "models") == 0 )
        {
            flat_stated = 1;
            if( strcmp(value, "empty") != 0 )
            {
                int parts[RECDEF_LOC_MODELS_MAX];
                int const n = recdef_int_list(value, parts, RECDEF_LOC_MODELS_MAX);
                ok = n >= 0;
                for( int f = 0; f < n; f++ )
                    recdef_ints_push(&flat, parts[f]);
            }
        }
        else if( (index = recdef_indexed(key, "multiloc")) >= 0 )
        {
            int target = -1;
            multi_stated = 1;
            ok = recdef_ref(ctx, "loc", value, &target);
            if( ok )
                recdef_ints_set(&transforms, index, target);
        }
        else if( strcmp(key, "multivarbit") == 0 )
            ok = recdef_ref(ctx, "varbit", value, &loc.transform_varbit);
        else if( strcmp(key, "multivarp") == 0 )
            ok = recdef_ref(ctx, "varp", value, &loc.transform_varp);
        else if( strcmp(key, "multidefaultnone") == 0 )
            ok = strcmp(value, "yes") == 0;
        else if( strcmp(key, "soundrandom") == 0 )
            ok = strcmp(value, "empty") == 0;
        else if( (index = recdef_indexed(key, "soundrandom")) >= 0 )
        {
            int sound = 0;
            ok = recdef_int(value, &sound);
            if( ok )
                recdef_ints_set(&sounds, index, sound);
        }
        else if( (index = recdef_indexed(key, "op")) >= 0 && index < 9 )
        {
            /* The decoder counts a stated op whatever it says, and holds
             * "hidden" as no action: a loc whose only op is hidden is still
             * one the pointer picks (RSCache_Dat2ConfigLocFinish). */
            loc._actions_seen++;
            ToriMem_Free(ToriMem_Editor(), owned_text[2 + index]);
            owned_text[2 + index] = recdef_text(value);
            loc.actions[index] = strcasecmp(owned_text[2 + index], "hidden") == 0 ? NULL : owned_text[2 + index];
        }
        else if( strncmp(key, "recol", 5) == 0 || strncmp(key, "retex", 5) == 0 )
        {
            /* Pairs, collected below; `recol=empty` states a list of none. */
            if( strcmp(key, "recol") == 0 || strcmp(key, "retex") == 0 )
                ok = strcmp(value, "empty") == 0;
        }
        else if( strcmp(key, "width") == 0 )
            ok = recdef_int(value, &loc.size_x);
        else if( strcmp(key, "length") == 0 )
            ok = recdef_int(value, &loc.size_z);
        else if( strcmp(key, "blockwalk") == 0 )
        {
            /* 0 is opcode 17 (which clears projectiles too), 1 is 27. */
            if( strcmp(value, "0") == 0 )
            {
                loc.blocks_walk = 0;
                loc.blocks_projectiles = 0;
            }
            else if( strcmp(value, "1") == 0 )
                loc.blocks_walk = 1;
            else
                ok = 0;
        }
        else if( strcmp(key, "blockwalkalso") == 0 )
        {
            /* A 17 beneath the 27 `blockwalk=1` shows: the walk flag stays
             * the 27's, the projectiles are the 17's. */
            ok = strcmp(value, "0") == 0;
            if( ok )
                loc.blocks_projectiles = 0;
        }
        else if( strcmp(key, "blockrange") == 0 || strcmp(key, "blockrangealso") == 0 )
        {
            ok = strcmp(value, "0") == 0;
            if( ok )
                loc.blocks_projectiles = 0;
        }
        else if( strcmp(key, "active") == 0 )
            ok = recdef_int(value, &loc.is_interactive);
        else if( strcmp(key, "contourground") == 0 )
            ok = recdef_int(value, &loc.contoured_ground);
        else if( strcmp(key, "contourgroundtype") == 0 )
            ok = recdef_int(value, &loc.contour_ground_type) && loc.contour_ground_type >= 1
                 && loc.contour_ground_type <= 5;
        else if( strcmp(key, "contourgroundparam") == 0 )
            ok = recdef_int(value, &loc.contour_ground_param);
        else if( strcmp(key, "sharelight") == 0 )
        {
            ok = strcmp(value, "1") == 0;
            loc.sharelight = ok;
        }
        else if( strcmp(key, "occlude") == 0 )
        {
            ok = strcmp(value, "1") == 0;
            loc.occlude = ok;
        }
        else if( strcmp(key, "anim") == 0 )
            ok = recdef_ref(ctx, "seq", value, &loc.seq_id);
        else if( strcmp(key, "wallwidth") == 0 )
            ok = recdef_int(value, &loc.wall_width);
        else if( strcmp(key, "ambient") == 0 )
            ok = recdef_int(value, &loc.ambient);
        else if( strcmp(key, "contrast") == 0 )
            ok = recdef_int(value, &loc.contrast);
        else if( strcmp(key, "mapfunction") == 0 )
            ok = recdef_int(value, &loc.map_function_id);
        else if( strcmp(key, "category") == 0 )
            ok = recdef_ref(ctx, "category", value, &loc.category);
        else if( strcmp(key, "mirror") == 0 )
        {
            ok = strcmp(value, "1") == 0;
            loc.mirrored = ok;
        }
        else if( strcmp(key, "shadow") == 0 )
        {
            ok = strcmp(value, "no") == 0;
            if( ok )
                loc.shadowed = 0;
        }
        else if( strcmp(key, "resizex") == 0 )
            ok = recdef_int(value, &loc.resize_x);
        else if( strcmp(key, "resizey") == 0 )
            ok = recdef_int(value, &loc.resize_height);
        else if( strcmp(key, "resizez") == 0 )
            ok = recdef_int(value, &loc.resize_z);
        else if( strcmp(key, "mapscene") == 0 )
            ok = recdef_int(value, &loc.map_scene_id);
        else if( strcmp(key, "forceapproach") == 0 )
            ok = recdef_int(value, &loc.force_approach);
        else if( strcmp(key, "offsetx") == 0 )
            ok = recdef_int(value, &loc.offset_x);
        else if( strcmp(key, "offsety") == 0 )
            ok = recdef_int(value, &loc.offset_y);
        else if( strcmp(key, "offsetz") == 0 )
            ok = recdef_int(value, &loc.offset_z);
        else if( strcmp(key, "forcedecor") == 0 )
        {
            ok = strcmp(value, "1") == 0;
            loc.obstructs_ground = ok;
        }
        else if( strcmp(key, "breakroutefinding") == 0 )
        {
            ok = strcmp(value, "1") == 0;
            loc.break_routefinding = ok;
        }
        else if( strcmp(key, "raiseobject") == 0 )
            ok = recdef_int(value, &loc.support_items);
        else if( strcmp(key, "soundid") == 0 )
            ok = recdef_int(value, &loc.ambient_sound_id);
        else if( strcmp(key, "soundmintick") == 0 )
            ok = recdef_int(value, &loc.ambient_sound_ticks_min);
        else if( strcmp(key, "soundmaxtick") == 0 )
            ok = recdef_int(value, &loc.ambient_sound_ticks_max);
        else if( strcmp(key, "sounddistance") == 0 )
            ok = recdef_int(value, &loc.ambient_sound_distance);
        else if( strcmp(key, "soundretain") == 0 )
            ok = recdef_int(value, &loc.ambient_sound_retain);
        else if( strcmp(key, "randomanimstart") == 0 )
        {
            ok = strcmp(value, "no") == 0;
            if( ok )
                loc.seq_random_start = false;
        }
        else if( strcmp(key, "deferanimchange") == 0 )
        {
            ok = strcmp(value, "yes") == 0;
            loc.defer_anim_change = ok;
        }
        else
        {
            /* Params, sub / conditional ops, campaigns, random anims, the
             * sound fades: no reader in the client's loc record here. */
            report->stated--;
            report->skipped++;
        }
        if( !ok )
            recdef_note_bad(report, key, value);
    }

    if( !recdef_pairs(&block, "recol", &recol_s, &recol_d) )
    {
        recdef_note_bad(report, "recol", "(mismatched s/d pairs)");
        recol_s.count = recol_d.count = 0;
    }
    if( !recdef_pairs(&block, "retex", &retex_s, &retex_d) )
    {
        recdef_note_bad(report, "retex", "(mismatched s/d pairs)");
        retex_s.count = retex_d.count = 0;
    }

    /* The model table: `models=` is opcode 5's shape (one list, no shapes: a
     * count of 1 with `shapes` NULL is what the decoder makes of it);
     * `shape<N>=` rows are opcode 1's. */
    flat_models_ptr = flat.items;
    flat_length = flat.count;
    if( flat_stated && shape_rows == 0 )
    {
        if( flat.count > 0 )
        {
            loc.shapes = NULL;
            loc.models = &flat_models_ptr;
            loc.lengths = &flat_length;
            loc.shapes_and_model_count = 1;
        }
    }
    else if( shape_rows > 0 )
    {
        loc.shapes = shape_of.items;
        loc.models = shape_models;
        loc.lengths = shape_lengths;
        loc.shapes_and_model_count = shape_rows;
    }
    /* The default sits last, where the decoder parks it. */
    if( multi_stated && transforms.count >= 2 )
    {
        loc.transforms = transforms.items;
        loc.transform_count = transforms.count;
    }
    loc.ambient_sound_ids = sounds.items;
    loc.ambient_sound_id_count = sounds.count;
    loc.recolors_from = recol_s.items;
    loc.recolors_to = recol_d.items;
    loc.recolor_count = recol_s.count;
    loc.retextures_from = retex_s.items;
    loc.retextures_to = retex_d.items;
    loc.retexture_count = retex_s.count;

    RSCache_Dat2ConfigLocFinish(&loc, (unsigned)RSCache_Dat2ConfigLocFlags(ctx->profile));
    loc._id = id;
    out = ToriRS_LocationFromRSCacheDat2(id, &loc);
    assert(out);

    /* Every pointer above is this function's (the adaptor copied): the
     * struct gives none of them to the rscache allocator. */
    loc.name = NULL;
    loc.desc = NULL;
    for( int i = 0; i < 10; i++ )
        loc.actions[i] = NULL;
    loc.shapes = NULL;
    loc.models = NULL;
    loc.lengths = NULL;
    loc.shapes_and_model_count = 0;
    loc.transforms = NULL;
    loc.ambient_sound_ids = NULL;
    loc.recolors_from = loc.recolors_to = NULL;
    loc.retextures_from = loc.retextures_to = NULL;
    RSCache_Dat2ConfigLocFreeInplace(&loc);

    for( int i = 0; i < (int)(sizeof(owned_text) / sizeof(owned_text[0])); i++ )
        ToriMem_Free(ToriMem_Editor(), owned_text[i]);
    for( int r = 0; r < shape_rows; r++ )
        ToriMem_Free(ToriMem_Editor(), shape_models[r]);
    ToriMem_Free(ToriMem_Editor(), shape_models);
    ToriMem_Free(ToriMem_Editor(), shape_lengths);
    recdef_ints_free(&shape_of);
    recdef_ints_free(&flat);
    recdef_ints_free(&transforms);
    recdef_ints_free(&sounds);
    recdef_ints_free(&recol_s);
    recdef_ints_free(&recol_d);
    recdef_ints_free(&retex_s);
    recdef_ints_free(&retex_d);
    recdef_block_free(&block);
    return out;
}

/* ---- npc ------------------------------------------------------------------- */

struct ToriRS_Npctype*
Editor_RecDefNpc(
    const struct Editor_RecDefContext* ctx,
    int id,
    const char* text,
    size_t length,
    struct Editor_RecDefReport* report)
{
    /* The animation keys: each names one field of the decode struct. */
    static const struct
    {
        const char* key;
        size_t offset;
    } k_anims[] = {
        { "readyanim", offsetof(struct RSCache_Dat2ConfigNpc, standing_animation) },
        { "walkanim", offsetof(struct RSCache_Dat2ConfigNpc, walking_animation) },
        { "walkanim_b", offsetof(struct RSCache_Dat2ConfigNpc, rotate180_animation) },
        { "walkanim_l", offsetof(struct RSCache_Dat2ConfigNpc, rotate_left_animation) },
        { "walkanim_r", offsetof(struct RSCache_Dat2ConfigNpc, rotate_right_animation) },
        { "idleleftanim", offsetof(struct RSCache_Dat2ConfigNpc, idle_rotate_left_animation) },
        { "idlerightanim", offsetof(struct RSCache_Dat2ConfigNpc, idle_rotate_right_animation) },
        { "runanim", offsetof(struct RSCache_Dat2ConfigNpc, run_animation) },
        { "runanim_b", offsetof(struct RSCache_Dat2ConfigNpc, run_rotate180_animation) },
        { "runanim_l", offsetof(struct RSCache_Dat2ConfigNpc, run_rotate_left_animation) },
        { "runanim_r", offsetof(struct RSCache_Dat2ConfigNpc, run_rotate_right_animation) },
        { "crawlanim", offsetof(struct RSCache_Dat2ConfigNpc, crawl_animation) },
        { "crawlanim_b", offsetof(struct RSCache_Dat2ConfigNpc, crawl_rotate180_animation) },
        { "crawlanim_l", offsetof(struct RSCache_Dat2ConfigNpc, crawl_rotate_left_animation) },
        { "crawlanim_r", offsetof(struct RSCache_Dat2ConfigNpc, crawl_rotate_right_animation) },
    };
    /* The plain numbers. */
    static const struct
    {
        const char* key;
        size_t offset;
    } k_ints[] = {
        { "size", offsetof(struct RSCache_Dat2ConfigNpc, size) },
        { "vislevel", offsetof(struct RSCache_Dat2ConfigNpc, combat_level) },
        { "resizeh", offsetof(struct RSCache_Dat2ConfigNpc, width_scale) },
        { "resizev", offsetof(struct RSCache_Dat2ConfigNpc, height_scale) },
        { "ambient", offsetof(struct RSCache_Dat2ConfigNpc, ambient) },
        { "contrast", offsetof(struct RSCache_Dat2ConfigNpc, contrast) },
        { "turnspeed", offsetof(struct RSCache_Dat2ConfigNpc, rotation_speed) },
        { "height", offsetof(struct RSCache_Dat2ConfigNpc, height) },
        { "soundidle", offsetof(struct RSCache_Dat2ConfigNpc, sound_idle) },
        { "soundcrawl", offsetof(struct RSCache_Dat2ConfigNpc, sound_crawl) },
        { "soundwalk", offsetof(struct RSCache_Dat2ConfigNpc, sound_walk) },
        { "soundrun", offsetof(struct RSCache_Dat2ConfigNpc, sound_run) },
        { "soundradius", offsetof(struct RSCache_Dat2ConfigNpc, sound_radius) },
        { "soundvolume", offsetof(struct RSCache_Dat2ConfigNpc, ambient_sound_volume) },
        { "bastype", offsetof(struct RSCache_Dat2ConfigNpc, bas_type_id) },
        { "footprintsize", offsetof(struct RSCache_Dat2ConfigNpc, footprint_size) },
        { "overlaptint", offsetof(struct RSCache_Dat2ConfigNpc, overlap_tint_hsl) },
    };
    struct Editor_RecDefReport local;
    struct recdef_block block;
    struct RSCache_Dat2ConfigNpc npc;
    struct ToriRS_Npctype* out;
    struct recdef_ints models = { 0 };
    struct recdef_ints heads = { 0 };
    struct recdef_ints transforms = { 0 };
    struct recdef_ints icon_archive = { 0 };
    struct recdef_ints icon_sprite = { 0 };
    struct recdef_ints recol_s = { 0 };
    struct recdef_ints recol_d = { 0 };
    struct recdef_ints retex_s = { 0 };
    struct recdef_ints retex_d = { 0 };
    short* icon_sprites = NULL;
    char* owned_text[2 + 5] = { 0 };

    assert(ctx);
    assert(ctx->profile);
    assert(id >= 0);
    assert(text || length == 0);
    if( !report )
        report = &local;
    memset(report, 0, sizeof(*report));
    recdef_block_read(&block, text, length);
    if( block.count == 0 )
    {
        recdef_block_free(&block);
        return NULL;
    }

    memset(&npc, 0, sizeof(npc));
    RSCache_Dat2ConfigNpcInit(&npc);
    for( int i = 0; i < block.count; i++ )
    {
        const char* key = block.fields[i].key;
        const char* value = block.fields[i].value;
        int ok = 1;
        int index;
        int handled = 0;

        if( strcmp(value, "default") == 0 )
            continue;
        report->stated++;
        for( size_t a = 0; a < sizeof(k_anims) / sizeof(k_anims[0]) && !handled; a++ )
            if( strcmp(key, k_anims[a].key) == 0 )
            {
                ok = recdef_ref(ctx, "seq", value, (int*)((char*)&npc + k_anims[a].offset));
                handled = 1;
            }
        for( size_t n = 0; n < sizeof(k_ints) / sizeof(k_ints[0]) && !handled; n++ )
            if( strcmp(key, k_ints[n].key) == 0 )
            {
                ok = recdef_int(value, (int*)((char*)&npc + k_ints[n].offset));
                handled = 1;
            }
        if( handled )
        {
        }
        else if( strcmp(key, "name") == 0 )
        {
            ToriMem_Free(ToriMem_Editor(), owned_text[0]);
            npc.name = owned_text[0] = recdef_text(value);
        }
        else if( strcmp(key, "desc") == 0 )
        {
            ToriMem_Free(ToriMem_Editor(), owned_text[1]);
            npc.desc = owned_text[1] = recdef_text(value);
        }
        else if( strcmp(key, "model") == 0 || strcmp(key, "head") == 0 || strcmp(key, "headicon") == 0
                 || strcmp(key, "multinpc") == 0 )
            ok = strcmp(value, "empty") == 0;
        else if( (index = recdef_indexed(key, "model")) >= 0 )
        {
            int model = 0;
            ok = recdef_int(value, &model);
            if( ok )
                recdef_ints_set(&models, index, model);
        }
        else if( (index = recdef_indexed(key, "headicon")) >= 0 )
        {
            int pair[2];
            ok = recdef_int_list(value, pair, 2) == 2;
            if( ok )
            {
                recdef_ints_set(&icon_archive, index, pair[0]);
                recdef_ints_set(&icon_sprite, index, pair[1]);
            }
        }
        else if( (index = recdef_indexed(key, "head")) >= 0 )
        {
            int model = 0;
            ok = recdef_int(value, &model);
            if( ok )
                recdef_ints_set(&heads, index, model);
        }
        else if( (index = recdef_indexed(key, "multinpc")) >= 0 )
        {
            int target = -1;
            ok = recdef_ref(ctx, "npc", value, &target);
            if( ok )
                recdef_ints_set(&transforms, index, target);
        }
        else if( strcmp(key, "multivarbit") == 0 )
            ok = recdef_ref(ctx, "varbit", value, &npc.varbit_id);
        else if( strcmp(key, "multivarp") == 0 )
            ok = recdef_ref(ctx, "varp", value, &npc.varp_index);
        else if( strncmp(key, "recol", 5) == 0 || strncmp(key, "retex", 5) == 0 )
        {
            /* Pairs, collected below. */
        }
        else if( (index = recdef_indexed(key, "op")) >= 0 && index < 5 )
        {
            /* The decoder holds a stated "Hidden" as an empty slot. */
            ToriMem_Free(ToriMem_Editor(), owned_text[2 + index]);
            owned_text[2 + index] = recdef_text(value);
            npc.actions[index] = strcmp(owned_text[2 + index], "Hidden") == 0 ? NULL : owned_text[2 + index];
        }
        else if( (index = recdef_indexed(key, "stat")) >= 0 && index < 6 )
            ok = recdef_int(value, &npc.stats[index]);
        else if( strcmp(key, "category") == 0 )
            ok = recdef_ref(ctx, "category", value, &npc.category);
        else if( strcmp(key, "minimap") == 0 )
        {
            ok = strcmp(value, "no") == 0;
            if( ok )
                npc.is_minimap_visible = false;
        }
        else if( strcmp(key, "alwaysontop") == 0 )
        {
            ok = strcmp(value, "yes") == 0;
            if( ok )
            {
                npc.has_render_priority = true;
                npc.render_priority = 1;
            }
        }
        else if( strcmp(key, "renderpriority") == 0 )
        {
            ok = strcmp(value, "2") == 0;
            if( ok )
            {
                npc.has_render_priority = true;
                npc.render_priority = 2;
            }
        }
        else if( strcmp(key, "follower") == 0 )
        {
            ok = strcmp(value, "yes") == 0;
            if( ok )
            {
                npc.is_pet = true;
                npc.low_priority_follower_ops = true;
            }
        }
        else if( strcmp(key, "interactable") == 0 )
        {
            ok = strcmp(value, "no") == 0;
            if( ok )
                npc.is_interactable = false;
        }
        else if( strcmp(key, "rotationflag") == 0 )
        {
            ok = strcmp(value, "no") == 0;
            if( ok )
                npc.rotation_flag = false;
        }
        else if( strcmp(key, "pet") == 0 )
        {
            ok = strcmp(value, "yes") == 0;
            if( ok )
                npc.is_pet = true;
        }
        else if( strcmp(key, "lowpriorityops") == 0 )
        {
            ok = strcmp(value, "yes") == 0;
            if( ok )
                npc.low_priority_follower_ops = true;
        }
        else if( strcmp(key, "opcode129") == 0 )
        {
            ok = strcmp(value, "yes") == 0;
            if( ok )
                npc.unknown1 = true;
        }
        else if( strcmp(key, "hideforoverlap") == 0 )
        {
            ok = strcmp(value, "yes") == 0;
            if( ok )
                npc.can_hide_for_overlap = true;
        }
        else if( strcmp(key, "idleanimrestart") == 0 )
        {
            ok = strcmp(value, "yes") == 0;
            if( ok )
                npc.idle_anim_restart = true;
        }
        else if( strcmp(key, "zbuf") == 0 )
        {
            ok = strcmp(value, "no") == 0;
            if( ok )
                npc.zbuf = false;
        }
        else
        {
            /* Params and the sub / conditional ops. */
            report->stated--;
            report->skipped++;
        }
        if( !ok )
            recdef_note_bad(report, key, value);
    }

    if( !recdef_pairs(&block, "recol", &recol_s, &recol_d) )
    {
        recdef_note_bad(report, "recol", "(mismatched s/d pairs)");
        recol_s.count = recol_d.count = 0;
    }
    if( !recdef_pairs(&block, "retex", &retex_s, &retex_d) )
    {
        recdef_note_bad(report, "retex", "(mismatched s/d pairs)");
        retex_s.count = retex_d.count = 0;
    }

    npc.models = models.items;
    npc.models_count = models.count;
    npc.chathead_models = heads.items;
    npc.chathead_models_count = heads.count;
    /* A multinpc table is the list and its trailing default. */
    if( transforms.count >= 2 )
    {
        npc.configs = transforms.items;
        npc.configs_count = transforms.count;
    }
    if( icon_archive.count > 0 )
    {
        icon_sprites = ToriMem_Alloc(ToriMem_Editor(), (size_t)icon_archive.count * sizeof(short));
        assert(icon_sprites);
        for( int i = 0; i < icon_archive.count; i++ )
            icon_sprites[i] = (short)icon_sprite.items[i];
        npc.head_icon_archive_ids = icon_archive.items;
        npc.head_icon_sprite_index = icon_sprites;
        npc.head_icon_count = icon_archive.count;
    }
    npc.recolor_to_find = recol_s.items;
    npc.recolor_to_replace = recol_d.items;
    npc.recolor_count = recol_s.count;
    npc.retexture_to_find = retex_s.items;
    npc.retexture_to_replace = retex_d.items;
    npc.retexture_count = retex_s.count;

    RSCache_Dat2ConfigNpcFinish(&npc, (unsigned)RSCache_Dat2ConfigNpcFlags(ctx->profile));
    out = ToriRS_NpctypeFromRSCacheDat2(id, &npc);
    assert(out);

    npc.name = NULL;
    npc.desc = NULL;
    for( int i = 0; i < 5; i++ )
        npc.actions[i] = NULL;
    npc.models = NULL;
    npc.chathead_models = NULL;
    npc.configs = NULL;
    npc.head_icon_archive_ids = NULL;
    npc.head_icon_sprite_index = NULL;
    npc.recolor_to_find = npc.recolor_to_replace = NULL;
    npc.retexture_to_find = npc.retexture_to_replace = NULL;
    RSCache_Dat2ConfigNpcFreeInplace(&npc);

    for( int i = 0; i < (int)(sizeof(owned_text) / sizeof(owned_text[0])); i++ )
        ToriMem_Free(ToriMem_Editor(), owned_text[i]);
    ToriMem_Free(ToriMem_Editor(), icon_sprites);
    recdef_ints_free(&models);
    recdef_ints_free(&heads);
    recdef_ints_free(&transforms);
    recdef_ints_free(&icon_archive);
    recdef_ints_free(&icon_sprite);
    recdef_ints_free(&recol_s);
    recdef_ints_free(&recol_d);
    recdef_ints_free(&retex_s);
    recdef_ints_free(&retex_d);
    recdef_block_free(&block);
    return out;
}

/* ---- obj ------------------------------------------------------------------- */

struct ToriRS_Objtype*
Editor_RecDefObj(
    const struct Editor_RecDefContext* ctx,
    int id,
    const char* text,
    size_t length,
    struct Editor_RecDefReport* report)
{
    static const struct
    {
        const char* key;
        size_t offset;
    } k_ints[] = {
        { "model", offsetof(struct RSCache_Dat2ConfigObj, inventory_model_id) },
        { "2dzoom", offsetof(struct RSCache_Dat2ConfigObj, zoom2d) },
        { "2dxan", offsetof(struct RSCache_Dat2ConfigObj, xan2d) },
        { "2dyan", offsetof(struct RSCache_Dat2ConfigObj, yan2d) },
        { "2dzan", offsetof(struct RSCache_Dat2ConfigObj, zan2d) },
        { "2dxof", offsetof(struct RSCache_Dat2ConfigObj, offset_x2d) },
        { "2dyof", offsetof(struct RSCache_Dat2ConfigObj, offset_y2d) },
        { "resizex", offsetof(struct RSCache_Dat2ConfigObj, resize_x) },
        { "resizey", offsetof(struct RSCache_Dat2ConfigObj, resize_y) },
        { "resizez", offsetof(struct RSCache_Dat2ConfigObj, resize_z) },
        { "cost", offsetof(struct RSCache_Dat2ConfigObj, cost) },
        { "stackable", offsetof(struct RSCache_Dat2ConfigObj, stacking_behaviour) },
        { "wearpos", offsetof(struct RSCache_Dat2ConfigObj, wearpos_1) },
        { "wearpos2", offsetof(struct RSCache_Dat2ConfigObj, wearpos_2) },
        { "wearpos3", offsetof(struct RSCache_Dat2ConfigObj, wearpos_3) },
        { "ambient", offsetof(struct RSCache_Dat2ConfigObj, ambient) },
        { "contrast", offsetof(struct RSCache_Dat2ConfigObj, contrast) },
        { "team", offsetof(struct RSCache_Dat2ConfigObj, team) },
        { "weight", offsetof(struct RSCache_Dat2ConfigObj, weight) },
        { "shiftclickdrop", offsetof(struct RSCache_Dat2ConfigObj, shift_click_drop_index) },
        { "manwear", offsetof(struct RSCache_Dat2ConfigObj, male_model_0) },
        { "manwearoff", offsetof(struct RSCache_Dat2ConfigObj, male_offset) },
        { "manwear2", offsetof(struct RSCache_Dat2ConfigObj, male_model_1) },
        { "manwear3", offsetof(struct RSCache_Dat2ConfigObj, male_model_2) },
        { "manhead", offsetof(struct RSCache_Dat2ConfigObj, male_head_model) },
        { "manhead2", offsetof(struct RSCache_Dat2ConfigObj, male_head_model_2) },
        { "womanwear", offsetof(struct RSCache_Dat2ConfigObj, female_model_0) },
        { "womanwearoff", offsetof(struct RSCache_Dat2ConfigObj, female_offset) },
        { "womanwear2", offsetof(struct RSCache_Dat2ConfigObj, female_model_1) },
        { "womanwear3", offsetof(struct RSCache_Dat2ConfigObj, female_model_2) },
        { "womanhead", offsetof(struct RSCache_Dat2ConfigObj, female_head_model) },
        { "womanhead2", offsetof(struct RSCache_Dat2ConfigObj, female_head_model_2) },
    };
    /* The fields that name another obj. */
    static const struct
    {
        const char* key;
        size_t offset;
    } k_objs[] = {
        { "certlink", offsetof(struct RSCache_Dat2ConfigObj, noted_id) },
        { "certtemplate", offsetof(struct RSCache_Dat2ConfigObj, noted_template) },
        { "boughtlink", offsetof(struct RSCache_Dat2ConfigObj, bought_id) },
        { "boughttemplate", offsetof(struct RSCache_Dat2ConfigObj, bought_template_id) },
        { "placeholderlink", offsetof(struct RSCache_Dat2ConfigObj, placeholder_id) },
        { "placeholdertemplate", offsetof(struct RSCache_Dat2ConfigObj, placeholder_template_id) },
    };
    struct Editor_RecDefReport local;
    struct recdef_block block;
    struct RSCache_Dat2ConfigObj obj;
    struct ToriRS_Objtype* out;
    struct recdef_ints recol_s = { 0 };
    struct recdef_ints recol_d = { 0 };
    struct recdef_ints retex_s = { 0 };
    struct recdef_ints retex_d = { 0 };
    /* name, desc, op1..5, ifop1..5 */
    char* owned_text[2 + 5 + 5] = { 0 };
    char* init_name;

    assert(ctx);
    assert(ctx->profile);
    assert(id >= 0);
    assert(text || length == 0);
    if( !report )
        report = &local;
    memset(report, 0, sizeof(*report));
    recdef_block_read(&block, text, length);
    if( block.count == 0 )
    {
        recdef_block_free(&block);
        return NULL;
    }

    RSCache_Dat2ConfigObjInit(&obj);
    /* The decoder's default name ("null") is the rscache allocator's: it goes
     * back there, whatever name the text states. */
    init_name = obj.name;
    for( int i = 0; i < block.count; i++ )
    {
        const char* key = block.fields[i].key;
        const char* value = block.fields[i].value;
        int ok = 1;
        int index;
        int handled = 0;

        if( strcmp(value, "default") == 0 )
            continue;
        report->stated++;
        for( size_t n = 0; n < sizeof(k_ints) / sizeof(k_ints[0]) && !handled; n++ )
            if( strcmp(key, k_ints[n].key) == 0 )
            {
                ok = recdef_int(value, (int*)((char*)&obj + k_ints[n].offset));
                handled = 1;
            }
        for( size_t n = 0; n < sizeof(k_objs) / sizeof(k_objs[0]) && !handled; n++ )
            if( strcmp(key, k_objs[n].key) == 0 )
            {
                ok = recdef_ref(ctx, "obj", value, (int*)((char*)&obj + k_objs[n].offset));
                handled = 1;
            }
        if( handled )
        {
        }
        else if( strcmp(key, "name") == 0 )
        {
            ToriMem_Free(ToriMem_Editor(), owned_text[0]);
            obj.name = owned_text[0] = recdef_text(value);
        }
        else if( strcmp(key, "desc") == 0 )
        {
            ToriMem_Free(ToriMem_Editor(), owned_text[1]);
            obj.examine = owned_text[1] = recdef_text(value);
        }
        else if( (index = recdef_indexed(key, "countobj")) >= 0 )
        {
            char name[64];
            const char* comma = strchr(value, ',');
            ok = index < 10 && comma && (size_t)(comma - value) < sizeof(name);
            if( ok )
            {
                memcpy(name, value, (size_t)(comma - value));
                name[comma - value] = '\0';
                ok = recdef_ref(ctx, "obj", name, &obj.count_obj[index]) && recdef_int(comma + 1, &obj.count_co[index]);
            }
        }
        else if( (index = recdef_indexed(key, "ifop")) >= 0 )
        {
            ok = index < 5;
            if( ok )
            {
                ToriMem_Free(ToriMem_Editor(), owned_text[7 + index]);
                obj.if_actions[index] = owned_text[7 + index] = recdef_text(value);
            }
        }
        else if( (index = recdef_indexed(key, "op")) >= 0 )
        {
            ok = index < 5;
            if( ok )
            {
                /* The client hides an op spelled "hidden". */
                ToriMem_Free(ToriMem_Editor(), owned_text[2 + index]);
                owned_text[2 + index] = recdef_text(value);
                obj.actions[index] = strcasecmp(owned_text[2 + index], "Hidden") == 0 ? NULL : owned_text[2 + index];
            }
        }
        else if( strcmp(key, "tradeable") == 0 )
        {
            ok = strcmp(value, "no") == 0;
            if( ok )
                obj.tradeable = false;
        }
        else if( strcmp(key, "getradeable") == 0 )
        {
            ok = strcmp(value, "yes") == 0;
            if( ok )
                obj.ge_tradeable = true;
        }
        else if( strcmp(key, "members") == 0 )
        {
            ok = strcmp(value, "yes") == 0;
            if( ok )
                obj.is_members = true;
        }
        else if( strcmp(key, "category") == 0 )
            ok = recdef_ref(ctx, "category", value, &obj.category);
        else if( strncmp(key, "recol", 5) == 0 || strncmp(key, "retex", 5) == 0 )
        {
            /* Pairs, collected below. */
        }
        else
        {
            /* Params, sub actions, the sub / conditional ops, opcode 161 and
             * flag 251. */
            report->stated--;
            report->skipped++;
        }
        if( !ok )
            recdef_note_bad(report, key, value);
    }

    if( !recdef_pairs(&block, "recol", &recol_s, &recol_d) )
    {
        recdef_note_bad(report, "recol", "(mismatched s/d pairs)");
        recol_s.count = recol_d.count = 0;
    }
    if( !recdef_pairs(&block, "retex", &retex_s, &retex_d) )
    {
        recdef_note_bad(report, "retex", "(mismatched s/d pairs)");
        retex_s.count = retex_d.count = 0;
    }
    obj.recolors_from = recol_s.items;
    obj.recolors_to = recol_d.items;
    obj.recolor_count = recol_s.count;
    obj.retextures_from = retex_s.items;
    obj.retextures_to = retex_d.items;
    obj.retexture_count = retex_s.count;

    obj._id = id;
    out = ToriRS_ObjtypeFromRSCacheDat2(id, &obj);
    assert(out);

    obj.name = init_name;
    obj.examine = NULL;
    for( int i = 0; i < 5; i++ )
    {
        obj.actions[i] = NULL;
        obj.if_actions[i] = NULL;
    }
    obj.recolors_from = obj.recolors_to = NULL;
    obj.retextures_from = obj.retextures_to = NULL;
    RSCache_Dat2ConfigObjFreeInplace(&obj);

    for( int i = 0; i < (int)(sizeof(owned_text) / sizeof(owned_text[0])); i++ )
        ToriMem_Free(ToriMem_Editor(), owned_text[i]);
    recdef_ints_free(&recol_s);
    recdef_ints_free(&recol_d);
    recdef_ints_free(&retex_s);
    recdef_ints_free(&retex_d);
    recdef_block_free(&block);
    return out;
}

/* ---- two records, field by field ------------------------------------------- */

struct recdef_diff
{
    char* out;
    size_t size;
    size_t used;
    int count;
};

static void
recdef_diff_note(
    struct recdef_diff* diff,
    const char* field)
{
    if( diff->out && diff->used + 1 < diff->size )
    {
        diff->used += (size_t)snprintf(diff->out + diff->used, diff->size - diff->used, "%s%s",
            diff->count ? " " : "", field);
        if( diff->used >= diff->size )
            diff->used = diff->size - 1;
    }
    diff->count++;
}

static int
recdef_same_ints(
    const int* a,
    int a_count,
    const int* b,
    int b_count)
{
    if( a_count != b_count )
        return 0;
    return a_count <= 0 || memcmp(a, b, (size_t)a_count * sizeof(int)) == 0;
}

#define RECDEF_DIFF_INT(field)                                                                    \
    do                                                                                            \
    {                                                                                             \
        if( a->field != b->field )                                                                \
            recdef_diff_note(&diff, #field);                                                      \
    } while( 0 )
#define RECDEF_DIFF_BYTES(field)                                                                  \
    do                                                                                            \
    {                                                                                             \
        if( memcmp(a->field, b->field, sizeof(a->field)) != 0 )                                   \
            recdef_diff_note(&diff, #field);                                                      \
    } while( 0 )
#define RECDEF_DIFF_TEXT(field)                                                                   \
    do                                                                                            \
    {                                                                                             \
        if( strcmp(a->field, b->field) != 0 )                                                     \
            recdef_diff_note(&diff, #field);                                                      \
    } while( 0 )
#define RECDEF_DIFF_LIST(list, count)                                                             \
    do                                                                                            \
    {                                                                                             \
        if( !recdef_same_ints(a->list, a->count, b->list, b->count) )                             \
            recdef_diff_note(&diff, #list);                                                       \
    } while( 0 )

/** The menu actions, text by text (the bytes past a string's end are
 *  whatever the copy left). */
static int
recdef_same_actions(
    const char a[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN],
    const char b[TORIRS_MENU_ACTION_SLOTS][TORIRS_MENU_ACTION_LEN])
{
    for( int i = 0; i < TORIRS_MENU_ACTION_SLOTS; i++ )
        if( strcmp(a[i], b[i]) != 0 )
            return 0;
    return 1;
}

int
Editor_RecDefLocDiff(
    const struct ToriRS_Location* a,
    const struct ToriRS_Location* b,
    char* out,
    size_t out_size)
{
    struct recdef_diff diff = { out, out_size, 0, 0 };
    int models_same;

    assert(a);
    assert(b);
    assert(out || out_size == 0);
    if( out_size > 0 )
        out[0] = 0;
    RECDEF_DIFF_TEXT(name);
    RECDEF_DIFF_TEXT(desc);
    if( !recdef_same_actions(a->actions, b->actions) )
        recdef_diff_note(&diff, "actions");
    models_same = a->shapes_and_model_count == b->shapes_and_model_count && (a->shapes == NULL) == (b->shapes == NULL);
    for( int i = 0; models_same && i < a->shapes_and_model_count; i++ )
        models_same = (!a->shapes || a->shapes[i] == b->shapes[i])
                      && recdef_same_ints(a->models[i], a->lengths[i], b->models[i], b->lengths[i]);
    if( !models_same )
        recdef_diff_note(&diff, "models");
    RECDEF_DIFF_INT(size_x);
    RECDEF_DIFF_INT(size_z);
    RECDEF_DIFF_INT(blocks_walk);
    RECDEF_DIFF_INT(blocks_projectiles);
    RECDEF_DIFF_INT(force_approach);
    RECDEF_DIFF_INT(wall_width);
    RECDEF_DIFF_INT(seq_id);
    RECDEF_DIFF_INT(defer_anim_change);
    RECDEF_DIFF_INT(contoured_ground);
    RECDEF_DIFF_INT(contour_ground_type);
    RECDEF_DIFF_INT(contour_ground_param);
    RECDEF_DIFF_INT(sharelight);
    RECDEF_DIFF_INT(occlude);
    RECDEF_DIFF_INT(shadowed);
    RECDEF_DIFF_INT(ambient);
    RECDEF_DIFF_INT(contrast);
    RECDEF_DIFF_INT(mirrored);
    RECDEF_DIFF_INT(resize_x);
    RECDEF_DIFF_INT(resize_height);
    RECDEF_DIFF_INT(resize_z);
    RECDEF_DIFF_INT(offset_x);
    RECDEF_DIFF_INT(offset_y);
    RECDEF_DIFF_INT(offset_z);
    RECDEF_DIFF_LIST(recolors_from, recolor_count);
    RECDEF_DIFF_LIST(recolors_to, recolor_count);
    RECDEF_DIFF_LIST(retextures_from, retexture_count);
    RECDEF_DIFF_LIST(retextures_to, retexture_count);
    RECDEF_DIFF_INT(map_scene_id);
    RECDEF_DIFF_INT(map_function_id);
    RECDEF_DIFF_INT(ambient_sound_id);
    RECDEF_DIFF_INT(ambient_sound_distance);
    RECDEF_DIFF_INT(ambient_sound_retain);
    RECDEF_DIFF_INT(ambient_sound_ticks_min);
    RECDEF_DIFF_INT(ambient_sound_ticks_max);
    RECDEF_DIFF_LIST(ambient_sound_ids, ambient_sound_id_count);
    RECDEF_DIFF_INT(transform_varbit);
    RECDEF_DIFF_INT(transform_varp);
    RECDEF_DIFF_LIST(transforms, transform_count);
    RECDEF_DIFF_INT(is_interactive);
    RECDEF_DIFF_INT(raiseobject);
    RECDEF_DIFF_INT(category);
    return diff.count;
}

int
Editor_RecDefNpcDiff(
    const struct ToriRS_Npctype* a,
    const struct ToriRS_Npctype* b,
    char* out,
    size_t out_size)
{
    struct recdef_diff diff = { out, out_size, 0, 0 };

    assert(a);
    assert(b);
    assert(out || out_size == 0);
    if( out_size > 0 )
        out[0] = 0;
    RECDEF_DIFF_TEXT(name);
    RECDEF_DIFF_TEXT(desc);
    if( !recdef_same_actions(a->actions, b->actions) )
        recdef_diff_note(&diff, "actions");
    RECDEF_DIFF_INT(combat_level);
    RECDEF_DIFF_INT(size);
    RECDEF_DIFF_INT(sound_idle);
    RECDEF_DIFF_INT(sound_crawl);
    RECDEF_DIFF_INT(sound_walk);
    RECDEF_DIFF_INT(sound_run);
    RECDEF_DIFF_INT(sound_radius);
    RECDEF_DIFF_INT(ambient_sound_volume);
    RECDEF_DIFF_LIST(models, models_count);
    RECDEF_DIFF_LIST(heads, heads_count);
    RECDEF_DIFF_LIST(recolors_from, recolor_count);
    RECDEF_DIFF_LIST(recolors_to, recolor_count);
    RECDEF_DIFF_LIST(retextures_from, retexture_count);
    RECDEF_DIFF_LIST(retextures_to, retexture_count);
    RECDEF_DIFF_INT(readyanim);
    RECDEF_DIFF_INT(walkanim);
    RECDEF_DIFF_INT(walkanim_b);
    RECDEF_DIFF_INT(walkanim_r);
    RECDEF_DIFF_INT(walkanim_l);
    RECDEF_DIFF_INT(turnanim_l);
    RECDEF_DIFF_INT(turnanim_r);
    RECDEF_DIFF_INT(runanim);
    RECDEF_DIFF_INT(runanim_b);
    RECDEF_DIFF_INT(runanim_r);
    RECDEF_DIFF_INT(runanim_l);
    RECDEF_DIFF_INT(crawlanim);
    RECDEF_DIFF_INT(crawlanim_b);
    RECDEF_DIFF_INT(crawlanim_r);
    RECDEF_DIFF_INT(crawlanim_l);
    RECDEF_DIFF_INT(turn_speed);
    RECDEF_DIFF_INT(no_rotation_flag);
    RECDEF_DIFF_INT(idle_anim_restart);
    RECDEF_DIFF_INT(width_scale);
    RECDEF_DIFF_INT(height_scale);
    RECDEF_DIFF_INT(alwaysontop);
    RECDEF_DIFF_INT(minimap_visible);
    RECDEF_DIFF_INT(interactable);
    RECDEF_DIFF_INT(head_icon_group);
    RECDEF_DIFF_INT(head_icon_index);
    RECDEF_DIFF_INT(ambient);
    RECDEF_DIFF_INT(contrast);
    RECDEF_DIFF_INT(height);
    RECDEF_DIFF_INT(footprint_size);
    RECDEF_DIFF_INT(zbuf_off);
    RECDEF_DIFF_INT(transform_varbit);
    RECDEF_DIFF_INT(transform_varp);
    RECDEF_DIFF_LIST(transforms, transform_count);
    RECDEF_DIFF_INT(category);
    return diff.count;
}

int
Editor_RecDefObjDiff(
    const struct ToriRS_Objtype* a,
    const struct ToriRS_Objtype* b,
    char* out,
    size_t out_size)
{
    struct recdef_diff diff = { out, out_size, 0, 0 };

    assert(a);
    assert(b);
    assert(out || out_size == 0);
    if( out_size > 0 )
        out[0] = 0;
    RECDEF_DIFF_TEXT(name);
    RECDEF_DIFF_TEXT(desc);
    if( !recdef_same_actions(a->inv_actions, b->inv_actions) )
        recdef_diff_note(&diff, "inv_actions");
    if( !recdef_same_actions(a->ground_actions, b->ground_actions) )
        recdef_diff_note(&diff, "ground_actions");
    RECDEF_DIFF_INT(stackable);
    RECDEF_DIFF_INT(inventory_model_id);
    RECDEF_DIFF_INT(cert_link);
    RECDEF_DIFF_INT(cert_template);
    RECDEF_DIFF_INT(placeholder_link);
    RECDEF_DIFF_INT(placeholder_template);
    RECDEF_DIFF_INT(category);
    RECDEF_DIFF_INT(cost);
    RECDEF_DIFF_INT(ge_tradeable);
    RECDEF_DIFF_INT(members);
    RECDEF_DIFF_INT(bought_template);
    RECDEF_DIFF_INT(shift_click_drop_index);
    RECDEF_DIFF_INT(team);
    RECDEF_DIFF_INT(zoom2d);
    RECDEF_DIFF_INT(xan2d);
    RECDEF_DIFF_INT(yan2d);
    RECDEF_DIFF_INT(zan2d);
    RECDEF_DIFF_INT(offset_x2d);
    RECDEF_DIFF_INT(offset_y2d);
    RECDEF_DIFF_INT(resize_x);
    RECDEF_DIFF_INT(resize_y);
    RECDEF_DIFF_INT(resize_z);
    RECDEF_DIFF_BYTES(count_obj);
    RECDEF_DIFF_BYTES(count_co);
    RECDEF_DIFF_INT(ambient);
    RECDEF_DIFF_INT(contrast);
    RECDEF_DIFF_LIST(recolors_from, recolor_count);
    RECDEF_DIFF_LIST(recolors_to, recolor_count);
    RECDEF_DIFF_LIST(retextures_from, retexture_count);
    RECDEF_DIFF_LIST(retextures_to, retexture_count);
    RECDEF_DIFF_INT(manwear);
    RECDEF_DIFF_INT(manwear2);
    RECDEF_DIFF_INT(manwear3);
    RECDEF_DIFF_INT(womanwear);
    RECDEF_DIFF_INT(womanwear2);
    RECDEF_DIFF_INT(womanwear3);
    RECDEF_DIFF_INT(manhead);
    RECDEF_DIFF_INT(manhead2);
    RECDEF_DIFF_INT(womanhead);
    RECDEF_DIFF_INT(womanhead2);
    RECDEF_DIFF_INT(wearpos);
    RECDEF_DIFF_INT(wearpos2);
    RECDEF_DIFF_INT(wearpos3);
    return diff.count;
}
