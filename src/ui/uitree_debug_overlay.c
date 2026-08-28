#include "uitree_debug_overlay.h"
#include <assert.h>

/* The only include with any content in it: `static const int` advance tables
 * emitted by fontbake. Nothing else in this file reaches outside the C
 * library — see the dependency note in the header. */
#include "uitree_debug_font_metrics.h"
/* Macros only -- the authored geometry and palette this chrome shares with
 * every other presentation of the same model. See the note there on why no one
 * file owns them. */
#include "torirs_chrome_metrics.h"

#include <string.h>

/* The two names for one ceiling: the model's, which the widgets are clamped
 * against, and the authored one the CS2 and DOM presentations build their own
 * list from. They have to be the same number, and this is the one file that
 * can see both. */
_Static_assert(
    (int)TORIRS_CHROME_DROPDOWN_ROWS == (int)TORIRS_CHROME_M_DROP_LIST_ROWS,
    "chrome: the model's dropdown row ceiling is not the authored one");

/* ---- chrome metrics ------------------------------------------------------
 *
 * Client chrome, in the same category as UIMinimenu_LayoutFromLineBox: not
 * config, not content, no cache behind it. Everything derives from the two
 * baked fonts' line boxes so a different bake reflows rather than overlaps.
 *
 * Every one of them is a CHROME pixel, not a screen pixel: DBG_PX multiplies
 * by the overlay's scale, so the whole of this table is one authored size that
 * a HighDPI or scaled display reads at 2x or 3x. Scale 1 is the identity, so
 * the numbers below are still literally the geometry a 1x chrome lays out.
 */

/**
 * A chrome pixel count at this overlay's scale.
 *
 * Expands `ui`, exactly as DBG_DROP_ROW_H already did. That is deliberate:
 * every chrome metric is read inside a function that has the ToriRSChrome in
 * hand, so a site that does not have one fails to COMPILE rather than
 * silently laying itself out at 1x underneath text drawn at 3x. Scaled
 * geometry that goes wrong quietly is the failure mode worth designing out --
 * it looks like a rendering bug, not like a missing multiply.
 */
#define DBG_PX(px) ((px) * ui->scale)
/** The chrome's hairline: every rule, rail and border inset is one of these.
 *  A 1px rule under a 3x font reads as a rendering fault, not as a border. */
#define DBG_RULE DBG_PX(1)

/** Inner left/right padding of a window panel's content column. */
#define DBG_PAD_X DBG_PX(TORIRS_CHROME_M_PAD)
/** Inner top/bottom padding of a window panel's content column. */
#define DBG_PAD_Y DBG_PX(TORIRS_CHROME_M_PAD)
/** Extra pixels between consecutive rows. */
#define DBG_ROW_GAP DBG_PX(TORIRS_CHROME_M_ROW_GAP)
/**
 * Height of every row in a window panel, whatever the row holds.
 *
 * Uniform, not font-derived per widget: a column whose rows are each as tall
 * as their own contents does not line its controls up, and a settings list
 * that does not line up is the one thing a settings list has to do. It is
 * still comfortably over the p12 line box, so nothing is cropped -- and a row
 * that genuinely needs more (an open colour picker) grows by an ADDEND below
 * itself rather than by breaking the grid.
 */
#define DBG_ROW_H DBG_PX(TORIRS_CHROME_M_ROW_H)
/** The label column of a labelled row. Fixed -- see TORIRS_CHROME_M_LABEL_W. */
#define DBG_LABEL_W DBG_PX(TORIRS_CHROME_M_LABEL_W)
/**
 * Checkbox edge -- the size of the art it is wearing, and nothing else.
 *
 * The interfaces' on/off pair is a 17x17 sprite (`~script7859` sizes its
 * graphic `cc_setsize(17, 17)`) and the bordered well is an 18x18 one, and a
 * UI sprite drawn at anything but its baked size speckles, since the outline
 * is baked before the scale. So the control is sized to the art rather than
 * the art squeezed into a control -- which is why this reads the instance's
 * style instead of being one constant. The flat fallback box just inherits
 * whichever number came out.
 */
#define DBG_CHECK_SIZE DBG_PX(ToriRSChrome_CheckBoxMetric(ui->check_style))
/** Gap between a checkbox's mark and its label. */
#define DBG_CHECK_GAP DBG_PX(TORIRS_CHROME_M_CHECK_GAP)
/**
 * A LISTROW's switch: the hit box the 17x17 tick/cross is right-aligned in.
 *
 * Wider than the art on purpose -- the slack falls between the sprite and the
 * settings well to its left, where nothing is drawn, and it gives the switch a
 * press target that does not need the pointer on the glyph.
 */
#define DBG_TOGGLE_W DBG_PX(TORIRS_CHROME_M_TOGGLE_W)
#define DBG_TOGGLE_H DBG_PX(TORIRS_CHROME_M_TOGGLE_H)
/** Edge of the settings affordance, and the air around each of the two. */
#define DBG_ROW_ICON DBG_PX(TORIRS_CHROME_M_ROW_ICON)
#define DBG_ROW_ICON_GAP DBG_PX(TORIRS_CHROME_M_ROW_ICON_GAP)
/** Gap between a roster row's name and the furniture that follows it. */
#define DBG_ROW_NAME_GAP DBG_PX(TORIRS_CHROME_M_ROW_NAME_GAP)
/** The three dots of a settings well: size, pitch, and the well's left inset. */
#define DBG_DOT DBG_PX(TORIRS_CHROME_M_DOT)
#define DBG_DOT_PITCH DBG_PX(TORIRS_CHROME_M_DOT_PITCH)
#define DBG_DOT_INSET DBG_PX(TORIRS_CHROME_M_DOT_INSET)
/** Left inset of the text inside a field box. */
#define DBG_INPUT_PAD_X DBG_PX(TORIRS_CHROME_M_FIELD_PAD_X)
/** Inset of a field's decoration: the arrow, the swatch, the focus ring. */
#define DBG_FIELD_INSET DBG_PX(TORIRS_CHROME_M_FIELD_INSET)
/** A text input never lays out narrower than this, even when empty. */
#define DBG_INPUT_MIN_W DBG_PX(60)
/** A multiline field's own two numbers -- the air above the first line inside
 *  its box, and the default line count. @see TORIRS_CHROME_M_TEXTAREA_ROWS. */
#define DBG_TEXTAREA_PAD_Y DBG_PX(TORIRS_CHROME_M_TEXTAREA_PAD_Y)
/** Lines one wrap may produce. A value of TORIRS_CHROME_INPUT_MAX newlines is
 *  that many lines plus the empty one after the last of them, and the wrap must
 *  never be handed a smaller ceiling than its input can reach -- a truncated
 *  wrap puts the caret on a line that is not where the glyphs are. */
#define DBG_TEXTAREA_LINES_MAX (TORIRS_CHROME_INPUT_MAX + 1)
/**
 * Edge of the arrow button on a closed dropdown, and the width of a scrollbar.
 *
 * One number because in the reference it is one sprite: the same 16x16 arrow
 * (graphic_788 down, graphic_773 up) sits on the left of every closed dropdown
 * and at both ends of every scrollbar, and ~script31 builds the bar 16 wide to
 * hold it. Scaled here, so a 3x chrome gets a 48px bar with the arrow blown up
 * to fill it rather than a 16px sprite marooned in a 48px column.
 */
#define DBG_SCROLL_W DBG_PX(TORIRS_CHROME_M_SCROLL_W)
/**
 * The arrow on a closed dropdown is sized to its ROW, not to the scrollbar.
 *
 * It used to be DBG_SCROLL_W, on the strength of it being the same sprite --
 * but the sprite is stretched into whatever box it is given, and a 16-wide
 * arrow inside an 18-tall field box leaves one pixel of frame above and below
 * it. Sized from the row it lives in, it sits inside the box's inset the way
 * script_3850 places it.
 */
#define DBG_DROP_ARROW_W DBG_PX(TORIRS_CHROME_M_DROP_ARROW)
/** The scrollbar grip's two fixed caps (~script31: `cc_setsize(0, 5, 1, 0)`). */
#define DBG_SCROLL_CAP_H DBG_PX(TORIRS_CHROME_M_SCROLL_CAP_H)
/** Shortest the grip may get, caps included (~script31: `if ($height9 < 10)`). */
#define DBG_SCROLL_GRIP_MIN DBG_PX(TORIRS_CHROME_M_SCROLL_GRIP_MIN)
/** Air around the open list's rows (script_9114 sizes it `$int26 + 4`). */
#define DBG_DROP_LIST_PAD DBG_PX(TORIRS_CHROME_M_DROP_LIST_PAD)
/** Row pitch inside the open dropdown list.
 *
 *  The reference's rows are 20 tall around a 14px line. Written as the line
 *  box plus its air rather than as 20 so a re-bake reflows the list instead of
 *  cropping it, which is the same rule every other metric here follows -- the
 *  authored TORIRS_CHROME_M_DROP_LIST_ROW_H is what this comes out at for the
 *  p12 the rows are set in, and is there for the presentations that cannot
 *  measure a face at all. */
#define DBG_DROP_ROW_H (ToriRSChrome_FontLineBox(ui->theme.font_row, ui->scale) + DBG_PX(4))
/* A separator is an ordinary row with a rule down its middle, so it needs no
 * height of its own -- see the SEPARATOR case in dbg_build_window. */

/* ---- colour picker -------------------------------------------------------
 *
 * The swatch is INSIDE the field box, at its left, so a colour row is the same
 * control as a text row with a sample in front of it -- the reference's own
 * settings fields all wear one box, and giving the colour rows a second one
 * beside it would make the column ragged for the sake of one widget kind.
 */
/** Side of the swatch square inside the field box. */
#define DBG_SWATCH DBG_PX(TORIRS_CHROME_M_SWATCH)
/** Air between the swatch and the hex it labels. */
#define DBG_SWATCH_GAP DBG_PX(TORIRS_CHROME_M_SWATCH_GAP)
/** Height of one axis bar in the popup. */
#define DBG_COLORBAR_H DBG_PX(TORIRS_CHROME_M_COLORBAR_H)
/** Air above, below and between the bars. */
#define DBG_COLORBAR_GAP DBG_PX(TORIRS_CHROME_M_COLORBAR_GAP)
/** Width of the marker drawn over the chosen cell of a bar. */
#define DBG_COLORBAR_MARK DBG_PX(3)
/** The popup's own padding, matching the dropdown list's. */
#define DBG_COLORPOP_PAD DBG_DROP_LIST_PAD
/** Popup width. Wide enough that the 128-step lightness bar gets more than one
 *  pixel per value at 1x, which is what makes a sweep along it read as
 *  continuous rather than notched. */
#define DBG_COLORPOP_W DBG_PX(160)
/** Air either side of a button's caption, when one is measured rather than
 *  centred in the label column (the open dropdown list's rows). */
#define DBG_BUTTON_PAD_X DBG_PX(8)
/** Air around a tab's caption. The vertical pad is the tab's whole height over
 *  the line box; there is no bottom border on the selected tab, which is what
 *  joins it to the content below (see dbg_push_tabstrip). */
#define DBG_TAB_PAD_X DBG_PX(TORIRS_CHROME_M_TAB_PAD_X)
/** A tab's own height, so a strip is the same 20 everywhere rather than
 *  whatever the row face happens to measure. */
#define DBG_TAB_H DBG_PX(TORIRS_CHROME_M_TAB_H)
/** Thickness of the optional nine-slice panel frame's RAIL -- what the content
 *  column is inset by. See dbg_push_frame; the corners are wider. */
#define DBG_FRAME DBG_PX(TORIRS_CHROME_M_FRAME)
/** Side of one frame corner tile. It carries this much rail along each of its
 *  two outer edges, so it is blitted square rather than at the rail's width. */
#define DBG_FRAME_CORNER DBG_PX(TORIRS_CHROME_M_FRAME_CORNER)
/** Air between the close button and the panel's inner edge. */
#define DBG_CLOSE_PAD DBG_PX(TORIRS_CHROME_M_CLOSE_PAD)
/** Narrowest a compressed tab may get before its caption is simply clipped. */
#define DBG_TAB_MIN_W DBG_PX(12)
/** Narrowest a panel may be, before borders. */
#define DBG_MIN_CONTENT_W DBG_PX(16)
/** Edge of one dot in a resize grip's caret. */
#define DBG_GRIP_DOT DBG_PX(2)
/** Centre-to-centre step between grip dots, along both axes. */
#define DBG_GRIP_PITCH DBG_PX(3)
/** Carets in a grip: three nested chevrons, the shortest in the corner. A
 *  COUNT, not a length -- a 2x grip is the same three chevrons drawn twice the
 *  size, not six of them. */
#define DBG_GRIP_LINES 3
/** The grip's drawn extent, both axes. */
#define DBG_GRIP_SIZE ((DBG_GRIP_LINES - 1) * DBG_GRIP_PITCH + DBG_GRIP_DOT)
/** The grip's grab box: the drawn triangle plus a pixel of slop each way. */
#define DBG_GRIP_HIT (DBG_GRIP_SIZE + DBG_PX(2))
/** Narrowest a panel may be dragged. Below this the grip and the title bar
 *  would be fighting over the same pixels. */
#define DBG_MIN_PANEL_W (DBG_GRIP_HIT + 2 * DBG_PAD_X + DBG_PX(2))
/** Shortest a panel may be dragged: its header block, a pad, and the strip the
 *  grip lives in. Anything less is a panel with no room to grab. */
#define DBG_MIN_PANEL_H                                                                            \
    (ToriRSChrome_FontLineBox(TORIRS_CHROME_FONT_MENU, ui->scale) + DBG_PX(3) + DBG_PAD_Y +        \
     DBG_GRIP_HIT + DBG_RULE)

struct ToriRSChromeTheme const torirs_chrome_theme_default = {
    .panel_body = 0x232323,
    .panel_border = 0x6E6E60,
    .panel_title_bg = 0x000000,
    .panel_title_text = 0xC8C8C8,
    .text = 0xDCDCDC,
    .text_dim = 0x8C8C8C,
    .accent = 0xFFFF00,
    .input_bg = 0x101010,
    .input_border = 0x5A5A50,
    .input_border_focus = 0xFFFF00,
    .input_text = 0xFFFFFF,
    /* A shade off the flat panel body, for the same reason the osrs theme's is
     * a shade off the brown: a multiline box at input_bg reads as a hole. */
    .textarea_bg = 0x1A1A1A,
    .check_box = 0x9A9A8C,
    .check_mark = 0x50FF50,
    /* The minimenu's own palette (UITREE_MINIMENU_COLOR_BODY and the black
     * chrome strips emit_minimenu draws), so a debug menu and a game minimenu
     * on screen together read as the same widget. */
    .menu_body = 0x5D5447,
    .menu_chrome = 0x000000,
    .menu_text = 0xFFFFFF,
    .menu_hover_text = 0xFFFF00,
    .separator = 0x4B4B4B,
    /* The flat look keeps the dropdown's *structure* -- inset button, veiled
     * rows, a veil under the cursor -- and only drops the reference palette
     * for its own grey one. The bands are near-invisible (250/255) so the
     * zebra reads as a hairline rather than as stripes on a grey panel, and
     * the hover veil is the one that has to be obvious. */
    .dropdown_border = 0x1A1A1A,
    .dropdown_border_inner = 0x5A5A50,
    .dropdown_text = 0xDCDCDC,
    .dropdown_veil = 0x000000,
    .dropdown_band_trans = 250,
    .dropdown_band_trans_alt = 255,
    .dropdown_row_trans_hover = 150,
    .dropdown_hover_trans = 220,
    .scroll_track = 0x23201B,
    .scroll_grip = 0x4D4233,
    .scroll_grip_hi = 0x766654,
    .scroll_grip_lo = 0x332D25,
    .text_shadowed = 0,
    .font_row = TORIRS_CHROME_FONT_SMALL,
    .skin_panel_body = 0,
    .skin_dropdown = 0,
};

/*
 * The reference interface palette.
 *
 * Note what this is NOT: an invented brown. Every constant below already
 * appears in the client -- 0x5D5447 is UITREE_MINIMENU_COLOR_BODY, and the
 * black chrome and 0xFFFF00 hover are what emit_minimenu draws. Picking them
 * from what the game draws rather than from a screenshot is what keeps a panel
 * and a real widget agreeing when they sit next to each other.
 */
struct ToriRSChromeTheme const torirs_chrome_theme_osrs = {
    .panel_body = TORIRS_CHROME_C_BODY,
    /* Black, like every menu and interface edge the game draws. */
    .panel_border = TORIRS_CHROME_C_CHROME,
    .panel_title_bg = TORIRS_CHROME_C_CHROME,
    /* The minimenu's own title colour -- the body brown on the black bar, which
     * is exactly what emit_minimenu draws "Choose Option" in. It used to be the
     * interfaces' heading orange, and a panel titled in orange next to a
     * minimenu titled in brown read as two different widgets. */
    .panel_title_text = TORIRS_CHROME_C_BODY,
    .text = TORIRS_CHROME_C_TEXT,
    /* script_3850 sets an ENABLED setting's label in 0xff981f and a disabled
     * one in 0x9f9f9f -- so the settings page's row labels are orange, not the
     * grey this used. That one colour is most of why a panel here and the real
     * settings page side by side did not read as the same interface. */
    .text_dim = TORIRS_CHROME_C_LABEL,
    .accent = TORIRS_CHROME_C_ACCENT,
    /* Text entry is black-on-black-bordered in the reference, not inset grey. */
    .input_bg = TORIRS_CHROME_C_FIELD_BG,
    .input_border = 0x3E3529,
    .input_border_focus = TORIRS_CHROME_C_ACCENT,
    .input_text = TORIRS_CHROME_C_TEXT,
    /* ~script7210's own fill for the ground-items lists. */
    .textarea_bg = TORIRS_CHROME_C_TEXTAREA_BG,
    .check_box = TORIRS_CHROME_C_CHROME,
    /* The green/red pair the interfaces use for on/off state. */
    .check_mark = TORIRS_CHROME_C_ON,
    .menu_body = TORIRS_CHROME_C_BODY,
    .menu_chrome = TORIRS_CHROME_C_CHROME,
    .menu_text = TORIRS_CHROME_C_TEXT,
    .menu_hover_text = TORIRS_CHROME_C_ACCENT,
    .separator = TORIRS_CHROME_C_CHROME,
    /* script_3850, verbatim: the closed button is graphic_297 tiled, framed in
     * 0x0e0e0c with a 0x474745 inset, its value set in 0xff981f. */
    .dropdown_border = TORIRS_CHROME_C_FRAME,
    .dropdown_border_inner = TORIRS_CHROME_C_FRAME_INSET,
    .dropdown_text = TORIRS_CHROME_C_LABEL,
    /* script_9114, verbatim: black bands at 220 and 200, 240 under the
     * cursor. The hovered row is the LEAST veiled one, so the highlight is
     * the tiled body showing through rather than a colour painted on. */
    .dropdown_veil = TORIRS_CHROME_C_CHROME,
    .dropdown_band_trans = 220,
    .dropdown_band_trans_alt = 200,
    .dropdown_row_trans_hover = 240,
    .dropdown_hover_trans = 220,
    .scroll_track = TORIRS_CHROME_C_SCROLL_TRACK,
    .scroll_grip = TORIRS_CHROME_C_SCROLL_GRIP,
    .scroll_grip_hi = TORIRS_CHROME_C_SCROLL_GRIP_HI,
    .scroll_grip_lo = TORIRS_CHROME_C_SCROLL_GRIP_LO,
    .text_shadowed = 1,
    /* p12: the face the reference sets interface body text in. Rows grow from
     * the 12px debug box to the game's 16px one, which is the point -- this
     * chrome is meant to sit beside real widgets, not below them. */
    .font_row = TORIRS_CHROME_FONT_BODY,
    .skin_panel_body = 1,
    .skin_dropdown = 1,
};

/*
 * The minimenu's geometry, recomputed here rather than included.
 *
 * This is UIMinimenu_LayoutFromLineBox (src/ui/uitree_minimenu.c) verbatim.
 * It is duplicated because including uitree_minimenu.h would drag uitree.h and
 * the whole ui/ layer into a module whose entire point is that it has no
 * dependencies. The duplication is pinned by a test that asserts the two agree
 * for every line box (uitree_test_debug_overlay.c), so it cannot drift
 * silently — if that test fails, this block is the one that is wrong.
 */
struct DbgMenuLayout
{
    int line_height;
    int row_stride;
    int header_bar_h;
    int separator_y;
    int option_base_y;
    int chrome_h;
    int hover_above;
    int hover_below;
    int width_pad;
    int border_inset;
};

static struct DbgMenuLayout
dbg_menu_layout(int line_box, int scale)
{
    int const box = line_box > 0 ? line_box : ToriRSChromeFont_Menu_LINE_BOX * scale;
    struct DbgMenuLayout l;
    /* `box` arrives already scaled (it is a scaled font's line box); the
     * constants around it are chrome pixels and are scaled here, so the whole
     * layout is the 1x layout multiplied. At scale 1 this is
     * UIMinimenu_LayoutFromLineBox verbatim, which is what the pin test in
     * uitree_test_debug_overlay.c compares against. */
    l.line_height = box - 2 * scale;
    l.row_stride = box - scale;
    l.header_bar_h = box;
    l.separator_y = box + 2 * scale;
    l.option_base_y = 2 * box - scale;
    l.chrome_h = box + 5 * scale;
    l.hover_above = box - 3 * scale;
    l.hover_below = 3 * scale;
    l.width_pad = 8 * scale;
    l.border_inset = box + 3 * scale;
    return l;
}

/* ---- HSL16 colour --------------------------------------------------------
 *
 * The revision's own palette, recomputed here rather than read out of the
 * rasteriser's g_hsl16_to_rgb_table.
 *
 * This module links no renderer (see the header's dependency note), and that
 * is not an obstacle to route around -- it is what makes a swatch drawable in
 * a test with no scene, in a build whose palette has not been initialised, and
 * on the frame before any cache has opened. So the arithmetic is ported
 * instead: pix3d_init_palette at brightness 0.8, constant for constant, with
 * the one pow() it needs replaced by the ramp below.
 *
 * A SECOND IMPLEMENTATION OF A CONVERSION IS A LIABILITY unless something
 * checks it, so something does: test-debug-overlay-visual links both this and
 * toridraw and asserts they agree on all 32768 entries. That test is also what
 * will say so if the client ever changes its palette brightness -- the failure
 * names the drift instead of leaving the chrome a shade off for a year.
 */

/** pow(i / 256.0, 0.8) * 256, truncated -- pix3d_set_gamma's per-channel step
 *  at the brightness the client builds its palette with. Baked because the
 *  alternative is <math.h> and a libm link in a module that has neither. */
static const short DBG_HSL_GAMMA80[256] = {
      0,  3,  5,  7,  9, 10, 12, 14, 15, 17, 19, 20, 22, 23, 25, 26,
     27, 29, 30, 31, 33, 34, 35, 37, 38, 39, 41, 42, 43, 44, 46, 47,
     48, 49, 50, 52, 53, 54, 55, 56, 57, 59, 60, 61, 62, 63, 64, 65,
     67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 78, 79, 80, 81, 82, 83,
     84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
    100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,
    116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,
    132,133,134,134,135,136,137,138,139,140,141,142,143,144,145,146,
    147,147,148,149,150,151,152,153,154,155,156,157,157,158,159,160,
    161,162,163,164,165,166,166,167,168,169,170,171,172,173,174,174,
    175,176,177,178,179,180,181,181,182,183,184,185,186,187,187,188,
    189,190,191,192,193,193,194,195,196,197,198,199,199,200,201,202,
    203,204,205,205,206,207,208,209,210,210,211,212,213,214,215,215,
    216,217,218,219,220,220,221,222,223,224,225,225,226,227,228,229,
    230,230,231,232,233,234,234,235,236,237,238,239,239,240,241,242,
    243,243,244,245,246,247,247,248,249,250,251,251,252,253,254,255,
};

/** One channel of the HSL -> RGB sector walk the reference spells three times
 *  over, once per channel, with a rotated hue. */
static double
dbg_hsl_channel(double t, double p, double q)
{
    if( t > 1.0 )
        t -= 1.0;
    if( t < 0.0 )
        t += 1.0;
    if( t * 6.0 < 1.0 )
        return p + (q - p) * 6.0 * t;
    if( t * 2.0 < 1.0 )
        return q;
    if( t * 3.0 < 2.0 )
        return p + (q - p) * (0.6666666666666666 - t) * 6.0;
    return p;
}

static int
dbg_gamma80(double channel)
{
    int i = (int)(channel * 256.0);
    if( i < 0 )
        i = 0;
    if( i > 255 )
        i = 255;
    return DBG_HSL_GAMMA80[i];
}

uint32_t
ToriRSChrome_Hsl16ToRgb(int hsl16)
{
    /* The palette is indexed exactly as pix3d_init_palette fills it: the outer
     * loop's `y` is the top nine bits (hue and saturation together) and the
     * inner loop's `x` is the seven lightness bits. */
    int const packed = hsl16 & 0xFFFF;
    int const y = packed >> 7;
    double const hue = (double)(y / 8) / 64.0 + 0.0078125;
    double const saturation = (double)(y & 0x7) / 8.0 + 0.0625;
    double const lightness = (double)(packed & 0x7F) / 128.0;
    double r = lightness;
    double g = lightness;
    double b = lightness;

    /* `saturation` is never 0 here -- the +0.0625 floor sees to that -- but the
     * branch is the reference's and is kept so the two read as the same
     * function rather than as one that happens to agree. */
    if( saturation != 0.0 )
    {
        double const q = lightness < 0.5 ? lightness * (saturation + 1.0)
                                         : lightness + saturation - lightness * saturation;
        double const p = lightness * 2.0 - q;
        r = dbg_hsl_channel(hue + 0.3333333333333333, p, q);
        g = dbg_hsl_channel(hue, p, q);
        b = dbg_hsl_channel(hue - 0.3333333333333333, p, q);
    }

    return ((uint32_t)dbg_gamma80(r) << 16) | ((uint32_t)dbg_gamma80(g) << 8) |
           (uint32_t)dbg_gamma80(b);
}

void
ToriRSChrome_Hsl16Split(int hsl16, int* hue, int* sat, int* lum)
{
    int const packed = hsl16 & 0xFFFF;
    if( hue )
        *hue = (packed >> 10) & 0x3F;
    if( sat )
        *sat = (packed >> 7) & 0x7;
    if( lum )
        *lum = packed & 0x7F;
}

int
ToriRSChrome_Hsl16Pack(int hue, int sat, int lum)
{
    if( hue < 0 )
        hue = 0;
    if( hue > TORIRS_CHROME_COLOR_HUE_STEPS - 1 )
        hue = TORIRS_CHROME_COLOR_HUE_STEPS - 1;
    if( sat < 0 )
        sat = 0;
    if( sat > TORIRS_CHROME_COLOR_SAT_STEPS - 1 )
        sat = TORIRS_CHROME_COLOR_SAT_STEPS - 1;
    if( lum < 0 )
        lum = 0;
    if( lum > TORIRS_CHROME_COLOR_LUM_STEPS - 1 )
        lum = TORIRS_CHROME_COLOR_LUM_STEPS - 1;
    return (hue << 10) | (sat << 7) | lum;
}

/** ceil() for a non-negative double, so this file needs no <math.h>. */
static int
dbg_ceil_nonneg(double v)
{
    int const truncated = (int)v;
    return (double)truncated < v ? truncated + 1 : truncated;
}

int
ToriRSChrome_Hsl16FromRgb(uint32_t rgb)
{
    /*
     * A port of the reference's rgbToHSL at brightness 1.0, which is what makes
     * its pow(channel, 1/brightness) an identity and lets the gamma step drop
     * out. Written out rather than approximated because the QUANTISATION is the
     * point: the ceilings and the `% 63` are what land a chosen colour on the
     * same palette entry the game's own art uses, and a "close enough"
     * conversion gives a beam that is visibly the wrong hue.
     *
     * The same conversion the plugin api exposes as hsl_from_rgb, and now the
     * only copy of it -- app_plugin_hsl_from_rgb calls through to here.
     */
    double const r = (double)((rgb >> 16) & 0xFF) / 255.0;
    double const g = (double)((rgb >> 8) & 0xFF) / 255.0;
    double const b = (double)(rgb & 0xFF) / 255.0;
    double max = r > g ? r : g;
    double min = r < g ? r : g;
    double hue = 0.0;
    double span;
    double sat;
    double lum;

    if( b > max )
        max = b;
    if( b < min )
        min = b;
    span = max - min;

    /* Hue as Color.RGBtoHSB computes it: 0 on a grey, where the sector is
     * undefined rather than zero by accident. */
    if( span > 0.0 )
    {
        double const rc = (max - r) / span;
        double const gc = (max - g) / span;
        double const bc = (max - b) / span;
        if( r >= max )
            hue = bc - gc;
        else if( g >= max )
            hue = 2.0 + rc - bc;
        else
            hue = 4.0 + gc - rc;
        hue /= 6.0;
        if( hue < 0.0 )
            hue += 1.0;
    }

    /* HSB -> HSL. `max` is the brightness and `span / max` the HSB saturation;
     * the reference recovers luminance from them and re-derives an HSL
     * saturation against it. */
    {
        double const brightness = max;
        double const hsb_sat = max > 0.0 ? span / max : 0.0;
        double const other = 1.0 - (brightness - (brightness * hsb_sat) / 2.0);
        lum = brightness - (brightness * hsb_sat) / 2.0;
        sat = (lum > 0.0 && lum < 1.0) ? (brightness - lum) / (lum < other ? lum : other) : 0.0;
    }

    {
        int h = dbg_ceil_nonneg(hue * 64.0) % 63;
        int const s = dbg_ceil_nonneg(sat * 7.0);
        int const l = dbg_ceil_nonneg(lum * 127.0);
        if( h < 0 )
            h = 0;
        return ToriRSChrome_Hsl16Pack(h, s, l);
    }
}

int
ToriRSChrome_Hsl16NearestRgb(uint32_t rgb)
{
    int const want_r = (int)((rgb >> 16) & 0xFF);
    int const want_g = (int)((rgb >> 8) & 0xFF);
    int const want_b = (int)(rgb & 0xFF);
    int best = 0;
    long best_d = -1;

    /*
     * The whole palette, in sum-of-squares. Exact rather than clever, because
     * the axes are not independent under the palette's own gamma and a
     * per-axis guess lands a shade off in exactly the cases a picker is used
     * for -- and because "closest" is the only definition of this that keeps
     * the round trip still.
     *
     * The scan runs over all 65536 packed values rather than the 32768
     * distinct ones, since hue and saturation share the top nine bits and the
     * duplicates cost nothing but iterations. First match wins, so a colour
     * with several exact representatives always resolves to the same one --
     * which is the other half of stability.
     */
    for( int hsl = 0; hsl < 65536; hsl++ )
    {
        uint32_t const c = ToriRSChrome_Hsl16ToRgb(hsl);
        long const dr = (long)((c >> 16) & 0xFF) - want_r;
        long const dg = (long)((c >> 8) & 0xFF) - want_g;
        long const db = (long)(c & 0xFF) - want_b;
        long const d = dr * dr + dg * dg + db * db;
        if( best_d >= 0 && d >= best_d )
            continue;
        best_d = d;
        best = hsl;
        if( d == 0 )
            break;
    }
    return best;
}

