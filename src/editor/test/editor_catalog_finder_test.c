/*
 * The Catalog as a loc finder (workbench 6.2, design section 6), content-free:
 *
 *   ./editor_catalog_finder_test <tmp-content-dir>
 *
 * Part 1, the model, fed synthetic rows (Editor_CatalogModelFeed: the model
 * is pure): typed key=value words taken into chips and the chips composed
 * into the filter text; a loc row's placeable shapes read from its summary
 * fields and the keycap a click lands on mapped to its shape; the cells text;
 * the continuous window (the anchor, the prefetch a quarter page early, the
 * page cache, the count); the sort over a whole result and its refusal over
 * a big one; a local list filtered by Find and the shape chips; the recent
 * picks; the rule for what the Preview follows.
 *
 * Part 2, the FAVOURITES codec (editor_favourites.h): parse -> emit byte for
 * byte, the grammar's refusals, the splices that add, re-pose and remove a
 * favourite, applied and inverted.
 *
 * Part 3, the registered Catalog and Preview over an embedded ToriRSMapEd on
 * a synthetic tree written under the directory given (150 locs whose
 * configs/all.loc states shape lines, an npc, an obj; no OSRS-Content): the
 * cells rows and their thumbnails (asked for the rows on screen only), a
 * scroll that lands on the prefetched page, chips typed and removed, a shape
 * keycap click, the favourite star through the authority and its undo, the
 * favourites shortcut, recents, sort by size, Pick from world, and the
 * Preview following the selection after a pick.
 */

#include "editor/editor.h"
#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_favourites.h"
#include "editor/editor_host.h"
#include "editor/editor_ui.h"
#include "editor/panels/edpanel_catalog_model.h"
#include "editor/panels/edpanel_chips.h"
#include "editor/panels/edpanel_ops.h"
#include "editor/panels/edpanels.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"
#include "torirsmaped/torirs_maped_catalog.h"
#include "ui/uitree_debug_overlay.h"

#include <rscache.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

/* ==== part 1: the model ========================================================= */

static void
test_chips(void)
{
    struct Editor_CatalogChips chips;
    char rest[192];
    char composed[192];
    char label[64];
    uint32_t mask;

    Editor_CatalogChipsInit(&chips);
    CHECK(Editor_CatalogChipsTake(&chips, "fence shape=wall  op=Open dark", rest, sizeof(rest)) == 2
              && chips.count == 2 && strcmp(rest, "fence dark") == 0,
        "two key=value words become chips, the rest stays text (%d, '%s')", chips.count, rest);
    CHECK(strcmp(chips.chip[0].key, "shape") == 0 && strcmp(chips.chip[0].value, "wall") == 0
              && strcmp(chips.chip[1].key, "op") == 0 && strcmp(chips.chip[1].value, "Open") == 0,
        "in their order, key and value");
    Editor_CatalogChipsCompose(&chips, rest, composed, sizeof(composed));
    CHECK(strcmp(composed, "fence dark shape=wall op=Open") == 0, "the free words then the chips: '%s'", composed);
    CHECK(Editor_CatalogChipsTake(&chips, "op=Open", rest, sizeof(rest)) == 0 && chips.count == 2 && rest[0] == '\0',
        "a chip already there is not added twice");
    CHECK(Editor_CatalogChipsTake(&chips, "Bad=key =x y= a=b=c", rest, sizeof(rest)) == 0
              && strcmp(rest, "Bad=key =x y= a=b=c") == 0,
        "words the grammar refuses stay text ('%s')", rest);
    Editor_CatalogChipsRemove(&chips, 0);
    CHECK(chips.count == 1 && strcmp(chips.chip[0].key, "op") == 0, "x removes a chip");
    CHECK(Editor_CatalogChipsAdd(&chips, "shape", "2"), "a shape number is a chip");
    Editor_CatalogChipsCompose(&chips, "", composed, sizeof(composed));
    CHECK(strcmp(composed, "op=Open shape=2") == 0,
        "a shape number composes as it reads: the server resolves shape= against the placeable shapes: '%s'",
        composed);
    Editor_CatalogChipLabel(&chips.chip[1], label, sizeof(label));
    CHECK(strcmp(label, "shape: 2") == 0, "its label (%s)", label);
    Editor_CatalogChipsInit(&chips);
    (void)Editor_CatalogChipsAdd(&chips, "shape", "wall");
    Editor_CatalogChipTip(&chips.chip[0], label, sizeof(label));
    CHECK(strstr(label, "0 1 2 3 9") != NULL, "a shape class chip's tooltip lists its numbers (%s)", label);
    (void)Editor_CatalogChipsAdd(&chips, "multivar", "*");
    Editor_CatalogChipLabel(&chips.chip[1], label, sizeof(label));
    CHECK(strcmp(label, "multivar: any") == 0, "multivar=* reads 'any' (%s)", label);
    for( int i = 0; i < 10; i++ )
    {
        char value[8];
        snprintf(value, sizeof(value), "v%d", i);
        (void)Editor_CatalogChipsAdd(&chips, "op", value);
    }
    CHECK(chips.count == EDITOR_CATALOG_CHIPS_MAX, "chips stop at %d", EDITOR_CATALOG_CHIPS_MAX);
    CHECK(Editor_CatalogChipsTake(&chips, "op=more ", rest, sizeof(rest)) == 0 && strcmp(rest, "op=more") == 0,
        "past the last chip a key=value stays text, still a filter word");

    CHECK(Editor_CatalogShapeClass("wall", &mask) && mask == ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 9)),
        "wall is 0 1 2 3 9");
    CHECK(Editor_CatalogShapeClass("decor", &mask) && mask == 0x1F0u, "decor is 4 to 8");
    CHECK(Editor_CatalogShapeClass("centre", &mask) && mask == 0xC00u, "centre is 10 and 11");
    CHECK(Editor_CatalogShapeClass("roof", &mask) && mask == 0x3FF000u, "roof is 12 to 21");
    CHECK(Editor_CatalogShapeClass("ground", &mask) && mask == (1u << 22), "ground is 22");
    CHECK(Editor_CatalogShapeClass("22", &mask) && mask == (1u << 22) && !Editor_CatalogShapeClass("23", &mask)
              && !Editor_CatalogShapeClass("door", &mask),
        "a number names itself; 23 and words that are no class do not");
    {
        char key[EDITOR_CATALOG_CHIP_KEY_MAX];
        char value[EDITOR_CATALOG_CHIP_VALUE_MAX];
        int whole = 0;
        int typed = 0;
        for( int i = 0; i < Editor_CatalogFilterMenuCount(); i++ )
        {
            int const kind = Editor_CatalogFilterMenuItem(i, key, value);
            whole += kind == 1;
            typed += kind == 2;
            if( kind == 1 && strcmp(key, "dialogue") == 0 )
                CHECK(strcmp(value, "yes") == 0, "has dialogue is dialogue=yes");
        }
        CHECK(Editor_CatalogFilterMenuItem(0, key, value) == 0 && whole >= 10 && typed == 2,
            "+ filter: the title, whole chips (shape classes, size, multivar, dialogue, spawn) and two keys to type");
    }
}

