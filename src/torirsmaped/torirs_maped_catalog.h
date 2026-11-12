#ifndef SRC_TORIRSMAPED_TORIRS_MAPED_CATALOG_H
#define SRC_TORIRSMAPED_TORIRS_MAPED_CATALOG_H

/*
 * The asset catalog and the square atlas (design #catalog, #squares).
 *
 * The editor server indexes the content tree where the names already are as
 * text, instead of sweeping cache records through the client's task system:
 *
 *   pack/<table>.pack                    id=name tables (models, sprites, synths,
 *                                  songs, jingles, interfaces, clientscripts,
 *                                  maps, categories, stats, ...)
 *   pack/<ns>.alloc                ids this tree allocated, by namespace
 *   pack/<ns>.client|.server       membership: which entities have each half
 *   configs/all.<type>.compack     the cache member index: id=name
 *   configs/all.<type>             the cache `[name]` blocks
 *   server/scripts/.../configs/<file>.<type>   server overlay `[name]` blocks
 *   npc_stats/, npc_combat/        per-npc server stats and combat anims
 *   interfaces/<name>.compack, .if    components and root types
 *   scripts/<name>.cs2                  clientscript / proc headers (+ .cs2b flag)
 *   server/scripts/.../<file>.rs2       trigger headers `[trigger,subject]`
 *   server/scripts/.../<file>.spawn     spawn rows, counted per square
 *   maps/, ported/<lane>/maps/     squares: `.jm2` level-0 colours, `.jl2` locs
 *   worldmap/areas/<x>.wma, .wmc   which world-map area claims each square
 *
 * Every source file is parsed on its own and remembered with its mtime and
 * size; a refresh re-stats the tree and re-parses only what changed, then
 * re-merges (the merge is cheap next to reading 190 MB of config text). The
 * warm build merges each source as it is parsed instead (Service).
 *
 * Wire (protocol v2, MAPED_IMPLEMENTATION.md section 3; strings are
 * `u32 length, bytes`, ints little-endian):
 *
 *   CATALOG_QUERY   u32 kind (TORIRSMAPED_CATALOG_ANY = every kind),
 *                   string filter, u32 flags, u32 page, u32 page_size
 *                   (0 = TORIRSMAPED_CATALOG_PAGE_DEFAULT)
 *       -> FACT_CATALOG_PAGE  u32 status, u32 total, u32 page, u32 page_size,
 *                             u32 row_count, row_count * (u32 kind, i32 id,
 *                             string name, string display, u32 provenance,
 *                             u32 field_count, field_count * (string key,
 *                             string value))
 *
 *     The filter is whitespace-separated terms. A term holding '=' is a
 *     `key=value` filter (`op=Open`, `multivar=varb4153`, `table=1_skeletons`,
 *     `name="Tool Leprechaun"`): the key matches a summary field exactly or
 *     by family (`op` matches op1..op5). The value narrows as a prefix, and
 *     is tested case-insensitively in this order, which is also the order
 *     the hits rank in (a record whose filters all matched whole comes
 *     before one that matched a filter only as a prefix, after the terms'
 *     own rank):
 *
 *       1. whole: `op=Open` names "Open";
 *       2. a prefix of a whole word of the value (words split at spaces
 *          and commas): `op=Op` names "Open" and "Open door", `shapes=9`
 *          names "0 2 9", `multivar=varb4153` names "varb4153_x" (the old
 *          rule, a prefix ending at '_', is this rule's first word) -- but
 *          a value that is a number never stops inside a longer number:
 *          `shapes=2` is not "22", `size=1` names "1x1" and not "10x2";
 *       3. `value*` written out: a prefix of the whole value.
 *
 *     Typing on narrows the held hits ("op=Ope" after "op=Op"), except
 *     where a number grows ("shapes=22" after "shapes=2").
 *
 *     Two keys resolve server-side: `id=<n>` (the record's id) and, for
 *     locs, `shape=<class or number>` against the loc's placeable shapes
 *     (the `shapes` field below): the classes are `wall` (0, 1, 2, 3, 9),
 *     `decor` (4..8), `centre` (10, 11), `roof` (12..21) and `ground` (22);
 *     a prefix names every class it starts (`shape=c` is centre) and a
 *     number names that shape (`shape=10`).
 *
 *     Any other term must match the record's name, display name or decimal
 *     id — as a substring, or whole with TORIRSMAPED_CATALOG_EXACT, where
 *     the remaining text is one term and the lookup is a hash probe rather
 *     than a scan.
 *
 *     Kind TORIRSMAPED_CATALOG_ANY is a SCOPED query: the kinds are tested
 *     in a fixed order (loc, npc, obj, then the rest by number), each
 *     stopping at `page_size` hits, and no further kind is tested once the
 *     page asked for is full -- a keystroke costs what its page costs, not
 *     a test of every record. A numeric term is first looked up by id in
 *     every kind, so the record it names is on the first page. `total` is
 *     then what was found; TORIRSMAPED_CATALOG_PAGE_SCOPED says a cap or the
 *     window cut more. (ToriRSMapEdCatalog_Query in-process answers every
 *     match.) EXACT queries are hash probes and are never scoped.
 *
 *     A loc row carries three derived fields (field names stable):
 *       `shapes`  the shapes it can be placed as, in the order the Locs
 *                 panel offers them (centrepiece, ground decor, walls,
 *                 decor, roofs), space-separated ("10 11", "0 2 9"): a
 *                 `shape<n>=<shape>,<model>...` row with a model gives that
 *                 shape, wall decor 5..8 draw shape 4's models and 11 draws
 *                 10's, and a loc with `models=` and no shape rows is a
 *                 centrepiece (10, 11). A loc with no model rows has none.
 *       `size`    "<width>x<length>", 1 where the config states none.
 *       `layer`   the loc layer of each of its shapes, once each, in that
 *                 order: `wall` (0-3), `decor` (4-8), `main` (9-21: the
 *                 diagonal wall, centrepieces, roofs; Client-TS GROUND),
 *                 `ground` (22, ground decor) -- "wall main" for a fence
 *                 with a diagonal. Absent with `shapes`.
 *
 *     With TORIRSMAPED_CATALOG_PROGRESS in `flags` (a client that saw
 *     TORIRSMAPED_CAP_CATALOG_PROGRESS) FACT_CATALOG_PAGE ends with
 *     `u32 page_flags (TORIRSMAPED_CATALOG_PAGE_*), u32 done, u32 total`
 *     after the rows, and the query never waits for the warm build: while
 *     it runs the answer comes from the partial index (locs are parsed
 *     first) with TORIRSMAPED_CATALOG_PAGE_PARTIAL and the build's progress
 *     as done/total (KiB of source text; total 0 while the tree is still
 *     being scanned; 1/1 once built). Without the flag the payload ends at
 *     the last row, as a v2 reader expects, and a query during the build
 *     finishes it first.
 *
 *   CATALOG_RECORD  u32 kind, i32 id, string name (non-empty name wins;
 *                   otherwise the record with that id)
 *       -> FACT_CATALOG_RECORD u32 status, u32 kind, i32 id, string name,
 *                              string display, u32 provenance,
 *                              u32 source_count, source_count * (string path,
 *                              u32 provenance bit, u32 line),
 *                              u32 field_count, field_count * (string key,
 *                              string value, u32 source, string
 *                              overridden_value, u32 overridden_source)
 *
 *     `source` indexes the source list; TORIRSMAPED_CATALOG_NO_SOURCE when a
 *     field has none (derived ones). A server overlay that restates a cache
 *     field carries the cache's value in `overridden_value`. Config blocks
 *     come back whole (every line, `default` included), read from disk at
 *     the indexed offset.
 *
 *   CATALOG_SQUARES (empty, or u32 flags: TORIRSMAPED_CATALOG_SQUARES_LOCS,
 *                    only to a server with TORIRSMAPED_CAP_CATALOG_SQUARE_LOCS)
 *       -> FACT_CATALOG_SQUARES u32 status,
 *                               u32 storage_count, storage_count * string
 *                               ("maps", "ported/<lane>/maps" — every lane
 *                               directory, with or without squares yet),
 *                               u32 area_count, area_count * (string internal,
 *                               string external, u32 is_main),
 *                               u32 row_count, row_count * (i32 map_x,
 *                               i32 map_z, u32 storage, u32 flags
 *                               (TORIRSMAPED_SQUARE_*), u32 spawn_count,
 *                               u32 rgb (0xRRGGBB), u32 primary_area
 *                               (TORIRSMAPED_CATALOG_NO_AREA = unmapped),
 *                               u32 area_n, area_n * u32 area,
 *                               [u32 loc_count: the locs its .jl2 places,
 *                               present exactly when `flags` holds
 *                               TORIRSMAPED_SQUARE_LOC_COUNT])
 *
 *     A connection that asked with TORIRSMAPED_CATALOG_SQUARES_LOCS gets
 *     every row flagged TORIRSMAPED_SQUARE_LOC_COUNT, with the count; any
 *     other gets the v2 rows. The flag rides in the row so a reader never
 *     has to know which form a broadcast that crossed its request is in.
 *
 *   FACT_CATALOG_SQUARES_CHANGED, same payload (in the form that connection
 *   asked for; the v2 rows until it asks), is BROADCAST to every v2
 *   connection when the atlas changes under it: a DOC_CREATE (the new
 *   square appears with TORIRSMAPED_SQUARE_CREATED before anything is on
 *   disk) or a save that wrote squares. A mirror replaces its atlas.
 *
 *   CATALOG_USES    u32 kind, string name, u32 cursor (0: start)
 *                   (workbench 6.6, the Record panel's Where used tab; only
 *                   to a server with TORIRSMAPED_CAP_CATALOG_USES)
 *       -> FACT_CATALOG_USES u32 status, u32 kind, string name,
 *                            u32 next_cursor (0: the search is done),
 *                            u32 files_done, u32 files_total,
 *                            u32 row_count, row_count * (u32 use
 *                            (TORIRSMAPED_USE_*), string path (content-
 *                            relative), u32 line (1-based, 0 none),
 *                            u32 count, string text)
 *
 *     Where the record is NAMED, read from the files the index lists -- a
 *     search, not an index: the tree's words are not kept. One request
 *     reads sources from `cursor` on for about
 *     TORIRSMAPED_CATALOG_USES_SLICE_MS (at least one source) and at most
 *     TORIRSMAPED_CATALOG_USES_ROWS rows, then answers with the cursor to
 *     ask from next; the client asks again on a later frame until it is 0,
 *     so no request holds the window for longer than a slice. What counts:
 *
 *       SPAWN      a .spawn row of the record (npc, obj), one row per square
 *                  with its count
 *       PLACEMENT  a loc's placements in a square's .jl2 (by id), one row per
 *                  square with its count: the atlas's loc counts, per loc
 *       TRIGGER    an .rs2 header `[<trigger>,<name>]`
 *       SCRIPT     any other .rs2 line naming it as a word
 *       INTERFACE  an .if line naming it
 *       CS2        a .cs2 line naming it
 *       CONFIG     a server overlay field naming it (another record's
 *                  `next_loc_stage=<name>`, a shop's stock)
 *       PALETTE    an editor/palettes.txt slot `<kind>:<name>`
 *       STAMP      an editor/stamps/<s>.stamp loc row of its id
 *
 *     A file says at most TORIRSMAPED_CATALOG_USES_PER_FILE lines; the row
 *     after them counts the rest (USE_MORE). configs/all.<type> (the cache
 *     half, 86 MB of locs) is not read. ABSENT when the index has no such
 *     record (a palette or a stamp may still name it: they are read too).
 */

