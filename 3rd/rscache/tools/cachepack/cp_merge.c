#include "cp_merge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Arity — whether a key holds one value or many — is *observed from rank 0*, not
 * declared.
 *
 * The plan called for declaring it. Measuring first showed why that is the wrong
 * shape: **eighteen** keys repeat within a single block across this tree, and the
 * set is per type and per revision — `dbrow.values` (14,243 blocks), `seq.frame`
 * (12,795), `obj.param` (7,417), `loc.condop` (28), `loc.condsubop` (3). A table
 * naming them would be a nineteenth thing to keep in step with the cache, and the
 * first missing entry corrupts a record quietly.
 *
 * Rank 0 is machine output from the cache and is internally consistent, so a key it
 * states twice in one block *is* multi-valued, by construction. That makes the
 * observation sound rather than a guess — and it is why the duplicate-key rule
 * below only polices rank 1, which is the hand-written layer where a repeat really
 * is ambiguous.
 */
static int
key_is_multi(const struct CP_MergeSet* set, const char* key)
{
    for( int i = 0; i < set->multi_count; i++ )
    {
        if( strcmp(set->multi[i], key) == 0 )
            return 1;
    }
    return 0;
}

static void
remember_multi(struct CP_MergeSet* set, const char* key)
{
    if( key_is_multi(set, key) )
        return;
    if( set->multi_count == set->multi_capacity )
    {
        int next = set->multi_capacity ? set->multi_capacity * 2 : 16;
        char** grown = (char**)realloc(set->multi, (size_t)next * sizeof(*grown));

        if( !grown )
            return;
        set->multi = grown;
        set->multi_capacity = next;
    }
    set->multi[set->multi_count++] = strdup(key);
}

/** Learn the type's multi-valued keys from one rank-0 block. */
static void
learn_arity(struct CP_MergeSet* set, const struct CP_Config* block)
{
    for( int i = 0; i < block->count; i++ )
    {
        for( int j = i + 1; j < block->count; j++ )
        {
            if( strcmp(block->lines[i].key, block->lines[j].key) == 0 )
            {
                remember_multi(set, block->lines[i].key);
                break;
            }
        }
    }
}

/** `param=<name>,<value>` addresses one entry of a map; the name selects it. */
static int
key_is_map(const char* key)
{
    return strcmp(key, "param") == 0;
}

/** The map key a `param=<name>,<value>` line addresses, into `out`. */
static void
map_subkey(const char* value, char* out, size_t out_size)
{
    const char* comma = strchr(value, ',');
    size_t n = comma ? (size_t)(comma - value) : strlen(value);

    if( n >= out_size )
        n = out_size - 1;
    memcpy(out, value, n);
    out[n] = '\0';
}

static struct CP_MergedRecord*
find_or_add(struct CP_MergeSet* set, const char* debugname)
{
    struct CP_MergedRecord* rec;

    for( int i = 0; i < set->count; i++ )
    {
        if( strcmp(set->records[i].debugname, debugname) == 0 )
            return &set->records[i];
    }
    if( set->count == set->capacity )
    {
        int next = set->capacity ? set->capacity * 2 : 256;
        struct CP_MergedRecord* grown =
            (struct CP_MergedRecord*)realloc(set->records, (size_t)next * sizeof(*grown));

        if( !grown )
            return NULL;
        set->records = grown;
        set->capacity = next;
    }
    rec = &set->records[set->count++];
    memset(rec, 0, sizeof(*rec));
    rec->debugname = strdup(debugname);
    return rec;
}

/** Index of the line this one replaces, or -1 to append. */
static int
slot_for(const struct CP_MergedRecord* rec, const char* key, const char* value)
{
    int mapped = key_is_map(key);
    char want[128];

    if( mapped )
        map_subkey(value, want, sizeof(want));
    for( int i = 0; i < rec->count; i++ )
    {
        if( strcmp(rec->lines[i].key, key) != 0 )
            continue;
        if( !mapped )
            return i;
        {
            char have[128];

            map_subkey(rec->lines[i].value, have, sizeof(have));
            if( strcmp(have, want) == 0 )
                return i;
        }
    }
    return -1;
}