/* Synthetic rows, as CATALOG_QUERY returns them. */
static const struct EditorHost_CatalogField k_fence_fields[] = {
    { "shape1", "0,100" }, { "shape2", "2,101" }, { "shape3", "9,102" }, { "category", "fence" }, { "width", "1" },
};
static const struct EditorHost_CatalogField k_tree_fields[] = {
    { "models", "200" }, { "width", "2" }, { "length", "3" }, { "category", "tree" },
};
static const struct EditorHost_CatalogField k_torch_fields[] = {
    { "shape1", "4,300" },
};
static const struct EditorHost_CatalogField k_many_fields[] = {
    { "shape1", "0,1" }, { "shape2", "1,2" }, { "shape3", "2,3" }, { "shape4", "3,4" }, { "shape5", "9,5" },
    { "shape6", "4,6" }, { "shape7", "10,7" },
};

static struct EditorHost_CatalogRow
row_of(
    uint32_t kind,
    int id,
    const char* name,
    const char* display,
    const struct EditorHost_CatalogField* fields,
    int field_count)
{
    struct EditorHost_CatalogRow row;
    memset(&row, 0, sizeof(row));
    row.kind = kind;
    row.id = id;
    row.name = name;
    row.display = display;
    row.fields = fields;
    row.field_count = field_count;
    return row;
}

static void
test_rows(void)
{
    struct EditorHost_CatalogRow const fence = row_of(TORIRSMAPED_CATALOG_LOC, 980, "fence_wood", "Fence", k_fence_fields, 5);
    struct EditorHost_CatalogRow const tree = row_of(TORIRSMAPED_CATALOG_LOC, 1, "oak_tree", "Oak", k_tree_fields, 4);
    struct EditorHost_CatalogRow const torch = row_of(TORIRSMAPED_CATALOG_LOC, 2, "wall_torch", "", k_torch_fields, 1);
    struct EditorHost_CatalogRow const many = row_of(TORIRSMAPED_CATALOG_LOC, 3, "many", "", k_many_fields, 7);
    struct EditorHost_CatalogRow const npc = row_of(TORIRSMAPED_CATALOG_NPC, 4, "guard", "Guard", k_fence_fields, 5);
    struct Editor_CatalogModel model;
    struct Editor_CatalogView view;
    char text[192];
    int w;
    int l;

    CHECK(Editor_CatalogRowShapeMask(&fence) == ((1u << 0) | (1u << 2) | (1u << 9)),
        "shape1..3 = 0, 2, 9 (%x)", Editor_CatalogRowShapeMask(&fence));
    CHECK(Editor_CatalogRowShapeMask(&tree) == ((1u << 10) | (1u << 11)),
        "models= and no shape line is a centrepiece: 10 and its diagonal 11");
    CHECK(Editor_CatalogRowShapeMask(&torch) == 0x1F0u, "a decor model places as 4 to 8, as Editor_LocShapeMask says");
    CHECK(Editor_CatalogRowShapeMask(&npc) == 0, "an npc has no shapes");
    Editor_CatalogRowSize(&tree, &w, &l);
    CHECK(w == 2 && l == 3, "size from width= and length=");
    Editor_CatalogRowSize(&torch, &w, &l);
    CHECK(w == 1 && l == 1, "1x1 when they are absent");

    Editor_CatalogModelInit(&model, TORIRSMAPED_CATALOG_LOC, 10);
    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "");
    {
        struct EditorHost_CatalogRow const rows[4] = { fence, tree, torch, many };
        Editor_CatalogModelFeed(&model, 0, 4, rows, 4);
    }
    CHECK(Editor_CatalogModelViewCount(&model) == 4 && Editor_CatalogModelViewAt(&model, 0, &view)
              && strcmp(view.name, "fence_wood") == 0 && strcmp(view.category, "fence") == 0,
        "a fed page is the list");
    Editor_CatalogCellsText(&view, 77, 0, text, sizeof(text));
    CHECK(strcmp(text, "77\x1f" "fence_wood\x1f" "Fence\x1f" "0 2 9\x1f" "980") == 0,
        "the cells: picture, name, display name, the shapes as keycaps, the id");
    CHECK(Editor_CatalogViewShapeAt(&view, 2) == 2 && Editor_CatalogViewShapeAt(&view, 3) == 9
              && Editor_CatalogViewShapeAt(&view, 0) == -1 && Editor_CatalogViewShapeAt(&view, 4) == -1,
        "keycap 2 is shape 2, keycap 3 shape 9, the body and past them none");
    Editor_CatalogCellsText(&view, 0, 1, text, sizeof(text));
    CHECK(strncmp(text, "\x1f" "loc fence_wood\x1f", 16) == 0, "no picture yet: an empty cell; all kinds names it");
    (void)Editor_CatalogModelViewAt(&model, 1, &view);
    Editor_CatalogCellsText(&view, 0, 0, text, sizeof(text));
    CHECK(strstr(text, "\x1f" "10 11\x1f") != NULL, "a centrepiece's keys are 10 11 (%s)", text);
    (void)Editor_CatalogModelViewAt(&model, 3, &view);
    Editor_CatalogCellsText(&view, 0, 0, text, sizeof(text));
    CHECK(strstr(text, "\x1f" "10 11 0 1 2 3 9 +5\x1f") != NULL,
        "past eight shapes the last cap says how many more (%s)", text);
    CHECK(Editor_CatalogViewShapeAt(&view, 7) == 9 && Editor_CatalogViewShapeAt(&view, 8) == -1,
        "and that cap is no shape");
    Editor_CatalogModelFree(&model);
}

/* The server's derived loc fields and the PROGRESS tail (workbench 6.2, the
 * two halves joined): `shapes` wins over the shapeK= rows, `size` over
 * width= / length=, `layer` reaches the view; a partial answer says so in
 * the count, a scoped total reads "N+". */
