/*
 * The catalog's work bounds (docs/MAPED_STATUS.md "Catalog performance").
 *
 * Measured 2026-11-02 on the owner's tree (63,307 locs, 405k records, ~100k
 * files): a keystroke in the Catalog's Find or a click on a row stalled the
 * editor's frame 0.2-1.8 s whenever two seconds had passed since the last
 * request, because every request re-stat'ed the whole content tree before it
 * answered. This test pins what replaced that, by COUNTING work rather than
 * timing it (timings are the machine's; counts are the code's):
 *
 *   - the index builds in slices on the service pass (more than one slice);
 *   - no query, page flip or record view stats the tree, however long since
 *     the last one -- a request only starts a sweep, and the sweep is
 *     advanced by the service pass a slice at a time, and still catches an
 *     edit;
 *   - a query that extends the held one tests only the held hits, a page
 *     flip tests nothing, and narrowing answers exactly what a full scan
 *     answers;
 *   - a record view reads its own block once and stats only its own files;
 *     a record whose file changed is re-read alone, not by a tree scan.
 *
 * And the loc finder's (workbench 6.2): filter values narrow as a prefix
 * (and typing on narrows the held hits), `shape=` classes resolve to the
 * shapes, loc rows carry shapes/size/layer, kind "all" is scoped (its first
 * page costs what the page holds; ids first) and is timed against the
 * unscoped scan; the warm build parses configs/all.loc in byte parts, merges
 * source by source with locs first, answers PROGRESS queries from the
 * partial index meanwhile, and ends as a full merge would have.
 *
 * Synthetic: a 60,000-loc tree with 3,000 script files, written under
 * argv[1]. Then, given the real content tree as argv[2], the same requests
 * timed on it and printed (not checked).
 *
 * Content-free too: the square atlas of a three-square tree (each row's loc
 * count on the wire, the instance-space flag).
 *
 * usage: catalog_perf_test <scratch-dir> [<content-dir>]
 */

#include "editor/editor_doc.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_buf.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define PERF_LOCS 60000
#define PERF_SCRIPT_DIRS 30
#define PERF_SCRIPTS_PER_DIR 100

static int g_checks;
static int g_failures;

#define CHECK(cond, ...)                                                                                     \
    do                                                                                                       \
    {                                                                                                        \
        g_checks++;                                                                                          \
        if( !(cond) )                                                                                        \
        {                                                                                                    \
            g_failures++;                                                                                    \
            printf("FAIL: ");                                                                                \
            printf(__VA_ARGS__);                                                                             \
            printf("\n");                                                                                    \
        }                                                                                                    \
    } while( 0 )

static double
now_ms(void)
{
    /* Monotonic, and on every platform: MinGW's MSVCRT has no timespec_get. */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static void
make_dir(
    const char* root,
    const char* relative)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    mkdir(path, 0755);
}

static FILE*
open_out(
    const char* root,
    const char* relative)
{
    char path[2048];
    FILE* f;
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    f = fopen(path, "wb");
    if( !f )
    {
        printf("FAIL: cannot write %s\n", path);
        exit(1);
    }
    return f;
}

static void
write_text(
    const char* root,
    const char* relative,
    const char* text)
{
    FILE* f = open_out(root, relative);
    fputs(text, f);
    fclose(f);
}

/* What a synthetic loc can be placed as (its config's model rows): an oak is
 * a centrepiece (`models=`, no shape rows), a door a fence with an L and a
 * diagonal, then wall decor, ground decor and roofs; the rest have no
 * models. Every 5th loc is 2x3. */
enum perf_shape_class
{
    PERF_NONE = 0,
    PERF_CENTRE,
    PERF_WALL,
    PERF_DECOR,
    PERF_GROUND,
    PERF_ROOF,
};

static int
loc_class(int i)
{
    if( i % 7 == 0 )
        return PERF_CENTRE;
    if( i % 11 == 0 )
        return PERF_WALL;
    if( i % 13 == 0 )
        return PERF_DECOR;
    if( i % 17 == 0 )
        return PERF_GROUND;
    if( i % 19 == 0 )
        return PERF_ROOF;
    return PERF_NONE;
}

static const char*
loc_model_rows(int i)
{
    switch( loc_class(i) )
    {
    case PERF_CENTRE:
        return "models=1234\n";
    case PERF_WALL:
        return "shape1=0,2000\nshape2=2,2001\nshape3=9,2002\n";
    case PERF_DECOR:
        return "shape1=4,3000\n";
    case PERF_GROUND:
        return "shape1=22,4000\n";
    case PERF_ROOF:
        return "shape1=12,5000\nshape2=13,5001\n";
    default:
        return "";
    }
}

static int
count_class(int cls)
{
    int n = 0;
    for( int i = 0; i < PERF_LOCS; i++ )
        n += loc_class(i) == cls;
    return n;
}

/* Names a typist narrows through: every 7th loc is an oak something, every
 * 11th a door, the rest generic. */
static void
loc_name(
    int i,
    char* out,
    size_t out_size)
{
    if( i % 7 == 0 )
        snprintf(out, out_size, "oaktree_%d", i);
    else if( i % 11 == 0 )
        snprintf(out, out_size, "door_%d", i);
    else
        snprintf(out, out_size, "perf_loc_%d", i);
}

static void
write_tree(const char* root)
{
    FILE* config;
    FILE* compack;
    char name[64];

    mkdir(root, 0755);
    make_dir(root, "configs");
    make_dir(root, "pack");
    make_dir(root, "server");
    make_dir(root, "server/scripts");
    make_dir(root, "server/scripts/areas");
    config = open_out(root, "configs/all.loc");
    compack = open_out(root, "configs/all.loc.compack");
    for( int i = 0; i < PERF_LOCS; i++ )
    {
        loc_name(i, name, sizeof(name));
        fprintf(config, "[%s]\nname=%s %d\n%s\n%s%s\n", name, i % 7 == 0 ? "Oak tree" : i % 11 == 0 ? "Door" : "Thing",
            i, i % 11 == 0 ? "op1=Open" : "op1=Search", loc_model_rows(i), i % 5 == 0 ? "width=2\nlength=3\n" : "");
        fprintf(compack, "%d=%s\n", i, name);
    }
    fclose(config);
    fclose(compack);
    /* A few npcs: kind "all" tests them after the locs. guard_captain's id
     * is 700, a number that is also in hundreds of loc names. */
    write_text(root, "configs/all.npc", "[hans]\nname=Hans\n\n[guard_captain]\nname=Guard captain\n");
    write_text(root, "configs/all.npc.compack", "3=hans\n700=guard_captain\n");
    for( int d = 0; d < PERF_SCRIPT_DIRS; d++ )
    {
        char rel[256];
        snprintf(rel, sizeof(rel), "server/scripts/areas/area_%d", d);
        make_dir(root, rel);
        snprintf(rel, sizeof(rel), "server/scripts/areas/area_%d/scripts", d);
        make_dir(root, rel);
        for( int s = 0; s < PERF_SCRIPTS_PER_DIR; s++ )
        {
            char text[256];
            int const loc = (d * PERF_SCRIPTS_PER_DIR + s) * 7;
            loc_name(loc, name, sizeof(name));
            snprintf(rel, sizeof(rel), "server/scripts/areas/area_%d/scripts/s%d.rs2", d, s);
            snprintf(text, sizeof(text), "[oploc1,%s]\nmes(\"An oak.\");\n", name);
            write_text(root, rel, text);
        }
    }
    /* An authored overlay over loc 5, for the stale-record check. */
    make_dir(root, "server/scripts/areas/area_0/configs");
    write_text(root, "server/scripts/areas/area_0/configs/perf.loc", "[perf_loc_5]\nop2=Kick\n");
}