static int
dbg_hex_digit(char c)
{
    if( c >= '0' && c <= '9' )
        return c - '0';
    if( c >= 'a' && c <= 'f' )
        return c - 'a' + 10;
    if( c >= 'A' && c <= 'F' )
        return c - 'A' + 10;
    return -1;
}

int
ToriRSChrome_ParseHexRgb(char const* text, uint32_t* out)
{
    uint32_t value = 0;

    assert(out);
    if( !text )
        return 0;
    while( *text == ' ' || *text == '\t' )
        text++;
    if( *text == '#' )
        text++;
    else if( text[0] == '0' && (text[1] == 'x' || text[1] == 'X') )
        text += 2;

    for( int i = 0; i < 6; i++ )
    {
        int const digit = dbg_hex_digit(text[i]);
        if( digit < 0 )
            return 0;
        value = (value << 4) | (uint32_t)digit;
    }
    /* Exactly six digits: a seventh means the user typed something this is not,
     * and truncating it would commit a colour they did not write. */
    if( dbg_hex_digit(text[6]) >= 0 )
        return 0;

    *out = value;
    return 1;
}

/* ---- text metrics -------------------------------------------------------- */

/*
 * The (slot, scale) pair, resolved against the bake.
 *
 * Nine tables, not three multiplied by a factor: an advance is the pen step of
 * a glyph that was BAKED at this size, so reading the 2x table is the only way
 * to measure what the 2x renderer will actually draw. The bake happens to make
 * the two agree exactly (fontbake scales every metric by the same integer),
 * and that is a property worth having rather than one worth assuming -- the
 * scaled-metrics test asserts it.
 */
static int const*
dbg_advance_table(int font_slot, int scale)
{
    assert(scale >= TORIRS_CHROME_SCALE_MIN);
    assert(scale <= TORIRS_CHROME_SCALE_MAX);
    switch( font_slot )
    {
    case TORIRS_CHROME_FONT_MENU:
        return scale == 3   ? ToriRSChromeFont_Menu3x_advance_px
               : scale == 2 ? ToriRSChromeFont_Menu2x_advance_px
                            : ToriRSChromeFont_Menu_advance_px;
    case TORIRS_CHROME_FONT_BODY:
        return scale == 3   ? ToriRSChromeFont_Body3x_advance_px
               : scale == 2 ? ToriRSChromeFont_Body2x_advance_px
                            : ToriRSChromeFont_Body_advance_px;
    default:
        return scale == 3   ? ToriRSChromeFont_Small3x_advance_px
               : scale == 2 ? ToriRSChromeFont_Small2x_advance_px
                            : ToriRSChromeFont_Small_advance_px;
    }
}

int
ToriRSChrome_FontLineHeight(int font_slot, int scale)
{
    assert(scale >= TORIRS_CHROME_SCALE_MIN);
    assert(scale <= TORIRS_CHROME_SCALE_MAX);
    switch( font_slot )
    {
    case TORIRS_CHROME_FONT_MENU:
        return scale == 3   ? ToriRSChromeFont_Menu3x_LINE_HEIGHT
               : scale == 2 ? ToriRSChromeFont_Menu2x_LINE_HEIGHT
                            : ToriRSChromeFont_Menu_LINE_HEIGHT;
    case TORIRS_CHROME_FONT_BODY:
        return scale == 3   ? ToriRSChromeFont_Body3x_LINE_HEIGHT
               : scale == 2 ? ToriRSChromeFont_Body2x_LINE_HEIGHT
                            : ToriRSChromeFont_Body_LINE_HEIGHT;
    default:
        return scale == 3   ? ToriRSChromeFont_Small3x_LINE_HEIGHT
               : scale == 2 ? ToriRSChromeFont_Small2x_LINE_HEIGHT
                            : ToriRSChromeFont_Small_LINE_HEIGHT;
    }
}

int
ToriRSChrome_FontLineBox(int font_slot, int scale)
{
    assert(scale >= TORIRS_CHROME_SCALE_MIN);
    assert(scale <= TORIRS_CHROME_SCALE_MAX);
    switch( font_slot )
    {
    case TORIRS_CHROME_FONT_MENU:
        return scale == 3   ? ToriRSChromeFont_Menu3x_LINE_BOX
               : scale == 2 ? ToriRSChromeFont_Menu2x_LINE_BOX
                            : ToriRSChromeFont_Menu_LINE_BOX;
    case TORIRS_CHROME_FONT_BODY:
        return scale == 3   ? ToriRSChromeFont_Body3x_LINE_BOX
               : scale == 2 ? ToriRSChromeFont_Body2x_LINE_BOX
                            : ToriRSChromeFont_Body_LINE_BOX;
    default:
        return scale == 3   ? ToriRSChromeFont_Small3x_LINE_BOX
               : scale == 2 ? ToriRSChromeFont_Small2x_LINE_BOX
                            : ToriRSChromeFont_Small_LINE_BOX;
    }
}

int
ToriRSChrome_MeasureText(int font_slot, int scale, char const* text)
{
    int const* adv = dbg_advance_table(font_slot, scale);
    int w = 0;
    unsigned char const* p;

    assert(text);
    for( p = (unsigned char const*)text; *p; p++ )
        w += adv[*p];
    return w;
}

/** Width of the first `len` bytes — what the caret position is measured from. */
static int
dbg_measure_prefix(int font_slot, int scale, char const* text, int len)
{
    int const* adv = dbg_advance_table(font_slot, scale);
    int w = 0;

    for( int i = 0; i < len && text[i]; i++ )
        w += adv[(unsigned char)text[i]];
    return w;
}

int
ToriRSChrome_WrapText(
    int font_slot,
    int scale,
    char const* text,
    int width,
    int* out_start,
    int* out_len,
    int max_lines)
{
    int const* adv = dbg_advance_table(font_slot, scale);
    int lines = 0;
    int i = 0;
    int done = 0;

    assert(text);
    assert(max_lines > 0);

    while( !done && lines < max_lines )
    {
        int const start = i;
        /* Byte after the last space that still fit, so a break lands BETWEEN
         * words. -1 while the line has held no space yet, which is what makes
         * a single word wider than the box break where it runs out instead of
         * backing up to the start of the line and never advancing. */
        int space = -1;
        int wrapped = 0;
        int px = 0;
        int end;

        while( text[i] && text[i] != '\n' )
        {
            int const a = adv[(unsigned char)text[i]];
            /* `i > start` so a box narrower than one glyph still consumes
             * one per line rather than looping forever on the same byte. */
            if( width > 0 && px + a > width && i > start )
            {
                wrapped = 1;
                break;
            }
            px += a;
            if( text[i] == ' ' )
                space = i + 1;
            i++;
        }
        end = i;
        if( wrapped && space > start && space < i )
        {
            /* The trailing space stays on the line it ended -- it is invisible
             * where it is drawn, and keeping the slices CONTIGUOUS is what
             * lets a caret offset be turned back into a line and a column. */
            end = space;
            i = space;
        }
        if( out_start )
            out_start[lines] = start;
        if( out_len )
            out_len[lines] = end - start;
        lines++;
        if( wrapped )
            continue;
        if( text[i] == '\n' )
            i++; /* ...and the empty line after a trailing one still counts. */
        else
            done = 1;
    }
    return lines;
}

/* ---- small helpers ------------------------------------------------------- */

/* Defined with the dropdown machinery below; needed earlier by SetHidden. */
static void
dbg_dropdown_close(struct ToriRSChrome* ui);

/*
 * THE one answer to "where does the box start" -- widths, draws, hit tests and
 * the dropdown popup all ask dbg_row_box_offset / dbg_row_box_top, which is
 * what keeps a popup opening under the box it belongs to rather than under
 * where the box would have been.
 */

/**
 * Is this row's caption too long for the label column, so that it takes a line
 * of its own above the control?
 *
 * The column is 104 pixels and a settings panel is 320: a caption that does
 * not fit used to be drawn from the row's left edge anyway, straight under the
 * field box that is painted after it. The visible result is a name with its
 * second half missing -- not truncated with the box beside it, but sliced off
 * BY the box, which reads as a rendering fault rather than as a long name.
 *
 * Clipping it to the column instead would only make the slice tidy; the name
 * would still be unreadable, and a settings row whose name you cannot read is
 * not a settings row. So a long caption gets the whole row width and the
 * control goes underneath it, which is the shape a multiline field already
 * has (@see TORIRS_CHROME_W_TEXTAREA) and the shape the reference's own wider
 * settings rows have.
 *
 * Only the three kinds that put a BOX in the label row's right-hand column
 * answer yes. A checkbox or a list row draws its name in the space the control
 * does not use and has no column to overrun.
 */
static int
dbg_row_label_stacked(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    if( !w->label[0] )
        return 0;
    if( w->kind != TORIRS_CHROME_W_TEXTINPUT && w->kind != TORIRS_CHROME_W_DROPDOWN &&
        w->kind != TORIRS_CHROME_W_COLORPICK )
        return 0;
    return ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->label) +
               DBG_ROW_NAME_GAP >
           DBG_LABEL_W;
}

/**
 * Offset from a labelled row's left edge to its control box.
 *
 * Zero for a STACKED row, whose caption is not beside the box at all.
 */
static int
dbg_row_box_offset(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    /*
     * A FIXED column, not one measured from the labels in the panel.
     *
     * Measured was the old behaviour and it moves every field in the panel
     * sideways the moment one label changes -- a plugin renaming a setting
     * reflows the whole page. Fixed is also what the other presentations lay
     * out, so a panel keeps its shape across a change of executor.
     *
     * An UNLABELLED row gets no column at all rather than an empty one: there
     * is nothing to line it up with, and reserving 104 pixels for a caption
     * that will never be drawn is how a lone dropdown in a 210-wide panel ends
     * up half the width of the panel holding it.
     */
    if( dbg_row_label_stacked(ui, w) )
        return 0;
    return w->label[0] ? DBG_LABEL_W : 0;
}

/** Offset from a row's top to its control box. A stacked row spends its first
 *  line on the caption; every other row puts the box at the top. */
static int
dbg_row_box_top(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    return dbg_row_label_stacked(ui, w) ? DBG_ROW_H : 0;
}

/**
 * Natural width of a row that puts a box in the label row, given the box's own
 * want.
 *
 * Stacked, the two are on separate lines and the row wants whichever is wider
 * -- asking for caption PLUS box would demand a panel twice as wide as
 * anything on the row, which is how a stacked row would end up widening the
 * window it was introduced to fit inside.
 */
static int
dbg_row_stacked_width(
    struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w, int box_w)
{
    if( !dbg_row_label_stacked(ui, w) )
        return dbg_row_box_offset(ui, w) + box_w;

    int const label_w = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->label);
    return label_w > box_w ? label_w : box_w;
}

/* ---- a multiline field's geometry ----------------------------------------
 *
 * ONE arithmetic for the box, the strip the glyphs go in, and where a byte
 * offset lands among them -- asked by the layout, the draw, the hit test and
 * the key handler alike. Two of those computing it separately is a caret drawn
 * where the user did not click, which is the failure a shared helper prevents
 * here for the same reason dbg_row_box_offset prevents it for a one-line row.
 */

/** Visible lines of a multiline field, clamped. @see ToriRSChrome_TextArea. */
static int
dbg_textarea_rows(struct ToriRSChromeWidget const* w)
{
    int rows = w->rows > 0 ? w->rows : TORIRS_CHROME_M_TEXTAREA_ROWS;
    if( rows > TORIRS_CHROME_M_TEXTAREA_ROWS_MAX )
        rows = TORIRS_CHROME_M_TEXTAREA_ROWS_MAX;
    return rows;
}

/** Pitch of one wrapped line: the row face's own line box, so a re-bake at
 *  another size reflows instead of overlapping. */
static int
dbg_textarea_line_h(struct ToriRSChrome const* ui)
{
    return ToriRSChrome_FontLineBox(ui->theme.font_row, ui->scale);
}

/** Height of the field box alone, without the caption line above it. */
static int
dbg_textarea_box_h(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    return dbg_textarea_rows(w) * dbg_textarea_line_h(ui) + 2 * DBG_TEXTAREA_PAD_Y +
           2 * DBG_RULE;
}

/** The field box, from the row box the layout resolved onto the widget. */
static struct ToriRSChromeRect
dbg_textarea_box(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    struct ToriRSChromeRect box;
    /* The caption is a line of its own ABOVE the box -- the reference's own
     * shape, and the reason a multiline row is not laid out in the label
     * column. @see TORIRS_CHROME_W_TEXTAREA. */
    box.x = w->x;
    box.y = w->y + (w->label[0] ? DBG_ROW_H : 0);
    box.w = w->w;
    box.h = dbg_textarea_box_h(ui, w);
    return box;
}

/** Where the glyphs go inside that box: inside the frame, inside the padding. */
static struct ToriRSChromeRect
dbg_textarea_inner(struct ToriRSChrome const* ui, struct ToriRSChromeRect box)
{
    struct ToriRSChromeRect in;
    in.x = box.x + DBG_RULE + DBG_INPUT_PAD_X;
    in.y = box.y + DBG_RULE + DBG_TEXTAREA_PAD_Y;
    in.w = box.w - 2 * (DBG_RULE + DBG_INPUT_PAD_X);
    in.h = box.h - 2 * (DBG_RULE + DBG_TEXTAREA_PAD_Y);
    if( in.w < 0 )
        in.w = 0;
    if( in.h < 0 )
        in.h = 0;
    return in;
}

/** Break this widget's value at its own box width. @return the line count. */
static int
dbg_textarea_wrap(
    struct ToriRSChrome const* ui,
    struct ToriRSChromeWidget const* w,
    int* starts,
    int* lens)
{
    struct ToriRSChromeRect const in = dbg_textarea_inner(ui, dbg_textarea_box(ui, w));
    return ToriRSChrome_WrapText(
        ui->theme.font_row, ui->scale, w->text, in.w, starts, lens,
        DBG_TEXTAREA_LINES_MAX);
}

/**
 * Which display line a byte offset is on.
 *
 * The LAST line that contains it, and that is the interesting half: a soft
 * wrap leaves one offset on two lines -- the end of the line it broke and the
 * start of the next -- and putting the caret on the later of them is what makes
 * typing at a wrap appear where the glyph will land. A HARD newline has no such
 * ambiguity, because the '\n' itself takes an offset that belongs to neither.
 */
static int
dbg_textarea_line_of(int const* starts, int const* lens, int count, int caret)
{
    int line = 0;
    for( int i = 0; i < count; i++ )
        if( caret >= starts[i] && caret <= starts[i] + lens[i] )
            line = i;
    return line;
}

/** Pixel width of `line`'s first `col` bytes -- where the caret is drawn. */
static int
dbg_textarea_col_px(
    struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w, int start, int col)
{
    return dbg_measure_prefix(ui->theme.font_row, ui->scale, w->text + start, col);
}

/** The column a click at `x_off` pixels into a line lands on. Rounds to the
 *  NEAREST gap between glyphs, which is where a caret is expected to go. */
static int
dbg_textarea_col_at(
    struct ToriRSChrome const* ui,
    struct ToriRSChromeWidget const* w,
    int start,
    int len,
    int x_off)
{
    int const* adv = dbg_advance_table(ui->theme.font_row, ui->scale);
    int px = 0;
    int col = 0;

    while( col < len )
    {
        int const a = adv[(unsigned char)w->text[start + col]];
        if( px + a / 2 > x_off )
            break;
        px += a;
        col++;
    }
    return col;
}

/**
 * Bring the caret's line into view, and clamp the offset to the content.
 *
 * Run after every edit rather than at the draw, because the HIT TEST reads
 * `scroll` too: a click that lands on the line the draw showed has to address
 * the line the model thinks is there, and a scroll fixed up at draw time is one
 * frame behind every click.
 */
static void
dbg_textarea_scroll_to_caret(struct ToriRSChrome const* ui, struct ToriRSChromeWidget* w)
{
    int starts[DBG_TEXTAREA_LINES_MAX];
    int lens[DBG_TEXTAREA_LINES_MAX];
    int const count = dbg_textarea_wrap(ui, w, starts, lens);
    int const rows = dbg_textarea_rows(w);
    int const line = dbg_textarea_line_of(starts, lens, count, w->caret);

    if( w->scroll > line )
        w->scroll = line;
    if( w->scroll < line - rows + 1 )
        w->scroll = line - rows + 1;
    if( w->scroll > count - rows )
        w->scroll = count - rows;
    if( w->scroll < 0 )
        w->scroll = 0;
}


/** Is (x, y) inside `r`? The rect-taking form; dbg_point_in takes four ints. */
static int
dbg_point_in_rect(int x, int y, struct ToriRSChromeRect r)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static int
dbg_valid_panel(struct ToriRSChrome const* ui, int panel)
{
    return ui && panel >= 0 && panel < ui->panel_count;
}

static int
dbg_valid_widget(struct ToriRSChrome const* ui, int widget)
{
    return ui && widget >= 0 && widget < ui->widget_count &&
           ui->widgets[widget].kind != TORIRS_CHROME_W_FREE;
}

/**
 * Does this row take part in layout, drawing and hit testing?
 *
 * The one answer, asked by the measuring pass, the drawing pass and the hit
 * test alike. Skipping in only one of them is the bug this exists to prevent:
 * a row measured but not drawn leaves a hole, and a row drawn but not measured
 * runs off the bottom border -- both of which this file has had before, which
 * is why the comment at the drawing site says so.
 */
static int
dbg_widget_shown(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    if( w->hidden || w->kind == TORIRS_CHROME_W_FREE )
        return 0;
    /* A row belonging to no tab in particular shows on all of them; one with a
     * tab shows only on its own. The strip itself carries -1, so it survives
     * every switch -- a strip that hid itself could not be clicked back. */
    if( w->tab >= 0 && w->tab != ui->panels[w->panel].active_tab )
        return 0;
    return 1;
}

static void
dbg_copy(char* dst, int cap, char const* src)
{
    int i = 0;
    if( src )
        for( ; i < cap - 1 && src[i]; i++ )
            dst[i] = src[i];
    dst[i] = '\0';
}

/** Mark a panel for relayout. Also flags the model so Build stops being a no-op. */
static void
dbg_dirty_panel(struct ToriRSChrome* ui, int panel)
{
    if( !dbg_valid_panel(ui, panel) )
        return;
    ui->panels[panel].dirty = 1;
    ui->dirty = 1;
}

static void
dbg_dirty_widget(struct ToriRSChrome* ui, int widget)
{
    if( dbg_valid_widget(ui, widget) )
        dbg_dirty_panel(ui, ui->widgets[widget].panel);
}

/** Union `r` into the accumulated invalid region. Empty boxes contribute nothing. */
static void
dbg_damage_add(struct ToriRSChrome* ui, struct ToriRSChromeRect r)
{
    int x0;
    int y0;
    int x1;
    int y1;

    if( r.w <= 0 || r.h <= 0 )
        return;
    if( ui->damage.w <= 0 || ui->damage.h <= 0 )
    {
        ui->damage = r;
        return;
    }
    x0 = ui->damage.x < r.x ? ui->damage.x : r.x;
    y0 = ui->damage.y < r.y ? ui->damage.y : r.y;
    x1 = ui->damage.x + ui->damage.w > r.x + r.w ? ui->damage.x + ui->damage.w : r.x + r.w;
    y1 = ui->damage.y + ui->damage.h > r.y + r.h ? ui->damage.y + ui->damage.h : r.y + r.h;
    ui->damage.x = x0;
    ui->damage.y = y0;
    ui->damage.w = x1 - x0;
    ui->damage.h = y1 - y0;
}

/* ---- lifecycle ----------------------------------------------------------- */

void
ToriRSChrome_Init(struct ToriRSChrome* ui)
{
    assert(ui);
    memset(ui, 0, sizeof(*ui));
    /* The reference look is the default: this chrome is the editor's face, and
     * the editor is a tool for this game. A caller that wants the flat debug
     * palette assigns `torirs_chrome_theme_default` over this -- no env read here,
     * because reading one would be this module's first dependency outside the
     * C library and the whole point of it is that it has none. */
    ui->theme = torirs_chrome_theme_osrs;
    /* Native size. Zero (the memset) would multiply every metric to nothing,
     * which draws an empty chrome that looks like a failed boot. */
    ui->scale = 1;
    /* TICK, the settings page's tick/cross: the style every panel in this
     * chrome was authored against. @see enum ToriRSChromeCheckStyle. It is
     * also the memset's value, spelled out because a default nobody can find
     * in the code is a default nobody can change. */
    ui->check_style = TORIRS_CHROME_CHECK_STYLE_TICK;
    ui->focus = -1;
    ui->hover = -1;
    ui->press = -1;
    ui->activated = -1;
    ui->dropdown_open = -1;
    ui->colorpick_open = -1;
    ui->colorpick_drag_bar = TORIRS_CHROME_COLORBAR_NONE;
    ui->drag_panel = -1;
    ui->resize_panel = -1;
    ui->free_widget = -1;
    ui->scroll_panel = -1;
    ui->hover_close_panel = -1;
    ui->caret_visible = 1;
}

void
ToriRSChrome_Reset(struct ToriRSChrome* ui)
{
    assert(ui);
    /* Everything on screen is going away, so all of it is invalid. */
    for( int i = 0; i < ui->panel_count; i++ )
        dbg_damage_add(ui, ui->panels[i].last_rect);
    ui->panel_count = 0;
    ui->dropdown_open = -1;
    ui->dropdown_hover_row = -1;
    ui->dropdown_scroll_drag = 0;
    ui->colorpick_open = -1;
    ui->colorpick_drag_bar = TORIRS_CHROME_COLORBAR_NONE;
    ui->widget_count = 0;
    /* The whole array is gone, so the free list that indexed into it is too --
     * leaving it would hand out slots above the new high-water mark. */
    ui->free_widget = -1;
    ui->prim_count = 0;
    ui->focus = -1;
    ui->hover = -1;
    ui->press = -1;
    ui->activated = -1;
    ui->drag_panel = -1;
    ui->resize_panel = -1;
    ui->scroll_panel = -1;
    ui->hover_close_panel = -1;
    ui->overflow = 0;
    ui->dirty = 1;
}

void
ToriRSChrome_SetScale(struct ToriRSChrome* ui, int scale)
{
    assert(ui);
    assert(scale >= TORIRS_CHROME_SCALE_MIN);
    assert(scale <= TORIRS_CHROME_SCALE_MAX);
    if( ui->scale == scale )
        return;
    /* Every panel is about to change size, so the boxes they occupy NOW are
     * stale as well as the ones they are about to occupy: damage the old
     * bounds before the relayout overwrites them. */
    for( int i = 0; i < ui->panel_count; i++ )
        dbg_damage_add(ui, ui->panels[i].last_rect);
    ui->scale = scale;
    for( int i = 0; i < ui->panel_count; i++ )
        dbg_dirty_panel(ui, i);
    ui->dirty = 1;
}

int
ToriRSChrome_Scale(struct ToriRSChrome const* ui)
{
    assert(ui);
    return ui->scale;
}

int
ToriRSChrome_CheckBoxMetric(int style)
{
    return style == TORIRS_CHROME_CHECK_STYLE_BOX ? TORIRS_CHROME_M_BOX_SQUARE
                                                  : TORIRS_CHROME_M_BOX;
}

void
ToriRSChrome_SetCheckStyle(struct ToriRSChrome* ui, int style)
{
    assert(ui);
    if( ui->check_style == style )
        return;
    /* The same damage-then-dirty order SetScale uses, and for the same reason:
     * the two arts are different widths, so the row a checkbox sits in is a
     * different size afterwards and the box it occupied NOW is stale too. */
    for( int i = 0; i < ui->panel_count; i++ )
        dbg_damage_add(ui, ui->panels[i].last_rect);
    ui->check_style = style;
    for( int i = 0; i < ui->panel_count; i++ )
        dbg_dirty_panel(ui, i);
    ui->dirty = 1;
}

int
ToriRSChrome_CheckStyle(struct ToriRSChrome const* ui)
{
    assert(ui);
    return ui->check_style;
}

void
ToriRSChrome_SetTheme(struct ToriRSChrome* ui, struct ToriRSChromeTheme const* theme)
{
    assert(ui);
    assert(theme);
    if( memcmp(&ui->theme, theme, sizeof(*theme)) == 0 )
        return;
    ui->theme = *theme;
    for( int i = 0; i < ui->panel_count; i++ )
        dbg_dirty_panel(ui, i);
}

/* ---- building ------------------------------------------------------------ */

int
ToriRSChrome_PanelAdd(
    struct ToriRSChrome* ui,
    int style,
    int x,
    int y,
    int fixed_w,
    char const* title)
{
    struct ToriRSChromePanel* p;
    int const handle = ui ? ui->panel_count : -1;

    if( !ui || ui->panel_count >= TORIRS_CHROME_MAX_PANELS )
    {
        if( ui )
            ui->overflow = 1;
        return -1;
    }
    p = &ui->panels[handle];
    memset(p, 0, sizeof(*p));
    p->style = style;
    p->visible = 1;
    p->x = x;
    p->y = y;
    p->fixed_w = fixed_w;
    p->first_widget = -1;
    p->last_widget = -1;
    /* -1 is "every tab": a panel with no strip must not have its rows stamped
     * onto tab 0, or adding a strip later would hide all of them at once. */
    p->build_tab = -1;
    p->dirty = 1;
    dbg_copy(p->title, TORIRS_CHROME_LABEL_MAX, title);
    ui->panel_count++;
    ui->dirty = 1;
    return handle;
}

void
ToriRSChrome_PanelMove(struct ToriRSChrome* ui, int panel, int x, int y)
{
    if( !dbg_valid_panel(ui, panel) )
        return;
    if( ui->panels[panel].x == x && ui->panels[panel].y == y )
        return;
    ui->panels[panel].x = x;
    ui->panels[panel].y = y;
    dbg_dirty_panel(ui, panel);
}

void
ToriRSChrome_PanelSetVisible(struct ToriRSChrome* ui, int panel, int visible)
{
    if( !dbg_valid_panel(ui, panel) )
        return;
    visible = visible ? 1 : 0;
    if( ui->panels[panel].visible == visible )
        return;
    ui->panels[panel].visible = visible;
    dbg_dirty_panel(ui, panel);
}

void
ToriRSChrome_PanelSetFramed(struct ToriRSChrome* ui, int panel, int framed)
{
    assert(ui);
    if( !dbg_valid_panel(ui, panel) )
        return;
    if( ui->panels[panel].framed == (framed ? 1 : 0) )
        return;
    ui->panels[panel].framed = framed ? 1 : 0;
    dbg_dirty_panel(ui, panel);
}

void
ToriRSChrome_PanelSetResizable(struct ToriRSChrome* ui, int panel, int resizable)
{
    if( !dbg_valid_panel(ui, panel) )
        return;
    resizable = resizable ? 1 : 0;
    if( ui->panels[panel].resizable == resizable )
        return;
    ui->panels[panel].resizable = resizable;
    /* The grip is part of the panel's chrome, so turning it on or off is a
     * relayout of that panel and nothing else. */
    dbg_dirty_panel(ui, panel);
}

void
ToriRSChrome_PanelSetClosable(struct ToriRSChrome* ui, int panel, int closable)
{
    assert(ui);
    if( !dbg_valid_panel(ui, panel) )
        return;
    closable = closable ? 1 : 0;
    if( ui->panels[panel].closable == closable )
        return;
    ui->panels[panel].closable = closable;
    dbg_dirty_panel(ui, panel);
}

/**
 * Screen rect of a closable panel's Close button, or a zero rect.
 *
 * Pinned to the RIGHT end of the title bar and sized to the bar's own height,
 * so it sits where a window's close box sits and cannot land on the title text
 * -- which is left-aligned and, on this window, says which page is up.
 */
static int
dbg_panel_is_framed(struct ToriRSChrome const* ui, struct ToriRSChromePanel const* p);

static struct ToriRSChromeRect
dbg_panel_close_box(
    struct ToriRSChrome const* ui,
    struct ToriRSChromePanel const* p,
    struct ToriRSChromeRect box)
{
    struct ToriRSChromeRect out = { 0, 0, 0, 0 };
    /* The title bar is one menu line box tall and starts at the panel's INNER
     * edge -- dbg_menu_layout's header_bar_h, and dbg_build_window's `edge`.
     * Both are read here rather than assumed, because they are what the button
     * has to be centred in: placed at a fixed 2px from the panel's outer edge
     * it sat ABOVE the bar the moment the panel wore a frame thicker than a
     * pixel, which is every framed panel now that the rail is 6. */
    int const bar_h = ToriRSChrome_FontLineBox(TORIRS_CHROME_FONT_MENU, ui->scale);
    int const side = bar_h - 2 * DBG_RULE;
    int const edge = dbg_panel_is_framed(ui, p) ? DBG_FRAME : DBG_RULE;

    assert(ui);
    assert(p);
    if( box.w <= 0 || side <= 0 )
        return out;
    out.w = side;
    out.h = side;
    out.y = box.y + edge + (bar_h - side) / 2;
    out.x = box.x + box.w - edge - DBG_CLOSE_PAD - side;
    return out;
}

static struct ToriRSChromeRect
dbg_panel_close_rect(struct ToriRSChrome const* ui, int panel)
{
    struct ToriRSChromeRect out = { 0, 0, 0, 0 };
    struct ToriRSChromePanel const* p;

    if( !dbg_valid_panel(ui, panel) )
        return out;
    p = &ui->panels[panel];
    if( !p->closable || !p->title[0] )
        return out;
    /* Hit off LAST_RECT, not the live x/y -- the same rule the header drag and
     * the resize grip follow. Mid-drag the panel's own fields have already
     * moved, and the button that was clicked is the one that was drawn. */
    return dbg_panel_close_box(ui, p, p->last_rect);
}

/** Panel whose Close button is under the point, or -1. */
static int
dbg_panel_close_at(struct ToriRSChrome const* ui, int x, int y)
{
    for( int i = ui->panel_count - 1; i >= 0; i-- )
    {
        if( !ui->panels[i].visible || !ui->panels[i].closable )
            continue;
        if( dbg_point_in_rect(x, y, dbg_panel_close_rect(ui, i)) )
            return i;
    }
    return -1;
}

void
ToriRSChrome_PanelSetFixedWidth(struct ToriRSChrome* ui, int panel, int width)
{
    if( !dbg_valid_panel(ui, panel) )
        return;
    if( width < 0 )
        width = 0;
    if( ui->panels[panel].fixed_w == width )
        return;
    /* The box it occupies now is about to be vacated on one side or the
     * other, so it is stale either way. */
    dbg_damage_add(ui, ui->panels[panel].last_rect);
    ui->panels[panel].fixed_w = width;
    dbg_dirty_panel(ui, panel);
}

