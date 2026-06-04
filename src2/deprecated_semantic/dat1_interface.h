#ifndef DAT1_INTERFACE_H
#define DAT1_INTERFACE_H

#include "dat1_element.h"

struct Dat1BuildCache;
struct DashMap;
struct GameCache;
struct UIScene;

/* ------------------------------------------------------------------
 * Dat1_Interface
 *
 * Revconfig "interface" / RS component element.  One instance per
 * CacheDatConfigComponent root that needs to be baked into the UITree.
 *
 * Load sequence
 * -------------
 * NEED_ARCHIVE  -> declare interfaces + media archive requests.
 * WAIT          -> wait for revconfig routing to populate buildcache.
 * INSPECT       -> walk the component tree; declare model/font
 *                  archive requests; preload all graphic sprites into
 *                  UIScene + sprite_map so uitree_build can assume
 *                  every resource is present.
 * WAIT_DEPS     -> wait for model + font archives (if any).
 * CONVERT       -> convert CacheModel -> ToriDraw_Model -> gamecache.
 * DONE / FAILED
 * ------------------------------------------------------------------ */
struct Dat1_Interface
{
    enum Dat1_LoadState state;
    int                 component_id;
    bool                need_fonts;

    /* Model ids discovered during INSPECT; converted during CONVERT. */
    int model_ids[DAT1_INTERFACE_MAX_MODELS];
    int model_count;

    struct Dat1_ArchiveRequest waiting[DAT1_INTERFACE_MAX_REQUESTS];
    int                        waiting_count;
};

void
dat1_interface_init(struct Dat1_Interface* iface, int component_id);

/**
 * Advance the interface state machine one tick.
 *
 * @param sprite_map  Written during INSPECT: graphic sprite name -> scene_id.
 */
void
dat1_interface_step(
    struct Dat1_Interface* iface,
    struct Dat1BuildCache* buildcache,
    struct GameCache*      gamecache,
    struct UIScene*        scene,
    struct DashMap*        sprite_map);

#endif /* DAT1_INTERFACE_H */
