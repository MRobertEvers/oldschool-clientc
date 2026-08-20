#include "cachepack.h"

#include "checksum.h"
#include "cp_fields.h"
#include "cp_membership.h"
#include "cp_merge.h"
#include "cp_register.h"
#include "cp_walk.h"

#include "archive.h"
#include "cache_edit.h"
#include "cache_write.h"
#include "dat2disk.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#include <direct.h>
#define cp_mkdir(p) _mkdir(p)
#else
#include <unistd.h>
#define cp_mkdir(p) mkdir(p, 0755)
#endif

/*
 * The pack driver — LostCity's engine/tools/pack/config/PackShared.ts `packConfigs`.
 *
 * The reference reads every `.npc` / `.loc` / ... under the source tree, parses each
 * `[name]` block, packs it into a `.dat`/`.idx` pair, and finally writes the whole
 * config jagfile into the cache. Same shape here, with the container of the era:
 * one archive per type inside config table 2, one file per record id, written
 * through RSCache_Dat2Edit so the reference table's CRCs, sizes and child lists all
 * move with it.
 *
 * ## Why this writes into a copy of a cache rather than building one from nothing
 *
 * A cache is far more than its configs — models, sprites, maps, scripts, sounds and
 * a dozen other tables that this tool leaves alone. Emitting only the config table
 * would produce a directory the client cannot boot. So `pack` starts from a base
 * cache, exactly as the reference's `cache.write(0, 2, config)` writes one archive
 * into an existing cache file and leaves the rest untouched. Use `--base` to copy
 * first; without it the cache at `--out` is edited in place.
 *
 * ## Ordering
 *
 * Types are packed in register order, and the register puts the referenced types
 * first, because a `.loc` naming a sequence needs that sequence's pack line loaded.
 * Pack lines all load up front, so the order only matters for reporting.
 */

/** Names are the id authority: a `[name]` with no pack line has no id to write to. */
static int
resolve_id(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* debugname,
    int* out_id)
{
    int id = cp_name_find(ctx, type, debugname);
    if( id >= 0 )
    {
        *out_id = id;
        return 1;
    }
    fprintf(
        stderr,
        "cachepack: %s [%s] has no id — add a line to configs/all.%s.compack\n",
        cp_type(type)->name,
        debugname,
        cp_type(type)->name);
    return 0;
}

struct CP_PackStats
{
    int records;
    int bytes;
    int failed;
    /** The server pack's own counters — see the section below. */
    int server_records;
    int server_fields;
    int server_bytes;
    /**
     * §2's cells (a) and (c) — an entity carrying a field its side cannot
     * receive, and an entity nothing claims.
     *
     * Separate from `failed`, which counts records that could not be *written*.
     * These were written, or deliberately were not; what is wrong is the tree's
     * statement about them. Both reach the exit status, because a routing error
     * that only printed would be the silence this whole change removes.
     */
    int membership_errors;
};

/* ---- the server pack ----------------------------------------------------- */

/*
 * `server/pack/` — the half of a record the client cache cannot hold.
 *
 * A cache npc record says what the client needs and nothing about hitpoints,
 * respawn rate or hunt mode, because no client opcode exists for them. Until now
 * those reached the server by being re-parsed out of the `.npc` files under
 * `server/scripts` at every boot, which is the "two tools write a derived cache
 * and they do not compose" problem of `docs/CONTENT_ARCHITECTURE.md` §3.5.
 *
 * So `pack` now emits both halves from the *same* merged record: the client band
 * into the target cache, and the server band into a second dat2 under the content
 * tree. `src/net/mock/mock230_servercodec.c` reads that band back; the opcodes and
 * widths on both sides come from `fields/<type>.ini` and from nowhere else.
 *
 * ## Addressed as (config kind, record id)
 *
 * One archive per record, in the idx numbered by the type's config kind — the same
 * two coordinates the client cache uses, so a server record is found by the same
 * arithmetic as the client one it overlays. There is no reference table: nothing
 * needs to enumerate the pack, and `RSCache_Dat2DiskWriteArchive` writes none. The
 * per-archive header (`cp_fields.h`) carries the version and CRC that a reference
 * table would otherwise have supplied.
 *
 * ## Rebuilt whole, never appended to
 *
 * The container appends and orphans: an archive rewritten in place leaves its old
 * sectors behind, and a record *deleted* from the tree keeps its old archive
 * forever. Both are wrong for an output that is purely derived, so the dat2 and
 * its idx files are removed before the run. That is also why a `--types` run
 * produces a partial server pack, which is said out loud rather than left to be
 * discovered.
 */

/** mkdir -p, for the two components under srcdir. */
static int
ensure_dir(const char* path)
{
    struct stat st;

    if( stat(path, &st) == 0 )
        return S_ISDIR(st.st_mode) ? 0 : -1;
    /*
     * EEXIST is success. That is the ordinary mkdir -p rule (another process
     * may have won the race), and on Windows it is also what makes an ABSOLUTE
     * path work at all: the callers below split on '/' from index 1, so the
     * first component of "C:/Users/..." is the bare drive designator "C:",
     * where MSVCRT's stat() fails with ENOENT and _mkdir() then fails with
     * EEXIST. Without this, every absolute path is an error.
     */
    if( cp_mkdir(path) == 0 )
        return 0;
    return errno == EEXIST ? 0 : -1;
}

static int
ensure_dir_p(const char* path)
{
    char buf[1200];
    size_t len;

    snprintf(buf, sizeof(buf), "%s", path);
    len = strlen(buf);
    for( size_t i = 1; i < len; i++ )
    {
        if( buf[i] != '/' )
            continue;
        buf[i] = '\0';
        if( ensure_dir(buf) != 0 )
            return -1;
        buf[i] = '/';
    }
    return ensure_dir(path);
}

/**
 * Clear the server pack so the run rebuilds it from nothing.
 *
 * Targeted rather than a recursive delete: the `.dat2` and the idx of every config
 * kind this tool knows, and nothing else. A tree may keep other things under
 * `server/`, and a packer that removes a directory it did not create is a packer
 * that eventually removes the wrong one.
 */
static void
server_pack_clear(const char* dir)
{
    char path[1200];
    int group_count = 0;
    const struct CP_ServerGroup* groups = cp_server_groups(&group_count);

    /* The write path keeps the .dat2 and .idxN open between archives. Windows
     * will not delete a file that still has an open handle, so a stale cached
     * handle here would turn "rebuild the server pack from nothing" into
     * "append to yesterday's" — silently, because remove() just fails. */
    RSCache_Dat2DiskWriteFlush();

    snprintf(path, sizeof(path), "%s/main_file_cache.dat2", dir);
    remove(path);
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", dir, cp_type(i)->config_kind);
        remove(path);
    }
    for( int i = 0; i < group_count; i++ )
    {
        snprintf(path, sizeof(path), "%s/main_file_cache.idx%d", dir, groups[i].group);
        remove(path);
    }
}

/** Per-register-field tallies, so an unresolved value is reported once per field
 *  rather than once per record — 800 door locs would otherwise print 800 lines.
 *
 *  `unresolved` and `no_ref` used to be one counter with one message, and the
 *  message described only `no_ref` ("the register declares no `ref` namespace
 *  for it"). A field that *did* declare a `ref` and named something the pack has
 *  never heard of therefore printed a line blaming the register, was dropped
 *  from the band, read back as *absent*, and got answered with the declared
 *  default — a second silent default one layer below the first. They are two
 *  different facts and only one of them is a declared gap. */
struct CP_FieldTally
{
    int written;
    int unresolved;
    int no_ref;
    int out_of_range;
    char sample[64];
    char no_ref_sample[64];
};

/**
 * The text stating `name` on `rec`, or NULL.
 *
 * Two spellings, because the two layers state the same field differently. Rank 0
 * is the machine export and writes `param=<name>,<kind>,<value>`; rank 1 is
 * LostCity's grammar and writes `param=<name>,<value>`. A first-class
 * `<name>=<value>` line wins over either, since that is how the authored layer
 * spells the fields the cache has no param for at all (`hitpoints`, `respawnrate`).
 */
static const char*
merged_value(
    const struct CP_MergedRecord* rec,
    const char* name,
    char* scratch,
    size_t scratch_size)
{
    for( int i = 0; i < rec->count; i++ )
    {
        if( strcmp(rec->lines[i].key, name) == 0 )
            return rec->lines[i].value;
    }
    for( int i = 0; i < rec->count; i++ )
    {
        const char* value;
        const char* comma;
        const char* second;

        if( strcmp(rec->lines[i].key, "param") != 0 )
            continue;
        value = rec->lines[i].value;
        comma = strchr(value, ',');
        if( !comma || (size_t)(comma - value) != strlen(name) ||
            strncmp(value, name, strlen(name)) != 0 )
            continue;

        /* `,int,` / `,str,` / `,long,` is the machine export's type column. Only
         * those three, so an authored two-field row whose *value* happens to start
         * with a comma-free word is not mistaken for one. */
        second = strchr(comma + 1, ',');
        if( second )
        {
            size_t kind = (size_t)(second - comma - 1);

            if( (kind == 3 && strncmp(comma + 1, "int", 3) == 0) ||
                (kind == 3 && strncmp(comma + 1, "str", 3) == 0) ||
                (kind == 4 && strncmp(comma + 1, "long", 4) == 0) )
            {
                snprintf(scratch, scratch_size, "%s", second + 1);
                return scratch;
            }
        }
        snprintf(scratch, scratch_size, "%s", comma + 1);
        return scratch;
    }
    return NULL;
}

/**
 * A decimal literal, or a name in the namespace the register declares.
 *
 * `cp_resolve_ref` is deliberately not used: it warns per call site, and here the
 * interesting report is per field over the whole type. A field with no `ref` and a
 * symbolic value is not an error — `huntmode = aggressive` is an engine enum with
 * no cache namespace — it is a declared gap, and the tally is how it stays visible.
 *
 * Three answers, not two:
 *
 *   1   resolved (a literal, or a name the `ref` namespace holds)
 *   0   the field declares a `ref` and the name is not in it — an error: the
 *       author named something that does not exist and the value would vanish
 *  -1   the field declares no `ref` at all, so nothing could have resolved it —
 *       the declared gap
 */
static int
resolve_field_value(
    struct CP_Ctx* ctx,
    const struct CP_Field* field,
    const char* text,
    int* out)
{
    int ref_type;
    int id;

    if( cp_parse_int(text, out) )
        return 1;
    if( !field->ref[0] )
        return -1;
    /* LostCity's lookupParamValue: `null` is the stated absence for any
     * ref-typed value and packs as -1 — `param=death_drop,null` is "drops
     * nothing", not a name that failed to resolve. Only meaningful for fields
     * whose wire width can carry it, which the u4 range check enforces. */
    if( strcmp(text, "null") == 0 )
    {
        *out = -1;
        return 1;
    }
    /* A misspelled `ref` is already fatal at the top of pack_server_type, so a
     * type that does not resolve here cannot happen — treat it as the gap rather
     * than blaming the author for the register's typo. */
    ref_type = cp_type_by_name(field->ref);
    if( ref_type < 0 )
        return -1;
    id = cp_name_find(ctx, (enum CP_TypeId)ref_type, text);
    if( id < 0 )
        return 0;
    *out = id;
    return 1;
}

/**
 * Is this block the type's `[default]`, rather than a record?
 *
 * `[default]` states what a record is *before* any block describes it. It has no
 * id by construction, the server reads it from the text overlay (the tree's
 * `general/configs/npc_default.npc`), and no encoder writes it anywhere. Counting
 * it as an unresolved name meant the one legitimate no-id block sat permanently
 * in the same bucket as a genuine typo, which is why that bucket could never be
 * made fatal.
 *
 * Named, because four places have to agree about it and one of them got it
 * wrong: the seeder wrote `default` into `pack/npc.server`, where it is a claim
 * that an entity with that name has a server half — and there is no such entity,
 * so `mock230_pack`'s id check reported the name as unresolvable. That is the
 * check doing its job; the fix is that a defaults block is not a membership
 * candidate at all. The spelling stays in one function so a second type's
 * defaults block cannot be handled by three of the four.
 *
 * The fourth caller is the routing gate itself. A block no membership file names
 * and no cache holds is §2's cell (c) and an error, which `[default]` would be
 * permanently — it is not an entity, so nothing can ever claim it.
 */
static int
record_is_defaults_block(const struct CP_MergedRecord* rec)
{
    return strcmp(rec->debugname, "default") == 0;
}

/**
 * Fill `band` with the fields `rec` states, tallying every value that could not
 * go. Returns `band->stated` — how many fields landed.
 *
 * Lifted out of `pack_server_type` so the membership seeder can ask the same
 * question the writer asks (`cp_membership_emit`). "Does this record have a
 * server half" is exactly `stated != 0`, and a seeder that re-derived it from
 * the register would be a second answer to a question with one gate — which is
 * the class of drift this whole change exists to remove. Nothing here writes, so
 * the caller decides what the answer is for.
 */