void
ToriRSChrome_PanelFill(struct ToriRSChrome* ui, int panel, int w, int h)
{
    struct ToriRSChromePanel* p;

    assert(ui);
    /* A surface with no size is not a surface: the caller that has one asks
     * its presentation for it and stays out of here when the answer is no. */
    assert(w > 0);
    assert(h > 0);
    if( !dbg_valid_panel(ui, panel) )
        return;
    p = &ui->panels[panel];
    if( p->filled && p->x == 0 && p->y == 0 && p->fixed_w == w && p->fixed_h == h )
        return;
    /* The box it occupies now is vacated on some side or other -- growing
     * leaves nothing behind, shrinking leaves the old edges. */
    dbg_damage_add(ui, p->last_rect);
    p->filled = 1;
    p->x = 0;
    p->y = 0;
    p->fixed_w = w;
    p->fixed_h = h;
    dbg_dirty_panel(ui, panel);
}

struct ToriRSChromeRect
ToriRSChrome_PanelRect(struct ToriRSChrome const* ui, int panel)
{
    struct ToriRSChromeRect none = { 0, 0, 0, 0 };
    if( !dbg_valid_panel(ui, panel) )
        return none;
    return ui->panels[panel].last_rect;
}

static int
dbg_widget_add(struct ToriRSChrome* ui, int panel, int kind)
{
    struct ToriRSChromeWidget* w;
    int handle;

    if( !dbg_valid_panel(ui, panel) )
        return -1;
    /* A recycled slot before a fresh one, so a panel rebuilt every frame stays
     * inside the array instead of walking the high-water mark up to the cap. */
    if( ui->free_widget >= 0 )
    {
        handle = ui->free_widget;
        ui->free_widget = ui->widgets[handle].next;
    }
    else if( ui->widget_count < TORIRS_CHROME_MAX_WIDGETS )
    {
        handle = ui->widget_count++;
    }
    else
    {
        ui->overflow = 1;
        return -1;
    }
    w = &ui->widgets[handle];
    memset(w, 0, sizeof(*w));
    w->kind = kind;
    w->panel = panel;
    w->next = -1;
    w->tab = ui->panels[panel].build_tab;
    /* Distinct for the life of the instance -- see ToriRSChromeWidget::serial. */
    w->serial = ++ui->next_serial;

    if( ui->panels[panel].first_widget < 0 )
        ui->panels[panel].first_widget = handle;
    else
        ui->widgets[ui->panels[panel].last_widget].next = handle;
    ui->panels[panel].last_widget = handle;

    dbg_dirty_panel(ui, panel);
    return handle;
}

/** Drop every latch that names `widget`, so nothing outlives its target. */
static void
dbg_widget_forget(struct ToriRSChrome* ui, int widget)
{
    if( ui->focus == widget )
        ui->focus = -1;
    if( ui->hover == widget )
        ui->hover = -1;
    if( ui->press == widget )
        ui->press = -1;
    if( ui->activated == widget )
        ui->activated = -1;
    if( ui->dropdown_open == widget )
    {
        ui->dropdown_open = -1;
        ui->dropdown_hover_row = -1;
        ui->dropdown_scroll_drag = 0;
    }
    if( ui->colorpick_open == widget )
    {
        ui->colorpick_open = -1;
        ui->colorpick_drag_bar = TORIRS_CHROME_COLORBAR_NONE;
        ui->widgets[widget].checked = 0;
    }
}

void
ToriRSChrome_WidgetRemove(struct ToriRSChrome* ui, int widget)
{
    struct ToriRSChromePanel* p;
    int prev;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) )
        return;
    p = &ui->panels[ui->widgets[widget].panel];

    /* Unlink from the panel's singly-linked row list. A walk rather than a back
     * pointer: rebuilds clear whole panels at a time (which never walks), and a
     * second link to keep in step is a second link to get wrong. */
    if( p->first_widget == widget )
    {
        p->first_widget = ui->widgets[widget].next;
    }
    else
    {
        for( prev = p->first_widget; prev >= 0; prev = ui->widgets[prev].next )
            if( ui->widgets[prev].next == widget )
            {
                ui->widgets[prev].next = ui->widgets[widget].next;
                break;
            }
    }
    if( p->last_widget == widget )
    {
        int last = -1;
        for( prev = p->first_widget; prev >= 0; prev = ui->widgets[prev].next )
            last = prev;
        p->last_widget = last;
    }

    dbg_widget_forget(ui, widget);
    dbg_dirty_panel(ui, ui->widgets[widget].panel);

    memset(&ui->widgets[widget], 0, sizeof(ui->widgets[widget]));
    ui->widgets[widget].kind = TORIRS_CHROME_W_FREE;
    ui->widgets[widget].next = ui->free_widget;
    ui->free_widget = widget;
}

void
ToriRSChrome_PanelClearWidgets(struct ToriRSChrome* ui, int panel)
{
    int widget;

    assert(ui);
    if( !dbg_valid_panel(ui, panel) )
        return;

    widget = ui->panels[panel].first_widget;
    while( widget >= 0 )
    {
        int const next = ui->widgets[widget].next;
        dbg_widget_forget(ui, widget);
        memset(&ui->widgets[widget], 0, sizeof(ui->widgets[widget]));
        ui->widgets[widget].kind = TORIRS_CHROME_W_FREE;
        ui->widgets[widget].next = ui->free_widget;
        ui->free_widget = widget;
        widget = next;
    }
    ui->panels[panel].first_widget = -1;
    ui->panels[panel].last_widget = -1;
    /* The panel is about to be rebuilt from the top, so the builder's tab
     * stamp goes back to "every tab" rather than whatever the last run left. */
    ui->panels[panel].build_tab = -1;
    ui->panels[panel].scroll_y = 0;
    dbg_dirty_panel(ui, panel);
}

int
ToriRSChrome_Label(struct ToriRSChrome* ui, int panel, char const* text)
{
    return ToriRSChrome_LabelColored(ui, panel, text, 0);
}

int
ToriRSChrome_ModelView(struct ToriRSChrome* ui, int panel, int w, int h)
{
    int const handle = dbg_widget_add(ui, panel, TORIRS_CHROME_W_MODELVIEW);
    if( handle < 0 )
        return -1;
    ui->widgets[handle].view_w = w > 0 ? w : 64;
    ui->widgets[handle].view_h = h > 0 ? h : 64;
    ui->widgets[handle].view_scene_id = 0;
    return handle;
}

void
ToriRSChrome_ModelViewSet(struct ToriRSChrome* ui, int widget, int scene_sprite_id)
{
    assert(ui);
    if( !dbg_valid_widget(ui, widget) )
        return;
    if( ui->widgets[widget].view_scene_id == scene_sprite_id )
        return;
    ui->widgets[widget].view_scene_id = scene_sprite_id;
    dbg_dirty_widget(ui, widget);
}



int
ToriRSChrome_LabelColored(struct ToriRSChrome* ui, int panel, char const* text, uint32_t color)
{
    int const h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_LABEL);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].text, TORIRS_CHROME_INPUT_MAX, text);
    ui->widgets[h].color = color;
    return h;
}

int
ToriRSChrome_Checkbox(struct ToriRSChrome* ui, int panel, char const* label, int checked)
{
    int const h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_CHECKBOX);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].label, TORIRS_CHROME_LABEL_MAX, label);
    ui->widgets[h].checked = checked ? 1 : 0;
    return h;
}

int
ToriRSChrome_ListRow(
    struct ToriRSChrome* ui, int panel, char const* label, int checked, int has_action)
{
    int const h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_LISTROW);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].label, TORIRS_CHROME_LABEL_MAX, label);
    ui->widgets[h].checked = checked ? 1 : 0;
    ui->widgets[h].row_action = has_action ? 1 : 0;
    return h;
}

int
ToriRSChrome_ListRowLocked(struct ToriRSChrome* ui, int panel, char const* label)
{
    int const h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_LISTROW);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].label, TORIRS_CHROME_LABEL_MAX, label);
    /* A locked row is always an action row: with no switch, opening the page
     * is the only thing it can do, and a row that does nothing at all is not
     * worth a line. */
    ui->widgets[h].row_action = 1;
    ui->widgets[h].row_locked = 1;
    return h;
}

int
ToriRSChrome_TextInput(struct ToriRSChrome* ui, int panel, char const* label, char const* text)
{
    int const h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_TEXTINPUT);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].label, TORIRS_CHROME_LABEL_MAX, label);
    dbg_copy(ui->widgets[h].text, TORIRS_CHROME_INPUT_MAX, text);
    ui->widgets[h].caret = (int)strlen(ui->widgets[h].text);
    return h;
}

int
ToriRSChrome_TextArea(
    struct ToriRSChrome* ui, int panel, char const* label, char const* text, int rows)
{
    int const h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_TEXTAREA);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].label, TORIRS_CHROME_LABEL_MAX, label);
    dbg_copy(ui->widgets[h].text, TORIRS_CHROME_INPUT_MAX, text);
    /* Clamped rather than asserted: `rows` reaches here from a plugin
     * manifest, and a manifest asking for eighty lines is a value to bound,
     * not a contract this module wrote. 0 means "the authored default", which
     * is what a caller with no opinion passes. */
    if( rows > TORIRS_CHROME_M_TEXTAREA_ROWS_MAX )
        rows = TORIRS_CHROME_M_TEXTAREA_ROWS_MAX;
    ui->widgets[h].rows = rows > 0 ? rows : TORIRS_CHROME_M_TEXTAREA_ROWS;
    /* At the END, as a text input opens: an edit continues a value rather than
     * replacing it, and a caret parked at 0 means the first keystroke goes in
     * front of everything already there. */
    ui->widgets[h].caret = (int)strlen(ui->widgets[h].text);
    return h;
}

/* ---- the colour picker's model half -------------------------------------- */

/** Rewrite the field to the hex of the value it now holds. The value is the
 *  truth and the text is its rendering, so this runs on every set -- including
 *  the one that follows a typed hex, which is how a colour that quantised
 *  shows the user the entry it actually landed on. */
static void
dbg_colorpick_write_text(struct ToriRSChromeWidget* w)
{
    uint32_t const rgb = ToriRSChrome_Hsl16ToRgb(w->selected);
    static char const HEX[] = "0123456789ABCDEF";
    char out[8];

    out[0] = '#';
    for( int i = 0; i < 6; i++ )
        out[1 + i] = HEX[(rgb >> (20 - 4 * i)) & 0xF];
    out[7] = '\0';
    memcpy(w->text, out, sizeof(out));
    w->caret = 7;
}

int
ToriRSChrome_ColorPick(struct ToriRSChrome* ui, int panel, char const* label, int hsl16)
{
    int const h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_COLORPICK);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].label, TORIRS_CHROME_LABEL_MAX, label);
    ui->widgets[h].selected = hsl16 & 0xFFFF;
    dbg_colorpick_write_text(&ui->widgets[h]);
    return h;
}

int
ToriRSChrome_ColorPickValue(struct ToriRSChrome const* ui, int widget)
{
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_COLORPICK )
        return -1;
    return ui->widgets[widget].selected;
}

void
ToriRSChrome_ColorPickSet(struct ToriRSChrome* ui, int widget, int hsl16)
{
    struct ToriRSChromeWidget* w;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_COLORPICK )
        return;
    w = &ui->widgets[widget];
    hsl16 &= 0xFFFF;
    if( w->selected == hsl16 )
        return;
    w->selected = hsl16;
    dbg_colorpick_write_text(w);
    dbg_dirty_widget(ui, widget);
}

int
ToriRSChrome_ColorPickCommitText(struct ToriRSChrome* ui, int widget)
{
    struct ToriRSChromeWidget* w;
    uint32_t rgb = 0;
    int hsl;

    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_COLORPICK )
        return 0;
    w = &ui->widgets[widget];
    if( !ToriRSChrome_ParseHexRgb(w->text, &rgb) )
    {
        /* Not a colour. The VALUE is untouched and the text is put back to it,
         * because a field left holding "#00FF" would show one thing while the
         * swatch beside it showed another -- and the next Save would write the
         * half-typed string into the config. */
        dbg_colorpick_write_text(w);
        dbg_dirty_widget(ui, widget);
        return 0;
    }
    /* NEAREST, not the reference quantiser: a typed hex should land on the
     * palette entry that looks most like it, and -- because the field is
     * re-read from the store on every open -- the mapping has to be one a
     * colour survives. @see ToriRSChrome_Hsl16NearestRgb. */
    hsl = ToriRSChrome_Hsl16NearestRgb(rgb);
    /* The text is rewritten even when the value did not move: the user may
     * have typed a hex that quantises onto the entry already showing, and
     * leaving their spelling in the field would say it was accepted verbatim. */
    if( w->selected == hsl )
    {
        dbg_colorpick_write_text(w);
        dbg_dirty_widget(ui, widget);
        return 0;
    }
    w->selected = hsl;
    dbg_colorpick_write_text(w);
    dbg_dirty_widget(ui, widget);
    return 1;
}

int
ToriRSChrome_ColorPickIsOpen(struct ToriRSChrome const* ui, int widget)
{
    assert(ui);
    return ui->colorpick_open >= 0 && ui->colorpick_open == widget;
}

void
ToriRSChrome_ColorPickSetOpen(struct ToriRSChrome* ui, int widget, int open)
{
    assert(ui);
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].kind != TORIRS_CHROME_W_COLORPICK )
        return;
    if( open )
    {
        if( ui->colorpick_open == widget )
            return;
        /* Only one thing floats over the chrome at a time -- see the note on
         * colorpick_open for why that is enforced here rather than by sharing
         * a latch with the dropdown. */
        if( ui->dropdown_open >= 0 )
            dbg_dropdown_close(ui);
        if( ui->colorpick_open >= 0 )
        {
            ui->widgets[ui->colorpick_open].checked = 0;
            dbg_dirty_widget(ui, ui->colorpick_open);
        }
        ui->colorpick_open = widget;
        ui->colorpick_drag_bar = TORIRS_CHROME_COLORBAR_NONE;
        /* Mirrored onto the widget's own `checked` rather than left in the
         * instance-wide latch alone, because that field is what the executor
         * seam already diffs and announces (WIDGET_CHECKED). A NATIVE-WIDGET
         * executor draws its own bars -- the model's popup is prims, and a DOM
         * or a component tree cannot use those -- so "is this picker open" has
         * to cross the seam, and this is the crossing that already exists. */
        ui->widgets[widget].checked = 1;
        dbg_dirty_widget(ui, widget);
        return;
    }
    if( ui->colorpick_open != widget )
        return;
    ui->colorpick_open = -1;
    ui->colorpick_drag_bar = TORIRS_CHROME_COLORBAR_NONE;
    ui->widgets[widget].checked = 0;
    dbg_dirty_widget(ui, widget);
}

/**
 * Drop the focus, committing whatever the field was holding.
 *
 * A colour field is the one widget whose TEXT is not its value -- the hex is a
 * rendering of a palette entry -- so a hex typed and then abandoned by
 * clicking elsewhere has to be resolved somewhere. Doing it here rather than
 * per keystroke is what stops the swatch flickering through six wrong colours
 * while the user types the right one; doing it at all is what stops a field
 * reading "#123456" beside a swatch that is still the old colour.
 *
 * Every other kind is unaffected: a text input's text IS its value, so
 * releasing its focus has nothing to settle.
 */
static void
dbg_focus_release(struct ToriRSChrome* ui)
{
    int const had = ui->focus;

    if( had < 0 )
        return;
    ui->focus = -1;
    dbg_dirty_widget(ui, had);
    if( dbg_valid_widget(ui, had) && ui->widgets[had].kind == TORIRS_CHROME_W_COLORPICK )
        ToriRSChrome_ColorPickCommitText(ui, had);
}

/** Keep `selected` inside the list and the scroll window over the selection. */
static void
dbg_dropdown_clamp(struct ToriRSChromeWidget* w)
{
    int rows;

    if( w->selected >= w->option_count )
        w->selected = w->option_count - 1;
    if( w->selected < -1 )
        w->selected = -1;

    rows = w->option_count < TORIRS_CHROME_DROPDOWN_ROWS ? w->option_count
                                                         : TORIRS_CHROME_DROPDOWN_ROWS;
    if( w->scroll > w->option_count - rows )
        w->scroll = w->option_count - rows;
    if( w->scroll < 0 )
        w->scroll = 0;
}

int
ToriRSChrome_MenuDrop(
    struct ToriRSChrome* ui,
    int panel,
    char const* title,
    char const* const* options,
    int option_count)
{
    int const h = ToriRSChrome_Dropdown(ui, panel, "", options, option_count, -1);
    if( h < 0 )
        return -1;
    /* The title rides in `text`, like a MENUITEM's, because menu-mode draws it
     * as the whole closed state; `label` stays empty so the dropdown-rect
     * label offset is zero and the list opens flush under the title. */
    dbg_copy(ui->widgets[h].text, TORIRS_CHROME_INPUT_MAX, title);
    ui->widgets[h].menu_mode = 1;
    ui->widgets[h].selected = -1;
    return h;
}

void
ToriRSChrome_SetHidden(struct ToriRSChrome* ui, int widget, int hidden)
{
    assert(ui);
    if( !dbg_valid_widget(ui, widget) )
        return;
    if( ui->widgets[widget].hidden == (hidden ? 1 : 0) )
        return;
    ui->widgets[widget].hidden = hidden ? 1 : 0;
    dbg_dirty_panel(ui, ui->widgets[widget].panel);
    /* Hiding the open dropdown takes its popup with it. */
    if( hidden && ui->dropdown_open == widget )
        dbg_dropdown_close(ui);
    if( hidden && ui->colorpick_open == widget )
        ToriRSChrome_ColorPickSetOpen(ui, widget, 0);
}

int
ToriRSChrome_Dropdown(
    struct ToriRSChrome* ui,
    int panel,
    char const* label,
    char const* const* options,
    int option_count,
    int selected)
{
    int const h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_DROPDOWN);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].label, TORIRS_CHROME_LABEL_MAX, label);
    ui->widgets[h].options = options;
    ui->widgets[h].option_count = options ? option_count : 0;
    ui->widgets[h].selected = selected;
    dbg_dropdown_clamp(&ui->widgets[h]);
    /* Open on the selection so a palette of hundreds does not open at the top
     * with the current choice off screen. */
    ui->widgets[h].scroll = ui->widgets[h].selected > 0 ? ui->widgets[h].selected : 0;
    dbg_dropdown_clamp(&ui->widgets[h]);
    return h;
}

void
ToriRSChrome_DropdownSetOptions(
    struct ToriRSChrome* ui,
    int widget,
    char const* const* options,
    int option_count,
    int selected)
{
    struct ToriRSChromeWidget* w;

    if( !dbg_valid_widget(ui, widget) )
        return;
    w = &ui->widgets[widget];
    w->options = options;
    w->option_count = options ? option_count : 0;
    w->selected = selected;
    w->scroll = selected > 0 ? selected : 0;
    dbg_dropdown_clamp(w);
    /* The rows under an open list just changed identity; leaving it open would
     * let the next click select something the user never saw. */
    if( ui->dropdown_open == widget )
        ui->dropdown_open = -1;
    dbg_dirty_widget(ui, widget);
}

int
ToriRSChrome_DropdownSelected(struct ToriRSChrome const* ui, int widget)
{
    if( !dbg_valid_widget(ui, widget) )
        return -1;
    return ui->widgets[widget].selected;
}

void
ToriRSChrome_DropdownSetSelected(struct ToriRSChrome* ui, int widget, int selected)
{
    struct ToriRSChromeWidget* w;

    if( !dbg_valid_widget(ui, widget) )
        return;
    w = &ui->widgets[widget];
    if( w->selected == selected )
        return;
    w->selected = selected;
    dbg_dropdown_clamp(w);
    dbg_dirty_widget(ui, widget);
}

int
ToriRSChrome_Separator(struct ToriRSChrome* ui, int panel)
{
    return dbg_widget_add(ui, panel, TORIRS_CHROME_W_SEPARATOR);
}

int
ToriRSChrome_MenuItem(struct ToriRSChrome* ui, int panel, char const* text)
{
    int const h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_MENUITEM);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].text, TORIRS_CHROME_INPUT_MAX, text);
    return h;
}

int
ToriRSChrome_Button(struct ToriRSChrome* ui, int panel, char const* text)
{
    int const h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_BUTTON);
    if( h < 0 )
        return -1;
    dbg_copy(ui->widgets[h].text, TORIRS_CHROME_INPUT_MAX, text);
    return h;
}

/* ---- tabs ---------------------------------------------------------------- */

int
ToriRSChrome_Tabs(
    struct ToriRSChrome* ui,
    int panel,
    char const* const* titles,
    int title_count,
    int selected)
{
    int h;

    assert(ui);
    assert(titles || title_count == 0);
    h = dbg_widget_add(ui, panel, TORIRS_CHROME_W_TABSTRIP);
    if( h < 0 )
        return -1;
    ui->widgets[h].options = titles;
    ui->widgets[h].option_count = title_count;
    /* The strip belongs to no tab: one that stamped itself with tab 0 would
     * vanish the moment tab 1 was chosen, taking the only way back with it. */
    ui->widgets[h].tab = -1;
    if( selected < 0 || selected >= title_count )
        selected = 0;
    ui->widgets[h].selected = selected;
    ui->panels[panel].active_tab = selected;
    return h;
}

void
ToriRSChrome_TabsSetTitles(
    struct ToriRSChrome* ui,
    int widget,
    char const* const* titles,
    int title_count)
{
    struct ToriRSChromeWidget* w;

    assert(ui);
    assert(titles || title_count == 0);
    if( !dbg_valid_widget(ui, widget) )
        return;
    w = &ui->widgets[widget];
    if( w->options == titles && w->option_count == title_count )
        return;
    w->options = titles;
    w->option_count = title_count;
    if( w->selected >= title_count )
        w->selected = title_count > 0 ? title_count - 1 : 0;
    ui->panels[w->panel].active_tab = w->selected;
    dbg_dirty_widget(ui, widget);
}

int
ToriRSChrome_PanelActiveTab(struct ToriRSChrome const* ui, int panel)
{
    if( !dbg_valid_panel(ui, panel) )
        return 0;
    return ui->panels[panel].active_tab;
}

void
ToriRSChrome_PanelSetActiveTab(struct ToriRSChrome* ui, int panel, int tab)
{
    assert(ui);
    if( !dbg_valid_panel(ui, panel) )
        return;
    if( tab < 0 )
        tab = 0;
    if( ui->panels[panel].active_tab == tab )
        return;
    ui->panels[panel].active_tab = tab;
    /* The rows about to appear are a different set from the ones going away, so
     * a scroll offset measured against the old tab means nothing on the new
     * one -- and a tab that opened already scrolled reads as a broken panel. */
    ui->panels[panel].scroll_y = 0;
    /* Whatever was focused or hovered may be on the tab that just left. */
    if( ui->focus >= 0 && ui->widgets[ui->focus].panel == panel )
        dbg_focus_release(ui);
    if( ui->hover >= 0 && ui->widgets[ui->hover].panel == panel )
        ui->hover = -1;
    if( ui->dropdown_open >= 0 && ui->widgets[ui->dropdown_open].panel == panel )
        dbg_dropdown_close(ui);
    if( ui->colorpick_open >= 0 && ui->widgets[ui->colorpick_open].panel == panel )
        ToriRSChrome_ColorPickSetOpen(ui, ui->colorpick_open, 0);
    for( int w = ui->panels[panel].first_widget; w >= 0; w = ui->widgets[w].next )
        if( ui->widgets[w].kind == TORIRS_CHROME_W_TABSTRIP )
        {
            ui->widgets[w].selected = tab;
            break;
        }
    dbg_dirty_panel(ui, panel);
}

void
ToriRSChrome_PanelBeginTab(struct ToriRSChrome* ui, int panel, int tab)
{
    assert(ui);
    if( !dbg_valid_panel(ui, panel) )
        return;
    ui->panels[panel].build_tab = tab < 0 ? -1 : tab;
}

void
ToriRSChrome_PanelSetTitle(struct ToriRSChrome* ui, int panel, char const* title)
{
    assert(ui);
    assert(title);
    if( !dbg_valid_panel(ui, panel) )
        return;
    if( strcmp(ui->panels[panel].title, title) == 0 )
        return;
    dbg_copy(ui->panels[panel].title, TORIRS_CHROME_LABEL_MAX, title);
    dbg_dirty_panel(ui, panel);
}

void
ToriRSChrome_PanelSetScrollable(struct ToriRSChrome* ui, int panel, int scrollable)
{
    assert(ui);
    if( !dbg_valid_panel(ui, panel) )
        return;
    if( ui->panels[panel].scrollable == (scrollable ? 1 : 0) )
        return;
    ui->panels[panel].scrollable = scrollable ? 1 : 0;
    if( !ui->panels[panel].scrollable )
        ui->panels[panel].scroll_y = 0;
    dbg_dirty_panel(ui, panel);
}

/* ---- mutation ------------------------------------------------------------ */

void
ToriRSChrome_SetText(struct ToriRSChrome* ui, int widget, char const* text)
{
    struct ToriRSChromeWidget* w;
    char buf[TORIRS_CHROME_INPUT_MAX];

    if( !dbg_valid_widget(ui, widget) )
        return;
    w = &ui->widgets[widget];
    dbg_copy(buf, TORIRS_CHROME_INPUT_MAX, text);
    /* Compare-then-set is the whole point of retained mode: an app that
     * rewrites its frame counter every frame with the same string does no
     * work at all. */
    if( strcmp(w->text, buf) == 0 )
        return;
    memcpy(w->text, buf, sizeof(buf));
    if( w->caret > (int)strlen(w->text) )
        w->caret = (int)strlen(w->text);
    /* A shorter value has fewer lines, and an offset measured against the old
     * one leaves a multiline box scrolled past everything it now holds --
     * which draws as an empty field that the user cannot scroll back up. */
    if( w->kind == TORIRS_CHROME_W_TEXTAREA )
        dbg_textarea_scroll_to_caret(ui, w);
    dbg_dirty_widget(ui, widget);
}

void
ToriRSChrome_SetLabel(struct ToriRSChrome* ui, int widget, char const* label)
{
    struct ToriRSChromeWidget* w;
    char buf[TORIRS_CHROME_LABEL_MAX];

    if( !dbg_valid_widget(ui, widget) )
        return;
    w = &ui->widgets[widget];
    dbg_copy(buf, TORIRS_CHROME_LABEL_MAX, label);
    if( strcmp(w->label, buf) == 0 )
        return;
    memcpy(w->label, buf, sizeof(buf));
    dbg_dirty_widget(ui, widget);
}

void
ToriRSChrome_SetColor(struct ToriRSChrome* ui, int widget, uint32_t color)
{
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].color == color )
        return;
    ui->widgets[widget].color = color;
    dbg_dirty_widget(ui, widget);
}

void
ToriRSChrome_SetChecked(struct ToriRSChrome* ui, int widget, int checked)
{
    checked = checked ? 1 : 0;
    if( !dbg_valid_widget(ui, widget) || ui->widgets[widget].checked == checked )
        return;
    ui->widgets[widget].checked = checked;
    dbg_dirty_widget(ui, widget);
}

int
ToriRSChrome_Checked(struct ToriRSChrome const* ui, int widget)
{
    return dbg_valid_widget(ui, widget) ? ui->widgets[widget].checked : 0;
}

char const*
ToriRSChrome_Text(struct ToriRSChrome const* ui, int widget)
{
    return dbg_valid_widget(ui, widget) ? ui->widgets[widget].text : "";
}

void
ToriRSChrome_SetCaretVisible(struct ToriRSChrome* ui, int visible)
{
    visible = visible ? 1 : 0;
    assert(ui);
    if( ui->caret_visible == visible )
        return;
    ui->caret_visible = visible;
    /* Only the panel holding the focused input repaints for a blink. */
    if( ui->focus >= 0 )
        dbg_dirty_widget(ui, ui->focus);
}

/* ---- layout -------------------------------------------------------------- */

/** Natural content width of one row, before the panel's padding. */
static int
dbg_widget_width(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    (void)ui;
    switch( w->kind )
    {
    /* No `w->text ? .. : 0` anywhere below: `text` and `label` are fixed-size
     * ARRAYS on the widget, never pointers, so the test is always true and the
     * compiler says so. An empty one measures 0, which is what it wanted. */
    case TORIRS_CHROME_W_LABEL:
        return ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->text);
    case TORIRS_CHROME_W_CHECKBOX:
        return DBG_CHECK_SIZE + DBG_CHECK_GAP +
               ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->label);
    case TORIRS_CHROME_W_LISTROW:
        /* Name, then the fixed furniture at the right end. A row that cannot
         * have its natural width is TRUNCATED at draw rather than laid out
         * narrower -- the switch has to stay reachable at the same x down the
         * whole list, which is what makes it a list instead of a form. */
        return ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->label) +
               DBG_ROW_NAME_GAP + (w->row_action ? DBG_ROW_ICON + DBG_ROW_ICON_GAP : 0) +
               (w->row_locked ? 0 : DBG_TOGGLE_W);
    case TORIRS_CHROME_W_TEXTINPUT:
    {
        int box_w = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->text) +
                    2 * DBG_INPUT_PAD_X + 2 * DBG_RULE;
        if( box_w < DBG_INPUT_MIN_W )
            box_w = DBG_INPUT_MIN_W;
        return dbg_row_stacked_width(ui, w, box_w);
    }
    case TORIRS_CHROME_W_TEXTAREA:
        /*
         * A minimum, not a fit. The value is WRAPPED, so there is no width at
         * which it stops needing one -- measuring the string would ask for a
         * panel as wide as the longest list anyone ever pastes in. What the
         * box does want is enough room to be worth wrapping into, which is the
         * one-line field's minimum plus the caption column it does not use.
         */
        return DBG_LABEL_W + DBG_INPUT_MIN_W;
    case TORIRS_CHROME_W_COLORPICK:
    {
        /* The hex is a fixed seven characters, so the box is measured from the
         * string it will ALWAYS hold rather than from the one it holds now --
         * otherwise the column twitches by a pixel or two as the value changes
         * under the cursor. */
        int box_w = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, "#FFFFFF") +
                    DBG_SWATCH + DBG_SWATCH_GAP + 2 * DBG_INPUT_PAD_X + 2 * DBG_RULE;
        if( box_w < DBG_INPUT_MIN_W )
            box_w = DBG_INPUT_MIN_W;
        return dbg_row_stacked_width(ui, w, box_w);
    }
    case TORIRS_CHROME_W_MENUITEM:
        /* In a WINDOW panel this is a button and is measured as one; the
         * menu-style panel measures its own rows in dbg_build_menu. */
        return DBG_LABEL_W;
    case TORIRS_CHROME_W_DROPDOWN:
    {
        int box_w = DBG_INPUT_MIN_W;
        /* Sized to the widest option, so choosing one never resizes the panel
         * under the cursor. Palettes are built once, so this walk is not per
         * frame -- dbg_widget_width only runs on a dirty build. */
        for( int i = 0; i < w->option_count; i++ )
        {
            int const ow = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->options[i]);
            if( ow > box_w )
                box_w = ow;
        }
        box_w += 2 * DBG_INPUT_PAD_X + 2 * DBG_RULE + DBG_DROP_ARROW_W;
        return dbg_row_stacked_width(ui, w, box_w);
    }
    case TORIRS_CHROME_W_MODELVIEW:
        return w->view_w + 2 * DBG_RULE;
    case TORIRS_CHROME_W_BUTTON:
        /* The label column's width, not the caption's -- see the BUTTON case
         * in dbg_build_window for why a column of buttons is not ragged. */
        return DBG_LABEL_W;
    case TORIRS_CHROME_W_TABSTRIP:
    {
        /* The natural width is every tab at its own caption width. A strip
         * wider than the panel is not clipped away, it is COMPRESSED at draw
         * time -- see dbg_tab_rect -- so this is the width that avoids that,
         * not a width the strip is guaranteed. */
        int total = 0;
        for( int i = 0; i < w->option_count; i++ )
            total += ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->options[i]) +
                     2 * DBG_TAB_PAD_X;
        return total;
    }
    case TORIRS_CHROME_W_SEPARATOR:
    default:
        return 0;
    }
}

