/*
 * The asset catalog and the square atlas (torirs_maped_catalog.h has the
 * sources and the wire).
 *
 * BUILD NOTE: this file is compiled INTO torirs_maped.c (`#include
 * "torirs_maped_catalog.c"` at its end), not linked on its own. torirs_maped.c
 * is named on several hand-written link lines (the client's SRCS with and
 * without EMBED_SERVER, the embed test, the daemon), and the phase that added
 * the catalog could not edit any of them; compiling it into the one unit they
 * all already list is what makes every one of them carry it. Never add this
 * file to a link line that also has torirs_maped.c: the symbols would be
 * defined twice. Every file-scope name here starts with `cat_` (statics) or
 * `ToriRSMapEdCatalog_` (the API) so the two units cannot collide.
 *
 * Shape of the index:
 *
 *   sources   one per file the catalog reads, each with its mtime/size and
 *             what parsing it produced: CONTRIBUTIONS (kind, name, id,
 *             display, a summary field list, the byte range of its block) in
 *             a string pool of its own, plus per-type extras (a square's
 *             level-0 colour histogram, an area file's claims).
 *   records   the merge: contributions joined by (kind, name) into one record
 *             per entity, with a chain of refs back to every contribution so
 *             a record view can read each source's block from disk.
 *   atlas     one row per square on disk, coloured, claimed by world-map
 *             areas, with its spawn count.
 *
 * A refresh re-stats, re-parses changed sources, drops vanished ones and
 * re-merges. Records point into source pools, so the merge is redone whenever
 * any source changed — never patched.
 */

#include "torirs_maped_catalog.h"

#include "torirs_maped.h"
#include "editor/editor_doc.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "log/torirs_log.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define CAT_NONE 0xFFFFFFFFu
#define CAT_PATH_MAX 1024
/** Squares are addressed by (x, z) < 256 each: tile coordinates reach 16383. */
#define CAT_REGION_SPAN 256
#define CAT_FIELDS_MAX 96
#define CAT_QUERY_TERMS_MAX 8
#define CAT_QUERY_FILTERS_MAX 8

enum cat_src_type
{
    CAT_SRC_PACK = 0,
    CAT_SRC_ALLOC,
    CAT_SRC_MEMBER_CLIENT,
    CAT_SRC_MEMBER_SERVER,
    CAT_SRC_COMPACK,
    CAT_SRC_CONFIG,
    CAT_SRC_OVERLAY,
    CAT_SRC_NPC_STATS,
    CAT_SRC_NPC_COMBAT,
    CAT_SRC_IF_COMPACK,
    CAT_SRC_IF,
    CAT_SRC_CS2,
    CAT_SRC_CS2B,
    CAT_SRC_RS2,
    CAT_SRC_SPAWN,
    CAT_SRC_JM2,
    CAT_SRC_JL2,
    CAT_SRC_WMA,
    CAT_SRC_WMC,
};

/** One thing a source says about one entity. Strings are pool offsets. */
struct cat_contrib
{
    uint32_t name;
    uint32_t display;
    /** "key\0value\0key\0value\0\0", or CAT_NONE. */
    uint32_t fields;
    /** The block's byte range in the file (config and overlay blocks, the
     *  whole file for stats); what a record view reads back. */
    uint32_t off;
    uint32_t len;
    uint32_t line;
    int32_t id;
    /** A spawn tally's row count. */
    int32_t value;
    uint16_t kind;
    uint16_t prov;
};

struct cat_area_src
{
    uint32_t internal;
    uint32_t external;
    int is_main;
};

struct cat_claim
{
    /** Pool offset of the claiming area's internal name. */
    uint32_t area;
    int rx0;
    int rz0;
    int rx1;
    int rz1;
};

struct cat_source
{
    char* path;
    uint32_t hash;
    int type;
    int kind;
    int64_t mtime_ns;
    int64_t size;
    int seen;
    int dirty;

    struct cat_contrib* contribs;
    int contrib_count;
    int contrib_cap;
    char* pool;
    size_t pool_len;
    size_t pool_cap;

    /* JM2: level-0 (overlay << 8 | underlay) -> tile count, as pairs. */
    uint32_t* hist;
    int hist_count;
    /* JL2: the locs it places (one a line after the header). */
    int has_locs;
    int loc_count;
    /* WMA / WMC */
    struct cat_area_src* areas;
    int area_count;
    int area_cap;
    struct cat_claim* claims;
    int claim_count;
    int claim_cap;
};

struct cat_ref
{
    int32_t source;
    int32_t contrib;
    int32_t next;
};

enum
{
    CAT_REC_DIALOGUE = 1,
};

struct cat_record
{
    const char* name;
    const char* display;
    int32_t id;
    uint32_t prov;
    uint16_t kind;
    uint16_t flags;
    int32_t users;
    int32_t square;
    int32_t first_ref;
    int32_t last_ref;
    int32_t next_name;
    int32_t next_display;
    int32_t next_id;
};

struct cat_area
{
    const char* internal;
    const char* external;
    int is_main;
    int claimed;
};

struct cat_square
{
    int map_x;
    int map_z;
    int storage;
    uint32_t flags;
    int spawn_count;
    int loc_count;
    uint32_t rgb;
    int primary_area;
    int area_first;
    int area_n;
    int record;
};

struct cat_hit
{
    int score;
    int kind;
    int32_t id;
    const char* name;
    int record;
};

/** The last query's hits (cat_query_hits): the next page, a repeat and a
 *  narrowing query read them instead of the records. */
struct cat_held_query
{
    int valid;
    uint32_t kind;
    uint32_t flags;
    uint32_t revision;
    char filter[1024];
    struct cat_hit* hits;
    int count;
    int cap;
};

/** Directories the sliced scan holds open at once; a deeper subtree is
 *  walked whole when reached (none in the tree is near it). */
#define CAT_JOB_DEPTH 24

enum cat_job_phase
{
    CAT_JOB_IDLE = 0,
    /** Walking the roots, a slice at a time (cat_found marks what changed). */
    CAT_JOB_SCAN,
    /** The warm build only: parsing the dirty sources a few at a time. A
     *  sweep parses and merges in one step, since a record's refs index its
     *  sources' contributions and must not see half a re-parse. */
    CAT_JOB_PARSE,
};

struct cat_job_dir
{
    DIR* dir;
    int root;
    char rel[CAT_PATH_MAX];
};

/** The background index job (cat_job_begin / cat_job_step): the warm build,
 *  then freshness sweeps. */
struct cat_job
{
    enum cat_job_phase phase;
    /** The next root to open: k_cat_roots, then `lanes`. */
    int root;
    int depth;
    struct cat_job_dir stack[CAT_JOB_DEPTH];
    /** "maps" and the lanes' maps/, in the order the full scan walks them. */
    char** lanes;
    int lane_count;
    int parse_at;
    struct ToriRSMapEdCatalogStats stats;
};

struct ToriRSMapEdCatalog
{
    char content_dir[CAT_PATH_MAX];
    int refresh_interval_s;
    time_t last_refresh;
    int built;
    /** A save happened: the next query refreshes whatever the interval. */
    int invalid;

    struct cat_source* sources;
    int source_count;
    int source_cap;
    int32_t* path_slots;
    int path_slot_count;

    /** "maps", then "ported/<lane>/maps" per lane directory, by name. */
    char** storages;
    int storage_count;
    /** The last maps scan changed the list: square rows index it, so the
     *  merge must run even when no file changed (a new, empty lane). */
    int storages_changed;

    /* the merge */
    struct cat_record* records;
    int record_count;
    struct cat_ref* refs;
    int ref_count;
    int32_t* name_heads;
    int32_t* display_heads;
    int32_t* id_heads;
    uint32_t hash_mask;

    struct cat_area* areas;
    int area_count;
    struct cat_square* squares;
    int square_count;
    int* square_areas;
    int square_area_count;
    /** Area chains per region, for squares DOC_CREATE makes after the merge. */
    int32_t* region_heads;
    int32_t* region_nodes; /* pairs: area, next */
    int region_node_count;
    int* spawn_counts; /* CAT_REGION_SPAN^2 */

    /** Reused file buffer. */
    char* scratch;
    size_t scratch_cap;

    /** The background job: the warm build and the freshness sweeps. */
    struct cat_job job;
    /** Bumped by every merge; the held query is keyed on it. */
    uint32_t revision;
    /** Bumped by a merge of a built index that took in a maps change (a
     *  square file parsed or gone, a lane): the atlas holders want it. */
    uint32_t maps_revision;
    struct cat_held_query held;
    struct ToriRSMapEdCatalogCounters counters;
};

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static double
cat_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void*
cat_grow(
    void* ptr,
    int* cap,
    int need,
    size_t elem)
{
    int n;
    if( need <= *cap )
        return ptr;
    n = *cap > 0 ? *cap : 16;
    while( n < need )
        n *= 2;
    ptr = ToriMem_Realloc(ToriMem_Editor(), ptr, (size_t)n * elem);
    assert(ptr);
    *cap = n;
    return ptr;
}

static char
cat_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static uint32_t
cat_hash_lower(
    uint32_t kind,
    const char* s)
{
    uint32_t h = 2166136261u ^ (kind * 16777619u);
    for( ; *s; s++ )
        h = (h ^ (uint8_t)cat_lower(*s)) * 16777619u;
    return h;
}