static void
test_server_fields(void)
{
    static const struct EditorHost_CatalogField k_derived[] = {
        { "shape1", "0,fence_a" },  { "shapes", "0 2 9" }, { "size", "2x3" },
        { "layer", "wall main" },   { "width", "5" },
    };
    static const struct EditorHost_CatalogField k_none[] = { { "shapes", "" }, { "shape1", "10,x" } };
    struct EditorHost_CatalogRow const derived = row_of(TORIRSMAPED_CATALOG_LOC, 9, "fence_d", "", k_derived, 5);
    struct EditorHost_CatalogRow const none = row_of(TORIRSMAPED_CATALOG_LOC, 10, "no_model", "", k_none, 2);
    struct Editor_CatalogModel model;
    struct Editor_CatalogView view;
    char text[192];
    int w;
    int l;

    CHECK(Editor_CatalogRowShapeMask(&derived) == ((1u << 0) | (1u << 2) | (1u << 9)),
        "server fields: `shapes` is the mask (%x)", Editor_CatalogRowShapeMask(&derived));
    CHECK(Editor_CatalogRowShapeMask(&none) == 0, "server fields: an empty `shapes` is no shape, whatever shapeK says");
    Editor_CatalogRowSize(&derived, &w, &l);
    CHECK(w == 2 && l == 3, "server fields: `size` wins over width= (%dx%d)", w, l);

    Editor_CatalogModelInit(&model, TORIRSMAPED_CATALOG_LOC, 10);
    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "");
    Editor_CatalogModelFeed(&model, 0, 1, &derived, 1);
    CHECK(Editor_CatalogModelViewAt(&model, 0, &view) && strcmp(view.layer, "wall main") == 0 && view.size_w == 2
              && view.size_l == 3,
        "server fields: the view carries the layer and the size");
    CHECK(!Editor_CatalogModelPartial(&model), "server fields: a v2 answer is whole");
    model.result.page_flags = TORIRSMAPED_CATALOG_PAGE_PARTIAL | TORIRSMAPED_CATALOG_PAGE_SCOPED;
    model.result.progress_done = 512;
    model.result.progress_total = 2048;
    CHECK(Editor_CatalogModelPartial(&model), "server fields: PAGE_PARTIAL is a partial answer");
    Editor_CatalogModelRangeText(&model, 0, text, sizeof(text));
    CHECK(strstr(text, "of 1+ loc so far, index 25% read") != NULL,
        "server fields: the count says scoped and how far the build is ('%s')", text);
    model.result.page_flags = TORIRSMAPED_CATALOG_PAGE_SCOPED;
    Editor_CatalogModelRangeText(&model, 0, text, sizeof(text));
    CHECK(strcmp(text, "1 to 1 of 1+ loc") == 0, "server fields: a whole scoped answer ('%s')", text);
    Editor_CatalogModelFree(&model);
}

/** `count` rows of page `page`: names "row_<n>" so the absolute row reads
 *  back; sizes vary so a sort by size has work. */
static void
feed_page(
    struct Editor_CatalogModel* model,
    uint32_t page,
    uint32_t total,
    int count)
{
    static char names[64][16];
    static struct EditorHost_CatalogField fields[64][1];
    static char widths[64][8];
    struct EditorHost_CatalogRow rows[64];

    assert(count <= 64);
    for( int i = 0; i < count; i++ )
    {
        int const n = (int)(page * model->page_size) + i;
        snprintf(names[i], sizeof(names[i]), "row_%03d", 99 - n);
        snprintf(widths[i], sizeof(widths[i]), "%d", (n % 3) + 1);
        fields[i][0].key = "width";
        fields[i][0].value = widths[i];
        rows[i] = row_of(TORIRSMAPED_CATALOG_LOC, n, names[i], "", fields[i], 1);
    }
    Editor_CatalogModelFeed(model, page, total, rows, count);
}

static void
test_window(void)
{
    struct Editor_CatalogModel model;
    struct Editor_CatalogView view;
    char text[192];
    uint32_t key;

    Editor_CatalogModelInit(&model, TORIRSMAPED_CATALOG_LOC, 10);
    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "row");
    Editor_CatalogModelWantWindow(&model, 0, 6);
    CHECK(Editor_CatalogModelNextPage(&model) == 0, "a new question asks the window's page first");
    feed_page(&model, 0, 35, 10);
    key = model.key_serial;
    CHECK(Editor_CatalogModelViewCount(&model) == 35, "the list is the whole result (35), not a page");
    CHECK(Editor_CatalogModelNextPage(&model) == -1, "a window well inside its page prefetches nothing");
    Editor_CatalogModelRangeText(&model, 0, text, sizeof(text));
    CHECK(strcmp(text, "1 to 10 of 35 loc") == 0, "the count: %s", text);
    Editor_CatalogModelWantWindow(&model, 4, 6);
    CHECK(Editor_CatalogModelNextPage(&model) == 1, "a window within a quarter page of the end prefetches the next");
    feed_page(&model, 1, 35, 10);
    CHECK(model.key_serial == key, "a page landing is the same list (no scroll back to the top)");
    CHECK(Editor_CatalogModelViewAt(&model, 15, &view) && view.id == 15, "row 15 reads from the prefetched page");
    CHECK(!Editor_CatalogModelViewAt(&model, 25, &view), "row 25's page is not held: drawn blank");
    Editor_CatalogModelRangeText(&model, 4, text, sizeof(text));
    CHECK(strcmp(text, "5 to 20 of 35 loc") == 0, "the count runs to the last held row: %s", text);
    Editor_CatalogModelWantWindow(&model, 12, 6);
    CHECK(model.page == 1 && Editor_CatalogModelNextPage(&model) == -1,
        "a window on the prefetched page needs no round trip (the anchor swaps in)");
    Editor_CatalogModelWantWindow(&model, 28, 6);
    CHECK(Editor_CatalogModelNextPage(&model) == 2, "a window on page 2 asks it");
    feed_page(&model, 2, 35, 10);
    CHECK(Editor_CatalogModelNextPage(&model) == 3, "and the last page is a quarter page from its end");
    feed_page(&model, 3, 35, 5);
    CHECK(Editor_CatalogModelViewAt(&model, 34, &view) && view.id == 34, "the last row");
    Editor_CatalogModelWantWindow(&model, 0, 6);
    CHECK(model.page == 0 && Editor_CatalogModelNextPage(&model) == -1, "back at the top the cache answers");

    /* Sort: the whole result held, every row in the order. */
    Editor_CatalogModelSetSort(&model, EDITOR_CATALOG_SORT_NAME);
    CHECK(Editor_CatalogModelSortApplies(&model) && Editor_CatalogModelViewAt(&model, 0, &view)
              && strcmp(view.name, "row_065") == 0,
        "sort by name orders every held row (%s first)", view.name);
    Editor_CatalogModelSetSort(&model, EDITOR_CATALOG_SORT_SIZE);
    {
        int ordered = 1;
        int prev = 0;
        for( int r = 0; r < 35; r++ )
        {
            ordered &= Editor_CatalogModelViewAt(&model, r, &view) && view.size_w * view.size_l >= prev;
            prev = view.size_w * view.size_l;
        }
        CHECK(ordered, "sort by size: every footprint at least the one before");
    }
    Editor_CatalogModelSetSort(&model, EDITOR_CATALOG_SORT_ID);
    CHECK(Editor_CatalogModelViewAt(&model, 0, &view) && view.id == 0 && Editor_CatalogModelViewAt(&model, 34, &view)
              && view.id == 34,
        "sort by id");
    Editor_CatalogModelSetSort(&model, EDITOR_CATALOG_SORT_BEST);

    /* A new question is a new list. */
    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "other");
    CHECK(Editor_CatalogModelNextPage(&model) == 0, "a changed find asks its first page");
    feed_page(&model, 0, 600, 10);
    CHECK(model.key_serial != key, "and its answer is a new list (scrolled to the top)");
    Editor_CatalogModelSetSort(&model, EDITOR_CATALOG_SORT_NAME);
    Editor_CatalogModelWantWindow(&model, 0, 6);
    CHECK(!Editor_CatalogModelSortApplies(&model) && Editor_CatalogModelNextPage(&model) == -1,
        "a sort over 60 pages is not gathered: the server's order, nothing fetched for it");
    Editor_CatalogModelRangeText(&model, 0, text, sizeof(text));
    CHECK(strstr(text, "of 600 loc") && strstr(text, "best match"), "and the count says so: %s", text);
    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "few");
    feed_page(&model, 0, 25, 10);
    CHECK(!Editor_CatalogModelSortApplies(&model) && Editor_CatalogModelNextPage(&model) == 1,
        "a sort over three pages gathers them");
    feed_page(&model, 1, 25, 10);
    feed_page(&model, 2, 25, 5);
    CHECK(Editor_CatalogModelSortApplies(&model) && Editor_CatalogModelViewAt(&model, 0, &view)
              && strcmp(view.name, "row_075") == 0,
        "then sorts them (%s)", view.name);
    Editor_CatalogModelFree(&model);
}

