#ifndef SRC_INPUT_TORIRS_WORLD_TAP_H
#define SRC_INPUT_TORIRS_WORLD_TAP_H

/*
 * A finger's tap on the world that arrived before any frame had picked at its
 * pixel, held until one has.
 *
 * A world click runs the default row of the menu built from what the last
 * rendered frame picked under the pointer. A mouse hovers its click's pixel
 * for frames before it clicks, so that pick is already the pointer's, and a
 * mouse click always runs at once. A finger has no hover: a tap whose press
 * and release land in one frame arrives before any frame has picked there.
 *
 * So a touch click whose pixel the pick does not answer is HELD at its point
 * instead of run. While it is held the pointer stays at that point, so the
 * next frame picks there. Each later frame without a new world click counts
 * one frame against it, and it comes due -- released, to run at its point --
 * on the first frame whose pick answers that pixel, or once it has waited
 * TORIRS_WORLD_TAP_WAIT_FRAMES frames, on whatever the pick holds then. A new
 * world click replaces a held tap whichever way the new one goes.
 *
 * A zeroed struct is the state with no tap held.
 *
 * This does not know what a pick is, where the world is drawn, or what a
 * click runs. The caller says whether a click was a touch and whether the
 * last pick answers a pixel, keeps drawing frames while a tap is held, and
 * decides whether a tap that came due still lands on the world before it
 * runs it.
 */

/** Frames a held tap waits for a pick at its pixel before it comes due on
 *  whatever the pick holds. One drawn frame is enough; the rest covers a
 *  frame that built no world (a skipped draw) without holding a tap visibly
 *  late. */
#define TORIRS_WORLD_TAP_WAIT_FRAMES 4

struct ToriRS_WorldTap
{
    /** A tap is held. */
    int active;
    /** Where it landed, in canvas pixels. */
    int x;
    int y;
    /** Frames it has waited since it was held. */
    int frames;
};

/** No tap held, every field zero. Also the state of a zeroed struct. */
void
ToriRS_WorldTapReset(struct ToriRS_WorldTap* tap);

/** Is a tap held? */
int
ToriRS_WorldTapHeld(struct ToriRS_WorldTap const* tap);

/**
 * Where the world should take the pointer to be this frame: the held tap's
 * point while one is held, otherwise (mouse_x, mouse_y). Returns 1 while a tap
 * is held.
 */
int
ToriRS_WorldTapPointer(
    struct ToriRS_WorldTap const* tap,
    int mouse_x,
    int mouse_y,
    int* out_x,
    int* out_y);

/**
 * A new click on the world at (x, y). It replaces any held tap. When it is a
 * touch (`is_touch`) and the last pick does not answer (x, y)
 * (`pick_answers` 0), it is held there and this returns 1: the caller draws
 * another frame and runs nothing now. Otherwise nothing is held and this
 * returns 0: the caller runs the click now.
 */
int
ToriRS_WorldTapClick(
    struct ToriRS_WorldTap* tap,
    int x,
    int y,
    int is_touch,
    int pick_answers);

/**
 * A frame with no new world click while a tap is held: counts the frame.
 * Returns 1 when the tap is due -- the last pick answers its point
 * (`pick_answers`), or it has now waited TORIRS_WORLD_TAP_WAIT_FRAMES -- and
 * releases it; its point and frame count stay readable. Returns 0 while it
 * goes on waiting.
 */
int
ToriRS_WorldTapStep(
    struct ToriRS_WorldTap* tap,
    int pick_answers);

/** The held (or just released) tap's point and frames waited. */
int
ToriRS_WorldTapX(struct ToriRS_WorldTap const* tap);
int
ToriRS_WorldTapY(struct ToriRS_WorldTap const* tap);
int
ToriRS_WorldTapFrames(struct ToriRS_WorldTap const* tap);

#endif
