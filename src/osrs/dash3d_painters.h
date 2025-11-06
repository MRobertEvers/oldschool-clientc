#ifndef DASH3D_PAINTERS_H
#define DASH3D_PAINTERS_H

enum Dash3DPainterOpKind
{
    DASH3D_PAINTERS_NONE,
    DASH3D_PAINTERS_DBG_TILE,
    DASH3D_PAINTERS_DRAW_GROUND,
    DASH3D_PAINTERS_DRAW_LOC,
    DASH3D_PAINTERS_DRAW_WALL,
    DASH3D_PAINTERS_DRAW_GROUND_DECOR,
    DASH3D_PAINTERS_DRAW_GROUND_OBJECT,
    DASH3D_PAINTERS_DRAW_WALL_DECOR,
};

struct Dash3DPainterOp
{
    enum Dash3DPainterOpKind op;

    int x;
    int z;
    int level;

    union
    {
        struct
        {
            int loc_index;
        } _loc;

        struct
        {
            int override_color;
            int color_hsl16;
        } _ground;

        struct
        {
            int loc_index;
            int is_wall_a;
        } _wall;

        struct
        {
            int loc_index;
        } _ground_decor;

        struct
        {
            int loc_index;
            int is_wall_a;
            int __rotation;
        } _wall_decor;

        struct
        {
            int num; // 0, 1, 2
        } _ground_object;

        struct
        {
            int color;
        } _dbg;
    };
};

#endif