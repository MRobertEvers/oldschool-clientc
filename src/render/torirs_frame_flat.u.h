/* Frame-owned flatten copies. The command ABI and renderer stay unchanged:
 * each copy contains this frame's posed geometry, with the native scene Y
 * scale and HSL override already applied. World-only replay shares ownership
 * with its parent frame, so command pointers remain valid until FrameEnd. */
#include "toridraw_model.h"
#include "toridraw_model_transform.h"

struct ToriRS_FrameFlatModel
{
    struct ToriDraw_Model* model;
    const void* source;
    float scale;
    int hsl;
    struct ToriRS_FrameFlatModel* next;
};

struct ToriRS_FrameFlatArena
{
    struct ToriRS_FrameFlatModel* head;
};

static struct ToriDraw_ModelHandle
frame_flat_model(struct ToriRS_FrameFlatArena* arena,
                 struct ToriDraw_ModelHandle source, float scale, int hsl)
{
    assert(arena);
    assert(scale > 0);
    const void* key = source.kind == TORIDRAWMK_GROUND
        ? (const void*)source.u.model.ground : (const void*)ToriDraw_ModelRead(source);
    assert(key);
    for( struct ToriRS_FrameFlatModel* p = arena->head; p; p = p->next )
        if( p->source == key && p->scale == scale && p->hsl == hsl )
            return ToriDraw_ModelHandleOwned(p->model);
    struct ToriDraw_Model* copy;
    if( source.kind == TORIDRAWMK_GROUND )
    {
        const struct ToriDraw_ModelGround* g = source.u.model.ground;
        copy = ToriDraw_ModelNew(g->vertex_count, g->face_count, 0);
#define COPY_GROUND(field, count) \
        copy->field = g->field && (count) > 0 \
            ? ToriDraw_BufCopy(g->field, (size_t)(count), sizeof(*g->field)) : NULL
        COPY_GROUND(vertices_x, g->vertex_count);
        COPY_GROUND(vertices_y, g->vertex_count);
        COPY_GROUND(vertices_z, g->vertex_count);
        COPY_GROUND(face_indices_a, g->face_count);
        COPY_GROUND(face_indices_b, g->face_count);
        COPY_GROUND(face_indices_c, g->face_count);
        COPY_GROUND(face_colors_a, g->face_count);
        COPY_GROUND(face_colors_b, g->face_count);
        COPY_GROUND(face_colors_c, g->face_count);
#undef COPY_GROUND
    }
    else
    {
        assert(ToriDraw_ModelKindIsFull(source.kind));
        copy = ToriDraw_ModelCopy(ToriDraw_ModelRead(source));
    }
    assert(copy);
    for( int i = 0; i < copy->vertex_count; ++i )
        copy->vertices_y[i] = (vertexint_t)((float)copy->vertices_y[i] * scale);
    for( int i = 0; i < copy->face_count; ++i )
    {
        /* Keep native hidden faces hidden; every visible face gets one HSL. */
        if( copy->face_colors_a ) copy->face_colors_a[i] = (hsl16_t)hsl;
        if( copy->face_colors_b ) copy->face_colors_b[i] = (hsl16_t)hsl;
        if( copy->face_colors_c && copy->face_colors_c[i] != (hsl16_t)-2 )
            copy->face_colors_c[i] = (hsl16_t)hsl;
        if( copy->face_textures ) copy->face_textures[i] = -1;
    }
    ToriDraw_ModelSetBoundsCylinder(copy);
    struct ToriRS_FrameFlatModel* entry = calloc(1, sizeof(*entry));
    assert(entry);
    *entry = (struct ToriRS_FrameFlatModel){copy, key, scale, hsl, arena->head};
    arena->head = entry;
    return ToriDraw_ModelHandleOwned(copy);
}

static void
frame_flat_free(struct ToriRS_FrameFlatArena* arena)
{
    if( !arena ) return;
    struct ToriRS_FrameFlatModel* p = arena->head;
    while( p )
    {
        struct ToriRS_FrameFlatModel* next = p->next;
        ToriDraw_ModelFree(p->model);
        free(p);
        p = next;
    }
    free(arena);
}
