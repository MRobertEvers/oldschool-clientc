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
 * re-merges (the merge is cheap next to reading 190 MB of config text).
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
 *     by family (`op` matches op1..op5), the value matches case-insensitively
 *     whole, or as a prefix ending at '_' (so `varb4153` names
 *     `varb4153_x`). Any other term must match the record's name, display
 *     name or decimal id — as a substring, or whole with
 *     TORIRSMAPED_CATALOG_EXACT, where the remaining text is one term and
 *     the lookup is a hash probe rather than a scan.
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
 *   CATALOG_SQUARES (empty)
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
 *                               u32 loc_count (the locs its .jl2 places;
 *                               appended 2026-11-05, workbench 6.4))
 *
 *   FACT_CATALOG_SQUARES_CHANGED, same payload, is BROADCAST to every v2
 *   connection when the atlas changes under it: a DOC_CREATE (the new
 *   square appears with TORIRSMAPED_SQUARE_CREATED before anything is on
 *   disk) or a save that wrote squares. A mirror replaces its atlas.
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
};

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
};

/** Instance space: the unmapped high-coordinate squares the instance helper
 *  fills (the workbench design's section 13, "unmapped space 0..19 x
 *  100..255"), map_x in [X0, X1] and map_z in [Z0, Z1]. The picker draws it
 *  hatched where no square exists. */
#define TORIRSMAPED_INSTANCE_X0 0
#define TORIRSMAPED_INSTANCE_X1 19
#define TORIRSMAPED_INSTANCE_Z0 100
#define TORIRSMAPED_INSTANCE_Z1 255

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

/** Only maps/ and the lanes' maps/ are re-stated: what a square save calls,
 *  so writing one square does not re-stat 40,000 script files. */
int
ToriRSMapEdCatalog_RefreshMaps(struct ToriRSMapEdCatalog* catalog);

/** 1 once the index has been built: until then nobody holds an atlas, and a
 *  change needs no broadcast. */
int
ToriRSMapEdCatalog_Built(const struct ToriRSMapEdCatalog* catalog);

/** Milliseconds one service slice may spend on a freshness sweep, and on
 *  the warm build (the build is ~6 s of work on the owner's tree: at 6 ms a
 *  frame it is done ~20 s after the editor opens; a Catalog opened sooner
 *  waits for the rest). */
#define TORIRSMAPED_CATALOG_SLICE_MS 2.0
#define TORIRSMAPED_CATALOG_WARM_SLICE_MS 6.0

/**
 * The index's background job, from the server's service pass (every pump):
 * builds the index in slices from the first call (a scan, then the files
 * parsed a few at a time, then the merge), and afterwards advances a
 * freshness sweep a request started. Spends about `budget_ms` per call; one
 * file's parse or the closing merge may run past it. Returns 1 while work
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
    /** Merges run (each bumps the index revision). */
    long merges;
    /** Records a query tested; queries that tested every record, that
     *  narrowed the previous query's hits, that the held hits answered. */
    long examined;
    long full_scans;
    long narrowed;
    long held;
    /** Config blocks read back from disk for record views. */
    long block_reads;
};

void
ToriRSMapEdCatalog_Counters(
    const struct ToriRSMapEdCatalog* catalog,
    struct ToriRSMapEdCatalogCounters* out);

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

/** The atlas, with the authority's open documents folded in: squares that
 *  exist only in `doc` (DOC_CREATE, unsaved) appear flagged CREATED, open
 *  and dirty squares are flagged. */
int
ToriRSMapEdCatalog_HandleSquares(
    struct ToriRSMapEdCatalog* catalog,
    const struct Editor_Doc* doc,
    const uint8_t* body,
    uint32_t length,
    struct ToriRSMapEdBuf* out);

/** The same payload without the refresh: what the server broadcasts as
 *  FACT_CATALOG_SQUARES_CHANGED right after a DOC_CREATE or a maps refresh,
 *  where a full stat sweep of the tree would be wasted. */
void
ToriRSMapEdCatalog_EncodeSquares(
    struct ToriRSMapEdCatalog* catalog,
    const struct Editor_Doc* doc,
    struct ToriRSMapEdBuf* out);

/* ---- direct lookups (the test, and anything in-process) -------------- */

/** The query engine behind CATALOG_QUERY: matching record indices, sorted
 *  (exact name matches first, then by kind, id, name), written into
 *  `out_records` up to `max`. Returns the total match count.
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
