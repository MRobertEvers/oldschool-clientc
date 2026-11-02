/*
 * The PALETTES codec (editor/editor_palette.h): named strips of catalog
 * records, line-preserving.
 *
 *   ./editor_palette_test
 *
 * No content tree: the file is small and authored by the editor, so the
 * fixtures are written here, including the hand-formatted shapes a person
 * leaves behind (extra spaces, CRLF, a comment between rows, no final
 * newline).
 */

#include "editor/editor_palette.h"
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
emit(const struct Editor_PalettesFile* file)
{
    size_t const n = Editor_PalettesEmit(file, g_emit, sizeof(g_emit));
    if( n >= sizeof(g_emit) )
    {
        printf("FAIL: a fixture outgrew the emit buffer\n");
        exit(1);
    }
    return g_emit;
}

static int
parse(
    struct Editor_PalettesFile* file,
    const char* text)
{
    struct Editor_PalettesParseResult r = Editor_PalettesParse(file, text, strlen(text));
    return r.ok ? 0 : r.line;
}

/** Apply `count` commands in order (or undo them in reverse). */
static int
apply_all(
    struct Editor_PalettesFile* file,
    const struct Editor_Cmd* cmds,
    int count,
    int inverse)
{
    int applied = 0;
    for( int i = 0; i < count; i++ )
    {
        int const at = inverse ? count - 1 - i : i;
        applied += Editor_PalettesApply(file, &cmds[at], inverse);
    }
    return applied;
}

static const char k_fixture[] = "# my palettes\r\n"
                                "palette Lumbridge   | note The castle grounds\r\n"
                                "palette Lumbridge | slot 1 | loc:oaktree\r\n"
                                "\r\n"
                                "// a wall next\r\n"
                                "palette  Lumbridge |slot 2| loc:loc_1902 | shape 0 rot 1\r\n"
                                "palette Lumbridge | npc:hans | slot 4\r\n"
                                "palette Shops | slot 3 | obj:crate";

static void
test_round_trip(void)
{
    struct Editor_PalettesFile file;
    const struct Editor_PaletteRow* row;

    Editor_PalettesInit(&file);
    CHECK(parse(&file, k_fixture) == 0, "the fixture parses");
    CHECK(strcmp(emit(&file), k_fixture) == 0, "parse -> emit is byte-identical (CRLF, comments, no final newline)");
    CHECK(Editor_PalettesCount(&file) == 2 && strcmp(Editor_PalettesName(&file, 0), "Lumbridge") == 0
              && strcmp(Editor_PalettesName(&file, 1), "Shops") == 0 && Editor_PalettesName(&file, 2) == NULL,
        "two palettes, in the order of their first lines");
    CHECK(Editor_PalettesSlotCount(&file, "Lumbridge") == 3 && Editor_PalettesSlotCount(&file, "Shops") == 1,
        "slot counts");
    CHECK(Editor_PalettesFindHeader(&file, "Lumbridge") == 1 && Editor_PalettesFindHeader(&file, "Shops") == -1,
        "a header is optional");
    row = Editor_PalettesEntry(&file, "Lumbridge", 2);
    CHECK(row && row->kind == EDITOR_PALETTE_LOC && strcmp(row->record, "loc_1902") == 0 && row->shape == 0
              && row->rotation == 1,
        "slot 2 is the wall with its pose");
    row = Editor_PalettesEntry(&file, "Lumbridge", 4);
    CHECK(row && row->kind == EDITOR_PALETTE_NPC && strcmp(row->record, "hans") == 0 && row->shape == -1,
        "segments in any order: slot 4 is hans");
    CHECK(Editor_PalettesEntry(&file, "Lumbridge", 3) == NULL, "slot 3 is empty");
    CHECK(strcmp(file.lines[1].row->note, "The castle grounds") == 0, "the header's note");

    CHECK(parse(&file, "") == 0 && file.line_count == 0 && strcmp(emit(&file), "") == 0, "empty is a valid file");
    CHECK(parse(&file, "palette A\n\n") == 0 && strcmp(emit(&file), "palette A\n\n") == 0, "trailing blank line kept");
    Editor_PalettesFree(&file);
}

