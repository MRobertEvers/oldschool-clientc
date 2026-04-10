#ifndef OSRS_SCRIPT_QUEUE_H
#define OSRS_SCRIPT_QUEUE_H

#include <stdint.h>

struct RevPacket_LC245_2_Item;

/* Script kind; one entry per runnable script. */
enum ScriptKind
{
    SCRIPT_INIT,
    SCRIPT_INIT_UI,
    SCRIPT_LOAD_SCENE_DAT,
    SCRIPT_LOAD_SCENE,
    SCRIPT_PKT_REBUILD_NORMAL,
    SCRIPT_PKT_PLAYER_INFO,
    SCRIPT_PKT_NPC_INFO,
    SCRIPT_PKT_IF_SETTAB,
    SCRIPT_PKT_UPDATE_INV_FULL,
    SCRIPT_LOAD_CULLMAP,
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

struct ScriptArgsRebuildNormal
{
    int zonex;
    int zonez;
};

struct ScriptArgsPlayerInfo
{
    int length;
    uint8_t* data;
};

struct ScriptArgsNpcInfo
{
    int length;
    uint8_t* data;
};

struct ScriptArgsLc245Packet
{
    struct RevPacket_LC245_2_Item* item;
};

struct ScriptArgsLoadCullmap
{
    int viewport_w;
    int viewport_h;
    int fov;
    int draw_radius;
};

/* Tagged union of all script argument structs (one member per runnable script). */
struct ScriptArgs
{
    enum ScriptKind tag;
    union
    {
        struct ScriptArgsLoadSceneDat load_scene_dat;
        struct ScriptArgsLoadScene load_scene;
        struct ScriptArgsRebuildNormal rebuild_normal;
        struct ScriptArgsPlayerInfo player_info;
        struct ScriptArgsNpcInfo npc_info;
        struct ScriptArgsLc245Packet lc245_packet;
        struct ScriptArgsLoadCullmap load_cullmap;
    } u;
};

/* One runnable script in the queue: args (tag + payload). */
struct ScriptQueueItem
{
    struct ScriptArgs args;
    /** If non-NULL, free with gameproto_free_lc245_2_item + free() after Lua script finishes. */
    struct RevPacket_LC245_2_Item* lc245_2_packet_to_free;
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
