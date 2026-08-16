#ifndef CONTOUR_GROUND_H
#define CONTOUR_GROUND_H

#include <stdbool.h>
#include <stdint.h>

enum ContourVertexType
{
    CONTOUR_VERTEX_INT,
    CONTOUR_VERTEX_INT16,
};

static inline int
contour_vertex_get(
    const void* arr,
    enum ContourVertexType t,
    int i)
{
    if( t == CONTOUR_VERTEX_INT16 )
        return (int)((const int16_t*)arr)[i];
    return ((const int*)arr)[i];
}

/** Loc config `contour_ground_type` (cache opcodes 21, 81, 93, 94, 95). Values must stay 1–5. */
enum ContourGroundType
{
    /** Opcode 21: sample main heightmap, offset by scene placement height. */
    CONTOUR_GROUND_SCENE_FOLLOW = 1,
    /** Opcode 81: threshold blend using `param` and optional heightmap sample. */
    CONTOUR_GROUND_THRESHOLD_BLEND = 2,
    /** Opcode 93: keep model vertex Y. */
    CONTOUR_GROUND_MODEL_Y = 3,
    /** Opcode 94: sample heightmap one level above. */
    CONTOUR_GROUND_ABOVE_OFFSET = 4,
    /** Opcode 95: sample main and above, blend by vertex Y span. */
    CONTOUR_GROUND_DUAL_LEVEL_BLEND = 5,
};

enum ContourGroundCmdKind
{
    /** Caller: interpolated height at (draw_x, draw_z, slevel) in the main heightmap. */
    CONTOUR_CMD_FETCH_HEIGHT,
    /** Caller: interpolated height at (draw_x, draw_z, slevel) in the above heightmap / level. */
    CONTOUR_CMD_FETCH_HEIGHT_ABOVE,
    /** Caller: apply contour_y to vertices_y[vertex_index] (or equivalent). */
    CONTOUR_CMD_SET_Y,
};

struct ContourGroundCommand
{
    enum ContourGroundCmdKind kind;
    /* FETCH_HEIGHT / FETCH_HEIGHT_ABOVE */
    int draw_x;
    int draw_z;
    int slevel;
    /* SET_Y */
    int vertex_index;
    int contour_y;
};

struct ContourGround
{
    enum ContourGroundType type;
    int param;
    int scene_x;
    int scene_z;
    int scene_height;
    int slevel;
    const void* vertices_x;
    const void* vertices_y;
    const void* vertices_z;
    int vertex_count;
    int used_vertex_count;
    enum ContourVertexType vertex_type;

    int _i;
    int _limit;
    int _substep;
    int _height;
    int _height_above;
    int _min_y;
    int _delta_y;
    int _hm_size_x;
    int _hm_size_z;
    bool _active;
};

/**
 * Returns true if contouring should run; false means caller skips the loop.
 * hm_above_size_x/z: tile dimensions for the above map (use 0,0 for types 1–3).
 */
bool
contour_ground_init(
    struct ContourGround* cg,
    int type,
    int param,
    int hm_size_x,
    int hm_size_z,
    int hm_above_size_x,
    int hm_above_size_z,
    const void* vertices_x,
    const void* vertices_y,
    const void* vertices_z,
    int vertex_count,
    int used_vertex_count,
    enum ContourVertexType vertex_type,
    int scene_x,
    int scene_z,
    int scene_height,
    int slevel);

bool
contour_ground_next(
    struct ContourGround* cg,
    struct ContourGroundCommand* cmd);

/** Call after CONTOUR_CMD_FETCH_HEIGHT or CONTOUR_CMD_FETCH_HEIGHT_ABOVE before the next next(). */
void
contour_ground_provide(
    struct ContourGround* cg,
    int height);

#endif
