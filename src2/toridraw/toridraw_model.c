#include "toridraw_model.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriDraw_Normals*
toridraw_normals_new(
    int vertex_count,
    int face_count)
{
    struct ToriDraw_Normals* normals = malloc(sizeof(struct ToriDraw_Normals));
    memset(normals, 0, sizeof(struct ToriDraw_Normals));
    normals->vertex_normals = malloc(sizeof(struct ToriDraw_Normal) * (size_t)vertex_count);
    memset(normals->vertex_normals, 0, sizeof(struct ToriDraw_Normal) * (size_t)vertex_count);
    normals->vertex_normals_count = vertex_count;
    if( face_count > 0 )
    {
        normals->face_normals = malloc(sizeof(struct ToriDraw_Normal) * (size_t)face_count);
        memset(normals->face_normals, 0, sizeof(struct ToriDraw_Normal) * (size_t)face_count);
        normals->face_normals_count = face_count;
    }
    return normals;
}

void
toridraw_normals_free(struct ToriDraw_Normals* normals)
{
    if( !normals )
        return;
    free(normals->vertex_normals);
    free(normals->face_normals);
    free(normals);
}

static int
toridraw_model_needs_face_normals(struct ToriDraw_Model* model)
{
    const int* fi = model->face_infos;
    if( !fi )
        return 0;
    int fc = model->face_count;
    for( int i = 0; i < fc; i++ )
    {
        if( (fi[i] & 0x3) == 1 )
            return 1;
    }
    return 0;
}

void
toridraw_model_alloc_normals(struct ToriDraw_Model* model)
{
    assert(model);
    if( model->normals )
        return;
    int face_n = toridraw_model_needs_face_normals(model) ? model->face_count : 0;
    model->normals = toridraw_normals_new(model->vertex_count, face_n);
}