static uint32_t
cat_hash_bytes(const char* s)
{
    uint32_t h = 2166136261u;
    for( ; *s; s++ )
        h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

static int
cat_ieq(
    const char* a,
    const char* b)
{
    for( ; *a && *b; a++, b++ )
    {
        if( cat_lower(*a) != cat_lower(*b) )
            return 0;
    }
    return *a == *b;
}

/** Case-insensitive: does `hay` start with `needle`? */
static int
cat_iprefix(
    const char* hay,
    const char* needle)
{
    for( ; *needle; hay++, needle++ )
    {
        if( !*hay || cat_lower(*hay) != cat_lower(*needle) )
            return 0;
    }
    return 1;
}

/** Case-insensitive substring; `needle` already lower-case. */
static int
cat_icontains(
    const char* hay,
    const char* needle)
{
    if( !needle[0] )
        return 1;
    for( ; *hay; hay++ )
    {
        if( cat_lower(*hay) != needle[0] )
            continue;
        if( cat_iprefix(hay, needle) )
            return 1;
    }
    return 0;
}

/** Length of `key` without its trailing digits: "op3" -> "op". */
static size_t
cat_family_len(
    const char* key,
    size_t length)
{
    while( length > 0 && key[length - 1] >= '0' && key[length - 1] <= '9' )
        length--;
    return length;
}

static int
cat_parse_int(
    const char* s,
    const char* end,
    int* out)
{
    int sign = 1;
    int value = 0;
    int digits = 0;
    if( s < end && *s == '-' )
    {
        sign = -1;
        s++;
    }
    while( s < end && *s >= '0' && *s <= '9' )
    {
        value = value * 10 + (*s - '0');
        s++;
        digits++;
    }
    if( !digits || s != end )
        return 0;
    *out = value * sign;
    return 1;
}

/** "m<x>_<z>" (a bare name, no extension). */
static int
cat_square_name(
    const char* name,
    size_t length,
    int* out_x,
    int* out_z)
{
    const char* underscore;
    if( length < 4 || name[0] != 'm' )
        return 0;
    underscore = memchr(name, '_', length);
    if( !underscore )
        return 0;
    if( !cat_parse_int(name + 1, underscore, out_x)
        || !cat_parse_int(underscore + 1, name + length, out_z) )
        return 0;
    return *out_x >= 0 && *out_x < CAT_REGION_SPAN && *out_z >= 0 && *out_z < CAT_REGION_SPAN;
}

static int64_t
cat_mtime_ns(const struct stat* st)
{
#if defined(__APPLE__)
    return (int64_t)st->st_mtimespec.tv_sec * 1000000000 + st->st_mtimespec.tv_nsec;
#elif defined(__linux__) && defined(st_mtime)
    return (int64_t)st->st_mtim.tv_sec * 1000000000 + st->st_mtim.tv_nsec;
#else
    return (int64_t)st->st_mtime * 1000000000;
#endif
}

/** Read a file (or its first `max` bytes, 0 = all) into the catalog's
 *  scratch buffer, NUL-terminated. `size_hint` is the size the scan saw: one
 *  open and one read for 40,000 small files is most of what an index build
 *  costs. Returns NULL when it cannot be read — a file that vanished between
 *  the scan and the parse is data. */
static char*
cat_read(
    struct ToriRSMapEdCatalog* catalog,
    const char* rel,
    size_t size_hint,
    size_t max,
    size_t* out_size)
{
    char path[CAT_PATH_MAX * 2];
    size_t want = size_hint;
    size_t got = 0;
    int fd;

    snprintf(path, sizeof(path), "%s/%s", catalog->content_dir, rel);
    fd = open(path, O_RDONLY | O_BINARY);
    if( fd < 0 )
        return NULL;
    if( max > 0 && want > max )
        want = max;
    for( ;; )
    {
        ssize_t n;
        if( got + 1 >= catalog->scratch_cap || want + 1 > catalog->scratch_cap )
        {
            size_t cap = catalog->scratch_cap ? catalog->scratch_cap : 65536;
            while( cap < want + 1 || cap < got + 4096 )
                cap *= 2;
            catalog->scratch = ToriMem_Realloc(ToriMem_Editor(), catalog->scratch, cap);
            assert(catalog->scratch);
            catalog->scratch_cap = cap;
        }
        if( max > 0 && got >= max )
            break;
        n = read(fd, catalog->scratch + got,
            (max > 0 ? max - got : catalog->scratch_cap - 1 - got));
        if( n <= 0 )
            break;
        got += (size_t)n;
        /* The file grew since the scan: keep reading (the next refresh
         * re-parses it anyway, since its mtime moved). */
        if( got > want )
            want = got;
    }
    close(fd);
    catalog->scratch[got] = '\0';
    *out_size = got;
    return catalog->scratch;
}

/* ------------------------------------------------------------------ */
/* Source pools                                                        */
/* ------------------------------------------------------------------ */

static uint32_t
cat_pool_add(
    struct cat_source* src,
    const char* text,
    size_t length)
{
    size_t at = src->pool_len;
    if( at + length + 1 > src->pool_cap )
    {
        size_t cap = src->pool_cap ? src->pool_cap : 256;
        while( cap < at + length + 1 )
            cap *= 2;
        src->pool = ToriMem_Realloc(ToriMem_Editor(), src->pool, cap);
        assert(src->pool);
        src->pool_cap = cap;
    }
    if( length > 0 )
        memcpy(src->pool + at, text, length);
    src->pool[at + length] = '\0';
    src->pool_len = at + length + 1;
    assert(src->pool_len < CAT_NONE);
    return (uint32_t)at;
}

static uint32_t
cat_pool_str(
    struct cat_source* src,
    const char* text)
{
    return cat_pool_add(src, text, strlen(text));
}

static uint32_t
cat_pool_int(
    struct cat_source* src,
    int value)
{
    char text[24];
    snprintf(text, sizeof(text), "%d", value);
    return cat_pool_str(src, text);
}

static struct cat_contrib*
cat_contrib_add(
    struct cat_source* src,
    int kind,
    int prov)
{
    struct cat_contrib* c;
    src->contribs = cat_grow(
        src->contribs, &src->contrib_cap, src->contrib_count + 1, sizeof(*src->contribs));
    c = &src->contribs[src->contrib_count++];
    memset(c, 0, sizeof(*c));
    c->name = CAT_NONE;
    c->display = CAT_NONE;
    c->fields = CAT_NONE;
    c->id = -1;
    c->kind = (uint16_t)kind;
    c->prov = (uint16_t)prov;
    return c;
}

static void
cat_source_clear(struct cat_source* src)
{
    src->contrib_count = 0;
    src->pool_len = 0;
    src->hist_count = 0;
    src->has_locs = 0;
    src->loc_count = 0;
    src->area_count = 0;
    src->claim_count = 0;
}

static void
cat_source_free(struct cat_source* src)
{
    ToriMem_Free(ToriMem_Editor(), src->path);
    ToriMem_Free(ToriMem_Editor(), src->contribs);
    ToriMem_Free(ToriMem_Editor(), src->pool);
    ToriMem_Free(ToriMem_Editor(), src->hist);
    ToriMem_Free(ToriMem_Editor(), src->areas);
    ToriMem_Free(ToriMem_Editor(), src->claims);
    memset(src, 0, sizeof(*src));
}

/* ------------------------------------------------------------------ */
/* Kind tables                                                         */
/* ------------------------------------------------------------------ */

/** A config type name ("loc", "varbit") -> its kind, or -1. */
static int
cat_config_kind(
    const char* name,
    size_t length)
{
    for( int kind = TORIRSMAPED_CATALOG_LOC; kind <= TORIRSMAPED_CATALOG_MAPELEMENT; kind++ )
    {
        const char* label = ToriRSMapEdCatalog_KindName((uint32_t)kind);
        if( strlen(label) == length && memcmp(label, name, length) == 0 )
            return kind;
    }
    return -1;
}

/** pack/<stem>.pack -> kind. A leading "<index>_" is the cache index. */
static int
cat_pack_kind(const char* stem)
{
    static const struct
    {
        const char* table;
        int kind;
    } tables[] = {
        { "animations", TORIRSMAPED_CATALOG_FRAMESET },
        { "skeletons", TORIRSMAPED_CATALOG_SKELETON },
        { "interfaces", TORIRSMAPED_CATALOG_INTERFACE },
        { "soundeffects", TORIRSMAPED_CATALOG_SYNTH },
        { "maps", TORIRSMAPED_CATALOG_SQUARE },
        { "musictracks", TORIRSMAPED_CATALOG_SONG },
        { "models", TORIRSMAPED_CATALOG_MODEL },
        { "sprites", TORIRSMAPED_CATALOG_SPRITE },
        { "textures", TORIRSMAPED_CATALOG_TEXTURE },
        { "musicjingles", TORIRSMAPED_CATALOG_JINGLE },
        { "clientscripts", TORIRSMAPED_CATALOG_CS2 },
        { "fonts", TORIRSMAPED_CATALOG_FONT },
        { "category", TORIRSMAPED_CATALOG_CATEGORY },
        { "stat", TORIRSMAPED_CATALOG_STAT },
    };
    const char* table = stem;
    const char* underscore = strchr(stem, '_');

    if( underscore && stem[0] >= '0' && stem[0] <= '9' )
        table = underscore + 1;
    for( size_t i = 0; i < sizeof(tables) / sizeof(tables[0]); i++ )
    {
        if( strcmp(tables[i].table, table) == 0 )
            return tables[i].kind;
    }
    return TORIRSMAPED_CATALOG_PACK;
}

/**
 * The summary fields a CACHE block keeps, by key family (a key minus its
 * trailing digits). Everything else in all.loc's 86 MB is the record view's
 * business, read back from disk on demand. Overlay blocks keep every field:
 * they are small and what they state is exactly what a filter wants.
 */
static const char* const*
cat_whitelist(int kind)
{
    static const char* const loc[] = { "op", "shape", "width", "length", "category",
        "multivarp", "multivarbit", "model", "models", "blockwalk", "active", NULL };
    static const char* const npc[] = { "op", "vislevel", "size", "category", "multivarp",
        "multivarbit", "model", NULL };
    static const char* const obj[] = { "op", "ifop", "stackable", "wearpos", "cost",
        "category", "members", "certlink", "model", "tradeable", NULL };
    static const char* const spotanim[] = { "model", "anim", NULL };
    static const char* const varp[] = { "clientcode", "scope", "transmit", "type", NULL };
    static const char* const varbit[] = { "basevar", "startbit", "endbit", NULL };
    static const char* const varc[] = { "type", "persist", NULL };
    static const char* const enum_[] = { "inputtype", "outputtype", NULL };
    static const char* const param[] = { "type", "default", NULL };
    static const char* const inv[] = { "size", NULL };
    static const char* const dbrow[] = { "table", NULL };
    static const char* const idk[] = { "bodypart", "model", NULL };
    static const char* const underlay[] = { "colour", NULL };
    static const char* const overlay[] = { "colour", "texture", "blendcolour",
        "hideunderlay", NULL };
    static const char* const mapelement[] = { "category", "sprite", NULL };
    static const char* const other[] = { "category", NULL };

    switch( kind )
    {
    case TORIRSMAPED_CATALOG_LOC:
        return loc;
    case TORIRSMAPED_CATALOG_NPC:
        return npc;
    case TORIRSMAPED_CATALOG_OBJ:
        return obj;
    case TORIRSMAPED_CATALOG_SPOTANIM:
        return spotanim;
    case TORIRSMAPED_CATALOG_VARP:
        return varp;
    case TORIRSMAPED_CATALOG_VARBIT:
        return varbit;
    case TORIRSMAPED_CATALOG_VARC:
        return varc;
    case TORIRSMAPED_CATALOG_ENUM:
        return enum_;
    case TORIRSMAPED_CATALOG_PARAM:
        return param;
    case TORIRSMAPED_CATALOG_INV:
        return inv;
    case TORIRSMAPED_CATALOG_DBROW:
        return dbrow;
    case TORIRSMAPED_CATALOG_IDK:
        return idk;
    case TORIRSMAPED_CATALOG_UNDERLAY:
        return underlay;
    case TORIRSMAPED_CATALOG_OVERLAY:
        return overlay;
    case TORIRSMAPED_CATALOG_MAPELEMENT:
        return mapelement;
    default:
        return other;
    }
}

static int
cat_listed(
    const char* const* list,
    const char* key,
    size_t length)
{
    for( ; *list; list++ )
    {
        if( strlen(*list) == length && memcmp(*list, key, length) == 0 )
            return 1;
    }
    return 0;
}

/** Which repeated key family a kind COUNTS rather than stores. */
static const char*
cat_counted_family(int kind)
{
    switch( kind )
    {
    case TORIRSMAPED_CATALOG_SEQ:
        return "frame";
    case TORIRSMAPED_CATALOG_ENUM:
        return "val";
    case TORIRSMAPED_CATALOG_STRUCT:
        return "param";
    case TORIRSMAPED_CATALOG_DBROW:
        return "data";
    case TORIRSMAPED_CATALOG_DBTABLE:
        return "column";
    default:
        return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Parsers                                                             */
/* ------------------------------------------------------------------ */

/** Iterate lines: sets *line_end (before any '\r') and returns the start of
 *  the next line. */
static const char*
cat_next_line(
    const char* p,
    const char* end,
    const char** out_line_end)
{
    const char* nl = memchr(p, '\n', (size_t)(end - p));
    const char* le = nl ? nl : end;
    if( le > p && le[-1] == '\r' )
        le--;
    *out_line_end = le;
    return nl ? nl + 1 : end;
}

static void
cat_trim(
    const char** s,
    const char** e)
{
    while( *s < *e && (**s == ' ' || **s == '\t') )
        (*s)++;
    while( *e > *s && ((*e)[-1] == ' ' || (*e)[-1] == '\t') )
        (*e)--;
}

/** `id=name[ extra]` tables: pack/<t>.pack, pack/<ns>.alloc, compacks. */
static void
cat_parse_idname(
    struct cat_source* src,
    const char* text,
    size_t size,
    int kind,
    int prov,
    const char* table)
{
    const char* p = text;
    const char* end = text + size;
    uint32_t line = 0;

    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        const char* eq;
        const char* name_end;
        int id;

        line++;
        eq = memchr(p, '=', (size_t)(le - p));
        if( p < le && p[0] != '/' && eq && cat_parse_int(p, eq, &id) )
        {
            struct cat_contrib* c;
            name_end = eq + 1;
            while( name_end < le && *name_end != ' ' && *name_end != '\t' )
                name_end++;
            if( name_end > eq + 1 )
            {
                c = cat_contrib_add(src, kind, prov);
                c->id = id;
                c->line = line;
                c->name = cat_pool_add(src, eq + 1, (size_t)(name_end - (eq + 1)));
                if( table || name_end < le )
                {
                    const char* extra = name_end;
                    const char* extra_end = le;
                    c->fields = (uint32_t)src->pool_len;
                    if( table )
                    {
                        cat_pool_str(src, "table");
                        cat_pool_str(src, table);
                    }
                    cat_trim(&extra, &extra_end);
                    if( extra < extra_end )
                    {
                        cat_pool_str(src, "extra");
                        cat_pool_add(src, extra, (size_t)(extra_end - extra));
                    }
                    cat_pool_add(src, "", 0);
                }
            }
        }
        p = next;
    }
}

/** pack/<ns>.client|.server: one name per line. */
static void
cat_parse_members(
    struct cat_source* src,
    const char* text,
    size_t size,
    int kind,
    int prov)
{
    const char* p = text;
    const char* end = text + size;
    uint32_t line = 0;

    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        const char* s = p;
        line++;
        cat_trim(&s, &le);
        if( s < le && s[0] != '/' )
        {
            struct cat_contrib* c = cat_contrib_add(src, kind, prov);
            c->line = line;
            c->name = cat_pool_add(src, s, (size_t)(le - s));
        }
        p = next;
    }
}

struct cat_block
{
    struct cat_contrib* contrib;
    int index;
    int slots;
    int counted;
    uint32_t multivar;
};

static void
cat_block_close(
    struct cat_source* src,
    struct cat_block* block,
    uint32_t end_offset)
{
    struct cat_contrib* c;
    if( block->index < 0 )
        return;
    /* The pool may have moved since the block opened; re-take the pointer. */
    c = &src->contribs[block->index];
    if( block->slots > 0 )
    {
        cat_pool_str(src, "slots");
        cat_pool_int(src, block->slots);
    }
    if( block->counted > 0 )
    {
        cat_pool_str(src, c->kind == TORIRSMAPED_CATALOG_SEQ ? "frames" : "count");
        cat_pool_int(src, block->counted);
    }
    if( block->multivar != CAT_NONE )
    {
        char value[256];
        snprintf(value, sizeof(value), "%s", src->pool + block->multivar);
        cat_pool_str(src, "multivar");
        cat_pool_str(src, value);
    }
    cat_pool_add(src, "", 0);
    c->len = end_offset - c->off;
    block->index = -1;
}

/**
 * `[name]` blocks of `key=value` lines: the cache's configs/all.<type> (only
 * whitelisted summary fields kept) and the server's overlays (`keep_all`).
 * `default` values say nothing and are dropped; repeated multi slots and
 * per-kind repeated families (seq frames, enum vals, ...) are counted.
 */
static void
cat_parse_blocks(
    struct cat_source* src,
    const char* text,
    size_t size,
    int kind,
    int prov,
    int keep_all)
{
    const char* const* whitelist = cat_whitelist(kind);
    const char* counted = cat_counted_family(kind);
    size_t counted_len = counted ? strlen(counted) : 0;
    const char* p = text;
    const char* end = text + size;
    uint32_t line = 0;
    struct cat_block block;

    assert(size < CAT_NONE);
    block.index = -1;

    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        line++;

        if( p < le && p[0] == '[' )
        {
            const char* rb = memchr(p, ']', (size_t)(le - p));
            cat_block_close(src, &block, (uint32_t)(p - text));
            if( rb && rb > p + 1 )
            {
                struct cat_contrib* c = cat_contrib_add(src, kind, prov);
                c->name = cat_pool_add(src, p + 1, (size_t)(rb - (p + 1)));
                c->off = (uint32_t)(p - text);
                c->line = line;
                c->fields = (uint32_t)src->pool_len;
                block.index = src->contrib_count - 1;
                block.slots = 0;
                block.counted = 0;
                block.multivar = CAT_NONE;
            }
        }
        else if( block.index >= 0 && p < le && p[0] != '/' )
        {
            const char* eq = memchr(p, '=', (size_t)(le - p));
            if( eq )
            {
                const char* k = p;
                const char* ke = eq;
                const char* v = eq + 1;
                const char* ve = le;
                size_t klen;
                size_t family;

                cat_trim(&k, &ke);
                cat_trim(&v, &ve);
                klen = (size_t)(ke - k);
                family = cat_family_len(k, klen);
                if( (size_t)(ve - v) == 7 && memcmp(v, "default", 7) == 0 )
                {
                    /* says nothing */
                }
                else if( (family == 8 && memcmp(k, "multiloc", 8) == 0)
                         || (family == 8 && memcmp(k, "multinpc", 8) == 0) )
                    block.slots++;
                else if( counted && family == counted_len && memcmp(k, counted, family) == 0 )
                    block.counted++;
                else if( klen == 4 && memcmp(k, "name", 4) == 0 )
                {
                    uint32_t at;
                    cat_pool_add(src, k, klen);
                    at = cat_pool_add(src, v, (size_t)(ve - v));
                    src->contribs[block.index].display = at;
                }
                else if( keep_all || cat_listed(whitelist, k, family) )
                {
                    uint32_t at;
                    cat_pool_add(src, k, klen);
                    at = cat_pool_add(src, v, (size_t)(ve - v));
                    if( ((klen == 9 && memcmp(k, "multivarp", 9) == 0)
                         || (klen == 11 && memcmp(k, "multivarbit", 11) == 0))
                        && !((size_t)(ve - v) == 2 && memcmp(v, "-1", 2) == 0) )
                        block.multivar = at;
                }
            }
        }
        p = next;
    }
    cat_block_close(src, &block, (uint32_t)size);
}

/** npc_stats/<x>.stats and npc_combat/<x>.combat: `key = value  // note`
 *  lines for the npc the file is named after. */
static void
cat_parse_keyvalue_file(
    struct cat_source* src,
    const char* text,
    size_t size,
    const char* stem,
    const char* const* keep)
{
    struct cat_contrib* c = cat_contrib_add(src, TORIRSMAPED_CATALOG_NPC, TORIRSMAPED_PROV_STATS);
    const char* p = text;
    const char* end = text + size;
    int index = src->contrib_count - 1;

    c->name = cat_pool_str(src, stem);
    c->off = 0;
    c->len = (uint32_t)size;
    c->line = 1;
    src->contribs[index].fields = (uint32_t)src->pool_len;
    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        const char* comment;
        const char* eq;

        comment = NULL;
        for( const char* s = p; s + 1 < le; s++ )
        {
            if( s[0] == '/' && s[1] == '/' )
            {
                comment = s;
                break;
            }
        }
        if( comment )
            le = comment;
        eq = memchr(p, '=', (size_t)(le - p));
        if( eq )
        {
            const char* k = p;
            const char* ke = eq;
            const char* v = eq + 1;
            const char* ve = le;
            cat_trim(&k, &ke);
            cat_trim(&v, &ve);
            if( k < ke && v < ve && cat_listed(keep, k, (size_t)(ke - k)) )
            {
                cat_pool_add(src, k, (size_t)(ke - k));
                cat_pool_add(src, v, (size_t)(ve - v));
            }
        }
        p = next;
    }
    cat_pool_add(src, "", 0);
}

/** interfaces/<name>.compack: the interface and one record per component. */
static void
cat_parse_if_compack(
    struct cat_source* src,
    const char* text,
    size_t size,
    const char* stem)
{
    const char* p = text;
    const char* end = text + size;
    uint32_t line = 0;
    int components = 0;
    struct cat_contrib* iface;
    int iface_index;

    iface = cat_contrib_add(src, TORIRSMAPED_CATALOG_INTERFACE, TORIRSMAPED_PROV_COMPACK);
    iface_index = src->contrib_count - 1;
    iface->name = cat_pool_str(src, stem);

    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        const char* eq = memchr(p, '=', (size_t)(le - p));
        int id;
        line++;
        if( p < le && p[0] != '/' && eq && eq + 1 < le && cat_parse_int(p, eq, &id) )
        {
            char name[512];
            struct cat_contrib* c;
            snprintf(name, sizeof(name), "%s:%.*s", stem, (int)(le - (eq + 1)), eq + 1);
            c = cat_contrib_add(src, TORIRSMAPED_CATALOG_COMPONENT, TORIRSMAPED_PROV_COMPACK);
            c->id = id;
            c->line = line;
            c->name = cat_pool_str(src, name);
            c->fields = (uint32_t)src->pool_len;
            cat_pool_str(src, "interface");
            cat_pool_str(src, stem);
            cat_pool_add(src, "", 0);
            components++;
        }
        p = next;
    }
    src->contribs[iface_index].fields = (uint32_t)src->pool_len;
    cat_pool_str(src, "components");
    cat_pool_int(src, components);
    cat_pool_add(src, "", 0);
}

/** interfaces/<name>.if: the `type=` of every root (no `layer=`). */
static void
cat_parse_if(
    struct cat_source* src,
    const char* text,
    size_t size,
    const char* stem)
{
    const char* p = text;
    const char* end = text + size;
    char roots[192];
    size_t roots_len = 0;
    int in_block = 0;
    int has_layer = 0;
    char type[16];
    struct cat_contrib* c;

    roots[0] = '\0';
    type[0] = '\0';
    for( ;; )
    {
        const char* le = end;
        const char* next = end;
        int at_end = p >= end;
        if( !at_end )
            next = cat_next_line(p, end, &le);
        if( at_end || (p < le && p[0] == '[') )
        {
            if( in_block && !has_layer && type[0] && roots_len + strlen(type) + 2 < sizeof(roots)
                && !strstr(roots, type) )
            {
                roots_len += (size_t)snprintf(
                    roots + roots_len, sizeof(roots) - roots_len, "%s%s", roots_len ? "," : "", type);
            }
            if( at_end )
                break;
            in_block = 1;
            has_layer = 0;
            type[0] = '\0';
        }
        else if( in_block && le - p > 6 && memcmp(p, "layer=", 6) == 0 )
            has_layer = 1;
        else if( in_block && le - p > 5 && memcmp(p, "type=", 5) == 0 )
            snprintf(type, sizeof(type), "%.*s", (int)(le - p - 5), p + 5);
        p = next;
    }

    c = cat_contrib_add(src, TORIRSMAPED_CATALOG_INTERFACE, TORIRSMAPED_PROV_SCRIPT);
    c->name = cat_pool_str(src, stem);
    c->fields = (uint32_t)src->pool_len;
    cat_pool_str(src, "roots");
    cat_pool_str(src, roots);
    cat_pool_add(src, "", 0);
}

/** Count the comma-separated items between '(' at *p and its ')'. */
static int
cat_arity(
    const char** p,
    const char* end)
{
    const char* s = *p;
    int commas = 0;
    int any = 0;
    if( s >= end || *s != '(' )
        return -1;
    for( s++; s < end && *s != ')'; s++ )
    {
        if( *s == ',' )
            commas++;
        else if( *s != ' ' && *s != '\t' )
            any = 1;
    }
    *p = s < end ? s + 1 : end;
    return any ? commas + 1 : 0;
}

/** scripts/<name>.cs2: `// <id>` then `[clientscript,name](args)(returns)`. */
static void
cat_parse_cs2(
    struct cat_source* src,
    const char* text,
    size_t size)
{
    const char* p = text;
    const char* end = text + size;
    int id = -1;
    uint32_t line = 0;

    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        line++;
        if( line == 1 && le - p > 3 && p[0] == '/' && p[1] == '/' )
        {
            const char* s = p + 2;
            const char* e = le;
            cat_trim(&s, &e);
            if( !cat_parse_int(s, e, &id) )
                id = -1;
        }
        else if( p < le && p[0] == '[' )
        {
            const char* comma = memchr(p, ',', (size_t)(le - p));
            const char* rb = memchr(p, ']', (size_t)(le - p));
            if( comma && rb && rb > comma + 1 )
            {
                struct cat_contrib* c = cat_contrib_add(src, TORIRSMAPED_CATALOG_CS2, TORIRSMAPED_PROV_SCRIPT);
                const char* after = rb + 1;
                int arity;
                int returns;
                c->name = cat_pool_add(src, comma + 1, (size_t)(rb - (comma + 1)));
                c->id = id;
                c->line = line;
                arity = cat_arity(&after, le);
                returns = cat_arity(&after, le);
                c->fields = (uint32_t)src->pool_len;
                cat_pool_str(src, "kind");
                cat_pool_add(src, p + 1, (size_t)(comma - (p + 1)));
                if( arity >= 0 )
                {
                    cat_pool_str(src, "arity");
                    cat_pool_int(src, arity);
                }
                if( returns >= 0 )
                {
                    cat_pool_str(src, "returns");
                    cat_pool_int(src, returns);
                }
                cat_pool_add(src, "", 0);
            }
            return;
        }
        p = next;
    }
}

/** An .rs2 file's trigger headers, one contribution each. */
static void
cat_parse_rs2(
    struct cat_source* src,
    const char* text,
    size_t size)
{
    const char* p = text;
    const char* end = text + size;
    uint32_t line = 0;
    uint32_t file = cat_pool_str(src, src->path);

    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        line++;
        if( p < le && p[0] == '[' )
        {
            const char* comma = memchr(p, ',', (size_t)(le - p));
            const char* rb = memchr(p, ']', (size_t)(le - p));
            if( comma && rb && comma < rb && rb > comma + 1 )
            {
                struct cat_contrib* c = cat_contrib_add(src, TORIRSMAPED_CATALOG_TRIGGER, TORIRSMAPED_PROV_SCRIPT);
                c->name = cat_pool_add(src, p + 1, (size_t)(rb - (p + 1)));
                c->display = file;
                c->line = line;
                c->fields = (uint32_t)src->pool_len;
                cat_pool_str(src, "trigger");
                cat_pool_add(src, p + 1, (size_t)(comma - (p + 1)));
                cat_pool_str(src, "subject");
                cat_pool_add(src, comma + 1, (size_t)(rb - (comma + 1)));
                cat_pool_add(src, "", 0);
            }
        }
        p = next;
    }
}

