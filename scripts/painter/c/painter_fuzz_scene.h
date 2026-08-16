#ifndef PAINTER_FUZZ_SCENE_H
#define PAINTER_FUZZ_SCENE_H

#include "painters.h"

#include <stdint.h>

#define PAINTER_FUZZ_MAX_GRID 51

typedef struct
{
    int scenery;
    int wall;
    int walldecor;
    int grounddecor;
    int groundobj;
} PainterFuzzAddedCounts;

typedef struct
{
    uint32_t seed;
    int grid;
    int levels;
    int use_cullmap; /* 0 = nocull, 1 = analytic span, 2 = runtime bake */
    int camera_sx;
    int camera_sz;
    int camera_slevel;
    int pitch;
    int yaw;
    uint8_t level_mask;
    int scenery_count;
    int wall_count;
    int decor_count;
    int ground_decor_count;
} PainterFuzzConfig;

void painter_fuzz_fill_config(PainterFuzzConfig* cfg, uint32_t seed);

struct Painter* painter_fuzz_build_scene(
    const PainterFuzzConfig* cfg,
    struct PaintersCullMap** out_cm,
    PainterFuzzAddedCounts* added);

#endif /* PAINTER_FUZZ_SCENE_H */
