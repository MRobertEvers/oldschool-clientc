/*
 * `pack/<ns>.client` and `pack/<ns>.server`: names round-trip, prose survives,
 * and a set that is stated twice says so.
 *
 * These files are the on-disk statement of which *entities* have a half on each
 * side (docs/PACK_ENTITY_SPLIT_PLAN.md), and they are authored: a name moves
 * sides because a person decided it should, and the justification lives beside
 * it as a comment. So the properties here are the same load-bearing ones
 * `test_pack.c` checks for `id=name` files, one column narrower:
 *
 *   round trip   load -> save reproduces the file byte for byte, header, blank
 *                lines, per-name notes and trailing block included.
 *                `pack/param.pack` lost all 58 of its header lines to a writer
 *                that emitted only the data, and that header was the record of
 *                which claims had been checked and how.
 *   sorted       there is no id to sort by, so the name is the order. Two
 *                emissions of the same set must be byte-identical or a review
 *                diff stops being the set difference.
 *   duplicates   a set stating a name twice is an editing mistake with no
 *                correct resolution, so it is counted rather than collapsed.
 *   malformed    a line that is neither prose nor a name is skipped *and*
 *                reported, so a thinned file is visible.
 *
 * No cache and no content tree: these are text files, so this never skips.
 */

#include "rscache_test.h"

#include "cp_membership.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tool_posix_compat.h"

#ifndef RSCACHE_TEST_TMP
#define RSCACHE_TEST_TMP "build/membership_fixtures"
#endif

static void
tmp_path(
    char* out,
    size_t out_size,
    const char* leaf)
{
    tool_mkdir(RSCACHE_TEST_TMP);
    snprintf(out, out_size, "%s/%s", RSCACHE_TEST_TMP, leaf);
}

static int
write_file(
    const char* path,
    const char* text)
{
    FILE* file = fopen(path, "wb");

    if( !file )
        return 0;
    fwrite(text, 1, strlen(text), file);
    fclose(file);
    return 1;
}

/** Whole file as a NUL-terminated string, or NULL. Caller frees. */
static char*
read_file(const char* path)
{
    FILE* file = fopen(path, "rb");
    long size;
    char* data;

    if( !file )
        return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size < 0 )
    {
        fclose(file);
        return NULL;
    }
    data = malloc((size_t)size + 1);
    if( !data )
    {
        fclose(file);
        return NULL;
    }
    if( size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        free(data);
        fclose(file);
        return NULL;
    }
    data[size] = '\0';
    fclose(file);
    return data;
}

/*
 * The fixture, in canonical form: sorted by name, no padding around a name, and
 * every comment position the format has appearing exactly once — header, blank
 * line, a block before a name, a trailing note, and a block after the last name.
 * The round trip is only a real check if the fixture exercises the anchoring
 * rules.
 */
static const char* const FIXTURE =
    "// Which `npc` entities have a server half.\n"
    "//\n"
    "// Names, never ids.\n"
    "\n"
    "cow\n"
    "// Hans wanders and the wander range is a server field.\n"
    "hans  // and this one was checked against the cache\n"
    "man\n"
    "\n"
    "// Nothing follows, and this block anchors to no name.\n";

static void
test_round_trip(void)
{
    char path[512];
    struct CP_Membership set;
    char* text;

    tmp_path(path, sizeof(path), "npc.server");
    RSCACHE_CHECK(write_file(path, FIXTURE));

    RSCACHE_CHECK(cp_membership_load(&set, path, "npc", CP_MEMBERSHIP_SERVER, 0));
    RSCACHE_CHECK_EQ(set.count, 3);
    RSCACHE_CHECK_EQ(set.malformed, 0);
    RSCACHE_CHECK_EQ(set.duplicates, 0);
    RSCACHE_CHECK(cp_membership_has(&set, "hans"));
    RSCACHE_CHECK(!cp_membership_has(&set, "goblin"));

    RSCACHE_CHECK(cp_membership_save(&set, path));
    text = read_file(path);
    RSCACHE_CHECK(text != NULL);
    if( text )
        RSCACHE_CHECK_STR_EQ(text, FIXTURE);
    free(text);
    cp_membership_free(&set);
}