static int
server_band_build(
    struct CP_Ctx* ctx,
    const struct CP_Fields* fields,
    const struct CP_MergedRecord* rec,
    struct CP_FieldTally* tally,
    struct CP_ServerBand* band)
{
    cp_server_band_init(band);
    for( int f = 0; f < fields->band_count; f++ )
    {
        const struct CP_Field* field = &fields->entries[f];
        char scratch[512];
        const char* text = merged_value(rec, field->name, scratch, sizeof(scratch));
        int value = 0;

        /*
         * Presence is the criterion — never a comparison against a default,
         * and above all never against zero. `death_drop` defaults to -1 and obj
         * 0 is a real obj; the reader keeps "absent" and "present and zero"
         * apart precisely so the authored record can say either. The writer's
         * job is to preserve that distinction, and it has no defaults record to
         * derive it from anyway.
         */
        if( !text )
            continue;
        {
            int resolved = resolve_field_value(ctx, field, text, &value);

            if( resolved < 0 )
            {
                if( !tally[f].no_ref )
                    snprintf(tally[f].no_ref_sample, sizeof(tally[f].no_ref_sample), "%s", text);
                tally[f].no_ref++;
                continue;
            }
            if( !resolved )
            {
                if( !tally[f].unresolved )
                    snprintf(tally[f].sample, sizeof(tally[f].sample), "%s", text);
                tally[f].unresolved++;
                continue;
            }
        }
        if( !cp_server_band_put(band, field, value) )
        {
            if( !tally[f].out_of_range )
                snprintf(tally[f].sample, sizeof(tally[f].sample), "%d", value);
            tally[f].out_of_range++;
            continue;
        }
        tally[f].written++;
    }
    return band->stated;
}

/* ---- the entity gate ------------------------------------------------------ */

/*
 * Which entities have a half on each side — docs/PACK_ENTITY_SPLIT_PLAN.md §4
 * step 3, *enforce*.
 *
 * The field register (`fields/<type>.ini`) says which **fields** reach which
 * side. `pack/<ns>.client` and `pack/<ns>.server` say which **entities** do, and
 * until this step nothing read them: the packer asked *where a `[name]` block was
 * authored* — provenance — and consulted one boolean per type. Provenance leaves
 * no artifact a second tool can disagree with, which is the defect the split
 * exists to remove.
 *
 * ## The rule, per side
 *
 *   client   named in `pack/<ns>.client`, **or** the base cache already holds
 *            (config kind, id).
 *   server   named in `pack/<ns>.server`.
 *
 * The second client clause is CONTENT_PACK_PLAN §0's substrate rule stated as a
 * lookup, and it is what lets `<ns>.client` be a *subset* rather than a roster:
 * restating every cache record would be 143,435 lines that have to agree with
 * `configs/all.<ns>.compack` by hand (§8.2). It is a genuine second source and
 * not provenance renamed — the answer comes from the cache this pack is building
 * on, which is open anyway, rather than from which directory a block was found
 * in. The two are held against each other here and the disagreements are counted:
 * a rank-0 record the cache does not hold, and a rank-1-only record it does, are
 * both reported per type.
 *
 * ## The two files overlap; they do not partition
 *
 * "In the server pack" means *has a server half*, not *is exclusively ours*. An
 * npc is a cache record on the client and twenty server fields in the band at the
 * same time, so `npc.server` naming 2,199 records takes none of them off the
 * client — the substrate clause answers that question independently.
 *
 * ## The three cells §2 refuses to leave as fallthrough
 *
 *   (a) a client-only entity carrying a server field   ERROR
 *   (b) a server-only entity carrying a client field   counted, never fatal
 *   (c) an entity in neither file                      ERROR, naming both files
 *
 * (b) is not (a) mirrored. The 32 authored enums are server tables wearing enum
 * grammar and every field they state is client-shaped; erroring would make the
 * gate permanently red and teach nobody anything. It is counted and named.
 *
 * (a) is asked as *states a band field, and `<ns>.server` does not name it*.
 * "Client-only" would need the substrate clause, and `pack --server-only` opens
 * no cache — a server gate that answered differently in the two modes would break
 * the one property that mode has, that its bands are the bands a full pack
 * writes. The shipped condition covers (a) exactly and additionally catches an
 * unclaimed record, which the client pass names as (c).
 *
 * (c) is scoped by the substrate clause — *in neither file **and** not in the
 * base cache* — because a cache record is claimed by the cache and the files
 * deliberately do not restate it. Without that scope the error would fire on
 * every record in every namespace that has no membership file, which §8.3 says
 * means the opposite: everything there is the cache's.
 *
 * ## What is left of `records = client|server`
 *
 * The **default**, and only that: it routes a record named in neither file, which
 * is what keeps the migration incremental. A namespace nobody has seeded still
 * packs exactly as it did, and the same is true per record inside a seeded
 * namespace. Where a record *is* named, the file is the whole answer.
 *
 * The server side's incremental switch is the file's *existence* rather than a
 * per-record default: there is no `records =` for the band — the old gate is
 * field presence — so "unnamed record, no `<ns>.server` on disk" keeps the band
 * gate, while "unnamed record, `<ns>.server` on disk" writes no band and errors
 * if the record states one. That is what makes deleting a line from the file
 * actually move the record, which is step 4's whole feature.
 */

/**
 * The gate the packer used before membership existed, now the **default** for a
 * record no membership file names.
 *
 * A record no cache layer states is *new*, and new records are opt-in.
 * `records = client` in the type's register is the opt-in. Without it a block
 * that exists only under `server/scripts` is taken for a server table wearing a
 * config type's grammar, which is what the authored enums are: `bank_tabs` and
 * `worn_slots` are read by `mock230_content_enum` and no client script has ever
 * heard of them. Writing them would add records with no reader.
 *
 * It reads `origin_rank`, so it is provenance — which is exactly why it is no
 * longer the answer. It survives as the fallback because a tree whose membership
 * has not been seeded must still pack the way it packed yesterday.
 */
static int
record_is_client(
    const struct CP_MergedRecord* rec,
    const struct CP_Fields* fields)
{
    return !(rec->origin_rank > 0 && !fields->records_client);
}

/**
 * One namespace's routing evidence: the two files, and the base cache's id set.
 *
 * Built once per type and handed to both halves of the pack, so the client loop
 * and the band writer answer the same question from the same bytes. `base` is
 * open only when a cache is (`pack --server-only` opens none), and `base_open`
 * says so rather than leaving "not in the cache" to stand in for "did not look".
 */
struct CP_Routing
{
    struct CP_Membership client;
    struct CP_Membership server;
    struct CP_Group base;
    int base_open;

    /* client-side populations */
    int client_by_name;
    int client_by_substrate;
    int client_by_rank0;
    int client_by_default;
    int cell_b;      /**< server-only entities that state a client field */
    int server_only; /**< records the client side refused, cell (b) or not */
    /* server-side populations */
    int server_by_name;
    int server_by_alloc; /**< claimed by pack/<ns>.alloc — the allocation is the membership */
    int band_by_alloc;   /**< the same clause, asked by the band gate */
    int server_by_default;
    /* errors */
    int cell_a;
    int cell_c;
    /* the substrate clause held against provenance — §8.2 asks for both */
    int rank0_not_in_cache;
    int rank0_nameless;
    int rank1_in_cache;
};

static void
routing_load(
    struct CP_Ctx* ctx,
    enum CP_TypeId type_id,
    struct CP_Routing* routing)
{
    const struct CP_Type* type = cp_type(type_id);
    char path[1200];

    memset(routing, 0, sizeof(*routing));
    cp_membership_path(path, sizeof(path), ctx->srcdir, type->name, CP_MEMBERSHIP_CLIENT);
    cp_membership_load(&routing->client, path, type->name, CP_MEMBERSHIP_CLIENT, 1);
    cp_membership_path(path, sizeof(path), ctx->srcdir, type->name, CP_MEMBERSHIP_SERVER);
    cp_membership_load(&routing->server, path, type->name, CP_MEMBERSHIP_SERVER, 1);

    /*
     * The substrate clause needs the cache's own id set, so it is read from the
     * cache and not from the tree. A group that will not open leaves the clause
     * *unanswered* rather than answering "no": every cache record would otherwise
     * look unclaimed at once, and the gate would drop the namespace.
     */
    if( ctx->cache_open && type->config_kind >= 0 )
        routing->base_open = cp_group_open(ctx, type_id, &routing->base);
}

static void
routing_free(struct CP_Routing* routing)
{
    if( routing->base_open )
        cp_group_free(&routing->base);
    cp_membership_free(&routing->client);
    cp_membership_free(&routing->server);
}

/**
 * Does the base cache already hold this record? -1 when nothing looked.
 *
 * The clause is *(config kind, id)*, so a name the pack gives no id is answered
 * **no** rather than "unknown": a record without an id cannot be a record the
 * cache holds. `*out_id` carries which of the two it was, because the two are
 * different findings and only one of them is about the cache.
 *
 * The id is resolved silently (`cp_name_find`, not `resolve_id`): a record the
 * gate is about to route away from the client must not produce the "has no id"
 * diagnostic the client path owns, or asking the question would change the
 * report.
 */
static int
record_in_base_cache(
    struct CP_Ctx* ctx,
    enum CP_TypeId type_id,
    const struct CP_Routing* routing,
    const struct CP_MergedRecord* rec,
    int* out_id)
{
    *out_id = -1;
    if( !routing->base_open )
        return -1;
    *out_id = cp_name_find(ctx, type_id, rec->debugname);
    if( *out_id < 0 )
        return 0;
    return cp_group_find_id(&routing->base, *out_id) >= 0;
}

/**
 * Does this record state a field declared for the client?
 *
 * §2's decision table asks it of cell (b), where the answer decides whether a
 * server-only entity is losing something. A key the register does not mention is
 * one the client encoder receives untouched, so it counts; `client = drop` and
 * `client = error` do not.
 *
 * Reads the `param=<name>,...` spelling as well as the bare key, the way
 * `merged_value` does — `obj` states a declared field inside a param row, and a
 * reader that knew only one spelling would answer no for a record full of them.
 */
static int
record_states_client_field(
    const struct CP_Fields* fields,
    const struct CP_MergedRecord* rec)
{
    for( int i = 0; i < rec->count; i++ )
    {
        const struct CP_Field* field = cp_fields_find(fields, rec->lines[i].key);

        if( !field && strcmp(rec->lines[i].key, "param") == 0 )
        {
            const char* value = rec->lines[i].value;
            const char* comma = strchr(value, ',');
            size_t n = comma ? (size_t)(comma - value) : strlen(value);
            char name[48];

            if( n < sizeof(name) )
            {
                memcpy(name, value, n);
                name[n] = 0;
                field = cp_fields_find(fields, name);
            }
        }
        if( !field )
            return 1; /* unknown to the register, so the encoder sees it */
        if( field->client == CP_FIELD_CLIENT_NATIVE || field->client == CP_FIELD_CLIENT_PARAM )
            return 1;
    }
    return 0;
}

/**
 * Does this entity belong in the client cache?
 *
 * Named, substrate, then the default — and the order is the rule. `<ns>.server`
 * naming a record does not take it off the client, because the files overlap;
 * what takes it off the client is that nothing puts it on, which for a record the
 * cache does not hold means no `<ns>.client` line.
 */
static int
routing_client_member(
    struct CP_Ctx* ctx,
    enum CP_TypeId type_id,
    const struct CP_Fields* fields,
    struct CP_Routing* routing,
    const struct CP_MergedRecord* rec,
    int* errors)
{
    const struct CP_Type* type = cp_type(type_id);
    int in_cache;
    int id = -1;

    /* Not an entity, so no membership file names it and none should — see
     * `record_is_defaults_block`. The third of the three places that must agree
     * about it. */
    if( record_is_defaults_block(rec) )
        return record_is_client(rec, fields);

    in_cache = record_in_base_cache(ctx, type_id, routing, rec, &id);
    if( in_cache == 1 && rec->origin_rank > 0 )
        routing->rank1_in_cache++;
    if( in_cache == 0 && rec->origin_rank == 0 )
    {
        if( id < 0 )
            routing->rank0_nameless++;
        else
            routing->rank0_not_in_cache++;
    }

    if( cp_membership_has(&routing->client, rec->debugname) )
    {
        routing->client_by_name++;
        return 1;
    }
    if( in_cache == 1 )
    {
        routing->client_by_substrate++;
        return 1;
    }
    if( rec->origin_rank == 0 )
    {
        /*
         * A rank-0 block is a cache record by construction.
         *
         * `configs/all.<type>` is the machine export *of the cache*, so a record
         * the tree did not add is the client's whether or not it also has a
         * server half — which is exactly what pack/<type>.server's own header
         * says: "the two files overlap rather than partition ... 'in the server
         * pack' means 'has a server half', not 'is exclusively ours'".
         *
         * The substrate clause above is the same statement asked of a base cache,
         * and it is the only one that used to be made. That works while `--base`
         * is given and silently inverts without it: `in_cache` can never be 1, so
         * every record named in <type>.server fell through to cell (b) and was
         * dropped from the client cache. On this tree that was 776 locs — the
         * doors among them, which is how it was found: castledoubledoorl loaded
         * as `pos -1`, absent from a group holding 61,418 of the namespace's
         * 62,194 records.
         *
         * Provenance, not the cache, is the authority here, so this answers the
         * same in both modes rather than depending on what was opened.
         */
        routing->client_by_rank0++;
        return 1;
    }
    if( cp_membership_has(&routing->server, rec->debugname) )
    {
        /* §2 cell (b): a server-only entity carrying a client field. Counted and
         * named, never fatal — this is what `records = server` has always done,
         * one entity at a time instead of one type at a time. */
        routing->server_only++;
        if( record_states_client_field(fields, rec) )
            routing->cell_b++;
        return 0;
    }
    if( cp_name_find_alloc(ctx, type_id, rec->debugname) >= 0 )
    {
        /* The allocation ledger claims it. A `pack/<ns>.alloc` id is one
         * `ss_allocate.py` handed out past the cache's high-water mark, so the
         * record cannot be the cache's — and restating every allocated name in
         * `pack/<ns>.server` would be a roster that has to agree with the
         * ledger by hand, which is the drift cell (c) kept finding (560 varps,
         * enums, params and structs allocated after the 2026-08-02 seeding,
         * plus every dbrow). The allocation is the membership; `<ns>.client`
         * can still put one on the client explicitly, which is why this clause
         * sits below the two files and the substrate. */
        routing->server_by_alloc++;
        routing->server_only++;
        return 0;
    }
    if( in_cache == 0 )
    {
        /* §2 cell (c): nothing claims it. The default still routes it, so the
         * cache does not move while the tree is being corrected — but the tree is
         * wrong until a file names it, and the exit status says so. */
        if( id < 0 )
            cp_warn(ctx, &routing->cell_c,
                    "%s [%s] is named by neither pack/%s.client nor pack/%s.server and "
                    "configs/all.%s.compack gives it no id — nothing can claim it",
                    type->name, rec->debugname, type->name, type->name, type->name);
        else
            cp_warn(ctx, &routing->cell_c,
                    "%s [%s] is named by neither pack/%s.client nor pack/%s.server, and the "
                    "base cache does not hold %s %d — nothing claims this record",
                    type->name, rec->debugname, type->name, type->name, type->name, id);
        (*errors)++;
    }
    if( record_is_client(rec, fields) )
    {
        routing->client_by_default++;
        return 1;
    }
    routing->server_only++;
    return 0;
}

