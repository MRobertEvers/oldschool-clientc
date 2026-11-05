#ifndef SRC_EDITOR_PANELS_EDPANEL_PICKER_NAV_H
#define SRC_EDITOR_PANELS_EDPANEL_PICKER_NAV_H

/**
 * The square picker's navigation and panes, the pure half (workbench design
 * section 5, "Navigation"; build step 6.4): the focused cell and how the
 * arrows move it, the open set the strip shows and its order, the strip's
 * collapse rule, the Recent list, and text fitted to a track. The Squares
 * panel (edpanel_squares.c) owns the instances and draws them; nothing here
 * touches the chrome or allocates.
 */

#include "edpanel_picker.h"

/* ---- the focused cell ------------------------------------------------------
 *
 * The focus is the picker's `focus` square, or the empty cell New would make
 * (`empty_valid`). The arrows move it a square at a time, north up; Enter
 * opens it; the detail pane answers for it. It is drawn with the selection
 * outline (the Squares panel colours that white).
 */

/** The focused cell: 1 and its coordinates, or 0 when nothing is focused. */
int
Editor_PickerFocusCell(
    const struct Editor_Picker* picker,
    int* out_map_x,
    int* out_map_z);

/** Focus cell (map_x, map_z): the square there (the page's storage first),
 *  else the empty cell. The selection becomes that square alone. */
void
Editor_PickerFocusSet(
    struct Editor_Picker* picker,
    int map_x,
    int map_z);

/** Move the focus (dx, dz) squares (+z north), clamped to the world; from
 *  nothing, the cell under the well's middle is the start. The view pans so
 *  the cell is in the well. Returns 1 when the focus moved. */
int
Editor_PickerFocusStep(
    struct Editor_Picker* picker,
    int dx,
    int dz);

/** Pan the least that brings cell (map_x, map_z) wholly into the well (its
 *  middle, when the cell is larger than the well). */
void
Editor_PickerKeepInView(
    struct Editor_Picker* picker,
    int map_x,
    int map_z);

/* ---- the open set ------------------------------------------------------------
 *
 * What this window holds: every square in its world now (OPEN_HERE) and every
 * one with unsaved edits in its mirror (DIRTY_HERE), in the order they were
 * opened, reordered by a drag on the strip. Kept by full key, so an atlas
 * refresh that renumbers the storages keeps the order.
 */

#define EDITOR_PICKER_OPEN_SET_MAX 64

struct Editor_PickerOpenItem
{
    char key[EDITOR_DOC_KEY_MAX];
    int map_x;
    int map_z;
    /** In this window's world now. */
    int here;
    /** Unsaved in this window's mirror. */
    int dirty;
};

struct Editor_PickerOpenSet
{
    int count;
    struct Editor_PickerOpenItem items[EDITOR_PICKER_OPEN_SET_MAX];
};

/** Follow the picker's OPEN_HERE / DIRTY_HERE flags: squares no longer held
 *  leave, new ones join at the end (open order), the rest keep their place
 *  and take their flags. Returns 1 when anything changed. */
int
Editor_PickerOpenSetSync(
    struct Editor_PickerOpenSet* set,
    const struct Editor_Picker* picker);

/** Index of `key` in the set, or -1. */
int
Editor_PickerOpenSetFind(
    const struct Editor_PickerOpenSet* set,
    const char* key);

/** Move item `from` to position `to` (the others shift). */
void
Editor_PickerOpenSetMove(
    struct Editor_PickerOpenSet* set,
    int from,
    int to);

/** The chip's text for a key: "m50_50", or "<lane>/m16_101" for a lane's. */
void
Editor_PickerChipText(
    const char* key,
    char out[EDITOR_PICKER_NAME_MAX]);

/* ---- the strip's collapse rule ------------------------------------------------
 *
 * Chips in order while they fit the track; when they do not, as many as fit
 * BESIDE a "+N" chip for the rest (whose names its tooltip lists). Nothing is
 * ever clipped: a chip either shows whole or is counted in N.
 */

struct Editor_PickerStripLayout
{
    /** Chips shown, the first `shown` of the set; `hidden` = count - shown. */
    int shown;
    int hidden;
    int x[EDITOR_PICKER_OPEN_SET_MAX];
    int w[EDITOR_PICKER_OPEN_SET_MAX];
    /** The "+N" chip, when hidden > 0. */
    int more_x;
    int more_w;
};

/** Lay `count` chips of `widths` into `track` px with `gap` between:
 *  `more_width(user, n)` is the width of the "+n" chip. */
void
Editor_PickerStripLay(
    const int* widths,
    int count,
    int track,
    int gap,
    int (*more_width)(void* user, int hidden),
    void* user,
    struct Editor_PickerStripLayout* out);

/* ---- Recent -------------------------------------------------------------------- */

#define EDITOR_PICKER_RECENT_MAX 8

struct Editor_PickerRecent
{
    int count;
    /** Newest first. */
    char keys[EDITOR_PICKER_RECENT_MAX][EDITOR_DOC_KEY_MAX];
};

/** `key` to the front; once only; the oldest past EDITOR_PICKER_RECENT_MAX
 *  goes. */
void
Editor_PickerRecentPush(
    struct Editor_PickerRecent* recent,
    const char* key);

/* ---- text on a track ------------------------------------------------------------ */

/**
 * `text` as at most `width` px by `measure(user, s)`: whole when it fits,
 * else its longest prefix that fits with "..." after it (never ending on a
 * blank before the dots), else "...". Returns 1 when it was shortened.
 * `out_size` must hold the dots (> 4).
 */
int
Editor_PickerFitText(
    const char* text,
    int width,
    int (*measure)(void* user, const char* s),
    void* user,
    char* out,
    int out_size);

#endif
