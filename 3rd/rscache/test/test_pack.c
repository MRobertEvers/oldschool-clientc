/*
 * `pack/<ns>.pack` files: comments survive, writes merge, deletion is explicit.
 *
 * Under the one-file-per-namespace model (docs/CONTENT_PACK_PLAN.md §3) a pack
 * file is hand-owned *and* machine-written, so every property this suite checks
 * is load-bearing rather than tidy:
 *
 *   round trip   load -> save -> load reproduces the file byte for byte,
 *                comments and blank lines included. Before this existed,
 *                `pack/param.pack` lost all 58 of its header lines to a single
 *                `cachepack unpack`, and that header was the record of which
 *                param-id claims had been checked against a cache and how.
 *   merge        an id on disk that the in-memory pack never heard of survives
 *                the write. This is what lets a tool that only knows about npcs
 *                write `npc.pack` without deleting the names a human added.
 *   tombstones   ...and yet `lc_pack_remove` still removes, because a merging
 *                save that could not delete would make retiring an id
 *                impossible. The two rules only coexist because removal is
 *                recorded rather than inferred from absence.
 *   sparse       `<type>_<id>` filler is omitted (it resolves from the name
 *                alone), *unless* someone wrote a comment on it — a comment
 *                means the id was discussed, and dropping the line loses that.
 *
 * No cache and no content tree are involved: pack files are text, so this runs
 * anywhere and never skips.
 */

#include "rscache_test.h"

#include "lc_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Not `build/test_pack` — that is the test binary's own path, and mkdir would
 * lose to it. */
#ifndef RSCACHE_TEST_TMP
#define RSCACHE_TEST_TMP "build/pack_fixtures"
#endif

static void
tmp_path(
    char* out,
    size_t out_size,
    const char* leaf)
{
    mkdir(RSCACHE_TEST_TMP, 0755);
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
    if( !file )
        return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size < 0 )
    {
        fclose(file);
        return NULL;
    }
    char* data = malloc((size_t)size + 1);
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
 * The fixture, in canonical form.
 *
 * Canonical means `id=name` with no spaces around the `=` and ids ascending,
 * which is what every pack file in the tree looks like and what the writer
 * emits. Byte-identity is claimed for canonical files: a file written
 * `115 = bankcert`, or with its ids out of order, normalises on the way out, and
 * those are separate (and separately checked) properties.
 *
 * Every comment position the format has appears exactly once here, because the
 * round trip is only a real check if the fixture exercises the anchoring rules —
 * header, blank line, block before an id, trailing note, block after the last
 * id.
 */
static const char* const FIXTURE =
    "// The two params this server invented, and why.\n"
    "//\n"
    "// Checked against cache.osrs239 on 2026-07-29: it defines 0-2633, so\n"
    "// anything at or below 2633 is already spoken for.\n"
    "\n"
    "// The cache already has an attack-rate param; this is not a new one.\n"
    "14=attackrate  // cache: attackrate\n"
    "\n"
    "// Allocated off `max+1`, which is what makes the assignment permanent.\n"
    "2634=npc_hitpoints\n"
    "2635=npc_respawnrate\n"
    "\n"
    "// Nothing below here has been re-checked.\n";