/** A .spawn file's rows tallied per square: contributions of kind SQUARE
 *  with prov 0 (never records), id = x << 8 | z, value = rows. */
static void
cat_parse_spawn(
    struct cat_source* src,
    const char* text,
    size_t size)
{
    const char* p = text;
    const char* end = text + size;

    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        char c0 = p < le ? p[0] : '\0';
        if( c0 && c0 != '/' && c0 != '=' && c0 != '-' && c0 != ' ' && c0 != '\t' )
        {
            /* name x z level [count] */
            const char* s = p;
            const char* tokens[4];
            const char* token_ends[4];
            int n = 0;
            int x;
            int z;
            while( s < le && n < 4 )
            {
                while( s < le && (*s == ' ' || *s == '\t') )
                    s++;
                if( s >= le )
                    break;
                tokens[n] = s;
                while( s < le && *s != ' ' && *s != '\t' )
                    s++;
                token_ends[n++] = s;
            }
            if( n == 4 && cat_parse_int(tokens[1], token_ends[1], &x)
                && cat_parse_int(tokens[2], token_ends[2], &z) && x >= 0 && z >= 0
                && (x >> 6) < CAT_REGION_SPAN && (z >> 6) < CAT_REGION_SPAN )
            {
                int32_t key = ((x >> 6) << 8) | (z >> 6);
                int found = 0;
                for( int i = 0; i < src->contrib_count; i++ )
                {
                    if( src->contribs[i].id == key )
                    {
                        src->contribs[i].value++;
                        found = 1;
                        break;
                    }
                }
                if( !found )
                {
                    struct cat_contrib* c = cat_contrib_add(src, TORIRSMAPED_CATALOG_SQUARE, 0);
                    c->id = key;
                    c->value = 1;
                }
            }
        }
        p = next;
    }
}

/** The square's key from its file path: "maps/m50_50.jm2" -> "m50_50",
 *  "ported/l/maps/m16_101.jl2" -> "ported/l/maps/m16_101". */
static void
cat_square_key(
    const char* path,
    char* out,
    size_t out_size)
{
    const char* dot = strrchr(path, '.');
    size_t length = dot ? (size_t)(dot - path) : strlen(path);
    if( strncmp(path, "maps/", 5) == 0 )
    {
        path += 5;
        length -= 5;
    }
    snprintf(out, out_size, "%.*s", (int)length, path);
}

static const char*
cat_basename(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void
cat_add_square_contrib(
    struct cat_source* src,
    int x,
    int z)
{
    char key[CAT_PATH_MAX];
    char bare[32];
    struct cat_contrib* c = cat_contrib_add(src, TORIRSMAPED_CATALOG_SQUARE, TORIRSMAPED_PROV_MAP);
    cat_square_key(src->path, key, sizeof(key));
    snprintf(bare, sizeof(bare), "m%d_%d", x, z);
    c->name = cat_pool_str(src, key);
    c->display = cat_pool_str(src, bare);
}

/** A .jm2: the level-0 (overlay, underlay) histogram the thumbnail colour is
 *  averaged from, resolved against all.underlay/all.overlay at merge time
 *  (so a recoloured underlay recolours the atlas without re-reading maps). */
static void
cat_parse_jm2(
    struct cat_source* src,
    const char* text,
    size_t size,
    int x,
    int z)
{
    enum
    {
        SLOTS = 1024
    };
    uint32_t keys[SLOTS];
    uint32_t counts[SLOTS];
    const char* p = text;
    const char* end = text + size;
    int distinct = 0;

    memset(counts, 0, sizeof(counts));
    cat_add_square_contrib(src, x, z);

    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        if( le - p > 2 && p[1] == ' ' && p[0] >= '1' && p[0] <= '3' )
            break; /* levels are written in order; level 0 is done */
        if( le - p > 2 && p[0] == '0' && p[1] == ' ' )
        {
            const char* colon = memchr(p, ':', (size_t)(le - p));
            uint32_t ul = 0;
            uint32_t ov = 0;
            const char* s = colon ? colon + 1 : le;
            while( s < le )
            {
                while( s < le && *s == ' ' )
                    s++;
                if( s < le && (*s == 'u' || *s == 'o') )
                {
                    char which = *s++;
                    uint32_t value = 0;
                    while( s < le && *s >= '0' && *s <= '9' )
                        value = value * 10 + (uint32_t)(*s++ - '0');
                    if( which == 'u' )
                        ul = value & 0xFF;
                    else
                        ov = value & 0xFFFF;
                }
                while( s < le && *s != ' ' )
                    s++;
            }
            if( ul || ov )
            {
                uint32_t key = (ov << 8) | ul;
                uint32_t slot = (key * 2654435761u) & (SLOTS - 1);
                while( counts[slot] && keys[slot] != key )
                    slot = (slot + 1) & (SLOTS - 1);
                if( !counts[slot] )
                {
                    if( distinct >= SLOTS / 2 )
                        goto next_line; /* a square of >512 distinct floors: enough */
                    keys[slot] = key;
                    distinct++;
                }
                counts[slot]++;
            }
        }
    next_line:
        p = next;
    }

    if( distinct > 0 )
    {
        src->hist = ToriMem_Realloc(
            ToriMem_Editor(), src->hist, (size_t)distinct * 2 * sizeof(*src->hist));
        assert(src->hist);
        for( int slot = 0; slot < SLOTS; slot++ )
        {
            if( !counts[slot] )
                continue;
            src->hist[src->hist_count * 2] = keys[slot];
            src->hist[src->hist_count * 2 + 1] = counts[slot];
            src->hist_count++;
        }
    }
}

/** A .jl2: how many locs it places after its `==== LOC ====` header (one
 *  a line, each starting with its level). */
static void
cat_parse_jl2(
    struct cat_source* src,
    const char* text,
    size_t size,
    int x,
    int z)
{
    const char* p = text;
    const char* end = text + size;
    cat_add_square_contrib(src, x, z);
    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        if( p < le && p[0] >= '0' && p[0] <= '9' )
            src->loc_count++;
        p = next;
    }
    src->has_locs = src->loc_count > 0;
}

static int
cat_parse_ints(
    const char* s,
    const char* e,
    int* out,
    int max)
{
    int n = 0;
    while( s < e && n < max )
    {
        const char* comma = memchr(s, ',', (size_t)(e - s));
        const char* te = comma ? comma : e;
        if( !cat_parse_int(s, te, &out[n]) )
            return -1;
        n++;
        s = comma ? comma + 1 : e;
    }
    return n;
}

static void
cat_claim_add(
    struct cat_source* src,
    uint32_t area,
    int rx0,
    int rz0,
    int rx1,
    int rz1)
{
    struct cat_claim* claim;
    src->claims = cat_grow(src->claims, &src->claim_cap, src->claim_count + 1, sizeof(*src->claims));
    claim = &src->claims[src->claim_count++];
    claim->area = area;
    claim->rx0 = rx0 < rx1 ? rx0 : rx1;
    claim->rx1 = rx0 < rx1 ? rx1 : rx0;
    claim->rz0 = rz0 < rz1 ? rz0 : rz1;
    claim->rz1 = rz0 < rz1 ? rz1 : rz0;
}

/**
 * worldmap/areas/<x>.wma (areas and their sections) and .wmc (composite
 * region lists). Both claim squares by the SOURCE side of each section or
 * region — the game coordinates; the destination is where the world map
 * draws them.
 */
static void
cat_parse_worldmap(
    struct cat_source* src,
    const char* text,
    size_t size,
    int composite)
{
    const char* p = text;
    const char* end = text + size;
    uint32_t area = CAT_NONE;
    int area_index = -1;

    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        if( p < le && p[0] == '[' )
        {
            const char* rb = memchr(p, ']', (size_t)(le - p));
            area = CAT_NONE;
            area_index = -1;
            if( rb && rb > p + 1 )
            {
                area = cat_pool_add(src, p + 1, (size_t)(rb - (p + 1)));
                if( !composite )
                {
                    src->areas = cat_grow(
                        src->areas, &src->area_cap, src->area_count + 1, sizeof(*src->areas));
                    area_index = src->area_count++;
                    src->areas[area_index].internal = area;
                    src->areas[area_index].external = area;
                    src->areas[area_index].is_main = 0;
                }
            }
        }
        else if( area != CAT_NONE )
        {
            const char* eq = memchr(p, '=', (size_t)(le - p));
            if( eq )
            {
                size_t klen = (size_t)(eq - p);
                int v[18];
                if( !composite && area_index >= 0 && klen == 8 && memcmp(p, "external", 8) == 0 )
                    src->areas[area_index].external = cat_pool_add(src, eq + 1, (size_t)(le - (eq + 1)));
                else if( !composite && area_index >= 0 && klen == 4 && memcmp(p, "main", 4) == 0 )
                    src->areas[area_index].is_main = le - eq == 4 && memcmp(eq + 1, "yes", 3) == 0;
                else if( !composite && klen > 7 && memcmp(p, "section", 7) == 0
                         && cat_family_len(p, klen) == 7
                         && cat_parse_ints(eq + 1, le, v, 18) == 18 )
                {
                    /* type, min_plane, planes, src rx, rz, rx_end, rz_end, ... */
                    if( v[0] == 0 )
                        cat_claim_add(src, area, v[3], v[4], v[5], v[6]);
                    else
                        cat_claim_add(src, area, v[3], v[4], v[3], v[4]);
                }
                else if( composite && klen > 6 && memcmp(p, "region", 6) == 0
                         && cat_family_len(p, klen) == 6
                         && cat_parse_ints(eq + 1, le, v, 13) == 13 )
                {
                    /* kind, min_plane, planes, src rx, src rz, ... */
                    cat_claim_add(src, area, v[3], v[4], v[3], v[4]);
                }
            }
        }
        p = next;
    }
}

/** The file's name without directory or extension, into `out`. */
static void
cat_stem(
    const char* path,
    char* out,
    size_t out_size)
{
    const char* base = cat_basename(path);
    const char* dot = strrchr(base, '.');
    size_t length = dot ? (size_t)(dot - base) : strlen(base);
    snprintf(out, out_size, "%.*s", (int)length, base);
}

static void
cat_parse_source(
    struct ToriRSMapEdCatalog* catalog,
    struct cat_source* src)
{
    static const char* const stats_keep[] = { "hitpoints", "attack", "strength", "defence",
        "magic", "ranged", "source", NULL };
    size_t size = 0;
    char* text;
    char stem[CAT_PATH_MAX];
    int x;
    int z;
    size_t max = 0;

    cat_source_clear(src);
    if( src->type == CAT_SRC_NPC_COMBAT )
    {
        /* 16,000 files of death/attack/defend anims that no summary field
         * shows: indexed by name alone, read only by a record view (the
         * whole file is the block). Opening each one was a fifth of the
         * index build. */
        struct cat_contrib* c = cat_contrib_add(src, TORIRSMAPED_CATALOG_NPC, TORIRSMAPED_PROV_STATS);
        cat_stem(src->path, stem, sizeof(stem));
        c->name = cat_pool_str(src, stem);
        c->off = 0;
        c->len = (uint32_t)src->size;
        c->line = 1;
        return;
    }
    if( src->type == CAT_SRC_CS2 || src->type == CAT_SRC_JL2 )
        max = 4096;
    text = cat_read(catalog, src->path, (size_t)src->size, max, &size);
    if( !text )
        return;
    cat_stem(src->path, stem, sizeof(stem));

