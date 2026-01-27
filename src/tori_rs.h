#ifndef TORI_RS_H
#define TORI_RS_H

#include "datastruct/vec.h"
#include "osrs/game.h"
#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "osrs/painters.h"
#include "osrs/scene.h"
#include "tori_rs_render.h"

#include <stdbool.h>
#include <stdint.h>

struct DashGraphics;
struct DashModel;
struct DashPosition;
struct DashViewPort;
struct DashCamera;

struct GGame*
LibToriRS_GameNew(
    struct GIOQueue* io,
    int graphics3d_width,
    int graphics3d_height);

void
LibToriRS_GameProcessInput(
    struct GGame* game,
    struct GInput* input);

/**
 * @deprecated Not needed.
 */
void
LibToriRS_GameStepTasks(
    struct GGame* game,
    struct GInput* input,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

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

int
LibToriRS_NetIsReady(struct GGame* game);

void
LibToriRS_NetConnect(
    struct GGame* game,
    char* username,
    char* password);

int
LibToriRS_NetPump(struct GGame* game);

void
LibToriRS_NetRecv(
    struct GGame* game,
    uint8_t* data,
    int data_size);

void
LibToriRS_NetDisconnected(struct GGame* game);

int
LibToriRS_NetGetOutgoing(
    struct GGame* game,
    uint8_t* buffer,
    int buffer_size);

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