/*
 * Animaya transparency, end to end, on real data: Akkha's vanish
 * (seq 9784, animaya 262 on framemap 2065) from the content tree's dumped
 * files, through RSCache_Dat2AnimMayaNewDecode, the engine bake
 * (ToriDraw_SkeletalAnimFromRSCache) and toridraw's pose
 * (ToriDraw_ModelAnimateSkeletal).
 *
 * The rev-239 client fades a model by its framemap's type-5 groups: each
 * TRANSPARENCY curve's value x 255 onto the alpha of every face carrying one
 * of the group's labels (class144.method4834). Before this the decoder dropped
 * every non-bone curve, so the fade never drew.
 *
 * Usage: toridraw_skeletal_alpha_test <osrs-content dir>
 */
#include "engine/toridraw_animation_from_rscache.h"

#include "datatypes/dat2_animaya.h"
#include "datatypes/dat2_framemap.h"
#include "datatypes/dat2_skeletalbase.h"
#include "toridraw_animation.h"
#include "toridraw_model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;

#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static char*
read_file(
    const char* path,
    int* out_size)
{
    FILE* f = fopen(path, "rb");
    if( !f )
    {
        fprintf(stderr, "cannot open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* data = malloc((size_t)n);
    assert(data);
    size_t const got = fread(data, 1, (size_t)n, f);
    fclose(f);
    if( got != (size_t)n )
    {
        fprintf(stderr, "short read on %s\n", path);
        exit(1);
    }
    *out_size = (int)n;
    return data;
}

/* The client's step, group by group: a = clamp((int)(a + v * 255)). */
static int
expected_alpha(
    const struct ToriDraw_SkeletalAnim* sk,
    int frame,
    int label)
{
    int a = 0;
    for( int g = 0; g < sk->alpha_group_count; g++ )
    {
        int has = 0;
        for( int k = 0; k < sk->alpha_label_counts[g]; k++ )
            if( sk->alpha_labels[g][k] == label )
                has = 1;
        if( !has )
            continue;
        a = (int)((float)a + sk->alpha_values[frame * sk->alpha_group_count + g] * 255.0f);
        if( a < 0 )
            a = 0;
        if( a > 255 )
            a = 255;
    }
    return a;
}

int
main(int argc, char** argv)
{
    if( argc < 2 )
    {
        fprintf(stderr, "usage: %s <osrs-content dir>\n", argv[0]);
        return 2;
    }
    char path[1024];
    int maya_size = 0;
    int fm_size = 0;

    snprintf(path, sizeof(path), "%s/animayas/animaya_262.animaya", argv[1]);
    char* maya_data = read_file(path, &maya_size);
    snprintf(path, sizeof(path), "%s/framemaps/base_2065.base", argv[1]);
    char* fm_data = read_file(path, &fm_size);

    struct RSCache_Dat2AnimMaya* maya =
        RSCache_Dat2AnimMayaNewDecode(262 << 16, maya_data, maya_size);
    struct RSCache_Dat2Framemap* fm =
        RSCache_Dat2FramemapNewDecodeCodec(2065, fm_data, fm_size, RSCACHE_CODEC_FRAMEMAP_V1);
    assert(maya);
    assert(fm);
    struct RSCache_Dat2SkeletalBase* base = RSCache_Dat2SkeletalBaseNewFromFramemap(fm);
    assert(base);

    /* The decode keeps all 67 transparency curves, one a type-5 group. */
    int transparency = 0;
    for( int g = 0; g < maya->group_curve_count; g++ )
        if( maya->group_curves[g].transform_type == RSCACHE_ANIMAYA_TRANSFORM_TRANSPARENCY )
            transparency++;
    CHECK(transparency == 67);

    struct ToriDraw_SkeletalAnim* sk = ToriDraw_SkeletalAnimFromRSCache(9784, maya, base, fm, 0);
    assert(sk);
    CHECK(sk->alpha_group_count == 67);
    CHECK(sk->frame_count >= 31);

    /* Every group's values in [0, 1]; one fades out and back (1 -> 0 -> 1, the
     * curve on framemap group 13), one fades in and stays (0 -> 1). */
    int out_and_back = 0;
    int fades_in = 0;
    int labelled = 1;
    for( int g = 0; g < sk->alpha_group_count; g++ )
    {
        float const v0 = sk->alpha_values[0 * sk->alpha_group_count + g];
        float const v15 = sk->alpha_values[15 * sk->alpha_group_count + g];
        float const v30 = sk->alpha_values[30 * sk->alpha_group_count + g];
        for( int f = 0; f < sk->frame_count; f++ )
        {
            float const v = sk->alpha_values[f * sk->alpha_group_count + g];
            CHECK(v >= -0.001f && v <= 1.001f);
        }
        if( v0 > 0.99f && fabsf(v15) < 0.01f && v30 > 0.99f )
            out_and_back++;
        if( fabsf(v0) < 0.01f && v30 > 0.99f )
            fades_in++;
        if( sk->alpha_label_counts[g] <= 0 )
            labelled = 0;
    }
    CHECK(out_and_back > 0);
    CHECK(fades_in > 0);
    CHECK(labelled);

    /* Pose a two-face model whose first face carries the first group's first
     * label: it takes the client's sum, the other face stays opaque, a model
     * with no alphas gets them, and posing again does not accumulate. */
    int const label = sk->alpha_labels[0][0];
    struct ToriDraw_Model* model = ToriDraw_ModelNew(1, 2, 0);
    model->vertices_x = calloc(1, sizeof(*model->vertices_x));
    model->vertices_y = calloc(1, sizeof(*model->vertices_y));
    model->vertices_z = calloc(1, sizeof(*model->vertices_z));
    assert(model->vertices_x && model->vertices_y && model->vertices_z);
    model->animaya_vertex_count = 1;
    model->animaya_group_counts = calloc(1, 1);
    model->animaya_groups = calloc(1, sizeof(uint8_t*));
    model->animaya_scales = calloc(1, sizeof(uint8_t*));
    assert(model->animaya_group_counts && model->animaya_groups && model->animaya_scales);
    model->face_bones = calloc(1, sizeof(*model->face_bones));
    assert(model->face_bones);
    model->face_bones->bones_count = label + 1;
    model->face_bones->bones = calloc((size_t)label + 1, sizeof(boneint_t*));
    model->face_bones->bones_sizes = calloc((size_t)label + 1, sizeof(boneint_t));
    assert(model->face_bones->bones && model->face_bones->bones_sizes);
    model->face_bones->bones[label] = calloc(1, sizeof(boneint_t));
    assert(model->face_bones->bones[label]);
    model->face_bones->bones[label][0] = 0;
    model->face_bones->bones_sizes[label] = 1;
    ToriDraw_ModelCaptureOriginalVertices(model);
    CHECK(model->face_alphas == NULL);

    for( int f = 0; f < sk->frame_count; f += 3 )
    {
        ToriDraw_ModelAnimateSkeletal(model, sk, f);
        CHECK(model->face_alphas != NULL);
        if( !model->face_alphas )
            break;
        CHECK(model->face_alphas[0] == expected_alpha(sk, f, label));
        CHECK(model->face_alphas[1] == 0);
    }
    ToriDraw_ModelAnimateSkeletal(model, sk, 0);
    int const first = model->face_alphas[0];
    ToriDraw_ModelAnimateSkeletal(model, sk, 0);
    CHECK(model->face_alphas[0] == first);

    ToriDraw_ModelFree(model);
    ToriDraw_SkeletalAnimFree(sk);
    RSCache_Dat2SkeletalBaseFree(base);
    RSCache_Dat2FramemapFree(fm);
    RSCache_Dat2AnimMayaFree(maya);
    free(maya_data);
    free(fm_data);

    if( g_failures )
    {
        fprintf(stderr, "toridraw_skeletal_alpha_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("toridraw_skeletal_alpha_test: ok (67 alpha groups, the fade posed)\n");
    return 0;
}
