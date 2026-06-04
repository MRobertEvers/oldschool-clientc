#ifndef DAT1_ELEMENT_H
#define DAT1_ELEMENT_H

#include <stdbool.h>

/* Maximum archive requests a single element can track.
 * Sprites need 1; interfaces need up to 3 + N models. */
#define DAT1_SPRITE_MAX_REQUESTS    1
#define DAT1_INTERFACE_MAX_REQUESTS 40
#define DAT1_INTERFACE_MAX_MODELS   32

/* ------------------------------------------------------------------
 * Load state shared by all Dat1_X elements.
 *
 * NEED_ARCHIVE -> declare Dat1_ArchiveRequest(s) into waiting[].
 * WAIT         -> revconfig decodes resolved IO into dat1_buildcache
 *                 and marks request done.
 * INSPECT      -> read dat1_buildcache; may declare secondary requests.
 * WAIT_DEPS    -> secondary requests still pending.
 * CONVERT      -> convert dat1_buildcache raw -> gamecache/UIScene.
 * DONE / FAILED
 * ------------------------------------------------------------------ */
enum Dat1_LoadState
{
    DAT1_STATE_NEED_ARCHIVE = 0,
    DAT1_STATE_WAIT,
    DAT1_STATE_INSPECT,
    DAT1_STATE_WAIT_DEPS,
    DAT1_STATE_CONVERT,
    DAT1_STATE_DONE,
    DAT1_STATE_FAILED,
};

/* One cache archive (or model) request an element is waiting on.
 * table_id/archive_id/flags mirror LibToriRS_IOQueuePush parameters. */
struct Dat1_ArchiveRequest
{
    int  table_id;
    int  archive_id;
    int  flags;
    bool resolved;
};

/* Sprite name -> UIScene element lookup used by uitree_build. */
struct Dat1_SpriteMapEntry
{
    char name[64];
    int  scene_id;
    int  atlas_index;
};

enum Dat1_ElementKind
{
    DAT1_ELEM_SPRITE    = 0,
    DAT1_ELEM_INTERFACE = 1,
    /* DAT2_ELEM_* reserved */
};

#endif /* DAT1_ELEMENT_H */
