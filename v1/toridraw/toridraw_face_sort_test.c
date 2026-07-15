/*
 * Regression test for stale depth-bucket face indices in ToriDraw_ComputeProjectedFaceOrder.
 *
 * Build and run:
 *   cc -std=c11 -Wall -Wextra -o toridraw_face_sort_test src2/toridraw/toridraw_face_sort_test.c
 *   ./toridraw_face_sort_test
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int16_t faceint_t;

#define DEPTH_LEVELS 1500
#define DEPTH_STRIDE 512

static int
emit_face_order(
    int* face_draw_order,
    faceint_t* face_depth_buckets,
    faceint_t* face_depth_bucket_counts,
    int model_min_depth,
    int model_max_depth)
{
    int order_index = 0;
    for( int depth = model_max_depth; depth < 1500 && depth >= model_min_depth; depth-- )
    {
        int bucket_count = (int)face_depth_bucket_counts[depth];
        if( bucket_count == 0 )
            continue;

        faceint_t* faces = &face_depth_buckets[depth << 9];
        for( int j = 0; j < bucket_count; j++ )
            face_draw_order[order_index++] = faces[j];
    }
    return order_index;
}

static int
max_face_index(const int* order, int count)
{
    int max_face = -1;
    for( int i = 0; i < count; i++ )
    {
        if( order[i] > max_face )
            max_face = order[i];
    }
    return max_face;
}

static void
seed_large_model_bucket(faceint_t* counts, faceint_t* buckets, int depth, int stale_faces)
{
    counts[depth] = (faceint_t)stale_faces;
    for( int i = 0; i < stale_faces; i++ )
        buckets[(depth << 9) + i] = (faceint_t)(100 + i);
}

static void
test_partial_clear_leaves_stale_faces(void)
{
    static faceint_t buckets[DEPTH_LEVELS * DEPTH_STRIDE];
    static faceint_t counts[DEPTH_LEVELS];
    static int order[256];

    const int model_min_depth = 91;
    const int stale_depth = 250;
    const int small_face_count = 120;

    memset(buckets, 0, sizeof(buckets));
    memset(counts, 0, sizeof(counts));
    seed_large_model_bucket(counts, buckets, stale_depth, 50);

    size_t clear_buckets = (size_t)(model_min_depth * 2 + 1);
    memset(counts, 0, clear_buckets * sizeof(counts[0]));

    for( int f = 0; f < 3; f++ )
    {
        int count = counts[stale_depth];
        counts[stale_depth] = (faceint_t)(count + 1);
        buckets[(stale_depth << 9) + count] = (faceint_t)f;
    }

    int order_count =
        emit_face_order(order, buckets, counts, stale_depth, stale_depth);

    assert(max_face_index(order, order_count) >= small_face_count);
    printf("partial clear reproduces stale face indices (max_face=%d)\n",
           max_face_index(order, order_count));
}

static void
test_full_clear_keeps_faces_in_range(void)
{
    static faceint_t buckets[DEPTH_LEVELS * DEPTH_STRIDE];
    static faceint_t counts[DEPTH_LEVELS];
    static int order[256];

    const int stale_depth = 250;
    const int small_face_count = 3;

    memset(buckets, 0, sizeof(buckets));
    memset(counts, 0, sizeof(counts));
    seed_large_model_bucket(counts, buckets, stale_depth, 50);

    memset(counts, 0, DEPTH_LEVELS * sizeof(counts[0]));

    for( int f = 0; f < small_face_count; f++ )
    {
        int count = counts[stale_depth];
        counts[stale_depth] = (faceint_t)(count + 1);
        buckets[(stale_depth << 9) + count] = (faceint_t)f;
    }

    int order_count =
        emit_face_order(order, buckets, counts, stale_depth, stale_depth);

    int max_face = max_face_index(order, order_count);
    assert(max_face < small_face_count);
    printf("full clear keeps face indices in range (max_face=%d count=%d)\n", max_face, order_count);
}

int
main(void)
{
    test_partial_clear_leaves_stale_faces();
    test_full_clear_keeps_faces_in_range();
    printf("toridraw_face_sort_test: ok\n");
    return 0;
}
