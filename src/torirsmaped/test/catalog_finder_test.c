/*
 * The catalog as a loc finder (design #catalog "Finding locs", workbench
 * step 6.2), over a tiny synthetic tree written under argv[1] -- no content
 * tree needed:
 *
 *   - a filter value matches whole, then as a prefix of a whole word, then
 *     as a written-out `value*` prefix, and the hits rank in that order; a
 *     number never stops inside a longer number;
 *   - typing on narrows the held hits only where the longer value implies
 *     the shorter;
 *   - `shape=` resolves the five classes (and numbers) server-side against
 *     the loc's placeable shapes;
 *   - a loc row carries `shapes`, `size` and `layer`, from the merged model
 *     rows (an overlay's win);
 *   - the PROGRESS tail is there only when asked for, and a scoped "all"
 *     says when it cut.
 *
 * usage: catalog_finder_test <scratch-dir>
 */

#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_buf.h"
#include "torirsmaped/torirs_maped_catalog.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static void
make_dir(
    const char* root,
    const char* relative)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    mkdir(path, 0755);
}

static void
write_text(
    const char* root,
    const char* relative,
    const char* text)
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
    fputs(text, f);
    fclose(f);
}

static void
write_tree(const char* root)
{
    mkdir(root, 0755);
    make_dir(root, "configs");
    make_dir(root, "server");
    make_dir(root, "server/scripts");
    make_dir(root, "server/scripts/area");
    make_dir(root, "server/scripts/area/configs");
    write_text(root, "configs/all.loc",
        "// finder test locs\n\n"
        "[fence_wood]\nname=Fence\nshape1=0,100\nshape2=2,101\nshape3=9,102\nop1=Open\n\n"
        "[fence_wood_corner]\nname=Fence corner\nshape1=3,103\n\n"
        "[crate]\nname=Crate\nmodels=200\nwidth=2\nlength=1\nop1=Search\n\n"
        "[banner]\nname=Banner\nshape1=4,300\n\n"
        "[rug]\nname=Rug\nshape1=22,400\n\n"
        "[roof_a]\nshape1=12,500\nshape2=21,501\n\n"
        "[odd11]\nshape1=11,600\n\n"
        "[combo]\nshape1=10,700\nshape2=0,701\n\n"
        "[shell]\nmultiloc1=crate\nmultiloc2=rug\n\n"
        "[opener]\nop1=Opener\n\n"
        "[openx]\nop1=Open\nop2=Openly\n\n"
        "[varloc]\nmultivarbit=varb4153_ics_little_var\nmultiloc1=crate\n\n"
        "[stub]\nshape1=0\n\n");
    write_text(root, "configs/all.loc.compack",
        "0=fence_wood\n1=fence_wood_corner\n2=crate\n3=banner\n4=rug\n5=roof_a\n6=odd11\n7=combo\n8=shell\n"
        "9=opener\n10=openx\n11=varloc\n12=stub\n");
    write_text(root, "configs/all.npc", "[hans]\nname=Hans\n\n[open_guard]\nname=Guard\nop1=Open\n");
    write_text(root, "configs/all.npc.compack", "0=hans\n1=open_guard\n");
    /* The server's overlay restates the crate's model rows: it is ground
     * decor now. */
    write_text(root, "server/scripts/area/configs/area.loc", "[crate]\nshape1=22,999\n");
}

/* ---- the wire ---- */

struct answer
{
    uint32_t status;
    uint32_t total;
    uint32_t rows;
    char names[16][256];
    char shapes[256];
    char size[256];
    char layer[256];
    int has_shapes;
    int has_layer;
    uint32_t page_flags;
    uint32_t done;
    uint32_t of;
    int tail;
    int exact_end;
};

static void
ask(
    struct ToriRSMapEdCatalog* catalog,
    uint32_t kind,
    const char* filter,
    uint32_t flags,
    uint32_t page_size,
    struct answer* out)
{
    struct ToriRSMapEdBuf body;
    struct ToriRSMapEdBuf reply;
    struct ToriRSMapEdReader reader;

