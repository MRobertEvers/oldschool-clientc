#ifndef SRC_EDITOR_EDITOR_ALLOC_H
#define SRC_EDITOR_EDITOR_ALLOC_H

/**
 * Id allocation for new records, by tools/ss_allocate.py's rules
 * (docs/MAPED_DESIGN.html #documents "Pack and membership", #vars, #save).
 *
 * A new config record needs a number, and the number is the one thing two
 * writers must never both choose. ss_allocate.py is the authority on how one
 * is chosen; this mirrors it rule for rule so an id the editor hands out is
 * the id the build would have handed out, and editor_alloc_test holds the two
 * to the same answer on the real tree:
 *
 *   - The id is one past the highest id ANY layer claims — the namespace's
 *     member index (`configs/all.<ns>.compack`, or `pack/<ns>.pack` for
 *     3_interfaces / component / stat / category), every lane's member index
 *     and ledger under `ported/<lane>/` in name order, the numeric `[<ns>_N]`
 *     blocks of `configs/all.<ns>`, and the ledger `pack/<ns>.alloc` itself —
 *     but never below the namespace's floor: `content.ini`'s `base =` for
 *     `[namespace:<ns>]`, else the register's (src/content/content_register.c).
 *   - A name any layer already binds is not allocated again. For varp, varbit
 *     and varc a name carries its kind and id (`varp6400_my_counter`), so the
 *     bare name a layer holds prefixed is known too; a new var's name is
 *     coined `<prefix><id>_<name>`, and a name that already spells a prefix
 *     keeps that id when it is free and at or above the floor.
 *   - A namespace whose `ids` axis is not `server` is never allocated into
 *     the ledger (a cache-owned id is read off the cache, not invented).
 *   - The ledger is appended to, never rewritten: an empty one gets
 *     ss_allocate.py's header, one without the marker line gets the marker,
 *     then `id=name`.
 *
 * Plus the one rule the server adds: varps live in the server headroom, so a
 * varp at or past TORIRSSERVER_VARP_CACHE_MAX + TORIRSSERVER_VARP_SERVER_HEADROOM
 * (6400 + 3200, torirs_server.h) is refused rather than handed out to be
 * dropped by the server's bounds check.
 *
 * NOTHING HERE WRITES A FILE. The result of an allocation is a list of
 * commands — TEXT_SPLICEs appending the ledger / member-index line and the
 * membership lines, and a CFG_BLOCK add for the record — that the authority
 * applies as one stroke, so the allocation is undoable, every mirror sees it,
 * and Save writes it like any other edit. The TEXT documents they address:
 *
 *   "pack:<ns>.alloc"    pack/<ns>.alloc            the ledger
 *   "pack:<ns>.client"   pack/<ns>.client           membership, client half
 *   "pack:<ns>.server"   pack/<ns>.server           membership, server half
 *   "compack:<ns>"       configs/all.<ns>.compack   the member index
 *   "pack:<ns>.pack"     pack/<ns>.pack             member index, non-config
 *
 * Loading reads the tree (read-only); planning takes the CURRENT text of each
 * document it appends to, which is the open document's text when one is open
 * — so a second allocation before a save lands after the first.
 */

#include "editor_cmd.h"

#include <stddef.h>

/** The varp ceiling: TORIRSSERVER_VARP_CACHE_MAX (6400) +
 *  TORIRSSERVER_VARP_SERVER_HEADROOM (3200). editor_alloc_test checks it
 *  against torirs_server.h. */
#define EDITOR_ALLOC_VARP_CEILING 9600

/** ss_allocate.py's marker line, verbatim. */
#define EDITOR_ALLOC_MARKER                                                                        \
    "// --- allocated below this line by tools/ss_allocate.py; do not hand-edit ---"

enum Editor_AllocTarget
{
    /** The id line goes into the ledger, pack/<ns>.alloc: a server-owned id. */
    EDITOR_ALLOC_LEDGER = 0,
    /** The id line goes into the member index (configs/all.<ns>.compack, or
     *  pack/<ns>.pack): a new cache record, e.g. a varbit the client decodes. */
    EDITOR_ALLOC_MEMBER_INDEX,
};

