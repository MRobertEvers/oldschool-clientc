#include "rscache_test.h"

#include "datatypes/dat2_config_obj.h"
#include "datatypes/dat2_config_sequence.h"
#include "revisions/revisions.h"

#include <string.h>

static struct RSCache
rs530_profile(void)
{
    return RSCache_ProfileForIdentity(
        RSCACHE_GAME_RS2, RSCACHE_EPOCH_DAT2, 530, RSCACHE_QUIRK_NONE);
}

static void
test_sequence_530_changed_opcodes(void)
{
    RSCACHE_TEST_GROUP("sequence 530 opcodes 13/14");

    /* opcode 13: two frame slots; frame 0 has two sounds. The public neutral
     * shape retains the first sound and must still consume the second u16 id. */
    unsigned char record[] = {
        13, 0, 2,
        2, 0x01, 0x23, 0x23, 0xAB, 0xCD,
        0,
        14,
        0
    };
    struct RSCache profile = rs530_profile();
    struct RSCache_Dat2ConfigSequence sequence;
    memset(&sequence, 0, sizeof(sequence));
    RSCache_Dat2ConfigSequenceDecodeProfile(
        &sequence, &profile, (char*)record, (int)sizeof(record));

    RSCACHE_CHECK_EQ(sequence._consumed, sizeof(record));
    RSCACHE_CHECK_EQ(sequence.frame_sounds.count, 1);
    RSCACHE_CHECK_EQ(sequence.frame_sounds.frames[0], 0);
    RSCACHE_CHECK_EQ(sequence.frame_sounds.sounds[0].id, 0x123);
    RSCACHE_CHECK_EQ(sequence.frame_sounds.sounds[0].loops, 2);
    RSCACHE_CHECK_EQ(sequence.frame_sounds.sounds[0].location, 3);
    RSCACHE_CHECK(sequence.rs2_530_sound_flag);

    RSCache_Dat2ConfigSequenceFreeInplace(&sequence);
}

static void
test_obj_530_changed_opcodes(void)
{
    RSCACHE_TEST_GROUP("obj 530 changed opcodes");

    unsigned char record[] = {
        23, 0x12, 0x34,
        24, 0x23, 0x45,
        25, 0x34, 0x56,
        26, 0x45, 0x67,
        42, 3, 0xAA, 0xBB, 0xCC,
        96, 0xFE,
        121, 0x56, 0x78,
        122, 0x67, 0x89,
        125, 1, 2, 3,
        126, 4, 5, 6,
        127, 7, 0x70, 0x01,
        128, 8, 0x70, 0x02,
        129, 9, 0x70, 0x03,
        130, 10, 0x70, 0x04,
        0
    };
    struct RSCache profile = rs530_profile();
    struct RSCache_Dat2ConfigObj* obj = RSCache_Dat2ConfigObjNewDecodeProfile(
        &profile, (char*)record, (int)sizeof(record));

    RSCACHE_CHECK(obj != NULL);
    RSCACHE_CHECK_EQ(obj->_consumed, sizeof(record));
    RSCACHE_CHECK_EQ(obj->male_model_0, 0x1234);
    RSCACHE_CHECK_EQ(obj->male_model_1, 0x2345);
    RSCACHE_CHECK_EQ(obj->female_model_0, 0x3456);
    RSCACHE_CHECK_EQ(obj->female_model_1, 0x4567);
    RSCACHE_CHECK_EQ(obj->item_type, -2);
    RSCACHE_CHECK_EQ(obj->lend_id, 0x5678);
    RSCACHE_CHECK_EQ(obj->lend_template_id, 0x6789);

    RSCache_Dat2ConfigObjFree(obj);
}

int
main(void)
{
    printf("RS2 rev-530 config codec tests\n");
    test_sequence_530_changed_opcodes();
    test_obj_530_changed_opcodes();
    RSCACHE_CHECK(rscache_test_checks > 0);
    return rscache_test_report("rs530 codec");
}