#include <stddef.h>
#include <stdint.h>

enum ToriRSMapEdCatalogFrame
{
    /* Requests 41..48 and facts 161..168 are this owner's
     * (MAPED_IMPLEMENTATION.md section 3). */
    TORIRSMAPED_REQ_CATALOG_QUERY = 41,
    TORIRSMAPED_REQ_CATALOG_RECORD = 42,
    TORIRSMAPED_REQ_CATALOG_SQUARES = 43,

    TORIRSMAPED_FACT_CATALOG_PAGE = 161,
    TORIRSMAPED_FACT_CATALOG_RECORD = 162,
    TORIRSMAPED_FACT_CATALOG_SQUARES = 163,
    /** Broadcast form of FACT_CATALOG_SQUARES: the atlas changed. */
    TORIRSMAPED_FACT_CATALOG_SQUARES_CHANGED = 164,
    /** Where a record is used (workbench 6.6; TORIRSMAPED_CAP_CATALOG_USES). */
    TORIRSMAPED_REQ_CATALOG_USES = 44,
    TORIRSMAPED_FACT_CATALOG_USES = 165,
};

/** A FACT_CATALOG_USES row's `use`. */
enum ToriRSMapEdCatalogUse
{
    TORIRSMAPED_USE_SPAWN = 1,
    TORIRSMAPED_USE_PLACEMENT,
    TORIRSMAPED_USE_TRIGGER,
    TORIRSMAPED_USE_SCRIPT,
    TORIRSMAPED_USE_INTERFACE,
    TORIRSMAPED_USE_CS2,
    TORIRSMAPED_USE_CONFIG,
    TORIRSMAPED_USE_PALETTE,
    TORIRSMAPED_USE_STAMP,
    /** The row after a file's TORIRSMAPED_CATALOG_USES_PER_FILE lines: its
     *  count is the lines not listed. */
    TORIRSMAPED_USE_MORE,
};

