#include "rscache_test.h"

#include "datatypes/dat2_config_loc.h"
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
test_loc_530_opcode_95_has_no_payload(void)
{
    RSCACHE_TEST_GROUP("loc 530 opcode 95 has no payload");

    /* Exact loc 10836 bytes.  Opcode 95 is immediately followed by opcode 5;
     * consuming a g2 here turns the model bytes into a false opcode. */
    unsigned char record[] = { 19, 0, 95, 0x05, 0x01, 0x89, 0x93, 0 };
    struct RSCache profile = rs530_profile();
    struct RSCache_Dat2ConfigLoc* loc = RSCache_Dat2ConfigLocNewDecodeProfile(
        &profile, (char*)record, (int)sizeof(record));

    RSCACHE_CHECK(loc != NULL);
    RSCACHE_CHECK_EQ(loc->_consumed, sizeof(record));
    RSCACHE_CHECK_EQ(loc->contour_ground_type, 5);
    RSCACHE_CHECK_EQ(loc->shapes_and_model_count, 1);
    RSCACHE_CHECK_EQ(loc->lengths[0], 1);
    RSCACHE_CHECK_EQ(loc->models[0][0], 35219);

    RSCache_Dat2ConfigLocFree(loc);
}

static void
test_loc_530_obelisk_model_list(void)
{
    RSCACHE_TEST_GROUP("loc 530 flat opcode-5 model list");

    /* Exact loc 28716 bytes from the rev-530 source cache.  Opcode 5 is
     * `u8 count, count x u16 model`, not the nested rev-643 form. */
    unsigned char record[] = {
        15, 2, 14, 2, 29, 10, 39, 84, 24, 0x21, 0x3e,
        30, 'I', 'n', 'f', 'u', 's', 'e', '-', 'p', 'o', 'u', 'c', 'h', 0,
        31, 'R', 'e', 'n', 'e', 'w', '-', 'p', 'o', 'i', 'n', 't', 's', 0,
        5, 1, 0x7b, 0xc6,
        2, 'O', 'b', 'e', 'l', 'i', 's', 'k', 0,
        0
    };
    struct RSCache profile = rs530_profile();
    struct RSCache_Dat2ConfigLoc* loc = RSCache_Dat2ConfigLocNewDecodeProfile(
        &profile, (char*)record, (int)sizeof(record));

    RSCACHE_CHECK(loc != NULL);
    RSCACHE_CHECK_EQ(loc->_consumed, sizeof(record));
    RSCACHE_CHECK(loc->name && strcmp(loc->name, "Obelisk") == 0);
    RSCACHE_CHECK_EQ(loc->size_x, 2);
    RSCACHE_CHECK_EQ(loc->size_z, 2);
    RSCACHE_CHECK_EQ(loc->seq_id, 8510);
    RSCACHE_CHECK_EQ(loc->shapes_and_model_count, 1);
    RSCACHE_CHECK(loc->shapes == NULL);
    RSCACHE_CHECK(loc->lengths != NULL);
    RSCACHE_CHECK_EQ(loc->lengths[0], 1);
    RSCACHE_CHECK_EQ(loc->models[0][0], 31686);
    RSCACHE_CHECK(loc->actions[0] && strcmp(loc->actions[0], "Infuse-pouch") == 0);
    RSCACHE_CHECK(loc->actions[1] && strcmp(loc->actions[1], "Renew-points") == 0);

    RSCache_Dat2ConfigLocFree(loc);
}

static void
test_sequence_530_changed_opcodes(void)
{
    RSCACHE_TEST_GROUP("sequence 530 opcodes 13/14");

    /* opcode 13: two frame slots; frame 0 declares two sounds. The second u16
     * is that frame's *alternative*, which the client rolls against the first
     * with the first's loops and radius — so both land on frame 0 rather than
     * the second being consumed and dropped. */
    unsigned char record[] = {
        13, 0, 2,
        2, 0x01, 0x23, 0x23, 0xAB, 0xCD,
        0,
        14,
        0
    };
    struct RSCache profile = rs530_profile();
    struct RSCache_Dat2ConfigSequence sequence;
    unsigned char encoded[64];
    uint32_t written;
    memset(&sequence, 0, sizeof(sequence));
    RSCache_Dat2ConfigSequenceDecodeProfile(
        &sequence, &profile, (char*)record, (int)sizeof(record));

    RSCACHE_CHECK_EQ(sequence._consumed, sizeof(record));
    RSCACHE_CHECK_EQ(sequence.frame_sounds.count, 2);
    RSCACHE_CHECK_EQ(sequence.frame_sounds.frames[0], 0);
    RSCACHE_CHECK_EQ(sequence.frame_sounds.sounds[0].id, 0x123);
    RSCACHE_CHECK_EQ(sequence.frame_sounds.sounds[0].loops, 2);
    RSCACHE_CHECK_EQ(sequence.frame_sounds.sounds[0].location, 3);
    RSCACHE_CHECK_EQ(sequence.frame_sounds.frames[1], 0);
    RSCACHE_CHECK_EQ(sequence.frame_sounds.sounds[1].id, 0xABCD);
    /* The alternative inherits the packed entry's playback fields. */
    RSCACHE_CHECK_EQ(sequence.frame_sounds.sounds[1].loops, 2);
    RSCACHE_CHECK_EQ(sequence.frame_sounds.sounds[1].location, 3);
    RSCACHE_CHECK(sequence.rs2_530_sound_flag);

    /* And the encoder writes the alternative list back rather than only the
     * first id. The frame list is re-emitted dense to the highest *sounded*
     * frame, so the source record's trailing empty slot is not reproduced —
     * that is the map's shape, not something the alternatives changed. */
    {
        static const unsigned char expected[] = {
            13, 0, 1,
            2, 0x01, 0x23, 0x23, 0xAB, 0xCD,
            14,
            0
        };
        written = RSCache_Dat2ConfigSequenceEncode(
            &profile, &sequence, encoded, (uint32_t)sizeof(encoded));
        RSCACHE_CHECK_EQ(written, sizeof(expected));
        RSCACHE_CHECK(memcmp(encoded, expected, sizeof(expected)) == 0);
    }

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
    test_loc_530_opcode_95_has_no_payload();
    test_loc_530_obelisk_model_list();
    test_sequence_530_changed_opcodes();
    test_obj_530_changed_opcodes();
    RSCACHE_CHECK(rscache_test_checks > 0);
    return rscache_test_report("rs530 codec");
}
