#ifndef UI_INV_SELECTION_H
#define UI_INV_SELECTION_H

#include <stdbool.h>

/** Selected inventory slot for click-to-use / click-to-swap (Client.ts selectedItem). */
struct UIInvSelection
{
    int source_id;
    int slot;
};

void
ui_inv_selection_reset(struct UIInvSelection* sel);

void
ui_inv_selection_set(struct UIInvSelection* sel, int source_id, int slot);

void
ui_inv_selection_clear(struct UIInvSelection* sel);

bool
ui_inv_selection_is_set(struct UIInvSelection const* sel);

bool
ui_inv_selection_matches(
    struct UIInvSelection const* sel,
    int source_id,
    int slot);

#endif
