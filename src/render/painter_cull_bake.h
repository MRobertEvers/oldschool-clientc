#ifndef SRC_RENDER_PAINTER_CULL_BAKE_H
#define SRC_RENDER_PAINTER_CULL_BAKE_H

/*
 * Whether the painter's baked cull map (TORIRS_PAINTER_CULL=baked) has to be
 * baked again for the viewport it is about to draw.
 *
 * The bake is a CPU pass of several hundred milliseconds, so a viewport that
 * is being resized must not trigger one every frame. This remembers the
 * viewport size the last bake was made for, including a bake that came out
 * empty, and answers that a new one is needed only when nothing has been
 * baked yet or the viewport has moved 8 pixels or more on either axis.
 *
 * This does not bake, hold the cull map or know the camera. The caller asks
 * before it bakes, bakes, and notes the size it baked for.
 */

#include <stdbool.h>

struct PainterCullBake
{
    /** The viewport the last bake was made for; 0 while none has been. */
    int w;
    int h;
};

/** A bake for a `vw` x `vh` viewport is due. */
bool
PainterCullBake_NeedsBake(
    struct PainterCullBake const* bake,
    int vw,
    int vh);

/** A bake for a `vw` x `vh` viewport was made. */
void
PainterCullBake_Note(
    struct PainterCullBake* bake,
    int vw,
    int vh);

#endif