#define TORIRSMAPED_CATALOG_USES_SLICE_MS 4.0
#define TORIRSMAPED_CATALOG_USES_ROWS 256
#define TORIRSMAPED_CATALOG_USES_PER_FILE 8

/** "spawn", "placement", "trigger", "script", "interface", "cs2", "config",
 *  "palette", "stamp", "more"; "?" past the end. Inline for the client. */
static inline const char*
ToriRSMapEdCatalog_UseName(uint32_t use)
{
    static const char* const names[] = { "?", "spawn", "placement", "trigger", "script", "interface", "cs2",
        "config", "palette", "stamp", "more" };
    if( use > TORIRSMAPED_USE_MORE )
        return "?";
    return names[use];
}

/** What a record is. The wire carries the number; the names below are the
 *  panel's tab labels and the `kind` a query result reports. */
enum ToriRSMapEdCatalogKind
{
    /* config types: configs/all.<name> + server overlays *.<name> */
    TORIRSMAPED_CATALOG_LOC = 0,
    TORIRSMAPED_CATALOG_NPC,
    TORIRSMAPED_CATALOG_OBJ,
    TORIRSMAPED_CATALOG_SEQ,
    TORIRSMAPED_CATALOG_SPOTANIM,
    TORIRSMAPED_CATALOG_VARP,
    TORIRSMAPED_CATALOG_VARBIT,
    TORIRSMAPED_CATALOG_VARC,
    TORIRSMAPED_CATALOG_VARN,
    TORIRSMAPED_CATALOG_VARS,
    TORIRSMAPED_CATALOG_STRUCT,
    TORIRSMAPED_CATALOG_ENUM,
    TORIRSMAPED_CATALOG_PARAM,
    TORIRSMAPED_CATALOG_INV,
    TORIRSMAPED_CATALOG_DBROW,
    TORIRSMAPED_CATALOG_DBTABLE,
    TORIRSMAPED_CATALOG_IDK,
    TORIRSMAPED_CATALOG_UNDERLAY,
    TORIRSMAPED_CATALOG_OVERLAY,
    TORIRSMAPED_CATALOG_HITSPLAT,
    TORIRSMAPED_CATALOG_HEALTHBAR,
    TORIRSMAPED_CATALOG_MAPELEMENT,
    /* pack tables */
    TORIRSMAPED_CATALOG_MODEL,
    TORIRSMAPED_CATALOG_SPRITE,
    TORIRSMAPED_CATALOG_TEXTURE,
    TORIRSMAPED_CATALOG_SYNTH,
    TORIRSMAPED_CATALOG_SONG,
    TORIRSMAPED_CATALOG_JINGLE,
    TORIRSMAPED_CATALOG_FRAMESET,
    TORIRSMAPED_CATALOG_SKELETON,
    TORIRSMAPED_CATALOG_FONT,
    TORIRSMAPED_CATALOG_CATEGORY,
    TORIRSMAPED_CATALOG_STAT,
    /** Any other pack/<table>.pack; its rows carry `table=<table>`. */
    TORIRSMAPED_CATALOG_PACK,
    /* the rest of the tree */
    TORIRSMAPED_CATALOG_INTERFACE,
    /** "<interface>:<component>". */
    TORIRSMAPED_CATALOG_COMPONENT,
    TORIRSMAPED_CATALOG_CS2,
    /** "<trigger>,<subject>" from an .rs2 header; one record per header. */
    TORIRSMAPED_CATALOG_TRIGGER,
    /** "m<x>_<z>", or "ported/<lane>/maps/m<x>_<z>" for a lane's square. */
    TORIRSMAPED_CATALOG_SQUARE,
    TORIRSMAPED_CATALOG_KIND_COUNT,
};

