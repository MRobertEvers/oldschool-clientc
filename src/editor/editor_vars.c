#include "editor_vars.h"

#include "editor_cfg.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- names ----------------------------------------------------------------- */

static const char*
kind_prefix(enum Editor_VarKind kind)
{
    switch( kind )
    {
    case EDITOR_VAR_VARP:
        return "varp";
    case EDITOR_VAR_VARBIT:
        return "varb";
    case EDITOR_VAR_VARC:
        return "varc";
    }
    return "";
}

int
Editor_VarsNameParse(
    const char* name,
    enum Editor_VarKind* out_kind,
    int* out_id,
    char* out_base,
    size_t base_size)
{
    enum Editor_VarKind kind;
    const char* at;
    long id = 0;

    assert(name);

    if( strncmp(name, "varp", 4) == 0 )
        kind = EDITOR_VAR_VARP;
    else if( strncmp(name, "varb", 4) == 0 )
        kind = EDITOR_VAR_VARBIT;
    else if( strncmp(name, "varc", 4) == 0 )
        kind = EDITOR_VAR_VARC;
    else
        return 0;
    at = name + 4;
    if( !isdigit((unsigned char)*at) )
        return 0;
    while( isdigit((unsigned char)*at) )
    {
        if( id < 100000000 )
            id = id * 10 + (*at - '0');
        at++;
    }
    if( *at != '_' || at[1] == '\0' )
        return 0;
    if( out_kind )
        *out_kind = kind;
    if( out_id )
        *out_id = (int)id;
    if( out_base )
        snprintf(out_base, base_size, "%s", at + 1);
    return 1;
}

int
Editor_VarsName(
    enum Editor_VarKind kind,
    int id,
    const char* name,
    char* out,
    size_t out_size)
{
    enum Editor_VarKind parsed_kind;
    int parsed_id;
    const char* base = name;
    char bare[256];
    int written;

    assert(name);
    assert(out);

    if( id < 0 || name[0] == '\0' )
        return 0;
    if( Editor_VarsNameParse(name, &parsed_kind, &parsed_id, bare, sizeof(bare))
        && parsed_kind == kind && parsed_id == id )
        base = bare;
    written = snprintf(out, out_size, "%s%d_%s", kind_prefix(kind), id, base);
    return written > 0 && (size_t)written < out_size;
}

/* ---- slot tables -------------------------------------------------------------- */

static unsigned
hash_name(const char* name)
{
    unsigned hash = 2166136261u;

    for( ; *name; name++ )
    {
        hash ^= (unsigned char)*name;
        hash *= 16777619u;
    }
    return hash;
}

/** The slot of `name` in `slots` (capacity a power of two), found or empty.
 *  `name_of(user, index)` reads an entry's name. */
static int
slot_find(
    const int* slots,
    int capacity,
    const char* name,
    const char* (*name_of)(const struct Editor_VarsIndex*, int),
    const struct Editor_VarsIndex* index)
{
    unsigned mask = (unsigned)capacity - 1;
    unsigned at = hash_name(name) & mask;

    while( slots[at] >= 0 && strcmp(name_of(index, slots[at]), name) != 0 )
        at = (at + 1) & mask;
    return (int)at;
}

static const char*
varbit_name_of(
    const struct Editor_VarsIndex* index,
    int i)
{
    return index->varbits[i].name;
}

static const char*
varp_name_of(
    const struct Editor_VarsIndex* index,
    int i)
{
    return index->varps[i].name;
}

/** Rebuild a slot table for `count` entries at a load factor under 1/2. */
static void
slots_rebuild(
    int** slots,
    int* capacity,
    int count,
    const char* (*name_of)(const struct Editor_VarsIndex*, int),
    const struct Editor_VarsIndex* index)
{
    int next = 1024;

    while( next < count * 2 + 2 )
        next *= 2;
    ToriMem_Free(ToriMem_Editor(), *slots);
    *slots = ToriMem_Alloc(ToriMem_Editor(), sizeof(**slots) * (size_t)next);
    assert(*slots);
    for( int i = 0; i < next; i++ )
        (*slots)[i] = -1;
    *capacity = next;
    for( int i = 0; i < count; i++ )
        (*slots)[slot_find(*slots, next, name_of(index, i), name_of, index)] = i;
}

/* ---- index ---------------------------------------------------------------------- */