/* ---- the wire, as the client asks it ---- */

#define PAGE_ROWS_KEPT 16

struct page_answer
{
    uint32_t status;
    uint32_t total;
    uint32_t rows;
    char first[128];
    /** Every row's name (up to PAGE_ROWS_KEPT) and kind. */
    char names[PAGE_ROWS_KEPT][128];
    uint32_t kinds[PAGE_ROWS_KEPT];
    /** The first row's derived loc fields ("" when absent). */
    char shapes[256];
    char size[256];
    char layer[256];
    /** The TORIRSMAPED_CATALOG_PROGRESS tail, and whether the payload ended
     *  exactly where the reader expected (with or without the tail). */
    int tail;
    uint32_t page_flags;
    uint32_t done;
    uint32_t of;
    int exact_end;
};

static void
ask_query_ex(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* filter,
    uint32_t flags,
    uint32_t page,
    uint32_t page_size,
    struct page_answer* out)
{
    struct ToriRSMapEdBuf body;
    struct ToriRSMapEdBuf answer;
    struct ToriRSMapEdReader reader;

    memset(&body, 0, sizeof(body));
    memset(&answer, 0, sizeof(answer));
    memset(out, 0, sizeof(*out));
    ToriRSMapEd_PutU32(&body, kind);
    ToriRSMapEd_PutString(&body, filter);
    ToriRSMapEd_PutU32(&body, flags);
    ToriRSMapEd_PutU32(&body, page);
    ToriRSMapEd_PutU32(&body, page_size);
    (void)ToriRSMapEdCatalog_HandleQuery(
        catalog, ToriRSMapEd_BufPeek(&body), (uint32_t)ToriRSMapEd_BufAvailable(&body), &answer);
    ToriRSMapEd_ReaderInit(&reader, ToriRSMapEd_BufPeek(&answer), (uint32_t)ToriRSMapEd_BufAvailable(&answer));
    out->status = ToriRSMapEd_GetU32(&reader);
    out->total = ToriRSMapEd_GetU32(&reader);
    (void)ToriRSMapEd_GetU32(&reader);
    (void)ToriRSMapEd_GetU32(&reader);
    out->rows = ToriRSMapEd_GetU32(&reader);
    for( uint32_t r = 0; r < out->rows && !reader.bad; r++ )
    {
        char name[128];
        char display[128];
        uint32_t fields;
        uint32_t const row_kind = ToriRSMapEd_GetU32(&reader);
        (void)ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetString(&reader, name, sizeof(name));
        ToriRSMapEd_GetString(&reader, display, sizeof(display));
        (void)ToriRSMapEd_GetU32(&reader);
        if( r == 0 )
            snprintf(out->first, sizeof(out->first), "%s", name);
        if( r < PAGE_ROWS_KEPT )
        {
            snprintf(out->names[r], sizeof(out->names[r]), "%s", name);
            out->kinds[r] = row_kind;
        }
        fields = ToriRSMapEd_GetU32(&reader);
        for( uint32_t f = 0; f < fields && !reader.bad; f++ )
        {
            char key[64];
            char value[256];
            ToriRSMapEd_GetString(&reader, key, sizeof(key));
            ToriRSMapEd_GetString(&reader, value, sizeof(value));
            if( r != 0 )
                continue;
            if( strcmp(key, "shapes") == 0 )
                snprintf(out->shapes, sizeof(out->shapes), "%s", value);
            else if( strcmp(key, "size") == 0 )
                snprintf(out->size, sizeof(out->size), "%s", value);
            else if( strcmp(key, "layer") == 0 )
                snprintf(out->layer, sizeof(out->layer), "%s", value);
        }
    }
    if( flags & TORIRSMAPED_CATALOG_PROGRESS )
    {
        out->page_flags = ToriRSMapEd_GetU32(&reader);
        out->done = ToriRSMapEd_GetU32(&reader);
        out->of = ToriRSMapEd_GetU32(&reader);
        out->tail = !reader.bad;
    }
    out->exact_end = ToriRSMapEd_ReaderDone(&reader);
    ToriRSMapEd_BufFree(&body);
    ToriRSMapEd_BufFree(&answer);
}

static void
ask_query(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* filter,
    uint32_t page,
    struct page_answer* out)
{
    ask_query_ex(catalog, kind, filter, 0, page, 12, out);
}

/** CATALOG_RECORD for loc `name`; the value of `key` in `value` ("" none).
 *  Returns the status. */
static uint32_t
ask_record(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* name,
    const char* key,
    char* value,
    size_t value_size)
{
    struct ToriRSMapEdBuf body;
    struct ToriRSMapEdBuf answer;
    struct ToriRSMapEdReader reader;
    char text[512];
    uint32_t status;
    uint32_t sources;
    uint32_t fields;

    memset(&body, 0, sizeof(body));
    memset(&answer, 0, sizeof(answer));
    value[0] = '\0';
    ToriRSMapEd_PutU32(&body, kind);
    ToriRSMapEd_PutU32(&body, (uint32_t)-1);
    ToriRSMapEd_PutString(&body, name);
    (void)ToriRSMapEdCatalog_HandleRecord(
        catalog, ToriRSMapEd_BufPeek(&body), (uint32_t)ToriRSMapEd_BufAvailable(&body), &answer);
    ToriRSMapEd_ReaderInit(&reader, ToriRSMapEd_BufPeek(&answer), (uint32_t)ToriRSMapEd_BufAvailable(&answer));
    status = ToriRSMapEd_GetU32(&reader);
    if( status == 0 )
    {
        (void)ToriRSMapEd_GetU32(&reader);
        (void)ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetString(&reader, text, sizeof(text));
        ToriRSMapEd_GetString(&reader, text, sizeof(text));
        (void)ToriRSMapEd_GetU32(&reader);
        sources = ToriRSMapEd_GetU32(&reader);
        for( uint32_t i = 0; i < sources && !reader.bad; i++ )
        {
            ToriRSMapEd_GetString(&reader, text, sizeof(text));
            (void)ToriRSMapEd_GetU32(&reader);
            (void)ToriRSMapEd_GetU32(&reader);
        }
        fields = ToriRSMapEd_GetU32(&reader);
        for( uint32_t i = 0; i < fields && !reader.bad; i++ )
        {
            char field_key[128];
            ToriRSMapEd_GetString(&reader, field_key, sizeof(field_key));
            ToriRSMapEd_GetString(&reader, text, sizeof(text));
            if( strcmp(field_key, key) == 0 )
                snprintf(value, value_size, "%s", text);
            (void)ToriRSMapEd_GetU32(&reader);
            ToriRSMapEd_GetString(&reader, text, sizeof(text));
            (void)ToriRSMapEd_GetU32(&reader);
        }
    }
    ToriRSMapEd_BufFree(&body);
    ToriRSMapEd_BufFree(&answer);
    return status;
}

