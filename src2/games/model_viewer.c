#include "model_viewer.h"

#include "toripix/toridraw.h"

#include <stdlib.h>
#include <string.h>

struct GameModelViewer*
game_modelviewer_new(void)
{
    struct GameModelViewer* game_model_viewer = malloc(sizeof(struct GameModelViewer));
    if( !game_model_viewer )
        return NULL;
    memset(game_model_viewer, 0, sizeof(struct GameModelViewer));

    struct ToriDraw_Position* camera_position = malloc(sizeof(struct ToriDraw_Position));
    memset(camera_position, 0, sizeof(struct ToriDraw_Position));
    camera_position->x = 0;
    camera_position->y = 0;
    camera_position->z = -300;
    camera_position->pitch = 0;
    camera_position->yaw = 0;
    camera_position->roll = 0;

    game_model_viewer->camera_position = camera_position;

    struct ToriDraw_Camera* camera = malloc(sizeof(struct ToriDraw_Camera));
    memset(camera, 0, sizeof(struct ToriDraw_Camera));
    camera->fov_rpi2048 = 512;
    camera->near_plane_z = 50;
    camera->pitch = 148;
    camera->yaw = 0;
    camera->roll = 0;
    game_model_viewer->camera = camera;

    struct ToriDraw_ViewPort* view_port = malloc(sizeof(struct ToriDraw_ViewPort));
    memset(view_port, 0, sizeof(struct ToriDraw_ViewPort));
    view_port->width = 800;
    view_port->height = 600;
    view_port->stride = 800;
    view_port->x_center = 400;
    view_port->y_center = 300;
    game_model_viewer->view_port = view_port;

    return game_model_viewer;
}

void
game_modelviewer_free(struct GameModelViewer* game_model_viewer)
{
    if( !game_model_viewer )
        return;
    free(game_model_viewer->camera_position);
    free(game_model_viewer->camera);
    free(game_model_viewer->view_port);
    free(game_model_viewer);
}

void
game_modelviewer_set_model(
    struct GameModelViewer* game_model_viewer,
    struct ToriDraw_ModelHandle model)
{
    if( !game_model_viewer )
        return;
    game_model_viewer->model = model;
}

void
game_modelviewer_process_input(
    struct GameModelViewer* game_model_viewer,
    struct LibToriRS_Input* input)
{}

void
game_modelviewer_move_forward(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    // Relative to the direction the camera is facing
    int direction_x_ish16 = toridraw_sin(game_model_viewer->camera->yaw);
    int direction_z_ish16 = toridraw_cos(game_model_viewer->camera->yaw);

    int delta_x = (direction_x_ish16 * amount) >> 16;
    int delta_z = (direction_z_ish16 * amount) >> 16;
    game_model_viewer->camera_position->x -= delta_x;
    game_model_viewer->camera_position->z += delta_z;
}

void
game_modelviewer_move_backward(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_modelviewer_move_forward(game_model_viewer, -amount);
}

void
game_modelviewer_move_left(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    // Relative to the direction the camera is facing
    int direction_x_ish16 = toridraw_cos(game_model_viewer->camera->yaw);
    int direction_z_ish16 = toridraw_sin(game_model_viewer->camera->yaw);
    game_model_viewer->camera_position->x += (direction_x_ish16 * amount) >> 16;
    game_model_viewer->camera_position->z += (direction_z_ish16 * amount) >> 16;
}

void
game_modelviewer_move_right(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_modelviewer_move_left(game_model_viewer, -amount);
}

void
game_modelviewer_move_up(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_model_viewer->camera_position->y += amount;
}

void
game_modelviewer_move_down(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_model_viewer->camera_position->y -= amount;
}

void
game_modelviewer_rotate_left(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_model_viewer->camera->yaw = toridraw_add_angle(game_model_viewer->camera->yaw, amount);
}

void
game_modelviewer_rotate_right(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_modelviewer_rotate_left(game_model_viewer, -amount);
}

void
game_modelviewer_rotate_up(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_model_viewer->camera->pitch = toridraw_add_angle(game_model_viewer->camera->pitch, amount);
}

void
game_modelviewer_rotate_down(
    struct GameModelViewer* game_model_viewer,
    int amount)
{
    game_modelviewer_rotate_up(game_model_viewer, -amount);
}