/**
 * May this entity have a band written for it?
 *
 * Asked after the band is built, so "states a server field" is a fact rather than
 * a guess. Answering "no" for a record that states one is §2 cell (a) and an
 * error: the tree declared a field the entity's own side cannot receive, which is
 * `client = error`'s shape one layer down — a statement the tree makes about
 * itself.
 *
 * No cache is consulted. `pack --server-only` opens none, and a server gate that
 * answered differently in the two modes would break the one property that mode
 * has: the bands it writes are the bands a full pack writes.
 */
static int
routing_server_member(
    struct CP_Ctx* ctx,
    enum CP_TypeId type_id,
    struct CP_Routing* routing,
    const struct CP_MergedRecord* rec,
    int stated,
    int* errors)
{
    const struct CP_Type* type = cp_type(type_id);

    if( cp_membership_has(&routing->server, rec->debugname) )
    {
        routing->server_by_name++;
        return 1;
    }
    if( cp_name_find_alloc(ctx, type_id, rec->debugname) >= 0 )
    {
        /* The allocation ledger claims it for the server side, so it may carry
         * a band — the same clause the client gate applies, asked from the
         * other direction. `pack --server-only` can answer it identically: the
         * ledger is a tree file, no cache is consulted. */
        routing->band_by_alloc++;
        return 1;
    }
    if( !routing->server.present )
    {
        /* Nobody has stated this namespace's membership, so the old gate — field
         * presence — is still the answer. The count is the size of what is not
         * yet governed by a file. */
        routing->server_by_default++;
        return 1;
    }
    if( stated )
    {
        cp_warn(ctx, &routing->cell_a,
                "%s [%s] states %d field(s) fields/%s.ini routes to the server band, and "
                "pack/%s.server does not name it — an entity cannot carry a field its own "
                "side does not receive",
                type->name, rec->debugname, stated, type->name, type->name);
        (*errors)++;
    }
    return 0;
}

/**
 * What the gate did, per namespace.
 *
 * Every line is a population and every population is named, because "the cache
 * did not move" is only evidence if the *reasons* it did not move are visible: a
 * record routed by its file and a record routed by the fallback produce the same
 * bytes and are not the same state of the tree.
 *
 * The two substrate lines are §8.2's demanded verification — every rank-0 record
 * present in the base cache, no rank-1-only record present — held here because
 * this is the pass with both the merge and the cache open.
 */
static void
routing_report_server(
    const struct CP_Type* type,
    const struct CP_Routing* routing)
{
    if( routing->server_by_name || routing->server_by_default )
        printf("  %-11s   server: %d by pack/%s.server, %d by the field gate (no file)\n",
               type->name, routing->server_by_name, type->name, routing->server_by_default);
    if( routing->band_by_alloc )
        printf("  %-11s   server: %d band(s) claimed by pack/%s.alloc\n",
               type->name, routing->band_by_alloc, type->name);
}

static void
routing_report(
    const struct CP_Type* type,
    const struct CP_Fields* fields,
    const struct CP_Routing* routing)
{
    if( routing->client_by_name || routing->client_by_substrate || routing->client_by_rank0 )
        printf("  %-11s   client: %d by pack/%s.client, %d because the base cache holds "
               "them, %d because rank 0 states them\n",
               type->name, routing->client_by_name, type->name, routing->client_by_substrate,
               routing->client_by_rank0);
    routing_report_server(type, routing);
    if( routing->server_only )
        printf("  %-11s   %d record(s) are not the client's: %d of them state a field "
               "declared for the client (plan §2 cell (b), counted)\n",
               type->name, routing->server_only, routing->cell_b);
    if( routing->server_by_alloc )
        printf("  %-11s   %d of those are claimed by pack/%s.alloc — the allocation is "
               "the membership\n",
               type->name, routing->server_by_alloc, type->name);
    if( routing->client_by_default )
        printf("  %-11s   %d record(s) no file names and the base cache does not hold "
               "reached the client through the gate membership replaced (rank 0, or "
               "`records = %s`%s)\n",
               type->name, routing->client_by_default,
               fields->records_client ? "client" : "server",
               fields->from_file ? "" : " — undeclared, so the default");
    if( !routing->base_open )
        printf("  %-11s   no base cache group: the substrate clause could not be asked, so "
               "every record fell back to the gate membership replaced\n",
               type->name);
    if( routing->rank0_not_in_cache )
        printf("  %-11s   %d record(s) configs/all.%s states sit on an id the base cache "
               "does not hold\n",
               type->name, routing->rank0_not_in_cache, type->name);
    if( routing->rank0_nameless )
        printf("  %-11s   %d record(s) configs/all.%s states have no id in "
               "configs/all.%s.compack\n",
               type->name, routing->rank0_nameless, type->name, type->name);
    if( routing->rank1_in_cache )
        printf("  %-11s   %d record(s) only server/scripts states are in the base cache\n",
               type->name, routing->rank1_in_cache);
}

/**
 * Write one type's server bands.
 *
 * Runs off the *merged* record, not off `found[0]`: the fields here exist only in
 * the authored layer, which is the whole reason the merge was built.
 */
static int
pack_server_type(
    struct CP_Ctx* ctx,
    enum CP_TypeId type_id,
    const struct CP_MergeSet* merged,
    struct CP_Routing* routing,
    const char* server_dir,
    struct CP_PackStats* stats)
{
    const struct CP_Type* type = cp_type(type_id);
    struct CP_Fields fields;
    struct CP_FieldTally tally[CP_FIELDS_MAX];
    int unnamed = 0;
    int bad_ref = 0;

    if( cp_fields_load(&fields, ctx->srcdir, type->name) <= 0 )
        return 1; /* nothing declared: nothing to write, and that is a valid tree */
    if( cp_fields_check(&fields) != 0 )
    {
        fprintf(stderr, "cachepack: fields/%s.ini is not a register the server codec can read\n",
                type->name);
        return 0;
    }

    /* `ref` names a cachepack config type, and that is checked here rather than in
     * cp_fields.c so that file stays free of the type register. A misspelling is
     * fatal: every symbolic value for that field would silently fall through to
     * "unresolved" and the pack would look merely incomplete. */
    for( int i = 0; i < fields.count; i++ )
    {
        if( fields.entries[i].ref[0] && cp_type_by_name(fields.entries[i].ref) < 0 )
        {
            fprintf(stderr, "cachepack: fields/%s.ini: [%s.%s] ref = `%s` is not a config type\n",
                    type->name, type->name, fields.entries[i].name, fields.entries[i].ref);
            bad_ref++;
        }
    }
    if( bad_ref )
        return 0;

    memset(tally, 0, sizeof(tally));
    if( ensure_dir_p(server_dir) != 0 )
    {
        fprintf(stderr, "cachepack: cannot create %s\n", server_dir);
        return 0;
    }

    for( int r = 0; r < merged->count; r++ )
    {
        const struct CP_MergedRecord* rec = &merged->records[r];
        struct CP_ServerBand band;
        uint8_t archive[CP_SERVER_BAND_MAX + CP_SERVER_PACK_HEADER];
        uint8_t container[CP_SERVER_BAND_MAX + CP_SERVER_PACK_HEADER + 16];
        uint32_t payload;
        uint32_t framed;
        int id = 0;

        if( server_band_build(ctx, &fields, rec, tally, &band) == 0 )
            continue; /* the record states nothing this register knows */

        if( !cp_server_band_finish(&band) )
            continue;

        if( record_is_defaults_block(rec) )
            continue;

        /*
         * The entity gate, after the band and before the id: whether the record
         * *has* a server half is now a statement in `pack/<type>.server`, and the
         * band it built is the evidence for or against that statement. Asked here
         * rather than at the top of the loop so the field tallies — and the
         * unresolved-value diagnostics that reach the exit status — still see
         * every record, exactly as they did when provenance decided this.
         */
        if( !routing_server_member(ctx, type_id, routing, rec, band.stated,
                                   &stats->membership_errors) )
            continue;

        id = cp_name_find(ctx, type_id, rec->debugname);
        if( id < 0 )
        {
            cp_warn(ctx, &ctx->warn_unresolved_name,
                    "%s [%s] states server fields but has no id — add a line to "
                    "configs/all.%s.compack",
                    type->name, rec->debugname, type->name);
            unnamed++;
            continue;
        }

        payload = cp_server_archive_build(&band, archive, sizeof(archive));
        if( !payload )
            continue;
        /*
         * Stored, not compressed. A band is tens of bytes; gzip's header alone is
         * larger than most of them, and the container's own length fields already
         * bound the read.
         */
        framed = RSCache_ArchiveEncode(container, sizeof(container), archive, payload,
                                       RSCACHE_ARCHIVE_COMPRESSION_NONE, NULL);
        if( !framed )
            continue;
        if( RSCache_Dat2DiskWriteArchive(server_dir, type->config_kind, id, container,
                                         (int)framed) != 0 )
        {
            fprintf(stderr, "cachepack: %s [%s] failed to write its server band\n", type->name,
                    rec->debugname);
            stats->failed++;
            continue;
        }
        stats->server_records++;
        stats->server_bytes += (int)framed;
        stats->server_fields += band.stated;
    }

    if( stats->server_records )
        printf("  %-11s server pack: %d record(s), %d field(s), %d bytes in idx%d\n", type->name,
               stats->server_records, stats->server_fields, stats->server_bytes,
               type->config_kind);
    for( int f = 0; f < fields.count; f++ )
    {
        const struct CP_Field* field = &fields.entries[f];

        if( tally[f].no_ref )
            printf("  %-11s   %s: %d value(s) not written — `%s` is a name and the register "
                   "declares no `ref` namespace for it\n",
                   type->name, field->name, tally[f].no_ref, tally[f].no_ref_sample);
        if( tally[f].unresolved )
        {
            /* An error, and it reaches the exit status through
             * `warn_unresolved_name`: the field is declared to resolve through a
             * namespace and the author named something that is not in it. Silence
             * here is a field that reads back absent and is answered by its
             * default — indistinguishable from a record that meant the default. */
            cp_warn(ctx, &ctx->warn_unresolved_name,
                    "%s.%s: %d value(s) name nothing in %s — `%s` is the first",
                    type->name, field->name, tally[f].unresolved, field->ref, tally[f].sample);
        }
        if( tally[f].out_of_range )
            printf("  %-11s   %s: %d value(s) not written — %s does not fit u%d\n", type->name,
                   field->name, tally[f].out_of_range, tally[f].sample, (int)field->wire);
    }
    if( unnamed )
        printf("  %-11s   %d record(s) with server fields have no id\n", type->name, unnamed);
    return 1;
}

/* ---- the client view of a merged record ---------------------------------- */

/*
 * The other half of the split: what `cp_npc.c` is allowed to see.
 *
 * A merged record holds both bands at once. Handing it straight to the client
 * encoder would offer it `hitpoints` and `huntmode`, which it would report as
 * unknown keys and drop; handing it only rank 0 would lose the authored records
 * entirely. The register is what separates them, per key:
 *
 *   native   the encoder's own key — passed through untouched
 *   param:N  not a key at all to the encoder; folded into the record's param
 *            table under that param's *name*
 *   drop     server-only; never reaches the encoder
 *   error    the encoder must refuse, loudly, at build time
 *
 * A key the register does not mention is passed through, so a type with no
 * `fields/<type>.ini` behaves exactly as it did before this existed. That is what
 * keeps 15 of the 20 config types byte-identical without declaring anything.
 */
struct CP_ClientView
{
    struct CP_Config config;
    /** Lines this view synthesised and therefore owns. The rest point into the
     *  merged record, which outlives the view. */
    char** owned;
    int owned_count;
    int owned_capacity;
};

static void
client_view_free(struct CP_ClientView* view)
{
    for( int i = 0; i < view->owned_count; i++ )
        free(view->owned[i]);
    free(view->owned);
    free(view->config.lines);
    memset(view, 0, sizeof(*view));
}