static struct ToriRSMapEdCatalogCounters
counters(const struct ToriRSMapEdCatalog* catalog)
{
    struct ToriRSMapEdCatalogCounters c;
    ToriRSMapEdCatalog_Counters(catalog, &c);
    return c;
}

/** The longest service slice the last service_until_idle ran. */
static double g_slice_max_ms;

/** Service slices until the job is done; returns how many it took. */
static int
service_until_idle(
    struct ToriRSMapEdCatalog* catalog,
    double budget_ms)
{
    int slices = 0;
    int more = 1;
    g_slice_max_ms = 0.0;
    while( more )
    {
        double const t0 = now_ms();
        more = ToriRSMapEdCatalog_Service(catalog, budget_ms);
        if( now_ms() - t0 > g_slice_max_ms )
            g_slice_max_ms = now_ms() - t0;
        slices++;
    }
    return slices;
}

/* ---- the loc finder (design #catalog, workbench 6.2) ---- */

/** Filter values narrow as a prefix; shape classes resolve to numbers; a loc
 *  row carries shapes, size and layer. */
static void
test_finder(struct ToriRSMapEdCatalog* catalog)
{
    struct ToriRSMapEdCatalogCounters before;
    struct ToriRSMapEdCatalogCounters after;
    struct page_answer answer;
    int const doors = count_class(PERF_WALL);
    int const oaks = count_class(PERF_CENTRE);
    int const decor = count_class(PERF_DECOR);
    int const ground = count_class(PERF_GROUND);
    int const roofs = count_class(PERF_ROOF);

    /* Prefix narrowing: `op=Op` names "Open" (it matched nothing when a
     * value had to be whole), and typing on tests only the held hits. */
    ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "op=Op", 0, &answer);
    CHECK(answer.total == (uint32_t)((PERF_LOCS + 10) / 11),
        "op=Op names every Open loc as a prefix of the word (%u)", answer.total);
    {
        uint32_t const held = answer.total;
        before = counters(catalog);
        ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "op=Ope", 0, &answer);
        after = counters(catalog);
        CHECK(after.narrowed == before.narrowed + 1 && after.examined - before.examined == (long)held
                  && answer.total == held,
            "op=Ope after op=Op narrows the %u held hits (%ld tested, %u kept)", held,
            after.examined - before.examined, answer.total);
    }
    ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "op=Op", 0, &answer);
    CHECK(strcmp(answer.first, "oaktree_0") == 0 && strcmp(answer.names[1], "door_11") == 0,
        "and its hits are the Open locs, by id (%s, %s)", answer.first, answer.names[1]);
    ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "shapes=2", 0, &answer);
    CHECK(answer.total == (uint32_t)doors, "shapes=2 is the walls' 2, never ground decor's 22 (%u of %d)",
        answer.total, doors);
    before = counters(catalog);
    ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "shapes=22", 0, &answer);
    after = counters(catalog);
    CHECK(answer.total == (uint32_t)ground && after.narrowed == before.narrowed,
        "shapes=22 after shapes=2 is asked cold, not narrowed (%u of %d)", answer.total, ground);
    ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "size=2", 0, &answer);
    CHECK(answer.total == (uint32_t)(PERF_LOCS / 5), "size=2 names the 2x3 locs (%u)", answer.total);

    /* Shape classes resolve server-side; a number still works. */
    {
        static const struct
        {
            const char* filter;
            int cls;
            int cls2;
        } classes[] = {
            { "shape=wall", PERF_WALL, PERF_NONE },
            { "shape=decor", PERF_DECOR, PERF_NONE },
            { "shape=centre", PERF_CENTRE, PERF_NONE },
            { "shape=roof", PERF_ROOF, PERF_NONE },
            { "shape=ground", PERF_GROUND, PERF_NONE },
            { "shape=c", PERF_CENTRE, PERF_NONE },
            { "shape=10", PERF_CENTRE, PERF_NONE },
            { "shape=11", PERF_CENTRE, PERF_NONE },
            { "shape=9", PERF_WALL, PERF_NONE },
            { "shape=5", PERF_DECOR, PERF_NONE },
            { "shape=13", PERF_ROOF, PERF_NONE },
            { "shape=Wall", PERF_WALL, PERF_NONE },
        };
        for( size_t c = 0; c < sizeof(classes) / sizeof(classes[0]); c++ )
        {
            int const want = count_class(classes[c].cls);
            ask_query(catalog, TORIRSMAPED_CATALOG_LOC, classes[c].filter, 0, &answer);
            CHECK(answer.total == (uint32_t)want, "%s finds %d locs (%u)", classes[c].filter, want, answer.total);
        }
        ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "shape=1", 0, &answer);
        CHECK(answer.total == 0, "shape=1 is the corner post alone, which no loc has (%u)", answer.total);
        ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "shape=x", 0, &answer);
        CHECK(answer.total == 0, "a shape value naming no class finds nothing (%u)", answer.total);
        ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "shape=wall door_11", 0, &answer);
        CHECK(answer.total >= 1 && strcmp(answer.first, "door_11") == 0,
            "shape=wall plus a term (%u, %s)", answer.total, answer.first);
        CHECK(oaks + doors + decor + ground + roofs < PERF_LOCS && decor > 0 && ground > 0 && roofs > 0,
            "the tree holds every class and locs with none (%d %d %d %d %d)", oaks, doors, decor, ground, roofs);
    }

    /* The derived loc fields on a row. */
    ask_query_ex(catalog, TORIRSMAPED_CATALOG_LOC, "door_11", TORIRSMAPED_CATALOG_EXACT, 0, 12, &answer);
    CHECK(answer.rows == 1 && strcmp(answer.shapes, "0 2 9") == 0 && strcmp(answer.layer, "wall main") == 0
              && strcmp(answer.size, "1x1") == 0,
        "a fence row: shapes '%s', layer '%s', size '%s'", answer.shapes, answer.layer, answer.size);
    ask_query_ex(catalog, TORIRSMAPED_CATALOG_LOC, "oaktree_35", TORIRSMAPED_CATALOG_EXACT, 0, 12, &answer);
    CHECK(answer.rows == 1 && strcmp(answer.shapes, "10 11") == 0 && strcmp(answer.layer, "main") == 0
              && strcmp(answer.size, "2x3") == 0,
        "a centrepiece row (models=, no shape rows): shapes '%s', layer '%s', size '%s'", answer.shapes,
        answer.layer, answer.size);
    ask_query_ex(catalog, TORIRSMAPED_CATALOG_LOC, "perf_loc_26", TORIRSMAPED_CATALOG_EXACT, 0, 12, &answer);
    CHECK(answer.rows == 1 && strcmp(answer.shapes, "4 5 6 7 8") == 0 && strcmp(answer.layer, "decor") == 0,
        "a decor row: 5..8 draw shape 4's models (shapes '%s', layer '%s')", answer.shapes, answer.layer);
    ask_query_ex(catalog, TORIRSMAPED_CATALOG_LOC, "perf_loc_1", TORIRSMAPED_CATALOG_EXACT, 0, 12, &answer);
    CHECK(answer.rows == 1 && answer.shapes[0] == '\0' && answer.layer[0] == '\0' && strcmp(answer.size, "1x1") == 0,
        "a loc with no model rows has no shapes or layer, and is 1x1 ('%s', '%s', '%s')", answer.shapes,
        answer.layer, answer.size);
}

