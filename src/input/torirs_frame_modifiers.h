#ifndef SRC_INPUT_TORIRS_FRAME_MODIFIERS_H
#define SRC_INPUT_TORIRS_FRAME_MODIFIERS_H

/*
 * The modifier keys as they stood at this frame's input pump, held for the
 * code that acts later in the same frame without the input in hand, and the
 * input frame itself, parked for the code that has to ask about a key of its
 * own choosing (a plugin's key_held, the menu's Shift).
 *
 * One modifier so far: Ctrl (reference keyHeld[5]). The reference reads it
 * inside tryMove, so it applies to ground, minimap AND interaction clicks
 * alike; the minimenu action path that runs the last two has no input handle,
 * so the state is latched once a frame and read from here. The move packets
 * carry it as their key combination.
 *
 * The parked input is a pointer the caller owns; NULL, the state before any
 * frame has been pumped, is "no input yet" and every key reads as up.
 *
 * A zeroed struct is the state with no modifier held and no input parked.
 *
 * This does not know what a key is, when a frame begins, or what a held
 * modifier changes. The caller reads the key from its input, latches the
 * answer at the point of its frame where the input is pumped, and passes what
 * it reads on to whatever builds the move.
 */

struct LibToriRS_Input;

struct ToriRS_FrameModifiers
{
    /** Ctrl was held as of the last latch, 1 or 0. */
    int ctrl;
    /** This frame's input, as last parked; NULL before the first. */
    struct LibToriRS_Input* input;
};

/** No modifier held, no input parked, every field zero. Also the state of a zeroed struct. */
void
ToriRS_FrameModifiersReset(struct ToriRS_FrameModifiers* mods);

/** This frame's Ctrl: nonzero when it is held. Stored as 1 or 0. */
void
ToriRS_FrameModifiersLatchCtrl(
    struct ToriRS_FrameModifiers* mods,
    int held);

/** Ctrl as of the last latch, 1 or 0. */
int
ToriRS_FrameModifiersCtrl(struct ToriRS_FrameModifiers const* mods);

/** Park this frame's input (NULL is none) for code later in the frame. */
void
ToriRS_FrameModifiersParkInput(
    struct ToriRS_FrameModifiers* mods,
    struct LibToriRS_Input* input);

/** The input as last parked; NULL when none has been. */
struct LibToriRS_Input*
ToriRS_FrameModifiersInput(struct ToriRS_FrameModifiers const* mods);

#endif
