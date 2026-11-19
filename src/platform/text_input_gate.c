#include "platform/text_input_gate.h"

#include <assert.h>
#include <string.h>

void
TextInputGate_Reset(struct TextInputGate* gate)
{
    assert(gate);
    memset(gate, 0, sizeof(*gate));
}

void
TextInputGate_SetPluginRequest(
    struct TextInputGate* gate,
    int on)
{
    assert(gate);
    gate->plugin_on = on ? 1 : 0;
    gate->dirty = 1;
}

void
TextInputGate_SetScriptRequest(
    struct TextInputGate* gate,
    int open)
{
    assert(gate);
    gate->vm_keyboard_open = open ? 1 : 0;
}

void
TextInputGate_Invalidate(struct TextInputGate* gate)
{
    assert(gate);
    gate->effective = -1;
}

int
TextInputGate_SetKeyboardInset(
    struct TextInputGate* gate,
    int rows)
{
    assert(gate);
    if( gate->keyboard_inset == rows )
        return 0;
    /* The keyboard went away under the client. A script's standing request
     * to show it does not survive that. */
    if( gate->keyboard_inset > 0 && rows == 0 )
        gate->vm_keyboard_open = 0;
    gate->keyboard_inset = rows;
    return 1;
}

int
TextInputGate_KeyboardInset(struct TextInputGate const* gate)
{
    assert(gate);
    return gate->keyboard_inset;
}

int
TextInputGate_Wanted(
    struct TextInputGate const* gate,
    int client_wants)
{
    assert(gate);
    if( client_wants )
        return 1;
    if( gate->vm_keyboard_open )
        return 1;
    /* Kept last so the client's own fields win when both are true. */
    return gate->plugin_on ? 1 : 0;
}

int
TextInputGate_Take(
    struct TextInputGate* gate,
    int wanted,
    int* out_on)
{
    assert(gate);
    assert(out_on);

    if( wanted != gate->effective )
    {
        gate->effective = wanted;
        gate->dirty = 1;
    }

    if( !gate->dirty )
        return 0;
    gate->dirty = 0;
    *out_on = gate->effective;
    return 1;
}
