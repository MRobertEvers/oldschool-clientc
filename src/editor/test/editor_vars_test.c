/**
 * Varps, varbits and their names (editor_vars.h).
 *
 *   editor_vars_test <content-dir>
 *
 * Names: varp<id>_<name> / varb<id>_<name> are coined and read back, and a
 * name is never prefixed twice. The bit map, the overlap check, the free-range
 * search and the new-varbit commands on a small index, the commands applied
 * to a CFG document. Then the real tree: every varbit in configs/all.varbit
 * and the server overlays is read, every basevar resolves, a known varp's map
 * is right, an overlay field beats the cache's, and the overlap pairs the
 * cache really has are counted and listed (they are the cache's, so they are
 * reported, not failed).
 *
 * Read-only over the content tree.
 */

#include "editor/editor_cmd.h"
#include "editor/editor_doc.h"
#include "editor/editor_vars.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    g_checks++;
    if( !condition )
    {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

static void
add_varps(
    struct Editor_VarsIndex* index,
    const char* text)
{
    Editor_VarsAddVarps(index, text, strlen(text));
}

static void
add_varbits(
    struct Editor_VarsIndex* index,
    const char* text)
{
    Editor_VarsAddVarbits(index, text, strlen(text));
}

static void
test_names(void)
{
    char out[EDITOR_CMD_NAME_MAX];
    char base[EDITOR_CMD_NAME_MAX];
    enum Editor_VarKind kind;
    int id;

    check(Editor_VarsName(EDITOR_VAR_VARP, 29, "cookquest", out, sizeof(out))
              && strcmp(out, "varp29_cookquest") == 0,
        "varp<id>_<name>");
    check(Editor_VarsName(EDITOR_VAR_VARBIT, 346, "golem_a", out, sizeof(out))
              && strcmp(out, "varb346_golem_a") == 0,
        "varb<id>_<name>");
    check(Editor_VarsName(EDITOR_VAR_VARC, 181, "meslayermode", out, sizeof(out))
              && strcmp(out, "varc181_meslayermode") == 0,
        "varc<id>_<name>");
    check(Editor_VarsName(EDITOR_VAR_VARP, 29, "varp29_cookquest", out, sizeof(out))
              && strcmp(out, "varp29_cookquest") == 0,
        "a name already carrying its prefix is not prefixed twice");
    check(Editor_VarsName(EDITOR_VAR_VARP, 29, "varp30_other", out, sizeof(out))
              && strcmp(out, "varp29_varp30_other") == 0,
        "a prefix with another id is part of the name (as var_prefix_names.py)");
    check(!Editor_VarsName(EDITOR_VAR_VARP, -1, "x", out, sizeof(out)), "a negative id is refused");
    check(!Editor_VarsName(EDITOR_VAR_VARP, 1, "", out, sizeof(out)), "an empty name is refused");
    check(!Editor_VarsName(EDITOR_VAR_VARP, 1, "name", out, 6), "a name that does not fit is refused");

    check(Editor_VarsNameParse("varb10_pyre_loc", &kind, &id, base, sizeof(base))
              && kind == EDITOR_VAR_VARBIT && id == 10 && strcmp(base, "pyre_loc") == 0,
        "a prefixed name reads back");
    check(Editor_VarsNameParse("varp12_3_x", &kind, &id, base, sizeof(base)) && id == 12
              && strcmp(base, "3_x") == 0,
        "the id is the digits before the first '_'");
    check(!Editor_VarsNameParse("varp_x", &kind, &id, base, sizeof(base)), "no digits, no prefix");
    check(!Editor_VarsNameParse("varp12_", &kind, &id, base, sizeof(base)), "no rest, no prefix");
    check(!Editor_VarsNameParse("cookquest", &kind, &id, base, sizeof(base)), "a bare name");
}

static struct Editor_Doc g_doc;

static void
test_small_index(void)
{
    struct Editor_VarsIndex index;
    struct Editor_VarpBits bits;
    struct Editor_Cmd commands[4];
    int conflict = -1;

    Editor_VarsIndexInit(&index);
    add_varps(&index, "100=varp100_quest\n101=varp101_other // note\n[varp102_block]\nx=1\n");
    add_varbits(&index,
        "5=unprefixed_member\n"
        "[varb1_low]\nbasevar=varp100_quest\nstartbit=0\nendbit=3\n\n"
        "[varb2_high]\nbasevar=varp100_quest\nstartbit=8\nendbit=15\n\n"
        "[varb3_clash]\nbasevar=varp100_quest\nstartbit=12\nendbit=13\n\n"
        "[unprefixed_member]\nbasevar=varp101_other\nstartbit=0\nendbit=31\n\n"
        "[varb6_bare_base]\nbasevar=quest\nstartbit=20\nendbit=20\n\n"
        "[varb7_unplaced]\nbasevar=varp100_quest\nstartbit=default\nendbit=default\n");
    /* An overlay restates one field of a cache record. */
    add_varbits(&index, "[varb2_high]\nendbit=10\n");
    Editor_VarsIndexFinish(&index);

    check(index.varbit_count == 6, "six varbit records");
    check(Editor_VarsVarpId(&index, "varp101_other") == 101, "a member line names a varp");
    check(Editor_VarsVarpId(&index, "varp102_block") == 102, "a header names a varp");
    check(Editor_VarsVarpId(&index, "quest") == 100, "a bare name finds its prefixed varp");
    check(Editor_VarsVarpId(&index, "nothing") == -1, "an unknown varp is -1");
    {
        int found = Editor_VarsVarbitFind(&index, "unprefixed_member");
        check(found >= 0 && index.varbits[found].id == 5, "an unprefixed varbit takes its member id");
        found = Editor_VarsVarbitFind(&index, "varb2_high");
        check(found >= 0 && index.varbits[found].startbit == 8 && index.varbits[found].endbit == 10,
            "the overlay's endbit beats the cache's, its unstated fields stay");
        found = Editor_VarsVarbitFind(&index, "varb6_bare_base");
        check(found >= 0 && index.varbits[found].basevar == 100, "a bare basevar resolves");
    }

    check(Editor_VarsBitMap(&index, 100, &bits) == 4, "four varbits placed on varp 100");
    check(bits.used == (0xFu | (0x7u << 8) | (0x3u << 12) | (1u << 20)) && bits.overlapped == 0,
        "after the overlay, varp 100 has no overlap");
    check(Editor_VarsBitMap(&index, 101, &bits) == 1 && bits.used == 0xFFFFFFFFu,
        "a whole-varp varbit covers all 32 bits");

    check(Editor_VarsRangeFree(&index, 100, 4, 7, -1, &conflict) == 1, "bits 4..7 are free");
    check(Editor_VarsRangeFree(&index, 100, 2, 5, -1, &conflict) == 0
              && conflict == Editor_VarsVarbitFind(&index, "varb1_low"),
        "bits 2..5 clash with varb1_low");
    check(Editor_VarsRangeFree(&index, 100, 0, 3, Editor_VarsVarbitFind(&index, "varb1_low"), NULL)
              == 1,
        "a varbit's own range is free to it");
    check(Editor_VarsRangeFree(&index, 100, 5, 4, -1, NULL) == -1, "start past end is not a range");
    check(Editor_VarsRangeFree(&index, 100, 30, 32, -1, NULL) == -1, "bit 32 is not a bit");
    check(Editor_VarsFindFree(&index, 100, 4) == 4, "the lowest four free bits start at 4");
    check(Editor_VarsFindFree(&index, 100, 9) == 21, "nine free bits are found past bit 20");
    check(Editor_VarsFindFree(&index, 100, 12) == -1, "no run of twelve free bits");
    check(Editor_VarsFindFree(&index, 101, 1) == -1, "a full varp has no free bit");

    check(Editor_VarsEmitVarbit(&index, "varb9_new", 100, 2, 5, commands, 4) == 0,
        "a new varbit over a used bit is refused");
    check(Editor_VarsEmitVarbit(&index, "varb9_new", 999, 0, 0, commands, 4) == 0,
        "a new varbit on an unknown varp is refused");
    check(Editor_VarsEmitVarbit(&index, "varb9_new", 100, 4, 7, commands, 4) == 3,
        "a new varbit on free bits emits three fields");

    /* The commands land on the record a CFG_BLOCK add opened. */
    {
        struct Editor_DocHandle handle;
        struct Editor_Cmd block;
        int applied = 0;

        memset(&g_doc, 0, sizeof(g_doc));
        Editor_DocInit(&g_doc, NULL);
        check(Editor_DocOpen(&g_doc, EDITOR_DOC_CFG, "varbit:varb9_new", &handle) && handle.text,
            "the new varbit's CFG document opens");
        if( handle.text )
        {
            Editor_TextDocSet(handle.text, "", 0);
            Editor_CmdInit(&block, EDITOR_DOC_CFG, "varbit:varb9_new", EDITOR_CMD_CFG_BLOCK);
            block.has_after = 1;
            snprintf(block.u.cfg_block.name, sizeof(block.u.cfg_block.name), "varb9_new");
            applied += Editor_CmdApply(&g_doc, &block, EDITOR_CMD_FORWARD);
            for( int i = 0; i < 3; i++ )
                applied += Editor_CmdApply(&g_doc, &commands[i], EDITOR_CMD_FORWARD);
            check(applied == 4, "the record and its three fields apply");
            check(strcmp(handle.text->text,
                      "[varb9_new]\nbasevar=varp100_quest\nstartbit=4\nendbit=7\n")
                      == 0,
                "the new varbit's text");
        }
        Editor_DocFree(&g_doc);
    }

    /* Put the clash back: the overlap check finds it. */
    add_varbits(&index, "[varb2_high]\nendbit=15\n");
    Editor_VarsIndexFinish(&index);
    check(Editor_VarsBitMap(&index, 100, &bits) == 4 && bits.overlapped == (0x3u << 12),
        "bits 12..13 are flagged shared");
    check(Editor_VarsOverlaps(&index, NULL, NULL) == 1, "one overlapping pair");

    Editor_VarsIndexFree(&index);
}

struct Listing
{
    const struct Editor_VarsIndex* index;
    int shown;
};

static void
list_pair(
    void* user_data,
    int first,
    int second)
{
    struct Listing* listing = user_data;
    const struct Editor_Varbit* a = &listing->index->varbits[first];
    const struct Editor_Varbit* b = &listing->index->varbits[second];

    if( listing->shown++ < 8 )
        printf("    varp %d: %s [%d..%d] and %s [%d..%d]\n",
            a->basevar,
            a->name,
            a->startbit,
            a->endbit,
            b->name,
            b->startbit,
            b->endbit);
}

static void
test_tree(const char* content_dir)
{
    struct Editor_VarsIndex index;
    struct Editor_VarpBits bits;
    struct Listing listing;
    clock_t began = clock();
    int unresolved = 0;
    int unplaced = 0;
    int varps_used = 0;
    int last_varp = -1;
    int pairs;

    Editor_VarsIndexInit(&index);
    check(Editor_VarsIndexLoad(&index, content_dir), "the tree's varbits load");

    for( int i = 0; i < index.varbit_count; i++ )
    {
        if( index.varbits[i].basevar < 0 )
            unresolved++;
        if( index.varbits[i].startbit < 0 || index.varbits[i].endbit < 0 )
            unplaced++;
    }
    for( int i = 0; i < index.order_count; i++ )
    {
        int varp = index.varbits[index.order[i]].basevar;
        if( varp != last_varp )
            varps_used++;
        last_varp = varp;
    }
    listing.index = &index;
    listing.shown = 0;
    printf("  tree (%.2f s): %d varbits, %d placed over %d varps, %d varp names, "
           "%d unresolved basevars, %d without bits\n",
        (double)(clock() - began) / CLOCKS_PER_SEC,
        index.varbit_count,
        index.order_count,
        varps_used,
        index.varp_count,
        unresolved,
        unplaced);
    pairs = Editor_VarsOverlaps(&index, list_pair, &listing);
    printf("  overlapping varbit pairs in the tree: %d%s\n", pairs, pairs > 8 ? " (first 8 shown)" : "");

    check(index.varbit_count == 19634, "every all.varbit record was read, long names included");
    check(unresolved == 0, "every basevar resolves to a varp");
    check(index.order_count == index.varbit_count - unplaced, "every varbit with bits is placed");

    {
        int found = Editor_VarsVarbitFind(&index, "varb10_pyre_loc");
        check(found >= 0 && index.varbits[found].basevar == 340 && index.varbits[found].startbit == 9
                  && index.varbits[found].endbit == 13,
            "varb10_pyre_loc is varp 340 bits 9..13 (the overlay agrees with the cache)");
    }
    Editor_VarsBitMap(&index, 318, &bits);
    check((bits.used & 0x3Fu) == 0x3Fu, "varp 318 carries the troll varbits on bits 0..5");
    check(Editor_VarsRangeFree(&index, 318, 0, 0, -1, NULL) == 0, "bit 0 of varp 318 is taken");

    Editor_VarsIndexFree(&index);
}

int
main(
    int argc,
    char** argv)
{
    test_names();
    test_small_index();
    if( argc > 1 )
        test_tree(argv[1]);
    printf("editor_vars_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
