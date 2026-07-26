#include "trspk_triangles.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
trspk_triangles_free(struct TRSPK_Triangles* triangles)
{
    free(triangles->config);
    triangles->config = NULL;
    triangles->cap = 0u;
}

void
trspk_triangles_ensure(
    struct TRSPK_Triangles* triangles,
    uint32_t tri_count)
{
    if( tri_count <= triangles->cap )
        return;

    uint32_t new_cap = triangles->cap ? triangles->cap * 2u : TRSPK_TRIANGLES_INIT_CAP;
    while( new_cap < tri_count )
        new_cap *= 2u;

    int* grown = (int*)realloc(triangles->config, new_cap * sizeof(int));
    assert(grown != NULL);
    if( triangles->cap > 0u )
        memset(grown + triangles->cap, 0xFF, (new_cap - triangles->cap) * sizeof(int));
    else
        memset(grown, 0xFF, new_cap * sizeof(int));

    triangles->config = grown;
    triangles->cap = new_cap;
}