/**
 * Row height of one widget, excluding DBG_ROW_GAP.
 *
 * ONE height for every kind of row, not a height per widget.
 *
 * Each kind used to measure itself -- a checkbox as tall as its 17px art, an
 * input as tall as its line box plus its padding, a list row as tall as its
 * switch -- and the result was a column whose controls did not line up with
 * each other, and which did not line up with the same panel built by the CS2
 * executor either. A settings list that does not line up is failing at the one
 * thing a settings list does, so the grid wins and the contents are placed
 * inside it (centred, by dbg_row_text_baseline and the box arithmetic beside
 * it) rather than the other way round.
 *
 * DBG_ROW_H is comfortably over the p12 line box, so nothing is cropped. Still
 * takes the ToriRSChrome, because the two kinds that genuinely cannot live in
 * the grid -- a model view, which is sized by its caller, and a tab strip,
 * which is a strip and not a row -- are measured here too.
 */
static int
dbg_widget_height(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    switch( w->kind )
    {
    case TORIRS_CHROME_W_MODELVIEW:
        return w->view_h + 2 * DBG_RULE;
    case TORIRS_CHROME_W_TABSTRIP:
        return DBG_TAB_H;
    case TORIRS_CHROME_W_TEXTAREA:
        /* The third kind that cannot live in the grid, and for the plainest
         * reason of the three: it is `rows` lines tall by construction, with
         * its caption on a row of its own above. */
        return (w->label[0] ? DBG_ROW_H : 0) + dbg_textarea_box_h(ui, w);
    case TORIRS_CHROME_W_FREE:
        return 0;
    default:
        /* A caption that did not fit its column took a line of its own, so the
         * row is two lines tall. @see dbg_row_label_stacked. */
        return DBG_ROW_H + dbg_row_box_top(ui, w);
    }
}

/**
 * The baseline that centres one line of the row face in a box `h` tall.
 *
 * This is ToriDraw2D's own y_align == 1 arithmetic, which is what the CS2
 * executor's every TEXT component uses (`comp.text_v_align = 1`): the baseline
 * is `max_ascent + (h - max_ascent - max_descent) / 2` below the box top. The
 * baked faces have `line_box == max_ascent + max_descent` exactly -- p12 is
 * 12 + 4 == 16 -- so the line box stands in for the pair and the two
 * presentations land on the same pixel row.
 */
static int
dbg_row_text_baseline(struct ToriRSChrome const* ui, int box_y, int box_h)
{
    int const line_box = ToriRSChrome_FontLineBox(ui->theme.font_row, ui->scale);
    return box_y + (box_h - line_box) / 2 +
           ToriRSChrome_FontLineHeight(ui->theme.font_row, ui->scale);
}

/* ---- display list -------------------------------------------------------- */

static struct ToriRSChromePrim*
dbg_prim_push(struct ToriRSChrome* ui)
{
    struct ToriRSChromePrim* p;
    if( ui->prim_count >= TORIRS_CHROME_MAX_PRIMS )
    {
        ui->overflow = 1;
        return NULL;
    }
    p = &ui->prims[ui->prim_count++];
    memset(p, 0, sizeof(*p));
    return p;
}

/**
 * One wrapped line, copied into the build's scratch and NUL-terminated.
 *
 * A TEXT prim BORROWS its string, and a wrapped line is a SLICE of a widget's
 * value rather than a string of its own -- the byte after it belongs to the
 * next line, so there is nowhere to put a terminator without destroying it.
 * @return NULL when the pool is exhausted, which raises `overflow` exactly as
 * running out of prims does.
 */
static char const*
dbg_wrap_line(struct ToriRSChrome* ui, char const* src, int len)
{
    char* out;

    assert(src);
    if( len < 0 )
        len = 0;
    if( ui->wrap_used + len + 1 > TORIRS_CHROME_WRAP_POOL )
    {
        ui->overflow = 1;
        return NULL;
    }
    out = ui->wrap_pool + ui->wrap_used;
    memcpy(out, src, (size_t)len);
    out[len] = '\0';
    ui->wrap_used += len + 1;
    return out;
}

/** @param trans 0 opaque .. 255 invisible, the client's sense. */
static void
dbg_push_rect_trans(
    struct ToriRSChrome* ui,
    int x,
    int y,
    int w,
    int h,
    uint32_t color,
    int filled,
    int trans,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromePrim* p;
    if( w <= 0 || h <= 0 )
        return;
    /* Fully transparent is not a primitive. The reference builds invisible
     * rects because they still carry a click op; ours carry nothing, so one
     * here would be a display-list slot spent painting nothing. */
    if( trans >= 255 )
        return;
    p = dbg_prim_push(ui);
    if( !p )
        return;
    p->kind = TORIRS_CHROME_PRIM_RECT;
    p->x = x;
    p->y = y;
    p->w = w;
    p->h = h;
    p->color = color;
    p->filled = filled;
    p->trans = trans;
    p->clip = clip;
}

static void
dbg_push_rect(
    struct ToriRSChrome* ui,
    int x,
    int y,
    int w,
    int h,
    uint32_t color,
    int filled,
    struct ToriRSChromeRect clip)
{
    dbg_push_rect_trans(ui, x, y, w, h, color, filled, 0, clip);
}

/** @param y the text baseline (ToriDraw2D_DrawString's y), not a box top. */
static void
dbg_push_text(
    struct ToriRSChrome* ui,
    int x,
    int y,
    char const* text,
    uint32_t color,
    int font_slot,
    int shadowed,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromePrim* p;
    if( !text || !text[0] )
        return;
    p = dbg_prim_push(ui);
    if( !p )
        return;
    p->kind = TORIRS_CHROME_PRIM_TEXT;
    p->x = x;
    p->y = y;
    p->color = color;
    p->font_slot = font_slot;
    p->baseline = 1;
    /* OR, not assign: the theme flag can add the reference's drop shadow to
     * every string, but must never take one away from a call site that already
     * asked for it (the menu rows). */
    p->shadowed = shadowed || ui->theme.text_shadowed;
    p->text = text;
    p->clip = clip;
}

/** One baked image scaled into `w` x `h`; 0 x 0 means the image's own size. */
static void
dbg_push_sprite_box(
    struct ToriRSChrome* ui,
    int x,
    int y,
    int w,
    int h,
    int slot,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromePrim* p;

    /* 0 x 0 is the documented native-size request. A box with exactly one side
     * zero is a caller that computed a run and got it wrong, and silently
     * blitting the 16x5 grip middle at native size over a cap is not the
     * answer to that. */
    assert(w >= 0);
    assert(h >= 0);
    assert((w == 0) == (h == 0));
    p = dbg_prim_push(ui);
    if( !p )
        return;
    p->kind = TORIRS_CHROME_PRIM_SPRITE;
    p->x = x;
    p->y = y;
    p->w = w;
    p->h = h;
    p->sprite_slot = slot;
    p->clip = clip;
}

/** One baked image at its native size. */
static void
dbg_push_sprite(
    struct ToriRSChrome* ui,
    int x,
    int y,
    int slot,
    struct ToriRSChromeRect clip)
{
    dbg_push_sprite_box(ui, x, y, 0, 0, slot, clip);
}

static int
dbg_skin_has(struct ToriRSChrome const* ui, int slot)
{
    return (ui->skin_avail & (1u << slot)) != 0;
}

/**
 * Fill a box by repeating a skin image across it, clipped to the box.
 *
 * The tile's own size is not known up here -- the drawer holds the pixels --
 * so this steps by a size the caller passes and lets the clip rect cut the
 * last row and column. Callers pass the size they uploaded; a wrong guess
 * costs overdraw at the edges, never a gap, because the step is what the
 * loop bounds use.
 */
static void
dbg_fill_tiled(
    struct ToriRSChrome* ui,
    int x,
    int y,
    int w,
    int h,
    int slot,
    int tile_w,
    int tile_h,
    struct ToriRSChromeRect clip)
{
    if( tile_w <= 0 || tile_h <= 0 )
        return;
    for( int ty = y; ty < y + h; ty += tile_h )
        for( int tx = x; tx < x + w; tx += tile_w )
            dbg_push_sprite(ui, tx, ty, slot, clip);
}

/** The overlap of two boxes; w or h comes back 0 when they do not meet. */
static struct ToriRSChromeRect
dbg_rect_clip(struct ToriRSChromeRect a, struct ToriRSChromeRect b)
{
    struct ToriRSChromeRect r;
    int const x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int const y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;

    r.x = a.x > b.x ? a.x : b.x;
    r.y = a.y > b.y ? a.y : b.y;
    r.w = x1 > r.x ? x1 - r.x : 0;
    r.h = y1 > r.y ? y1 - r.y : 0;
    return r;
}

/* ---- scrollbars ----------------------------------------------------------
 *
 * The client's bar, both of the ways the client draws it.
 *
 * With the baked skin present it is assembled from the six cache sprites
 * ~script31 assembles it from -- two arrow buttons, a track, and a grip whose
 * middle stretches between two 5px caps -- which is why the skin bake carries
 * exactly those six. Without them it falls back to the flat IF1 form the
 * client's own scrollbar_v renderer draws: a dark track, a grip, a highlight
 * down its top and left and a shadow down its bottom and right.
 *
 * Both forms are 16 chrome pixels wide, because that is the sprite's width and
 * the width ~script31 gives the column.
 */

/** Where the grip sits inside a bar, and how tall it is. */
struct DbgScrollGeom
{
    /** Screen y of the track (below the up arrow) and its length. */
    int track_y;
    int track_h;
    int grip_y;
    int grip_h;
};

/**
 * Resolve a bar's grip from the content it scrolls.
 *
 * ~script31's arithmetic: the grip is the visible fraction of the track, never
 * shorter than 10, and its travel is the leftover track spread across the
 * scroll range. Returns 0 when there is nothing to scroll, which is also the
 * answer to "should a bar be drawn at all".
 *
 * Everything is in pixels rather than rows so the same function serves the
 * draw, the hit test and the drag -- a grip whose position is computed twice
 * is a grip that jumps under the cursor.
 */
static int
dbg_scroll_geom(
    struct ToriRSChromeRect bar,
    int content_px,
    int view_px,
    int offset_px,
    int arrow_h,
    int grip_min,
    struct DbgScrollGeom* out)
{
    int range;

    assert(out);
    out->track_y = bar.y + arrow_h;
    out->track_h = bar.h - 2 * arrow_h;
    out->grip_y = out->track_y;
    out->grip_h = 0;
    if( out->track_h <= 0 || content_px <= 0 || view_px <= 0 || content_px <= view_px )
        return 0;

    out->grip_h = out->track_h * view_px / content_px;
    if( out->grip_h < grip_min )
        out->grip_h = grip_min;
    if( out->grip_h > out->track_h )
        out->grip_h = out->track_h;
    range = content_px - view_px;
    out->grip_y = out->track_y + (out->track_h - out->grip_h) * offset_px / range;
    return 1;
}

/** One arrow button: the baked sprite, or a wedge of rects when there is none. */
static void
dbg_push_scroll_arrow(
    struct ToriRSChrome* ui,
    int x,
    int y,
    int size,
    int slot,
    int down,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromeTheme const* th = &ui->theme;

    if( th->skin_dropdown && dbg_skin_has(ui, slot) )
    {
        dbg_push_sprite_box(ui, x, y, size, size, slot, clip);
        return;
    }
    /* The fallback wedge, drawn on a grip-coloured button so the arrow ends
     * still read as the two things you can press. Rects rather than a glyph:
     * the baked fonts carry no arrow, and the display list's only shape is a
     * rect anyway. */
    dbg_push_rect(ui, x, y, size, size, th->scroll_grip, 1, clip);
    dbg_push_rect(ui, x, y, size, size, th->scroll_grip_lo, 0, clip);
    for( int step = 0; step * 2 < size; step++ )
    {
        int const row = down ? y + size / 4 + step : y + (3 * size) / 4 - step;
        int const w = size - 2 * step - size / 2;
        if( w <= 0 )
            break;
        dbg_push_rect(ui, x + step + size / 4, row, w, DBG_RULE, th->scroll_grip_hi, 1, clip);
    }
}

/**
 * The whole bar, over `bar`, for a view of `view_px` into `content_px`.
 *
 * Draws nothing when the content fits: a bar with a full-length grip is a
 * control that cannot do anything, and the reference hides it the same way
 * (`if_setscrollsize(0, 0, ...)`).
 */
static void
dbg_push_scrollbar(
    struct ToriRSChrome* ui,
    struct ToriRSChromeRect bar,
    int content_px,
    int view_px,
    int offset_px,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    struct DbgScrollGeom g;
    int const arrow = DBG_SCROLL_W;
    struct ToriRSChromeRect const inner = dbg_rect_clip(clip, bar);

    if( bar.w <= 0 || bar.h <= 0 )
        return;
    if( !dbg_scroll_geom(bar, content_px, view_px, offset_px, arrow, DBG_SCROLL_GRIP_MIN, &g) )
        return;

    /* Track first, then the grip over it, then the arrows -- draw order as
     * ~script31 creates them. */
    if( th->skin_dropdown && dbg_skin_has(ui, TORIRS_CHROME_SKIN_SCROLL_TRACK) )
        dbg_push_sprite_box(
            ui, bar.x, g.track_y, bar.w, g.track_h, TORIRS_CHROME_SKIN_SCROLL_TRACK, inner);
    else
        dbg_push_rect(ui, bar.x, g.track_y, bar.w, g.track_h, th->scroll_track, 1, inner);

    if( th->skin_dropdown && dbg_skin_has(ui, TORIRS_CHROME_SKIN_SCROLL_GRIP_MID) )
    {
        /* The middle stretched over the WHOLE grip and the caps laid on its
         * ends, not three pieces butted together. That is ~script31's order,
         * and it is what keeps a 10px grip -- shorter than its own two caps --
         * looking like a grip instead of like two overlapping stubs. */
        dbg_push_sprite_box(
            ui, bar.x, g.grip_y, bar.w, g.grip_h, TORIRS_CHROME_SKIN_SCROLL_GRIP_MID, inner);
        dbg_push_sprite_box(
            ui,
            bar.x,
            g.grip_y,
            bar.w,
            DBG_SCROLL_CAP_H,
            TORIRS_CHROME_SKIN_SCROLL_GRIP_TOP,
            inner);
        dbg_push_sprite_box(
            ui,
            bar.x,
            g.grip_y + g.grip_h - DBG_SCROLL_CAP_H,
            bar.w,
            DBG_SCROLL_CAP_H,
            TORIRS_CHROME_SKIN_SCROLL_GRIP_BOTTOM,
            inner);
    }
    else
    {
        /* torirs_frame.c's translate_scrollbar_v_step, steps 3..8: the body,
         * a 2px highlight down the top and the left, a shadow down the right
         * and the bottom. */
        dbg_push_rect(ui, bar.x, g.grip_y, bar.w, g.grip_h, th->scroll_grip, 1, inner);
        dbg_push_rect(ui, bar.x, g.grip_y, 2 * DBG_RULE, g.grip_h, th->scroll_grip_hi, 1, inner);
        dbg_push_rect(ui, bar.x, g.grip_y, bar.w, 2 * DBG_RULE, th->scroll_grip_hi, 1, inner);
        dbg_push_rect(
            ui, bar.x + bar.w - DBG_RULE, g.grip_y, DBG_RULE, g.grip_h, th->scroll_grip_lo, 1,
            inner);
        dbg_push_rect(
            ui,
            bar.x,
            g.grip_y + g.grip_h - DBG_RULE,
            bar.w,
            DBG_RULE,
            th->scroll_grip_lo,
            1,
            inner);
    }

    dbg_push_scroll_arrow(ui, bar.x, bar.y, arrow, TORIRS_CHROME_SKIN_SCROLL_UP, 0, inner);
    dbg_push_scroll_arrow(
        ui, bar.x, bar.y + bar.h - arrow, arrow, TORIRS_CHROME_SKIN_SCROLL_DOWN, 1, inner);
}

/* ---- tab strips ----------------------------------------------------------
 *
 * Tabs are laid out by PREFIX SUM rather than by width-per-tab: the left edge
 * of tab i is the scaled prefix of the captions before it, and its width is the
 * next edge minus that one. Widths computed independently and then scaled
 * accumulate one rounding error per tab, which shows up as a strip that stops a
 * few pixels short of its panel; edges from a common prefix always meet, and
 * the last one lands exactly on the end.
 *
 * Compression rather than clipping when the captions do not fit, because a tab
 * strip is a set of destinations: a tab scrolled off the end of its own strip
 * cannot be reached, where a tab squeezed to its first few letters still can.
 */

/** Natural width of one tab: its caption plus the air either side. */
static int
dbg_tab_natural(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w, int index)
{
    return ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->options[index]) +
           2 * DBG_TAB_PAD_X;
}

/** Sum of the natural widths of the tabs before `count`. */
static int
dbg_tab_prefix(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w, int count)
{
    int sum = 0;
    for( int i = 0; i < count && i < w->option_count; i++ )
        sum += dbg_tab_natural(ui, w, i);
    return sum;
}

/**
 * Box of one tab, in absolute screen pixels. Empty (w 0) for an out-of-range
 * index or a strip with no room.
 *
 * THE one answer to "where is tab i", asked by the draw and by the hit test.
 */
static struct ToriRSChromeRect
dbg_tab_rect(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w, int index)
{
    struct ToriRSChromeRect r = { 0, 0, 0, 0 };
    int total;
    int x0;
    int x1;

    if( index < 0 || index >= w->option_count || w->w <= 0 )
        return r;
    total = dbg_tab_prefix(ui, w, w->option_count);
    if( total <= 0 )
        return r;

    if( total <= w->w )
    {
        x0 = dbg_tab_prefix(ui, w, index);
        x1 = x0 + dbg_tab_natural(ui, w, index);
    }
    else
    {
        x0 = dbg_tab_prefix(ui, w, index) * w->w / total;
        x1 = dbg_tab_prefix(ui, w, index + 1) * w->w / total;
        if( x1 - x0 < DBG_TAB_MIN_W )
            x1 = x0 + DBG_TAB_MIN_W;
    }
    r.x = w->x + x0;
    r.y = w->y;
    r.w = x1 - x0;
    r.h = w->h;
    return r;
}

/** Tab under a point, or -1. */
static int
dbg_tab_at(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w, int x, int y)
{
    for( int i = 0; i < w->option_count; i++ )
    {
        struct ToriRSChromeRect const r = dbg_tab_rect(ui, w, i);
        if( r.w > 0 && dbg_point_in_rect(x, y, r) )
            return i;
    }
    return -1;
}

/**
 * The box the strip's tabs actually occupy. Empty (w 0) for a strip with none.
 *
 * The union rather than a list, and the union is exact rather than
 * conservative: dbg_tab_rect lays the tabs out contiguously from the strip's
 * left edge, so first-through-last covers every tab and nothing else. What is
 * left over is the strip's empty tail, which is the only part of it that is not
 * already a control. @see ToriRSChrome_WindowDragRegion.
 */
static struct ToriRSChromeRect
dbg_tab_run_rect(struct ToriRSChrome const* ui, struct ToriRSChromeWidget const* w)
{
    struct ToriRSChromeRect run = { 0, 0, 0, 0 };

    assert(ui);
    assert(w);
    for( int i = 0; i < w->option_count; i++ )
    {
        struct ToriRSChromeRect const r = dbg_tab_rect(ui, w, i);
        int right;

        if( r.w <= 0 )
            continue;
        if( run.w == 0 )
        {
            run = r;
            continue;
        }
        right = r.x + r.w > run.x + run.w ? r.x + r.w : run.x + run.w;
        if( r.x < run.x )
            run.x = r.x;
        run.w = right - run.x;
    }
    return run;
}

/**
 * The strip: a rule along the bottom, and a raised box per tab.
 *
 * The SELECTED tab has no bottom rule -- that gap is what joins it to the
 * content beneath, and it is the whole of what makes a tab strip read as tabs
 * rather than as a row of buttons.
 */
static void
dbg_push_tabstrip(
    struct ToriRSChrome* ui,
    struct ToriRSChromeWidget const* w,
    int active,
    int hover_tab,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    int const base_y = w->y + w->h - DBG_RULE;
    int const text_y = dbg_row_text_baseline(ui, w->y, w->h);

    dbg_push_rect(ui, w->x, base_y, w->w, DBG_RULE, th->panel_border, 1, clip);

    for( int i = 0; i < w->option_count; i++ )
    {
        struct ToriRSChromeRect const r = dbg_tab_rect(ui, w, i);
        int const on = (i == active);
        struct ToriRSChromeRect tab_clip;
        int caption_w;
        int text_x;

        if( r.w <= 0 )
            continue;

        /* An unselected tab is a veil over the panel body, so the tabs read as
         * part of the same surface; the selected one is the body itself,
         * unveiled, continuing into the content below. */
        if( !on )
            dbg_push_rect_trans(
                ui, r.x, r.y, r.w, r.h, th->dropdown_veil, 1,
                i == hover_tab ? th->dropdown_row_trans_hover : th->dropdown_band_trans, clip);

        dbg_push_rect(ui, r.x, r.y, DBG_RULE, r.h, th->panel_border, 1, clip);
        dbg_push_rect(ui, r.x, r.y, r.w, DBG_RULE, th->panel_border, 1, clip);
        if( i == w->option_count - 1 )
            dbg_push_rect(ui, r.x + r.w - DBG_RULE, r.y, DBG_RULE, r.h, th->panel_border, 1, clip);
        /* Erase the base rule under the selected tab -- see the note above. */
        if( on )
            dbg_push_rect(
                ui, r.x + DBG_RULE, base_y, r.w - 2 * DBG_RULE, DBG_RULE, th->panel_body, 1, clip);

        /* Captions are clipped to their own tab so a compressed strip cuts the
         * text at the tab's edge instead of running it under its neighbour. */
        tab_clip = dbg_rect_clip(clip, r);
        caption_w = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->options[i]);
        text_x = r.w > caption_w ? r.x + (r.w - caption_w) / 2 : r.x + DBG_RULE;
        dbg_push_text(
            ui,
            text_x,
            text_y,
            w->options[i],
            on ? th->text : (i == hover_tab ? th->accent : th->text_dim),
            ui->theme.font_row,
            0,
            tab_clip);
    }
}

/*
 * The interfaces' own panel frame, as a nine-slice.
 *
 * The border the gameframe's popout strip draws around the panels that mount
 * in it -- which is what the plugin window wears so it reads as the game's own
 * furniture rather than as a developer overlay.
 *
 * Nine pieces and not one stretched box, because the corners are ROUNDED: the
 * outer pixel of each 3x3 corner is transparent, so a single sprite stretched
 * over the panel would smear that transparency down both edges. Corners blit
 * at DBG_FRAME_CORNER square, edges stretch along their runs at DBG_FRAME, and no centre is
 * deliberately not drawn -- the panel's own tile is already under it, and a
 * flat brown painted over the parchment is the frame erasing the surface it
 * frames.
 *
 * Nothing is drawn at all when the build has no frame in its skin: a panel
 * with a border that half arrived is worse than one with none, so the whole
 * frame is gated on its TOP_LEFT piece and the rest are asserted rather than
 * tested (they are baked together or not at all).
 */
static void
dbg_push_frame(struct ToriRSChrome* ui, struct ToriRSChromeRect box, struct ToriRSChromeRect clip)
{
    int const rail = DBG_FRAME;
    int const c = DBG_FRAME_CORNER;
    int const mid_w = box.w - 2 * c;
    int const mid_h = box.h - 2 * c;
    /* The far corners' origins, and the far rails'. Two different insets,
     * because a corner is a 32-wide tile and a rail is 6 thick. */
    int const corner_r = box.x + box.w - c;
    int const corner_b = box.y + box.h - c;
    int const rail_r = box.x + box.w - rail;
    int const rail_b = box.y + box.h - rail;

    if( !dbg_skin_has(ui, TORIRS_CHROME_SKIN_FRAME_TOP_LEFT) )
        return;
    /* A panel narrower or shorter than two of its own corners has no edge run
     * to stretch, and the corners OVERLAP -- which is not a degenerate case to
     * guard against but what the 42px-wide popout strip does with this very
     * art. They still read as a frame; the run is simply gone. */
    dbg_push_sprite_box(ui, box.x, box.y, c, c, TORIRS_CHROME_SKIN_FRAME_TOP_LEFT, clip);
    dbg_push_sprite_box(ui, corner_r, box.y, c, c, TORIRS_CHROME_SKIN_FRAME_TOP_RIGHT, clip);
    dbg_push_sprite_box(ui, box.x, corner_b, c, c, TORIRS_CHROME_SKIN_FRAME_BOTTOM_LEFT, clip);
    dbg_push_sprite_box(ui, corner_r, corner_b, c, c, TORIRS_CHROME_SKIN_FRAME_BOTTOM_RIGHT, clip);
    if( mid_w > 0 )
    {
        dbg_push_sprite_box(
            ui, box.x + c, box.y, mid_w, rail, TORIRS_CHROME_SKIN_FRAME_TOP, clip);
        dbg_push_sprite_box(
            ui, box.x + c, rail_b, mid_w, rail, TORIRS_CHROME_SKIN_FRAME_BOTTOM, clip);
    }
    if( mid_h > 0 )
    {
        dbg_push_sprite_box(
            ui, box.x, box.y + c, rail, mid_h, TORIRS_CHROME_SKIN_FRAME_LEFT, clip);
        dbg_push_sprite_box(
            ui, rail_r, box.y + c, rail, mid_h, TORIRS_CHROME_SKIN_FRAME_RIGHT, clip);
    }
}

/**
 * Does this panel wear the nine-slice frame?
 *
 * Asked, rather than reading `framed` at each of the half-dozen sites that
 * need the panel's border thickness: a build that baked no frame must lay the
 * panel out at the 1px rail it is actually going to draw, or its content sits
 * three pixels in from an edge that is not there.
 */
static int
dbg_panel_is_framed(struct ToriRSChrome const* ui, struct ToriRSChromePanel const* p)
{
    return p->framed && dbg_skin_has(ui, TORIRS_CHROME_SKIN_FRAME_TOP_LEFT);
}

/*
 * The chrome a SETTINGS FIELD wears: a tiled body under a near-black frame with
 * a grey inset one pixel inside it.
 *
 * script_3850 verbatim, and shared because the reference shares it: a dropdown
 * button and a text input on the settings page are the SAME box -- tiled
 * graphic_297, an 0x0e0e0c outline, an 0x474745 inset -- and differ only in
 * what goes inside. A text input drawn as a flat rectangle instead is the one
 * thing that made this chrome not look like the game's.
 */
static void
dbg_push_field_chrome(
    struct ToriRSChrome* ui,
    struct ToriRSChromeRect box,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    struct ToriRSChromeRect const inside = dbg_rect_clip(clip, box);

    /* Flat fill first either way: the tile carries transparent pixels at its
     * edges, and tiling it straight onto the panel would show through them. */
    dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->input_bg, 1, clip);
    if( th->skin_dropdown && dbg_skin_has(ui, TORIRS_CHROME_SKIN_PANEL_BODY) )
        dbg_fill_tiled(
            ui, box.x, box.y, box.w, box.h, TORIRS_CHROME_SKIN_PANEL_BODY, ui->skin_tile_w,
            ui->skin_tile_h, inside);

    dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->dropdown_border, 0, clip);
    dbg_push_rect(
        ui,
        box.x + DBG_RULE,
        box.y + DBG_RULE,
        box.w - 2 * DBG_RULE,
        box.h - 2 * DBG_RULE,
        th->dropdown_border_inner,
        0,
        clip);
}

/*
 * The closed dropdown button, as script_3850 builds it.
 *
 * Five pieces in this order: the panel tile, a near-black frame, a grey inset
 * one pixel inside it, the arrow sprite on the RIGHT, and the current value
 * centred in what is left. The arrow points down while the list is shut and up
 * while it is open, which in the reference is literally the same two sprites
 * the scrollbar's ends wear -- so the skin carries one pair, not two.
 *
 * The arrow's side is `script_3850`'s, read off the position modes rather than
 * guessed: it places the arrow at `cc_setposition(12, .., 2, 0)` and the value
 * at `(28, .., 2, 0)`, and IF3 x-mode 2 is `parent_w - base - self_w`, so the
 * 16-wide arrow's left edge lands exactly where the value's right edge does.
 * Arrow at the right, value in the strip beside it. (An earlier revision of
 * this drew it on the left, on the strength of the raw x values alone.)
 */
static void
dbg_push_dropdown_button(
    struct ToriRSChrome* ui,
    struct ToriRSChromeRect box,
    char const* text,
    int open,
    int hovered,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    struct ToriRSChromeRect const inside = dbg_rect_clip(clip, box);
    int const arrow = DBG_DROP_ARROW_W;
    int const arrow_slot = open ? TORIRS_CHROME_SKIN_SCROLL_UP : TORIRS_CHROME_SKIN_SCROLL_DOWN;
    int const line_box = ToriRSChrome_FontLineBox(th->font_row, ui->scale);
    int const text_top = box.y + (box.h - line_box) / 2;
    struct ToriRSChromeRect text_clip;
    int text_x;
    int text_w;
    int shown_w;

    assert(text);
    if( box.w <= 0 || box.h <= 0 )
        return;

    dbg_push_field_chrome(ui, box, clip);

    int const arrow_x = box.x + box.w - DBG_FIELD_INSET - arrow;

    dbg_push_scroll_arrow(
        ui, arrow_x, box.y + (box.h - arrow) / 2, arrow, arrow_slot, !open, inside);

    /* The value gets the strip left of the arrow, centred in it when it fits
     * and left-aligned when it does not -- the reference sizes its button to
     * the text and so only ever has the first case, and a centred string that
     * is being clipped at both ends is unreadable. */
    text_x = box.x + DBG_FIELD_INSET;
    text_w = arrow_x - text_x;
    shown_w = ToriRSChrome_MeasureText(th->font_row, ui->scale, text);
    if( shown_w < text_w )
        text_x += (text_w - shown_w) / 2;
    text_clip = inside;
    if( text_clip.x + text_clip.w > arrow_x )
        text_clip.w = arrow_x > text_clip.x ? arrow_x - text_clip.x : 0;
    dbg_push_text(
        ui,
        text_x,
        text_top + ToriRSChrome_FontLineHeight(th->font_row, ui->scale),
        text,
        th->dropdown_text,
        th->font_row,
        0,
        text_clip);

    /* The hover veil goes on last, over the text, because that is where
     * script_3850 creates it -- it is a translucent black rect that starts
     * invisible and drops to trans 220 on mouseover. */
    if( hovered && !open )
        dbg_push_rect_trans(
            ui,
            box.x + DBG_RULE,
            box.y + DBG_RULE,
            box.w - 2 * DBG_RULE,
            box.h - 2 * DBG_RULE,
            th->dropdown_veil,
            1,
            th->dropdown_hover_trans,
            clip);
}