static int
view_push(
    struct CP_ClientView* view,
    char* key,
    char* value,
    int owns_value)
{
    if( view->config.count == view->config.capacity )
    {
        int next = view->config.capacity ? view->config.capacity * 2 : 32;
        struct CP_ConfigLine* grown =
            (struct CP_ConfigLine*)realloc(view->config.lines, (size_t)next * sizeof(*grown));

        if( !grown )
            return 0;
        view->config.lines = grown;
        view->config.capacity = next;
    }
    if( owns_value )
    {
        if( view->owned_count == view->owned_capacity )
        {
            int next = view->owned_capacity ? view->owned_capacity * 2 : 16;
            char** grown = (char**)realloc(view->owned, (size_t)next * sizeof(*grown));

            if( !grown )
                return 0;
            view->owned = grown;
            view->owned_capacity = next;
        }
        view->owned[view->owned_count++] = value;
    }
    view->config.lines[view->config.count].key = key;
    view->config.lines[view->config.count].value = value;
    view->config.lines[view->config.count].line_no = 0;
    view->config.count++;
    return 1;
}

/** The param name a `param=<name>,...` line addresses. Mirrors cp_merge.c's own
 *  `map_subkey`, which is static there for the same reason it is static here. */
static void
map_subkey(
    const char* value,
    char* out,
    size_t out_size)
{
    const char* comma = strchr(value, ',');
    size_t length = comma ? (size_t)(comma - value) : strlen(value);

    if( length >= out_size )
        length = out_size - 1;
    memcpy(out, value, length);
    out[length] = '\0';
}

/** Does the record already state `param=<name>,...` itself? */
static int
states_param(
    const struct CP_MergedRecord* rec,
    const char* name)
{
    size_t length = strlen(name);

    for( int i = 0; i < rec->count; i++ )
    {
        const char* value = rec->lines[i].value;

        if( strcmp(rec->lines[i].key, "param") != 0 )
            continue;
        if( strncmp(value, name, length) == 0 && value[length] == ',' )
            return 1;
    }
    return 0;
}

static int
client_view_build(
    struct CP_Ctx* ctx,
    const struct CP_Fields* fields,
    const struct CP_MergedRecord* rec,
    struct CP_ClientView* view,
    int* out_projected,
    int* out_errors)
{
    memset(view, 0, sizeof(*view));
    view->config.debugname = rec->debugname;

    for( int i = 0; i < rec->count; i++ )
    {
        const struct CP_Field* field = cp_fields_find(fields, rec->lines[i].key);

        /*
         * A `param=<name>,...` line is looked up by the *param name*, not by the
         * key `param`.
         *
         * `obj` states exactly one authored field and states it that way:
         * `param=levelrequire,attack,60`. The register declares `[obj.levelrequire]
         * client = drop`, and without this the filter would see the key `param`,
         * find no declaration, and hand `cp_obj.c` a line whose middle column is a
         * stat name where a param kind belongs.
         */
        if( !field && strcmp(rec->lines[i].key, "param") == 0 )
        {
            char subkey[128];

            map_subkey(rec->lines[i].value, subkey, sizeof(subkey));
            field = cp_fields_find(fields, subkey);
            /* A projected field states the param *is* how it reaches the client, so
             * the line stays. Only `drop` and `error` strip it. */
            if( field && field->client == CP_FIELD_CLIENT_PARAM )
                field = NULL;
        }

        if( field )
        {
            if( field->client == CP_FIELD_CLIENT_ERROR )
            {
                fprintf(stderr,
                        "cachepack: %s [%s]: `%s` is declared `client = error` and the record "
                        "states it — the cache cannot express this field\n",
                        fields->type, rec->debugname, field->name);
                (*out_errors)++;
                continue;
            }
            if( field->client != CP_FIELD_CLIENT_NATIVE )
                continue; /* param:N is injected below; drop never reaches the encoder */
        }
        if( !view_push(view, rec->lines[i].key, rec->lines[i].value, 0) )
            return 0;
    }

    /*
     * Then the projection.
     *
     * Skipped when the record states the param itself: `configs/all.npc` already
     * carries `param=attackrate,int,4` for 2,196 npcs, and injecting a second
     * entry for the same param would write the map twice. The record's own
     * statement is also the *merged* one, so an authored override has already won
     * by the time this runs.
     */
    for( int f = 0; f < fields->count; f++ )
    {
        const struct CP_Field* field = &fields->entries[f];
        char scratch[512];
        const char* text;
        char* line;
        int value = 0;
        int param_id;
        char kind;

        if( field->client != CP_FIELD_CLIENT_PARAM || !field->param_name[0] )
            continue;
        text = merged_value(rec, field->name, scratch, sizeof(scratch));
        if( !text || states_param(rec, field->param_name) )
            continue;
        if( !resolve_field_value(ctx, field, text, &value) )
            continue; /* already tallied by the band pass */

        param_id = cp_name_find(ctx, CP_TYPE_PARAM, field->param_name);
        if( param_id < 0 )
        {
            cp_warn(ctx, &ctx->warn_unresolved_name,
                    "%s [%s]: `client = param:%s` names no param in configs/all.param.compack",
                    fields->type, rec->debugname, field->param_name);
            continue;
        }
        kind = cp_param_type_of(ctx, param_id);

        line = (char*)malloc(600);
        if( !line )
            return 0;
        /* The machine spelling, three fields. `cp_parse_param` accepts the
         * two-field authored one too, but writing the kind here keeps the
         * projection independent of whether the param's own record loaded. */
        snprintf(line, 600, "%s,%s,%d", field->param_name, kind == 's' ? "str" : "int", value);
        if( !view_push(view, (char*)"param", line, 1) )
        {
            free(line);
            return 0;
        }
        (*out_projected)++;
    }
    return 1;
}

/**
 * Write the name tables of the namespaces the cache has no table for.
 *
 * `stat` and `category` are not records the client ignores — they are types it
 * has no concept of. They have lived as `pack/<ns>.pack` text re-read at every
 * boot, which is the same problem the server band removed for npc, so they get
 * the same destination: an archive in `server/pack`, at a group id from the
 * reserved space `cp_fields.h` justifies.
 *
 * Flat rather than an opcode band, because that is what they are. A band is a
 * per-record overlay carrying fields; these are one small table of `id -> name`
 * with no fields at all, so the payload is the table and the `kind` byte in the
 * header says which grammar it is.
 */
static int
pack_server_names(
    struct CP_Ctx* ctx,
    const char* server_dir)
{
    int group_count = 0;
    const struct CP_ServerGroup* groups = cp_server_groups(&group_count);

    for( int g = 0; g < group_count; g++ )
    {
        char path[1200];
        struct LC_Pack pack;
        int* ids;
        const char** names;
        int count = 0;
        uint8_t* table;
        uint32_t table_size;
        uint8_t* archive;
        uint32_t payload;
        uint8_t* container;
        uint32_t framed;
        uint32_t bound;

        snprintf(path, sizeof(path), "%s/pack/%s.pack", ctx->srcdir, groups[g].name);
        memset(&pack, 0, sizeof(pack));
        /* `pack.names` is the test, not `pack.max`. A missing file is a normal
         * state and `lc_pack_load` reports it by returning a zeroed struct — so
         * `max` comes back as 0, not as the -1 this guard was written for, and
         * the loop below dereferenced a NULL `names`. `cachepack pack` on any
         * tree with no `server/pack/<name>.pack` segfaulted here.
         *
         * The bound is `<` too: `max` is one past the highest listed id, so
         * `<=` read one entry beyond it — in bounds only as long as `capacity`
         * happened to exceed `max`. */
        if( !lc_pack_load(&pack, path, groups[g].name, 1) || !pack.names || pack.max <= 0 )
        {
            /* Absent is a normal state, not a failure: a tree that has not named
             * a namespace yet simply has no table to emit. */
            lc_pack_free(&pack);
            continue;
        }

        ids = (int*)malloc(sizeof(*ids) * (size_t)pack.max);
        names = (const char**)malloc(sizeof(*names) * (size_t)pack.max);
        if( !ids || !names )
        {
            free(ids);
            free(names);
            lc_pack_free(&pack);
            return 0;
        }
        /* Sparse in, sparse out. An id the pack does not list gets no entry —
         * the same rule `lc_pack_save_sparse` follows, and the reason the table
         * carries its ids rather than implying them from position. */
        for( int id = 0; id < pack.max; id++ )
        {
            if( !pack.names[id] )
                continue;
            ids[count] = id;
            names[count] = pack.names[id];
            count++;
        }
        if( count == 0 )
        {
            free(ids);
            free(names);
            lc_pack_free(&pack);
            continue;
        }

        bound = 2;
        for( int i = 0; i < count; i++ )
            bound += 4 + (uint32_t)strlen(names[i]) + 1;
        table = (uint8_t*)malloc(bound);
        archive = (uint8_t*)malloc(bound + CP_SERVER_PACK_HEADER);
        container = (uint8_t*)malloc(bound + CP_SERVER_PACK_HEADER + 16);
        if( !table || !archive || !container )
        {
            free(table);
            free(archive);
            free(container);
            free(ids);
            free(names);
            lc_pack_free(&pack);
            return 0;
        }

        table_size = cp_server_names_encode(ids, names, count, table, bound);
        payload = table_size
                      ? cp_server_archive_build_payload(
                            CP_SERVER_PAYLOAD_NAMES, table, table_size, archive,
                            bound + CP_SERVER_PACK_HEADER)
                      : 0;
        framed = payload ? RSCache_ArchiveEncode(container,
                                                 bound + CP_SERVER_PACK_HEADER + 16, archive,
                                                 payload, RSCACHE_ARCHIVE_COMPRESSION_NONE, NULL)
                         : 0;
        if( !framed ||
            RSCache_Dat2DiskWriteArchive(server_dir, groups[g].group, 0, container, (int)framed) !=
                0 )
        {
            fprintf(stderr, "cachepack: %s: failed to write its name table\n", groups[g].name);
            free(table);
            free(archive);
            free(container);
            free(ids);
            free(names);
            lc_pack_free(&pack);
            return 0;
        }
        printf("  %-11s server pack: %d name(s), %d bytes in idx%d\n", groups[g].name, count,
               (int)framed, groups[g].group);
        free(table);
        free(archive);
        free(container);
        free(ids);
        free(names);
        lc_pack_free(&pack);
    }
    return 1;
}

static int
pack_type(
    struct CP_Ctx* ctx,
    enum CP_TypeId type_id,
    struct RSCache_Dat2Edit* edit,
    int config_table,
    const char* server_dir,
    struct CP_PackStats* stats)
{
    const struct CP_Type* type = cp_type(type_id);
    memset(stats, 0, sizeof(*stats));

    if( type->flags & CP_TYPE_NO_ENCODER )
    {
        printf("  %-11s skipped (no encoder; source records kept)\n", type->name);
        return 1;
    }

    /*
     * Found by walking, not by constructing a path.
     *
     * `configs/all.<type>` was the only place a config record could live, and the
     * packer knew that as a `snprintf`. Discovery by extension is what lets a
     * second layer exist at all — the `server/scripts` tree states the same types in
     * the same grammar — so the path stops being knowledge the packer holds and
     * becomes something the tree answers.
     *
     * Still one file per type at this phase: `configs/` is the only root, so this
     * finds exactly what the `snprintf` found and the packed bytes are unchanged.
     * That is the bar for the change.
     */
    const char* found[CP_PACK_MAX_SOURCES];
    int found_count = cp_walk_find(&ctx->walk, type->name, found, CP_PACK_MAX_SOURCES);
    struct CP_MergeSet merged;
    struct CP_Fields fields;
    struct CP_Routing routing;
    int projected = 0;
    int view_errors = 0;
    int ok = 1;

    if( found_count <= 0 )
    {
        /* A type the source tree does not carry is not an error: a partial unpack
         * is a normal thing to want, and the base cache still holds those records. */
        printf("  %-11s no source file\n", type->name);
        return 1;
    }

    /*
     * Merge every layer into one record set, then split it in two.
     *
     * Both encoders now run from the *same* merged record — the shape
     * `opcode_codec.h` describes and LostCity's
     * `ConfigDatIdx = { client, server }` has always had. What separates them is
     * the field register and nothing else: `client_view_build` hands the client
     * encoder the `native` keys plus the `param:N` projections, and the server
     * band takes the fields with a `server = opcode:` row.
     *
     * A record that exists only at rank 1 is therefore written for the first
     * time. That is what `configs/all.enum` has been missing: the 32 enums the
     * server allocated from base 5995 (ids 5995..6026) and nothing ever encoded.
     * This comment said "seven" for as long as there were seven; the count is
     * measured now rather than remembered.
     */
    memset(&merged, 0, sizeof(merged));
    for( int i = 0; i < found_count && ok; i++ )
    {
        struct CP_ConfigFile layer;

        if( !cp_config_file_load(&layer, found[i]) )
            continue;
        ok = cp_merge_add(&merged, &layer, cp_merge_rank_for(i, found[i]), found[i]);
        cp_config_file_free(&layer);
    }
    if( !ok )
    {
        cp_merge_free(&merged);
        return 0;
    }
    if( found_count > 1 )
        printf("  %-11s %d record(s), %d overlaid by %d file(s)\n", type->name, merged.count,
               merged.overlaid_count, found_count - 1);

    /* Both halves route on the same two files and the same cache id set, so it is
     * read once and handed to both. */
    routing_load(ctx, type_id, &routing);

