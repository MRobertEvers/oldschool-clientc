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

static void
write_curve_header(
    struct RSCache_Buffer* buffer,
    int transform_type,
    int index,
    int curve_type,
    int point_count,
    int end_interp)
{
    p1(buffer, transform_type);
    pshortsmart(buffer, index);
    p1(buffer, curve_type);
    p2(buffer, point_count);
    p1(buffer, 0);          /* curve's own type byte */
    p1(buffer, 0);          /* start interpolation */
    p1(buffer, end_interp); /* end interpolation */
    p1(buffer, 0);          /* Hermite, not Bezier time */
}

/*
 * The non-bone transform types are kept by framemap group (rev-239
 * class488.field5860), one type a group, a curve by its slot; a curve whose
 * slot is past its type's count (class492) is dropped. A transparency curve
 * keeps its raw points and holds its last value past its last point when its
 * end mode is 0 -- a finished fade stays faded (class488.method10820) -- and
 * reads its own range without ever being densely sampled.
 */
static void
test_group_curves(void)
{
    RSCACHE_TEST_GROUP("Animaya non-bone transform groups");

    RSCACHE_CHECK_EQ(RSCache_Dat2AnimMayaTransformCurveCount(RSCACHE_ANIMAYA_TRANSFORM_BONE), 9);
    RSCACHE_CHECK_EQ(
        RSCache_Dat2AnimMayaTransformCurveCount(RSCACHE_ANIMAYA_TRANSFORM_TRANSPARENCY), 1);
    RSCACHE_CHECK_EQ(RSCache_Dat2AnimMayaTransformCurveCount(RSCACHE_ANIMAYA_TRANSFORM_TYPE3), 6);

    struct RSCache_Buffer encoded;
    RSCache_BufferInitAlloc(&encoded, 0);

    p1(&encoded, 1);  /* version */
    p2(&encoded, 42); /* skeletal base id */
    p2(&encoded, 0);
    p2(&encoded, 30);
    p1(&encoded, 0); /* pose id */
    p2(&encoded, 4); /* curve count */

    /* A bone curve, bone 0 rotation-X. */
    write_curve_header(&encoded, RSCACHE_ANIMAYA_TRANSFORM_BONE, 0, 1, 2, 0);
    write_curve_point(&encoded, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    write_curve_point(&encoded, 30, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    /* The fade: group 3, curve type 16 (slot 0), out and back -- 0 at tick 0,
     * 1 at 15, 0 at 30 (the shape of Akkha's vanish on its middle groups). */
    write_curve_header(&encoded, RSCACHE_ANIMAYA_TRANSFORM_TRANSPARENCY, 3, 16, 3, 0);
    write_curve_point(&encoded, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    write_curve_point(&encoded, 15, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    write_curve_point(&encoded, 30, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    /* Type 2 on group 1, curve type 11 (slot 1): stored, never applied. */
    write_curve_header(&encoded, RSCACHE_ANIMAYA_TRANSFORM_TYPE2, 1, 11, 1, 0);
    write_curve_point(&encoded, 0, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f);

    /* Transparency on curve type 11 (slot 1): past its one curve, dropped. */
    write_curve_header(&encoded, RSCACHE_ANIMAYA_TRANSFORM_TRANSPARENCY, 5, 11, 1, 0);
    write_curve_point(&encoded, 0, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f);

    struct RSCache_Dat2AnimMaya* maya =
        RSCache_Dat2AnimMayaNewDecode(9, (const char*)encoded.data, (int)RSCache_BufferLength(&encoded));
    RSCACHE_CHECK(maya != NULL);
    if( !maya )
    {
        RSCache_BufferRelease(&encoded);
        return;
    }

    RSCACHE_CHECK_EQ(maya->bone_curve_count, 1);
    RSCACHE_CHECK_EQ(maya->group_curve_count, 4);
    RSCACHE_CHECK(maya->group_curves != NULL);
    if( maya->group_curves && maya->group_curve_count == 4 )
    {
        struct RSCache_Dat2GroupCurves* fade = &maya->group_curves[3];
        RSCACHE_CHECK_EQ(fade->transform_type, RSCACHE_ANIMAYA_TRANSFORM_TRANSPARENCY);
        RSCACHE_CHECK(fade->curves[0] != NULL);
        if( fade->curves[0] )
        {
            RSCACHE_CHECK(fade->curves[0]->points != NULL);
            /* The raw curve knows its own range (no CurveLoad ran): the
             * middle point reads as itself, not as the last one. */
            RSCACHE_CHECK_EQ(fade->curves[0]->start_tick, 0);
            RSCACHE_CHECK_EQ(fade->curves[0]->end_tick, 30);
            RSCACHE_CHECK(RSCache_Dat2AnimMayaCurveEvaluate(fade->curves[0], 0) == 0.0f);
            RSCACHE_CHECK(RSCache_Dat2AnimMayaCurveEvaluate(fade->curves[0], 15) == 1.0f);
            RSCACHE_CHECK(RSCache_Dat2AnimMayaCurveEvaluate(fade->curves[0], 30) == 0.0f);
            RSCACHE_CHECK(RSCache_Dat2AnimMayaCurveEvaluate(fade->curves[0], 45) == 0.0f);
            RSCACHE_CHECK(RSCache_Dat2AnimMayaCurveEvaluate(fade->curves[0], -5) == 0.0f);
        }
        RSCACHE_CHECK_EQ(maya->group_curves[1].transform_type, RSCACHE_ANIMAYA_TRANSFORM_TYPE2);
        RSCACHE_CHECK(maya->group_curves[1].curves[1] != NULL);
        RSCACHE_CHECK(maya->group_curves[1].curves[0] == NULL);
        RSCACHE_CHECK_EQ(maya->group_curves[0].transform_type, RSCACHE_ANIMAYA_TRANSFORM_NONE);
        RSCACHE_CHECK_EQ(maya->group_curves[2].transform_type, RSCACHE_ANIMAYA_TRANSFORM_NONE);
    }

    RSCache_Dat2AnimMayaFree(maya);
    RSCache_BufferRelease(&encoded);
}

int
main(void)
{
    test_step_tangent_sentinel();
    test_group_curves();
    return rscache_test_report("animaya");
}