/**
 * Grab box of a panel's resize grip: the bottom-right corner, inside the edge.
 *
 * One function, used by the draw and by the hit test, so the pixels that show
 * a grip and the pixels that take one cannot drift apart. Callers check
 * `resizable` first -- this answers for any panel, because a rect is not a
 * permission.
 */
static struct ToriRSChromeRect
dbg_grip_rect(struct ToriRSChrome const* ui, struct ToriRSChromeRect panel)
{
    struct ToriRSChromeRect r;
    r.x = panel.x + panel.w - DBG_RULE - DBG_GRIP_HIT;
    r.y = panel.y + panel.h - DBG_RULE - DBG_GRIP_HIT;
    r.w = DBG_GRIP_HIT;
    r.h = DBG_GRIP_HIT;
    return r;
}

/*
 * The resize grip: three diagonal carets nested into the bottom-right corner,
 * the corner both edges it drags meet at.
 *
 * Dots rather than solid diagonals because a diagonal line, in a display list
 * whose only shape is a rect, costs one primitive per pixel -- three full
 * eight-pixel lines would be 24 prims on every panel, against a 512-prim
 * budget shared with everything else on screen. Six 2x2 dots read as the same
 * chevrons and cost six.
 */
static void
dbg_push_grip(
    struct ToriRSChrome* ui,
    struct ToriRSChromePanel const* p,
    struct ToriRSChromeRect clip)
{
    struct ToriRSChromeRect const box = { p->x, p->y, p->w, p->h };
    struct ToriRSChromeRect const g = dbg_grip_rect(ui, box);
    /* Inside the rail and above the bottom rule: the grab box has a pixel of
     * slop on both, and the dots must not paint over either. */
    int const x1 = g.x + g.w - 2 * DBG_RULE;
    int const y1 = g.y + g.h - 2 * DBG_RULE;

    for( int j = 0; j < DBG_GRIP_LINES; j++ )
        for( int i = 0; i + j < DBG_GRIP_LINES; i++ )
            dbg_push_rect(
                ui,
                x1 - DBG_GRIP_DOT + DBG_RULE - i * DBG_GRIP_PITCH,
                y1 - DBG_GRIP_DOT + DBG_RULE - j * DBG_GRIP_PITCH,
                DBG_GRIP_DOT,
                DBG_GRIP_DOT,
                ui->theme.panel_border,
                1,
                clip);
}

/**
 * Does this panel show a resize grip?
 *
 * A FILLED panel does not, however it was declared: its size is the surface's,
 * so a grip drag would be undone by the next fill -- and the corner it reserves
 * would be a strip of empty body at the bottom of a window nobody can resize
 * from the inside. Asked here rather than tested at each of the two sites, so
 * the draw and the hit test cannot answer it differently.
 */
static int
dbg_panel_has_grip(struct ToriRSChromePanel const* p)
{
    assert(p);
    return p->resizable && !p->filled;
}

/*
 * A window panel: the minimenu's chrome, body fill, an optional title bar in
 * the menu face, then one row per widget in the theme's row face.
 */
static void
dbg_build_window(struct ToriRSChrome* ui, struct ToriRSChromePanel* p)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    struct DbgMenuLayout const l =
        dbg_menu_layout(ToriRSChrome_FontLineBox(TORIRS_CHROME_FONT_MENU, ui->scale), ui->scale);
    /*
     * The panel's own border thickness: the minimenu's hairline rail, or the
     * nine-slice frame when the panel asked for one and the build baked it.
     *
     * ONE number, read by the size, the content column, the clip, the scroll
     * column and the footer alike. Every one of those used DBG_RULE directly
     * before the frame existed, and a frame added at the draw alone would have
     * been three pixels of border painted over the first row.
     */
    int const edge = dbg_panel_is_framed(ui, p) ? DBG_FRAME : DBG_RULE;
    /* The minimenu header block is authored against a 1px border, so a thicker
     * one shifts the whole of it down rather than restating its arithmetic. */
    int const head_y = p->y + edge - DBG_RULE;
    /* Distance from the panel's top edge to the first content row, borders
     * included: the black bar, the body gap under it and the separator rule --
     * the minimenu's own header block. A titleless panel has just its top
     * border. */
    int const head_h = p->title[0] ? (edge - DBG_RULE) + l.separator_y + DBG_RULE : edge;
    /* Bottom padding. A resizable panel reserves the grip's full grab box
     * instead of the usual pad, so the carets get a strip of their own: at
     * DBG_PAD_Y the grip reaches up into the last row and the two draw over
     * each other, which reads as a rendering fault rather than as a corner you
     * can pull. */
    int const foot_h = dbg_panel_has_grip(p) ? DBG_GRIP_HIT : DBG_PAD_Y;
    struct ToriRSChromeRect clip;
    int content_w = 0;
    int content_h = 0;
    int content_top;
    int content_bot;
    int overflow;
    int bar_w;
    int row_y;
    int widget;
    /** The pinned tab strip, and the band it reserves above the rows. */
    int strip = -1;
    int strip_h = 0;
    int strip_y;

    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        int w;
        if( !dbg_widget_shown(ui, &ui->widgets[widget]) )
            continue;
        w = dbg_widget_width(ui, &ui->widgets[widget]);
        if( w > content_w )
            content_w = w;
        /*
         * The strip is PINNED, not scrolled: it sits between the header and the
         * rows, and the rows move under it. A strip counted as content would
         * scroll off the top of its own panel, and the tabs it holds are the
         * only way back to the rows that scrolled it away -- the same
         * unreachable-destination problem tab compression exists to avoid.
         */
        if( ui->widgets[widget].kind == TORIRS_CHROME_W_TABSTRIP && strip < 0 )
        {
            strip = widget;
            strip_h = dbg_widget_height(ui, &ui->widgets[widget]) + DBG_ROW_GAP;
            continue;
        }
        content_h += dbg_widget_height(ui, &ui->widgets[widget]) + DBG_ROW_GAP;
    }
    if( content_h > 0 )
        content_h -= DBG_ROW_GAP;
    if( p->title[0] )
    {
        int const tw = ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_MENU, ui->scale, p->title);
        if( tw > content_w )
            content_w = tw;
    }
    if( content_w < DBG_MIN_CONTENT_W )
        content_w = DBG_MIN_CONTENT_W;

    p->w = p->fixed_w > 0 ? p->fixed_w : content_w + 2 * DBG_PAD_X + 2 * edge;
    /* content_h is 0 for an empty panel, so this is also the empty case: the
     * header block, the pads and the bottom border. */
    p->h = p->fixed_h > 0 ? p->fixed_h
                          : head_h + DBG_PAD_Y + strip_h + content_h + foot_h + edge;

    /*
     * The scroll window, and whether there is anything to scroll.
     *
     * Held on the panel because the bar's draw, its hit test and its drag all
     * read them, and a view height recomputed per caller is a grip that lands
     * somewhere different depending on who asked. A content-sized panel is
     * exactly as tall as its rows, so `overflow` there is always false --
     * scrolling only ever engages under a hand-set or dragged height.
     */
    p->content_h = content_h;
    /* The strip's band is taken off the view, not off the content: it is chrome
     * above the scroll window, in the same category as the title bar. */
    p->view_h = p->h - head_h - DBG_PAD_Y - strip_h - foot_h - edge;
    if( p->view_h < 0 )
        p->view_h = 0;
    overflow = p->scrollable && content_h > p->view_h;
    if( !overflow )
    {
        p->scroll_y = 0;
    }
    else
    {
        /* Clamped here rather than at the wheel, so a panel that SHRANK -- by a
         * grip drag, a tab switch, a row being removed -- cannot be left
         * scrolled past content that is no longer there. */
        int const max_scroll = content_h - p->view_h;
        if( p->scroll_y > max_scroll )
            p->scroll_y = max_scroll;
        if( p->scroll_y < 0 )
            p->scroll_y = 0;
    }
    bar_w = overflow ? DBG_SCROLL_W : 0;

    clip.x = p->x;
    clip.y = p->y;
    clip.w = p->w;
    clip.h = p->h;
    /* Flat fill first either way: the parchment carries transparent pixels at
     * its edges, and tiling it straight onto whatever was behind the panel
     * would show the world through them. */
    dbg_push_rect(ui, p->x, p->y, p->w, p->h, th->panel_body, 1, clip);
    if( th->skin_panel_body && dbg_skin_has(ui, TORIRS_CHROME_SKIN_PANEL_BODY) )
        dbg_fill_tiled(
            ui, p->x, p->y, p->w, p->h, TORIRS_CHROME_SKIN_PANEL_BODY, ui->skin_tile_w,
            ui->skin_tile_h, clip);
    /* The frame goes on directly over the body, so the title bar and the rows
     * that follow all land INSIDE it -- and so the rounded corners have the
     * parchment behind them rather than the world. */
    if( dbg_panel_is_framed(ui, p) )
    {
        struct ToriRSChromeRect box;
        box.x = p->x;
        box.y = p->y;
        box.w = p->w;
        box.h = p->h;
        dbg_push_frame(ui, box, clip);
    }

    /*
     * The minimenu's chrome, on a window panel.
     *
     * Not "structurally the same" -- the same boxes at the same offsets, taken
     * from the same dbg_menu_layout dbg_build_menu uses (and emit_minimenu
     * before it): header bar, the body gap and separator rule under it, a
     * bottom rule, and a rail down each side starting below the separator. A
     * panel and a real game menu on screen together have to read as one
     * widget, and the two former deviations -- rails run the full height, and
     * a 1px outline around the outside -- were both visible as an edge the
     * minimenu does not have.
     *
     * The palette stays the window's own (`panel_*`) rather than the menu's:
     * the keys are separately themable so the flat developer look can keep a
     * legible grey title, and it is only the osrs theme that sets them to the
     * menu's values.
     */
    if( p->title[0] )
    {
        dbg_push_rect(
            ui,
            p->x + edge,
            head_y + DBG_RULE,
            p->w - 2 * edge,
            l.header_bar_h,
            th->panel_title_bg,
            1,
            clip);
        dbg_push_text(
            ui,
            p->x + edge + DBG_PX(2),
            head_y + DBG_PX(2) + ToriRSChrome_FontLineHeight(TORIRS_CHROME_FONT_MENU, ui->scale),
            p->title,
            th->panel_title_text,
            TORIRS_CHROME_FONT_MENU,
            0,
            clip);
        /* The rule closing the header, where the minimenu's separator sits. */
        dbg_push_rect(
            ui,
            p->x + edge,
            head_y + l.separator_y,
            p->w - 2 * edge,
            DBG_RULE,
            th->panel_border,
            1,
            clip);

        /*
         * Close, when the panel asked for it: the interfaces' own window X.
         *
         * ONE button. There was an Ok beside it wearing the checkbox tick, and
         * a green tick is the game's answer to a QUESTION -- it belongs against
         * a setting, not in a title bar, where it read as a second unlabelled
         * copy of the Save row two inches below it.
         *
         * Drawn from the panel's LIVE box rather than last_rect: last_rect is
         * written at the end of this same build, so reading it here draws
         * nothing at all on the frame a panel first appears -- which is every
         * frame, for a panel rebuilt from scratch. dbg_panel_close_box is
         * shared with the hit test off last_rect, so the two cannot drift.
         */
        if( p->closable )
        {
            struct ToriRSChromeRect live;
            struct ToriRSChromeRect box;

            live.x = p->x;
            live.y = p->y;
            live.w = p->w;
            live.h = p->h;
            box = dbg_panel_close_box(ui, p, live);
            if( box.w > 0 )
            {
                /*
                 * The hover is IN THE ART: the two baked images are one button
                 * lit from opposite corners, so the hovered one reads as
                 * pressed in. That is also why no accent outline goes over it
                 * -- a control with two hover indications reads as selected
                 * rather than as under the cursor.
                 */
                int const hot = dbg_point_in_rect(ui->hover_x, ui->hover_y, box);
                int const slot =
                    hot ? TORIRS_CHROME_SKIN_CLOSE_OVER : TORIRS_CHROME_SKIN_CLOSE;

                if( th->skin_dropdown && dbg_skin_has(ui, slot) )
                    dbg_push_sprite_box(ui, box.x, box.y, box.w, box.h, slot, clip);
                else
                {
                    /* No skin: a framed box in the theme's dismiss colour, so
                     * the way out exists on a build that baked no art rather
                     * than being invisible. The outline comes back with it --
                     * a flat box has no bevel to invert. */
                    dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->input_bg, 1, clip);
                    dbg_push_rect(
                        ui, box.x + DBG_RULE, box.y + DBG_RULE, box.w - 2 * DBG_RULE,
                        box.h - 2 * DBG_RULE, th->accent, 1, clip);
                    if( hot )
                        dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->accent, 0, clip);
                }
            }
        }
    }
    /* Bottom rule and the two side rails: inset a pixel, and running from the
     * separator down, exactly as dbg_build_menu draws them. A titleless panel
     * has no separator to start at, so its rails start at the top border
     * instead -- the menu never has that case, having always a title. */
    if( !dbg_panel_is_framed(ui, p) )
    {
        int const rail_y = p->title[0] ? l.separator_y : DBG_RULE;
        int const rail_h = p->h - rail_y - DBG_RULE;

        dbg_push_rect(
            ui,
            p->x + DBG_RULE,
            p->y + p->h - 2 * DBG_RULE,
            p->w - 2 * DBG_RULE,
            DBG_RULE,
            th->panel_border,
            1,
            clip);
        dbg_push_rect(
            ui, p->x + DBG_RULE, p->y + rail_y, DBG_RULE, rail_h, th->panel_border, 1, clip);
        dbg_push_rect(
            ui,
            p->x + p->w - 2 * DBG_RULE,
            p->y + rail_y,
            DBG_RULE,
            rail_h,
            th->panel_border,
            1,
            clip);
    }
    if( p->resizable )
        dbg_push_grip(ui, p, clip);

    strip_y = p->y + head_h + DBG_PAD_Y;
    content_top = strip_y + strip_h;
    content_bot = p->y + p->h - foot_h - edge;
    p->content_y = content_top;

    /* The strip, before the scroll window is set up: it is drawn against the
     * panel's own clip, so the rows' tighter one never crops it. */
    if( strip >= 0 )
    {
        struct ToriRSChromeWidget* s = &ui->widgets[strip];
        struct ToriRSChromeRect strip_clip;

        s->x = p->x + edge + DBG_PAD_X;
        s->y = strip_y;
        s->w = p->w - 2 * edge - 2 * DBG_PAD_X;
        s->h = dbg_widget_height(ui, s);

        strip_clip.x = s->x;
        strip_clip.y = s->y;
        strip_clip.w = s->w;
        strip_clip.h = s->h;
        dbg_push_tabstrip(
            ui,
            s,
            p->active_tab,
            ui->hover == strip ? dbg_tab_at(ui, s, ui->hover_x, ui->hover_y) : -1,
            strip_clip);
    }

    /* The bar sits in the content column's right-hand edge, above the footer,
     * and is drawn before the rows so a row's clip can exclude its column. */
    if( overflow )
    {
        struct ToriRSChromeRect bar;
        bar.x = p->x + p->w - edge - DBG_PAD_X - bar_w;
        bar.y = content_top;
        bar.w = bar_w;
        bar.h = p->view_h;
        if( bar.h > 0 )
            dbg_push_scrollbar(ui, bar, p->content_h, p->view_h, p->scroll_y, clip);
    }

    /* Rows are clipped to the content column, so an over-long label is cut at
     * the border instead of painting over it. A scrolling panel tightens that
     * to the scroll window itself: a row half off the top would otherwise paint
     * up into the header's padding, and one half off the bottom over the grip. */
    clip.x = p->x + edge + DBG_PAD_X;
    clip.y = overflow ? content_top : p->y + head_h;
    clip.w = p->w - 2 * edge - 2 * DBG_PAD_X - bar_w;
    clip.h = overflow ? content_bot - content_top : p->h - head_h - edge;
    if( clip.w < 0 )
        clip.w = 0;
    if( clip.h < 0 )
        clip.h = 0;

    row_y = content_top - p->scroll_y;
    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        struct ToriRSChromeWidget* w = &ui->widgets[widget];
        int row_h;
        int row_x;
        int hovered;

        /* Not shown: no space, no draw -- and no stale hit box, or the widget
         * would keep taking clicks meant for the row drawn where it was. The
         * measuring loop above skipped it too; skipping in only one of the
         * two draws every later row across the bottom border. */
        if( !dbg_widget_shown(ui, w) )
        {
            w->x = 0;
            w->y = 0;
            w->w = 0;
            w->h = 0;
            continue;
        }
        /* Already placed and drawn above the scroll window, and it must not
         * take a row's worth of space here as well. */
        if( widget == strip )
            continue;
        row_h = dbg_widget_height(ui, w);
        row_x = p->x + edge + DBG_PAD_X;
        hovered = ui->hover == widget;

        w->x = row_x;
        w->y = row_y;
        w->w = p->w - 2 * edge - 2 * DBG_PAD_X - bar_w;
        w->h = row_h;

        /*
         * A scrolled panel steps PAST rows outside the window rather than
         * stopping at them: the rows below one that is off the top are the
         * ones the user scrolled down to see, so `break` would empty the panel.
         * The hit box is zeroed for the same reason the dropped-row case
         * zeroes it -- an invisible row must not take clicks -- and the loop
         * still advances row_y so the rows that ARE in view land correctly.
         */
        if( overflow && (row_y + row_h <= content_top || row_y >= content_bot) )
        {
            w->w = 0;
            w->h = 0;
            row_y += row_h + DBG_ROW_GAP;
            continue;
        }

        /*
         * A row past the bottom of a hand-sized panel is GONE, not merely
         * clipped.
         *
         * The clip rect alone would stop it being drawn and leave its hit box
         * where it was, so the panel would keep taking clicks on rows nobody
         * can see -- the worst version of this, because it is invisible until
         * someone toggles a checkbox they did not know was under the cursor.
         * Zeroing the box takes the row out of the hit test too, and `break`
         * because rows only ever go downwards from here.
         *
         * Measured against the footer rather than the clip rect so a surviving
         * row cannot run under the grip. On a content-sized panel the last row
         * ends exactly on this line, so nothing is ever dropped there -- it is
         * only a hand-sized height that can cut rows off.
         */
        if( !overflow && row_y + row_h > content_bot )
        {
            for( ; widget >= 0; widget = ui->widgets[widget].next )
            {
                ui->widgets[widget].w = 0;
                ui->widgets[widget].h = 0;
            }
            break;
        }

        switch( w->kind )
        {
        case TORIRS_CHROME_W_LABEL:
            dbg_push_text(
                ui,
                row_x,
                dbg_row_text_baseline(ui, row_y, row_h),
                w->text,
                w->color ? w->color : th->text,
                ui->theme.font_row,
                0,
                clip);
            break;

        case TORIRS_CHROME_W_SEPARATOR:
            dbg_push_rect(
                ui, row_x, row_y + row_h / 2, w->w, DBG_RULE, th->separator, 1, clip);
            break;

        case TORIRS_CHROME_W_MODELVIEW:
        {
            /* The box always draws, sprite or not: an empty bordered well says
             * "preview goes here", where nothing at all reads as a layout
             * hole. The sprite prim carries the scene id itself; the frame
             * translator uses it in place of the skin mapping. */
            dbg_push_rect(
                ui, row_x, row_y, w->view_w + 2 * DBG_RULE, w->view_h + 2 * DBG_RULE,
                th->input_bg, 1, clip);
            dbg_push_rect(
                ui, row_x, row_y, w->view_w + 2 * DBG_RULE, w->view_h + 2 * DBG_RULE,
                ui->focus == widget ? th->input_border_focus : th->input_border, 0, clip);
            if( w->view_scene_id > 0 )
            {
                struct ToriRSChromePrim* p2 = dbg_prim_push(ui);
                if( p2 )
                {
                    p2->kind = TORIRS_CHROME_PRIM_SPRITE;
                    p2->x = row_x + DBG_RULE;
                    p2->y = row_y + DBG_RULE;
                    p2->sprite_scene_id = w->view_scene_id;
                    /* Clip to the well, not the panel: the host's render may
                     * be larger than the box. */
                    p2->clip.x = row_x + DBG_RULE;
                    p2->clip.y = row_y + DBG_RULE;
                    p2->clip.w = w->view_w;
                    p2->clip.h = w->view_h;
                }
            }
            break;
        }

        case TORIRS_CHROME_W_CHECKBOX:
        {
            int const box_y = row_y + (row_h - DBG_CHECK_SIZE) / 2;
            int const mark = ToriRSChrome_CheckSlot(ui->check_style, w->checked);
            /*
             * The game's own art, not a box with a blob in it.
             *
             * Every boolean in this client's interfaces is a pair of sprites
             * -- there is no DRAWN checkbox anywhere in the cache to imitate.
             * There are two such pairs, and which one this instance wears is
             * ToriRSChrome_SetCheckStyle's answer: the settings page's tick
             * and cross, or the journals' bordered well. Note that the tick
             * style's OFF is a red cross and not an ABSENCE -- an unticked box
             * says "nothing here yet", a red cross says "off", and the second
             * is what a settings row means. The well style says it the other
             * way, which is why the two are a choice and not a fallback.
             */
            if( th->skin_dropdown && dbg_skin_has(ui, mark) )
            {
                dbg_push_sprite_box(
                    ui, row_x, box_y, DBG_CHECK_SIZE, DBG_CHECK_SIZE, mark, clip);
            }
            else
            {
                dbg_push_rect(
                    ui, row_x, box_y, DBG_CHECK_SIZE, DBG_CHECK_SIZE, th->input_bg, 1, clip);
                dbg_push_rect(
                    ui,
                    row_x,
                    box_y,
                    DBG_CHECK_SIZE,
                    DBG_CHECK_SIZE,
                    hovered ? th->accent : th->check_box,
                    0,
                    clip);
                if( w->checked )
                    dbg_push_rect(
                        ui,
                        row_x + DBG_PX(2),
                        box_y + DBG_PX(2),
                        DBG_CHECK_SIZE - DBG_PX(4),
                        DBG_CHECK_SIZE - DBG_PX(4),
                        th->check_mark,
                        1,
                        clip);
            }
            dbg_push_text(
                ui,
                row_x + DBG_CHECK_SIZE + DBG_CHECK_GAP,
                dbg_row_text_baseline(ui, row_y, row_h),
                w->label,
                hovered ? th->accent : (w->color ? w->color : th->text),
                ui->theme.font_row,
                0,
                clip);
            break;
        }

        case TORIRS_CHROME_W_LISTROW:
        {
            /* Right-to-left: the switch is pinned to the row's right edge, the
             * settings affordance sits inside it, and the name gets what is
             * left. Pinned rather than flowed so a column of rows lines its
             * controls up regardless of how long the names are. */
            int const tog_x = row_x + w->w - (w->row_locked ? 0 : DBG_TOGGLE_W);
            int const tog_y = row_y + (row_h - DBG_TOGGLE_H) / 2;
            int const icon_x = tog_x - DBG_ROW_ICON_GAP - DBG_ROW_ICON;
            int const icon_y = row_y + (row_h - DBG_ROW_ICON) / 2;
            int const name_w =
                (w->row_action ? icon_x : tog_x) - DBG_ROW_NAME_GAP - row_x;
            struct ToriRSChromeRect name_clip = clip;

            /* The name, clipped to its own column so a long one stops at the
             * furniture instead of running under it. */
            if( name_clip.w > name_w )
                name_clip.w = name_w > 0 ? name_w : 0;
            dbg_push_text(
                ui,
                row_x,
                dbg_row_text_baseline(ui, row_y, row_h),
                w->label,
                hovered ? th->accent : (w->color ? w->color : th->text),
                ui->theme.font_row,
                0,
                name_clip);

            /* The settings affordance: three dots in a settings-field well.
             *
             * Dots rather than a gear because the baked faces carry no glyph
             * for one and the display list's only shape is a rect -- three of
             * them read as "there is more here", which is the whole message.
             *
             * The well is dbg_push_field_chrome, the SAME box every other
             * pressable thing in this chrome wears, not a flat rect with a
             * border. It used to be the latter, and a black box edged in brown
             * beside a column of tiled, twice-framed fields is the one piece
             * on the row that did not read as part of the interface. */
            if( w->row_action )
            {
                struct ToriRSChromeRect well;
                well.x = icon_x;
                well.y = icon_y;
                well.w = DBG_ROW_ICON;
                well.h = DBG_ROW_ICON;
                dbg_push_field_chrome(ui, well, clip);
                if( hovered )
                    dbg_push_rect(
                        ui, icon_x, icon_y, DBG_ROW_ICON, DBG_ROW_ICON, th->accent, 0, clip);
                for( int d = 0; d < 3; d++ )
                    dbg_push_rect(
                        ui,
                        icon_x + DBG_DOT_INSET + d * DBG_DOT_PITCH,
                        icon_y + DBG_ROW_ICON / 2 - DBG_RULE,
                        DBG_DOT,
                        DBG_DOT,
                        th->text_dim,
                        1,
                        clip);
            }

            /* A locked row has no second state, so it has no switch: nothing
             * below draws and nothing above reserved the column for it. */
            if( w->row_locked )
                break;

            /* The switch: a well with the knob at the end its state names, lit
             * in the interfaces' own on/off green when it is on. */
            int const tog_mark = ToriRSChrome_CheckSlot(ui->check_style, w->checked);
            /*
             * The same tick/cross a checkbox wears, not a sliding switch.
             *
             * A slider is a foreign idiom here -- this game has no such
             * control anywhere -- and the row is asking the same on/off
             * question a settings checkbox asks, so it should look like one.
             * The 24x12 hit box is kept and the art right-aligned inside it at
             * whatever size its style is: the slack falls between the sprite
             * and the row's settings affordance, where nothing is drawn anyway.
             */
            if( th->skin_dropdown && dbg_skin_has(ui, tog_mark) )
            {
                dbg_push_sprite_box(
                    ui,
                    tog_x + DBG_TOGGLE_W - DBG_CHECK_SIZE,
                    row_y + (row_h - DBG_CHECK_SIZE) / 2,
                    DBG_CHECK_SIZE,
                    DBG_CHECK_SIZE,
                    tog_mark,
                    clip);
            }
            else
            {
                dbg_push_rect(
                    ui, tog_x, tog_y, DBG_TOGGLE_W, DBG_TOGGLE_H, th->input_bg, 1, clip);
                dbg_push_rect(
                    ui, tog_x, tog_y, DBG_TOGGLE_W, DBG_TOGGLE_H,
                    hovered ? th->accent : th->check_box, 0, clip);
                dbg_push_rect(
                    ui,
                    w->checked ? tog_x + DBG_TOGGLE_W - DBG_TOGGLE_H + DBG_PX(1)
                               : tog_x + DBG_PX(1),
                    tog_y + DBG_PX(1),
                    DBG_TOGGLE_H - DBG_PX(2),
                    DBG_TOGGLE_H - DBG_PX(2),
                    w->checked ? th->check_mark : th->scroll_grip,
                    1,
                    clip);
            }
            break;
        }

        /*
         * A button, and the command row that is the same thing by another name.
         *
         * Both wear the settings field box with the caption centred in it,
         * which is how the reference draws a pressable row -- script_3850's own
         * Save is exactly this shape. A MENUITEM in a WINDOW panel is a
         * command, so it is drawn as one; the menu-style panel draws its own
         * rows elsewhere (dbg_build_menu) and is untouched by this.
         *
         * The box is DBG_LABEL_W wide -- the label column's width, not the
         * caption's. Caption-width was the old behaviour and it makes a column
         * of buttons ragged, with Save and Revert two different sizes because
         * their words are; the label column is the one measure in the panel
         * every other row is already aligned to.
         */
        case TORIRS_CHROME_W_MENUITEM:
        case TORIRS_CHROME_W_BUTTON:
        {
            char const* caption = w->text[0] ? w->text : w->label;
            int const box_w = DBG_LABEL_W < w->w ? DBG_LABEL_W : w->w;
            int const pressed = ui->press == widget && hovered;
            int const nudge = pressed ? DBG_RULE : 0;
            int const caption_w = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, caption);
            struct ToriRSChromeRect box;

            box.x = row_x;
            box.y = row_y;
            box.w = box_w;
            box.h = row_h;
            dbg_push_field_chrome(ui, box, clip);
            if( hovered )
                dbg_push_rect(ui, row_x, row_y, box_w, row_h, th->accent, 0, clip);
            dbg_push_text(
                ui,
                /* A pressed button shifts its caption a pixel down and right --
                 * the whole of what makes the press read as a press. */
                row_x + (box_w - caption_w) / 2 + nudge,
                dbg_row_text_baseline(ui, row_y, row_h) + nudge,
                caption,
                hovered ? th->accent : (w->color ? w->color : th->text),
                ui->theme.font_row,
                0,
                dbg_rect_clip(clip, box));
            /* The hit box is the drawn box, not the whole row: the empty strip
             * beside a button must not press it. */
            w->w = box_w;
            break;
        }

        /* TORIRS_CHROME_W_TABSTRIP is not reachable here: the panel's one strip is
         * placed and drawn above, and a second one is skipped as a row with
         * nothing to draw rather than laid out as a second strip. */

        case TORIRS_CHROME_W_DROPDOWN:
        {
            int const label_w = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->label);
            int const box_x = row_x + dbg_row_box_offset(ui, w);
            int const box_y = row_y + dbg_row_box_top(ui, w);
            int const box_h = row_h - dbg_row_box_top(ui, w);
            int const box_w = row_x + w->w - box_x;
            int const open = ui->dropdown_open == widget;
            char const* shown = (w->selected >= 0 && w->selected < w->option_count)
                                    ? w->options[w->selected]
                                    : "";
            struct ToriRSChromeRect box;

            if( label_w > 0 )
                dbg_push_text(
                    ui,
                    row_x,
                    dbg_row_text_baseline(ui, row_y, DBG_ROW_H),
                    w->label,
                    w->color ? w->color : th->text_dim,
                    ui->theme.font_row,
                    0,
                    clip);
            if( box_w <= 0 )
                break;

            box.x = box_x;
            box.y = box_y;
            box.w = box_w;
            box.h = box_h;
            dbg_push_dropdown_button(ui, box, shown, open, hovered, clip);
            break;
        }

        case TORIRS_CHROME_W_TEXTINPUT:
        {
            int const label_w = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->label);
            int const box_x = row_x + dbg_row_box_offset(ui, w);
            int const box_y = row_y + dbg_row_box_top(ui, w);
            int const box_h = row_h - dbg_row_box_top(ui, w);
            int const box_w = row_x + w->w - box_x;
            int const focused = ui->focus == widget;
            struct ToriRSChromeRect inner;
            int caret_px;
            int scroll = 0;
            int text_x;

            if( label_w > 0 )
                dbg_push_text(
                    ui,
                    row_x,
                    dbg_row_text_baseline(ui, row_y, DBG_ROW_H),
                    w->label,
                    w->color ? w->color : th->text_dim,
                    ui->theme.font_row,
                    0,
                    clip);
            if( box_w <= 0 )
                break;

            {
                /* The same box a dropdown button wears -- see
                 * dbg_push_field_chrome. Focus is a third outline INSIDE the
                 * inset rather than a recolour of it: the reference's frame is
                 * two fixed colours, and swapping one for yellow would be a
                 * focused field that no longer reads as the same control. */
                struct ToriRSChromeRect field;
                field.x = box_x;
                field.y = box_y;
                field.w = box_w;
                field.h = box_h;
                dbg_push_field_chrome(ui, field, clip);
                if( focused )
                    dbg_push_rect(
                        ui,
                        box_x + DBG_RULE,
                        box_y + DBG_RULE,
                        box_w - 2 * DBG_RULE,
                        box_h - 2 * DBG_RULE,
                        th->input_border_focus,
                        0,
                        clip);
            }

            /* Scroll the content so the caret stays inside the box — the
             * classic single-line edit behaviour. */
            inner.x = box_x + DBG_INPUT_PAD_X;
            inner.y = box_y + DBG_RULE;
            inner.w = box_w - 2 * DBG_INPUT_PAD_X;
            inner.h = box_h - 2 * DBG_RULE;
            if( inner.w < 0 )
                inner.w = 0;
            caret_px = dbg_measure_prefix(ui->theme.font_row, ui->scale, w->text, w->caret);
            if( caret_px > inner.w )
                scroll = caret_px - inner.w;
            text_x = inner.x - scroll;

            dbg_push_text(
                ui,
                text_x,
                dbg_row_text_baseline(ui, box_y, box_h),
                w->text,
                th->input_text,
                ui->theme.font_row,
                0,
                inner);
            if( focused && ui->caret_visible )
                dbg_push_rect(
                    ui,
                    text_x + caret_px,
                    box_y + DBG_FIELD_INSET,
                    DBG_RULE,
                    box_h - 2 * DBG_FIELD_INSET,
                    th->input_text,
                    1,
                    inner);
            break;
        }

        case TORIRS_CHROME_W_TEXTAREA:
        {
            /*
             * The cache's own multiline field, as ~script7210 builds one: a
             * flat 0x372e22 body -- NOT the tradebacking a one-line field
             * wears, because a mostly-empty tiled box reads as a hole in the
             * panel -- inside ~script715's two-colour frame, with the caption
             * on a line of its own above rather than in the label column.
             */
            struct ToriRSChromeRect const box = dbg_textarea_box(ui, w);
            struct ToriRSChromeRect const inner = dbg_textarea_inner(ui, box);
            struct ToriRSChromeRect const text_clip = dbg_rect_clip(clip, inner);
            int const focused = ui->focus == widget;
            int const rows = dbg_textarea_rows(w);
            int const line_h = dbg_textarea_line_h(ui);
            int const ascent = ToriRSChrome_FontLineHeight(ui->theme.font_row, ui->scale);
            int starts[DBG_TEXTAREA_LINES_MAX];
            int lens[DBG_TEXTAREA_LINES_MAX];
            int count;

            if( w->label[0] )
                dbg_push_text(
                    ui,
                    row_x,
                    dbg_row_text_baseline(ui, row_y, DBG_ROW_H),
                    w->label,
                    w->color ? w->color : th->text_dim,
                    ui->theme.font_row,
                    0,
                    clip);
            if( box.w <= 0 || inner.w <= 0 )
                break;

            dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->textarea_bg, 1, clip);
            dbg_push_rect(ui, box.x, box.y, box.w, box.h, th->dropdown_border, 0, clip);
            dbg_push_rect(
                ui,
                box.x + DBG_RULE,
                box.y + DBG_RULE,
                box.w - 2 * DBG_RULE,
                box.h - 2 * DBG_RULE,
                focused ? th->input_border_focus : th->dropdown_border_inner,
                0,
                clip);

            count = dbg_textarea_wrap(ui, w, starts, lens);
            for( int li = w->scroll; li < count && li - w->scroll < rows; li++ )
            {
                char const* line = dbg_wrap_line(ui, w->text + starts[li], lens[li]);
                if( !line )
                    break;
                dbg_push_text(
                    ui,
                    inner.x,
                    inner.y + (li - w->scroll) * line_h + ascent,
                    line,
                    th->input_text,
                    ui->theme.font_row,
                    0,
                    text_clip);
            }
            if( focused && ui->caret_visible )
            {
                int const cl = dbg_textarea_line_of(starts, lens, count, w->caret);
                if( cl >= w->scroll && cl - w->scroll < rows )
                    dbg_push_rect(
                        ui,
                        inner.x +
                            dbg_textarea_col_px(ui, w, starts[cl], w->caret - starts[cl]),
                        inner.y + (cl - w->scroll) * line_h,
                        DBG_RULE,
                        line_h,
                        th->input_text,
                        1,
                        text_clip);
            }
            break;
        }

        case TORIRS_CHROME_W_COLORPICK:
        {
            /* The same labelled field a text row wears, with a swatch sitting
             * inside its box at the left. The swatch is the popup's handle and
             * the rest of the box is an ordinary text field, so the row offers
             * both ways to say a colour without either one being hidden behind
             * a mode. */
            int const label_w = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->label);
            int const box_x = row_x + dbg_row_box_offset(ui, w);
            int const box_y = row_y + dbg_row_box_top(ui, w);
            int const box_h = row_h - dbg_row_box_top(ui, w);
            int const box_w = row_x + w->w - box_x;
            int const focused = ui->focus == widget;
            int const open = ui->colorpick_open == widget;
            int const sw_x = box_x + DBG_PX(3);
            int const sw_y = box_y + (box_h - DBG_SWATCH) / 2;
            struct ToriRSChromeRect inner;
            int text_x;
            int caret_px;

            if( label_w > 0 )
                dbg_push_text(
                    ui,
                    row_x,
                    dbg_row_text_baseline(ui, row_y, DBG_ROW_H),
                    w->label,
                    w->color ? w->color : th->text_dim,
                    ui->theme.font_row,
                    0,
                    clip);
            if( box_w <= 0 )
                break;

            {
                struct ToriRSChromeRect field;
                field.x = box_x;
                field.y = box_y;
                field.w = box_w;
                field.h = box_h;
                dbg_push_field_chrome(ui, field, clip);
                if( focused )
                    dbg_push_rect(
                        ui,
                        box_x + DBG_RULE,
                        box_y + DBG_RULE,
                        box_w - 2 * DBG_RULE,
                        box_h - 2 * DBG_RULE,
                        th->input_border_focus,
                        0,
                        clip);
            }

            /* The sample, framed so a colour close to the field's own black
             * still reads as a swatch rather than as a hole in the box. An
             * open picker outlines it in the accent, which is the only thing
             * on the row that says which field the popup belongs to. */
            dbg_push_rect(
                ui, sw_x, sw_y, DBG_SWATCH, DBG_SWATCH,
                ToriRSChrome_Hsl16ToRgb(w->selected), 1, clip);
            dbg_push_rect(
                ui, sw_x, sw_y, DBG_SWATCH, DBG_SWATCH,
                (open || hovered) ? th->accent : th->dropdown_border_inner, 0, clip);

            inner.x = sw_x + DBG_SWATCH + DBG_SWATCH_GAP;
            inner.y = box_y + DBG_RULE;
            inner.w = box_x + box_w - DBG_INPUT_PAD_X - inner.x;
            inner.h = box_h - 2 * DBG_RULE;
            if( inner.w < 0 )
                inner.w = 0;
            text_x = inner.x;
            caret_px = dbg_measure_prefix(ui->theme.font_row, ui->scale, w->text, w->caret);
            if( caret_px > inner.w )
                text_x -= caret_px - inner.w;

            dbg_push_text(
                ui,
                text_x,
                dbg_row_text_baseline(ui, box_y, box_h),
                w->text,
                th->input_text,
                ui->theme.font_row,
                0,
                inner);
            if( focused && ui->caret_visible )
                dbg_push_rect(
                    ui,
                    text_x + caret_px,
                    box_y + DBG_FIELD_INSET,
                    DBG_RULE,
                    box_h - 2 * DBG_FIELD_INSET,
                    th->input_text,
                    1,
                    inner);
            break;
        }

        default:
            break;
        }

        /*
         * Trim a straddling row's hit box to the part of it you can see.
         *
         * Only the scrolling case can produce one: the row is drawn clipped, so
         * without this its clickable area would extend past the fold and a
         * click on the footer strip -- or on the resize grip -- would land on
         * the half-row above or below it.
         */
        if( overflow )
        {
            int const top = w->y < content_top ? content_top : w->y;
            int const bot = w->y + w->h > content_bot ? content_bot : w->y + w->h;
            w->y = top;
            w->h = bot - top > 0 ? bot - top : 0;
        }
        row_y += row_h + DBG_ROW_GAP;
    }
}

