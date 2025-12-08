#include "grender.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct GRenderCommandBuffer
{
    struct GRenderCommand* commands;
    int command_count;
    int command_capacity;
};

struct GRenderCommandBuffer*
grendercb_new(int capacity)
{
    struct GRenderCommandBuffer* buffer = malloc(sizeof(struct GRenderCommandBuffer));
    buffer->commands = malloc(capacity * sizeof(struct GRenderCommand));
    buffer->command_count = 0;
    buffer->command_capacity = capacity;
    return buffer;
}

void
grendercb_add_command(struct GRenderCommandBuffer* buffer, struct GRenderCommand command)
{
    if( buffer->command_count >= buffer->command_capacity )
    {
        buffer->command_capacity *= 2;
        buffer->commands =
            realloc(buffer->commands, buffer->command_capacity * sizeof(struct GRenderCommand));
    }
    buffer->commands[buffer->command_count++] = command;
}

void
grendercb_reset(struct GRenderCommandBuffer* buffer)
{
    buffer->command_count = 0;
}

struct GRenderCommand*
grendercb_at(struct GRenderCommandBuffer* buffer, int index)
{
    assert(index >= 0 && index < buffer->command_count);
    return &buffer->commands[index];
}

int
grendercb_count(struct GRenderCommandBuffer* buffer)
{
    return buffer->command_count;
}