void
Editor_VarsIndexInit(struct Editor_VarsIndex* index)
{
    assert(index);
    memset(index, 0, sizeof(*index));
}

void
Editor_VarsIndexFree(struct Editor_VarsIndex* index)
{
    if( !index )
        return;
    ToriMem_Free(ToriMem_Editor(), index->varbits);
    ToriMem_Free(ToriMem_Editor(), index->varbit_members);
    ToriMem_Free(ToriMem_Editor(), index->varps);
    ToriMem_Free(ToriMem_Editor(), index->varbit_slots);
    ToriMem_Free(ToriMem_Editor(), index->varp_slots);
    ToriMem_Free(ToriMem_Editor(), index->order);
    memset(index, 0, sizeof(*index));
}

/** "[0-9]+" -> 1 and the value. */
static int
parse_int(
    const char* text,
    size_t length,
    int* out)
{
    long value = 0;

    if( length == 0 || length > 9 )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        if( !isdigit((unsigned char)text[i]) )
            return 0;
        value = value * 10 + (text[i] - '0');
    }
    *out = (int)value;
    return 1;
}

static int
copy_name(
    char* out,
    size_t out_size,
    const char* text,
    size_t length)
{
    if( length == 0 || length + 1 > out_size )
        return 0;
    memcpy(out, text, length);
    out[length] = '\0';
    return 1;
}

static int
varp_find(
    const struct Editor_VarsIndex* index,
    const char* name)
{
    int slot;

    if( index->varp_slot_capacity == 0 )
        return -1;
    slot = slot_find(index->varp_slots, index->varp_slot_capacity, name, varp_name_of, index);
    return index->varp_slots[slot];
}

static void
varp_add(
    struct Editor_VarsIndex* index,
    const char* name,
    int id)
{
    struct Editor_VarpName* entry;

    if( varp_find(index, name) >= 0 )
        return;
    if( index->varp_count == index->varp_capacity )
    {
        index->varp_capacity = index->varp_capacity ? index->varp_capacity * 2 : 1024;
        index->varps = ToriMem_Realloc(
            ToriMem_Editor(), index->varps, sizeof(*index->varps) * (size_t)index->varp_capacity);
        assert(index->varps);
    }
    entry = &index->varps[index->varp_count++];
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    entry->id = id;
    if( index->varp_count * 2 + 2 > index->varp_slot_capacity )
        slots_rebuild(
            &index->varp_slots, &index->varp_slot_capacity, index->varp_count, varp_name_of, index);
    else
        index->varp_slots[slot_find(
            index->varp_slots, index->varp_slot_capacity, name, varp_name_of, index)] =
            index->varp_count - 1;
}

void
Editor_VarsAddVarps(
    struct Editor_VarsIndex* index,
    const char* text,
    size_t length)
{
    size_t at = 0;

    assert(index);
    assert(text || length == 0);

    index->finished = 0;
    while( at < length )
    {
        struct Editor_CfgLine line;
        char name[EDITOR_VARS_NAME_MAX];
        int id;

        at = Editor_CfgLineParse(text, length, at, &line);
        if( line.kind == EDITOR_CFG_LINE_HEADER && line.closed )
        {
            enum Editor_VarKind kind;

            if( copy_name(name, sizeof(name), text + line.start + 1, line.part)
                && Editor_VarsNameParse(name, &kind, &id, NULL, 0) && kind == EDITOR_VAR_VARP )
                varp_add(index, name, id);
        }
        else if( line.kind == EDITOR_CFG_LINE_FIELD
                 && parse_int(text + line.start, line.part, &id) )
        {
            /* `<id>=<name>`, a trailing `// note` dropped. */
            const char* value = text + line.start + line.part + 1;
            size_t value_length = line.length - line.part - 1;
            const char* note = NULL;

            for( size_t i = 0; i + 1 < value_length; i++ )
            {
                if( value[i] == '/' && value[i + 1] == '/' )
                {
                    note = value + i;
                    break;
                }
            }
            if( note )
                value_length = (size_t)(note - value);
            while( value_length > 0 && isspace((unsigned char)value[value_length - 1]) )
                value_length--;
            if( copy_name(name, sizeof(name), value, value_length) )
                varp_add(index, name, id);
        }
    }
}