static void
test_refusals(void)
{
    struct Editor_PalettesFile file;
    static const struct
    {
        const char* text;
        int line;
    } k_bad[] = {
        { "palette A\npalette A\n", 2 },                          /* two headers */
        { "palette A | slot 1 | loc:a\npalette A | slot 1 | npc:b\n", 2 }, /* one slot twice */
        { "palette A | slot 11 | loc:a\n", 1 },                   /* slot past 10 */
        { "palette A | slot 0 | loc:a\n", 1 },                    /* slot 0 */
        { "palette A | slot 1\n", 1 },                            /* slot, no record */
        { "palette A | loc:a\n", 1 },                             /* record, no slot */
        { "palette A | slot 1 | npc:hans | shape 10 rot 0\n", 1 }, /* a pose on an npc */
        { "palette A | slot 1 | loc:a | shape 23 rot 0\n", 1 },    /* shape past 22 */
        { "palette A | slot 1 | loc:a | shape 10 rot 4\n", 1 },    /* rotation past 3 */
        { "palette A | slot 1 | loc:a | shape 10\n", 1 },          /* shape without rot */
        { "palette A | slot 1 | box:a\n", 1 },                    /* unknown kind */
        { "palette A | slot 1 | loc:two words\n", 1 },            /* a space in a record */
        { "palette | slot 1 | loc:a\n", 1 },                      /* no name */
        { "palette A | colour 3\n", 1 },                          /* unknown segment */
        { "pallet A\n", 1 },                                      /* unknown keyword */
        { "# ok\npalette A | slot 1 | loc:a | slot 2\n", 2 },     /* a segment twice */
    };

    Editor_PalettesInit(&file);
    for( size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++ )
    {
        int const line = parse(&file, k_bad[i].text);
        CHECK(line == k_bad[i].line, "refusal %zu reports line %d (got %d): %s", i, k_bad[i].line, line, k_bad[i].text);
    }
    /* A refused file still round-trips: the text is data. */
    parse(&file, "palette A | slot 99 | loc:a\nnot a record\n");
    CHECK(strcmp(emit(&file), "palette A | slot 99 | loc:a\nnot a record\n") == 0, "malformed text is kept byte for byte");
    Editor_PalettesFree(&file);
}

static void
test_format_and_rules(void)
{
    struct Editor_PalettesFile file;
    struct Editor_PaletteRow row;
    struct Editor_PaletteRow back;
    char text[EDITOR_PALETTES_LINE_MAX + 1];

    Editor_PaletteEntryRow(&row, "Lumbridge", 2, EDITOR_PALETTE_LOC, "loc_1902");
    row.shape = 0;
    row.rotation = 1;
    snprintf(row.note, sizeof(row.note), "the east wall");
    Editor_PaletteFormat(&row, text, sizeof(text));
    CHECK(strcmp(text, "palette Lumbridge | slot 2 | loc:loc_1902 | shape 0 rot 1 | note the east wall") == 0,
        "an entry's row (%s)", text);
    CHECK(Editor_PalettesParseLine(text, &back) == EDITOR_PALETTES_LINE_ENTRY && memcmp(&row, &back, sizeof(row)) == 0,
        "format -> parse gives the row back");
    Editor_PaletteHeaderRow(&row, "Lumbridge", NULL);
    Editor_PaletteFormat(&row, text, sizeof(text));
    CHECK(strcmp(text, "palette Lumbridge") == 0 && Editor_PaletteRowValid(&row), "a header's row (%s)", text);
    Editor_PaletteEntryRow(&row, "A", 1, EDITOR_PALETTE_NPC, "hans");
    row.shape = 10;
    CHECK(!Editor_PaletteRowValid(&row), "a pose on an npc entry is not valid");
    CHECK(!Editor_PaletteNameValid(" A") && !Editor_PaletteNameValid("A|B") && !Editor_PaletteNameValid("#A")
              && Editor_PaletteNameValid("Lumbridge castle"),
        "palette names");
    CHECK(Editor_PaletteKindFromName("npc") == EDITOR_PALETTE_NPC && Editor_PaletteKindFromName("seq") == -1
              && strcmp(Editor_PaletteKindName(EDITOR_PALETTE_OBJ), "obj") == 0,
        "kind names");

    /* The first-free-slot rule: the lowest empty slot; 0 when full. */
    Editor_PalettesInit(&file);
    CHECK(Editor_PalettesFirstFreeSlot(&file, "New") == 1, "an unnamed palette starts at slot 1");
    parse(&file, k_fixture);
    CHECK(Editor_PalettesFirstFreeSlot(&file, "Lumbridge") == 3, "Lumbridge's first free slot is 3 (1, 2, 4 held)");
    CHECK(Editor_PalettesFirstFreeSlot(&file, "Shops") == 1, "Shops' is 1 (3 held)");
    {
        char full[2048];
        int at = 0;
        for( int s = 1; s <= EDITOR_PALETTE_SLOTS; s++ )
            at += snprintf(full + at, sizeof(full) - (size_t)at, "palette F | slot %d | obj:o%d\n", s, s);
        parse(&file, full);
        CHECK(Editor_PalettesFirstFreeSlot(&file, "F") == 0, "a palette of ten has no free slot");
    }
    Editor_PalettesFree(&file);
}

