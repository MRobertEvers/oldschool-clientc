#ifndef TORI_RS_H
#define TORI_RS_H

#include "datastruct/vec.h"
#include "osrs/game.h"
#include "tori_rs_net_shared.h"
#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "osrs/painters.h"
#include "tori_rs_render.h"

#include <stdbool.h>
#include <stdint.h>

struct DashGraphics;
struct DashModel;
struct DashPosition;
struct DashViewPort;
struct DashCamera;

struct ToriRSRenderCommandBuffer;
struct ToriRSRenderCommand;

struct ToriRSPlatformScript
{
    char name[64];

    struct
    {
        int type;
        union
        {
            int _iarg;
            char* _strarg;
        };
    } args[10];
    int argno;
};

struct GGame*
LibToriRS_GameNew(
    struct GIOQueue* io,
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
    struct ToriRSPlatformScript* out);

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
LibToriRS_NetInit(
    struct GGame* game,
    LibToriRS_NetContext* ctx);

void
LibToriRS_NetPoll(LibToriRS_NetContext* ctx);

void
LibToriRS_NetSend(
    struct GGame* game,
    const uint8_t* data,
    int size);

void
LibToriRS_NetSendReconnect(struct GGame* game);

void
LibToriRS_OnMessage(
    struct GGame* game,
    const uint8_t* data,
    int size);

void
LibToriRS_OnStatusChange(
    struct GGame* game,
    NetStatus status);

void
LibToriRS_NetConnectLogin(
    struct GGame* game,
    const char* username,
    const char* password);

void
LibToriRS_NetConnectGame(struct GGame* game);

void
LibToriRS_NetProcessInbound(struct GGame* game);

void
LibToriRS_GameFree(struct GGame* game);

void
LibToriRS_GameStep(
    struct GGame* game,
    struct GInput* input,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

bool
LibToriRS_GameIsRunning(struct GGame* game);

#endif