#ifndef SRC_UITREE_HOVERTEXT_H
#define SRC_UITREE_HOVERTEXT_H

#include "uitree_minimenu.h"

#include <stdbool.h>

/*
 * Mouseover ("hover") text: the single line the client paints at the top-left
 * of the world viewport describing what the pointer would act on.
 *
 * In the reference this is NOT client drawing — it is cache clientscript 4726
 * (re-armed every cycle by wrapper 4725 via if_setontimer on its own container,
 * which it resolves with enum<component,component>(<proc 900 layout enum>,
 * 161:37)). 4726 bails when minimenu_isopen (7108), reads minimenu_entry (7101
 * -> option, target), minimenu_numops (7110) and minimenu_type (7100), then
 * calls proc 4727 to cc_create one TEXT child:
 *
 *   cc_settextfont(496)      b12_full — the font the minimenu already uses
 *   cc_settextshadow(1)
 *   cc_setcolour(0xD8D8D8)
 *   text = "<option> <target>", and when numops > 1:
 *          text + "</col>" + " / " + (numops - 1) + " more options"
 *
 * Here the same line is composed from an already-built UIMinimenu (the client
 * side of that contract: this port owns the menu model the opcodes would
 * expose) and drawn by the UIELEM_BUILTIN_HOVERTEXT emit expansion.
 */

#define UITREE_HOVERTEXT_LEN 192
/** proc 4727 cc_setcolour. */
#define UITREE_HOVERTEXT_COLOR 0xD8D8D8

/** Line box height at the default bold-12 metrics (same box the minimenu rows
 *  draw into, so the baseline lands identically). */
#define UITREE_HOVERTEXT_BOX_H UITREE_MINIMENU_DEFAULT_LINE_BOX

struct UIHoverText
{
    bool visible;
    /** Screen box of the text, left-aligned (app anchors it to the world
     *  viewport, mirroring 4726's container origin). */
    int x;
    int y;
    int w;
    int h;
    /** Cache font scene id (bold-12), shared with the minimenu. */
    int font_id;
    char text[UITREE_HOVERTEXT_LEN];
};

void
UIHoverText_Reset(struct UIHoverText* hover);

/**
 * Compose the line for a menu built at the pointer (already sorted by
 * UIMinimenu_SortPriorityActions, so the acting row is the last one).
 *
 * Returns false and hides when the menu holds nothing but Cancel — the
 * reference shows no text when minimenu_numops is 0. Leaves x/y/font_id alone
 * so the caller owns placement.
 */
bool
UIHoverText_Compose(struct UIMinimenu const* menu, struct UIHoverText* out);

#endif
