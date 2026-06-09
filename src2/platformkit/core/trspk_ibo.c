#include "trspk_ibo.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t TRSPK_IBOCHAIN_INITIAL_CAPACITY = 64;

static void
trspk_ibo_free_indices(struct TRSPK_IBO* ibo)
{
    if( ibo == NULL )
    {
        return;
    }

    switch( ibo->index_format )
    {
    case TRSPK_INDEX_FORMAT_U16:
        free(ibo->indices.as_u16);
        ibo->indices.as_u16 = NULL;
        break;
    case TRSPK_INDEX_FORMAT_U32:
        free(ibo->indices.as_u32);
        ibo->indices.as_u32 = NULL;
        break;
    default:
        break;
    }
}

static bool
trspk_ibo_alloc_indices(
    struct TRSPK_IBO* ibo,
    uint32_t capacity)
{
    switch( ibo->index_format )
    {
    case TRSPK_INDEX_FORMAT_U16:
        ibo->indices.as_u16 = (uint16_t*)malloc(sizeof(uint16_t) * capacity);
        return ibo->indices.as_u16 != NULL;
    case TRSPK_INDEX_FORMAT_U32:
        ibo->indices.as_u32 = (uint32_t*)malloc(sizeof(uint32_t) * capacity);
        return ibo->indices.as_u32 != NULL;
    default:
        return false;
    }
}

static bool
trspk_ibo_grow_indices(
    struct TRSPK_IBO* ibo,
    uint32_t new_capacity)
{
    switch( ibo->index_format )
    {
    case TRSPK_INDEX_FORMAT_U16:
    {
        uint16_t* grown = (uint16_t*)realloc(ibo->indices.as_u16, sizeof(uint16_t) * new_capacity);
        if( grown == NULL )
        {
            return false;
        }
        ibo->indices.as_u16 = grown;
        return true;
    }
    case TRSPK_INDEX_FORMAT_U32:
    {
        uint32_t* grown = (uint32_t*)realloc(ibo->indices.as_u32, sizeof(uint32_t) * new_capacity);
        if( grown == NULL )
        {
            return false;
        }
        ibo->indices.as_u32 = grown;
        return true;
    }
    default:
        return false;
    }
}

static struct TRSPK_IBOChainNode*
trspk_ibochain_node_create(
    enum TRSPK_IndexFormat index_format,
    uint32_t offset)
{
    struct TRSPK_IBOChainNode* node =
        (struct TRSPK_IBOChainNode*)malloc(sizeof(struct TRSPK_IBOChainNode));
    assert(node != NULL);

    memset(node, 0, sizeof(struct TRSPK_IBOChainNode));

    node->capacity = TRSPK_IBOCHAIN_INITIAL_CAPACITY;
    node->ibo.offset = offset;
    node->ibo.index_format = index_format;
    node->ibo.index_count = 0;

    const bool allocated = trspk_ibo_alloc_indices(&node->ibo, node->capacity);
    assert(allocated);

    return node;
}

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

    const bool allocated = trspk_ibo_alloc_indices(ibo, index_count);
    assert(allocated);

    return ibo;
}

void
trspk_ibo_free(struct TRSPK_IBO* ibo)
{
    if( ibo == NULL )
    {
        return;
    }

    trspk_ibo_free_indices(ibo);
    free(ibo);
}

bool
trspk_ibo_reserve(
    struct TRSPK_IBO* ibo,
    uint32_t index_count)
{
    if( ibo == NULL )
        return false;

    if( index_count == 0u )
        return true;

    if( index_count <= ibo->index_count )
        return true;

    uint32_t new_capacity = ibo->index_count ? ibo->index_count : 1u;
    while( new_capacity < index_count )
        new_capacity *= 2u;

    if( !trspk_ibo_grow_indices(ibo, new_capacity) )
        return false;

    ibo->index_count = new_capacity;
    return true;
}

struct TRSPK_IBOChain*
trspk_ibochain_create(enum TRSPK_IndexFormat index_format)
{
    struct TRSPK_IBOChain* chain = (struct TRSPK_IBOChain*)malloc(sizeof(struct TRSPK_IBOChain));
    assert(chain != NULL);

    memset(chain, 0, sizeof(struct TRSPK_IBOChain));
    chain->index_format = index_format;

    return chain;
}