static void
test_local_and_recents(void)
{
    struct Editor_CatalogModel model;
    struct Editor_CatalogLocalRow rows[3];
    struct Editor_CatalogView view;
    struct Editor_CatalogRecents recents;
    char text[192];

    memset(rows, 0, sizeof(rows));
    rows[0].kind = TORIRSMAPED_CATALOG_LOC;
    rows[0].id = 30;
    snprintf(rows[0].name, sizeof(rows[0].name), "oak_tree");
    snprintf(rows[0].display, sizeof(rows[0].display), "Oak");
    rows[0].shape_mask = 0xC00u;
    rows[0].shape = -1;
    rows[1] = rows[0];
    rows[1].id = 10;
    snprintf(rows[1].name, sizeof(rows[1].name), "fence_wood");
    snprintf(rows[1].display, sizeof(rows[1].display), "Fence");
    rows[1].shape_mask = (1u << 0) | (1u << 2);
    rows[1].shape = 2;
    rows[1].rotation = 3;
    rows[2] = rows[0];
    rows[2].id = 20;
    snprintf(rows[2].name, sizeof(rows[2].name), "oak_door");
    snprintf(rows[2].display, sizeof(rows[2].display), "Door");
    rows[2].shape_mask = 1u << 0;

    Editor_CatalogModelInit(&model, TORIRSMAPED_CATALOG_LOC, 10);
    Editor_CatalogModelSetLocal(&model, rows, 3);
    Editor_CatalogModelSetSource(&model, EDITOR_CATALOG_SOURCE_FAVOURITES);
    CHECK(Editor_CatalogModelViewCount(&model) == 3 && Editor_CatalogModelNextPage(&model) == -1,
        "a local list shows its rows and asks the server nothing");
    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "OAK");
    CHECK(Editor_CatalogModelViewCount(&model) == 2, "Find's words filter it at once, any case");
    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "oak shape=wall");
    CHECK(Editor_CatalogModelViewCount(&model) == 1 && Editor_CatalogModelViewAt(&model, 0, &view)
              && strcmp(view.name, "oak_door") == 0,
        "a shape chip keeps the rows with a shape in the class");
    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "door");
    CHECK(Editor_CatalogModelViewCount(&model) == 1, "a display name matches too");
    Editor_CatalogModelWant(&model, TORIRSMAPED_CATALOG_LOC, "");
    Editor_CatalogModelSetSort(&model, EDITOR_CATALOG_SORT_ID);
    CHECK(Editor_CatalogModelViewAt(&model, 0, &view) && view.id == 10 && view.shape == 2 && view.rotation == 3,
        "sorted by id; a favourite carries its pose");
    Editor_CatalogModelRangeText(&model, 0, text, sizeof(text));
    CHECK(strcmp(text, "1 to 3 of 3 favourites") == 0, "the count names the list: %s", text);
    CHECK(Editor_CatalogModelFindRow(&model, TORIRSMAPED_CATALOG_LOC, "oak_tree") == 2, "FindRow in the shown order");
    Editor_CatalogModelSetSource(&model, EDITOR_CATALOG_SOURCE_INDEX);
    CHECK(Editor_CatalogModelViewCount(&model) == 0 && Editor_CatalogModelNextPage(&model) == 0,
        "back on the index it asks");
    Editor_CatalogModelFree(&model);

    memset(&recents, 0, sizeof(recents));
    for( int i = 0; i < 25; i++ )
    {
        struct Editor_CatalogLocalRow row;
        memset(&row, 0, sizeof(row));
        row.kind = TORIRSMAPED_CATALOG_LOC;
        row.id = i;
        row.shape = -1;
        snprintf(row.name, sizeof(row.name), "loc_%d", i);
        Editor_CatalogRecentsPush(&recents, &row);
    }
    CHECK(recents.count == EDITOR_CATALOG_RECENTS_MAX && recents.rows[0].id == 24
              && recents.rows[EDITOR_CATALOG_RECENTS_MAX - 1].id == 5,
        "the last 20 picks, newest first (%d, %d .. %d)", recents.count, recents.rows[0].id,
        recents.rows[recents.count - 1].id);
    {
        struct Editor_CatalogLocalRow again = recents.rows[10];
        again.shape = 2;
        again.rotation = 1;
        Editor_CatalogRecentsPush(&recents, &again);
        CHECK(recents.count == EDITOR_CATALOG_RECENTS_MAX && recents.rows[0].id == again.id
                  && recents.rows[0].shape == 2 && recents.rows[11].id != again.id,
            "a pick made again moves to the front, its pose updated, once");
    }
}

static void
test_preview_follow(void)
{
    struct Editor_CatalogPreviewInput input;
    int kind;
    int id;

    memset(&input, 0, sizeof(input));
    input.pick_kind = 0;
    input.pick_id = 50;
    input.rung_id = -1;
    input.selection_count = 1;
    input.selection_loc_id = 70;
    input.placing = 1;
    CHECK(Editor_CatalogPreviewFollow(&input, &kind, &id) && kind == 0 && id == 50, "placing: the pick");
    input.placing = 0;
    CHECK(Editor_CatalogPreviewFollow(&input, &kind, &id) && kind == 0 && id == 70,
        "selecting: the primary selection, though something was picked");
    input.selection_count = 0;
    CHECK(Editor_CatalogPreviewFollow(&input, &kind, &id) && id == 50, "nothing selected: the pick");
    input.rung_id = 51;
    CHECK(Editor_CatalogPreviewFollow(&input, &kind, &id) && id == 51, "a chosen multiloc rung stands in");
    input.rung_id = -2;
    CHECK(!Editor_CatalogPreviewFollow(&input, &kind, &id) && kind == 0 && id < 0,
        "a value with nothing at it shows nothing");
    input.pick_kind = 1;
    input.pick_id = 3;
    input.rung_id = -1;
    input.placing = 1;
    CHECK(Editor_CatalogPreviewFollow(&input, &kind, &id) && kind == 1 && id == 3, "an npc pick while placing");
    input.pick_kind = -1;
    input.pick_id = -1;
    input.selection_count = 2;
    CHECK(Editor_CatalogPreviewFollow(&input, &kind, &id) && id == 70, "no pick: the selection, even placing");
    input.selection_count = 0;
    CHECK(!Editor_CatalogPreviewFollow(&input, &kind, &id), "neither: nothing");
}

/* ==== part 2: the favourites codec ================================================= */

