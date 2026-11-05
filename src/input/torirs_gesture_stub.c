/*
 * The gesture list's reader, STUBBED until the bus frames land.
 *
 * Workbench 6.1 is two slices: one puts TORIRS_CMD_GESTURE_PAN / _PINCH /
 * _TWIST on the bus (SDL2, Win32, the web) and decodes them into
 * struct LibToriRS_Input; the other (this branch) consumes them in the
 * chrome, the World, the picker and the Preview. Until the two are joined
 * this file answers "no frames, never a touch", so every consumer is wired
 * and testable (the tests hand it frames directly) and the client behaves
 * exactly as before. The integrator deletes this file and points the two
 * functions at the decoded list (src/input/torirs_gesture.h).
 */
#include "input/torirs_gesture.h"

#include <assert.h>
#include <stddef.h>

int
ToriRS_InputGestures(
    const struct LibToriRS_Input* input,
    const struct ToriRS_GestureFrame** out)
{
    assert(input);
    assert(out);
    (void)input;
    *out = NULL;
    return 0;
}

int
ToriRS_InputLastWasTouch(const struct LibToriRS_Input* input)
{
    assert(input);
    (void)input;
    return 0;
}