    if( server_dir && !pack_server_type(ctx, type_id, &merged, &routing, server_dir, stats) )
    {
        routing_free(&routing);
        cp_merge_free(&merged);
        return 0;
    }

    cp_fields_load(&fields, ctx->srcdir, type->name);

    /* One buffer for every record. 64 KB is comfortably past the largest config
     * record in any cache measured (the widest loc is under 2 KB); an encoder that
     * needs more returns 0 rather than overrunning, and that is reported. */
    uint8_t* buffer = malloc(64 * 1024);
    if( !buffer )
    {
        routing_free(&routing);
        cp_merge_free(&merged);
        return 0;
    }

    for( int i = 0; i < merged.count; i++ )
    {
        const struct CP_MergedRecord* rec = &merged.records[i];
        struct CP_ClientView view;
        int id = 0;
        uint32_t size;

        if( !routing_client_member(ctx, type_id, &fields, &routing, rec,
                                   &stats->membership_errors) )
            continue;
        if( !resolve_id(ctx, type_id, rec->debugname, &id) )
        {
            stats->failed++;
            continue;
        }
        if( !client_view_build(ctx, &fields, rec, &view, &projected, &view_errors) )
        {
            client_view_free(&view);
            stats->failed++;
            continue;
        }
        size = type->pack(ctx, id, &view.config, buffer, 64 * 1024);
        client_view_free(&view);
        if( size == 0 )
        {
            fprintf(stderr, "cachepack: %s [%s] failed to encode\n", type->name, rec->debugname);
            stats->failed++;
            continue;
        }
        if( !RSCache_Dat2EditPutFile(edit, config_table, type->config_kind, id, buffer, size) )
        {
            fprintf(stderr, "cachepack: %s [%s] failed to stage\n", type->name, rec->debugname);
            stats->failed++;
            continue;
        }
        stats->records++;
        stats->bytes += (int)size;
    }

    free(buffer);
    cp_merge_free(&merged);

    printf("  %-11s %6d records, %d bytes%s\n", type->name, stats->records, stats->bytes,
           stats->failed ? " (with failures)" : "");
    if( projected )
        printf("  %-11s   %d param(s) projected from the field register\n", type->name,
               projected);
    routing_report(type, &fields, &routing);
    routing_free(&routing);
    if( view_errors )
    {
        /* `client = error` is the disposition whose whole purpose is to fail the
         * build rather than let a field vanish into the gap between the two
         * encoders. Honouring it anywhere but here would defeat it. */
        fprintf(stderr, "cachepack: %s: %d field(s) declared `client = error` were stated\n",
                type->name, view_errors);
        return 0;
    }
    return 1;
}

int
cp_pack_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel,
    const char* base_cache_dir,
    const char* out_cache_dir)
{
    if( base_cache_dir )
    {
        printf("Copying %s -> %s\n", base_cache_dir, out_cache_dir);
        if( tool_copy_cache_dir(base_cache_dir, out_cache_dir) != 0 )
        {
            fprintf(stderr, "cachepack: failed to copy the base cache\n");
            return 0;
        }
    }
    else
    {
        /*
         * No --base: build the cache out of the content tree alone.
         *
         * The write path never needed a donor cache -- WriteArchive creates the
         * .dat2 and each .idxN on demand, and the commit builds a reference
         * table for every table it touches. Only the open needed a file to
         * already be there, which is what this creates. What lands is then
         * exactly what the tree states, with none of a base cache's untouched
         * records carried along.
         */
        printf("Creating %s (no --base: packing the tree alone)\n", out_cache_dir);
        if( !tool_dat2_create(out_cache_dir) )
            return 0;
    }

    if( !tool_dat2_open(out_cache_dir, &ctx->profile, &ctx->cache) )
        return 0;
    ctx->cache_open = true;

    int config_table = RSCache_Dat2DiskTableId(ctx->cache.disk, RSCACHE_DAT2_TABLE_CONFIGS);
    if( config_table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
    {
        fprintf(stderr, "cachepack: the target cache has no config table\n");
        return 0;
    }

    struct RSCache_Dat2Edit* edit = RSCache_Dat2EditNew(ctx->cache.disk);
    if( !edit )
        return 0;

    /*
     * One walk for the whole run, over two roots at two ranks. `configs/` is the
     * machine export; `server/scripts` is what a person authored on top of it, and
     * a later rank overlays an earlier one.
     *
     * The two layers state the same types in *different grammars* —
     * `configs/all.npc` uses the key names `cp_npc.c` encodes, while `lumbridge.npc`
     * uses LostCity's (`hitpoints=`, `respawnrate=`, `huntmode=`), which no cache
     * record has a field for. That is now the split rather than the obstacle: the
     * client encoder still reads only rank 0, and the keys it cannot express are
     * routed through the field register into the server pack.
     */
    {
        static const char* const ROOTS[] = { "configs", "server/scripts" };
        static const int RANKS[] = { 0, 1 };

        cp_walk_tree(&ctx->walk, ctx->srcdir, ROOTS, RANKS, 2);
        cp_constants_load(ctx);
    }

    /*
     * The server pack is rebuilt from nothing on every run, so it is cleared
     * before the first type rather than per type — a type whose records all lost
     * their server fields must end up with no archives, not with yesterday's.
     */
    char server_dir[1200];
    snprintf(server_dir, sizeof(server_dir), "%s/server/pack", ctx->srcdir);
    server_pack_clear(server_dir);
    if( !sel->all )
        printf("Note: --types restricts the server pack too; %s will hold only the "
               "selected types\n",
               server_dir);

    /*
     * The param types, before the type loop and not lazily.
     *
     * The register packs `loc` and `npc` before `param`, so a record stating
     * `param=death_drop,bones` is encoded while the param table would still be
     * empty — and `bones` is only obj 526 because `death_drop` is a `namedobj`.
     */
    printf("Typed %d param(s) from the tree\n", cp_param_types_load(ctx));

    printf("Packing configs from %s\n", ctx->srcdir);
    int total = 0, failed = 0, server_total = 0, membership_errors = 0;
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        if( !sel->all && !(sel->mask & (1u << i)) )
            continue;
        struct CP_PackStats stats;
        if( !pack_type(ctx, i, edit, config_table, server_dir, &stats) )
        {
            RSCache_Dat2EditFree(edit);
            return 0;
        }
        total += stats.records;
        failed += stats.failed;
        server_total += stats.server_records;
        membership_errors += stats.membership_errors;
    }

    /*
     * After the types, and unconditional on `--types`: `stat` and `category` are
     * not config types, so no selection mask can name them and restricting the
     * config pass has no bearing on whether their names are current.
     */
    if( !pack_server_names(ctx, server_dir) )
    {
        RSCache_Dat2EditFree(edit);
        return 0;
    }

    if( total == 0 )
    {
        fprintf(stderr, "cachepack: nothing to write\n");
        RSCache_Dat2EditFree(edit);
        return 0;
    }

    printf("Committing %d records into %s\n", total, out_cache_dir);
    bool ok = RSCache_Dat2EditCommit(edit, out_cache_dir);
    RSCache_Dat2EditFree(edit);
    if( !ok )
    {
        fprintf(stderr, "cachepack: commit failed\n");
        return 0;
    }
    /* The config table is on disk. Everything after this may safely add to the
     * cache; before it, there is nothing to add to (see CP_Ctx.cache_committed). */
    ctx->cache_committed = true;

    printf("Done. %d records written, %d failed, %d unknown keys, %d unresolved names.\n", total,
           failed, ctx->warn_unknown_key, ctx->warn_unresolved_name);
    if( server_total )
        printf("Server pack: %d record(s) in %s\n", server_total, server_dir);
    else
        printf("Server pack: nothing declared — no fields/<type>.ini states a "
               "`server = opcode:...` row for a record the tree carries\n");
    if( membership_errors )
        printf("Membership: %d record(s) are routed by a statement the tree does not make "
               "(docs/PACK_ENTITY_SPLIT_PLAN.md §2 cells (a) and (c))\n",
               membership_errors);
    /* A failure here is a record the target cache still holds in its old form, not
     * a corrupt one — but the caller should know the pack was partial. A membership
     * error is not a partial write at all: the bytes are what the tree asked for
     * and the tree is what is wrong, which is still not a success. */
    return failed == 0 && membership_errors == 0;
}

/*
 * The server half alone (`pack --server-only`).
 *
 * A full pack copies the base cache and emits 116,450 archives, which is too
 * heavy to run on every build — it was OOM-killed the first time a test bar ran
 * it that way. The server pack needs none of that: `pack_server_type` reads the
 * merged text and the name packs and never opens a cache, so the band the boot
 * depends on can be refreshed in the build the way `mock230-scripts` refreshes
 * the script pack.
 *
 * The bar for this mode is byte-identity: the bands it writes must be exactly
 * the bands a full `pack` writes, which holds because both run the same walk,
 * the same merge and the same `pack_server_type` — this function is the merge
 * loop of `pack_type` minus the client encoder.
 */
int
cp_pack_server_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel)
{
    char server_dir[1200];
    int server_total = 0;

    {
        static const char* const ROOTS[] = { "configs", "server/scripts" };
        static const int RANKS[] = { 0, 1 };

        cp_walk_tree(&ctx->walk, ctx->srcdir, ROOTS, RANKS, 2);
        cp_constants_load(ctx);
    }

    snprintf(server_dir, sizeof(server_dir), "%s/server/pack", ctx->srcdir);
    server_pack_clear(server_dir);
    if( !sel->all )
        printf("Note: --types restricts the server pack too; %s will hold only the "
               "selected types\n",
               server_dir);

    printf("Packing the server bands from %s\n", ctx->srcdir);
    int membership_errors = 0;
    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        const struct CP_Type* type = cp_type(i);
        const char* found[CP_PACK_MAX_SOURCES];
        int found_count;
        struct CP_MergeSet merged;
        struct CP_Fields fields;
        struct CP_Routing routing;
        struct CP_PackStats stats;
        int ok = 1;

        if( !sel->all && !(sel->mask & (1u << i)) )
            continue;
        /*
         * Before the merge, not after: a type whose register declares no
         * `server = opcode:` row contributes nothing to the server pack, so
         * this mode has no reason to load — or police — its records at all.
         * That is not only speed. The authored-layer rules the merge enforces
         * are the *full* pack's contract, and at least one type (`dbrow`, whose
         * list keys exist only at rank 1) currently cannot pass them — the
         * full-pack blocker docs/DBTABLES.md records. A server-only refresh
         * must not be hostage to it.
         */
        if( cp_fields_load(&fields, ctx->srcdir, type->name) <= 0 || fields.band_count <= 0 )
            continue;
        found_count = cp_walk_find(&ctx->walk, type->name, found, CP_PACK_MAX_SOURCES);
        if( found_count <= 0 )
            continue;

        memset(&merged, 0, sizeof(merged));
        memset(&stats, 0, sizeof(stats));
        for( int f = 0; f < found_count && ok; f++ )
        {
            struct CP_ConfigFile layer;

            if( !cp_config_file_load(&layer, found[f]) )
                continue;
            ok = cp_merge_add(&merged, &layer, cp_merge_rank_for(f, found[f]), found[f]);
            cp_config_file_free(&layer);
        }
        /*
         * The same gate the full pack runs, from the same files. No cache is
         * opened here, and the server side never asks for one: `<ns>.server` names
         * the entities with a server half, and whether the client cache also holds
         * them is the *client* question. That is what keeps the two modes writing
         * the same bands.
         */
        routing_load(ctx, i, &routing);
        if( !ok || !pack_server_type(ctx, i, &merged, &routing, server_dir, &stats) )
        {
            routing_free(&routing);
            cp_merge_free(&merged);
            return 0;
        }
        routing_report_server(type, &routing);
        membership_errors += stats.membership_errors;
        routing_free(&routing);
        cp_merge_free(&merged);
        server_total += stats.server_records;
    }

    if( !pack_server_names(ctx, server_dir) )
        return 0;

    printf("Server pack: %d record(s) in %s, %d unresolved names\n", server_total, server_dir,
           ctx->warn_unresolved_name);
    /*
     * An unresolved name is a failure, not a note.
     *
     * It used to `return 1` regardless, so `make mock230-servpack` was green with
     * a band that had silently dropped whatever did not resolve — and the dropped
     * field reads back as *absent*, which the param-defaults path answers with the
     * declared default. That is triage §13 bar 1 one layer below the config
     * loader: the tree said something, the pack quietly said nothing, and no exit
     * status anywhere disagreed.
     *
     * The count is zero today. It was 1 before `[default]` stopped being counted
     * as a nameless record, which is the only reason this could not be turned on
     * until now.
     */
    if( membership_errors )
        printf("Membership: %d record(s) state a server field pack/<ns>.server does not "
               "claim (docs/PACK_ENTITY_SPLIT_PLAN.md §2 cell (a))\n",
               membership_errors);
    return ctx->warn_unresolved_name == 0 && membership_errors == 0;
}

/* ---- the membership seed -------------------------------------------------- */

