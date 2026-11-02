/*
 * The AREAS codec (editor/editor_areas.h): named areas and square
 * collections, line-preserving.
 *
 *   ./editor_areas_test
 *
 * No content tree: the file is small and authored by the editor, so the
 * fixtures are written here, including the hand-formatted shapes a person
 * leaves behind (extra spaces, CRLF, a comment between rows, no final
 * newline).
 */

#include "editor/editor_areas.h"
#include "mem/torirs_mem.h"
#include "torirsmaped/torirs_maped.h"

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

static char g_emit[8192];

static const char*
emit(const struct Editor_AreasFile* file)
{
    size_t const n = Editor_AreasEmit(file, g_emit, sizeof(g_emit));
    if( n >= sizeof(g_emit) )
    {
        printf("FAIL: a fixture outgrew the emit buffer\n");
        exit(1);
    }
    return g_emit;
}

static int
parse(
    struct Editor_AreasFile* file,
    const char* text)
{
    struct Editor_AreasParseResult r = Editor_AreasParse(file, text, strlen(text));
    return r.ok ? 0 : r.line;
}

static void
test_round_trip(void)
{
    static const char* const k_texts[] = {
        "",
        "\n",
        "# just a comment",
        "area A | level 0 | rects 1 2 3 4\n",
        "area A | level 0 | rects 1 2 3 4",
        ("# header\n\narea Lumbridge castle | level 0 | rects 3205 3208 3225 3226 | colour 2 | note The castle\n"
         "// a slash comment\n"
         "collection Lumbridge | squares m50_50 m50_51 ported/testlane/maps/m50_50 | note Start\n\n"),
        ("area   Spaced  name   |level all|  rects 10 20 30 40 ,  50 60 70 80|note  x\r\n"
         "collection Empty | squares\r\n"),
        "area B | rects 5 5 5 5 | level 3 | color 7\n",
    };
    struct Editor_AreasFile file;

    Editor_AreasInit(&file);
    for( size_t i = 0; i < sizeof(k_texts) / sizeof(k_texts[0]); i++ )
    {
        int const bad = parse(&file, k_texts[i]);
        CHECK(bad == 0, "fixture %zu parses (line %d)", i, bad);
        CHECK(strcmp(emit(&file), k_texts[i]) == 0, "fixture %zu emits byte-identical: '%s'", i, g_emit);
    }

    parse(&file, k_texts[6]);
    CHECK(Editor_AreasAreaCount(&file) == 1 && Editor_AreasCollectionCount(&file) == 1, "one of each");
    {
        int const a = Editor_AreasFindArea(&file, "Spaced  name");
        CHECK(a == 0, "the name keeps its inner spaces, trimmed at the ends (%d)", a);
        if( a == 0 )
        {
            const struct Editor_Area* area = file.lines[0].area;
            CHECK(area->level == -1 && area->rect_count == 2 && area->rects[1].x1 == 70, "level all, two rects");
            CHECK(strcmp(area->note, "x") == 0, "note trimmed: '%s'", area->note);
            CHECK(area->colour == -1, "no colour = by place");
        }
        CHECK(Editor_AreasFindCollection(&file, "Empty") == 1, "empty collection");
    }
    parse(&file, k_texts[7]);
    CHECK(file.lines[0].area && file.lines[0].area->colour == 7 && file.lines[0].area->level == 3,
        "segments in any order, 'color' spelling");
    Editor_AreasFree(&file);
}