/*
 * Insertion order must not reach the file.
 *
 * The seeder adds names in merge order — which is walk order, which is
 * `readdir` order one level down — so a writer that preserved insertion order
 * would emit a different file on a different machine for identical content, and
 * the byte-diff that step 3 of the migration rests on would be worthless.
 */
static void
test_order_is_the_name(void)
{
    char path[512];
    struct CP_Membership set;
    char* text;

    cp_membership_init(&set, "loc", CP_MEMBERSHIP_SERVER);
    RSCACHE_CHECK_EQ(cp_membership_add(&set, "zebra"), 1);
    RSCACHE_CHECK_EQ(cp_membership_add(&set, "apple"), 1);
    RSCACHE_CHECK_EQ(cp_membership_add(&set, "mango"), 1);
    /* Already listed is a normal answer, not a failure: the seeder asks both
     * gates and both may name the same record. */
    RSCACHE_CHECK_EQ(cp_membership_add(&set, "apple"), 0);
    RSCACHE_CHECK_EQ(set.count, 3);

    tmp_path(path, sizeof(path), "loc.server");
    RSCACHE_CHECK(cp_membership_save(&set, path));
    text = read_file(path);
    RSCACHE_CHECK(text != NULL);
    if( text )
        RSCACHE_CHECK_STR_EQ(text, "apple\nmango\nzebra\n");
    free(text);
    cp_membership_free(&set);
}

/** A comment stays with the name it was written above, wherever that name
 *  sorts. */
static void
test_comments_follow_their_name(void)
{
    char path[512];
    struct CP_Membership set;
    char* text;

    tmp_path(path, sizeof(path), "enum.server");
    RSCACHE_CHECK(write_file(path, "zebra\n// about apple\napple  // and a note\n"));
    RSCACHE_CHECK(cp_membership_load(&set, path, "enum", CP_MEMBERSHIP_SERVER, 0));
    RSCACHE_CHECK(cp_membership_save(&set, path));

    text = read_file(path);
    RSCACHE_CHECK(text != NULL);
    if( text )
        RSCACHE_CHECK_STR_EQ(text, "// about apple\napple  // and a note\nzebra\n");
    free(text);
    cp_membership_free(&set);
}

/*
 * A name stated twice is counted, not collapsed.
 *
 * The file is a set, so the two lines cannot disagree about membership — but
 * they can disagree about *what someone wrote next to the name*, and silently
 * keeping one note over the other would be choosing an answer with no basis.
 */
static void
test_duplicates_are_counted(void)
{
    char path[512];
    struct CP_Membership set;

    tmp_path(path, sizeof(path), "dup.server");
    RSCACHE_CHECK(write_file(path, "cow\nman\ncow  // moved here last week\n"));
    RSCACHE_CHECK(cp_membership_load(&set, path, "npc", CP_MEMBERSHIP_SERVER, 0));
    RSCACHE_CHECK_EQ(set.count, 2);
    RSCACHE_CHECK_EQ(set.duplicates, 1);
    cp_membership_free(&set);
}

/*
 * A line the format does not accept is skipped *and* counted.
 *
 * `12=goblin` is the shape that matters: someone reaching for the `id=name`
 * habit of every other file in `pack/`. Binding it would put an entity called
 * "12=goblin" in the set, which nothing would ever resolve; reporting it is what
 * makes the mistake visible on the first run.
 */
static void
test_malformed_lines_are_counted(void)
{
    char path[512];
    struct CP_Membership set;

    tmp_path(path, sizeof(path), "bad.client");
    RSCACHE_CHECK(write_file(path, "cow\n12=goblin\nnot a name\nman\n"));
    RSCACHE_CHECK(cp_membership_load(&set, path, "npc", CP_MEMBERSHIP_CLIENT, 0));
    RSCACHE_CHECK_EQ(set.count, 2);
    RSCACHE_CHECK_EQ(set.malformed, 2);
    RSCACHE_CHECK(cp_membership_has(&set, "cow"));
    RSCACHE_CHECK(!cp_membership_has(&set, "goblin"));
    cp_membership_free(&set);
}