/*
 * A menu panel: the minimenu's chrome and geometry (see dbg_menu_layout),
 * rows top-to-bottom.
 */
static void
dbg_build_menu(struct ToriRSChrome* ui, struct ToriRSChromePanel* p)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    struct DbgMenuLayout const l =
        dbg_menu_layout(ToriRSChrome_FontLineBox(TORIRS_CHROME_FONT_MENU, ui->scale), ui->scale);
    struct ToriRSChromeRect clip;
    int content_w = 0;
    int rows = 0;
    int widget;
    int row;

    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        int const w =
            ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_MENU, ui->scale, ui->widgets[widget].text);
        if( w > content_w )
            content_w = w;
        rows++;
    }
    if( p->title[0] )
    {
        int const tw = ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_MENU, ui->scale, p->title);
        if( tw > content_w )
            content_w = tw;
    }

    p->w = p->fixed_w > 0 ? p->fixed_w : content_w + l.width_pad;
    p->h = rows * l.row_stride + l.chrome_h;

    clip.x = p->x;
    clip.y = p->y;
    clip.w = p->w;
    clip.h = p->h;

    /* Body, black title bar, separator, bottom rule, side rails — the same
     * six boxes emit_minimenu draws, in the same order. */
    dbg_push_rect(ui, p->x, p->y, p->w, p->h, th->menu_body, 1, clip);
    dbg_push_rect(
        ui,
        p->x + DBG_RULE,
        p->y + DBG_RULE,
        p->w - 2 * DBG_RULE,
        l.header_bar_h,
        th->menu_chrome,
        1,
        clip);
    dbg_push_rect(
        ui,
        p->x + DBG_RULE,
        p->y + l.separator_y,
        p->w - 2 * DBG_RULE,
        DBG_RULE,
        th->menu_chrome,
        1,
        clip);
    dbg_push_rect(
        ui,
        p->x + DBG_RULE,
        p->y + p->h - 2 * DBG_RULE,
        p->w - 2 * DBG_RULE,
        DBG_RULE,
        th->menu_chrome,
        1,
        clip);
    dbg_push_rect(
        ui,
        p->x + DBG_RULE,
        p->y + l.separator_y,
        DBG_RULE,
        p->h - l.border_inset,
        th->menu_chrome,
        1,
        clip);
    dbg_push_rect(
        ui,
        p->x + p->w - 2 * DBG_RULE,
        p->y + l.separator_y,
        DBG_RULE,
        p->h - l.border_inset,
        th->menu_chrome,
        1,
        clip);
    dbg_push_text(
        ui,
        p->x + DBG_PX(3),
        p->y + DBG_PX(2) + ToriRSChrome_FontLineHeight(TORIRS_CHROME_FONT_MENU, ui->scale),
        p->title,
        th->menu_body,
        TORIRS_CHROME_FONT_MENU,
        0,
        clip);

    row = 0;
    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        struct ToriRSChromeWidget* w = &ui->widgets[widget];
        int const baseline = p->y + row * l.row_stride + l.option_base_y;
        int const hovered = ui->hover == widget;

        /* The hit box the reference minimenu uses, so a row picked here is the
         * row drawn here. */
        w->x = p->x;
        w->y = baseline - l.hover_above;
        w->w = p->w;
        w->h = l.hover_above + l.hover_below;

        dbg_push_text(
            ui,
            p->x + DBG_PX(3),
            baseline,
            w->text,
            hovered ? th->menu_hover_text : (w->color ? w->color : th->menu_text),
            TORIRS_CHROME_FONT_MENU,
            1,
            clip);
        row++;
    }
}

static void
dbg_build_dropdown_list(struct ToriRSChrome* ui);
static struct ToriRSChromeRect
dbg_dropdown_rect(struct ToriRSChrome const* ui);
static void
dbg_build_colorpick_popup(struct ToriRSChrome* ui);

/*
 * The File/Edit bar: one horizontal row of menu titles across the top.
 *
 * Chrome-wise it is the minimenu header stretched across the screen -- black
 * strip, menu-face titles, accent on hover -- and each title is a menu-mode
 * dropdown whose option list opens beneath it, reusing the popup machinery
 * wholesale rather than growing a second popup implementation.
 */
static void
dbg_build_menubar(struct ToriRSChrome* ui, struct ToriRSChromePanel* p)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    int const line = ToriRSChrome_FontLineBox(TORIRS_CHROME_FONT_MENU, ui->scale);
    struct ToriRSChromeRect clip;
    int pen_x;
    int widget;
    int content_w = 0;

    p->h = line + DBG_PX(4);
    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        struct ToriRSChromeWidget const* w = &ui->widgets[widget];
        if( w->hidden )
            continue;
        content_w +=
            ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_MENU, ui->scale, w->text) + 2 * DBG_PAD_X;
    }
    p->w = p->fixed_w > 0 ? p->fixed_w : content_w + 2 * DBG_RULE;

    clip.x = p->x;
    clip.y = p->y;
    clip.w = p->w;
    clip.h = p->h;
    dbg_push_rect(ui, p->x, p->y, p->w, p->h, th->menu_chrome, 1, clip);
    /* The border. In the body colour, not panel_border: the bar's fill is the
     * chrome black, and in the osrs theme panel_border is black too -- a
     * black-on-black outline is the "missing border" this line exists to fix.
     * The body brown is the one colour the minimenu already pairs with its
     * black header, so the bar reads as the same chrome family. */
    dbg_push_rect(ui, p->x, p->y, p->w, p->h, th->menu_body, 0, clip);

    pen_x = p->x + DBG_PAD_X;
    for( widget = p->first_widget; widget >= 0; widget = ui->widgets[widget].next )
    {
        struct ToriRSChromeWidget* w = &ui->widgets[widget];
        int tw;
        int const hovered = ui->hover == widget || ui->dropdown_open == widget;

        if( w->hidden )
        {
            w->x = 0;
            w->y = 0;
            w->w = 0;
            w->h = 0;
            continue;
        }
        tw = ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_MENU, ui->scale, w->text);
        w->x = pen_x - DBG_PAD_X / 2;
        w->y = p->y;
        w->w = tw + DBG_PAD_X;
        w->h = p->h;
        dbg_push_text(
            ui,
            pen_x,
            p->y + DBG_PX(2) + ToriRSChrome_FontLineHeight(TORIRS_CHROME_FONT_MENU, ui->scale),
            w->text,
            hovered ? th->accent : th->panel_title_text,
            TORIRS_CHROME_FONT_MENU,
            1,
            clip);
        pen_x += tw + 2 * DBG_PAD_X;
    }
}

int
ToriRSChrome_Build(struct ToriRSChrome* ui)
{
    assert(ui);
    /* The retained-mode payoff: a frame in which nothing moved does no
     * measurement, no layout and no display-list work at all. */
    if( !ui->dirty )
        return 0;

    ui->prim_count = 0;
    ui->overflow = 0;
    /* With the prims, not after them: every string in the pool is pointed at by
     * a prim of the list being thrown away, and a pool that outlived one build
     * would hand the next one's lines to prims still holding the last one's. */
    ui->wrap_used = 0;
    /* Bumped here rather than at the end, so it moves even on a build that
     * ends up emitting nothing -- a panel being hidden is a change a host
     * copying this list has to see. */
    ui->build_serial++;

    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct ToriRSChromePanel* p = &ui->panels[i];
        struct ToriRSChromeRect rect;

        if( !p->visible )
        {
            if( p->dirty )
            {
                /* It was on screen last frame; that area has to be repainted. */
                dbg_damage_add(ui, p->last_rect);
                p->last_rect.w = 0;
                p->last_rect.h = 0;
                p->dirty = 0;
            }
            continue;
        }

        if( p->style == TORIRS_CHROME_PANEL_MENU )
            dbg_build_menu(ui, p);
        else if( p->style == TORIRS_CHROME_PANEL_MENUBAR )
            dbg_build_menubar(ui, p);
        else
            dbg_build_window(ui, p);

        rect.x = p->x;
        rect.y = p->y;
        rect.w = p->w;
        rect.h = p->h;
        if( p->dirty )
        {
            /* Old bounds ∪ new bounds: a panel that shrank or moved leaves the
             * vacated pixels invalid too. */
            dbg_damage_add(ui, p->last_rect);
            dbg_damage_add(ui, rect);
            p->dirty = 0;
        }
        p->last_rect = rect;
    }

    /* After every panel, so the open list is never buried under one drawn
     * later. This is the whole reason the list is overlay state rather than a
     * child of the panel that owns the dropdown. */
    dbg_build_dropdown_list(ui);
    dbg_build_colorpick_popup(ui);

    ui->dirty = 0;
    return 1;
}

/** Screen rect of the open list, or a zero rect when none is open. */
static struct ToriRSChromeRect
dbg_dropdown_rect(struct ToriRSChrome const* ui)
{
    struct ToriRSChromeRect rect = { 0, 0, 0, 0 };
    struct ToriRSChromeWidget const* w;
    int rows;

    if( ui->dropdown_open < 0 )
        return rect;

    w = &ui->widgets[ui->dropdown_open];
    rows = w->option_count < TORIRS_CHROME_DROPDOWN_ROWS ? w->option_count
                                                         : TORIRS_CHROME_DROPDOWN_ROWS;
    if( rows <= 0 )
        return rect;

    if( w->menu_mode )
    {
        /* A menu list is sized to its rows, not to the title above it. */
        int widest = 0;
        for( int i = 0; i < w->option_count; i++ )
        {
            int const ow = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, w->options[i]);
            if( ow > widest )
                widest = ow;
        }
        rect.x = w->x;
        rect.w = widest + 2 * DBG_INPUT_PAD_X + DBG_PX(6);
    }
    else
    {
        rect.x = w->x + dbg_row_box_offset(ui, w);
        rect.w = w->x + w->w - rect.x;
    }
    rect.y = w->y + w->h;
    /* Rows plus a pad above and below, which is script_9114's `$int26 + 4`. */
    rect.h = rows * DBG_DROP_ROW_H + 2 * DBG_DROP_LIST_PAD;
    return rect;
}

/** Rows the open list shows at once. */
static int
dbg_dropdown_rows(struct ToriRSChrome const* ui)
{
    struct ToriRSChromeWidget const* w;

    assert(ui->dropdown_open >= 0);
    w = &ui->widgets[ui->dropdown_open];
    return w->option_count < TORIRS_CHROME_DROPDOWN_ROWS ? w->option_count
                                                         : TORIRS_CHROME_DROPDOWN_ROWS;
}

/**
 * The open list's scrollbar column, or a zero rect when the list does not
 * scroll.
 *
 * Inside the list rather than beside it, which is where the cache puts it --
 * interface 134's floating list is 100 wide and holds an 80-wide row column
 * next to a 16-wide bar. So the rows lose the width the bar takes, and a long
 * option is clipped at the bar instead of running under it.
 */
static struct ToriRSChromeRect
dbg_dropdown_scrollbar_rect(struct ToriRSChrome const* ui)
{
    struct ToriRSChromeRect bar = { 0, 0, 0, 0 };
    struct ToriRSChromeRect rect;
    struct ToriRSChromeWidget const* w;

    if( ui->dropdown_open < 0 )
        return bar;
    w = &ui->widgets[ui->dropdown_open];
    if( w->option_count <= dbg_dropdown_rows(ui) )
        return bar;

    rect = dbg_dropdown_rect(ui);
    if( rect.w <= 2 * DBG_DROP_LIST_PAD + DBG_SCROLL_W )
        return bar;
    bar.x = rect.x + rect.w - DBG_DROP_LIST_PAD - DBG_SCROLL_W;
    bar.y = rect.y + DBG_DROP_LIST_PAD;
    bar.w = DBG_SCROLL_W;
    bar.h = rect.h - 2 * DBG_DROP_LIST_PAD;
    return bar;
}

/** The open list's scroll position, its window and its content, in pixels --
 *  the units dbg_scroll_geom works in. */
static void
dbg_dropdown_scroll_px(struct ToriRSChrome const* ui, int* content, int* view, int* offset)
{
    struct ToriRSChromeWidget const* w = &ui->widgets[ui->dropdown_open];

    assert(content);
    assert(view);
    assert(offset);
    *content = w->option_count * DBG_DROP_ROW_H;
    *view = dbg_dropdown_rows(ui) * DBG_DROP_ROW_H;
    *offset = w->scroll * DBG_DROP_ROW_H;
}

static void
dbg_build_dropdown_list(struct ToriRSChrome* ui)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    struct ToriRSChromeWidget const* w;
    struct ToriRSChromeRect rect;
    struct ToriRSChromeRect clip;
    struct ToriRSChromeRect row_clip;
    struct ToriRSChromeRect bar;
    int rows;
    int row_x;
    int row_w;

    if( ui->dropdown_open < 0 )
        return;

    w = &ui->widgets[ui->dropdown_open];
    rect = dbg_dropdown_rect(ui);
    if( rect.w <= 0 || rect.h <= 0 )
        return;

    rows = dbg_dropdown_rows(ui);
    clip = rect;
    bar = dbg_dropdown_scrollbar_rect(ui);
    row_x = rect.x + DBG_DROP_LIST_PAD;
    row_w = rect.w - 2 * DBG_DROP_LIST_PAD - (bar.w > 0 ? bar.w : 0);
    row_clip = clip;
    if( row_clip.w > row_x + row_w - row_clip.x )
        row_clip.w = row_x + row_w - row_clip.x;
    if( row_clip.w < 0 )
        row_clip.w = 0;

    /*
     * The body, in whichever of the two the list is.
     *
     * A value list is the cache's dropdown: graphic_1040 tiled, a *lighter*
     * parchment than the graphic_297 a panel and a closed button wear. A menu
     * list is the minimenu, and keeps the minimenu's flat brown -- the two are
     * different widgets in the game and the split is here rather than in the
     * theme because no palette can turn one into the other.
     *
     * Flat fill first either way: the tile has transparent pixels at its edges.
     */
    dbg_push_rect(ui, rect.x, rect.y, rect.w, rect.h, th->menu_body, 1, clip);
    if( !w->menu_mode && th->skin_dropdown && dbg_skin_has(ui, TORIRS_CHROME_SKIN_DROPDOWN_BODY) )
        dbg_fill_tiled(
            ui, rect.x, rect.y, rect.w, rect.h, TORIRS_CHROME_SKIN_DROPDOWN_BODY, ui->skin_tile_w,
            ui->skin_tile_h, clip);
    /*
     * The edge, and it is NOT the same edge for the two widgets.
     *
     * A value list wears the box its own button wears -- script_3850's
     * near-black frame with the grey inset a pixel inside it -- because in the
     * reference the two are one control seen open: the button's frame runs
     * down into the list's, and a list edged in a single flat rule reads as a
     * tooltip that happened to appear under a field. A menu keeps the
     * minimenu's one-pixel chrome, which is the whole of the border that
     * widget has.
     */
    if( w->menu_mode )
        dbg_push_rect(ui, rect.x, rect.y, rect.w, rect.h, th->menu_chrome, 0, clip);
    else
    {
        dbg_push_rect(ui, rect.x, rect.y, rect.w, rect.h, th->dropdown_border, 0, clip);
        dbg_push_rect(
            ui, rect.x + DBG_RULE, rect.y + DBG_RULE, rect.w - 2 * DBG_RULE,
            rect.h - 2 * DBG_RULE, th->dropdown_border_inner, 0, clip);
    }

    for( int row = 0; row < rows; row++ )
    {
        int const index = w->scroll + row;
        int const y = rect.y + DBG_DROP_LIST_PAD + row * DBG_DROP_ROW_H;
        int const hovered = ui->dropdown_hover_row == row;
        int const baseline =
            y + (DBG_DROP_ROW_H - ToriRSChrome_FontLineBox(th->font_row, ui->scale)) / 2 +
            ToriRSChrome_FontLineHeight(th->font_row, ui->scale);
        int text_x = row_x + DBG_INPUT_PAD_X;

        if( index < 0 || index >= w->option_count )
            break;

        if( w->menu_mode )
        {
            /* The minimenu's row: left-aligned, and the cursor picks it out by
             * turning the text yellow rather than by lighting the row. */
            dbg_push_text(
                ui,
                text_x,
                baseline,
                w->options[index],
                hovered ? th->menu_hover_text : th->menu_text,
                th->font_row,
                0,
                row_clip);
            continue;
        }

        {
            /*
             * The bands alternate on the OPTION index, not on the screen row,
             * so scrolling slides the stripes with the text instead of leaving
             * them pinned to the window and strobing under it.
             */
            int trans = (index & 1) ? th->dropdown_band_trans_alt : th->dropdown_band_trans;
            int const shown_w =
                ToriRSChrome_MeasureText(th->font_row, ui->scale, w->options[index]);

            if( hovered )
                trans = th->dropdown_row_trans_hover;
            dbg_push_rect_trans(
                ui, row_x, y, row_w, DBG_DROP_ROW_H, th->dropdown_veil, 1, trans, clip);

            /* Centred, as `cc_settextalign(1, 1, 14)` centres every row of the
             * reference's list -- and left-aligned instead when the option is
             * too wide for the column, where centring would clip both ends. */
            if( shown_w < row_w - 2 * DBG_INPUT_PAD_X )
                text_x = row_x + (row_w - shown_w) / 2;
            /*
             * Every row in the SAME orange, the chosen one included.
             *
             * It used to go accent-yellow, which is a marker the reference's
             * list does not carry: the row the pointer is on is the only one
             * picked out there, and it is picked out by its BAND. A second
             * highlight in a second colour reads as two cursors, and the
             * chosen option is already stated by the button above the list.
             */
            dbg_push_text(
                ui,
                text_x,
                baseline,
                w->options[index],
                th->dropdown_text,
                th->font_row,
                0,
                row_clip);
        }
    }

    /* The bar the game draws beside every list that overflows. A long palette
     * with no bar gives no clue how far down it goes -- and no way to get
     * there but the wheel. */
    if( bar.w > 0 )
    {
        int content;
        int view;
        int offset;
        dbg_dropdown_scroll_px(ui, &content, &view, &offset);
        dbg_push_scrollbar(ui, bar, content, view, offset, clip);
    }
}

/* ---- the colour picker's popup -------------------------------------------
 *
 * Three bars, one per HSL16 axis, drawn as a run of cells sampled along the
 * axis they name. Bars rather than a hue/lightness square with a saturation
 * slider beside it -- the shape most RGB pickers use -- because the axes here
 * are not continuous: hue has 64 values and saturation has EIGHT, and a square
 * would render those eight as a smooth gradient the pointer cannot actually
 * land between. One bar per axis shows the resolution honestly, and a sweep
 * along it lands on cells the renderer can produce.
 *
 * Drawn after every panel for the same reason the dropdown list is: a popup
 * that is a child of its panel is a popup its neighbours can be drawn over.
 */

/** How many values each bar spans, by enum ToriRSChromeColorBar. */
static int
dbg_colorbar_steps(int bar)
{
    switch( bar )
    {
    case TORIRS_CHROME_COLORBAR_HUE:
        return TORIRS_CHROME_COLOR_HUE_STEPS;
    case TORIRS_CHROME_COLORBAR_SAT:
        return TORIRS_CHROME_COLOR_SAT_STEPS;
    case TORIRS_CHROME_COLORBAR_LUM:
        return TORIRS_CHROME_COLOR_LUM_STEPS;
    default:
        return 0;
    }
}

/** The value on `bar` of a packed HSL16. */
static int
dbg_colorbar_value(int hsl16, int bar)
{
    int hue;
    int sat;
    int lum;
    ToriRSChrome_Hsl16Split(hsl16, &hue, &sat, &lum);
    switch( bar )
    {
    case TORIRS_CHROME_COLORBAR_HUE:
        return hue;
    case TORIRS_CHROME_COLORBAR_SAT:
        return sat;
    case TORIRS_CHROME_COLORBAR_LUM:
        return lum;
    default:
        return 0;
    }
}

/** `hsl16` with `bar`'s axis moved to `value`, the other two left alone. */
static int
dbg_colorbar_with(int hsl16, int bar, int value)
{
    int hue;
    int sat;
    int lum;
    ToriRSChrome_Hsl16Split(hsl16, &hue, &sat, &lum);
    switch( bar )
    {
    case TORIRS_CHROME_COLORBAR_HUE:
        hue = value;
        break;
    case TORIRS_CHROME_COLORBAR_SAT:
        sat = value;
        break;
    case TORIRS_CHROME_COLORBAR_LUM:
        lum = value;
        break;
    default:
        return hsl16;
    }
    return ToriRSChrome_Hsl16Pack(hue, sat, lum);
}

/** The caption in front of one bar. One character so the column it costs is a
 *  column, not a margin. */
static char const*
dbg_colorbar_caption(int bar)
{
    switch( bar )
    {
    case TORIRS_CHROME_COLORBAR_HUE:
        return "H";
    case TORIRS_CHROME_COLORBAR_SAT:
        return "S";
    case TORIRS_CHROME_COLORBAR_LUM:
        return "L";
    default:
        return "";
    }
}

