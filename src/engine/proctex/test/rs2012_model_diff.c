/*
 * Field-by-field diff of two OB3 models, for exactly one question: did a
 * decode-modify-encode round trip corrupt anything it was not supposed to
 * touch? The write tools verify face counts and priorities; this verifies
 * everything else -- colors, infos, textures, texture coords, bones, alpha,
 * vertices -- and prints the first mismatch of each kind.
 *
 *   make -C src rs2012-model-diff
 *   src/build/rs2012_model_diff original.ob3 rewritten.ob3
 */
#include "datatypes/model.h"
#include <assert.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t*
read_file(const char* path, long* out_size)
{
    FILE* f = fopen(path, "rb");
    uint8_t* bytes;
    long size;
    if( !f )
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bytes = (uint8_t*)malloc((size_t)size);
    assert(bytes);
    if( fread(bytes, 1, (size_t)size, f) != (size_t)size )
    {
        free(bytes);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = size;
    return bytes;
}

#define CMP_ARR(name, a, b, n, fmt, cast)                                                          \
    do                                                                                             \
    {                                                                                              \
        if( (a == NULL) != (b == NULL) )                                                           \
            printf("  %-22s PRESENCE differs: %s vs %s\n", name, a ? "set" : "NULL",               \
                   b ? "set" : "NULL");                                                            \
        else if( a && b )                                                                          \
        {                                                                                          \
            int diffs = 0, first = -1;                                                             \
            for( int i = 0; i < (n); i++ )                                                         \
                if( a[i] != b[i] )                                                                 \
                {                                                                                  \
                    if( first < 0 )                                                                \
                        first = i;                                                                 \
                    diffs++;                                                                       \
                }                                                                                  \
            if( diffs )                                                                            \
                printf("  %-22s %d/%d differ; first [%d]: " fmt " -> " fmt "\n", name, diffs,      \
                       (n), first, cast a[first], cast b[first]);                                  \
            else                                                                                   \
                printf("  %-22s identical (%d)\n", name, (n));                                     \
        }                                                                                          \
    } while( 0 )

int
main(int argc, char** argv)
{
    long sa = 0, sb = 0;
    uint8_t* ba;
    uint8_t* bb;
    struct RSCache_ModelProvenance* pa = NULL;
    struct RSCache_ModelProvenance* pb = NULL;
    struct RSCache_Model* a;
    struct RSCache_Model* b;

    if( argc != 3 )
    {
        fprintf(stderr, "usage: %s a.ob3 b.ob3\n", argv[0]);
        return 2;
    }
    ba = read_file(argv[1], &sa);
    bb = read_file(argv[2], &sb);
    if( !ba || !bb )
        return 1;
    a = RSCache_ModelNewDecodeProvenance(ba, (int)sa, &pa);
    b = RSCache_ModelNewDecodeProvenance(bb, (int)sb, &pb);
    if( !a || !b )
    {
        fprintf(stderr, "decode failed: %p %p\n", (void*)a, (void*)b);
        return 1;
    }

    printf("%s (%ld B) vs %s (%ld B)\n", argv[1], sa, argv[2], sb);
    printf("  vertices %d vs %d, faces %d vs %d, textured slots %d vs %d\n",
           a->vertex_count, b->vertex_count, a->face_count, b->face_count,
           a->textured_face_count, b->textured_face_count);
    printf("  format_version %d vs %d, model_priority %d vs %d\n",
           a->format_version, b->format_version, a->model_priority, b->model_priority);
    printf("  provenance format %d vs %d, header flags[1] %d vs %d\n",
           pa->format, pb->format,
           pa->header_flag_count > 1 ? pa->header_flags[1] : -1,
           pb->header_flag_count > 1 ? pb->header_flags[1] : -1);

    if( a->vertex_count == b->vertex_count )
    {
        CMP_ARR("vertices_x", a->vertices_x, b->vertices_x, a->vertex_count, "%d", (int));
        CMP_ARR("vertices_y", a->vertices_y, b->vertices_y, a->vertex_count, "%d", (int));
        CMP_ARR("vertices_z", a->vertices_z, b->vertices_z, a->vertex_count, "%d", (int));
        CMP_ARR("vertex_bone_map", a->vertex_bone_map, b->vertex_bone_map, a->vertex_count,
                "%d", (int));
    }
    if( a->face_count == b->face_count )
    {
        int const fc = a->face_count;
        CMP_ARR("face_indices_a", a->face_indices_a, b->face_indices_a, fc, "%d", (int));
        CMP_ARR("face_indices_b", a->face_indices_b, b->face_indices_b, fc, "%d", (int));
        CMP_ARR("face_indices_c", a->face_indices_c, b->face_indices_c, fc, "%d", (int));
        CMP_ARR("face_colors", a->face_colors, b->face_colors, fc, "%u", (unsigned));
        CMP_ARR("face_infos", a->face_infos, b->face_infos, fc, "%d", (int));
        CMP_ARR("face_alphas", a->face_alphas, b->face_alphas, fc, "%d", (int));
        CMP_ARR("face_priorities", a->face_priorities, b->face_priorities, fc, "%d", (int));
        CMP_ARR("face_bone_map", a->face_bone_map, b->face_bone_map, fc, "%d", (int));
        CMP_ARR("face_textures", a->face_textures, b->face_textures, fc, "%d", (int));
        CMP_ARR("face_texture_coords", a->face_texture_coords, b->face_texture_coords, fc,
                "%d", (int));
    }
    if( a->textured_face_count == b->textured_face_count && a->textured_face_count > 0 )
    {
        int const t = a->textured_face_count;
        CMP_ARR("textured_p", a->textured_p_coordinate, b->textured_p_coordinate, t, "%d",
                (int));
        CMP_ARR("textured_m", a->textured_m_coordinate, b->textured_m_coordinate, t, "%d",
                (int));
        CMP_ARR("textured_n", a->textured_n_coordinate, b->textured_n_coordinate, t, "%d",
                (int));
    }
    return 0;
}
