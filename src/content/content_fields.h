#ifndef SRC_CONTENT_CONTENT_FIELDS_H
#define SRC_CONTENT_CONTENT_FIELDS_H

/*
 * The server's view of the field register: `fields/<type>.ini` in a content tree.
 *
 * Content is authored in its own schema, and the client's cache is a *projection*
 * of it (docs/CONTENT_PACK_PLAN.md §5). A record has fields the client reads, fields
 * only the server reads, and fields that reach the client by being folded into its
 * param table; the register declares which is which, per field:
 *
 *     ; fields/npc.ini
 *     [npc.hitpoints]    scope = server   server = opcode:77:u2
 *     [npc.name]         scope = client   client = native
 *     [npc.death_drop]   scope = server   server = opcode:151:u4   ref = obj
 *     [npc.new_thing]    scope = server   client = error
 *
 * **The default is `scope = server`, `client = drop`.** A field reaches the client
 * because someone wrote down that it does — opt in, never opt out.
 *
 * ## One parser
 *
 * The grammar and its parser are the rscache library's (`rscache_register.h`),
 * the same code cachepack reads the file with. This server used to carry a second
 * parser of the same file, and a file two programs read with two parsers is a file
 * they can come to disagree about — about a field's opcode, say, which is a value
 * landing in the wrong struct member with no error anywhere. What stays here is
 * only the server's half: the built-in defaults, and the rule for laying a tree's
 * file over them.
 *
 * ## The defaults, and the overlay
 *
 * A tree without `fields/<type>.ini` still boots: it gets the built-in defaults
 * (content_fields.c). A tree *with* one gets the defaults with the file laid over
 * them, so a tree that declares only the one field it invented still gets the ones
 * that already worked — the same rule the namespace register (`content.ini`)
 * follows.
 *
 * Both halves go through the one parser. The defaults are register TEXT, and the
 * overlay is the grammar's own: a section may appear more than once and its rows
 * merge in order, so the defaults followed by the file, parsed as one text, is
 * exactly "the file's rows win". No default carries a server opcode, so the band a
 * tree's register declares is the file's alone — the same band cachepack reads with
 * `RSCache_RegisterLoad`.
 *
 * ## What the server reads from it
 *
 *   scope   an unhandled npc/loc config key the register calls `client` is a key
 *           the client's own record already states: counted, not an error
 *   param   which runtime param an authored loc `param=<name>,...` line binds
 *   ref     the namespace a symbolic value resolves through
 *   server  the band: which opcode and width each band field is read at
 *           (`torirs_server_servercodec.h` binds those names to struct members)
 */

#include "rscache_register.h"

/**
 * The built-in defaults for `type`, with `<dir>/fields/<type>.ini` laid over them
 * when the tree has one. Returns the field count.
 *
 * Never refuses to load: a malformed row is reported and counted in
 * `fields->rejected`, and RSCache_RegisterCheck turns that into a problem count
 * for the caller to act on.
 */
int
ContentFields_Load(
    struct RSCache_Register* fields,
    const char* dir,
    const char* type);

/** The built-in defaults for `type` (`npc`, `loc`; any other type has none),
 *  without touching the filesystem. Returns the field count. */
int
ContentFields_Defaults(
    struct RSCache_Register* fields,
    const char* type);

/** The defaults for `type` as register text; "" for a type with none. */
const char*
ContentFields_DefaultsText(const char* type);

#endif
