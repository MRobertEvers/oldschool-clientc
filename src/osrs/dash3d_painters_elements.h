#ifndef DASH3D_PAINTERS_ELEMENTS_H
#define DASH3D_PAINTERS_ELEMENTS_H

enum ElementStep
{
    // Do not draw ground until adjacent tiles are done,
    // unless we are spanned by that tile.
    E_STEP_VERIFY_FURTHER_TILES_DONE_UNLESS_SPANNED,
    E_STEP_GROUND,
    E_STEP_WAIT_ADJACENT_GROUND,
    E_STEP_LOCS,
    E_STEP_NOTIFY_ADJACENT_TILES,
    E_STEP_NEAR_WALL,
    E_STEP_DONE,
};

struct PaintersElement
{
    enum ElementStep step;

    int remaining_locs;
    int generation;
    int q_count;

    int near_wall_flags;
};

#endif