enum
{
    /** CATALOG_QUERY's kind for "search every kind". */
    TORIRSMAPED_CATALOG_ANY = 0xFFFFFFFFu,
    TORIRSMAPED_CATALOG_NO_SOURCE = 0xFFFFFFFFu,
    TORIRSMAPED_CATALOG_NO_AREA = 0xFFFFFFFFu,
    TORIRSMAPED_CATALOG_PAGE_DEFAULT = 100,
    TORIRSMAPED_CATALOG_PAGE_MAX = 1000,
};

/** CATALOG_QUERY flags. */
enum ToriRSMapEdCatalogQueryFlag
{
    /** The text (filters aside) is one whole name, display name or id. */
    TORIRSMAPED_CATALOG_EXACT = 1,
    /** Answer from the partial index while the warm build runs, and end
     *  FACT_CATALOG_PAGE with the page flags and the build's progress. Only
     *  to a server with TORIRSMAPED_CAP_CATALOG_PROGRESS. */
    TORIRSMAPED_CATALOG_PROGRESS = 2,
};

/** FACT_CATALOG_PAGE's `page_flags` (the PROGRESS tail). */
enum ToriRSMapEdCatalogPageFlag
{
    /** The warm build is still running: the answer is what is merged so far. */
    TORIRSMAPED_CATALOG_PAGE_PARTIAL = 1,
    /** A scoped "all" left records untested: a kind reached its cap or a
     *  later kind was not tested; `total` counts only what was found. */
    TORIRSMAPED_CATALOG_PAGE_SCOPED = 2,
};

/** Where a record (or one of its fields) came from. A record's provenance
 *  is the union over its sources. */
enum ToriRSMapEdCatalogProvenance
{
    /** A `[name]` block in configs/all.<type>. */
    TORIRSMAPED_PROV_CACHE = 1,
    /** configs/all.<type>.compack or interfaces/<name>.compack. */
    TORIRSMAPED_PROV_COMPACK = 2,
    /** A server overlay block under server/scripts/.../configs/. */
    TORIRSMAPED_PROV_OVERLAY = 4,
    /** pack/<ns>.alloc. */
    TORIRSMAPED_PROV_ALLOC = 8,
    /** A pack/<table>.pack row. */
    TORIRSMAPED_PROV_PACK = 16,
    /** Named in pack/<ns>.client. */
    TORIRSMAPED_PROV_CLIENT = 32,
    /** Named in pack/<ns>.server. */
    TORIRSMAPED_PROV_SERVER = 64,
    /** npc_stats/ or npc_combat/. */
    TORIRSMAPED_PROV_STATS = 128,
    /** A .cs2 header, an .rs2 trigger header, or an .if file. */
    TORIRSMAPED_PROV_SCRIPT = 256,
    /** maps/ or a lane's maps/. */
    TORIRSMAPED_PROV_MAP = 512,
};

