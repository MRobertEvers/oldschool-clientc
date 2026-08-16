#ifndef TORIRS_COMPONENT_FROM_RSCACHE_H
#define TORIRS_COMPONENT_FROM_RSCACHE_H

#include "engine/torirs_types.h"

#include <rscache.h>

struct ToriRS_Component*
ToriRS_ComponentFromRSCacheDat2(const struct RSCache_Dat2Component* src);

struct ToriRS_ComponentPack*
ToriRS_ComponentPackFromRSCacheDat2(const struct RSCache_Dat2ComponentPack* src);

/**
 * Convert one dat1 IF1 component. Sprite name refs (graphic/activeGraphic/
 * invSlotGraphic) are copied verbatim into sprite_ref/sprite_active_ref/
 * inv_slot_sprite_ref with the numeric graphic ids left at -1 — the dat1 pack
 * load task resolves names to provider sprite ids afterwards. font stays a
 * title-font slot 0-3.
 */
struct ToriRS_Component*
ToriRS_ComponentFromRSCacheDat1(const struct RSCache_Dat1ConfigComponent* src);

/**
 * Build a pack from the DFS subtree rooted at root_id (preorder; children keep
 * dat1 walk layout rel = childX + child->x). Components keep raw dat1 ids.
 * Returns NULL when root_id is missing from the list.
 */
struct ToriRS_ComponentPack*
ToriRS_ComponentPackFromRSCacheDat1(
    const struct RSCache_Dat1ConfigComponentList* list,
    int root_id);

#endif
