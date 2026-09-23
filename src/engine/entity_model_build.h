#ifndef ENGINE_ENTITY_MODEL_BUILD_H
#define ENGINE_ENTITY_MODEL_BUILD_H

/*
 * Entity model composition from ALREADY-CACHED configs/models — pure CPU, no
 * IO. Callers (the entity-sync exec tasks, spawn tasks) await the idk / obj
 * / model loads first. The player body is all or nothing: it is not built
 * until every part is resident, matching the reference's isReady gate.
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
 * Returns an owned model, or NULL when the appearance is not resident (see
 * PlayerModel_AppearanceResident) or names no model at all. Never a partial
 * body: the caller keeps whatever it drew before and asks again later.
 */
struct ToriDraw_Model*
PlayerModel_BuildFromAppearance(
    struct CacheProvider* provider,
    int const slots[12],
    int const colors[5],
    int gender);

/*
 * Is every idk / obj config the appearance names resident, and every model
 * those configs name for `gender`? What PlayerModel_BuildFromAppearance
 * requires before it builds anything.
 */
int
PlayerModel_AppearanceResident(
    struct CacheProvider* provider,
    int const slots[12],
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
 * Wear position of design part `part` (0 hair, 1 jaw, 2 torso, 3 arms,
 * 4 hands, 5 legs, 6 feet) — PlayerComposition's own table
 * (Statics.method8884 in the 239 client). The appearance array is indexed by
 * WEAR POSITION while the design panel and the idk table speak in body parts,
 * so anything that writes an identity kit into an appearance goes through
 * this. Returns -1 for an out-of-range part.
 *
 * The server derives the same seven numbers from the other end
 * (`k_idk_bodypart_wearpos`, torirs_server_content.c, transcribed from the
 * reference's `Player.body`). The two agreeing is the check worth recording:
 * they came from different sources.
 */
int
PlayerModel_DesignPartWearpos(int part);

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
 * so the interface widget can animate it. Returns an owned model, or NULL when
 * the appearance is not resident or names no head model at all. Never a partial
 * head — the caller caches what it gets and never rebuilds it, so a head short
 * one part would be that player's face for the rest of the session.
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
