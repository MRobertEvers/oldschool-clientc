#ifndef TORIRS_MODEL_INST_CACHE_H
#define TORIRS_MODEL_INST_CACHE_H

/*
 * LRU of lit/transformed ToriDraw_Model bases, sized after Client-TS:
 *   SpotType.modelCache 30, NpcType 30, LocType.mc1 500 / mc2 30, ObjType 50.
 *
 * Callers never retain the cached pointer — they ToriDraw_ModelCopy from a hit
 * (or build, insert, then copy). Cleared at the world-build seam alongside
 * CacheProvider_TrimDerivedCaches.
 */

#include "engine/torirs_lru.h"

#include <stdbool.h>
#include <stdint.h>

struct ToriDraw_Model;

enum TorirsModelInstKind
{
    TORIRS_MODEL_INST_SPOT = 0,
    TORIRS_MODEL_INST_NPC,
    TORIRS_MODEL_INST_LOC_BASE,
    TORIRS_MODEL_INST_LOC_SHAPE,
    TORIRS_MODEL_INST_OBJ,
    TORIRS_MODEL_INST_PLAYER,
    TORIRS_MODEL_INST_KIND_COUNT
};

struct TorirsModelInstEntry
{
    struct TorirsLruNode node;
    struct ToriDraw_Model* model;
};

struct TorirsModelInstCache
{
    struct TorirsLru lrus[TORIRS_MODEL_INST_KIND_COUNT];
    int inited;
};

bool TorirsModelInstCache_Init(struct TorirsModelInstCache* cache);
void TorirsModelInstCache_Free(struct TorirsModelInstCache* cache);
void TorirsModelInstCache_Clear(struct TorirsModelInstCache* cache);

/** Look up a cached base. Does not transfer ownership. Touches LRU on hit. */
struct ToriDraw_Model* TorirsModelInstCache_Get(
    struct TorirsModelInstCache* cache,
    enum TorirsModelInstKind kind,
    int64_t key);

/** Insert a newly built base. Takes ownership of `model`. Evicts LRU if full. */
void TorirsModelInstCache_Put(
    struct TorirsModelInstCache* cache,
    enum TorirsModelInstKind kind,
    int64_t key,
    struct ToriDraw_Model* model);

/** Convenience: hit → ModelCopy; miss → NULL (caller builds + Put + Copy). */
struct ToriDraw_Model* TorirsModelInstCache_CopyGet(
    struct TorirsModelInstCache* cache,
    enum TorirsModelInstKind kind,
    int64_t key);

#endif /* TORIRS_MODEL_INST_CACHE_H */
