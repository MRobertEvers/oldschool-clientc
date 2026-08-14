#ifndef EV_PLAYER_H
#define EV_PLAYER_H

/*
 * A PLAYER, with equipment and an attached graphic — the other half of
 * ev_build.h, which only ever knew how to build an npc.
 *
 * An npc is one config with a model list on it. A player is not: it is a set of
 * identity kits plus the wear models of whatever is equipped, and a
 * player-attached spotanim is not a scene object at all — the client MERGES the
 * posed graphic into the player's own model (`app_world_sync_one_entity_
 * spotanim`, src/app.c). Anything that wants to see a weapon animation the way
 * the game draws it has to reproduce that merge, because the merge is what
 * decides where the graphic lands.
 *
 * Everything here follows the client's own order, in the same spirit as
 * ev_build.c: PlayerModel_BuildFromAppearance (src/engine/entity_model_build.c)
 * for the body, app_world_build_spotanim_model + app_world_sync_one_entity_
 * spotanim (src/app.c) for the graphic.
 */

#include "asset_access.h"

struct ToriDraw_Model;
struct ToriDraw_Animation;

#define EV_PLAYER_MAX_WORN 12
#define EV_PLAYER_MAX_PARTS 64
/** Identity-kit body parts: 0 hair, 1 jaw, 2 torso, 3 arms, 4 hands, 5 legs,
 *  6 feet — engine/player_appearance.h's PLAYER_APPEARANCE_PARTS. */
#define EV_PLAYER_PARTS 7
/** How far to scan idk configs for the default body, matching
 *  PLAYER_IDK_SCAN_MAX. */
#define EV_PLAYER_IDK_SCAN_MAX 1024

struct EV_PlayerSpec
{
    /** 0 male (manwear models), 1 female. */
    int gender;
    /** Identity kit per body part, or -1 to take the first selectable kit the
     *  cache offers for that part — what PlayerAppearance_ResolveDefaultMale
     *  does for the design screen. */
    int kits[EV_PLAYER_PARTS];
    /** Equipped obj ids, in draw order. -1 entries are skipped. */
    int worn[EV_PLAYER_MAX_WORN];
    int worn_count;
};

/**
 * Which source contributed which vertices and faces to the merged player.
 *
 * This is the whole point of building the player here rather than reaching for
 * an existing composite: to measure where the blade is during a swing you have
 * to know which of the merged model's vertices ARE the blade, and after
 * ToriDraw_ModelMerge that information exists nowhere else.
 */
struct EV_PlayerPart
{
    /** Obj id when `is_obj`, identity-kit id otherwise. */
    int source_id;
    int is_obj;
    int model_id;
    int vertex_first;
    int vertex_count;
    int face_first;
    int face_count;
};

struct EV_PlayerPartMap
{
    int count;
    struct EV_PlayerPart parts[EV_PLAYER_MAX_PARTS];
};

/** Default male: every kit auto-resolved, nothing worn. */
void
ev_player_spec_init(struct EV_PlayerSpec* spec);

/**
 * Composite the player: identity-kit part models plus the gendered wear models
 * of every worn obj, each recoloured by its own record, merged, lit and bounded.
 *
 * `out_map` is optional and receives the vertex/face range each source occupies
 * in the merged model.
 *
 * Design colours are NOT applied: this builds the palette-identity avatar (all
 * five colour slots 0), which is what PlayerModel_BuildFromAppearance produces
 * for that input, so the palette tables do not need a second copy here. Nothing
 * downstream measures colour.
 *
 * Returns an owned model, or NULL when nothing resolved.
 */
struct ToriDraw_Model*
ev_build_player_model(
    struct Tool_Dat2Cache* c,
    const struct EV_PlayerSpec* spec,
    struct EV_PlayerPartMap* out_map);

/**
 * The drawable model for a spotanim, with the record's static transforms baked
 * in — app_world_build_spotanim_model, minus the texture work the viewer has no
 * material table for.
 *
 * `model_file_override`, when non-NULL, is a path to a raw model record on disk
 * that replaces the cache's model for this spotanim. That is not a debug
 * convenience: the arc this tool exists to measure is an EXPORTED asset that the
 * content tree has since edited (a .model under `OSRS-Content`), so the
 * bytes the game draws and the bytes in cache.osrs239 are different models, and
 * a measurement against the wrong one is worse than none.
 *
 * `out_seq_id` receives the record's animation id (-1 when it has none).
 * Returns an owned model or NULL.
 */
struct ToriDraw_Model*
ev_build_spotanim_model(
    struct Tool_Dat2Cache* c,
    int spotanim_id,
    const char* model_file_override,
    int* out_seq_id);

/*
 * The merge itself is NOT here. It is pure toridraw — a pose, a translate and a
 * combine — so it lives in ev_render.c, where the browser module can link it
 * too. Splitting it the other way would mean the page and this tool merged by
 * two different routines, and the merge is the step under test.
 */

#endif
