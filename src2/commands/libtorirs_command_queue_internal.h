#ifndef LIBTORIRS_COMMAND_QUEUE_INTERNAL_H
#define LIBTORIRS_COMMAND_QUEUE_INTERNAL_H

#include "../input/libtorirs_input.h"

#include <stdbool.h>

struct LibToriRS_Command_Packet
{
    void* data;
    int data_size;
};

struct LibToriRS_Command_KeyDown
{
    enum LibToriRS_KeyCode key;
    enum LibToriRS_KeyEventType event_type;
};

struct LibToriRS_Command_KeyUp
{
    enum LibToriRS_KeyCode key;
    enum LibToriRS_KeyEventType event_type;
};

struct LibToriRS_Command_MouseDown
{
    enum LibToriRS_MouseButton button;
    enum LibToriRS_MouseEventType event_type;
    int mouse_x;
    int mouse_y;
};

struct LibToriRS_Command_MouseUp
{
    enum LibToriRS_MouseButton button;
    enum LibToriRS_MouseEventType event_type;
    int mouse_x;
    int mouse_y;
};

struct LibToriRS_Command_MouseMove
{
    int mouse_x;
    int mouse_y;
    int xrel;
    int yrel;
};

struct LibToriRS_Command_MouseWheel
{
    int wheel;
    int mouse_x;
    int mouse_y;
};

enum LibToriRS_CommandType
{
    LIBTORIRS_COMMAND_TYPE_UNKNOWN,
    LIBTORIRS_COMMAND_TYPE_QUIT,
    LIBTORIRS_COMMAND_TYPE_PACKET,
    LIBTORIRS_COMMAND_TYPE_KEY_DOWN,
    LIBTORIRS_COMMAND_TYPE_KEY_UP,
    LIBTORIRS_COMMAND_TYPE_MOUSE_DOWN,
    LIBTORIRS_COMMAND_TYPE_MOUSE_UP,
    LIBTORIRS_COMMAND_TYPE_MOUSE_MOVE,
    LIBTORIRS_COMMAND_TYPE_MOUSE_WHEEL,
    LIBTORIRS_COMMAND_TYPE_COUNT
};

struct LibToriRS_Command
{
    enum LibToriRS_CommandType type;
    union
    {
        struct LibToriRS_Command_Packet packet;
        struct LibToriRS_Command_KeyDown key_down;
        struct LibToriRS_Command_KeyUp key_up;
        struct LibToriRS_Command_MouseDown mouse_down;
        struct LibToriRS_Command_MouseUp mouse_up;
        struct LibToriRS_Command_MouseMove mouse_move;
    } u;
};

#define LIBTORIRS_COMMAND_QUEUE_MAX_SIZE 1024

struct LibToriRS_CommandQueue
{
    bool quit;
    struct LibToriRS_Command commands[LIBTORIRS_COMMAND_QUEUE_MAX_SIZE];
    int count;
};

#endif