static void
test_refusals(void)
{
    static const struct
    {
        const char* text;
        int line;
    } k_bad[] = {
        { "area A | level 0\n", 1 },                                        /* no rects */
        { "# ok\nzone A | rects 1 2 3 4\n", 2 },                            /* unknown keyword */
        { "area A | rects 1 2 3\n", 1 },                                    /* three numbers */
        { "area A | rects 1 2 3 4 | level 4\n", 1 },                        /* level out of range */
        { "area A | rects 1 2 3 4 | level 0 | level 1\n", 1 },              /* segment twice */
        { "area A | rects 1 2 3 4 | colour 8\n", 1 },                       /* colour out of range */
        { "area A | rects 1 2 3 4 | flavour 1\n", 1 },                      /* unknown segment */
        { "area | rects 1 2 3 4\n", 1 },                                    /* no name */
        { "area A | rects 1 2 3 4\narea A | rects 5 6 7 8\n", 2 },          /* duplicate name */
        { "collection C | squares m50_50 m50_50\n", 1 },                    /* duplicate key */
        { "collection C | squares 50_50\n", 1 },                            /* not a square key */
        { "collection C | squares ../maps/m50_50\n", 1 },                   /* escaping storage */
        { "area A | rects 1 2 3 4 | rects 5 6 7 8\n", 1 },                  /* rects twice */
        { "area A | rects 1 2 3 99999\n", 1 },                              /* past the tile range */
    };
    struct Editor_AreasFile file;

    Editor_AreasInit(&file);
    for( size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++ )
    {
        int const line = parse(&file, k_bad[i].text);
        CHECK(line == k_bad[i].line, "bad %zu refused at line %d (got %d)", i, k_bad[i].line, line);
    }
    /* An area and a collection may share a name: unique PER KIND. */
    CHECK(parse(&file, "area X | rects 1 1 2 2\ncollection X | squares m1_1\n") == 0, "kinds do not collide");
    Editor_AreasFree(&file);

    CHECK(Editor_AreaNameValid("Lumbridge castle"), "a name with a space");
    CHECK(!Editor_AreaNameValid(" lead"), "leading space");
    CHECK(!Editor_AreaNameValid("trail "), "trailing space");
    CHECK(!Editor_AreaNameValid("a|b"), "a bar");
    CHECK(!Editor_AreaNameValid("#x"), "a comment marker");
    CHECK(!Editor_AreaNameValid(""), "empty");
    CHECK(Editor_CollectionKeyValid("m50_50"), "bare key");
    CHECK(Editor_CollectionKeyValid("ported/testlane/maps/m16_101"), "storage key");
    CHECK(!Editor_CollectionKeyValid("m50_50x"), "trailing junk");
    CHECK(!Editor_CollectionKeyValid("/abs/m1_1"), "absolute storage");
}

static void
test_format(void)
{
    struct Editor_Area area;
    struct Editor_Area back;
    struct Editor_Collection* coll = calloc(2, sizeof(*coll));
    char row[EDITOR_AREAS_LINE_MAX + 1];

    assert(coll);
    memset(&area, 0, sizeof(area));
    snprintf(area.name, sizeof(area.name), "Lumbridge castle");
    area.level = 0;
    area.colour = -1;
    CHECK(Editor_AreaAddRect(&area, 3225, 3226, 3205, 3208), "add a rect given corner-swapped");
    CHECK(area.rects[0].x0 == 3205 && area.rects[0].z1 == 3226, "normalised");
    Editor_AreaFormat(&area, row, sizeof(row));
    CHECK(strcmp(row, "area Lumbridge castle | level 0 | rects 3205 3208 3225 3226") == 0, "row: '%s'", row);
    CHECK(Editor_AreasParseLine(row, &back, NULL) == EDITOR_AREAS_LINE_AREA, "parses back");
    CHECK(memcmp(&back, &area, sizeof(area)) == 0, "the same record");
    {
        int cx;
        int cz;
        Editor_AreaAddRect(&area, 3230, 3200, 3240, 3210);
        Editor_AreaCentre(&area, &cx, &cz);
        CHECK(cx == (3205 + 3240) / 2 && cz == (3200 + 3226) / 2, "centre of the bounds %d,%d", cx, cz);
    }
    area.level = -1;
    area.colour = 3;
    snprintf(area.note, sizeof(area.note), "two parts");
    Editor_AreaFormat(&area, row, sizeof(row));
    CHECK(strcmp(row, "area Lumbridge castle | level all | rects 3205 3208 3225 3226, 3230 3200 3240 3210 | colour "
                      "3 | note two parts") == 0,
        "row: '%s'", row);
    CHECK(Editor_AreaValid(&area), "valid");
    for( int i = area.rect_count; i < EDITOR_AREA_RECTS_MAX; i++ )
        Editor_AreaAddRect(&area, 10000 + i, 10000, 10010 + i, 10010);
    CHECK(!Editor_AreaAddRect(&area, 1, 1, 2, 2), "full at %d rects", EDITOR_AREA_RECTS_MAX);
    CHECK(Editor_AreaFormat(&area, NULL, 0) <= EDITOR_AREAS_LINE_MAX, "a full area still fits one command");

    snprintf(coll[0].name, sizeof(coll[0].name), "Lumbridge");
    CHECK(Editor_CollectionAdd(&coll[0], "m50_50"), "add a key");
    CHECK(!Editor_CollectionAdd(&coll[0], "m50_50"), "not twice");
    CHECK(Editor_CollectionAdd(&coll[0], "ported/testlane/maps/m50_51"), "a storage key");
    Editor_CollectionFormat(&coll[0], row, sizeof(row));
    CHECK(strcmp(row, "collection Lumbridge | squares m50_50 ported/testlane/maps/m50_51") == 0, "row: '%s'", row);
    CHECK(Editor_AreasParseLine(row, NULL, &coll[1]) == EDITOR_AREAS_LINE_COLLECTION, "parses back");
    CHECK(memcmp(&coll[0], &coll[1], sizeof(coll[0])) == 0, "the same collection");
    CHECK(Editor_CollectionRemove(&coll[0], "m50_50") && coll[0].square_count == 1, "remove");
    {
        int added = 0;
        char key[32];
        for( int i = 0; i < 200; i++ )
        {
            snprintf(key, sizeof(key), "m%d_%d", 100 + i, 200 + i);
            added += Editor_CollectionAdd(&coll[0], key);
        }
        CHECK(Editor_CollectionFormat(&coll[0], NULL, 0) <= EDITOR_AREAS_LINE_MAX,
            "adding stops before the row outgrows a command (%d added)", added);
        CHECK(added > 40, "room for %d plain keys", added);
    }
    free(coll);
}

