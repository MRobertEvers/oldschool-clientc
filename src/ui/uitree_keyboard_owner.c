#include "ui/uitree_keyboard_owner.h"

#include <assert.h>

bool
UIKeyboard_TextInputFocused(struct UIKeyboardOwners const* owners)
{
    assert(owners);

    /* The chat flags only mean anything when this lane has a chat line at all;
     * a dat2 gameframe with no revconfig chatbox leaves them set from
     * whatever ran before. */
    if( owners->chat_line_present &&
        (owners->chat_input_active || owners->chat_social_input_open ||
         owners->chat_dialog_input_open) )
        return true;

    /* `== 1`, not truthiness: unset reads -1. */
    if( owners->interface_input_active_varc == 1 )
        return true;

    if( owners->tree_input_focus_id >= 0 )
        return true;

    return owners->chrome_field_focused;
}
