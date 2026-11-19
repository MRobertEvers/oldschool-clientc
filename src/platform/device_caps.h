#ifndef SRC_PLATFORM_DEVICE_CAPS_H
#define SRC_PLATFORM_DEVICE_CAPS_H

/*
 * Three facts about the device the client runs on, stated at boot and read
 * for the rest of the run.
 *
 *   touch_camera     the pointer is a FINGER, so the finger may turn the
 *                    camera whatever the revision's `controls=` list says.
 *                    That list reproduces a revision's DESKTOP control
 *                    scheme; a finger drag is synthesised as a middle-button
 *                    drag (ToriRS_TouchSetViewport says why) and would be
 *                    refused by the same test, for a decision no revision
 *                    made, because none of them ran on a phone. The platform
 *                    states it.
 *   touch_ui         the interface is sized for a finger: the minimenu opens
 *                    in its touch style, the row a tap chooses lingers after
 *                    the popup has gone, the plugin chrome is an edge tab. It
 *                    is a POLICY, and the one thing decided here:
 *                    DeviceCaps_TouchUiRule says it follows the clientscript
 *                    identity (clienttype 7, or on_mobile), is always on
 *                    where the platform's pointer is a finger, and that an
 *                    override read as a NUMBER outranks both, so `0` can take
 *                    a mobile-identity run back to the desktop popup. It is
 *                    what `core.capability("touch")` answers, so it must be
 *                    set before any plugin starts.
 *   screen_keyboard  the device can raise an on-screen keyboard, so a chrome
 *                    may offer a switch for it. Not the same fact as
 *                    touch_ui: nothing above the platform can overrule it,
 *                    and a desk with the touch policy on still has no keys
 *                    to summon. It is what
 *                    `core.capability("input.screen_keyboard")` answers.
 *
 * A zeroed struct is a mouse on a desk with no on-screen keyboard.
 *
 * This does not know which platform it is on, what identity the scripts were
 * told, or where an override comes from. The caller asks the platform, works
 * out the identity, reads the override from the environment, and calls the
 * setters before anything reads them.
 */

struct DeviceCaps
{
    /** A finger may turn the camera on every revision. */
    int touch_camera;
    /** The interface is sized for a finger (DeviceCaps_TouchUiRule). */
    int touch_ui;
    /** The device can raise an on-screen keyboard. */
    int screen_keyboard;
};

/** A mouse on a desk with no on-screen keyboard: every field zero. Also the
 *  state of a zeroed struct. */
void
DeviceCaps_Reset(struct DeviceCaps* caps);

/**
 * The touch policy.
 *
 * On when the scripts were told this is the mobile client (`clienttype` 7, or
 * `on_mobile` nonzero) or the platform's pointer is a finger
 * (`touch_platform` nonzero). `override_text`, when it is neither NULL nor
 * empty, is read as a number and outranks all of it: nonzero on, zero off.
 * Returns 1 or 0.
 */
int
DeviceCaps_TouchUiRule(
    int clienttype,
    int on_mobile,
    int touch_platform,
    char const* override_text);

/** The finger may turn the camera; nonzero for yes, stored as 1 or 0. */
void
DeviceCaps_SetTouchCamera(
    struct DeviceCaps* caps,
    int on);

int
DeviceCaps_TouchCamera(struct DeviceCaps const* caps);

/** The touch policy as resolved (DeviceCaps_TouchUiRule); stored as 1 or 0. */
void
DeviceCaps_SetTouchUi(
    struct DeviceCaps* caps,
    int on);

int
DeviceCaps_TouchUi(struct DeviceCaps const* caps);

/** The platform's answer to "can keys be summoned", stored as given: the
 *  platform's own boolean. */
void
DeviceCaps_SetScreenKeyboard(
    struct DeviceCaps* caps,
    int has);

int
DeviceCaps_ScreenKeyboard(struct DeviceCaps const* caps);

#endif
