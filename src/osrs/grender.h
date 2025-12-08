#ifndef OSRS_GRENDER_H
#define OSRS_GRENDER_H

enum GRenderCommandKind
{
    GRENDER_CMD_MODEL_LOAD,
    GRENDER_CMD_MODEL_DRAW,
};

struct GRenderCommand
{
    enum GRenderCommandKind kind;
    union
    {
        struct
        {
            int model_id;
        } _model_draw;

        struct
        {
            int model_id;
        } _model_load;
    };
};

struct GRenderCommandBuffer
{
    struct GRenderCommand* commands;
    int command_count;
    int command_capacity;
};

struct GRenderCommandBuffer* grendercb_new(int capacity);
void grendercb_free(struct GRenderCommandBuffer* buffer);

#endif