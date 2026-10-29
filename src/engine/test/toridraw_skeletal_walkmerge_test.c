/*
 * The skeletal walkmerge, on real data: Ocellus's phase-one volley
 * (npc_ocellus_phase1_basic_projectile_start, animaya 994, opcode-17 mask)
 * over his walk (npc_ocellus_phase1_walk, animaya 1004), from the content
 * tree's dumped files through the engine bake and
 * ToriDraw_ModelAnimateSkeletalMasked.
 *
 * Rev 241 (class394.method9194 -> class174.method5754/5752 ->
 * class128.method4376): the action poses every bone whose mask is false, the
 * walk every bone whose mask is true, and the merged locals are composed
 * through the rig and skinned once. Before this a masked skeletal action
 * posed every bone itself and the walker slid along in the action's stance.
 *
 * The model is one vertex per bone, weighted wholly to it, so a vertex shows
 * exactly the matrix its bone ended up with.
 *
 * Usage: toridraw_skeletal_walkmerge_test <osrs-content dir>
 */
#include "engine/toridraw_animation_from_rscache.h"

#include "datatypes/dat2_animaya.h"
#include "datatypes/dat2_framemap.h"
#include "datatypes/dat2_skeletalbase.h"
#include "toridraw_animation.h"
#include "toridraw_model.h"

#include <assert.h>
#include <stdbool.h>
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
    char* data = malloc((size_t)n + 1);
    assert(data);
    size_t const got = fread(data, 1, (size_t)n, f);
    fclose(f);
    if( got != (size_t)n )
    {
        fprintf(stderr, "short read on %s\n", path);
        exit(1);
    }
    data[n] = '\0';
    *out_size = (int)n;
    return data;
}

/* The seq's mayamask= lines, as the config dumps them. */
static int
read_masks(
    const char* content,
    const char* seq,
    bool* masks)
{
    char path[1024];
    char header[256];
    int size = 0;
    int count = 0;

    snprintf(path, sizeof(path), "%s/configs/all.seq", content);
    char* text = read_file(path, &size);
    snprintf(header, sizeof(header), "\n[%s]\n", seq);
    char* at = strstr(text, header);
    if( !at )
    {
        fprintf(stderr, "no [%s] in %s\n", seq, path);
        exit(1);
    }
    at += strlen(header);
    char* end = strstr(at, "\n[");
    if( end )
        *end = '\0';
    for( char* line = strstr(at, "mayamask="); line; line = strstr(line + 1, "mayamask=") )
    {
        int bone = atoi(line + 9);
        if( line[9] < '0' || line[9] > '9' )
            continue;
        assert(bone >= 0 && bone < 256);
        masks[bone] = true;
        count++;
    }
    free(text);
    return count;
}

static struct ToriDraw_SkeletalAnim*
bake(
    const char* content,
    int seq_id,
    int maya_file,
    struct RSCache_Dat2Framemap** fm_out,
    struct RSCache_Dat2SkeletalBase** base_out)
{
    char path[1024];
    int size = 0;

    snprintf(path, sizeof(path), "%s/animayas/animaya_%d.animaya", content, maya_file);
    char* data = read_file(path, &size);
    struct RSCache_Dat2AnimMaya* maya = RSCache_Dat2AnimMayaNewDecode(maya_file << 16, data, size);
    assert(maya);
    free(data);

    if( !*fm_out )
    {
        snprintf(path, sizeof(path), "%s/framemaps/base_%d.base", content, maya->base_id);
        data = read_file(path, &size);
        *fm_out = RSCache_Dat2FramemapNewDecodeCodec(
            maya->base_id, data, size, RSCACHE_CODEC_FRAMEMAP_V1);
        assert(*fm_out);
        free(data);
        *base_out = RSCache_Dat2SkeletalBaseNewFromFramemap(*fm_out);
        assert(*base_out);
    }
    CHECK((*base_out)->id == maya->base_id);

    struct ToriDraw_SkeletalAnim* sk =
        ToriDraw_SkeletalAnimFromRSCache(seq_id, maya, *base_out, *fm_out, 0);
    assert(sk);
    RSCache_Dat2AnimMayaFree(maya);
    return sk;
}

/* One vertex per bone, at a spot off every axis, weighted wholly to it. */
static struct ToriDraw_Model*
bone_probe_model(int bones)
{
    struct ToriDraw_Model* model = ToriDraw_ModelNew(bones, 0, 0);
    model->vertices_x = calloc((size_t)bones, sizeof(*model->vertices_x));
    model->vertices_y = calloc((size_t)bones, sizeof(*model->vertices_y));
    model->vertices_z = calloc((size_t)bones, sizeof(*model->vertices_z));
    model->animaya_vertex_count = bones;
    model->animaya_group_counts = calloc((size_t)bones, 1);
    model->animaya_groups = calloc((size_t)bones, sizeof(uint8_t*));
    model->animaya_scales = calloc((size_t)bones, sizeof(uint8_t*));
    assert(model->vertices_x && model->vertices_y && model->vertices_z);
    assert(model->animaya_group_counts && model->animaya_groups && model->animaya_scales);
    for( int b = 0; b < bones; b++ )
    {
        model->vertices_x[b] = (vertexint_t)(40 + 3 * b);
        model->vertices_y[b] = (vertexint_t)(-60 - 5 * b);
        model->vertices_z[b] = (vertexint_t)(25 + 2 * b);
        model->animaya_group_counts[b] = 1;
        model->animaya_groups[b] = malloc(1);
        model->animaya_scales[b] = malloc(1);
        assert(model->animaya_groups[b] && model->animaya_scales[b]);
        model->animaya_groups[b][0] = (uint8_t)b;
        model->animaya_scales[b][0] = 255;
    }
    ToriDraw_ModelCaptureOriginalVertices(model);
    return model;
}

