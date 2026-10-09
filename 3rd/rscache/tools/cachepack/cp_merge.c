#include "cp_merge.h"

#include "cachepack.h"
#include "rscache_register.h"

#include <assert.h>

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

static int
key_seen_rank0(const struct CP_MergeSet* set, const char* key)
{
    for( int i = 0; i < set->seen0_count; i++ )
    {
        if( strcmp(set->seen0[i], key) == 0 )
            return 1;
    }
    return 0;
}

static void
remember_seen0(struct CP_MergeSet* set, const char* key)
{
    if( key_seen_rank0(set, key) )
        return;
    if( set->seen0_count == set->seen0_capacity )
    {
        int next = set->seen0_capacity ? set->seen0_capacity * 2 : 32;
        char** grown = (char**)realloc(set->seen0, (size_t)next * sizeof(*grown));

        if( !grown )
            return;
        set->seen0 = grown;
        set->seen0_capacity = next;
    }
    set->seen0[set->seen0_count++] = strdup(key);
}

/** Learn the type's multi-valued keys from one rank-0 block, and record every
 *  key rank 0 states so a rank-1 repeat can be told apart from an authored-only
 *  vocabulary (see CP_MergeSet.seen0). */
static void
learn_arity(struct CP_MergeSet* set, const struct CP_Config* block)
{
    for( int i = 0; i < block->count; i++ )
        remember_seen0(set, block->lines[i].key);
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

/* FNV-1a. Cheap, and debugnames are short identifiers rather than adversarial
 * input, so the distribution is all that matters. Never returns 0 as a slot
 * marker — the slot's record_plus_one is what says "empty". */
static unsigned
merge_hash(const char* s)
{
    unsigned h = 2166136261u;

    while( *s )
    {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

static int merge_index_insert(struct CP_MergeSet* set, unsigned hash, int record);

static int
merge_index_grow(struct CP_MergeSet* set)
{
    int capacity = set->index_capacity ? set->index_capacity * 2 : 512;
    struct CP_MergeIndexSlot* old = set->index;
    int old_capacity = set->index_capacity;

    set->index = (struct CP_MergeIndexSlot*)calloc((size_t)capacity, sizeof(*set->index));
    if( !set->index )
    {
        set->index = old;
        return 0;
    }
    set->index_capacity = capacity;

    for( int i = 0; i < old_capacity; i++ )
    {
        if( old[i].record_plus_one )
            merge_index_insert(set, old[i].hash, old[i].record_plus_one - 1);
    }
    free(old);
    return 1;
}

static int
merge_index_insert(struct CP_MergeSet* set, unsigned hash, int record)
{
    unsigned mask;
    unsigned slot;

    if( set->index_capacity == 0 || (set->count + 1) * 10 >= set->index_capacity * 7 )
    {
        if( !merge_index_grow(set) )
            return 0;
    }

    mask = (unsigned)set->index_capacity - 1u;
    slot = hash & mask;
    while( set->index[slot].record_plus_one )
        slot = (slot + 1u) & mask;
    set->index[slot].hash = hash;
    set->index[slot].record_plus_one = record + 1;
    return 1;
}

static int
merge_index_find(const struct CP_MergeSet* set, unsigned hash, const char* debugname)
{
    unsigned mask;
    unsigned slot;

    if( !set->index || set->index_capacity == 0 )
        return -1;

    mask = (unsigned)set->index_capacity - 1u;
    slot = hash & mask;
    for( ;; )
    {
        int rec = set->index[slot].record_plus_one;

        if( !rec )
            return -1;
        /* Compare the hash first: a miss costs no strcmp at all, which is the
         * whole point of the table. */
        if( set->index[slot].hash == hash &&
            strcmp(set->records[rec - 1].debugname, debugname) == 0 )
            return rec - 1;
        slot = (slot + 1u) & mask;
    }
}

static struct CP_MergedRecord*
find_or_add(struct CP_MergeSet* set, const char* debugname, int rank)
{
    struct CP_MergedRecord* rec;
    unsigned hash = merge_hash(debugname);
    int at = merge_index_find(set, hash, debugname);

    if( at >= 0 )
        return &set->records[at];

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
    rec->origin_rank = rank;
    /* After the append, so the stored index is the record's final position. */
    if( !merge_index_insert(set, hash, set->count - 1) )
        return NULL;
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

static int
value_is_marker(const char* value)
{
    return cp_value_is_default(value) || cp_value_is_empty(value);
}

/*
 * A list or map key whose lower layers say only `key=default` / `key=empty` holds
 * a marker, not entries, so a higher layer's first real line REPLACES it rather
 * than appending beside it (which would leave the packer a marker next to a
 * value). And a higher layer's own marker clears the key: `recol=default` in an
 * overlay means "this record has no recolours", whatever rank 0 listed.
 *
 * Returns 1 if the lower-rank lines for `key` were removed.
 */
/** The INDEXED family `key` is a line of, or NULL. */
static const struct CP_KeySpec*
indexed_family(
    const struct CP_MergeSet* set,
    const char* key)
{
    const struct CP_KeySpec* spec;

    if( !set->keys )
        return NULL;
    spec = cp_key_spec_in(set->keys, key);
    return (spec && (spec->flags & CP_KEY_INDEXED)) ? spec : NULL;
}

/** The register list field `key` is a line of (`stock3`, or the bare stem
 *  `stock` its markers are written on), or NULL. */
static const struct RSCache_RegisterField*
register_list_family(
    const struct CP_MergeSet* set,
    const char* key)
{
    const struct RSCache_RegisterField* field;

    if( !set->fields )
        return NULL;
    field = cp_register_find_line(set->fields, key);
    if( !field || (field->text != RSCACHE_REGISTER_TEXT_INDEXED &&
                   field->text != RSCACHE_REGISTER_TEXT_LIST) )
        return NULL;
    return field;
}

/** Do two line keys name the same field? */
static int
same_field(
    const struct CP_MergeSet* set,
    const char* a,
    const char* b)
{
    const struct CP_KeySpec* family = indexed_family(set, a);
    const struct RSCache_RegisterField* list = register_list_family(set, a);

    if( strcmp(a, b) == 0 )
        return 1;
    if( list && list == register_list_family(set, b) )
        return 1;
    return family && family == indexed_family(set, b);
}

static int
drop_lower_markers(
    const struct CP_MergeSet* set,
    struct CP_MergedRecord* rec,
    const char* key,
    const char* value,
    int rank)
{
    int lower = 0;
    int lower_markers = 0;
    int w = 0;

    for( int i = 0; i < rec->count; i++ )
    {
        if( !same_field(set, rec->lines[i].key, key) || rec->lines[i].rank >= rank )
            continue;
        lower++;
        lower_markers += value_is_marker(rec->lines[i].value);
    }
    if( lower == 0 )
        return 0;
    if( !value_is_marker(value) && lower_markers != lower )
        return 0;

    for( int i = 0; i < rec->count; i++ )
    {
        if( same_field(set, rec->lines[i].key, key) && rec->lines[i].rank < rank )
        {
            free(rec->lines[i].key);
            free(rec->lines[i].value);
            continue;
        }
        rec->lines[w++] = rec->lines[i];
    }
    rec->count = w;
    return 1;
}

/*
 * A dbrow's `data=<column>,<value>...` lines are a map keyed by column, and a
 * column may hold several tuples (one line each). A higher layer stating a
 * column replaces EVERY lower-layer line for that column -- its tuples are the
 * column's tuples now -- while further lines for it in the same layer append as
 * that layer's next tuples. Without this an overlay's tuples were appended to
 * the cache's, so the column held both.
 */
static int
key_is_tuple_map(const char* key)
{
    return strcmp(key, "data") == 0;
}

static void
drop_lower_column(
    struct CP_MergedRecord* rec,
    const char* value,
    int rank)
{
    char want[128];
    int w = 0;

    map_subkey(value, want, sizeof(want));
    for( int i = 0; i < rec->count; i++ )
    {
        char have[128];

        if( strcmp(rec->lines[i].key, "data") == 0 && rec->lines[i].rank < rank )
        {
            map_subkey(rec->lines[i].value, have, sizeof(have));
            if( strcmp(have, want) == 0 )
            {
                free(rec->lines[i].key);
                free(rec->lines[i].value);
                continue;
            }
        }
        rec->lines[w++] = rec->lines[i];
    }
    rec->count = w;
}

int
cp_merge_rank_for(int index, const char* path)
{
    const char* base;

    if( index == 0 )
        return 0;
    assert(path);
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    /* A lane's own configs (`ported/<lane>/configs`, walked only for an included
     * lane) define its records, and the authored layer may still overlay them. */
    if( strstr(path, "/ported/") )
        return 1;
    return strstr(base, ".generated.") ? 1 : 2;
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
        rec = find_or_add(set, block->debugname, rank);
        if( !rec )
            return 0;
        for( int l = 0; l < block->count; l++ )
        {
            const char* key = block->lines[l].key;
            const char* value = block->lines[l].value;
            int at = slot_for(rec, key, value);
            /*
             * Is this key a LIST for merge purposes?
             *
             * `param` is stated many times per record, so `key_is_multi` says
             * yes — but a param line carries its own sub-key and `slot_for`
             * already matched on it. Two lines reaching here are therefore two
             * statements of ONE param, not two entries in a list, and every
             * test below that asks "is this a list?" has to answer no or the
             * duplicate is appended and never noticed.
             *
             * It was not noticed. `arena_bouncer` carried
             * `param=death_drop,vile_ashes` from `combat_stats.generated.npc`
             * and `param=death_drop,null` from `quest_arena.npc`; both survived
             * the merge, `merged_value` returned the FIRST, and the server band
             * was written with the ashes while the runtime — which assigns
             * `def->death_drop` per line, so last file wins — held -1. Three
             * npcs disagreed that way and `ToriRSServer_Pack`'s band equivalence
             * check reported them as mismatched archives with no way to
             * converge: re-running the packer reproduced them exactly.
             */
            const struct RSCache_RegisterField* list = register_list_family(set, key);
            int listy = (key_is_multi(set, key) ||
                         (list && list->text == RSCACHE_REGISTER_TEXT_LIST)) &&
                        !key_is_map(key);

            if( (listy || key_is_map(key) || indexed_family(set, key) || list) &&
                drop_lower_markers(set, rec, key, value, rank) )
                at = slot_for(rec, key, value);
            if( key_is_tuple_map(key) && !value_is_marker(value) )
            {
                drop_lower_column(rec, value, rank);
                if( !push_line(rec, key, value, rank, origin) )
                    return 0;
                contributed = 1;
                continue;
            }

            /*
             * A key rank 0 states more than once is a list: append, never replace.
             * Overriding it would collapse a record's frames, params or condops to
             * whichever line came last.
             */
            if( at >= 0 && listy )
                at = -1;
            /* `param=levelrequire,attack,60` twice on one record is two entries of
             * a list field the register spells as a param, not one param stated
             * twice: append. */
            if( at >= 0 && key_is_map(key) && set->fields && !value_is_marker(value) )
            {
                char subkey[128];
                const struct RSCache_RegisterField* field;

                map_subkey(value, subkey, sizeof(subkey));
                field = cp_register_find(set->fields, subkey);
                if( field && field->text == RSCACHE_REGISTER_TEXT_PARAM &&
                    field->wire == RSCACHE_REGISTER_WIRE_LIST )
                    at = -1;
            }

            if( at < 0 )
            {
                if( !push_line(rec, key, value, rank, origin) )
                    return 0;
                contributed = 1;
                continue;
            }
            if( rec->lines[at].rank == rank && rank > 0 && !listy &&
                !key_seen_rank0(set, key) )
            {
                /* Rank 0 has no opinion on this key, so the authored layer is
                 * the only authority on its arity and this repeat is a list.
                 * Remember it, so the rest of the record behaves the same way. */
                remember_multi(set, key);
                if( !push_line(rec, key, value, rank, origin) )
                    return 0;
                contributed = 1;
                continue;
            }
            /*
             * A restatement is not a disagreement. `combat_stats.generated.npc`
             * emits `param=attackrate` twice per block with the same value —
             * 4,4xx of the 4,5xx duplicates in this tree are that, and treating
             * them as ambiguity would bury the eight that actually contradict.
             * Identical text keeps the slot it already has and says nothing.
             */
            if( at >= 0 && strcmp(rec->lines[at].value, value) == 0 )
                continue;

            if( rec->lines[at].rank == rank && rank > 0 && !listy && key_is_map(key) )
            {
                /*
                 * Two authored files contradicting each other about one param.
                 *
                 * Reported, then resolved LAST-WINS — because that is what the
                 * server does. `torirs_server_content.c` assigns `def->death_drop`
                 * (and every other param it reads) per line as it walks
                 * `server/scripts`, so the file the walk reaches last is the one
                 * the running game obeys. This merge is the *validator's* view
                 * of the same tree, and a validator that resolved the tie the
                 * other way would not be catching a bug, it would be inventing
                 * one: that is exactly how three npcs came to have a server band
                 * saying `vile_ashes`/`big_bones` while the world they validate
                 * said "drops nothing", with no re-run able to converge them.
                 *
                 * It stays a warning rather than a hard failure because the tie
                 * has a defined answer and the game is already playing it. What
                 * is *not* defined is whether that answer is the one anybody
                 * meant — 82 of these exist in this tree, 56 of them a
                 * hand-authored God Wars or Theatre anim being overridden by
                 * `npc_anims.generated.npc` purely because `npc/` sorts after
                 * `areas/`. Re-ranking generated files below authored ones is
                 * the fix for that, and it is a content-behaviour change owned
                 * by its own pass, not a side effect of this one.
                 */
                char subkey[128];

                map_subkey(value, subkey, sizeof(subkey));
                fprintf(stderr,
                        "cachepack: [%s] states `%s=%s` twice in the authored layer — %s "
                        "then %s, and the later file wins (as it does at run time)\n",
                        block->debugname, key, subkey, rec->lines[at].origin, origin);
                free(rec->lines[at].value);
                rec->lines[at].value = strdup(value);
                rec->lines[at].origin = origin;
                contributed = 1;
                continue;
            }
            if( rec->lines[at].rank == rank && rank > 0 && !listy )
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
    /* Same table find_or_add maintains — this used to be the identical scan. */
    int at = merge_index_find(set, merge_hash(debugname), debugname);

    if( at >= 0 )
        return &set->records[at];
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
    free(set->index);
    for( int i = 0; i < set->multi_count; i++ )
        free(set->multi[i]);
    free(set->multi);
    for( int i = 0; i < set->seen0_count; i++ )
        free(set->seen0[i]);
    free(set->seen0);
    memset(set, 0, sizeof(*set));
}
