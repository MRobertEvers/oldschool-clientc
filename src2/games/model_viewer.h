#ifndef MODEL_VIEWER_H
#define MODEL_VIEWER_H

#include "src/graphics/dash.h"

struct GameModelViewer
{
    struct DashModel* model;
};

struct GameModelViewer*
game_modelviewer_new(void);

void
game_modelviewer_free(struct GameModelViewer* game_model_viewer);

void
game_modelviewer_set_model(
    struct GameModelViewer* game_model_viewer,
    struct DashModel* model);

#endif