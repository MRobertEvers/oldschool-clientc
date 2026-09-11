/*
 * The chrome's authored geometry and palette, at 1x -- one copy, read by every
 * presentation of it.
 *
 * WHY THIS FILE EXISTS. The plugin window is presented more than once: by the
 * internal in-canvas chrome (ui/uitree_debug_overlay.c) and by the external
 * WEB/BROWSER DOM executors. They implement the SAME picture by different
 * means, and for as long as each carried its own copy of the numbers they
 * slowly stopped agreeing -- a row 20 tall beside one
 * 18 tall, a 12px settings well beside a 14px one, a toggle at 22x11 beside one
 * at 24x12. None of that is a bug either file can see; it only shows up as
 * "the panel looks different depending on which executor is bound".
 *
 * So the numbers live here and neither file owns them. A change lands in both
 * presentations or in neither, which is the only way two renderings of one
 * model stay one look.
 *
 * UNITS. Everything below is a 1x CHROME pixel. The in-canvas chrome multiplies
 * by its own scale (DBG_PX); a presentation whose own surface already scales
 * uses them raw. Neither is a screen pixel, and no site here may assume it
 * is.
 *
 * WHAT IS NOT HERE. The minimenu-style panel's geometry, which is
 * UIMinimenu_LayoutFromLineBox's and is pinned against it separately, and
 * anything derived from a font's line box -- a bake at a different size has to
 * reflow, not overlap. Those stay computed at their use.
 *
 * NO INCLUDES, DELIBERATELY. uitree_debug_overlay.c reaches nothing outside the
 * C library (that is what lets it draw on a cache that failed to open), so this
 * is macros and comments and nothing else.
 */
#ifndef TORIRS_CHROME_METRICS_H
#define TORIRS_CHROME_METRICS_H

/* ---- the row grid --------------------------------------------------------
 *
 * Every row in a window panel is one TORIRS_CHROME_M_ROW_H tall, whatever it
 * holds, separated by TORIRS_CHROME_M_ROW_GAP. Uniform rather than
 * font-derived per widget, because a column whose rows are each as tall as
 * their own contents does not line its controls up -- and a settings list that
 * does not line up is the one thing a settings list has to do. A row that grows
 * (an open colour picker's axis bars) does it as an ADDEND, keeping the grid.
 */

/** Inner padding of a window panel's content column, all four sides. */
#define TORIRS_CHROME_M_PAD 6
/** Height of one row, excluding the gap below it. */
#define TORIRS_CHROME_M_ROW_H 18
/** Vertical space between two rows. */
#define TORIRS_CHROME_M_ROW_GAP 3
/**
 * The label column of a labelled row -- a text input, a dropdown, a colour.
 *
 * Fixed rather than measured from the longest label in the panel: a column
 * whose width follows its contents moves every field sideways when one label
 * changes, and the fields have to line up down the panel for the same reason
 * the rows do.
 */
#define TORIRS_CHROME_M_LABEL_W 104

/* ---- semantic read-only components -------------------------------------
 *
 * Ordinary form controls stay on the compact 18px grid above. Read-only
 * content has its own authored component heights: enough room for the OSRS
 * inset frame and, for navigation/progress, a second line or meter. Browser
 * CSS and the internal fallback are regression-tested against these values.
 */
#define TORIRS_CHROME_M_HEADING_H 24
#define TORIRS_CHROME_M_DATA_ROW_H 28
#define TORIRS_CHROME_M_PROGRESS_H 40
#define TORIRS_CHROME_M_ACTION_ROW_H 42

/* ---- controls ------------------------------------------------------------ */

/** The interfaces' own on/off art, and the box a checkbox reserves for it. */
#define TORIRS_CHROME_M_BOX 17
/**
 * The same box for the OTHER boolean the interfaces draw: the bordered well
 * with a tick in it (archives 2847/2848), which is one pixel wider.
 *
 * A second number rather than a shared 17 or a shared 18, because a UI sprite
 * drawn at anything but its baked size speckles -- the outline is baked before
 * the scale -- so the control is sized to whichever art it is wearing. Which
 * one that is comes from enum ToriRSChromeCheckStyle, and every presentation
 * of the chrome branches on it at the one place it lays a checkbox out.
 */
#define TORIRS_CHROME_M_BOX_SQUARE 18
/** Gap between a checkbox's mark and its label. */
#define TORIRS_CHROME_M_CHECK_GAP 6

