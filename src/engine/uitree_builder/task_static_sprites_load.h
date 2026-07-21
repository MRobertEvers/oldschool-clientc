#ifndef TASK_STATIC_SPRITES_LOAD_H
#define TASK_STATIC_SPRITES_LOAD_H

#include "asyncio.h"

struct CacheProvider;
struct UITreeSceneBridge;

/**
 * Load every client-hardcoded sprite (StaticSpriteSlot) the cache provides and
 * bind it to its bridge slot. Slots already bound are skipped; archives the
 * era lacks (e.g. mod_icons on dat1) stay unbound at -1.
 * Returns NULL when bridge is NULL — callers use TASK_AWAITSELF_IF.
 */
struct ToriRS_Task*
CreateTask_StaticSpritesLoad(
    struct CacheProvider* provider,
    struct UITreeSceneBridge* bridge);

#endif
