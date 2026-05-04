#ifndef OSRS_SCRIPT_QUEUE_H
#define OSRS_SCRIPT_QUEUE_H

#include <stdint.h>

/* Script kind; one entry per runnable script. */
enum ScriptKind
{
    SCRIPT_INIT,
    SCRIPT_INIT_UI,
    SCRIPT_LOAD_SCENE_DAT,
    SCRIPT_LOAD_SCENE,
    SCRIPT_PKT_DISPATCH,
    SCRIPT_LOAD_CULLMAP,
    SCRIPT_SPAWN_ELEMENT,
    SCRIPT_COUNT
};

/* Args for load_scene_dat(wx_sw, wz_sw, wx_ne, wz_ne, size_x, size_z) */
struct ScriptArgsLoadSceneDat
{
    int wx_sw;
    int wz_sw;
    int wx_ne;
    int wz_ne;
    int size_x;
    int size_z;
};

struct ScriptArgsLoadScene
{
    int wx_sw;
    int wz_sw;
    int wx_ne;
    int wz_ne;
    int size_x;
    int size_z;
};

struct ScriptArgsLoadCullmap
{
    int viewport_w;
    int viewport_h;
    /** Near clip Z (same units as gen_painters_cullmap --near); baked filename uses f{near_clip_z}. */
    int near_clip_z;
    int draw_radius;
};

struct ScriptArgsSpawnElement
{
    int world_x;
    int world_z;
    int world_level;
    int model_id;
    int seq_id;
};

/* Tagged union of all script argument structs (one member per runnable script). */
struct ScriptArgs
{
    enum ScriptKind tag;
    union
    {
        struct ScriptArgsLoadSceneDat load_scene_dat;
        struct ScriptArgsLoadScene load_scene;
        struct ScriptArgsLoadCullmap load_cullmap;
        struct ScriptArgsSpawnElement spawn_element;
    } u;
};

/* One runnable script in the queue: args (tag + payload). */
struct ScriptQueueItem
{
    struct ScriptArgs args;
    struct ScriptQueueItem* next;
};

/* Queue of runnable scripts (FIFO list). */
struct ScriptQueue
{
    struct ScriptQueueItem* head;
    struct ScriptQueueItem* tail;
};

void
script_queue_init(struct ScriptQueue* q);

void
script_queue_clear(struct ScriptQueue* q);

struct ScriptQueueItem*
script_queue_push(
    struct ScriptQueue* q,
    const struct ScriptArgs* args);

struct ScriptQueueItem*
script_queue_pop(struct ScriptQueue* q);

int
script_queue_empty(const struct ScriptQueue* q);

struct ScriptQueueItem*
script_queue_peek(const struct ScriptQueue* q);

void
script_queue_free_item(struct ScriptQueueItem* item);

#endif
