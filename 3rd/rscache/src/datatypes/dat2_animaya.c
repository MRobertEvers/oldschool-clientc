#include "dat2_animaya.h"

#include "../dat2disk.h"
#include "../filelist.h"
#include "../reference_table.h"
#include "../rsbuffer.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * CurveInterp — ported from docs/skeletal/CurveInterp.ts
 * ========================================================================= */

#define ULP 1.1920929e-7f
#define ULP2 (2.0f * ULP)

/* Forward declarations */
static float
interpolate_curve(
    struct RSCache_Dat2Curve* c,
    int t);
static float
extrapolate_curve(
    struct RSCache_Dat2Curve* c,
    int t,
    int is_start);
static void
method3282(float v[2]);
static float
method8290(
    struct RSCache_Dat2Curve* c,
    int t);
static float
method6869(
    const float* values,
    int last_index,
    float var2);
static int
method5023(
    const float* var0,
    int var1,
    float var2,
    int var3,
    float var4,
    int var5,
    float* var6);

/* Interpolation state cached on the curve struct — we reuse the interp fields
 * from CurveInterp by storing them directly on the curve after decode. */
typedef struct
{
    int point_index;
    int point_index_updated;
    int interp_bool;
    float v0, v1, v2, v3, v4, v5, v6, v7, v8, v9;
} CurveState;

/* We store CurveState inside a tiny auxiliary array because we must not bloat
 * RSCache_Dat2Curve (shared with the header). Solved by using the `type`
 * field for caching — but simpler: just keep a local file-scope helper that
 * works purely on the passed struct without extra state (the interpolation is
 * evaluated during Load, not at getValue time). */

/* ---- polynomial / bezier helpers ---- */

static float
method6869(
    const float* values,
    int last_index,
    float var2)
{
    float output = values[last_index];
    for( int i = last_index - 1; i >= 0; i-- )
        output = output * var2 + values[i];
    return output;
}

/* Animaya encodes stepped curve segments with Java Float.MAX_VALUE in both
 * outgoing tangent fields.  Comparing against a decimal float literal is not
 * reliable when an i686 build evaluates the comparison in x87 extended
 * precision, so recognize the serialized IEEE-754 value directly. */
static int
curve_tangent_is_max_value(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits == UINT32_C(0x7f7fffff);
}

static void
method3282(float v[2])
{
    v[1] = 1.0f - v[1];
    if( v[0] < 0.0f )
        v[0] = 0.0f;
    if( v[1] < 0.0f )
        v[1] = 0.0f;

    if( v[0] > 1.0f || v[1] > 1.0f )
    {
        float var1 = 1.0f + v[0] * (v[0] - 2.0f + v[1]) + (v[1] - 2.0f) * v[1];
        if( var1 + ULP > 0.0f )
        {
            if( ULP + v[0] < 1.3333334f )
            {
                float var2 = v[0] - 2.0f;
                float var3 = v[0] - 1.0f;
                float var4 = sqrtf(var2 * var2 - 4.0f * var3 * var3);
                float var5 = 0.5f * (var4 + -var2);
                if( v[1] + ULP > var5 )
                    v[1] = var5 - ULP;
                else
                {
                    float var6 = (-var2 - var4) * 0.5f;
                    if( v[1] < var6 + ULP )
                        v[1] = var6 + ULP;
                }
            }
            else
            {
                v[0] = 1.3333334f - ULP;
                v[1] = 0.33333334f - ULP;
            }
        }
    }
    v[1] = 1.0f - v[1];
}