/** Kind "all" is scoped: the kinds in order, each capped at the page, so a
 *  keystroke costs its first page; a numeric term matches ids first. Timed
 *  against the unscoped scan every "all" query ran before (the in-process
 *  lookup still answers every match that way). */
static void
test_scoped_all(struct ToriRSMapEdCatalog* catalog)
{
    static const char* const typed[] = { "", "o", "oa", "door", "700", "op=Chop" };
    struct ToriRSMapEdCatalogCounters before;
    struct ToriRSMapEdCatalogCounters after;
    struct page_answer answer;
    int records[4];

    for( size_t i = 0; i < sizeof(typed) / sizeof(typed[0]); i++ )
    {
        double t0;
        double unscoped_ms;
        long unscoped_examined;
        int unscoped;
        /* another kind first: neither query may answer from held hits */
        ask_query(catalog, TORIRSMAPED_CATALOG_NPC, "", 0, &answer);
        before = counters(catalog);
        t0 = now_ms();
        unscoped = ToriRSMapEdCatalog_Query(catalog, TORIRSMAPED_CATALOG_ANY, typed[i], 0, records, 4);
        unscoped_ms = now_ms() - t0;
        after = counters(catalog);
        unscoped_examined = after.examined - before.examined;
        ask_query(catalog, TORIRSMAPED_CATALOG_NPC, "", 0, &answer);
        before = counters(catalog);
        t0 = now_ms();
        ask_query_ex(catalog, TORIRSMAPED_CATALOG_ANY, typed[i], TORIRSMAPED_CATALOG_PROGRESS, 0, 100, &answer);
        after = counters(catalog);
        printf("catalog_perf: all '%s': unscoped %.2f ms, %ld tested, %d hits -> scoped %.2f ms, %ld tested, %u "
               "on the page%s\n",
            typed[i], unscoped_ms, unscoped_examined, unscoped, now_ms() - t0, after.examined - before.examined,
            answer.total, answer.page_flags & TORIRSMAPED_CATALOG_PAGE_SCOPED ? " (cut)" : "");
        if( strcmp(typed[i], "") == 0 )
        {
            CHECK(after.examined - before.examined <= 100 && answer.total == 100
                      && (answer.page_flags & TORIRSMAPED_CATALOG_PAGE_SCOPED) && after.scoped == before.scoped + 1,
                "all '' tests what its first page holds (%ld tested, %u, flags %u), not %ld records",
                after.examined - before.examined, answer.total, answer.page_flags, unscoped_examined);
            CHECK(unscoped_examined >= PERF_LOCS, "the in-process lookup still tests every record (%ld)",
                unscoped_examined);
        }
        else if( strcmp(typed[i], "door") == 0 )
            CHECK(after.examined - before.examined < PERF_LOCS / 10 && answer.total == 100
                      && answer.kinds[0] == TORIRSMAPED_CATALOG_LOC,
                "all 'door' stops at the first page of loc doors (%ld tested, %u)", after.examined - before.examined,
                answer.total);
        else if( strcmp(typed[i], "700") == 0 )
            CHECK(answer.rows >= 2 && strcmp(answer.names[0], "oaktree_700") == 0
                      && strcmp(answer.names[1], "guard_captain") == 0,
                "all '700': the ids match first, the npc too though the locs fill the page (%s, %s)",
                answer.names[0], answer.names[1]);
        else if( strcmp(typed[i], "op=Chop") == 0 )
            CHECK(answer.total == 0 && !(answer.page_flags & TORIRSMAPED_CATALOG_PAGE_SCOPED),
                "a query nothing matches tests every kind and says nothing was cut (%u, flags %u)", answer.total,
                answer.page_flags);
    }

    /* Paging a scoped answer: page 1 widens the window, past the loc cap
     * into the npcs. */
    ask_query_ex(catalog, TORIRSMAPED_CATALOG_ANY, "a", TORIRSMAPED_CATALOG_PROGRESS, 0, 100, &answer);
    {
        uint32_t const first_total = answer.total;
        ask_query_ex(catalog, TORIRSMAPED_CATALOG_ANY, "a", TORIRSMAPED_CATALOG_PROGRESS, 1, 100, &answer);
        CHECK(first_total == 100 && answer.total > 100 && answer.rows >= 1
                  && answer.kinds[0] == TORIRSMAPED_CATALOG_NPC && strcmp(answer.names[0], "hans") == 0,
            "page 1 of a cut 'all' asks the next window, past the loc cap into the npcs (%u then %u, %s)",
            first_total, answer.total, answer.names[0]);
    }
    /* The tail is only there when asked for: a v2 reader sees the payload it
     * always did. */
    ask_query_ex(catalog, TORIRSMAPED_CATALOG_ANY, "door", 0, 0, 12, &answer);
    CHECK(answer.exact_end && answer.total == 12, "without PROGRESS the page ends at its last row (%u)",
        answer.total);
    ask_query_ex(catalog, TORIRSMAPED_CATALOG_ANY, "door", TORIRSMAPED_CATALOG_PROGRESS, 0, 12, &answer);
    CHECK(answer.exact_end && answer.tail && !(answer.page_flags & TORIRSMAPED_CATALOG_PAGE_PARTIAL)
              && answer.done == answer.of,
        "with PROGRESS it ends with the flags and progress (flags %u, %u/%u)", answer.page_flags, answer.done,
        answer.of);
}

/* ---- the synthetic tree ---- */