static int
push_line(struct CP_MergedRecord* rec, const char* key, const char* value, int rank,
          const char* origin)
{
    struct CP_MergedLine* slot;

    if( rec->count == rec->capacity )
    {
        int next = rec->capacity ? rec->capacity * 2 : 16;
        struct CP_MergedLine* grown =
            (struct CP_MergedLine*)realloc(rec->lines, (size_t)next * sizeof(*grown));

        if( !grown )
            return 0;
        rec->lines = grown;
        rec->capacity = next;
    }
    slot = &rec->lines[rec->count++];
    slot->key = strdup(key);
    slot->value = strdup(value);
    slot->rank = rank;
    slot->origin = origin;
    return 1;
}

int
cp_merge_add(
    struct CP_MergeSet* set,
    const struct CP_ConfigFile* file,
    int rank,
    const char* origin)
{
    for( int b = 0; b < file->count; b++ )
    {
        const struct CP_Config* block = &file->configs[b];
        struct CP_MergedRecord* rec;
        int contributed = 0;

        if( rank == 0 )
            learn_arity(set, block);
        rec = find_or_add(set, block->debugname);
        if( !rec )
            return 0;
        for( int l = 0; l < block->count; l++ )
        {
            const char* key = block->lines[l].key;
            const char* value = block->lines[l].value;
            int at = slot_for(rec, key, value);

            /*
             * A key rank 0 states more than once is a list: append, never replace.
             * Overriding it would collapse a record's frames, params or condops to
             * whichever line came last.
             */
            if( at >= 0 && key_is_multi(set, key) && !key_is_map(key) )
                at = -1;

            if( at < 0 )
            {
                if( !push_line(rec, key, value, rank, origin) )
                    return 0;
                contributed = 1;
                continue;
            }
            if( rec->lines[at].rank == rank && rank > 0 && !key_is_multi(set, key) )
            {
                /* Only the authored layer is policed. Two hand-written files
                 * stating one key is ambiguous and last-wins would pick by
                 * directory order; rank 0 repeating a key is just a list. */
                fprintf(stderr,
                        "cachepack: [%s] states `%s` twice in the authored layer — %s "
                        "and %s\n",
                        block->debugname, key, rec->lines[at].origin, origin);
                return 0;
            }
            if( rec->lines[at].rank == rank )
            {
                /* Same rank, rank 0: a list. */
                if( !push_line(rec, key, value, rank, origin) )
                    return 0;
                continue;
            }
            /* A later rank overrides an earlier one; an earlier one never
             * overrides a later, so a file order change cannot flip a value. */
            if( rank > rec->lines[at].rank )
            {
                free(rec->lines[at].value);
                rec->lines[at].value = strdup(value);
                rec->lines[at].rank = rank;
                rec->lines[at].origin = origin;
                contributed = 1;
            }
        }
        if( contributed && rank > 0 && !rec->overlaid )
        {
            rec->overlaid = 1;
            set->overlaid_count++;
        }
    }
    return 1;
}

const struct CP_MergedRecord*
cp_merge_find(const struct CP_MergeSet* set, const char* debugname)
{
    for( int i = 0; i < set->count; i++ )
    {
        if( strcmp(set->records[i].debugname, debugname) == 0 )
            return &set->records[i];
    }
    return NULL;
}

void
cp_merge_free(struct CP_MergeSet* set)
{
    for( int i = 0; i < set->count; i++ )
    {
        for( int l = 0; l < set->records[i].count; l++ )
        {
            free(set->records[i].lines[l].key);
            free(set->records[i].lines[l].value);
        }
        free(set->records[i].lines);
        free(set->records[i].debugname);
    }
    free(set->records);
    for( int i = 0; i < set->multi_count; i++ )
        free(set->multi[i]);
    free(set->multi);
    memset(set, 0, sizeof(*set));
}
