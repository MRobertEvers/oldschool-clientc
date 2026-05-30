#ifndef TRSPK_IBO_H
#define TRSPK_IBO_H

#include "trspk_indices.h"

#include <stdint.h>

struct TRSPK_IBO
{
    uint32_t index_count;
    uint32_t offset;
    enum TRSPK_IndexFormat index_format;
    union
    {
        uint32_t* as_u32;
        uint16_t* as_u16;
    } indices;
};

struct TRSPK_IBO*
trspk_ibo_create(
    uint32_t index_count,
    enum TRSPK_IndexFormat index_format);

void
trspk_ibo_free(struct TRSPK_IBO* ibo);

#endif