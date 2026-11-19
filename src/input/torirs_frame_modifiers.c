#include "input/torirs_frame_modifiers.h"

#include <assert.h>
#include <string.h>

void
ToriRS_FrameModifiersReset(struct ToriRS_FrameModifiers* mods)
{
    assert(mods);
    memset(mods, 0, sizeof(*mods));
}

void
ToriRS_FrameModifiersLatchCtrl(
    struct ToriRS_FrameModifiers* mods,
    int held)
{
    assert(mods);
    mods->ctrl = held ? 1 : 0;
}

int
ToriRS_FrameModifiersCtrl(struct ToriRS_FrameModifiers const* mods)
{
    assert(mods);
    return mods->ctrl;
}

void
ToriRS_FrameModifiersParkInput(
    struct ToriRS_FrameModifiers* mods,
    struct LibToriRS_Input* input)
{
    assert(mods);
    mods->input = input;
}

struct LibToriRS_Input*
ToriRS_FrameModifiersInput(struct ToriRS_FrameModifiers const* mods)
{
    assert(mods);
    return mods->input;
}