void
trspk_ibochain_free(struct TRSPK_IBOChain* chain)
{
    if( chain == NULL )
    {
        return;
    }

    struct TRSPK_IBOChainNode* node = chain->head;
    while( node != NULL )
    {
        struct TRSPK_IBOChainNode* next = node->next;
        trspk_ibo_free_indices(&node->ibo);
        free(node);
        node = next;
    }

    free(chain);
}

void
trspk_ibochain_reset(struct TRSPK_IBOChain* chain)
{
    if( chain == NULL || chain->head == NULL )
        return;

    for( struct TRSPK_IBOChainNode* node = chain->head; node != NULL; node = node->next )
        node->ibo.index_count = 0u;

    chain->tail = chain->head;
}

static void
trspk_ibochain_ensure_tail_node(
    struct TRSPK_IBOChain* chain,
    uint32_t offset)
{
    if( chain->tail != NULL && chain->tail->ibo.offset == offset )
    {
        return;
    }

    struct TRSPK_IBOChainNode* node = trspk_ibochain_node_create(chain->index_format, offset);

    if( chain->tail != NULL )
    {
        chain->tail->next = node;
    }
    else
    {
        chain->head = node;
    }

    chain->tail = node;
}

static void
trspk_ibochain_ensure_capacity(
    struct TRSPK_IBOChainNode* tail,
    uint32_t index_count)
{
    const uint32_t needed = tail->ibo.index_count + index_count;
    if( needed <= tail->capacity )
    {
        return;
    }

    uint32_t new_capacity = tail->capacity;
    while( new_capacity < needed )
    {
        new_capacity *= 2u;
    }

    const bool grown = trspk_ibo_grow_indices(&tail->ibo, new_capacity);
    assert(grown);
    tail->capacity = new_capacity;
}

static void
trspk_ibochain_append_u16(
    struct TRSPK_IBOChain* chain,
    uint32_t offset,
    const uint16_t* indices,
    uint32_t index_count)
{
    assert(chain != NULL);
    assert(chain->index_format == TRSPK_INDEX_FORMAT_U16);
    assert(indices != NULL || index_count == 0u);

    if( index_count == 0u )
    {
        return;
    }

    trspk_ibochain_ensure_tail_node(chain, offset);

    struct TRSPK_IBOChainNode* tail = chain->tail;
    trspk_ibochain_ensure_capacity(tail, index_count);

    memcpy(
        tail->ibo.indices.as_u16 + tail->ibo.index_count,
        indices,
        sizeof(uint16_t) * index_count);
    tail->ibo.index_count += index_count;
}

static void
trspk_ibochain_append_u32(
    struct TRSPK_IBOChain* chain,
    uint32_t offset,
    const uint32_t* indices,
    uint32_t index_count)
{
    assert(chain != NULL);
    assert(chain->index_format == TRSPK_INDEX_FORMAT_U32);
    assert(indices != NULL || index_count == 0u);

    if( index_count == 0u )
    {
        return;
    }

    trspk_ibochain_ensure_tail_node(chain, offset);

    struct TRSPK_IBOChainNode* tail = chain->tail;
    trspk_ibochain_ensure_capacity(tail, index_count);

    memcpy(
        tail->ibo.indices.as_u32 + tail->ibo.index_count,
        indices,
        sizeof(uint32_t) * index_count);
    tail->ibo.index_count += index_count;
}

void
trspk_ibochain_push16(
    struct TRSPK_IBOChain* chain,
    uint32_t offset,
    const uint16_t* indices,
    uint32_t index_count)
{
    trspk_ibochain_append_u16(chain, offset, indices, index_count);
}

void
trspk_ibochain_push32(
    struct TRSPK_IBOChain* chain,
    uint32_t offset,
    const uint32_t* indices,
    uint32_t index_count)
{
    trspk_ibochain_append_u32(chain, offset, indices, index_count);
}

void
trspk_ibochain_pushs16(
    struct TRSPK_IBOChain* chain,
    uint32_t offset,
    const int16_t* indices,
    uint32_t index_count)
{
    trspk_ibochain_append_u16(
        chain,
        offset,
        (const uint16_t*)indices,
        index_count);
}

void
trspk_ibochain_pushs32(
    struct TRSPK_IBOChain* chain,
    uint32_t offset,
    const int32_t* indices,
    uint32_t index_count)
{
    trspk_ibochain_append_u32(
        chain,
        offset,
        (const uint32_t*)indices,
        index_count);
}