/*
 * `cachepack membership` — write down, once, the routing a namespace would get
 * if it stated none.
 *
 * Step 1 of docs/PACK_ENTITY_SPLIT_PLAN.md §4: *emit, do not enforce*. Its value
 * is that the files were produced by the gates themselves — `record_is_client`
 * and `server_band_build` — rather than by a rule someone wrote down a second
 * time. Step 2 turned on the checks that hold them against provenance and against
 * the id bases; step 3 made the packer route on them, and could then assert the
 * packed cache was byte-identical, which is only a meaningful assertion because
 * the seed came from the gates.
 *
 * It is still exactly the right seed now that routing has moved, and for the same
 * reason: those two gates are what the packer falls back to for a record no file
 * names, so seeding a namespace from them and then enforcing the result is a
 * fixed point. Re-seeding a file that already exists is refused, so this can
 * never restate a decision someone has since changed.
 *
 * ## The two gates give two answers, and both are recorded
 *
 * The packer decides each side separately and the decisions are not complements:
 *
 *   client   `record_is_client` — provenance plus one boolean per type.
 *   server   `band.stated != 0` — *field presence*, which is why npc writes 2,199
 *            bands off 42 authored blocks: 2,158 cache npcs carry
 *            `param=attackrate` and `fields/npc.ini` declares that a server field.
 *
 * `<ns>.server` is the union of "the client gate refused it" and "the server gate
 * accepted it", because both are the packer saying the record has a server half.
 * The plan's §2 is explicit that the two files *overlap rather than partition* —
 * an npc is a cache record on the client and twenty server fields in the band —
 * so a record appearing in both is the normal case and not a contradiction.
 *
 * ## A subset, not a roster
 *
 * `<ns>.client` lists only records the *tree adds*. Restating every cache record
 * would mean 143,435 lines that duplicate `configs/all.<ns>.compack` line for
 * line, and two files that must agree by hand is the failure
 * `content_register.h` catalogues three times. A record the machine export
 * states is a cache record by construction; the membership files cover exactly
 * the population where that construction says nothing, which is also exactly the
 * population provenance routes silently today.
 *
 * ## What gets a file
 *
 * A namespace where either gate has something to say. Twelve of the twenty
 * config types have no `server/scripts` file at all and no server band, so their
 * two files would both be empty — and forty files of noise would make the
 * absence of a file mean nothing. Both files of a pair are written even when one
 * is empty, so "npc adds nothing to the client" is stated rather than missing.
 */

/** The seeder's per-type counters, so the report can distinguish the two gates. */
struct CP_MembershipTally
{
    int client;
    int server_refused_by_client_gate;
    int server_by_band;
};

/**
 * Seed one type's pair from the merged record set. Returns 0 on failure.
 *
 * `fields` is loaded by the caller because both gates need it and the register
 * is read once per type.
 */
static int
membership_seed_type(
    struct CP_Ctx* ctx,
    const struct CP_MergeSet* merged,
    const struct CP_Fields* fields,
    struct CP_Membership* client,
    struct CP_Membership* server,
    struct CP_MembershipTally* tally)
{
    struct CP_FieldTally* discard = calloc(CP_FIELDS_MAX, sizeof(*discard));

    if( !discard )
        return 0;

    for( int r = 0; r < merged->count; r++ )
    {
        const struct CP_MergedRecord* rec = &merged->records[r];
        struct CP_ServerBand band;

        /* Not an entity, so not a membership candidate on either side — see
         * `record_is_defaults_block`. Both gates would otherwise name it, and
         * the routing gate now reads these files, so a `[default]` line in one
         * would be a permanent unclaimed record rather than a harmless one. */
        if( record_is_defaults_block(rec) )
            continue;

        if( record_is_client(rec, fields) )
        {
            /*
             * Only the records the tree adds. A rank-0 record is in the client
             * cache because it *is* a cache record, and `configs/all.<ns>` says
             * so already.
             */
            if( rec->origin_rank > 0 )
            {
                if( cp_membership_add(client, rec->debugname) < 0 )
                {
                    free(discard);
                    return 0;
                }
                tally->client++;
            }
        }
        else
        {
            if( cp_membership_add(server, rec->debugname) < 0 )
            {
                free(discard);
                return 0;
            }
            tally->server_refused_by_client_gate++;
        }

        /*
         * The band gate, asked exactly as `pack_server_type` asks it. The tally
         * is thrown away: an unresolved value is reported by the writer, and
         * reporting it twice would suggest two failures.
         */
        if( fields->band_count > 0 && server_band_build(ctx, fields, rec, discard, &band) != 0 )
        {
            int added = cp_membership_add(server, rec->debugname);

            if( added < 0 )
            {
                free(discard);
                return 0;
            }
            if( added )
                tally->server_by_band++;
        }
    }
    free(discard);
    return 1;
}

/**
 * The header a seeded file carries.
 *
 * Written once, then owned by whoever edits the file. Parameterised rather than
 * per-namespace prose, because a header that had to be written twenty times
 * would be twenty things to keep in step — the same argument the field register
 * makes for having no built-in defaults.
 */
static int
membership_write_header(
    struct CP_Membership* set,
    const char* ns,
    enum CP_MembershipSide side)
{
    char text[2400];
    const char* half = side == CP_MEMBERSHIP_SERVER ? "server" : "client";

    snprintf(
        text, sizeof(text),
        "// Which `%s` entities have a %s half.\n"
        "//\n"
        "// Names, never ids. The id comes from `configs/all.%s.compack`, the member\n"
        "// index both sides share — ids and names stay in one namespace per data type\n"
        "// and only membership is split. An id here would be a second id authority, and\n"
        "// an id has nothing to resolve against and so cannot fail to resolve\n"
        "// (docs/LOSTCITY_PORT_TRIAGE.md §13 bar 5).\n"
        "//\n"
        "// A **subset** of that namespace, not a roster: it lists only records the tree\n"
        "// adds. A record `configs/all.%s` states is a cache record by construction, and\n"
        "// restating all of them here would be a second copy of the index that has to\n"
        "// agree with the first by hand.\n"
        "//\n"
        "// The two files overlap rather than partition. \"In the server pack\" means \"has\n"
        "// a server half\", not \"is exclusively ours\": an npc is a cache record on the\n"
        "// client and a band of server fields at the same time.\n"
        "//\n"
        "// Authored. `content.ini` declares `membership = authored` for this namespace;\n"
        "// nothing regenerates the file. `cachepack membership` creates only files that\n"
        "// do not exist and never replaces one; `unpack` and `verify` never open one.\n"
        "//\n"
        "// **`cachepack pack` routes on it** — docs/PACK_ENTITY_SPLIT_PLAN.md §4 step 3.\n"
        "// A record gets a server band only if `%s.server` names it, and reaches the\n"
        "// client cache only if `%s.client` names it or the base cache already holds its\n"
        "// id. Taking a name out of a file takes the record off that side; if the record\n"
        "// still states a field the register routes there, the pack fails rather than\n"
        "// dropping it silently (§2 cell (a)). The seed came from the gates this\n"
        "// replaced, so turning the routing on changed no packed byte.\n"
        "\n",
        ns, half, ns, ns, ns, ns);

    set->preamble = malloc(strlen(text) + 1);
    if( !set->preamble )
        return 0;
    memcpy(set->preamble, text, strlen(text) + 1);
    return 1;
}

/**
 * Write one file, and read it straight back.
 *
 * The read-back is not ceremony: this format's whole job is to be re-read by
 * step 2 and step 3, and a writer checked only by its own author is the failure
 * `3rd/rscache` answers everywhere else with a byte-exact round trip. Cheap
 * here — the largest file is 2,200 lines.
 */
static int
membership_commit(
    const struct CP_Membership* set,
    const char* srcdir,
    int* out_written)
{
    char path[1200];
    struct CP_Membership back;
    int ok = 1;

    cp_membership_path(path, sizeof(path), srcdir, set->ns, set->side);

    {
        FILE* probe = fopen(path, "rb");

        if( probe )
        {
            fclose(probe);
            /*
             * Never replaced. A membership file is authored the moment it
             * exists, and this tool has no way to tell a line someone moved
             * from a line it wrote itself — a merge would need that
             * distinction and it does not exist on disk.
             */
            printf("  %-11s %-6s already exists — left alone\n", set->ns,
                   cp_membership_side_name(set->side));
            return 1;
        }
    }
    if( !cp_register_membership_is_authored(set->ns) )
    {
        fprintf(stderr, "cachepack: %s declares its membership generated; refusing to seed it\n",
                set->ns);
        return 0;
    }
    if( !cp_membership_save(set, path) )
        return 0;

    if( !cp_membership_load(&back, path, set->ns, set->side, 0) )
        return 0;
    if( back.count != set->count || back.malformed || back.duplicates )
        ok = 0;
    for( int i = 0; ok && i < set->count; i++ )
    {
        if( !cp_membership_has(&back, set->names[i]) )
            ok = 0;
    }
    cp_membership_free(&back);
    if( !ok )
    {
        fprintf(stderr, "cachepack: %s does not read back as it was written\n", path);
        return 0;
    }

    printf("  %-11s %-6s %5d name(s) -> %s\n", set->ns, cp_membership_side_name(set->side),
           set->count, path);
    (*out_written)++;
    return 1;
}

int
cp_membership_emit(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel)
{
    int written = 0;
    int skipped_types = 0;
    int unmergeable = 0;

    {
        static const char* const ROOTS[] = { "configs", "server/scripts" };
        static const int RANKS[] = { 0, 1 };

        cp_walk_tree(&ctx->walk, ctx->srcdir, ROOTS, RANKS, 2);
        cp_constants_load(ctx);
    }

    /* Before the type loop for the same reason `cp_pack_run` does it: a record
     * stating `param=death_drop,bones` needs the param's declared type before
     * the value can be resolved, and the band gate resolves values. */
    printf("Typed %d param(s) from the tree\n", cp_param_types_load(ctx));
    printf("Seeding membership from %s\n", ctx->srcdir);

    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        const struct CP_Type* type = cp_type(i);
        const char* found[CP_PACK_MAX_SOURCES];
        int found_count;
        struct CP_MergeSet merged;
        struct CP_Fields fields;
        struct CP_Membership client;
        struct CP_Membership server;
        struct CP_MembershipTally tally;
        int ok = 1;

        if( !sel->all && !(sel->mask & (1u << i)) )
            continue;
        found_count = cp_walk_find(&ctx->walk, type->name, found, CP_PACK_MAX_SOURCES);
        if( found_count <= 0 )
            continue;

        memset(&merged, 0, sizeof(merged));
        memset(&tally, 0, sizeof(tally));
        for( int f = 0; f < found_count && ok; f++ )
        {
            struct CP_ConfigFile layer;

            if( !cp_config_file_load(&layer, found[f]) )
                continue;
            ok = cp_merge_add(&merged, &layer, cp_merge_rank_for(f, found[f]), found[f]);
            cp_config_file_free(&layer);
        }
        if( !ok )
        {
            /*
             * Reported, not fatal, and this is the honest place to say it: the
             * packer has no routing answer for a type whose authored layer will
             * not merge, so there is nothing to write down. `dbrow` and
             * `dbtable` are that case today (docs/DBTABLES.md), and they are the
             * types §5 of the plan expects the split to earn most on — so the
             * gap is worth a line rather than a silent absence.
             */
            fprintf(stderr,
                    "cachepack: %s: the authored layer does not merge, so the packer has no "
                    "routing answer to record — no membership seeded\n",
                    type->name);
            cp_merge_free(&merged);
            unmergeable++;
            continue;
        }

        cp_fields_load(&fields, ctx->srcdir, type->name);
        cp_membership_init(&client, type->name, CP_MEMBERSHIP_CLIENT);
        cp_membership_init(&server, type->name, CP_MEMBERSHIP_SERVER);
        ok = membership_seed_type(ctx, &merged, &fields, &client, &server, &tally);
        cp_merge_free(&merged);

        if( ok && client.count == 0 && server.count == 0 )
        {
            /* Nothing to state. The absence of a pair is then a fact — every
             * record in this namespace is the cache's — rather than noise. */
            skipped_types++;
        }
        else if( ok )
        {
            printf("  %-11s client gate: %d added record(s) to the client, %d refused; "
                   "band gate: %d further record(s)\n",
                   type->name, tally.client, tally.server_refused_by_client_gate,
                   tally.server_by_band);
            ok = membership_write_header(&client, type->name, CP_MEMBERSHIP_CLIENT) &&
                 membership_write_header(&server, type->name, CP_MEMBERSHIP_SERVER) &&
                 membership_commit(&client, ctx->srcdir, &written) &&
                 membership_commit(&server, ctx->srcdir, &written);
        }
        cp_membership_free(&client);
        cp_membership_free(&server);
        if( !ok )
            return 0;
    }

    printf("Membership: %d file(s) written, %d namespace(s) had nothing to state", written,
           skipped_types);
    if( unmergeable )
        printf(", %d could not be merged", unmergeable);
    printf("\n");
    printf("`cachepack pack` routes on these files (docs/PACK_ENTITY_SPLIT_PLAN.md §4 step\n"
           "3), so a namespace seeded here now packs the way it packed before it was\n"
           "seeded — that is what the seed is for. Read what was written before relying\n"
           "on it: a seed records today's answer, not a decision anyone made.\n");
    return 1;
}

/* ---- the membership check, half one: against provenance ------------------ */

