/*
 * The shop model (editor/editor_shop.h), content-free:
 *
 *   ./editor_shop_test <shop_samples.tsv> <oracle-dir>
 *
 *   oracle    every shop of the samples file emitted by editor_shop.c is,
 *             byte for byte, what tools/gen_shop_scripts.py's own renderers
 *             wrote for the same rows (editor_shop_oracle.py, run first by
 *             the make target into <oracle-dir>): a general store with two
 *             owners (SHOPS_PLAN 4.3), a members' specialty shop whose title
 *             holds '"' and two of whose owners are patched stubs, a
 *             quest-gated shop with no owner, a shop with no `special`
 *   parse     the generator's pair parses back (origin GENERATOR, Save
 *             refused with the reason) and re-emits identically; the
 *             builder's own pair (EDITOR_SHOP_MARK, the authored line)
 *             round-trips with origin EDITOR; damaged text is refused with
 *             its line
 *   prices    calc_shop_value by hand: a pot (cost 1) at Lumbridge sells
 *             for 1 and buys for 0 (SHOPS_PLAN 6's unit), bread (cost 12)
 *             at 1300 / 400 sells for 15 and buys for 4, the floor of 1,
 *             the clamp and the 100 per mille floor off baseline
 *   check     the model's own refusals, one sentence each
 *   wiki      wiki/shop_stock.csv rows of one shop: quoted fields, review
 *             and unresolved rows skipped and counted, other shops ignored
 */

#include "editor/test/editor_test_text.h"
#include "editor/editor_shop.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failed++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

enum
{
    SAMPLES_MAX = 8,
    TEXT_MAX = 16384,
};

static struct Editor_Shop g_samples[SAMPLES_MAX];
static int g_sample_count;

static char*
read_file(
    const char* path,
    size_t* out_length)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* text;

    *out_length = 0;
    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = malloc((size_t)size + 1);
    assert(text);
    if( fread(text, 1, (size_t)size, f) != (size_t)size )
        size = 0;
    text[size] = '\0';
    fclose(f);
    *out_length = (size_t)size;
    editor_test_text_lf(text, out_length);
    return text;
}

/** Split `line` at tabs into `cells` (in place). */
static int
split_tabs(
    char* line,
    char** cells,
    int max)
{
    int count = 0;
    char* at = line;
    while( count < max )
    {
        char* tab = strchr(at, '\t');
        cells[count++] = at;
        if( !tab )
            break;
        *tab = '\0';
        at = tab + 1;
    }
    return count;
}

static void
load_samples(const char* path)
{
    size_t length;
    char* text = read_file(path, &length);
    char* line;
    char* save = NULL;

    if( !text )
    {
        printf("FAIL: cannot read %s\n", path);
        exit(1);
    }
    for( line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save) )
    {
        char* cells[16];
        int n;
        if( line[0] == '#' || !line[0] )
            continue;
        n = split_tabs(line, cells, 16);
        if( strcmp(cells[0], "shop") == 0 && n >= 12 )
        {
            struct Editor_Shop* shop = &g_samples[g_sample_count++];
            assert(g_sample_count <= SAMPLES_MAX);
            Editor_ShopInit(shop, cells[1]);
            snprintf(shop->name, sizeof(shop->name), "%s", cells[2]);
            snprintf(shop->page_title, sizeof(shop->page_title), "%s", cells[3]);
            snprintf(shop->revid, sizeof(shop->revid), "%s", cells[4]);
            snprintf(shop->fetch_date, sizeof(shop->fetch_date), "%s", cells[5]);
            snprintf(shop->special, sizeof(shop->special), "%s", cells[6]);
            shop->members = cells[7][0] == 'Y' || cells[7][0] == 'y';
            shop->buy = atoi(cells[8]);
            shop->sell = atoi(cells[9]);
            shop->delta = atoi(cells[10]);
            snprintf(shop->inv, sizeof(shop->inv), "%s", cells[11]);
            /* gen_shop_scripts.py always binds op3. */
            shop->op = 3;
            shop->origin = EDITOR_SHOP_ORIGIN_GENERATOR;
        }
        else if( strcmp(cells[0], "owner") == 0 && n >= 3 )
        {
            struct Editor_Shop* shop = &g_samples[g_sample_count - 1];
            CHECK(Editor_ShopOwnerAdd(shop, cells[1]), "samples: owner %s added", cells[1]);
            shop->owners[shop->owner_count - 1].declared = atoi(cells[2]);
        }
        else if( strcmp(cells[0], "stock") == 0 && n >= 4 )
        {
            struct Editor_Shop* shop = &g_samples[g_sample_count - 1];
            CHECK(Editor_ShopStockAdd(shop, cells[1], atoi(cells[2]), atoi(cells[3])) >= 0, "samples: stock %s",
                cells[1]);
        }
        else
        {
            printf("FAIL: %s: a row this test does not read: %s\n", path, cells[0]);
            exit(1);
        }
    }
    free(text);
}

