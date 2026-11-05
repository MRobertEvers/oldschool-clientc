#ifndef SRC_EDITOR_PANELS_EDPANEL_CHIPS_H
#define SRC_EDITOR_PANELS_EDPANEL_CHIPS_H

/**
 * A strip of chips (workbench 6.2): the Catalog's filter chips under Find
 * ("shape: wall x"), its shortcut chips ("favourites (14)"), and the
 * Preview pane's shape and rotation rows ("0 wall", "1", "Fit"). One GRAPH
 * well, so the same picture reaches the canvas and the DOM (rects with keys,
 * text) with no widget of its own:
 *
 *   - chips flow left to right and wrap onto the strip's lines; a caption
 *     wider than a line is measured and ellipsised (rule R2), never cut, and
 *     chips past the last line become one "+n" chip whose tooltip names them;
 *   - a chip is a keyed rect: a press on it is a GRAPH_PRESS event carrying
 *     its key (Edp_ChipsKeyOf); a chip with an `x` wears a second key on its
 *     x, so the x removes and the body does what the body does;
 *   - hovering a chip shows its own tooltip (edp_chips_hover, once a frame:
 *     compare-then-set, nothing allocated);
 *   - the picture is restated through GraphBegin / End, which records
 *     nothing when it did not change.
 */

#include "editor/editor_ui.h"

#define EDP_CHIPS_MAX 24
#define EDP_CHIP_LABEL_MAX 48
/** A tooltip holds 63 bytes (TORIRS_CHROME_LABEL_MAX). */
#define EDP_CHIP_TIP_MAX 64
/** One chip line, logical px: a row of the chrome's text plus its air. */
#define EDP_CHIP_H 16
#define EDP_CHIP_GAP 3
/** Keys: a chip's own is EDP_CHIP_KEY_BASE + its index, its x
 *  EDP_CHIP_X_BASE + its index, the "+n" chip EDP_CHIP_MORE_KEY. */
#define EDP_CHIP_KEY_BASE 1000
#define EDP_CHIP_X_BASE 2000
#define EDP_CHIP_MORE_KEY 3000

struct Edp_Chip
{
    char label[EDP_CHIP_LABEL_MAX];
    char tip[EDP_CHIP_TIP_MAX];
    /** Drawn lit (the active shortcut, the current shape). */
    int active;
    /** Wears an x that removes it. */
    int has_x;
    /** The action's chord, drawn as a keycap after the caption ("" none;
     *  rule R4: the chord next to the action). */
    char chord[24];
    /* Laid out: well-local logical px. -1 x when it did not fit. */
    int x;
    int y;
    int w;
};

struct Edp_ChipStrip
{
    int graph;
    int lines;
    struct Edp_Chip chips[EDP_CHIPS_MAX];
    int count;
    /** Chips that did not fit, and the "+n" chip's box. */
    int hidden;
    int more_x;
    int more_y;
    int more_w;
    char more_tip[EDP_CHIP_TIP_MAX];
    /** What the picture was laid out from (labels, flags, width): an
     *  unchanged strip is not measured again. */
    uint64_t hash;
};

/** The strip's GRAPH on `panel`, `lines` chip lines tall. */
void
edp_chips_init(
    struct EditorUi* ui,
    struct Edp_ChipStrip* strip,
    int panel,
    int lines);

/** Start restating the chips. */
void
edp_chips_begin(struct Edp_ChipStrip* strip);

/** One chip; `tip` may be NULL (the caption is its tip). Returns its index,
 *  -1 past EDP_CHIPS_MAX. */
int
edp_chips_push(
    struct Edp_ChipStrip* strip,
    const char* label,
    const char* tip,
    int active,
    int has_x);

/** The chord the last pushed chip wears as a keycap (Editor_HotkeysJoin's
 *  spelling, "" none). */
void
edp_chips_set_chord(
    struct Edp_ChipStrip* strip,
    int index,
    const char* chord);

/** Lay the chips out at the well's width and restate the picture. */
void
edp_chips_end(
    struct EditorUi* ui,
    struct Edp_ChipStrip* strip);

/**
 * What a GRAPH event's key names: the chip's index, with `*out_x` 1 when it
 * was the chip's x; -1 for no chip (the "+n" chip, the well's empty part).
 */
int
edp_chips_key_of(
    const struct Edp_ChipStrip* strip,
    int key,
    int* out_x);

/** The tooltip follows the chip under the pointer. Once a frame. */
void
edp_chips_hover(
    struct EditorUi* ui,
    struct Edp_ChipStrip* strip);

#endif