/*
 * A file with a header and no names is a real state and keeps its prose.
 *
 * Five of the ten seeded files are exactly this — "the tree adds no npc to the
 * client" — and it is a statement, not an empty file. `lc_pack_save_sparse`
 * deletes a file with nothing left in it; this must not, or the difference
 * between "stated, and empty" and "never stated" would be lost.
 */
static void
test_empty_with_header_survives(void)
{
    char path[512];
    struct CP_Membership set;
    char* text;

    tmp_path(path, sizeof(path), "empty.client");
    RSCACHE_CHECK(write_file(path, "// The tree adds no npc to the client.\n"));
    RSCACHE_CHECK(cp_membership_load(&set, path, "npc", CP_MEMBERSHIP_CLIENT, 0));
    RSCACHE_CHECK_EQ(set.count, 0);
    RSCACHE_CHECK(set.preamble != NULL);
    RSCACHE_CHECK(cp_membership_save(&set, path));
    text = read_file(path);
    RSCACHE_CHECK(text != NULL);
    if( text )
        RSCACHE_CHECK_STR_EQ(text, "// The tree adds no npc to the client.\n");
    free(text);
    cp_membership_free(&set);
}

/** A namespace with no membership stated loads as an empty set rather than a
 *  failure — that absence is how fifteen of the twenty config types read. */
static void
test_missing_file_is_empty(void)
{
    struct CP_Membership set;

    RSCACHE_CHECK(cp_membership_load(&set, RSCACHE_TEST_TMP "/nonesuch.server", "seq",
                                     CP_MEMBERSHIP_SERVER, 1));
    RSCACHE_CHECK_EQ(set.count, 0);
    RSCACHE_CHECK(!cp_membership_has(&set, "anything"));
    cp_membership_free(&set);

    RSCACHE_CHECK(!cp_membership_load(&set, RSCACHE_TEST_TMP "/nonesuch.server", "seq",
                                      CP_MEMBERSHIP_SERVER, 0));
}

/**
 * An **empty** file and an **absent** file are different statements, and the
 * routing gate acts on the difference.
 *
 * `npc.client` is empty because the tree adds no npc the client is told about —
 * both halves of a pair are written even when one is empty, for exactly that
 * reason. No file at all means nobody has said anything, and there the packer
 * keeps the gate it used before membership existed. A reader that could not tell
 * the two apart would silently delete every server band in a tree that had not
 * been seeded (`cp_pack.c`'s `routing_server_member`).
 */
static void
test_present_separates_empty_from_absent(void)
{
    struct CP_Membership set;
    char path[512];

    tmp_path(path, sizeof(path), "presence.client");
    RSCACHE_CHECK(write_file(path, "// Nothing here, and that is the statement.\n"));
    RSCACHE_CHECK(cp_membership_load(&set, path, "npc", CP_MEMBERSHIP_CLIENT, 1));
    RSCACHE_CHECK_EQ(set.count, 0);
    RSCACHE_CHECK_EQ(set.present, 1);
    cp_membership_free(&set);

    RSCACHE_CHECK(cp_membership_load(&set, RSCACHE_TEST_TMP "/nonesuch.client", "npc",
                                     CP_MEMBERSHIP_CLIENT, 1));
    RSCACHE_CHECK_EQ(set.count, 0);
    RSCACHE_CHECK_EQ(set.present, 0);
    cp_membership_free(&set);
}

/** The extension is the side, so the two cannot be spelled differently in two
 *  places. */
static void
test_path_and_side_agree(void)
{
    char path[512];

    RSCACHE_CHECK_STR_EQ(cp_membership_side_name(CP_MEMBERSHIP_CLIENT), "client");
    RSCACHE_CHECK_STR_EQ(cp_membership_side_name(CP_MEMBERSHIP_SERVER), "server");
    cp_membership_path(path, sizeof(path), "tree", "npc", CP_MEMBERSHIP_SERVER);
    RSCACHE_CHECK_STR_EQ(path, "tree/pack/npc.server");
}

int
main(void)
{
    test_round_trip();
    test_order_is_the_name();
    test_comments_follow_their_name();
    test_duplicates_are_counted();
    test_malformed_lines_are_counted();
    test_empty_with_header_survives();
    test_missing_file_is_empty();
    test_present_separates_empty_from_absent();
    test_path_and_side_agree();
    return rscache_test_report("membership");
}
