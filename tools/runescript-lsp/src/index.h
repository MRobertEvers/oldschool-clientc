#ifndef TOOLS_RUNESCRIPT_LSP_INDEX_H
#define TOOLS_RUNESCRIPT_LSP_INDEX_H

/*
 * The workspace index: every name the content tree declares, and where.
 *
 * A name in RuneScript is resolved against a set of namespaces, not a scope
 * chain — `bronze_bar` is an obj because an obj record is named that, and
 * `%bankpin_code` is a varp because `pack/varp.alloc` allocated one. So the
 * index is a name -> (kind, id, definition site) multimap, and the same name
 * legitimately appears in several namespaces at once (an interface and a loc
 * routinely share one).
 *
 * A single name can also have several definition sites within one namespace,
 * and they answer different questions:
 *
 *   the record       `[bankpin_code]` in a .varp file — what it IS
 *   the allocation   `5727=bankpin_code` in pack/varp.alloc — the id this
 *                    server gave it
 *   the name index   `5727=bankpin_code` in configs/all.varp.compack — the id
 *                    the cache knows it by
 *   the membership   a bare line in pack/varp.server — which half owns it
 *
 * Go-to-definition returns all of them rather than picking, because which one
 * you wanted depends on what you were about to change.
 */

#include <stddef.h>
#include <stdint.h>

enum RS_Kind
{
    RS_KIND_UNKNOWN = 0,

    /* Scripts */
    RS_KIND_PROC,
    RS_KIND_LABEL,
    RS_KIND_TRIGGER_SCRIPT, /**< `[opheldu,bow_string]` and friends */
    RS_KIND_CLIENTSCRIPT,
    RS_KIND_COMMAND, /**< declared in engine.rs2, or a built-in opcode */

    /* Values */
    RS_KIND_CONSTANT,
    RS_KIND_LOCAL,
    RS_KIND_TRIGGER, /**< the trigger word itself: `opheldu`, `login` */
    RS_KIND_TYPE,    /**< `int`, `obj`, `coord`, ... */

    /* Cache namespaces */
    RS_KIND_NPC,
    RS_KIND_OBJ,
    RS_KIND_LOC,
    RS_KIND_INV,
    RS_KIND_ENUM,
    RS_KIND_STRUCT,
    RS_KIND_PARAM,
    RS_KIND_SEQ,
    RS_KIND_SPOTANIM,
    RS_KIND_VARP,
    RS_KIND_VARBIT,
    RS_KIND_VARC,
    RS_KIND_VARN,
    RS_KIND_VARS,
    RS_KIND_DBTABLE,
    RS_KIND_DBROW,
    RS_KIND_DBCOLUMN, /**< always qualified: `fletching_table:product` */
    RS_KIND_INTERFACE,
    RS_KIND_COMPONENT,
    RS_KIND_CATEGORY,
    RS_KIND_STAT,
    RS_KIND_SYNTH,
    RS_KIND_JINGLE,
    RS_KIND_IDK,
    RS_KIND_MESANIM,
    RS_KIND_HITSPLAT,
    RS_KIND_HEALTHBAR,
    RS_KIND_MAPELEMENT,
    RS_KIND_OVERLAY,
    RS_KIND_UNDERLAY,
    RS_KIND_MODEL,
    RS_KIND_ANIM,
    RS_KIND_SPRITE,
    RS_KIND_TEXTURE,
    RS_KIND_FONT,
    RS_KIND_MAP,
    RS_KIND_SPAWN,

    RS_KIND_COUNT
};

/** How a definition site states the name. */
enum RS_Origin
{
    RS_ORIGIN_RECORD = 0, /**< `[name]` in a .npc/.obj/.varp/... file */
    RS_ORIGIN_ALLOC,      /**< `id=name` in pack/<ns>.alloc */
    RS_ORIGIN_NAMEINDEX,  /**< `id=name` in a .pack/.compack/.memberpack */
    RS_ORIGIN_MEMBERSHIP, /**< a bare name line in pack/<ns>.client|.server */
    RS_ORIGIN_SCRIPT,     /**< a `[trigger,subject]` header */
    RS_ORIGIN_CONSTANT,   /**< `^name = value` */
    RS_ORIGIN_SPAWN,      /**< a row in a .spawn file */
    RS_ORIGIN_BUILTIN     /**< an engine opcode, with no file behind it */
};