/** FACT_CATALOG_SQUARES row flags. */
enum ToriRSMapEdSquareFlag
{
    TORIRSMAPED_SQUARE_HAS_JM2 = 1,
    TORIRSMAPED_SQUARE_HAS_JL2 = 2,
    /** The `.jl2` places at least one loc. */
    TORIRSMAPED_SQUARE_HAS_LOCS = 4,
    /** Open in the authority (any connection opened it). */
    TORIRSMAPED_SQUARE_OPEN = 8,
    /** Open with unsaved edits. */
    TORIRSMAPED_SQUARE_DIRTY = 16,
    /** Made by DOC_CREATE and not yet saved: nothing of it is on disk. */
    TORIRSMAPED_SQUARE_CREATED = 32,
    /** No world-map area claims it and it lies in instance space
     *  (TORIRSMAPED_INSTANCE_*): what the instance helper makes. */
    TORIRSMAPED_SQUARE_INSTANCE = 64,
    /** The row ends with `u32 loc_count` (the connection asked with
     *  TORIRSMAPED_CATALOG_SQUARES_LOCS). Wire only: says how to read the
     *  row, not anything about the square. */
    TORIRSMAPED_SQUARE_LOC_COUNT = 128,
};

/** CATALOG_SQUARES request flags (the optional `u32 flags` body). */
enum ToriRSMapEdCatalogSquaresFlag
{
    /** End every row with its loc count (TORIRSMAPED_SQUARE_LOC_COUNT).
     *  Only to a server with TORIRSMAPED_CAP_CATALOG_SQUARE_LOCS. */
    TORIRSMAPED_CATALOG_SQUARES_LOCS = 1,
    /** Answer EDITOR_HOST_BUSY (the status and four empty lists) instead of
     *  waiting for the warm build on its worker thread: for a caller that
     *  asks again every tick (the Square picker's atlas). Only to a server
     *  with TORIRSMAPED_CAP_CATALOG_SQUARES_NOWAIT. */
    TORIRSMAPED_CATALOG_SQUARES_NOWAIT = 2,
};

/** Instance space: the unmapped high-coordinate squares the instance helper
 *  fills (the workbench design's section 13, "unmapped space 0..19 x
 *  100..255"), map_x in [X0, X1] and map_z in [Z0, Z1]. The picker draws it
 *  hatched where no square exists. */
#define TORIRSMAPED_INSTANCE_X0 0
#define TORIRSMAPED_INSTANCE_X1 19
#define TORIRSMAPED_INSTANCE_Z0 100
#define TORIRSMAPED_INSTANCE_Z1 255

/** The instance pool: where the server stamps a runtime copy of a template
 *  (torirsserver/torirs_server_mapinstance.c, MAPINSTANCE_SCAN_X0/Z0 and
 *  MAPINSTANCE_SQUARES). Nobody builds here; a square here is a copy of one
 *  of the templates above, made when a player enters and gone when they
 *  leave. */
#define TORIRSMAPED_POOL_X0 100
#define TORIRSMAPED_POOL_X1 255
#define TORIRSMAPED_POOL_Z0 1
#define TORIRSMAPED_POOL_Z1 255

/** Underground: the surface's dungeons and caves sit 6400 tiles (100
 *  squares) north of the ground above them, so map_z >= 100 west of the pool
 *  and east of the templates. */
#define TORIRSMAPED_UNDERGROUND_X0 (TORIRSMAPED_INSTANCE_X1 + 1)
#define TORIRSMAPED_UNDERGROUND_X1 (TORIRSMAPED_POOL_X0 - 1)
#define TORIRSMAPED_UNDERGROUND_Z0 100
#define TORIRSMAPED_UNDERGROUND_Z1 255

/** Whether square (map_x, map_z) is in instance space. Inline so the picker
 *  links no catalog code. */
static inline int
ToriRSMapEdCatalog_InInstanceSpace(
    int map_x,
    int map_z)
{
    return map_x >= TORIRSMAPED_INSTANCE_X0 && map_x <= TORIRSMAPED_INSTANCE_X1
           && map_z >= TORIRSMAPED_INSTANCE_Z0 && map_z <= TORIRSMAPED_INSTANCE_Z1;
}

/** The tab label of a kind ("loc", "npc", ...); "?" past the end. Inline so
 *  a client that links no catalog code can still name the tabs. */
static inline const char*
ToriRSMapEdCatalog_KindName(uint32_t kind)
{
    static const char* const names[TORIRSMAPED_CATALOG_KIND_COUNT] = {
        "loc",      "npc",      "obj",       "seq",       "spotanim",  "varp",
        "varbit",   "varc",     "varn",      "vars",      "struct",    "enum",
        "param",    "inv",      "dbrow",     "dbtable",   "idk",       "underlay",
        "overlay",  "hitsplat", "healthbar", "mapelement", "model",    "sprite",
        "texture",  "synth",    "song",      "jingle",    "frameset",  "skeleton",
        "font",     "category", "stat",      "pack",      "interface", "component",
        "cs2",      "trigger",  "square",
    };
    if( kind >= TORIRSMAPED_CATALOG_KIND_COUNT )
        return "?";
    return names[kind];
}

/* ------------------------------------------------------------------ */
/* The server-side index                                               */
/* ------------------------------------------------------------------ */

struct ToriRSMapEdBuf;
struct Editor_Doc;
struct ToriRSMapEdCatalog;

/**
 * A catalog over `content_dir`. Builds nothing yet: the service pass builds
 * it in slices (ToriRSMapEdCatalog_Service), and the first query (or
 * ToriRSMapEdCatalog_Refresh) finishes whatever is left, so a process that
 * neither services nor queries it never reads the tree. Never writes
 * anything.
 */