/** A roster row's switch: the hit box the tick/cross is right-aligned inside. */
#define TORIRS_CHROME_M_TOGGLE_W 24
#define TORIRS_CHROME_M_TOGGLE_H 12
/**
 * A roster row's settings affordance -- the three-dot well.
 *
 * 16, not the 14 this was. A settings field is TWO rules deep (the near-black
 * outline over the grey inset, @see TORIRS_CHROME_C_FRAME/FRAME_INSET), so a
 * 14px well is 4px of frame around a 10px hole -- and with the dots' own ink
 * 8 of that 10, the frame is the loudest thing in the control and the three
 * dots read as grit caught inside it. At 16 the same two rules leave 12, which
 * is the first size the dots have air on both sides of them.
 *
 * It is also within a pixel of the 17x17 tick/cross beside it
 * (TORIRS_CHROME_M_BOX), which is what stops the row's two controls reading as
 * a big one and a small one.
 */
#define TORIRS_CHROME_M_ROW_ICON 16
/** Gap between that well and the switch to its right. */
#define TORIRS_CHROME_M_ROW_ICON_GAP 5
/** Gap between a roster row's name and whatever furniture follows it. */
#define TORIRS_CHROME_M_ROW_NAME_GAP 4
/**
 * The three dots: their size, their pitch, and the well's left inset.
 *
 * The pitch is the dot plus a gap of its OWN size. At the 3 it was, the gap
 * was a single pixel and the row of them closed up into one dashed bar at
 * every scale -- three dots have to be three things, and the only way a 2px
 * square says "dot" rather than "block" is the space around it.
 *
 * INSET centres the run: 3 + 2*4 + 2 = 13 of 16, so the ink sits 3 in from
 * both edges and one clear pixel inside the grey rule.
 */
#define TORIRS_CHROME_M_DOT 2
#define TORIRS_CHROME_M_DOT_PITCH 4
#define TORIRS_CHROME_M_DOT_INSET 3

/* ---- the scrollbar, as ~script31 assembles one --------------------------- */

/** Width of the bar, and the side of each arrow button. */
#define TORIRS_CHROME_M_SCROLL_W 16
/** The grip's fixed end caps; its middle stretches between them. */
#define TORIRS_CHROME_M_SCROLL_CAP_H 5
/** Shortest a grip may become, so it stays grabbable in a long list. */
#define TORIRS_CHROME_M_SCROLL_GRIP_MIN 10

/* ---- the HSL16 colour picker --------------------------------------------- */

/** The swatch inside a colour row's field box. */
#define TORIRS_CHROME_M_SWATCH 11
/** Gap between that swatch and the hex beside it. */
#define TORIRS_CHROME_M_SWATCH_GAP 4
/** One axis bar, and the space under it. */
#define TORIRS_CHROME_M_COLORBAR_H 12
#define TORIRS_CHROME_M_COLORBAR_GAP 2
/** Inset of the axis bars inside the field box's own width. */
#define TORIRS_CHROME_M_COLORBAR_INSET 2

/* ---- the panel frame -----------------------------------------------------
 *
 * The interfaces' own nine-slice border -- the one the gameframe's popout strip
 * (interface 728) draws around the panels that mount in it, and the frame this
 * window wears so it reads as the game's own furniture. Sprites 846/820/847,
 * 821/841, 848/828/849.
 *
 * TWO numbers, not one, and that is what this art needs. The RAIL is a 6px bar;
 * the CORNERS are 32x32 tiles carrying an L of that bar along their two outer
 * edges. So a corner is blitted at 32 and an edge is stretched along its run at
 * a thickness of 6 -- and it is the RAIL, not the corner, that the content
 * column has to be inset by.
 *
 * The four EDGE pieces are baked CROPPED. In the cache they are 32x32 tiles
 * with the 6px bar floating in the middle, placed by the component's own
 * negative offset; spritebake's `@X,Y,W,H` takes the bar out at bake time, so
 * neither rasteriser has to know about a tile it draws none of. @see the crop
 * arguments in the generated bake's header comment.
 *
 * There is no CENTRE piece. The cache authors one and the strip never shows it:
 * the panel's own tradebacking is already under the frame, and painting a flat
 * brown over the parchment would be the frame erasing the surface it frames.
 *
 * A panel asks for the frame (ToriRSChrome_PanelSetFramed) rather than getting
 * it from its style, because the two things it competes with are both
 * legitimate: a floating developer panel wants the minimenu's rails so it reads
 * as a menu, and a panel mounted inside a surface that already draws a frame
 * must not draw a second one inside it.
 */

/** Thickness of the frame's rail: what the content column is inset by. */
#define TORIRS_CHROME_M_FRAME 6
/** Side of one corner tile, which carries 32px of rail along each of its two
 *  outer edges. A panel narrower than two of these has no edge run left and
 *  wears overlapping corners -- which is what the 42px-wide strip itself does. */
