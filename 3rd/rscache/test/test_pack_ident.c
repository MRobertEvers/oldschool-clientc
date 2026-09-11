/*
 * The identifier a pack line wants written into table 255.
 *
 * `put_archive` calls `cp_pack_archive_identifier`: hashcode, else djb2 of
 * hashname, else djb2 of the pack filename. This suite holds that helper to
 * those three cases, then writes the result onto a reference table the same
 * way the packer does — so a tree-only pack still has the column the client
 * looks up by name.
 */

#include "rscache_test.h"

#include "archive.h"
#include "cachepack.h"
#include "lc_pack.h"
#include "reference_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
test_hashname_becomes_djb2(void)
{
    struct LC_Pack pack;
    struct RSCache_ReferenceTable table = { 0 };
    int identifier;

    RSCACHE_TEST_GROUP("hashname identifier");
    memset(&pack, 0, sizeof(pack));
    snprintf(pack.type, sizeof(pack.type), "%s", "script");
    RSCACHE_CHECK(lc_pack_set(&pack, 1703, "world_map_element_13_option_1_1703"));
    RSCACHE_CHECK(lc_pack_set_hashname(&pack, 1703, "3338"));

    identifier = cp_pack_archive_identifier(&pack, 1703, "world_map_element_13_option_1_1703");
    RSCACHE_CHECK_EQ(identifier, RSCache_ArchiveNameHashDat2((char*)"3338"));
    RSCACHE_CHECK(identifier != RSCache_ArchiveNameHashDat2((char*)"world_map_element_13_option_1_1703"));

    table.archive_count = 1704;
    table.archives = calloc(1704, sizeof(*table.archives));
    RSCACHE_CHECK(table.archives != NULL);
    RSCache_ReferenceTableSetHasArchive(&table, 1703, true);
    RSCache_ReferenceTableSetIdentifier(&table, 1703, identifier);
    RSCACHE_CHECK_EQ(RSCache_ReferenceTableIdentifier(&table, 1703), identifier);

    free(table.identifiers);
    free(table.present);
    free(table.archives);
    lc_pack_free(&pack);
}

static void
test_hashcode_is_raw(void)
{
    struct LC_Pack pack;
    struct RSCache_ReferenceTable table = { 0 };
    int identifier;

    RSCACHE_TEST_GROUP("hashcode identifier");
    memset(&pack, 0, sizeof(pack));
    snprintf(pack.type, sizeof(pack.type), "%s", "script");
    RSCACHE_CHECK(lc_pack_set(&pack, 1, "fixture"));
    RSCACHE_CHECK(lc_pack_set_hashcode(&pack, 1, 42));

    identifier = cp_pack_archive_identifier(&pack, 1, "fixture");
    RSCACHE_CHECK_EQ(identifier, 42);

    table.archive_count = 2;
    table.archives = calloc(2, sizeof(*table.archives));
    RSCACHE_CHECK(table.archives != NULL);
    RSCache_ReferenceTableSetHasArchive(&table, 1, true);
    RSCache_ReferenceTableSetIdentifier(&table, 1, identifier);
    RSCACHE_CHECK_EQ(RSCache_ReferenceTableIdentifier(&table, 1), 42);

    free(table.identifiers);
    free(table.present);
    free(table.archives);
    lc_pack_free(&pack);
}

static void
test_pack_name_is_the_fallback(void)
{
    struct LC_Pack pack;

    RSCACHE_TEST_GROUP("pack-name fallback");
    memset(&pack, 0, sizeof(pack));
    snprintf(pack.type, sizeof(pack.type), "%s", "sprite");
    RSCACHE_CHECK(lc_pack_set(&pack, 0, "compass"));

    RSCACHE_CHECK_EQ(cp_pack_archive_identifier(&pack, 0, "compass"),
                     RSCache_ArchiveNameHashDat2((char*)"compass"));
    RSCACHE_CHECK_EQ(cp_pack_archive_identifier(NULL, 0, "compass"),
                     RSCache_ArchiveNameHashDat2((char*)"compass"));
    lc_pack_free(&pack);
}

int
main(void)
{
    test_hashname_becomes_djb2();
    test_hashcode_is_raw();
    test_pack_name_is_the_fallback();
    return rscache_test_report("pack_ident");
}
