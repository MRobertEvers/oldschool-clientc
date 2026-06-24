#ifndef TRSPK_IBO_H
#define TRSPK_IBO_H

#include "trspk_indices.h"

#include <stdbool.h>
#include <stdint.h>

#define TRSPK_VBO_GROUP_STATIC 0u
#define TRSPK_VBO_GROUP_DYNAMIC 1u
#define TRSPK_VBO_GROUP_COUNT 2u

struct TRSPK_IBO
{
    uint32_t index_count;
    // Opengl es 2 does not support an offset of the index buffer.
    // The VBOs must be split themselves.
    // In D3D9, the index buffer can be offset, so we can use a single index buffer for all models.
    uint32_t offset;
    enum TRSPK_IndexFormat index_format;
    union
    {
        uint32_t* as_u32;
        uint16_t* as_u16;
    } indices;
};

struct TRSPK_IBOChainNode
{
    struct TRSPK_IBO ibo;
    uint32_t capacity;
    uint32_t group;
    struct TRSPK_IBOChainNode* next;
};

struct TRSPK_IBOChain
{
    struct TRSPK_IBOChainNode* head;
    struct TRSPK_IBOChainNode* tail;
    enum TRSPK_IndexFormat index_format;
    struct TRSPK_IBOChainNode* free_head;
};

struct TRSPK_IBO*
trspk_ibo_create(
    uint32_t index_count,
    enum TRSPK_IndexFormat index_format);

void
trspk_ibo_free(struct TRSPK_IBO* ibo);

bool
trspk_ibo_reserve(
    struct TRSPK_IBO* ibo,
    uint32_t index_count);

struct TRSPK_IBOChain*
trspk_ibochain_create(enum TRSPK_IndexFormat index_format);

void
trspk_ibochain_free(struct TRSPK_IBOChain* chain);

void
trspk_ibochain_reset(struct TRSPK_IBOChain* chain);

void
trspk_ibochain_push16(
    struct TRSPK_IBOChain* chain,
    uint32_t group,
    uint32_t offset,
    const uint16_t* indices,
    uint32_t index_count);

void
trspk_ibochain_push32(
    struct TRSPK_IBOChain* chain,
    uint32_t group,
    uint32_t offset,
    const uint32_t* indices,
    uint32_t index_count);

void
trspk_ibochain_pushs16(
    struct TRSPK_IBOChain* chain,
    uint32_t group,
    uint32_t offset,
    const int16_t* indices,
    uint32_t index_count);

void
trspk_ibochain_pushs32(
    struct TRSPK_IBOChain* chain,
    uint32_t group,
    uint32_t offset,
    const int32_t* indices,
    uint32_t index_count);

#endif