#define TORIRS_CHROME_M_FRAME_CORNER 32

/* ---- the rail ------------------------------------------------------------ */

/**
 * Width of the plugin rail -- the icon column -- INCLUDING its frame.
 *
 * The gameframe's own popout strip (interface 728) is 42 wide and wears the
 * nine-slice above, so the rail beside it is 42 too: two stone columns of
 * different widths standing next to each other read as one of them being
 * wrong, and the one the player has seen for twenty years is not the one
 * that is. 42 = 6 rail + 30 content + 6 rail, which is what leaves a 26px
 * icon its own margins.
 *
 * Every presentation of the rail is sized from this: the mac child window,
 * the Win32 rail window, the Android pane, and the CSS the page lays the rail
 * out with (`.tpc-rail` in modern.css / legacy-ie8.css). A host that reserves
 * a different allocation from the one the page draws in leaves a band of
 * window background down the seam.
 */
#define TORIRS_CHROME_M_RAIL_W 42

/* ---- the tab strip ------------------------------------------------------- */

/** Height of a tab, and the padding either side of its caption. */
#define TORIRS_CHROME_M_TAB_H 20
#define TORIRS_CHROME_M_TAB_PAD_X 5

/* ---- the title bar's close button ---------------------------------------- */

/**
 * Air between the close button and the panel's inner edge.
 *
 * ON TOP of the border, not instead of it: the button is placed from the inner
 * edge (the frame's rail, or the 1px rail on an unframed panel), and this is
 * the gap the reference leaves between window furniture and the frame it sits
 * inside. Without it the button touches the rail, which reads as part of the
 * border rather than as a control.
 */
#define TORIRS_CHROME_M_CLOSE_PAD 3

/* ---- field internals ----------------------------------------------------- */

/** Left inset of the text inside a field box. */
#define TORIRS_CHROME_M_FIELD_PAD_X 4
/** Inset of a field's decoration -- the arrow, the swatch, the focus ring. */
#define TORIRS_CHROME_M_FIELD_INSET 2
/**
 * The dropdown arrow's side.
 *
 * Derived from the row rather than fixed at the scrollbar's 16: the arrow sits
 * inside the field box with FIELD_INSET above and below it, and one sized
 * independently of the row would either overflow the box or float in it.
 */
#define TORIRS_CHROME_M_DROP_ARROW                                                                 \
    (TORIRS_CHROME_M_ROW_H - 2 * TORIRS_CHROME_M_FIELD_INSET)

/* ---- the multiline field, as the cache's own builds one -------------------
 *
 * The ground-items settings page (`loottools`, interface 650) is where this
 * game authors a multi-line text box, and `~script7210` -- the proc
 * `~script7213` calls to build both the highlight list and the filter list --
 * is the whole recipe:
 *
 *     cc_create(.., 3, ..)              a RECT, filled 0x372e22
 *     cc_create(.., 12, ..)             the INPUT, inset 2 on every side
 *     cc_input_setlinewrappingwidth(cc_getwidth - 2)
 *     cc_settextfont(fontmetrics_495)   the plain body face
 *     cc_setcolour(^white)  cc_settextshadow(true)  cc_settextalign(0, 0, 0)
 *
 * and the frame around it is `~script715`, which is the SAME two-colour pair a
 * settings field wears (TORIRS_CHROME_C_FRAME over TORIRS_CHROME_C_FRAME_INSET).
 * So a multiline field needs no art of its own -- one authored fill colour and
 * the frame the chrome already draws. Nothing was added to the bake for it.
 *
 * The header above the box ("Highlighted items", "Filtered items") is a plain
 * TEXT component in the settings orange, one row tall -- which is why the label
 * of a multiline row sits ABOVE its box rather than in the label column a
 * one-line field uses. A 104px caption column beside a four-line list would
 * take the width the list is for.
 */

/** Body fill of a multiline field: `~script7210`'s own `cc_setcolour`. Lighter
 *  than a one-line field's black, because this box is mostly empty and reads as
 *  a hole in the panel at TORIRS_CHROME_C_FIELD_BG. */
#define TORIRS_CHROME_C_TEXTAREA_BG 0x372E22

/** Visible lines a multiline field shows before it scrolls, when its caller
 *  names no other number. Four is what the reference's own two lists come out
 *  at in the popout strip's width. */
