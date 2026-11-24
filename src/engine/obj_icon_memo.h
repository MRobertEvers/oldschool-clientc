#ifndef ENGINE_OBJ_ICON_MEMO_H
#define ENGINE_OBJ_ICON_MEMO_H

/*
 * "Is this item's icon ready, and which scene sprite is it?" -- remembered
 * per item.
 *
 * WHAT IT IS. One entry per obj id (a flat array, no hashing): the count it
 * was asked for, the scene sprite id of its baked shadow icon, and the
 * provider's CacheProvider_AssetDropRevision when that answer was taken. A
 * repeat of the same (obj, count) under the same revision is a load and two
 * compares.
 *
 * WHAT IT REPLACES. Per cc_setobject on an icon cell, the answer was rebuilt
 * from scratch every time: ObjModelLoad_NeedsWork (up to three objtype
 * lookups, the model, each of its textures) and then the bridge's
 * (obj, count) icon map (hmap_hash_bytes + hmap_search). The bank asks it
 * 4,187 times an open, for the same items, and gets the same answer.
 *
 * WHAT IS REMEMBERED. Only a READY answer: NeedsWork said nothing was
 * missing and the bridge had (or baked) the icon. A not-ready answer is not
 * kept -- that path yields or hands the cell to the lazy icon loader, and is
 * the one-off of an item's first appearance.
 *
 * WHAT INVALIDATES IT. A ready answer can only become wrong when something it
 * read LEAVES: an objtype (the item, its count variant, its cert template or
 * link, its placeholder link), its model, or one of the model's textures --
 * retired, evicted, trimmed, cleaned up or replaced. Every one of those moves
 * CacheProvider_AssetDropRevision (cache_provider.c,
 * cache_provider_assets_dropped), and an entry taken under another revision
 * is a miss. An ARRIVAL cannot turn ready into not-ready, so it does not
 * invalidate. The scene sprite id is the bridge's obj_icon_map entry for
 * (obj, count), which is never removed while the bridge lives; the memo lives
 * and dies with the same bridge (UITreeSceneBridge_Init / _Free).
 */

#include <stdint.h>

struct ObjIconMemoEntry
{
    /* 0: nothing remembered. Otherwise the scene sprite id (> 0). */
    int32_t scene_id;
    int32_t count;
    uint64_t revision;
};

struct ObjIconMemo
{
    struct ObjIconMemoEntry* by_id;
    int32_t cap;
};

/* Ids at or above this are not remembered (always a miss). */
#define OBJ_ICON_MEMO_MAX_ID (1 << 20)

/* The remembered scene sprite id for (obj_id, count) under `revision`, or 0. */
static inline int32_t
ObjIconMemo_Get(
    struct ObjIconMemo const* memo,
    int obj_id,
    int count,
    uint64_t revision)
{
    struct ObjIconMemoEntry const* e;
    if( (uint32_t)obj_id >= (uint32_t)memo->cap )
        return 0;
    e = &memo->by_id[obj_id];
    if( e->count != count || e->revision != revision )
        return 0;
    return e->scene_id;
}

/* Remember that (obj_id, count) is ready under `revision` with icon
 * `scene_id` (> 0). Replaces whatever the id held. */
void
ObjIconMemo_Put(
    struct ObjIconMemo* memo,
    int obj_id,
    int count,
    uint64_t revision,
    int32_t scene_id);

/* Release the array. Accepts a memo that never grew. */
void
ObjIconMemo_Free(struct ObjIconMemo* memo);

#endif
