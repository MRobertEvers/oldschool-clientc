#include "rscache_test.h"

#include "datatypes/dat2_config_npc.h"
#include "revisions/revisions.h"
#include "rscache_profile.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Npc config opcode 42: the type's recolour-all (rev 241+; rev-242 deob
 * class454.field5610, `var27 = (short) g2`; rev-241 class431 the same; the
 * rev-239 reference lists 42 among the opcodes it ignores, class405).
 *
 * No npc record in cache.osrs242 states it (all 16,892 decode exactly with
 * no opcode-42 support), so the record below is real bytes with one stated:
 * cache.osrs242 npc 154, "Treasure fairy", complete, and the same record
 * with `2a 12 34` (opcode 42, colour 0x1234) after its recolours. Before
 * opcode 42 was decoded, that record stopped at the opcode: the name and the
 * model after it were never read.
 */

/* cache.osrs242 npc 154, complete. */
static const uint8_t k_treasure_fairy[] = {
    0x5f, 0x00, 0x00,                                     /* 95 combat level 0 */
    0x0c, 0x01,                                           /* 12 size 1 */
    0x0e, 0x00, 0x6a,                                     /* 14 walk 106 */
    0x0d, 0x00, 0x70,                                     /* 13 ready 112 */
    0x28, 0x02, 0x58, 0xe1, 0xa8, 0xe7, 0x24, 0xe7, 0x00, 0x08, /* 40 two recolours */
    0x02, 'T', 'r', 'e', 'a', 's', 'u', 'r', 'e', ' ', 'f', 'a', 'i', 'r', 'y', 0x00,
    0x3d, 0x01, 0x00, 0x00, 0x0b, 0x29,                   /* 61 one int model, 2857 */
    0x00,
};

/* The same, stating opcode 42 after the recolours. */
static const uint8_t k_treasure_fairy_42[] = {
    0x5f, 0x00, 0x00,
    0x0c, 0x01,
    0x0e, 0x00, 0x6a,
    0x0d, 0x00, 0x70,
    0x28, 0x02, 0x58, 0xe1, 0xa8, 0xe7, 0x24, 0xe7, 0x00, 0x08,
    0x2a, 0x12, 0x34, /* 42 recolour-all 0x1234 */
    0x02, 'T', 'r', 'e', 'a', 's', 'u', 'r', 'e', ' ', 'f', 'a', 'i', 'r', 'y', 0x00,
    0x3d, 0x01, 0x00, 0x00, 0x0b, 0x29,
    0x00,
};

static struct RSCache_Dat2ConfigNpc*
decode(
    const char* profile_name,
    const uint8_t* bytes,
    int size)
{
    struct RSCache profile;
    char* copy = malloc((size_t)size);
    struct RSCache_Dat2ConfigNpc* npc;

    RSCACHE_CHECK(RSCache_ProfileByName(profile_name, &profile));
    RSCACHE_CHECK(copy != NULL);
    memcpy(copy, bytes, (size_t)size);
    npc = RSCache_Dat2ConfigNpcNewDecodeProfile(&profile, copy, size);
    free(copy);
    return npc;
}

static void
test_record_without_it(void)
{
    struct RSCache_Dat2ConfigNpc* npc;

    RSCACHE_TEST_GROUP("osrs242 npc 154 as the cache has it: no recolour-all");
    npc = decode("osrs242", k_treasure_fairy, (int)sizeof(k_treasure_fairy));
    RSCACHE_CHECK(npc != NULL);
    RSCACHE_CHECK_EQ(npc->_consumed, (int)sizeof(k_treasure_fairy));
    RSCACHE_CHECK_EQ(npc->recolor_all, -1);
    RSCACHE_CHECK(!RSCache_PresenceHas(&npc->present, RSCACHE_NPC_FIELD_RECOLOR_ALL));
    RSCACHE_CHECK_EQ(npc->recolor_count, 2);
    RSCache_Dat2ConfigNpcFree(npc);
}

static void
test_rev242_reads_it(void)
{
    struct RSCache_Dat2ConfigNpc* npc;
    struct RSCache profile;
    uint8_t out[256];
    uint32_t len;

    RSCACHE_TEST_GROUP("osrs242: opcode 42 is a g2, and the record reads on past it");
    npc = decode("osrs242", k_treasure_fairy_42, (int)sizeof(k_treasure_fairy_42));
    RSCACHE_CHECK(npc != NULL);
    RSCACHE_CHECK_EQ(npc->_consumed, (int)sizeof(k_treasure_fairy_42));
    RSCACHE_CHECK_EQ(npc->recolor_all, 0x1234);
    RSCACHE_CHECK(RSCache_PresenceHas(&npc->present, RSCACHE_NPC_FIELD_RECOLOR_ALL));
    RSCACHE_CHECK_EQ(npc->recolor_count, 2);
    RSCACHE_CHECK_EQ(npc->recolor_to_find[1], 0x24e7);
    RSCACHE_CHECK_STR_EQ(npc->name, "Treasure fairy");
    RSCACHE_CHECK_EQ(npc->models_count, 1);
    RSCACHE_CHECK_EQ(npc->models ? npc->models[0] : -1, 2857);

    /* The encoder writes it back, and it reads the same. */
    RSCACHE_CHECK(RSCache_ProfileByName("osrs242", &profile));
    len = RSCache_Dat2ConfigNpcEncodeProfile(&profile, npc, out, (uint32_t)sizeof(out));
    RSCache_Dat2ConfigNpcFree(npc);
    RSCACHE_CHECK(len > 0);
    npc = decode("osrs242", out, (int)len);
    RSCACHE_CHECK(npc != NULL);
    RSCACHE_CHECK_EQ(npc->_consumed, (int)len);
    RSCACHE_CHECK_EQ(npc->recolor_all, 0x1234);
    RSCACHE_CHECK_STR_EQ(npc->name, "Treasure fairy");
    RSCache_Dat2ConfigNpcFree(npc);
}

static void
test_rev239_has_no_such_opcode(void)
{
    struct RSCache_Dat2ConfigNpc* npc;

    RSCACHE_TEST_GROUP("osrs239: opcode 42 is not this revision's, and the decode stops on it");
    npc = decode("osrs239", k_treasure_fairy_42, (int)sizeof(k_treasure_fairy_42));
    RSCACHE_CHECK(npc != NULL);
    RSCACHE_CHECK(npc->_consumed != (int)sizeof(k_treasure_fairy_42));
    RSCACHE_CHECK_EQ(npc->recolor_all, -1);
    RSCache_Dat2ConfigNpcFree(npc);
}

int
main(void)
{
    test_record_without_it();
    test_rev242_reads_it();
    test_rev239_has_no_such_opcode();
    printf("npc recolour-all: %d checks, %d failures\n", rscache_test_checks, rscache_test_failures);
    return rscache_test_failures ? 1 : 0;
}
