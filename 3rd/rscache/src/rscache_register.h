#ifndef RSCACHE_REGISTER_H
#define RSCACHE_REGISTER_H

/*
 * The field register: `fields/<type>.ini` in a content tree.
 *
 * ## What it is
 *
 * The declaration of every field a config type carries beyond what the client
 * cache's own opcodes express: the server's fields (`hitpoints`, a varp's
 * `scope`), how each is spelled in config text, whether and how it reaches the
 * client, and where it lands in the server band.
 *
 *     [npc.hitpoints]    server = opcode:77:u2
 *     [npc.death_drop]   server = opcode:151:u4   ref = obj   text = param
 *     [npc.desc]         scope = client           client = native
 *
 * ## Why it lives in this library
 *
 * It is the interface between the content tools and whatever reads their output.
 * cachepack writes the server band from it; the game server reads the band back
 * with it; and both used to carry their own parser of this one file, which is how
 * the two came to disagree about what a field was. The library holds the parser
 * and the band codec (`rscache_band.h`), and knows nothing about any particular
 * field: what `hitpoints` MEANS is the server's, injected as a binding of field
 * name to struct member (`rscache_band.h`), never compiled in here. Nothing in this
 * library includes anything from the game's own source tree.
 *
 * ## Grammar
 *
 * Sections `[<type>.<field>]` declare a field; a section may appear more than once
 * and its rows merge, so a file can state the projection in one place and the
 * server opcodes in another. A bare `[<type>]` section is about the type itself.
 * Rows are `key = value`; `;` and `#` start a comment. Keys:
 *
 *   scope  = server | client                  who reads it (default server)
 *   client = drop | native | error | param:<p>  how it reaches the client (default drop)
 *   param  = <p>                              its runtime param binding
 *   server = opcode:<64..255>:<u1|u2|u4|string|list> | drop
 *                                             its home in the server band
 *   type   = <t>[,<t>...]                     a list's tuple element types (and a
 *                                             scalar's value type), spelled as db
 *                                             columns and params spell them: int,
 *                                             string, boolean, coord, obj, npc, ...
 *   ref    = <config type>                    namespace a symbolic value names
 *   values = <word>:<n>[,<word>:<n>...]       the words a scalar int field is
 *                                             spelled with (`huntmode=aggressive`),
 *                                             and the number each packs as
 *   text   = key | param | indexed | list     its ONE spelling in config text (a
 *                                             list spelled `param` is one
 *                                             `param=<name>,<tuple>` line per tuple)
 *   records = client                          (bare section) added records go to
 *                                             the client cache by default
 *
 * An unknown key is skipped: a row only one reader understands is how the two
 * halves of one file stay independent. A malformed known row is counted in
 * `rejected` and reported, never guessed at.
 */

#include <stddef.h>

/** Who reads the field. */
enum RSCache_RegisterScope
{
    RSCACHE_REGISTER_SCOPE_SERVER = 0,
    RSCACHE_REGISTER_SCOPE_CLIENT,
};

/** How the field reaches the client cache, if at all. */
enum RSCache_RegisterClient
{
    /** Server-only. The client encoder never sees the key. Default. */
    RSCACHE_REGISTER_CLIENT_DROP = 0,
    /** The record's own client field; the client codec handles the key. */
    RSCACHE_REGISTER_CLIENT_NATIVE,
    /** Folded into the record's param table, under `param_name`. */
    RSCACHE_REGISTER_CLIENT_PARAM,
    /** The encoder must refuse a record stating it. */
    RSCACHE_REGISTER_CLIENT_ERROR,
};

/** A server-band payload width. NONE: the field has no home in the band. */
enum RSCache_RegisterWire
{
    RSCACHE_REGISTER_WIRE_NONE = 0,
    RSCACHE_REGISTER_WIRE_U1,
    RSCACHE_REGISTER_WIRE_U2,
    RSCACHE_REGISTER_WIRE_U4,
    /** A string, NUL-terminated on the wire. */
    RSCACHE_REGISTER_WIRE_STRING,
    /** A u16 count of tuples, each element by its `type`: a `string` element is a
     *  NUL-terminated string, every other type an int32. */
    RSCACHE_REGISTER_WIRE_LIST,
};

