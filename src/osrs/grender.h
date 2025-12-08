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

struct GRenderCommandBuffer;

struct GRenderCommandBuffer* grendercb_new(int capacity);
void grendercb_free(struct GRenderCommandBuffer* buffer);
void grendercb_add_command(struct GRenderCommandBuffer* buffer, struct GRenderCommand command);

void grendercb_reset(struct GRenderCommandBuffer* buffer);

struct GRenderCommand* grendercb_at(struct GRenderCommandBuffer* buffer, int index);
int grendercb_count(struct GRenderCommandBuffer* buffer);

#endif