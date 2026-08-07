/*
 * Animaya curve decode/evaluation regressions.
 *
 * Stepped curve segments are marked by Java Float.MAX_VALUE in both outgoing
 * tangent fields.  The serialized bit pattern must be recognized directly:
 * an i686/x87 build can keep a decimal float literal at extended precision,
 * making an otherwise equivalent floating-point comparison fail.
 */

#include "rscache_test.h"

#include <rscache.h>

#include <float.h>

static void
write_curve_point(
    struct RSCache_Buffer* buffer,
    int tick,
    float value,
    float tangent_in_x,
    float tangent_in_y,
    float tangent_out_x,
    float tangent_out_y)
{
    p2b(buffer, tick);
    pf(buffer, value);
    pf(buffer, tangent_in_x);
    pf(buffer, tangent_in_y);
    pf(buffer, tangent_out_x);
    pf(buffer, tangent_out_y);
}

static void
test_step_tangent_sentinel(void)
{
    RSCACHE_TEST_GROUP("Animaya step-tangent sentinel");

    struct RSCache_Buffer encoded;
    RSCache_BufferInitAlloc(&encoded, 0);

    /* SkeletalSeq header. */
    p1(&encoded, 1);   /* version */
    p2(&encoded, 42);  /* skeletal base id */
    p2(&encoded, 0);   /* start (unused by the decoder) */
    p2(&encoded, 5);   /* end (unused by the decoder) */
    p1(&encoded, 0);   /* pose id */
    p2(&encoded, 1);   /* curve count */

    /* One BONE curve on bone 0, rotation-X slot 0. */
    p1(&encoded, 1);
    pshortsmart(&encoded, 0);
    p1(&encoded, 1);

    p2(&encoded, 3); /* three keyframes */
    p1(&encoded, 0); /* curve type */
    p1(&encoded, 0); /* start interpolation */
    p1(&encoded, 0); /* end interpolation */
    p1(&encoded, 0); /* Hermite compatibility mode, not Bezier time */

    write_curve_point(&encoded, -1, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    write_curve_point(&encoded, 0, 1.0f, 0.0f, 0.0f, FLT_MAX, FLT_MAX);
    write_curve_point(&encoded, 5, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    int encoded_size = (int)RSCache_BufferLength(&encoded);
    struct RSCache_Dat2AnimMaya* maya =
        RSCache_Dat2AnimMayaNewDecode(7, (const char*)encoded.data, encoded_size);

    RSCACHE_CHECK(maya != NULL);
    if( !maya )
    {
        RSCache_BufferRelease(&encoded);
        return;
    }

    RSCACHE_CHECK_EQ(maya->id, 7);
    RSCACHE_CHECK_EQ(maya->version, 1);
    RSCACHE_CHECK_EQ(maya->base_id, 42);
    RSCACHE_CHECK_EQ(maya->bone_curve_count, 1);
    RSCACHE_CHECK(maya->bone_curves != NULL);

    struct RSCache_Dat2Curve* curve = maya->bone_curves[0].curves[0];
    RSCACHE_CHECK(curve != NULL);
    if( curve )
    {
        RSCACHE_CHECK_EQ(curve->start_tick, -1);
        RSCACHE_CHECK_EQ(curve->end_tick, 5);
        RSCACHE_CHECK(curve->points == NULL);
        RSCACHE_CHECK(curve->values != NULL);

        RSCACHE_CHECK(RSCache_Dat2AnimMayaCurveGetValue(curve, 0) == 1.0f);
        RSCACHE_CHECK(RSCache_Dat2AnimMayaCurveGetValue(curve, 1) == 2.0f);
        RSCACHE_CHECK(RSCache_Dat2AnimMayaCurveGetValue(curve, 4) == 2.0f);
        RSCACHE_CHECK(RSCache_Dat2AnimMayaCurveGetValue(curve, 5) == 2.0f);
    }

    RSCache_Dat2AnimMayaFree(maya);
    RSCache_BufferRelease(&encoded);
}

int
main(void)
{
    test_step_tangent_sentinel();
    return rscache_test_report("animaya");
}