/** Screen rect of the open picker's popup, or a zero rect when none is open. */
static struct ToriRSChromeRect
dbg_colorpick_rect(struct ToriRSChrome const* ui)
{
    struct ToriRSChromeRect rect = { 0, 0, 0, 0 };
    struct ToriRSChromeWidget const* w;

    if( ui->colorpick_open < 0 )
        return rect;
    w = &ui->widgets[ui->colorpick_open];
    /* Aligned with the FIELD, not with the row: the popup belongs to the box
     * the swatch is in, and hanging it off the label column would leave it
     * floating under text that has nothing to do with it. */
    rect.x = w->x + dbg_row_box_offset(ui, w);
    rect.y = w->y + w->h;
    rect.w = DBG_COLORPOP_W;
    rect.h = 2 * DBG_COLORPOP_PAD + TORIRS_CHROME_COLORBAR_COUNT * DBG_COLORBAR_H +
             (TORIRS_CHROME_COLORBAR_COUNT - 1) * DBG_COLORBAR_GAP;
    /*
     * Pulled back inside the panel when it would hang off the right edge.
     *
     * The popup is wider than the field it belongs to -- deliberately, since
     * the lightness bar's 128 steps want the pixels -- and a floating box that
     * spills past the window it came out of reads as a rendering fault rather
     * than as a popup. Shifted rather than narrowed so the bars keep their
     * resolution, and never left of the panel, so a panel narrower than the
     * popup gets one that starts flush instead of hanging off the other side.
     */
    {
        struct ToriRSChromeRect const box = ui->panels[w->panel].last_rect;
        if( box.w > 0 && rect.x + rect.w > box.x + box.w - DBG_RULE )
            rect.x = box.x + box.w - DBG_RULE - rect.w;
        if( box.w > 0 && rect.x < box.x )
            rect.x = box.x;
    }
    return rect;
}

/** Screen rect of one bar's CELLS -- the caption column is outside it, so a
 *  click on the letter is not a click on the axis. */
static struct ToriRSChromeRect
dbg_colorpick_bar_rect(struct ToriRSChrome const* ui, int bar)
{
    struct ToriRSChromeRect rect = dbg_colorpick_rect(ui);
    struct ToriRSChromeRect out = { 0, 0, 0, 0 };
    int const cap_w = ToriRSChrome_MeasureText(ui->theme.font_row, ui->scale, "M") + DBG_PX(3);

    if( rect.w <= 0 || bar < 0 || bar >= TORIRS_CHROME_COLORBAR_COUNT )
        return out;
    out.x = rect.x + DBG_COLORPOP_PAD + cap_w;
    out.y = rect.y + DBG_COLORPOP_PAD + bar * (DBG_COLORBAR_H + DBG_COLORBAR_GAP);
    out.w = rect.x + rect.w - DBG_COLORPOP_PAD - out.x;
    out.h = DBG_COLORBAR_H;
    if( out.w < 0 )
        out.w = 0;
    return out;
}

/** Which bar (x, y) is on, or TORIRS_CHROME_COLORBAR_NONE. */
static int
dbg_colorpick_bar_at(struct ToriRSChrome const* ui, int x, int y)
{
    if( ui->colorpick_open < 0 )
        return TORIRS_CHROME_COLORBAR_NONE;
    for( int bar = 0; bar < TORIRS_CHROME_COLORBAR_COUNT; bar++ )
        if( dbg_point_in_rect(x, y, dbg_colorpick_bar_rect(ui, bar)) )
            return bar;
    return TORIRS_CHROME_COLORBAR_NONE;
}

/**
 * The value a pointer at `x` names on `bar`.
 *
 * Clamped rather than rejected, because this is also what a DRAG reads: once a
 * sweep has started on a bar it keeps that bar until the button comes up, and
 * a pointer that has run off the end should sit at the end rather than stop
 * reporting. That is what makes the two extremes reachable at all -- the last
 * cell of a bar is one cell wide and the first is against the frame.
 */
static int
dbg_colorpick_value_at(struct ToriRSChrome const* ui, int bar, int x)
{
    struct ToriRSChromeRect const rect = dbg_colorpick_bar_rect(ui, bar);
    int const steps = dbg_colorbar_steps(bar);
    int value;

    if( rect.w <= 0 || steps <= 0 )
        return 0;
    value = (x - rect.x) * steps / rect.w;
    if( value < 0 )
        value = 0;
    if( value > steps - 1 )
        value = steps - 1;
    return value;
}

/** Apply a press or a drag at `x` on `bar` to the open picker.
 *  @return 1 when the value moved. */
static int
dbg_colorpick_apply_at(struct ToriRSChrome* ui, int bar, int x)
{
    struct ToriRSChromeWidget* w;
    int next;

    if( ui->colorpick_open < 0 || bar == TORIRS_CHROME_COLORBAR_NONE )
        return 0;
    w = &ui->widgets[ui->colorpick_open];
    next = dbg_colorbar_with(w->selected, bar, dbg_colorpick_value_at(ui, bar, x));
    if( next == w->selected )
        return 0;
    w->selected = next;
    dbg_colorpick_write_text(w);
    dbg_dirty_widget(ui, ui->colorpick_open);
    /* The popup floats outside its panel's box, so the panel's own damage rect
     * does not cover it -- without this the bars keep the marker they had when
     * a host presents only the damaged region. */
    dbg_damage_add(ui, dbg_colorpick_rect(ui));
    return 1;
}

static void
dbg_build_colorpick_popup(struct ToriRSChrome* ui)
{
    struct ToriRSChromeTheme const* th = &ui->theme;
    struct ToriRSChromeWidget const* w;
    struct ToriRSChromeRect rect;
    struct ToriRSChromeRect clip;

    if( ui->colorpick_open < 0 )
        return;
    w = &ui->widgets[ui->colorpick_open];
    rect = dbg_colorpick_rect(ui);
    if( rect.w <= 0 || rect.h <= 0 )
        return;
    clip = rect;

    dbg_push_field_chrome(ui, rect, clip);

    for( int bar = 0; bar < TORIRS_CHROME_COLORBAR_COUNT; bar++ )
    {
        struct ToriRSChromeRect const box = dbg_colorpick_bar_rect(ui, bar);
        int const steps = dbg_colorbar_steps(bar);
        int const chosen = dbg_colorbar_value(w->selected, bar);
        /* One cell per value where the bar is wide enough, and one cell per
         * PIXEL where it is not -- a 128-step lightness ramp in an 80-pixel bar
         * would otherwise be 128 zero-width rects, which is 128 wasted prims
         * and nothing on screen. The hit test reads the full range either way,
         * so a narrow bar still reaches every value; only the picture is
         * coarser. */
        int const cells = box.w > 0 && steps > box.w ? box.w : steps;
        int mark_x = box.x;
        int mark_w = DBG_COLORBAR_MARK;

        if( box.w <= 0 || cells <= 0 )
            continue;

        dbg_push_text(
            ui,
            rect.x + DBG_COLORPOP_PAD,
            box.y + (box.h - ToriRSChrome_FontLineBox(th->font_row, ui->scale)) / 2 +
                ToriRSChrome_FontLineHeight(th->font_row, ui->scale),
            dbg_colorbar_caption(bar),
            th->text_dim,
            th->font_row,
            0,
            clip);

        for( int i = 0; i < cells; i++ )
        {
            int const x0 = box.x + box.w * i / cells;
            int const x1 = box.x + box.w * (i + 1) / cells;
            int const value = steps * i / cells;
            if( x1 <= x0 )
                continue;
            dbg_push_rect(
                ui, x0, box.y, x1 - x0, box.h,
                ToriRSChrome_Hsl16ToRgb(dbg_colorbar_with(w->selected, bar, value)), 1, clip);
        }

        /* The marker sits over the CELL the value occupies, sized to it where
         * the cells are wide (saturation's eight are nearly twenty pixels
         * each) and to a hairline where they are not. A fixed-width marker
         * would sit inside one of the eight and read as "somewhere around
         * here" on the one axis whose steps are individually visible. */
        {
            int const cell = chosen * cells / (steps > 0 ? steps : 1);
            int const x0 = box.x + box.w * cell / cells;
            int const x1 = box.x + box.w * (cell + 1) / cells;
            if( x1 - x0 > mark_w )
                mark_w = x1 - x0;
            mark_x = x0;
            if( mark_x + mark_w > box.x + box.w )
                mark_x = box.x + box.w - mark_w;
        }
        dbg_push_rect(ui, mark_x, box.y, mark_w, box.h, th->accent, 0, clip);
        dbg_push_rect(
            ui, mark_x + DBG_RULE, box.y + DBG_RULE, mark_w - 2 * DBG_RULE,
            box.h - 2 * DBG_RULE, th->dropdown_border, 0, clip);
    }
}

struct ToriRSChromePrim const*
ToriRSChrome_Prims(struct ToriRSChrome const* ui, int* out_count)
{
    if( out_count )
        *out_count = ui ? ui->prim_count : 0;
    return ui ? ui->prims : NULL;
}

int
ToriRSChrome_Damage(struct ToriRSChrome const* ui, struct ToriRSChromeRect* out)
{
    assert(ui);
    if( ui->damage.w <= 0 || ui->damage.h <= 0 )
        return 0;
    if( out )
        *out = ui->damage;
    return 1;
}

void
ToriRSChrome_DamageClear(struct ToriRSChrome* ui)
{
    assert(ui);
    ui->damage.x = 0;
    ui->damage.y = 0;
    ui->damage.w = 0;
    ui->damage.h = 0;
}

/* ---- input --------------------------------------------------------------- */

static int
dbg_point_in(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

/** Topmost panel under the point, or -1. Later panels are drawn later, so they win. */
static int
dbg_panel_at(struct ToriRSChrome const* ui, int x, int y)
{
    for( int i = ui->panel_count - 1; i >= 0; i-- )
    {
        struct ToriRSChromePanel const* p = &ui->panels[i];
        if( !p->visible || p->last_rect.w <= 0 )
            continue;
        if( dbg_point_in(x, y, p->last_rect.x, p->last_rect.y, p->last_rect.w, p->last_rect.h) )
            return i;
    }
    return -1;
}

/**
 * The title bar's box, off `last_rect`. Zero for a panel with no title.
 *
 * ONE answer, shared by the in-canvas header drag and by the OS window's drag
 * region. They are the same bar and they are handles for two different windows;
 * two copies of this arithmetic is how one of them ends up grabbing a band the
 * title is not drawn in.
 *
 * Off LAST_RECT rather than the live x/y for the drag's sake: mid-drag the
 * panel's own fields have already moved, and the bar that was grabbed is the
 * one that was drawn.
 */
static struct ToriRSChromeRect
dbg_panel_title_rect(struct ToriRSChrome const* ui, struct ToriRSChromePanel const* p)
{
    struct ToriRSChromeRect r = { 0, 0, 0, 0 };

    assert(ui);
    assert(p);
    if( !p->title[0] || p->last_rect.w <= 0 )
        return r;
    r.x = p->last_rect.x + DBG_RULE;
    r.y = p->last_rect.y + DBG_RULE;
    r.w = p->last_rect.w - 2 * DBG_RULE;
    /* Down to the BOTTOM of the black bar, borders included. dbg_build_window
     * draws that bar at the panel's inner edge -- p->y + `edge` -- so a handle
     * one line box tall measured from the outer edge stops short of it by the
     * frame's thickness on a framed panel: the grab band lands on the top
     * border and the bar's first few rows, and the rest of the bar, which is
     * the whole of what the user is aiming at, does nothing. */
    r.h = (dbg_panel_is_framed(ui, p) ? DBG_FRAME : DBG_RULE) - DBG_RULE +
          ToriRSChrome_FontLineBox(TORIRS_CHROME_FONT_MENU, ui->scale);
    return r;
}

/**
 * Panel whose HEADER BAR is under the point, or -1.
 *
 * The header is the drag handle, so this is deliberately narrower than
 * dbg_panel_at: only the title strip, and only on a panel that has a title to
 * strip. A menu-style panel is not draggable -- it is a popup that appears
 * where it was asked for and closes on the next click, so a handle on it would
 * be a handle on something with no persistent position to move.
 */
static int
dbg_panel_header_at(struct ToriRSChrome const* ui, int x, int y)
{
    for( int i = ui->panel_count - 1; i >= 0; i-- )
    {
        struct ToriRSChromePanel const* p = &ui->panels[i];

        if( !p->visible || p->last_rect.w <= 0 || p->style != TORIRS_CHROME_PANEL_WINDOW )
            continue;
        /* A filled panel has no position of its own to move: the next fill
         * puts it back at the origin, so a drag would be a title bar that
         * takes the cursor and gives nothing back. The window it fills is what
         * moves instead -- see ToriRSChrome_WindowDragRegion, which claims
         * this same bar for exactly the panels this skips. */
        if( p->filled )
            continue;
        if( dbg_point_in_rect(x, y, dbg_panel_title_rect(ui, p)) )
            return i;
    }
    return -1;
}

/**
 * Panel whose RESIZE GRIP is under the point, or -1.
 *
 * Hit off `last_rect`, not the live x/y: mid-drag the panel's own fields have
 * already moved and the grip that was grabbed is the one that was drawn.
 */
static int
dbg_panel_grip_at(struct ToriRSChrome const* ui, int x, int y)
{
    for( int i = ui->panel_count - 1; i >= 0; i-- )
    {
        struct ToriRSChromePanel const* p = &ui->panels[i];
        struct ToriRSChromeRect g;

        if( !p->visible || p->last_rect.w <= 0 || !dbg_panel_has_grip(p) )
            continue;
        if( p->style != TORIRS_CHROME_PANEL_WINDOW )
            continue;
        g = dbg_grip_rect(ui, p->last_rect);
        if( dbg_point_in(x, y, g.x, g.y, g.w, g.h) )
            return i;
    }
    return -1;
}

/**
 * Put a panel's bottom-right corner at (`right`, `bottom`).
 *
 * The origin does not move -- that is the whole reason the grip is in this
 * corner rather than another: a bottom-right drag is two independent clamps on
 * the size and nothing else, where any other corner also has to walk x or y
 * and can drift the edge the user is not touching.
 *
 * Both `fixed_w` and `fixed_h` are written, which is what pins the result
 * against the next Build's content sizing.
 */
static void
dbg_panel_resize_to(struct ToriRSChrome* ui, int panel, int right, int bottom)
{
    struct ToriRSChromePanel* p = &ui->panels[panel];
    int w = right - p->x;
    int h = bottom - p->y;

    if( w < DBG_MIN_PANEL_W )
        w = DBG_MIN_PANEL_W;
    if( h < DBG_MIN_PANEL_H )
        h = DBG_MIN_PANEL_H;
    if( p->fixed_w == w && p->fixed_h == h )
        return;
    /* Same reason the move damages: the strip a shrinking panel just uncovered
     * is stale, and only damage tells the host to repaint it. */
    dbg_damage_add(ui, p->last_rect);
    p->fixed_w = w;
    p->fixed_h = h;
    dbg_dirty_panel(ui, panel);
}

/** Move a panel, invalidating both the box it left and the one it now covers. */
static void
dbg_panel_move_to(struct ToriRSChrome* ui, int panel, int x, int y)
{
    struct ToriRSChromePanel* p = &ui->panels[panel];

    if( p->x == x && p->y == y )
        return;
    /* The vacated box has to repaint too, or the panel smears across the
     * screen on every drag frame -- damage is the only thing telling the host
     * that those pixels are now stale. */
    dbg_damage_add(ui, p->last_rect);
    p->x = x;
    p->y = y;
    dbg_dirty_panel(ui, panel);
}

/* ---- panel scrolling ------------------------------------------------------
 *
 * The bar is geometry, not a widget: it belongs to the panel the way the resize
 * grip does, so it is hit off `last_rect` for the same reason the grip is --
 * mid-drag the panel's live fields may already have moved, and what was grabbed
 * is what was drawn.
 */

/** Does this panel currently have a bar? Answered from the last Build. */
static int
dbg_panel_scrolls(struct ToriRSChromePanel const* p)
{
    return p->scrollable && p->view_h > 0 && p->content_h > p->view_h;
}

/**
 * The panel's scrollbar box, or an empty rect when it has none.
 *
 * Built from the origin and view height Build resolved, not from the
 * header/strip/footer metrics all over again -- the bar has to sit exactly over
 * the window the rows scroll inside, and two derivations of the same band drift
 * the moment either end of it grows a new piece of chrome.
 */
static struct ToriRSChromeRect
dbg_panel_scrollbar_rect(struct ToriRSChrome const* ui, int panel)
{
    struct ToriRSChromeRect r = { 0, 0, 0, 0 };
    struct ToriRSChromePanel const* p = &ui->panels[panel];

    if( !p->visible || p->last_rect.w <= 0 || !dbg_panel_scrolls(p) )
        return r;
    r.x = p->last_rect.x + p->last_rect.w - DBG_RULE - DBG_PAD_X - DBG_SCROLL_W;
    r.y = p->content_y;
    r.w = DBG_SCROLL_W;
    r.h = p->view_h;
    return r;
}

/** Move a panel's scroll to `scroll` px, clamped. @return 1 if it moved. */
static int
dbg_panel_scroll_to(struct ToriRSChrome* ui, int panel, int scroll)
{
    struct ToriRSChromePanel* p = &ui->panels[panel];
    int const max_scroll = p->content_h - p->view_h;

    if( scroll > max_scroll )
        scroll = max_scroll;
    if( scroll < 0 )
        scroll = 0;
    if( p->scroll_y == scroll )
        return 0;
    p->scroll_y = scroll;
    dbg_dirty_panel(ui, panel);
    return 1;
}

/**
 * Press on a panel's bar: an arrow steps, the track pages, the grip starts a
 * drag. @return 1 when the press belonged to a bar.
 */
static int
dbg_panel_scroll_press(struct ToriRSChrome* ui, int x, int y)
{
    for( int i = ui->panel_count - 1; i >= 0; i-- )
    {
        struct ToriRSChromeRect const bar = dbg_panel_scrollbar_rect(ui, i);
        struct ToriRSChromePanel* p = &ui->panels[i];
        struct DbgScrollGeom g;
        int const step = ToriRSChrome_FontLineBox(ui->theme.font_row, ui->scale);

        if( bar.w <= 0 || !dbg_point_in_rect(x, y, bar) )
            continue;

        if( y < bar.y + DBG_SCROLL_W )
        {
            dbg_panel_scroll_to(ui, i, p->scroll_y - step);
            return 1;
        }
        if( y >= bar.y + bar.h - DBG_SCROLL_W )
        {
            dbg_panel_scroll_to(ui, i, p->scroll_y + step);
            return 1;
        }
        if( !dbg_scroll_geom(
                bar, p->content_h, p->view_h, p->scroll_y, DBG_SCROLL_W, DBG_SCROLL_GRIP_MIN, &g) )
            return 1;
        if( y >= g.grip_y && y < g.grip_y + g.grip_h )
        {
            ui->scroll_panel = i;
            ui->scroll_grab = y - g.grip_y;
            return 1;
        }
        /* Above or below the grip: page by a viewful, the desktop convention. */
        dbg_panel_scroll_to(
            ui, i, y < g.grip_y ? p->scroll_y - p->view_h : p->scroll_y + p->view_h);
        return 1;
    }
    return 0;
}

/** Continue a panel grip drag: put the grip's top under the held grab point. */
static void
dbg_panel_scroll_drag_to(struct ToriRSChrome* ui, int y)
{
    int const panel = ui->scroll_panel;
    struct ToriRSChromePanel* p;
    struct ToriRSChromeRect bar;
    struct DbgScrollGeom g;
    int travel;

    if( !dbg_valid_panel(ui, panel) )
        return;
    p = &ui->panels[panel];
    bar = dbg_panel_scrollbar_rect(ui, panel);
    if( bar.w <= 0 )
        return;
    if( !dbg_scroll_geom(
            bar, p->content_h, p->view_h, p->scroll_y, DBG_SCROLL_W, DBG_SCROLL_GRIP_MIN, &g) )
        return;
    travel = g.track_h - g.grip_h;
    if( travel <= 0 )
        return;
    dbg_panel_scroll_to(
        ui, panel, (y - ui->scroll_grab - g.track_y) * (p->content_h - p->view_h) / travel);
}

int
ToriRSChrome_HasVisiblePanel(struct ToriRSChrome const* ui)
{
    assert(ui);
    for( int i = 0; i < ui->panel_count; i++ )
        if( ui->panels[i].visible )
            return 1;
    return 0;
}

int
ToriRSChrome_HitTest(struct ToriRSChrome const* ui, int x, int y)
{
    int panel;

    assert(ui);
    panel = dbg_panel_at(ui, x, y);
    if( panel < 0 )
        return -1;
    for( int w = ui->panels[panel].first_widget; w >= 0; w = ui->widgets[w].next )
    {
        struct ToriRSChromeWidget const* wd = &ui->widgets[w];
        if( !dbg_widget_shown(ui, wd) || wd->kind == TORIRS_CHROME_W_SEPARATOR ||
            wd->kind == TORIRS_CHROME_W_LABEL )
            continue;
        if( dbg_point_in(x, y, wd->x, wd->y, wd->w, wd->h) )
            return w;
    }
    return -1;
}

/**
 * The panel's pinned tab strip, or -1.
 *
 * The FIRST shown one, matching dbg_build_window's own choice of which strip to
 * pin: a panel with two would lay the second out as an ordinary scrolling row,
 * and a handle over a row that scrolls is a handle that moves when the list
 * does.
 */
static int
dbg_panel_tabstrip(struct ToriRSChrome const* ui, struct ToriRSChromePanel const* p)
{
    assert(ui);
    assert(p);
    for( int w = p->first_widget; w >= 0; w = ui->widgets[w].next )
        if( ui->widgets[w].kind == TORIRS_CHROME_W_TABSTRIP &&
            dbg_widget_shown(ui, &ui->widgets[w]) )
            return w;
    return -1;
}

int
ToriRSChrome_WindowDragRegion(
    struct ToriRSChrome const* ui, int panel, struct ToriRSChromeDragRegion* out)
{
    struct ToriRSChromePanel const* p;
    struct ToriRSChromeRect r;
    int strip;

    assert(ui);
    assert(out);
    memset(out, 0, sizeof(*out));

    if( !dbg_valid_panel(ui, panel) )
        return 0;
    p = &ui->panels[panel];
    /* Never built, hidden, not a window, or floating. The last is the one worth
     * naming: a floating panel is moved INSIDE the canvas by this same bar, and
     * a handle that did both would drag the game out from under the pointer. */
    if( !p->visible || p->last_rect.w <= 0 || p->style != TORIRS_CHROME_PANEL_WINDOW ||
        !p->filled )
        return 0;

    r = dbg_panel_title_rect(ui, p);
    if( r.w > 0 && r.h > 0 )
        out->handles[out->handle_count++] = r;

    strip = dbg_panel_tabstrip(ui, p);
    if( strip >= 0 && ui->widgets[strip].w > 0 && ui->widgets[strip].h > 0 )
    {
        struct ToriRSChromeWidget const* w = &ui->widgets[strip];

        r.x = w->x;
        r.y = w->y;
        r.w = w->w;
        r.h = w->h;
        out->handles[out->handle_count++] = r;

        r = dbg_tab_run_rect(ui, w);
        if( r.w > 0 )
            out->holes[out->hole_count++] = r;
    }

    if( out->handle_count == 0 )
        return 0;

    /*
     * A closable panel's Close sits IN the title bar.
     *
     * Unpunched it is unreachable rather than merely awkward: the press that
     * would close the window starts a drag of it instead, and the button never
     * sees a click at all. That is the whole hazard of this feature in one
     * case -- a handle is not a decoration, it is a region that eats input --
     * and it lands on the one control a frameless window cannot do without.
     */
    r = dbg_panel_close_rect(ui, panel);
    if( r.w > 0 && out->hole_count < TORIRS_CHROME_DRAG_HOLES_MAX )
        out->holes[out->hole_count++] = r;

    /*
     * Popups float OUTSIDE the widget that owns them, so an open list or picker
     * can cover the strip or the bar. While one is up it owns every press in
     * its box -- including the ones that dismiss it -- so it must not be a drag
     * handle for the frames it is open.
     */
    if( ui->dropdown_open >= 0 && out->hole_count < TORIRS_CHROME_DRAG_HOLES_MAX )
    {
        r = dbg_dropdown_rect(ui);
        if( r.w > 0 )
            out->holes[out->hole_count++] = r;
    }
    if( ui->colorpick_open >= 0 && out->hole_count < TORIRS_CHROME_DRAG_HOLES_MAX )
    {
        r = dbg_colorpick_rect(ui);
        if( r.w > 0 )
            out->holes[out->hole_count++] = r;
    }
    return 1;
}

int
ToriRSChromeDragRegion_Contains(struct ToriRSChromeDragRegion const* region, int x, int y)
{
    int on = 0;

    assert(region);
    for( int i = 0; i < region->handle_count && !on; i++ )
        on = dbg_point_in_rect(x, y, region->handles[i]);
    if( !on )
        return 0;
    for( int i = 0; i < region->hole_count; i++ )
        if( dbg_point_in_rect(x, y, region->holes[i]) )
            return 0;
    return 1;
}

/**
 * Row of the open dropdown list under a point, or -1.
 *
 * The list is not made of widgets, so it is not part of the widget hit test —
 * it is a view on the borrowed `options` array, and its rows are addressed by
 * index. Every mouse entry point asks this FIRST, because an open list sits
 * over whatever panel is beneath it and must take the click.
 */
static int
dbg_dropdown_row_at(struct ToriRSChrome const* ui, int x, int y)
{
    struct ToriRSChromeRect rect;
    struct ToriRSChromeRect bar;
    struct ToriRSChromeWidget const* w;
    int row;
    int rows;

    if( ui->dropdown_open < 0 )
        return -1;
    rect = dbg_dropdown_rect(ui);
    if( rect.w <= 0 || !dbg_point_in(x, y, rect.x, rect.y, rect.w, rect.h) )
        return -1;
    /* The bar is inside the list, so its column is not a row. Without this a
     * press on the grip would also be a press on whatever option it covers,
     * and releasing the drag would pick that option. */
    bar = dbg_dropdown_scrollbar_rect(ui);
    if( bar.w > 0 && dbg_point_in(x, y, bar.x, bar.y, bar.w, bar.h) )
        return -1;

    w = &ui->widgets[ui->dropdown_open];
    rows = dbg_dropdown_rows(ui);
    row = (y - rect.y - DBG_DROP_LIST_PAD) / DBG_DROP_ROW_H;
    if( row < 0 || row >= rows || w->scroll + row >= w->option_count )
        return -1;
    return row;
}

/** Move the open list to `scroll`, damaging it if that changed anything. */
static int
dbg_dropdown_scroll_to(struct ToriRSChrome* ui, int scroll)
{
    struct ToriRSChromeWidget* w = &ui->widgets[ui->dropdown_open];
    int const before = w->scroll;

    w->scroll = scroll;
    dbg_dropdown_clamp(w);
    if( w->scroll == before )
        return 0;
    ui->dirty = 1;
    dbg_damage_add(ui, dbg_dropdown_rect(ui));
    /* The hovered row is a WINDOW index: the same row number now shows a
     * different option, so whatever was highlighted has moved out from under
     * the cursor. Recomputed by the next move; cleared now so the highlight
     * does not lag a scroll by one event. */
    ui->dropdown_hover_row = -1;
    return 1;
}

/**
 * Take a press on the open list's scrollbar.
 *
 * Returns 0 when the point is not on a bar, so the caller falls through to the
 * rows. The three regions are the ones the reference wires up in ~script31: an
 * arrow at each end steps a row (`script32`/`script33`), the grip drags
 * (`script35`), and the track pages (`script34`).
 */
static int
dbg_dropdown_scroll_press(struct ToriRSChrome* ui, int x, int y)
{
    struct ToriRSChromeRect bar;
    struct DbgScrollGeom g;
    struct ToriRSChromeWidget const* w;
    int content;
    int view;
    int offset;

    if( ui->dropdown_open < 0 )
        return 0;
    bar = dbg_dropdown_scrollbar_rect(ui);
    if( bar.w <= 0 || !dbg_point_in(x, y, bar.x, bar.y, bar.w, bar.h) )
        return 0;

    w = &ui->widgets[ui->dropdown_open];
    dbg_dropdown_scroll_px(ui, &content, &view, &offset);
    if( !dbg_scroll_geom(bar, content, view, offset, DBG_SCROLL_W, DBG_SCROLL_GRIP_MIN, &g) )
        return 1;

    if( y < g.track_y )
        dbg_dropdown_scroll_to(ui, w->scroll - 1);
    else if( y >= bar.y + bar.h - DBG_SCROLL_W )
        dbg_dropdown_scroll_to(ui, w->scroll + 1);
    else if( y < g.grip_y )
        dbg_dropdown_scroll_to(ui, w->scroll - dbg_dropdown_rows(ui));
    else if( y >= g.grip_y + g.grip_h )
        dbg_dropdown_scroll_to(ui, w->scroll + dbg_dropdown_rows(ui));
    else
    {
        ui->dropdown_scroll_drag = 1;
        ui->dropdown_scroll_grab = y - g.grip_y;
    }
    return 1;
}

/** Follow a grip drag: put the grabbed point of the grip back under the cursor. */
static void
dbg_dropdown_scroll_drag_to(struct ToriRSChrome* ui, int y)
{
    struct ToriRSChromeRect bar;
    struct DbgScrollGeom g;
    int content;
    int view;
    int offset;
    int travel;
    int range;

    assert(ui->dropdown_open >= 0);
    bar = dbg_dropdown_scrollbar_rect(ui);
    dbg_dropdown_scroll_px(ui, &content, &view, &offset);
    if( bar.w <= 0 ||
        !dbg_scroll_geom(bar, content, view, offset, DBG_SCROLL_W, DBG_SCROLL_GRIP_MIN, &g) )
    {
        ui->dropdown_scroll_drag = 0;
        return;
    }

    /* Invert dbg_scroll_geom: the grip's top travels (track_h - grip_h) over a
     * scroll range of (content - view), so a wanted top maps back to a wanted
     * offset. Rounded to the nearest row on the way out, because this list
     * scrolls by rows -- a pixel offset it cannot hold would make the grip
     * drift away from the cursor as the drag went on. */
    travel = g.track_h - g.grip_h;
    range = content - view;
    if( travel <= 0 || range <= 0 )
        return;
    offset = (y - ui->dropdown_scroll_grab - g.track_y) * range / travel;
    dbg_dropdown_scroll_to(
        ui, (offset + DBG_DROP_ROW_H / 2) / (DBG_DROP_ROW_H > 0 ? DBG_DROP_ROW_H : 1));
}

/** Close the open list, damaging the area it occupied. */
static void
dbg_dropdown_close(struct ToriRSChrome* ui)
{
    if( ui->dropdown_open < 0 )
        return;
    dbg_damage_add(ui, dbg_dropdown_rect(ui));
    dbg_dirty_widget(ui, ui->dropdown_open);
    ui->dropdown_open = -1;
    ui->dropdown_hover_row = -1;
    /* The grip cannot outlive the list it scrolls: a drag left armed here
     * would move the NEXT list the moment one opened. */
    ui->dropdown_scroll_drag = 0;
    ui->dirty = 1;
}

int
ToriRSChrome_MouseMove(struct ToriRSChrome* ui, int x, int y)
{
    int hit;

    assert(ui);

    /* Recorded before any early return: a strip's hovered TAB is resolved from
     * the position at draw time, and a move that a drag consumes still moved
     * the cursor. */
    ui->hover_x = x;
    ui->hover_y = y;

    /*
     * The title bar's Close, tracked here for the same reason and with the same
     * "before any early return" placement.
     *
     * It is not a widget -- it is the panel's own chrome, so the widget hover
     * below never names it -- and it changes PICTURE under the cursor rather
     * than gaining an outline. A change of picture that nothing marks dirty is
     * a change that does not get drawn: the pressed art would turn up only on a
     * frame something unrelated had already rebuilt.
     */
    {
        int const panel = dbg_panel_close_at(ui, x, y);

        if( panel != ui->hover_close_panel )
        {
            /* Both panels repaint: the one losing the highlight and the one
             * gaining it, exactly as the widget hover below does. */
            dbg_dirty_panel(ui, ui->hover_close_panel);
            ui->hover_close_panel = panel;
            dbg_dirty_panel(ui, panel);
        }
    }

    /* A panel's own grip, held for as long as the button is: same rule as the
     * dropdown's below it, and checked first for the same reason. */
    if( ui->scroll_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->scroll_panel) )
        {
            dbg_panel_scroll_drag_to(ui, y);
            return 1;
        }
        ui->scroll_panel = -1;
    }

    /* Ahead of every other claim on the pointer, including the panel drags
     * below: a scrollbar grip is held until it is let go, and the cursor
     * leaving the bar (or the list) does not release it. */
    if( ui->dropdown_scroll_drag )
    {
        if( ui->dropdown_open < 0 )
            ui->dropdown_scroll_drag = 0;
        else
        {
            dbg_dropdown_scroll_drag_to(ui, y);
            return 1;
        }
    }

    /* Ahead of everything, for the same reason the drag is: the pointer
     * belongs to the grip until it is let go. */
    if( ui->resize_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->resize_panel) )
            dbg_panel_resize_to(
                ui, ui->resize_panel, x - ui->resize_grab_x, y - ui->resize_grab_y);
        else
            ui->resize_panel = -1;
        return 1;
    }

    /* Ahead of everything: while a panel is held, the pointer belongs to it
     * wherever it has got to, including outside the panel's own box. Hover
     * must not follow the cursor mid-drag or rows light up under a panel the
     * user is carrying. */
    if( ui->drag_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->drag_panel) )
            dbg_panel_move_to(ui, ui->drag_panel, x - ui->drag_grab_x, y - ui->drag_grab_y);
        else
            ui->drag_panel = -1;
        return 1;
    }

    /* A bar sweep, ahead of hover for the reason every other drag here is: the
     * pointer belongs to the bar it was pressed on until the button comes up,
     * including after it has left the popup. That is what makes both ends of
     * an axis reachable -- see dbg_colorpick_value_at. */
    if( ui->colorpick_drag_bar != TORIRS_CHROME_COLORBAR_NONE )
    {
        if( ui->colorpick_open < 0 )
            ui->colorpick_drag_bar = TORIRS_CHROME_COLORBAR_NONE;
        else
        {
            dbg_colorpick_apply_at(ui, ui->colorpick_drag_bar, x);
            return 1;
        }
    }

    if( ui->dropdown_open >= 0 )
    {
        int const row = dbg_dropdown_row_at(ui, x, y);
        if( row != ui->dropdown_hover_row )
        {
            ui->dropdown_hover_row = row;
            ui->dirty = 1;
            dbg_damage_add(ui, dbg_dropdown_rect(ui));
        }
        if( row >= 0 )
            return 1;
    }

    hit = ToriRSChrome_HitTest(ui, x, y);
    if( hit != ui->hover )
    {
        /* Both rows repaint: the one losing the highlight and the one gaining it. */
        dbg_dirty_widget(ui, ui->hover);
        ui->hover = hit;
        dbg_dirty_widget(ui, hit);
    }
    else if( hit >= 0 && ui->widgets[hit].kind == TORIRS_CHROME_W_TABSTRIP )
    {
        /* Moving between two tabs of one strip does not change the hovered
         * WIDGET, so the highlight would stick to the tab first entered. The
         * strip is the one widget whose hover lives below handle granularity. */
        dbg_dirty_widget(ui, hit);
    }
    if( ui->colorpick_open >= 0 && dbg_point_in_rect(x, y, dbg_colorpick_rect(ui)) )
        return 1;
    return dbg_panel_at(ui, x, y) >= 0;
}