enum Editor_AllocStatus
{
    EDITOR_ALLOC_OK = 0,
    /** The name is already bound; the result carries its id and spelling. */
    EDITOR_ALLOC_EXISTS,
    /** This namespace cannot be allocated into this target (cache-owned ids
     *  into the ledger, a composed or protocol-fixed namespace). */
    EDITOR_ALLOC_REFUSED,
    /** Past the namespace's ceiling (varps beyond the server headroom). */
    EDITOR_ALLOC_FULL,
    /** Not a name a `[block]` header can carry, or too long once prefixed. */
    EDITOR_ALLOC_BAD_NAME,
    /** A prefixed name states an id something else holds, or one below the
     *  floor. */
    EDITOR_ALLOC_ID_TAKEN,
    /** The output command array is too small. */
    EDITOR_ALLOC_NO_ROOM,
};

/** name -> id, open addressing; names owned. */
struct Editor_AllocNames
{
    char** names;
    /** Per slot: the full spelling a bare name stands for, or NULL. */
    char** spellings;
    int* ids;
    int capacity;
    int count;
};

/** A set of ids, open addressing. */
struct Editor_AllocIds
{
    int* slots;
    unsigned char* used;
    int capacity;
    int count;
};

struct Editor_AllocNs
{
    char ns[48];
    /** The floor: content.ini `base`, else the register's; 0 = none. */
    int floor;
    /** First id that may not be handed out; 0 = no ceiling. */
    int ceiling;
    /** content.ini `ids =` for this namespace is `server` (the default when
     *  unstated, as ss_allocate.py's id_authority). */
    int ids_server;
    /** Composed (component) or protocol-fixed (`ids = protocol`) ids: no
     *  counter can produce one. */
    int not_allocatable;
    /** "varp" / "varb" / "varc", or "" (ss_allocate.py's VAR_PREFIX). */
    char prefix[8];

    /** Highest id the non-ledger layers claim (members + numeric blocks),
     *  and the ledger's, -1 when none. */
    int highest_other;
    int highest_ledger;

    /** The ledger's bindings (a later line rebinds, as a dict does) and every
     *  other layer's (the first binding of a name wins). */
    struct Editor_AllocNames ledger;
    struct Editor_AllocNames other;
    /** For prefixed namespaces: the bare name of every prefixed binding ->
     *  its full spelling's id. */
    struct Editor_AllocNames bare;
    /** Every id a binding holds. */
    struct Editor_AllocIds held;

    /** The next id to try; -1 until the first allocation fixes it at
     *  max(mark, floor). Adopting a prefixed name does not move it. */
    int cursor;
};

/** Start an empty namespace state. `floor` 0 = none. */
void
Editor_AllocNsInit(
    struct Editor_AllocNs* state,
    const char* ns,
    int floor,
    int ids_server);

/** Frees and zeroes. NULL-tolerant. */
void
Editor_AllocNsFree(struct Editor_AllocNs* state);

/** Feed a member layer: `id=name` lines (a member index, a lane's ledger). */
void
Editor_AllocNsAddMembers(
    struct Editor_AllocNs* state,
    const char* text,
    size_t length);

/** Feed THE ledger, pack/<ns>.alloc (the marker line is skipped). */
void
Editor_AllocNsAddLedger(
    struct Editor_AllocNs* state,
    const char* text,
    size_t length);

/** Feed configs/all.<ns>: its numeric `[<ns>_N]` headers raise the mark. */
void
Editor_AllocNsAddConfig(
    struct Editor_AllocNs* state,
    const char* text,
    size_t length);

/**
 * Build the state for `ns` from the content tree at `content_dir`, the way
 * ss_allocate.py reads it: floor and ids axis from content.ini (register
 * defaults under it), the member index, each lane's member index and ledger,
 * the numeric blocks, the ledger. Read-only. Returns 0 only when the content
 * root cannot be read at all.
 */
int
Editor_AllocNsLoad(
    struct Editor_AllocNs* state,
    const char* content_dir,
    const char* ns);

struct Editor_AllocText;

/**
 * Editor_AllocNsLoad with the member index and the ledger taken from the
 * given texts instead of the files — the CURRENT text of an open document
 * (an allocation not yet saved, or one an undo took back). NULL reads the
 * file; a text whose `text` is NULL is an absent (empty) layer.
 */
int
Editor_AllocNsLoadWith(
    struct Editor_AllocNs* state,
    const char* content_dir,
    const char* ns,
    const struct Editor_AllocText* index_text,
    const struct Editor_AllocText* ledger_text);

