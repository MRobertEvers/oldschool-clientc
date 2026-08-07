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
 * Composite a player model from a PLAYER_INFO appearance: the 12 canonical
 * appearance slots (pkt_player_appearance.h — empty, kit or obj, tagged with
 * Appearance_PackKit / Appearance_PackObj), 5 design colours
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
 * Palette size of a design colour slot (0 hair, 1 torso, 2 legs, 3 feet,
 * 4 skin) — reference ClientPlayer.recol1d[part].length, which is what the
 * design screen's colour arrows wrap around. Returns 0 for an out-of-range
 * part.
 */
int
PlayerModel_DesignColourCount(int part);

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

/*
 * Composite the player's CHATHEAD from the same appearance: the identity-kit
 * *head* models (idk->heads) plus the gendered worn-equipment head models
 * (obj->manhead/womanhead) of the head-bearing slots, design-recoloured like the
 * body (reference ClientPlayer.getHeadModel). Lights + captures the merged model
 * so the interface widget can animate it. Returns an owned model or NULL.
 */
struct ToriDraw_Model*
PlayerHeadModel_BuildFromAppearance(
    struct CacheProvider* provider,
    int const slots[12],
    int const colors[5],
    int gender);

/*
 * List the head model ids the appearance references — identity-kit heads plus
 * the gendered worn-equipment heads — so a task can await CreateTask_ModelLoad
 * for each before compositing the chathead. The idk/obj configs must already be
 * loaded. Returns the count written (capped at cap).
 */
int
PlayerHeadModel_CollectHeadModelIds(
    struct CacheProvider* provider,
    int const slots[12],
    int gender,
    int* out_ids,
    int cap);

#endif