static int
apply(
    struct Editor_AreasFile* file,
    const struct Editor_Cmd* command,
    int inverse)
{
    return Editor_AreasApply(file, command, inverse);
}

static void
test_commands(void)
{
    struct Editor_AreasFile file;
    struct Editor_Area area;
    struct Editor_Collection* coll = calloc(1, sizeof(*coll));
    struct Editor_Cmd* cmd = calloc(4, sizeof(*cmd));
    char label[128];
    char first[8192];

    assert(coll);
    assert(cmd);
    Editor_AreasInit(&file);
    parse(&file, "");

    /* A new record on an empty file brings the header; undo takes it back. */
    memset(&area, 0, sizeof(area));
    snprintf(area.name, sizeof(area.name), "Lumbridge castle");
    area.level = 0;
    area.colour = -1;
    Editor_AreaAddRect(&area, 3205, 3208, 3225, 3226);
    CHECK(Editor_AreaSetCommand(&cmd[0], &file, NULL, &area), "mark command");
    CHECK(cmd[0].doc_kind == EDITOR_DOC_AREAS && strcmp(cmd[0].doc_key, "areas") == 0, "addressed to areas");
    CHECK(cmd[0].kind == EDITOR_CMD_AREA_SET && !cmd[0].has_before && cmd[0].has_after, "an add");
    Editor_AreasCmdLabel(&cmd[0], label, sizeof(label));
    CHECK(strcmp(label, "Mark area Lumbridge castle") == 0, "label '%s'", label);
    CHECK(apply(&file, &cmd[0], 0), "applied");
    CHECK(strncmp(emit(&file), "# ToriRSMapEd named areas", 25) == 0, "header first: '%s'", g_emit);
    CHECK(strstr(g_emit, "\narea Lumbridge castle | level 0 | rects 3205 3208 3225 3226\n") != NULL, "the row");
    CHECK(!apply(&file, &cmd[0], 0), "the same add again is refused (name taken)");
    CHECK(!Editor_AreaSetCommand(&cmd[1], &file, NULL, &area), "a second area of that name is refused");
    CHECK(apply(&file, &cmd[0], 1), "undone");
    CHECK(strcmp(emit(&file), "") == 0, "an absent file again: '%s'", g_emit);
    CHECK(apply(&file, &cmd[0], 0), "redone");
    snprintf(first, sizeof(first), "%s", emit(&file));

    /* Rename, then level, then an extra rect: each is a SET carrying the old row. */
    {
        struct Editor_Area renamed = area;
        snprintf(renamed.name, sizeof(renamed.name), "Castle");
        CHECK(Editor_AreaSetCommand(&cmd[1], &file, "Lumbridge castle", &renamed), "rename command");
        Editor_AreasCmdLabel(&cmd[1], label, sizeof(label));
        CHECK(strcmp(label, "Rename area Lumbridge castle to Castle") == 0, "label '%s'", label);
        CHECK(apply(&file, &cmd[1], 0), "renamed");
        CHECK(Editor_AreasFindArea(&file, "Castle") >= 0 && Editor_AreasFindArea(&file, "Lumbridge castle") < 0,
            "found by the new name only");
        CHECK(!Editor_AreaSetCommand(&cmd[2], &file, "Castle", &renamed), "nothing changes: no command");
        renamed.level = 1;
        CHECK(Editor_AreaSetCommand(&cmd[2], &file, "Castle", &renamed), "level command");
        Editor_AreasCmdLabel(&cmd[2], label, sizeof(label));
        CHECK(strcmp(label, "Set level of area Castle to 1") == 0, "label '%s'", label);
        CHECK(apply(&file, &cmd[2], 0), "level set");
        CHECK(apply(&file, &cmd[2], 1) && apply(&file, &cmd[1], 1), "both undone");
        CHECK(strcmp(emit(&file), first) == 0, "back to the first add byte for byte");
        CHECK(!apply(&file, &cmd[2], 0), "a stale SET (its before row is gone) is refused");
    }

    /* A hand-formatted row in the middle: an edit rewrites it, the undo puts
     * its exact bytes back; a remove's undo puts it back at its index. */
    parse(&file, "# mine\narea  A |rects 1 1 2 2   | level 0\n\ncollection C | squares m50_50\n# tail\n");
    snprintf(first, sizeof(first), "%s", emit(&file));
    {
        struct Editor_Area a = *file.lines[1].area;
        Editor_AreaAddRect(&a, 5, 5, 6, 6);
        CHECK(Editor_AreaSetCommand(&cmd[0], &file, "A", &a), "add-rect command");
        Editor_AreasCmdLabel(&cmd[0], label, sizeof(label));
        CHECK(strcmp(label, "Add rect to area A") == 0, "label '%s'", label);
        CHECK(apply(&file, &cmd[0], 0), "rect added");
        CHECK(strcmp(file.lines[1].text, "area A | level 0 | rects 1 1 2 2, 5 5 6 6") == 0, "rewritten in place: '%s'",
            file.lines[1].text);
        CHECK(apply(&file, &cmd[0], 1), "undone");
        CHECK(strcmp(emit(&file), first) == 0, "the hand-formatted bytes are back: '%s'", g_emit);
    }
    CHECK(Editor_CollectionRemoveCommand(&cmd[1], &file, "C"), "remove command");
    CHECK(cmd[1].u.area.line == 3, "remembers its line (%d)", cmd[1].u.area.line);
    Editor_AreasCmdLabel(&cmd[1], label, sizeof(label));
    CHECK(strcmp(label, "Delete collection C") == 0, "label '%s'", label);
    CHECK(apply(&file, &cmd[1], 0), "removed");
    CHECK(strcmp(emit(&file), "# mine\narea  A |rects 1 1 2 2   | level 0\n\n# tail\n") == 0, "gone: '%s'", g_emit);
    CHECK(apply(&file, &cmd[1], 1), "undone");
    CHECK(strcmp(emit(&file), first) == 0, "back where it stood");
    CHECK(!Editor_AreaRemoveCommand(&cmd[2], &file, "nope"), "removing an absent area makes no command");

    /* Collections: new, add keys, rename. */
    memset(coll, 0, sizeof(*coll));
    snprintf(coll->name, sizeof(coll->name), "Lumbridge");
    Editor_CollectionAdd(coll, "m50_50");
    CHECK(Editor_CollectionSetCommand(&cmd[0], &file, NULL, coll), "new collection");
    Editor_AreasCmdLabel(&cmd[0], label, sizeof(label));
    CHECK(strcmp(label, "New collection Lumbridge") == 0, "label '%s'", label);
    CHECK(apply(&file, &cmd[0], 0), "applied");
    CHECK(strcmp(file.lines[file.line_count - 1].text, "collection Lumbridge | squares m50_50") == 0,
        "appended: '%s'", file.lines[file.line_count - 1].text);
    Editor_CollectionAdd(coll, "m50_51");
    CHECK(Editor_CollectionSetCommand(&cmd[1], &file, "Lumbridge", coll), "add a square");
    Editor_AreasCmdLabel(&cmd[1], label, sizeof(label));
    CHECK(strcmp(label, "Add 1 square to collection Lumbridge") == 0, "label '%s'", label);
    CHECK(apply(&file, &cmd[1], 0), "applied");
    snprintf(coll->name, sizeof(coll->name), "C");
    CHECK(!Editor_CollectionSetCommand(&cmd[2], &file, "Lumbridge", coll), "renaming onto a taken name is refused");
    CHECK(apply(&file, &cmd[1], 1) && apply(&file, &cmd[0], 1), "undone");
    CHECK(strcmp(emit(&file), first) == 0, "the file as it was");

    /* A command of the wrong shape or kind is refused. */
    cmd[3] = cmd[1];
    cmd[3].kind = EDITOR_CMD_AREA_SET;
    CHECK(!apply(&file, &cmd[3], 0), "a collection row under AREA_SET is refused");

    Editor_AreasFree(&file);
    free(coll);
    free(cmd);
}