/** Apply one TEXT_SPLICE (forward or back) to `text`, checking its bytes. */
static int
apply_splice(
    char* text,
    size_t* length,
    size_t capacity,
    const struct Editor_Cmd* cmd,
    int inverse)
{
    uint32_t const at = cmd->u.text_splice.offset;
    uint32_t const before = inverse ? cmd->u.text_splice.after_len : cmd->u.text_splice.before_len;
    uint32_t const after = inverse ? cmd->u.text_splice.before_len : cmd->u.text_splice.after_len;
    const char* out_text = inverse ? cmd->u.text_splice.text : cmd->u.text_splice.text + cmd->u.text_splice.before_len;
    const char* in_text = inverse ? cmd->u.text_splice.text + cmd->u.text_splice.before_len : cmd->u.text_splice.text;

    if( at + before > *length || *length - before + after >= capacity || memcmp(text + at, in_text, before) != 0 )
        return 0;
    memmove(text + at + after, text + at + before, *length - at - before);
    memcpy(text + at, out_text, after);
    *length = *length - before + after;
    text[*length] = '\0';
    return 1;
}

static void
test_favourites_codec(void)
{
    static const char k_text[] = "# mine\r\nfavourite loc:fence_wood | shape 0 rot 1\r\n\r\n// note\n"
                                 "favourite npc:hans\nfavourite obj:bronze_sword";
    struct Editor_FavouritesFile file;
    struct Editor_FavouriteRow row;
    struct Editor_Cmd cmd;
    char out[512];
    char text[1024];
    size_t length;
    struct Editor_FavouritesParseResult result;

    Editor_FavouritesInit(&file);
    result = Editor_FavouritesParse(&file, k_text, strlen(k_text));
    CHECK(result.ok && Editor_FavouritesCount(&file) == 3, "three favourites among comments, CRLF and a blank");
    CHECK(Editor_FavouritesEmit(&file, out, sizeof(out)) == strlen(k_text) && strcmp(out, k_text) == 0,
        "parse -> emit is byte for byte, the missing final newline kept");
    CHECK(Editor_FavouritesAt(&file, 0)->shape == 0 && Editor_FavouritesAt(&file, 0)->rotation == 1
              && Editor_FavouritesAt(&file, 1)->kind == 1 && Editor_FavouritesAt(&file, 1)->shape == -1
              && strcmp(Editor_FavouritesAt(&file, 2)->record, "bronze_sword") == 0,
        "rows, kinds and poses");
    CHECK(Editor_FavouritesParseLine("favourite npc:hans | shape 0 rot 0", 35, NULL) == -1,
        "a pose on an npc is refused");
    CHECK(Editor_FavouritesParseLine("favourite tree:oak", 18, NULL) == -1, "an unknown kind is refused");
    CHECK(Editor_FavouritesParseLine("favourite loc:a | shape 23 rot 0", 32, NULL) == -1, "shape 23 is refused");
    CHECK(Editor_FavouritesParseLine("favourites loc:a", 16, NULL) == -1, "the word is favourite");
    CHECK(Editor_FavouritesParseLine("   ", 3, NULL) == 0, "a blank line is text");
    result = Editor_FavouritesParse(&file, "favourite loc:a\nfavourite loc:a\n", 32);
    CHECK(!result.ok && result.line == 2, "a second line for one record fails on its line (%d)", result.line);

    /* Into an empty file: the header lines come with the first. */
    Editor_FavouritesParse(&file, NULL, 0);
    memset(&row, 0, sizeof(row));
    row.kind = 0;
    snprintf(row.record, sizeof(row.record), "fence_wood");
    row.shape = 2;
    row.rotation = 3;
    length = 0;
    text[0] = '\0';
    CHECK(Editor_FavouritesAddCommand(&cmd, &file, &row) && cmd.kind == EDITOR_CMD_TEXT_SPLICE
              && cmd.doc_kind == EDITOR_DOC_TEXT && strcmp(cmd.doc_key, EDITOR_FAVOURITES_DOC_KEY) == 0,
        "an add is one TEXT_SPLICE on the favourites document");
    CHECK(apply_splice(text, &length, sizeof(text), &cmd, 0)
              && strcmp(text, EDITOR_FAVOURITES_HEADER "favourite loc:fence_wood | shape 2 rot 3\n") == 0,
        "the header and the line: '%s'", text);
    Editor_FavouritesParse(&file, text, length);
    CHECK(!Editor_FavouritesAddCommand(&cmd, &file, &row), "the same favourite again is nothing to do");
    row.shape = 0;
    row.rotation = 0;
    CHECK(Editor_FavouritesAddCommand(&cmd, &file, &row) && apply_splice(text, &length, sizeof(text), &cmd, 0)
              && strcmp(text, EDITOR_FAVOURITES_HEADER "favourite loc:fence_wood | shape 0 rot 0\n") == 0,
        "another pose rewrites the line in place");
    CHECK(apply_splice(text, &length, sizeof(text), &cmd, 1)
              && strcmp(text, EDITOR_FAVOURITES_HEADER "favourite loc:fence_wood | shape 2 rot 3\n") == 0,
        "and the splice's inverse puts it back byte for byte");
    Editor_FavouritesParse(&file, text, length);
    memset(&row, 0, sizeof(row));
    row.kind = 1;
    row.shape = -1;
    snprintf(row.record, sizeof(row.record), "hans");
    CHECK(Editor_FavouritesAddCommand(&cmd, &file, &row) && apply_splice(text, &length, sizeof(text), &cmd, 0),
        "a second favourite");
    Editor_FavouritesParse(&file, text, length);
    CHECK(Editor_FavouritesRemoveCommand(&cmd, &file, 0, "fence_wood") && apply_splice(text, &length, sizeof(text), &cmd, 0)
              && strcmp(text, EDITOR_FAVOURITES_HEADER "favourite npc:hans\n") == 0,
        "a remove takes exactly its line: '%s'", text);
    Editor_FavouritesParse(&file, text, length);
    CHECK(Editor_FavouritesRemoveCommand(&cmd, &file, 1, "hans") && apply_splice(text, &length, sizeof(text), &cmd, 0)
              && length == 0,
        "the last one takes the header it came with");
    CHECK(!Editor_FavouritesRemoveCommand(&cmd, &file, 2, "nothing"), "removing what is not a favourite: nothing");

    /* A hand-written file without a final newline gains one before the add. */
    Editor_FavouritesParse(&file, "favourite npc:hans", 18);
    snprintf(text, sizeof(text), "favourite npc:hans");
    length = strlen(text);
    snprintf(row.record, sizeof(row.record), "guard");
    CHECK(Editor_FavouritesAddCommand(&cmd, &file, &row) && apply_splice(text, &length, sizeof(text), &cmd, 0)
              && strcmp(text, "favourite npc:hans\nfavourite npc:guard\n") == 0,
        "the add ends the last line first: '%s'", text);
    snprintf(row.record, sizeof(row.record), "has space");
    CHECK(!Editor_FavouritesAddCommand(&cmd, &file, &row), "a record name the grammar refuses is not written");
    Editor_FavouritesFree(&file);
}

/* ==== part 3: the panels over an embedded server ================================== */

#define FIXTURE_LOCS 150