static int
varbit_find(
    const struct Editor_VarsIndex* index,
    const char* name)
{
    int slot;

    if( index->varbit_slot_capacity == 0 )
        return -1;
    slot = slot_find(index->varbit_slots, index->varbit_slot_capacity, name, varbit_name_of, index);
    return index->varbit_slots[slot];
}

static int
varbit_add(
    struct Editor_VarsIndex* index,
    const char* name)
{
    struct Editor_Varbit* entry;
    int found = varbit_find(index, name);

    if( found >= 0 )
        return found;
    if( index->varbit_count == index->varbit_capacity )
    {
        index->varbit_capacity = index->varbit_capacity ? index->varbit_capacity * 2 : 1024;
        index->varbits = ToriMem_Realloc(
            ToriMem_Editor(),
            index->varbits,
            sizeof(*index->varbits) * (size_t)index->varbit_capacity);
        assert(index->varbits);
    }
    entry = &index->varbits[index->varbit_count++];
    memset(entry, 0, sizeof(*entry));
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    entry->id = -1;
    entry->basevar = -1;
    entry->startbit = -1;
    entry->endbit = -1;
    if( !Editor_VarsNameParse(name, NULL, &entry->id, NULL, 0) )
        entry->id = -1;
    if( index->varbit_count * 2 + 2 > index->varbit_slot_capacity )
        slots_rebuild(
            &index->varbit_slots,
            &index->varbit_slot_capacity,
            index->varbit_count,
            varbit_name_of,
            index);
    else
        index->varbit_slots[slot_find(
            index->varbit_slots, index->varbit_slot_capacity, name, varbit_name_of, index)] =
            index->varbit_count - 1;
    return index->varbit_count - 1;
}

void
Editor_VarsAddVarbits(
    struct Editor_VarsIndex* index,
    const char* text,
    size_t length)
{
    size_t at = 0;
    int current = -1;

    assert(index);
    assert(text || length == 0);

    index->finished = 0;
    while( at < length )
    {
        struct Editor_CfgLine line;
        char name[EDITOR_VARS_NAME_MAX];
        const char* key;
        const char* value;
        size_t value_length;
        int number;

        at = Editor_CfgLineParse(text, length, at, &line);
        if( line.kind == EDITOR_CFG_LINE_HEADER )
        {
            current = -1;
            if( line.closed && copy_name(name, sizeof(name), text + line.start + 1, line.part) )
                current = varbit_add(index, name);
            continue;
        }
        if( line.kind != EDITOR_CFG_LINE_FIELD )
            continue;
        key = text + line.start;
        value = key + line.part + 1;
        value_length = line.length - line.part - 1;

        if( current < 0 )
        {
            /* A member line before any block: `<id>=<name>`. */
            if( parse_int(key, line.part, &number) )
            {
                while( value_length > 0 && isspace((unsigned char)value[value_length - 1]) )
                    value_length--;
                if( copy_name(name, sizeof(name), value, value_length) )
                {
                    struct Editor_VarpName* member;

                    if( index->varbit_member_count == index->varbit_member_capacity )
                    {
                        index->varbit_member_capacity =
                            index->varbit_member_capacity ? index->varbit_member_capacity * 2 : 1024;
                        index->varbit_members = ToriMem_Realloc(
                            ToriMem_Editor(),
                            index->varbit_members,
                            sizeof(*index->varbit_members) * (size_t)index->varbit_member_capacity);
                        assert(index->varbit_members);
                    }
                    member = &index->varbit_members[index->varbit_member_count++];
                    snprintf(member->name, sizeof(member->name), "%s", name);
                    member->id = number;
                }
            }
            continue;
        }

        if( line.part == 7 && memcmp(key, "basevar", 7) == 0 )
        {
            struct Editor_Varbit* entry = &index->varbits[current];

            if( !copy_name(entry->basevar_name, sizeof(entry->basevar_name), value, value_length) )
                entry->basevar_name[0] = '\0';
        }
        else if( line.part == 8 && memcmp(key, "startbit", 8) == 0 )
            index->varbits[current].startbit =
                parse_int(value, value_length, &number) ? number : -1;
        else if( line.part == 6 && memcmp(key, "endbit", 6) == 0 )
            index->varbits[current].endbit = parse_int(value, value_length, &number) ? number : -1;
    }
}

