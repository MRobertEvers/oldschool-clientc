#ifndef SRC_UITREE_CROSS_H
#define SRC_UITREE_CROSS_H

#include <stdbool.h>

/*
 * Walk/interact click-cross overlay (reference ClickCrossOverlay: the 8-frame
 * "cross" sprite pack — frames 0-3 yellow walk, 4-7 red interact, 100ms per
 * frame, expiring at 400ms). Yellow = walk; red = any targeting action.
 */
enum UICrossMode
{
    UI_CROSS_OFF = 0,
    UI_CROSS_WALK = 1,
    UI_CROSS_INTERACT = 2,
};

struct UICross
{
    int x;
    int y;
    enum UICrossMode mode;
    /** Animation phase 0..399 ms, +delta per tick while mode != OFF. */
    int cycle;
};

void
UICross_Reset(struct UICross* cross);

void
UICross_Show(struct UICross* cross, enum UICrossMode mode, int x, int y);

void
UICross_Tick(struct UICross* cross, int delta_ms);

bool
UICross_IsActive(struct UICross const* cross);

/** Atlas frame in the cross sprite pack: cycle/100, +4 when interact. */
int
UICross_AtlasFrame(struct UICross const* cross);

#endif
