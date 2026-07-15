#ifndef TRSPK_BATCH16_H
#define TRSPK_BATCH16_H

#include "trspk_vertex.h"

#include <stdint.h>

#define TRSPK_BATCH16_MAX_VERTICES 65535

struct TRSPK_Batch16
{
    struct TRSPK_VBO* vbo;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t index_offset;
};

#endif