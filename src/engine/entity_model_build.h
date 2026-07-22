#ifndef ENGINE_ENTITY_MODEL_BUILD_H
#define ENGINE_ENTITY_MODEL_BUILD_H

/*
 * Entity model composition from ALREADY-CACHED configs/models — pure CPU, no
 * IO. Callers (the entity-sync exec tasks, spawn tasks) await the idk / obj
 * / model loads first; anything still missing is skipped, matching the
 * reference's skip-render-until-loaded behavior.
 */

#include <stdint.h>

struct CacheProvider;
struct ToriDraw_Model;

/*
 * Composite a player model from a PLAYER_INFO appearance: 12 slots
 * (0 empty | 0x100+identity-kit part | 0x200+worn obj), 5 design colours
 * (reference ClientPlayer.recol1d/recol2d), gender picks manwear/womanwear
 * models. Lights + captures the model (ready for a dynamic scene element).
 * Returns an owned model or NULL when nothing resolved.
 */
struct ToriDraw_Model*
PlayerModel_BuildFromAppearance(
    struct CacheProvider* provider,
    int const slots[12],
    int const colors[5],
    int gender);

/*
 * List the cache model ids the appearance references (idk part models +
 * worn-equipment models), so a task can await CreateTask_ModelLoad for each.
 * Configs (idk/obj) must already be loaded for the listing to be complete.
 * Returns the count written (capped at cap).
 */
int
PlayerModel_CollectAppearanceModelIds(
    struct CacheProvider* provider,
    int const slots[12],
    int gender,
    int* out_ids,
    int cap);

#endif