static void
test_round_trip(void)
{
    char path[512];
    char again[512];

    RSCACHE_TEST_GROUP("round trip");
    tmp_path(path, sizeof(path), "roundtrip.pack");
    tmp_path(again, sizeof(again), "roundtrip.out.pack");
    RSCACHE_CHECK(write_file(path, FIXTURE));

    struct LC_Pack pack;
    RSCACHE_CHECK(lc_pack_load(&pack, path, "param", 0));
    RSCACHE_CHECK_EQ(pack.malformed, 0);

    RSCACHE_CHECK_STR_EQ(pack.names[14], "attackrate");
    RSCACHE_CHECK_STR_EQ(pack.names[2634], "npc_hitpoints");
    RSCACHE_CHECK_EQ(pack.max, 2636);

    /* The trailing note is readable as text, without the `//` or the padding. */
    RSCACHE_CHECK_STR_EQ(lc_pack_note(&pack, 14), "cache: attackrate");
    RSCACHE_CHECK(lc_pack_note(&pack, 2634) == NULL);

    /* Written to a *different* path, so this is not the merge rule quietly
     * passing the test for the round trip. */
    RSCACHE_CHECK(lc_pack_save(&pack, again));

    char* text = read_file(again);
    RSCACHE_CHECK(text != NULL);
    /* The bar: the file comes back exactly. Before this, `pack/param.pack` lost
     * all 58 of its header lines to one unpack. */
    if( text )
        RSCACHE_CHECK_STR_EQ(text, FIXTURE);
    free(text);

    struct LC_Pack reread;
    RSCACHE_CHECK(lc_pack_load(&reread, again, "param", 0));
    RSCACHE_CHECK_EQ(reread.max, pack.max);
    RSCACHE_CHECK_STR_EQ(reread.names[14], "attackrate");
    RSCACHE_CHECK_STR_EQ(lc_pack_note(&reread, 14), "cache: attackrate");
    lc_pack_free(&reread);
    lc_pack_free(&pack);
}

static void
test_sort_keeps_comments_attached(void)
{
    char path[512];

    RSCACHE_TEST_GROUP("sorting");
    tmp_path(path, sizeof(path), "sorted.pack");
    /* Ids descending, each with something written about it. Sorting on write is
     * the engine's own behaviour; the property worth checking is that a comment
     * travels with the id it was written above rather than staying where it was
     * in the file. */
    RSCACHE_CHECK(write_file(path,
                             "// about nine\n"
                             "9=guard\n"
                             "// about three\n"
                             "3=goblin\n"));

    struct LC_Pack pack;
    RSCACHE_CHECK(lc_pack_load(&pack, path, "npc", 0));
    RSCACHE_CHECK(lc_pack_save(&pack, path));

    char* text = read_file(path);
    RSCACHE_CHECK(text != NULL);
    if( text )
    {
        /* `// about nine` was the file header (it preceded the first id), so it
         * stays at the top; `// about three` follows id 3 down to its new place. */
        RSCACHE_CHECK_STR_EQ(text, "// about nine\n"
                                   "// about three\n"
                                   "3=goblin\n"
                                   "9=guard\n");
    }
    free(text);
    lc_pack_free(&pack);
}

static void
test_merge_preserves_unknown_ids(void)
{
    char path[512];

    RSCACHE_TEST_GROUP("merge");
    tmp_path(path, sizeof(path), "merge.pack");
    RSCACHE_CHECK(write_file(path,
                             "// header\n"
                             "3=goblin\n"
                             "7=guard  // renamed by this world\n"));

    /* A writer that only knows about id 3 — which is every tool that regenerates
     * one namespace from one source. Nothing here has ever heard of 7. */
    struct LC_Pack pack;
    memset(&pack, 0, sizeof(pack));
    snprintf(pack.type, sizeof(pack.type), "%s", "npc");
    RSCACHE_CHECK(lc_pack_set(&pack, 3, "goblin"));
    RSCACHE_CHECK(lc_pack_save(&pack, path));

    char* text = read_file(path);
    RSCACHE_CHECK(text != NULL);
    if( text )
    {
        RSCACHE_CHECK(strstr(text, "3=goblin\n") != NULL);
        /* The survivor, comment and all. */
        RSCACHE_CHECK(strstr(text, "7=guard  // renamed by this world\n") != NULL);
        RSCACHE_CHECK(strstr(text, "// header\n") != NULL);
    }
    free(text);
    lc_pack_free(&pack);
}

static void
test_removal_is_explicit(void)
{
    char path[512];

    RSCACHE_TEST_GROUP("removal");
    tmp_path(path, sizeof(path), "remove.pack");
    RSCACHE_CHECK(write_file(path,
                             "3=goblin\n"
                             "7=guard\n"));

    struct LC_Pack pack;
    RSCACHE_CHECK(lc_pack_load(&pack, path, "npc", 0));
    RSCACHE_CHECK(lc_pack_remove(&pack, 7));
    RSCACHE_CHECK_EQ(pack.removed, 1);
    /* max follows the file, so the next allocation cannot land past the end of
     * what was written. */
    RSCACHE_CHECK_EQ(pack.max, 4);
    RSCACHE_CHECK(lc_pack_save(&pack, path));

    char* text = read_file(path);
    RSCACHE_CHECK(text != NULL);
    if( text )
    {
        RSCACHE_CHECK(strstr(text, "3=goblin\n") != NULL);
        /* Merge must not resurrect it: the removal was recorded, and the whole
         * reason removal is recorded rather than inferred from absence is that
         * absence is what merge is built to repair. */
        RSCACHE_CHECK(strstr(text, "7=") == NULL);
    }
    free(text);
    lc_pack_free(&pack);
}