static void
test_header_lifecycle(void)
{
    struct Editor_AreasFile file;
    struct Editor_Area area;
    struct Editor_Cmd* cmd = calloc(2, sizeof(*cmd));

    assert(cmd);
    Editor_AreasInit(&file);
    parse(&file, "");
    memset(&area, 0, sizeof(area));
    snprintf(area.name, sizeof(area.name), "Only");
    area.colour = -1;
    area.level = 2;
    Editor_AreaAddRect(&area, 1, 1, 1, 1);
    Editor_AreaSetCommand(&cmd[0], &file, NULL, &area);
    apply(&file, &cmd[0], 0);
    /* The last record removed: the header goes too; its undo brings both. */
    CHECK(Editor_AreaRemoveCommand(&cmd[1], &file, "Only"), "remove the only area");
    CHECK(apply(&file, &cmd[1], 0), "removed");
    CHECK(file.line_count == 0, "header gone with it (%d lines)", file.line_count);
    CHECK(apply(&file, &cmd[1], 1), "undone");
    CHECK(file.line_count == 4 && Editor_AreasFindArea(&file, "Only") == 3, "header and row back");
    Editor_AreasFree(&file);
    free(cmd);
}

static void
test_wire(void)
{
    struct Editor_AreasFile file;
    struct Editor_Area area;
    struct Editor_Cmd* cmd = calloc(2, sizeof(*cmd));
    static const enum Editor_CmdKind k_kinds[4] = {
        EDITOR_CMD_AREA_SET, EDITOR_CMD_AREA_REMOVE, EDITOR_CMD_COLLECTION_SET, EDITOR_CMD_COLLECTION_REMOVE
    };

    assert(cmd);
    Editor_AreasInit(&file);
    parse(&file, "area A | level 0 | rects 1 1 2 2\ncollection C | squares m50_50\n");
    memset(&area, 0, sizeof(area));
    snprintf(area.name, sizeof(area.name), "B");
    area.colour = 5;
    area.level = -1;
    Editor_AreaAddRect(&area, 7, 7, 9, 9);
    for( int k = 0; k < 4; k++ )
    {
        struct ToriRSMapEdBuf buf;
        int made = 0;

        if( k_kinds[k] == EDITOR_CMD_AREA_SET )
            made = Editor_AreaSetCommand(&cmd[0], &file, "A", &area);
        else if( k_kinds[k] == EDITOR_CMD_AREA_REMOVE )
            made = Editor_AreaRemoveCommand(&cmd[0], &file, "A");
        else if( k_kinds[k] == EDITOR_CMD_COLLECTION_SET )
        {
            struct Editor_Collection* c = calloc(1, sizeof(*c));
            assert(c);
            snprintf(c->name, sizeof(c->name), "D");
            Editor_CollectionAdd(c, "m1_2");
            made = Editor_CollectionSetCommand(&cmd[0], &file, NULL, c);
            free(c);
        }
        else
            made = Editor_CollectionRemoveCommand(&cmd[0], &file, "C");
        CHECK(made, "kind %d made", (int)k_kinds[k]);
        memset(&buf, 0, sizeof(buf));
        ToriRSMapEd_CmdEncode(&cmd[0], &buf);
        CHECK(ToriRSMapEd_CmdDecode(ToriRSMapEd_BufPeek(&buf), (uint32_t)ToriRSMapEd_BufAvailable(&buf), &cmd[1])
                  == TORIRSMAPED_CMD_DECODE_OK,
            "kind %d decodes", (int)k_kinds[k]);
        CHECK(memcmp(&cmd[0], &cmd[1], sizeof(cmd[0])) == 0, "kind %d round-trips the wire", (int)k_kinds[k]);
        /* Truncated: a remote peer's short frame is data, not a crash. */
        CHECK(ToriRSMapEd_CmdDecode(ToriRSMapEd_BufPeek(&buf), (uint32_t)ToriRSMapEd_BufAvailable(&buf) - 1, &cmd[1])
                  == TORIRSMAPED_CMD_DECODE_MALFORMED,
            "kind %d truncated is malformed", (int)k_kinds[k]);
        ToriRSMapEd_BufFree(&buf);
    }
    Editor_AreasFree(&file);
    free(cmd);
}

int
main(void)
{
    ToriMem_Init();
    test_round_trip();
    test_refusals();
    test_format();
    test_commands();
    test_header_lifecycle();
    test_wire();
    printf("editor_areas_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