/* Polynomial root finder — method5023 from CurveInterp.ts */
static int
method5023(
    const float* var0,
    int var1,
    float var2,
    int var3,
    float var4,
    int var5,
    float* var6)
{
    float var7 = 0.0f;
    for( int i = 0; i < var1 + 1; i++ )
        var7 += fabsf(var0[i]);

    float var44 = (fabsf(var2) + fabsf(var4)) * (float)(var1 + 1) * ULP;
    if( var7 <= var44 )
        return -1;

    float var9[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    for( int i = 0; i < var1 + 1; i++ )
        var9[i] = (1.0f / var7) * var0[i];

    while( fabsf(var9[var1]) < var44 )
        var1--;

    int status = 0;
    if( var1 == 0 )
        return status;
    if( var1 == 1 )
    {
        var6[0] = -var9[0] / var9[1];
        int c1 = var3 ? (var2 < var6[0] + var44) : (var2 < var6[0] - var44);
        int c2 = var5 ? (var4 > var6[0] - var44) : (var4 > var6[0] + var44);
        status = (c1 && c2) ? 1 : 0;
        if( status > 0 )
        {
            if( var3 && var6[0] < var2 )
                var6[0] = var2;
            else if( var5 && var6[0] > var4 )
                var6[0] = var4;
        }
        return status;
    }

    /* degree >= 2 */
    float field4756[5];
    int field4757 = var1;
    for( int i = 0; i < 5; i++ )
        field4756[i] = var9[i];

    float var12[5] = { 0 };
    for( int i = 1; i <= var1; i++ )
        var12[i - 1] = (float)i * var9[i];

    float var41[5] = { 0 };
    int recursiveStatus = method5023(var12, var1 - 1, var2, 0, var4, 0, var41);
    if( recursiveStatus == -1 )
        return 0;

    int var15 = 0;
    float var17 = 0.0f, var18 = 0.0f, var19 = 0.0f;

    for( int s = 0; s <= recursiveStatus; s++ )
    {
        if( status > var1 )
            return status;

        float var16;
        if( s == 0 )
        {
            var16 = var2;
            var18 = method6869(var9, var1, var2);
            if( fabsf(var18) <= var44 && var3 )
                var6[status++] = var2;
        }
        else
        {
            var16 = var19;
            var18 = var17;
        }

        if( recursiveStatus == s )
        {
            var19 = var4;
            var15 = 0;
        }
        else
        {
            var19 = var41[s];
        }

        var17 = method6869(var9, var1, var19);
        if( var15 )
        {
            var15 = 0;
        }
        else if( fabsf(var17) < var44 )
        {
            if( recursiveStatus != s || var5 )
            {
                var6[status++] = var19;
                var15 = 1;
            }
        }
        else if( (var18 < 0.0f && var17 > 0.0f) || (var18 > 0.0f && var17 < 0.0f) )
        {
            int var22 = status++;
            float var24 = var16, var25 = var19;
            float var26 = method6869(field4756, field4757, var16);
            float var23;
            if( fabsf(var26) < ULP )
            {
                var23 = var16;
            }
            else
            {
                float var27 = method6869(field4756, field4757, var19);
                if( fabsf(var27) < ULP )
                {
                    var23 = var19;
                }
                else
                {
                    float var28 = 0.0f, var29 = 0.0f, var30 = 0.0f, var35 = 0.0f;
                    int var36 = 1, var37 = 0;
                    do
                    {
                        var37 = 0;
                        if( var36 )
                        {
                            var28 = var24;
                            var35 = var26;
                            var29 = var25 - var24;
                            var30 = var29;
                            var36 = 0;
                        }
                        if( fabsf(var35) < fabsf(var27) )
                        {
                            float tmp_f = var24;
                            var24 = var25;
                            var25 = var28;
                            var28 = tmp_f;
                            tmp_f = var26;
                            var26 = var27;
                            var27 = var35;
                            var35 = tmp_f;
                        }
                        float var38 = ULP2 * fabsf(var25) + 0.0f;
                        float var39 = 0.5f * (var28 - var25);
                        int var40 = (fabsf(var39) > var38) && (var27 != 0.0f);
                        if( var40 )
                        {
                            if( fabsf(var30) < var38 || fabsf(var26) <= fabsf(var27) )
                            {
                                var29 = var39;
                                var30 = var39;
                            }
                            else
                            {
                                float var34 = var27 / var26;
                                float var31, var32;
                                if( var24 == var28 )
                                {
                                    var31 = var39 * 2.0f * var34;
                                    var32 = 1.0f - var34;
                                }
                                else
                                {
                                    var32 = var26 / var35;
                                    float var33 = var27 / var35;
                                    var31 = var34 * (var39 * 2.0f * var32 * (var32 - var33) -
                                                     (var33 - 1.0f) * (var25 - var24));
                                    var32 = (var33 - 1.0f) * (var32 - 1.0f) * (var34 - 1.0f);
                                }
                                if( var31 > 0.0f )
                                    var32 = -var32;
                                else
                                    var31 = -var31;
                                float tmp_v30 = var30;
                                var30 = var29;
                                if( 2.0f * var31 < 3.0f * var39 * var32 - fabsf(var32 * var38) &&
                                    var31 < fabsf(var32 * tmp_v30 * 0.5f) )
                                    var29 = var31 / var32;
                                else
                                {
                                    var29 = var39;
                                    var30 = var39;
                                }
                            }
                            var24 = var25;
                            var26 = var27;
                            if( fabsf(var29) > var38 )
                                var25 += var29;
                            else if( var39 > 0.0f )
                                var25 += var38;
                            else
                                var25 -= var38;
                            var27 = method6869(field4756, field4757, var25);
                            if( var27 * (var35 / fabsf(var35)) > 0.0f )
                            {
                                var36 = 1;
                                var37 = 1;
                            }
                            else
                                var37 = 1;
                        }
                    } while( var37 );
                    var23 = var25;
                }
            }
            var6[var22] = var23;
            if( status > 1 && var6[status - 2] >= var6[status - 1] - var44 )
            {
                var6[status - 2] = 0.5f * (var6[status - 2] + var6[status - 1]);
                status--;
            }
        }
    }
    return status;
}

/* ---- interpolate state (mutable, stored inline since curves are ephemeral) ---- */
/* We replicate the mutable state from Curve.ts directly as local vars inside
 * interpolate_curve; curves are evaluated once during Load and discarded. */

/* Forward-declare for evaluate_at_t used in Load */
typedef struct
{
    int point_index;
    int point_index_updated;
    int interp_bool;
    float v0, v1, v2, v3, v4, v5, v6, v7, v8, v9;
} CurveEvalState;

static int
get_point_index(
    const struct RSCache_Dat2Curve* c,
    int t,
    int current)
{
    const struct RSCache_Dat2CurvePoint* pts = c->points;
    int count = c->point_count;

    if( current < 0 || pts[current].x > t || (current + 1 < count && pts[current + 1].x <= t) )
    {
        if( t >= c->start_tick && t <= c->end_tick )
        {
            int new_idx = current;
            if( count > 0 )
            {
                int lo = 0, hi = count - 1;
                do
                {
                    int mid = (lo + hi) >> 1;
                    if( t < pts[mid].x )
                    {
                        if( mid > 0 && t > pts[mid - 1].x )
                        {
                            new_idx = mid - 1;
                            break;
                        }
                        hi = mid - 1;
                    }
                    else
                    {
                        if( t <= pts[mid].x )
                        {
                            new_idx = mid;
                            break;
                        }
                        if( mid + 1 < count && t < pts[mid + 1].x )
                        {
                            new_idx = mid;
                            break;
                        }
                        lo = mid + 1;
                    }
                } while( lo <= hi );
            }
            return new_idx;
        }
        return -1;
    }
    return current;
}

static float
extrapolate_curve(
    struct RSCache_Dat2Curve* c,
    int t,
    int is_start)
{
    if( !c || !c->points || c->point_count == 0 )
        return 0.0f;
    float var4 = (float)c->points[0].x;
    float var5 = (float)c->points[c->point_count - 1].x;
    float var6 = var5 - var4;
    if( var6 == 0.0f )
        return c->points[0].y;

    float var7 = t > var5 ? (t - var5) / var6 : (t - var4) / var6;
    int var8 = (int)var7;
    float var10 = fabsf(var7 - (float)var8);
    float var11 = var10 * var6;
    var8 = (int)fabsf(1.0f + (float)var8);
    float var12 = (float)var8 / 2.0f;
    int var14 = (int)var12;
    var10 = var12 - (float)var14;

    if( is_start )
    {
        if( c->start_interp == 4 )
        {
            if( var10 != 0.0f )
                var11 += var4;
            else
                var11 = var5 - var11;
        }
        else if( c->start_interp == 2 || c->start_interp == 3 )
        {
            var11 = var5 - var11;
        }
        else if( c->start_interp == 1 )
        {
            var11 = var4 - t;
            float v16 = c->points[0].field2;
            float v17 = c->points[0].field3;
            float output = c->points[0].y;
            if( v16 != 0.0f )
                output -= (var11 * v17) / v16;
            return output;
        }
    }
    else
    {
        if( c->end_interp == 4 )
        {
            if( var10 != 0.0f )
                var11 = var5 - var11;
            else
                var11 += var4;
        }
        else if( c->end_interp == 2 || c->end_interp == 3 )
        {
            var11 += var4;
        }
        else if( c->end_interp == 1 )
        {
            var11 = t - var5;
            int last = c->point_count - 1;
            float v16 = c->points[last].field4;
            float v17 = c->points[last].field5;
            float output = c->points[last].y;
            if( v16 != 0.0f )
                output += (v17 * var11) / v16;
            return output;
        }
    }

    float output = interpolate_curve(c, (int)var11);
    if( is_start && c->start_interp == 3 )
    {
        float v18 = c->points[c->point_count - 1].y - c->points[0].y;
        output = output - v18 * (float)var8;
    }
    else if( !is_start && c->end_interp == 3 )
    {
        float v18 = c->points[c->point_count - 1].y - c->points[0].y;
        output = output + v18 * (float)var8;
    }
    return output;
}

static float
method8290_local(
    struct RSCache_Dat2Curve* c,
    int t,
    float iv0,
    float iv1,
    float iv2,
    float iv3,
    float iv4,
    float iv5,
    float iv6,
    float iv7,
    float iv8,
    float iv9,
    int interp_bool)
{
    float v0;
    if( iv0 == (float)t )
        v0 = 0.0f;
    else if( (float)t == iv1 )
        v0 = 1.0f;
    else
        v0 = ((float)t - iv0) / (iv1 - iv0);

    float v1;
    if( interp_bool )
    {
        v1 = v0;
    }
    else
    {
        float in[4] = { iv2 - v0, iv3, iv4, iv5 };
        float out5[5] = { 0 };
        int r = method5023(in, 3, 0.0f, 1, 1.0f, 1, out5);
        v1 = (r == 1) ? out5[0] : 0.0f;
    }
    return v1 * (iv7 + v1 * (v1 * iv9 + iv8)) + iv6;
}

/* Core interpolation — evaluates one tick from raw CurvePoints.
 * Mirrors Curve.ts interpolateCurve, keeping mutable state in local vars. */
static float
interpolate_curve_at(
    struct RSCache_Dat2Curve* c,
    int t,
    int* io_point_index,
    int* io_piu,
    int* io_interp_bool,
    float* io_v0,
    float* io_v1,
    float* io_v2,
    float* io_v3,
    float* io_v4,
    float* io_v5,
    float* io_v6,
    float* io_v7,
    float* io_v8,
    float* io_v9)
{
    if( !c || !c->points || c->point_count == 0 )
        return 0.0f;

    if( t < c->start_tick )
    {
        if( c->start_interp == 0 )
            return c->points[0].y;
        return extrapolate_curve(c, t, 1);
    }
    if( t > c->end_tick )
    {
        if( c->end_interp == 0 )
            return c->points[c->point_count - 1].y;
        return extrapolate_curve(c, t, 0);
    }

    int pidx = get_point_index(c, t, *io_point_index);
    if( pidx < 0 || pidx >= c->point_count )
        return 0.0f;

    int updated = (*io_point_index != pidx);
    if( updated )
    {
        *io_point_index = pidx;
        *io_piu = 1;
    }

    const struct RSCache_Dat2CurvePoint* point = &c->points[pidx];

    /* Check boolean shortcuts */
    int bool0 = (point->field4 == 0.0f && point->field5 == 0.0f);
    int bool1 =
        curve_tangent_is_max_value(point->field4) &&
        curve_tangent_is_max_value(point->field5);
    if( pidx + 1 >= c->point_count )
        bool0 = 1;

    if( !bool0 && !bool1 && *io_piu )
    {
        const struct RSCache_Dat2CurvePoint* next = &c->points[pidx + 1];
        float var5 = (float)point->x;
        float var9 = point->y;
        float var6 = point->field4 * 0.33333334f + var5;
        float var10 = point->field5 * 0.33333334f + var9;
        float var8 = (float)next->x;
        float var12 = next->y;
        float var7 = var8 - next->field2 * 0.33333334f;
        float var11 = var12 - next->field3 * 0.33333334f;

        if( c->bool_flag )
        {
            float var15 = var10, var16 = var11;
            float var17 = var8 - var5;
            if( var17 != 0.0f )
            {
                float var18 = var6 - var5;
                float var19 = var7 - var5;
                float v29[2] = { var18 / var17, var19 / var17 };
                *io_interp_bool = (v29[0] == 0.33333334f && v29[1] == 0.6666667f);
                float var21 = v29[0], var22 = v29[1];
                if( v29[0] < 0.0f )
                    v29[0] = 0.0f;
                if( v29[1] > 1.0f )
                    v29[1] = 1.0f;
                if( v29[0] > 1.0f || v29[1] < -1.0f )
                    method3282(v29);
                if( v29[0] != var21 && var21 != 0.0f )
                    var15 = ((var10 - var9) * v29[0]) / var21 + var9;
                if( var22 != v29[1] && 1.0f != var22 )
                    var16 = var12 - ((1.0f - v29[1]) * (var12 - var11)) / (1.0f - var22);

                *io_v0 = var5;
                *io_v1 = var8;
                float var23 = v29[0], var24 = v29[1];
                float v25 = var23 - 0.0f, v26 = var24 - var23, v27 = 1.0f - var24;
                float v28 = v26 - v25;
                *io_v5 = v27 - v26 - v28;
                *io_v4 = v28 + v28 + v28;
                *io_v3 = v25 + v25 + v25;
                *io_v2 = 0.0f;
                v25 = var15 - var9;
                v26 = var16 - var15;
                v27 = var12 - var16;
                v28 = v26 - v25;
                *io_v9 = v27 - v26 - v28;
                *io_v8 = v28 + v28 + v28;
                *io_v7 = v25 + v25 + v25;
                *io_v6 = var9;
            }
        }
        else
        {
            *io_v0 = var5;
            float var13 = var8 - var5;
            float var14 = var12 - var9;
            float v15 = var6 - var5, v16 = 0.0f, v17 = 0.0f;
            if( v15 != 0.0f )
                v16 = (var10 - var9) / v15;
            v15 = var8 - var7;
            if( v15 != 0.0f )
                v17 = (var12 - var11) / v15;
            float var18 = 1.0f / (var13 * var13);
            float var19 = v16 * var13, var20 = v17 * var13;
            *io_v2 = (var18 * (var19 + var20 - var14 - var14)) / var13;
            *io_v3 = var18 * (var14 + var14 + var14 - var19 - var19 - var20);
            *io_v4 = v16;
            *io_v5 = var9;
        }
        *io_piu = 0;
    }

    if( bool0 )
        return point->y;
    if( bool1 )
    {
        if( (int)point->x != t && pidx + 1 < c->point_count )
            return c->points[pidx + 1].y;
        return point->y;
    }
    if( c->bool_flag )
        return method8290_local(
            c,
            t,
            *io_v0,
            *io_v1,
            *io_v2,
            *io_v3,
            *io_v4,
            *io_v5,
            *io_v6,
            *io_v7,
            *io_v8,
            *io_v9,
            *io_interp_bool);

    float var6b = (float)t - *io_v0;
    return *io_v5 + var6b * ((var6b * *io_v2 + *io_v3) * var6b + *io_v4);
}

/* Simple wrapper used during extrapolation (no mutable state needed — it
 * evaluates from raw points).  We use a fresh per-call state here. */
static float
interpolate_curve(
    struct RSCache_Dat2Curve* c,
    int t)
{
    int pi = 0, piu = 1, ib = 0;
    float v0 = 0, v1 = 0, v2 = 0, v3 = 0, v4 = 0, v5 = 0, v6 = 0, v7 = 0, v8 = 0, v9 = 0;
    return interpolate_curve_at(
        c, t, &pi, &piu, &ib, &v0, &v1, &v2, &v3, &v4, &v5, &v6, &v7, &v8, &v9);
}

/* ---- RSCache_Dat2AnimMayaCurveLoad ---- */

void
RSCache_Dat2AnimMayaCurveLoad(struct RSCache_Dat2Curve* c)
{
    if( !c || !c->points || c->point_count == 0 )
        return;

    c->start_tick = c->points[0].x;
    c->end_tick = c->points[c->point_count - 1].x;
    int duration = c->end_tick - c->start_tick;
    c->values = malloc((size_t)(duration + 1) * sizeof(float));
    if( !c->values )
        return;

    int pi = 0, piu = 1, ib = 0;
    float v0 = 0, v1 = 0, v2 = 0, v3 = 0, v4 = 0, v5 = 0, v6 = 0, v7 = 0, v8 = 0, v9 = 0;

    for( int t = c->start_tick; t <= c->end_tick; t++ )
        c->values[t - c->start_tick] = interpolate_curve_at(
            c, t, &pi, &piu, &ib, &v0, &v1, &v2, &v3, &v4, &v5, &v6, &v7, &v8, &v9);

    /* min/max outside range (Curve.ts mirrors a bug: points are undefined here) */
    c->min_value = 0.0f;
    c->max_value = 0.0f;

    free(c->points);
    c->points = NULL;
    c->point_count = 0;
}

float
RSCache_Dat2AnimMayaCurveGetValue(
    const struct RSCache_Dat2Curve* c,
    int t)
{
    if( !c || !c->values )
        return 0.0f;
    if( t < c->start_tick )
        return c->min_value;
    if( t > c->end_tick )
        return c->max_value;
    return c->values[t - c->start_tick];
}

/* ---- memory ---- */

void
RSCache_Dat2CurveFree(struct RSCache_Dat2Curve* c)
{
    if( !c )
        return;
    free(c->points);
    free(c->values);
    free(c);
}

void
RSCache_Dat2AnimMayaFree(struct RSCache_Dat2AnimMaya* maya)
{
    if( !maya )
        return;
    if( maya->bone_curves )
    {
        for( int b = 0; b < maya->bone_curve_count; b++ )
        {
            for( int s = 0; s < 9; s++ )
                RSCache_Dat2CurveFree(maya->bone_curves[b].curves[s]);
        }
        free(maya->bone_curves);
    }
    free(maya);
}

/* ---- decode ---- */

/* Decode one Curve from the stream.  Returns a heap-allocated Curve
 * with raw CurvePoints (not yet sampled).  Caller must call CurveLoad. */
static struct RSCache_Dat2Curve*
curve_decode(
    int curve_id,
    struct RSCache_Buffer* buf,
    int version)
{
    struct RSCache_Dat2Curve* c = calloc(1, sizeof(struct RSCache_Dat2Curve));
    if( !c )
        return NULL;
    c->id = curve_id;

    int count = g2(buf);
    c->type = g1(buf);
    c->start_interp = g1(buf);
    c->end_interp = g1(buf);
    c->bool_flag = g1(buf) != 0;

    if( count > 0 )
    {
        c->points = malloc((size_t)count * sizeof(struct RSCache_Dat2CurvePoint));
        if( !c->points )
        {
            free(c);
            return NULL;
        }
        c->point_count = count;
        for( int i = 0; i < count; i++ )
        {
            struct RSCache_Dat2CurvePoint* p = &c->points[i];
            p->x = (int16_t)g2b(buf); /* readShort */
            p->y = gf(buf);           /* readFloat */
            p->field2 = gf(buf);
            p->field3 = gf(buf);
            p->field4 = gf(buf);
            p->field5 = gf(buf);
        }
    }
    return c;
}

struct RSCache_Dat2AnimMaya*
RSCache_Dat2AnimMayaNewDecode(
    int id,
    const char* data,
    int data_size)
{
    if( !data || data_size < 6 )
        return NULL;

    struct RSCache_Buffer buf = {
        .data = (uint8_t*)data,
        .size = (uint32_t)data_size,
        .position = 0,
    };

    int version = g1(&buf);
    int base_id = g2(&buf);     /* u16 baseId */
    g2(&buf);                   /* skip u16 */
    g2(&buf);                   /* skip u16 */
    int pose_id = g1(&buf);     /* u8 poseId */
    int curve_count = g2(&buf); /* u16 curveCount */

    struct RSCache_Dat2AnimMaya* maya = calloc(1, sizeof(struct RSCache_Dat2AnimMaya));
    if( !maya )
        return NULL;
    maya->id = id;
    maya->version = version;
    maya->base_id = base_id;
    maya->pose_id = pose_id;

    /* We need to collect bone_curves indexed by boneIndex.
     * Bone count is not in the stream; we discover max boneIndex as we go,
     * then resize once at the end. */
    int capacity = 64;
    struct RSCache_Dat2BoneCurves* bone_curves =
        calloc((size_t)capacity, sizeof(struct RSCache_Dat2BoneCurves));
    if( !bone_curves )
    {
        free(maya);
        return NULL;
    }
    int max_bone = -1;

    for( int i = 0; i < curve_count; i++ )
    {
        /* u8 transformType */
        int transform_type = g1(&buf);
        /* smart2 boneIndex (SkeletalSeq.ts readSmart2) */
        int bone_index = RSCache_BufferReadShortSmart(&buf);
        /* u8 curveType */
        int curve_type = g1(&buf);

        /* Map curveType -> slot index (getCurveIndex from CurveType.ts)
         * CURVE_INDICES = [-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 0] */
        static const int CURVE_INDICES[17] = { -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 0 };
        int slot = (curve_type >= 0 && curve_type < 17) ? CURVE_INDICES[curve_type] : -1;

        struct RSCache_Dat2Curve* curve = curve_decode(i, &buf, version);

        /* Only store BONE (transformType==1) curves; discard others. */
        if( transform_type == 1 && slot >= 0 && bone_index >= 0 )
        {
            if( bone_index >= capacity )
            {
                int new_cap = capacity * 2;
                while( new_cap <= bone_index )
                    new_cap *= 2;
                struct RSCache_Dat2BoneCurves* grown =
                    realloc(bone_curves, (size_t)new_cap * sizeof(struct RSCache_Dat2BoneCurves));
                if( !grown )
                {
                    RSCache_Dat2CurveFree(curve);
                    continue;
                }
                memset(
                    grown + capacity,
                    0,
                    (size_t)(new_cap - capacity) * sizeof(struct RSCache_Dat2BoneCurves));
                bone_curves = grown;
                capacity = new_cap;
            }

            /* Free any existing curve in that slot and replace */
            RSCache_Dat2CurveFree(bone_curves[bone_index].curves[slot]);
            bone_curves[bone_index].curves[slot] = curve;
            curve = NULL; /* ownership transferred */

            if( bone_index > max_bone )
                max_bone = bone_index;
        }

        if( curve )
            RSCache_Dat2CurveFree(curve);
    }

    assert((int)buf.position == data_size);

    maya->bone_curve_count = max_bone + 1;
    maya->bone_curves = bone_curves;

    /* Sample all curves to dense per-tick float arrays */
    for( int b = 0; b < maya->bone_curve_count; b++ )
        for( int s = 0; s < 9; s++ )
            if( maya->bone_curves[b].curves[s] )
                RSCache_Dat2AnimMayaCurveLoad(maya->bone_curves[b].curves[s]);

    return maya;
}

struct RSCache_Dat2AnimMaya*
RSCache_Dat2AnimMayaNewFromArchive(
    struct RSCache_ReferenceTable* table,
    struct RSCache_Dat2DiskArchive* archive,
    int anim_maya_id)
{
    struct RSCache_Dat2AnimMaya* maya = NULL;
    struct RSCache_FileList* filelist = NULL;
    struct RSCache_ReferenceTableArchive* archive_ref = NULL;

    if( !archive )
        return NULL;

    int archive_id = anim_maya_id >> 16;
    int file_id = anim_maya_id & 0xFFFF;

    if( table )
    {
        if( !RSCache_Dat2DiskArchiveInitMetadataFromTable(table, archive) )
            return NULL;
        if( archive_id >= 0 && archive_id < table->archive_count )
            archive_ref = &table->archives[archive_id];
    }

    filelist =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !filelist )
        return NULL;

    for( int i = 0; i < filelist->file_count; i++ )
    {
        int id = archive_ref ? archive_ref->children.files[i].id : i;
        if( id != file_id )
            continue;

        maya = RSCache_Dat2AnimMayaNewDecode(
            anim_maya_id, filelist->files[i], filelist->file_sizes[i]);
        break;
    }

    RSCache_FileListFree(filelist);
    return maya;
}

struct RSCache_Dat2AnimMaya*
RSCache_Dat2AnimMayaNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int anim_maya_id)
{
    struct RSCache_Dat2AnimMaya* maya = NULL;
    struct RSCache_Dat2DiskArchive* archive = NULL;
    struct RSCache_FileList* filelist = NULL;

    if( !cache )
        return NULL;

    int archive_id = anim_maya_id >> 16;
    int file_id = anim_maya_id & 0xFFFF;

    /* Maya skeletal animation is an OldSchool table. A 643 cache puts varbits at that
     * id, so there is nothing here to read rather than something that fails to decode. */
    int animayas_table = RSCache_Dat2DiskTableId(cache, RSCACHE_DAT2_TABLE_ANIMAYAS);
    if( animayas_table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return NULL;

    archive = RSCache_Dat2DiskArchiveNewLoad(cache, animayas_table, archive_id);
    if( !archive )
        return NULL;

    struct RSCache_ReferenceTable* table = cache->tables[animayas_table];
    maya = RSCache_Dat2AnimMayaNewFromArchive(table, archive, anim_maya_id);
    RSCache_Dat2DiskArchiveFree(archive);
    return maya;
}
