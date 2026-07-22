#ifndef SRC_UITREE_MINIMENU_H
#define SRC_UITREE_MINIMENU_H

#include <stdbool.h>

/*
 * Right-click minimenu model (reference Client "Choose Option" menu). Pure
 * data + layout math — population lives in the game layer (rs_minimenu_build)
 * and drawing in the emit walk (UIELEM_BUILTIN_MINIMENU expansion).
 *
 * Layout follows the reference: height = rows * 15 + 22 chrome at the default
 * bold-12 line height, menu horizontally centered on the click, rows drawn
 * bottom-to-top so the highest-priority (last after sort) entry is on top.
 */

#define UITREE_MINIMENU_MAX_OPTIONS 10
#define UITREE_MINIMENU_OPTION_LEN 128
#define UITREE_MINIMENU_DEFAULT_LINE_HEIGHT 14
/** Reference OPTIONS_MENU background/title color. */
#define UITREE_MINIMENU_COLOR_BODY 0x5D5447

/** What a menu row targets. NPC/SCENERY/TERRAIN are the seam for the world
 * pickset task; only NONE/UI/INV_SLOT are producible today. */
enum UIMinimenuPickKind
{
    UI_MINIMENU_PICK_NONE = 0,
    UI_MINIMENU_PICK_UI,
    UI_MINIMENU_PICK_INV_SLOT,
    UI_MINIMENU_PICK_NPC,
    UI_MINIMENU_PICK_SCENERY,
    UI_MINIMENU_PICK_TERRAIN,
    UI_MINIMENU_PICK_OBJ,
};

/*
 * Target reference carried by a row. Nodes are referenced by component_id, not
 * node index — CS2 CC ops can realloc/reclaim node slots between menu open and
 * option select.
 *  UI:       id = component_id
 *  INV_SLOT: id = component_id of the inv node, secondary = slot,
 *            tertiary = obj_id
 */
struct UIMinimenuPick
{
    enum UIMinimenuPickKind kind;
    int id;
    int secondary_id;
    int tertiary_id;
    int quaternary_id;
};

struct UIMinimenuLayout
{
    int line_height;
    int row_stride;
    int header_text_y;
    int header_bar_h;
    int separator_y;
    int option_base_y;
    int chrome_h;
    int hover_above;
    int hover_below;
    int width_pad;
    int click_y_bias;
    int border_inset;
};

struct UIMinimenuOption
{
    char text[UITREE_MINIMENU_OPTION_LEN];
    /** RevConfigMiniMenuAction value (rev-254 action id scheme, +2000 =
     * deprioritized). Kept as int so ui/ stays leaf. */
    int action;
    /** Config op slot 0..4 this row came from, or -1. */
    int action_index;
    struct UIMinimenuPick pick;
};

struct UIMinimenu
{
    bool visible;
    int x;
    int y;
    int width;
    int height;
    int hovered_option; /* -1 = none */
    /** Cache font id used for measuring and drawing rows (bold-12). */
    int font_id;
    struct UIMinimenuLayout layout;
    struct UIMinimenuOption options[UITREE_MINIMENU_MAX_OPTIONS];
    int option_count;
};

/** Width callback: pixel width of text in font_id, or <= 0 when unknown. */
typedef int (*UIMinimenuMeasureFn)(void* ud, int font_id, char const* text);

struct UIMinimenuLayout
UIMinimenu_LayoutFromLineHeight(int line_height);

void
UIMinimenu_Reset(struct UIMinimenu* menu);

void
UIMinimenu_Hide(struct UIMinimenu* menu);

bool
UIMinimenu_AddOption(
    struct UIMinimenu* menu,
    char const* text,
    int action,
    int action_index,
    struct UIMinimenuPick pick);

/** Reference insertion-order bubble: normal-priority rows (< 1000) sink below
 * high ids (> 1000: Cancel/Examine); rows draw bottom-to-top afterwards. */
void
UIMinimenu_SortPriorityActions(struct UIMinimenu* menu);

/** Deprioritize bias (reference +2000): pushes a row below normal entries. */
static inline int
UIMinimenu_ActionDeprioritize(int action)
{
    return action > 1000 ? action : action + 2000;
}

/** Compute layout + content width for the current rows. line_height <= 0 falls
 * back to the default; measure == NULL falls back to strlen * 6. */
bool
UIMinimenu_PrepareShow(
    struct UIMinimenu const* menu,
    int line_height,
    UIMinimenuMeasureFn measure,
    void* measure_ud,
    struct UIMinimenuLayout* out_layout,
    int* out_content_width);

int
UIMinimenu_Height(struct UIMinimenuLayout const* layout, int option_count);

/** Text baseline-row y of option i (bottom-to-top: 0 = bottom row). */
int
UIMinimenu_OptionY(struct UIMinimenu const* menu, int option_index);

/** Place at the click (centered horizontally, biased up), clamped inside the
 * viewport, and mark visible. */
void
UIMinimenu_ShowAt(
    struct UIMinimenu* menu,
    struct UIMinimenuLayout layout,
    int content_width,
    int click_x,
    int click_y,
    int viewport_w,
    int viewport_h);

/** >= 0 option row; -1 inside chrome / margin; -2 outside (close). */
int
UIMinimenu_HitOption(struct UIMinimenu const* menu, int click_x, int click_y);

/** Returns true when the hovered row changed. */
bool
UIMinimenu_UpdateHover(struct UIMinimenu* menu, int mouse_x, int mouse_y);

#endif