static void
test_commands(void)
{
    struct Editor_PalettesFile file;
    struct Editor_Cmd* cmd = calloc(EDITOR_PALETTE_PLAN_MAX * 2, sizeof(*cmd));
    struct Editor_PaletteRow row;
    char label[160];
    char before[sizeof(g_emit)];
    int n;

    assert(cmd);
    Editor_PalettesInit(&file);

    /* Add into the first free slot of a hand-written file: after the
     * palette's last line, nothing else moves. */
    parse(&file, k_fixture);
    snprintf(before, sizeof(before), "%s", emit(&file));
    Editor_PaletteEntryRow(&row, "Lumbridge", Editor_PalettesFirstFreeSlot(&file, "Lumbridge"), EDITOR_PALETTE_OBJ,
        "crate");
    n = Editor_PalettePlanAdd(cmd, EDITOR_PALETTE_PLAN_MAX, &file, &row);
    CHECK(n == 1 && cmd[0].kind == EDITOR_CMD_PALETTE_SET && !cmd[0].has_before && cmd[0].doc_kind == EDITOR_DOC_PALETTES
              && strcmp(cmd[0].doc_key, "palettes") == 0,
        "an add is one SET with no before, addressed to palettes");
    Editor_PalettesCmdLabel(&cmd[0], label, sizeof(label));
    CHECK(strcmp(label, "Add crate to palette Lumbridge slot 3") == 0, "label: %s", label);
    CHECK(apply_all(&file, cmd, n, 0) == 1, "applied");
    CHECK(file.lines[7].row && strcmp(file.lines[7].text, "palette Lumbridge | slot 3 | obj:crate") == 0
              && file.lines[8].row && strcmp(file.lines[8].row->palette, "Shops") == 0,
        "it lands after Lumbridge's last line, before Shops");
    CHECK(Editor_PalettesFirstFreeSlot(&file, "Lumbridge") == 5, "the next free slot is 5");
    CHECK(apply_all(&file, cmd, n, 1) == 1 && strcmp(emit(&file), before) == 0, "undo: byte-identical");

    /* A byte mismatch refuses: a command made against other bytes. */
    parse(&file, "palette A | slot 1 | loc:oaktree\n");
    Editor_PaletteEntryRow(&row, "A", 1, EDITOR_PALETTE_LOC, "willow");
    n = Editor_PalettePlanAdd(cmd, EDITOR_PALETTE_PLAN_MAX, &file, &row);
    CHECK(n == 1 && cmd[0].has_before, "putting a record in a held slot replaces it (a SET with a before)");
    Editor_PalettesCmdLabel(&cmd[0], label, sizeof(label));
    CHECK(strcmp(label, "Replace oaktree with willow in palette A slot 1") == 0, "label: %s", label);
    parse(&file, "palette A | slot 1 | loc:oaktree  \n");
    CHECK(Editor_PalettesApply(&file, &cmd[0], 0) == 0 && strcmp(emit(&file), "palette A | slot 1 | loc:oaktree  \n") == 0,
        "the same key with other bytes refuses, nothing written");
    parse(&file, "palette A | slot 1 | loc:oaktree\n");
    CHECK(Editor_PalettesApply(&file, &cmd[0], 1) == 0, "an undo whose after is not there refuses");
    CHECK(Editor_PalettesApply(&file, &cmd[0], 0) == 1 && Editor_PalettesApply(&file, &cmd[0], 0) == 0,
        "applied once; a second time the before is gone");
    Editor_PaletteEntryRow(&row, "A", 1, EDITOR_PALETTE_LOC, "willow");
    CHECK(Editor_PaletteSetCommand(&cmd[1], &file, 0, &row) == 0, "a SET that changes nothing is not made");
    CHECK(Editor_PaletteSetCommand(&cmd[1], &file, -1, &row) == 0, "a new line on a held key is not made");

    /* A pose. */
    row.shape = 10;
    row.rotation = 3;
    CHECK(Editor_PaletteSetCommand(&cmd[1], &file, 0, &row) == 1, "set a pose");
    Editor_PalettesCmdLabel(&cmd[1], label, sizeof(label));
    CHECK(strcmp(label, "Set pose of willow in palette A slot 1 to shape 10 rot 3") == 0, "label: %s", label);

    /* Remove: the undo puts the line back at its index. */
    parse(&file, k_fixture);
    snprintf(before, sizeof(before), "%s", emit(&file));
    CHECK(Editor_PaletteRemoveCommand(&cmd[0], &file, Editor_PalettesFindEntry(&file, "Lumbridge", 2)) == 1,
        "remove slot 2");
    Editor_PalettesCmdLabel(&cmd[0], label, sizeof(label));
    CHECK(strcmp(label, "Remove loc_1902 from palette Lumbridge slot 2") == 0, "label: %s", label);
    CHECK(Editor_PalettesApply(&file, &cmd[0], 0) == 1 && Editor_PalettesEntry(&file, "Lumbridge", 2) == NULL,
        "slot 2 is empty");
    CHECK(Editor_PalettesApply(&file, &cmd[0], 1) == 1 && strcmp(emit(&file), before) == 0,
        "undo: the hand-formatted line is back where it stood, byte for byte");
    CHECK(Editor_PaletteRemoveCommand(&cmd[0], &file, 0) == 0, "a comment line is not removable");

    /* Rename moves every line of the palette (and only its lines). */
    n = Editor_PalettePlanRename(cmd, EDITOR_PALETTE_PLAN_MAX, &file, "Lumbridge", "Lumbridge castle");
    CHECK(n == 4, "rename: one SET per line of the palette (%d)", n);
    Editor_PalettesCmdLabel(&cmd[0], label, sizeof(label));
    CHECK(strcmp(label, "Rename palette Lumbridge to Lumbridge castle") == 0, "label: %s", label);
    CHECK(apply_all(&file, cmd, n, 0) == n && !Editor_PalettesHas(&file, "Lumbridge")
              && Editor_PalettesSlotCount(&file, "Lumbridge castle") == 3
              && Editor_PalettesFindHeader(&file, "Lumbridge castle") == 1 && Editor_PalettesSlotCount(&file, "Shops") == 1,
        "every line moved, Shops untouched");
    {
        const struct Editor_PaletteRow* wall = Editor_PalettesEntry(&file, "Lumbridge castle", 2);
        CHECK(wall && wall->shape == 0 && wall->rotation == 1, "the wall keeps its pose");
    }
    CHECK(apply_all(&file, cmd, n, 1) == n && strcmp(emit(&file), before) == 0, "rename undone: byte-identical");
    CHECK(Editor_PalettePlanRename(cmd, EDITOR_PALETTE_PLAN_MAX, &file, "Lumbridge", "Shops") == 0,
        "renaming onto a taken name plans nothing");
    CHECK(Editor_PalettePlanRename(cmd, EDITOR_PALETTE_PLAN_MAX, &file, "Nope", "X") == 0, "no such palette");

    /* Move into an empty slot, then a swap. */
    n = Editor_PalettePlanMove(cmd, EDITOR_PALETTE_PLAN_MAX, &file, "Lumbridge", 4, 3);
    CHECK(n == 1, "move to an empty slot is one SET");
    Editor_PalettesCmdLabel(&cmd[0], label, sizeof(label));
    CHECK(strcmp(label, "Move hans to slot 3 of palette Lumbridge") == 0, "label: %s", label);
    CHECK(apply_all(&file, cmd, n, 0) == 1 && Editor_PalettesEntry(&file, "Lumbridge", 3)
              && Editor_PalettesEntry(&file, "Lumbridge", 4) == NULL,
        "hans is in slot 3");
    CHECK(apply_all(&file, cmd, n, 1) == 1 && strcmp(emit(&file), before) == 0, "move undone");
    n = Editor_PalettePlanMove(cmd, EDITOR_PALETTE_PLAN_MAX, &file, "Lumbridge", 1, 4);
    CHECK(n == 3, "move onto a held slot swaps (3 commands, %d)", n);
    CHECK(apply_all(&file, cmd, n, 0) == 3
              && strcmp(Editor_PalettesEntry(&file, "Lumbridge", 4)->record, "oaktree") == 0
              && strcmp(Editor_PalettesEntry(&file, "Lumbridge", 1)->record, "hans") == 0,
        "oaktree and hans swapped");
    CHECK(apply_all(&file, cmd, n, 1) == 3 && strcmp(emit(&file), before) == 0, "swap undone: byte-identical");

    /* Delete takes every line, the header first. */
    n = Editor_PalettePlanDelete(cmd, EDITOR_PALETTE_PLAN_MAX, &file, "Lumbridge");
    CHECK(n == 4, "delete: one REMOVE per line (%d)", n);
    Editor_PalettesCmdLabel(&cmd[0], label, sizeof(label));
    CHECK(strcmp(label, "Delete palette Lumbridge") == 0, "label: %s", label);
    CHECK(apply_all(&file, cmd, n, 0) == n && !Editor_PalettesHas(&file, "Lumbridge") && Editor_PalettesCount(&file) == 1,
        "only Shops is left");
    CHECK(apply_all(&file, cmd, n, 1) == n && strcmp(emit(&file), before) == 0, "delete undone: byte-identical");

    Editor_PalettesFree(&file);
    free(cmd);
}