static void
first_difference(
    const char* label,
    const char* a,
    size_t a_length,
    const char* b,
    size_t b_length)
{
    size_t i = 0;
    size_t line_start = 0;
    int line = 1;
    while( i < a_length && i < b_length && a[i] == b[i] )
    {
        if( a[i] == '\n' )
        {
            line++;
            line_start = i + 1;
        }
        i++;
    }
    printf("  %s: first difference at byte %zu (line %d)\n    c:      %.80s\n    python: %.80s\n", label, i, line,
        a + line_start, b + line_start);
}

/* ---- oracle ------------------------------------------------------------------- */

static void
test_oracle(const char* oracle_dir)
{
    static char text[TEXT_MAX];

    CHECK(g_sample_count == 4, "four sample shops (read %d)", g_sample_count);
    for( int s = 0; s < g_sample_count; s++ )
    {
        const struct Editor_Shop* shop = &g_samples[s];
        for( int which = 0; which < 2; which++ )
        {
            char path[2048];
            size_t want_length;
            char* want;
            size_t length;

            snprintf(path, sizeof(path), "%s/%s.%s", oracle_dir, shop->key, which ? "inv" : "rs2");
            want = read_file(path, &want_length);
            CHECK(want != NULL, "oracle: gen_shop_scripts.py wrote %s", path);
            if( !want )
                continue;
            length = which ? Editor_ShopEmitInv(shop, EDITOR_SHOP_GENERATOR_MARK, text, sizeof(text))
                           : Editor_ShopEmitRs2(shop, EDITOR_SHOP_GENERATOR_MARK, text, sizeof(text));
            CHECK(length < sizeof(text), "oracle: %s fits", path);
            CHECK(length == want_length && memcmp(text, want, length) == 0,
                "oracle: %s.%s is gen_shop_scripts.py's bytes (%zu vs %zu)", shop->key, which ? "inv" : "rs2",
                length, want_length);
            if( length != want_length || memcmp(text, want, length) != 0 )
                first_difference(path, text, length, want, want_length);
            /* snprintf's sizing: a NULL count is the same length. */
            CHECK((which ? Editor_ShopEmitInv(shop, EDITOR_SHOP_GENERATOR_MARK, NULL, 0)
                         : Editor_ShopEmitRs2(shop, EDITOR_SHOP_GENERATOR_MARK, NULL, 0))
                      == length,
                "oracle: the count call agrees for %s", path);
            free(want);
        }
    }
}

/* ---- parse --------------------------------------------------------------------- */

