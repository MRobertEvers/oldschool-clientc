#include "model_viewer.h"

#include <stdlib.h>

struct GameModelViewer*
game_modelviewer_new(void)
{
    struct GameModelViewer* game_model_viewer = malloc(sizeof(struct GameModelViewer));
    if( !game_model_viewer )
        return NULL;
    game_model_viewer->model = NULL;
    return game_model_viewer;
}

void
game_modelviewer_free(struct GameModelViewer* game_model_viewer)
{
    if( !game_model_viewer )
        return;
    free(game_model_viewer);
}

void
game_modelviewer_set_model(
    struct GameModelViewer* game_model_viewer,
    struct DashModel* model)
{
    if( !game_model_viewer )
        return;
    game_model_viewer->model = model;
}