static struct Editor g_editor;
static struct RSCache g_profile;
static struct ToriRSChrome g_chrome;
static struct EditorUi g_ui;
static int g_app_stand_in;
/* The thumbnail service's stand-in: the rows asked for. */
static int g_thumb_asked[4096];
static int g_thumb_asks;

static int
fake_loc_thumbnail(
    void* user_data,
    int loc_id,
    int shape)
{
    (void)user_data;
    (void)shape;
    if( g_thumb_asks < (int)(sizeof(g_thumb_asked) / sizeof(g_thumb_asked[0])) )
        g_thumb_asked[g_thumb_asks] = loc_id;
    g_thumb_asks++;
    return 500 + loc_id;
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
make_dir(
    const char* root,
    const char* relative)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    mkdir(path, 0755);
}

static void
write_fixture(const char* root)
{
    static char config[FIXTURE_LOCS * 96];
    static char compack[FIXTURE_LOCS * 24];
    size_t at = 0;
    size_t cat = 0;

    make_dir(root, "");
    make_dir(root, "configs");
    make_dir(root, "maps");
    make_dir(root, "pack");
    make_dir(root, "editor");
    at += (size_t)snprintf(config + at, sizeof(config) - at,
        "[fence_wood]\nname=Fence\nshape1=0,100\nshape2=2,101\nshape3=9,102\ncategory=fence\n\n"
        "[oak_tree]\nname=Oak\nmodels=200\nwidth=2\nlength=2\ncategory=tree\n\n"
        "[wall_torch]\nname=Torch\nshape1=4,300\n\n");
    cat += (size_t)snprintf(compack + cat, sizeof(compack) - cat, "0=fence_wood\n1=oak_tree\n2=wall_torch\n");
    for( int i = 3; i < FIXTURE_LOCS; i++ )
    {
        at += (size_t)snprintf(config + at, sizeof(config) - at, "[thing_%03d]\nname=Thing\nmodels=400\nwidth=%d\n\n", i,
            (i % 3) + 1);
        cat += (size_t)snprintf(compack + cat, sizeof(compack) - cat, "%d=thing_%03d\n", i, i);
    }
    write_text(root, "configs/all.loc", config);
    write_text(root, "configs/all.loc.compack", compack);
    write_text(root, "configs/all.npc", "[test_guard]\nname=Guard\n");
    write_text(root, "configs/all.npc.compack", "0=test_guard\n");
    write_text(root, "configs/all.obj", "[test_knife]\nname=Knife\n");
    write_text(root, "configs/all.obj.compack", "0=test_knife\n");
}

static void
frame(void)
{
    ToriRSChrome_Build(&g_chrome);
    Editor_UiTick(&g_ui);
    ToriRSChrome_Build(&g_chrome);
}

static void
frames(int n)
{
    for( int i = 0; i < n; i++ )
        frame();
}

static void
activate(int widget)
{
    assert(widget >= 0);
    g_chrome.activated = widget;
    frame();
}

static int
find_panel(const char* title)
{
    for( int i = 0; i < g_chrome.panel_count; i++ )
        if( strcmp(g_chrome.panels[i].title, title) == 0 )
            return i;
    return -1;
}

static int
find_widget(
    int panel,
    int kind,
    const char* caption,
    int nth)
{
    for( int i = 0; i < g_chrome.widget_count; i++ )
    {
        const struct ToriRSChromeWidget* w = &g_chrome.widgets[i];
        if( w->panel != panel || w->kind != kind )
            continue;
        if( caption && strcmp(w->label, caption) != 0 && strcmp(w->text, caption) != 0 )
            continue;
        if( nth-- == 0 )
            return i;
    }
    return -1;
}

/** The text a VLIST row holds, "" when the chrome holds none for it. */
static const char*
row_text(
    int list,
    int row)
{
    for( int i = 0; i < TORIRS_CHROME_VROW_POOL; i++ )
        if( g_chrome.vrows[i].owner == list && g_chrome.vrows[i].index == row )
            return g_chrome.vrows[i].text;
    return "";
}

static void
press_chip(
    int graph,
    int key)
{
    struct ToriRSChromeEvent event;
    memset(&event, 0, sizeof(event));
    event.kind = TORIRS_CHROME_EVENT_GRAPH_PRESS;
    event.widget = graph;
    event.serial = g_chrome.widgets[graph].serial;
    event.key = key;
    (void)ToriRSChrome_PushEvent(&g_chrome, &event);
    frame();
}

static const char*
fav_text(void)
{
    struct Editor_DocHandle handle;
    static char text[4096];

    if( !Editor_DocFind(&g_editor.doc, EDITOR_DOC_TEXT, EDITOR_FAVOURITES_DOC_KEY, &handle) || !handle.text
        || !handle.text->loaded || handle.text->length >= sizeof(text) )
        return "(not open)";
    memcpy(text, handle.text->text, handle.text->length);
    text[handle.text->length] = '\0';
    return text;
}