static void
test_synthetic(const char* root)
{
    struct ToriRSMapEdCatalog* catalog;
    struct ToriRSMapEdCatalogCounters before;
    struct ToriRSMapEdCatalogCounters after;
    struct page_answer answer;
    struct page_answer full;
    char value[128];
    int const files = PERF_SCRIPT_DIRS * PERF_SCRIPTS_PER_DIR;
    int slices;
    double t0;

    write_tree(root);
    catalog = ToriRSMapEdCatalog_Create(root);

    /* The warm build, a slice at a time. */
    t0 = now_ms();
    slices = service_until_idle(catalog, TORIRSMAPED_CATALOG_SLICE_MS);
    after = counters(catalog);
    printf("catalog_perf: warm build %.0f ms in %d slices (longest %.1f ms), %ld stats\n", now_ms() - t0, slices,
        g_slice_max_ms, after.stats);
    CHECK(ToriRSMapEdCatalog_Built(catalog), "the service pass builds the index without a request");
    CHECK(slices > 1, "the warm build is sliced (%d slices)", slices);
    CHECK(after.stats >= files, "the build stat'ed the tree (%ld stats over %d files)", after.stats, files);

    /* Every request is "after the interval" from here on: under the old rule
     * each of them re-stat'ed the whole tree first. */
    ToriRSMapEdCatalog_SetRefreshInterval(catalog, 0);
    before = counters(catalog);
    ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "", 0, &answer);
    CHECK(answer.status == 0 && answer.total == PERF_LOCS, "kind loc lists every loc (%u)", answer.total);
    CHECK(ToriRSMapEdCatalog_Working(catalog), "a request past the interval starts a sweep instead of waiting on one");
    {
        static const char* const typed[] = { "o", "oa", "oak", "oakt", "oaktr", "oaktre", "oaktree", "oaktree_7",
            "d", "do", "door", "op=Open", "op=Open door" };
        for( size_t i = 0; i < sizeof(typed) / sizeof(typed[0]); i++ )
            ask_query(catalog, TORIRSMAPED_CATALOG_LOC, typed[i], 0, &answer);
        for( int i = 0; i < 20; i++ )
        {
            char name[64];
            loc_name(i * 13, name, sizeof(name));
            CHECK(ask_record(catalog, TORIRSMAPED_CATALOG_LOC, name, "name", value, sizeof(value)) == 0 && value[0],
                "record %s answers with its name", name);
        }
    }
    after = counters(catalog);
    printf("catalog_perf: 14 queries + 20 records past the interval: %ld stats (the tree is %d files)\n",
        after.stats - before.stats, files);
    CHECK(after.stats - before.stats <= 20 * 2,
        "requests stat only the records' own files, never the tree (%ld stats)", after.stats - before.stats);
    CHECK(after.merges == before.merges, "and merge nothing");

    /* The sweep: in slices, and it catches an edit. */
    slices = service_until_idle(catalog, 0.5);
    after = counters(catalog);
    CHECK(slices > 1, "the sweep ran in %d slices of 0.5 ms", slices);
    CHECK(after.stats - before.stats >= files, "the sweep stat'ed the tree (%ld)", after.stats - before.stats);
    CHECK(after.merges == before.merges, "an unchanged tree merges nothing");

    write_text(root, "server/scripts/areas/area_3/scripts/s7.rs2",
        "[oploc1,oaktree_2149]\nmes(\"An oak.\");\n[oploc2,perf_loc_1]\nmes(\"Edited.\");\n");
    ask_query(catalog, TORIRSMAPED_CATALOG_TRIGGER, "oploc2", 0, &answer);
    CHECK(answer.total == 0, "the edit is not in the index before a sweep sees it (%u)", answer.total);
    before = counters(catalog);
    {
        uint32_t const maps = ToriRSMapEdCatalog_MapsRevision(catalog);
        slices = service_until_idle(catalog, 0.5);
        CHECK(ToriRSMapEdCatalog_MapsRevision(catalog) == maps, "a script edit is no atlas change");
    }
    after = counters(catalog);
    CHECK(after.merges == before.merges + 1, "the sweep merged the edit (%ld merges)", after.merges - before.merges);
    ask_query(catalog, TORIRSMAPED_CATALOG_TRIGGER, "oploc2", 0, &answer);
    CHECK(answer.total == 1 && strcmp(answer.first, "oploc2,perf_loc_1") == 0,
        "the edited script's new trigger is found after the sweep (%u, %s)", answer.total, answer.first);

    /* Narrowing: a query that extends the held one tests only the held hits;
     * a page flip tests nothing. */
    ToriRSMapEdCatalog_SetRefreshInterval(catalog, 3600);
    service_until_idle(catalog, 2.0);
    ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "", 0, &answer);
    before = counters(catalog);
    ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "oa", 0, &answer);
    after = counters(catalog);
    CHECK(after.examined - before.examined == PERF_LOCS,
        "'oa' after '' tests the held %d locs, not every record (%ld)", PERF_LOCS, after.examined - before.examined);
    {
        uint32_t const held = answer.total;
        before = counters(catalog);
        ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "oak", 0, &answer);
        after = counters(catalog);
        CHECK(after.examined - before.examined == (long)held && after.narrowed == before.narrowed + 1,
            "'oak' after 'oa' tests only the %u held hits (%ld)", held, after.examined - before.examined);
    }
    before = counters(catalog);
    ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "oak", 1, &answer);
    ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "oak", 2, &answer);
    after = counters(catalog);
    CHECK(after.examined == before.examined && after.held == before.held + 2,
        "a page flip tests no record (%ld)", after.examined - before.examined);

    /* Narrowing answers what a full scan answers, including where it must
     * not narrow (a number matches by id; a filter value is matched whole). */
    {
        static const char* const sequences[][4] = {
            { "o", "oa", "oak", "oak tree" },
            { "1", "12", "123", NULL },
            { "op=Open", "op=Open door", "op=Open door 1", NULL },
            { "op=Op", "op=Open", NULL, NULL },
            { "door", "door_1", "door_11", NULL },
            /* 6.2: filter values narrow as a prefix; a number never stops
             * inside a longer one */
            { "op=O", "op=Op", "op=Ope", "op=Open" },
            { "shape=w", "shape=wa", "shape=wal", "shape=wall" },
            { "shapes=2", "shapes=22", NULL, NULL },
            { "size=2", "size=2x", "size=2x3", NULL },
        };
        for( size_t s = 0; s < sizeof(sequences) / sizeof(sequences[0]); s++ )
        {
            ask_query(catalog, TORIRSMAPED_CATALOG_LOC, "", 0, &answer);
            for( int q = 0; q < 4 && sequences[s][q]; q++ )
            {
                ask_query(catalog, TORIRSMAPED_CATALOG_LOC, sequences[s][q], 0, &answer);
                /* The same question asked cold: another kind first drops the held hits. */
                ask_query(catalog, TORIRSMAPED_CATALOG_NPC, "", 0, &full);
                ask_query(catalog, TORIRSMAPED_CATALOG_LOC, sequences[s][q], 0, &full);
                CHECK(answer.total == full.total && strcmp(answer.first, full.first) == 0,
                    "typed '%s' answers %u (%s), a cold query %u (%s)", sequences[s][q], answer.total, answer.first,
                    full.total, full.first);
                /* Back to the typed state for the next keystroke. */
                ask_query(catalog, TORIRSMAPED_CATALOG_LOC, sequences[s][q], 0, &answer);
            }
        }
    }

    test_finder(catalog);
    test_scoped_all(catalog);

    /* A record view: its own block, read once; its own file, stat'ed once. */
    before = counters(catalog);
    CHECK(ask_record(catalog, TORIRSMAPED_CATALOG_LOC, "oaktree_700", "name", value, sizeof(value)) == 0
              && strcmp(value, "Oak tree 700") == 0,
        "oaktree_700's record reads its block (%s)", value);
    after = counters(catalog);
    CHECK(after.block_reads - before.block_reads == 1, "one block read for a one-file record (%ld)",
        after.block_reads - before.block_reads);
    CHECK(after.stats - before.stats <= 2, "the pick stats its own files only (%ld)", after.stats - before.stats);
    CHECK(after.merges == before.merges, "and merges nothing");

    /* A record whose overlay changed: re-read alone, not by a tree scan. */
    CHECK(ask_record(catalog, TORIRSMAPED_CATALOG_LOC, "perf_loc_5", "op2", value, sizeof(value)) == 0
              && strcmp(value, "Kick") == 0,
        "perf_loc_5 carries its overlay's op2 (%s)", value);
    write_text(root, "server/scripts/areas/area_0/configs/perf.loc", "[perf_loc_5]\nop2=Kick hard\n");
    before = counters(catalog);
    CHECK(ask_record(catalog, TORIRSMAPED_CATALOG_LOC, "perf_loc_5", "op2", value, sizeof(value)) == 0
              && strcmp(value, "Kick hard") == 0,
        "the edited overlay is read back on the next view (%s)", value);
    after = counters(catalog);
    CHECK(after.merges == before.merges + 1 && after.stats - before.stats <= 6,
        "by re-reading the changed file alone (%ld stats, %ld merges)", after.stats - before.stats,
        after.merges - before.merges);

    ToriRSMapEdCatalog_Free(catalog);
}

