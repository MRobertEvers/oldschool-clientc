#include "tori_rs_render.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriRSRenderCommandBuffer
{
    struct ToriRSRenderCommand* commands;
    int command_count;
    int command_capacity;
};

struct ToriRSRenderCommandBuffer*
tori_rs_render_command_buffer_new(int capacity)
{
    struct ToriRSRenderCommandBuffer* buffer = malloc(sizeof(struct ToriRSRenderCommandBuffer));
    buffer->commands = malloc(capacity * sizeof(struct ToriRSRenderCommand));
    buffer->command_count = 0;
    buffer->command_capacity = capacity;
    return buffer;
}

void
tori_rs_render_command_buffer_add_command(
    struct ToriRSRenderCommandBuffer* buffer,
    struct ToriRSRenderCommand command)
{
    if( buffer->command_count >= buffer->command_capacity )
    {
        buffer->command_capacity *= 2;
        buffer->commands = realloc(
            buffer->commands, buffer->command_capacity * sizeof(struct ToriRSRenderCommand));
    }
    buffer->commands[buffer->command_count++] = command;
}

void
tori_rs_render_command_buffer_reset(struct ToriRSRenderCommandBuffer* buffer)
{
    buffer->command_count = 0;
}

struct ToriRSRenderCommand*
tori_rs_render_command_buffer_at(
    struct ToriRSRenderCommandBuffer* buffer,
    int index)
{
    assert(index >= 0 && index < buffer->command_count);
    return &buffer->commands[index];
}

int
tori_rs_render_command_buffer_count(struct ToriRSRenderCommandBuffer* buffer)
{
    return buffer->command_count;
}