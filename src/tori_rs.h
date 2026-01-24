#ifndef TORI_RS_H
#define TORI_RS_H

#include "datastruct/vec.h"
#include "osrs/game.h"
#include "osrs/ginput.h"
#include "osrs/gio.h"
#include "osrs/grender.h"
#include "osrs/painters.h"
#include "osrs/scene.h"

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
void
LibToriRS_GameStepTasks(
    struct GGame* game,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer);

void
LibToriRS_BeginFrame(struct GGame* game);

void
LibToriRS_EndFrame(struct GGame* game);

void
LibToriRS_GameFree(struct GGame* game);
void
LibToriRS_GameStep(
    struct GGame* game,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer);

bool
LibToriRS_GameIsRunning(struct GGame* game);

#endif