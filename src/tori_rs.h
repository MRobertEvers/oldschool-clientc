#ifndef TORI_RS_H
#define TORI_RS_H

#include "datastruct/vec.h"
#include "osrs/game.h"
#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "osrs/lua_sidecar/lua_platform.h"
#include "osrs/painters.h"
#include "tori_rs_render.h"

#include <stdbool.h>
#include <stdint.h>

#define TORI_BUFFER_SIZE 65536 // 64KB

struct DashGraphics;
struct DashModel;
struct DashPosition;
struct DashViewPort;
struct DashCamera;

struct ToriRSRenderCommandBuffer;
struct ToriRSRenderCommand;

#pragma pack(push, 1)
struct ToriRSNetMessageHeader
{
    uint32_t type;   // 4 bytes
    uint16_t length; // 2 bytes
};
#pragma pack(pop)

struct ToriRSNetRingBuffer
{
    uint8_t data[TORI_BUFFER_SIZE];
    uint32_t head;
    uint32_t tail;
};

struct ToriRSNetSharedBuffer
{
    struct ToriRSNetRingBuffer game_to_platform;
    struct ToriRSNetRingBuffer platform_to_game;
    struct ToriRSNetRingBuffer pending;
    int32_t status;
};

enum ToriRSNetMessageType
{
    TORI_RS_NET_MSG_CONNECT = 1,     // Game -> Platform
    TORI_RS_NET_MSG_SEND_DATA = 2,   // Game -> Platform
    TORI_RS_NET_MSG_CONN_STATUS = 3, // Platform -> Game
    TORI_RS_NET_MSG_RECV_DATA = 4    // Platform -> Game
};

enum ToriRSNetConnectionStatus
{
    TORI_RS_NET_STATUS_DISCONNECTED = 0,
    TORI_RS_NET_STATUS_CONNECTING = 1,
    TORI_RS_NET_STATUS_CONNECTED = 2,
    TORI_RS_NET_STATUS_FAILED = 3
};

struct GGame*
LibToriRS_GameNew(
    struct ToriRSNetSharedBuffer* net_shared,
    int graphics3d_width,
    int graphics3d_height);

void
LibToriRS_GameProcessInput(
    struct GGame* game,
    struct GInput* input);

void
LibToriRS_FrameBegin(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

bool
LibToriRS_FrameNextCommand(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer,
    struct ToriRSRenderCommand* command);

void
LibToriRS_FrameEnd(struct GGame* game);

struct ScriptQueue*
LibToriRS_LuaScriptQueue(struct GGame* game);

bool
LibToriRS_LuaScriptQueueIsEmpty(struct GGame* game);

void
LibToriRS_LuaScriptQueuePop(
    struct GGame* game,
    struct LuaGameScript* out);

/* Network status for host and game to read/set */
typedef enum
{
    NET_IDLE = 0,
    NET_CONNECTING,
    NET_CONNECTED,
    NET_ERROR
} NetStatus;

NetStatus
LibToriRS_NetGetStatus(struct GGame* game);

void
LibToriRS_NetSend(
    struct GGame* game,
    const uint8_t* data,
    int size);

void
LibToriRS_NetConnectLogin(
    struct GGame* game,
    const char* host,
    const char* username,
    const char* password);

void
LibToriRS_NetConnectGame(
    struct GGame* game,
    const char* host);

void
LibToriRS_NetPump(struct GGame* game);

void
LibToriRS_GameFree(struct GGame* game);

void
LibToriRS_GameStep(
    struct GGame* game,
    struct GInput* input,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

bool
LibToriRS_GameIsRunning(struct GGame* game);

// --- Interface Functions ---
struct ToriRSNetSharedBuffer*
LibToriRS_NetNewBuffer();

void
LibToriRS_NetFreeBuffer(struct ToriRSNetSharedBuffer* sb);

int
LibToriRS_NetPush(
    struct ToriRSNetRingBuffer* rb,
    uint32_t type,
    const uint8_t* data,
    uint16_t len);

int
LibToriRS_NetPop(
    struct ToriRSNetRingBuffer* rb,
    struct ToriRSNetMessageHeader* out_header,
    uint8_t* out_data);

uint32_t
LibToriRS_NetGetSharedBufferSize();

uint32_t
LibToriRS_NetGetOffset_G2P_Data();

uint32_t
LibToriRS_NetGetOffset_G2P_Head();

uint32_t
LibToriRS_NetGetOffset_G2P_Tail();

uint32_t
LibToriRS_NetGetOffset_P2G_Data();

uint32_t
LibToriRS_NetGetOffset_P2G_Head();

uint32_t
LibToriRS_NetGetOffset_P2G_Tail();

uint32_t
LibToriRS_NetGetOffset_Status();

#endif