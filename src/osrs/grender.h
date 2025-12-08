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

#endif