/** A field's one spelling in config text. */
enum RSCache_RegisterText
{
    /** `hitpoints=60`: a key of the type, stated by every record (`=default`). */
    RSCACHE_REGISTER_TEXT_KEY = 0,
    /** `param=death_drop,bones`: an entry of the record's param map. */
    RSCACHE_REGISTER_TEXT_PARAM,
    /** `stock1=beer,3,100`, `stock2=...`: a list, one numbered line per tuple. */
    RSCACHE_REGISTER_TEXT_INDEXED,
    /** `column=...` repeated: a list, one line per tuple under the bare key. */
    RSCACHE_REGISTER_TEXT_LIST,
};

enum
{
    RSCACHE_REGISTER_MAX = 128,
    /** Elements a list tuple (or a scalar's `type`) may declare. */
    RSCACHE_REGISTER_TYPES_MAX = 8,
    /** Words a `values = ...` row may declare. */
    RSCACHE_REGISTER_WORDS_MAX = 16,
};

struct RSCache_RegisterField
{
    /** `hitpoints`: the key as config text spells it, without the type prefix. */
    char name[48];
    enum RSCache_RegisterScope scope;
    enum RSCache_RegisterClient client;
    /** The param a `client = param:<p>` projection or `param = <p>` binds. */
    char param_name[48];
    /** 64..255, or 0 when the field has no server band. */
    int opcode;
    enum RSCache_RegisterWire wire;
    /** A config type name a symbolic value resolves through, or "". */
    char ref[32];
    enum RSCache_RegisterText text;
    /** `type = ...`: element type names, in tuple order. */
    char types[RSCACHE_REGISTER_TYPES_MAX][24];
    int type_count;
    /** `values = ...`: the words this field is spelled with, and their numbers.
     *  Two words may share a number; a number's spelling is its first word. */
    char words[RSCACHE_REGISTER_WORDS_MAX][24];
    int word_values[RSCACHE_REGISTER_WORDS_MAX];
    int word_count;
};

struct RSCache_Register
{
    char type[32];
    /** Band fields first, ascending by opcode; the rest after, in file order. */
    struct RSCache_RegisterField entries[RSCACHE_REGISTER_MAX];
    int count;
    /** How many of `entries` carry a server opcode (they are `[0, band_count)`). */
    int band_count;
    /** `records = client` in the bare `[<type>]` section. */
    int records_client;
    /** 1 when a file was read. */
    int from_file;
    /** Malformed rows (each reported). A register with any is not usable. */
    int rejected;
};

/**
 * Parse a register from memory. `type` is the config type the file describes;
 * rows for other types' sections are ignored. Diagnostics go to stderr, prefixed
 * `fields/<type>.ini`. Returns the field count.
 */
int
RSCache_RegisterParse(
    struct RSCache_Register* reg,
    const char* type,
    const char* text,
    size_t size);

/**
 * Read `<dir>/fields/<type>.ini`. A missing file is an empty register (count 0,
 * `from_file` 0): a type the tree declares nothing for. Returns the field count.
 */
int
RSCache_RegisterLoad(
    struct RSCache_Register* reg,
    const char* dir,
    const char* type);

/** The field a config line spelled `key` states: the field named `key`, or the
 *  `text = indexed` field `key` is a numbered line of (`patrol3` -> patrol).
 *  NULL when neither. */
const struct RSCache_RegisterField*
RSCache_RegisterFindLine(
    const struct RSCache_Register* reg,
    const char* key);

/** The field named `name`, or NULL. */
const struct RSCache_RegisterField*
RSCache_RegisterFind(
    const struct RSCache_Register* reg,
    const char* name);

/** The number `word` packs as for `field` (its `values = ...` row): 1 and
 *  `*out` set, or 0 when the field declares no such word. */
int
RSCache_RegisterWordValue(
    const struct RSCache_RegisterField* field,
    const char* word,
    int* out);

/** The word `value` is spelled with for `field`, or NULL. */
const char*
RSCache_RegisterValueWord(
    const struct RSCache_RegisterField* field,
    int value);

/**
 * The register's own consistency: no rejected row, no duplicate opcode, no field
 * the band cannot carry yet. Reports each problem; returns the count.
 */
int
RSCache_RegisterCheck(const struct RSCache_Register* reg);

#endif
