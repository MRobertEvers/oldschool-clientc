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
 * Synthetic: a 60,000-loc tree with 3,000 script files, written under
 * argv[1]. Then, given the real content tree as argv[2], the same requests
 * timed on it and printed (not checked).
 *
 * usage: catalog_perf_test <scratch-dir> [<content-dir>]
 */

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
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
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
        fprintf(config, "[%s]\nname=%s %d\n%s\n\n", name, i % 7 == 0 ? "Oak tree" : i % 11 == 0 ? "Door" : "Thing",
            i, i % 11 == 0 ? "op1=Open" : "op1=Search");
        fprintf(compack, "%d=%s\n", i, name);
    }
    fclose(config);
    fclose(compack);
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

struct page_answer
{
    uint32_t status;
    uint32_t total;
    uint32_t rows;
    char first[128];
};

static void
ask_query(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* filter,
    uint32_t page,
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
    ToriRSMapEd_PutU32(&body, 0);
    ToriRSMapEd_PutU32(&body, page);
    ToriRSMapEd_PutU32(&body, 12);
    (void)ToriRSMapEdCatalog_HandleQuery(
        catalog, ToriRSMapEd_BufPeek(&body), (uint32_t)ToriRSMapEd_BufAvailable(&body), &answer);
    ToriRSMapEd_ReaderInit(&reader, ToriRSMapEd_BufPeek(&answer), (uint32_t)ToriRSMapEd_BufAvailable(&answer));
    out->status = ToriRSMapEd_GetU32(&reader);
    out->total = ToriRSMapEd_GetU32(&reader);
    (void)ToriRSMapEd_GetU32(&reader);
    (void)ToriRSMapEd_GetU32(&reader);
    out->rows = ToriRSMapEd_GetU32(&reader);
    if( out->rows > 0 )
    {
        char display[128];
        (void)ToriRSMapEd_GetU32(&reader);
        (void)ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetString(&reader, out->first, sizeof(out->first));
        ToriRSMapEd_GetString(&reader, display, sizeof(display));
    }
    ToriRSMapEd_BufFree(&body);
    ToriRSMapEd_BufFree(&answer);
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
    snprintf(root, sizeof(root), "%s/content", argv[1]);
    test_synthetic(root);
    if( argc > 2 && argv[2][0] )
    {
        struct stat st;
        if( stat(argv[2], &st) == 0 )
            test_real(argv[2]);
    }
    printf("catalog_perf_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