static void
test_panels(const char* root)
{
    const struct Editor_CatalogModel* model;
    const struct Editor_CatalogRecents* recents;
    struct Editor_CatalogView view;
    int panel;
    int list;
    int find;
    int count_row;
    int sort;
    int filter;
    int chips_graph;
    int shortcuts_graph;
    int kind;
    int id;

    ToriRSChrome_Init(&g_chrome);
    ToriRSChrome_SetSurface(&g_chrome, 1280, 900);
    Editor_UiInit(&g_ui, &g_chrome, &g_editor);
    g_ui.app_services.user_data = &g_app_stand_in;
    g_ui.app_services.loc_thumbnail = fake_loc_thumbnail;
    Editor_Catalog_Register(&g_ui);
    Editor_UiSetVisible(&g_ui, 1);
    panel = find_panel("Catalog");
    CHECK(panel >= 0, "the Catalog registers");
    if( panel < 0 )
        return;
    ToriRSChrome_PanelSetVisible(&g_chrome, panel, 1);
    list = find_widget(panel, TORIRS_CHROME_W_VLIST, NULL, 0);
    find = find_widget(panel, TORIRS_CHROME_W_TEXTINPUT, "Find", 0);
    sort = find_widget(panel, TORIRS_CHROME_W_DROPDOWN, "Sort", 0);
    filter = find_widget(panel, TORIRS_CHROME_W_DROPDOWN, "Filter", 0);
    chips_graph = find_widget(panel, TORIRS_CHROME_W_GRAPH, NULL, 0);
    shortcuts_graph = find_widget(panel, TORIRS_CHROME_W_GRAPH, NULL, 1);
    count_row = find_widget(panel, TORIRS_CHROME_W_LABEL, NULL, 0);
    CHECK(list >= 0 && find >= 0 && sort >= 0 && filter >= 0 && chips_graph >= 0 && shortcuts_graph >= 0
              && count_row >= 0,
        "its widgets are there");
    if( list < 0 || find < 0 || sort < 0 || filter < 0 || chips_graph < 0 || shortcuts_graph < 0 || count_row < 0 )
        return;
    CHECK(g_chrome.widgets[list].iaux == 1 && strstr(g_chrome.widgets[list].label, "shapes"),
        "the results are a cells list with a header row");
    CHECK(find_widget(panel, TORIRS_CHROME_W_BUTTON, "Next page", 0) < 0, "no Next page button");
    frames(3);
    model = Editor_Catalog_Model(&g_ui);
    CHECK(model && Editor_CatalogModelViewCount(model) == FIXTURE_LOCS, "the list is all %d locs (%d)", FIXTURE_LOCS,
        model ? Editor_CatalogModelViewCount(model) : -1);
    CHECK(strcmp(g_chrome.widgets[count_row].text, "1 to 100 of 150 loc") == 0, "the count: %s",
        g_chrome.widgets[count_row].text);
    CHECK(strcmp(row_text(list, 0), "500\x1f" "fence_wood\x1f" "Fence\x1f" "0 2 9\x1f" "0") == 0,
        "row 0: its picture, the name, the display name dimmed, shapes 0 2 9 as keycaps, the id (%s)",
        row_text(list, 0));
    {
        int outside = 0;
        int asks = g_thumb_asks < 4096 ? g_thumb_asks : 4096;
        for( int i = 0; i < asks; i++ )
            outside += g_thumb_asked[i] < 0 || g_thumb_asked[i] >= 12;
        CHECK(g_thumb_asks > 0 && outside == 0, "thumbnails are asked for the rows on screen only (%d of %d outside)",
            outside, g_thumb_asks);
    }

    /* Continuous: a scroll lands on the prefetched page. */
    {
        int const queries = model->queries;
        ToriRSChrome_VListSetSelected(&g_chrome, list, 3);
        ToriRSChrome_VListSetScroll(&g_chrome, list, 85);
        frames(2);
        CHECK(model->prefetches == 1 && model->queries == queries + 1,
            "the window nearing the page's end prefetched page 1, one round trip (%d)", model->prefetches);
        ToriRSChrome_VListSetScroll(&g_chrome, list, 98);
        frames(2);
        CHECK(model->queries == queries + 1 && strstr(row_text(list, 101), "thing_101") != NULL,
            "rows past 100 come from it with no new question (%s)", row_text(list, 101));
        CHECK(ToriRSChrome_VListSelected(&g_chrome, list) == 3, "the same list: the selection kept");
        CHECK(strcmp(g_chrome.widgets[count_row].text, "99 to 150 of 150 loc") == 0, "the count: %s",
            g_chrome.widgets[count_row].text);
    }

    /* Chips: a finished key=value word leaves the box. */
    ToriRSChrome_SetText(&g_chrome, find, "category=tree oa");
    frames(3);
    CHECK(strcmp(ToriRSChrome_Text(&g_chrome, find), "oa") == 0 && strcmp(model->filter, "oa category=tree") == 0,
        "category=tree became a chip; Find keeps 'oa' (box '%s', filter '%s')", ToriRSChrome_Text(&g_chrome, find),
        model->filter);
    CHECK(!g_chrome.widgets[chips_graph].hidden && Editor_CatalogModelViewCount(model) == 1
              && strstr(row_text(list, 0), "oak_tree"),
        "the chip strip shows and the query finds oak_tree (%d)", Editor_CatalogModelViewCount(model));
    CHECK(g_chrome.widgets[list].imin == 0, "a new question scrolls to the top");
    CHECK(model->result.progress_total >= 1 && model->result.progress_done == model->result.progress_total
              && !Editor_CatalogModelPartial(model),
        "the panel asked with PROGRESS and read the server's tail: a built index is whole (%u of %u, flags %u)",
        model->result.progress_done, model->result.progress_total, model->result.page_flags);
    ToriRSChrome_SetText(&g_chrome, find, "oa category=tree ");
    frames(2);
    CHECK(strcmp(model->filter, "oa category=tree") == 0, "the same chip typed again is not added twice");
    press_chip(chips_graph, EDP_CHIP_X_BASE + 0);
    frames(2);
    CHECK(strcmp(model->filter, "oa") == 0 && g_chrome.widgets[chips_graph].hidden,
        "its x removes it (filter '%s')", model->filter);
    ToriRSChrome_SetText(&g_chrome, find, "shape=2 ");
    frames(3);
    CHECK(strcmp(model->filter, "shape=2") == 0 && Editor_CatalogModelViewCount(model) == 1
              && strstr(row_text(list, 0), "fence_wood"),
        "shape=2 matches the loc that states shape 2 (filter '%s', %d rows)", model->filter,
        Editor_CatalogModelViewCount(model));
    press_chip(chips_graph, EDP_CHIP_X_BASE + 0);
    ToriRSChrome_DropdownSetSelected(&g_chrome, filter, 1);
    activate(filter);
    frame();
    CHECK(strcmp(model->filter, "shape=wall") == 0 && ToriRSChrome_DropdownSelected(&g_chrome, filter) == 0,
        "+ filter adds the shape: wall chip and goes back to its title (%s)", model->filter);
    press_chip(chips_graph, EDP_CHIP_X_BASE + 0);

    /* A shape keycap picks the loc at that shape. */
    ToriRSChrome_SetText(&g_chrome, find, "fence_wood");
    frames(3);
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_SELECT);
    g_ui.tools.place_shape = 0;
    ToriRSChrome_VListSetSelected(&g_chrome, list, 0);
    g_chrome.widgets[list].vlist_cell = 2;
    activate(list);
    CHECK(g_ui.tools.pick_kind == 0 && g_ui.tools.pick_id == 0 && g_ui.tools.place_shape == 2
              && g_ui.tools.tool == EDITOR_TOOL_LOC_PLACE,
        "keycap 2 on fence_wood: the pick at shape 2, Place armed (id %d, shape %d)", g_ui.tools.pick_id,
        g_ui.tools.place_shape);
    CHECK(strstr(Editor_UiStatusText(&g_ui), "shape 2") != NULL, "the status bar says so (%s)",
        Editor_UiStatusText(&g_ui));
    recents = Editor_Catalog_Recents(&g_ui);
    CHECK(recents && recents->count == 1 && strcmp(recents->rows[0].name, "fence_wood") == 0
              && recents->rows[0].shape == 2,
        "the pick is the most recent, with its pose");

    /* The favourite star, through the authority, one undo step. */
    g_ui.tools.place_rotation = 1;
    CHECK(!Editor_Catalog_IsFavourite(&g_ui), "not a favourite yet");
    CHECK(Editor_Catalog_ToggleFavourite(&g_ui) == 1
              && strcmp(fav_text(), EDITOR_FAVOURITES_HEADER "favourite loc:fence_wood | shape 2 rot 1\n") == 0,
        "the star writes it with Place's pose: '%s'", fav_text());
    frame();
    CHECK(Editor_Catalog_IsFavourite(&g_ui), "and it is one");
    CHECK(Editor_Undo(&g_editor) >= 1 && strcmp(fav_text(), "") == 0, "one undo takes it out");
    frame();
    CHECK(!Editor_Catalog_IsFavourite(&g_ui), "and the star follows");
    CHECK(Editor_Redo(&g_editor) >= 1 && strstr(fav_text(), "favourite loc:fence_wood") != NULL, "redo puts it back");
    frame();
    CHECK(Editor_SaveAll(&g_editor) >= 1, "Save all writes it");
    {
        char path[2048];
        char disk[4096];
        size_t got;
        FILE* f;
        snprintf(path, sizeof(path), "%s/editor/favourites.txt", root);
        f = fopen(path, "rb");
        got = f ? fread(disk, 1, sizeof(disk) - 1, f) : 0;
        if( f )
            fclose(f);
        disk[got] = '\0';
        CHECK(strcmp(disk, fav_text()) == 0, "editor/favourites.txt is the document");
    }

    /* The favourites shortcut: the list is the file's rows. */
    ToriRSChrome_SetText(&g_chrome, find, "");
    frames(2);
    press_chip(shortcuts_graph, EDP_CHIP_KEY_BASE + 0);
    frames(2);
    CHECK(model->source == EDITOR_CATALOG_SOURCE_FAVOURITES && Editor_CatalogModelViewCount(model) == 1
              && Editor_CatalogModelViewAt(model, 0, &view) && view.id == 0 && view.shape == 2 && view.rotation == 1
              && view.shape_mask == ((1u << 0) | (1u << 2) | (1u << 9)),
        "favourites (1): fence_wood, its id and shapes from the catalog, its pose from the file");
    CHECK(strstr(g_chrome.widgets[count_row].text, "favourites") != NULL, "the count names it (%s)",
        g_chrome.widgets[count_row].text);
    g_ui.tools.place_shape = 0;
    ToriRSChrome_VListSetSelected(&g_chrome, list, 0);
    g_chrome.widgets[list].vlist_cell = 0;
    activate(list);
    CHECK(g_ui.tools.pick_id == 0 && g_ui.tools.place_shape == 2 && g_ui.tools.place_rotation == 1,
        "picking a favourite arms its pose");
    press_chip(shortcuts_graph, EDP_CHIP_KEY_BASE + 0);
    frames(2);
    CHECK(model->source == EDITOR_CATALOG_SOURCE_INDEX, "its second click is the index again");
    press_chip(shortcuts_graph, EDP_CHIP_KEY_BASE + 1);
    frames(2);
    CHECK(model->source == EDITOR_CATALOG_SOURCE_RECENT && Editor_CatalogModelViewCount(model) == 1,
        "recent (1): one pick so far, made twice (%d)", Editor_CatalogModelViewCount(model));
    press_chip(shortcuts_graph, EDP_CHIP_KEY_BASE + 1);
    press_chip(shortcuts_graph, EDP_CHIP_KEY_BASE + 3);
    frames(3);
    CHECK(strcmp(model->filter, "category=fence") == 0, "same category adds the pick's category chip (%s)",
        model->filter);
    press_chip(chips_graph, EDP_CHIP_X_BASE + 0);

    /* Sort by size over a small result. */
    ToriRSChrome_SetText(&g_chrome, find, "thing_01");
    frames(3);
    ToriRSChrome_DropdownSetSelected(&g_chrome, sort, EDITOR_CATALOG_SORT_SIZE);
    activate(sort);
    frames(2);
    {
        int ordered = Editor_CatalogModelSortApplies(model) && Editor_CatalogModelViewCount(model) == 10;
        int prev = 0;
        for( int r = 0; r < Editor_CatalogModelViewCount(model); r++ )
        {
            ordered &= Editor_CatalogModelViewAt(model, r, &view) && view.size_w * view.size_l >= prev;
            prev = view.size_w * view.size_l;
        }
        CHECK(ordered, "sort: size orders thing_010..019 by footprint (%d rows)", Editor_CatalogModelViewCount(model));
    }
    ToriRSChrome_DropdownSetSelected(&g_chrome, sort, EDITOR_CATALOG_SORT_BEST);
    activate(sort);

    /* Pick from world (Shift+I): its exact pose, Find = its name, the row in view. */
    CHECK(Editor_Catalog_PickFromWorld(&g_ui, 2, 4, 3), "Pick from world");
    frames(4);
    CHECK(g_ui.tools.pick_kind == 0 && g_ui.tools.pick_id == 2 && g_ui.tools.place_shape == 4
              && g_ui.tools.place_rotation == 3 && g_ui.tools.tool == EDITOR_TOOL_LOC_PLACE,
        "the pick is wall_torch at shape 4 rot 3, Place armed");
    CHECK(strcmp(ToriRSChrome_Text(&g_chrome, find), "wall_torch") == 0, "Find is its name (%s)",
        ToriRSChrome_Text(&g_chrome, find));
    {
        int const row = Editor_CatalogModelFindRow(model, TORIRSMAPED_CATALOG_LOC, "wall_torch");
        CHECK(row >= 0 && ToriRSChrome_VListSelected(&g_chrome, list) == row && row >= g_chrome.widgets[list].imin
                  && row < g_chrome.widgets[list].imin + g_chrome.widgets[list].rows,
            "its row is selected and in view (row %d, selected %d)", row, ToriRSChrome_VListSelected(&g_chrome, list));
    }

    /* The Preview follows the selection while selecting, after a pick. */
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_SELECT);
    g_ui.tools.loc_count = 1;
    g_ui.tools.locs[0].loc_id = 1;
    CHECK(Editor_Preview_Subject(&g_ui, &kind, &id) && kind == 0 && id == 1,
        "selecting: the Preview shows the selected oak_tree, not the pick (%d %d)", kind, id);
    Editor_UiSetTool(&g_ui, EDITOR_TOOL_LOC_PLACE);
    CHECK(Editor_Preview_Subject(&g_ui, &kind, &id) && kind == 0 && id == 2, "placing: the pick (%d)", id);
    g_ui.tools.loc_count = 0;

    /* Refusals say why. */
    g_ui.tools.pick_kind = -1;
    g_ui.tools.pick_id = -1;
    CHECK(Editor_Catalog_ToggleFavourite(&g_ui) == -1 && strstr(Editor_UiStatusText(&g_ui), "Favourite refused"),
        "a star with nothing picked is refused in the status bar (%s)", Editor_UiStatusText(&g_ui));
    CHECK(Editor_Catalog_FindInWorld(&g_ui) == 0 && strstr(Editor_UiStatusText(&g_ui), "refused"),
        "Find in world with no loc pick says why");
    Editor_UiFree(&g_ui);
}

int
main(
    int argc,
    char** argv)
{
    struct EditorHost host;
    const char* root;

    if( argc < 2 )
    {
        fprintf(stderr, "usage: %s <tmp-content-dir>\n", argv[0]);
        return 2;
    }
    root = argv[1];
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    test_chips();
    test_rows();
    test_server_fields();
    test_window();
    test_local_and_recents();
    test_preview_follow();
    test_favourites_codec();
    if( !RSCache_ProfileByName("osrs241", &g_profile) )
    {
        printf("FAIL: no osrs241 profile\n");
        return 1;
    }
    write_fixture(root);
    if( !Editor_HostOpenMapEdEmbed(&host, root, NULL) )
    {
        printf("FAIL: the embedded ToriRSMapEd did not open %s\n", root);
        return 1;
    }
    Editor_OpenHost(&g_editor, &host, root, &g_profile);
    test_panels(root);
    Editor_Close(&g_editor);
    printf("editor_catalog_finder_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