/* ---- the warm build never blocks: partial answers, locs first ---- */

static void
test_partial_build(const char* root)
{
    struct ToriRSMapEdCatalog* warm = ToriRSMapEdCatalog_Create(root);
    struct ToriRSMapEdCatalog* whole;
    struct ToriRSMapEdCatalogCounters c;
    struct page_answer answer;
    struct page_answer other;
    int slices = 0;
    int saw_locs_first = 0;
    int locs_after_triggers = 0;
    int partial_answers = 0;
    int more = 1;
    double slice_max = 0.0;
    double query_max = 0.0;
    uint32_t last_done = 0;
    int progress_rose = 1;

    /* 256 KB parts: the synthetic all.loc (4.5 MB) is many of them. */
    ToriRSMapEdCatalog_SetSliceBytes(warm, 256 * 1024);
    while( more )
    {
        double t0 = now_ms();
        more = ToriRSMapEdCatalog_Service(warm, TORIRSMAPED_CATALOG_SLICE_MS);
        if( now_ms() - t0 > slice_max )
            slice_max = now_ms() - t0;
        slices++;
        if( !more )
            break;
        t0 = now_ms();
        ask_query_ex(warm, TORIRSMAPED_CATALOG_LOC, "", TORIRSMAPED_CATALOG_PROGRESS, 0, 12, &answer);
        if( now_ms() - t0 > query_max )
            query_max = now_ms() - t0;
        if( !(answer.page_flags & TORIRSMAPED_CATALOG_PAGE_PARTIAL) )
            continue;
        partial_answers++;
        if( answer.done < last_done )
            progress_rose = 0;
        last_done = answer.done;
        ask_query_ex(warm, TORIRSMAPED_CATALOG_TRIGGER, "", TORIRSMAPED_CATALOG_PROGRESS, 0, 12, &other);
        if( other.total > 0 && answer.total < PERF_LOCS )
            locs_after_triggers++;
        if( answer.total > 0 && !saw_locs_first )
        {
            saw_locs_first = 1;
            CHECK(!ToriRSMapEdCatalog_Built(warm) && answer.tail && answer.of > 0 && answer.done < answer.of,
                "a query during the warm build answers from the partial index at %u/%u KiB", answer.done, answer.of);
            CHECK(other.total == 0, "the locs are merged first: %u locs while %u triggers", answer.total, other.total);
        }
    }
    CHECK(locs_after_triggers == 0, "no trigger was merged before every loc (%d answers said so)",
        locs_after_triggers);
    ToriRSMapEdCatalog_Counters(warm, &c);
    printf("catalog_perf: warm build in %d slices of %.0f ms (longest %.1f ms), %ld parts (largest %ld bytes), %ld "
           "sources merged one at a time; %d partial answers, the slowest %.2f ms\n",
        slices, TORIRSMAPED_CATALOG_SLICE_MS, slice_max, c.parts, c.part_bytes_max, c.incremental, partial_answers,
        query_max);
    CHECK(saw_locs_first, "a partial answer held the locs before the build ended (%d partial answers)",
        partial_answers);
    CHECK(progress_rose, "the progress only rises");
    CHECK(c.parts >= 4 && c.part_bytes_max <= 256 * 1024,
        "configs/all.loc is parsed in byte slices, none past the part size (%ld parts, largest %ld)", c.parts,
        c.part_bytes_max);
    CHECK(c.merges == 1 && c.incremental > 3,
        "the warm build merges source by source and runs no full merge (%ld incremental, %ld whole)", c.incremental,
        c.merges);
    ask_query_ex(warm, TORIRSMAPED_CATALOG_LOC, "", TORIRSMAPED_CATALOG_PROGRESS, 0, 12, &answer);
    CHECK(!(answer.page_flags & TORIRSMAPED_CATALOG_PAGE_PARTIAL) && answer.done == answer.of
              && answer.total == PERF_LOCS,
        "once built the answer is whole (flags %u, %u/%u, %u)", answer.page_flags, answer.done, answer.of,
        answer.total);

    /* The index the warm build merged a source at a time answers what a
     * whole build's full merge answers. */
    whole = ToriRSMapEdCatalog_Create(root);
    (void)ToriRSMapEdCatalog_Refresh(whole, 1, NULL);
    {
        static const struct
        {
            uint32_t kind;
            const char* filter;
            uint32_t flags;
        } asks[] = {
            { TORIRSMAPED_CATALOG_LOC, "", 0 },
            { TORIRSMAPED_CATALOG_LOC, "oak", 0 },
            { TORIRSMAPED_CATALOG_LOC, "op=Kick", 0 },
            { TORIRSMAPED_CATALOG_LOC, "Door 22", TORIRSMAPED_CATALOG_EXACT },
            { TORIRSMAPED_CATALOG_LOC, "700", TORIRSMAPED_CATALOG_EXACT },
            { TORIRSMAPED_CATALOG_NPC, "Guard captain", TORIRSMAPED_CATALOG_EXACT },
            { TORIRSMAPED_CATALOG_TRIGGER, "oploc", 0 },
            { TORIRSMAPED_CATALOG_ANY, "door_7", 0 },
            { TORIRSMAPED_CATALOG_LOC, "shape=roof", 0 },
        };
        for( size_t i = 0; i < sizeof(asks) / sizeof(asks[0]); i++ )
        {
            ask_query_ex(warm, asks[i].kind, asks[i].filter, asks[i].flags, 0, 12, &answer);
            ask_query_ex(whole, asks[i].kind, asks[i].filter, asks[i].flags, 0, 12, &other);
            CHECK(answer.total == other.total && strcmp(answer.first, other.first) == 0
                      && strcmp(answer.shapes, other.shapes) == 0,
                "'%s': the warm index answers %u (%s), a full merge %u (%s)", asks[i].filter, answer.total,
                answer.first, other.total, other.first);
        }
    }
    ToriRSMapEdCatalog_Free(whole);
    ToriRSMapEdCatalog_Free(warm);

    /* A v2 query (no PROGRESS) during the build still waits for it, as it
     * always did. */
    warm = ToriRSMapEdCatalog_Create(root);
    for( int i = 0; i < 3; i++ )
        (void)ToriRSMapEdCatalog_Service(warm, TORIRSMAPED_CATALOG_SLICE_MS);
    ask_query(warm, TORIRSMAPED_CATALOG_LOC, "", 0, &answer);
    CHECK(ToriRSMapEdCatalog_Built(warm) && answer.total == PERF_LOCS && answer.exact_end,
        "a query without PROGRESS finishes the build first (%u)", answer.total);
    ToriRSMapEdCatalog_Free(warm);

    /* A PROGRESS query to a catalog nobody serviced starts the build with
     * one slice and answers partial; it never runs the build to its end. */
    warm = ToriRSMapEdCatalog_Create(root);
    ask_query_ex(warm, TORIRSMAPED_CATALOG_LOC, "", TORIRSMAPED_CATALOG_PROGRESS, 0, 12, &answer);
    CHECK(!ToriRSMapEdCatalog_Built(warm) && ToriRSMapEdCatalog_Working(warm)
              && (answer.page_flags & TORIRSMAPED_CATALOG_PAGE_PARTIAL),
        "a cold PROGRESS query starts the build and answers partial (flags %u, %u)", answer.page_flags, answer.total);
    ToriRSMapEdCatalog_Free(warm);
}