int
ToriRSChrome_MouseDown(struct ToriRSChrome* ui, int x, int y)
{
    int hit;

    assert(ui);

    /*
     * A press inside the open picker belongs to the picker, and starts a
     * sweep; a press anywhere else closes it -- the same dismiss-on-press rule
     * the dropdown list follows, and checked first for the same reason. The
     * popup floats outside its panel's box, so without this the press would
     * fall through to whatever the panel underneath happens to have there.
     */
    if( ui->colorpick_open >= 0 )
    {
        int const bar = dbg_colorpick_bar_at(ui, x, y);
        if( bar != TORIRS_CHROME_COLORBAR_NONE )
        {
            ui->colorpick_drag_bar = bar;
            dbg_colorpick_apply_at(ui, bar, x);
            ui->press = -1;
            return 1;
        }
        if( dbg_point_in_rect(x, y, dbg_colorpick_rect(ui)) )
        {
            /* The popup's own padding: inert, but it must not dismiss either --
             * a click a pixel above a bar should not close the thing you are
             * aiming at. */
            ui->press = -1;
            return 1;
        }
        ToriRSChrome_ColorPickSetOpen(ui, ui->colorpick_open, 0);
    }

    /* A press inside the open list belongs to the list; MouseUp turns it into
     * a selection. A press anywhere else closes it, which is what makes
     * clicking away dismiss rather than select. */
    if( ui->dropdown_open >= 0 )
    {
        /* The bar first: it is inside the list, and a press on it must not be
         * the press that dismisses the list. */
        if( dbg_dropdown_scroll_press(ui, x, y) )
        {
            ui->press = -1;
            return 1;
        }
        if( dbg_dropdown_row_at(ui, x, y) >= 0 )
        {
            ui->press = -1;
            return 1;
        }
        dbg_dropdown_close(ui);
    }

    /*
     * A closable panel's Close, before its header drag: the button lives IN the
     * title bar, and a press on it must not also pick the window up. Acted on
     * at RELEASE like every other clickable thing here, so a press that slides
     * off cancels.
     */
    if( dbg_panel_close_at(ui, x, y) >= 0 )
    {
        dbg_focus_release(ui);
        ui->press = -1;
        return 1;
    }

    /* A panel's own bar, before its rows: the bar's column is inside the
     * content area, so a press there would otherwise land on whatever row it
     * happens to sit beside. */
    if( dbg_panel_scroll_press(ui, x, y) )
    {
        ui->press = -1;
        return 1;
    }

    /* The grip is tested before the widgets, not after them as the header is.
     * The header strip is empty by construction, so a control found there is a
     * control the caller deliberately put there and it should win; the grip
     * sits in the bottom-left of the content column, where the last row's
     * padding can legitimately overlap it, and a grip that loses to whatever
     * row happens to end near it is a grip that works on some panels and not
     * others. */
    {
        int const grip = dbg_panel_grip_at(ui, x, y);
        if( grip >= 0 )
        {
            /* Offsets from the corner being dragged, not from the origin, so
             * the panel keeps the exact point it was picked up by instead of
             * snapping its corner to the cursor on the first move. */
            ui->resize_grab_x =
                x - (ui->panels[grip].last_rect.x + ui->panels[grip].last_rect.w);
            ui->resize_grab_y =
                y - (ui->panels[grip].last_rect.y + ui->panels[grip].last_rect.h);
            ui->resize_panel = grip;
            ui->press = -1;
            dbg_focus_release(ui);
            return 1;
        }
    }

    hit = ToriRSChrome_HitTest(ui, x, y);

    /* A press on the header with no widget under it starts a drag. Widgets win
     * -- a control that happens to sit in the title strip is still a control --
     * which is why this is tested after the hit test rather than before it. */
    if( hit < 0 )
    {
        int const header = dbg_panel_header_at(ui, x, y);
        if( header >= 0 )
        {
            ui->drag_panel = header;
            ui->drag_grab_x = x - ui->panels[header].x;
            ui->drag_grab_y = y - ui->panels[header].y;
            ui->press = -1;
            dbg_focus_release(ui);
            return 1;
        }
    }

    ui->press = hit;
    if( hit >= 0 && ui->widgets[hit].kind == TORIRS_CHROME_W_MODELVIEW )
    {
        /* Focusable so the host can route WASD/zoom keys at it -- the same
         * focus slot text inputs use, so clicking anywhere else releases it
         * through the existing paths. */
        if( ui->focus != hit )
        {
            dbg_dirty_widget(ui, ui->focus);
            ui->focus = hit;
            dbg_dirty_widget(ui, hit);
        }
        return 1;
    }
    if( hit >= 0 && ui->widgets[hit].kind == TORIRS_CHROME_W_COLORPICK )
    {
        /*
         * Two zones, and which one was pressed is the row's whole affordance:
         * the SWATCH opens the axis popup, the rest of the box is an ordinary
         * text field.
         *
         * Both, rather than one or the other, because the two ways a colour
         * arrives are different jobs. A hex out of a wiki page or another
         * client is pasted or typed; a colour chosen by eye is swept. Making
         * the row a picker alone would mean no way to enter a known value, and
         * a field alone is what this replaced.
         */
        struct ToriRSChromeWidget* w = &ui->widgets[hit];
        int const sw_x = w->x + dbg_row_box_offset(ui, w) + DBG_RULE + DBG_INPUT_PAD_X;

        if( x < sw_x + DBG_SWATCH + DBG_SWATCH_GAP / 2 )
        {
            /* Leaving the field the popup's own dismiss rule handles the
             * close, so this only ever has to open. */
            dbg_focus_release(ui);
            ToriRSChrome_ColorPickSetOpen(ui, hit, 1);
            ui->press = -1;
            return 1;
        }
        if( ui->focus != hit )
        {
            dbg_dirty_widget(ui, ui->focus);
            ui->focus = hit;
            dbg_dirty_widget(ui, hit);
        }
        w->caret = (int)strlen(w->text);
        return 1;
    }
    if( hit >= 0 && ui->widgets[hit].kind == TORIRS_CHROME_W_TEXTAREA )
    {
        /*
         * The caret goes WHERE THE CLICK WAS, unlike a one-line field's, which
         * parks it at the end.
         *
         * Not a refinement: a multiline box holds a list long enough that the
         * user is reaching for a particular item in the middle of it, and a
         * click that always jumped to the end would make every edit start by
         * arrowing back through the whole value.
         */
        struct ToriRSChromeWidget* w = &ui->widgets[hit];
        struct ToriRSChromeRect const inner = dbg_textarea_inner(ui, dbg_textarea_box(ui, w));
        int starts[DBG_TEXTAREA_LINES_MAX];
        int lens[DBG_TEXTAREA_LINES_MAX];
        int const count = dbg_textarea_wrap(ui, w, starts, lens);
        int const line_h = dbg_textarea_line_h(ui);
        int line;

        if( ui->focus != hit )
        {
            dbg_dirty_widget(ui, ui->focus);
            ui->focus = hit;
        }
        /* The caption band is inside the row's hit box, so a press up there
         * has to land on the first VISIBLE line rather than on a negative one. */
        line = y <= inner.y ? w->scroll : w->scroll + (y - inner.y) / line_h;
        if( line >= count )
            line = count - 1;
        if( line < 0 )
            line = 0;
        w->caret =
            starts[line] + dbg_textarea_col_at(ui, w, starts[line], lens[line], x - inner.x);
        dbg_textarea_scroll_to_caret(ui, w);
        dbg_dirty_widget(ui, hit);
        return 1;
    }
    if( hit >= 0 && ui->widgets[hit].kind == TORIRS_CHROME_W_TEXTINPUT )
    {
        if( ui->focus != hit )
        {
            dbg_dirty_widget(ui, ui->focus);
            ui->focus = hit;
            dbg_dirty_widget(ui, hit);
        }
        ui->widgets[hit].caret = (int)strlen(ui->widgets[hit].text);
    }
    else
        dbg_focus_release(ui);
    return dbg_panel_at(ui, x, y) >= 0;
}

int
ToriRSChrome_MouseUp(struct ToriRSChrome* ui, int x, int y)
{
    int hit;

    assert(ui);

    /* The release that ends a panel's grip drag, ahead of the row logic for the
     * same reason the dropdown's is: letting go over a row must not click it. */
    if( ui->scroll_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->scroll_panel) )
            dbg_panel_scroll_drag_to(ui, y);
        ui->scroll_panel = -1;
        ui->press = -1;
        return 1;
    }

    /* A closable panel's Close. Ahead of the row logic for the same reason
     * every other piece of panel furniture is: the button sits over the title
     * bar, not over a row. It DISCARDS -- staged rows live in the chrome and
     * nothing writes them but their own Save. */
    {
        int const panel = dbg_panel_close_at(ui, x, y);
        if( panel >= 0 )
        {
            ToriRSChrome_PanelSetVisible(ui, panel, 0);
            ui->press = -1;
            return 1;
        }
    }

    /* The release that ends a bar sweep. It reports the change through the
     * ordinary activation latch -- exactly as choosing a dropdown row does --
     * so a host reacts to a colour being picked through the loop it already
     * has, and does so ONCE at the end of the sweep rather than for every
     * pixel the pointer crossed. */
    if( ui->colorpick_drag_bar != TORIRS_CHROME_COLORBAR_NONE )
    {
        int const picker = ui->colorpick_open;
        if( picker >= 0 )
            dbg_colorpick_apply_at(ui, ui->colorpick_drag_bar, x);
        ui->colorpick_drag_bar = TORIRS_CHROME_COLORBAR_NONE;
        ui->press = -1;
        if( picker >= 0 )
            ui->activated = picker;
        return 1;
    }

    /* The release that ends a grip drag. It reports consumed and returns
     * before the row logic below, so letting go of the grip over a row does
     * not also choose that row. */
    if( ui->dropdown_scroll_drag )
    {
        if( ui->dropdown_open >= 0 )
            dbg_dropdown_scroll_drag_to(ui, y);
        ui->dropdown_scroll_drag = 0;
        ui->press = -1;
        return 1;
    }

    /* The release that ends a resize, before the one that ends a drag: the two
     * are exclusive, and this one also reports consumed so the click that set
     * the width is not delivered to the world underneath. */
    if( ui->resize_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->resize_panel) )
            dbg_panel_resize_to(
                ui, ui->resize_panel, x - ui->resize_grab_x, y - ui->resize_grab_y);
        ui->resize_panel = -1;
        ui->press = -1;
        return 1;
    }

    /* The release that ends a drag. It lands the panel at the cursor one last
     * time and retires the grab -- and reports consumed, so the click that
     * dropped the panel is not also delivered to whatever is underneath it. */
    if( ui->drag_panel >= 0 )
    {
        if( dbg_valid_panel(ui, ui->drag_panel) )
            dbg_panel_move_to(ui, ui->drag_panel, x - ui->drag_grab_x, y - ui->drag_grab_y);
        ui->drag_panel = -1;
        ui->press = -1;
        return 1;
    }

    if( ui->dropdown_open >= 0 )
    {
        int const row = dbg_dropdown_row_at(ui, x, y);
        if( row >= 0 )
        {
            struct ToriRSChromeWidget* dd = &ui->widgets[ui->dropdown_open];
            int const chosen = ui->dropdown_open;
            dd->selected = dd->scroll + row;
            dbg_dropdown_close(ui);
            /* Reported like any other activation, so a caller drains choices
             * and clicks through one TakeActivated loop. */
            ui->activated = chosen;
            ui->press = -1;
            return 1;
        }
    }

    hit = ToriRSChrome_HitTest(ui, x, y);
    /* Press and release must land on the same widget, so a drag off a checkbox
     * cancels rather than toggles. */
    if( hit >= 0 && hit == ui->press )
    {
        struct ToriRSChromeWidget* w = &ui->widgets[hit];
        if( w->kind == TORIRS_CHROME_W_CHECKBOX )
        {
            w->checked = !w->checked;
            ui->activated = hit;
            dbg_dirty_widget(ui, hit);
        }
        else if( w->kind == TORIRS_CHROME_W_LISTROW )
        {
            /*
             * Which zone: the switch at the right end flips it, and anything
             * left of that -- the affordance and the name both -- opens the
             * row. The NAME opening it rather than doing nothing is the one
             * departure from the reference list, because a row whose largest
             * target is dead reads as broken; a row with no action falls back
             * to toggling, so no zone is ever inert.
             */
            int const tog_x = w->x + w->w - DBG_TOGGLE_W;
            /* A locked row is all action zone: there is no switch to hit, so
             * the right end of it opens the page like the rest. */
            if( w->row_locked || (w->row_action && x < tog_x) )
            {
                ui->activated = hit;
                ui->activated_action = 1;
            }
            else
            {
                w->checked = !w->checked;
                ui->activated = hit;
                ui->activated_action = 0;
            }
            dbg_dirty_widget(ui, hit);
        }
        else if( w->kind == TORIRS_CHROME_W_MENUITEM || w->kind == TORIRS_CHROME_W_BUTTON )
        {
            ui->activated = hit;
            /* The button repaints because its pressed state just ended. */
            if( w->kind == TORIRS_CHROME_W_BUTTON )
                dbg_dirty_widget(ui, hit);
        }
        else if( w->kind == TORIRS_CHROME_W_TABSTRIP )
        {
            int const tab = dbg_tab_at(ui, w, x, y);
            /* A click on the tab already showing is not a no-op to report: the
             * caller may be using the strip to re-run a page's build. It just
             * does not need the switch work, which PanelSetActiveTab skips on
             * its own. */
            if( tab >= 0 )
            {
                ToriRSChrome_PanelSetActiveTab(ui, w->panel, tab);
                ui->activated = hit;
            }
        }
        else if( w->kind == TORIRS_CHROME_W_DROPDOWN )
        {
            /* Toggle: a second click on the closed row shuts it again. */
            if( ui->dropdown_open == hit )
            {
                dbg_dropdown_close(ui);
            }
            else if( w->option_count > 0 )
            {
                /* A menu has no persistent value: clearing on open keeps the
                 * list from highlighting last time's command, and makes
                 * choosing the same row twice two activations. */
                if( w->menu_mode )
                    w->selected = -1;
                ui->dropdown_open = hit;
                ui->dropdown_hover_row = -1;
                dbg_dropdown_clamp(w);
                dbg_dirty_widget(ui, hit);
            }
        }
    }
    ui->press = -1;
    return dbg_panel_at(ui, x, y) >= 0;
}


int
ToriRSChrome_MouseWheel(struct ToriRSChrome* ui, int x, int y, int delta)
{
    struct ToriRSChromeWidget* w;
    int before;

    assert(ui);

    /* An open picker takes the wheel over its bars, stepping the axis under the
     * pointer by one. The wheel is the only way to move a 128-step lightness
     * bar one value at a time -- a bar that narrow gives each cell well under
     * a pixel, so a pointer cannot address them individually. */
    if( ui->colorpick_open >= 0 )
    {
        int const bar = dbg_colorpick_bar_at(ui, x, y);
        if( bar != TORIRS_CHROME_COLORBAR_NONE )
        {
            struct ToriRSChromeWidget* pick = &ui->widgets[ui->colorpick_open];
            /* Wheel up (positive delta) moves UP the axis, which is to the
             * right on every one of these bars. */
            int const next = dbg_colorbar_with(
                pick->selected, bar, dbg_colorbar_value(pick->selected, bar) + delta);
            if( next != pick->selected )
            {
                pick->selected = next;
                dbg_colorpick_write_text(pick);
                dbg_dirty_widget(ui, ui->colorpick_open);
                dbg_damage_add(ui, dbg_colorpick_rect(ui));
                ui->activated = ui->colorpick_open;
            }
            return 1;
        }
        if( dbg_point_in_rect(x, y, dbg_colorpick_rect(ui)) )
            return 1;
    }

    /* No open list: the wheel may still belong to a widget under the pointer.
     * A CLOSED dropdown steps its selection -- the desktop convention, and
     * what makes a palette of hundreds usable without opening it -- except in
     * menu mode, where the rows are commands and a wheel must never run one.
     * Anything else on a panel consumes the event into nothing, so the camera
     * behind the panel stays still. */
    if( ui->dropdown_open < 0 )
    {
        int const hit = ToriRSChrome_HitTest(ui, x, y);
        if( hit >= 0 && ui->widgets[hit].kind == TORIRS_CHROME_W_DROPDOWN &&
            !ui->widgets[hit].menu_mode && ui->widgets[hit].option_count > 0 )
        {
            struct ToriRSChromeWidget* dd = &ui->widgets[hit];
            int const was = dd->selected;
            /* Wheel down (negative delta) moves DOWN the list. */
            dd->selected -= delta;
            if( dd->selected < 0 )
                dd->selected = 0;
            if( dd->selected >= dd->option_count )
                dd->selected = dd->option_count - 1;
            if( dd->selected != was )
            {
                /* Reported like a chosen row, so the caller's TakeActivated
                 * loop reacts to a wheel exactly as it would to a click. */
                ui->activated = hit;
                dbg_dirty_widget(ui, hit);
            }
            return 1;
        }
        /* Otherwise the panel under the pointer scrolls, if it can. Tested
         * after the closed-dropdown case so a wheel over a picker still steps
         * the picker rather than scrolling the page out from under it. */
        {
            int const panel = dbg_panel_at(ui, x, y);
            if( panel >= 0 )
            {
                if( dbg_panel_scrolls(&ui->panels[panel]) )
                {
                    int const step = ToriRSChrome_FontLineBox(ui->theme.font_row, ui->scale);
                    /* Wheel down (negative delta) moves the content UP. */
                    dbg_panel_scroll_to(ui, panel, ui->panels[panel].scroll_y - delta * step);
                }
                return 1;
            }
            return 0;
        }
    }

    {
        struct ToriRSChromeRect const rect = dbg_dropdown_rect(ui);
        if( rect.w <= 0 || !dbg_point_in(x, y, rect.x, rect.y, rect.w, rect.h) )
            return dbg_panel_at(ui, x, y) >= 0;
    }

    w = &ui->widgets[ui->dropdown_open];
    before = w->scroll;
    w->scroll += delta;
    dbg_dropdown_clamp(w);
    if( w->scroll != before )
    {
        /* The list is not a panel, so panel dirtying would not repaint it. */
        ui->dirty = 1;
        dbg_damage_add(ui, dbg_dropdown_rect(ui));
    }
    /* Consumed either way: a wheel over an open list must never also zoom the
     * camera behind it, including at the ends of the list. */
    return 1;
}

int
ToriRSChrome_WantsWheel(struct ToriRSChrome const* ui, int x, int y)
{
    assert(ui);
    if( ui->dropdown_open >= 0 )
    {
        struct ToriRSChromeRect const rect = dbg_dropdown_rect(ui);
        if( rect.w > 0 && dbg_point_in(x, y, rect.x, rect.y, rect.w, rect.h) )
            return 1;
    }
    if( ui->colorpick_open >= 0 && dbg_point_in_rect(x, y, dbg_colorpick_rect(ui)) )
        return 1;
    return dbg_panel_at(ui, x, y) >= 0;
}

int
ToriRSChrome_KeyChar(struct ToriRSChrome* ui, int ch)
{
    struct ToriRSChromeWidget* w;
    int len;

    assert(ui);
    if( ui->focus < 0 )
        return 0;
    /* Typing edits TEXT FIELDS only -- a text input, a multiline field, or a
     * colour picker's hex. A model view holds focus so the HOST can route its
     * own keys at it; characters falling through into its unused text field
     * would be silent state nobody can see. */
    if( ui->widgets[ui->focus].kind != TORIRS_CHROME_W_TEXTINPUT &&
        ui->widgets[ui->focus].kind != TORIRS_CHROME_W_TEXTAREA &&
        ui->widgets[ui->focus].kind != TORIRS_CHROME_W_COLORPICK )
        return 0;
    /* Still no control bytes, a multiline field included: a newline arrives as
     * TORIRS_CHROME_KEY_ENTER, which is where the decision to insert one or to
     * commit belongs. A '\n' typed as a CHARACTER is a paste or a stray
     * keymap, and letting it through would put a line break in a one-line
     * field that has no way to show it. */
    if( ch < 0x20 || ch > 0x7E )
        return 0;
    w = &ui->widgets[ui->focus];
    len = (int)strlen(w->text);
    if( len >= TORIRS_CHROME_INPUT_MAX - 1 )
        return 1;
    if( w->caret < 0 )
        w->caret = 0;
    if( w->caret > len )
        w->caret = len;
    memmove(w->text + w->caret + 1, w->text + w->caret, (size_t)(len - w->caret) + 1);
    w->text[w->caret] = (char)ch;
    w->caret++;
    if( w->kind == TORIRS_CHROME_W_TEXTAREA )
        dbg_textarea_scroll_to_caret(ui, w);
    dbg_dirty_widget(ui, ui->focus);
    return 1;
}

/**
 * The four editing keys a MULTILINE field answers differently from a one-line
 * one. @return 1 when it was one of them and it has been handled.
 *
 * Split out rather than folded into the switch below because the difference is
 * not a special case of the same behaviour: Home means the start of the LINE
 * where a one-line field has only a value to go to the start of, Up and Down
 * mean nothing at all without lines to move between, and Enter INSERTS instead
 * of committing -- which is the one that makes the control usable, since a
 * multiline field with no way to type a line break is a wide one-line field.
 */
static int
dbg_textarea_key(struct ToriRSChrome* ui, struct ToriRSChromeWidget* w, int key, int len)
{
    int starts[DBG_TEXTAREA_LINES_MAX];
    int lens[DBG_TEXTAREA_LINES_MAX];
    int count;
    int line;

    switch( key )
    {
    case TORIRS_CHROME_KEY_HOME:
    case TORIRS_CHROME_KEY_END:
    case TORIRS_CHROME_KEY_UP:
    case TORIRS_CHROME_KEY_DOWN:
        break;
    case TORIRS_CHROME_KEY_ENTER:
        /* The value has room or it does not; either way the key was ours, so
         * Enter never falls through to the commit a one-line field does. */
        if( len < TORIRS_CHROME_INPUT_MAX - 1 )
        {
            memmove(
                w->text + w->caret + 1, w->text + w->caret,
                (size_t)(len - w->caret) + 1);
            w->text[w->caret] = '\n';
            w->caret++;
            dbg_textarea_scroll_to_caret(ui, w);
            dbg_dirty_widget(ui, ui->focus);
        }
        return 1;
    default:
        return 0;
    }

    count = dbg_textarea_wrap(ui, w, starts, lens);
    line = dbg_textarea_line_of(starts, lens, count, w->caret);
    if( key == TORIRS_CHROME_KEY_HOME )
        w->caret = starts[line];
    else if( key == TORIRS_CHROME_KEY_END )
        w->caret = starts[line] + lens[line];
    else
    {
        int const target = line + (key == TORIRS_CHROME_KEY_UP ? -1 : 1);
        int col;

        /* Off the top or the bottom is a no-op that still CONSUMES the key:
         * letting it through would step the game's own camera the moment a
         * caret reached the first line of a field the user is typing in. */
        if( target < 0 || target >= count )
            return 1;
        col = w->caret - starts[line];
        if( col > lens[target] )
            col = lens[target];
        w->caret = starts[target] + col;
    }
    dbg_textarea_scroll_to_caret(ui, w);
    dbg_dirty_widget(ui, ui->focus);
    return 1;
}

int
ToriRSChrome_KeyEdit(struct ToriRSChrome* ui, int key)
{
    struct ToriRSChromeWidget* w;
    int len;
    int area;

    assert(ui);
    if( ui->focus < 0 )
        return 0;
    /* Same rule as KeyChar: editing keys belong to text fields alone. */
    if( ui->widgets[ui->focus].kind != TORIRS_CHROME_W_TEXTINPUT &&
        ui->widgets[ui->focus].kind != TORIRS_CHROME_W_TEXTAREA &&
        ui->widgets[ui->focus].kind != TORIRS_CHROME_W_COLORPICK )
        return 0;
    w = &ui->widgets[ui->focus];
    area = w->kind == TORIRS_CHROME_W_TEXTAREA;
    len = (int)strlen(w->text);
    if( w->caret < 0 )
        w->caret = 0;
    if( w->caret > len )
        w->caret = len;

    if( area && dbg_textarea_key(ui, w, key, len) )
        return 1;

    switch( key )
    {
    case TORIRS_CHROME_KEY_BACKSPACE:
        if( w->caret > 0 )
        {
            memmove(w->text + w->caret - 1, w->text + w->caret, (size_t)(len - w->caret) + 1);
            w->caret--;
            break;
        }
        return 1;
    case TORIRS_CHROME_KEY_DELETE:
        if( w->caret < len )
        {
            memmove(w->text + w->caret, w->text + w->caret + 1, (size_t)(len - w->caret));
            break;
        }
        return 1;
    case TORIRS_CHROME_KEY_LEFT:
        if( w->caret > 0 )
        {
            w->caret--;
            break;
        }
        return 1;
    case TORIRS_CHROME_KEY_RIGHT:
        if( w->caret < len )
        {
            w->caret++;
            break;
        }
        return 1;
    case TORIRS_CHROME_KEY_HOME:
        if( w->caret != 0 )
        {
            w->caret = 0;
            break;
        }
        return 1;
    case TORIRS_CHROME_KEY_END:
        if( w->caret != len )
        {
            w->caret = len;
            break;
        }
        return 1;
    case TORIRS_CHROME_KEY_ENTER:
        /* Enter COMMITS, which for a colour is where the typed hex becomes a
         * palette entry and the swatch catches up. A multiline field never
         * reaches this -- see dbg_textarea_key, where Enter inserts. */
        if( w->kind == TORIRS_CHROME_W_COLORPICK )
            ToriRSChrome_ColorPickCommitText(ui, ui->focus);
        ui->activated = ui->focus;
        return 1;
    case TORIRS_CHROME_KEY_ESCAPE:
        /* Escape ABANDONS: the field goes back to the value it is showing
         * rather than committing the half-typed hex the way a blur does. That
         * is the one difference between the two ways out of an edit, and it is
         * the difference the key is for. */
        if( w->kind == TORIRS_CHROME_W_COLORPICK )
            dbg_colorpick_write_text(w);
        dbg_dirty_widget(ui, ui->focus);
        ui->focus = -1;
        return 1;
    default:
        return 0;
    }

    /* The tail every key that MOVED or ERASED something shares. A multiline
     * field has to follow its caret; a one-line one scrolls at the draw. */
    if( area )
        dbg_textarea_scroll_to_caret(ui, w);
    dbg_dirty_widget(ui, ui->focus);
    return 1;
}

int
ToriRSChrome_TakeActivated(struct ToriRSChrome* ui)
{
    int const fired = ui ? ui->activated : -1;
    if( ui )
    {
        /* Carried across the drain, because a caller can only ask which kind
         * of activation it got AFTER it has taken one. */
        ui->taken_action = ui->activated_action;
        ui->activated = -1;
        ui->activated_action = 0;
    }
    return fired;
}

int
ToriRSChrome_ActivationWasAction(struct ToriRSChrome const* ui)
{
    assert(ui);
    return ui->taken_action;
}
