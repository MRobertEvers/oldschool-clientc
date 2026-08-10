#include "rscache_test.h"

#include "datatypes/dat2_framemap.h"

#include <stdint.h>

static void
test_v3_downgrade(void)
{
    RSCACHE_TEST_GROUP("V3 framemap encodes as an explicit target codec");

    unsigned char v3[] = {
        2,             /* transforms */
        1, 2,          /* types */
        1, 0,          /* transform_actor */
        0x12, 0x34, 0x56, 0x78, /* masks */
        1, 2,          /* bone-group lengths */
        7, 8, 9        /* bone groups */
    };
    unsigned char want_v1[] = { 2, 1, 2, 1, 2, 7, 8, 9 };
    unsigned char want_v2[] = { 2, 1, 2, 1, 0, 1, 2, 7, 8, 9 };
    unsigned char out[64] = { 0 };

    struct RSCache_Dat2Framemap* def = RSCache_Dat2FramemapNewDecodeCodec(
        1491, (char*)v3, (int)sizeof(v3), RSCACHE_CODEC_FRAMEMAP_V3);
    RSCACHE_CHECK(def != NULL);

    uint32_t written = RSCache_Dat2FramemapEncodeCodec(
        def, RSCACHE_CODEC_FRAMEMAP_V1, out, sizeof(out));
    RSCACHE_CHECK_EQ(written, sizeof(want_v1));
    RSCACHE_CHECK_BYTES_EQ(out, want_v1, sizeof(want_v1));

    written = RSCache_Dat2FramemapEncodeCodec(
        def, RSCACHE_CODEC_FRAMEMAP_V2, out, sizeof(out));
    RSCACHE_CHECK_EQ(written, sizeof(want_v2));
    RSCACHE_CHECK_BYTES_EQ(out, want_v2, sizeof(want_v2));

    written = RSCache_Dat2FramemapEncodeCodec(
        def, RSCACHE_CODEC_FRAMEMAP_V3, out, sizeof(out));
    RSCACHE_CHECK_EQ(written, sizeof(v3));
    RSCACHE_CHECK_BYTES_EQ(out, v3, sizeof(v3));

    RSCache_Dat2FramemapFree(def);
}

int
main(void)
{
    printf("framemap codec tests\n");
    test_v3_downgrade();
    RSCACHE_CHECK(rscache_test_checks > 0);
    return rscache_test_report("framemap codec");
}
