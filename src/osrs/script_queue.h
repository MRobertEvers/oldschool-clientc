#ifndef OSRS_SCRIPT_QUEUE_H
#define OSRS_SCRIPT_QUEUE_H

/* Script kind; one entry per runnable script. */
enum ScriptKind
{
    SCRIPT_LOAD_SCENE_DAT,
    SCRIPT_LOAD_SCENE,
    SCRIPT_PKT_NPC_INFO,
    SCRIPT_PKT_REBUILD_NORMAL,
    SCRIPT_PKT_PLAYER_INFO,
    SCRIPT_LOAD_INVENTORY_MODELS,  // New script for loading inventory models
    SCRIPT_PKT_UPDATE_INV_FULL,
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

/* Args for pkt_npc_info: item = RevPacket_LC245_2_Item*, io = GIOQueue* */
struct ScriptArgsPktNpcInfo
{
    void* item;
    void* io;
};

/* Args for pkt_rebuild_normal / pkt_player_info: same as pkt_npc_info */
struct ScriptArgsPktRebuildNormal
{
    void* item;
    void* io;
};
struct ScriptArgsPktPlayerInfo
{
    void* item;
    void* io;
};

struct ScriptArgsPktUpdateInvFull
{
    void* item;
    void* io;
};

/* Args for load_inventory_models: no args needed */
struct ScriptArgsLoadInventoryModels
{
    int dummy;  // Unused, but C doesn't allow empty structs
};

/* Tagged union of all script argument structs (one member per runnable script). */
struct ScriptArgs
{
    enum ScriptKind tag;
    union
    {
        struct ScriptArgsLoadSceneDat load_scene_dat;
        struct ScriptArgsLoadScene load_scene;
        struct ScriptArgsPktNpcInfo pkt_npc_info;
        struct ScriptArgsPktRebuildNormal pkt_rebuild_normal;
        struct ScriptArgsPktPlayerInfo pkt_player_info;
        struct ScriptArgsLoadInventoryModels load_inventory_models;
        struct ScriptArgsPktUpdateInvFull pkt_update_inv_full;
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
