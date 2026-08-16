#ifndef EV_CONFIG_H
#define EV_CONFIG_H

/*
 * One config record out of a cache, decoded.
 *
 * asset_access.c has a loader per datatype and none for idk, obj, spotanim or
 * loc, and each of those loaders is the same twenty lines around a different
 * decoder. This is that shape, once.
 *
 * It is a file of its own rather than a static helper inside ev_player.c —
 * where it started — because the twenty lines are not boilerplate. They carry
 * the branch between the two record layouts a cache actually uses: sharded
 * across archives, where an id's high bits pick the archive, and one file
 * inside a single config archive. A second copy that got the branch wrong
 * would not fail, it would simply find nothing, and "this cache has no locs"
 * is indistinguishable from a cache that has none.
 */

#include "asset_access.h"

struct RSCache_Dat2ConfigIdk;
struct RSCache_Dat2ConfigObj;
struct RSCache_Dat2ConfigLoc;
struct RSCache_Dat2ConfigSpotanim;

/**
 * The raw bytes of one config record, and the profile that decodes them.
 *
 * `out_profile` is the cache's profile with the *group's* revision applied,
 * which is what the record decoders need: a config archive states the revision
 * its records were written at, and reading them at the cache-wide one decodes
 * at the wrong field widths.
 *
 * Returns 0 when the cache holds no such record. The caller owns `*out_bytes`.
 */
int
ev_config_record(
    struct Tool_Dat2Cache* c,
    enum RSCache_Type type,
    int config_kind,
    int record_id,
    char** out_bytes,
    int* out_size,
    struct RSCache* out_profile);

/* Each returns NULL when the record is absent or does not decode. Free with the
 * matching RSCache_Dat2Config*Free. */

struct RSCache_Dat2ConfigIdk*
ev_idk_load(struct Tool_Dat2Cache* c, int idk_id);

struct RSCache_Dat2ConfigObj*
ev_obj_load(struct Tool_Dat2Cache* c, int obj_id);

struct RSCache_Dat2ConfigLoc*
ev_loc_load(struct Tool_Dat2Cache* c, int loc_id);

struct RSCache_Dat2ConfigSpotanim*
ev_spotanim_load(struct Tool_Dat2Cache* c, int spotanim_id);

#endif