static void
test_parse(const char* oracle_dir)
{
    static char rs2[TEXT_MAX];
    static char inv[TEXT_MAX];
    static char again[TEXT_MAX];
    static struct Editor_Shop parsed;
    char why[256];

    for( int s = 0; s < g_sample_count; s++ )
    {
        char path[2048];
        size_t rs2_length;
        size_t inv_length;
        char* python_rs2;
        char* python_inv;
        size_t length;

        snprintf(path, sizeof(path), "%s/%s.rs2", oracle_dir, g_samples[s].key);
        python_rs2 = read_file(path, &rs2_length);
        snprintf(path, sizeof(path), "%s/%s.inv", oracle_dir, g_samples[s].key);
        python_inv = read_file(path, &inv_length);
        if( !python_rs2 || !python_inv )
        {
            CHECK(0, "parse: the oracle's pair for %s", g_samples[s].key);
            free(python_rs2);
            free(python_inv);
            continue;
        }
        CHECK(Editor_ShopParse(&parsed, python_rs2, rs2_length, python_inv, inv_length, why, sizeof(why)),
            "parse: gen_shop_scripts.py's %s reads back (%s)", g_samples[s].key, why);
        CHECK(parsed.origin == EDITOR_SHOP_ORIGIN_GENERATOR, "parse: %s is the generator's", g_samples[s].key);
        CHECK(Editor_ShopSaveRefusal(&parsed) != NULL, "parse: Save refuses the generator's %s", g_samples[s].key);
        CHECK(strcmp(parsed.key, g_samples[s].key) == 0 && parsed.stock_count == g_samples[s].stock_count
                  && parsed.owner_count == g_samples[s].owner_count && parsed.sell == g_samples[s].sell
                  && parsed.buy == g_samples[s].buy && parsed.delta == g_samples[s].delta
                  && parsed.members == g_samples[s].members && strcmp(parsed.inv, g_samples[s].inv) == 0
                  && strcmp(parsed.name, g_samples[s].name) == 0,
            "parse: %s's fields come back", g_samples[s].key);
        length = Editor_ShopEmitRs2(&parsed, EDITOR_SHOP_GENERATOR_MARK, again, sizeof(again));
        CHECK(length == rs2_length && memcmp(again, python_rs2, length) == 0, "parse: %s.rs2 re-emits identically",
            g_samples[s].key);
        length = Editor_ShopEmitInv(&parsed, EDITOR_SHOP_GENERATOR_MARK, again, sizeof(again));
        CHECK(length == inv_length && memcmp(again, python_inv, length) == 0, "parse: %s.inv re-emits identically",
            g_samples[s].key);
        free(python_rs2);
        free(python_inv);
    }

    /* The builder's own pair: authored here, op 1, every owner declared. */
    {
        struct Editor_Shop shop;
        size_t rs2_length;
        size_t inv_length;

        Editor_ShopInit(&shop, "lumbridge_general");
        snprintf(shop.name, sizeof(shop.name), "%s", "Lumbridge \"General\"");
        snprintf(shop.special, sizeof(shop.special), "%s", "General store");
        Editor_ShopOwnerAdd(&shop, "shop_keeper_lumbridge");
        shop.op = 1;
        Editor_ShopStockAdd(&shop, "pot", 5, 100);
        Editor_ShopStockAdd(&shop, "bread", 10, 50);
        rs2_length = Editor_ShopEmitRs2(&shop, EDITOR_SHOP_MARK, rs2, sizeof(rs2));
        inv_length = Editor_ShopEmitInv(&shop, EDITOR_SHOP_MARK, inv, sizeof(inv));
        CHECK(strstr(rs2, EDITOR_SHOP_AUTHORED_LINE "\n") != NULL, "parse: an authored shop cites no wiki page");
        CHECK(strstr(rs2, "[opnpc1,shop_keeper_lumbridge] @lumbridge_general_open;\n") != NULL,
            "parse: the op chosen is the trigger's");
        CHECK(strstr(rs2, "~openshop(lumbridge_general, 400, 1300, 30, \"Lumbridge 'General'\");\n") != NULL,
            "parse: ~openshop takes inv, buy, sell, delta and the title with '\"' as '\\''");
        CHECK(strstr(inv, "[lumbridge_general]\nscope=shared\nrestock=yes\nstackall=yes\nallstock=yes\n"
                          "stock1=pot,5,100\nstock2=bread,10,50\n")
                  != NULL,
            "parse: the .inv states the stock in order");
        CHECK(Editor_ShopParse(&parsed, rs2, rs2_length, inv, inv_length, why, sizeof(why)),
            "parse: the builder's pair reads back (%s)", why);
        CHECK(parsed.origin == EDITOR_SHOP_ORIGIN_EDITOR && Editor_ShopSaveRefusal(&parsed) == NULL,
            "parse: the builder may save over its own pair");
        CHECK(parsed.op == 1 && parsed.stock_count == 2 && parsed.stock[1].count == 10 && parsed.stock[1].rate == 50
                  && Editor_ShopIsGeneral(&parsed) && !parsed.page_title[0],
            "parse: op, stock and special come back");
        CHECK(Editor_ShopEmitRs2(&parsed, EDITOR_SHOP_MARK, again, sizeof(again)) == rs2_length
                  && memcmp(again, rs2, rs2_length) == 0,
            "parse: and re-emit byte for byte");

        /* Damaged text is refused with the line. */
        {
            char damaged[TEXT_MAX];
            char* at;
            memcpy(damaged, rs2, rs2_length + 1);
            at = strstr(damaged, "[opnpc1,");
            assert(at);
            at[6] = '9';
            CHECK(!Editor_ShopParse(&parsed, damaged, rs2_length, inv, inv_length, why, sizeof(why))
                      && strstr(why, "line 7") != NULL,
                "parse: op 9 is refused at line 7 (%s)", why);
            memcpy(damaged, inv, inv_length + 1);
            at = strstr(damaged, "stock2=");
            assert(at);
            at[5] = '3';
            CHECK(!Editor_ShopParse(&parsed, rs2, rs2_length, damaged, inv_length, why, sizeof(why))
                      && strstr(why, "stock3 where stock2") != NULL,
                "parse: a skipped stock number is refused (%s)", why);
            CHECK(!Editor_ShopParse(&parsed, rs2, rs2_length, inv, inv_length - 1, why, sizeof(why)),
                "parse: an .inv without its final newline is not the emitter's");
            snprintf(damaged, sizeof(damaged), "%s", "// hand-written\n[opnpc3,x] @y;\n");
            CHECK(!Editor_ShopParse(&parsed, damaged, strlen(damaged), inv, inv_length, why, sizeof(why))
                      && strstr(why, "line 1") != NULL,
                "parse: a file with neither mark is refused at line 1 (%s)", why);
        }
    }
}