struct ToriRSMapEdCatalog*
ToriRSMapEdCatalog_Create(const char* content_dir);

/** NULL-tolerant. */
void
ToriRSMapEdCatalog_Free(struct ToriRSMapEdCatalog* catalog);

/** What one refresh did, for logs and the test. */
struct ToriRSMapEdCatalogStats
{
    int sources;
    int parsed;
    int removed;
    int records;
    int squares;
    double scan_ms;
    double parse_ms;
    double merge_ms;
};

/**
 * Re-stat every source and re-parse what changed (on the first call: build
 * everything), all at once. `force` 0 does nothing when the last refresh was
 * under `refresh_interval_s` ago. Returns 1 when the merged index changed.
 *
 * Requests do NOT call this (docs/MAPED_STATUS.md "Catalog performance"):
 * stat'ing the ~100,000 files of the tree took 0.2-1.8 s, and doing it on the
 * request after every two-second pause made a keystroke or a row click stall
 * the editor's frame for that long. A request answers from the index it has
 * (building it only when there is none) and, once `refresh_interval_s` has
 * passed since the last pass, starts a freshness SWEEP that
 * ToriRSMapEdCatalog_Service advances a slice at a time; what the sweep finds
 * changed is parsed and merged when it ends. A record view re-checks its own
 * files on every request and re-reads them when they changed.
 */
int
ToriRSMapEdCatalog_Refresh(
    struct ToriRSMapEdCatalog* catalog,
    int force,
    struct ToriRSMapEdCatalogStats* out_stats);

/** Only maps/ and the lanes' maps/ are re-stated (~11,900 files, ~0.2 s on
 *  Windows), and the next query sweeps the whole tree. For a change whose
 *  files nobody can name: a lane's Discard. A save names its files and calls
 *  ToriRSMapEdCatalog_RefreshPaths instead. */
int
ToriRSMapEdCatalog_RefreshMaps(struct ToriRSMapEdCatalog* catalog);

/**
 * What a save calls with the files it wrote or removed: `rels`, `count` of
 * them, each relative to the content root ("maps/m50_50.jl2",
 * "server/scripts/areas/edited/configs/m50_50.spawn"). Each is classified as
 * the full scan would (a path no scan records -- a .filepack, editor/... --
 * is skipped), stat'ed once, and re-parsed even when its mtime and size say
 * nothing changed (Windows mtimes are whole seconds); one that is gone drops
 * its source. Then, if anything changed, one full merge. Nothing else is
 * stat'ed, unless a square lands in a lane directory the storage list lacks
 * (then ported/ is listed again).
 *
 * Leaves the refresh interval and a pending sweep's reason alone, and a sweep
 * in flight runs on (it would see these files unchanged) -- except when the
 * lane list moved, which ends it. maps_revision, revision and the storage
 * list move exactly as RefreshMaps would move them for the same change.
 * Before the index is built it does nothing (the build reads the tree as it
 * stands). `count` 0 does nothing; otherwise `rels` and every entry must be
 * non-NULL. Returns 1 when the merged index changed.
 */
int
ToriRSMapEdCatalog_RefreshPaths(
    struct ToriRSMapEdCatalog* catalog,
    const char* const* rels,
    int count);

/**
 * A record the AUTHORITY holds and no file has yet: what SAVE_AS ("Copy to
 * new") made, from the moment it is made until a save writes it. The index
 * reads the tree, so without this the copy was the pick but in no list and
 * under no name until Save (2026-11-09): Find did not find it, Place and
 * "Use catalog pick" refused it, the Record panel could not look up its
 * layers.
 *
 * PendingAdd notes that record `name` of `kind` was given `id` (SAVE_AS calls
 * it). PendingSync, before a catalog request is answered, makes the index
 * agree with the authority's CFG document "<type>:<name>" for every noted
 * record: listed (its id as pack/<type>.alloc will bind it, its block as
 * configs/all.<type> will hold it, parsed from the document's text) while the
 * document is unsaved; parsed again when its text moved; taken out when the
 * block is gone (the copy was undone -- a redo lists it again) and for good
 * once it is saved (the file's own source carries it; the save names the file
 * to RefreshPaths). Returns 1 when the merged index changed. Does nothing
 * while the index is still being built: the next request syncs.
 */
void
ToriRSMapEdCatalog_PendingAdd(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    int32_t id,
    const char* name);

int
ToriRSMapEdCatalog_PendingSync(
    struct ToriRSMapEdCatalog* catalog,
    struct Editor_Doc* doc);

/** Bytes of one overlay path below (the CFG document key's limit). */
#define TORIRSMAPED_CATALOG_OVERLAY_MAX 128

/**
 * The server overlay files that state record `name` of `kind`, as paths under
 * server/scripts ("areas/lumbridge/configs/lumbridge.npc",
 * "npc/configs/combat_stats.generated.npc"): what SAVE_AS reads the source's
 * server fields from (editor_alloc.h Editor_AllocCopyOverlayPlan). Each once,
 * in no particular order; a path that does not fit is left out. Returns how
 * many (at most `max`), 0 when the index has no such record, -1 while the
 * index is still being built.
 */
int
ToriRSMapEdCatalog_RecordOverlays(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* name,
    char (*out)[TORIRSMAPED_CATALOG_OVERLAY_MAX],
    int max);