/*
 * `cachepack membership --check-only` — hold `pack/<ns>.client` / `.server`
 * against the *other* statement of the same fact.
 *
 * Step 2 of docs/PACK_ENTITY_SPLIT_PLAN.md §4: *check, do not enforce*. §3.3
 * names two agreement checks and this is the first of them — membership against
 * provenance. The second, membership against the id range, is not here: its
 * authority is `server_base`, which lives in `src/content/content_register.c` and
 * is overlaid by `content.ini`, and cachepack deliberately links nothing from
 * `src/` (`cp_register.h`). It runs in `mock230_pack`, which holds that register
 * and the base cache already. One check per tool, each where its fact is.
 *
 * This check does not route anything, and it is not what enforces membership —
 * `pack` does that, from the same files (see the entity-gate section). The two
 * are deliberately separate: this one has the merge's `origin_rank` and can ask
 * whether a *file* agrees with where a block was authored, which is a question
 * about the tree rather than about the cache being built.
 *
 * ## What "provenance" is, exactly
 *
 * Two bits per merged record, and neither is derivable from the other:
 *
 *   `origin_rank`  which layer first declared the block. 0 means the machine
 *                  export from the cache states it, so the record is the
 *                  cache's by construction (§8.2's substrate rule).
 *   `overlaid`     whether any layer above rank 0 contributed a key. A cache
 *                  record with an authored overlay has `origin_rank == 0` and
 *                  `overlaid == 1`, and it is a different thing from both a pure
 *                  cache record and a record the tree invents.
 *
 * "Has a rank-1 block" is the union: the tree wrote something about this entity.
 *
 * ## Why most of this counts rather than errors
 *
 * The routing being checked is *today's*, which the files were seeded from, so a
 * disagreement here is a statement about the tree rather than about the seed. Two
 * populations are large, legitimate and known (§8.5): npc's server bands built
 * off cache-exported `param=attackrate` rows, and the authored overlays in
 * namespaces that declare no server band at all. Erroring on either would make
 * this gate red permanently and teach nobody anything — §2's cell (b) argument,
 * one layer down. They are counted, named and printed.
 *
 * What errors is the population that cannot be explained by any gate: a name no
 * config layer states, a name in `.server` that neither provenance nor the band
 * gate would have put there, and §2's cell (c) — a record the tree invents and
 * neither file claims. All three are zero on this tree, which is what makes them
 * usable as errors.
 */

/** One namespace's verdict. Every field is a population, not a boolean, because
 *  the interesting answers here are all counts. */
struct CP_MembershipVerdict
{
    /* `.server`, against provenance */
    int server_listed;
    int server_unknown;       /**< no config layer states this name — ERROR */
    int server_no_gate;       /**< no authored block and no server band — ERROR */
    int server_band_only;     /**< no authored block; the band gate accepts it */
    int server_authored;      /**< agreed: the tree wrote a block for it */
    /* `.client`, against provenance */
    int client_listed;
    int client_unknown;       /**< no config layer states this name — ERROR */
    int client_refused;       /**< the live client gate refuses it — ERROR */
    int client_cache_record;  /**< rank 0 states it; the file says the tree adds it */
    int client_authored;      /**< agreed */
    /* records neither file names */
    int server_by_alloc;      /**< claimed by pack/<ns>.alloc — the allocation is the membership */
    int unclaimed_new;        /**< rank-1-only and in neither file — ERROR, cell (c) */
    int unclaimed_overlay;    /**< an authored overlay on a cache record, unclaimed */
    int unclaimed_unrouted;   /**< ...and it states a field that reaches neither side */
    /**
     * Records — claimed or not — stating a field the register routes nowhere.
     *
     * Not a membership disagreement and not counted as one. It is here because
     * this is the pass that already has the merge and the register open, and
     * because the number is the reason `<ns>.server` cannot yet be the whole
     * answer: a field with no client disposition and no band opcode is a server
     * half that no packed file carries, so no membership statement about the
     * entity could route it anywhere. §2's cell (a)/(b) work, one layer down,
     * and it belongs to step 4.
     */
    int unrouted_records;
};

/**
 * Does this record state, at rank 1, a field the register routes nowhere?
 *
 * `client = drop` with no `server = opcode:` row means the value reaches neither
 * the cache nor the band: the packer reads it, drops it, and the only reader left
 * is `mock230_content.c`'s parse of the same text. That is a **real server half
 * with no artifact**, and it is what separates the two kinds of unclaimed
 * overlay — a `param=` row on an obj is routed and merely unstated, while
 * `protect=` on a varp is a fact no packed file carries at all.
 *
 * Asked of the rank-1 lines only. A rank-0 line stating such a field would be the
 * machine export inventing one, which cannot happen: the export writes back the
 * keys the decoder produced.
 *
 * **Two spellings, because the tree uses both.** `varp` states the field as its
 * own key (`protect=no`); `obj` states it inside a param row
 * (`param=levelrequire,attack,60`), which is how a *repeatable* field is written
 * in a grammar whose native slot holds two of them. `merged_value` already reads
 * both for the band writer — the name is everything up to the first comma — and
 * this uses that same rule rather than a second one. Reading only the bare key
 * form made `obj`'s 1,257 requirement lines look absent, which is exactly the
 * silence this check exists to break.
 */
static int
overlay_states_unrouted_field(
    const struct CP_Fields* fields,
    const struct CP_MergedRecord* rec,
    const char** out_key)
{
    for( int i = 0; i < rec->count; i++ )
    {
        const struct CP_Field* field;

        if( rec->lines[i].rank == 0 )
            continue;
        field = cp_fields_find(fields, rec->lines[i].key);
        if( !field && strcmp(rec->lines[i].key, "param") == 0 )
        {
            const char* value = rec->lines[i].value;
            const char* comma = strchr(value, ',');
            size_t n = comma ? (size_t)(comma - value) : strlen(value);
            char name[48];

            if( n >= sizeof(name) )
                continue;
            memcpy(name, value, n);
            name[n] = 0;
            field = cp_fields_find(fields, name);
        }
        if( !field || field->client != CP_FIELD_CLIENT_DROP || field->opcode != 0 )
            continue;
        if( out_key )
            *out_key = field->name;
        return 1;
    }
    return 0;
}

/**
 * Which declared band fields a record states, for the report.
 *
 * `server_band_build` answers *whether* a record has a server half; when the
 * answer is "yes, and nothing authored it", the useful next question is which
 * field said so — `attackrate` and `hitpoints` are very different findings. Read
 * through `merged_value`, so a field projected into `param=` is found the same
 * way the band writer finds it.
 */
static void
band_fields_stated(
    const struct CP_Fields* fields,
    const struct CP_MergedRecord* rec,
    int* counts)
{
    for( int f = 0; f < fields->band_count; f++ )
    {
        char scratch[512];

        if( merged_value(rec, fields->entries[f].name, scratch, sizeof(scratch)) )
            counts[f]++;
    }
}

static int
membership_check_type(
    struct CP_Ctx* ctx,
    const struct CP_Type* type,
    const struct CP_MergeSet* merged,
    const struct CP_Fields* fields,
    const struct CP_Membership* client,
    const struct CP_Membership* server,
    struct CP_MembershipVerdict* v,
    int* band_field_counts,
    int* errors)
{
    struct CP_FieldTally* discard = calloc(CP_FIELDS_MAX, sizeof(*discard));

    if( !discard )
        return 0;

    v->server_listed = server->count;
    v->client_listed = client->count;

    for( int i = 0; i < server->count; i++ )
    {
        const char* name = server->names[i];
        const struct CP_MergedRecord* rec = cp_merge_find(merged, name);
        struct CP_ServerBand band;
        int has_band;

        if( !rec )
        {
            cp_warn(ctx, &v->server_unknown,
                    "pack/%s.server names `%s`, and no config layer states it — the "
                    "name resolves to no record",
                    type->name, name);
            (*errors)++;
            continue;
        }
        if( rec->origin_rank > 0 || rec->overlaid )
        {
            v->server_authored++;
            continue;
        }
        has_band =
            fields->band_count > 0 && server_band_build(ctx, fields, rec, discard, &band) != 0;
        if( has_band )
        {
            v->server_band_only++;
            band_fields_stated(fields, rec, band_field_counts);
            continue;
        }
        cp_warn(ctx, &v->server_no_gate,
                "pack/%s.server names `%s`, and the tree states nothing for it: no "
                "block under server/scripts and no field fields/%s.ini routes to the band",
                type->name, name, type->name);
        (*errors)++;
    }

    for( int i = 0; i < client->count; i++ )
    {
        const char* name = client->names[i];
        const struct CP_MergedRecord* rec = cp_merge_find(merged, name);

        if( !rec )
        {
            cp_warn(ctx, &v->client_unknown,
                    "pack/%s.client names `%s`, and no config layer states it — the "
                    "name resolves to no record",
                    type->name, name);
            (*errors)++;
            continue;
        }
        if( !record_is_client(rec, fields) )
        {
            cp_warn(ctx, &v->client_refused,
                    "pack/%s.client names `%s`, and the packer's client gate refuses "
                    "it — `records = client` is not stated in fields/%s.ini",
                    type->name, name, type->name);
            (*errors)++;
            continue;
        }
        if( rec->origin_rank == 0 )
        {
            cp_warn(ctx, &v->client_cache_record,
                    "pack/%s.client names `%s`, which configs/all.%s already states — a "
                    "membership file lists the records the tree *adds*",
                    type->name, name, type->name);
            continue;
        }
        v->client_authored++;
    }

    /*
     * The other direction, and the one the files cannot state by being read: a
     * record the tree wrote something about that neither file names. §2's cell
     * (c) is the rank-1-*only* half of it — a record nothing claims — and it is
     * an error. An authored overlay on a cache record is not: the cache claims
     * the client half, so the only unstated fact is whether the overlay has a
     * server half, and in a namespace with no band there is nothing to state.
     */
    for( int r = 0; r < merged->count; r++ )
    {
        const struct CP_MergedRecord* rec = &merged->records[r];

        if( rec->origin_rank == 0 && !rec->overlaid )
            continue;
        if( overlay_states_unrouted_field(fields, rec, NULL) )
            v->unrouted_records++;
        /* The seeder skips it, so demanding that a file name it would be this
         * check disagreeing with the tool it is checking. */
        if( record_is_defaults_block(rec) )
            continue;
        if( cp_membership_has(client, rec->debugname) ||
            cp_membership_has(server, rec->debugname) )
            continue;
        if( cp_name_find_alloc(ctx, (enum CP_TypeId)cp_type_by_name(type->name),
                               rec->debugname) >= 0 )
        {
            /* The allocation ledger claims it — the same clause the routing
             * gate applies. A roster line would restate the ledger by hand. */
            v->server_by_alloc++;
            continue;
        }
        if( rec->origin_rank > 0 )
        {
            cp_warn(ctx, &v->unclaimed_new,
                    "%s [%s] is stated only under server/scripts and appears in neither "
                    "pack/%s.client nor pack/%s.server — nothing claims it",
                    type->name, rec->debugname, type->name, type->name);
            (*errors)++;
            continue;
        }
        {
            const char* unrouted = NULL;

            if( overlay_states_unrouted_field(fields, rec, &unrouted) )
            {
                cp_warn(ctx, &v->unclaimed_unrouted,
                        "%s [%s] states `%s`, which fields/%s.ini routes to neither side, "
                        "and appears in neither pack/%s.client nor pack/%s.server",
                        type->name, rec->debugname, unrouted, type->name, type->name,
                        type->name);
                continue;
            }
        }
        cp_warn(ctx, &v->unclaimed_overlay,
                "%s [%s] carries an authored overlay and appears in neither "
                "pack/%s.client nor pack/%s.server",
                type->name, rec->debugname, type->name, type->name);
    }

    free(discard);
    return 1;
}

