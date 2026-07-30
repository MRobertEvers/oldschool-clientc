#ifndef SRC_CONTENT_CONTENT_REGISTER_H
#define SRC_CONTENT_CONTENT_REGISTER_H

/*
 * The namespace register: `content.ini` at the root of a content tree.
 *
 * One declaration of what namespaces exist and who owns each part of them,
 * read by everything that loads symbols. Before this there were three tables,
 * in two languages, that agreed by accident:
 *
 *   mock230_content.c   k_namespaces[]        13 rows
 *   ssc_symbols.c       kind_for_pack()       21 filenames
 *   cachepack           cp_names_load         20 registered types
 *
 * `server/pack/category.pack` was loaded by the compiler and not by the runtime.
 * That was harmless — a category id is baked into bytecode at compile time, so
 * the runtime only needs the number off the obj record — but harmless by luck is
 * the problem: adding a namespace meant three edits in two languages, and
 * forgetting one produced a symbol that resolved in one tool and not the other.
 *
 * Three axes, which the old `server/pack/` split collapsed into one directory
 * name (docs/CONTENT_ARCHITECTURE.md §4):
 *
 *   ids    who may choose the number   cache | server | protocol
 *   names  where layer 0 comes from    cache | authored | derived | imported
 *
 * A tree with no `content.ini` gets the built-in defaults, so this is additive:
 * nothing has to ship the file before the loaders can use the register.
 */

#include <stddef.h>

/** Who may choose a record's id. */
enum ContentIdAuthority
{
    /** The client's cache fixes it. Never allocated by us. */
    CONTENT_IDS_CACHE = 0,
    /** Ours to assign, from the layer-0 high-water mark. */
    CONTENT_IDS_SERVER,
    /** The wire fixes it — `stat` is the index UPDATE_STAT carries, and moving
     *  it is not the cache's to do. */
    CONTENT_IDS_PROTOCOL,
};

/** Where the *generated* names (`pack/<ns>.pack`) come from. */
enum ContentNameAuthority
{
    /** The cache's own gameval table; cachepack regenerates the file wholesale. */
    CONTENT_NAMES_CACHE = 0,
    /** Humans wrote them; there is no generated file at all. */
    CONTENT_NAMES_AUTHORED,
    /** Crawled out of the configs — LostCity does this for `category`. */
    CONTENT_NAMES_DERIVED,
    /** Taken from a foreign revision's table, so every line is a *claim* that
     *  has to be re-validated against the cache actually in use. */
    CONTENT_NAMES_IMPORTED,
};

struct ContentNamespace
{
    /** `varp`, `npc`, `stat`. The pack file is `pack/<name>.pack`. */
    char name[48];
    enum ContentIdAuthority ids;
    enum ContentNameAuthority names;
    /** 1 when this namespace shares a RuneScript name domain with the others
     *  that set it — varp/varbit/varn/vars all answer to `%name`, so a name
     *  cannot mean one of each. */
    int shared_var_domain;
    /**
     * Archive id in the cache's gameval table (OSRS idx 24) that names this
     * namespace, or -1 when the cache names nothing here.
     *
     * Stated in the register because it is the *evidence* for the `names`
     * column, and keeping the two apart is what let three namespaces claim
     * `names = cache` while having no archive to be generated from — which told
     * cachepack it could rewrite `pack/param.pack`, and the save path then
     * deleted the 58-line header justifying every name in it.
     *
     * With both facts in one row the invariant is checkable without either side
     * having to see the other's table: `names == CONTENT_NAMES_CACHE` if and
     * only if `gameval_archive >= 0`. `ContentRegister_Validate` is that check.
     */
    int gameval_archive;
    /**
     * The first id this tree may allocate in the namespace, or 0 for one where
     * allocating is meaningless.
     *
     * Adding an npc or a model means picking a number the client's cache does not
     * use. With the cache pinned to one revision (docs/CONTENT_PACK_PLAN.md §0,
     * decision 1) the largest id it states is a *constant*, so this needs no
     * headroom gate — only somewhere to start, written down where both the
     * allocator and the boot check can see it.
     *
     * A round number above the cache's maximum rather than `max + 1`, so an id in
     * a log reads as ours at a glance. `param` is the exception at 2634, which is
     * exactly `max + 1`: those ids are already allocated and referenced by
     * content, and renumbering them again would be a second migration for no gain.
     *
     * **Zero means do not allocate**, and three kinds of namespace mean it:
     *
     *   - the id is composed from something else, so a counter cannot produce a
     *     valid one — `map` is `(x << 8) | z`, `component` is
     *     `(interface << 16) | child`, a `dbrow` is addressed through its table;
     *   - the wire fixes it (`stat`);
     *   - nobody has stated a base yet, which is the safe default. Refusing to
     *     allocate is recoverable; allocating on top of a real record is the
     *     failure `pack/param.pack` shipped for a while, when nine server params
     *     sat on ids the cache already defined.
     */
    int server_base;
};

enum
{
    CONTENT_REGISTER_MAX = 64,
};

struct ContentRegister
{
    struct ContentNamespace entries[CONTENT_REGISTER_MAX];
    int count;
    /** 1 when a `content.ini` was actually read, 0 when these are the built-in
     *  defaults. Worth reporting: a tree that meant to declare a namespace and
     *  misspelled the file gets defaults rather than an error otherwise. */
    int from_file;
};

/**
 * Read `<dir>/content.ini`, or fall back to the built-in defaults.
 *
 * Never fails: a missing or malformed file yields the defaults and says so, so
 * a tree that has not adopted the register still loads. Returns the number of
 * namespaces.
 */
int
ContentRegister_Load(
    struct ContentRegister* reg,
    const char* dir);

/** The defaults, without touching the filesystem. */
int
ContentRegister_Defaults(struct ContentRegister* reg);

/**
 * Refuse a register whose `names` authority contradicts its gameval evidence.
 *
 * One rule: a namespace claims `names = cache` if and only if a gameval archive
 * names it. Claiming it without one tells cachepack to regenerate a file that has
 * no generator, and the save path used to delete every comment in it; denying it
 * with one leaves the cache's own names unimported and every id spelled
 * `<type>_<id>`.
 *
 * The failure is a *class* rather than an instance — it recurs every time someone
 * adds a namespace — which is why this is a check and not a one-off correction.
 * Each violation is printed with the namespace and both facts. Returns the number
 * of violations, so 0 means agreement.
 */
int
ContentRegister_Validate(const struct ContentRegister* reg);

/**
 * Trim an ini key or value in place, and drop an inline `;`/`#` comment.
 *
 * Exported because **3rd/ini does not do it**, and every consumer that forgets
 * gets the same silent failure: the parser skips whitespace *before* an element,
 * then takes the key as everything up to `=` and the value as everything to end
 * of line, so `ids       = cache` arrives as key `"ids       "` and value
 * `" cache"`. A plain strcmp then matches nothing, and a perfectly valid file
 * parses into no settings at all while reporting success.
 *
 * Safe for bare-word values, which is what both the register and the player save
 * format use. A value that could legitimately contain `;` needs its own parse.
 */
void
ContentIni_Trim(char* text);

/** By name, or NULL. */
const struct ContentNamespace*
ContentRegister_Find(
    const struct ContentRegister* reg,
    const char* name);

/** The namespace a pack *filename* belongs to (`varp.pack` -> `varp`), or NULL.
 *  This is what replaces the compiler's filename table. */
const struct ContentNamespace*
ContentRegister_ForPackFile(
    const struct ContentRegister* reg,
    const char* filename);

#endif