/* ---- prices ------------------------------------------------------------------- */

static void
test_prices(void)
{
    struct Editor_Shop shop;

    Editor_ShopInit(&shop, "lumbridge_general");
    Editor_ShopStockAdd(&shop, "pot", 5, 100);
    Editor_ShopStockAdd(&shop, "bread", 10, 50);
    Editor_ShopStockAdd(&shop, "feather", 1000, 10);

    /* Case 1, SHOPS_PLAN section 6: a pot (oc_cost 1) at the Lumbridge
     * multipliers. 1300 * 1 / 1000 = 1.3 -> 1; 400 * 1 / 1000 = 0.4 -> 0. */
    CHECK(Editor_ShopSellsAt(&shop, 0, 1) == 1, "prices: a pot sells for 1 gp (got %d)", Editor_ShopSellsAt(&shop, 0, 1));
    CHECK(Editor_ShopBuysAt(&shop, 0, 1) == 0, "prices: a pot buys for 0 gp (got %d)", Editor_ShopBuysAt(&shop, 0, 1));
    /* Case 2: bread (oc_cost 12). 1300 * 12 / 1000 = 15.6 -> 15;
     * 400 * 12 / 1000 = 4.8 -> 4. Truncated, as scale() truncates. */
    CHECK(Editor_ShopSellsAt(&shop, 1, 12) == 15, "prices: bread sells for 15 gp (got %d)",
        Editor_ShopSellsAt(&shop, 1, 12));
    CHECK(Editor_ShopBuysAt(&shop, 1, 12) == 4, "prices: bread buys for 4 gp (got %d)", Editor_ShopBuysAt(&shop, 1, 12));
    /* The floor of 1 is on the selling side only. */
    CHECK(Editor_ShopSellsAt(&shop, 2, 0) == 1 && Editor_ShopBuysAt(&shop, 2, 0) == 0,
        "prices: a free obj still sells for 1 and buys for 0");
    /* Off baseline: 5 over base at delta 30 is 150 per mille off. */
    CHECK(Editor_ShopValue(1300, 30, 5, 0, 100) == 115, "prices: 1150 per mille of 100 is 115 (got %d)",
        Editor_ShopValue(1300, 30, 5, 0, 100));
    /* The clamp: at most 1000 off, at most 5000 on, never under 100. */
    CHECK(Editor_ShopValue(1300, 30, 100, 0, 1000) == 300, "prices: the move is clamped at 1000 per mille");
    CHECK(Editor_ShopValue(1300, 30, 0, 1000, 1000) == 6300, "prices: and at -5000 per mille");
    CHECK(Editor_ShopValue(400, 30, 100, 0, 1000) == 100, "prices: and the rate never under 100 per mille");
}

/* ---- check ---------------------------------------------------------------------- */

static int
issues_name(
    char (*issues)[EDITOR_SHOP_TEXT_MAX],
    int count,
    const char* fragment)
{
    for( int i = 0; i < count; i++ )
    {
        if( strstr(issues[i], fragment) )
            return 1;
    }
    return 0;
}