static void
test_header_lifecycle(void)
{
    struct Editor_PalettesFile file;
    struct Editor_Cmd* cmd = calloc(EDITOR_PALETTE_PLAN_MAX, sizeof(*cmd));
    struct Editor_PaletteRow row;
    char label[160];
    int n;

    assert(cmd);
    Editor_PalettesInit(&file);
    Editor_PaletteHeaderRow(&row, "Lumbridge", NULL);
    CHECK(Editor_PaletteSetCommand(&cmd[0], &file, -1, &row) == 1, "New palette into an empty file");
    Editor_PalettesCmdLabel(&cmd[0], label, sizeof(label));
    CHECK(strcmp(label, "New palette Lumbridge") == 0, "label: %s", label);
    CHECK(Editor_PalettesApply(&file, &cmd[0], 0) == 1 && file.line_count == 4 && file.final_newline
              && file.lines[0].kind == EDITOR_PALETTES_LINE_TEXT && strcmp(file.lines[3].text, "palette Lumbridge") == 0,
        "the first record brings the three-line header");
    Editor_PaletteEntryRow(&row, "Lumbridge", 1, EDITOR_PALETTE_LOC, "oaktree");
    CHECK(Editor_PalettePlanAdd(&cmd[1], 1, &file, &row) == 1 && Editor_PalettesApply(&file, &cmd[1], 0) == 1,
        "slot 1");
    Editor_PaletteEntryRow(&row, "Lumbridge", 2, EDITOR_PALETTE_NPC, "hans");
    CHECK(Editor_PalettePlanAdd(&cmd[2], 1, &file, &row) == 1 && Editor_PalettesApply(&file, &cmd[2], 0) == 1,
        "then slot 2");
    CHECK(strcmp(file.lines[4].text, "palette Lumbridge | slot 1 | loc:oaktree") == 0
              && strcmp(file.lines[5].text, "palette Lumbridge | slot 2 | npc:hans") == 0,
        "in order under the header");
    for( int i = 2; i >= 0; i-- )
        CHECK(Editor_PalettesApply(&file, &cmd[i], 1) == 1, "undo %d", i);
    CHECK(file.line_count == 0 && strcmp(emit(&file), "") == 0, "undoing every add leaves an absent file absent");

    /* A header added to a palette that only had entries goes before them. */
    parse(&file, "palette A | slot 1 | loc:a\npalette B | slot 1 | loc:b\n");
    Editor_PaletteHeaderRow(&row, "B", "bees");
    CHECK(Editor_PaletteSetCommand(&cmd[0], &file, -1, &row) == 1 && Editor_PalettesApply(&file, &cmd[0], 0) == 1
              && strcmp(emit(&file), "palette A | slot 1 | loc:a\npalette B | note bees\npalette B | slot 1 | loc:b\n") == 0,
        "the header lands before B's first line (%s)", emit(&file));

    /* Deleting the only palette of a file the editor made empties it. */
    parse(&file, "");
    Editor_PaletteEntryRow(&row, "Solo", 1, EDITOR_PALETTE_OBJ, "crate");
    n = Editor_PalettePlanAdd(cmd, 1, &file, &row);
    apply_all(&file, cmd, n, 0);
    n = Editor_PalettePlanDelete(cmd, EDITOR_PALETTE_PLAN_MAX, &file, "Solo");
    CHECK(n == 1 && apply_all(&file, cmd, n, 0) == 1 && file.line_count == 0, "deleting the last palette takes the header");
    CHECK(apply_all(&file, cmd, n, 1) == 1 && file.line_count == 4, "and its undo brings both back");
    Editor_PalettesFree(&file);
    free(cmd);
}

