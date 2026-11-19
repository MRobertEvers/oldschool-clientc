#ifndef SRC_PLATFORM_TEXT_INPUT_GATE_H
#define SRC_PLATFORM_TEXT_INPUT_GATE_H

/*
 * Whether the platform's on-screen keyboard (its text input) should be up, and
 * whether that answer has changed since the platform was last told.
 *
 * Three things can want it. The client's own fields -- the login form, the
 * chat line -- which the caller works out and passes in. The mobile scripts,
 * whose MOBILE_KEYBOARDSHOW asks for it and whose hide puts it away. And a
 * plugin, which states on or off. Any one of them is enough.
 *
 * The push is edge-triggered: TextInputGate_Take hands the answer out only
 * when it differs from what was last handed out, or when a plugin has stated
 * a new request. The person can dismiss the keyboard with the system's own
 * button, which changes none of the wants, so a deliberate ask (a tap on a
 * login field, a script's show or hide, a plugin focusing the chat line)
 * invalidates what was last pushed and the next take pushes the current
 * answer again.
 *
 * The platform reports how many canvas rows the keyboard covers. That inset
 * falling to zero is the keyboard going away underneath the client -- a
 * system Back looks like nothing else from here -- and it retracts the
 * scripts' standing request with it.
 *
 * Nothing here knows the screen, the login form, the chat line or the
 * platform. The caller says whether its own fields want the keyboard, raises
 * and lowers the platform's text input with what Take hands back, hands the
 * inset to the layout's safe area, and logs.
 */

struct TextInputGate
{
    /** The mobile scripts asked for the soft keyboard (MOBILE_KEYBOARDSHOW*)
     *  and have not asked to put it away. Its own flag rather than the chat
     *  line's focus: focusing the chat line means "typed keys go to the chat
     *  line", which the script never said, and on a phone nothing else ever
     *  clears it -- so the client would ask for the keyboard for the rest of
     *  the session, and the safe-area inset it brings squashes the frame into
     *  the top half of the screen. Cleared by the script's own hide, and by
     *  the keyboard going away underneath it (the inset falling to zero). */
    int vm_keyboard_open;
    /** CANVAS rows the OS soft keyboard covers at the bottom, 0 when it is
     *  away (TORIRS_CMD_KEYBOARD_INSET; only touch platforms ever push it). */
    int keyboard_inset;
    /** A plugin asked for the on-screen keyboard. */
    int plugin_on;
    /** What the platform was last told; -1 when that has been forgotten so the
     *  next take pushes the current answer again. */
    int effective;
    /** A change waits for the next take. */
    int dirty;
};

/** Nothing wanted, nothing pushed, no inset. */
void
TextInputGate_Reset(struct TextInputGate* gate);

/** A plugin states the keyboard on or off. Always taken by the next take, even
 *  when the answer it leads to is the one already pushed. */
void
TextInputGate_SetPluginRequest(
    struct TextInputGate* gate,
    int on);

/** The mobile scripts show (`open` non-zero) or hide the keyboard. */
void
TextInputGate_SetScriptRequest(
    struct TextInputGate* gate,
    int open);

/** Forget what was last pushed, so the next take pushes the answer again. */
void
TextInputGate_Invalidate(struct TextInputGate* gate);

/**
 * The platform reports the keyboard covering `rows` canvas rows. Returns 1
 * when that differs from the last report, 0 when it does not. A fall from a
 * positive inset to zero retracts the scripts' request.
 */
int
TextInputGate_SetKeyboardInset(
    struct TextInputGate* gate,
    int rows);

/** Canvas rows the keyboard covers, as last reported. */
int
TextInputGate_KeyboardInset(struct TextInputGate const* gate);

/**
 * Whether the keyboard should be up: `client_wants` (the caller's own fields),
 * or the scripts' request, or a plugin's. 1 or 0.
 */
int
TextInputGate_Wanted(
    struct TextInputGate const* gate,
    int client_wants);

/**
 * Take a change in the answer, once. `wanted` is TextInputGate_Wanted's answer
 * this frame. Returns 1 and fills `*out_on` when the platform must be told;
 * returns 0 and leaves `*out_on` untouched when it need not.
 */
int
TextInputGate_Take(
    struct TextInputGate* gate,
    int wanted,
    int* out_on);

#endif /* SRC_PLATFORM_TEXT_INPUT_GATE_H */