/* ---- the square atlas, content-free (workbench 6.4) ---- */

/* A tree of three .jl2 squares: the atlas row's loc count is the locs each
 * places (one a line after the header), the wire carries it after the row's
 * areas, and an unmapped square in instance space (0..19 x 100..255) is
 * flagged TORIRSMAPED_SQUARE_INSTANCE while one outside it is not. */
static void
test_atlas(const char* root)
{
    static struct Editor_Doc doc;
    struct ToriRSMapEdCatalog* catalog;
    struct ToriRSMapEdCatalogSquare square;
    struct ToriRSMapEdBuf answer;
    struct ToriRSMapEdReader reader;
    uint32_t count;
    int seen = 0;

    mkdir(root, 0755);
    make_dir(root, "maps");
    write_text(root, "maps/m50_50.jl2", "==== LOC ====\n0 1 2: 100 10 0\n0 3 4: 101 10 1\n1 5 6: 102 22 2\n");
    write_text(root, "maps/m5_120.jl2", "==== LOC ====\n0 8 8: 7 10 0\n");
    write_text(root, "maps/m40_40.jl2", "==== LOC ====\n");
    catalog = ToriRSMapEdCatalog_Create(root);
    service_until_idle(catalog, TORIRSMAPED_CATALOG_SLICE_MS);

    CHECK(ToriRSMapEdCatalog_Square(catalog, "m50_50", &square) && square.loc_count == 3
              && (square.flags & TORIRSMAPED_SQUARE_HAS_LOCS),
        "m50_50 places 3 locs (%d)", square.loc_count);
    CHECK(!(square.flags & TORIRSMAPED_SQUARE_INSTANCE), "...and is not in instance space");
    CHECK(ToriRSMapEdCatalog_Square(catalog, "m5_120", &square) && square.loc_count == 1
              && (square.flags & TORIRSMAPED_SQUARE_INSTANCE),
        "m5_120: 1 loc, unmapped in instance space, flagged (flags %u)", square.flags);
    CHECK(ToriRSMapEdCatalog_Square(catalog, "m40_40", &square) && square.loc_count == 0
              && !(square.flags & TORIRSMAPED_SQUARE_HAS_LOCS),
        "m40_40: a header and no locs (%d)", square.loc_count);

    /* The wire, in both forms: the v2 request (empty body) gets the v2
     * rows, which carry no count; a request with
     * TORIRSMAPED_CATALOG_SQUARES_LOCS gets every row flagged
     * TORIRSMAPED_SQUARE_LOC_COUNT and ending with its count. */
    Editor_DocInit(&doc, NULL);
    for( int form = 0; form < 2; form++ )
    {
        uint8_t body[4];
        int with_locs = -1;
        int flagged = 0;
        seen = 0;
        ToriRSMapEd_WriteU32(body, TORIRSMAPED_CATALOG_SQUARES_LOCS);
        memset(&answer, 0, sizeof(answer));
        CHECK(ToriRSMapEdCatalog_HandleSquares(catalog, &doc, body, form ? 4 : 0, &with_locs, &answer)
                  && with_locs == form,
            "CATALOG_SQUARES with %s body answers, rows %s loc counts", form ? "a LOCS" : "an empty",
            form ? "with" : "without");
        ToriRSMapEd_ReaderInit(
            &reader, ToriRSMapEd_BufPeek(&answer), (uint32_t)ToriRSMapEd_BufAvailable(&answer));
        CHECK(ToriRSMapEd_GetU32(&reader) == 0, "CATALOG_SQUARES answers OK");
        count = ToriRSMapEd_GetU32(&reader);
        for( uint32_t i = 0; i < count && !reader.bad; i++ )
        {
            char name[256];
            ToriRSMapEd_GetString(&reader, name, sizeof(name));
        }
        count = ToriRSMapEd_GetU32(&reader);
        for( uint32_t i = 0; i < count && !reader.bad; i++ )
        {
            char name[256];
            ToriRSMapEd_GetString(&reader, name, sizeof(name));
            ToriRSMapEd_GetString(&reader, name, sizeof(name));
            (void)ToriRSMapEd_GetU32(&reader);
        }
        count = ToriRSMapEd_GetU32(&reader);
        CHECK(count == 3, "three rows (%u)", count);
        for( uint32_t i = 0; i < count && !reader.bad; i++ )
        {
            int32_t const x = (int32_t)ToriRSMapEd_GetU32(&reader);
            int32_t const z = (int32_t)ToriRSMapEd_GetU32(&reader);
            uint32_t flags;
            uint32_t area_n;
            uint32_t locs = 0;
            (void)ToriRSMapEd_GetU32(&reader);
            flags = ToriRSMapEd_GetU32(&reader);
            (void)ToriRSMapEd_GetU32(&reader);
            (void)ToriRSMapEd_GetU32(&reader);
            (void)ToriRSMapEd_GetU32(&reader);
            area_n = ToriRSMapEd_GetU32(&reader);
            for( uint32_t a = 0; a < area_n && !reader.bad; a++ )
                (void)ToriRSMapEd_GetU32(&reader);
            flagged += (flags & TORIRSMAPED_SQUARE_LOC_COUNT) != 0;
            if( flags & TORIRSMAPED_SQUARE_LOC_COUNT )
                locs = ToriRSMapEd_GetU32(&reader);
            if( x == 50 && z == 50 )
                seen += locs == (form ? 3u : 0u);
            if( x == 5 && z == 120 )
                seen += locs == (form ? 1u : 0u) && (flags & TORIRSMAPED_SQUARE_INSTANCE);
            if( x == 40 && z == 40 )
                seen += locs == 0;
        }
        CHECK(!reader.bad && ToriRSMapEd_ReaderDone(&reader), "the rows read to the end, nothing left over");
        CHECK(flagged == (form ? 3 : 0), "%d of 3 rows flagged LOC_COUNT", flagged);
        CHECK(seen == 3, "each row carries its loc count or none, the instance row its flag (%d of 3)", seen);
        ToriRSMapEd_BufFree(&answer);
    }
    {
        /* an unknown request flag, or a body that is not one u32, ends the
         * session (0), as any malformed frame does */
        uint8_t body[8] = { 0 };
        int with_locs = 0;
        ToriRSMapEd_WriteU32(body, 2);
        memset(&answer, 0, sizeof(answer));
        CHECK(!ToriRSMapEdCatalog_HandleSquares(catalog, &doc, body, 4, &with_locs, &answer),
            "an unknown CATALOG_SQUARES flag is refused");
        CHECK(!ToriRSMapEdCatalog_HandleSquares(catalog, &doc, body, 8, &with_locs, &answer),
            "a CATALOG_SQUARES body that is not one u32 is refused");
    }
    ToriRSMapEd_BufFree(&answer);
    Editor_DocFree(&doc);
    ToriRSMapEdCatalog_Free(catalog);
}

