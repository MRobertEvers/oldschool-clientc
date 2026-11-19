#ifndef SRC_INPUT_TORIRS_INPUT_CLAIMS_H
#define SRC_INPUT_TORIRS_INPUT_CLAIMS_H

#include "input/torirs_keymap.h"

#include <stdint.h>

/*
 * Who has already taken a part of this frame's input, so that the next reader
 * of the same input stands down instead of acting on it a second time.
 *
 * Three claims, each with its own lifetime:
 *
 *   keys     by OSRS key code: a key a hotkey binding acted on. A debug key
 *            on the same OSRS key does not also fire. Cleared by
 *            ToriRS_InputClaimsClearKeys, which the caller runs once a frame
 *            before the bindings dispatch; a claim made earlier in the frame
 *            that must survive the clear is claimed again after it.
 *   frame    the input frame as a whole: something acted on it, so it must
 *            not be held over and handed back next frame, and a world click
 *            that comes after it stands down. Dropped by
 *            ToriRS_InputClaimsBeginFrame at the top of a frame.
 *   pointer  this frame's pointer is over chrome drawn in the canvas, so the
 *            game sees none of it. Latched once a frame, after the chrome
 *            that may have moved, opened or closed has ticked, and read by
 *            everything after; it holds until the next latch.
 *
 * A zeroed struct is the state with nothing claimed.
 *
 * This does not know what a key is bound to, which chrome is under the
 * pointer, or when a frame begins. The caller maps a key to its OSRS code,
 * asks its chrome whether the pointer is over it and latches the answer, and
 * calls the begin and the clear at the points of its frame they belong to.
 */

struct ToriRS_InputClaims
{
    /** Keys a binding acted on this frame, indexed by OSRS key code. */
    uint8_t key[TORIRS_OSRSKEY_COUNT];
    /** Something has acted on this input frame. */
    int frame;
    /** This frame's pointer is over chrome, as last latched. */
    int chrome_pointer;
};

/** Nothing claimed, every field zero. Also the state of a zeroed struct. */
void
ToriRS_InputClaimsReset(struct ToriRS_InputClaims* claims);

/** A new input frame: drops the frame claim. Keys and the pointer latch are
 *  left as they are. */
void
ToriRS_InputClaimsBeginFrame(struct ToriRS_InputClaims* claims);

/** Something acted on this input frame. */
void
ToriRS_InputClaimsTakeFrame(struct ToriRS_InputClaims* claims);

/** Has anything acted on this input frame since it began? */
int
ToriRS_InputClaimsFrameTaken(struct ToriRS_InputClaims const* claims);

/** Drops every key claim. The frame claim and the pointer latch are left. */
void
ToriRS_InputClaimsClearKeys(struct ToriRS_InputClaims* claims);

/** `osrs_key` (0 .. TORIRS_OSRSKEY_COUNT-1) acted this frame. */
void
ToriRS_InputClaimsClaimKey(
    struct ToriRS_InputClaims* claims,
    int osrs_key);

/** Has `osrs_key` (0 .. TORIRS_OSRSKEY_COUNT-1) acted this frame? */
int
ToriRS_InputClaimsKeyClaimed(
    struct ToriRS_InputClaims const* claims,
    int osrs_key);

/** This frame's answer to "is the pointer over chrome", nonzero for yes. */
void
ToriRS_InputClaimsLatchChromePointer(
    struct ToriRS_InputClaims* claims,
    int owned);

/** The last latched answer, as given to the latch. */
int
ToriRS_InputClaimsChromePointer(struct ToriRS_InputClaims const* claims);

#endif