static void
test_check(void)
{
    struct Editor_Shop shop;
    char issues[EDITOR_SHOP_ISSUES_MAX][EDITOR_SHOP_TEXT_MAX];
    int count;

    Editor_ShopInit(&shop, "new_shop");
    count = Editor_ShopCheck(&shop, issues, EDITOR_SHOP_ISSUES_MAX);
    CHECK(count == 2 && issues_name(issues, count, "No owner") && issues_name(issues, count, "No stock"),
        "check: a new shop wants an owner and stock (%d issues)", count);

    Editor_ShopOwnerAdd(&shop, "shop_keeper");
    Editor_ShopStockAdd(&shop, "pot", 5, 100);
    CHECK(Editor_ShopCheck(&shop, issues, EDITOR_SHOP_ISSUES_MAX) == 0, "check: then it may be saved");

    Editor_ShopStockAdd(&shop, "pot", 0, 0);
    shop.op = 6;
    shop.buy = 2000;
    count = Editor_ShopCheck(&shop, issues, EDITOR_SHOP_ISSUES_MAX);
    CHECK(issues_name(issues, count, "op6") && issues_name(issues, count, "count 0")
              && issues_name(issues, count, "rate 0") && issues_name(issues, count, "repeats pot")
              && issues_name(issues, count, "sell back for profit"),
        "check: op, count, rate, the repeat and buy over sell each say so (%d issues)", count);
    Editor_ShopStockRemove(&shop, 1);
    CHECK(shop.stock_count == 1 && strcmp(shop.stock[0].obj, "pot") == 0, "check: a row is removed");
    CHECK(!Editor_ShopKeyValid("Bad key") && !Editor_ShopKeyValid("") && Editor_ShopKeyValid("ok_1"),
        "check: keys are [a-z0-9_]");
    CHECK(!Editor_ShopOwnerAdd(&shop, "shop_keeper"), "check: an owner is listed once");
    {
        char rs2[256];
        char inv[256];
        Editor_ShopDocKeys("lumbridge_general", rs2, sizeof(rs2), inv, sizeof(inv));
        CHECK(strcmp(rs2, "rs2:shop/edited/lumbridge_general/scripts/lumbridge_general.rs2") == 0
                  && strcmp(inv, "rs2:shop/edited/lumbridge_general/configs/lumbridge_general.inv") == 0,
            "check: the documents live in the builder's own directory (%s, %s)", rs2, inv);
    }
}

/* ---- wiki ------------------------------------------------------------------------ */

static void
test_wiki(void)
{
    static const char k_csv[] = "shop_key,item_name,obj_gameval,stock,restock,match_rule,review\r\n"
                                "lumbridge_general_store,Pot,pot_empty,5,10,exact,\r\n"
                                "lumbridge_general_store,\"Skirt (blue, long)\",dwarf_skirt1,2,100,strip-qualifier,variant\r\n"
                                "lumbridge_general_store,Barbarian Assault#Upgrades,,1,0,,\r\n"
                                "varrock_general_store,Pot,pot_empty,3,10,exact,\r\n"
                                "lumbridge_general_store,\"Shears, \"\"good\"\"\",shears,2,100,exact,\r\n";
    struct Editor_ShopStock rows[8];
    int skipped = -1;
    int const count = Editor_ShopWikiStock(k_csv, sizeof(k_csv) - 1, "lumbridge_general_store", rows, 8, &skipped);

    CHECK(count == 2, "wiki: two resolved, reviewed rows (got %d)", count);
    CHECK(skipped == 2, "wiki: the variant and the unresolved line are skipped and counted (got %d)", skipped);
    CHECK(count >= 2 && strcmp(rows[0].obj, "pot_empty") == 0 && rows[0].count == 5 && rows[0].rate == 10
              && strcmp(rows[1].obj, "shears") == 0 && rows[1].count == 2 && rows[1].rate == 100,
        "wiki: the rows in file order, quoted commas and quotes read");
    CHECK(Editor_ShopWikiStock("a,b\n1,2\n", 8, "x", rows, 8, &skipped) == -1, "wiki: a csv without the columns is -1");
}

int
main(
    int argc,
    char** argv)
{
    if( argc < 3 )
    {
        fprintf(stderr, "usage: %s <shop_samples.tsv> <oracle-dir>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    load_samples(argv[1]);
    test_oracle(argv[2]);
    test_parse(argv[2]);
    test_prices();
    test_check();
    test_wiki();
    printf("editor_shop_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