/* ---- the real tree, printed ---- */

static void
time_query(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* filter)
{
    struct page_answer answer;
    double const t0 = now_ms();
    ask_query(catalog, kind, filter, 0, &answer);
    printf("catalog_perf: real  %-4s '%s': %.2f ms (%u)\n", kind == TORIRSMAPED_CATALOG_ANY ? "any" : "loc", filter,
        now_ms() - t0, answer.total);
}

static void
test_real(const char* content)
{
    struct ToriRSMapEdCatalog* catalog = ToriRSMapEdCatalog_Create(content);
    struct ToriRSMapEdCatalogCounters c;
    struct ToriRSMapEdCatalogStats stats;
    char value[128];
    double t0;
    int slices;
    static const char* const typed[] = { "", "o", "oa", "oak", "oakt", "oaktr", "oaktre", "oaktree" };

    t0 = now_ms();
    slices = service_until_idle(catalog, TORIRSMAPED_CATALOG_SLICE_MS);
    ToriRSMapEdCatalog_Counters(catalog, &c);
    printf("catalog_perf: real tree warm build %.0f ms in %d slices, longest slice %.1f ms\n", now_ms() - t0, slices,
        g_slice_max_ms);
    t0 = now_ms();
    (void)ToriRSMapEdCatalog_Refresh(catalog, 1, &stats);
    printf("catalog_perf: real tree whole re-stat (what every request after a 2 s pause paid): %.0f ms\n",
        now_ms() - t0);
    ToriRSMapEdCatalog_SetRefreshInterval(catalog, 0);
    for( size_t i = 0; i < sizeof(typed) / sizeof(typed[0]); i++ )
        time_query(catalog, TORIRSMAPED_CATALOG_LOC, typed[i]);
    for( size_t i = 0; i < sizeof(typed) / sizeof(typed[0]); i++ )
        time_query(catalog, TORIRSMAPED_CATALOG_ANY, typed[i]);
    time_query(catalog, TORIRSMAPED_CATALOG_LOC, "op=Chop");
    time_query(catalog, TORIRSMAPED_CATALOG_ANY, "op=Chop");
    t0 = now_ms();
    (void)ask_record(catalog, TORIRSMAPED_CATALOG_LOC, "oaktree", "name", value, sizeof(value));
    printf("catalog_perf: real  record loc oaktree: %.2f ms (%s)\n", now_ms() - t0, value);
    t0 = now_ms();
    slices = service_until_idle(catalog, TORIRSMAPED_CATALOG_SLICE_MS);
    ToriRSMapEdCatalog_Counters(catalog, &c);
    printf("catalog_perf: real tree sweep %.0f ms wall in %d slices of %.0f ms, longest %.1f ms (%ld stats so far)\n",
        now_ms() - t0, slices, TORIRSMAPED_CATALOG_SLICE_MS, g_slice_max_ms, c.stats);
    ToriRSMapEdCatalog_Free(catalog);
}

int
main(
    int argc,
    char** argv)
{
    char root[2048];

    if( argc < 2 )
    {
        fprintf(stderr, "usage: catalog_perf_test <scratch-dir> [<content-dir>]\n");
        return 2;
    }
    /* This file measures the SLICED warm build -- its slices, its parts, its
     * partial answers between Service passes -- so it asks for that build on
     * every platform. Before the first catalog exists: the catalog reads the
     * variable once. The worker-thread build is catalog_test's and
     * maped_embed_test's (they run with the platform's default). */
    {
        static char k_sliced[] = "TORIRS_CATALOG_WARM_THREAD=0";
        int const set = putenv(k_sliced);
        assert(set == 0);
        (void)set;
    }
    snprintf(root, sizeof(root), "%s/content", argv[1]);
    test_synthetic(root);
    test_partial_build(root);
    snprintf(root, sizeof(root), "%s/atlas", argv[1]);
    test_atlas(root);
    if( argc > 2 && argv[2][0] )
    {
        struct stat st;
        if( stat(argv[2], &st) == 0 )
            test_real(argv[2]);
    }
    printf("catalog_perf_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
