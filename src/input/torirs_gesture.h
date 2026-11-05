#ifndef SRC_INPUT_TORIRS_GESTURE_H
#define SRC_INPUT_TORIRS_GESTURE_H

/*
 * One frame's gesture frames: the continuous PAN / PINCH / TWIST the editor
 * and the chrome consume (workbench design section 14, "Mouse, trackpad,
 * touch": the three bus frames).
 *
 * The bus carries them as TORIRS_CMD_GESTURE_PAN / _PINCH / _TWIST, decoded
 * into struct LibToriRS_Input as a per-frame list, oldest first, cleared at
 * the frame's Begin like the key events. This header is the consumers' half
 * of that contract: what one frame is and how to read the list. The game
 * never reads it -- its touch synthesis (src/input/torirs_touch.c: the
 * middle-button drag, the wheel notches, the arrow keys) stays what it was.
 *
 * Units are canvas pixels, the pointer's own space, so a consumer that
 * hit-tests (the chrome) and one that projects (the World) agree about
 * where a frame is.
 */

#include "input/torirs_input.h"

/** enum ToriRS_GestureKind: which of the three a frame is. */
enum ToriRS_GestureKind
{
    TORIRS_GESTURE_NONE = 0,
    /** dx, dy canvas px since the last frame; `fingers` 2 for a touch pan or
     *  a trackpad's two-finger scroll, 1 for a one-finger drag where the host
     *  reports one. */
    TORIRS_GESTURE_PAN,
    /** scale_q16: the ratio since the last frame, 65536 = 1 (> 1 fingers
     *  apart), about (x, y). */
    TORIRS_GESTURE_PINCH,
    /** radians_q16: the turn since the last frame, 65536 = 1 radian,
     *  positive clockwise on the canvas (y grows down), about (x, y). */
    TORIRS_GESTURE_TWIST,
};

/** enum ToriRS_GesturePhase. */
enum ToriRS_GesturePhase
{
    TORIRS_GESTURE_BEGIN = 0,
    TORIRS_GESTURE_UPDATE,
    TORIRS_GESTURE_END,
};

/** Frames one input frame carries at most. */
#define TORIRS_GESTURE_FRAMES_MAX 16

struct ToriRS_GestureFrame
{
    /** enum ToriRS_GestureKind. */
    int kind;
    /** The gesture's centre (the pointer, the fingers' midpoint), canvas px. */
    int x;
    int y;
    /** PAN only. */
    int dx;
    int dy;
    int fingers;
    /** PINCH only. */
    int scale_q16;
    /** TWIST only. */
    int radians_q16;
    /** enum ToriRS_GesturePhase. */
    int phase;
};

/**
 * This frame's gesture frames, oldest first, into `*out` (borrowed: valid
 * until the input's next Begin). Returns how many; 0 leaves `*out` NULL.
 */
int
ToriRS_InputGestures(
    const struct LibToriRS_Input* input,
    const struct ToriRS_GestureFrame** out);

/**
 * Was this frame's last pointer event a finger (a touch pan, pinch, tap or
 * drag) rather than a mouse or a trackpad? The input's last-event kind; it
 * holds across frames with no pointer event. The hint strip spells the
 * gestures while it does (Editor_HotkeysHint's `touch`).
 */
int
ToriRS_InputLastWasTouch(const struct LibToriRS_Input* input);

#endif /* SRC_INPUT_TORIRS_GESTURE_H */
