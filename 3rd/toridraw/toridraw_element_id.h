#ifndef TORIDRAW_ELEMENT_ID_H
#define TORIDRAW_ELEMENT_ID_H

#include <stdbool.h>
#include <stdint.h>

/**
 * A scene element id, carrying what kind of thing the world put there.
 *
 * The id has always been a dense index into the scene's element list, and
 * every consumer that wanted to know what an element WAS had to go and ask.
 * `pick_classify_element` is the worst of them: for each non-terrain hit it
 * tried the npc pool, then the player pool, then scenery, then objstacks, and
 * each of those is a linear walk of a pool's active list. O(hits x scene) to
 * answer a question the emitter knew the answer to.
 *
 * So the kind rides in the id's top four bits, exactly as v1 did it
 * (`RS_ENTITY_ID` in v1/world/entity_registry.h: kind << 28 | index). The low
 * 28 bits stay the scene index, so a tagged id is still the thing the scene
 * indexes with once it has been masked -- which is why the masking lives in
 * the scene's two accessors and not at a thousand call sites.
 *
 * The kind alone turns four pool walks into one. It cannot turn that one into
 * a direct lookup, because the low bits have to remain the SCENE index and
 * cannot also be the pool index; v1 had the same constraint and answered it
 * the same way, with a record carrying `world_index` beside the element id.
 *
 * The struct wrapper is the point of the file. A bare `int element_id` is
 * indistinguishable from a pool index, a component id, a server slot or a
 * loc id, and this tree passes all five around. Wrapping it means the compiler
 * refuses the mix-up, and it costs nothing: every accessor below is inline and
 * the struct is one uint32.
 */

enum ToriDraw_ElementKind
{
    TORIDRAW_ELEMENT_KIND_NONE = 0,
    TORIDRAW_ELEMENT_KIND_TERRAIN = 1,
    TORIDRAW_ELEMENT_KIND_SCENERY = 2,
    TORIDRAW_ELEMENT_KIND_PLAYER = 3,
    TORIDRAW_ELEMENT_KIND_NPC = 4,
    TORIDRAW_ELEMENT_KIND_OBJSTACK = 5,
    TORIDRAW_ELEMENT_KIND_PROJECTILE = 6,
    TORIDRAW_ELEMENT_KIND_SPOTANIM = 7,
};

#define TORIDRAW_ELEMENT_KIND_SHIFT 28u
#define TORIDRAW_ELEMENT_KIND_MASK 0xFu
#define TORIDRAW_ELEMENT_INDEX_MASK ((1u << TORIDRAW_ELEMENT_KIND_SHIFT) - 1u)

/** The index no element has. Chosen so ElementId_Raw() of a none-id is -1,
 *  which is the sentinel every existing `int element_id` already uses. */
#define TORIDRAW_ELEMENT_INDEX_NIL TORIDRAW_ELEMENT_INDEX_MASK

struct ElementId
{
    uint32_t _id;
};

static inline enum ToriDraw_ElementKind
ElementId_Kind(struct ElementId id)
{
    return (enum ToriDraw_ElementKind)((id._id >> TORIDRAW_ELEMENT_KIND_SHIFT) &
                                       TORIDRAW_ELEMENT_KIND_MASK);
}

/** The scene index, or -1 when the id names nothing. */
static inline int
ElementId_Index(struct ElementId id)
{
    uint32_t const index = id._id & TORIDRAW_ELEMENT_INDEX_MASK;
    return index == TORIDRAW_ELEMENT_INDEX_NIL ? -1 : (int)index;
}

static inline bool
ElementId_IsNone(struct ElementId id)
{
    return (id._id & TORIDRAW_ELEMENT_INDEX_MASK) == TORIDRAW_ELEMENT_INDEX_NIL;
}

static inline struct ElementId
ElementId_None(void)
{
    struct ElementId id;
    id._id = TORIDRAW_ELEMENT_INDEX_NIL;
    return id;
}

static inline struct ElementId
ElementId_Make(enum ToriDraw_ElementKind kind, int index)
{
    struct ElementId id;
    if( index < 0 )
        return ElementId_None();
    id._id = (((uint32_t)kind & TORIDRAW_ELEMENT_KIND_MASK) << TORIDRAW_ELEMENT_KIND_SHIFT) |
             ((uint32_t)index & TORIDRAW_ELEMENT_INDEX_MASK);
    return id;
}

/** Re-tag without moving the element. */
static inline struct ElementId
ElementId_WithKind(struct ElementId id, enum ToriDraw_ElementKind kind)
{
    return ElementId_Make(kind, ElementId_Index(id));
}

/**
 * The `int` the rest of the tree still passes around, and back again.
 *
 * These two are the bridge, not the destination: an id that has been through
 * Raw() has lost the compiler's help but not the tag, so a consumer that has
 * not been converted yet keeps working and a converted one can recover the
 * kind. A none-id round-trips through -1, which is the sentinel every
 * `int element_id` already tests for.
 */
static inline int
ElementId_Raw(struct ElementId id)
{
    return ElementId_IsNone(id) ? -1 : (int)id._id;
}

static inline struct ElementId
ElementId_FromRaw(int raw)
{
    struct ElementId id;
    if( raw < 0 )
        return ElementId_None();
    id._id = (uint32_t)raw;
    return id;
}

static inline bool
ElementId_Eq(struct ElementId a, struct ElementId b)
{
    return a._id == b._id;
}

/**
 * The scene index of an id that may or may not have been tagged.
 *
 * This is what the scene's accessors and the renderers' per-element tables
 * call: they are handed a raw int from a code path that may predate the tag,
 * and an untagged id is kind NONE, whose low bits are already the index. So
 * masking is correct for both and there is no flag day.
 */
static inline int
ToriDraw_ElementIndexOfRaw(int raw)
{
    return raw < 0 ? raw : (int)((uint32_t)raw & TORIDRAW_ELEMENT_INDEX_MASK);
}

#endif /* TORIDRAW_ELEMENT_ID_H */