/** 1 when any layer binds `name` (for a var, its prefixed spelling too):
 *  the id and the bound spelling, each when asked for. */
int
Editor_AllocNsLookup(
    const struct Editor_AllocNs* state,
    const char* name,
    int* out_id,
    char* out_spelling,
    size_t spelling_size);

/** max(highest claimed + 1, floor): ss_allocate.py's `base` before it skips
 *  held ids. */
int
Editor_AllocNsBase(const struct Editor_AllocNs* state);

/** The id the next fresh name would get. */
int
Editor_AllocNsNextId(struct Editor_AllocNs* state);

/**
 * Choose an id and spelling for `name` and record them in the state, so the
 * next call allocates past them. EDITOR_ALLOC_EXISTS fills the bound id and
 * spelling instead. Nothing is emitted.
 */
enum Editor_AllocStatus
Editor_AllocNsCoin(
    struct Editor_AllocNs* state,
    enum Editor_AllocTarget target,
    const char* name,
    int* out_id,
    char* out_name,
    size_t name_size);

/* ---- emitting --------------------------------------------------------------- */

/** The current text of a TEXT document; text NULL = absent (reads as empty). */
struct Editor_AllocText
{
    const char* text;
    size_t length;
};

/**
 * TEXT document keys for the files above. `which` is "alloc", "client",
 * "server" or "index" (the member index). Returns 0 when it does not fit.
 */
int
Editor_AllocDocKey(
    const char* ns,
    const char* which,
    char* out,
    size_t out_size);

/**
 * The bytes ss_allocate.py's write_pack appends to the ledger `current` for
 * one new `id=name`: a missing final newline, the header when the ledger is
 * empty, the marker when it has none, then the line. Returns the length, or
 * 0 when `out_size` is too small.
 */
size_t
Editor_AllocLedgerAppend(
    const char* ns,
    const struct Editor_AllocText* current,
    int id,
    const char* name,
    char* out,
    size_t out_size);

/**
 * Append `bytes` to the end of the TEXT document `doc_key` whose current
 * text is `current`, as TEXT_SPLICE commands of at most
 * EDITOR_CMD_SPLICE_MAX bytes each, verbatim. Returns the number written, or
 * -1 when `max` is too small.
 */
int
Editor_AllocEmitAppend(
    const char* doc_key,
    const struct Editor_AllocText* current,
    const char* bytes,
    size_t length,
    struct Editor_Cmd* out,
    int max);

struct Editor_AllocRequest
{
    const char* ns;
    /** The name as the user typed it (bare for a var; prefixed is adopted). */
    const char* name;
    enum Editor_AllocTarget target;
    /** Name the record in pack/<ns>.client / pack/<ns>.server too. A
     *  ledger-allocated record is server-side by its allocation alone; it
     *  needs a client line when the client must decode it. */
    int member_client;
    int member_server;
    /** Also emit CFG_BLOCK add for "<ns>:<coined name>". */
    int add_record;
    /** Current texts: the ledger or the member index (whichever `target`
     *  names), and the two membership files. */
    struct Editor_AllocText id_file;
    struct Editor_AllocText client_file;
    struct Editor_AllocText server_file;
};

struct Editor_AllocResult
{
    enum Editor_AllocStatus status;
    int id;
    char name[EDITOR_CMD_NAME_MAX];
    int command_count;
};

/**
 * Allocate and emit: Coin, then the id line (ledger or member index), the
 * membership lines not already present, and the record's CFG_BLOCK add, in
 * that order, into `out`. On anything but EDITOR_ALLOC_OK no command is
 * emitted and the state is unchanged (EXISTS fills the bound id and name).
 */
enum Editor_AllocStatus
Editor_AllocPlan(
    struct Editor_AllocNs* state,
    const struct Editor_AllocRequest* request,
    struct Editor_Cmd* out,
    int max,
    struct Editor_AllocResult* result);

/* ---- copying a record (design #save, "Save as new") ------------------------ */

