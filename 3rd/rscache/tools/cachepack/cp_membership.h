#ifndef RSCACHE_TOOLS_CACHEPACK_CP_MEMBERSHIP_H
#define RSCACHE_TOOLS_CACHEPACK_CP_MEMBERSHIP_H

/*
 * `pack/<ns>.client` and `pack/<ns>.server` — which *entities* have a half on
 * each side.
 *
 * `fields/<type>.ini` says which **fields** reach which side. This says which
 * **entities** do. The two are independent questions and the packer answers only
 * the first on disk today; the second is inferred at pack time from where a
 * `[name]` block happened to be authored (`cp_pack.c`'s `origin_rank > 0 &&
 * !records_client`) and leaves no artifact a second tool could disagree with.
 * See docs/PACK_ENTITY_SPLIT_PLAN.md.
 *
 * ## Names, never ids
 *
 * A line here is a bare entity name. The id comes from the one place it has ever
 * come from — `configs/all.<ns>.compack`, the member index the two sides *share*
 * — because the whole point of the design is that ids and names stay in one
 * namespace per data type and only membership is split. A membership file that
 * carried an id would be a second id authority, and worse: an id has nothing to
 * resolve against and so cannot fail to resolve, which is bar 5 of
 * docs/LOSTCITY_PORT_TRIAGE.md §13. A misspelled *name* is caught the moment
 * something looks it up.
 *
 * That is also why this is not `LC_Pack`. That format is `id=name` and its
 * merge, sparse and synthetic-id rules all key off the id column. Sharing the
 * container would have meant inventing an id for every line, which is the exact
 * thing the format is trying not to have.
 *
 * ## Sorted by name, because there is no id to sort by
 *
 * `LC_Pack` writes ascending by id so a diff shows only appended lines. With no
 * id the equivalent stable order is the name itself, and it has the same
 * property: two emissions of the same content are byte-identical and a review
 * diff is the set difference. Emission order — merge order, which is walk order
 * — would be reproducible too, but it would look arbitrary to a reader and would
 * churn whenever a file was renamed.
 *
 * ## Comments are data
 *
 * Same rule and same reason as `lc_pack.h`: a header, a per-name justification
 * and a trailing block all survive a rewrite. `pack/param.pack` lost its 58-line
 * header to one tool that emitted only the data lines, and that header was the
 * record of which claims had been checked and how. These files are authored by
 * nature — a name moves sides because a person decided it should — so prose is
 * the majority of their value.
 *
 * ## Who may write them
 *
 * Nothing regenerates a membership file. `cachepack pack`, `unpack` and `verify`
 * never open one, and `cachepack membership` — the one-shot that seeds them from
 * the routing the packer performs today — **creates only files that do not
 * exist** and refuses to overwrite. `content.ini` states the same fact as
 * `membership = authored`, so the protection is a declaration rather than a
 * property of one function; see `cp_register.h`.
 *
 * Three things *read* them. `cachepack pack` **routes** on them — an entity gets
 * a server band only if `<ns>.server` names it, and reaches the client cache only
 * if `<ns>.client` names it or the base cache already holds its id
 * (docs/PACK_ENTITY_SPLIT_PLAN.md §4 step 3, and `cp_pack.c`'s entity-gate
 * section for the rule in full). `cachepack membership --check-only` holds them
 * against provenance; `ToriRSServer_Pack` holds them against each namespace's
 * allocation base and against the cache, which is why this file compiles into
 * that binary too (`src/makefile`, `TORIRSSERVER_PACK_SRCS`). It depends on nothing but
 * stdio so that it can. None of the three writes.
 *
 * A file that is *absent* and a file that is *empty* are different statements and
 * the packer acts on the difference — see `present`.
 *
 * A save is therefore a plain write and not a merge, unlike `lc_pack_save`.
 * The merge there exists because two tools write one file — a re-seed from the
 * cache's gameval table and a human — and neither knows the other's lines. Here
 * there is one writer and it writes once. If that ever stops being true, this
 * needs `lc_pack`'s merge before the second writer lands, not after.
 */

