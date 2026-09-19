#ifndef SRC_UI_UITREE_KEYBOARD_OWNER_H
#define SRC_UI_UITREE_KEYBOARD_OWNER_H

/**
 * Is the keyboard spoken for by something the player is typing into?
 *
 * The one question every hotkey has to ask, and the reason it is a declared
 * STRUCT rather than a conjunction written out at each call site: every such
 * spelling is a list of the text inputs that existed when it was written, so a
 * text input added later silently leaves the hotkeys live underneath it.
 *
 * That has happened four times already, and each time it looked like a
 * different bug: `f` switching a sidebar tab behind the caret; a spawn digit
 * spawning an npc while typing a bank amount; `W` flying the camera through a
 * search term; typing a height value into the map editor both editing the
 * field and toggling the editor on the same keystroke.
 *
 * So the sources are FIELDS. Adding a sixth kind of text input means adding a
 * field here, which is a change every caller compiles against -- rather than a
 * clause somebody has to remember to add to a boolean expression in five
 * places.
 */

#include <stdbool.h>

/**
 * Every place a keystroke could be going, gathered by the caller.
 *
 * The caller owns all five subsystems and is the only thing that can see them
 * at once; what is here is only the rule for combining them.
 */
struct UIKeyboardOwners
{
    /** The revconfig chat line exists in this tree at all. Without it the
     *  three chat flags below are stale state from a lane that has no
     *  chatbox. */
    bool chat_line_present;
    /** The chat line has focus -- taken by clicking it or pressing Enter. */
    bool chat_input_active;
    /** The "add friend" / "add ignore" prompt is open. */
    bool chat_social_input_open;
    /** A dialogue is waiting for a typed answer (name, amount). */
    bool chat_dialog_input_open;

    /**
     * An interface's OWN text box has the keyboard.
     *
     * A panel with a search field (settings 134, collection log 621, league
     * tasks) takes it by calling `~chatdefault_stopinput`, which sets a varc
     * and disarms the chatbox's onKey; `~chatdefault_restoreinput` puts both
     * back. It is the cache's own focus flag, and reading it is how the client
     * learns that something it does not own is being typed into.
     *
     * Unset reads -1, which is NOT "a box has it" -- so the caller must pass
     * the varc's value here and let this compare against 1, rather than
     * passing a truth value it decided itself.
     */
    int interface_input_active_varc;

    /** An IF3 text-entry field with the caret in it, or -1. The third kind of
     *  text input in this client and the newest -- exactly the case this
     *  module's header warns about. @see UITree_InputFocusId. */
    int tree_input_focus_id;

    /**
     * A ToriRSChrome field under the caret, in EITHER chrome instance.
     *
     * Both, which is the whole point. The developer chrome (the map editor's
     * Height field and its neighbours) was named and the plugin window was
     * not, so a keystroke aimed at a plugin's colour or note field also fired
     * whatever debug hotkey shares that letter, and was typed into the chat
     * line underneath.
     */
    bool chrome_field_focused;
};

/** True when any of the sources above has the keyboard. */
bool
UIKeyboard_TextInputFocused(struct UIKeyboardOwners const* owners);

#endif /* SRC_UI_UITREE_KEYBOARD_OWNER_H */