struct RS_Symbol
{
    char* name;
    enum RS_Kind kind;
    enum RS_Origin origin;
    int32_t id; /**< -1 when the site does not state one. */

    char* file; /**< absolute path; NULL for RS_ORIGIN_BUILTIN. */
    uint32_t line;
    uint32_t character;
    uint32_t end_line;
    uint32_t end_character;

    char* detail; /**< signature, value text, or the record's `name=` field. */
    char* doc;    /**< the `//` block immediately above the definition. */

    /**
     * The name this one is a second spelling of, or NULL.
     *
     * The decompiled client corpus addresses an interface by id —
     * `interface_774:48` — where the cache calls 774 `toa_partydetails`. The
     * alias resolves on its own, but its *members* are indexed under the real
     * name, so a qualified lookup has to re-spell the left half before it can
     * find the right one. This is what it re-spells to.
     */
    char* canonical;
};

struct RS_Index
{
    struct RS_Symbol* symbols;
    int count;
    int capacity;

    /** Indices into `symbols`, sorted by (name, kind, origin). */
    int32_t* order;
    int sorted;

    /** Every file the index read, for the workspace-wide reference scan. */
    char** files;
    int file_count;
    int file_capacity;

    char** roots;
    int root_count;

    /** Property keys seen per extension, for completion inside config files. */
    struct RS_KeySet* keysets;
    int keyset_count;
    int keyset_capacity;
};

void
RS_IndexInit(struct RS_Index* index);

void
RS_IndexFree(struct RS_Index* index);

/** Walk `root` and index everything under it. Safe to call for several roots. */
void
RS_IndexAddRoot(struct RS_Index* index, const char* root);

/** Re-read one file, replacing whatever it contributed before. */
void
RS_IndexReloadFile(struct RS_Index* index, const char* path);

/** Seed the engine's own vocabulary: opcode names, trigger names, type names. */
void
RS_IndexAddBuiltins(struct RS_Index* index);

/**
 * Every symbol called `name`, as a contiguous run of `index->order`.
 *
 * Returns the count and writes the first position; the caller reads
 * `index->symbols[index->order[first + i]]`.
 */
int
RS_IndexFind(struct RS_Index* index, const char* name, int* out_first);

/** The first symbol of that kind called `name`, or NULL. */
const struct RS_Symbol*
RS_IndexFindKind(struct RS_Index* index, const char* name, enum RS_Kind kind);

/** Names beginning with `prefix`, for completion. Returns how many were written. */
int
RS_IndexPrefix(
    struct RS_Index* index,
    const char* prefix,
    int32_t* out,
    int out_capacity);

/** The lower-case namespace word for a kind — "npc", "varp", "proc". */
const char*
RS_KindName(enum RS_Kind kind);

/** True when this extension is a text declaration file the index reads. */
int
RS_IsConfigExtension(const char* extension);

/** The kind a config file extension declares records of, or RS_KIND_UNKNOWN. */
enum RS_Kind
RS_KindForExtension(const char* extension);

/** The kind a `pack/<stem>.alloc`-style namespace stem names. */
enum RS_Kind
RS_KindForNamespace(const char* stem);

const char*
RS_OriginName(enum RS_Origin origin);

/** True when this kind can be written with a `%` sigil. */
int
RS_KindIsVariable(enum RS_Kind kind);

/**
 * The canonical spelling of `name`, or NULL when it is already canonical.
 *
 * Used to turn `interface_774:48` into `toa_partydetails:48`.
 */
const char*
RS_IndexCanonical(struct RS_Index* index, const char* name);

/** The property keys seen in files with this extension. */
int
RS_IndexKeys(
    struct RS_Index* index,
    const char* extension,
    const char* const** out_keys);

#endif
