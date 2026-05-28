#include "trspk_ibo.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct TRSPK_IBO*
trspk_ibo_create(
    uint32_t index_count,
    enum TRSPK_IndexFormat index_format)
{
    struct TRSPK_IBO* ibo = (struct TRSPK_IBO*)malloc(sizeof(struct TRSPK_IBO));
    assert(ibo != NULL);

    memset(ibo, 0, sizeof(struct TRSPK_IBO));

    ibo->index_count = index_count;
    ibo->index_format = index_format;

    switch( index_format )
    {
    case TRSPK_INDEX_FORMAT_U16:
        ibo->indices.as_u16 = (uint16_t*)malloc(sizeof(uint16_t) * index_count);
        break;
    case TRSPK_INDEX_FORMAT_U32:
        ibo->indices.as_u32 = (uint32_t*)malloc(sizeof(uint32_t) * index_count);
        break;
    default:
        assert(false);
        return NULL;
    }

    return ibo;
}