#include <stddef.h>

/** Which side a file states. The value is also the file's extension, so the two
 *  cannot be spelled differently in two places. */
enum CP_MembershipSide
{
    CP_MEMBERSHIP_CLIENT = 0,
    CP_MEMBERSHIP_SERVER = 1,
    CP_MEMBERSHIP_SIDES = 2,
};

/**
 * One membership file in memory, comments and all.
 *
 * `names`, `trailing` and `preceding` are one table with three columns, dense
 * and sorted by `names[i]`. A comment is anchored to the name that follows it,
 * which is what lets the file stay sorted while a justification stays attached
 * to what it justifies.
 */
struct CP_Membership
{
    /** Namespace as `configs/all.<ns>.compack` spells it. */
    char ns[48];
    enum CP_MembershipSide side;
    char** names;
    /** The raw `// ...` trailing a name, whitespace included, or NULL. */
    char** trailing;
    /** Comment and blank lines immediately preceding a name, or NULL. */
    char** preceding;
    int count;
    int capacity;
    /** The comment block before the first name — the file header. */
    char* preamble;
    /** The comment block after the last name, anchored to no name. */
    char* trailer;
    /** Lines that were neither prose nor a bare name. Skipped, but counted and
     *  reported: a thinned file should be visible rather than silent. */
    int malformed;
    /** Names the file states twice. A duplicate is not an error the reader can
     *  fix by choosing, so it is counted here and reported by the caller. */
    int duplicates;
    /**
     * 1 when the file was on disk — which is not the same as `count > 0`.
     *
     * An empty file is a *statement*: `npc.client` is empty because the tree adds
     * no npc the client is told about, and both halves of a pair are written even
     * when one of them is empty for exactly that reason (§8.3). An absent file is
     * the opposite — nobody has said anything yet — and the routing gate treats
     * the two differently: where `<ns>.server` exists it is the whole answer for
     * that namespace, and where it does not the packer keeps the gate it used
     * before. A reader that could not tell them apart would silently delete every
     * server band in a tree that had not been seeded yet.
     */
    int present;
};

/** The extension for a side: "client" or "server". */
const char*
cp_membership_side_name(enum CP_MembershipSide side);

/** `<srcdir>/pack/<ns>.<side>`. */
void
cp_membership_path(
    char* out,
    size_t out_size,
    const char* srcdir,
    const char* ns,
    enum CP_MembershipSide side);

void
cp_membership_init(
    struct CP_Membership* set,
    const char* ns,
    enum CP_MembershipSide side);

void
cp_membership_free(struct CP_Membership* set);

/**
 * Insert `name`, keeping the array sorted.
 *
 * Returns 1 when the name was added, 0 when it was already listed, -1 on
 * allocation failure. "Already listed" is a normal answer rather than an error:
 * the two gates a seed reads can both name the same record.
 */
int
cp_membership_add(
    struct CP_Membership* set,
    const char* name);

/** 1 when `name` is listed. */
int
cp_membership_has(
    const struct CP_Membership* set,
    const char* name);

/**
 * Read `path`.
 *
 * `allow_missing` treats a nonexistent file as an empty set, which is how a
 * namespace with no membership stated reads. A file that exists and cannot be
 * read is always a failure. Returns 0 on failure.
 */
int
cp_membership_load(
    struct CP_Membership* set,
    const char* path,
    const char* ns,
    enum CP_MembershipSide side,
    int allow_missing);

/**
 * Write `path`: the preamble, then one name per line ascending with its
 * preceding block and trailing note, then the trailer.
 *
 * Not a merge — see the header comment. Returns 0 on failure.
 */
int
cp_membership_save(
    const struct CP_Membership* set,
    const char* path);

#endif
