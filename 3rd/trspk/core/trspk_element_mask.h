#ifndef TRSPK_ELEMENT_MASK_H
#define TRSPK_ELEMENT_MASK_H

#include <stdbool.h>
#include <stdint.h>

/*
 * One bit per scene element index (ToriDraw_ElementIndexOfRaw).
 *
 * What the renderers use it for: the elements a world build no longer
 * describes. A build bakes each element once; when one is later reloaded
 * (MODEL_LOAD), animated (ANIM_LOAD) or removed (MODEL_UNLOAD) its batch pose
 * is stale -- and because the scene hands a freed element index straight back
 * out, the next loc a script spawns usually arrives under the same id. A
 * renderer that resolves batch poses first would draw the old loc over it.
 * The mask says "not from the build", and outlives the batch-pose rebuilds a
 * renderer runs on every batch begin, commit and clear.
 */
struct TRSPK_ElementMask
{
    uint32_t* bits;
    uint32_t capacity; /* elements, a multiple of 32 */
};

void
trspk_element_mask_set(
    struct TRSPK_ElementMask* mask,
    int element_id);

/* False for a negative id: nothing is ever marked under one. */
bool
trspk_element_mask_test(
    const struct TRSPK_ElementMask* mask,
    int element_id);

/* Unmark everything; the storage is kept. */
void
trspk_element_mask_clear(struct TRSPK_ElementMask* mask);

void
trspk_element_mask_free(struct TRSPK_ElementMask* mask);

#endif