static void
test_wire(void)
{
    struct Editor_PalettesFile file;
    struct Editor_PaletteRow row;
    struct Editor_Cmd* cmd = calloc(2, sizeof(*cmd));

    assert(cmd);
    Editor_PalettesInit(&file);
    parse(&file, k_fixture);
    for( int k = 0; k < 2; k++ )
    {
        struct ToriRSMapEdBuf buf;
        int made;

        if( k == 0 )
        {
            Editor_PaletteEntryRow(&row, "Lumbridge", 1, EDITOR_PALETTE_LOC, "willow");
            row.shape = 10;
            made = Editor_PaletteSetCommand(&cmd[0], &file, Editor_PalettesFindEntry(&file, "Lumbridge", 1), &row);
        }
        else
            made = Editor_PaletteRemoveCommand(&cmd[0], &file, Editor_PalettesFindEntry(&file, "Lumbridge", 4));
        CHECK(made, "kind %d made", k);
        memset(&buf, 0, sizeof(buf));
        ToriRSMapEd_CmdEncode(&cmd[0], &buf);
        CHECK(ToriRSMapEd_CmdDecode(ToriRSMapEd_BufPeek(&buf), (uint32_t)ToriRSMapEd_BufAvailable(&buf), &cmd[1])
                  == TORIRSMAPED_CMD_DECODE_OK,
            "kind %d decodes", k);
        CHECK(memcmp(&cmd[0], &cmd[1], sizeof(cmd[0])) == 0, "kind %d round-trips the wire", k);
        CHECK(ToriRSMapEd_CmdDecode(ToriRSMapEd_BufPeek(&buf), (uint32_t)ToriRSMapEd_BufAvailable(&buf) - 1, &cmd[1])
                  == TORIRSMAPED_CMD_DECODE_MALFORMED,
            "kind %d truncated is malformed", k);
        ToriRSMapEd_BufFree(&buf);
    }
    Editor_PalettesFree(&file);
    free(cmd);
}

int
main(void)
{
    ToriMem_Init();
    test_round_trip();
    test_refusals();
    test_format_and_rules();
    test_commands();
    test_header_lifecycle();
    test_wire();
    printf("editor_palette_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
