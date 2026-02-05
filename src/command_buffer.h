#ifndef COMMAND_BUFFER_H
#define COMMAND_BUFFER_H

#include <stdint.h>

struct ToriRSRenderCommandBuffer
{
    struct ToriRSRenderCommand* commands;
    int command_count;
    int command_capacity;
};

#endif