static void
test_sparse_keeps_commented_filler(void)
{
    char path[512];

    RSCACHE_TEST_GROUP("sparse");
    tmp_path(path, sizeof(path), "sparse.pack");

    struct LC_Pack pack;
    memset(&pack, 0, sizeof(pack));
    snprintf(pack.type, sizeof(pack.type), "%s", "param");
    RSCACHE_CHECK(lc_pack_set(&pack, 1, "param_1"));   /* filler, no comment */
    RSCACHE_CHECK(lc_pack_set(&pack, 2, "param_2"));   /* filler, but discussed */
    RSCACHE_CHECK(lc_pack_set_note(&pack, 2, "unnamed, but the bank reads it"));
    RSCACHE_CHECK(lc_pack_set(&pack, 3, "stabattack")); /* a real name */
    RSCACHE_CHECK(lc_pack_save_sparse(&pack, path));

    char* text = read_file(path);
    RSCACHE_CHECK(text != NULL);
    if( text )
    {
        RSCACHE_CHECK(strstr(text, "1=") == NULL);
        RSCACHE_CHECK(strstr(text, "2=param_2  // unnamed, but the bank reads it\n") != NULL);
        RSCACHE_CHECK(strstr(text, "3=stabattack\n") != NULL);
    }
    free(text);
    lc_pack_free(&pack);
}

static void
test_malformed_lines_are_counted(void)
{
    char path[512];

    RSCACHE_TEST_GROUP("malformed");
    tmp_path(path, sizeof(path), "malformed.pack");
    RSCACHE_CHECK(write_file(path,
                             "3=goblin\n"
                             "this line is neither\n"
                             "=nameless\n"
                             "9=guard\n"));

    struct LC_Pack pack;
    RSCACHE_CHECK(lc_pack_load(&pack, path, "npc", 0));
    /* Skipped, as the engine's own loader skips them — but counted, so a pack
     * that is being silently thinned says so. */
    RSCACHE_CHECK_EQ(pack.malformed, 2);
    RSCACHE_CHECK_STR_EQ(pack.names[3], "goblin");
    RSCACHE_CHECK_STR_EQ(pack.names[9], "guard");
    lc_pack_free(&pack);
}

static void
test_spacing_normalises(void)
{
    char path[512];

    RSCACHE_TEST_GROUP("spacing");
    tmp_path(path, sizeof(path), "spacing.pack");
    RSCACHE_CHECK(write_file(path, "115 = bankcert\n"));

    struct LC_Pack pack;
    RSCACHE_CHECK(lc_pack_load(&pack, path, "varp", 0));
    /* Whitespace around the `=` is not part of the name. It used to be, which
     * made `bankcert` unresolvable in a file that looked correct. */
    RSCACHE_CHECK_STR_EQ(pack.names[115], "bankcert");
    RSCACHE_CHECK_EQ(lc_pack_find(&pack, "bankcert"), 115);
    RSCACHE_CHECK(lc_pack_save(&pack, path));

    char* text = read_file(path);
    RSCACHE_CHECK(text != NULL);
    if( text )
        RSCACHE_CHECK_STR_EQ(text, "115=bankcert\n");
    free(text);
    lc_pack_free(&pack);
}

int
main(void)
{
    test_round_trip();
    test_sort_keeps_comments_attached();
    test_merge_preserves_unknown_ids();
    test_removal_is_explicit();
    test_sparse_keeps_commented_filler();
    test_malformed_lines_are_counted();
    test_spacing_normalises();
    return rscache_test_report("pack");
}
