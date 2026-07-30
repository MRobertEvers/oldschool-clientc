#ifndef SRC_CONTENT_CONTENT_FIELDS_H
#define SRC_CONTENT_CONTENT_FIELDS_H

/*
 * The field register: `fields/<type>.ini` in a content tree.
 *
 * Content is authored in its own schema, and the client's cache is a *projection*
 * of it (docs/CONTENT_PACK_PLAN.md §5). A record has fields the client reads, fields
 * only the server reads, and fields that reach the client by being folded into its
 * param table — and until now the third group was a hardcoded C table:
 *
 *     static const struct BakedParam baked[] = {
 *         { "hitpoints",   def->hitpoints   },
 *         { "attacklevel", def->attack      },
 *         ...
 *
 * That table said the same thing three times over. `mock230_content.c`'s key ladder
 * already knew `hitpoints` was a field; `pack/param.pack` already knew what number
 * `hitpoints` was; and this restated both, in C, where a content author could not
 * see it and could not change it without a rebuild. Adding a projected field meant
 * editing a struct, a parser and a baker, and forgetting the third produced a field
 * that loaded, worked in the mock, and silently never reached the cache.
 *
 * So the projection is declared:
 *
 *     ; fields/npc.ini
 *     [npc.hitpoints]    scope = server   client = param:hitpoints
 *     [npc.name]         scope = client   client = native
 *     [npc.death_drop]   scope = server   client = param:death_drop
 *     [npc.drop_table]   scope = server   client = drop
 *     [npc.new_thing]    scope = server   client = error
 *
 * **The default is `scope = server`, `client = drop`.** A field reaches the client
 * because someone wrote down that it does — opt in, never opt out.
 *
 * `error` is the disposition worth having deliberately: it is how you find out at
 * *build* time that you authored something the client will never see, rather than
 * in game.
 *
 * ## What this does not do
 *
 * It does not yet drive the config *parser*. `mock230_content.c` still has its own
 * `strcmp` ladder mapping a key to a struct field, and unifying the two is the rest
 * of §5.3. What the register owns today is the projection — which field becomes
 * which param — and that is the half that was duplicated.
 */

#include <stddef.h>

/** Who reads the field. */
enum ContentFieldScope
{
    /** The server, and nothing else unless `client` says otherwise. Default. */
    CONTENT_SCOPE_SERVER = 0,
    /** The client's own record field. */
    CONTENT_SCOPE_CLIENT,
};

/** How the field reaches the client, if at all. */
enum ContentFieldClient
{
    /**
     * Server-only; the client encoder omits it silently. Default.
     *
     * For fields with no sensible cache representation at all — drop tables,
     * dialogue, RuneScript — not for fields being withheld. Nothing here is secret
     * (docs/CONTENT_PACK_PLAN.md §0, decision 4), so a param is readable by anyone
     * holding the cache and that is fine.
     */
    CONTENT_CLIENT_DROP = 0,
    /** The record's own field in the dat2 encoding. */
    CONTENT_CLIENT_NATIVE,
    /** Folded into the record's param table, under `param_name`. */
    CONTENT_CLIENT_PARAM,
    /** The client encoder must refuse. No silent loss allowed. */
    CONTENT_CLIENT_ERROR,
};

struct ContentField
{
    /** `hitpoints` — the key as configs and the field register spell it. */
    char name[48];
    enum ContentFieldScope scope;
    enum ContentFieldClient client;
    /** For `CONTENT_CLIENT_PARAM`: the param's *name*, resolved through
     *  `pack/param.pack` at bake time. A name and not a number, because which
     *  number `hitpoints` is is the pack file's business and it has already been
     *  renumbered once. */
    char param_name[48];
};

enum
{
    CONTENT_FIELDS_MAX = 128,
};

struct ContentFields
{
    struct ContentField entries[CONTENT_FIELDS_MAX];
    int count;
    /** 1 when a `fields/<type>.ini` was actually read. Worth reporting: a tree that
     *  meant to declare a projection and misspelled the file gets the built-in
     *  defaults and bakes something subtly different. */
    int from_file;
};

/**
 * Read `<dir>/fields/<type>.ini`, or fall back to the built-in defaults.
 *
 * Never fails: a missing or malformed file yields the defaults and says so, so a
 * tree that has not adopted the register still bakes what it baked before. Returns
 * the number of fields.
 */
int
ContentFields_Load(
    struct ContentFields* fields,
    const char* dir,
    const char* type);

/** The defaults for `type` (`npc`, `loc`), without touching the filesystem. */
int
ContentFields_Defaults(
    struct ContentFields* fields,
    const char* type);

/** By name, or NULL. */
const struct ContentField*
ContentFields_Find(
    const struct ContentFields* fields,
    const char* name);

#endif
