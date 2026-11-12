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
#include "perf/frame_stats.h"

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

/* The warm build runs on a worker thread where the platform has one
 * (cat_warm_thread_*): pthreads, and on Windows its own threads (the tree's
 * rule: src/makefile, "Windows uses its own threads"); the web lane keeps the
 * sliced build.
 *
 * Windows kept the sliced build until 2026-11-10, and there it was worse than
 * slow: with no thread ToriRSMapEdCatalog_Building is never true, so the
 * Squares list's first CATALOG_SQUARES -- asked with NOWAIT precisely so the
 * window need not wait -- ran the whole build inside cat_request_index, on
 * the editor's UI thread. That is 4 s of work with the files in the cache and
 * minutes with them cold (the first launch after a build): the owner's window
 * sat at "Not Responding" with an empty loading bar, main thread in
 * cat_parse_source under squares_fetch's await_reply. */
#if defined(__EMSCRIPTEN__)
#define CAT_WARM_THREAD 0
#elif defined(_WIN32)
/* Lean, so the old winsock.h stays out: this file is compiled into
 * torirs_maped.c ahead of the units that include winsock2.h. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <windows.h>
#define CAT_WARM_THREAD 1
typedef HANDLE cat_warm_thread_t;
#else
#include <pthread.h>
#define CAT_WARM_THREAD 1
typedef pthread_t cat_warm_thread_t;
#endif

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
/** Loc shapes 0..22 (EDITOR_LOC_SHAPE_COUNT). */
#define CAT_LOC_SHAPES 23

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
    /** A record the authority holds that no file has yet (an unsaved "Copy
     *  to new"): its id and its block, from memory (`cat_source.mem`). Last,
     *  so the trace's type names and every table indexed by type stand. */
    CAT_SRC_PENDING,
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

    /* CAT_SRC_PENDING: the block's text (owned) and the id it was given. */
    char* mem;
    size_t mem_len;
    int32_t mem_id;
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
    /** A scoped "all" (cat_query_scoped): the per-kind cap (0: not scoped),
     *  the hits it stopped at, and whether a cap or that window cut any. */
    int scope_cap;
    int scope_want;
    int more;
    char filter[1024];
    struct cat_hit* hits;
    int count;
    int cap;
};

/** One record made by SAVE_AS, until a save writes it (PendingSync). */
struct cat_pending
{
    uint32_t kind;
    int32_t id;
    char name[64];
    /** The CFG document revision the index's copy was parsed from. */
    uint32_t revision;
    /** The index holds a CAT_SRC_PENDING source for it. */
    int listed;
};

/** Directories the sliced scan holds open at once; a deeper subtree is
 *  walked whole when reached (none in the tree is near it). */
#define CAT_JOB_DEPTH 24

enum cat_job_phase
{
    CAT_JOB_IDLE = 0,
    /** Walking the roots, a slice at a time (cat_found marks what changed). */
    CAT_JOB_SCAN,
    /** The warm build only: the queue's sources (locs first, cat_warm_queue)
     *  parsed -- a large configs/all.<type> or overlay in byte slices -- and
     *  each merged into the live index once it is whole, so a query answers
     *  from what is there. A sweep parses and merges in one step, since a
     *  record's refs index its sources' contributions and must not see half
     *  a re-parse. */
    CAT_JOB_PARSE,
    /** The warm build's end: the derived fields a slice of records at a
     *  time, then the areas and the atlas, and the index is built. */
    CAT_JOB_FINISH,
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
    struct ToriRSMapEdCatalogStats stats;
    /** The warm build on its worker thread (cat_warm_thread_start / join):
     *  while `thread_live`, the thread owns every field of the catalog and
     *  the main thread reads only `thread_done`. */
    int thread_live;
    int thread_done;
#if CAT_WARM_THREAD
    cat_warm_thread_t thread;
#endif

    /* The warm build: source indices in the order they are parsed and
     * merged, the next one, and how far into it the job is. */
    int* queue;
    int queue_count;
    int queue_cap;
    int parse_at;
    /** The head's parse is whole; its contributions merged so far. */
    int parsed;
    int merge_at;
    /** A large blocks file part-parsed: the next byte, the lines before it. */
    int part_started;
    size_t part_off;
    uint32_t part_line;
    /** The window of that file in memory: bytes [win_off, win_end) are whole
     *  blocks (or lines) not all parsed yet; win_final when it reaches the
     *  file's end. Freed when the job ends. */
    char* win;
    size_t win_cap;
    size_t win_off;
    size_t win_end;
    int win_final;
    /** CAT_JOB_FINISH: the next record whose derived fields are due. */
    int finish_at;
    /** Bytes of the queue's sources, and of those parsed (the progress a
     *  partial answer reports). */
    int64_t bytes_total;
    int64_t bytes_done;
    /** ToriRSMapEdCatalog_Progress's answer as the worker thread last left
     *  it (atomics): what the main thread reads while the thread owns the
     *  rest. */
    uint32_t shown_done_kib;
    uint32_t shown_total_kib;
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
    int records_cap;
    struct cat_ref* refs;
    int ref_count;
    int refs_cap;
    /** The warm build merges one source at a time (cat_job_step): each
     *  contribution joins the display and id chains as it merges. A full
     *  merge builds them after. */
    int chains_live;
    /** A live merge renamed a record into another display chain. */
    int chains_stale;
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
    /** Bytes of a blocks file one warm slice parses at most
     *  (TORIRSMAPED_CATALOG_SLICE_BYTES; the test lowers it). */
    size_t slice_bytes;
    /** Bumped by every merge; the held query is keyed on it. */
    uint32_t revision;
    /** Bumped by a merge of a built index that took in a maps change (a
     *  square file parsed or gone, a lane): the atlas holders want it. */
    uint32_t maps_revision;
    /** Record indices by kind (cat_kind_index), for `kind_index_revision`. */
    int32_t* kind_records;
    int kind_records_cap;
    int kind_start[TORIRSMAPED_CATALOG_KIND_COUNT + 1];
    int kind_index_valid;
    uint32_t kind_index_revision;
    struct cat_held_query held;
    struct ToriRSMapEdCatalogCounters counters;