static char*
read_file(
    const char* path,
    size_t* out_length)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* data;

    *out_length = 0;
    if( !f )
        return NULL;
    if( fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 )
    {
        fclose(f);
        return NULL;
    }
    rewind(f);
    data = ToriMem_Alloc(ToriMem_Editor(), (size_t)size + 1);
    assert(data);
    if( size > 0 && fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        fclose(f);
        ToriMem_Free(ToriMem_Editor(), data);
        return NULL;
    }
    fclose(f);
    data[size] = '\0';
    *out_length = (size_t)size;
    return data;
}

static int
feed(
    struct Editor_VarsIndex* index,
    const char* path,
    void (*fn)(struct Editor_VarsIndex*, const char*, size_t))
{
    size_t length;
    char* text = read_file(path, &length);

    if( !text )
        return 0;
    fn(index, text, length);
    ToriMem_Free(ToriMem_Editor(), text);
    return 1;
}

static int
compare_names(
    const void* a,
    const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

/** Every `*.varbit` under `dir`, depth first in sorted name order. */
static void
feed_overlays(
    struct Editor_VarsIndex* index,
    const char* dir)
{
    DIR* handle = opendir(dir);
    struct dirent* entry;
    char** names = NULL;
    int count = 0;
    int capacity = 0;

    if( !handle )
        return;
    while( (entry = readdir(handle)) != NULL )
    {
        if( entry->d_name[0] == '.' )
            continue;
        if( count == capacity )
        {
            capacity = capacity ? capacity * 2 : 64;
            names = ToriMem_Realloc(ToriMem_Editor(), names, sizeof(*names) * (size_t)capacity);
            assert(names);
        }
        names[count] = ToriMem_Strdup(ToriMem_Editor(), entry->d_name);
        assert(names[count]);
        count++;
    }
    closedir(handle);
    /* Not with nothing read: names is NULL then, and qsort's nonnull base
     * lets the optimizer drop the NULL test in the ToriMem_Free below
     * (editor_alloc.c's lanes, the same crash). */
    if( count > 1 )
        qsort(names, (size_t)count, sizeof(*names), compare_names);
    for( int i = 0; i < count; i++ )
    {
        char path[4096];
        struct stat st;
        size_t length = strlen(names[i]);

        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        if( stat(path, &st) == 0 && S_ISDIR(st.st_mode) )
            feed_overlays(index, path);
        else if( length > 7 && strcmp(names[i] + length - 7, ".varbit") == 0 )
            feed(index, path, Editor_VarsAddVarbits);
        ToriMem_Free(ToriMem_Editor(), names[i]);
    }
    ToriMem_Free(ToriMem_Editor(), names);
}

int
Editor_VarsIndexLoad(
    struct Editor_VarsIndex* index,
    const char* content_dir)
{
    char path[4096];

    assert(index);
    assert(content_dir);

    snprintf(path, sizeof(path), "%s/configs/all.varp.compack", content_dir);
    feed(index, path, Editor_VarsAddVarps);
    snprintf(path, sizeof(path), "%s/pack/varp.alloc", content_dir);
    feed(index, path, Editor_VarsAddVarps);
    snprintf(path, sizeof(path), "%s/configs/all.varbit.compack", content_dir);
    feed(index, path, Editor_VarsAddVarbits);
    snprintf(path, sizeof(path), "%s/configs/all.varbit", content_dir);
    if( !feed(index, path, Editor_VarsAddVarbits) )
        return 0;
    snprintf(path, sizeof(path), "%s/server/scripts", content_dir);
    feed_overlays(index, path);
    Editor_VarsIndexFinish(index);
    return 1;
}

int
Editor_VarsVarpId(
    const struct Editor_VarsIndex* index,
    const char* name)
{
    enum Editor_VarKind kind;
    int id;
    int found;

    assert(index);
    assert(name);

    if( Editor_VarsNameParse(name, &kind, &id, NULL, 0) && kind == EDITOR_VAR_VARP )
        return id;
    found = varp_find(index, name);
    if( found >= 0 )
        return index->varps[found].id;
    /* A bare name whose prefixed spelling is bound. */
    for( int i = 0; i < index->varp_count; i++ )
    {
        char bare[EDITOR_VARS_NAME_MAX];

        if( Editor_VarsNameParse(index->varps[i].name, &kind, &id, bare, sizeof(bare))
            && strcmp(bare, name) == 0 )
            return index->varps[i].id;
    }
    return -1;
}

int
Editor_VarsVarbitFind(
    const struct Editor_VarsIndex* index,
    const char* name)
{
    assert(index);
    assert(name);
    return varbit_find(index, name);
}

/** A placed varbit's sort key, copied so qsort needs no global. */
struct order_key
{
    int basevar;
    int startbit;
    int endbit;
    int index;
};

static int
compare_order(
    const void* a,
    const void* b)
{
    const struct order_key* x = a;
    const struct order_key* y = b;

    if( x->basevar != y->basevar )
        return x->basevar < y->basevar ? -1 : 1;
    if( x->startbit != y->startbit )
        return x->startbit < y->startbit ? -1 : 1;
    if( x->endbit != y->endbit )
        return x->endbit < y->endbit ? -1 : 1;
    return x->index < y->index ? -1 : x->index > y->index;
}

static int
placed(const struct Editor_Varbit* varbit)
{
    return varbit->basevar >= 0 && varbit->startbit >= 0 && varbit->endbit >= varbit->startbit
           && varbit->endbit < EDITOR_VARP_BITS;
}

void
Editor_VarsIndexFinish(struct Editor_VarsIndex* index)
{
    assert(index);

    /* Ids for unprefixed varbits from the member lines. */
    for( int m = 0; m < index->varbit_member_count; m++ )
    {
        int found = varbit_find(index, index->varbit_members[m].name);

        if( found >= 0 && index->varbits[found].id < 0 )
            index->varbits[found].id = index->varbit_members[m].id;
    }
    for( int i = 0; i < index->varbit_count; i++ )
    {
        struct Editor_Varbit* varbit = &index->varbits[i];

        varbit->basevar =
            varbit->basevar_name[0] ? Editor_VarsVarpId(index, varbit->basevar_name) : -1;
    }

    ToriMem_Free(ToriMem_Editor(), index->order);
    index->order = NULL;
    index->order_count = 0;
    if( index->varbit_count > 0 )
    {
        index->order = ToriMem_Alloc(ToriMem_Editor(), sizeof(*index->order) * (size_t)index->varbit_count);
        assert(index->order);
    }
    if( index->varbit_count > 0 )
    {
        struct order_key* keys =
            ToriMem_Alloc(ToriMem_Editor(), sizeof(*keys) * (size_t)index->varbit_count);
        int count = 0;

        assert(keys);
        for( int i = 0; i < index->varbit_count; i++ )
        {
            const struct Editor_Varbit* varbit = &index->varbits[i];

            if( !placed(varbit) )
                continue;
            keys[count].basevar = varbit->basevar;
            keys[count].startbit = varbit->startbit;
            keys[count].endbit = varbit->endbit;
            keys[count].index = i;
            count++;
        }
        qsort(keys, (size_t)count, sizeof(*keys), compare_order);
        for( int i = 0; i < count; i++ )
            index->order[i] = keys[i].index;
        index->order_count = count;
        ToriMem_Free(ToriMem_Editor(), keys);
    }
    index->finished = 1;
}

/** First position in `order` whose varbit is on `varp`. */
static int
order_lower_bound(
    const struct Editor_VarsIndex* index,
    int varp)
{
    int low = 0;
    int high = index->order_count;

    while( low < high )
    {
        int middle = (low + high) / 2;

        if( index->varbits[index->order[middle]].basevar < varp )
            low = middle + 1;
        else
            high = middle;
    }
    return low;
}

static uint32_t
range_mask(
    int startbit,
    int endbit)
{
    uint32_t width = (uint32_t)(endbit - startbit + 1);
    uint32_t bits = width >= 32 ? 0xFFFFFFFFu : ((1u << width) - 1u);

    return bits << startbit;
}

int
Editor_VarsBitMap(
    const struct Editor_VarsIndex* index,
    int varp,
    struct Editor_VarpBits* out)
{
    assert(index);
    assert(out);
    assert(index->finished);

    memset(out, 0, sizeof(*out));
    out->varp = varp;
    for( int at = order_lower_bound(index, varp); at < index->order_count; at++ )
    {
        const struct Editor_Varbit* varbit = &index->varbits[index->order[at]];
        uint32_t mask;

        if( varbit->basevar != varp )
            break;
        mask = range_mask(varbit->startbit, varbit->endbit);
        out->overlapped |= out->used & mask;
        out->used |= mask;
        if( out->varbit_count < (int)(sizeof(out->varbits) / sizeof(out->varbits[0])) )
            out->varbits[out->varbit_count] = index->order[at];
        out->varbit_count++;
    }
    return out->varbit_count;
}

int
Editor_VarsRangeFree(
    const struct Editor_VarsIndex* index,
    int varp,
    int startbit,
    int endbit,
    int ignore,
    int* out_conflict)
{
    uint32_t mask;

    assert(index);
    assert(index->finished);

    if( startbit < 0 || endbit >= EDITOR_VARP_BITS || startbit > endbit )
        return -1;
    mask = range_mask(startbit, endbit);
    for( int at = order_lower_bound(index, varp); at < index->order_count; at++ )
    {
        const struct Editor_Varbit* varbit = &index->varbits[index->order[at]];

        if( varbit->basevar != varp )
            break;
        if( index->order[at] == ignore )
            continue;
        if( range_mask(varbit->startbit, varbit->endbit) & mask )
        {
            if( out_conflict )
                *out_conflict = index->order[at];
            return 0;
        }
    }
    return 1;
}

int
Editor_VarsFindFree(
    const struct Editor_VarsIndex* index,
    int varp,
    int width)
{
    struct Editor_VarpBits bits;

    assert(index);

    if( width < 1 || width > EDITOR_VARP_BITS )
        return -1;
    Editor_VarsBitMap(index, varp, &bits);
    for( int start = 0; start + width <= EDITOR_VARP_BITS; start++ )
    {
        if( (bits.used & range_mask(start, start + width - 1)) == 0 )
            return start;
    }
    return -1;
}

int
Editor_VarsOverlaps(
    const struct Editor_VarsIndex* index,
    void (*each)(void* user_data, int first, int second),
    void* user_data)
{
    int pairs = 0;

    assert(index);
    assert(index->finished);

    /* Sorted by (varp, startbit): a varbit can only overlap the ones after it
     * on the same varp that start at or before its end. */
    for( int a = 0; a < index->order_count; a++ )
    {
        const struct Editor_Varbit* first = &index->varbits[index->order[a]];

        for( int b = a + 1; b < index->order_count; b++ )
        {
            const struct Editor_Varbit* second = &index->varbits[index->order[b]];

            if( second->basevar != first->basevar || second->startbit > first->endbit )
                break;
            pairs++;
            if( each )
                each(user_data, index->order[a], index->order[b]);
        }
    }
    return pairs;
}

int
Editor_VarsEmitVarbit(
    const struct Editor_VarsIndex* index,
    const char* name,
    int varp,
    int startbit,
    int endbit,
    struct Editor_Cmd* out,
    int max)
{
    const char* varp_name = NULL;
    char doc_key[EDITOR_DOC_KEY_MAX];
    static const char* const keys[3] = { "basevar", "startbit", "endbit" };
    char values[3][EDITOR_CMD_VALUE_MAX];
    int written;

    assert(index);
    assert(name);
    assert(out || max == 0);

    if( max < 3 || Editor_VarsRangeFree(index, varp, startbit, endbit, -1, NULL) != 1 )
        return 0;
    for( int i = 0; i < index->varp_count; i++ )
    {
        enum Editor_VarKind kind;
        int id;

        /* The prefixed spelling, which is what basevar= values carry. */
        if( index->varps[i].id == varp
            && (!varp_name
                || (Editor_VarsNameParse(index->varps[i].name, &kind, &id, NULL, 0)
                    && kind == EDITOR_VAR_VARP && id == varp)) )
            varp_name = index->varps[i].name;
    }
    if( !varp_name )
        return 0;
    written = snprintf(doc_key, sizeof(doc_key), "varbit:%s", name);
    if( written < 0 || (size_t)written >= sizeof(doc_key) )
        return 0;

    snprintf(values[0], sizeof(values[0]), "%s", varp_name);
    snprintf(values[1], sizeof(values[1]), "%d", startbit);
    snprintf(values[2], sizeof(values[2]), "%d", endbit);
    for( int i = 0; i < 3; i++ )
    {
        Editor_CmdInit(&out[i], EDITOR_DOC_CFG, doc_key, EDITOR_CMD_CFG_FIELD);
        out[i].has_after = 1;
        snprintf(out[i].u.cfg_field.key, sizeof(out[i].u.cfg_field.key), "%s", keys[i]);
        snprintf(out[i].u.cfg_field.after, sizeof(out[i].u.cfg_field.after), "%s", values[i]);
    }
    return 3;
}
