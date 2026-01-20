#ifndef LIBG_H
#define LIBG_H

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
libg_game_new(struct GIOQueue* io);

void
libg_game_process_input(
    struct GGame* game,
    struct GInput* input);
void
libg_game_step_tasks(
    struct GGame* game,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer);

void
LibToriRS_BeginFrame(struct GGame* game);

void
LibToriRS_EndFrame(struct GGame* game);

void
libg_game_free(struct GGame* game);
void
libg_game_step(
    struct GGame* game,
    struct GInput* input,
    struct GRenderCommandBuffer* render_command_buffer);

bool
libg_game_is_running(struct GGame* game);

#endif