#ifndef POSER_GL_C_PG_GIZMO_H
#define POSER_GL_C_PG_GIZMO_H

/*
 * The three manipulators, one per transformation type.
 *
 * A gizmo never writes a transformation itself: it works out how far the cursor
 * dragged along its axis and reports an adjustment, which the editor applies
 * through the same undoable command the numeric sliders use. That is what keeps
 * dragging and typing a value indistinguishable in the history.
 */

#include "pg_math.h"
#include "pg_render.h"

#include <stdbool.h>

enum PG_GizmoKind
{
    PG_GIZMO_TRANSLATION = 0,
    PG_GIZMO_ROTATION,
    PG_GIZMO_SCALE
};

struct PG_GizmoAxis
{
    int axis; /* 0 = x, 1 = y, 2 = z */
    struct PG_Vec4 colour;
    struct PG_Vec3 rotation;
    struct PG_Vec3 previous_intersection;
};

struct PG_Gizmo
{
    int kind;
    float scale;
    struct PG_GizmoAxis axes[3];
    struct PG_Vec3 position;
    bool active;
    int selected_axis; /* -1 when none */
};

/** Configure for a transformation type; call whenever the selection changes. */
void
pg_gizmo_set(struct PG_Gizmo* gizmo, int kind, struct PG_Vec3 position);

/**
 * Hover-test or manipulate, depending on whether the left button is held.
 *
 * When a drag produced movement, returns the axis index and writes the delta to
 * apply to that coordinate through `out_delta`; `out_cyclic` marks the rotation
 * gizmo, whose values wrap at the ends of the range rather than clamping.
 * Returns -1 when nothing changed.
 */
int
pg_gizmo_update(
    struct PG_Gizmo* gizmo,
    struct PG_Ray ray,
    bool lmb_down,
    int entity_size,
    int* out_delta,
    bool* out_cyclic);

void
pg_gizmo_render(
    struct PG_Gizmo* gizmo,
    struct PG_Renderer* renderer,
    struct PG_Mat4 view,
    int entity_size);

#endif
