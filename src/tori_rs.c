#include "tori_rs.h"

#include "graphics/dash.h"
#include "graphics/lighting.h"
#include "osrs/buildcachedat.h"
#include "osrs/dash_utils.h"
#include "osrs/packetbuffer.h"
#include "osrs/rscache/cache.h"
#include "osrs/rscache/cache_dat.h"

// clang-format off
#include "osrs/gameproto_packets_write.u.c"
#include "tori_rs_input.u.c"
#include "tori_rs_net.u.c"
#include "tori_rs_cycle.u.c"
#include "tori_rs_init.u.c"
#include "tori_rs_frame.u.c"
#include "tori_rs_scripts.u.c"
// clang-format on

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool
LibToriRS_GameIsRunning(struct GGame* game)
{
    return game->running;
}

struct ScriptQueue*
LibToriRS_LuaScriptQueue(struct GGame* game)
{
    if( !game )
        return NULL;
    return &game->script_queue;
}

bool
LibToriRS_LuaScriptQueueIsEmpty(struct GGame* game)
{
    if( !game )
        return true;
    return script_queue_empty(&game->script_queue) != 0;
}

void
LibToriRS_LuaScriptQueuePop(
    struct GGame* game,
    struct LuaGameScript* out)
{
    struct ScriptQueueItem* item = NULL;
    const char* script_name = NULL;

    item = script_queue_pop(&game->script_queue);
    if( !item )
        return;

    script_convert_to_lua(item, out);

    script_queue_free_item(item);
}

struct ToriRSNetSharedBuffer*
LibToriRS_NetNewBuffer()
{
    struct ToriRSNetSharedBuffer* sb =
        (struct ToriRSNetSharedBuffer*)malloc(sizeof(struct ToriRSNetSharedBuffer));
    if( sb )
        memset(sb, 0, sizeof(struct ToriRSNetSharedBuffer));
    return sb;
}

void
LibToriRS_NetFreeBuffer(struct ToriRSNetSharedBuffer* sb)
{
    if( sb )
        free(sb);
}

static uint32_t
GetFreeSpace(struct ToriRSNetRingBuffer* rb)
{
    if( rb->head >= rb->tail )
    {
        return TORI_BUFFER_SIZE - (rb->head - rb->tail) - 1;
    }
    return rb->tail - rb->head - 1;
}

// Internal helper to write to ring buffer with wrap-around
static void
RingWrite(
    struct ToriRSNetRingBuffer* rb,
    const uint8_t* src,
    uint32_t len)
{
    uint32_t space_to_end = TORI_BUFFER_SIZE - rb->head;
    if( len <= space_to_end )
    {
        memcpy(&rb->data[rb->head], src, len);
    }
    else
    {
        memcpy(&rb->data[rb->head], src, space_to_end);
        memcpy(&rb->data[0], src + space_to_end, len - space_to_end);
    }
    rb->head = (rb->head + len) % TORI_BUFFER_SIZE;
}

// Internal helper to read from ring buffer with wrap-around
static void
RingRead(
    struct ToriRSNetRingBuffer* rb,
    uint8_t* dest,
    uint32_t len)
{
    uint32_t data_to_end = TORI_BUFFER_SIZE - rb->tail;
    if( len <= data_to_end )
    {
        memcpy(dest, &rb->data[rb->tail], len);
    }
    else
    {
        memcpy(dest, &rb->data[rb->tail], data_to_end);
        memcpy(dest + data_to_end, &rb->data[0], len - data_to_end);
    }
    rb->tail = (rb->tail + len) % TORI_BUFFER_SIZE;
}

int
LibToriRS_NetPush(
    struct ToriRSNetRingBuffer* rb,
    uint32_t type,
    const uint8_t* data,
    uint16_t len)
{
    uint32_t total_needed = sizeof(struct ToriRSNetMessageHeader) + len;
    if( GetFreeSpace(rb) < total_needed )
        return 0;

    struct ToriRSNetMessageHeader header = { type, len };
    RingWrite(rb, (uint8_t*)&header, sizeof(struct ToriRSNetMessageHeader));
    if( len > 0 && data )
    {
        RingWrite(rb, data, len);
    }
    return 1;
}

int
LibToriRS_NetPop(
    struct ToriRSNetRingBuffer* rb,
    struct ToriRSNetMessageHeader* out_header,
    uint8_t* out_data)
{
    if( rb->head == rb->tail )
        return 0;

    // Peek the header first
    uint32_t saved_tail = rb->tail;
    RingRead(rb, (uint8_t*)out_header, sizeof(struct ToriRSNetMessageHeader));

    if( out_header->length > 0 && out_data )
    {
        RingRead(rb, out_data, out_header->length);
    }
    else if( out_header->length > 0 )
    {
        // If caller didn't provide buffer but length > 0, reset tail and fail
        rb->tail = saved_tail;
        return 0;
    }
    return 1;
}

uint32_t
LibToriRS_NetGetSharedBufferSize()
{
    return sizeof(struct ToriRSNetSharedBuffer);
}

uint32_t
LibToriRS_NetGetOffset_G2P_Data()
{
    return offsetof(struct ToriRSNetSharedBuffer, game_to_platform.data);
}

uint32_t
LibToriRS_NetGetOffset_G2P_Head()
{
    return offsetof(struct ToriRSNetSharedBuffer, game_to_platform.head);
}

uint32_t
LibToriRS_NetGetOffset_G2P_Tail()
{
    return offsetof(struct ToriRSNetSharedBuffer, game_to_platform.tail);
}

uint32_t
LibToriRS_NetGetOffset_P2G_Data()
{
    return offsetof(struct ToriRSNetSharedBuffer, platform_to_game.data);
}

uint32_t
LibToriRS_NetGetOffset_P2G_Head()
{
    return offsetof(struct ToriRSNetSharedBuffer, platform_to_game.head);
}

uint32_t
LibToriRS_NetGetOffset_P2G_Tail()
{
    return offsetof(struct ToriRSNetSharedBuffer, platform_to_game.tail);
}

uint32_t
LibToriRS_NetGetOffset_Status()
{
    return offsetof(struct ToriRSNetSharedBuffer, status);
}