    switch( src->type )
    {
    case CAT_SRC_PACK:
        cat_parse_idname(src, text, size, src->kind, TORIRSMAPED_PROV_PACK,
            src->kind == TORIRSMAPED_CATALOG_PACK ? stem : NULL);
        break;
    case CAT_SRC_ALLOC:
        cat_parse_idname(src, text, size, src->kind, TORIRSMAPED_PROV_ALLOC, NULL);
        break;
    case CAT_SRC_COMPACK:
        cat_parse_idname(src, text, size, src->kind, TORIRSMAPED_PROV_COMPACK, NULL);
        break;
    case CAT_SRC_MEMBER_CLIENT:
        cat_parse_members(src, text, size, src->kind, TORIRSMAPED_PROV_CLIENT);
        break;
    case CAT_SRC_MEMBER_SERVER:
        cat_parse_members(src, text, size, src->kind, TORIRSMAPED_PROV_SERVER);
        break;
    case CAT_SRC_CONFIG:
        cat_parse_blocks(src, text, size, src->kind, TORIRSMAPED_PROV_CACHE, 0);
        break;
    case CAT_SRC_OVERLAY:
        cat_parse_blocks(src, text, size, src->kind, TORIRSMAPED_PROV_OVERLAY, 1);
        break;
    case CAT_SRC_NPC_STATS:
        cat_parse_keyvalue_file(src, text, size, stem, stats_keep);
        break;
    case CAT_SRC_IF_COMPACK:
        cat_parse_if_compack(src, text, size, stem);
        break;
    case CAT_SRC_IF:
        cat_parse_if(src, text, size, stem);
        break;
    case CAT_SRC_CS2:
        cat_parse_cs2(src, text, size);
        break;
    case CAT_SRC_CS2B:
    {
        struct cat_contrib* c = cat_contrib_add(src, TORIRSMAPED_CATALOG_CS2, TORIRSMAPED_PROV_SCRIPT);
        c->name = cat_pool_str(src, stem);
        c->fields = (uint32_t)src->pool_len;
        cat_pool_str(src, "cs2b");
        cat_pool_str(src, "yes");
        cat_pool_add(src, "", 0);
        break;
    }
    case CAT_SRC_RS2:
        cat_parse_rs2(src, text, size);
        break;
    case CAT_SRC_SPAWN:
        cat_parse_spawn(src, text, size);
        break;
    case CAT_SRC_JM2:
        if( cat_square_name(stem, strlen(stem), &x, &z) )
            cat_parse_jm2(src, text, size, x, z);
        break;
    case CAT_SRC_JL2:
        if( cat_square_name(stem, strlen(stem), &x, &z) )
            cat_parse_jl2(src, text, size, x, z);
        break;
    case CAT_SRC_WMA:
        cat_parse_worldmap(src, text, size, 0);
        break;
    case CAT_SRC_WMC:
        cat_parse_worldmap(src, text, size, 1);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* The scan                                                            */
/* ------------------------------------------------------------------ */

static void
cat_path_index_rebuild(struct ToriRSMapEdCatalog* catalog)
{
    int slots = 1024;
    while( slots < catalog->source_count * 2 )
        slots *= 2;
    if( slots != catalog->path_slot_count )
    {
        ToriMem_Free(ToriMem_Editor(), catalog->path_slots);
        catalog->path_slots = ToriMem_Alloc(ToriMem_Editor(), (size_t)slots * sizeof(int32_t));
        assert(catalog->path_slots);
        catalog->path_slot_count = slots;
    }
    for( int i = 0; i < slots; i++ )
        catalog->path_slots[i] = -1;
    for( int i = 0; i < catalog->source_count; i++ )
    {
        uint32_t slot = catalog->sources[i].hash & (uint32_t)(slots - 1);
        while( catalog->path_slots[slot] >= 0 )
            slot = (slot + 1) & (uint32_t)(slots - 1);
        catalog->path_slots[slot] = i;
    }
}

static int
cat_path_find(
    const struct ToriRSMapEdCatalog* catalog,
    const char* path,
    uint32_t hash)
{
    uint32_t mask;
    uint32_t slot;
    if( catalog->path_slot_count == 0 )
        return -1;
    mask = (uint32_t)(catalog->path_slot_count - 1);
    for( slot = hash & mask; catalog->path_slots[slot] >= 0; slot = (slot + 1) & mask )
    {
        const struct cat_source* src = &catalog->sources[catalog->path_slots[slot]];
        if( src->hash == hash && strcmp(src->path, path) == 0 )
            return catalog->path_slots[slot];
    }
    return -1;
}

/** A file the scan found: known and unchanged, changed, or new. */
static void
cat_found(
    struct ToriRSMapEdCatalog* catalog,
    const char* rel,
    int type,
    int kind,
    const struct stat* st)
{
    uint32_t hash = cat_hash_bytes(rel);
    int index = cat_path_find(catalog, rel, hash);
    int64_t mtime = cat_mtime_ns(st);
    struct cat_source* src;

    if( index >= 0 )
    {
        src = &catalog->sources[index];
        src->seen = 1;
        if( src->mtime_ns != mtime || src->size != (int64_t)st->st_size || src->type != type )
        {
            src->dirty = 1;
            src->mtime_ns = mtime;
            src->size = (int64_t)st->st_size;
            src->type = type;
            src->kind = kind;
        }
        return;
    }

    catalog->sources = cat_grow(
        catalog->sources, &catalog->source_cap, catalog->source_count + 1, sizeof(*catalog->sources));
    src = &catalog->sources[catalog->source_count++];
    memset(src, 0, sizeof(*src));
    src->path = ToriMem_Strdup(ToriMem_Editor(), rel);
    assert(src->path);
    src->hash = hash;
    src->type = type;
    src->kind = kind;
    src->mtime_ns = mtime;
    src->size = (int64_t)st->st_size;
    src->seen = 1;
    src->dirty = 1;
    /* Keep the index usable mid-scan: rebuild when it passes half full. */
    if( catalog->source_count * 2 > catalog->path_slot_count )
        cat_path_index_rebuild(catalog);
    else
    {
        uint32_t mask = (uint32_t)(catalog->path_slot_count - 1);
        uint32_t slot = hash & mask;
        while( catalog->path_slots[slot] >= 0 )
            slot = (slot + 1) & mask;
        catalog->path_slots[slot] = catalog->source_count - 1;
    }
}

/** What a file is, by where it lives and its name. 0 = not a source. */
typedef int (*cat_classify_fn)(
    const char* rel_dir,
    const char* name,
    int* out_type,
    int* out_kind);

static const char*
cat_ext(const char* name)
{
    const char* dot = strrchr(name, '.');
    return dot ? dot + 1 : "";
}

static int
cat_classify_pack(
    const char* rel_dir,
    const char* name,
    int* out_type,
    int* out_kind)
{
    const char* ext = cat_ext(name);
    char stem[256];
    (void)rel_dir;
    snprintf(stem, sizeof(stem), "%.*s", (int)(ext - name - (ext > name ? 1 : 0)), name);
    if( strcmp(ext, "pack") == 0 )
    {
        *out_type = CAT_SRC_PACK;
        *out_kind = cat_pack_kind(stem);
        return 1;
    }
    if( strcmp(ext, "alloc") == 0 || strcmp(ext, "client") == 0 || strcmp(ext, "server") == 0 )
    {
        int kind = cat_config_kind(stem, strlen(stem));
        if( kind < 0 )
            return 0;
        *out_kind = kind;
        *out_type = ext[0] == 'a'   ? CAT_SRC_ALLOC
                    : ext[0] == 'c' ? CAT_SRC_MEMBER_CLIENT
                                    : CAT_SRC_MEMBER_SERVER;
        return 1;
    }
    return 0;
}

static int
cat_classify_configs(
    const char* rel_dir,
    const char* name,
    int* out_type,
    int* out_kind)
{
    const char* type = name + 4;
    const char* dot;
    int kind;
    (void)rel_dir;
    if( strncmp(name, "all.", 4) != 0 )
        return 0;
    dot = strchr(type, '.');
    kind = cat_config_kind(type, dot ? (size_t)(dot - type) : strlen(type));
    if( kind < 0 )
        return 0;
    if( dot && strcmp(dot, ".compack") == 0 )
        *out_type = CAT_SRC_COMPACK;
    else if( !dot )
        *out_type = CAT_SRC_CONFIG;
    else
        return 0;
    *out_kind = kind;
    return 1;
}

static int
cat_classify_scripts_tree(
    const char* rel_dir,
    const char* name,
    int* out_type,
    int* out_kind)
{
    const char* ext = cat_ext(name);
    const char* last_dir = cat_basename(rel_dir);
    *out_kind = -1;
    if( strcmp(ext, "rs2") == 0 )
    {
        *out_type = CAT_SRC_RS2;
        return 1;
    }
    if( strcmp(ext, "spawn") == 0 )
    {
        *out_type = CAT_SRC_SPAWN;
        return 1;
    }
    if( strcmp(last_dir, "configs") == 0 )
    {
        int kind = cat_config_kind(ext, strlen(ext));
        if( kind >= 0 )
        {
            *out_type = CAT_SRC_OVERLAY;
            *out_kind = kind;
            return 1;
        }
    }
    return 0;
}

static int
cat_classify_npc_stats(
    const char* rel_dir,
    const char* name,
    int* out_type,
    int* out_kind)
{
    (void)rel_dir;
    *out_kind = TORIRSMAPED_CATALOG_NPC;
    if( strcmp(cat_ext(name), "stats") == 0 )
    {
        *out_type = CAT_SRC_NPC_STATS;
        return 1;
    }
    if( strcmp(cat_ext(name), "combat") == 0 )
    {
        *out_type = CAT_SRC_NPC_COMBAT;
        return 1;
    }
    return 0;
}

static int
cat_classify_interfaces(
    const char* rel_dir,
    const char* name,
    int* out_type,
    int* out_kind)
{
    (void)rel_dir;
    *out_kind = TORIRSMAPED_CATALOG_INTERFACE;
    if( strcmp(cat_ext(name), "compack") == 0 )
    {
        *out_type = CAT_SRC_IF_COMPACK;
        return 1;
    }
    if( strcmp(cat_ext(name), "if") == 0 )
    {
        *out_type = CAT_SRC_IF;
        return 1;
    }
    return 0;
}

static int
cat_classify_cs2(
    const char* rel_dir,
    const char* name,
    int* out_type,
    int* out_kind)
{
    (void)rel_dir;
    *out_kind = TORIRSMAPED_CATALOG_CS2;
    if( strcmp(cat_ext(name), "cs2") == 0 )
    {
        *out_type = CAT_SRC_CS2;
        return 1;
    }
    if( strcmp(cat_ext(name), "cs2b") == 0 )
    {
        *out_type = CAT_SRC_CS2B;
        return 1;
    }
    return 0;
}

static int
cat_classify_maps(
    const char* rel_dir,
    const char* name,
    int* out_type,
    int* out_kind)
{
    const char* ext = cat_ext(name);
    int x;
    int z;
    (void)rel_dir;
    *out_kind = TORIRSMAPED_CATALOG_SQUARE;
    if( ext == name || !cat_square_name(name, (size_t)(ext - 1 - name), &x, &z) )
        return 0;
    if( strcmp(ext, "jm2") == 0 )
    {
        *out_type = CAT_SRC_JM2;
        return 1;
    }
    if( strcmp(ext, "jl2") == 0 )
    {
        *out_type = CAT_SRC_JL2;
        return 1;
    }
    return 0;
}

static int
cat_classify_worldmap(
    const char* rel_dir,
    const char* name,
    int* out_type,
    int* out_kind)
{
    (void)rel_dir;
    *out_kind = -1;
    if( strcmp(cat_ext(name), "wma") == 0 )
    {
        *out_type = CAT_SRC_WMA;
        return 1;
    }
    if( strcmp(cat_ext(name), "wmc") == 0 )
    {
        *out_type = CAT_SRC_WMC;
        return 1;
    }
    return 0;
}

/**
 * One directory entry of a scan, in `rel_dir`: a source (cat_found has
 * recorded it; 1), a directory (its path in `rel`; 2), or neither (0).
 */
static int
cat_walk_entry(
    struct ToriRSMapEdCatalog* catalog,
    const char* rel_dir,
    const struct dirent* entry,
    cat_classify_fn classify,
    char* rel,
    size_t rel_size)
{
    char full[CAT_PATH_MAX * 2];
    struct stat st;
    int is_dir = -1;
    int type = 0;
    int kind = -1;

    if( entry->d_name[0] == '.' )
        return 0;
    if( (size_t)snprintf(rel, rel_size, "%s/%s", rel_dir, entry->d_name) >= rel_size )
        return 0;
#if defined(DT_DIR)
    if( entry->d_type == DT_DIR )
        is_dir = 1;
    else if( entry->d_type == DT_REG )
        is_dir = 0;
#endif
    if( is_dir == 0 && !classify(rel_dir, entry->d_name, &type, &kind) )
        return 0;
    snprintf(full, sizeof(full), "%s/%s", catalog->content_dir, rel);
    catalog->counters.stats++;
    if( stat(full, &st) != 0 )
        return 0;
    if( is_dir < 0 )
        is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
    if( is_dir )
        return 2;
    if( !classify(rel_dir, entry->d_name, &type, &kind) )
        return 0;
    cat_found(catalog, rel, type, kind, &st);
    return 1;
}

static void
cat_walk(
    struct ToriRSMapEdCatalog* catalog,
    const char* rel_dir,
    int recursive,
    cat_classify_fn classify)
{
    char dir_path[CAT_PATH_MAX * 2];
    DIR* dir;
    struct dirent* entry;

    snprintf(dir_path, sizeof(dir_path), "%s/%s", catalog->content_dir, rel_dir);
    dir = opendir(dir_path);
    if( !dir )
        return;
    while( (entry = readdir(dir)) != NULL )
    {
        char rel[CAT_PATH_MAX];
        if( cat_walk_entry(catalog, rel_dir, entry, classify, rel, sizeof(rel)) == 2 && recursive )
            cat_walk(catalog, rel, recursive, classify);
    }
    closedir(dir);
}

/** What a full scan walks before the maps, in order (cat_scan and the sliced
 *  job read the same table). */
struct cat_root
{
    const char* rel;
    int recursive;
    cat_classify_fn classify;
};

static const struct cat_root k_cat_roots[] = {
    { "pack", 0, cat_classify_pack },
    { "configs", 0, cat_classify_configs },
    { "server/scripts", 1, cat_classify_scripts_tree },
    { "npc_stats", 1, cat_classify_npc_stats },
    { "npc_combat", 1, cat_classify_npc_stats },
    { "interfaces", 0, cat_classify_interfaces },
    { "scripts", 0, cat_classify_cs2 },
    { "worldmap/areas", 0, cat_classify_worldmap },
};
#define CAT_ROOT_COUNT ((int)(sizeof(k_cat_roots) / sizeof(k_cat_roots[0])))

static int
cat_storage_cmp(
    const void* a,
    const void* b)
{
    return strcmp(*(char* const*)a, *(char* const*)b);
}

static void
cat_lanes_free(
    char** lanes,
    int count)
{
    for( int i = 0; i < count; i++ )
        ToriMem_Free(ToriMem_Editor(), lanes[i]);
    ToriMem_Free(ToriMem_Editor(), lanes);
}

static void
cat_lanes_add(
    char*** lanes,
    int* count,
    int* cap,
    const char* storage)
{
    *lanes = cat_grow(*lanes, cap, *count + 1, sizeof(**lanes));
    (*lanes)[*count] = ToriMem_Strdup(ToriMem_Editor(), storage);
    assert((*lanes)[*count]);
    (*count)++;
}

/** "maps", then "ported/<lane>/maps" for every lane directory, in the
 *  order the directory lists them (the order the scan walks them). */
static char**
cat_lanes(
    struct ToriRSMapEdCatalog* catalog,
    int* out_count)
{
    char dir_path[CAT_PATH_MAX * 2];
    DIR* dir;
    struct dirent* entry;
    char** lanes = NULL;
    int count = 0;
    int cap = 0;

    cat_lanes_add(&lanes, &count, &cap, "maps");
    snprintf(dir_path, sizeof(dir_path), "%s/ported", catalog->content_dir);
    dir = opendir(dir_path);
    while( dir && (entry = readdir(dir)) != NULL )
    {
        char full[CAT_PATH_MAX * 2];
        char storage[CAT_PATH_MAX];
        struct stat st;
        if( entry->d_name[0] == '.' )
            continue;
        snprintf(full, sizeof(full), "%s/ported/%s", catalog->content_dir, entry->d_name);
        catalog->counters.stats++;
        if( stat(full, &st) != 0 || !S_ISDIR(st.st_mode) )
            continue;
        snprintf(storage, sizeof(storage), "ported/%s/maps", entry->d_name);
        cat_lanes_add(&lanes, &count, &cap, storage);
    }
    if( dir )
        closedir(dir);
    *out_count = count;
    return lanes;
}

/** A copy of `lanes` as the storage list holds it: "maps", then the lanes
 *  by name. */
static char**
cat_lanes_sorted(
    char* const* lanes,
    int count)
{
    char** sorted = ToriMem_Alloc(ToriMem_Editor(), (size_t)count * sizeof(*sorted));
    assert(sorted);
    for( int i = 0; i < count; i++ )
    {
        sorted[i] = ToriMem_Strdup(ToriMem_Editor(), lanes[i]);
        assert(sorted[i]);
    }
    if( count > 2 )
        qsort(sorted + 1, (size_t)count - 1, sizeof(*sorted), cat_storage_cmp);
    return sorted;
}

static int
cat_lanes_same(
    char* const* a,
    int a_count,
    char* const* b,
    int b_count)
{
    if( a_count != b_count )
        return 0;
    for( int i = 0; i < a_count; i++ )
        if( strcmp(a[i], b[i]) != 0 )
            return 0;
    return 1;
}

/** Adopt `sorted` as the storage list (taking it), noting whether it moved
 *  any square's storage index. */
static void
cat_storages_adopt(
    struct ToriRSMapEdCatalog* catalog,
    char** sorted,
    int count)
{
    catalog->storages_changed = !cat_lanes_same(catalog->storages, catalog->storage_count, sorted, count);
    cat_lanes_free(catalog->storages, catalog->storage_count);
    catalog->storages = sorted;
    catalog->storage_count = count;
}

/** maps/, then every lane's maps/; rebuilds the storage list. */
static void
cat_scan_maps(struct ToriRSMapEdCatalog* catalog)
{
    int count;
    char** lanes = cat_lanes(catalog, &count);

    for( int i = 0; i < count; i++ )
        cat_walk(catalog, lanes[i], 0, cat_classify_maps);
    cat_storages_adopt(catalog, cat_lanes_sorted(lanes, count), count);
    cat_lanes_free(lanes, count);
}

static int
cat_is_maps_type(int type)
{
    return type == CAT_SRC_JM2 || type == CAT_SRC_JL2;
}

static void
cat_scan(
    struct ToriRSMapEdCatalog* catalog,
    int maps_only)
{
    for( int i = 0; i < catalog->source_count; i++ )
        catalog->sources[i].seen = maps_only && !cat_is_maps_type(catalog->sources[i].type);
    if( catalog->path_slot_count == 0 )
        cat_path_index_rebuild(catalog);

    if( !maps_only )
        for( int r = 0; r < CAT_ROOT_COUNT; r++ )
            cat_walk(catalog, k_cat_roots[r].rel, k_cat_roots[r].recursive, k_cat_roots[r].classify);
    cat_scan_maps(catalog);
}

/** Drop sources the scan did not see. Returns how many went. */
static int
cat_compact(struct ToriRSMapEdCatalog* catalog)
{
    int kept = 0;
    int removed = 0;
    for( int i = 0; i < catalog->source_count; i++ )
    {
        if( !catalog->sources[i].seen )
        {
            cat_source_free(&catalog->sources[i]);
            removed++;
            continue;
        }
        if( kept != i )
            catalog->sources[kept] = catalog->sources[i];
        kept++;
    }
    catalog->source_count = kept;
    if( removed )
        cat_path_index_rebuild(catalog);
    return removed;
}

/* ------------------------------------------------------------------ */
/* The merge                                                           */
/* ------------------------------------------------------------------ */

static const char*
cat_cstr(
    const struct cat_source* src,
    uint32_t at)
{
    return at == CAT_NONE ? NULL : src->pool + at;
}

static int
cat_record_new(
    struct ToriRSMapEdCatalog* catalog,
    int kind,
    const char* name)
{
    struct cat_record* rec = &catalog->records[catalog->record_count];
    uint32_t slot;
    memset(rec, 0, sizeof(*rec));
    rec->name = name;
    rec->display = NULL;
    rec->id = -1;
    rec->kind = (uint16_t)kind;
    rec->square = -1;
    rec->first_ref = -1;
    rec->last_ref = -1;
    slot = cat_hash_lower((uint32_t)kind, name) & catalog->hash_mask;
    rec->next_name = catalog->name_heads[slot];
    rec->next_display = -1;
    rec->next_id = -1;
    catalog->name_heads[slot] = catalog->record_count;
    return catalog->record_count++;
}

/** The record (kind, name) whose id agrees with `id` (or has none). */
static int
cat_record_find_merge(
    const struct ToriRSMapEdCatalog* catalog,
    int kind,
    const char* name,
    int32_t id)
{
    uint32_t slot = cat_hash_lower((uint32_t)kind, name) & catalog->hash_mask;
    for( int32_t r = catalog->name_heads[slot]; r >= 0; r = catalog->records[r].next_name )
    {
        const struct cat_record* rec = &catalog->records[r];
        if( rec->kind != kind || strcmp(rec->name, name) != 0 )
            continue;
        if( id < 0 || rec->id < 0 || rec->id == id )
            return r;
    }
    return -1;
}

static void
cat_merge_contrib(
    struct ToriRSMapEdCatalog* catalog,
    int source,
    int index)
{
    struct cat_source* src = &catalog->sources[source];
    struct cat_contrib* c = &src->contribs[index];
    const char* name = cat_cstr(src, c->name);
    struct cat_record* rec;
    struct cat_ref* ref;
    int r;

    if( !name || !c->prov )
        return;
    /* Every trigger header is its own record: two files may both say
     * [opheld1,x] for different lanes, and a record view shows each. */
    r = c->kind == TORIRSMAPED_CATALOG_TRIGGER ? -1
                                               : cat_record_find_merge(catalog, c->kind, name, c->id);
    if( r < 0 )
        r = cat_record_new(catalog, c->kind, name);
    rec = &catalog->records[r];
    rec->prov |= c->prov;
    if( rec->id < 0 && c->id >= 0 )
        rec->id = c->id;
    if( c->display != CAT_NONE )
        rec->display = src->pool + c->display;

    ref = &catalog->refs[catalog->ref_count];
    ref->source = source;
    ref->contrib = index;
    ref->next = -1;
    if( rec->last_ref >= 0 )
        catalog->refs[rec->last_ref].next = catalog->ref_count;
    else
        rec->first_ref = catalog->ref_count;
    rec->last_ref = catalog->ref_count;
    catalog->ref_count++;
}

/** A summary field of a record: the LAST contribution stating it wins, and
 *  contributions are merged cache first, overlays after. */
static const char*
cat_record_field(
    const struct ToriRSMapEdCatalog* catalog,
    int record,
    const char* key)
{
    const char* found = NULL;
    for( int32_t r = catalog->records[record].first_ref; r >= 0; r = catalog->refs[r].next )
    {
        const struct cat_source* src = &catalog->sources[catalog->refs[r].source];
        const struct cat_contrib* c = &src->contribs[catalog->refs[r].contrib];
        const char* k;
        if( c->fields == CAT_NONE )
            continue;
        for( k = src->pool + c->fields; *k; )
        {
            const char* v = k + strlen(k) + 1;
            if( strcmp(k, key) == 0 )
                found = v;
            k = v + strlen(v) + 1;
        }
    }
    return found;
}

static int
cat_find_exact(
    const struct ToriRSMapEdCatalog* catalog,
    int kind,
    const char* name)
{
    uint32_t slot;
    if( !catalog->name_heads )
        return -1;
    slot = cat_hash_lower((uint32_t)kind, name) & catalog->hash_mask;
    for( int32_t r = catalog->name_heads[slot]; r >= 0; r = catalog->records[r].next_name )
    {
        if( catalog->records[r].kind == kind && strcmp(catalog->records[r].name, name) == 0 )
            return r;
    }
    return -1;
}

/** "0xRRGGBB" / decimal -> rgb, or -1 for absent / the 0xFF00FF "no colour". */
static int32_t
cat_colour(const char* value)
{
    long v;
    char* end;
    if( !value )
        return -1;
    v = strtol(value, &end, 0);
    if( end == value || v < 0 || v > 0xFFFFFF || v == 0xFF00FF )
        return -1;
    return (int32_t)v;
}

static int
cat_square_storage_index(
    const struct ToriRSMapEdCatalog* catalog,
    const char* key)
{
    const char* slash = strrchr(key, '/');
    if( !slash )
        return 0;
    for( int i = 1; i < catalog->storage_count; i++ )
    {
        size_t length = strlen(catalog->storages[i]);
        if( (size_t)(slash - key) == length && strncmp(catalog->storages[i], key, length) == 0 )
            return i;
    }
    return -1;
}

static void
cat_region_claim(
    struct ToriRSMapEdCatalog* catalog,
    int area,
    int rx,
    int rz,
    int* node_cap)
{
    int32_t index;
    if( rx < 0 || rz < 0 || rx >= CAT_REGION_SPAN || rz >= CAT_REGION_SPAN )
        return;
    index = rx * CAT_REGION_SPAN + rz;
    for( int32_t n = catalog->region_heads[index]; n >= 0; n = catalog->region_nodes[n * 2 + 1] )
    {
        if( catalog->region_nodes[n * 2] == area )
            return;
    }
    catalog->region_nodes = cat_grow(
        catalog->region_nodes, node_cap, (catalog->region_node_count + 1) * 2, sizeof(int32_t));
    catalog->region_nodes[catalog->region_node_count * 2] = area;
    catalog->region_nodes[catalog->region_node_count * 2 + 1] = catalog->region_heads[index];
    catalog->region_heads[index] = catalog->region_node_count;
    catalog->region_node_count++;
    catalog->areas[area].claimed++;
}

/** Areas claiming (x, z) into `out` (most specific first). Returns count. */
static int
cat_square_area_list(
    const struct ToriRSMapEdCatalog* catalog,
    int x,
    int z,
    int* out,
    int max)
{
    int n = 0;
    if( !catalog->region_heads || x < 0 || z < 0 || x >= CAT_REGION_SPAN || z >= CAT_REGION_SPAN )
        return 0;
    for( int32_t node = catalog->region_heads[x * CAT_REGION_SPAN + z]; node >= 0 && n < max;
         node = catalog->region_nodes[node * 2 + 1] )
    {
        int area = catalog->region_nodes[node * 2];
        int at = n++;
        /* insertion by claim size: the dungeon that claims 12 regions is a
         * better name for a square than the surface that claims 1,600 */
        while( at > 0 && catalog->areas[out[at - 1]].claimed > catalog->areas[area].claimed )
        {
            out[at] = out[at - 1];
            at--;
        }
        out[at] = area;
    }
    return n;
}

static void
cat_merge_areas(struct ToriRSMapEdCatalog* catalog)
{
    int area_cap = 0;
    int node_cap = 0;

    ToriMem_Free(ToriMem_Editor(), catalog->areas);
    catalog->areas = NULL;
    catalog->area_count = 0;
    for( int s = 0; s < catalog->source_count; s++ )
    {
        const struct cat_source* src = &catalog->sources[s];
        if( src->type != CAT_SRC_WMA )
            continue;
        for( int a = 0; a < src->area_count; a++ )
        {
            catalog->areas = cat_grow(catalog->areas, &area_cap, catalog->area_count + 1, sizeof(*catalog->areas));
            catalog->areas[catalog->area_count].internal = src->pool + src->areas[a].internal;
            catalog->areas[catalog->area_count].external = src->pool + src->areas[a].external;
            catalog->areas[catalog->area_count].is_main = src->areas[a].is_main;
            catalog->areas[catalog->area_count].claimed = 0;
            catalog->area_count++;
        }
    }

    if( !catalog->region_heads )
    {
        catalog->region_heads = ToriMem_Alloc(
            ToriMem_Editor(), (size_t)CAT_REGION_SPAN * CAT_REGION_SPAN * sizeof(int32_t));
        assert(catalog->region_heads);
    }
    for( int i = 0; i < CAT_REGION_SPAN * CAT_REGION_SPAN; i++ )
        catalog->region_heads[i] = -1;
    ToriMem_Free(ToriMem_Editor(), catalog->region_nodes);
    catalog->region_nodes = NULL;
    catalog->region_node_count = 0;

    for( int s = 0; s < catalog->source_count; s++ )
    {
        const struct cat_source* src = &catalog->sources[s];
        if( src->type != CAT_SRC_WMA && src->type != CAT_SRC_WMC )
            continue;
        for( int c = 0; c < src->claim_count; c++ )
        {
            const struct cat_claim* claim = &src->claims[c];
            const char* name = src->pool + claim->area;
            int area = -1;
            for( int a = 0; a < catalog->area_count; a++ )
            {
                if( strcmp(catalog->areas[a].internal, name) == 0 )
                {
                    area = a;
                    break;
                }
            }
            if( area < 0 )
                continue;
            for( int rx = claim->rx0; rx <= claim->rx1 && rx < CAT_REGION_SPAN; rx++ )
            {
                for( int rz = claim->rz0; rz <= claim->rz1 && rz < CAT_REGION_SPAN; rz++ )
                    cat_region_claim(catalog, area, rx, rz, &node_cap);
            }
        }
    }
}

static uint32_t
cat_square_rgb(
    const struct cat_source* jm2,
    const int32_t* ul_rgb,
    const int32_t* ov_rgb,
    int ov_count)
{
    uint64_t r = 0;
    uint64_t g = 0;
    uint64_t b = 0;
    uint64_t n = 0;
    for( int i = 0; i < jm2->hist_count; i++ )
    {
        uint32_t key = jm2->hist[i * 2];
        uint32_t count = jm2->hist[i * 2 + 1];
        int ov = (int)(key >> 8);
        int ul = (int)(key & 0xFF);
        int32_t rgb = -1;
        if( ov > 0 && ov - 1 < ov_count )
            rgb = ov_rgb[ov - 1];
        if( rgb < 0 && ul > 0 )
            rgb = ul_rgb[ul - 1];
        if( rgb < 0 )
            continue;
        r += (uint64_t)((rgb >> 16) & 0xFF) * count;
        g += (uint64_t)((rgb >> 8) & 0xFF) * count;
        b += (uint64_t)(rgb & 0xFF) * count;
        n += count;
    }
    if( !n )
        return 0;
    return (uint32_t)(((r / n) << 16) | ((g / n) << 8) | (b / n));
}

static void
cat_merge_atlas(struct ToriRSMapEdCatalog* catalog)
{
    int32_t ul_rgb[256];
    int32_t* ov_rgb = NULL;
    int ov_count = 0;
    int square_cap = 0;
    int area_list_cap = 0;

    for( int i = 0; i < 256; i++ )
        ul_rgb[i] = -1;
    for( int r = 0; r < catalog->record_count; r++ )
    {
        const struct cat_record* rec = &catalog->records[r];
        if( rec->kind == TORIRSMAPED_CATALOG_OVERLAY && rec->id >= ov_count )
            ov_count = rec->id + 1;
    }
    if( ov_count > 0 )
    {
        ov_rgb = ToriMem_Alloc(ToriMem_Editor(), (size_t)ov_count * sizeof(*ov_rgb));
        assert(ov_rgb);
        for( int i = 0; i < ov_count; i++ )
            ov_rgb[i] = -1;
    }
    for( int r = 0; r < catalog->record_count; r++ )
    {
        const struct cat_record* rec = &catalog->records[r];
        if( rec->id < 0 )
            continue;
        if( rec->kind == TORIRSMAPED_CATALOG_UNDERLAY && rec->id < 256 )
            ul_rgb[rec->id] = cat_colour(cat_record_field(catalog, r, "colour"));
        else if( rec->kind == TORIRSMAPED_CATALOG_OVERLAY )
        {
            /* A textured floor has no colour of its own; its blend colour is
             * what the minimap shows. Neither: the underlay shows through. */
            int32_t rgb = cat_colour(cat_record_field(catalog, r, "colour"));
            if( rgb < 0 )
                rgb = cat_colour(cat_record_field(catalog, r, "blendcolour"));
            ov_rgb[rec->id] = rgb;
        }
    }

    if( !catalog->spawn_counts )
    {
        catalog->spawn_counts = ToriMem_Alloc(
            ToriMem_Editor(), (size_t)CAT_REGION_SPAN * CAT_REGION_SPAN * sizeof(int));
        assert(catalog->spawn_counts);
    }
    memset(catalog->spawn_counts, 0, (size_t)CAT_REGION_SPAN * CAT_REGION_SPAN * sizeof(int));
    for( int s = 0; s < catalog->source_count; s++ )
    {
        const struct cat_source* src = &catalog->sources[s];
        if( src->type != CAT_SRC_SPAWN )
            continue;
        for( int i = 0; i < src->contrib_count; i++ )
        {
            int x = (src->contribs[i].id >> 8) & 0xFF;
            int z = src->contribs[i].id & 0xFF;
            catalog->spawn_counts[x * CAT_REGION_SPAN + z] += src->contribs[i].value;
        }
    }

    ToriMem_Free(ToriMem_Editor(), catalog->squares);
    ToriMem_Free(ToriMem_Editor(), catalog->square_areas);
    catalog->squares = NULL;
    catalog->square_areas = NULL;
    catalog->square_count = 0;
    catalog->square_area_count = 0;

    for( int r = 0; r < catalog->record_count; r++ )
    {
        struct cat_record* rec = &catalog->records[r];
        struct cat_square* sq;
        int areas[64];
        int area_n;
        int x;
        int z;
        int storage;

        if( rec->kind != TORIRSMAPED_CATALOG_SQUARE || !(rec->prov & TORIRSMAPED_PROV_MAP) )
            continue;
        if( !rec->display || !cat_square_name(rec->display, strlen(rec->display), &x, &z) )
            continue;
        storage = cat_square_storage_index(catalog, rec->name);
        if( storage < 0 )
            continue;
        catalog->squares = cat_grow(
            catalog->squares, &square_cap, catalog->square_count + 1, sizeof(*catalog->squares));
        sq = &catalog->squares[catalog->square_count];
        memset(sq, 0, sizeof(*sq));
        sq->map_x = x;
        sq->map_z = z;
        sq->storage = storage;
        sq->record = r;
        sq->spawn_count = catalog->spawn_counts[x * CAT_REGION_SPAN + z];
        for( int32_t ref = rec->first_ref; ref >= 0; ref = catalog->refs[ref].next )
        {
            const struct cat_source* src = &catalog->sources[catalog->refs[ref].source];
            if( src->type == CAT_SRC_JM2 )
            {
                sq->flags |= TORIRSMAPED_SQUARE_HAS_JM2;
                sq->rgb = cat_square_rgb(src, ul_rgb, ov_rgb, ov_count);
            }
            else if( src->type == CAT_SRC_JL2 )
            {
                sq->flags |= TORIRSMAPED_SQUARE_HAS_JL2;
                if( src->has_locs )
                    sq->flags |= TORIRSMAPED_SQUARE_HAS_LOCS;
                sq->loc_count = src->loc_count;
            }
        }
        area_n = cat_square_area_list(catalog, x, z, areas, 64);
        sq->primary_area = area_n > 0 ? areas[0] : -1;
        if( area_n == 0 && ToriRSMapEdCatalog_InInstanceSpace(x, z) )
            sq->flags |= TORIRSMAPED_SQUARE_INSTANCE;
        sq->area_first = catalog->square_area_count;
        sq->area_n = area_n;
        catalog->square_areas = cat_grow(catalog->square_areas, &area_list_cap,
            catalog->square_area_count + area_n + 1, sizeof(*catalog->square_areas));
        memcpy(catalog->square_areas + catalog->square_area_count, areas, (size_t)area_n * sizeof(int));
        catalog->square_area_count += area_n;
        rec->square = catalog->square_count;
        catalog->square_count++;
    }
    ToriMem_Free(ToriMem_Editor(), ov_rgb);
}

/** Facts that need the whole index: npcs with a Talk-to script, vars read
 *  by multi shells. */
static void
cat_merge_derived(struct ToriRSMapEdCatalog* catalog)
{
    for( int r = 0; r < catalog->record_count; r++ )
    {
        struct cat_record* rec = &catalog->records[r];
        if( rec->kind == TORIRSMAPED_CATALOG_TRIGGER )
        {
            const char* comma = strchr(rec->name, ',');
            if( comma && (size_t)(comma - rec->name) == 6 && memcmp(rec->name, "opnpc1", 6) == 0 )
            {
                int npc = cat_find_exact(catalog, TORIRSMAPED_CATALOG_NPC, comma + 1);
                if( npc >= 0 )
                    catalog->records[npc].flags |= CAT_REC_DIALOGUE;
            }
        }
        else if( rec->kind == TORIRSMAPED_CATALOG_LOC || rec->kind == TORIRSMAPED_CATALOG_NPC )
        {
            const char* var = cat_record_field(catalog, r, "multivar");
            if( var )
            {
                int target = cat_find_exact(catalog, TORIRSMAPED_CATALOG_VARBIT, var);
                if( target < 0 )
                    target = cat_find_exact(catalog, TORIRSMAPED_CATALOG_VARP, var);
                if( target >= 0 )
                    catalog->records[target].users++;
            }
        }
    }
}

static void
cat_merge(struct ToriRSMapEdCatalog* catalog)
{
    /* Ids first (they name records), then cache blocks, then everything that
     * overrides or annotates them — the order a field lookup's "last one
     * wins" relies on. */
    static const int order[][5] = {
        { CAT_SRC_COMPACK, CAT_SRC_PACK, CAT_SRC_ALLOC, CAT_SRC_IF_COMPACK, CAT_SRC_CS2 },
        { CAT_SRC_CONFIG, CAT_SRC_JM2, CAT_SRC_JL2, CAT_SRC_IF, CAT_SRC_CS2B },
        { CAT_SRC_OVERLAY, CAT_SRC_NPC_STATS, CAT_SRC_NPC_COMBAT, CAT_SRC_MEMBER_CLIENT,
            CAT_SRC_MEMBER_SERVER },
        { CAT_SRC_RS2, -1, -1, -1, -1 },
    };
    int total = 0;
    uint32_t slots = 1024;

    for( int s = 0; s < catalog->source_count; s++ )
        total += catalog->sources[s].contrib_count;
    while( slots < (uint32_t)total * 2 )
        slots *= 2;

    ToriMem_Free(ToriMem_Editor(), catalog->records);
    ToriMem_Free(ToriMem_Editor(), catalog->refs);
    ToriMem_Free(ToriMem_Editor(), catalog->name_heads);
    ToriMem_Free(ToriMem_Editor(), catalog->display_heads);
    ToriMem_Free(ToriMem_Editor(), catalog->id_heads);
    catalog->records = ToriMem_Alloc(ToriMem_Editor(), (size_t)(total + 1) * sizeof(*catalog->records));
    catalog->refs = ToriMem_Alloc(ToriMem_Editor(), (size_t)(total + 1) * sizeof(*catalog->refs));
    catalog->name_heads = ToriMem_Alloc(ToriMem_Editor(), slots * sizeof(int32_t));
    catalog->display_heads = ToriMem_Alloc(ToriMem_Editor(), slots * sizeof(int32_t));
    catalog->id_heads = ToriMem_Alloc(ToriMem_Editor(), slots * sizeof(int32_t));
    assert(catalog->records);
    assert(catalog->refs);
    assert(catalog->name_heads);
    assert(catalog->display_heads);
    assert(catalog->id_heads);
    catalog->hash_mask = slots - 1;
    catalog->record_count = 0;
    catalog->ref_count = 0;
    for( uint32_t i = 0; i < slots; i++ )
    {
        catalog->name_heads[i] = -1;
        catalog->display_heads[i] = -1;
        catalog->id_heads[i] = -1;
    }

    for( size_t pass = 0; pass < sizeof(order) / sizeof(order[0]); pass++ )
    {
        for( int s = 0; s < catalog->source_count; s++ )
        {
            int type = catalog->sources[s].type;
            int in_pass = 0;
            for( int t = 0; t < 5; t++ )
                in_pass |= order[pass][t] == type;
            if( !in_pass )
                continue;
            for( int i = 0; i < catalog->sources[s].contrib_count; i++ )
                cat_merge_contrib(catalog, s, i);
        }
    }

    for( int r = 0; r < catalog->record_count; r++ )
    {
        struct cat_record* rec = &catalog->records[r];
        if( rec->display )
        {
            uint32_t slot = cat_hash_lower(rec->kind, rec->display) & catalog->hash_mask;
            rec->next_display = catalog->display_heads[slot];
            catalog->display_heads[slot] = r;
        }
        if( rec->id >= 0 )
        {
            uint32_t slot = ((uint32_t)rec->id * 2654435761u ^ rec->kind * 40503u) & catalog->hash_mask;
            rec->next_id = catalog->id_heads[slot];
            catalog->id_heads[slot] = r;
        }
    }

    cat_merge_derived(catalog);
    cat_merge_areas(catalog);
    cat_merge_atlas(catalog);
}

/* ------------------------------------------------------------------ */
/* Lifecycle and refresh                                               */
/* ------------------------------------------------------------------ */

struct ToriRSMapEdCatalog*
ToriRSMapEdCatalog_Create(const char* content_dir)
{
    struct ToriRSMapEdCatalog* catalog;
    assert(content_dir);
    catalog = ToriMem_Calloc(ToriMem_Editor(), 1, sizeof(*catalog));
    assert(catalog);
    snprintf(catalog->content_dir, sizeof(catalog->content_dir), "%s", content_dir);
    catalog->refresh_interval_s = 2;
    return catalog;
}

static void
cat_job_abort(struct ToriRSMapEdCatalog* catalog);

void
ToriRSMapEdCatalog_Free(struct ToriRSMapEdCatalog* catalog)
{
    if( !catalog )
        return;
    cat_job_abort(catalog);
    ToriMem_Free(ToriMem_Editor(), catalog->held.hits);
    for( int i = 0; i < catalog->source_count; i++ )
        cat_source_free(&catalog->sources[i]);
    for( int i = 0; i < catalog->storage_count; i++ )
        ToriMem_Free(ToriMem_Editor(), catalog->storages[i]);
    ToriMem_Free(ToriMem_Editor(), catalog->storages);
    ToriMem_Free(ToriMem_Editor(), catalog->sources);
    ToriMem_Free(ToriMem_Editor(), catalog->path_slots);
    ToriMem_Free(ToriMem_Editor(), catalog->records);
    ToriMem_Free(ToriMem_Editor(), catalog->refs);
    ToriMem_Free(ToriMem_Editor(), catalog->name_heads);
    ToriMem_Free(ToriMem_Editor(), catalog->display_heads);
    ToriMem_Free(ToriMem_Editor(), catalog->id_heads);
    ToriMem_Free(ToriMem_Editor(), catalog->areas);
    ToriMem_Free(ToriMem_Editor(), catalog->squares);
    ToriMem_Free(ToriMem_Editor(), catalog->square_areas);
    ToriMem_Free(ToriMem_Editor(), catalog->region_heads);
    ToriMem_Free(ToriMem_Editor(), catalog->region_nodes);
    ToriMem_Free(ToriMem_Editor(), catalog->spawn_counts);
    ToriMem_Free(ToriMem_Editor(), catalog->scratch);
    ToriMem_Free(ToriMem_Editor(), catalog);
}

/**
 * After a scan: drop what it did not see (`compact`), parse every dirty
 * source, and merge when anything changed. `full_pass` restarts the refresh
 * interval. The scan's time is already in `stats`.
 */
static int
cat_finish(
    struct ToriRSMapEdCatalog* catalog,
    int compact,
    int full_pass,
    struct ToriRSMapEdCatalogStats* stats)
{
    double const t1 = cat_now_ms();
    double t2;
    int changed;

    int maps_changed = catalog->storages_changed;

    if( compact )
    {
        for( int i = 0; i < catalog->source_count && !maps_changed; i++ )
            maps_changed = !catalog->sources[i].seen && cat_is_maps_type(catalog->sources[i].type);
        stats->removed = cat_compact(catalog);
    }
    for( int i = 0; i < catalog->source_count && !maps_changed; i++ )
        maps_changed = catalog->sources[i].dirty && cat_is_maps_type(catalog->sources[i].type);
    {
        /* TORIRS_CATALOG_TRACE=1: where the parse time went, by source type */
        static const char* const type_names[] = { "pack", "alloc", "client", "server",
            "compack", "config", "overlay", "npc_stats", "npc_combat", "if_compack", "if",
            "cs2", "cs2b", "rs2", "spawn", "jm2", "jl2", "wma", "wmc" };
        double type_ms[sizeof(type_names) / sizeof(type_names[0])];
        int type_n[sizeof(type_names) / sizeof(type_names[0])];
        const char* trace = getenv("TORIRS_CATALOG_TRACE");
        memset(type_ms, 0, sizeof(type_ms));
        memset(type_n, 0, sizeof(type_n));
        for( int i = 0; i < catalog->source_count; i++ )
        {
            double at;
            int type = catalog->sources[i].type;
            if( !catalog->sources[i].dirty )
                continue;
            at = trace ? cat_now_ms() : 0.0;
            cat_parse_source(catalog, &catalog->sources[i]);
            catalog->sources[i].dirty = 0;
            stats->parsed++;
            if( trace && type >= 0 && (size_t)type < sizeof(type_names) / sizeof(type_names[0]) )
            {
                type_ms[type] += cat_now_ms() - at;
                type_n[type]++;
            }
        }
        for( size_t t = 0; trace && t < sizeof(type_names) / sizeof(type_names[0]); t++ )
        {
            if( type_n[t] )
                fprintf(stderr, "catalog: parse %-10s %6d files %8.1f ms\n", type_names[t], type_n[t],
                    type_ms[t]);
        }
    }
    t2 = cat_now_ms();
    changed = !catalog->built || stats->parsed > 0 || stats->removed > 0 || catalog->storages_changed;
    if( changed )
    {
        if( catalog->built && maps_changed )
            catalog->maps_revision++;
        cat_merge(catalog);
        catalog->revision++;
        catalog->counters.merges++;
    }
    catalog->storages_changed = 0;
    catalog->built = 1;
    if( full_pass )
    {
        catalog->invalid = 0;
        catalog->last_refresh = time(NULL);
    }
    stats->sources = catalog->source_count;
    stats->records = catalog->record_count;
    stats->squares = catalog->square_count;
    stats->parse_ms += t2 - t1;
    stats->merge_ms = cat_now_ms() - t2;
    /* all.loc alone is 86 MB: do not keep its buffer for the server's life */
    if( catalog->scratch_cap > 4 * 1024 * 1024 )
    {
        ToriMem_Free(ToriMem_Editor(), catalog->scratch);
        catalog->scratch = NULL;
        catalog->scratch_cap = 0;
    }
    return changed;
}

/** The whole refresh at once: what Refresh, RefreshMaps and a request
 *  that finds no index run. Ends any job in flight (this scan covers it). */
static int
cat_refresh(
    struct ToriRSMapEdCatalog* catalog,
    int maps_only,
    struct ToriRSMapEdCatalogStats* out_stats)
{
    struct ToriRSMapEdCatalogStats stats;
    double const t0 = cat_now_ms();
    int changed;

    cat_job_abort(catalog);
    memset(&stats, 0, sizeof(stats));
    cat_scan(catalog, maps_only);
    stats.scan_ms = cat_now_ms() - t0;
    changed = cat_finish(catalog, 1, !maps_only, &stats);
    if( out_stats )
        *out_stats = stats;
    return changed;
}

/* ---- the background job: the warm build and the freshness sweeps ---- */

static void
cat_job_abort(struct ToriRSMapEdCatalog* catalog)
{
    struct cat_job* job = &catalog->job;
    while( job->depth > 0 )
        closedir(job->stack[--job->depth].dir);
    cat_lanes_free(job->lanes, job->lane_count);
    job->lanes = NULL;
    job->lane_count = 0;
    job->phase = CAT_JOB_IDLE;
}

/**
 * Start a pass: the warm build when there is no index, else a freshness
 * sweep. A sweep that finds the lane directories changed (a lane came or
 * went: every square's storage index may move) runs the whole refresh at
 * once instead, and the job stays idle.
 */
static void
cat_job_begin(struct ToriRSMapEdCatalog* catalog)
{
    struct cat_job* job = &catalog->job;
    char** sorted;

    assert(job->phase == CAT_JOB_IDLE);
    memset(&job->stats, 0, sizeof(job->stats));
    job->root = 0;
    job->depth = 0;
    job->parse_at = 0;
    for( int i = 0; i < catalog->source_count; i++ )
        catalog->sources[i].seen = 0;
    if( catalog->path_slot_count == 0 )
        cat_path_index_rebuild(catalog);
    job->lanes = cat_lanes(catalog, &job->lane_count);
    sorted = cat_lanes_sorted(job->lanes, job->lane_count);
    if( !catalog->built )
        cat_storages_adopt(catalog, sorted, job->lane_count);
    else if( !cat_lanes_same(catalog->storages, catalog->storage_count, sorted, job->lane_count) )
    {
        cat_lanes_free(sorted, job->lane_count);
        (void)cat_refresh(catalog, 0, NULL);
        return;
    }
    else
        cat_lanes_free(sorted, job->lane_count);
    job->phase = CAT_JOB_SCAN;
}

/** Advance the job until `deadline_ms` (0: to its end). Returns 1 when the
 *  pass finished. */
static int
cat_job_step(
    struct ToriRSMapEdCatalog* catalog,
    double deadline_ms)
{
    struct cat_job* job = &catalog->job;
    double const t0 = cat_now_ms();
    unsigned n = 0;

    if( job->phase == CAT_JOB_SCAN )
    {
        for( ;; )
        {
            struct cat_job_dir* top;
            struct dirent* entry;
            char rel[CAT_PATH_MAX];
            int recursive;
            cat_classify_fn classify;
            int got;

            if( deadline_ms > 0.0 && (++n & 15u) == 0 && cat_now_ms() >= deadline_ms )
            {
                job->stats.scan_ms += cat_now_ms() - t0;
                return 0;
            }
            if( job->depth == 0 )
            {
                char dir_path[CAT_PATH_MAX * 2];
                const char* root_rel;
                DIR* dir;
                if( job->root >= CAT_ROOT_COUNT + job->lane_count )
                    break;
                root_rel = job->root < CAT_ROOT_COUNT ? k_cat_roots[job->root].rel
                                                      : job->lanes[job->root - CAT_ROOT_COUNT];
                snprintf(dir_path, sizeof(dir_path), "%s/%s", catalog->content_dir, root_rel);
                dir = opendir(dir_path);
                if( dir )
                {
                    job->stack[0].dir = dir;
                    job->stack[0].root = job->root;
                    snprintf(job->stack[0].rel, sizeof(job->stack[0].rel), "%s", root_rel);
                    job->depth = 1;
                }
                job->root++;
                continue;
            }
            top = &job->stack[job->depth - 1];
            entry = readdir(top->dir);
            if( !entry )
            {
                closedir(top->dir);
                job->depth--;
                continue;
            }
            recursive = top->root < CAT_ROOT_COUNT && k_cat_roots[top->root].recursive;
            classify = top->root < CAT_ROOT_COUNT ? k_cat_roots[top->root].classify : cat_classify_maps;
            got = cat_walk_entry(catalog, top->rel, entry, classify, rel, sizeof(rel));
            if( got != 2 || !recursive )
                continue;
            if( job->depth < CAT_JOB_DEPTH )
            {
                char dir_path[CAT_PATH_MAX * 2];
                DIR* dir;
                snprintf(dir_path, sizeof(dir_path), "%s/%s", catalog->content_dir, rel);
                dir = opendir(dir_path);
                if( dir )
                {
                    struct cat_job_dir* below = &job->stack[job->depth++];
                    below->dir = dir;
                    below->root = top->root;
                    snprintf(below->rel, sizeof(below->rel), "%s", rel);
                }
            }
            else
                cat_walk(catalog, rel, 1, classify);
        }
        job->stats.scan_ms += cat_now_ms() - t0;
        job->phase = catalog->built ? CAT_JOB_IDLE : CAT_JOB_PARSE;
    }
    if( job->phase == CAT_JOB_PARSE )
    {
        double const t1 = cat_now_ms();
        while( job->parse_at < catalog->source_count )
        {
            struct cat_source* src = &catalog->sources[job->parse_at++];
            if( !src->dirty )
                continue;
            cat_parse_source(catalog, src);
            src->dirty = 0;
            job->stats.parsed++;
            if( deadline_ms > 0.0 && cat_now_ms() >= deadline_ms )
            {
                job->stats.parse_ms += cat_now_ms() - t1;
                return 0;
            }
        }
        job->stats.parse_ms += cat_now_ms() - t1;
    }
    /* The scan is whole: drop what it did not see, parse (a sweep's) changes
     * and merge -- one step, so no request sees half of it. */
    cat_job_abort(catalog);
    (void)cat_finish(catalog, 1, 1, &job->stats);
    catalog->counters.sweeps++;
    if( getenv("TORIRS_CATALOG_TRACE") )
        fprintf(stderr, "catalog: pass done: scan %.1f ms, %d parsed (%.1f ms), %d removed, merge %.1f ms\n",
            job->stats.scan_ms, job->stats.parsed, job->stats.parse_ms, job->stats.removed, job->stats.merge_ms);
    return 1;
}

int
ToriRSMapEdCatalog_Service(
    struct ToriRSMapEdCatalog* catalog,
    double budget_ms)
{
    double const t0 = cat_now_ms();
    double took;
    int done;

    assert(catalog);
    assert(budget_ms > 0.0);
    /* The warm build starts on its own; a sweep only when a request asked. */
    if( catalog->job.phase == CAT_JOB_IDLE && !catalog->built )
        cat_job_begin(catalog);
    if( catalog->job.phase == CAT_JOB_IDLE )
        return 0;
    done = cat_job_step(catalog, t0 + budget_ms);
    took = cat_now_ms() - t0;
    catalog->counters.slices++;
    if( took > catalog->counters.slice_max_ms )
        catalog->counters.slice_max_ms = took;
    return !done;
}

uint32_t
ToriRSMapEdCatalog_MapsRevision(const struct ToriRSMapEdCatalog* catalog)
{
    assert(catalog);
    return catalog->maps_revision;
}

int
ToriRSMapEdCatalog_Working(const struct ToriRSMapEdCatalog* catalog)
{
    assert(catalog);
    return catalog->job.phase != CAT_JOB_IDLE;
}

void
ToriRSMapEdCatalog_Counters(
    const struct ToriRSMapEdCatalog* catalog,
    struct ToriRSMapEdCatalogCounters* out)
{
    assert(catalog);
    assert(out);
    *out = catalog->counters;
}

/**
 * What every request runs first: an index to answer from. Only a server with
 * no index yet waits (for the warm build to finish, or for a whole build);
 * otherwise, once the interval has passed (or a save asked), a sweep starts
 * for the service pass to advance, and the request answers from the index
 * as it stands.
 */
static void
cat_request_index(struct ToriRSMapEdCatalog* catalog)
{
    if( !catalog->built )
    {
        if( catalog->job.phase != CAT_JOB_IDLE )
            (void)cat_job_step(catalog, 0.0);
        if( !catalog->built )
            (void)cat_refresh(catalog, 0, NULL);
        return;
    }
    if( catalog->job.phase == CAT_JOB_IDLE
        && (catalog->invalid || time(NULL) - catalog->last_refresh >= catalog->refresh_interval_s) )
        cat_job_begin(catalog);
}

int
ToriRSMapEdCatalog_Refresh(
    struct ToriRSMapEdCatalog* catalog,
    int force,
    struct ToriRSMapEdCatalogStats* out_stats)
{
    assert(catalog);
    if( !force && catalog->built && !catalog->invalid
        && time(NULL) - catalog->last_refresh < catalog->refresh_interval_s )
    {
        if( out_stats )
            memset(out_stats, 0, sizeof(*out_stats));
        return 0;
    }
    return cat_refresh(catalog, 0, out_stats);
}

int
ToriRSMapEdCatalog_RefreshMaps(struct ToriRSMapEdCatalog* catalog)
{
    assert(catalog);
    if( !catalog->built )
        return cat_refresh(catalog, 0, NULL);
    /* Whatever else the save wrote (a spawn file, a config block) is caught
     * by the next query's full refresh. */
    catalog->invalid = 1;
    return cat_refresh(catalog, 1, NULL);
}

int
ToriRSMapEdCatalog_Built(const struct ToriRSMapEdCatalog* catalog)
{
    assert(catalog);
    return catalog->built;
}

void
ToriRSMapEdCatalog_SetRefreshInterval(
    struct ToriRSMapEdCatalog* catalog,
    int seconds)
{
    assert(catalog);
    assert(seconds >= 0);
    catalog->refresh_interval_s = seconds;
}

/* ------------------------------------------------------------------ */
/* Fields of a merged record                                           */
/* ------------------------------------------------------------------ */

struct cat_fields
{
    int n;
    const char* key[CAT_FIELDS_MAX];
    const char* value[CAT_FIELDS_MAX];
    char scratch[1024];
    size_t scratch_len;
};

static void
cat_fields_put(
    struct cat_fields* fields,
    const char* key,
    const char* value)
{
    for( int i = 0; i < fields->n; i++ )
    {
        if( strcmp(fields->key[i], key) == 0 )
        {
            fields->value[i] = value;
            return;
        }
    }
    if( fields->n < CAT_FIELDS_MAX )
    {
        fields->key[fields->n] = key;
        fields->value[fields->n] = value;
        fields->n++;
    }
}

static const char*
cat_fields_scratch(
    struct cat_fields* fields,
    const char* text)
{
    size_t length = strlen(text);
    char* at;
    if( fields->scratch_len + length + 1 > sizeof(fields->scratch) )
        return "";
    at = fields->scratch + fields->scratch_len;
    memcpy(at, text, length + 1);
    fields->scratch_len += length + 1;
    return at;
}

/** The record's summary: every contribution's fields, later ones winning,
 *  then the fields only the merge knows. */
static void
cat_collect(
    const struct ToriRSMapEdCatalog* catalog,
    int record,
    struct cat_fields* fields)
{
    const struct cat_record* rec = &catalog->records[record];
    char number[32];

    fields->n = 0;
    fields->scratch_len = 0;
    for( int32_t r = rec->first_ref; r >= 0; r = catalog->refs[r].next )
    {
        const struct cat_source* src = &catalog->sources[catalog->refs[r].source];
        const struct cat_contrib* c = &src->contribs[catalog->refs[r].contrib];
        const char* k;
        if( c->fields == CAT_NONE )
            continue;
        for( k = src->pool + c->fields; *k; )
        {
            const char* v = k + strlen(k) + 1;
            cat_fields_put(fields, k, v);
            k = v + strlen(v) + 1;
        }
        if( rec->kind == TORIRSMAPED_CATALOG_TRIGGER )
        {
            cat_fields_put(fields, "file", src->path);
            snprintf(number, sizeof(number), "%u", c->line);
            cat_fields_put(fields, "line", cat_fields_scratch(fields, number));
        }
    }
    if( rec->prov & TORIRSMAPED_PROV_CLIENT && rec->prov & TORIRSMAPED_PROV_SERVER )
        cat_fields_put(fields, "half", "both");
    else if( rec->prov & TORIRSMAPED_PROV_CLIENT )
        cat_fields_put(fields, "half", "client");
    else if( rec->prov & TORIRSMAPED_PROV_SERVER )
        cat_fields_put(fields, "half", "server");
    if( rec->flags & CAT_REC_DIALOGUE )
        cat_fields_put(fields, "dialogue", "yes");
    if( rec->kind == TORIRSMAPED_CATALOG_VARP || rec->kind == TORIRSMAPED_CATALOG_VARBIT )
    {
        snprintf(number, sizeof(number), "%d", rec->users);
        cat_fields_put(fields, "users", cat_fields_scratch(fields, number));
    }
    if( rec->square >= 0 )
    {
        const struct cat_square* sq = &catalog->squares[rec->square];
        snprintf(number, sizeof(number), "%d", sq->map_x);
        cat_fields_put(fields, "x", cat_fields_scratch(fields, number));
        snprintf(number, sizeof(number), "%d", sq->map_z);
        cat_fields_put(fields, "z", cat_fields_scratch(fields, number));
        cat_fields_put(fields, "storage", catalog->storages[sq->storage]);
        cat_fields_put(fields, "locs", sq->flags & TORIRSMAPED_SQUARE_HAS_LOCS ? "yes" : "no");
        snprintf(number, sizeof(number), "%d", sq->spawn_count);
        cat_fields_put(fields, "spawns", cat_fields_scratch(fields, number));
        cat_fields_put(fields, "area",
            sq->primary_area >= 0 ? catalog->areas[sq->primary_area].internal : "none");
    }
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

struct cat_query
{
    int term_count;
    char terms[CAT_QUERY_TERMS_MAX][128];
    char whole[256];
    int filter_count;
    char filter_key[CAT_QUERY_FILTERS_MAX][64];
    char filter_value[CAT_QUERY_FILTERS_MAX][192];
};

/** Read one token at *p: a "quoted run" or a run to whitespace. */
static void
cat_token(
    const char** p,
    char* out,
    size_t out_size)
{
    const char* s = *p;
    size_t n = 0;
    if( *s == '"' )
    {
        s++;
        while( *s && *s != '"' )
        {
            if( n + 1 < out_size )
                out[n++] = *s;
            s++;
        }
        if( *s == '"' )
            s++;
    }
    else
    {
        while( *s && *s != ' ' && *s != '\t' )
        {
            if( n + 1 < out_size )
                out[n++] = *s;
            s++;
        }
    }
    out[n] = '\0';
    *p = s;
}

static void
cat_query_parse(
    const char* text,
    struct cat_query* query)
{
    const char* p = text;
    size_t whole = 0;
    memset(query, 0, sizeof(*query));
    for( ;; )
    {
        const char* eq;
        const char* space;
        while( *p == ' ' || *p == '\t' )
            p++;
        if( !*p )
            break;
        eq = strchr(p, '=');
        space = p;
        while( *space && *space != ' ' && *space != '\t' && *space != '"' )
            space++;
        if( eq && eq < space && eq > p && query->filter_count < CAT_QUERY_FILTERS_MAX )
        {
            int f = query->filter_count++;
            size_t klen = (size_t)(eq - p);
            if( klen >= sizeof(query->filter_key[f]) )
                klen = sizeof(query->filter_key[f]) - 1;
            memcpy(query->filter_key[f], p, klen);
            query->filter_key[f][klen] = '\0';
            p = eq + 1;
            cat_token(&p, query->filter_value[f], sizeof(query->filter_value[f]));
            continue;
        }
        if( query->term_count < CAT_QUERY_TERMS_MAX )
        {
            char* term = query->terms[query->term_count++];
            cat_token(&p, term, sizeof(query->terms[0]));
            if( whole + strlen(term) + 2 < sizeof(query->whole) )
                whole += (size_t)snprintf(
                    query->whole + whole, sizeof(query->whole) - whole, "%s%s", whole ? " " : "", term);
            for( char* c = term; *c; c++ )
                *c = cat_lower(*c);
        }
        else
        {
            char skip[128];
            cat_token(&p, skip, sizeof(skip));
        }
    }
}

static int
cat_value_match(
    const char* field,
    const char* wanted)
{
    size_t length = strlen(wanted);
    if( length > 0 && wanted[length - 1] == '*' )
    {
        char prefix[192];
        snprintf(prefix, sizeof(prefix), "%.*s", (int)(length - 1), wanted);
        return cat_iprefix(field, prefix);
    }
    if( cat_ieq(field, wanted) )
        return 1;
    /* `varb4153` names `varb4153_ics_little_var` */
    return cat_iprefix(field, wanted) && field[length] == '_';
}

static int
cat_parse_whole_int(
    const char* text,
    int* out)
{
    return text[0] && cat_parse_int(text, text + strlen(text), out);
}

static int
cat_filters_match(
    const struct ToriRSMapEdCatalog* catalog,
    int record,
    const struct cat_query* query)
{
    const struct cat_record* rec = &catalog->records[record];
    struct cat_fields fields;
    int collected = 0;

    for( int f = 0; f < query->filter_count; f++ )
    {
        const char* key = query->filter_key[f];
        const char* wanted = query->filter_value[f];
        int hit = 0;
        int number;

        if( strcmp(key, "name") == 0 )
            hit = cat_value_match(rec->name, wanted)
                  || (rec->display && cat_value_match(rec->display, wanted));
        else if( strcmp(key, "id") == 0 )
            hit = cat_parse_whole_int(wanted, &number) && rec->id == number;
        else
        {
            size_t klen = strlen(key);
            if( !collected )
            {
                cat_collect(catalog, record, &fields);
                collected = 1;
            }
            for( int i = 0; i < fields.n && !hit; i++ )
            {
                const char* fk = fields.key[i];
                size_t flen = strlen(fk);
                if( strcmp(fk, key) != 0
                    && !(cat_family_len(fk, flen) == klen && memcmp(fk, key, klen) == 0) )
                    continue;
                hit = cat_value_match(fields.value[i], wanted);
            }
        }
        if( !hit )
            return 0;
    }
    return 1;
}

static int
cat_hit_cmp(
    const void* a,
    const void* b)
{
    const struct cat_hit* x = a;
    const struct cat_hit* y = b;
    uint32_t xid;
    uint32_t yid;
    if( x->score != y->score )
        return x->score - y->score;
    if( x->kind != y->kind )
        return x->kind - y->kind;
    /* no id (-1) sorts last */
    xid = (uint32_t)x->id;
    yid = (uint32_t)y->id;
    if( xid != yid )
        return xid < yid ? -1 : 1;
    return strcmp(x->name, y->name);
}

static int
cat_kind_ok(
    uint32_t want,
    int kind)
{
    return want == TORIRSMAPED_CATALOG_ANY || want == (uint32_t)kind;
}

/** 0: the whole text names it; 1: the name starts with the first term;
 *  2: anything else that matched. */
static int
cat_score(
    const struct cat_record* rec,
    const struct cat_query* query)
{
    int number;
    if( !query->term_count )
        return 2;
    if( cat_ieq(rec->name, query->whole) || (rec->display && cat_ieq(rec->display, query->whole))
        || (cat_parse_whole_int(query->whole, &number) && rec->id == number) )
        return 0;
    if( cat_iprefix(rec->name, query->terms[0]) )
        return 1;
    return 2;
}

static int
cat_terms_match(
    const struct cat_record* rec,
    const struct cat_query* query)
{
    for( int t = 0; t < query->term_count; t++ )
    {
        const char* term = query->terms[t];
        int number;
        if( cat_icontains(rec->name, term) )
            continue;
        if( rec->display && cat_icontains(rec->display, term) )
            continue;
        if( cat_parse_whole_int(term, &number) && rec->id == number )
            continue;
        return 0;
    }
    return 1;
}

static void
cat_hit_add(
    struct cat_hit** hits,
    int* count,
    int* cap,
    const struct cat_record* rec,
    int record,
    int score)
{
    struct cat_hit* hit;
    for( int i = 0; i < *count; i++ )
    {
        if( (*hits)[i].record == record )
            return;
    }
    *hits = cat_grow(*hits, cap, *count + 1, sizeof(**hits));
    hit = &(*hits)[(*count)++];
    hit->score = score;
    hit->kind = rec->kind;
    hit->id = rec->id;
    hit->name = rec->name;
    hit->record = record;
}

/**
 * 1 when every record `query` matches also matches `held`, so the held hits
 * hold every answer: each held filter is restated exactly (a filter value is
 * matched whole, so "op=Ch" says nothing about "op=Chop"), and each held
 * term is inside a term of `query` that is not a number (a number also
 * matches by id, which says nothing about the name).
 */
static int
cat_query_implies(
    const struct cat_query* query,
    const struct cat_query* held)
{
    for( int f = 0; f < held->filter_count; f++ )
    {
        int found = 0;
        for( int g = 0; g < query->filter_count && !found; g++ )
            found = strcmp(query->filter_key[g], held->filter_key[f]) == 0
                    && strcmp(query->filter_value[g], held->filter_value[f]) == 0;
        if( !found )
            return 0;
    }
    for( int t = 0; t < held->term_count; t++ )
    {
        int found = 0;
        for( int u = 0; u < query->term_count && !found; u++ )
        {
            int number;
            found = !cat_parse_whole_int(query->terms[u], &number) && strstr(query->terms[u], held->terms[t]) != NULL;
        }
        if( !found )
            return 0;
    }
    return 1;
}

/**
 * Every match of (kind, filter, flags), sorted, in the held query's buffer:
 * valid until the next query or merge. The held hits answer the same
 * question again (a page flip) without testing a record, and a question that
 * implies the held one (typing on) is tested against the held hits only.
 */
static int
cat_query_hits(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* filter,
    uint32_t flags,
    const struct cat_hit** out_hits)
{
    struct cat_held_query* held = &catalog->held;
    struct cat_query query;
    int const same_index = held->valid && held->revision == catalog->revision && held->kind == kind
                           && held->flags == flags;

    *out_hits = held->hits;
    if( same_index && strcmp(held->filter, filter) == 0 )
    {
        catalog->counters.held++;
        return held->count;
    }
    cat_query_parse(filter, &query);

    if( catalog->record_count == 0 )
        held->count = 0;
    else if( (flags & TORIRSMAPED_CATALOG_EXACT) && query.term_count > 0 )
    {
        /* Hash probes: by name and display name in every kind asked for,
         * and by id when the text is a number. */
        int number;
        int numeric = cat_parse_whole_int(query.whole, &number);
        held->count = 0;
        for( int k = 0; k < TORIRSMAPED_CATALOG_KIND_COUNT; k++ )
        {
            uint32_t slot;
            if( !cat_kind_ok(kind, k) )
                continue;
            slot = cat_hash_lower((uint32_t)k, query.whole) & catalog->hash_mask;
            for( int32_t r = catalog->name_heads[slot]; r >= 0; r = catalog->records[r].next_name )
            {
                const struct cat_record* rec = &catalog->records[r];
                if( rec->kind == k && cat_ieq(rec->name, query.whole)
                    && cat_filters_match(catalog, r, &query) )
                    cat_hit_add(&held->hits, &held->count, &held->cap, rec, r, 0);
            }
            for( int32_t r = catalog->display_heads[slot]; r >= 0; r = catalog->records[r].next_display )
            {
                const struct cat_record* rec = &catalog->records[r];
                if( rec->kind == k && cat_ieq(rec->display, query.whole)
                    && cat_filters_match(catalog, r, &query) )
                    cat_hit_add(&held->hits, &held->count, &held->cap, rec, r, 1);
            }
            if( numeric && number >= 0 )
            {
                slot = ((uint32_t)number * 2654435761u ^ (uint32_t)k * 40503u) & catalog->hash_mask;
                for( int32_t r = catalog->id_heads[slot]; r >= 0; r = catalog->records[r].next_id )
                {
                    const struct cat_record* rec = &catalog->records[r];
                    if( rec->kind == k && rec->id == number && cat_filters_match(catalog, r, &query) )
                        cat_hit_add(&held->hits, &held->count, &held->cap, rec, r, 1);
                }
            }
        }
    }
    else
    {
        struct cat_query held_query;
        int narrow = 0;
        if( same_index && !(held->flags & TORIRSMAPED_CATALOG_EXACT) )
        {
            cat_query_parse(held->filter, &held_query);
            narrow = cat_query_implies(&query, &held_query);
        }
        if( narrow )
        {
            /* Typing on: only the held hits can match; kept in place. */
            int kept = 0;
            for( int i = 0; i < held->count; i++ )
            {
                int const r = held->hits[i].record;
                const struct cat_record* rec = &catalog->records[r];
                catalog->counters.examined++;
                if( !cat_terms_match(rec, &query) || !cat_filters_match(catalog, r, &query) )
                    continue;
                held->hits[kept] = held->hits[i];
                held->hits[kept].score = cat_score(rec, &query);
                kept++;
            }
            held->count = kept;
            catalog->counters.narrowed++;
        }
        else
        {
            held->count = 0;
            for( int r = 0; r < catalog->record_count; r++ )
            {
                const struct cat_record* rec = &catalog->records[r];
                struct cat_hit* hit;
                catalog->counters.examined++;
                if( !cat_kind_ok(kind, rec->kind) || !cat_terms_match(rec, &query)
                    || !cat_filters_match(catalog, r, &query) )
                    continue;
                held->hits = cat_grow(held->hits, &held->cap, held->count + 1, sizeof(*held->hits));
                hit = &held->hits[held->count++];
                hit->score = cat_score(rec, &query);
                hit->kind = rec->kind;
                hit->id = rec->id;
                hit->name = rec->name;
                hit->record = r;
            }
            catalog->counters.full_scans++;
        }
    }
    if( held->count > 1 )
        qsort(held->hits, (size_t)held->count, sizeof(*held->hits), cat_hit_cmp);
    held->valid = 1;
    held->revision = catalog->revision;
    held->kind = kind;
    held->flags = flags;
    snprintf(held->filter, sizeof(held->filter), "%s", filter);
    *out_hits = held->hits;
    return held->count;
}

int
ToriRSMapEdCatalog_Query(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* filter,
    uint32_t flags,
    int* out_records,
    int max)
{
    const struct cat_hit* hits;
    int count;
    assert(catalog);
    assert(filter);
    assert(out_records || max == 0);
    cat_request_index(catalog);
    count = cat_query_hits(catalog, kind, filter, flags, &hits);
    for( int i = 0; i < count && i < max; i++ )
        out_records[i] = hits[i].record;
    return count;
}

void
ToriRSMapEdCatalog_Entry(
    const struct ToriRSMapEdCatalog* catalog,
    int record,
    struct ToriRSMapEdCatalogEntry* out)
{
    const struct cat_record* rec;
    assert(catalog);
    assert(out);
    assert(record >= 0);
    assert(record < catalog->record_count);
    rec = &catalog->records[record];
    out->kind = rec->kind;
    out->id = rec->id;
    out->provenance = rec->prov;
    out->name = rec->name;
    out->display = rec->display ? rec->display : "";
}

int
ToriRSMapEdCatalog_Field(
    const struct ToriRSMapEdCatalog* catalog,
    int record,
    const char* key,
    char* out,
    size_t out_size)
{
    struct cat_fields fields;
    assert(catalog);
    assert(key);
    assert(out);
    assert(out_size > 0);
    assert(record >= 0);
    assert(record < catalog->record_count);
    out[0] = '\0';
    cat_collect(catalog, record, &fields);
    for( int i = 0; i < fields.n; i++ )
    {
        if( strcmp(fields.key[i], key) == 0 )
        {
            snprintf(out, out_size, "%s", fields.value[i]);
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Wire handlers                                                       */
/* ------------------------------------------------------------------ */

static void
cat_put_row(
    const struct ToriRSMapEdCatalog* catalog,
    int record,
    struct ToriRSMapEdBuf* out)
{
    const struct cat_record* rec = &catalog->records[record];
    struct cat_fields fields;
    cat_collect(catalog, record, &fields);
    ToriRSMapEd_PutU32(out, rec->kind);
    ToriRSMapEd_PutU32(out, (uint32_t)rec->id);
    ToriRSMapEd_PutString(out, rec->name);
    ToriRSMapEd_PutString(out, rec->display ? rec->display : "");
    ToriRSMapEd_PutU32(out, rec->prov);
    ToriRSMapEd_PutU32(out, (uint32_t)fields.n);
    for( int i = 0; i < fields.n; i++ )
    {
        ToriRSMapEd_PutString(out, fields.key[i]);
        ToriRSMapEd_PutString(out, fields.value[i]);
    }
}

int
ToriRSMapEdCatalog_HandleQuery(
    struct ToriRSMapEdCatalog* catalog,
    const uint8_t* body,
    uint32_t length,
    struct ToriRSMapEdBuf* out)
{
    struct ToriRSMapEdReader reader;
    char filter[1024];
    uint32_t kind;
    uint32_t flags;
    uint32_t page;
    uint32_t page_size;
    const struct cat_hit* hits;
    int total;
    uint32_t first;
    uint32_t rows;

    assert(catalog);
    assert(out);
    ToriRSMapEd_ReaderInit(&reader, body, length);
    kind = ToriRSMapEd_GetU32(&reader);
    ToriRSMapEd_GetString(&reader, filter, sizeof(filter));
    flags = ToriRSMapEd_GetU32(&reader);
    page = ToriRSMapEd_GetU32(&reader);
    page_size = ToriRSMapEd_GetU32(&reader);
    if( !ToriRSMapEd_ReaderDone(&reader) )
        return 0;
    if( kind != TORIRSMAPED_CATALOG_ANY && kind >= TORIRSMAPED_CATALOG_KIND_COUNT )
    {
        ToriRSMapEd_PutU32(out, EDITOR_HOST_REFUSED);
        for( int i = 0; i < 4; i++ )
            ToriRSMapEd_PutU32(out, 0);
        return 1;
    }
    if( page_size == 0 )
        page_size = TORIRSMAPED_CATALOG_PAGE_DEFAULT;
    if( page_size > TORIRSMAPED_CATALOG_PAGE_MAX )
        page_size = TORIRSMAPED_CATALOG_PAGE_MAX;

    cat_request_index(catalog);
    total = cat_query_hits(catalog, kind, filter, flags, &hits);
    first = page * page_size;
    if( page > 0 && first / page != page_size )
        first = UINT32_MAX; /* overflow: past the end */
    rows = first < (uint32_t)total ? (uint32_t)total - first : 0;
    if( rows > page_size )
        rows = page_size;

    ToriRSMapEd_PutU32(out, EDITOR_HOST_OK);
    ToriRSMapEd_PutU32(out, (uint32_t)total);
    ToriRSMapEd_PutU32(out, page);
    ToriRSMapEd_PutU32(out, page_size);
    ToriRSMapEd_PutU32(out, rows);
    for( uint32_t i = 0; i < rows; i++ )
        cat_put_row(catalog, hits[first + i].record, out);
    return 1;
}

/* ---- CATALOG_RECORD ---- */

struct cat_rec_field
{
    uint32_t key;
    uint32_t value;
    uint32_t source;
    uint32_t old_value;
    uint32_t old_source;
};

struct cat_rec_build
{
    char* text;
    size_t text_len;
    size_t text_cap;
    struct cat_rec_field* fields;
    int field_count;
    int field_cap;
};

static uint32_t
cat_rec_text(
    struct cat_rec_build* build,
    const char* text,
    size_t length)
{
    size_t at = build->text_len;
    if( at + length + 1 > build->text_cap )
    {
        size_t cap = build->text_cap ? build->text_cap : 4096;
        while( cap < at + length + 1 )
            cap *= 2;
        build->text = ToriMem_Realloc(ToriMem_Editor(), build->text, cap);
        assert(build->text);
        build->text_cap = cap;
    }
    memcpy(build->text + at, text, length);
    build->text[at + length] = '\0';
    build->text_len = at + length + 1;
    return (uint32_t)at;
}

static void
cat_rec_put(
    struct cat_rec_build* build,
    const char* key,
    size_t key_len,
    const char* value,
    size_t value_len,
    uint32_t source)
{
    struct cat_rec_field* field;
    for( int i = 0; i < build->field_count; i++ )
    {
        field = &build->fields[i];
        if( strlen(build->text + field->key) == key_len
            && memcmp(build->text + field->key, key, key_len) == 0 )
        {
            if( field->source != source )
            {
                field->old_value = field->value;
                field->old_source = field->source;
            }
            field->value = cat_rec_text(build, value, value_len);
            field->source = source;
            return;
        }
    }
    build->fields = cat_grow(build->fields, &build->field_cap, build->field_count + 1, sizeof(*build->fields));
    field = &build->fields[build->field_count++];
    field->key = cat_rec_text(build, key, key_len);
    field->value = cat_rec_text(build, value, value_len);
    field->source = source;
    field->old_value = CAT_NONE;
    field->old_source = TORIRSMAPED_CATALOG_NO_SOURCE;
}

/** Every `key=value` (or `key = value // note`) line of a block read back
 *  from disk, header and comments skipped. */
static void
cat_rec_put_text(
    struct cat_rec_build* build,
    const char* text,
    size_t size,
    uint32_t source)
{
    const char* p = text;
    const char* end = text + size;
    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        if( p < le && p[0] != '[' && p[0] != '/' )
        {
            const char* comment = NULL;
            const char* eq;
            for( const char* s = p; s + 1 < le; s++ )
            {
                if( s[0] == '/' && s[1] == '/' && s > p && (s[-1] == ' ' || s[-1] == '\t') )
                {
                    comment = s;
                    break;
                }
            }
            if( comment )
                le = comment;
            eq = memchr(p, '=', (size_t)(le - p));
            if( eq )
            {
                const char* k = p;
                const char* ke = eq;
                const char* v = eq + 1;
                const char* ve = le;
                cat_trim(&k, &ke);
                cat_trim(&v, &ve);
                if( k < ke )
                    cat_rec_put(build, k, (size_t)(ke - k), v, (size_t)(ve - v), source);
            }
        }
        p = next;
    }
}

/** 1 when every file the record's blocks come from is as indexed (a sweep
 *  that has seen a file change but not yet re-parsed it marks it dirty). */
static int
cat_record_fresh(
    struct ToriRSMapEdCatalog* catalog,
    int record)
{
    for( int32_t r = catalog->records[record].first_ref; r >= 0; r = catalog->refs[r].next )
    {
        const struct cat_source* src = &catalog->sources[catalog->refs[r].source];
        char full[CAT_PATH_MAX * 2];
        struct stat st;
        if( src->dirty )
            return 0;
        snprintf(full, sizeof(full), "%s/%s", catalog->content_dir, src->path);
        catalog->counters.stats++;
        if( stat(full, &st) != 0 || cat_mtime_ns(&st) != src->mtime_ns
            || (int64_t)st.st_size != src->size )
            return 0;
    }
    return 1;
}

/**
 * A record's files changed under the index (its offsets are stale): re-read
 * just those files and merge, rather than re-stat the tree. A file that is
 * gone takes the whole refresh (the record may have gone with it). A sweep
 * in flight carries on; what it has marked dirty is parsed here too.
 */
static void
cat_refresh_record(
    struct ToriRSMapEdCatalog* catalog,
    int record)
{
    struct ToriRSMapEdCatalogStats stats;

    for( int32_t r = catalog->records[record].first_ref; r >= 0; r = catalog->refs[r].next )
    {
        struct cat_source* src = &catalog->sources[catalog->refs[r].source];
        char full[CAT_PATH_MAX * 2];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", catalog->content_dir, src->path);
        catalog->counters.stats++;
        if( stat(full, &st) != 0 )
        {
            (void)cat_refresh(catalog, 0, NULL);
            return;
        }
        if( cat_mtime_ns(&st) != src->mtime_ns || (int64_t)st.st_size != src->size )
        {
            src->mtime_ns = cat_mtime_ns(&st);
            src->size = (int64_t)st.st_size;
            src->dirty = 1;
        }
    }
    memset(&stats, 0, sizeof(stats));
    (void)cat_finish(catalog, 0, 0, &stats);
}

/** A source's merge rank for a record view (see HandleRecord). */
static int
cat_merge_rank(const struct cat_source* src)
{
    const char* base;
    switch( src->type )
    {
    case CAT_SRC_CONFIG:
        return 0;
    case CAT_SRC_NPC_STATS:
    case CAT_SRC_NPC_COMBAT:
        return 1;
    case CAT_SRC_OVERLAY:
        base = strrchr(src->path, '/');
        base = base ? base + 1 : src->path;
        return strstr(src->path, "ported/") || strstr(base, ".generated.") ? 1 : 2;
    default:
        return -1;
    }
}

static int
cat_record_lookup(
    const struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    int32_t id,
    const char* name)
{
    if( kind >= TORIRSMAPED_CATALOG_KIND_COUNT || !catalog->name_heads )
        return -1;
    if( name[0] )
        return cat_find_exact(catalog, (int)kind, name);
    if( id < 0 )
        return -1;
    {
        uint32_t slot = ((uint32_t)id * 2654435761u ^ kind * 40503u) & catalog->hash_mask;
        for( int32_t r = catalog->id_heads[slot]; r >= 0; r = catalog->records[r].next_id )
        {
            if( catalog->records[r].kind == kind && catalog->records[r].id == id )
                return r;
        }
    }
    return -1;
}

int
ToriRSMapEdCatalog_HandleRecord(
    struct ToriRSMapEdCatalog* catalog,
    const uint8_t* body,
    uint32_t length,
    struct ToriRSMapEdBuf* out)
{
    struct ToriRSMapEdReader reader;
    char name[512];
    uint32_t kind;
    int32_t id;
    int record;
    struct cat_rec_build build;
    int32_t sources[64];
    uint32_t source_lines[64];
    int source_count = 0;
    struct cat_fields derived;
    const struct cat_record* rec;

    assert(catalog);
    assert(out);
    ToriRSMapEd_ReaderInit(&reader, body, length);
    kind = ToriRSMapEd_GetU32(&reader);
    id = (int32_t)ToriRSMapEd_GetU32(&reader);
    ToriRSMapEd_GetString(&reader, name, sizeof(name));
    if( !ToriRSMapEd_ReaderDone(&reader) )
        return 0;

    cat_request_index(catalog);
    record = cat_record_lookup(catalog, kind, id, name);
    if( record >= 0 && !cat_record_fresh(catalog, record) )
    {
        /* A block's file changed under the index: offsets are stale. */
        cat_refresh_record(catalog, record);
        record = cat_record_lookup(catalog, kind, id, name);
    }
    if( record < 0 )
    {
        ToriRSMapEd_PutU32(out, EDITOR_HOST_ABSENT);
        ToriRSMapEd_PutU32(out, kind);
        ToriRSMapEd_PutU32(out, (uint32_t)id);
        ToriRSMapEd_PutString(out, name);
        ToriRSMapEd_PutString(out, "");
        ToriRSMapEd_PutU32(out, 0);
        ToriRSMapEd_PutU32(out, 0);
        ToriRSMapEd_PutU32(out, 0);
        return 1;
    }

    memset(&build, 0, sizeof(build));
    rec = &catalog->records[record];
    /* The contributions in the pack's MERGE order, so the field a record
     * view shows is the one the server reads: cachepack's ranks
     * (cp_merge.c cp_merge_rank_for) -- configs/all.<type> 0, a
     * `.generated.` overlay 1, an authored overlay 2 -- then path order
     * within a rank (the walk is sorted); later contributions win in
     * cat_rec_put. Index files and stats sources come first (rank -1 / 1). */
    {
        int32_t order[256];
        int order_count = 0;
        for( int32_t r = rec->first_ref; r >= 0 && order_count < 256; r = catalog->refs[r].next )
            order[order_count++] = r;
        for( int i = 1; i < order_count; i++ )
        {
            int32_t moving = order[i];
            int const rank = cat_merge_rank(&catalog->sources[catalog->refs[moving].source]);
            const char* path = catalog->sources[catalog->refs[moving].source].path;
            int j = i;
            while( j > 0 )
            {
                const struct cat_source* prev = &catalog->sources[catalog->refs[order[j - 1]].source];
                int const prev_rank = cat_merge_rank(prev);
                if( prev_rank < rank || (prev_rank == rank && strcmp(prev->path, path) <= 0) )
                    break;
                order[j] = order[j - 1];
                j--;
            }
            order[j] = moving;
        }
    for( int oi = 0; oi < order_count; oi++ )
    {
        int32_t const r = order[oi];
        const struct cat_source* src = &catalog->sources[catalog->refs[r].source];
        const struct cat_contrib* c = &src->contribs[catalog->refs[r].contrib];
        uint32_t source = TORIRSMAPED_CATALOG_NO_SOURCE;

        for( int i = 0; i < source_count; i++ )
        {
            if( sources[i] == catalog->refs[r].source )
                source = (uint32_t)i;
        }
        if( source == TORIRSMAPED_CATALOG_NO_SOURCE && source_count < 64 )
        {
            source = (uint32_t)source_count;
            sources[source_count] = catalog->refs[r].source;
            source_lines[source_count] = c->line;
            source_count++;
        }

        if( c->len > 0
            && (src->type == CAT_SRC_CONFIG || src->type == CAT_SRC_OVERLAY
                || src->type == CAT_SRC_NPC_STATS || src->type == CAT_SRC_NPC_COMBAT) )
        {
            char full[CAT_PATH_MAX * 2];
            FILE* file;
            char* block;
            size_t got = 0;
            snprintf(full, sizeof(full), "%s/%s", catalog->content_dir, src->path);
            file = fopen(full, "rb");
            if( !file )
                continue;
            block = ToriMem_Alloc(ToriMem_Editor(), (size_t)c->len + 1);
            assert(block);
            catalog->counters.block_reads++;
            if( fseek(file, (long)c->off, SEEK_SET) == 0 )
                got = fread(block, 1, c->len, file);
            fclose(file);
            cat_rec_put_text(&build, block, got, source);
            ToriMem_Free(ToriMem_Editor(), block);
        }
        else if( c->fields != CAT_NONE )
        {
            for( const char* k = src->pool + c->fields; *k; )
            {
                const char* v = k + strlen(k) + 1;
                cat_rec_put(&build, k, strlen(k), v, strlen(v), source);
                k = v + strlen(v) + 1;
            }
        }
    }
    }
    /* What only the merge knows: derived, sourced from nothing. */
    cat_collect(catalog, record, &derived);
    for( int i = 0; i < derived.n; i++ )
    {
        int known = 0;
        for( int f = 0; f < build.field_count && !known; f++ )
            known = strcmp(build.text + build.fields[f].key, derived.key[i]) == 0;
        if( !known )
            cat_rec_put(&build, derived.key[i], strlen(derived.key[i]), derived.value[i],
                strlen(derived.value[i]), TORIRSMAPED_CATALOG_NO_SOURCE);
    }

    ToriRSMapEd_PutU32(out, EDITOR_HOST_OK);
    ToriRSMapEd_PutU32(out, rec->kind);
    ToriRSMapEd_PutU32(out, (uint32_t)rec->id);
    ToriRSMapEd_PutString(out, rec->name);
    ToriRSMapEd_PutString(out, rec->display ? rec->display : "");
    ToriRSMapEd_PutU32(out, rec->prov);
    ToriRSMapEd_PutU32(out, (uint32_t)source_count);
    for( int i = 0; i < source_count; i++ )
    {
        const struct cat_source* src = &catalog->sources[sources[i]];
        uint32_t prov = 0;
        for( int32_t r = rec->first_ref; r >= 0 && !prov; r = catalog->refs[r].next )
        {
            if( catalog->refs[r].source == sources[i] )
                prov = src->contribs[catalog->refs[r].contrib].prov;
        }
        ToriRSMapEd_PutString(out, src->path);
        ToriRSMapEd_PutU32(out, prov);
        ToriRSMapEd_PutU32(out, source_lines[i]);
    }
    ToriRSMapEd_PutU32(out, (uint32_t)build.field_count);
    for( int i = 0; i < build.field_count; i++ )
    {
        const struct cat_rec_field* field = &build.fields[i];
        ToriRSMapEd_PutString(out, build.text + field->key);
        ToriRSMapEd_PutString(out, build.text + field->value);
        ToriRSMapEd_PutU32(out, field->source);
        ToriRSMapEd_PutString(out, field->old_value == CAT_NONE ? "" : build.text + field->old_value);
        ToriRSMapEd_PutU32(out, field->old_source);
    }
    ToriMem_Free(ToriMem_Editor(), build.text);
    ToriMem_Free(ToriMem_Editor(), build.fields);
    return 1;
}

/* ---- CATALOG_SQUARES ---- */

static void
cat_put_square(
    struct ToriRSMapEdBuf* out,
    int x,
    int z,
    int storage,
    uint32_t flags,
    int spawn_count,
    uint32_t rgb,
    const int* areas,
    int area_n,
    int loc_count)
{
    ToriRSMapEd_PutU32(out, (uint32_t)x);
    ToriRSMapEd_PutU32(out, (uint32_t)z);
    ToriRSMapEd_PutU32(out, (uint32_t)storage);
    ToriRSMapEd_PutU32(out, flags);
    ToriRSMapEd_PutU32(out, (uint32_t)spawn_count);
    ToriRSMapEd_PutU32(out, rgb);
    ToriRSMapEd_PutU32(out, area_n > 0 ? (uint32_t)areas[0] : TORIRSMAPED_CATALOG_NO_AREA);
    ToriRSMapEd_PutU32(out, (uint32_t)area_n);
    for( int i = 0; i < area_n; i++ )
        ToriRSMapEd_PutU32(out, (uint32_t)areas[i]);
    ToriRSMapEd_PutU32(out, (uint32_t)loc_count);
}

/** The storage index of a document's storage ("" = maps/), or -1. */
static int
cat_doc_storage_index(
    const struct ToriRSMapEdCatalog* catalog,
    const char* storage)
{
    if( !storage[0] || strcmp(storage, "maps") == 0 )
        return 0;
    for( int i = 1; i < catalog->storage_count; i++ )
    {
        if( strcmp(catalog->storages[i], storage) == 0 )
            return i;
    }
    return -1;
}

int
ToriRSMapEdCatalog_HandleSquares(
    struct ToriRSMapEdCatalog* catalog,
    const struct Editor_Doc* doc,
    const uint8_t* body,
    uint32_t length,
    struct ToriRSMapEdBuf* out)
{
    assert(catalog);
    assert(doc);
    assert(out);
    (void)body;
    if( length != 0 )
        return 0;
    cat_request_index(catalog);
    ToriRSMapEdCatalog_EncodeSquares(catalog, doc, out);
    return 1;
}

void
ToriRSMapEdCatalog_EncodeSquares(
    struct ToriRSMapEdCatalog* catalog,
    const struct Editor_Doc* doc,
    struct ToriRSMapEdBuf* out)
{
    uint32_t* doc_flags;
    int extra_storages = 0;
    const char* extra_storage_names[EDITOR_DOC_MAX_SQUARES];
    int doc_storage[EDITOR_DOC_MAX_SQUARES];
    int doc_row[EDITOR_DOC_MAX_SQUARES];
    int created = 0;

    assert(catalog);
    assert(doc);
    assert(out);
    assert(doc->square_count <= EDITOR_DOC_MAX_SQUARES);

    /* The authority's open squares: flag their rows, and give a row to the
     * ones that are not on disk yet. A created square in a lane directory
     * that does not exist yet brings its storage along. */
    doc_flags = ToriMem_Calloc(ToriMem_Editor(), (size_t)catalog->square_count + 1, sizeof(uint32_t));
    assert(doc_flags);
    for( int i = 0; i < doc->square_count; i++ )
    {
        const struct Editor_Square* square = &doc->squares[i];
        const char* storage = doc->square_storage[i];
        int index = cat_doc_storage_index(catalog, storage);
        uint32_t flags = TORIRSMAPED_SQUARE_OPEN;
        if( square->dirty_map || square->dirty_loc )
            flags |= TORIRSMAPED_SQUARE_DIRTY;
        doc_row[i] = -1;
        if( index < 0 )
        {
            int e;
            for( e = 0; e < extra_storages; e++ )
            {
                if( strcmp(extra_storage_names[e], storage) == 0 )
                    break;
            }
            if( e == extra_storages )
                extra_storage_names[extra_storages++] = storage;
            index = catalog->storage_count + e;
        }
        doc_storage[i] = index;
        for( int s = 0; s < catalog->square_count; s++ )
        {
            const struct cat_square* sq = &catalog->squares[s];
            if( sq->storage == index && sq->map_x == square->map_x && sq->map_z == square->map_z )
            {
                doc_flags[s] |= flags;
                doc_row[i] = s;
                break;
            }
        }
        if( doc_row[i] < 0 )
            created++;
    }

    ToriRSMapEd_PutU32(out, EDITOR_HOST_OK);
    ToriRSMapEd_PutU32(out, (uint32_t)(catalog->storage_count + extra_storages));
    for( int i = 0; i < catalog->storage_count; i++ )
        ToriRSMapEd_PutString(out, catalog->storages[i]);
    for( int i = 0; i < extra_storages; i++ )
        ToriRSMapEd_PutString(out, extra_storage_names[i]);
    ToriRSMapEd_PutU32(out, (uint32_t)catalog->area_count);
    for( int i = 0; i < catalog->area_count; i++ )
    {
        ToriRSMapEd_PutString(out, catalog->areas[i].internal);
        ToriRSMapEd_PutString(out, catalog->areas[i].external);
        ToriRSMapEd_PutU32(out, (uint32_t)catalog->areas[i].is_main);
    }
    ToriRSMapEd_PutU32(out, (uint32_t)(catalog->square_count + created));
    for( int s = 0; s < catalog->square_count; s++ )
    {
        const struct cat_square* sq = &catalog->squares[s];
        cat_put_square(out, sq->map_x, sq->map_z, sq->storage, sq->flags | doc_flags[s],
            sq->spawn_count, sq->rgb, catalog->square_areas + sq->area_first, sq->area_n, sq->loc_count);
    }
    for( int i = 0; i < doc->square_count; i++ )
    {
        const struct Editor_Square* square = &doc->squares[i];
        int areas[64];
        int area_n;
        uint32_t flags = TORIRSMAPED_SQUARE_OPEN | TORIRSMAPED_SQUARE_CREATED;
        int spawns = 0;
        if( doc_row[i] >= 0 )
            continue;
        if( square->dirty_map || square->dirty_loc )
            flags |= TORIRSMAPED_SQUARE_DIRTY;
        if( catalog->spawn_counts && square->map_x >= 0 && square->map_x < CAT_REGION_SPAN
            && square->map_z >= 0 && square->map_z < CAT_REGION_SPAN )
            spawns = catalog->spawn_counts[square->map_x * CAT_REGION_SPAN + square->map_z];
        area_n = cat_square_area_list(catalog, square->map_x, square->map_z, areas, 64);
        if( area_n == 0 && ToriRSMapEdCatalog_InInstanceSpace(square->map_x, square->map_z) )
            flags |= TORIRSMAPED_SQUARE_INSTANCE;
        /* Nothing of a created square is on disk: its locs are the
         * document's, not the index's. */
        cat_put_square(
            out, square->map_x, square->map_z, doc_storage[i], flags, spawns, 0, areas, area_n, 0);
    }
    ToriMem_Free(ToriMem_Editor(), doc_flags);
}

int
ToriRSMapEdCatalog_Square(
    const struct ToriRSMapEdCatalog* catalog,
    const char* key,
    struct ToriRSMapEdCatalogSquare* out)
{
    int record;
    const struct cat_square* sq;
    assert(catalog);
    assert(key);
    assert(out);
    record = cat_find_exact(catalog, TORIRSMAPED_CATALOG_SQUARE, key);
    if( record < 0 || catalog->records[record].square < 0 )
        return 0;
    sq = &catalog->squares[catalog->records[record].square];
    out->map_x = sq->map_x;
    out->map_z = sq->map_z;
    out->storage = catalog->storages[sq->storage];
    out->flags = sq->flags;
    out->spawn_count = sq->spawn_count;
    out->loc_count = sq->loc_count;
    out->rgb = sq->rgb;
    out->area = sq->primary_area >= 0 ? catalog->areas[sq->primary_area].internal : "none";
    out->area_count = sq->area_n;
    return 1;
}

int
ToriRSMapEdCatalog_SquareCount(const struct ToriRSMapEdCatalog* catalog)
{
    assert(catalog);
    return catalog->square_count;
}
