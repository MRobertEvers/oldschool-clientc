#include "ui_inv_selection.h"

void
ui_inv_selection_reset(struct UIInvSelection* sel)
{
    if( !sel )
        return;
    sel->source_id = -1;
    sel->slot = -1;
}

void
ui_inv_selection_set(struct UIInvSelection* sel, int source_id, int slot)
{
    if( !sel )
        return;
    sel->source_id = source_id;
    sel->slot = slot;
}

void
ui_inv_selection_clear(struct UIInvSelection* sel)
{
    ui_inv_selection_reset(sel);
}

bool
ui_inv_selection_is_set(struct UIInvSelection const* sel)
{
    return sel && sel->source_id >= 0 && sel->slot >= 0;
}

bool
ui_inv_selection_matches(
    struct UIInvSelection const* sel,
    int source_id,
    int slot)
{
    return sel && sel->source_id == source_id && sel->slot == slot;
}
