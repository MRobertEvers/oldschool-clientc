#ifndef TRSPK_DRAWRANGEEX_H
#define TRSPK_DRAWRANGEEX_H

#include "trspk_ibo.h"

#include <stdint.h>

struct TRSPK_DrawRangeList;
struct TRSPK_IBOChain;
struct TRSPK_Triangles;

uint32_t
trspk_drawrangeex_build16(
    struct TRSPK_DrawRangeList* draw_ranges,
    const struct TRSPK_Triangles* const triangles_by_group[TRSPK_VBO_GROUP_COUNT],
    const struct TRSPK_IBOChain* ibo_chain,
    uint16_t* dst);

uint32_t
trspk_drawrangeex_build32(
    struct TRSPK_DrawRangeList* draw_ranges,
    const struct TRSPK_Triangles* const triangles_by_group[TRSPK_VBO_GROUP_COUNT],
    const struct TRSPK_IBOChain* ibo_chain,
    uint32_t* dst);

#endif