#define TORIRS_CHROME_M_TEXTAREA_ROWS 4
/** Ceiling on that, so a row cannot ask for a box taller than the panel. */
#define TORIRS_CHROME_M_TEXTAREA_ROWS_MAX 16
/**
 * Pitch of one wrapped line inside the box.
 *
 * AUTHORED, for the same reason TORIRS_CHROME_M_DROP_LIST_ROW_H is: the
 * presentations that read this cannot measure text at all -- the DOM's advances
 * live in the browser and USER32's in the font it picked -- so they get
 * the number the p12 line box comes out at (12 ascent + 4 descent). The
 * in-canvas chrome measures its own face and uses that, and the two agree
 * because the face is the same one.
 */
#define TORIRS_CHROME_M_TEXTAREA_LINE 16
/** Air between the box's frame and the first glyph, top and bottom.
 *  `~script7210` insets its input by 2 and offsets it by another 3. */
#define TORIRS_CHROME_M_TEXTAREA_PAD_Y 3

/* ---- custom plugin region ------------------------------------------------
 *
 * A custom region is the retained chrome's bounded drawing well. It is taller
 * than an ordinary form row because its intended contents are charts,
 * timelines and previews rather than one line of text. The caller may request
 * another height, but this is the portable default used by the semantic panel
 * API and by native presenters rebuilding the same row.
 */

#define TORIRS_CHROME_M_CUSTOM_H 120
#define TORIRS_CHROME_M_CUSTOM_H_MIN 48
#define TORIRS_CHROME_M_CUSTOM_H_MAX 512

/* ---- the open dropdown list ----------------------------------------------
 *
 * The list is the same width as the button it hangs off and starts at its
 * bottom edge, so the two read as one control seen open. Only its own two
 * numbers live here.
 */

/** Air around the list's rows, all four sides (script_9114: `$int26 + 4`). */
#define TORIRS_CHROME_M_DROP_LIST_PAD 2
/**
 * Pitch of one row inside the list -- taller than a panel row, because the
 * reference's list rows are 20 around a 14px line where its settings rows are
 * 18 around the same.
 *
 * AUTHORED, unlike the in-canvas chrome's own list pitch, which is the row
 * face's line box plus its air so a re-bake at another size reflows instead of
 * cropping. The presentations that read this cannot measure text at all -- the
 * DOM's advances live in the browser -- so they get the number the reference
 * uses, which is what the
 * line-box form comes out at for the p12 the rows are set in.
 */
#define TORIRS_CHROME_M_DROP_LIST_ROW_H 20
/** Rows the list shows at once before it scrolls. The model's own ceiling
 *  (TORIRS_CHROME_DROPDOWN_ROWS) restated for the presentations that build
 *  their list without the model's help. */
#define TORIRS_CHROME_M_DROP_LIST_ROWS 10

/* ---- the palette ---------------------------------------------------------
 *
 * Note what this is NOT: an invented brown. Every constant below already
 * appears in the client -- BODY is UITREE_MINIMENU_COLOR_BODY, CHROME and
 * ACCENT are what emit_minimenu draws, and FRAME / FRAME_INSET / LABEL are
 * script_3850's own three, read off the script rather than off a screenshot.
 *
 * The in-canvas chrome reaches these through its THEME (so a flat developer
 * palette can be swapped in wholesale); a presentation with no theme of its own
 * reads them directly. Same numbers either way.
 */

/** The panel body under the tiled tradebacking. */
#define TORIRS_CHROME_C_BODY 0x5D5447
/** Every interface edge this game draws. */
#define TORIRS_CHROME_C_CHROME 0x000000
/** Body text. */
#define TORIRS_CHROME_C_TEXT 0xFFFFFF
/** script_3850's enabled-setting label, and the roster's dots. */
#define TORIRS_CHROME_C_LABEL 0xFF981F
/** Hover, focus, and the open picker's marks. */
#define TORIRS_CHROME_C_ACCENT 0xFFFF00
/** The interfaces' on-state green. */
#define TORIRS_CHROME_C_ON 0x00FF00
/** Under a field's tile -- the tile's own edges are transparent. */
#define TORIRS_CHROME_C_FIELD_BG 0x000000
/** script_3850's field frame and the grey inset one pixel inside it. */
#define TORIRS_CHROME_C_FRAME 0x0E0E0C
#define TORIRS_CHROME_C_FRAME_INSET 0x474745
/** ~script31's scrollbar, for the frames before its sprites are drawable. */
#define TORIRS_CHROME_C_SCROLL_TRACK 0x23201B
#define TORIRS_CHROME_C_SCROLL_GRIP 0x4D4233
#define TORIRS_CHROME_C_SCROLL_GRIP_HI 0x766654
#define TORIRS_CHROME_C_SCROLL_GRIP_LO 0x332D25

#endif
