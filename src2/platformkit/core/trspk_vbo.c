#include "trspk_vbo.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct TRSPK_VBO*
trspk_vbo_create(
    uint32_t index_count_hint,
    enum TRSPK_VertexFormat format)
{
    struct TRSPK_VBO* vbo = (struct TRSPK_VBO*)malloc(sizeof(struct TRSPK_VBO));
    assert(vbo != NULL);
    memset(vbo, 0, sizeof(struct TRSPK_VBO));

    vbo->format = format;

    if( index_count_hint > 0 )
        trspk_vbo_growby(vbo, index_count_hint);

    return vbo;
}

void
trspk_vbo_growby(
    struct TRSPK_VBO* vbo,
    uint32_t amount)
{
    if( amount == 0 )
    {
        return;
    }

    const uint32_t old_count = vbo->vertex_count;
    const uint32_t new_count = old_count + amount;

    switch( vbo->format )
    {
    case TRSPK_VERTEX_FORMAT_WEBGL1:
    {
        struct TRSPK_VertexWebGL1* grown = (struct TRSPK_VertexWebGL1*)realloc(
            vbo->vertices.as_webgl1, sizeof(struct TRSPK_VertexWebGL1) * new_count);
        assert(grown != NULL);
        vbo->vertices.as_webgl1 = grown;
        memset(&vbo->vertices.as_webgl1[old_count], 0, sizeof(struct TRSPK_VertexWebGL1) * amount);
        break;
    }
    case TRSPK_VERTEX_FORMAT_OPENGL3:
    {
        struct TRSPK_VertexOpenGl3* grown = (struct TRSPK_VertexOpenGl3*)realloc(
            vbo->vertices.as_opengl3, sizeof(struct TRSPK_VertexOpenGl3) * new_count);
        assert(grown != NULL);
        vbo->vertices.as_opengl3 = grown;
        memset(
            &vbo->vertices.as_opengl3[old_count], 0, sizeof(struct TRSPK_VertexOpenGl3) * amount);
        break;
    }
    default:
        assert(false);
        return;
    }

    vbo->vertex_count = new_count;
}

void
trspk_vbo_ensure_capacity(
    struct TRSPK_VBO* vbo,
    uint32_t min_vertex_count)
{
    if( vbo->vertex_count >= min_vertex_count )
        return;

    trspk_vbo_growby(vbo, min_vertex_count - vbo->vertex_count);
}

void
trspk_vbo_free(struct TRSPK_VBO* vbo)
{
    if( vbo == NULL )
    {
        return;
    }

    switch( vbo->format )
    {
    case TRSPK_VERTEX_FORMAT_WEBGL1:
        free(vbo->vertices.as_webgl1);
        break;
    case TRSPK_VERTEX_FORMAT_OPENGL3:
        free(vbo->vertices.as_opengl3);
        break;
    default:
        break;
    }

    free(vbo);
}