/** 1 once the index has been built: until then nobody holds an atlas, and a
 *  change needs no broadcast. */
int
ToriRSMapEdCatalog_Built(const struct ToriRSMapEdCatalog* catalog);

/** Milliseconds one service slice may spend on a freshness sweep, and on
 *  the warm build where it runs in slices (`TORIRS_CATALOG_WARM_THREAD=0`,
 *  the web and win32 lanes): the build is ~6 s of work on the owner's tree,
 *  at 6 ms a frame done ~20 s after the editor opens; a Catalog opened
 *  sooner answers from the partial index when it asks with
 *  TORIRSMAPED_CATALOG_PROGRESS. Elsewhere the warm build runs on a worker
 *  thread (the service pass starts it and takes the index back when it is
 *  done); a request meanwhile waits for it, except a PROGRESS query (an
 *  empty partial page and the build's progress) and a CATALOG_SQUARES with
 *  TORIRSMAPED_CATALOG_SQUARES_NOWAIT (answered EDITOR_HOST_BUSY). */
#define TORIRSMAPED_CATALOG_SLICE_MS 2.0
#define TORIRSMAPED_CATALOG_WARM_SLICE_MS 6.0
/** Bytes of one large config or overlay file (configs/all.loc is 86 MB) a
 *  warm slice reads and parses at most; the parse also stops at the slice's
 *  deadline, at a block boundary. */
#define TORIRSMAPED_CATALOG_SLICE_BYTES (4u * 1024u * 1024u)

/**
 * The index's background job, from the server's service pass (every pump):
 * builds the index in slices from the first call -- a scan; then the
 * sources in a fixed order (locs, npcs, objs, the rest), a large blocks
 * file in parts of at most TORIRSMAPED_CATALOG_SLICE_BYTES, each source
 * merged into the index as soon as it is parsed (incrementally: the warm
 * build runs no full merge); then the derived fields a slice of records at
 * a time, the areas and the atlas -- and afterwards advances a freshness
 * sweep a request started. Spends about `budget_ms` per call; a sweep's
 * closing parse-and-merge is one step (see Refresh). Returns 1 while work
 * remains (the daemon then polls quickly).
 */
int
ToriRSMapEdCatalog_Service(
    struct ToriRSMapEdCatalog* catalog,
    double budget_ms);

/** Changes when a merge of a built index took in a square file that changed,
 *  came or went (or a lane): the service pass then broadcasts the atlas, as
 *  a save does (torirs_maped_live.c ToriRSMapEd_Service). */
uint32_t
ToriRSMapEdCatalog_MapsRevision(const struct ToriRSMapEdCatalog* catalog);

/** 1 while a build or a sweep is in progress. */
int
ToriRSMapEdCatalog_Working(const struct ToriRSMapEdCatalog* catalog);

/** 1 while the warm build is running on its worker thread: a request now
 *  would wait for the rest of it (seconds), so the server answers
 *  EDITOR_HOST_BUSY instead and the panel asks again. 0 once the thread is
 *  done (the next service pass or request takes the index) and on the lanes
 *  that build in slices. */
int
ToriRSMapEdCatalog_Building(const struct ToriRSMapEdCatalog* catalog);

/** What the index has done since it was created: the work bounds the
 *  perf test (editor_catalog_perf_test.c) checks. */
struct ToriRSMapEdCatalogCounters
{
    /** Files and directories stat'ed, by any scan or freshness check. */
    long stats;
    /** Freshness sweeps (and the warm build) finished, and the service
     *  slices that advanced them; the longest slice. */
    long sweeps;
    long slices;
    double slice_max_ms;
    /** Whole merges run: a full re-merge (a sweep that saw a change, a whole
     *  refresh) or a warm build's end. Each bumps the index revision. */
    long merges;
    /** Sources the warm build merged into the live index one at a time. */
    long incremental;
    /** Parts of large blocks files the warm build parsed, and the most
     *  bytes one part covered (at most TORIRSMAPED_CATALOG_SLICE_BYTES
     *  unless one block is longer). */
    long parts;
    long part_bytes_max;
    /** Records a query tested; queries that tested every record of the
     *  kind asked (every record for an unscoped "all"), that narrowed the
     *  previous query's hits, that the held hits answered, that were
     *  scoped "all" queries. */
    long examined;
    long full_scans;
    long narrowed;
    long held;
    long scoped;
    /** Config blocks read back from disk for record views. */
    long block_reads;
    /** ToriRSMapEdCatalog_RefreshPaths calls that reached a built index. */
    long path_refreshes;
};

void
ToriRSMapEdCatalog_Counters(
    const struct ToriRSMapEdCatalog* catalog,
    struct ToriRSMapEdCatalogCounters* out);

/** 1 while the warm build runs (the index is partial), with its progress
 *  as the PROGRESS tail carries it (KiB done of KiB total; 0/0 while still
 *  scanning). 0 once built (1/1). */
int
ToriRSMapEdCatalog_Progress(
    const struct ToriRSMapEdCatalog* catalog,
    uint32_t* out_done_kib,
    uint32_t* out_total_kib);

