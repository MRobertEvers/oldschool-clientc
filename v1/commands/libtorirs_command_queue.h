#ifndef LIBTORIRS_COMMAND_QUEUE_H
#define LIBTORIRS_COMMAND_QUEUE_H

#include "../input/libtorirs_input.h"

#include <stdbool.h>
#include <stdint.h>

struct LibToriRS_CommandQueue;

struct LibToriRS_CommandQueue*
LibToriRS_CommandQueue_New(void);

void
LibToriRS_CommandQueue_Free(struct LibToriRS_CommandQueue* command_queue);

void
LibToriRS_CommandQueue_Clear(struct LibToriRS_CommandQueue* command_queue);

bool
LibToriRS_CommandQueue_IsQuit(struct LibToriRS_CommandQueue* command_queue);

void
LibToriRS_CommandQueue_PushQuitEvent(struct LibToriRS_CommandQueue* command_queue);

void
LibToriRS_CommandQueue_PushKeyEvent(
    struct LibToriRS_CommandQueue* command_queue,
    enum LibToriRS_KeyCode key,
    enum LibToriRS_KeyEventType event_type);

void
LibToriRS_CommandQueue_PushMouseButtonEvent(
    struct LibToriRS_CommandQueue* command_queue,
    enum LibToriRS_MouseButton button,
    enum LibToriRS_MouseEventType event_type,
    int mouse_x,
    int mouse_y);

void
LibToriRS_CommandQueue_PushMouseMoveEvent(
    struct LibToriRS_CommandQueue* command_queue,
    int mouse_x,
    int mouse_y,
    int xrel,
    int yrel);

void
LibToriRS_CommandQueue_PushMouseWheelEvent(
    struct LibToriRS_CommandQueue* command_queue,
    int wheel_y,
    int mouse_x,
    int mouse_y);

#endif