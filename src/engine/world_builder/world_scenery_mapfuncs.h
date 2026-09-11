#ifndef WORLD_SCENERY_MAPFUNCS_H
#define WORLD_SCENERY_MAPFUNCS_H

#include <stdbool.h>

/*
 * The dat1 "mapfunction" minimap icons that must NOT be nudged off their tile.
 *
 * Where the list comes from
 * -------------------------
 * Verbatim from the reference client, `Client-TS/src/client/Client.ts:5583`
 * (`minimapBuildBuffer`, the gather that fills activeMapFunction*):
 *
 *     if (func !== 22 && func !== 29 && func !== 34 && func !== 36 &&
 *         func !== 46 && func !== 47 && func !== 48) {
 *         ...10-step collision-respecting random walk, +/-3 tiles...
 *     }
 *
 * Every other gathered icon is jittered by that walk so a cluster of icons on
 * adjacent tiles does not draw on top of itself; these seven are pinned to the
 * loc's own tile. The reference states no reason and names no constant — it is
 * seven bare integers in the middle of the walk's condition.
 *
 * What the numbers are
 * --------------------
 * `func` is `LocType.mapfunction` (loc opcode 60). On a dat1 cache it is a
 * frame index into the 50-frame `mapfunction` Pix32 atlas in the media jagfile
 * (`STATIC_SPRITE_MAPFUNCTION`, engine/static_sprites.c) — a raw sprite index,
 * so the atlas is the only thing that can say what an entry means.
 *
 * The names below were read off that atlas and then confirmed by name. The
 * dat1 frame index survived into OldSchool as the sprite archive
 * `mapfunction_<frame>` (frame N of cache254's atlas is pixel-identical to
 * osrs239's `mapfunction_N`), and osrs239 does carry symbols: the sprite is
 * pointed at by one `mapelement`, and the locs that place that mapelement are
 * named. Frame -> osrs239 sprite id -> mapelement -> loc symbol:
 *
 *     22 -> 1470 -> mapelement 24 -> gem_store_icon
 *     29 -> 1477 -> mapelement 31 -> silk_store_icon
 *     34 -> 1482 -> mapelement 36 -> rare_trees_icon
 *     36 -> 1484 -> mapelement 38 -> food_store_icon
 *     46 -> 1494 -> mapelement 48 -> silver_store_icon
 *     47 -> 1495 -> mapelement 49 -> fur_store_icon
 *     48 -> 1496 -> mapelement 50 -> spice_store_icon
 *
 * Which reads as a market row: the stall icons (gem / silk / food / silver /
 * fur / spice) plus rare trees. Those sit on one specific stall or tree in a
 * dense line of them, where a +/-3 tile jitter would slide the icon onto a
 * neighbouring stall and mislabel it.
 *
 * The dat1 locs carrying them are pure markers — loc ids 2733 + func in
 * cache254, no name, no ops, all sharing model 1105 — so there is nothing on
 * the loc side to key this off; the frame index is the only identity.
 *
 * dat2 is a different number space
 * --------------------------------
 * On dat2/OSRS `func` is a **mapelement id**, not an atlas index (see
 * `app_mapfunction_scene_id`, src/app.c). Testing these seven against a
 * mapelement id compares two unrelated numberings: on osrs239 it would pin
 * mapelements 22/29/34/36/46/47/48, which are the herbalist / clothing / mace /
 * rare-trees / mining / chain / silver *store* icons — one accidental overlap
 * (rare trees is mapelement 36, i.e. dat1 frame 34's icon) and six wrong ones.
 * Hence the epoch gate at the call site: no dat2 icon is exempt, because the
 * dat2 exemption set has not been established.
 */

/** dat1 mapfunction atlas frames, by the icon each frame draws. */
enum World_MapFunctionDat1
{
    WORLD_MAPFUNC_DAT1_GEM_STORE = 22,
    WORLD_MAPFUNC_DAT1_SILK_STORE = 29,
    WORLD_MAPFUNC_DAT1_RARE_TREES = 34,
    WORLD_MAPFUNC_DAT1_FOOD_STORE = 36,
    WORLD_MAPFUNC_DAT1_SILVER_STORE = 46,
    WORLD_MAPFUNC_DAT1_FUR_STORE = 47,
    WORLD_MAPFUNC_DAT1_SPICE_STORE = 48,
};

/**
 * Does this dat1 atlas frame stay on the loc's own tile?
 *
 * dat1 only — the caller must have established the epoch, because the argument
 * is an atlas frame index and a dat2 `mapfunction` is not one.
 */
static inline bool
World_MapFunctionDat1StaysPut(int func)
{
    switch( func )
    {
    case WORLD_MAPFUNC_DAT1_GEM_STORE:
    case WORLD_MAPFUNC_DAT1_SILK_STORE:
    case WORLD_MAPFUNC_DAT1_RARE_TREES:
    case WORLD_MAPFUNC_DAT1_FOOD_STORE:
    case WORLD_MAPFUNC_DAT1_SILVER_STORE:
    case WORLD_MAPFUNC_DAT1_FUR_STORE:
    case WORLD_MAPFUNC_DAT1_SPICE_STORE:
        return true;
    default:
        return false;
    }
}

#endif
