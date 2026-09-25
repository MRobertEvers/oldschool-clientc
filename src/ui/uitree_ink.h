#ifndef SRC_UITREE_INK_H
#define SRC_UITREE_INK_H

#include <stdbool.h>

/*
 * The touch marker's STATE. Its artwork is ui/torirs_chrome_inkwell.h.
 *
 * ## How this differs from UICross, which is the whole point
 *
 * UICross is shown by the code paths that DID something: a walk was routed, an
 * interaction was sent. A tap that hits an interface widget, that misses every
 * target, or that lands while a modal is up shows no cross at all -- correctly,
 * because on a desktop the pointer is visible and the user can see the machine
 * received the click.
 *
 * A touchscreen has no pointer. A tap that draws nothing is indistinguishable
 * from a tap the digitiser dropped, and the user's response is to tap again --
 * which is how a missed tap becomes a double action. So this marker is shown
 * for EVERY touch, before anything has decided what the touch means, and the
 * colour is filled in afterwards by whatever the touch turned out to be.
 *
 * That ordering is the reason this is separate state rather than a mode on
 * UICross: the cross is shown *because* of an outcome and cannot be shown
 * before one exists.
 */

#include "ui/torirs_chrome_inkwell.h"

struct UIInk
{
    int x;
    int y;
    /** enum ToriRSInkwellColour. Set at Show and refinable until the marker
     *  expires -- @see UIInk_SetColour. */
    int colour;
    /** Milliseconds since it was shown; the marker ends at
     *  TORIRS_INKWELL_FRAMES * TORIRS_INKWELL_FRAME_MS. */
    int cycle;
    int active;
};

void
UIInk_Reset(struct UIInk* ink);

/**
 * A touch landed here. Starts the animation from frame 0.
 *
 * Called before the touch has been interpreted, so `colour` is the profile's
 * default for "a touch happened"; the interpretation refines it a moment later
 * through UIInk_SetColour.
 */
void
UIInk_Show(struct UIInk* ink, int colour, int x, int y);

/**
 * Recolour the marker that is already running, without restarting it.
 *
 * The walk/interact answer arrives in the same frame as the touch but AFTER
 * it, and restarting the animation there would drop the first frame and make
 * every interaction's marker visibly shorter than every walk's.
 */
void
UIInk_SetColour(struct UIInk* ink, int colour);

/**
 * The press turned out to be a drag -- take the marker back.
 *
 * A drag is not a tap, and the marker answers "the glass saw a tap". The
 * finger that is scrolling a list, turning the camera or carrying an inventory
 * item has continuous proof the device is following it, so the ripple left
 * behind at the grab point is noise the reference does not draw either.
 *
 * Distinct from letting it expire: this ends the animation wherever it had
 * got to, and distinct from UIInk_Reset only in saying why.
 */
void
UIInk_Cancel(struct UIInk* ink);

void
UIInk_Tick(struct UIInk* ink, int delta_ms);

bool
UIInk_IsActive(struct UIInk const* ink);

/** Frame within the style's pack, 0..TORIRS_INKWELL_FRAMES-1. */
int
UIInk_Frame(struct UIInk const* ink);

#endif