/* vertex b of `posed` from `model`, as three ints. */
static void
vertex(
    const struct ToriDraw_Model* model,
    int b,
    int* out)
{
    out[0] = model->vertices_x[b];
    out[1] = model->vertices_y[b];
    out[2] = model->vertices_z[b];
}

/* Within a unit: the palette composes in rscache, the merge in toridraw. */
static bool
near(
    const int* a,
    const int* b)
{
    for( int k = 0; k < 3; k++ )
        if( abs(a[k] - b[k]) > 1 )
            return false;
    return true;
}

/* Whether bone b and every ancestor share one mask value: then its matrix is
 * wholly one animation's and must equal that animation's own palette. */
static int
chain_side(
    const struct ToriDraw_SkeletalAnim* sk,
    const bool* masks,
    int b)
{
    bool const side = masks[b];
    for( int p = sk->parents[b]; p >= 0; p = sk->parents[p] )
        if( masks[p] != side )
            return -1;
    return side ? 1 : 0;
}

int
main(int argc, char** argv)
{
    if( argc < 2 )
    {
        fprintf(stderr, "usage: %s <osrs-content dir>\n", argv[0]);
        return 2;
    }
    const char* content = argv[1];
    struct RSCache_Dat2Framemap* fm = NULL;
    struct RSCache_Dat2SkeletalBase* base = NULL;
    bool masks[256] = { 0 };
    bool none[256] = { 0 };
    bool all[256];

    int const masked =
        read_masks(content, "npc_ocellus_phase1_basic_projectile_start", masks);
    CHECK(masked > 0);
    memset(all, 1, sizeof(all));

    struct ToriDraw_SkeletalAnim* action = bake(content, 1, 994, &fm, &base);
    struct ToriDraw_SkeletalAnim* walk = bake(content, 2, 1004, &fm, &base);
    CHECK(ToriDraw_SkeletalAnimSharesRig(action, walk));
    CHECK(action->locals && action->parents && action->inverse_binds);

    int const bones = action->bone_count;
    struct ToriDraw_Model* model = bone_probe_model(bones);
    struct ToriDraw_Model* ref_action = bone_probe_model(bones);
    struct ToriDraw_Model* ref_walk = bone_probe_model(bones);

    int whole_action = 0;
    int whole_walk = 0;
    int mixed_differs = 0;
    int const frames[] = { 0, 7, 19, 33 };
    for( size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); i++ )
    {
        int const fa = frames[i] % action->frame_count;
        int const fw = (frames[i] * 3 + 5) % walk->frame_count;
        ToriDraw_ModelAnimateSkeletal(ref_action, action, fa);
        ToriDraw_ModelAnimateSkeletal(ref_walk, walk, fw);

        /* No bone masked: the action's own pose. */
        ToriDraw_ModelAnimateSkeletalMasked(model, action, fa, walk, fw, none);
        for( int b = 0; b < bones; b++ )
        {
            int got[3], want[3];
            vertex(model, b, got);
            vertex(ref_action, b, want);
            CHECK(near(got, want));
        }

        /* Every bone masked: the walk's own pose. */
        ToriDraw_ModelAnimateSkeletalMasked(model, action, fa, walk, fw, all);
        for( int b = 0; b < bones; b++ )
        {
            int got[3], want[3];
            vertex(model, b, got);
            vertex(ref_walk, b, want);
            CHECK(near(got, want));
        }

        /* The seq's own mask: a bone whose whole chain is on one side is
         * that animation's; one that hangs off the other side's bone is
         * neither (it rides the other animation's parent). */
        ToriDraw_ModelAnimateSkeletalMasked(model, action, fa, walk, fw, masks);
        for( int b = 0; b < bones; b++ )
        {
            int got[3], a[3], w[3];
            vertex(model, b, got);
            vertex(ref_action, b, a);
            vertex(ref_walk, b, w);
            int const side = chain_side(action, masks, b);
            if( side == 0 )
            {
                CHECK(near(got, a));
                whole_action++;
            }
            else if( side == 1 )
            {
                CHECK(near(got, w));
                whole_walk++;
            }
            else if( !near(got, a) && !near(got, w) )
                mixed_differs++;
        }
    }
    /* Ocellus's mask holds the root (bone 0): every action bone hangs off a
     * walk-driven parent, so none is wholly the action's, and each of them
     * must have moved off BOTH plain poses -- the volley's arms carried by
     * the walk's body. */
    CHECK(whole_walk > 0);
    CHECK(mixed_differs > 0);

    ToriDraw_ModelFree(ref_walk);
    ToriDraw_ModelFree(ref_action);
    ToriDraw_ModelFree(model);
    ToriDraw_SkeletalAnimFree(walk);
    ToriDraw_SkeletalAnimFree(action);
    RSCache_Dat2SkeletalBaseFree(base);
    RSCache_Dat2FramemapFree(fm);

    if( g_failures )
    {
        fprintf(stderr, "toridraw_skeletal_walkmerge_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf(
        "toridraw_skeletal_walkmerge_test: ok (%d bones, %d masked; %d action / %d walk / %d "
        "mixed-chain poses)\n",
        bones, masked, whole_action, whole_walk, mixed_differs);
    return 0;
}