    memset(&body, 0, sizeof(body));
    memset(&reply, 0, sizeof(reply));
    memset(out, 0, sizeof(*out));
    ToriRSMapEd_PutU32(&body, kind);
    ToriRSMapEd_PutString(&body, filter);
    ToriRSMapEd_PutU32(&body, flags);
    ToriRSMapEd_PutU32(&body, 0);
    ToriRSMapEd_PutU32(&body, page_size);
    CHECK(ToriRSMapEdCatalog_HandleQuery(
              catalog, ToriRSMapEd_BufPeek(&body), (uint32_t)ToriRSMapEd_BufAvailable(&body), &reply),
        "'%s' is a well-formed query", filter);
    ToriRSMapEd_ReaderInit(&reader, ToriRSMapEd_BufPeek(&reply), (uint32_t)ToriRSMapEd_BufAvailable(&reply));
    out->status = ToriRSMapEd_GetU32(&reader);
    out->total = ToriRSMapEd_GetU32(&reader);
    (void)ToriRSMapEd_GetU32(&reader);
    (void)ToriRSMapEd_GetU32(&reader);
    out->rows = ToriRSMapEd_GetU32(&reader);
    for( uint32_t r = 0; r < out->rows && !reader.bad; r++ )
    {
        char text[256];
        uint32_t fields;
        (void)ToriRSMapEd_GetU32(&reader);
        (void)ToriRSMapEd_GetU32(&reader);
        ToriRSMapEd_GetString(&reader, text, sizeof(text));
        if( r < 16 )
            snprintf(out->names[r], sizeof(out->names[r]), "%s", text);
        ToriRSMapEd_GetString(&reader, text, sizeof(text));
        (void)ToriRSMapEd_GetU32(&reader);
        fields = ToriRSMapEd_GetU32(&reader);
        for( uint32_t f = 0; f < fields && !reader.bad; f++ )
        {
            char key[64];
            ToriRSMapEd_GetString(&reader, key, sizeof(key));
            ToriRSMapEd_GetString(&reader, text, sizeof(text));
            if( r != 0 )
                continue;
            if( strcmp(key, "shapes") == 0 )
            {
                snprintf(out->shapes, sizeof(out->shapes), "%s", text);
                out->has_shapes = 1;
            }
            else if( strcmp(key, "size") == 0 )
                snprintf(out->size, sizeof(out->size), "%s", text);
            else if( strcmp(key, "layer") == 0 )
            {
                snprintf(out->layer, sizeof(out->layer), "%s", text);
                out->has_layer = 1;
            }
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
    ToriRSMapEd_BufFree(&reply);
}

static void
ask_loc(
    struct ToriRSMapEdCatalog* catalog,
    const char* filter,
    struct answer* out)
{
    ask(catalog, TORIRSMAPED_CATALOG_LOC, filter, 0, 16, out);
}

static int
has_row(
    const struct answer* a,
    const char* name)
{
    for( uint32_t r = 0; r < a->rows && r < 16; r++ )
    {
        if( strcmp(a->names[r], name) == 0 )
            return 1;
    }
    return 0;
}

static struct ToriRSMapEdCatalogCounters
counters(const struct ToriRSMapEdCatalog* catalog)
{
    struct ToriRSMapEdCatalogCounters c;
    ToriRSMapEdCatalog_Counters(catalog, &c);
    return c;
}

/* ---- the checks ---- */

static void
test_value_rules(struct ToriRSMapEdCatalog* catalog)
{
    struct answer a;

    /* whole first, then a prefix of a word */
    ask_loc(catalog, "op=Open", &a);
    CHECK(a.total == 3 && strcmp(a.names[0], "fence_wood") == 0 && strcmp(a.names[1], "openx") == 0
              && strcmp(a.names[2], "opener") == 0,
        "op=Open: the whole matches rank before 'Opener' (%u: %s %s %s)", a.total, a.names[0], a.names[1],
        a.names[2]);
    ask_loc(catalog, "op=Opene", &a);
    CHECK(a.total == 1 && strcmp(a.names[0], "opener") == 0, "op=Opene: Opener alone (%u)", a.total);
    ask_loc(catalog, "op=open", &a);
    CHECK(a.total == 3, "a value matches case-insensitively (%u)", a.total);
    ask_loc(catalog, "op=pen", &a);
    CHECK(a.total == 0, "but only from a word's start (%u)", a.total);
    ask_loc(catalog, "op=Openly", &a);
    CHECK(a.total == 1 && strcmp(a.names[0], "openx") == 0, "the family covers op2 (%u)", a.total);

    /* the old rule, a prefix ending at '_', is the first word's prefix */
    ask_loc(catalog, "multivar=varb4153", &a);
    CHECK(a.total == 1 && strcmp(a.names[0], "varloc") == 0, "multivar=varb4153 names varb4153_ics_... (%u)",
        a.total);
    ask_loc(catalog, "multivar=varb41", &a);
    CHECK(a.total == 1, "and narrows as it is typed (%u)", a.total);

    /* a number never stops inside a longer number */
    ask_loc(catalog, "shapes=2", &a);
    CHECK(a.total == 1 && strcmp(a.names[0], "fence_wood") == 0, "shapes=2 is not the rug's 22 (%u)", a.total);
    ask_loc(catalog, "shapes=22", &a);
    CHECK(a.total == 2 && has_row(&a, "rug") && has_row(&a, "crate"), "shapes=22: the rug and the crate (%u)",
        a.total);
    ask_loc(catalog, "size=2", &a);
    CHECK(a.total == 1 && strcmp(a.names[0], "crate") == 0, "size=2 names 2x1 (%u)", a.total);
    ask_loc(catalog, "size=1", &a);
    CHECK(a.total == 12 && !has_row(&a, "crate"), "size=1 names every 1x1 loc, never 2x1 (%u)", a.total);
    ask_loc(catalog, "size=1x1", &a);
    CHECK(a.total == 12, "size=1x1 whole (%u)", a.total);

    /* written-out wildcards and quoted values */
    ask_loc(catalog, "name=fence_wood*", &a);
    CHECK(a.total == 2 && strcmp(a.names[0], "fence_wood") == 0, "name=fence_wood* is a prefix of the whole name (%u)",
        a.total);
    ask_loc(catalog, "name=wood*", &a);
    CHECK(a.total == 0, "which never starts mid-name (%u)", a.total);
    ask_loc(catalog, "name=\"Fence corner\"", &a);
    CHECK(a.total == 1 && strcmp(a.names[0], "fence_wood_corner") == 0, "a quoted display name (%u)", a.total);
    ask_loc(catalog, "name=\"Fence c\"", &a);
    CHECK(a.total == 1, "a quoted value narrows as a prefix too (%u)", a.total);
    ask_loc(catalog, "name=Fence", &a);
    CHECK(a.total == 2 && strcmp(a.names[0], "fence_wood") == 0 && strcmp(a.names[1], "fence_wood_corner") == 0,
        "name=Fence: the whole display first, then 'Fence corner' (%u: %s, %s)", a.total, a.names[0], a.names[1]);
    ask_loc(catalog, "id=1", &a);
    CHECK(a.total == 1 && strcmp(a.names[0], "fence_wood_corner") == 0, "id= is whole (%u)", a.total);
}

static void
test_narrowing(struct ToriRSMapEdCatalog* catalog)
{
    static const struct
    {
        const char* first;
        const char* then;
        int narrows;
    } steps[] = {
        { "op=Op", "op=Ope", 1 },
        { "op=Ope", "op=Open", 1 },
        { "op=Open", "op=Opene", 1 },
        { "shapes=2", "shapes=22", 0 },
        { "shapes=2", "shapes=2*", 0 },
        { "size=1", "size=1x", 1 },
        { "name=fence*", "name=fence_w*", 1 },
        { "name=fence*", "name=fence_wood", 0 },
        { "shape=w", "shape=wa", 1 },
        { "shape=1", "shape=12", 0 },
        { "op=Open", "op=Open crate", 1 },
    };
    struct answer typed;
    struct answer cold;

    for( size_t s = 0; s < sizeof(steps) / sizeof(steps[0]); s++ )
    {
        struct ToriRSMapEdCatalogCounters before;
        struct ToriRSMapEdCatalogCounters after;
        ask_loc(catalog, steps[s].first, &typed);
        before = counters(catalog);
        ask_loc(catalog, steps[s].then, &typed);
        after = counters(catalog);
        CHECK((after.narrowed == before.narrowed + 1) == steps[s].narrows, "'%s' after '%s' %s",
            steps[s].then, steps[s].first, steps[s].narrows ? "narrows the held hits" : "is asked cold");
        ask(catalog, TORIRSMAPED_CATALOG_NPC, "", 0, 16, &cold);
        ask_loc(catalog, steps[s].then, &cold);
        CHECK(typed.total == cold.total && strcmp(typed.names[0], cold.names[0]) == 0,
            "'%s' typed answers %u (%s), cold %u (%s)", steps[s].then, typed.total, typed.names[0], cold.total,
            cold.names[0]);
    }
}

static void
test_shape_classes(struct ToriRSMapEdCatalog* catalog)
{
    static const struct
    {
        const char* filter;
        uint32_t total;
        const char* first;
    } classes[] = {
        { "shape=wall", 3, "fence_wood" },        /* 0 2 9, 3, and the combo's 0 */
        { "shape=decor", 1, "banner" },           /* 4 drawn as 4..8 */
        { "shape=centre", 1, "combo" },           /* 10 (and 11 from it) */
        { "shape=roof", 1, "roof_a" },
        { "shape=ground", 2, "crate" },           /* the rug, and the crate by its overlay */
        { "shape=c", 1, "combo" },
        { "shape=Ro", 1, "roof_a" },
        { "shape=g*", 2, "crate" },
        { "shape=10", 1, "combo" },
        { "shape=11", 1, "combo" },               /* odd11's own 11 has no 10 models to draw */
        { "shape=9", 1, "fence_wood" },
        { "shape=7", 1, "banner" },
        { "shape=21", 1, "roof_a" },
        { "shape=1", 0, "" },
        { "shape=23", 0, "" },
        { "shape=window", 0, "" },
    };
    struct answer a;

    for( size_t c = 0; c < sizeof(classes) / sizeof(classes[0]); c++ )
    {
        ask_loc(catalog, classes[c].filter, &a);
        CHECK(a.total == classes[c].total && (a.total == 0 || strcmp(a.names[0], classes[c].first) == 0),
            "%s finds %u (%s): %u (%s)", classes[c].filter, classes[c].total, classes[c].first, a.total,
            a.names[0]);
    }
    ask(catalog, TORIRSMAPED_CATALOG_ANY, "shape=wall", 0, 16, &a);
    CHECK(a.total == 3, "shape= names locs only, never an npc (%u)", a.total);
    ask_loc(catalog, "shape=wall shape=centre", &a);
    CHECK(a.total == 1 && strcmp(a.names[0], "combo") == 0, "two shape filters both hold (%u)", a.total);
    ask_loc(catalog, "shape=wall op=Open", &a);
    CHECK(a.total == 1 && strcmp(a.names[0], "fence_wood") == 0, "a shape class with another filter (%u)", a.total);
}

static void
test_loc_fields(struct ToriRSMapEdCatalog* catalog)
{
    static const struct
    {
        const char* name;
        const char* shapes;
        const char* layer;
        const char* size;
    } rows[] = {
        { "fence_wood", "0 2 9", "wall main", "1x1" },
        { "fence_wood_corner", "3", "wall", "1x1" },
        { "crate", "22", "ground", "2x1" }, /* the overlay's shape row wins over models= */
        { "banner", "4 5 6 7 8", "decor", "1x1" },
        { "rug", "22", "ground", "1x1" },
        { "roof_a", "12 21", "main", "1x1" },
        { "combo", "10 11 0", "main wall", "1x1" }, /* offered centrepiece first */
        { "odd11", NULL, NULL, "1x1" },
        { "shell", NULL, NULL, "1x1" },
        { "stub", NULL, NULL, "1x1" }, /* a shape row with no model */
    };
    struct answer a;

    for( size_t r = 0; r < sizeof(rows) / sizeof(rows[0]); r++ )
    {
        ask(catalog, TORIRSMAPED_CATALOG_LOC, rows[r].name, TORIRSMAPED_CATALOG_EXACT, 16, &a);
        if( rows[r].shapes )
            CHECK(a.rows == 1 && a.has_shapes && strcmp(a.shapes, rows[r].shapes) == 0 && a.has_layer
                      && strcmp(a.layer, rows[r].layer) == 0 && strcmp(a.size, rows[r].size) == 0,
                "%s: shapes '%s' layer '%s' size '%s' (want '%s' '%s' '%s')", rows[r].name, a.shapes, a.layer,
                a.size, rows[r].shapes, rows[r].layer, rows[r].size);
        else
            CHECK(a.rows == 1 && !a.has_shapes && !a.has_layer && strcmp(a.size, rows[r].size) == 0,
                "%s: no model rows, no shapes or layer; size '%s'", rows[r].name, a.size);
    }
    ask(catalog, TORIRSMAPED_CATALOG_NPC, "hans", TORIRSMAPED_CATALOG_EXACT, 16, &a);
    CHECK(a.rows == 1 && !a.has_shapes && a.size[0] == '\0', "an npc row carries no loc fields");
    {
        int record;
        char value[64];
        CHECK(ToriRSMapEdCatalog_Query(catalog, TORIRSMAPED_CATALOG_LOC, "combo", TORIRSMAPED_CATALOG_EXACT,
                  &record, 1)
                      == 1
                  && ToriRSMapEdCatalog_Field(catalog, record, "shapes", value, sizeof(value))
                  && strcmp(value, "10 11 0") == 0,
            "the in-process field lookup sees the derived fields too (%s)", value);
    }
}

static void
test_wire(struct ToriRSMapEdCatalog* catalog)
{
    struct answer a;
    struct ToriRSMapEdBuf body;
    struct ToriRSMapEdBuf reply;

    ask(catalog, TORIRSMAPED_CATALOG_LOC, "fence", 0, 16, &a);
    CHECK(a.exact_end && a.total == 2, "without PROGRESS the payload ends at the last row (v2)");
    ask(catalog, TORIRSMAPED_CATALOG_LOC, "fence", TORIRSMAPED_CATALOG_PROGRESS, 16, &a);
    CHECK(a.exact_end && a.tail && a.page_flags == 0 && a.done == 1 && a.of == 1,
        "with PROGRESS it ends with flags 0 and 1/1 once built (%u, %u/%u)", a.page_flags, a.done, a.of);

    /* A scoped "all": two a page, locs first. */
    ask(catalog, TORIRSMAPED_CATALOG_ANY, "", TORIRSMAPED_CATALOG_PROGRESS, 2, &a);
    CHECK(a.total == 2 && (a.page_flags & TORIRSMAPED_CATALOG_PAGE_SCOPED) && strcmp(a.names[0], "fence_wood") == 0,
        "all '' two a page: the first two locs, flagged cut (%u, flags %u)", a.total, a.page_flags);
    ask(catalog, TORIRSMAPED_CATALOG_ANY, "op=Open", TORIRSMAPED_CATALOG_PROGRESS, 16, &a);
    CHECK(a.total == 4 && !(a.page_flags & TORIRSMAPED_CATALOG_PAGE_SCOPED) && has_row(&a, "open_guard"),
        "all 'op=Open' with room on the page: every kind tested, nothing cut (%u, flags %u)", a.total,
        a.page_flags);
    ask(catalog, TORIRSMAPED_CATALOG_ANY, "1", TORIRSMAPED_CATALOG_PROGRESS, 1, &a);
    CHECK(a.rows == 1 && strcmp(a.names[0], "fence_wood_corner") == 0,
        "all '1' one a page: id 1 is first by its id, before any name holding a 1 (%s)", a.names[0]);

    /* An unknown kind is refused; with PROGRESS the tail still follows. */
    memset(&body, 0, sizeof(body));
    memset(&reply, 0, sizeof(reply));
    ToriRSMapEd_PutU32(&body, 9999);
    ToriRSMapEd_PutString(&body, "");
    ToriRSMapEd_PutU32(&body, TORIRSMAPED_CATALOG_PROGRESS);
    ToriRSMapEd_PutU32(&body, 0);
    ToriRSMapEd_PutU32(&body, 0);
    (void)ToriRSMapEdCatalog_HandleQuery(
        catalog, ToriRSMapEd_BufPeek(&body), (uint32_t)ToriRSMapEd_BufAvailable(&body), &reply);
    CHECK(ToriRSMapEd_BufAvailable(&reply) == 8 * 4, "a refused PROGRESS query is the v2 refusal plus the tail (%d)",
        (int)ToriRSMapEd_BufAvailable(&reply));
    ToriRSMapEd_BufFree(&body);
    ToriRSMapEd_BufFree(&reply);
}

int
main(
    int argc,
    char** argv)
{
    char root[2048];
    struct ToriRSMapEdCatalog* catalog;
    uint32_t done;
    uint32_t of;

    if( argc < 2 )
    {
        fprintf(stderr, "usage: catalog_finder_test <scratch-dir>\n");
        return 2;
    }
    snprintf(root, sizeof(root), "%s/content", argv[1]);
    write_tree(root);
    catalog = ToriRSMapEdCatalog_Create(root);
    CHECK(ToriRSMapEdCatalog_Progress(catalog, &done, &of) && done == 0 && of == 0,
        "before anything ran the build is partial with no known work (%u/%u)", done, of);
    while( ToriRSMapEdCatalog_Service(catalog, TORIRSMAPED_CATALOG_WARM_SLICE_MS) )
        ;
    CHECK(ToriRSMapEdCatalog_Built(catalog), "the tiny tree warm-builds");
    ToriRSMapEdCatalog_SetRefreshInterval(catalog, 3600);

    test_value_rules(catalog);
    test_narrowing(catalog);
    test_shape_classes(catalog);
    test_loc_fields(catalog);
    test_wire(catalog);

    ToriRSMapEdCatalog_Free(catalog);
    printf("catalog_finder_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