struct Editor_AllocCopyRequest
{
    /** The namespace, which is also the CFG document's type ("loc"). */
    const char* ns;
    /** The record copied: its CFG document's current text (the whole
     *  `[name]` block, header included) and its name. */
    const char* source_name;
    const char* source_text;
    size_t source_length;
    /** The copy's name as typed. */
    const char* new_name;
    /** Where the id goes (EDITOR_ALLOC_LEDGER for a server-allocated id),
     *  and which membership lines the copy needs. */
    enum Editor_AllocTarget target;
    int member_client;
    int member_server;
    /** Current texts, as Editor_AllocRequest. */
    struct Editor_AllocText id_file;
    struct Editor_AllocText client_file;
    struct Editor_AllocText server_file;
};

/** Commands one copy of `source_length` bytes can need at most: the id line
 *  and the two membership lines (each split into splices), the CFG_BLOCK add,
 *  and one command per line of the source. */
int
Editor_AllocCopyCommandBound(const struct Editor_AllocCopyRequest* request);

/**
 * A config record copied to a new name with a fresh id, as one list of
 * commands for one undo step: everything Editor_AllocPlan emits for the new
 * name (the id line, the membership lines, the CFG_BLOCK add of
 * "<ns>:<new name>"), then every FIELD line of the source block, in order,
 * on the new record — a CFG_FIELD insert positioned at its line
 * ("key#occurrence@line"), or, for a value CFG_FIELD cannot carry (empty, or
 * past EDITOR_CMD_VALUE_MAX - 1 bytes) and for an OTHER line, a TEXT_SPLICE
 * of the whole line at the end of the record. Comments and blank lines are
 * not copied. The new record's CFG document must be empty when the commands
 * apply (a name nobody holds).
 *
 * EDITOR_ALLOC_REFUSED when the source text holds no `[source_name]` header;
 * every other status is Editor_AllocPlan's. Nothing is emitted unless OK.
 */
enum Editor_AllocStatus
Editor_AllocCopyPlan(
    struct Editor_AllocNs* state,
    const struct Editor_AllocCopyRequest* request,
    struct Editor_Cmd* out,
    int max,
    struct Editor_AllocResult* result);

/* ---- the server half of a copy ------------------------------------------------
 *
 * A record is not one block (editor_cfg.h, "a record across its layers"): an
 * npc's hitpoints, its attack / defend / death anims, attackrate, damagetype,
 * its wander and leash, huntmode -- everything the SERVER reads -- are
 * stated in server overlays (server/scripts/.../configs/<file>.<type>: the
 * generated layers and the authored one), not in configs/all.<type>. A copy
 * that carried only the cache block made a monster that looked like its
 * source and fought like nothing (engine defaults; 2026-11-09). So the copy
 * also gets ONE authored overlay of its own -- the record editor's file,
 * server/scripts/edited/<name>/configs/<name>.<type> -- holding what the
 * source's overlays resolve to, in the same undo step.
 */

/** One server overlay of the source: its path under server/scripts and the
 *  text of the source's block in it (its CFG document; empty when the file
 *  has no block for the record). */
struct Editor_AllocOverlayLayer
{
    const char* overlay;
    const char* text;
    size_t length;
};

/** "edited/<new_name>/configs/<new_name>.<ns>": where the copy's server
 *  fields go, under server/scripts. 0 when it does not fit. */
int
Editor_AllocCopyOverlayPath(
    const char* ns,
    const char* new_name,
    char* out,
    size_t out_size);

/** Commands Editor_AllocCopyOverlayPlan can need at most for these layers. */
int
Editor_AllocCopyOverlayBound(
    const struct Editor_AllocOverlayLayer* layers,
    int count);

/**
 * The source record's server fields as commands on the copy's own overlay
 * document ("<ns>:<new_name>@edited/<new_name>/configs/<new_name>.<ns>"):
 * a CFG_BLOCK add, then one line per field the layers RESOLVE to -- merged
 * as the pack merges them (Editor_CfgRecordResolve: rank, then path; a param
 * by its name), so a field two layers state is written once, with the value
 * the server reads. The document must be empty when they apply.
 *
 * Returns the commands written; 0 when the layers state nothing (`count` 0,
 * or no block in any of them); -1 when `max` is under
 * Editor_AllocCopyOverlayBound, a layer cannot be added (more than the
 * record editor's layers) or the overlay's document key does not fit (a
 * name over 34 bytes).
 */
int
Editor_AllocCopyOverlayPlan(
    const char* ns,
    const char* source_name,
    const char* new_name,
    const struct Editor_AllocOverlayLayer* layers,
    int count,
    struct Editor_Cmd* out,
    int max);

#endif