int
cp_membership_check(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel)
{
    int errors = 0;
    int pairs = 0;
    int nopair_overlay_types = 0;
    int total_band_only = 0;
    int total_unclaimed_overlay = 0;
    int total_unrouted = 0;

    {
        static const char* const ROOTS[] = { "configs", "server/scripts" };
        static const int RANKS[] = { 0, 1 };

        cp_walk_tree(&ctx->walk, ctx->srcdir, ROOTS, RANKS, 2);
        cp_constants_load(ctx);
    }

    printf("Typed %d param(s) from the tree\n", cp_param_types_load(ctx));
    printf("Checking membership against provenance in %s\n", ctx->srcdir);

    for( int i = 0; i < CP_TYPE_COUNT; i++ )
    {
        const struct CP_Type* type = cp_type(i);
        const char* found[CP_PACK_MAX_SOURCES];
        int found_count;
        struct CP_MergeSet merged;
        struct CP_Fields fields;
        struct CP_Membership client;
        struct CP_Membership server;
        struct CP_MembershipVerdict v;
        int band_field_counts[CP_FIELDS_MAX];
        char path[1200];
        int ok = 1;

        if( !sel->all && !(sel->mask & (1u << i)) )
            continue;

        memset(&v, 0, sizeof(v));
        memset(band_field_counts, 0, sizeof(band_field_counts));
        /* `cp_membership_load` initialises, including for a missing file, so an
         * absent pair reads as two empty sets rather than as an error. */
        cp_membership_path(path, sizeof(path), ctx->srcdir, type->name, CP_MEMBERSHIP_CLIENT);
        ok = cp_membership_load(&client, path, type->name, CP_MEMBERSHIP_CLIENT, 1);
        cp_membership_path(path, sizeof(path), ctx->srcdir, type->name, CP_MEMBERSHIP_SERVER);
        ok = ok && cp_membership_load(&server, path, type->name, CP_MEMBERSHIP_SERVER, 1);
        if( !ok )
        {
            fprintf(stderr, "cachepack: %s: a membership file exists and cannot be read\n",
                    type->name);
            cp_membership_free(&client);
            cp_membership_free(&server);
            return 0;
        }
        if( client.malformed || server.malformed || client.duplicates || server.duplicates )
        {
            /*
             * A line that is neither prose nor a name, or a name stated twice.
             * Both are silent thinning of an authored file — the reader has to
             * choose, and a reader that chooses is a reader that can be wrong —
             * so they fail here rather than being carried into the routing.
             */
            fprintf(stderr,
                    "cachepack: %s: membership files hold %d unreadable line(s) and %d "
                    "duplicate name(s)\n",
                    type->name, client.malformed + server.malformed,
                    client.duplicates + server.duplicates);
            errors++;
        }

        found_count = cp_walk_find(&ctx->walk, type->name, found, CP_PACK_MAX_SOURCES);
        if( found_count <= 0 )
        {
            if( client.count || server.count )
            {
                fprintf(stderr,
                        "cachepack: %s: membership is stated for %d name(s) and the tree "
                        "carries no %s file at all\n",
                        type->name, client.count + server.count, type->name);
                errors++;
            }
            cp_membership_free(&client);
            cp_membership_free(&server);
            continue;
        }

        memset(&merged, 0, sizeof(merged));
        for( int f = 0; f < found_count && ok; f++ )
        {
            struct CP_ConfigFile layer;

            if( !cp_config_file_load(&layer, found[f]) )
                continue;
            ok = cp_merge_add(&merged, &layer, cp_merge_rank_for(f, found[f]), found[f]);
            cp_config_file_free(&layer);
        }
        if( !ok )
        {
            /* Same answer the seeder gives, and for the same reason: a type whose
             * authored layer will not merge has no routing to disagree with.
             * Reported so the absence stays visible (docs/DBTABLES.md). */
            fprintf(stderr,
                    "cachepack: %s: the authored layer does not merge, so there is no "
                    "routing to check membership against\n",
                    type->name);
            cp_merge_free(&merged);
            cp_membership_free(&client);
            cp_membership_free(&server);
            continue;
        }

        cp_fields_load(&fields, ctx->srcdir, type->name);

        if( client.count == 0 && server.count == 0 )
        {
            /*
             * No pair, which §8.3 says means "every record in this namespace is
             * the cache's". Held to it: an authored record in a namespace that
             * states no membership is exactly the fact the absence denies. A
             * rank-1-*only* record here is cell (c) and errors; an overlay is
             * counted, because `obj`'s 857 of them are real and their server half
             * has no band to be routed through yet (§8.5 finding 3).
             */
            int overlays = 0;
            int unrouted = 0;
            int invented = 0;
            int alloc_claimed = 0;
            const char* unrouted_key = NULL;

            for( int r = 0; r < merged.count; r++ )
            {
                if( merged.records[r].origin_rank > 0 )
                {
                    /* The allocation ledger claims it, so the absence of a pair
                     * is not an absence of a statement — see the routing gate's
                     * alloc clause. */
                    if( cp_name_find_alloc(ctx, (enum CP_TypeId)i,
                                           merged.records[r].debugname) >= 0 )
                        alloc_claimed++;
                    else
                        invented++;
                }
                else if( merged.records[r].overlaid )
                {
                    overlays++;
                    if( overlay_states_unrouted_field(&fields, &merged.records[r],
                                                      &unrouted_key) )
                        unrouted++;
                }
            }
            if( alloc_claimed )
                printf("  %-11s no membership pair; %d record(s) claimed by pack/%s.alloc\n",
                       type->name, alloc_claimed, type->name);
            if( invented )
            {
                fprintf(stderr,
                        "cachepack: %s: %d record(s) exist only under server/scripts and "
                        "the namespace states no membership at all\n",
                        type->name, invented);
                errors += invented;
            }
            if( overlays )
            {
                printf("  %-11s no membership pair, and %d record(s) carry an authored "
                       "overlay",
                       type->name, overlays);
                if( unrouted )
                    printf(" (%d state `%s`, which fields/%s.ini routes to neither side)",
                           unrouted, unrouted_key ? unrouted_key : "?", type->name);
                printf("\n");
                nopair_overlay_types++;
                total_unclaimed_overlay += overlays;
                total_unrouted += unrouted;
            }
            cp_merge_free(&merged);
            cp_membership_free(&client);
            cp_membership_free(&server);
            continue;
        }

        pairs++;
        ok = membership_check_type(ctx, type, &merged, &fields, &client, &server, &v,
                                   band_field_counts, &errors);
        cp_merge_free(&merged);

        if( ok )
        {
            printf("  %-11s server %d listed = %d authored + %d band-only%s; "
                   "client %d listed = %d authored%s\n",
                   type->name, v.server_listed, v.server_authored, v.server_band_only,
                   v.server_unknown || v.server_no_gate ? " (+ disagreements)" : "",
                   v.client_listed, v.client_authored,
                   v.client_unknown || v.client_refused || v.client_cache_record
                       ? " (+ disagreements)"
                       : "");
            if( v.server_band_only )
            {
                /* Named fields, not just a count: "2,158 npcs have a server half"
                 * is a number, and "because the cache states their attackrate" is
                 * the finding. */
                printf("  %-11s   %d server name(s) have no authored block; their band "
                       "comes from cache-exported values:",
                       type->name, v.server_band_only);
                for( int f = 0; f < fields.band_count; f++ )
                {
                    if( band_field_counts[f] )
                        printf(" %s=%d", fields.entries[f].name, band_field_counts[f]);
                }
                printf("\n");
                total_band_only += v.server_band_only;
            }
            if( v.client_cache_record )
                printf("  %-11s   %d client name(s) are records configs/all.%s already "
                       "states\n",
                       type->name, v.client_cache_record, type->name);
            if( v.server_by_alloc )
                printf("  %-11s   %d record(s) named by neither file and claimed by "
                       "pack/%s.alloc\n",
                       type->name, v.server_by_alloc, type->name);
            if( v.unclaimed_overlay )
            {
                printf("  %-11s   %d authored overlay(s) named by neither file, every "
                       "stated field routed\n",
                       type->name, v.unclaimed_overlay);
                total_unclaimed_overlay += v.unclaimed_overlay;
            }
            if( v.unclaimed_unrouted )
                total_unclaimed_overlay += v.unclaimed_unrouted;
            if( v.unrouted_records )
            {
                printf("  %-11s   %d record(s) state a field fields/%s.ini routes to "
                       "neither side (%d of them named by no membership file)\n",
                       type->name, v.unrouted_records, type->name, v.unclaimed_unrouted);
                total_unrouted += v.unrouted_records;
            }
        }
        cp_membership_free(&client);
        cp_membership_free(&server);
        if( !ok )
            return 0;
    }

    printf("Membership vs provenance: %d namespace(s) with a pair, %d disagreement(s) that "
           "no gate explains\n",
           pairs, errors);
    printf("  %d server name(s) across all namespaces have a band and no authored block\n",
           total_band_only);
    printf("  %d authored record(s) are named by no membership file (%d namespace(s) have "
           "no pair at all)\n  %d record(s) state a field their register routes to "
           "neither side — no membership statement can route one\n",
           total_unclaimed_overlay, nopair_overlay_types, total_unrouted);
    /*
     * This said "routing is unchanged" while step 2 was the head of the
     * migration. Step 3 landed and it stopped being true: `pack` routes on
     * these files now, and the check's job changed with it — from a preview of
     * a switch nobody had thrown into a standing second source, held against
     * the provenance the gate no longer asks.
     */
    printf("`cachepack pack` routes on these files "
           "(docs/PACK_ENTITY_SPLIT_PLAN.md §4 step 3); this check holds them "
           "against the provenance the gate no longer asks.\n");
    return errors == 0;
}

/* ---- verify -------------------------------------------------------------- */

/*
 * The standing bar for every codec in this library: decode a record, write it back,
 * and compare. Here the trip is longer — record -> text -> record — so it measures
 * the text layer as well as the codecs.
 *
 * Two columns, for the same reason the library's own round-trip suite reports two:
 * a mismatch at *identical length* is a re-encoding (opcode order, or an aliased
 * opcode collapsed to its lowest spelling), while a mismatch at a different length
 * is a field that did not survive. High same-length with low exact is expected and
 * harmless; low both is a loss worth chasing.
 */
int
cp_verify_run(
    struct CP_Ctx* ctx,
    const struct CP_Selection* sel,
    const char* digest_dir)
{
    printf("Seeding names from the cache's gameval table...\n");
    cp_names_seed_from_cache(ctx);
    cp_names_report_coverage(ctx);

    printf("%-11s %8s   %8s %8s %8s   %8s %8s\n", "type", "records", "exact", "same-len",
           "differ", "codec-ex", "lost-here");

    uint8_t* buffer = malloc(64 * 1024);
    if( !buffer )
        return 0;
    uint8_t* codec_buffer = malloc(64 * 1024);
    if( !codec_buffer )
    {
        free(buffer);
        return 0;
    }
    int any_loss = 0;
    int text_regression = 0;

    for( int t = 0; t < CP_TYPE_COUNT; t++ )
    {
        if( !sel->all && !(sel->mask & (1u << t)) )
            continue;
        const struct CP_Type* type = cp_type(t);
        if( type->flags & CP_TYPE_NO_ENCODER )
        {
            printf("%-11s %8s   (decode only)\n", type->name, "-");
            continue;
        }
        cp_codec_fn codec = cp_codec_roundtrip(t);

        /*
         * A per-record digest of the bytes `pack` would write.
         *
         * Captured now, while the packer's output is still a pure function of
         * `configs/all.<type>`. Once a server overlay is merged in before encoding,
         * the only way to show that an *unoverlaid* record did not move is to
         * compare against a snapshot taken before the merge existed — a golden
         * captured afterwards proves nothing.
         *
         * Per record, not a whole-cache hash: a whole-cache hash says a byte moved
         * and not which record moved it, and the assertion this feeds is set
         * containment over record ids.
         */
        FILE* digests = NULL;
        if( digest_dir )
        {
            char path[1200];

            snprintf(path, sizeof(path), "%s/%s.digests", digest_dir, type->name);
            digests = fopen(path, "wb");
            if( !digests )
                fprintf(stderr, "cachepack: cannot write %s\n", path);
        }

        struct CP_Group group;
        if( !cp_group_open(ctx, t, &group) )
            continue;

        int records = 0, exact = 0, same_len = 0, differ = 0;
        int codec_exact = 0, lost_here = 0;
        struct CP_Lines lines;
        cp_lines_init(&lines);

        for( int i = 0; i < group.count; i++ )
        {
            int id = group.ids ? group.ids[i] : i;
            int size = 0;
            const uint8_t* record = cp_group_record(&group, i, &size);
            if( !record )
                continue;

            /* The baseline: what the library's own codec manages on this record. */
            int codec_ok = 0;
            if( codec )
            {
                uint32_t cw = codec(ctx, record, size, codec_buffer, 64 * 1024);
                codec_ok = cw == (uint32_t)size && memcmp(codec_buffer, record, (size_t)size) == 0;
                codec_exact += codec_ok;
            }

            cp_lines_clear(&lines);
            if( !type->unpack(ctx, id, record, size, &lines) )
                continue;

            /*
             * Round-trip through the *text*, not through the struct: serialising
             * the lines and parsing them back is what makes this a test of the
             * escaping and the key names rather than only of the codecs.
             */
            size_t text_size = 0;
            char* text = cp_lines_to_string(&lines, cp_name_ensure(ctx, t, id), &text_size);
            if( !text )
                break;

            struct CP_ConfigFile file;
            int parsed = cp_config_file_load_memory(&file, text, text_size, type->name);
            free(text);
            if( !parsed || file.count != 1 )
            {
                if( parsed )
                    cp_config_file_free(&file);
                differ++;
                records++;
                continue;
            }

            uint32_t written = type->pack(ctx, id, &file.configs[0], buffer, 64 * 1024);
            records++;
            if( digests && written )
                fprintf(digests, "%d=%08x\n", id,
                        RSCache_Crc32Buffer(buffer, (size_t)written));
            int text_ok = written == (uint32_t)size &&
                          memcmp(buffer, record, (size_t)size) == 0;
            if( text_ok )
                exact++;
            else if( written == (uint32_t)size )
                same_len++;
            else
                differ++;
            /* The number that matters: a record the codec reproduced and the text
             * did not is this tool's fault, and nothing else in the report
             * distinguishes it from the library's own documented losses. */
            if( codec_ok && !text_ok )
                lost_here++;
            cp_config_file_free(&file);
        }

        if( digests )
            fclose(digests);
        cp_lines_free(&lines);
        cp_group_free(&group);

        printf("%-11s %8d   %8d %8d %8d   %8d %8d%s\n", type->name, records, exact, same_len,
               differ, codec_exact, lost_here, lost_here ? "  <-- text layer" : "");
        if( differ > 0 )
            any_loss = 1;
        if( lost_here > 0 )
            text_regression = 1;
    }

    free(codec_buffer);
    free(buffer);

    if( any_loss )
        printf("\nRecords in the `differ` column changed length: a field did not survive the\n"
               "trip. `same-len` is a re-encoding (opcode order or an aliased opcode) and\n"
               "costs nothing.\n");
    printf("\n`codec-ex` is what the library's own decode->encode manages on the same\n"
           "records, with no text in between; `lost-here` counts records it reproduced\n"
           "byte-exactly and the text did not. That column is the one this tool owns,\n"
           "and it should be zero — everything else is the library's, and measured.\n");

    /* A non-zero lost-here column is a failure of this tool, so it fails the run.
     * The other columns are the library's fidelity and are reported, not judged. */
    return !text_regression;
}
