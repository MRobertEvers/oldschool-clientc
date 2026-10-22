#include "trspk_element_mask.h"
#include "../trspk_alloc.h"
#include "toridraw_element_id.h"

#include <assert.h>
#include <string.h>

void
trspk_element_mask_set(
    struct TRSPK_ElementMask* mask,
    int element_id)
{
    uint32_t index;
    assert(mask);
    assert(element_id >= 0);
    index = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id);
    if( index >= mask->capacity )
    {
        uint32_t capacity = mask->capacity ? mask->capacity : 1024u;
        uint32_t* grown;
        while( capacity <= index )
        {
            assert(capacity <= UINT32_MAX / 2u);
            capacity *= 2u;
        }
        grown = (uint32_t*)TRSPK_REALLOC(mask->bits, (size_t)(capacity / 32u) * sizeof(uint32_t));
        assert(grown);
        memset(
            grown + mask->capacity / 32u,
            0,
            (size_t)((capacity - mask->capacity) / 32u) * sizeof(uint32_t));
        mask->bits = grown;
        mask->capacity = capacity;
    }
    mask->bits[index >> 5] |= 1u << (index & 31u);
}

bool
trspk_element_mask_test(
    const struct TRSPK_ElementMask* mask,
    int element_id)
{
    uint32_t index;
    assert(mask);
    if( element_id < 0 )
        return false;
    index = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id);
    return index < mask->capacity && (mask->bits[index >> 5] & (1u << (index & 31u))) != 0u;
}

void
trspk_element_mask_clear(struct TRSPK_ElementMask* mask)
{
    assert(mask);
    if( mask->bits )
        memset(mask->bits, 0, (size_t)(mask->capacity / 32u) * sizeof(uint32_t));
}

void
trspk_element_mask_free(struct TRSPK_ElementMask* mask)
{
    if( !mask )
        return;
    TRSPK_FREE(mask->bits);
    mask->bits = NULL;
    mask->capacity = 0u;
}