/** Bytes per warm-build part of a large blocks file
 *  (TORIRSMAPED_CATALOG_SLICE_BYTES by default; the perf test lowers it to
 *  see a small tree's all.loc parsed in parts). At least 4096. */
void
ToriRSMapEdCatalog_SetSliceBytes(
    struct ToriRSMapEdCatalog* catalog,
    size_t bytes);

/** Seconds between the end of one freshness sweep and the next a request
 *  may start. Default 2. */
void
ToriRSMapEdCatalog_SetRefreshInterval(
    struct ToriRSMapEdCatalog* catalog,
    int seconds);

/**
 * The request handlers. Each decodes a request payload and appends the
 * answering fact's payload to `out`; the caller frames and sends it. A
 * malformed payload returns 0 and leaves `out` empty (the session closes,
 * as for any framing error); a well-formed request that finds nothing is
 * answered with a status, never refused.
 */
int
ToriRSMapEdCatalog_HandleQuery(
    struct ToriRSMapEdCatalog* catalog,
    const uint8_t* body,
    uint32_t length,
    struct ToriRSMapEdBuf* out);

int
ToriRSMapEdCatalog_HandleRecord(
    struct ToriRSMapEdCatalog* catalog,
    const uint8_t* body,
    uint32_t length,
    struct ToriRSMapEdBuf* out);

/** CATALOG_USES (the header comment): one slice of the search. 0 for a
 *  malformed body. */
int
ToriRSMapEdCatalog_HandleUses(
    struct ToriRSMapEdCatalog* catalog,
    const uint8_t* body,
    uint32_t length,
    struct ToriRSMapEdBuf* out);

/** The atlas, with the authority's open documents folded in: squares that
 *  exist only in `doc` (DOC_CREATE, unsaved) appear flagged CREATED, open
 *  and dirty squares are flagged. The body is empty (the v2 rows) or one
 *  `u32 flags`; `*inout_locs` is set to whether the rows carry loc_count,
 *  which the caller remembers for its broadcasts. 0 for a malformed body. */
int
ToriRSMapEdCatalog_HandleSquares(
    struct ToriRSMapEdCatalog* catalog,
    const struct Editor_Doc* doc,
    const uint8_t* body,
    uint32_t length,
    int* inout_locs,
    struct ToriRSMapEdBuf* out);

/** The same payload without the refresh: what the server broadcasts as
 *  FACT_CATALOG_SQUARES_CHANGED right after a DOC_CREATE or a maps refresh,
 *  where a full stat sweep of the tree would be wasted. `with_locs` picks
 *  the rows flagged TORIRSMAPED_SQUARE_LOC_COUNT (a connection that asked
 *  with TORIRSMAPED_CATALOG_SQUARES_LOCS) or the v2 rows. */
void
ToriRSMapEdCatalog_EncodeSquares(
    struct ToriRSMapEdCatalog* catalog,
    const struct Editor_Doc* doc,
    int with_locs,
    struct ToriRSMapEdBuf* out);

/* ---- direct lookups (the test, and anything in-process) -------------- */

/** The query engine behind CATALOG_QUERY: matching record indices, sorted
 *  (exact name matches first, whole filter matches before prefix ones, then
 *  by kind, id, name), written into `out_records` up to `max`. Returns the
 *  total match count: every match, kind "all" included (only the wire's
 *  "all" is scoped). Waits for the index when there is none.
 *
 *  The last query's hits are held, keyed on (kind, text, flags, index
 *  revision): the same query again (the next page) tests nothing, and a
 *  query whose terms and filters imply the held one's (typing on: "oa" after
 *  "o", "oak tree" after "oak") tests only the held hits. */
int
ToriRSMapEdCatalog_Query(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* filter,
    uint32_t flags,
    int* out_records,
    int max);

/** One merged record's identity. Pointers are valid until the next refresh
 *  that changes the index. */
struct ToriRSMapEdCatalogEntry
{
    uint32_t kind;
    int32_t id;
    uint32_t provenance;
    const char* name;
    const char* display;
};

void
ToriRSMapEdCatalog_Entry(
    const struct ToriRSMapEdCatalog* catalog,
    int record,
    struct ToriRSMapEdCatalogEntry* out);

/** A summary field of the merged record (overlay over cache, derived ones
 *  included) copied into `out`. Returns 0 when the record has no such field. */
int
ToriRSMapEdCatalog_Field(
    const struct ToriRSMapEdCatalog* catalog,
    int record,
    const char* key,
    char* out,
    size_t out_size);

/** One atlas row, by square key ("m50_50" or a lane key). Returns 0 when the
 *  tree has no such square. `out_area_name` gets the primary area's
 *  internal name, or "none". */
struct ToriRSMapEdCatalogSquare
{
    int map_x;
    int map_z;
    const char* storage;
    uint32_t flags;
    int spawn_count;
    int loc_count;
    uint32_t rgb;
    const char* area;
    int area_count;
};

int
ToriRSMapEdCatalog_Square(
    const struct ToriRSMapEdCatalog* catalog,
    const char* key,
    struct ToriRSMapEdCatalogSquare* out);

int
ToriRSMapEdCatalog_SquareCount(const struct ToriRSMapEdCatalog* catalog);

#endif
