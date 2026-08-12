/*
 * Per-face attribute dump for one OB3 model, as JSON lines on stdout.
 *
 * Written to answer localized "what IS this face" questions — which decal
 * shells overlap the QBD's neck, what materials and alphas they carry —
 * without another detour through a hex editor. One line per face:
 *
 *   {"i":90,"a":..,"b":..,"c":..,"cx":..,"cy":..,"cz":..,"color":..,
 *    "prio":4,"alpha":0,"info":0,"tex":-1,"texco":-1}
 *
 * plus one {"vertex_count":..} header. Faces whose three vertex positions
 * each coincide with some other face's vertex positions are the coplanar
 * decal candidates; that join is left to the caller (python), which is why
 * the dump includes raw coordinates rather than conclusions.
 *
 *   make -C src rs2012-model-faces
 *   src/build/rs2012_model_faces model.ob3 > faces.jsonl
 */
#include "datatypes/model.h"

#include <stdio.h>
#include <stdlib.h>

static uint8_t*
read_file(const char* path, int* out_size)
{
    FILE* fp = fopen(path, "rb");
    if (!fp)
    {
        fprintf(stderr, "cannot open %s\n", path);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t* bytes = malloc((size_t)size);
    if (!bytes || fread(bytes, 1, (size_t)size, fp) != (size_t)size)
    {
        fprintf(stderr, "short read on %s\n", path);
        exit(1);
    }
    fclose(fp);
    *out_size = (int)size;
    return bytes;
}

int
main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: rs2012_model_faces model.ob3\n");
        return 1;
    }
    int size = 0;
    uint8_t* bytes = read_file(argv[1], &size);
    struct RSCache_Model* m = RSCache_ModelNewDecode(bytes, size);
    if (!m)
    {
        fprintf(stderr, "%s does not decode as a model\n", argv[1]);
        return 1;
    }

    printf("{\"vertex_count\":%d,\"face_count\":%d,\"textured_face_count\":%d}\n",
           m->vertex_count, m->face_count, m->textured_face_count);

    for (int i = 0; i < m->face_count; i++)
    {
        int a = m->face_indices_a[i];
        int b = m->face_indices_b[i];
        int c = m->face_indices_c[i];
        printf("{\"i\":%d,\"a\":%d,\"b\":%d,\"c\":%d,"
               "\"pa\":[%d,%d,%d],\"pb\":[%d,%d,%d],\"pc\":[%d,%d,%d],"
               "\"color\":%d,\"prio\":%d,\"alpha\":%d,\"info\":%d,"
               "\"tex\":%d,\"texco\":%d}\n",
               i, a, b, c,
               m->vertices_x[a], m->vertices_y[a], m->vertices_z[a],
               m->vertices_x[b], m->vertices_y[b], m->vertices_z[b],
               m->vertices_x[c], m->vertices_y[c], m->vertices_z[c],
               m->face_colors ? m->face_colors[i] : -1,
               m->face_priorities ? m->face_priorities[i] : -1,
               m->face_alphas ? m->face_alphas[i] : 0,
               m->face_infos ? m->face_infos[i] : -1,
               m->face_textures ? m->face_textures[i] : -1,
               m->face_texture_coords ? m->face_texture_coords[i] : -1);
    }
    return 0;
}