    /** The records "Copy to new" made in this server's life whose files are
     *  not written yet (ToriRSMapEdCatalog_PendingAdd / _PendingSync). */
    struct cat_pending* pending;
    int pending_count;
    int pending_cap;
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

static int
cat_parse_whole_int(
    const char* text,
    int* out)
{
    return text[0] && cat_parse_int(text, text + strlen(text), out);
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

/** Up to `max` bytes of a file from byte `offset` into `*buf` (grown to
 *  `*cap`), NUL-terminated: one window of a warm build's large config file.
 *  Returns NULL when it cannot be opened (*out_size 0 at its end). */
static char*
cat_read_at(
    struct ToriRSMapEdCatalog* catalog,
    const char* rel,
    size_t offset,
    size_t max,
    char** buf,
    size_t* cap,
    size_t* out_size)
{
    char path[CAT_PATH_MAX * 2];
    size_t got = 0;
    int fd;

    assert(max > 0);
    assert(buf);
    assert(cap);
    snprintf(path, sizeof(path), "%s/%s", catalog->content_dir, rel);
    fd = open(path, O_RDONLY | O_BINARY);
    if( fd < 0 )
        return NULL;
    if( *cap < max + 1 )
    {
        *buf = ToriMem_Realloc(ToriMem_Editor(), *buf, max + 1);
        assert(*buf);
        *cap = max + 1;
    }
    if( lseek(fd, (off_t)offset, SEEK_SET) == (off_t)offset )
    {
        while( got < max )
        {
            ssize_t n = read(fd, *buf + got, max - got);
            if( n <= 0 )
                break;
            got += (size_t)n;
        }
    }
    close(fd);
    (*buf)[got] = '\0';
    *out_size = got;
    return *buf;
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

/**
 * Give back a parsed source's growth slack: the pool and the contribs grow
 * by doubling while a file parses, and over the 41,668 sources of the
 * owner's tree the slack was 44 MB of pool and a third of the contribs
 * (measured 2026-11-03: pools 79 MB used of 123 MB). A source parses once a
 * build (or once per change), so nothing grows them again until then.
 */
static void
cat_source_trim(struct cat_source* src)
{
    if( src->pool && src->pool_cap > src->pool_len + 1 )
    {
        char* pool = ToriMem_Realloc(ToriMem_Editor(), src->pool, src->pool_len + 1);
        assert(pool);
        src->pool = pool;
        src->pool_cap = src->pool_len + 1;
    }
    if( src->contribs && src->contrib_cap > src->contrib_count && src->contrib_count > 0 )
    {
        struct cat_contrib* contribs = ToriMem_Realloc(
            ToriMem_Editor(), src->contribs, (size_t)src->contrib_count * sizeof(*src->contribs));
        assert(contribs);
        src->contribs = contribs;
        src->contrib_cap = src->contrib_count;
    }
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
    ToriMem_Free(ToriMem_Editor(), src->mem);
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

/** `id=name[ extra]` tables: pack/<t>.pack, pack/<ns>.alloc, compacks.
 *  A PART of the file, as cat_parse_blocks_at takes one: whole lines, `*line`
 *  the lines before them; stops at a line start once `deadline_ms` has
 *  passed (0: never) and returns the bytes consumed. */
static size_t
cat_parse_idname_at(
    struct cat_source* src,
    const char* text,
    size_t size,
    uint32_t* line_io,
    int kind,
    int prov,
    const char* table,
    double deadline_ms)
{
    const char* p = text;
    const char* end = text + size;
    uint32_t line = *line_io;
    unsigned lines = 0;

    while( p < end )
    {
        const char* le;
        const char* next;
        const char* eq;
        const char* name_end;
        int id;

        if( deadline_ms > 0.0 && (++lines & 1023u) == 0 && cat_now_ms() >= deadline_ms )
            break;
        next = cat_next_line(p, end, &le);
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
    *line_io = line;
    return (size_t)(p - text);
}

static void
cat_parse_idname(
    struct cat_source* src,
    const char* text,
    size_t size,
    int kind,
    int prov,
    const char* table)
{
    uint32_t line = 0;
    (void)cat_parse_idname_at(src, text, size, &line, kind, prov, table, 0.0);
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
 *
 * A PART of the file: `text` is the bytes from file offset `base`, `*line`
 * the lines before them (advanced past what is parsed). The part must end at
 * a `[` header line or at the file's end, so no block spans two parts. Once
 * `deadline_ms` has passed (0: never) it stops before the next header and
 * returns the bytes it consumed; otherwise `size`.
 */
static size_t
cat_parse_blocks_at(
    struct cat_source* src,
    const char* text,
    size_t size,
    size_t base,
    uint32_t* line,
    int kind,
    int prov,
    int keep_all,
    double deadline_ms)
{
    const char* const* whitelist = cat_whitelist(kind);
    const char* counted = cat_counted_family(kind);
    size_t counted_len = counted ? strlen(counted) : 0;
    const char* p = text;
    const char* end = text + size;
    struct cat_block block;
    unsigned headers = 0;

    assert(line);
    assert(base + size < CAT_NONE);
    block.index = -1;

    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);

        if( p < le && p[0] == '[' && deadline_ms > 0.0 && (++headers & 63u) == 0 && p > text
            && cat_now_ms() >= deadline_ms )
            break;
        (*line)++;
        if( p < le && p[0] == '[' )
        {
            const char* rb = memchr(p, ']', (size_t)(le - p));
            cat_block_close(src, &block, (uint32_t)(base + (size_t)(p - text)));
            if( rb && rb > p + 1 )
            {
                struct cat_contrib* c = cat_contrib_add(src, kind, prov);
                c->name = cat_pool_add(src, p + 1, (size_t)(rb - (p + 1)));
                c->off = (uint32_t)(base + (size_t)(p - text));
                c->line = *line;
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
    cat_block_close(src, &block, (uint32_t)(base + (size_t)(p - text)));
    return (size_t)(p - text);
}

/** A whole blocks file at once. */
static void
cat_parse_blocks(
    struct cat_source* src,
    const char* text,
    size_t size,
    int kind,
    int prov,
    int keep_all)
{
    uint32_t line = 0;
    (void)cat_parse_blocks_at(src, text, size, 0, &line, kind, prov, keep_all, 0.0);
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
                        ul = value & 0xFFFF;
                    else
                        ov = value & 0xFFFF;
                }
                while( s < le && *s != ' ' )
                    s++;
            }
            if( ul || ov )
            {
                /* Sixteen bits each: an underlay is as wide as an overlay
                 * (a byte for it folded underlay 1102 onto 78). */
                uint32_t key = (ov << 16) | ul;
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
    if( src->type == CAT_SRC_PENDING )
    {
        /* What the ledger line and the block will say once they are files:
         * the id (as pack/<type>.alloc binds it), then the block. */
        struct cat_contrib* c = cat_contrib_add(src, src->kind, TORIRSMAPED_PROV_ALLOC);
        const char* name = strrchr(src->path, '#');
        assert(name);
        c->id = src->mem_id;
        c->line = 1;
        c->name = cat_pool_str(src, name + 1);
        cat_parse_blocks(src, src->mem, src->mem_len, src->kind, TORIRSMAPED_PROV_CACHE, 0);
        return;
    }
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
        /* A scan sees files; an unsaved record is not one. */
        if( !catalog->sources[i].seen && catalog->sources[i].type != CAT_SRC_PENDING )
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

static void
cat_index_rehash(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t slots);

static int
cat_record_new(
    struct ToriRSMapEdCatalog* catalog,
    int kind,
    const char* name)
{
    struct cat_record* rec;
    uint32_t slot;
    /* A live merge grows as it goes (no caller holds a record pointer
     * across this), and rehashes once it outgrows the warm build's estimate
     * (cat_warm_queue): the chains average one record a slot at most. A full
     * merge sizes both for every contribution and never grows here. */
    catalog->records = cat_grow(
        catalog->records, &catalog->records_cap, catalog->record_count + 1, sizeof(*catalog->records));
    if( (uint32_t)catalog->record_count > catalog->hash_mask )
        cat_index_rehash(catalog, (catalog->hash_mask + 1) * 2);
    rec = &catalog->records[catalog->record_count];
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
    {
        rec->id = c->id;
        if( catalog->chains_live )
        {
            uint32_t slot = ((uint32_t)rec->id * 2654435761u ^ rec->kind * 40503u) & catalog->hash_mask;
            rec->next_id = catalog->id_heads[slot];
            catalog->id_heads[slot] = r;
        }
    }
    if( c->display != CAT_NONE )
    {
        const char* display = src->pool + c->display;
        if( catalog->chains_live )
        {
            uint32_t slot = cat_hash_lower(rec->kind, display) & catalog->hash_mask;
            if( !rec->display )
            {
                rec->next_display = catalog->display_heads[slot];
                catalog->display_heads[slot] = r;
            }
            else if( slot != (cat_hash_lower(rec->kind, rec->display) & catalog->hash_mask) )
                catalog->chains_stale = 1; /* renamed into another chain: rebuilt at the end */
        }
        rec->display = display;
    }

    catalog->refs = cat_grow(catalog->refs, &catalog->refs_cap, catalog->ref_count + 1, sizeof(*catalog->refs));
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
    int ul_count,
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
        int ov = (int)(key >> 16);
        int ul = (int)(key & 0xFFFF);
        int32_t rgb = -1;
        if( ov > 0 && ov - 1 < ov_count )
            rgb = ov_rgb[ov - 1];
        if( rgb < 0 && ul > 0 && ul - 1 < ul_count )
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
    /* Both tables by record id, as long as the highest id the tree states:
     * the underlays were 256 slots, the cache's own, and a record made in
     * the tree (or a ported lane's, 1100 and up) coloured nothing. */
    int32_t* ul_rgb = NULL;
    int ul_count = 0;
    int32_t* ov_rgb = NULL;
    int ov_count = 0;
    int square_cap = 0;
    int area_list_cap = 0;

    for( int r = 0; r < catalog->record_count; r++ )
    {
        const struct cat_record* rec = &catalog->records[r];
        if( rec->kind == TORIRSMAPED_CATALOG_OVERLAY && rec->id >= ov_count )
            ov_count = rec->id + 1;
        if( rec->kind == TORIRSMAPED_CATALOG_UNDERLAY && rec->id >= ul_count )
            ul_count = rec->id + 1;
    }
    if( ul_count > 0 )
    {
        ul_rgb = ToriMem_Alloc(ToriMem_Editor(), (size_t)ul_count * sizeof(*ul_rgb));
        assert(ul_rgb);
        for( int i = 0; i < ul_count; i++ )
            ul_rgb[i] = -1;
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
        if( rec->kind == TORIRSMAPED_CATALOG_UNDERLAY )
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
                sq->rgb = cat_square_rgb(src, ul_rgb, ul_count, ov_rgb, ov_count);
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
    ToriMem_Free(ToriMem_Editor(), ul_rgb);
}

/** Facts that need the whole index: npcs with a Talk-to script, vars read
 *  by multi shells. */
static void
cat_merge_derived_one(
    struct ToriRSMapEdCatalog* catalog,
    int r)
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

static void
cat_merge_derived(struct ToriRSMapEdCatalog* catalog)
{
    for( int r = 0; r < catalog->record_count; r++ )
        cat_merge_derived_one(catalog, r);
}

/** Ids first (they name records), then cache blocks, then everything that
 *  overrides or annotates them -- the order a field lookup's "last one
 *  wins" relies on. Source types in no pass are not merged (spawn counts and
 *  world-map claims feed the atlas from their sources). */
static const int k_cat_merge_order[][5] = {
    { CAT_SRC_COMPACK, CAT_SRC_PACK, CAT_SRC_ALLOC, CAT_SRC_IF_COMPACK, CAT_SRC_CS2 },
    { CAT_SRC_CONFIG, CAT_SRC_JM2, CAT_SRC_JL2, CAT_SRC_IF, CAT_SRC_CS2B },
    { CAT_SRC_OVERLAY, CAT_SRC_NPC_STATS, CAT_SRC_NPC_COMBAT, CAT_SRC_MEMBER_CLIENT, CAT_SRC_MEMBER_SERVER },
    { CAT_SRC_RS2, CAT_SRC_PENDING, -1, -1, -1 },
};
#define CAT_MERGE_PASSES ((int)(sizeof(k_cat_merge_order) / sizeof(k_cat_merge_order[0])))

/** The merge pass a source type's contributions join in, or
 *  CAT_MERGE_PASSES when it is not merged. */
static int
cat_merge_pass(int type)
{
    for( int pass = 0; pass < CAT_MERGE_PASSES; pass++ )
    {
        for( int t = 0; t < 5; t++ )
        {
            if( k_cat_merge_order[pass][t] == type )
                return pass;
        }
    }
    return CAT_MERGE_PASSES;
}

/** An empty index with room for `records` records and refs and `slots`
 *  hash slots (a power of two). */
static void
cat_index_reset(
    struct ToriRSMapEdCatalog* catalog,
    int records,
    uint32_t slots)
{
    assert(records > 0);
    assert(slots > 0);
    assert((slots & (slots - 1)) == 0);
    ToriMem_Free(ToriMem_Editor(), catalog->records);
    ToriMem_Free(ToriMem_Editor(), catalog->refs);
    ToriMem_Free(ToriMem_Editor(), catalog->name_heads);
    ToriMem_Free(ToriMem_Editor(), catalog->display_heads);
    ToriMem_Free(ToriMem_Editor(), catalog->id_heads);
    catalog->records = ToriMem_Alloc(ToriMem_Editor(), (size_t)records * sizeof(*catalog->records));
    catalog->refs = ToriMem_Alloc(ToriMem_Editor(), (size_t)records * sizeof(*catalog->refs));
    catalog->name_heads = ToriMem_Alloc(ToriMem_Editor(), slots * sizeof(int32_t));
    catalog->display_heads = ToriMem_Alloc(ToriMem_Editor(), slots * sizeof(int32_t));
    catalog->id_heads = ToriMem_Alloc(ToriMem_Editor(), slots * sizeof(int32_t));
    assert(catalog->records);
    assert(catalog->refs);
    assert(catalog->name_heads);
    assert(catalog->display_heads);
    assert(catalog->id_heads);
    catalog->records_cap = records;
    catalog->refs_cap = records;
    catalog->hash_mask = slots - 1;
    catalog->record_count = 0;
    catalog->ref_count = 0;
    catalog->chains_stale = 0;
    for( uint32_t i = 0; i < slots; i++ )
    {
        catalog->name_heads[i] = -1;
        catalog->display_heads[i] = -1;
        catalog->id_heads[i] = -1;
    }
}

/** Rebuild the three hash chains over `slots` slots from the records. */
static void
cat_index_rehash(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t slots)
{
    assert((slots & (slots - 1)) == 0);
    if( slots != catalog->hash_mask + 1 )
    {
        ToriMem_Free(ToriMem_Editor(), catalog->name_heads);
        ToriMem_Free(ToriMem_Editor(), catalog->display_heads);
        ToriMem_Free(ToriMem_Editor(), catalog->id_heads);
        catalog->name_heads = ToriMem_Alloc(ToriMem_Editor(), slots * sizeof(int32_t));
        catalog->display_heads = ToriMem_Alloc(ToriMem_Editor(), slots * sizeof(int32_t));
        catalog->id_heads = ToriMem_Alloc(ToriMem_Editor(), slots * sizeof(int32_t));
        assert(catalog->name_heads);
        assert(catalog->display_heads);
        assert(catalog->id_heads);
        catalog->hash_mask = slots - 1;
    }
    for( uint32_t i = 0; i < slots; i++ )
    {
        catalog->name_heads[i] = -1;
        catalog->display_heads[i] = -1;
        catalog->id_heads[i] = -1;
    }
    for( int r = 0; r < catalog->record_count; r++ )
    {
        struct cat_record* rec = &catalog->records[r];
        uint32_t slot = cat_hash_lower(rec->kind, rec->name) & catalog->hash_mask;
        rec->next_name = catalog->name_heads[slot];
        catalog->name_heads[slot] = r;
        rec->next_display = -1;
        rec->next_id = -1;
        if( rec->display )
        {
            slot = cat_hash_lower(rec->kind, rec->display) & catalog->hash_mask;
            rec->next_display = catalog->display_heads[slot];
            catalog->display_heads[slot] = r;
        }
        if( rec->id >= 0 )
        {
            slot = ((uint32_t)rec->id * 2654435761u ^ rec->kind * 40503u) & catalog->hash_mask;
            rec->next_id = catalog->id_heads[slot];
            catalog->id_heads[slot] = r;
        }
    }
    catalog->chains_stale = 0;
}

static void
cat_merge(struct ToriRSMapEdCatalog* catalog)
{
    int total = 0;
    uint32_t slots = 1024;

    for( int s = 0; s < catalog->source_count; s++ )
        total += catalog->sources[s].contrib_count;
    while( slots < (uint32_t)total * 2 )
        slots *= 2;

    cat_index_reset(catalog, total + 1, slots);
    catalog->chains_live = 0;
    for( int pass = 0; pass < CAT_MERGE_PASSES; pass++ )
    {
        for( int s = 0; s < catalog->source_count; s++ )
        {
            if( cat_merge_pass(catalog->sources[s].type) != pass )
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
    catalog->slice_bytes = TORIRSMAPED_CATALOG_SLICE_BYTES;
    return catalog;
}

static void
cat_job_abort(struct ToriRSMapEdCatalog* catalog);

static void
cat_warm_thread_join(struct ToriRSMapEdCatalog* catalog);

void
ToriRSMapEdCatalog_Free(struct ToriRSMapEdCatalog* catalog)
{
    if( !catalog )
        return;
    cat_warm_thread_join(catalog);
    cat_job_abort(catalog);
    ToriMem_Free(ToriMem_Editor(), catalog->held.hits);
    ToriMem_Free(ToriMem_Editor(), catalog->kind_records);
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
    ToriMem_Free(ToriMem_Editor(), catalog->pending);
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
            cat_source_trim(&catalog->sources[i]);
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

    cat_warm_thread_join(catalog);
    cat_job_abort(catalog);
    memset(&stats, 0, sizeof(stats));
    cat_scan(catalog, maps_only);
    stats.scan_ms = cat_now_ms() - t0;
    changed = cat_finish(catalog, 1, !maps_only, &stats);
    /* TORIRS_EDITOR_FRAME_STATS: the first build is the "catalog ready"
     * startup mark (the mark keeps the first; a sweep changes nothing). */
    FrameStats_Mark(FRAME_STATS_MARK_CATALOG_READY);
    if( out_stats )
        *out_stats = stats;
    return changed;
}

/* ---- the warm build's worker thread ---------------------------------- */

/**
 * TORIRS_CATALOG_WARM_THREAD=0 keeps the warm build in the service pass's
 * slices (the shape before 2026-11-03: 6 ms of every frame for the ~25 s the
 * build took at that pace, and a Catalog or Square picker opened sooner
 * stalled for the rest of it). With the thread the frame pays nothing and
 * the build takes the ~4-7 s of work it is.
 */
static int
cat_warm_thread_wanted(void)
{
#if CAT_WARM_THREAD
    static int wanted = -1;
    if( wanted < 0 )
    {
        const char* env = getenv("TORIRS_CATALOG_WARM_THREAD");
        wanted = !(env && env[0] == '0');
    }
    return wanted;
#else
    return 0;
#endif
}

static int
cat_job_step(
    struct ToriRSMapEdCatalog* catalog,
    double deadline_ms);

#if CAT_WARM_THREAD
static void
cat_warm_thread_run(struct ToriRSMapEdCatalog* catalog)
{
    (void)cat_job_step(catalog, 0.0);
    /* Release: every write of the build is visible to the join's acquire. */
    __atomic_store_n(&catalog->job.thread_done, 1, __ATOMIC_RELEASE);
    ToriMem_ThreadExit();
}

#ifdef _WIN32
static unsigned __stdcall
cat_warm_thread_main(void* arg)
{
    cat_warm_thread_run(arg);
    return 0;
}

/** 1 when the thread took the build; 0: no thread, the slices build it. */
static int
cat_warm_thread_spawn(struct ToriRSMapEdCatalog* catalog)
{
    /* _beginthreadex, not CreateThread: the build uses the C runtime. */
    uintptr_t const handle = _beginthreadex(NULL, 0, cat_warm_thread_main, catalog, 0, NULL);
    if( handle == 0 )
        return 0;
    catalog->job.thread = (HANDLE)handle;
    return 1;
}

static void
cat_warm_thread_wait(struct ToriRSMapEdCatalog* catalog)
{
    WaitForSingleObject(catalog->job.thread, INFINITE);
    CloseHandle(catalog->job.thread);
}
#else
static void*
cat_warm_thread_main(void* arg)
{
    cat_warm_thread_run(arg);
    return NULL;
}

/** 1 when the thread took the build; 0: no thread, the slices build it. */
static int
cat_warm_thread_spawn(struct ToriRSMapEdCatalog* catalog)
{
    return pthread_create(&catalog->job.thread, NULL, cat_warm_thread_main, catalog) == 0;
}

static void
cat_warm_thread_wait(struct ToriRSMapEdCatalog* catalog)
{
    pthread_join(catalog->job.thread, NULL);
}
#endif
#endif

/** Hand the begun warm build to its thread. */
static void
cat_warm_thread_start(struct ToriRSMapEdCatalog* catalog)
{
#if CAT_WARM_THREAD
    struct cat_job* job = &catalog->job;
    assert(!job->thread_live);
    assert(job->phase != CAT_JOB_IDLE);
    job->thread_done = 0;
    job->shown_done_kib = 0;
    job->shown_total_kib = 0;
    if( !cat_warm_thread_spawn(catalog) )
        return; /* the slices below build it instead */
    job->thread_live = 1;
#else
    (void)catalog;
#endif
}

/** Whether the thread is still building: a request or a service pass must
 *  not read the catalog until it has joined. */
static int
cat_warm_thread_done(const struct ToriRSMapEdCatalog* catalog)
{
    return __atomic_load_n(&catalog->job.thread_done, __ATOMIC_ACQUIRE) != 0;
}

/** Wait for the thread (a request that cannot answer without the index) and
 *  take the catalog back. A no-op when no thread runs. */
static void
cat_warm_thread_join(struct ToriRSMapEdCatalog* catalog)
{
#if CAT_WARM_THREAD
    struct cat_job* job = &catalog->job;
    if( !job->thread_live )
        return;
    cat_warm_thread_wait(catalog);
    job->thread_live = 0;
    job->thread_done = 0;
    catalog->counters.slices++;
    FrameStats_Mark(FRAME_STATS_MARK_CATALOG_READY);
#else
    (void)catalog;
#endif
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
    ToriMem_Free(ToriMem_Editor(), job->queue);
    job->queue = NULL;
    job->queue_count = 0;
    job->queue_cap = 0;
    ToriMem_Free(ToriMem_Editor(), job->win);
    job->win = NULL;
    job->win_cap = 0;
    job->win_off = 0;
    job->win_end = 0;
    job->part_started = 0;
    job->phase = CAT_JOB_IDLE;
    /* A warm build cut short leaves sources half parsed (still dirty: the
     * whole refresh that follows re-parses them) and a live index the
     * refresh's full merge replaces. */
    catalog->chains_live = 0;
}

/** Which kinds the warm build parses first: locs, npcs, objs, the rest. */
static int
cat_warm_group(int kind)
{
    if( kind == TORIRSMAPED_CATALOG_LOC || kind == TORIRSMAPED_CATALOG_NPC || kind == TORIRSMAPED_CATALOG_OBJ )
        return kind;
    return 3;
}

struct cat_warm_key
{
    int key;
    int source;
};

static int
cat_warm_key_cmp(
    const void* a,
    const void* b)
{
    const struct cat_warm_key* x = a;
    const struct cat_warm_key* y = b;
    if( x->key != y->key )
        return x->key - y->key;
    return x->source - y->source;
}

/**
 * The warm build's parse order, once the scan is whole: every dirty source,
 * by kind group (locs first, cat_warm_group), then merge pass, then path
 * order. A record's contributions all share its kind, so within a group
 * this is the full merge's pass order, and the live index merged a source
 * at a time ends as cat_merge would have built it.
 */
static void
cat_warm_queue(struct ToriRSMapEdCatalog* catalog)
{
    struct cat_job* job = &catalog->job;
    struct cat_warm_key* keys;
    int n = 0;
    int64_t estimate = 0;
    uint32_t slots = 4096;

    job->stats.removed = cat_compact(catalog);
    keys = ToriMem_Alloc(ToriMem_Editor(), (size_t)(catalog->source_count + 1) * sizeof(*keys));
    assert(keys);
    job->bytes_total = 0;
    job->bytes_done = 0;
    /* Every source: one already parsed (none, unless an earlier build was
     * cut short) is merged without a parse. */
    for( int s = 0; s < catalog->source_count; s++ )
    {
        const struct cat_source* src = &catalog->sources[s];
        keys[n].key = cat_warm_group(src->kind) * (CAT_MERGE_PASSES + 1) + cat_merge_pass(src->type);
        keys[n].source = s;
        job->bytes_total += src->size;
        /* Records are mostly named by the id tables (a compack line is ~16
         * bytes); every other merged file names a few. */
        if( src->type == CAT_SRC_COMPACK || src->type == CAT_SRC_PACK || src->type == CAT_SRC_ALLOC )
            estimate += src->size / 16;
        else if( cat_merge_pass(src->type) < CAT_MERGE_PASSES )
            estimate += 2;
        n++;
    }
    while( slots < (uint64_t)estimate && slots < (1u << 24) )
        slots *= 2;
    qsort(keys, (size_t)n, sizeof(*keys), cat_warm_key_cmp);
    job->queue = cat_grow(job->queue, &job->queue_cap, n + 1, sizeof(*job->queue));
    for( int i = 0; i < n; i++ )
        job->queue[i] = keys[i].source;
    ToriMem_Free(ToriMem_Editor(), keys);
    job->queue_count = n;
    job->parse_at = 0;
    job->parsed = 0;
    job->merge_at = 0;
    job->part_started = 0;
    job->finish_at = 0;
    cat_index_reset(catalog, 1024, slots);
    catalog->chains_live = 1;
    catalog->revision++;
}

/** A configs/all.<type>, its .compack or an overlay at least this big is
 *  parsed in parts, so the slice's deadline holds inside it. */
#define CAT_PART_MIN_BYTES (64 * 1024)

static int
cat_warm_parted(
    const struct cat_source* src,
    int started)
{
    if( src->type != CAT_SRC_CONFIG && src->type != CAT_SRC_OVERLAY && src->type != CAT_SRC_COMPACK )
        return 0;
    return started || src->size >= CAT_PART_MIN_BYTES;
}

/**
 * One part of a large configs/all.<type>, .compack or overlay: at most
 * `slice_bytes` from where the last part stopped, cut before the last `[`
 * header in it (a compack: after the last whole line) so nothing spans two
 * parts (a block longer than the window widens it), and stopped at the
 * deadline. Returns 1 once the file is parsed whole.
 */
static int
cat_warm_parse_part(
    struct ToriRSMapEdCatalog* catalog,
    struct cat_source* src,
    double deadline_ms)
{
    struct cat_job* job = &catalog->job;
    size_t window = catalog->slice_bytes;
    int const overlay = src->type == CAT_SRC_OVERLAY;
    int const compack = src->type == CAT_SRC_COMPACK;

    const char* text;
    size_t size;
    size_t used;

    if( !job->part_started )
    {
        cat_source_clear(src);
        job->part_started = 1;
        job->part_off = 0;
        job->part_line = 0;
        job->win_end = 0;
    }
    /* The window read last slice still holds unparsed whole blocks when a
     * deadline stopped the parse inside it: no second read. */
    if( !(job->part_off >= job->win_off && job->part_off < job->win_end) )
    {
        for( ;; )
        {
            size_t got = 0;
            size_t cut;
            if( !cat_read_at(catalog, src->path, job->part_off, window, &job->win, &job->win_cap, &got) || got == 0 )
                return 1;
            job->win_final = got < window;
            cut = got;
            if( !job->win_final )
            {
                if( compack )
                    for( cut = got; cut > 0 && job->win[cut - 1] != '\n'; cut-- )
                        ;
                else
                    for( cut = got - 1; cut > 0 && !(job->win[cut] == '[' && job->win[cut - 1] == '\n'); cut-- )
                        ;
                if( cut == 0 )
                {
                    window *= 2;
                    continue;
                }
            }
            job->win_off = job->part_off;
            job->win_end = job->part_off + cut;
            break;
        }
    }
    text = job->win + (job->part_off - job->win_off);
    size = job->win_end - job->part_off;
    if( compack )
        used = cat_parse_idname_at(src, text, size, &job->part_line, src->kind, TORIRSMAPED_PROV_COMPACK, NULL,
            deadline_ms);
    else
        used = cat_parse_blocks_at(src, text, size, job->part_off, &job->part_line, src->kind,
            overlay ? TORIRSMAPED_PROV_OVERLAY : TORIRSMAPED_PROV_CACHE, overlay, deadline_ms);
    job->part_off += used;
    catalog->counters.parts++;
    if( (long)used > catalog->counters.part_bytes_max )
        catalog->counters.part_bytes_max = (long)used;
    return job->win_final && job->part_off == job->win_end;
}

/**
 * The warm build after its scan, until `deadline_ms` (0: to its end): each
 * queued source parsed and then merged into the live index, which a
 * TORIRSMAPED_CATALOG_PROGRESS query answers from meanwhile; then the
 * derived fields, the areas and the atlas. Returns 1 when the index is built.
 */
static void
cat_progress_publish(struct ToriRSMapEdCatalog* catalog);

static int
cat_warm_step(
    struct ToriRSMapEdCatalog* catalog,
    double deadline_ms)
{
    struct cat_job* job = &catalog->job;

    while( job->phase == CAT_JOB_PARSE )
    {
        int const s = job->queue_count > job->parse_at ? job->queue[job->parse_at] : -1;
        struct cat_source* src;
        if( s < 0 )
        {
            job->phase = CAT_JOB_FINISH;
            break;
        }
        if( deadline_ms > 0.0 && cat_now_ms() >= deadline_ms )
            return 0;
        src = &catalog->sources[s];
        if( !job->parsed )
        {
            if( !src->dirty )
            {
                /* parsed already */
            }
            else if( cat_warm_parted(src, job->part_started) )
            {
                if( !cat_warm_parse_part(catalog, src, deadline_ms) )
                    continue;
            }
            else
                cat_parse_source(catalog, src);
            if( src->dirty )
            {
                job->stats.parsed++;
                cat_source_trim(src);
            }
            src->dirty = 0;
            job->parsed = 1;
            job->merge_at = 0;
            continue;
        }
        if( cat_merge_pass(src->type) < CAT_MERGE_PASSES )
        {
            int const start = job->merge_at;
            while( job->merge_at < src->contrib_count )
            {
                cat_merge_contrib(catalog, s, job->merge_at++);
                if( deadline_ms > 0.0 && (job->merge_at & 1023) == 0 && cat_now_ms() >= deadline_ms )
                    break;
            }
            if( job->merge_at > start )
                catalog->revision++;
            if( job->merge_at < src->contrib_count )
                return 0;
            catalog->counters.incremental++;
        }
        job->bytes_done += src->size;
        cat_progress_publish(catalog);
        job->parse_at++;
        job->parsed = 0;
        job->part_started = 0;
    }

    /* CAT_JOB_FINISH */
    while( job->finish_at < catalog->record_count )
    {
        cat_merge_derived_one(catalog, job->finish_at++);
        if( deadline_ms > 0.0 && (job->finish_at & 1023) == 0 && cat_now_ms() >= deadline_ms )
            return 0;
    }
    if( catalog->chains_stale )
        cat_index_rehash(catalog, catalog->hash_mask + 1);
    cat_merge_areas(catalog);
    cat_merge_atlas(catalog);
    catalog->chains_live = 0;
    catalog->storages_changed = 0;
    catalog->built = 1;
    catalog->invalid = 0;
    catalog->last_refresh = time(NULL);
    catalog->revision++;
    catalog->counters.merges++;
    job->stats.sources = catalog->source_count;
    job->stats.records = catalog->record_count;
    job->stats.squares = catalog->square_count;
    if( catalog->scratch_cap > 4 * 1024 * 1024 + 1 )
    {
        ToriMem_Free(ToriMem_Editor(), catalog->scratch);
        catalog->scratch = NULL;
        catalog->scratch_cap = 0;
    }
    return 1;
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
        if( catalog->built )
            job->phase = CAT_JOB_IDLE;
        else
        {
            cat_warm_queue(catalog);
            job->phase = CAT_JOB_PARSE;
        }
    }
    if( job->phase == CAT_JOB_PARSE || job->phase == CAT_JOB_FINISH )
    {
        double const t1 = cat_now_ms();
        int const done = cat_warm_step(catalog, deadline_ms);
        job->stats.parse_ms += cat_now_ms() - t1;
        if( !done )
            return 0;
        cat_job_abort(catalog);
        catalog->counters.sweeps++;
        if( getenv("TORIRS_CATALOG_TRACE") )
            fprintf(stderr, "catalog: warm build done: scan %.1f ms, %d parsed and merged (%.1f ms), %d removed\n",
                job->stats.scan_ms, job->stats.parsed, job->stats.parse_ms, job->stats.removed);
        return 1;
    }
    /* The scan is whole: drop what it did not see, parse (a sweep's) changes
     * and merge -- one step, so no request sees half of it. */
    cat_job_abort(catalog);
    (void)cat_finish(catalog, 1, 1, &job->stats);
    catalog->counters.sweeps++;
    if( getenv("TORIRS_CATALOG_TRACE") )
    {
        size_t pool_bytes = 0;
        size_t pool_cap = 0;
        size_t contrib_bytes = 0;
        fprintf(stderr, "catalog: pass done: scan %.1f ms, %d parsed (%.1f ms), %d removed, merge %.1f ms\n",
            job->stats.scan_ms, job->stats.parsed, job->stats.parse_ms, job->stats.removed, job->stats.merge_ms);
        for( int i = 0; i < catalog->source_count; i++ )
        {
            pool_bytes += catalog->sources[i].pool_len;
            pool_cap += catalog->sources[i].pool_cap;
            contrib_bytes += (size_t)catalog->sources[i].contrib_cap * sizeof(struct cat_contrib);
        }
        fprintf(stderr,
            "catalog: memory: editor group live %.1f MB (peak %.1f MB); %d sources, pools %.1f MB used of %.1f MB,"
            " contribs %.1f MB, records %.1f MB, refs %.1f MB, scratch %.1f MB\n",
            (double)ToriMem_Editor()->live_bytes / 1048576.0, (double)ToriMem_Editor()->peak_bytes / 1048576.0,
            catalog->source_count, (double)pool_bytes / 1048576.0, (double)pool_cap / 1048576.0,
            (double)contrib_bytes / 1048576.0, (double)catalog->record_count * sizeof(struct cat_record) / 1048576.0,
            (double)catalog->ref_count * sizeof(struct cat_ref) / 1048576.0, (double)catalog->scratch_cap / 1048576.0);
    }
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
    /* The thread owns the catalog until it is done; then the join takes it
     * back (and the pass is over: the thread ran the job to its end). */
    if( catalog->job.thread_live )
    {
        if( !cat_warm_thread_done(catalog) )
            return 1;
        cat_warm_thread_join(catalog);
        return 0;
    }
    /* The warm build starts on its own; a sweep only when a request asked. */
    if( catalog->job.phase == CAT_JOB_IDLE && !catalog->built )
    {
        cat_job_begin(catalog);
        if( catalog->job.phase != CAT_JOB_IDLE && cat_warm_thread_wanted() )
        {
            cat_warm_thread_start(catalog);
            if( catalog->job.thread_live )
                return 1;
        }
    }
    if( catalog->job.phase == CAT_JOB_IDLE )
        return 0;
    {
        int const phase = catalog->job.phase;
        int const at = catalog->job.parse_at;
        done = cat_job_step(catalog, t0 + budget_ms);
        took = cat_now_ms() - t0;
        /* TORIRS_CATALOG_TRACE=1: a slice that ran past twice its budget, and
         * where it was */
        if( took > budget_ms * 2.0 && getenv("TORIRS_CATALOG_TRACE") )
            fprintf(stderr, "catalog: slice %.1f ms (budget %.1f) in phase %d at queue %d of %d\n", took, budget_ms,
                phase, at, catalog->job.queue_count);
    }
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
ToriRSMapEdCatalog_Building(const struct ToriRSMapEdCatalog* catalog)
{
    assert(catalog);
    return catalog->job.thread_live && !cat_warm_thread_done(catalog);
}

int
ToriRSMapEdCatalog_Working(const struct ToriRSMapEdCatalog* catalog)
{
    assert(catalog);
    /* `phase` is the thread's while it runs. */
    return catalog->job.thread_live || catalog->job.phase != CAT_JOB_IDLE;
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
    /* A warm build on its thread: this request needs the index, so it waits
     * for the rest of the build -- what a request before the build was done
     * always did, now against a thread that has run flat out since boot. */
    cat_warm_thread_join(catalog);
    if( !catalog->built )
    {
        if( catalog->job.phase != CAT_JOB_IDLE )
            (void)cat_job_step(catalog, 0.0);
        if( !catalog->built )
            (void)cat_refresh(catalog, 0, NULL);
        FrameStats_Mark(FRAME_STATS_MARK_CATALOG_READY);
        return;
    }
    if( catalog->job.phase == CAT_JOB_IDLE
        && (catalog->invalid || time(NULL) - catalog->last_refresh >= catalog->refresh_interval_s) )
        cat_job_begin(catalog);
}

/**
 * What a TORIRSMAPED_CATALOG_PROGRESS query runs first: never the rest of
 * the warm build. With an index it is cat_request_index; while the warm
 * build runs the query answers from the live index as it stands (partial);
 * and when nothing has started it (a process whose service pass has not run
 * yet) the build begins here with one warm slice, so a small tree is whole
 * by the time the answer is written.
 */
static void
cat_request_partial(struct ToriRSMapEdCatalog* catalog)
{
    /* The worker thread owns the catalog until it is done (HandleQuery
     * answers a PROGRESS asker without reading it meanwhile). */
    if( catalog->job.thread_live )
    {
        assert(cat_warm_thread_done(catalog));
        cat_warm_thread_join(catalog);
    }
    if( catalog->built )
    {
        cat_request_index(catalog);
        return;
    }
    if( catalog->job.phase == CAT_JOB_IDLE )
    {
        cat_job_begin(catalog);
        if( catalog->job.phase != CAT_JOB_IDLE )
            (void)cat_job_step(catalog, cat_now_ms() + TORIRSMAPED_CATALOG_WARM_SLICE_MS);
    }
}

/** What ToriRSMapEdCatalog_Progress says from the job's own fields: the main
 *  thread's, or the worker's (which publishes it, cat_progress_publish). */
static int
cat_progress_of(
    const struct ToriRSMapEdCatalog* catalog,
    uint32_t* out_done_kib,
    uint32_t* out_total_kib)
{
    const struct cat_job* job = &catalog->job;
    int64_t done;

    if( catalog->built )
    {
        *out_done_kib = 1;
        *out_total_kib = 1;
        return 0;
    }
    if( job->phase != CAT_JOB_PARSE && job->phase != CAT_JOB_FINISH )
    {
        /* still scanning: the work is not known yet */
        *out_done_kib = 0;
        *out_total_kib = 0;
        return 1;
    }
    done = job->bytes_done + (job->part_started ? (int64_t)job->part_off : 0);
    *out_total_kib = (uint32_t)((job->bytes_total + 1023) / 1024) + 1;
    /* the last unit is the finish (derived fields, areas, atlas) */
    *out_done_kib = (uint32_t)(done / 1024);
    if( *out_done_kib >= *out_total_kib )
        *out_done_kib = *out_total_kib - 1;
    return 1;
}

static void
cat_progress_publish(struct ToriRSMapEdCatalog* catalog)
{
    uint32_t done;
    uint32_t total;
    (void)cat_progress_of(catalog, &done, &total);
    __atomic_store_n(&catalog->job.shown_done_kib, done, __ATOMIC_RELAXED);
    __atomic_store_n(&catalog->job.shown_total_kib, total, __ATOMIC_RELAXED);
}

int
ToriRSMapEdCatalog_Progress(
    const struct ToriRSMapEdCatalog* catalog,
    uint32_t* out_done_kib,
    uint32_t* out_total_kib)
{
    assert(catalog);
    assert(out_done_kib);
    assert(out_total_kib);
    /* The worker thread owns the job: what it last published (0 of 0, "the
     * work is not known yet", until its first source is merged). */
    if( ToriRSMapEdCatalog_Building(catalog) )
    {
        *out_done_kib = __atomic_load_n(&catalog->job.shown_done_kib, __ATOMIC_RELAXED);
        *out_total_kib = __atomic_load_n(&catalog->job.shown_total_kib, __ATOMIC_RELAXED);
        return 1;
    }
    return cat_progress_of(catalog, out_done_kib, out_total_kib);
}

void
ToriRSMapEdCatalog_SetSliceBytes(
    struct ToriRSMapEdCatalog* catalog,
    size_t bytes)
{
    assert(catalog);
    assert(bytes >= 4096);
    catalog->slice_bytes = bytes;
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

/* ---- what one save wrote: ToriRSMapEdCatalog_RefreshPaths ---- */

/**
 * What the full scan would make of the file at content-relative `rel`: 1 with
 * its type and kind when some walk records it as a source, else 0 (outside
 * every root, below a root that is not walked recursively, under a name
 * starting with '.', or a name no classifier takes). A square file's maps
 * storage ("maps" or "ported/<lane>/maps") goes to `storage`, "" otherwise.
 */
static int
cat_classify_path(
    const char* rel,
    int* out_type,
    int* out_kind,
    char* storage,
    size_t storage_size)
{
    const char* slash = strrchr(rel, '/');
    const char* name;
    char rel_dir[CAT_PATH_MAX];

    storage[0] = '\0';
    if( !slash || slash == rel || strlen(rel) >= CAT_PATH_MAX )
        return 0;
    name = slash + 1;
    /* cat_walk_entry skips every name starting with '.', file or directory. */
    if( name[0] == '\0' || name[0] == '.' )
        return 0;
    snprintf(rel_dir, sizeof(rel_dir), "%.*s", (int)(slash - rel), rel);
    for( int r = 0; r < CAT_ROOT_COUNT; r++ )
    {
        size_t const n = strlen(k_cat_roots[r].rel);
        if( strncmp(rel_dir, k_cat_roots[r].rel, n) != 0 || (rel_dir[n] != '\0' && rel_dir[n] != '/') )
            continue;
        if( rel_dir[n] == '/' )
        {
            if( !k_cat_roots[r].recursive )
                return 0;
            for( const char* at = rel_dir + n; *at; at++ )
            {
                if( at[0] == '/' && at[1] == '.' )
                    return 0;
            }
        }
        return k_cat_roots[r].classify(rel_dir, name, out_type, out_kind);
    }
    /* maps/, or a lane's: "ported/<lane>/maps" (cat_lanes). */
    if( strcmp(rel_dir, "maps") != 0 )
    {
        const char* lane = rel_dir + 7;
        const char* lane_end;
        if( strncmp(rel_dir, "ported/", 7) != 0 || lane[0] == '.' )
            return 0;
        lane_end = strchr(lane, '/');
        if( !lane_end || lane_end == lane || strcmp(lane_end, "/maps") != 0 )
            return 0;
    }
    if( !cat_classify_maps(rel_dir, name, out_type, out_kind) )
        return 0;
    snprintf(storage, storage_size, "%s", rel_dir);
    return 1;
}

static int
cat_storage_known(
    const struct ToriRSMapEdCatalog* catalog,
    const char* storage)
{
    for( int i = 0; i < catalog->storage_count; i++ )
    {
        if( strcmp(catalog->storages[i], storage) == 0 )
            return 1;
    }
    return 0;
}

static int
cat_index_listed(
    const int* list,
    int count,
    int index)
{
    for( int i = 0; i < count; i++ )
    {
        if( list[i] == index )
            return 1;
    }
    return 0;
}

/** Drop the sources at `drop` (distinct indices), keeping every other
 *  source's `seen` and `dirty` -- unlike cat_compact, which a sweep in flight
 *  must be left to run at its own end. Returns how many went. */
static int
cat_drop_sources(
    struct ToriRSMapEdCatalog* catalog,
    const int* drop,
    int count)
{
    unsigned char* gone;
    int kept = 0;

    if( count == 0 )
        return 0;
    gone = ToriMem_Calloc(ToriMem_Editor(), (size_t)catalog->source_count, 1);
    assert(gone);
    for( int i = 0; i < count; i++ )
    {
        assert(drop[i] >= 0);
        assert(drop[i] < catalog->source_count);
        gone[drop[i]] = 1;
    }
    for( int i = 0; i < catalog->source_count; i++ )
    {
        if( gone[i] )
        {
            cat_source_free(&catalog->sources[i]);
            continue;
        }
        if( kept != i )
            catalog->sources[kept] = catalog->sources[i];
        kept++;
    }
    ToriMem_Free(ToriMem_Editor(), gone);
    catalog->source_count = kept;
    cat_path_index_rebuild(catalog);
    return count;
}

int
ToriRSMapEdCatalog_RefreshPaths(
    struct ToriRSMapEdCatalog* catalog,
    const char* const* rels,
    int count)
{
    double const t0 = cat_now_ms();
    double t1;
    double t2;
    int* touched;
    int* gone;
    int touched_count = 0;
    int gone_count = 0;
    int storages_stale = 0;
    int maps_changed = 0;
    int parsed = 0;
    int removed;
    int changed;

    assert(catalog);
    assert(count >= 0);
    if( count == 0 )
        return 0;
    assert(rels);
    /* A warm build that is done is taken back; one still running (or none
     * begun) reads the tree as it stands when it gets there. */
    if( catalog->job.thread_live && cat_warm_thread_done(catalog) )
        cat_warm_thread_join(catalog);
    if( catalog->job.thread_live || !catalog->built )
        return 0;
    /* A built index runs no warm phase: the job is idle or a sweep's scan. */
    assert(catalog->job.phase == CAT_JOB_IDLE || catalog->job.phase == CAT_JOB_SCAN);
    if( catalog->path_slot_count == 0 )
        cat_path_index_rebuild(catalog);

    touched = ToriMem_Alloc(ToriMem_Editor(), (size_t)count * sizeof(*touched));
    gone = ToriMem_Alloc(ToriMem_Editor(), (size_t)count * sizeof(*gone));
    assert(touched);
    assert(gone);
    for( int i = 0; i < count; i++ )
    {
        char full[CAT_PATH_MAX * 2];
        char storage[CAT_PATH_MAX];
        struct stat st;
        int type = 0;
        int kind = -1;
        int index;
        uint32_t hash;

        assert(rels[i]);
        if( !cat_classify_path(rels[i], &type, &kind, storage, sizeof(storage)) )
            continue;
        if( storage[0] && !cat_storage_known(catalog, storage) )
            storages_stale = 1;
        hash = cat_hash_bytes(rels[i]);
        index = cat_path_find(catalog, rels[i], hash);
        if( (index >= 0 && cat_index_listed(touched, touched_count, index))
            || (index >= 0 && cat_index_listed(gone, gone_count, index)) )
            continue;
        snprintf(full, sizeof(full), "%s/%s", catalog->content_dir, rels[i]);
        catalog->counters.stats++;
        if( stat(full, &st) != 0 || S_ISDIR(st.st_mode) )
        {
            /* Gone (or never a file): the source, if the index had one, goes. */
            if( index >= 0 )
                gone[gone_count++] = index;
            continue;
        }
        cat_found(catalog, rels[i], type, kind, &st);
        index = cat_path_find(catalog, rels[i], hash);
        assert(index >= 0);
        /* Re-parsed whatever its stat says: Windows mtimes are whole seconds
         * here, and a same-size rewrite inside one would look unchanged. */
        catalog->sources[index].dirty = 1;
        touched[touched_count++] = index;
    }

    /* A square in a lane directory the storage list does not have: list the
     * lanes again (what RefreshMaps's scan would have adopted). A sweep in
     * flight walks the lane list it began with and would drop that lane's
     * squares as unseen at its end, so it goes; its reason to run (the
     * interval, or a save's `invalid`) still stands for the next request. */
    if( storages_stale )
    {
        int lane_count;
        char** lanes = cat_lanes(catalog, &lane_count);
        char** sorted = cat_lanes_sorted(lanes, lane_count);
        cat_lanes_free(lanes, lane_count);
        if( cat_lanes_same(catalog->storages, catalog->storage_count, sorted, lane_count) )
            cat_lanes_free(sorted, lane_count);
        else
        {
            if( catalog->job.phase == CAT_JOB_SCAN )
                cat_job_abort(catalog);
            cat_storages_adopt(catalog, sorted, lane_count);
        }
    }

    t1 = cat_now_ms();
    /* Only these sources are parsed. A sweep in flight keeps what it has
     * found dirty for its own end (merged meanwhile from their last parse). */
    for( int i = 0; i < touched_count; i++ )
    {
        struct cat_source* src = &catalog->sources[touched[i]];
        cat_parse_source(catalog, src);
        cat_source_trim(src);
        src->dirty = 0;
        parsed++;
        if( cat_is_maps_type(src->type) )
            maps_changed = 1;
    }
    for( int i = 0; i < gone_count; i++ )
    {
        if( cat_is_maps_type(catalog->sources[gone[i]].type) )
            maps_changed = 1;
    }
    removed = cat_drop_sources(catalog, gone, gone_count);
    ToriMem_Free(ToriMem_Editor(), touched);
    ToriMem_Free(ToriMem_Editor(), gone);

    /* RefreshMaps's rule (cat_finish): any change merges; a square file
     * parsed or gone, or the lane list moved, is a maps change. */
    t2 = cat_now_ms();
    maps_changed |= catalog->storages_changed;
    changed = parsed > 0 || removed > 0 || catalog->storages_changed;
    if( changed )
    {
        if( maps_changed )
            catalog->maps_revision++;
        cat_merge(catalog);
        catalog->revision++;
        catalog->counters.merges++;
    }
    catalog->storages_changed = 0;
    catalog->counters.path_refreshes++;
    if( getenv("TORIRS_CATALOG_TRACE") )
        fprintf(stderr,
            "catalog: refresh paths: %d named, %d parsed, %d removed%s; stat %.1f ms, parse %.1f ms, merge %.1f ms\n",
            count, parsed, removed, storages_stale ? ", lanes re-listed" : "", t1 - t0, t2 - t1, cat_now_ms() - t2);
    /* all.loc alone is 86 MB: do not keep its buffer for the server's life */
    if( catalog->scratch_cap > 4 * 1024 * 1024 )
    {
        ToriMem_Free(ToriMem_Editor(), catalog->scratch);
        catalog->scratch = NULL;
        catalog->scratch_cap = 0;
    }
    return changed;
}

/* ---- records no file has yet: ToriRSMapEdCatalog_PendingAdd / _PendingSync ---- */

/** The index's source for unsaved record (kind, name), or -1; its path to
 *  `path` either way. */
static int
cat_pending_source(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* name,
    char* path,
    size_t path_size)
{
    /* Where a record view says the record is from: the file a save will put
     * it in, marked. No file of the tree starts with '(' or holds a '#' (and
     * the scan records neither), so it is nobody else's path. */
    snprintf(path, path_size, "(unsaved)/configs/all.%s#%s", ToriRSMapEdCatalog_KindName(kind), name);
    if( catalog->path_slot_count == 0 )
        cat_path_index_rebuild(catalog);
    return cat_path_find(catalog, path, cat_hash_bytes(path));
}

void
ToriRSMapEdCatalog_PendingAdd(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    int32_t id,
    const char* name)
{
    struct cat_pending* pending = NULL;

    assert(catalog);
    assert(kind < TORIRSMAPED_CATALOG_KIND_COUNT);
    assert(id >= 0);
    assert(name);
    assert(name[0]);
    assert(strlen(name) < sizeof(pending->name));
    for( int i = 0; i < catalog->pending_count && !pending; i++ )
        if( catalog->pending[i].kind == kind && strcmp(catalog->pending[i].name, name) == 0 )
            pending = &catalog->pending[i];
    if( !pending )
    {
        catalog->pending = cat_grow(
            catalog->pending, &catalog->pending_cap, catalog->pending_count + 1, sizeof(*catalog->pending));
        pending = &catalog->pending[catalog->pending_count++];
        memset(pending, 0, sizeof(*pending));
        pending->kind = kind;
        snprintf(pending->name, sizeof(pending->name), "%s", name);
    }
    /* An earlier copy of this name was undone and the name used again: the
     * index's source, if it still has one, is parsed again at the next sync
     * with this id. */
    pending->id = id;
    pending->revision = UINT32_MAX;
}

int
ToriRSMapEdCatalog_PendingSync(
    struct ToriRSMapEdCatalog* catalog,
    struct Editor_Doc* doc)
{
    int changed = 0;
    int kept = 0;

    assert(catalog);
    assert(doc);
    if( catalog->pending_count == 0 )
        return 0;
    /* As RefreshPaths: a warm build that is done is taken back; one still
     * running reads the tree as it stands, and the next request syncs. */
    if( catalog->job.thread_live && cat_warm_thread_done(catalog) )
        cat_warm_thread_join(catalog);
    if( catalog->job.thread_live || !catalog->built )
        return 0;

    for( int i = 0; i < catalog->pending_count; i++ )
    {
        struct cat_pending pending = catalog->pending[i];
        struct Editor_DocHandle handle;
        char key[EDITOR_DOC_KEY_MAX];
        char path[CAT_PATH_MAX];
        const struct Editor_TextDoc* text = NULL;
        int index;
        int saved;

        snprintf(key, sizeof(key), "%s:%s", ToriRSMapEdCatalog_KindName(pending.kind), pending.name);
        if( Editor_DocFind(doc, EDITOR_DOC_CFG, key, &handle) && handle.text && handle.text->loaded )
            text = handle.text;
        index = cat_pending_source(catalog, pending.kind, pending.name, path, sizeof(path));
        /* Written: the file's own source carries it from here (the save
         * named the file to RefreshPaths), and this entry is done. */
        saved = text && text->length > 0 && !text->dirty;
        if( !text || text->length == 0 || saved )
        {
            /* Saved, or the block is gone (the copy was undone; a redo
             * brings it back and the entry lists it again). */
            if( index >= 0 )
            {
                (void)cat_drop_sources(catalog, &index, 1);
                changed = 1;
            }
            pending.listed = 0;
            pending.revision = UINT32_MAX;
            if( !saved )
                catalog->pending[kept++] = pending;
            continue;
        }
        if( pending.listed && index >= 0 && pending.revision == text->revision )
        {
            catalog->pending[kept++] = pending;
            continue;
        }
        if( index < 0 )
        {
            struct cat_source* src;
            catalog->sources = cat_grow(
                catalog->sources, &catalog->source_cap, catalog->source_count + 1, sizeof(*catalog->sources));
            src = &catalog->sources[catalog->source_count];
            memset(src, 0, sizeof(*src));
            src->path = ToriMem_Strdup(ToriMem_Editor(), path);
            assert(src->path);
            src->hash = cat_hash_bytes(path);
            src->type = CAT_SRC_PENDING;
            src->kind = (int)pending.kind;
            src->seen = 1;
            index = catalog->source_count++;
            cat_path_index_rebuild(catalog);
        }
        {
            struct cat_source* src = &catalog->sources[index];
            ToriMem_Free(ToriMem_Editor(), src->mem);
            src->mem = ToriMem_Alloc(ToriMem_Editor(), text->length + 1);
            assert(src->mem);
            memcpy(src->mem, text->text, text->length);
            src->mem[text->length] = 0;
            src->mem_len = text->length;
            src->mem_id = pending.id;
            src->size = (int64_t)text->length;
            cat_parse_source(catalog, src);
            cat_source_trim(src);
            src->dirty = 0;
        }
        pending.listed = 1;
        pending.revision = text->revision;
        changed = 1;
        catalog->pending[kept++] = pending;
    }
    catalog->pending_count = kept;
    if( changed )
    {
        cat_merge(catalog);
        catalog->revision++;
        catalog->counters.merges++;
    }
    return changed;
}

int
ToriRSMapEdCatalog_RecordOverlays(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* name,
    char (*out)[TORIRSMAPED_CATALOG_OVERLAY_MAX],
    int max)
{
    int record;
    int count = 0;

    assert(catalog);
    assert(kind < TORIRSMAPED_CATALOG_KIND_COUNT);
    assert(name);
    assert(out);
    assert(max > 0);
    if( catalog->job.thread_live && cat_warm_thread_done(catalog) )
        cat_warm_thread_join(catalog);
    if( catalog->job.thread_live || !catalog->built )
        return -1;
    record = cat_find_exact(catalog, (int)kind, name);
    if( record < 0 )
        return 0;
    for( int32_t r = catalog->records[record].first_ref; r >= 0 && count < max; r = catalog->refs[r].next )
    {
        const struct cat_source* src = &catalog->sources[catalog->refs[r].source];
        int known = 0;

        if( src->type != CAT_SRC_OVERLAY || strncmp(src->path, "server/scripts/", 15) != 0
            || strlen(src->path + 15) >= TORIRSMAPED_CATALOG_OVERLAY_MAX )
            continue;
        for( int i = 0; i < count && !known; i++ )
            known = strcmp(out[i], src->path + 15) == 0;
        if( !known )
            snprintf(out[count++], TORIRSMAPED_CATALOG_OVERLAY_MAX, "%s", src->path + 15);
    }
    return count;
}

int
ToriRSMapEdCatalog_Built(const struct ToriRSMapEdCatalog* catalog)
{
    assert(catalog);
    /* `built` is the thread's while it runs (the join reads it next). */
    return !catalog->job.thread_live && catalog->built;
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
    /** A loc's placeable shapes (bit s: shape s), what `shapes` lists. */
    uint32_t loc_shapes;
    /** Whether the collect writes a loc's `shapes`, `size` and `layer` text
     *  (a filter on another key needs only the mask: cat_collect_for). */
    int loc_text;
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

/* ---- a loc's placeable shapes, size and layer ---- */

/** The shape whose models a placement of `shape` draws: wall decor 5..8
 *  draw shape 4's, the diagonal centrepiece 11 draws 10's
 *  (edpanel_ops.c Editor_LocShapeModelShape; the server links no panel). */
static int
cat_loc_model_shape(int shape)
{
    if( shape >= 4 && shape <= 8 )
        return 4;
    if( shape == 11 )
        return 10;
    return shape;
}

/** The order a loc's shapes are offered in: what is placed most
 *  (edpanel_ops.c k_loc_shape_order). `shapes` lists them in it. */
static const int k_cat_loc_shape_order[CAT_LOC_SHAPES] = {
    10, 11, 22, 0, 1, 2, 3, 9, 4, 5, 6, 7, 8, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
};

/** The loc layer a shape lands in (world.c World_LocShapeToLayer): wall 0-3,
 *  decor 4-8, main 9-21 (Client-TS GROUND: the diagonal wall, centrepieces,
 *  roofs), ground 22 (ground decor). No name is a prefix of another, so
 *  `layer=wall` is never the decor layer too. */
static int
cat_loc_layer(int shape)
{
    if( shape <= 3 )
        return 0;
    if( shape <= 8 )
        return 1;
    if( shape <= 21 )
        return 2;
    return 3;
}

static const char* const k_cat_loc_layer_names[4] = { "wall", "decor", "main", "ground" };

/**
 * The derived loc summary, from the merged fields (so an overlay's restated
 * model rows win): `shapes` (the placeable shapes, the Locs panel's rule --
 * a `shape<n>=<shape>,<model>...` row with a model gives that shape, decor
 * 5..8 draw 4's models and 11 draws 10's, and a loc with `models=` and no
 * shape rows is a centrepiece, 10 and 11), `size` ("<width>x<length>",
 * 1 when unstated) and `layer` (each shape's layer, once, in shape order).
 * A loc with no model rows has no `shapes` and no `layer`.
 */
static void
cat_collect_loc(struct cat_fields* fields)
{
    char text[128];
    size_t at = 0;
    uint32_t have = 0;
    uint32_t mask = 0;
    int listed = 0;
    int models = 0;
    int width = 1;
    int length = 1;
    int layers = 0;

    for( int i = 0; i < fields->n; i++ )
    {
        const char* key = fields->key[i];
        const char* value = fields->value[i];
        size_t const klen = strlen(key);
        if( klen > 5 && cat_family_len(key, klen) == 5 && memcmp(key, "shape", 5) == 0 )
        {
            const char* comma = strchr(value, ',');
            int shape;
            listed = 1;
            if( comma && comma[1] && cat_parse_int(value, comma, &shape) && shape >= 0 && shape < CAT_LOC_SHAPES )
                have |= 1u << shape;
        }
        else if( strcmp(key, "models") == 0 || strcmp(key, "model") == 0 )
            models = value[0] != '\0' && strcmp(value, "-1") != 0;
        else if( strcmp(key, "width") == 0 )
            (void)cat_parse_whole_int(value, &width);
        else if( strcmp(key, "length") == 0 )
            (void)cat_parse_whole_int(value, &length);
    }
    if( !listed && models )
        have = 1u << 10;
    for( int s = 0; s < CAT_LOC_SHAPES; s++ )
    {
        if( have & (1u << cat_loc_model_shape(s)) )
            mask |= 1u << s;
    }
    fields->loc_shapes = mask;
    if( !fields->loc_text )
        return;

    snprintf(text, sizeof(text), "%dx%d", width, length);
    cat_fields_put(fields, "size", cat_fields_scratch(fields, text));
    if( !mask )
        return;
    for( int i = 0; i < CAT_LOC_SHAPES; i++ )
    {
        int const s = k_cat_loc_shape_order[i];
        if( mask & (1u << s) )
            at += (size_t)snprintf(text + at, sizeof(text) - at, "%s%d", at ? " " : "", s);
    }
    cat_fields_put(fields, "shapes", cat_fields_scratch(fields, text));
    at = 0;
    for( int i = 0; i < CAT_LOC_SHAPES; i++ )
    {
        int const s = k_cat_loc_shape_order[i];
        int const layer = cat_loc_layer(s);
        if( !(mask & (1u << s)) || (layers & (1 << layer)) )
            continue;
        layers |= 1 << layer;
        at += (size_t)snprintf(text + at, sizeof(text) - at, "%s%s", at ? " " : "", k_cat_loc_layer_names[layer]);
    }
    cat_fields_put(fields, "layer", cat_fields_scratch(fields, text));
}

/** The record's summary: every contribution's fields, later ones winning,
 *  then the fields only the merge knows (a loc's derived text only with
 *  `loc_text`). */
static void
cat_collect_for(
    const struct ToriRSMapEdCatalog* catalog,
    int record,
    int loc_text,
    struct cat_fields* fields)
{
    const struct cat_record* rec = &catalog->records[record];
    char number[32];

    fields->n = 0;
    fields->scratch_len = 0;
    fields->loc_shapes = 0;
    fields->loc_text = loc_text;
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
    if( rec->kind == TORIRSMAPED_CATALOG_LOC )
        cat_collect_loc(fields);
}

/** The whole summary: what a row, a record view and a field lookup show. */
static void
cat_collect(
    const struct ToriRSMapEdCatalog* catalog,
    int record,
    struct cat_fields* fields)
{
    cat_collect_for(catalog, record, 1, fields);
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
cat_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/** Where a field value's words split: a summary list is "0 2 9" or
 *  "27297,27298". */
static int
cat_word_sep(char c)
{
    return c == ' ' || c == '\t' || c == ',';
}

/**
 * How a `key=value` filter value matched one field value (case-insensitive),
 * in the order the tests run and the hits rank (torirs_maped_catalog.h):
 *
 *   CAT_MATCH_WHOLE   the value is the field: `op=Open` names "Open";
 *   CAT_MATCH_WORD    a prefix of a whole word of it: `op=Op` names "Open",
 *                     `multivar=varb4153` names "varb4153_x" (the old
 *                     prefix-ending-at-'_' rule is this rule's first word),
 *                     `shapes=9` names "0 2 9" -- but a value that is a
 *                     number never stops inside a longer number: `shapes=2`
 *                     is not "22", `size=1` is "1x1" but not "10x2";
 *   CAT_MATCH_PREFIX  `value*` written out: a prefix of the whole field.
 */
enum
{
    CAT_MATCH_NONE = 0,
    CAT_MATCH_WHOLE,
    CAT_MATCH_WORD,
    CAT_MATCH_PREFIX,
};

static int
cat_value_match(
    const char* field,
    const char* wanted)
{
    size_t length = strlen(wanted);
    int numeric;
    int number;
    if( length > 0 && wanted[length - 1] == '*' )
    {
        char prefix[192];
        snprintf(prefix, sizeof(prefix), "%.*s", (int)(length - 1), wanted);
        return cat_iprefix(field, prefix) ? CAT_MATCH_PREFIX : CAT_MATCH_NONE;
    }
    if( cat_ieq(field, wanted) )
        return CAT_MATCH_WHOLE;
    if( length == 0 )
        return CAT_MATCH_NONE;
    numeric = cat_parse_whole_int(wanted, &number);
    /* Word starts: the field's first byte, and every byte after a run of
     * separators. */
    for( const char* w = field; *w; )
    {
        if( cat_iprefix(w, wanted) && !(numeric && cat_is_digit(w[length])) )
            return CAT_MATCH_WORD;
        while( *w && !cat_word_sep(*w) )
            w++;
        while( *w && cat_word_sep(*w) )
            w++;
    }
    return CAT_MATCH_NONE;
}

/**
 * 1 when every field value `longer` matches (cat_value_match), `shorter`
 * matches too: what lets a query typed on from `key=shorter` to
 * `key=longer` test only the held hits. `longer` must extend `shorter`, and
 * where `shorter` is a number the next character may not continue it (`2`
 * names "2 9" but `22` does not imply it, nor does `2*`). A held wildcard is
 * implied only by a longer wildcard.
 */
static int
cat_value_implies(
    const char* longer,
    const char* shorter)
{
    size_t const n = strlen(shorter);
    size_t const m = strlen(longer);
    int number;
    if( cat_ieq(longer, shorter) )
        return 1;
    if( n == 0 || m <= n )
        return 0;
    if( shorter[n - 1] == '*' )
    {
        /* "op*" -> "ope*": both prefixes of the whole field */
        char stem[192];
        if( longer[m - 1] != '*' || n - 1 >= sizeof(stem) )
            return 0;
        memcpy(stem, shorter, n - 1);
        stem[n - 1] = '\0';
        return cat_iprefix(longer, stem);
    }
    if( !cat_iprefix(longer, shorter) )
        return 0;
    if( cat_parse_whole_int(shorter, &number) && (cat_is_digit(longer[n]) || longer[n] == '*') )
        return 0;
    return 1;
}

/* ---- shape classes (design #catalog "Searching by shape") ---- */

/** The five classes `shape=` resolves by name (a value names every class it
 *  is a prefix of: `shape=c` is centre), and the shapes each holds. */
static const struct
{
    const char* name;
    uint32_t shapes;
} k_cat_shape_classes[] = {
    { "wall", (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 9) },
    { "decor", (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) },
    { "centre", (1u << 10) | (1u << 11) },
    { "roof", 0x3FF000u /* 12..21 */ },
    { "ground", 1u << 22 },
};

/** `shape=<value>` -> the shapes it names: a number names itself (0..22),
 *  a word the classes it names whole or as a prefix (`*` allowed). Sets
 *  *out_tier to the CAT_MATCH_* rank of the resolution. */
static uint32_t
cat_shape_filter_mask(
    const char* value,
    int* out_tier)
{
    char word[64];
    size_t length = strlen(value);
    uint32_t mask = 0;
    int number;
    int whole = 0;

    *out_tier = CAT_MATCH_NONE;
    if( cat_parse_whole_int(value, &number) )
    {
        if( number < 0 || number >= CAT_LOC_SHAPES )
            return 0;
        *out_tier = CAT_MATCH_WHOLE;
        return 1u << number;
    }
    if( length > 0 && value[length - 1] == '*' )
        length--;
    if( length == 0 || length >= sizeof(word) )
        return 0;
    memcpy(word, value, length);
    word[length] = '\0';
    for( size_t c = 0; c < sizeof(k_cat_shape_classes) / sizeof(k_cat_shape_classes[0]); c++ )
    {
        if( !cat_iprefix(k_cat_shape_classes[c].name, word) )
            continue;
        mask |= k_cat_shape_classes[c].shapes;
        whole |= cat_ieq(k_cat_shape_classes[c].name, word);
    }
    if( mask )
        *out_tier = whole ? CAT_MATCH_WHOLE : CAT_MATCH_WORD;
    return mask;
}

/**
 * Whether the record passes every `key=value` filter: -1 when one fails,
 * else the worst CAT_MATCH_* tier any filter matched by, less one (0: every
 * filter matched whole), which ranks the hit (cat_hit_score).
 */
static int
cat_filters_match(
    const struct ToriRSMapEdCatalog* catalog,
    int record,
    const struct cat_query* query)
{
    const struct cat_record* rec = &catalog->records[record];
    struct cat_fields fields;
    int collected = 0;
    int worst = CAT_MATCH_WHOLE;
    int loc_text = 0;

    /* A loc's derived text costs a few formats a record: only when a filter
     * reads it (`shape=` needs only the mask). */
    for( int f = 0; f < query->filter_count && rec->kind == TORIRSMAPED_CATALOG_LOC; f++ )
        loc_text |= strcmp(query->filter_key[f], "shapes") == 0 || strcmp(query->filter_key[f], "size") == 0
                    || strcmp(query->filter_key[f], "layer") == 0;
    for( int f = 0; f < query->filter_count; f++ )
    {
        const char* key = query->filter_key[f];
        const char* wanted = query->filter_value[f];
        int hit = CAT_MATCH_NONE;
        int number;

        if( strcmp(key, "name") == 0 )
        {
            /* the better of the name's and the display name's match */
            int const display = rec->display ? cat_value_match(rec->display, wanted) : CAT_MATCH_NONE;
            hit = cat_value_match(rec->name, wanted);
            if( display && (!hit || display < hit) )
                hit = display;
        }
        else if( strcmp(key, "id") == 0 )
            hit = cat_parse_whole_int(wanted, &number) && rec->id == number ? CAT_MATCH_WHOLE : CAT_MATCH_NONE;
        else if( strcmp(key, "shape") == 0 )
        {
            /* Resolved here to the shapes it names, against the loc's
             * placeable shapes (the `shapes` field); never the config's
             * shape<n> rows, whose values are "<shape>,<model>...". */
            int tier;
            uint32_t const want = cat_shape_filter_mask(wanted, &tier);
            if( rec->kind == TORIRSMAPED_CATALOG_LOC && want )
            {
                if( !collected )
                {
                    cat_collect_for(catalog, record, loc_text, &fields);
                    collected = 1;
                }
                if( fields.loc_shapes & want )
                    hit = tier;
            }
        }
        else
        {
            size_t klen = strlen(key);
            if( !collected )
            {
                cat_collect_for(catalog, record, loc_text, &fields);
                collected = 1;
            }
            for( int i = 0; i < fields.n; i++ )
            {
                const char* fk = fields.key[i];
                size_t flen = strlen(fk);
                int got;
                if( strcmp(fk, key) != 0
                    && !(cat_family_len(fk, flen) == klen && memcmp(fk, key, klen) == 0) )
                    continue;
                got = cat_value_match(fields.value[i], wanted);
                if( got && (!hit || got < hit) )
                    hit = got;
                if( hit == CAT_MATCH_WHOLE )
                    break;
            }
        }
        if( !hit )
            return -1;
        if( hit > worst )
            worst = hit;
    }
    return worst - CAT_MATCH_WHOLE;
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

/** A hit's rank: the terms' score (cat_score) first, then whether every
 *  filter matched whole (0) or one only as a prefix (1). */
static int
cat_hit_score(
    int term_score,
    int filter_rank)
{
    return term_score * 2 + (filter_rank > 0 ? 1 : 0);
}

/**
 * 1 when every record `query` matches also matches `held`, so the held hits
 * hold every answer: each held filter is restated with the same key and a
 * value that extends it (cat_value_implies: "op=Ope" after "op=Op", but not
 * "shapes=22" after "shapes=2"), and each held term is inside a term of
 * `query` that is not a number (a number also matches by id, which says
 * nothing about the name).
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
                    && cat_value_implies(query->filter_value[g], held->filter_value[f]);
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

/** The records of each kind, in record order, for the index revision: what a
 *  one-kind scan and a scoped any-kind query walk instead of every record.
 *  Rebuilt by the first query after a merge (a counting sort). */
static void
cat_kind_index(struct ToriRSMapEdCatalog* catalog)
{
    int at[TORIRSMAPED_CATALOG_KIND_COUNT];
    if( catalog->kind_index_valid && catalog->kind_index_revision == catalog->revision )
        return;
    catalog->kind_records = cat_grow(
        catalog->kind_records, &catalog->kind_records_cap, catalog->record_count + 1, sizeof(int32_t));
    memset(catalog->kind_start, 0, sizeof(catalog->kind_start));
    for( int r = 0; r < catalog->record_count; r++ )
        catalog->kind_start[catalog->records[r].kind + 1]++;
    for( int k = 0; k < TORIRSMAPED_CATALOG_KIND_COUNT; k++ )
    {
        catalog->kind_start[k + 1] += catalog->kind_start[k];
        at[k] = catalog->kind_start[k];
    }
    for( int r = 0; r < catalog->record_count; r++ )
        catalog->kind_records[at[catalog->records[r].kind]++] = r;
    catalog->kind_index_valid = 1;
    catalog->kind_index_revision = catalog->revision;
}

static void
cat_hit_put(
    struct cat_held_query* held,
    const struct cat_record* rec,
    int record,
    int score)
{
    struct cat_hit* hit;
    held->hits = cat_grow(held->hits, &held->cap, held->count + 1, sizeof(*held->hits));
    hit = &held->hits[held->count++];
    hit->score = score;
    hit->kind = rec->kind;
    hit->id = rec->id;
    hit->name = rec->name;
    hit->record = record;
}

/** Test one record against the query; a hit goes into the held hits. */
static int
cat_test_record(
    struct ToriRSMapEdCatalog* catalog,
    int record,
    const struct cat_query* query)
{
    const struct cat_record* rec = &catalog->records[record];
    int filter_rank;
    catalog->counters.examined++;
    if( !cat_terms_match(rec, query) )
        return 0;
    filter_rank = cat_filters_match(catalog, record, query);
    if( filter_rank < 0 )
        return 0;
    cat_hit_put(&catalog->held, rec, record, cat_hit_score(cat_score(rec, query), filter_rank));
    return 1;
}

/**
 * Kind "all" on the wire: the kinds are tested in their fixed order (loc,
 * npc, obj, then the rest by number), each stopping at `cap` hits, and no
 * further kind is tested once `want` hits are held -- so a keystroke costs
 * what the page it shows costs, not a test of every record. Before that, a
 * numeric term is looked up by id in every kind (a hash probe each), so the
 * record it names is in the first page whatever the caps cut. Sets
 * held->more when a cap or the window cut anything.
 */
static void
cat_query_scoped(
    struct ToriRSMapEdCatalog* catalog,
    const struct cat_query* query,
    int cap,
    int want)
{
    struct cat_held_query* held = &catalog->held;
    int probed[CAT_QUERY_TERMS_MAX];
    int probed_count = 0;

    held->count = 0;
    held->more = 0;
    for( int t = 0; t < query->term_count; t++ )
    {
        int number;
        if( !cat_parse_whole_int(query->terms[t], &number) || number < 0 )
            continue;
        probed[probed_count++] = number;
        for( int k = 0; k < TORIRSMAPED_CATALOG_KIND_COUNT; k++ )
        {
            uint32_t const slot = ((uint32_t)number * 2654435761u ^ (uint32_t)k * 40503u) & catalog->hash_mask;
            for( int32_t r = catalog->id_heads[slot]; r >= 0; r = catalog->records[r].next_id )
            {
                const struct cat_record* rec = &catalog->records[r];
                int duplicate = 0;
                if( rec->kind != k || rec->id != number )
                    continue;
                for( int i = 0; i < held->count && !duplicate; i++ )
                    duplicate = held->hits[i].record == r;
                if( !duplicate )
                    (void)cat_test_record(catalog, r, query);
            }
        }
    }
    cat_kind_index(catalog);
    for( int k = 0; k < TORIRSMAPED_CATALOG_KIND_COUNT; k++ )
    {
        int const begin = catalog->kind_start[k];
        int const end = catalog->kind_start[k + 1];
        int found = 0;
        if( begin == end )
            continue;
        if( held->count >= want )
        {
            held->more = 1;
            break;
        }
        for( int i = begin; i < end; i++ )
        {
            int const r = catalog->kind_records[i];
            int skip = 0;
            /* the id probe tested it */
            for( int p = 0; p < probed_count && !skip; p++ )
                skip = catalog->records[r].id == probed[p];
            if( skip || !cat_test_record(catalog, r, query) )
                continue;
            if( ++found >= cap )
            {
                held->more |= i + 1 < end;
                break;
            }
        }
    }
    catalog->counters.scoped++;
}

/**
 * Every match of (kind, filter, flags), sorted, in the held query's buffer:
 * valid until the next query or merge. The held hits answer the same
 * question again (a page flip) without testing a record, and a question that
 * implies the held one (typing on) is tested against the held hits only.
 *
 * `scope_cap` > 0 makes kind "all" a scoped query (cat_query_scoped) whose
 * answer may be cut (held->more); 0 answers every match, as the in-process
 * lookups want.
 */
static int
cat_query_hits(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* filter,
    uint32_t flags,
    int scope_cap,
    int scope_want,
    const struct cat_hit** out_hits)
{
    struct cat_held_query* held = &catalog->held;
    struct cat_query query;
    int const scoped = kind == TORIRSMAPED_CATALOG_ANY && scope_cap > 0 && !(flags & TORIRSMAPED_CATALOG_EXACT);
    int const cap = scoped ? scope_cap : 0;
    int const same_index = held->valid && held->revision == catalog->revision && held->kind == kind
                           && held->flags == flags && held->scope_cap == cap;

    *out_hits = held->hits;
    if( same_index && strcmp(held->filter, filter) == 0 && (!held->more || held->scope_want >= scope_want) )
    {
        catalog->counters.held++;
        return held->count;
    }
    cat_query_parse(filter, &query);

    if( catalog->record_count == 0 )
    {
        held->count = 0;
        held->more = 0;
    }
    else if( (flags & TORIRSMAPED_CATALOG_EXACT) && query.term_count > 0 )
    {
        /* Hash probes: by name and display name in every kind asked for,
         * and by id when the text is a number. */
        int number;
        int numeric = cat_parse_whole_int(query.whole, &number);
        held->count = 0;
        held->more = 0;
        for( int k = 0; k < TORIRSMAPED_CATALOG_KIND_COUNT; k++ )
        {
            uint32_t slot;
            int rank;
            if( !cat_kind_ok(kind, k) )
                continue;
            slot = cat_hash_lower((uint32_t)k, query.whole) & catalog->hash_mask;
            for( int32_t r = catalog->name_heads[slot]; r >= 0; r = catalog->records[r].next_name )
            {
                const struct cat_record* rec = &catalog->records[r];
                if( rec->kind == k && cat_ieq(rec->name, query.whole)
                    && (rank = cat_filters_match(catalog, r, &query)) >= 0 )
                    cat_hit_add(&held->hits, &held->count, &held->cap, rec, r, cat_hit_score(0, rank));
            }
            for( int32_t r = catalog->display_heads[slot]; r >= 0; r = catalog->records[r].next_display )
            {
                const struct cat_record* rec = &catalog->records[r];
                if( rec->kind == k && cat_ieq(rec->display, query.whole)
                    && (rank = cat_filters_match(catalog, r, &query)) >= 0 )
                    cat_hit_add(&held->hits, &held->count, &held->cap, rec, r, cat_hit_score(1, rank));
            }
            if( numeric && number >= 0 )
            {
                slot = ((uint32_t)number * 2654435761u ^ (uint32_t)k * 40503u) & catalog->hash_mask;
                for( int32_t r = catalog->id_heads[slot]; r >= 0; r = catalog->records[r].next_id )
                {
                    const struct cat_record* rec = &catalog->records[r];
                    if( rec->kind == k && rec->id == number && (rank = cat_filters_match(catalog, r, &query)) >= 0 )
                        cat_hit_add(&held->hits, &held->count, &held->cap, rec, r, cat_hit_score(1, rank));
                }
            }
        }
    }
    else
    {
        struct cat_query held_query;
        int narrow = 0;
        /* A cut answer (a scoped "all" that hit a cap) does not hold every
         * match, so nothing narrows from it. */
        if( same_index && !(held->flags & TORIRSMAPED_CATALOG_EXACT) && !held->more )
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
                int rank;
                catalog->counters.examined++;
                if( !cat_terms_match(rec, &query) || (rank = cat_filters_match(catalog, r, &query)) < 0 )
                    continue;
                held->hits[kept] = held->hits[i];
                held->hits[kept].score = cat_hit_score(cat_score(rec, &query), rank);
                kept++;
            }
            held->count = kept;
            catalog->counters.narrowed++;
        }
        else if( scoped )
            cat_query_scoped(catalog, &query, scope_cap, scope_want);
        else
        {
            held->count = 0;
            held->more = 0;
            if( kind == TORIRSMAPED_CATALOG_ANY )
            {
                for( int r = 0; r < catalog->record_count; r++ )
                    (void)cat_test_record(catalog, r, &query);
            }
            else
            {
                assert(kind < TORIRSMAPED_CATALOG_KIND_COUNT);
                cat_kind_index(catalog);
                for( int i = catalog->kind_start[kind]; i < catalog->kind_start[kind + 1]; i++ )
                    (void)cat_test_record(catalog, catalog->kind_records[i], &query);
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
    held->scope_cap = cap;
    held->scope_want = scope_want;
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
    cat_warm_thread_join(catalog);
    assert(filter);
    assert(out_records || max == 0);
    cat_request_index(catalog);
    count = cat_query_hits(catalog, kind, filter, flags, 0, 0, &hits);
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
    cat_warm_thread_join((struct ToriRSMapEdCatalog*)catalog);
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
    cat_warm_thread_join((struct ToriRSMapEdCatalog*)catalog);
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
    int want;

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
        for( int i = 0; (flags & TORIRSMAPED_CATALOG_PROGRESS) && i < 3; i++ )
            ToriRSMapEd_PutU32(out, 0);
        return 1;
    }
    if( page_size == 0 )
        page_size = TORIRSMAPED_CATALOG_PAGE_DEFAULT;
    if( page_size > TORIRSMAPED_CATALOG_PAGE_MAX )
        page_size = TORIRSMAPED_CATALOG_PAGE_MAX;
    /* The warm build on its worker thread owns the index until it is done: a
     * PROGRESS asker gets an empty partial page and the build's progress,
     * never a read racing the thread. Any other asker waits for the build
     * (cat_request_index joins the thread). */
    if( (flags & TORIRSMAPED_CATALOG_PROGRESS) && ToriRSMapEdCatalog_Building(catalog) )
    {
        uint32_t done;
        uint32_t of;
        (void)ToriRSMapEdCatalog_Progress(catalog, &done, &of);
        ToriRSMapEd_PutU32(out, EDITOR_HOST_OK);
        ToriRSMapEd_PutU32(out, 0);
        ToriRSMapEd_PutU32(out, page);
        ToriRSMapEd_PutU32(out, page_size);
        ToriRSMapEd_PutU32(out, 0);
        ToriRSMapEd_PutU32(out, TORIRSMAPED_CATALOG_PAGE_PARTIAL);
        ToriRSMapEd_PutU32(out, done);
        ToriRSMapEd_PutU32(out, of);
        return 1;
    }

    first = page * page_size;
    if( page > 0 && first / page != page_size )
        first = UINT32_MAX; /* overflow: past the end */
    /* A scoped "all" stops once it holds the page asked for. */
    want = first > (uint32_t)INT32_MAX - page_size ? INT32_MAX : (int)(first + page_size);

    if( flags & TORIRSMAPED_CATALOG_PROGRESS )
        cat_request_partial(catalog);
    else
        cat_request_index(catalog);
    total = cat_query_hits(catalog, kind, filter, flags, (int)page_size, want, &hits);
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
    if( flags & TORIRSMAPED_CATALOG_PROGRESS )
    {
        /* The v3 tail, only for a client that asked for it: a v2 reader
         * checks the payload ends at the last row. */
        uint32_t done;
        uint32_t of;
        uint32_t page_flags = 0;
        if( ToriRSMapEdCatalog_Progress(catalog, &done, &of) )
            page_flags |= TORIRSMAPED_CATALOG_PAGE_PARTIAL;
        if( catalog->held.more )
            page_flags |= TORIRSMAPED_CATALOG_PAGE_SCOPED;
        ToriRSMapEd_PutU32(out, page_flags);
        ToriRSMapEd_PutU32(out, done);
        ToriRSMapEd_PutU32(out, of);
    }
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
        /* An unsaved record's text is the authority's, kept current by
         * PendingSync: there is no file to have changed under it. */
        if( src->type == CAT_SRC_PENDING )
            continue;
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
        if( src->type == CAT_SRC_PENDING )
            continue;
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
    case CAT_SRC_PENDING:
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
        /* A miss on the warm build's partial index is not "no such record":
         * BUSY, so the asker tries again once the index is whole (the spawn
         * preview held every name asked in the first seconds as not found). */
        ToriRSMapEd_PutU32(out, ToriRSMapEdCatalog_Built(catalog) ? EDITOR_HOST_ABSENT : EDITOR_HOST_BUSY);
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

        if( c->len > 0 && src->type == CAT_SRC_PENDING )
        {
            assert((size_t)c->off + c->len <= src->mem_len);
            cat_rec_put_text(&build, src->mem + c->off, c->len, source);
        }
        else if( c->len > 0
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
    int with_locs,
    int loc_count)
{
    ToriRSMapEd_PutU32(out, (uint32_t)x);
    ToriRSMapEd_PutU32(out, (uint32_t)z);
    ToriRSMapEd_PutU32(out, (uint32_t)storage);
    ToriRSMapEd_PutU32(out, with_locs ? flags | TORIRSMAPED_SQUARE_LOC_COUNT : flags);
    ToriRSMapEd_PutU32(out, (uint32_t)spawn_count);
    ToriRSMapEd_PutU32(out, rgb);
    ToriRSMapEd_PutU32(out, area_n > 0 ? (uint32_t)areas[0] : TORIRSMAPED_CATALOG_NO_AREA);
    ToriRSMapEd_PutU32(out, (uint32_t)area_n);
    for( int i = 0; i < area_n; i++ )
        ToriRSMapEd_PutU32(out, (uint32_t)areas[i]);
    if( with_locs )
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
    int* inout_locs,
    struct ToriRSMapEdBuf* out)
{
    uint32_t flags = 0;
    assert(catalog);
    assert(doc);
    assert(inout_locs);
    assert(out);
    /* Empty is the v2 request; a `u32 flags` body comes only from a client
     * that saw TORIRSMAPED_CAP_CATALOG_SQUARE_LOCS or
     * TORIRSMAPED_CAP_CATALOG_SQUARES_NOWAIT. */
    if( length == 4 )
    {
        assert(body);
        flags = ToriRSMapEd_ReadU32(body);
        if( flags & ~(uint32_t)(TORIRSMAPED_CATALOG_SQUARES_LOCS | TORIRSMAPED_CATALOG_SQUARES_NOWAIT) )
            return 0;
    }
    else if( length != 0 )
        return 0;
    *inout_locs = (flags & TORIRSMAPED_CATALOG_SQUARES_LOCS) != 0;
    if( (flags & TORIRSMAPED_CATALOG_SQUARES_NOWAIT) && ToriRSMapEdCatalog_Building(catalog) )
    {
        /* The atlas's status, then empty storage, area, square and area-id
         * lists: the caller asks again next tick. */
        ToriRSMapEd_PutU32(out, EDITOR_HOST_BUSY);
        for( int i = 0; i < 4; i++ )
            ToriRSMapEd_PutU32(out, 0);
        return 1;
    }
    cat_request_index(catalog);
    ToriRSMapEdCatalog_EncodeSquares(catalog, doc, *inout_locs, out);
    return 1;
}

void
ToriRSMapEdCatalog_EncodeSquares(
    struct ToriRSMapEdCatalog* catalog,
    const struct Editor_Doc* doc,
    int with_locs,
    struct ToriRSMapEdBuf* out)
{
    uint32_t* doc_flags;
    int extra_storages = 0;
    const char* extra_storage_names[EDITOR_DOC_MAX_SQUARES];
    int doc_storage[EDITOR_DOC_MAX_SQUARES];
    int doc_row[EDITOR_DOC_MAX_SQUARES];
    int created = 0;

    assert(catalog);
    cat_warm_thread_join(catalog);
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
            sq->spawn_count, sq->rgb, catalog->square_areas + sq->area_first, sq->area_n, with_locs,
            sq->loc_count);
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
            out, square->map_x, square->map_z, doc_storage[i], flags, spawns, 0, areas, area_n, with_locs, 0);
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
    cat_warm_thread_join((struct ToriRSMapEdCatalog*)catalog);
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
    cat_warm_thread_join((struct ToriRSMapEdCatalog*)catalog);
    return catalog->square_count;
}

/* ------------------------------------------------------------------ */
/* Where used (workbench 6.6)                                          */
/* ------------------------------------------------------------------ */

/* CATALOG_USES (torirs_maped_catalog.h): a search over the files the index
 * lists, a slice per request. The index keeps no words, so this reads the
 * files; a slice is a few milliseconds of them, and the client asks again
 * with the cursor it was given. */

struct cat_uses
{
    const char* name;
    size_t name_length;
    uint32_t kind;
    int32_t id;
    struct ToriRSMapEdBuf* rows;
    uint32_t row_count;
};

/** `needle` anywhere in the `size` bytes at `hay` (memmem is not portable). */
static int
cat_memmem(
    const char* hay,
    size_t size,
    const char* needle,
    size_t length)
{
    const char* at = hay;
    const char* end = hay + size;

    if( length == 0 )
        return 1;
    while( at + length <= end )
    {
        const char* hit = memchr(at, needle[0], (size_t)(end - at));
        if( !hit || hit + length > end )
            return 0;
        if( memcmp(hit, needle, length) == 0 )
            return 1;
        at = hit + 1;
    }
    return 0;
}

static int
cat_word_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/** `name` as a whole word in [s, e). */
static int
cat_names_word(
    const char* s,
    const char* e,
    const char* name,
    size_t length)
{
    const char* at = s;

    while( at + length <= e )
    {
        const char* hit = memchr(at, name[0], (size_t)(e - at));
        if( !hit || hit + length > e )
            return 0;
        if( memcmp(hit, name, length) == 0 && (hit == s || !cat_word_char(hit[-1]))
            && (hit + length == e || !cat_word_char(hit[length])) )
            return 1;
        at = hit + 1;
    }
    return 0;
}

static void
cat_uses_row(
    struct cat_uses* uses,
    uint32_t use,
    const char* path,
    uint32_t line,
    uint32_t count,
    const char* text,
    size_t text_length)
{
    char clip[192];
    size_t const n = text_length < sizeof(clip) - 1 ? text_length : sizeof(clip) - 1;

    memcpy(clip, text, n);
    clip[n] = '\0';
    ToriRSMapEd_PutU32(uses->rows, use);
    ToriRSMapEd_PutString(uses->rows, path);
    ToriRSMapEd_PutU32(uses->rows, line);
    ToriRSMapEd_PutU32(uses->rows, count);
    ToriRSMapEd_PutString(uses->rows, clip);
    uses->row_count++;
}

/** Text files: every line naming the record as a word (an .rs2 header of it
 *  is a TRIGGER), at most TORIRSMAPED_CATALOG_USES_PER_FILE, then one MORE. */
static void
cat_uses_lines(
    struct cat_uses* uses,
    const char* path,
    const char* text,
    size_t size,
    uint32_t use)
{
    const char* p = text;
    const char* end = text + size;
    uint32_t line = 0;
    uint32_t listed = 0;
    uint32_t more = 0;

    /* Most files never name it: one pass for the bytes first. */
    if( !cat_memmem(text, size, uses->name, uses->name_length) )
        return;
    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        const char* s = p;
        line++;
        p = next;
        if( !cat_names_word(s, le, uses->name, uses->name_length) )
            continue;
        if( use == TORIRSMAPED_USE_CONFIG && s < le && s[0] == '[' )
            continue; /* a block's own header: the record itself */
        if( listed >= TORIRSMAPED_CATALOG_USES_PER_FILE )
        {
            more++;
            continue;
        }
        cat_trim(&s, &le);
        cat_uses_row(uses,
            use == TORIRSMAPED_USE_SCRIPT && s < le && s[0] == '[' ? (uint32_t)TORIRSMAPED_USE_TRIGGER : use, path,
            line, 1, s, (size_t)(le - s));
        listed++;
    }
    if( more )
        cat_uses_row(uses, TORIRSMAPED_USE_MORE, path, 0, more, "", 0);
}

/** A .spawn file: `name x z level [count]` rows of the record, per square. */
static void
cat_uses_spawns(
    struct cat_uses* uses,
    const char* path,
    const char* text,
    size_t size)
{
    int32_t keys[64];
    uint32_t counts[64];
    uint32_t lines[64];
    int key_count = 0;
    const char* p = text;
    const char* end = text + size;
    uint32_t line = 0;

    if( !cat_memmem(text, size, uses->name, uses->name_length) )
        return;
    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        const char* s = p;
        line++;
        p = next;
        if( (size_t)(le - s) > uses->name_length && memcmp(s, uses->name, uses->name_length) == 0
            && (s[uses->name_length] == ' ' || s[uses->name_length] == '\t') )
        {
            int x = atoi(s + uses->name_length + 1);
            const char* z_at = s + uses->name_length + 1;
            int32_t key;
            int found = -1;
            while( z_at < le && (*z_at == ' ' || *z_at == '\t') )
                z_at++;
            while( z_at < le && *z_at != ' ' && *z_at != '\t' )
                z_at++;
            key = ((x >> 6) << 8) | (atoi(z_at) >> 6);
            for( int k = 0; k < key_count; k++ )
                if( keys[k] == key )
                    found = k;
            if( found < 0 && key_count < 64 )
            {
                found = key_count++;
                keys[found] = key;
                counts[found] = 0;
                lines[found] = line;
            }
            if( found >= 0 )
                counts[found]++;
        }
    }
    for( int k = 0; k < key_count; k++ )
    {
        char text_out[64];
        int const n = snprintf(text_out, sizeof(text_out), "m%d_%d: %u spawn%s", keys[k] >> 8, keys[k] & 0xFF,
            counts[k], counts[k] == 1 ? "" : "s");
        cat_uses_row(uses, TORIRSMAPED_USE_SPAWN, path, lines[k], counts[k], text_out, (size_t)n);
    }
}

/** A square's .jl2: `<level> <x> <z>: <loc> <shape> <rot>` rows of the loc. */
static void
cat_uses_placements(
    struct cat_uses* uses,
    const char* path,
    const char* text,
    size_t size)
{
    const char* p = text;
    const char* end = text + size;
    uint32_t count = 0;
    uint32_t first = 0;
    uint32_t line = 0;

    while( p < end )
    {
        const char* le;
        const char* next = cat_next_line(p, end, &le);
        const char* colon = memchr(p, ':', (size_t)(le - p));
        line++;
        if( colon && p[0] >= '0' && p[0] <= '9' && atoi(colon + 1) == uses->id )
        {
            if( count++ == 0 )
                first = line;
        }
        p = next;
    }
    if( count )
    {
        const char* base = strrchr(path, '/');
        char text_out[96];
        int const n = snprintf(text_out, sizeof(text_out), "%.*s: %u placement%s",
            (int)strcspn(base ? base + 1 : path, "."), base ? base + 1 : path, count, count == 1 ? "" : "s");
        cat_uses_row(uses, TORIRSMAPED_USE_PLACEMENT, path, first, count, text_out, (size_t)n);
    }
}

/** editor/palettes.txt and editor/stamps/: not catalog sources, read last. */
static void
cat_uses_editor_files(
    struct ToriRSMapEdCatalog* catalog,
    struct cat_uses* uses)
{
    char* text;
    size_t size = 0;
    char slot[96];
    int const slot_length =
        snprintf(slot, sizeof(slot), "%s:%s", ToriRSMapEdCatalog_KindName(uses->kind), uses->name);

    text = cat_read(catalog, "editor/palettes.txt", 0, 0, &size);
    if( text && slot_length > 0 && (size_t)slot_length < sizeof(slot) )
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
            p = next;
            if( cat_names_word(s, le, slot, (size_t)slot_length) || (s < le && s[0] != '#'
                && cat_memmem(s, (size_t)(le - s), slot, (size_t)slot_length)
                && cat_names_word(s, le, uses->name, uses->name_length)) )
            {
                cat_trim(&s, &le);
                cat_uses_row(uses, TORIRSMAPED_USE_PALETTE, "editor/palettes.txt", line, 1, s, (size_t)(le - s));
            }
        }
    }
    if( uses->kind != TORIRSMAPED_CATALOG_LOC || uses->id < 0 )
        return;
    {
        char dir_path[CAT_PATH_MAX * 2];
        DIR* dir;
        struct dirent* entry;
        snprintf(dir_path, sizeof(dir_path), "%s/editor/stamps", catalog->content_dir);
        dir = opendir(dir_path);
        while( dir && (entry = readdir(dir)) != NULL )
        {
            char rel[CAT_PATH_MAX];
            size_t const n = strlen(entry->d_name);
            const char* p;
            const char* end;
            uint32_t count = 0;
            uint32_t first = 0;
            uint32_t line = 0;

            if( n < 7 || strcmp(entry->d_name + n - 6, ".stamp") != 0 )
                continue;
            snprintf(rel, sizeof(rel), "editor/stamps/%s", entry->d_name);
            text = cat_read(catalog, rel, 0, 0, &size);
            if( !text )
                continue;
            p = text;
            end = text + size;
            while( p < end )
            {
                const char* le;
                const char* next = cat_next_line(p, end, &le);
                int dx;
                int dz;
                int dlevel;
                int loc = -1;
                line++;
                /* loc <dx> <dz> <dlevel> <loc> <shape> <rot> [<w>x<h>] */
                if( le - p > 4 && memcmp(p, "loc ", 4) == 0 && sscanf(p + 4, "%d %d %d %d", &dx, &dz, &dlevel, &loc) == 4
                    && loc == uses->id && count++ == 0 )
                    first = line;
                p = next;
            }
            if( count )
            {
                char text_out[96];
                int const k = snprintf(text_out, sizeof(text_out), "stamp %.*s: %u loc%s", (int)(n - 6), entry->d_name,
                    count, count == 1 ? "" : "s");
                cat_uses_row(uses, TORIRSMAPED_USE_STAMP, rel, first, count, text_out, (size_t)k);
            }
        }
        if( dir )
            closedir(dir);
    }
}

int
ToriRSMapEdCatalog_HandleUses(
    struct ToriRSMapEdCatalog* catalog,
    const uint8_t* body,
    uint32_t length,
    struct ToriRSMapEdBuf* out)
{
    struct ToriRSMapEdReader reader;
    struct ToriRSMapEdBuf rows;
    struct cat_uses uses;
    char name[256];
    uint32_t kind;
    uint32_t cursor;
    uint32_t total;
    uint32_t at;
    int record;
    double deadline;

    assert(catalog);
    assert(out);
    ToriRSMapEd_ReaderInit(&reader, body, length);
    kind = ToriRSMapEd_GetU32(&reader);
    ToriRSMapEd_GetString(&reader, name, sizeof(name));
    cursor = ToriRSMapEd_GetU32(&reader);
    if( !ToriRSMapEd_ReaderDone(&reader) )
        return 0;

    cat_request_index(catalog);
    record = name[0] ? cat_record_lookup(catalog, kind, -1, name) : -1;
    memset(&rows, 0, sizeof(rows));
    memset(&uses, 0, sizeof(uses));
    uses.name = name;
    uses.name_length = strlen(name);
    uses.kind = kind;
    uses.id = record >= 0 ? catalog->records[record].id : -1;
    uses.rows = &rows;
    /* The sources, then one step for the editor's own files. */
    total = (uint32_t)catalog->source_count + 1;
    at = cursor < total ? cursor : total;
    deadline = cat_now_ms() + TORIRSMAPED_CATALOG_USES_SLICE_MS;
    while( name[0] && at < total && uses.row_count < TORIRSMAPED_CATALOG_USES_ROWS
           && (at == cursor || cat_now_ms() < deadline) )
    {
        const struct cat_source* src;
        uint32_t use = 0;
        char* text;
        size_t size = 0;

        if( at == (uint32_t)catalog->source_count )
        {
            cat_uses_editor_files(catalog, &uses);
            at++;
            continue;
        }
        src = &catalog->sources[at++];
        if( src->type == CAT_SRC_RS2 )
            use = TORIRSMAPED_USE_SCRIPT;
        else if( src->type == CAT_SRC_SPAWN && (kind == TORIRSMAPED_CATALOG_NPC || kind == TORIRSMAPED_CATALOG_OBJ) )
            use = TORIRSMAPED_USE_SPAWN;
        else if( src->type == CAT_SRC_JL2 && kind == TORIRSMAPED_CATALOG_LOC && uses.id >= 0 )
            use = TORIRSMAPED_USE_PLACEMENT;
        else if( src->type == CAT_SRC_IF )
            use = TORIRSMAPED_USE_INTERFACE;
        else if( src->type == CAT_SRC_CS2 )
            use = TORIRSMAPED_USE_CS2;
        else if( src->type == CAT_SRC_OVERLAY )
            use = TORIRSMAPED_USE_CONFIG;
        if( !use )
            continue;
        text = cat_read(catalog, src->path, (size_t)src->size, 0, &size);
        if( !text )
            continue;
        if( use == TORIRSMAPED_USE_SPAWN )
            cat_uses_spawns(&uses, src->path, text, size);
        else if( use == TORIRSMAPED_USE_PLACEMENT )
            cat_uses_placements(&uses, src->path, text, size);
        else
            cat_uses_lines(&uses, src->path, text, size, use);
    }
    ToriRSMapEd_PutU32(out, record >= 0 ? EDITOR_HOST_OK : EDITOR_HOST_ABSENT);
    ToriRSMapEd_PutU32(out, kind);
    ToriRSMapEd_PutString(out, name);
    ToriRSMapEd_PutU32(out, name[0] && at < total ? at : 0);
    ToriRSMapEd_PutU32(out, at);
    ToriRSMapEd_PutU32(out, total);
    ToriRSMapEd_PutU32(out, uses.row_count);
    if( rows.data )
        ToriRSMapEd_BufWrite(out, rows.data + rows.head, (int)ToriRSMapEd_BufAvailable(&rows));
    ToriRSMapEd_BufFree(&rows);
    return 1;
}
