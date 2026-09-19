#include "plugin/porcelain/torirs_porcelain.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Stone Drawer: a phone-shaped gameframe for the 2004 dat1 lanes.
 *
 * ## What it is
 *
 * The modern OldSchool mobile layout, built out of the rev-289 media jagfile's
 * own art. The scene fills the screen; a rail of stone tabs hugs the right
 * edge; the inventory PANEL slides out beside the rail when a tab is tapped and
 * goes away when the same tab is tapped again; the chatbox is a sheet in the
 * bottom-left corner behind its own switch. Nothing is docked, because on a
 * phone every docked pixel is one the world does not get.
 *
 * ## Why it is a second plugin and not a fourth layout in gameframe.c
 *
 * gameframe.c reproduces three frames that EXIST -- 2004's, OldSchool's fixed
 * 548 and OldSchool's resizable 161/164 -- and every number in it is copied
 * from the revconfig or the clientscript that authored them. Its value is that
 * it invents nothing, and a case in its switch that invented a whole frame
 * would erode exactly that: the next reader could no longer assume a number in
 * that file came from somewhere.
 *
 * This frame is authored here. It has no reference to be faithful to, its state
 * is its own (a drawer that opens and closes has no counterpart in any of those
 * three), and the two files disagree about what a tab stone is FOR -- there,
 * the pressed highlight over a surround that already drew the stone; here, the
 * whole button, because a floating rail has no surround behind it.
 *
 * ## The OldSchool lane
 *
 * This frame used to stand down on an OldSchool cache, whose own toplevel_osm
 * is a mobile frame already. It arranges over it now -- over whichever
 * toplevel the server opened -- so the drawer is the same drawer on every
 * world. Two things differ there, and both are asked of the host rather than
 * assumed (api->lane): the CHAT is the cache's own 519x165 behavior pack and
 * is placed whole, while its backing/bar/plates are replaced through retained
 * appearance facets; the ORBS are a pack laid out beside the map, placed as a
 * block at the offset the cache's frames use. The tab state is the cache's
 * too, through the host's tab verbs.
 *
 * Auto deliberately means the classic 2004 family everywhere: selecting this
 * frame on an OldSchool lane is how a player gets that look back. OldSchool
 * Mobile's own interface-601 stones, plate, ring and icons remain an explicit
 * family choice. @see MobileFamily.
 *
 * ## The canvas it asks for
 *
 * FOLLOW_WINDOW with a minimum, and the minimum is the point: the client's own
 * floor is the classic frame's 765x503, which is a statement about a revconfig
 * gameframe whose children are insets off that box. This layout is arithmetic
 * on whatever canvas it is handed, so it carries its own floor -- the size below
 * which its own pieces stop fitting each other, computed once in
 * MOBILE_MIN_W/H and asserted against the geometry it is derived from.
 *
 * ## The art
 *
 * The plugin SHIPS its art: nineteen PNGs in
 * `script/plugins/assets/mobile-gameframe/`, cut once at authoring time by the
 * `SOURCES.sh` beside them and committed. Nothing here is read out of whatever
 * cache the client happens to have booted, so the frame looks the same on every
 * lane it runs on and cannot be half-drawn by a cache that is missing a sprite.
 *
 * They are this plugin's own files under this plugin's own names, rather than a
 * path into gameframe-layout's folder: an asset name resolves inside the
 * CALLING plugin's directory, so a borrowed one would make this frame's art
 * disappear the day the other plugin renamed a file it owns. The names are the
 * plugin's for a second reason -- the pieces are used for things the 2004 client
 * never used them for, and the media file's names would describe the wrong job:
 *
 *   stone       the surround's vertical strip retained with the classic set.
 *   highlight   the pressed stone, drawn on the open tab only.
 *   drawer      the 2004 side panel, which the drawer IS.
 *   chat_button the 2004 chat filter stone with the strip it was cut out of
 *               already taken off it, hand-masked: reduced to a button's box,
 *               or reduced and three-sliced to an OldSchool bar's cells.
 *   chat_sheet  the torn parchment behind either lane's chat behavior.
 *   map_housing the map plate, with the minimap and compass in the holes the
 *               classic frame puts them in.
 *
 * The two switch glyphs are the OldSchool client's own chat and keyboard
 * pictures; the switch plate itself remains classic interface art.
 */

/* --------------------------------------------------------------- geometry */

/** Every floating piece is this far from the edge it is pinned to. */
#define MOBILE_MARGIN 4

#define MOBILE_TAB_COUNT 14

/*
 * The rail is the 2004 frame's TWO TAB ROWS, each stood on its end.
 *
 * The classic frame carries fourteen tabs as two horizontal rows of seven, one
 * above the inventory and one below it, and each row is a run of redstones laid
 * edge to edge -- three shapes, mirrored between the halves, in a rhythm of
 * wide-narrow-wide that is the row's own. That run is the thing the eye reads
 * as "the tab strip", and it is what this rail is: the TOP row turned a quarter
 * turn to make the left column, and the BOTTOM row turned to make the right.
 * Seven tabs a column, fourteen in all, and the two groups stay the two groups
 * the frame has always had.
 *
 * Turning rather than restacking is what keeps the stones' bevels running along
 * the rail. They were cut lit from one side for a horizontal row; stood upright
 * the light runs across every cell instead of down the strip, and fourteen
 * stones that should close into one piece read as fourteen loose buttons.
 *
 * So a cell's HEIGHT is the classic box's width, and the rhythm survives the
 * turn -- which is why the two columns are not the same length and neither is
 * a multiple of anything. Those sums are the row's own.
 */
#define MOBILE_RAIL_COLS 2
#define MOBILE_RAIL_ROWS 7
/*
 * Both columns are the SAME plate, mirrored against each other.
 *
 * The rail used to be the top row's plate beside the bottom row's, which are
 * different lengths (249 against 269) and different depths (45 against 37) --
 * so the two columns ended at different heights and their stones sat at
 * different insets. Two copies of one plate, back to back, close into a single
 * slab: same length, same depth, and the mirror puts a finished outer edge on
 * each side with the two inner edges meeting up the middle.
 */
#define MOBILE_RAIL_COL_W 45
#define MOBILE_RAIL_COL_H 249

/*
 * Where the plate's seven ROCKS are, measured off the cleaned art.
 *
 * The stones and their icons are centred on these rather than placed at the
 * 2004 frame's tab offsets, because the plate is not the 2004 frame's any more:
 * it was cleaned up by hand, and its rocks moved a few pixels each when it was.
 * Placing against the old table left every stone sitting slightly off the rock
 * it belongs to -- visible as soon as the highlight lit, because a lit stone
 * that is two pixels proud of its socket reads as a misprint.
 *
 * `start` is the rock's first pixel along the plate's length and `span` its
 * length. The seams they came from are the dark joints between rocks, found by
 * scanning the plate for columns darker than its mean; the numbers are recorded
 * here rather than re-derived at boot because a seam scan is a heuristic and a
 * table is not, and this art is shipped rather than discovered.
 */
struct MobileRock
{
    unsigned char start;
    unsigned char span;
};

static struct MobileRock const MOBILE_ROCK[MOBILE_RAIL_ROWS] = {
    { 28,  28 },
    { 56,  28 },
    { 84,  26 },
    { 110, 37 },
    { 147, 33 },
    { 180, 28 },
    { 208, 28 },
};

/*
 * The plate's stone band ACROSS its depth: rows 9..44 of 45.
 *
 * The turn maps this to the column's width, so it is what a cell's x and width
 * come from -- and on the mirrored right-hand column it lands at the other end,
 * which is why the two columns compute it from opposite edges.
 */
#define MOBILE_PLATE_BAND_Y 9
#define MOBILE_PLATE_BAND_D 36
#define MOBILE_RAIL_COL0_W MOBILE_RAIL_COL_W
#define MOBILE_RAIL_COL1_W MOBILE_RAIL_COL_W
#define MOBILE_RAIL_W (MOBILE_RAIL_COL_W * MOBILE_RAIL_COLS)
#define MOBILE_RAIL_H MOBILE_RAIL_COL_H

/** `classic_invback`, at its own size. The drawer is the 2004 side panel.
 *  The OldSchool plate (1031) is the same 190x261, which is not luck: both
 *  frames' side panels are authored to that box. */
#define MOBILE_PANEL_W 190
#define MOBILE_PANEL_H 261

/*
 * The OldSchool rail: OSM's tab columns, two of them side by side.
 *
 * Interface 601 lays each column as seven 40x40 stones on a 39-row stride --
 * each stone overlapping the one above by a row, which is how the bevels
 * close into one strip -- inside a dark 9-slice plate three pixels wide. Its
 * left column has six tabs and an expand stone, its right seven, and its
 * logout stone stands elsewhere; this frame has fourteen tabs and no expand,
 * so the logout takes the seventh cell of the left column and the columns are
 * OSM's own order otherwise. @see MOBILE_O_COLUMN.
 */
#define MOBILE_O_STONE 40
#define MOBILE_O_STRIDE 39
#define MOBILE_O_BORDER 3
#define MOBILE_O_COL_H (MOBILE_O_STRIDE * (MOBILE_RAIL_ROWS - 1) + MOBILE_O_STONE)
#define MOBILE_O_RAIL_W (MOBILE_O_STONE * MOBILE_RAIL_COLS + 2 * MOBILE_O_BORDER)
#define MOBILE_O_RAIL_H (MOBILE_O_COL_H + 2 * MOBILE_O_BORDER)

/** OSM's two columns in screen order, top to bottom: `tabs_left` is
 *  stone3,4,5,6,0,2 (+expand), `tabs_right` stone1,12,13,7,9,8,11. */
static unsigned char const MOBILE_O_COLUMN[MOBILE_RAIL_COLS][MOBILE_RAIL_ROWS] = {
    { 3, 4,  5,  6, 0, 2, 10 },
    { 1, 12, 13, 7, 9, 8, 11 },
};

/*
 * The OldSchool chat PACK's container: interface 162 mounts into a 519x165
 * layer and owns the text, buttons, actions and scrollbar. On that lane the
 * frame does not place a second chat surface; retained decoration facets dress
 * the pack in Stone Drawer art. @see mobile_chat_decoration_update.
 *
 * The DEFAULT and not the answer -- the four OldSchool toplevels each name
 * their own container and a resizable one need not be the fixed frame's size.
 * @see mobile_chat_native.
 */
#define MOBILE_O_CHAT_W_DEFAULT 519
#define MOBILE_O_CHAT_H_DEFAULT 165

/*
 * The OldSchool orb pack's block, relative to the map WINDOW.
 *
 * Interface 160 lays its four orbs out inside a 207x197 block the resizable
 * toplevels put 29 columns left of the ring and ten rows below its top; the
 * ring's map window is at (24, 8) in it. Stated against the window rather
 * than the ring so it holds for every housing this frame offers -- the
 * window is the one thing all three have at a measured place.
 */
#define MOBILE_O_ORBS_DX (-29 - 24)
#define MOBILE_O_ORBS_DY (10 - 8)
#define MOBILE_O_ORBS_W 207
#define MOBILE_O_ORBS_H 197
/* The activity adviser inside that block, at the resizable and mobile
 * toplevels' own spot under the run orb (torirs_gridmaster_pos). The one
 * child of the pack the TOPLEVEL positions: over the Fixed toplevel it would
 * otherwise sit right-aligned, inside the map circle. */
#define MOBILE_O_ADVISER_DX 85
#define MOBILE_O_ADVISER_DY 143
#define MOBILE_O_ADVISER_W 34
#define MOBILE_O_ADVISER_H 34

/*
 * The map housing: a RING, with the scene showing through everywhere it is not.
 *
 * Not the 2004 `mapback`, which is an opaque plate with a round window punched
 * in it. A plate is right for a frame that fills the screen and wrong for one
 * that floats on it: its square corners sit on the world, and the 2004 compass
 * hangs off its top-left as a square of its own because the plate is what used
 * to hide the corners. A ring has no corners to hide.
 *
 * The two holes are NOT given as numbers here. They are read off the art at
 * load time -- the ring states where its windows are and what shape they are,
 * and a constant beside it is a second copy of that which can only ever drift.
 * @see mobile_build_holes. These are the fallbacks used for the one declaration
 * that may happen before the picture has landed.
 */
#define MOBILE_MAP_W 233
#define MOBILE_MAP_H 168
#define MOBILE_MAP_HOLE_X 24
#define MOBILE_MAP_HOLE_Y 8
#define MOBILE_MAP_HOLE_W 152
#define MOBILE_MAP_HOLE_H 152
#define MOBILE_COMPASS_X 5
#define MOBILE_COMPASS_Y 5
#define MOBILE_COMPASS_W 35

/*
 * The chat block: the sheet, a stone bar under it, and ONE rectangle between
 * them.
 *
 * The bar used to be `backbase1`, the 2004 frame's bottom-left base plate, and
 * it is the wrong shape for a floating sheet twice over: it is 496 wide against
 * the sheet's 479, and its top edge is CUT rather than straight -- it is a
 * corner piece, shaped to mate with the surround above and beside it. Floated
 * on the scene with nothing to mate with, those two facts read as one: a chat
 * box with a notch out of its side and a ragged seam across it.
 *
 * So the bar is the plain stone strip tiled to the sheet's own width, and the
 * two are stacked flush.
 *
 * SIZED BY THE LANE, and the two numbers below are only what to fall back on.
 * How wide the chat surface is, is a fact about the revision and not about
 * this frame: a 2004 revconfig authors `chat_region` at 479x96 and the chat
 * renderer's own geometry agrees with it -- a 463-wide message column, the
 * scrollbar at x+463, the rule at y+77 -- while an OldSchool cache mounts
 * interface 162 into a 519x165 layer whose pack is built against those. Both
 * were constants here, which made this frame a claim about every revision it
 * would ever be loaded on; it now asks. @see mobile_chat_native.
 *
 * The fallbacks are the 2004 pair, because a lane that will not answer is one
 * whose chat is the builtin, and the builtin is 479x96 by construction.
 */
#define MOBILE_CHAT_W_DEFAULT 479
#define MOBILE_CHAT_H_DEFAULT 96
#define MOBILE_STRIP_H 36

/*
 * The sheet's TORN edge, and why the block sits seventeen columns in.
 *
 * `chat_sheet_rs289.png` is not `chatback.dat`. The cache's sheet is a flat
 * 479x96 rectangle with four hard corners -- which is the one shape a docked
 * frame can wear, because a surround explains it, and the one shape a FLOATING
 * sheet cannot: on the scene those corners read as a beige box someone dropped
 * on the grass. The parchment is the same 479x96 surface inside a torn fringe,
 * and the fringe is the whole point of it.
 *
 * It is no longer ONE picture, and that is what these numbers are now about.
 * A single 517x130 sheet has to be scaled to reach a bigger chatbox, and a
 * scaled tear is the one thing a tear must not be: the fringe's whole job is
 * to look like paper that was pulled apart, and pulling it further apart in
 * the resampler turns the tears into long smeared fingers along whichever edge
 * grew. So the sheet is cut into nine pieces -- four corners that are never
 * resized, four edges that REPEAT along their own axis, and a middle that
 * repeats both ways -- and composed at whatever size the chat surface needs.
 * @see mobile_compose_paper, and tools/cut_chat_sheet_tiles.py, which cuts
 * them and can prove the numbers below off its own output (`--proof`).
 *
 * The FRINGE is what survives the change unaltered, and that is the property
 * that makes a nine-patch the right shape for this: the corners are the same
 * pixels at every size and the edges repeat, so how far in from each edge the
 * paper is solid does not depend on how big the sheet is. Measured off the
 * composition at four sizes, it is 17 columns to the left, 21 to the right and
 * 17 rows top and bottom -- the same numbers the single sheet had, so a
 * composition at 517x130 reproduces the old picture's geometry exactly.
 *
 * The surface moves, not the art. The chat builtin's geometry is the 2004
 * client's and every number in it is fixed -- a 463-wide message column, the
 * scrollbar at x+463, the input line at y+77 -- so the block is pinned flush
 * to the bottom-left by the PARCHMENT, and the chat, the filter buttons and
 * the tap-blocker are all inset by the fringe so the core lands where they
 * meet it.
 */
#define MOBILE_PAPER_FRINGE_L 17
#define MOBILE_PAPER_FRINGE_T 17
#define MOBILE_PAPER_FRINGE_R 21
#define MOBILE_PAPER_FRINGE_B 17

/**
 * How many real, distinct pieces back one repeating edge, at most.
 *
 * A ceiling and not a promise: tools/cut_chat_sheet_tiles.py answers with
 * however many wraps its source actually supports below its quality gate,
 * which for this sheet is 3 for the top edge, 2 for the bottom, and 1 for the
 * left and right -- the left edge's tear only repeats well in one place, and
 * a second "variant" there would have been a worse seam than drawing the
 * single tile twice. Slots the cutter did not fill are NULL in
 * MOBILE_IMAGE_FILE, exactly like MOBILE_TAB_COUNT's missing tab-7 icon.
 */
#define MOBILE_PAPER_EDGE_VARIANTS_MAX 3
/** The middle tile is grain, not a shape, so it does not share the edges'
 *  scarcity: three disjoint low-blotch patches fit the source every time. */
#define MOBILE_PAPER_FILL_VARIANTS 3
/**
 * Inked rows below the surface's last, and the reason the block sits higher
 * than the surface alone would.
 *
 * The sheet used to be pinned by its surface -- chat_y = strip_y - chat_h --
 * which put the surface's last row on the strip and left the torn edge hanging
 * BELOW it, under the filter buttons, which then drew over the paper. So the
 * block is pinned by the ART's last inked row instead.
 *
 * Ink and not the piece: the bottom corners have empty rows past their last
 * tear, and pinning those would leave a visible gap where the art has nothing.
 * MOBILE_PAPER_FRINGE_B is 17 rows of fringe and 5 of them are empty, so 12
 * rows of it are drawn.
 */
#define MOBILE_PAPER_INK_B 12
/**
 * Extra clear air between the torn edge and the button row, on top of that.
 *
 * Zero, and that is the wanted value rather than a disabled feature: the paper
 * belongs directly above the buttons, and MOBILE_PAPER_INK_B alone already
 * puts it there. Pinning the art's last inked row to the row above the strip
 * leaves the two rows the button lift adds, which is the whole clearance the
 * frame wants -- the sheet reads as sitting ON the row rather than floating
 * over it.
 *
 * It stays a named number because it is the one knob here that is taste rather
 * than measurement, and because a positive value is the only way to say "hold
 * them further apart" without disturbing the fringe, which is not free to
 * move. @see MOBILE_PAPER_INK_B.
 */
#define MOBILE_CHAT_STRIP_GAP 0

/** The art that backs a chat surface of `w` by `h`: the fringe is added to it
 *  on all four sides. @see MOBILE_PAPER_FRINGE_L. */
#define MOBILE_PAPER_ART_W(w) ((w) + MOBILE_PAPER_FRINGE_L + MOBILE_PAPER_FRINGE_R)
#define MOBILE_PAPER_ART_H(h) ((h) + MOBILE_PAPER_FRINGE_T + MOBILE_PAPER_FRINGE_B)

/**
 * The chat surface's top row, and with it the whole block's.
 *
 * A macro because the layout pass and the tap-blocker both spelled it out
 * separately once; that is exactly the pair that drifts when the sheet art
 * changes shape, and the blocker landing seventeen rows off the sheet is a
 * frame that swallows taps on the world and passes taps on the chat. Only the
 * layout evaluates it now -- the blocker reads the answer back out of
 * g_frame.chat_y, which is the same number and is also the only one that
 * survives a soft keyboard.
 *
 * The bottom is a PARAMETER and is not the canvas's: with a keyboard up the
 * block hangs from the safe bottom instead. @see mobile_layout. So is the
 * surface's height, which is the LANE's rather than this frame's.
 * @see mobile_chat_native.
 */
#define MOBILE_CHAT_Y(bottom, chat_h)                                                              \
    ((bottom) - MOBILE_STRIP_H - (chat_h) - MOBILE_PAPER_INK_B - MOBILE_CHAT_STRIP_GAP)

/*
 * The four filter buttons, spread evenly across the bar.
 *
 * Evenly, and not at the classic frame's own 6/135/273/408: those are measured
 * against a 496-wide plate and the last of them ends at 508, so on a 479-wide
 * bar the Report button would hang off the end. An even spread is a LAYOUT
 * decision the moment the bar stops being the one the numbers came from.
 */
#define MOBILE_CHAT_BUTTON_COUNT 4
#define MOBILE_CHAT_BUTTON_W 100
#define MOBILE_CHAT_BUTTON_H 32
#define MOBILE_CHAT_BUTTON_LIFT ((MOBILE_STRIP_H - MOBILE_CHAT_BUTTON_H) / 2)

/*
 * The OldSchool pack's own bar is 519x23, and the 2004 strip that dresses it
 * is composed at exactly that box -- with a stone cut for every filter the
 * pack has, which is eight, and nothing at all between them.
 *
 * Two wrong answers came before this one. The pack's eight controls were first
 * re-dressed one at a time in a shrunken classic stone, which squashed the
 * bevel and left an OldSchool button-shaped rectangle behind every caption;
 * then the whole 519x23 LAYER was replaced, which took the eight filters away
 * with it and left four 2004 buttons where the player had All, Game, Public,
 * Private, Channel, Clan, Trade and Report.
 *
 * A 2004 chat filter is a STONE, and only the stone. The picture composed at
 * the bar's box is one stone per filter, at the boxes the eight plate roles
 * report, with every column between them and beyond the last one left CLEAR --
 * and the plates are held with no art. Everything written on them stays the
 * pack's: the name, and the green mode line under it that says On, Friends or
 * Off.
 *
 * NOT a slab with the stones cut into it. The band used to be tiled from the
 * clean rock beside the source hollow and the hollows re-cut into it, which is
 * how 2004 authored the sprite -- but 2004's slab was the chat's whole
 * backdrop, and here the backdrop is the torn parchment sheet. Tiling the rock
 * as well laid a second, rectangular backdrop across the sheet's bottom rows
 * and out to the corner past the last filter: the row read as a stone bar with
 * buttons pressed into it rather than as eight buttons standing on the paper.
 * So the band carries the stones and nothing else.
 *
 * @see MOBILE_CHAT_BUTTON_CAP, mobile_compose_classic_bar.
 */
/*
 * Every stone on this frame is `chat_button.png`, and there is nothing else.
 *
 * The slab is not read at all any more. Both paths used to cut their stone out
 * of `chat_plate.png` -- the 2004 strip -- one at a 100x32 WINDOW and one at a
 * measured silhouette, and both were answering a question the assets had
 * already answered: the sprite beside them is the button with the slab taken
 * off it, hand-masked at 295x97 so its alpha follows the stone's own outline.
 * It shipped on 2026-08-27 and nothing read it until the dark rectangle it
 * exists to prevent had been reported three times.
 *
 * So there is no silhouette to measure, no rock to fill with, and no corner to
 * round: a cut-out carries all three. What is left is one number.
 */
/** The sprite's rounded END, in its own columns: the three-slice copies this
 *  much from each side and resamples what is between, so a narrow cell keeps
 *  the shape of the ends instead of squashing them. MEASURED on the alpha --
 *  column coverage climbs from 0 and first reaches the full 97 rows at x=24,
 *  and falls again after x=240 -- so 25 of 295, and it is carried as source
 *  columns because the picture is reduced before it is sliced. */
#define MOBILE_CHAT_BUTTON_CAP 25
/** As many stones as a bar can be asked for: an OldSchool chatbox has eight
 *  filters and a 2004 one has four. */
#define MOBILE_CHAT_CELL_MAX 8

/** One stone to cut, in the BAR's own columns and rows. */
struct MobileChatCell
{
    int x;
    int y;
    int w;
    int h;
};

/** One small-font chat line of parchment beyond the cache's text backing. */
#define MOBILE_O_PAPER_PAD_T 14
#define MOBILE_O_PAPER_PAD_B 14

/**
 * Where filter button `i` starts, across a bar as wide as the chat surface.
 *
 * A function of the width and no longer of a constant, because the width is
 * the lane's: a bar spread across a hard-coded 479 under a 519-wide OldSchool
 * chatbox puts the last button forty columns short of the corner.
 *
 * Evenly, and not at the classic frame's own 6/135/273/408: those are measured
 * against a 496-wide plate and the last of them ends at 508, so on a 479-wide
 * bar the Report button would hang off the end. An even spread is a LAYOUT
 * decision the moment the bar stops being the one the numbers came from.
 *
 * The fringe is in here so the two call sites cannot drift apart over it.
 */
static int
mobile_chat_button_x(
    int i,
    int strip_w)
{
    int const cell = strip_w / MOBILE_CHAT_BUTTON_COUNT;

    assert(i >= 0);
    assert(i < MOBILE_CHAT_BUTTON_COUNT);
    assert(strip_w > 0);
    return i * cell + ((cell - MOBILE_CHAT_BUTTON_W) / 2) + MOBILE_PAPER_FRINGE_L;
}

/*
 * The chat switch: the grey interface button, at its own size.
 *
 * `miscgraphics2` frame 0 -- the button the 2004 logout and settings panels are
 * built from. It is a BUTTON, which is what this is, where the tab stones are
 * sockets in a rail; borrowing a stone made the switch look like a piece of the
 * rail that had come loose in the opposite corner.
 */
#define MOBILE_TOGGLE_W 36
#define MOBILE_TOGGLE_H 25
/** On the OldSchool family the two switches are 601's own 40x40 stones. */
#define MOBILE_O_TOGGLE_W MOBILE_O_STONE
#define MOBILE_O_TOGGLE_H MOBILE_O_STONE
/** The gap between the chat switch and the keyboard switch beside it. */
#define MOBILE_TOGGLE_GAP 4
/*
 * How much of its own size the CHAT glyph keeps.
 *
 * The atlas is cut for OldSchool Mobile's own 40-pixel buttons and these
 * switches are 36x25, so the bubbles at full size hang over the plate on three
 * sides. Two thirds fills the button and leaves a margin: 21x18 inside 36x25.
 *
 * Halving fitted too, and left them looking lost on the plate -- which is the
 * hazard of picking a fraction for the arithmetic rather than for the picture.
 * Two thirds is not a clean box filter the way a half is, but the scaler
 * averages each destination pixel's whole footprint, so what it costs is a
 * little softness at this size rather than the staircase a point sample would
 * give.
 */
#define MOBILE_ICON_NUM 2
#define MOBILE_ICON_DEN 3
/*
 * And how much of its own size the KEYBOARD glyph keeps.
 *
 * MEASURED, not chosen: `osm_keyboard`'s 33x36 canvas carries its ink at
 * x2..30, y13..25 -- a 29x13 keyboard. Centring that canvas in a 36x25 switch
 * leaves the ink 3 columns of stone on the left and 4 on the right, against
 * the 7 the scaled chat bubble gets, so the keyboard's black frame came within
 * a pixel or two of the plate's inner bevel and read as squeezed beside it.
 *
 * Three quarters takes the 29 columns of ink to 22 and buys back the same 7
 * columns of margin the bubble has. The fraction is derived from that number:
 * 36 - 2*7 = 22, and 22/29 rounds to 3/4.
 *
 * The ink NEVER left the plate -- this is a margin, not an overflow, which is
 * why it survived every unmagnified review.
 */
#define MOBILE_KEY_ICON_NUM 3
#define MOBILE_KEY_ICON_DEN 4
/*
 * And how much of its own size the PLUGINS wrench keeps.
 *
 * Measured the same way, off sprite 785's own canvas: 33x36 carrying a 24x24
 * wrench at x5..28, y6..29. HEIGHT is what binds this one, which is what makes
 * it different from the other two -- the switch is 36 wide and only 25 tall,
 * and a 24-row glyph in a 25-row button has half a row of stone either side.
 *
 * So the fraction comes from the rows: give it the 3 the chat bubble gets
 * (25 - 2*3 = 19) and 19/24 rounds to 4/5. That takes the ink to 19x19 and
 * leaves 8 columns of margin across, a little more than the 7 the other two
 * were fitted to -- which is right for a glyph that is square where they are
 * wide, and is the difference between "centred" and "cramped".
 */
#define MOBILE_PLUGIN_ICON_NUM 4
#define MOBILE_PLUGIN_ICON_DEN 5

/** What a bank or a dialogue is authored for. The cache's own interfaces are
 *  built against this box, so it is placed and never resized -- only moved. */
#define MOBILE_MODAL_W 512
#define MOBILE_MODAL_H 334

/*
 * The smallest canvas this frame still computes on.
 *
 * Height is the binding one and it is derived, not chosen: the drawer is pinned
 * to the bottom margin and the map housing to the top, so the canvas has to
 * hold both without the one climbing into the other --
 * MARGIN + MAP_H + PANEL_H + MARGIN. Width is a floor of taste rather than of
 * arithmetic: the rail and the drawer need 262 columns between them and the
 * world needs the rest to be worth looking at.
 */
#define MOBILE_MIN_H (MOBILE_MARGIN + MOBILE_MAP_H + MOBILE_PANEL_H + MOBILE_MARGIN)
#define MOBILE_MIN_W 640

_Static_assert(
    MOBILE_MIN_W > MOBILE_RAIL_W + MOBILE_PANEL_W + (2 * MOBILE_MARGIN),
    "the rail and the drawer must fit side by side at the smallest canvas");
_Static_assert(
    MOBILE_MIN_H > MOBILE_RAIL_H + (2 * MOBILE_MARGIN),
    "the rail must fit between the margins at the smallest canvas");

/* ----------------------------------------------------------------- assets */

enum MobileImage
{
    /** The two map housings the `housing` setting chooses between. */
    IMG_MAPBACK = 0,
    IMG_MAPBACK_RING,
    IMG_INVBACK,
    /**
     * The parchment. FOUR corners cut once, then a variant SET at each
     * repeating position -- up to MOBILE_PAPER_EDGE_VARIANTS_MAX real, distinct
     * strips for each edge and MOBILE_PAPER_FILL_VARIANTS for the middle,
     * picked between per repeat by a position hash so the pattern does not
     * read as one stamp walked across the sheet. @see mobile_compose_paper.
     *
     * Not one picture, because one picture can only reach a bigger chatbox by
     * being scaled, and a scaled tear stops being a tear -- and not a SINGLE
     * repeating tile either, because one tile laid end to end is still a
     * stamp; the eye catches the interval before it catches anything else.
     *
     * The corner box is not a constant here -- it is whatever the top-left
     * piece measures -- so a recut at a different corner size needs nothing
     * changed on this side. What DOES have to agree is the fringe, which the
     * cutter can print off its own output. @see MOBILE_PAPER_FRINGE_L.
     */
    IMG_PAPER_0,
    IMG_PAPER_TOP_0 = IMG_PAPER_0 + 4,
    IMG_PAPER_BOTTOM_0 = IMG_PAPER_TOP_0 + MOBILE_PAPER_EDGE_VARIANTS_MAX,
    IMG_PAPER_LEFT_0 = IMG_PAPER_BOTTOM_0 + MOBILE_PAPER_EDGE_VARIANTS_MAX,
    IMG_PAPER_RIGHT_0 = IMG_PAPER_LEFT_0 + MOBILE_PAPER_EDGE_VARIANTS_MAX,
    IMG_PAPER_FILL_0 = IMG_PAPER_RIGHT_0 + MOBILE_PAPER_EDGE_VARIANTS_MAX,
    IMG_STONE = IMG_PAPER_FILL_0 + MOBILE_PAPER_FILL_VARIANTS,
    /** The plate under a classic tab row, cleaned up. Both columns are this
     *  one picture, the second mirrored. @see MOBILE_RAIL_COL_W. */
    IMG_PLATE,
    /** The grey button the 2004 interfaces use for logout and the settings
     *  toggles -- `miscgraphics2` frame 0. The chat switch wears it. */
    IMG_SWITCH,
    /** The chat filter button, ALONE: the 2004 stone with the slab it was cut
     *  out of already taken off it, hand-masked at 295x97. Shipped since
     *  2026-08-27 and read by nothing until now -- both chat paths cut their
     *  own stone out of the 2004 STRIP instead, and a piece of that strip is a
     *  stone in a dark ragged rectangle the moment the chat stands on the
     *  world rather than on rock. It is the only chat art this frame reads.
     *  @see MOBILE_CHAT_BUTTON_CAP. */
    IMG_CHAT_BUTTON,
    /*
     * The two switch glyphs, from OldSchool's own sprite set.
     *
     * The chat one is `options_icons` frame 5, the pair of speech bubbles --
     * and it is the RIGHT one rather than a lookalike: interface 601's chat
     * filter toggle sets exactly that graphic on itself
     * (`cc_setgraphic("options_icons,5")` in torirs_osm_chatbox_bind), so this
     * is the icon OldSchool Mobile puts on the button that does this job.
     *
     * The keyboard is `osm_keyboard` (sprite 4794), copied whole from the
     * canonical export in `res/osm_keyboard_icon_sprite4794.png`. Its 33x36
     * canvas is mostly transparent around a centred 29x13 keyboard. That
     * authored canvas is intentional: mobile_draw_icon centres the canvas in
     * either switch family and the visible keys land in the middle of the
     * stone without a second offset table.
     *
     * Both are shipped on their authored canvases and only the CHAT glyph is
     * scaled, because it is cut for a 40px mobile button and overhangs a 36x25
     * switch.
     */
    IMG_ICON_KEYBOARD,
    IMG_ICON_CHAT,
    /**
     * The PLUGINS glyph: sprite 785, the OSRS wrench.
     *
     * The same picture the client's plugin launcher wears everywhere else --
     * the engine bakes it as TORIRS_CHROME_SKIN_PLUGIN_ICON rather than
     * resolving 785 at runtime, and this frame ships it rather than reading
     * it, for the one reason: on any cache that is not OldSchool's, 785 is
     * some unrelated image, and a launcher wearing the wrong picture is worse
     * than one wearing none.
     *
     * Also a 33x36 canvas, kept whole like the keyboard's, with its ink a
     * centred 24x24. @see MOBILE_PLUGIN_ICON_NUM.
     */
    IMG_ICON_PLUGINS,
    /** The 2004 compass rose. On a 2004 lane the cache's own IS this picture
     *  and the skin keeps it; on an OldSchool lane the cache's rose is
     *  OldSchool's, and a map plate cut for this one wants this one. */
    IMG_COMPASS,
    /** The three redstone shapes, in the order the classic frame's own stone
     *  index numbers them. @see MOBILE_TAB_STONE. */
    IMG_REDSTONE_0,
    IMG_REDSTONE_1,
    IMG_REDSTONE_2,
    IMG_SIDEICON_0,

    /* -- the OldSchool family, cache.osrs239's own mobile pieces -- */
    /** The 40x40 tab stone, idle and selected (`tli_button01_square_40x40`
     *  0 and 2). Also the plate under the two switches. */
    IMG_O_STONE = IMG_SIDEICON_0 + MOBILE_TAB_COUNT,
    IMG_O_STONE_LIT,
    /** The dark 9-slice 601 draws around a tab column: corners, edges and
     *  middle in reading order, 3x3 each. Composed into a plate per rail. */
    IMG_O_BORDER_0,
    /** `border_map_compass`, the thin ring around the map and the compass. */
    IMG_O_MAPBACK_RING = IMG_O_BORDER_0 + 9,
    /** The fixed frame's 190x261 side panel plate, which the drawer wears. */
    IMG_O_DRAWER,
    IMG_O_SIDEICON_0,

    MOBILE_IMG_COUNT = IMG_O_SIDEICON_0 + MOBILE_TAB_COUNT
};

/*
 * Which set of pieces the frame is assembled from.
 *
 * A FAMILY and not a lane: the rail, the drawer, the switches and the map
 * ring are all authored twice, once from each cache, and the choice between
 * them is the player's (`art`), with classic as Auto. What is NOT a family
 * choice is chat/orb behavior, which remains the lane's packs or the client's
 * builtins whatever the stones look like. @see mobile_lane_oldschool.
 */
enum MobileFamily
{
    FAMILY_CLASSIC = 0,
    FAMILY_OLDSCHOOL = 1
};

/*
 * Which stone each tab wears, and which way round -- the classic frame's own
 * table, copied.
 *
 * `stone` indexes the three redstone shapes, `flip_h`/`flip_v` are the mirrors
 * the 2004 frame applies (the right half of each row is the left half mirrored,
 * and the bottom row is the top row upside down), and `extent` is the classic
 * box's WIDTH -- which after the quarter turn is the cell's height.
 *
 * Copied from revconfig/rs245_2lc's `[layout:fixed]` by way of gameframe.c's
 * classic_fixed, and copied rather than re-derived for the same reason that one
 * is: the rhythm of wide and narrow stones is what the row looks like, and a
 * uniform stride is a different picture that happens to have fourteen cells.
 */
struct MobileTabStone
{
    unsigned char stone;
    unsigned char flip_h;
    unsigned char flip_v;
    /** The classic box's width, which after the turn is the cell's HEIGHT. */
    unsigned char extent;
    /** Where the box starts ALONG its plate -- classic x minus the plate's x.
     *  After the turn this is the cell's y down the column, which is why the
     *  stones are placed rather than stacked: the row has a gap in the middle
     *  and stacking would close it. */
    unsigned char along;
    /** And ACROSS it -- classic y minus the plate's y -- with the box's own
     *  height, which after the turn are the cell's x and width. */
    unsigned char across;
    unsigned char thickness;
};

/*
 * Plate origins: `backhmid1` is blitted at 516,160 and `backbase2` at 496,466,
 * so `along` and `across` below are each tab's classic x,y less those.
 */
static struct MobileTabStone const MOBILE_TAB_STONE[MOBILE_TAB_COUNT] = {
    /* the top row, on backhmid1, which becomes the left column */
    { 0, 0, 0, 38, 22,  10, 36 },
    { 1, 0, 0, 33, 54,  8,  36 },
    { 1, 0, 0, 38, 82,  8,  36 },
    { 2, 0, 0, 33, 110, 8,  36 },
    { 1, 1, 0, 33, 153, 8,  36 },
    { 1, 1, 0, 33, 181, 8,  36 },
    { 0, 1, 0, 38, 209, 9,  36 },
    /* the bottom row, on backbase2, which becomes the right column */
    { 0, 0, 1, 34, 42,  0,  36 },
    { 1, 0, 1, 30, 74,  0,  37 },
    { 1, 0, 1, 30, 102, 0,  37 },
    { 2, 0, 1, 44, 130, 1,  35 },
    { 1, 1, 1, 30, 173, 0,  37 },
    { 1, 1, 1, 30, 201, 0,  37 },
    { 0, 1, 1, 34, 229, 0,  36 },
};

/*
 * Thirteen icons for fourteen tabs.
 *
 * The 2004 atlas has no picture for tab 7 -- LostCity has no server constant
 * and no default if_settab for it -- so the table is shifted from tab 8 on. The
 * gap is named here rather than being an off-by-one somebody rediscovers from a
 * missing backpack. @see gameframe.c, which carries the same hole.
 */
static char const* const MOBILE_IMAGE_FILE[MOBILE_IMG_COUNT] = {
    [IMG_MAPBACK] = "map_housing.png",
    [IMG_MAPBACK_RING] = "map_housing_ring.png",
    [IMG_INVBACK] = "drawer.png",
    /* The parchment's nine pieces, cut from `chat_sheet_rs289.png` by
     * tools/cut_chat_sheet_tiles.py -- which is also where the sheet itself is
     * documented, and why it is not `chatback.dat`, the cache's flat
     * rectangle. @see IMG_PAPER_0, mobile_compose_paper. */
    [IMG_PAPER_0 + 0] = "chat_paper_tl.png",
    [IMG_PAPER_0 + 1] = "chat_paper_tr.png",
    [IMG_PAPER_0 + 2] = "chat_paper_bl.png",
    [IMG_PAPER_0 + 3] = "chat_paper_br.png",
    /* Three real wraps -- the widest room of any edge, so the cutter's
     * quality gate let the most through. */
    [IMG_PAPER_TOP_0 + 0] = "chat_paper_top_1.png",
    [IMG_PAPER_TOP_0 + 1] = "chat_paper_top_2.png",
    [IMG_PAPER_TOP_0 + 2] = "chat_paper_top_3.png",
    /* Two: the bottom tear only wraps well in two places in this sheet. */
    [IMG_PAPER_BOTTOM_0 + 0] = "chat_paper_bottom_1.png",
    [IMG_PAPER_BOTTOM_0 + 1] = "chat_paper_bottom_2.png",
    [IMG_PAPER_BOTTOM_0 + 2] = NULL,
    /* One: the left tear's only good wrap. A second slot here would have
     * been the next-best candidate at twenty times the seam cost -- worse
     * than repeating the one real piece, so the cutter left it out rather
     * than ship it. @see MOBILE_PAPER_EDGE_VARIANTS_MAX. */
    [IMG_PAPER_LEFT_0 + 0] = "chat_paper_left_1.png",
    [IMG_PAPER_LEFT_0 + 1] = NULL,
    [IMG_PAPER_LEFT_0 + 2] = NULL,
    /* The left variant(s) mirrored -- the right edge's own tear drifts and
     * will not wrap at all. @see tools/cut_chat_sheet_tiles.py. */
    [IMG_PAPER_RIGHT_0 + 0] = "chat_paper_right_1.png",
    [IMG_PAPER_RIGHT_0 + 1] = NULL,
    [IMG_PAPER_RIGHT_0 + 2] = NULL,
    /* Grain, not a shape -- always finds three. */
    [IMG_PAPER_FILL_0 + 0] = "chat_paper_fill_1.png",
    [IMG_PAPER_FILL_0 + 1] = "chat_paper_fill_2.png",
    [IMG_PAPER_FILL_0 + 2] = "chat_paper_fill_3.png",
    [IMG_STONE] = "stone.png",
    [IMG_PLATE] = "rail_back_top_cleaned.png",
    [IMG_SWITCH] = "switch.png",
    [IMG_CHAT_BUTTON] = "chat_button.png",
    [IMG_ICON_KEYBOARD] = "icon_keyboard.png",
    [IMG_ICON_CHAT] = "icon_chat.png",
    [IMG_ICON_PLUGINS] = "icon_plugins.png",
    [IMG_COMPASS] = "compass.png",
    [IMG_REDSTONE_0] = "highlight1.png",
    [IMG_REDSTONE_1] = "highlight2.png",
    [IMG_REDSTONE_2] = "highlight3.png",
    [IMG_SIDEICON_0 + 0] = "sideicon_0.png",
    [IMG_SIDEICON_0 + 1] = "sideicon_1.png",
    [IMG_SIDEICON_0 + 2] = "sideicon_2.png",
    [IMG_SIDEICON_0 + 3] = "sideicon_3.png",
    [IMG_SIDEICON_0 + 4] = "sideicon_4.png",
    [IMG_SIDEICON_0 + 5] = "sideicon_5.png",
    [IMG_SIDEICON_0 + 6] = "sideicon_6.png",
    [IMG_SIDEICON_0 + 7] = NULL,
    [IMG_SIDEICON_0 + 8] = "sideicon_7.png",
    [IMG_SIDEICON_0 + 9] = "sideicon_8.png",
    [IMG_SIDEICON_0 + 10] = "sideicon_9.png",
    [IMG_SIDEICON_0 + 11] = "sideicon_10.png",
    [IMG_SIDEICON_0 + 12] = "sideicon_11.png",
    [IMG_SIDEICON_0 + 13] = "sideicon_12.png",

    [IMG_O_STONE] = "osrs_stone.png",
    [IMG_O_STONE_LIT] = "osrs_stone_lit.png",
    [IMG_O_BORDER_0 + 0] = "osrs_border_0.png",
    [IMG_O_BORDER_0 + 1] = "osrs_border_1.png",
    [IMG_O_BORDER_0 + 2] = "osrs_border_2.png",
    [IMG_O_BORDER_0 + 3] = "osrs_border_3.png",
    [IMG_O_BORDER_0 + 4] = "osrs_border_4.png",
    [IMG_O_BORDER_0 + 5] = "osrs_border_5.png",
    [IMG_O_BORDER_0 + 6] = "osrs_border_6.png",
    [IMG_O_BORDER_0 + 7] = "osrs_border_7.png",
    [IMG_O_BORDER_0 + 8] = "osrs_border_8.png",
    [IMG_O_MAPBACK_RING] = "osrs_map_ring.png",
    [IMG_O_DRAWER] = "osrs_drawer.png",
    [IMG_O_SIDEICON_0 + 0] = "osrs_sideicon_0.png",
    [IMG_O_SIDEICON_0 + 1] = "osrs_sideicon_1.png",
    [IMG_O_SIDEICON_0 + 2] = "osrs_sideicon_2.png",
    [IMG_O_SIDEICON_0 + 3] = "osrs_sideicon_3.png",
    [IMG_O_SIDEICON_0 + 4] = "osrs_sideicon_4.png",
    [IMG_O_SIDEICON_0 + 5] = "osrs_sideicon_5.png",
    [IMG_O_SIDEICON_0 + 6] = "osrs_sideicon_6.png",
    [IMG_O_SIDEICON_0 + 7] = "osrs_sideicon_7.png",
    [IMG_O_SIDEICON_0 + 8] = "osrs_sideicon_8.png",
    [IMG_O_SIDEICON_0 + 9] = "osrs_sideicon_9.png",
    [IMG_O_SIDEICON_0 + 10] = "osrs_sideicon_10.png",
    [IMG_O_SIDEICON_0 + 11] = "osrs_sideicon_11.png",
    [IMG_O_SIDEICON_0 + 12] = "osrs_sideicon_12.png",
    [IMG_O_SIDEICON_0 + 13] = "osrs_sideicon_13.png",
};

/*
 * The four pictures the plugin RASTERISES for itself.
 *
 * Everything above is a file. These four are shapes, and a shape is the one
 * thing the shipped art cannot be: a quarter turn is not a crop, and a circle
 * is not a rectangle with a picture in it. @see image_compose, which exists for
 * exactly the second case.
 */
enum MobileComposed
{
    /** The two alpha cut-outs, read off the housing. Transparent is the window
     *  and opaque is clipped away -- the polarity plugin masks use on every
     *  cache era. @see mobile_build_masks. */
    ART_MINIMAP_MASK,
    ART_COMPASS_MASK,
    /**
     * One turned stone per tab, and a dimmed twin of each.
     *
     * Per TAB rather than per shape, because a tab's stone is a shape AND two
     * mirrors, and there are ten distinct combinations across the fourteen --
     * close enough to fourteen that a table keyed by the thing the draw pass
     * actually has in its hand is simpler than one it has to look up.
     */
    /** The 2004 chat filter button at its 100x32 box. ONE, for all four of
     *  them: the four are the same picture, so four slots held four copies of
     *  it and three of them were a decode and an image name the layer cannot
     *  spend elsewhere. An OldSchool bar wears the whole strip instead,
     *  composed at the box the pack reports. @see mobile_bar_art. */
    ART_CHAT_BUTTON,
    /** The chat glyph, scaled to fit the switch. The keyboard keeps
     *  `osm_keyboard`'s authored 33x36 transparent canvas.
     *  @see MOBILE_ICON_NUM. */
    ART_ICON_CHAT,
    /** The keyboard glyph, fitted to the switch the same way.
     *  @see MOBILE_KEY_ICON_NUM. */
    ART_ICON_KEYBOARD,
    /** The plugins wrench, fitted to the switch off its own rows.
     *  @see MOBILE_PLUGIN_ICON_NUM. */
    ART_ICON_PLUGINS,
    /** The two backing plates, turned: one whole column each. */
    ART_PLATE_0,
    ART_PLATE_1,
    /**
     * One turned stone per tab, drawn ONLY on the tab that is open.
     *
     * There is no dimmed twin. The redstone is the 2004 frame's PRESSED
     * highlight -- red is what "this one is open" looks like -- so a rail whose
     * every cell wore one said fourteen tabs were open at once, and the real
     * selection had to be picked out by shade. The plate is what the other
     * thirteen stand on, which is what the desktop frame does too.
     */
    ART_STONE_0,
    /** The OldSchool rail's plate: the dark 9-slice at the rail's size, both
     *  columns on one. @see mobile_compose_nine_slice. */
    ART_O_RAIL = ART_STONE_0 + MOBILE_TAB_COUNT,

    MOBILE_ART_COUNT
};

/*
 * A map housing: the art, and how big it is.
 *
 * The WINDOWS are not here. They are read off the picture at load time -- see
 * mobile_build_masks -- because a housing's windows are wherever that picture's
 * windows were drawn, and a box written down beside it is a second copy of that
 * which can only drift. The size is here because the layout needs it before the
 * read completes, to put the housing in the corner at all.
 */
struct MobileHousing
{
    int art;
    int width;
    int height;
};

/** The three choices, in the order the `housing` setting lists them; Auto
 *  picks by family. @see mobile_housing. */
static struct MobileHousing const MOBILE_HOUSING[] = {
    { IMG_MAPBACK,        233, 168 },
    { IMG_MAPBACK_RING,   182, 166 },
    { IMG_O_MAPBACK_RING, 182, 166 },
};
#define MOBILE_HOUSING_COUNT 3
#define MOBILE_HOUSING_AUTO MOBILE_HOUSING_COUNT

/** One window in the housing, in the housing's own pixels. */
struct MobileHole
{
    int x;
    int y;
    int w;
    int h;
    int area;
    /** One pixel of this window (row * width + col), to flood it back from.
     *  @see mobile_compose_window. */
    int seed;
};

/*
 * Where the chosen housing's windows are, and how big it turned out to be.
 *
 * Seeded with the default housing's measurements so the one declaration that
 * can happen before the picture lands still puts a map in the corner, and
 * replaced by what the read actually found the moment it does.
 */
struct MobileState;

/*
 * A picture this frame can both READ and DESCRIBE.
 *
 * The two halves want different things from one picture and neither can derive
 * the other's. The composers read PIXELS, so they need the handle; a Porcelain
 * description names an asset by NAME and never takes a handle, so a plan built
 * out of handles cannot be described at all. Carrying the pair together is what
 * lets `stone.png` and a chat bar this frame cut for itself ten milliseconds
 * ago sit in the same field.
 *
 * A zero `ref` beside a live `name` is the ordinary state for the first frames
 * after start: the name comes from the table and the bytes are still crossing
 * the IO queue.
 */
struct MobileArt
{
    char const* name;
    struct ToriRS_ImageRef ref;
};

/** Callback-scoped native V2 services threaded through layout helpers. */
/** One picture to blit, in canvas coordinates. Built by the layout pass. An
 *  entry with `op` is a tap blocker: an owned control the size of the box,
 *  wearing a blank face, so a tap on chrome does not walk the player. */
struct MobileBlit
{
    /*
     * The NAME this piece is described under, and the reason it is a name.
     *
     * It used to be `piece.%02d`, the entry's index in this table -- and the
     * table's LENGTH moves: the drawer's backing is recorded only while the
     * drawer is open, the parchment only while the sheet is up, and the second
     * switch only on a lane that has one. Every piece behind the one that came
     * or went therefore changed key, and a key that changes is a new widget:
     * the layer creates it at the END of the parent's children, which is over
     * every stone and icon the rail described earlier. Opening the drawer put
     * the right-hand plate on top of its own seven icons -- the column went
     * blank and stopped answering taps, because the plate was in front of it.
     *
     * So a piece is named for WHAT IT IS. The identity then survives every
     * other piece coming and going, which is the one property the reconciler
     * needs from it. The `piece.` stem stays: it is the family this frame's
     * own chrome is counted by, as against its rocks, stones and icons.
     * @see mobile_describe_chrome.
     */
    char const* key;
    struct MobileArt image;
    int x;
    int y;
    int w;
    int h;
    char const* op;
    /**
     * Drawn BEHIND a live surface instead of over the scene.
     *
     * Every other piece of this frame's chrome is anchored over the VIEWPORT,
     * which is right for a piece nothing of the lane's stands on -- the live
     * surfaces are raised over the whole of this plugin's chrome afterwards,
     * so a piece anchored there ends up under them. This field is for the
     * pieces that raise cannot carry, and there are two kinds.
     *
     * One is a surface the raise does not REACH. On a 2004 lane the chat is
     * the client's own builtin and the packs the server mounts into
     * `chatbox:chatmodal` sit under it, and raising the chat does not take
     * them with it. What was on screen was a blank sheet of parchment with the
     * tutorial box, the NPC dialogue and every message line painted underneath
     * it.
     *
     * The other is a piece described only in SOME states. "Over everything
     * this plugin owns" is said by anchoring the surface over the LAST item
     * the description stated, and an item that comes into existence after that
     * anchor was written is created at the end of the parent's children --
     * which is above it. The drawer's backing and its tap blocker are recorded
     * only while the drawer is open, so both were born over the panel they
     * belong under: the open tab was a blank sheet of drawer with the whole
     * interface painted beneath it, and the blocker over that swallowed every
     * tap the panel's own items should have answered first. @see
     * MobileBlit::key, which is the other half of the same trap.
     *
     * So such a piece says where it belongs rather than where everything else
     * goes: behind the surface it is the backing of. That is what the
     * OldSchool lane's retained `pack-sheet` says, and the reason that lane
     * never had the first of these.
     *
     * Kind NONE -- a zeroed field -- is "over the scene, like the rest".
     */
    struct PorcelainElement behind;
    /**
     * And what this piece is visible WITH, where that is not the same element.
     *
     * Kept apart from `behind` on purpose. The sheet travels with the chat and
     * always did. The drawer's backing does not travel with the mount it is
     * anchored behind: which mount that is follows the live tab by a poll, so
     * a gate on it would blank the backing for the one frame between the tap
     * that opened a panel and the frame that learns which one -- a flash of
     * bare world in the hole the backing exists to fill. The frame's own
     * `g_drawer_open` is the truth about whether there is a drawer to draw,
     * and it is already the reason the piece was recorded at all.
     */
    struct PorcelainElement visible_with;
};
#define MOBILE_BLIT_MAX 48

struct MobileTab
{
    int x;
    int y;
    int w;
    int h;
    int tabno;
    struct MobileArt icon;
    /** The picture the cell wears when it is the open tab. */
    struct MobileArt lit;
    /** And when it is not. Empty on the classic rail, where the plate under
     *  the stones is the picture; 601's own idle stone on the OldSchool one,
     *  which is the same 40x40 box the lit stone covers exactly. */
    struct MobileArt idle;
};

/*
 * The live surfaces a layout arranges, by ROLE, the way gameframe-layout
 * names them. The widget API resolves each on whichever lane is up.
 */
enum FrameSurface
{
    FRAME_SURFACE_VIEWPORT = 0,
    FRAME_SURFACE_MINIMAP,
    FRAME_SURFACE_COMPASS,
    FRAME_SURFACE_CHAT,
    FRAME_SURFACE_CHAT_BUTTONS,
    FRAME_SURFACE_SIDEBAR,
    FRAME_SURFACE_MODAL,
    FRAME_SURFACE_ORBS,
    FRAME_SURFACE_COUNT
};
static char const* const FRAME_SURFACE_ROLE[FRAME_SURFACE_COUNT] = {
    "viewport", "minimap", "compass", "chat", "chat_buttons", "sidebar", "main_modal", "orbs",
};
enum
{
    FRAME_ORBS_MEMBER_ACTIVITY_ADVISER = 0,
    FRAME_ORBS_MEMBER_WORLD_MAP,
    FRAME_ORBS_MEMBER_WIKI,
};
#define FRAME_MEMBER_MAX 16

struct MobileRect
{
    int placed;
    struct ToriRS_Rect rect;
};
struct MobileSkin
{
    int placed;
    struct MobileArt art;
    struct MobileArt mask;
};
/** An owned control with one operation: the chat switch, the keyboard switch. */
struct MobileToggle
{
    int placed;
    struct ToriRS_Rect box;
    struct MobileArt face;
    struct MobileArt glyph;
};

/*
 * What one layout pass wants on screen -- the PLAN, before anything is
 * described. The layout records into it through the helpers it always called
 * (mobile_blit, mobile_surface, mobile_member, mobile_ui_node), and the
 * describe pass then states it to Porcelain, which makes the tree match.
 */
struct MobileRuntime
{
    /** The RIGHT and BOTTOM edges of the usable box, in canvas coordinates:
     *  what a bottom-anchored piece hangs from. @see mobile_lane_area. */
    int canvas_w;
    int canvas_h;
    /** The row the chat block's last one is, plus one: the usable bottom less
     *  the margin every bottom-anchored piece is inset by. What the tap
     *  blocker ends at, so the margin's four rows of world stay tappable.
     *  @see chat_bottom in mobile_layout. */
    int chat_bottom;
    struct MobileBlit blit[MOBILE_BLIT_MAX];
    int blit_count;
    struct MobileTab tab[MOBILE_TAB_COUNT];
    int tab_count;
    int toggle_x;
    int toggle_y;
    int keys_x;
    int keys_y;
    int plugins_x;
    int plugins_y;
    struct MobileArt toggle_art;
    int toggle_w;
    int toggle_h;
    int panel_x;
    int panel_y;
    int chat_placed;
    int chat_y;
    int chat_pack;
    int chat_w;
    int chat_h;
    /* -- the widget plan -- */
    struct MobileRect surface[FRAME_SURFACE_COUNT];
    struct MobileRect member[FRAME_SURFACE_COUNT][FRAME_MEMBER_MAX];
    struct MobileSkin skin_minimap;
    struct MobileSkin skin_compass;
    int housing_placed;
    struct MobileArt housing_image;
    struct ToriRS_Rect housing_rect;
    struct MobileToggle chat_toggle;
    struct MobileToggle keyboard_toggle;
    struct MobileToggle plugins_toggle;
    /**
     * Where the OldSchool chat pack's filter bar goes, on a root that lays it
     * out ABOVE the messages. Unplaced on every root that already hangs it off
     * the bottom, which is every desktop one. @see mobile_describe_chat_bar.
     */
    struct MobileRect chat_bar;
    /** The 2004 plates under the four filter captions, behind the lane's buttons. */
    struct MobileRect plate[MOBILE_CHAT_BUTTON_COUNT];
    struct MobileArt plate_art[MOBILE_CHAT_BUTTON_COUNT];
    /**
     * Where the lane's XP-counter mount is PINNED -- its top-right corner, in
     * canvas coordinates, and no size at all.
     *
     * A corner and not a box, because everything interface 122 draws is
     * anchored to this slot's right edge (the counter inset 2 columns, the
     * seven drop columns 3) and nothing at all to its left or bottom. Pinning
     * the corner therefore places every visible part of it while leaving the
     * cache's own width and height -- which is what sets how far a drop falls
     * -- exactly as the toplevel authored them. @see mobile_describe_xp_drops.
     */
    int xp_drops_placed;
    int xp_drops_right;
    int xp_drops_top;
};

/** A composed picture and the box it was composed for. Held across passes: the
 *  size changes about as often as the chatbox does. `key` is whatever else the
 *  picture depends on -- for the chat bar, the stones cut into it. */
struct MobilePaper
{
    struct MobileArt art;
    /* The characters `art.name` points at. A composed picture's name is built
     * from the box it was composed for, so it cannot be a literal and it has to
     * outlive the call that made it: a description holds the NAME and re-reads
     * it at every fence. */
    char name[48];
    int w;
    int h;
    uint32_t key;
};

struct MobileState;
struct MobileTabHandle
{
    struct MobileState* state;
    int tabno;
};

struct MobileState
{
    struct ToriRS_Api* api;
    struct Porcelain* porcelain;
    /* Every shipped PNG, by name from the table and by handle on first use.
     * @see mobile_art_file: the handle is re-asked of the HOST every time,
     * because the layer owns the lifetime of a picture a DESCRIPTION names and
     * a composition SOURCE is named by no description. */
    struct MobileArt image[MOBILE_IMG_COUNT];
    /* The pictures this plugin rasterises for itself, and the names they are
     * published under. @see mobile_art. */
    struct MobileArt art[MOBILE_ART_COUNT];
    char art_name[MOBILE_ART_COUNT][24];
    struct MobileHole hole_map;
    struct MobileHole hole_compass;
    int map_w;
    int map_h;
    bool masks_ready;
    bool drawer_open;
    bool chat_open;
    bool keyboard_on;
    bool tab_present[MOBILE_TAB_COUNT];
    struct MobileRuntime frame;
    struct MobilePaper paper;
    /** The 2004 strip as composed, and the size it was composed for -- the
     *  same bargain the parchment makes. @see mobile_bar_art. */
    struct MobilePaper bar;
    /** A 1x1 transparent picture: the face of every tap blocker and cell, and
     *  of a stone that is not the open one. */
    struct MobileArt blank;
    int provided;
    int logged_pending;

    /*
     * The frame event's own numbers, carried by hand.
     *
     * Porcelain_FrameEvent takes the canvas and the safe rect, compares them,
     * stores them and notes an input when they move -- and then calls the
     * description with nothing but this pointer. A layout has no other input,
     * so a provider has to keep its own copy. @see the port report.
     */
    int canvas_w;
    int canvas_h;
    struct ToriRS_Rect safe;

    /*
     * The keys the description names its own children by, built once at start.
     *
     * A key has to outlive the describe that stated it -- the layer holds the
     * string, compares against it at the next fence and removes what is not
     * re-described -- so these cannot be a `char[24]` on the describe's stack
     * the way the old apply pass's `snprintf` into a local was.
     */
    char cell_key[MOBILE_TAB_COUNT][12];
    char lit_key[MOBILE_TAB_COUNT][12];
    char icon_key[MOBILE_TAB_COUNT][12];
    char plate_key[MOBILE_CHAT_BUTTON_COUNT][12];
    /** `<slot>:<member>` for every member this frame can place. @see
     *  mobile_member_element. */
    char member_role[FRAME_SURFACE_COUNT][FRAME_MEMBER_MAX][32];

    struct MobileTabHandle tab_handle[MOBILE_TAB_COUNT];
    /* Which stone is open and which the server has handed over. Neither is one
     * of the layer's inputs, so this plugin polls them per frame and says when
     * they moved; the describe is what decides what that means. */
    int tab_active_shown;
    uint32_t tab_given_shown;
    /*
     * Which tab's icon is DARK this instant, or -1.
     *
     * The tutorial's blink: the game points at a rock by taking its icon away
     * for half of every twenty cycles. Polled beside the two above and for the
     * same reason, and it is the one of the three that moves on a CLOCK --
     * twice a second, and only while something is flagged.
     */
    int tab_flash_dark_shown;
    /*
     * The viewport's incarnation, and whether it moved since the last
     * description.
     *
     * A remount replaces the lane's nodes. Porcelain re-describes onto the new
     * ones by itself, and the HOST is the half it cannot tell: the host owns
     * the frame record and is otherwise left holding the plan it was given for
     * nodes that no longer exist. There is no verb for "my frame changed" --
     * frame.invalidate is the host's own and takes no Porcelain handle -- and
     * a widget watch of this plugin's own cannot be kept beside the layer's.
     * @see mobile_on_start.
     */
    uint64_t viewport_incarnation;
    bool remounted;

    /*
     * The chat pack's BACKING, as this frame last measured it.
     *
     * Not a cache and not an optimisation: it is what keeps a node the LANE
     * re-creates from bouncing the chat block back to the toplevel's own
     * layout. `[role:chat_backing]` is script-created -- `cc(iface(chat,37),0)`
     * -- and `[proc,toplevel_chatbox_background]` cc_deleteall's 162:37 and
     * cc_creates it again on every chat view switch, which is what clicking
     * Public or Private does (`~script2823` calls it before it rebuilds the
     * box). The element rebinds to the new node by itself; what it cannot
     * promise is that the new node has been laid out by the fence that
     * re-describes.
     *
     * A fence that measures it as zero is not "this lane has no backing". It
     * used to read as one, and the whole block followed: the BAND came out
     * false, so the pack was placed `band` rows low and the filter row was
     * left where 601 puts it -- along the TOP -- and the torn sheet, which is
     * sized from this box, was not described at all and so was taken off. One
     * unlucky fence and the chat is wearing the lane's own furniture.
     *
     * So the last measurement stands until a new one arrives. It is this
     * frame's OWN reading of this lane's pack, taken at most a frame earlier,
     * and it is dropped with the rest of the plan when the screen changes.
     * @see mobile_chat_backing and mobile_on_screen.
     */
    struct ToriRS_WidgetBounds chat_backing_box;
};

/** Callback-scoped native V2 services threaded through layout helpers. */
struct MobileCall
{
    struct ToriRS_Api* api;
    struct MobileState* state;
};

#define g_api (ctx->api)
#define g_art (ctx->state->art)
#define g_hole_map (ctx->state->hole_map)
#define g_hole_compass (ctx->state->hole_compass)
#define g_map_w (ctx->state->map_w)
#define g_map_h (ctx->state->map_h)
#define g_masks_ready (ctx->state->masks_ready)
#define g_drawer_open (ctx->state->drawer_open)
#define g_chat_open (ctx->state->chat_open)
#define g_keyboard_on (ctx->state->keyboard_on)
#define g_tab_present (ctx->state->tab_present)
#define g_frame (ctx->state->frame)
#define g_paper (ctx->state->paper)
#define g_bar (ctx->state->bar)

/** Defined at the foot of this file; Porcelain_Open names it at on_start. */
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_MOBILE_GAMEFRAME;

/*
 * The layer handle this provider opened, for the test that reads the
 * steady-state counters.
 *
 * A test seam and nothing else: the host hands a test no way to reach a
 * plugin's instance state, and "a settled frame costs no engine call" is a
 * belief until something reads the counters. At most one gameframe provider
 * runs in a process -- the host arbitrates the frame -- so one pointer is the
 * whole of it. NULL between on_stop and the next on_start.
 */
static struct Porcelain* g_mobile_porcelain_for_testing;

struct Porcelain*
ToriRS_MobileGameframePorcelainForTesting(void)
{
    return g_mobile_porcelain_for_testing;
}

/* ------------------------------------------------------------------ images */

/*
 * One shipped picture, by table index.
 *
 * The HOST is asked, not the layer, and it is asked EVERY time. Which of the
 * two owns a picture's lifetime is decided by who NAMES it: Porcelain owns the
 * pictures a DESCRIPTION names, requesting one on the describe that first names
 * it and handing the slot back after several runs that did not. A composition
 * SOURCE is named by no description -- `chat_button.png` is read for its pixels
 * and never blitted -- and the host's image table is one slot per (plugin,
 * name) with no refcount, so a slot the layer releases takes the plugin's
 * handle with it. Asking again is a table lookup the host answers without
 * touching the disk. @see gameframe.c's frame_art, which is the same answer.
 */
static struct MobileArt
mobile_art_file(
    struct MobileCall* ctx,
    int which)
{
    struct MobileArt* art;

    assert(ctx);
    assert(which >= 0);
    assert(which < MOBILE_IMG_COUNT);
    art = &ctx->state->image[which];
    /* A gap in the table is a picture no layout ships -- the 2004 atlas has no
     * seventh tab icon -- and it is not a name that failed to load. */
    if( !art->name )
        return *art;
    (void)g_api->assets.image(g_api, art->name, &art->ref);
    return *art;
}

/** The same picture's handle, for the composers that read its pixels. */
static struct ToriRS_ImageRef
mobile_image(
    struct MobileCall* ctx,
    int which)
{
    return mobile_art_file(ctx, which).ref;
}

/*
 * Is a picture this frame COMPOSED still alive?
 *
 * The composed art is published under a name and then named by the description,
 * which means the layer takes an image slot for it -- and the layer hands a slot
 * back after several describe runs that did not name it. A stone that stops
 * being the open one is exactly such a name. Asking the size is the cheapest
 * liveness test there is, and it is the same call the caller was about to make
 * anyway. @see gameframe.c's frame_art_alive.
 */
static bool
mobile_own_size(
    struct MobileCall* ctx,
    struct MobileArt art,
    int* out_w,
    int* out_h)
{
    assert(ctx);
    assert(out_w);
    assert(out_h);
    *out_w = 0;
    *out_h = 0;
    if( !art.name || art.ref.value == 0 )
        return false;
    return g_api->assets.image_size(g_api, art.ref, out_w, out_h) && *out_w > 0 && *out_h > 0;
}

static bool
mobile_art_alive(
    struct MobileCall* ctx,
    struct MobileArt art)
{
    int width = 0;
    int height = 0;

    return mobile_own_size(ctx, art, &width, &height);
}

/** image_compose, plus the one thing a composer must not forget: the layer
 *  caches name -> handle, and a name recomposed under the same spelling is a
 *  different picture. @see Porcelain_ImageForget. */
static struct ToriRS_ImageRef
mobile_publish(
    struct MobileCall* ctx,
    char const* name,
    int width,
    int height,
    uint32_t const* argb)
{
    struct ToriRS_ImageRef handle = { 0 };

    assert(ctx);
    assert(name);
    assert(argb);
    (void)g_api->assets.image_compose(g_api, name, width, height, argb, &handle);
    Porcelain_ImageForget(ctx->state->porcelain, name);
    return handle;
}

/*
 * The 1x1 transparent picture, composed again if it is gone.
 *
 * It is a composed picture like any other and the layer releases a composed
 * picture that no description has named for several runs. Every other composed
 * picture in this file re-derives itself when its handle dies; this one used to
 * be the exception, for no reason but that it is made once at start.
 */
static struct MobileArt
mobile_blank(struct MobileCall* ctx)
{
    uint32_t const clear = 0;
    struct MobileState* state;

    assert(ctx);
    state = ctx->state;
    if( mobile_art_alive(ctx, state->blank) )
        return state->blank;
    state->blank.ref = mobile_publish(ctx, state->blank.name, 1, 1, &clear);
    return state->blank;
}

/*
 * Which map housing to wear.
 *
 * Read as a STRING and resolved two ways, because a config enum is stored as
 * its LABEL: the settings panel writes back whichever dropdown row was chosen,
 * and reading that as a number gives atoi("Ring") == 0 and silently pins the
 * first choice. The index form is still accepted -- plugin_prefs.ini is a file
 * people edit, and `housing=1` is the obvious thing to write in it.
 * @see gameframe.c's frame_layout_from_config, which had this exact bug.
 */
static char const* const MOBILE_HOUSING_NAME[] = { "Lizards", "Ring", "OldSchool", "Auto" };

/**
 * The chat bar's own LAYER, as an element.
 *
 * PORCELAIN_EL(CHAT_BAR) is the bar's PICTURE -- interface 162's
 * `controls_background_graphic` -- because that is what a frame dressing the
 * bar needs and it is all this file wanted until now. Moving the bar is a
 * different question: the seven captions, their mode lines and the eight
 * plates hang off `controls`, and a picture that moved on its own would leave
 * the captions standing where the bar used to be. @see [role:chat_controls].
 */
#define MOBILE_CHAT_CONTROLS PORCELAIN_ROLE_EL("chat_controls")

/**
 * The toplevel slot the XP counter and the XP drops are mounted into.
 *
 * Spelled, because the element vocabulary has no name for it and it is not a
 * member of any family it does name: it is a slot on the TOPLEVEL beside the
 * orb block rather than anything inside interface 160. A 2004 lane declares no
 * such rung and answers ABSENT, which is the same answer it gives for the orb
 * block itself. @see [role:frame_xp_drops].
 */
#define MOBILE_XP_DROPS PORCELAIN_ROLE_EL("frame_xp_drops")

/**
 * The layer the side panels stand in, one level above the `sidebar` slot.
 *
 * Moving the sidebar alone leaves this at the lane's box, and on 164 it is a
 * noClickThrough layer later in the input order than the rail: with a tab open
 * it swallowed the clicks on every rock it covered. @see
 * [role:sidebar_container], mobile_describe_surfaces.
 */
#define MOBILE_SIDEBAR_CONTAINER PORCELAIN_ROLE_EL("sidebar_container")

/*
 * Is this an OldSchool lane -- one whose chat and orbs are packs of the
 * cache's own toplevel? Asked of the host each time rather than latched at
 * init: the plugin is registered before the cache profile has been read.
 */
static int
mobile_lane_oldschool(struct MobileCall* ctx)
{
    struct ToriRS_LaneInfo lane;

    assert(ctx);
    if( !ctx->api->core.lane(ctx->api, &lane) )
        return 0;
    return lane.game == TORIRS_GAME_OLDSCHOOL;
}

/*
 * Does this DEVICE have an on-screen keyboard for the KEYS switch to raise?
 *
 * The switch calls `input.text_input`, which on a desktop backend reaches a
 * PlatformWindow_SetTextInput that has no keyboard to show -- so without this
 * the frame drew a button that could not do its one job. This frame is offered
 * on every lane, not only to phones, so "the mobile frame is up" was never the
 * same fact as "there are keys to summon".
 *
 * `input.screen_keyboard` and NOT `touch`. The two look interchangeable and
 * are not: `touch` is the input POLICY, which the login clienttype sets and
 * TORIRS_TOUCH_UI moves in both directions, so a desk can be running it --
 * and a desk still has no soft keyboard. This capability is the platform's own
 * answer, which nothing above it can overrule.
 *
 * Asked of the host each time rather than latched, for the same reason
 * @see mobile_lane_oldschool gives: the plugin is registered before the host
 * has settled, and the answer is a cheap field read.
 */
static bool
mobile_has_screen_keyboard(struct MobileCall* ctx)
{
    assert(ctx);
    return ctx->api->core.capability(ctx->api, "input.screen_keyboard");
}

/*
 * Is there a plugin WINDOW for a switch to open?
 *
 * The same shape as the keyboard question above, and for the same reason: a
 * dead button is worse than a missing one, so the frame asks the HOST and
 * places no switch where the answer is no. @see the keyboard's own gate for
 * why this frame stopped placing KEYS on a desk.
 *
 * TWO questions and not one, because they fail differently. The capability is
 * "is there a window behind this" -- a full client says yes, a focused harness
 * that builds no window says no. The api check is the minor version: `client`
 * is a pointer module that an older host may not carry at all, and these two
 * verbs are newer than the module. Either answer being no means the same
 * thing to the layout, so they are asked together, once.
 *
 * Why this frame carries a launcher at all: a frame that has replaced the
 * lane's chrome inherits the ways into the client's own windows along with the
 * stones, exactly as it inherits the tab strip and the tutorial's blink. The
 * engine's three launchers all stand down here -- the rail wants a presenter,
 * the pop-out nav column wants interface 728 on screen and a mobile toplevel
 * mounts it hidden, and the dat2 profile authors no
 * `option_action=PLUGIN_PANEL` button because on a desk it has the column.
 * @see ANDROID-CHROME-001 in docs/platform_quirks.md.
 */
static bool
mobile_has_plugin_window(struct MobileCall* ctx)
{
    assert(ctx);
    return ctx->api->core.capability(ctx->api, "client.plugin_window") &&
           ctx->api->client && ctx->api->client->plugin_window_show &&
           ctx->api->client->plugin_window_open;
}

/*
 * Does this ROOT lay the chat pack's filter bar out above the messages?
 *
 * Interface 162 is ONE pack and every OldSchool toplevel mounts the same one,
 * so its size cannot answer this: the desktop tops hang `controls` off the
 * bottom of the block and the mobile top pins it to the top, two rows down,
 * with the message area underneath. gameframe.c already carries the same fact
 * for its own chat placement and says so in the same words -- the size cannot
 * tell the two apart, only the root can -- and this is that question asked by
 * the same key.
 *
 * A NAME and never a number: `toplevel_mobile` is the profile's name for
 * whichever interface this revision mounts as its phone frame, so a revision
 * that renumbers it restates one id and nothing in this file moves. A lane
 * that names no mobile toplevel answers no, which is right: it has no root
 * that does this.
 */
static bool
mobile_chat_bar_on_top(struct MobileCall* ctx)
{
    int mobile_top = 0;

    assert(ctx);
    if( !g_api->cache.named_id(g_api, "iface", "toplevel_mobile", &mobile_top) )
        return false;
    if( mobile_top <= 0 )
        return false;
    return g_api->cache.frame_root(g_api) == mobile_top;
}

/*
 * The backing's box, with the last good one standing in for a fence that
 * cannot measure the new node.
 *
 * Every question this frame asks about the chat block goes through here, so
 * the band, the sheet and the row it is clipped to all answer from one
 * reading rather than three. The element is still asked every time -- a
 * backing the lane has put away answers false here as it always did, and the
 * sheet goes with it -- and only the BOX falls back.
 *
 * @see MobileState::chat_backing_box for why a zero box is a pending layout
 * rather than a lane without a backing.
 */
static bool
mobile_chat_backing(
    struct MobileCall* ctx,
    struct PorcelainElementState* out)
{
    assert(ctx);
    assert(out);
    if( !Porcelain_Element(ctx->state->porcelain, PORCELAIN_EL(CHAT_BACKING), out) )
        return false;
    if( out->box.width > 0 && out->box.height > 0 )
    {
        ctx->state->chat_backing_box = out->box;
        return true;
    }
    if( ctx->state->chat_backing_box.width <= 0 || ctx->state->chat_backing_box.height <= 0 )
        return false;
    out->box = ctx->state->chat_backing_box;
    return true;
}

/*
 * How many of the pack's rows the bar's band takes off the top.
 *
 * Measured as the rows the MESSAGE area does not occupy -- the pack's own
 * height less the backing's -- and never off the bar's own box, which is the
 * one thing this frame is about to move. A band read from the bar is 30 rows
 * before the move and 175 after it, and a description whose input is its own
 * output moves the bar back and forth for ever. The backing is bottom-anchored
 * inside the pack at a fixed height, so it answers the same number on every
 * run whatever this frame does to the bar.
 */
static bool
mobile_chat_bar_band(
    struct MobileCall* ctx,
    int chat_h,
    int* out_band)
{
    struct PorcelainElementState backing;

    assert(ctx);
    assert(out_band);
    *out_band = 0;
    if( !mobile_chat_backing(ctx, &backing) )
        return false;
    if( backing.box.height <= 0 || backing.box.height >= chat_h )
        return false;
    *out_band = chat_h - backing.box.height;
    return true;
}

/** The choices, in the order the `art` setting lists them. */
static char const* const MOBILE_FAMILY_NAME[] = { "Auto", "Classic", "OldSchool" };

/*
 * Which family the `art` setting means right now. Read as a label first and
 * as an index second, for the reason mobile_housing gives.
 */
static enum MobileFamily
mobile_family(struct MobileCall* ctx)
{
    char const* value = NULL;
    int choice = 0;

    assert(ctx);
    (void)ctx->api->config.get_string(ctx->api, "art", &value);
    if( value && value[0] )
    {
        if( value[0] >= '0' && value[0] <= '9' )
            choice = atoi(value);
        else
            for( int i = 0; i < 3; i++ )
                if( strcmp(value, MOBILE_FAMILY_NAME[i]) == 0 )
                    choice = i;
    }
    if( choice == 2 )
        return FAMILY_OLDSCHOOL;
    /*
     * Auto is CLASSIC on every lane. The Stone Drawer is the 2004 frame
     * turned into a phone layout, and that look -- redstones, the 2004 icons,
     * the parchment, the paper's filter buttons -- is what it is for; on an
     * OldSchool lane the point is to get it BACK over the cache's mobile
     * frame, not to imitate that frame. OldSchool Mobile's own pieces stay
     * available as a choice.
     */
    (void)ctx;
    return FAMILY_CLASSIC;
}

static struct MobileHousing const*
mobile_housing(struct MobileCall* ctx)
{
    char const* value = NULL;
    int choice = MOBILE_HOUSING_AUTO;

    assert(ctx);
    (void)ctx->api->config.get_string(ctx->api, "housing", &value);
    if( value && value[0] )
    {
        if( value[0] >= '0' && value[0] <= '9' )
            choice = atoi(value);
        else
            for( int i = 0; i <= MOBILE_HOUSING_AUTO; i++ )
                if( strcmp(value, MOBILE_HOUSING_NAME[i]) == 0 )
                    choice = i;
    }
    /* Auto is the family's own ring: the lizards on a 2004 frame, 601's
     * thin ring on an OldSchool one. */
    if( choice < 0 || choice >= MOBILE_HOUSING_AUTO )
        choice = mobile_family(ctx) == FAMILY_OLDSCHOOL ? 2 : 0;
    return &MOBILE_HOUSING[choice];
}

/*
 * The drawer, and the sheet.
 *
 * Both are the player's, both are the plugin's to remember, and neither has a
 * counterpart in the client: a role this plan does not place is one the
 * plugin hides itself (mobile_apply_surfaces), so "the drawer is shut" is not
 * a flag the sidebar reads, it is a frame that stops having a sidebar in it.
 *
 * The drawer starts SHUT. On a phone the first thing wanted is the world, and a
 * frame that opened onto a panel would be spending its first impression on the
 * one thing a tap can always bring back.
 */
/*
 * Whether this frame has asked for the on-screen keyboard.
 *
 * The plugin's own belief, not the platform's: SDL owns the real state and
 * there is no way to ask it, so what this tracks is what the switch last
 * requested. That is enough for a toggle, and it is why tapping the chat asks
 * for the keyboard rather than toggling it -- a tap on the chat means "I want
 * to type", which is only ever a request to show.
 */

/*
 * Which tabs this lane actually has, learned from the widget API.
 *
 * The apply pass's widgets.find_all by role both moves a mount and answers
 * whether the frame HAS one, and that answer is the only way to tell a tab
 * this cache lacks from one it puts somewhere else -- rs289lc has no clan
 * chat, and a stone wearing an icon for a panel that cannot open is worse than
 * a blank one, because it invites the tap that does nothing.
 *
 * Cached because the question can only be asked while the drawer is OPEN: a
 * shut drawer places no mounts, so it has nothing to ask with. That is sound
 * rather than merely convenient -- which tabs exist is a property of the CACHE
 * and does not change while a world is loaded -- and it starts out "present" so
 * that a frame declared before the drawer has ever been opened still wears its
 * icons.
 */

/*
 * The reduction itself, on raw buffers: `src_w` x `src_h` into `width` x
 * `height`, each destination pixel the MEAN of the source rectangle it covers.
 *
 * On buffers and not on handles because there are two callers with different
 * needs -- one publishes the result as a picture, the other three-slices it
 * into a band -- and the pixels are the only thing they share.
 *
 * Averaged in PREMULTIPLIED space, because the alternative is wrong at every
 * edge: a transparent pixel still carries a colour, and mixing that colour in
 * at full weight drags the rim of a cut-out toward whatever it happened to be
 * filled with -- black, here, so the button would come back with a dark fringe
 * all round it, which is the defect this whole path exists to avoid.
 */
static void
mobile_scale_pixels(
    uint32_t const* src,
    int src_w,
    int src_h,
    uint32_t* out,
    int width,
    int height)
{
    assert(src);
    assert(out);
    assert(src_w > 0);
    assert(src_h > 0);
    assert(width > 0);
    assert(height > 0);
    for( int row = 0; row < height; row++ )
    {
        int const y0 = (row * src_h) / height;
        int const y1 = (((row + 1) * src_h) / height) > y0 ? ((row + 1) * src_h) / height : y0 + 1;

        for( int col = 0; col < width; col++ )
        {
            int const x0 = (col * src_w) / width;
            int const x1 =
                (((col + 1) * src_w) / width) > x0 ? ((col + 1) * src_w) / width : x0 + 1;
            unsigned long sum_a = 0;
            unsigned long sum_r = 0;
            unsigned long sum_g = 0;
            unsigned long sum_b = 0;
            unsigned long count = 0;

            for( int sy = y0; sy < y1 && sy < src_h; sy++ )
            {
                for( int sx = x0; sx < x1 && sx < src_w; sx++ )
                {
                    uint32_t const pixel = src[(sy * src_w) + sx];
                    unsigned long const alpha = (pixel >> 24) & 0xffu;

                    sum_a += alpha;
                    sum_r += ((pixel >> 16) & 0xffu) * alpha;
                    sum_g += ((pixel >> 8) & 0xffu) * alpha;
                    sum_b += (pixel & 0xffu) * alpha;
                    count++;
                }
            }
            if( count == 0 || sum_a == 0 )
            {
                out[(row * width) + col] = 0x00000000u;
                continue;
            }
            out[(row * width) + col] = (uint32_t)(((sum_a / count) & 0xffu) << 24) |
                                       (uint32_t)(((sum_r / sum_a) & 0xffu) << 16) |
                                       (uint32_t)(((sum_g / sum_a) & 0xffu) << 8) |
                                       (uint32_t)((sum_b / sum_a) & 0xffu);
        }
    }
}

/*
 * The 2004 chat strip, at the box the pack's own bar occupies: one stone
 * re-cut for every filter that bar carries, and CLEAR everywhere else.
 *
 * ONE picture and not eight. The moment a plate is laid over the bar there are
 * two bars, and the seam between them is the rectangle you can see behind
 * every label -- so the whole row is composed here, at the bar's box, and what
 * changes per filter is which columns of it carry a stone. How many stones
 * there are is the LANE's answer -- eight on an OldSchool chatbox, four on a
 * 2004 one -- so they are re-cut rather than read off a source at its own four
 * columns.
 *
 * The stone is `chat_button.png`, REDUCED to the band's height first and
 * three-sliced second. That order is the whole of the quality: the sprite is
 * authored at roughly three times the box, so slicing it first would point
 * sample a 3:1 reduction and turn the bevel into a staircase, while reducing
 * it first hands the slice a picture already at the right scale. The reduction
 * keeps the sprite's proportions, so the ends stay the shape they were drawn.
 *
 * What the cells leave is TRANSPARENT, and so is everything the sprite's own
 * mask leaves: the parchment and the world show between the buttons and around
 * them. @see MOBILE_CHAT_BUTTON_CAP.
 */
static int
mobile_chat_band_row(
    int row,
    int rows,
    int stone_h)
{
    int band = row < rows / 2 ? row : stone_h - (rows - row);

    assert(stone_h > 0);
    if( band < 0 )
        band = 0;
    else if( band >= stone_h )
        band = stone_h - 1;
    return band;
}

/*
 * One stone, written into the band at the cell the filter reported.
 *
 * `stone` is the sprite already reduced to `stone_w` x `stone_h`, and `cap` is
 * MOBILE_CHAT_BUTTON_CAP carried through that same reduction. Rows come
 * top from the top and bottom from the bottom so both bevels stay exact and
 * only the flat middle is lost when a cell is shallower than the picture;
 * columns are the three-slice, with the ends copied and the middle resampled.
 *
 * A fully transparent source pixel is SKIPPED rather than written. The band is
 * cleared once and the cells do not overlap, so this changes no picture -- it
 * is there so that a lane which ever reports two cells that touch cannot have
 * one stone's mask erase the edge of its neighbour.
 */
static void
mobile_cut_stone(
    uint32_t* out,
    int width,
    int height,
    struct MobileChatCell const* cell,
    uint32_t const* stone,
    int stone_w,
    int stone_h,
    int cap)
{
    int const cols = cell->w;
    int const rows = cell->h;

    assert(out);
    assert(cap >= 0);
    assert(cell);
    assert(stone);
    assert(stone_w > 0);
    assert(stone_h > 0);
    /* A cell too narrow to hold both ends has no three-slice to make, and a
     * squashed end is worse than the clear column it would replace. */
    if( cols <= 2 * cap || rows <= 0 )
        return;
    for( int y = 0; y < rows; y++ )
    {
        int const oy = cell->y + y;
        int const sy = mobile_chat_band_row(y, rows, stone_h);

        if( oy < 0 || oy >= height )
            continue;
        for( int x = 0; x < cols; x++ )
        {
            int const ox = cell->x + x;
            uint32_t pixel;
            int sx;

            if( ox < 0 || ox >= width )
                continue;
            if( x < cap )
                sx = x;
            else if( x >= cols - cap )
                sx = stone_w - (cols - x);
            else
                sx = cap + (x - cap) * (stone_w - 2 * cap) / (cols - 2 * cap);
            pixel = stone[(size_t)sy * (size_t)stone_w + (size_t)sx];
            if( (pixel >> 24) == 0 )
                continue;
            out[(size_t)oy * (size_t)width + (size_t)ox] = pixel;
        }
    }
}

/*
 * The sprite, reduced to a band height. The caller frees it.
 *
 * Proportional: the width follows the height, so the ends keep the shape they
 * were drawn with and the three-slice above has a picture at its own scale to
 * copy them from. `cap` comes back reduced by the same ratio. Answers NULL for every reason a
 * picture can be missing -- the asset still crossing the IO queue is the common one -- and a bar
 * with no stone to cut is simply not composed this fence.
 */
static uint32_t*
mobile_chat_stone_at_height(
    struct MobileCall* ctx,
    int height,
    int* stone_w,
    int* cap)
{
    uint32_t* sprite;
    uint32_t* stone;
    int src_w = 0;
    int src_h = 0;
    int width;
    size_t copied = 0;

    assert(ctx);
    assert(stone_w);
    assert(cap);
    assert(height > 0);
    if( !g_api->assets.image_size(g_api, mobile_image(ctx, IMG_CHAT_BUTTON), &src_w, &src_h) ||
        src_w <= 0 || src_h <= 0 )
        return NULL;
    width = src_w * height / src_h;
    if( width <= 0 )
        return NULL;
    sprite = malloc((size_t)src_w * (size_t)src_h * sizeof(*sprite));
    assert(sprite);
    if( !g_api->assets.image_pixels(
            g_api,
            mobile_image(ctx, IMG_CHAT_BUTTON),
            sprite,
            (size_t)src_w * (size_t)src_h,
            &copied) ||
        copied != (size_t)src_w * (size_t)src_h )
    {
        free(sprite);
        return NULL;
    }
    stone = malloc((size_t)width * (size_t)height * sizeof(*stone));
    assert(stone);
    mobile_scale_pixels(sprite, src_w, src_h, stone, width, height);
    free(sprite);
    *stone_w = width;
    /* The end, reduced by the same ratio as the picture it was measured on, so
     * the number never has to be restated against a hardcoded source width. */
    *cap = MOBILE_CHAT_BUTTON_CAP * width / src_w;
    return stone;
}

static struct ToriRS_ImageRef
mobile_compose_classic_bar(
    struct MobileCall* ctx,
    char const* name,
    int width,
    int height,
    struct MobileChatCell const* cell,
    int cell_count)
{
    uint32_t* stone;
    uint32_t* out;
    int stone_w = 0;
    int cap = 0;
    struct ToriRS_ImageRef handle = { 0 };

    assert(ctx);
    assert(name);
    assert(width > 0);
    assert(height > 0);
    assert(cell_count >= 0);
    assert(cell_count == 0 || cell);
    /* Reduced ONCE for the whole row: every cell in a bar is the same height,
     * and a picture per cell would be the same picture eight times. */
    stone = mobile_chat_stone_at_height(ctx, height, &stone_w, &cap);
    if( !stone )
        return handle;

    /* CLEAR, and the stones are the only thing written over it: the band is a
     * row of buttons standing on the parchment, not a slab with buttons in it. */
    out = calloc((size_t)width * (size_t)height, sizeof(*out));
    assert(out);
    for( int i = 0; i < cell_count; i++ )
        mobile_cut_stone(out, width, height, &cell[i], stone, stone_w, height, cap);
    handle = mobile_publish(ctx, name, width, height, out);
    free(out);
    free(stone);
    return handle;
}

/*
 * The redstone through a quarter turn, optionally dimmed.
 *
 * `image_pixels` out, turn, `image_compose` back in -- the two halves of the
 * image api meeting, which is what lets a plugin build art out of art it
 * shipped without carrying a decoder.
 *
 * CLOCKWISE, and the direction is not cosmetic: it is what keeps the row's
 * left-to-right order running top-to-bottom down the column. Turning the other
 * way reverses it, and the rail comes out with Music at the top and Combat at
 * the bottom -- every tab where the muscle memory says the opposite one is.
 */
static struct ToriRS_ImageRef
mobile_compose_turned(
    struct MobileCall* ctx,
    char const* name,
    struct ToriRS_ImageRef src,
    int flip_h,
    int flip_v,
    int dim)
{
    uint32_t* px;
    uint32_t* out;
    int src_w = 0;
    int src_h = 0;
    size_t copied = 0;
    struct ToriRS_ImageRef handle = { 0 };

    assert(ctx);
    assert(name);
    if( src.value == 0 )
        return handle;
    if( !g_api->assets.image_size(g_api, src, &src_w, &src_h) || src_w <= 0 || src_h <= 0 )
        return handle;

    px = malloc((size_t)src_w * (size_t)src_h * sizeof(*px));
    assert(px);
    if( !g_api->assets.image_pixels(g_api, src, px, (size_t)src_w * (size_t)src_h, &copied) ||
        copied != (size_t)src_w * (size_t)src_h )
    {
        free(px);
        return handle;
    }
    /* The turned picture is the source transposed: its width is the source's
     * height, and nothing here assumes the stone is square. */
    out = malloc((size_t)src_w * (size_t)src_h * sizeof(*out));
    assert(out);
    for( int row = 0; row < src_w; row++ )
    {
        for( int col = 0; col < src_h; col++ )
        {
            /* Mirror FIRST, in the source's own axes, then turn -- the flips
             * are the 2004 frame's own and are stated about the upright stone,
             * so applying them after the turn would swap which one is which.
             * Clockwise: dest(col,row) reads src(row, src_h-1-col). */
            int src_x = row;
            int src_y = src_h - 1 - col;
            uint32_t pixel;

            if( flip_h )
                src_x = src_w - 1 - src_x;
            if( flip_v )
                src_y = src_h - 1 - src_y;
            pixel = px[(src_y * src_w) + src_x];

            if( dim )
            {
                /* Scale the COLOUR and keep the alpha: a dimmed stone has to
                 * stay the same shape, and scaling alpha would let the world
                 * through the rail instead of darkening it. */
                uint32_t const alpha = pixel & 0xff000000u;
                uint32_t const red = ((pixel >> 16) & 0xffu) * 5 / 8;
                uint32_t const green = ((pixel >> 8) & 0xffu) * 5 / 8;
                uint32_t const blue = (pixel & 0xffu) * 5 / 8;

                pixel = alpha | (red << 16) | (green << 8) | blue;
            }
            out[(row * src_h) + col] = pixel;
        }
    }
    handle = mobile_publish(ctx, name, src_h, src_w, out);
    free(px);
    free(out);
    return handle;
}

/*
 * How far the ring is sealed shut before its windows are looked for.
 *
 * The DRAIN is the reason this number exists. A map surround is not a closed
 * ring: the lizard one has a gap at the bottom between the two tails, and the
 * transparent middle runs out through it to the edge of the picture. A plain
 * flood inward from the border therefore reaches the middle, calls it outside,
 * and finds no windows at all -- which is exactly what happened, and why the
 * compass drew square over a mask that had never been built.
 *
 * A hard-edged ring needs none of it -- the lizard surround derives the same
 * two windows at a radius of zero -- so this is insurance rather than the
 * mechanism, and it is small on purpose: every pixel of seal is a pixel the
 * window is grown back by afterwards, and a large one would start rounding off
 * the corners of a window that genuinely has them.
 */
#define MOBILE_SEAL 2

static void
mobile_dilate(
    unsigned char* mask,
    unsigned char* scratch,
    int width,
    int height,
    int radius);

/*
 * The enclosed windows of a sealed ring, with each grown back to the shape the
 * artist drew.
 *
 * `sealed` is the ring dilated shut, `solid` the ring as drawn. The flood runs
 * over `sealed` so the drain cannot leak, and the windows are then grown back
 * by the same radius and clipped to `solid` -- which recovers the pixels the
 * sealing ate without ever crossing the ring itself.
 *
 * `window` comes back holding 1 for every pixel of every window found, which is
 * what mobile_compose_window cuts each mask out of.
 */
static int
mobile_hole_scan(
    unsigned char const* sealed,
    unsigned char const* solid,
    unsigned char* window,
    int width,
    int height,
    unsigned char* seen,
    int* stack,
    struct MobileHole* out,
    int out_max)
{
    int const pixels = width * height;
    int found = 0;
    int top = 0;

    assert(sealed);
    assert(solid);
    assert(window);
    assert(seen);
    assert(stack);
    assert(out);

    /* Flood the outside in from every border pixel, over the SEALED ring. */
    for( int i = 0; i < pixels; i++ )
        seen[i] = sealed[i] ? 2 : 0;
    /*
     * Marked as it is PUSHED, never as it is popped.
     *
     * Marking at pop lets the same pixel be pushed once per neighbour that
     * reaches it before it comes off, so the stack can hold several entries per
     * pixel -- and this one is sized at one per pixel. It overran and took the
     * client with it. Marking at push makes "on the stack" and "seen" the same
     * statement, and the bound is then a pixel each by construction.
     */
    for( int col = 0; col < width; col++ )
    {
        int const bottom = ((height - 1) * width) + col;

        if( !seen[col] )
        {
            seen[col] = 1;
            stack[top++] = col;
        }
        if( !seen[bottom] )
        {
            seen[bottom] = 1;
            stack[top++] = bottom;
        }
    }
    for( int row = 0; row < height; row++ )
    {
        int const left = row * width;
        int const right = left + width - 1;

        if( !seen[left] )
        {
            seen[left] = 1;
            stack[top++] = left;
        }
        if( !seen[right] )
        {
            seen[right] = 1;
            stack[top++] = right;
        }
    }
    while( top > 0 )
    {
        int const at = stack[--top];
        int const col = at % width;
        int const row = at / width;

        if( col > 0 && !seen[at - 1] )
        {
            seen[at - 1] = 1;
            stack[top++] = at - 1;
        }
        if( col < width - 1 && !seen[at + 1] )
        {
            seen[at + 1] = 1;
            stack[top++] = at + 1;
        }
        if( row > 0 && !seen[at - width] )
        {
            seen[at - width] = 1;
            stack[top++] = at - width;
        }
        if( row < height - 1 && !seen[at + width] )
        {
            seen[at + width] = 1;
            stack[top++] = at + width;
        }
    }

    /* Whatever the flood could not reach is an enclosed window -- eroded by the
     * sealing, so grow it back and clip it to the ring as drawn. */
    for( int i = 0; i < pixels; i++ )
        window[i] = (unsigned char)(seen[i] == 0);
    {
        unsigned char* grow_scratch = seen;

        mobile_dilate(window, grow_scratch, width, height, MOBILE_SEAL);
        for( int i = 0; i < pixels; i++ )
            window[i] = (unsigned char)(window[i] && !solid[i]);
    }

    /* Then measure each one. `seen` is reused as the visited map. */
    memset(seen, 0, (size_t)width * (size_t)height);
    for( int start = 0; start < pixels && found < out_max; start++ )
    {
        struct MobileHole* hole;

        if( !window[start] || seen[start] )
            continue;
        hole = &out[found];
        hole->x = start % width;
        hole->y = start / width;
        hole->w = 1;
        hole->h = 1;
        hole->area = 0;
        hole->seed = start;
        top = 0;
        stack[top++] = start;
        seen[start] = 1;
        while( top > 0 )
        {
            int const at = stack[--top];
            int const col = at % width;
            int const row = at / width;
            int const right = hole->x + hole->w - 1;
            int const bottom = hole->y + hole->h - 1;

            hole->area++;
            if( col < hole->x )
            {
                hole->w += hole->x - col;
                hole->x = col;
            }
            else if( col > right )
                hole->w = col - hole->x + 1;
            if( row < hole->y )
            {
                hole->h += hole->y - row;
                hole->y = row;
            }
            else if( row > bottom )
                hole->h = row - hole->y + 1;

            if( col > 0 && window[at - 1] && !seen[at - 1] )
            {
                seen[at - 1] = 1;
                stack[top++] = at - 1;
            }
            if( col < width - 1 && window[at + 1] && !seen[at + 1] )
            {
                seen[at + 1] = 1;
                stack[top++] = at + 1;
            }
            if( row > 0 && window[at - width] && !seen[at - width] )
            {
                seen[at - width] = 1;
                stack[top++] = at - width;
            }
            if( row < height - 1 && window[at + width] && !seen[at + width] )
            {
                seen[at + width] = 1;
                stack[top++] = at + width;
            }
        }
        /* A few stray pixels along the ring's outline are not a window. */
        if( hole->area >= 64 )
            found++;
    }
    return found;
}

/**
 * Cut one window out as an alpha mask: transparent where the window is, opaque
 * everywhere else, at the window's own box.
 *
 * The polarity is the plugin mask convention -- transparent is the WINDOW and
 * opaque is clipped away -- and the SIZE matters as much as the shape, because
 * the renderer takes the mask's dimensions as the surface's draw box, centred
 * in the node box. A mask cut to the window is therefore also what sizes the
 * minimap and the compass.
 *
 * THIS window and no other. `window` marks every enclosed pixel of the ring,
 * and the two windows' boxes overlap: the 2004 mapback's compass boss reaches
 * eight columns into the map's box. Cutting the map's mask from the union
 * left those columns transparent, so the map -- painted after the compass on
 * every OldSchool toplevel -- covered the rose's east edge and its E. The
 * window is flooded back from its seed pixel, four-connected as it was
 * measured, and only that region is cut. `own` and `stack` are scratch of
 * one entry per pixel.
 */
static struct ToriRS_ImageRef
mobile_compose_window(
    struct MobileCall* ctx,
    char const* name,
    unsigned char const* window,
    int width,
    int height,
    struct MobileHole const* hole,
    unsigned char* own,
    int* stack)
{
    uint32_t* out;
    struct ToriRS_ImageRef handle = { 0 };
    int const pixels = width * height;
    int top = 0;

    assert(ctx);
    assert(name);
    assert(window);
    assert(hole);
    assert(own);
    assert(stack);
    assert(hole->seed >= 0);
    assert(hole->seed < pixels);
    assert(window[hole->seed]);

    memset(own, 0, (size_t)pixels);
    own[hole->seed] = 1;
    stack[top++] = hole->seed;
    while( top > 0 )
    {
        int const at = stack[--top];
        int const col = at % width;
        int const row = at / width;

        if( col > 0 && window[at - 1] && !own[at - 1] )
        {
            own[at - 1] = 1;
            stack[top++] = at - 1;
        }
        if( col < width - 1 && window[at + 1] && !own[at + 1] )
        {
            own[at + 1] = 1;
            stack[top++] = at + 1;
        }
        if( row > 0 && window[at - width] && !own[at - width] )
        {
            own[at - width] = 1;
            stack[top++] = at - width;
        }
        if( row < height - 1 && window[at + width] && !own[at + width] )
        {
            own[at + width] = 1;
            stack[top++] = at + width;
        }
    }

    out = malloc((size_t)hole->w * (size_t)hole->h * sizeof(*out));
    assert(out);
    for( int row = 0; row < hole->h; row++ )
        for( int col = 0; col < hole->w; col++ )
        {
            int const at = ((hole->y + row) * width) + hole->x + col;

            out[(row * hole->w) + col] = own[at] ? 0x00000000u : 0xff000000u;
        }
    handle = mobile_publish(ctx, name, hole->w, hole->h, out);
    free(out);
    return handle;
}

/*
 * Grow `mask` outward by `radius` pixels, four-connected. `scratch` is a second
 * buffer of the same size, swapped through rather than allocated per pass.
 */
static void
mobile_dilate(
    unsigned char* mask,
    unsigned char* scratch,
    int width,
    int height,
    int radius)
{
    assert(mask);
    assert(scratch);

    for( int pass = 0; pass < radius; pass++ )
    {
        memcpy(scratch, mask, (size_t)width * (size_t)height);
        for( int row = 0; row < height; row++ )
        {
            for( int col = 0; col < width; col++ )
            {
                int const at = (row * width) + col;

                if( mask[at] )
                    continue;
                if( (col > 0 && mask[at - 1]) || (col < width - 1 && mask[at + 1]) ||
                    (row > 0 && mask[at - width]) || (row < height - 1 && mask[at + width]) )
                    scratch[at] = 1;
            }
        }
        memcpy(mask, scratch, (size_t)width * (size_t)height);
    }
}

/*
 * Read the housing's windows off the housing, and cut a mask for each.
 *
 * Seal the ring, flood the outside in from the border, and whatever transparent
 * pixels the flood cannot reach are the enclosed windows. Then grow those back
 * by the same amount they were sealed by, clipped to the ring, so each window
 * is the shape the artist drew rather than one eroded by the sealing.
 *
 * Derived rather than shipped as a mask pair per housing, so the mask and the
 * art agree BY CONSTRUCTION: a new ring dropped into the folder is measured,
 * not described, and there is no second file to keep in step with it.
 */
static void
mobile_build_masks(struct MobileCall* ctx)
{
    struct MobileHousing const* housing = mobile_housing(ctx);
    struct MobileHole hole[4];
    uint32_t* argb;
    unsigned char* solid;
    unsigned char* sealed;
    unsigned char* scratch;
    unsigned char* seen;
    int* stack;
    int width = 0;
    int height = 0;
    int count;
    int pixels;
    int map = 0;
    int compass = 0;

    assert(ctx);
    if( !g_api->assets.image_size(g_api, mobile_image(ctx, housing->art), &width, &height) ||
        width <= 0 || height <= 0 )
        return;
    pixels = width * height;

    argb = malloc((size_t)pixels * sizeof(*argb));
    assert(argb);
    {
        size_t copied = 0;
        if( !g_api->assets.image_pixels(
                g_api, mobile_image(ctx, housing->art), argb, (size_t)pixels, &copied) ||
            copied != (size_t)pixels )
        {
            free(argb);
            return;
        }
    }
    solid = malloc((size_t)pixels);
    assert(solid);
    sealed = malloc((size_t)pixels);
    assert(sealed);
    scratch = malloc((size_t)pixels);
    assert(scratch);
    seen = malloc((size_t)pixels);
    assert(seen);
    stack = malloc((size_t)pixels * sizeof(*stack));
    assert(stack);

    /*
     * Half-lit counts as background, not as ring.
     *
     * The housings are cut with hard edges -- their alpha is 0 or 255 and
     * nothing between -- so this threshold does not fall in the middle of any
     * real pixel. It matters anyway, because art that came through a resize or
     * an export with a soft halo has a fringe of alpha 1..20 all round it, and
     * treating that fringe as ring drags the window's edge a pixel or two
     * inward everywhere the halo is one-sided.
     */
    for( int i = 0; i < pixels; i++ )
    {
        solid[i] = (unsigned char)((argb[i] >> 24) >= 128);
        sealed[i] = solid[i];
    }
    mobile_dilate(sealed, scratch, width, height, MOBILE_SEAL);

    count = mobile_hole_scan(sealed, solid, scratch, width, height, seen, stack, hole, 4);
    if( count < 2 )
    {
        /* Said rather than guessed at: a ring this code has not been read
         * against is worth a line, and an unmasked map is a better failure than
         * a mask cut around the wrong shape. */
        g_api->core.log(
            g_api, "map housing has %d window(s); expected 2, leaving it unmasked", count);
    }
    else
    {
        /* By AREA, not by position: which corner the compass boss sits in is a
         * property of the picture, and the one thing true of every ring is that
         * the map's window is far the larger. */
        for( int i = 1; i < count; i++ )
            if( hole[i].area > hole[map].area )
                map = i;
        compass = map == 0 ? 1 : 0;
        for( int i = 0; i < count; i++ )
            if( i != map && hole[i].area > hole[compass].area )
                compass = i;

        g_map_w = width;
        g_map_h = height;
        g_hole_map = hole[map];
        g_hole_compass = hole[compass];
        g_art[ART_MINIMAP_MASK].ref = mobile_compose_window(
            ctx, g_art[ART_MINIMAP_MASK].name, scratch, width, height, &hole[map], seen, stack);
        g_art[ART_COMPASS_MASK].ref = mobile_compose_window(
            ctx, g_art[ART_COMPASS_MASK].name, scratch, width, height, &hole[compass], seen, stack);
        g_masks_ready =
            g_art[ART_MINIMAP_MASK].ref.value != 0 && g_art[ART_COMPASS_MASK].ref.value != 0;
        g_api->core.log(
            g_api,
            "map windows read: %dx%d at %d,%d and %dx%d at %d,%d",
            hole[map].w,
            hole[map].h,
            hole[map].x,
            hole[map].y,
            hole[compass].w,
            hole[compass].h,
            hole[compass].x,
            hole[compass].y);
    }

    free(argb);
    free(solid);
    free(sealed);
    free(scratch);
    free(seen);
    free(stack);
}

/*
 * An image at a different size, averaged down.
 *
 * Used here to reduce each 100x32 classic chat plate into the compact box an
 * OldSchool filter cell can hold. Point sampling a reduction throws away most
 * of the source and turns the stone edge into a staircase; taking the MEAN of
 * each destination pixel's footprint keeps the bevel and texture legible.
 *
 * Averaged in premultiplied space, because the alternative is wrong at every
 * edge: a transparent pixel still carries a colour, and mixing that colour in
 * at full weight drags the rim of the button toward whatever the cut-out
 * happened to be filled with -- black, here, so the button would come back with
 * a dark fringe all round it.
 */
static struct ToriRS_ImageRef
mobile_compose_scaled(
    struct MobileCall* ctx,
    char const* name,
    struct ToriRS_ImageRef src,
    int width,
    int height)
{
    uint32_t* px;
    uint32_t* out;
    int src_w = 0;
    int src_h = 0;
    size_t copied = 0;
    struct ToriRS_ImageRef handle = { 0 };

    assert(ctx);
    assert(name);
    assert(width > 0);
    assert(height > 0);
    if( src.value == 0 )
        return handle;
    if( !g_api->assets.image_size(g_api, src, &src_w, &src_h) || src_w <= 0 || src_h <= 0 )
        return handle;

    px = malloc((size_t)src_w * (size_t)src_h * sizeof(*px));
    assert(px);
    if( !g_api->assets.image_pixels(g_api, src, px, (size_t)src_w * (size_t)src_h, &copied) ||
        copied != (size_t)src_w * (size_t)src_h )
    {
        free(px);
        return handle;
    }
    out = malloc((size_t)width * (size_t)height * sizeof(*out));
    assert(out);
    mobile_scale_pixels(px, src_w, src_h, out, width, height);
    handle = mobile_publish(ctx, name, width, height, out);
    free(px);
    free(out);
    return handle;
}

/*
 * One chat filter, alone.
 *
 * `chat_button.png` IS the button with no background: the 2004 stone with the
 * slab it was cut out of already taken off it, hand-masked at 295x97 so the
 * alpha follows the stone's own rounded outline instead of a rectangle. It has
 * shipped in this plugin's assets since 2026-08-27 and nothing read it.
 *
 * What the four buttons wore instead was a 100x32 WINDOW of `chat_plate.png`,
 * and on a 2004 frame a window is the right answer: the hollow is cut INTO
 * `backbase1`, so the rock around it is the same rock the next hollow is cut
 * into, and a window of it is invisible against the rest of the slab. THIS
 * frame's chat strip stands on the world. Lift the window out of the slab and
 * that rock is a dark ragged rectangle around every button, which is what it
 * was reported as, three times.
 *
 * Reduced rather than re-cut, and reduced in premultiplied space: the sprite
 * is authored at three times the box, and it is a cut-out, so every rule
 * mobile_compose_scaled was written for applies here first. @see it.
 *
 * There is no `index`, and no four pictures. The 2004 strip's four buttons are
 * one stone stamped at four columns of different rubble -- MEASURED, over
 * x 4..99: a pixel agreeing across all four windows to within 14 per channel
 * is the button and one that differs is the rubble, and the agreeing region is
 * 70 to 81 wide on every row of the stone -- so ONE picture is all four, and
 * ART_CHAT_BUTTON is one slot rather than four copies of it.
 */
static struct ToriRS_ImageRef
mobile_compose_chat_button(
    struct MobileCall* ctx,
    char const* name)
{
    assert(ctx);
    assert(name);
    return mobile_compose_scaled(
        ctx, name, mobile_image(ctx, IMG_CHAT_BUTTON), MOBILE_CHAT_BUTTON_W, MOBILE_CHAT_BUTTON_H);
}

/*
 * A 9-slice plate at a size: corners as cut, edges and middle repeated.
 *
 * What 601 does with the same nine 3x3 pieces (`side_right_tabs_background`:
 * top_left, top_middle stretched, and so on). The pieces are 3x3 and the
 * middle is a flat dark, so repeating shows no seam.
 */
static struct ToriRS_ImageRef
mobile_compose_nine_slice(
    struct MobileCall* ctx,
    char const* name,
    int piece_base,
    int width,
    int height)
{
    uint32_t* px[9];
    uint32_t* out;
    int cell = 0;
    struct ToriRS_ImageRef handle = { 0 };

    assert(ctx);
    assert(name);
    assert(piece_base >= 0);
    assert(width > 0);
    assert(height > 0);
    for( int i = 0; i < 9; i++ )
    {
        int w = 0;
        int h = 0;
        struct ToriRS_ImageRef const slice = mobile_image(ctx, piece_base + i);
        if( slice.value == 0 || !g_api->assets.image_size(g_api, slice, &w, &h) || w <= 0 ||
            w != h )
            return handle;
        if( i == 0 )
            cell = w;
        else if( w != cell )
            return handle;
    }
    if( width < 2 * cell || height < 2 * cell )
        return handle;
    for( int i = 0; i < 9; i++ )
    {
        px[i] = malloc((size_t)cell * (size_t)cell * sizeof(**px));
        assert(px[i]);
        size_t copied = 0;
        if( !g_api->assets.image_pixels(
                g_api,
                mobile_image(ctx, piece_base + i),
                px[i],
                (size_t)cell * (size_t)cell,
                &copied) ||
            copied != (size_t)cell * (size_t)cell )
        {
            for( int j = 0; j <= i; j++ )
                free(px[j]);
            return handle;
        }
    }
    out = malloc((size_t)width * (size_t)height * sizeof(*out));
    assert(out);
    for( int y = 0; y < height; y++ )
    {
        int const row = y < cell ? 0 : y >= height - cell ? 2 : 1;
        int const sy = row == 0 ? y : row == 2 ? y - (height - cell) : (y - cell) % cell;
        for( int x = 0; x < width; x++ )
        {
            int const col = x < cell ? 0 : x >= width - cell ? 2 : 1;
            int const sx = col == 0 ? x : col == 2 ? x - (width - cell) : (x - cell) % cell;
            out[y * width + x] = px[row * 3 + col][sy * cell + sx];
        }
    }
    handle = mobile_publish(ctx, name, width, height, out);
    for( int i = 0; i < 9; i++ )
        free(px[i]);
    free(out);
    return handle;
}

/*
 * One picture this frame RASTERISES, made if it is not already there.
 *
 * Per slot and on demand, rather than the all-or-nothing latch this replaces.
 * Two reasons, and the second is the one that forced it:
 *
 *   - A composed picture is published under a NAME, and a description that
 *     names it makes the layer take an image slot for it -- which the layer
 *     hands back after several describe runs that did not name it. A stone that
 *     stops being the open one is exactly such a name, and the host's image
 *     table is one slot per (plugin, name) with no refcount, so the release
 *     takes this plugin's handle with it. Something has to notice and re-make
 *     it, and the cheapest place to notice is where the picture is asked for.
 *   - The latch cost up to twenty image_size calls a frame during boot and then
 *     invalidated the whole frame once. Here a picture whose SOURCE has not
 *     landed simply is not returned, the item that wanted it is not described,
 *     and the asset event re-runs the describe. @see mobile_on_asset.
 *
 * `ART_MINIMAP_MASK` and `ART_COMPASS_MASK` are not here: they are cut by
 * mobile_build_masks, which reads the ring's alpha and answers two WINDOW boxes
 * as well as two pictures, so its trigger is the housing changing rather than a
 * picture going missing. @see mobile_ensure_masks.
 */
static struct MobileArt
mobile_art(
    struct MobileCall* ctx,
    int which)
{
    static int const SHAPE[3] = { IMG_REDSTONE_0, IMG_REDSTONE_1, IMG_REDSTONE_2 };
    struct MobileArt* art;
    char const* name;

    assert(ctx);
    assert(which >= 0);
    assert(which < MOBILE_ART_COUNT);
    art = &ctx->state->art[which];
    if( mobile_art_alive(ctx, *art) )
        return *art;
    name = art->name;
    assert(name);
    if( which == ART_O_RAIL )
        /* The OldSchool rail's plate, at the rail's own size: 601's dark
         * nine-slice with both columns on one picture. */
        art->ref =
            mobile_compose_nine_slice(ctx, name, IMG_O_BORDER_0, MOBILE_O_RAIL_W, MOBILE_O_RAIL_H);
    else if( which >= ART_STONE_0 && which < ART_STONE_0 + MOBILE_TAB_COUNT )
    {
        /* The same half turn the plates take: a stone sits in a socket, and a
         * socket that turned over wants the bevel that turned over with it. */
        struct MobileTabStone const* stone = &MOBILE_TAB_STONE[which - ART_STONE_0];
        art->ref = mobile_compose_turned(
            ctx,
            name,
            mobile_image(ctx, SHAPE[stone->stone]),
            !stone->flip_h,
            !stone->flip_v,
            /*dim=*/0);
    }
    else if( which == ART_ICON_CHAT )
    {
        int icon_w = 0;
        int icon_h = 0;

        if( g_api->assets.image_size(g_api, mobile_image(ctx, IMG_ICON_CHAT), &icon_w, &icon_h) &&
            icon_w > 0 && icon_h > 0 )
            art->ref = mobile_compose_scaled(
                ctx,
                name,
                mobile_image(ctx, IMG_ICON_CHAT),
                (icon_w * MOBILE_ICON_NUM) / MOBILE_ICON_DEN,
                (icon_h * MOBILE_ICON_NUM) / MOBILE_ICON_DEN);
    }
    else if( which == ART_ICON_KEYBOARD )
    {
        int icon_w = 0;
        int icon_h = 0;

        if( g_api->assets.image_size(
                g_api, mobile_image(ctx, IMG_ICON_KEYBOARD), &icon_w, &icon_h) &&
            icon_w > 0 && icon_h > 0 )
            art->ref = mobile_compose_scaled(
                ctx,
                name,
                mobile_image(ctx, IMG_ICON_KEYBOARD),
                (icon_w * MOBILE_KEY_ICON_NUM) / MOBILE_KEY_ICON_DEN,
                (icon_h * MOBILE_KEY_ICON_NUM) / MOBILE_KEY_ICON_DEN);
    }
    else if( which == ART_ICON_PLUGINS )
    {
        int icon_w = 0;
        int icon_h = 0;

        if( g_api->assets.image_size(
                g_api, mobile_image(ctx, IMG_ICON_PLUGINS), &icon_w, &icon_h) &&
            icon_w > 0 && icon_h > 0 )
            art->ref = mobile_compose_scaled(
                ctx,
                name,
                mobile_image(ctx, IMG_ICON_PLUGINS),
                (icon_w * MOBILE_PLUGIN_ICON_NUM) / MOBILE_PLUGIN_ICON_DEN,
                (icon_h * MOBILE_PLUGIN_ICON_NUM) / MOBILE_PLUGIN_ICON_DEN);
    }
    else if( which == ART_CHAT_BUTTON )
        art->ref = mobile_compose_chat_button(ctx, name);
    /*
     * The plate takes the same quarter turn as the stones standing on it, and
     * the right-hand column takes it mirrored so the two sit back to back.
     *
     * Both then take a further half turn, so the plates face the way the rail
     * wants rather than the way the desktop row did. A half turn commutes with
     * the quarter turn, so it is simply both source flips inverted -- which is
     * why the left plate reads (1,1) and the right, being the mirrored one,
     * reads (1,0).
     */
    else if( which == ART_PLATE_0 )
        art->ref = mobile_compose_turned(ctx, name, mobile_image(ctx, IMG_PLATE), 1, 1, /*dim=*/0);
    else if( which == ART_PLATE_1 )
        art->ref = mobile_compose_turned(ctx, name, mobile_image(ctx, IMG_PLATE), 1, 0, /*dim=*/0);
    /* A picture whose source has not decoded yet is an EMPTY art and not a
     * failure: the item that wanted it is not described, and the asset event
     * runs the describe again. */
    if( art->ref.value == 0 )
        return (struct MobileArt){ NULL, { 0 } };
    return *art;
}

/*
 * The two round windows, read off the ring, and the two masks cut from them.
 *
 * Not in mobile_art, because this answers BOXES as well as pictures -- the
 * layout places the minimap and the compass at what the scan found -- and
 * because what makes it stale is the `housing` setting changing rather than a
 * picture being released. @see mobile_on_config.
 */
static void
mobile_ensure_masks(struct MobileCall* ctx)
{
    assert(ctx);
    if( g_masks_ready && mobile_art_alive(ctx, g_art[ART_MINIMAP_MASK]) &&
        mobile_art_alive(ctx, g_art[ART_COMPASS_MASK]) )
        return;
    mobile_build_masks(ctx);
}

/*
 * A window mask this frame has actually CUT, or nothing at all.
 *
 * mobile_art ends with the rule that a picture whose source has not decoded
 * yet is an EMPTY art rather than a failure: the item that wanted it is not
 * described, and the asset event runs the describe again. The two masks do not
 * go through mobile_art -- they are cut by mobile_build_masks, which answers
 * boxes as well as pictures -- so they had no half of that rule, and the
 * layout named `minimap_mask.png` whether or not a picture had ever been
 * published under it.
 *
 * Naming one that has not been cut is not a cosmetic slip, and this is the
 * defect that made the Stone Drawer a no-op on a 2004 lane. A name the layer
 * cannot resolve to a composed picture is handed to the HOST, which can only
 * read it as a file; the file does not exist, because this name is a
 * composition target and never was one; the asset slot is marked missing, and
 * missing is TERMINAL -- plugin_frame_provider_assets then answers "Required
 * gameframe asset could not be loaded" for the rest of the session and the
 * host stops asking for the frame at all. On a dat1 lane the ring's PNG had
 * not decoded by the first describe, the description named its masks anyway,
 * and the frame was refused before it had drawn a single stone. The 2004
 * surround stayed up with a few of this plugin's pieces floating over it,
 * which reads exactly like a frame that declined.
 *
 * So: no picture, no name.
 */
static struct MobileArt
mobile_mask(
    struct MobileCall* ctx,
    int which)
{
    assert(ctx);
    assert(which >= 0);
    assert(which < MOBILE_ART_COUNT);
    if( ctx->state->art[which].ref.value == 0 )
        return (struct MobileArt){ NULL, { 0 } };
    return ctx->state->art[which];
}

/* ---------------------------------------------------- recording the plan */

static void
mobile_blit_into(
    struct MobileCall* ctx,
    char const* key,
    struct MobileArt image,
    int x,
    int y,
    int w,
    int h,
    char const* op)
{
    struct MobileBlit* b;

    assert(ctx);
    assert(key);
    if( !image.name && !op )
        return;
    if( g_frame.blit_count >= MOBILE_BLIT_MAX )
    {
        /* Said rather than silently dropped: a frame missing one piece of
         * stone reads as a rendering bug, and this is the one thing here that
         * could cause it. */
        g_api->core.log(
            g_api, "mobile: more than %d chrome blits; the rest are dropped", MOBILE_BLIT_MAX);
        return;
    }
    b = &g_frame.blit[g_frame.blit_count++];
    b->key = key;
    b->image = image;
    b->x = x;
    b->y = y;
    b->w = w;
    b->h = h;
    b->op = op;
}

/** Chrome over the scene, under the live surfaces. */
static void
mobile_blit(
    struct MobileCall* ctx,
    char const* key,
    struct MobileArt image,
    int x,
    int y)
{
    mobile_blit_into(ctx, key, image, x, y, 0, 0, NULL);
}

/** The last piece recorded, marked as the backing of a live surface.
 *  @see MobileBlit::behind. */
static void
mobile_blit_mark_behind(
    struct MobileCall* ctx,
    int before,
    struct PorcelainElement behind,
    struct PorcelainElement visible_with)
{
    assert(ctx);
    /* Only the piece the call actually recorded. A full table is a state
     * mobile_blit_into already says out loud, and marking the piece BEFORE the
     * one that was dropped would put somebody else's stone under the chat. */
    if( g_frame.blit_count != before + 1 )
        return;
    g_frame.blit[before].behind = behind;
    g_frame.blit[before].visible_with = visible_with;
}

/** The same as mobile_blit, for a piece that belongs UNDER a live surface
 *  rather than over the scene. @see MobileBlit::behind. */
static void
mobile_blit_behind(
    struct MobileCall* ctx,
    char const* key,
    struct MobileArt image,
    int x,
    int y,
    struct PorcelainElement behind,
    struct PorcelainElement visible_with)
{
    int const before = g_frame.blit_count;

    assert(ctx);
    mobile_blit_into(ctx, key, image, x, y, 0, 0, NULL);
    mobile_blit_mark_behind(ctx, before, behind, visible_with);
}

/*
 * A rectangle that exists only to stop a tap falling through to the world.
 *
 * `behind` is the surface it covers, where it covers one: a blocker over the
 * chat or over the open panel has to sit UNDER that surface, or the scrollbar
 * and the panel's items never get their own taps -- which is the whole of what
 * these are for. Kind NONE for a blocker over nothing but this frame's own
 * stone. @see MobileBlit::behind.
 */
static void
mobile_blocker(
    struct MobileCall* ctx,
    char const* key,
    struct ToriRS_Rect box,
    char const* op,
    struct PorcelainElement behind)
{
    int const before = g_frame.blit_count;

    assert(op);
    mobile_blit_into(
        ctx, key, (struct MobileArt){ NULL, { 0 } }, box.x, box.y, box.width, box.height, op);
    mobile_blit_mark_behind(ctx, before, behind, PORCELAIN_EL(NONE));
}

static void
mobile_surface(
    struct MobileCall* ctx,
    int surface,
    int x,
    int y,
    int width,
    int height)
{
    assert(ctx);
    assert(surface >= 0 && surface < FRAME_SURFACE_COUNT);
    g_frame.surface[surface].placed = 1;
    g_frame.surface[surface].rect = (struct ToriRS_Rect){ x, y, width, height };
}

static void
mobile_member(
    struct MobileCall* ctx,
    int surface,
    int member,
    int x,
    int y,
    int width,
    int height)
{
    assert(ctx);
    assert(surface >= 0 && surface < FRAME_SURFACE_COUNT);
    if( member < 0 || member >= FRAME_MEMBER_MAX )
        return;
    g_frame.member[surface][member].placed = 1;
    g_frame.member[surface][member].rect = (struct ToriRS_Rect){ x, y, width, height };
}

/*
 * One piece of the plan, recorded by what it IS so the describe pass can state
 * it:
 *
 *   frame.minimap.housing      the housing plate, described over the compass
 *   chat-toggle / keyboard-toggle  the two switches, owned controls
 *   frame.sidebar.rail         a tap blocker over the rail plate
 *   frame.chat.button.*        a 2004 plate behind a lane filter button
 */
static void
mobile_ui_node(
    struct MobileCall* ctx,
    char const* name,
    struct ToriRS_Rect bounds,
    struct MobileArt image)
{
    assert(ctx);
    assert(name);
    if( strcmp(name, "frame.minimap.housing") == 0 )
    {
        if( !image.name )
            return;
        g_frame.housing_placed = 1;
        g_frame.housing_image = image;
        g_frame.housing_rect = bounds;
        return;
    }
    /* The switches, by name. A table and not a first-letter test: that read
     * `name[0] == 'c'` for "chat, else keyboard", which is not a question that
     * survives a third switch. */
    {
        static struct
        {
            char const* name;
            size_t toggle_offset;
            int art;
        } const SWITCH[] = {
            { "chat-toggle", offsetof(struct MobileRuntime, chat_toggle), ART_ICON_CHAT },
            { "keyboard-toggle", offsetof(struct MobileRuntime, keyboard_toggle),
              ART_ICON_KEYBOARD },
            { "plugins-toggle", offsetof(struct MobileRuntime, plugins_toggle),
              ART_ICON_PLUGINS },
        };

        for( size_t i = 0; i < sizeof(SWITCH) / sizeof(SWITCH[0]); i++ )
            if( strcmp(name, SWITCH[i].name) == 0 )
            {
                struct MobileToggle* t =
                    (struct MobileToggle*)((char*)&g_frame + SWITCH[i].toggle_offset);

                t->placed = 1;
                t->box = bounds;
                t->face = g_frame.toggle_art;
                t->glyph = mobile_art(ctx, SWITCH[i].art);
                return;
            }
    }
    if( strcmp(name, "frame.sidebar.rail") == 0 )
    {
        mobile_blocker(ctx, "piece.rail.blocker", bounds, "Rail", PORCELAIN_EL(NONE));
        return;
    }
    if( strncmp(name, "frame.chat.button.", 18) == 0 )
    {
        static char const* const NAME[MOBILE_CHAT_BUTTON_COUNT] = {
            "public", "private", "trade", "report"
        };
        for( int i = 0; i < MOBILE_CHAT_BUTTON_COUNT; i++ )
            if( strcmp(name + 18, NAME[i]) == 0 )
            {
                g_frame.plate[i].placed = 1;
                g_frame.plate[i].rect = bounds;
                g_frame.plate_art[i] = image;
            }
        return;
    }
}

/** The box this frame lays itself out in. @see mobile_lane_area. */
struct MobileArea
{
    int x;
    int y;
    int w;
    int h;
};

/*
 * The canvas, less what the platform covers and less the lane's own chrome.
 *
 * Three things come out of the canvas the host offered, and all three used to
 * be somewhere else or nowhere at all:
 *
 *   The SAFE rect -- the phone's keyboard band -- which the host states and
 *   re-asks this frame about when it moves. Its HEIGHT was honoured inline in
 *   on_gameframe; its ORIGIN was not, because MobileCall::origin_x/origin_y
 *   were declared and never read. A notch or a status bar therefore left every
 *   piece at the physical origin. That is the ledger's F8, and the fix is that
 *   the origin is in this box and every edge below is measured from it.
 *
 *   The lane's own docked STRIP: a LANE_CHROME member that is PRESENTED and
 *   spans a full edge moves that edge in, so the drawer does not open under it.
 *   It used to be one hand-spelled `lane_chrome_0` found, asked whether it was
 *   visible and asked for its box, inline in on_gameframe and WATCHED BY
 *   NOTHING -- so a strip that mounted after login never re-planned (F12). Here
 *   it is an ELEMENT, which is what registers the watch that re-runs this
 *   description when the strip comes or goes, and `presented` is the answer to
 *   both of the old questions at once. How many strips this lane HAS is asked
 *   of the DATA, because asking about one it does not have is an ABSENT finding
 *   per member per lane for a fact Porcelain_Count answers without a watch.
 *
 * Porcelain_Usable is the verb for this and it could not be used, for the same
 * reason the desktop provider could not use it: it answers an absolute rect
 * derived from the frame ROOT's box, and the root is not the canvas this frame
 * was offered -- adopting it outright lays a 1200-wide window out in the root's
 * columns. So the RULE is read off the element and applied to the host's
 * number. @see the port report: the verb should answer the CUTS, not the rect.
 */
static struct MobileArea
mobile_lane_area(
    struct MobileCall* ctx,
    int canvas_w,
    int canvas_h)
{
    struct MobileArea area = { 0, 0, canvas_w, canvas_h };
    struct ToriRS_Rect const safe = ctx->state->safe;

    assert(ctx);
    if( safe.width > 0 && safe.height > 0 && safe.x >= 0 && safe.y >= 0 &&
        safe.x + safe.width <= canvas_w && safe.y + safe.height <= canvas_h )
    {
        area.x = safe.x;
        area.y = safe.y;
        area.w = safe.width;
        area.h = safe.height;
    }
    for( int member = 0, strips = Porcelain_Count(ctx->state->porcelain, PORCELAIN_EL_LANE_CHROME);
         member < strips;
         member++ )
    {
        struct PorcelainElementState strip;
        if( !Porcelain_Element(ctx->state->porcelain, PORCELAIN_CHROME_EL(member), &strip) )
            continue;
        if( !strip.presented || strip.box.width <= 0 || strip.box.height <= 0 ||
            strip.box.width >= area.w )
            continue;
        /* A strip on either edge moves that edge in; one across neither is
         * ignored, which is the 601 case the lane chrome family exists for. */
        if( strip.box.x > area.x && strip.box.x + strip.box.width >= area.x + area.w )
            area.w = strip.box.x - area.x;
        else if( strip.box.x <= area.x && strip.box.x + strip.box.width < area.x + area.w )
        {
            area.w -= strip.box.x + strip.box.width - area.x;
            area.x = strip.box.x + strip.box.width;
        }
    }
    return area;
}
/*
 * One piece of the parchment, read out of its image once.
 *
 * Several of these are wanted at a time -- a corner, plus whichever variants
 * loaded for each repeating position -- and the composition walks all of
 * them, so they are fetched together and freed together: fetching per
 * destination row would re-read the same picture a hundred times.
 */
struct MobilePaperPiece
{
    uint32_t* px;
    int w;
    int h;
};

/** 1 when the piece was read. A missing asset is a runtime state -- a half
 *  installed plugin folder -- and not a contract violation. */
static int
mobile_paper_fetch(
    struct MobileCall* ctx,
    struct ToriRS_ImageRef image,
    struct MobilePaperPiece* out)
{
    assert(ctx);
    assert(out);
    out->px = NULL;
    out->w = 0;
    out->h = 0;
    if( image.value == 0 )
        return 0;
    if( !g_api->assets.image_size(g_api, image, &out->w, &out->h) || out->w <= 0 || out->h <= 0 )
        return 0;
    out->px = malloc((size_t)out->w * (size_t)out->h * sizeof(*out->px));
    assert(out->px);
    {
        size_t copied = 0;
        if( !g_api->assets.image_pixels(
                g_api, image, out->px, (size_t)out->w * (size_t)out->h, &copied) ||
            copied != (size_t)out->w * (size_t)out->h )
        {
            free(out->px);
            out->px = NULL;
            return 0;
        }
    }
    return 1;
}

/*
 * Read every real variant at one repeating position, compacted to the front.
 *
 * `image_ids` is MOBILE_PAPER_EDGE_VARIANTS_MAX (or FILL_VARIANTS) slots, some
 * of which are -1 -- the cutter did not ship a piece there because its source
 * had nowhere good enough left to cut one from. @see MOBILE_IMAGE_FILE. A
 * missing slot is skipped rather than counted, so `out[0..count)` is always
 * the pieces that DID load with no gap in the middle for a caller to trip on,
 * and `out[count..max_count)` is left zeroed by the caller's own memset --
 * `free(NULL)` on those in teardown is the ordinary idiom.
 */
static int
mobile_paper_fetch_set(
    struct MobileCall* ctx,
    int image_base,
    int max_count,
    struct MobilePaperPiece* out)
{
    int count = 0;

    assert(ctx);
    assert(image_base >= 0);
    assert(out);
    assert(max_count > 0);
    for( int i = 0; i < max_count; i++ )
        if( mobile_paper_fetch(ctx, mobile_image(ctx, image_base + i), &out[count]) )
            count++;
    return count;
}

/*
 * Which variant lands in one repeat cell -- deterministic, not random.
 *
 * The sheet must not shimmer between two draws of the same box, so this is a
 * pure function of the cell's position and cannot reach for a real RNG; a
 * cheap integer hash gets the look random asks for (no fixed cycle to notice)
 * without needing a seed anyone has to carry across frames. `salt` keeps the
 * four edges and the fill from choosing in lockstep -- without it, cell 0 of
 * every repeating position would always pick variant 0.
 */
static unsigned
mobile_paper_variant_hash(
    int cell,
    int salt)
{
    unsigned h = (unsigned)cell * 2654435761u + (unsigned)salt * 40503u;
    h ^= h >> 15;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    return h;
}

/*
 * Repeat along x, picking a variant per cell -- the top and bottom edges.
 *
 * Steps by the CHOSEN piece's own width and not a shared constant: two
 * variants of one edge are not the same period any more (the source only
 * wraps cleanly at a few exact spacings, so forcing one on every variant left
 * one of them a visibly bad seam -- @see tools/cut_chat_sheet_tiles.py,
 * wrap_variants), so the walk has to ask each piece its size as it goes.
 *
 * The clip on the last copy is where a tiled strip could show a seam, and it
 * does not: every variant was cut where its own tear happens to repeat, so a
 * whole copy butting the next is continuous, and the clipped last copy butts
 * a CORNER, which is a hard cut in the sheet's own art either way.
 */
static void
mobile_paper_tile_edge_h(
    uint32_t* dst,
    int dst_w,
    struct MobilePaperPiece const* variant,
    int variant_count,
    int salt,
    int x0,
    int y0,
    int x1,
    int y1)
{
    int cell = 0;

    assert(dst);
    assert(variant);
    assert(variant_count > 0);
    for( int x = x0; x < x1; cell++ )
    {
        struct MobilePaperPiece const* p =
            &variant[mobile_paper_variant_hash(cell, salt) % (unsigned)variant_count];
        int const cols = (x1 - x) < p->w ? (x1 - x) : p->w;
        int const rows = (y1 - y0) < p->h ? (y1 - y0) : p->h;

        for( int r = 0; r < rows; r++ )
            memcpy(
                &dst[(size_t)(y0 + r) * (size_t)dst_w + (size_t)x],
                &p->px[(size_t)r * (size_t)p->w],
                (size_t)cols * sizeof(*dst));
        x += p->w;
    }
}

/** Exactly mobile_paper_tile_edge_h, along y -- the left and right edges. */
static void
mobile_paper_tile_edge_v(
    uint32_t* dst,
    int dst_w,
    struct MobilePaperPiece const* variant,
    int variant_count,
    int salt,
    int x0,
    int y0,
    int x1,
    int y1)
{
    int cell = 0;

    assert(dst);
    assert(variant);
    assert(variant_count > 0);
    for( int y = y0; y < y1; cell++ )
    {
        struct MobilePaperPiece const* p =
            &variant[mobile_paper_variant_hash(cell, salt) % (unsigned)variant_count];
        int const rows = (y1 - y) < p->h ? (y1 - y) : p->h;
        int const cols = (x1 - x0) < p->w ? (x1 - x0) : p->w;

        for( int r = 0; r < rows; r++ )
            memcpy(
                &dst[(size_t)(y + r) * (size_t)dst_w + (size_t)x0],
                &p->px[(size_t)r * (size_t)p->w],
                (size_t)cols * sizeof(*dst));
        y += p->h;
    }
}

/*
 * The middle: repeats along BOTH axes, a variant picked per (row, column).
 *
 * Its own walk and not two passes of the edge tilers, because the middle is
 * the one piece that has to choose independently in two directions at once --
 * an edge only ever advances along its own line. `92821` is an arbitrary odd
 * multiplier with no meaning beyond spreading a row index away from a column
 * index before they fold into one hash; any prime-ish constant does the job.
 */
static void
mobile_paper_tile_fill(
    uint32_t* dst,
    int dst_w,
    struct MobilePaperPiece const* variant,
    int variant_count,
    int x0,
    int y0,
    int x1,
    int y1)
{
    int row = 0;

    assert(dst);
    assert(variant);
    assert(variant_count > 0);
    for( int y = y0; y < y1; row++ )
    {
        int col = 0;
        int step_h = 0;

        for( int x = x0; x < x1; col++ )
        {
            struct MobilePaperPiece const* p =
                &variant[mobile_paper_variant_hash(row * 92821 + col, 0) % (unsigned)variant_count];
            int const cols = (x1 - x) < p->w ? (x1 - x) : p->w;
            int const rows = (y1 - y) < p->h ? (y1 - y) : p->h;

            for( int r = 0; r < rows; r++ )
                memcpy(
                    &dst[(size_t)(y + r) * (size_t)dst_w + (size_t)x],
                    &p->px[(size_t)r * (size_t)p->w],
                    (size_t)cols * sizeof(*dst));
            step_h = p->h;
            x += p->w;
        }
        y += step_h;
    }
}

/*
 * The parchment at any size: a nine-patch whose edges and middle are TILED --
 * and, at each repeating position, chosen from several real variants rather
 * than one tile stamped end to end.
 *
 * Tiled and not stretched, which is the whole reason the sheet stopped being
 * one picture. A torn edge has a grain -- the tears are a few pixels across
 * and a couple of dozen apart -- and a resampler that has to reach 900 columns
 * from 517 does not make more tears, it makes the same tears half as sharp and
 * twice as wide. Repeating the strip makes more of them, at the size they were
 * drawn -- and repeating ONE strip is still a stamp, which is what the several
 * variants at each position are for. @see tools/cut_chat_sheet_tiles.py.
 *
 * The corner box is read off the top-left piece rather than named here, so the
 * art can be recut at a different corner size without this function knowing.
 * What it cannot absorb is a sheet SMALLER than two corners each way, which is
 * a box no chatbox has ever asked for -- 96x52 against a 2004 chat's 517x130 --
 * and is refused rather than clamped: silently composing something other than
 * the size asked for is how a backing ends up not under its chat.
 */
static struct ToriRS_ImageRef
mobile_compose_paper(
    struct MobileCall* ctx,
    char const* name,
    int width,
    int height)
{
    struct MobilePaperPiece corner[4];
    struct MobilePaperPiece top[MOBILE_PAPER_EDGE_VARIANTS_MAX];
    struct MobilePaperPiece bottom[MOBILE_PAPER_EDGE_VARIANTS_MAX];
    struct MobilePaperPiece left[MOBILE_PAPER_EDGE_VARIANTS_MAX];
    struct MobilePaperPiece right[MOBILE_PAPER_EDGE_VARIANTS_MAX];
    struct MobilePaperPiece fill[MOBILE_PAPER_FILL_VARIANTS];
    int top_count, bottom_count, left_count, right_count, fill_count;
    uint32_t* out;
    int corner_w = 0;
    int corner_h = 0;
    struct ToriRS_ImageRef handle = { 0 };

    assert(ctx);
    assert(name);
    assert(width > 0);
    assert(height > 0);

    /* Cleared first so the teardown below can free every slot -- fetched or
     * not, compacted or trailing -- however far each fetch got. */
    memset(corner, 0, sizeof(corner));
    memset(top, 0, sizeof(top));
    memset(bottom, 0, sizeof(bottom));
    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));
    memset(fill, 0, sizeof(fill));

    for( int i = 0; i < 4; i++ )
        if( !mobile_paper_fetch(ctx, mobile_image(ctx, IMG_PAPER_0 + i), &corner[i]) )
            goto done;
    top_count = mobile_paper_fetch_set(ctx, IMG_PAPER_TOP_0, MOBILE_PAPER_EDGE_VARIANTS_MAX, top);
    bottom_count =
        mobile_paper_fetch_set(ctx, IMG_PAPER_BOTTOM_0, MOBILE_PAPER_EDGE_VARIANTS_MAX, bottom);
    left_count =
        mobile_paper_fetch_set(ctx, IMG_PAPER_LEFT_0, MOBILE_PAPER_EDGE_VARIANTS_MAX, left);
    right_count =
        mobile_paper_fetch_set(ctx, IMG_PAPER_RIGHT_0, MOBILE_PAPER_EDGE_VARIANTS_MAX, right);
    fill_count = mobile_paper_fetch_set(ctx, IMG_PAPER_FILL_0, MOBILE_PAPER_FILL_VARIANTS, fill);
    if( top_count <= 0 || bottom_count <= 0 || left_count <= 0 || right_count <= 0 ||
        fill_count <= 0 )
        goto done;

    corner_w = corner[0].w;
    corner_h = corner[0].h;
    if( width < 2 * corner_w || height < 2 * corner_h )
        goto done;

    /* Zeroed, because the corners are the only pieces that cover their box
     * exactly and a tear leaves transparent pixels behind it. */
    out = calloc((size_t)width * (size_t)height, sizeof(*out));
    assert(out);

    mobile_paper_tile_fill(
        out, width, fill, fill_count, corner_w, corner_h, width - corner_w, height - corner_h);
    mobile_paper_tile_edge_h(
        out, width, top, top_count, 1, corner_w, 0, width - corner_w, corner_h);
    mobile_paper_tile_edge_h(
        out, width, bottom, bottom_count, 2, corner_w, height - corner_h, width - corner_w, height);
    mobile_paper_tile_edge_v(
        out, width, left, left_count, 3, 0, corner_h, corner_w, height - corner_h);
    mobile_paper_tile_edge_v(
        out, width, right, right_count, 4, width - corner_w, corner_h, width, height - corner_h);
    /* The corners last: each overwrites the two strips that ran up to it, so
     * the strips may be laid without knowing where they stop. */
    mobile_paper_tile_edge_h(out, width, &corner[0], 1, 0, 0, 0, corner_w, corner_h);
    mobile_paper_tile_edge_h(out, width, &corner[1], 1, 0, width - corner_w, 0, width, corner_h);
    mobile_paper_tile_edge_h(out, width, &corner[2], 1, 0, 0, height - corner_h, corner_w, height);
    mobile_paper_tile_edge_h(
        out, width, &corner[3], 1, 0, width - corner_w, height - corner_h, width, height);

    handle = mobile_publish(ctx, name, width, height, out);
    free(out);

done:
    for( int i = 0; i < 4; i++ )
        free(corner[i].px);
    for( int i = 0; i < MOBILE_PAPER_EDGE_VARIANTS_MAX; i++ )
    {
        free(top[i].px);
        free(bottom[i].px);
        free(left[i].px);
        free(right[i].px);
    }
    for( int i = 0; i < MOBILE_PAPER_FILL_VARIANTS; i++ )
        free(fill[i].px);
    return handle;
}

/*
 * How big the chat surface wants to be, on THIS lane.
 *
 * The one measurement in this frame that is not the frame's to make. Every
 * other box here is the plugin's art at a size the plugin chose; the chatbox
 * is a live surface with a fixed interior -- a message column, a scrollbar
 * beside it, an input line under a rule -- and how wide that interior is, is a
 * fact about the revision. A 2004 revconfig authors it at 479x96 and the chat
 * renderer agrees (clip to 463, scrollbar at x+463, rule at y+77); an
 * OldSchool cache mounts interface 162 into a 519x165 layer.
 *
 * Both numbers used to be constants here. That is a plugin asserting the shape
 * of every cache it will ever be loaded against, and it was already wrong on
 * two of the four OldSchool toplevels -- which is why the API grew a verb to
 * ask with. @see ToriRS_FrameApi::surface_native_size.
 *
 * The fallback is the lane's own default and not an error path: a frame whose
 * chat is sized as a proportion of its parent has no native size to report,
 * and 0 there means "you choose", not "something went wrong".
 */
static void
mobile_chat_native(
    struct MobileCall* ctx,
    int oldschool,
    int* out_w,
    int* out_h)
{
    struct ToriRS_WidgetBounds box;

    assert(ctx);
    assert(out_w);
    assert(out_h);
    *out_w = oldschool ? MOBILE_O_CHAT_W_DEFAULT : MOBILE_CHAT_W_DEFAULT;
    *out_h = oldschool ? MOBILE_O_CHAT_H_DEFAULT : MOBILE_CHAT_H_DEFAULT;
    /* By ELEMENT and not by the plugin's own `enum FrameSurface` handed to the
     * API as though it were a TORIRS_SURFACE_*. The two numberings differ, and
     * that this call worked at all was luck: CHAT is the one rung where they
     * agree. @see Porcelain_NativeSize. */
    if( !Porcelain_NativeSize(ctx->state->porcelain, PORCELAIN_EL(CHAT), &box) )
        return;
    if( box.width > 0 )
        *out_w = box.width;
    if( box.height > 0 )
        *out_h = box.height;
}

/*
 * One composed picture per size, kept until the size changes.
 *
 * The handle outlives the description that asked for it, because the size it
 * was composed for outlives it too: a canvas that is not being dragged asks for
 * the same sheet every describe, and re-tiling a 517x130 picture on each of
 * them would be the frame's whole cost.
 *
 * The size is in the NAME as well as in the cache, so a re-composition at a new
 * box is a new picture rather than a rewrite of one the host may still be
 * painting; the old handle is released after the new one exists. The name is
 * copied INTO the cache and `art.name` points at the copy, because a
 * description holds the name across fences and a local buffer is gone at the
 * return.
 *
 * The cached picture is an answer only while it is still ALIVE: the layer
 * releases a composed picture no description has named for several runs, and
 * handing back the name of one it has released describes a picture the host
 * does not hold. @see mobile_art_alive.
 */
static struct MobileArt
mobile_paper_art(
    struct MobileCall* ctx,
    int width,
    int height)
{
    char name[sizeof(g_paper.name)];
    struct ToriRS_ImageRef art;
    struct MobileArt const nothing = { NULL, { 0 } };

    assert(ctx);
    assert(width > 0);
    assert(height > 0);
    if( mobile_art_alive(ctx, g_paper.art) && g_paper.w == width && g_paper.h == height )
        return g_paper.art;

    snprintf(name, sizeof(name), "chat_paper_%dx%d.png", width, height);
    art = mobile_compose_paper(ctx, name, width, height);
    if( art.value == 0 )
        return mobile_art_alive(ctx, g_paper.art) ? g_paper.art : nothing;
    if( g_paper.art.ref.value != 0 )
        g_api->assets.image_release(g_api, g_paper.art.ref);
    (void)snprintf(g_paper.name, sizeof(g_paper.name), "%s", name);
    g_paper.art.name = g_paper.name;
    g_paper.art.ref = art;
    g_paper.w = width;
    g_paper.h = height;
    return g_paper.art;
}

/** The 2004 strip at `width` by `height`, composed once per (size, stones).
 *  @see mobile_paper_art, whose bargain this is. */
static struct MobileArt
mobile_bar_art(
    struct MobileCall* ctx,
    int width,
    int height,
    struct MobileChatCell const* cell,
    int cell_count)
{
    char name[sizeof(g_bar.name)];
    struct ToriRS_ImageRef art;
    struct MobileArt const nothing = { NULL, { 0 } };
    uint32_t key = (uint32_t)cell_count * 2654435761u;

    assert(ctx);
    assert(width > 0);
    assert(height > 0);
    assert(cell_count >= 0);
    assert(cell_count == 0 || cell);
    /* The stones are folded into a KEY rather than compared one by one: the
     * chatbox is rebuilt on the lane's schedule, and what matters is only that
     * a moved filter re-cuts the bar and an unmoved one does not. */
    for( int i = 0; i < cell_count; i++ )
        key = (key * 16777619u) ^
              (uint32_t)((cell[i].x * 31 + cell[i].y) * 31 + cell[i].w * 31 + cell[i].h);
    if( mobile_art_alive(ctx, g_bar.art) && g_bar.w == width && g_bar.h == height &&
        g_bar.key == key )
        return g_bar.art;

    snprintf(name, sizeof(name), "chat_bar_%dx%d_%08x.png", width, height, key);
    art = mobile_compose_classic_bar(ctx, name, width, height, cell, cell_count);
    if( art.value == 0 )
        return mobile_art_alive(ctx, g_bar.art) ? g_bar.art : nothing;
    if( g_bar.art.ref.value != 0 )
        g_api->assets.image_release(g_api, g_bar.art.ref);
    (void)snprintf(g_bar.name, sizeof(g_bar.name), "%s", name);
    g_bar.art.name = g_bar.name;
    g_bar.art.ref = art;
    g_bar.w = width;
    g_bar.h = height;
    g_bar.key = key;
    return g_bar.art;
}
/*
 * Is there room for the sheet AND the drawer, or does one have to give way?
 *
 * The sheet is pinned to the bottom-left and the drawer to the bottom-right,
 * and on a wide canvas they never meet -- which is the case this frame is
 * really for. On a narrow one they would overlap, and both are LIVE surfaces
 * the host draws rather than art this plugin blits, so the overlap would not
 * be one of them winning cleanly: it would be a chat log and an inventory
 * painted through each other.
 *
 * So the drawer wins and the sheet is not placed. Stated as a fact about the
 * geometry rather than as a rule about clicks, because that keeps it out of the
 * click paths entirely -- the player's intent is untouched, the switch goes on
 * working, and the sheet comes back by itself the moment the drawer is shut.
 */
static int
mobile_chat_visible(
    struct MobileCall* ctx,
    int canvas_w,
    int rail_w,
    int chat_w)
{
    if( !g_chat_open )
        return 0;
    if( !g_drawer_open )
        return 1;
    /* The ART's width and not the surface's: the sheet's torn fringe overhangs
     * it on the right too, so a canvas that fits 479 but not 517 would slide
     * the parchment under the drawer. @see MOBILE_PAPER_ART_W. On an OldSchool
     * lane the chat pack is its own 519. */
    return canvas_w - MOBILE_MARGIN - rail_w - MOBILE_PANEL_W >= chat_w;
}
/*
 * The OPEN panel, as an element: the mount whose interface the drawer shows.
 *
 * What a piece that belongs UNDER the panel names as its depth target. The
 * sidebar CONTAINER would be the obvious thing to name and cannot be: a 2004
 * lane has none -- `rs245_2lc_dat1_ui.ini` declares the fourteen mounts and
 * seats each under `fixed_shell` -- and an unresolved depth target is no
 * anchor at all, which leaves the piece exactly where it was.
 *
 * `tab_active_shown` is the mount the lane has UNHIDDEN, read back through
 * cache.tab_active, so it cannot disagree with which panel is painting. Out of
 * range means the frame does not know yet, and a piece with no target is a
 * piece over the scene -- which is what it was before, and harmless while
 * there is no panel for it to be under.
 */
static struct PorcelainElement
mobile_panel_element(struct MobileCall* ctx)
{
    int const tab = ctx->state->tab_active_shown;

    assert(ctx);
    if( tab < 0 || tab >= MOBILE_TAB_COUNT )
        return PORCELAIN_EL(NONE);
    return PORCELAIN_ROLE_EL(ctx->state->member_role[FRAME_SURFACE_SIDEBAR][tab]);
}

/*
 * Does this cache HAVE this tab?
 *
 * Asked of the ELEMENT, which answers it whether the drawer is open or shut. It
 * used to be learned from whether the sidebar mount was PLACED -- a question
 * only an open drawer could ask -- and the answer was written `true` and never
 * written false, so the three guards that read it were unreachable and
 * rs289lc's missing clan tab was never detected. That is the ledger's "Tab
 * presence" defect, and the row's own API column is this call.
 *
 * NOT-ABSENT rather than BOUND: an element that has not resolved yet is
 * PENDING, and a rail that dropped half its cells for the fences before the
 * lane mounts its sidebar would flicker. The member role is the same one
 * mobile_describe_surfaces asks about to place the mount, so this registers no
 * watch the description did not already own.
 */
static bool
mobile_tab_present(
    struct MobileCall* ctx,
    int tab)
{
    struct PorcelainElementState state;

    assert(ctx);
    assert(tab >= 0);
    assert(tab < MOBILE_TAB_COUNT);
    (void)Porcelain_Element(
        ctx->state->porcelain,
        PORCELAIN_ROLE_EL(ctx->state->member_role[FRAME_SURFACE_SIDEBAR][tab]),
        &state);
    g_tab_present[tab] = state.bind != PORCELAIN_ABSENT;
    return g_tab_present[tab];
}

/*
 * The 2004 rail: the two turned tab rows. @see MOBILE_ROCK, MOBILE_TAB_STONE.
 *
 * Fills g_frame.tab and places each present tab's mount when the drawer is
 * open. Split out of mobile_layout so the OldSchool rail beside it is a
 * sibling and not a branch through forty lines of arithmetic.
 */
static void
mobile_layout_rail_classic(
    struct MobileCall* ctx,
    int rail_x,
    int rail_y,
    int panel_x,
    int panel_y)
{
    assert(ctx);
    /*
     * The two columns, each a turned row stacked from its own top.
     *
     * The running offset is the point: a cell's height is the classic box's
     * width and those differ down the column, so a cell's place is the sum of
     * everything above it and not its index times a stride. A tab this cache
     * lacks still takes its place in that sum -- the rhythm belongs to the ROW,
     * and closing the gap would shift every stone under it onto a neighbour's.
     */
    for( int col = 0; col < MOBILE_RAIL_COLS; col++ )
    {
        int const first = col * MOBILE_RAIL_ROWS;
        int const plate_x = rail_x + (col * MOBILE_RAIL_COL_W);

        /* The plate first, then the stones that stand on it. Both columns are
         * pinned to the rail's top, and being one picture twice they end
         * level. */
        mobile_blit(
            ctx,
            col == 0 ? "piece.rail.0" : "piece.rail.1",
            mobile_art(ctx, col == 0 ? ART_PLATE_0 : ART_PLATE_1),
            plate_x,
            rail_y);

        for( int row = 0; row < MOBILE_RAIL_ROWS; row++ )
        {
            int const tab = first + row;
            /*
             * A cell IS its rock: the box the stone and the icon are centred
             * in, and the box a tap answers.
             *
             * Both columns are the same plate, so both read the same rock --
             * measured from the plate's far end, the plate being on its head,
             * and inset from opposite edges, the right-hand plate being the
             * mirrored one.
             */
            struct MobileRock const* rock = &MOBILE_ROCK[row];
            int const cell_h = rock->span;
            int const cell_y = rail_y + MOBILE_RAIL_COL_H - rock->start - rock->span;
            int const cell_x = plate_x + (col == 0 ? MOBILE_PLATE_BAND_Y
                                                   : MOBILE_RAIL_COL_W - MOBILE_PLATE_BAND_Y -
                                                         MOBILE_PLATE_BAND_D);
            int const cell_w = MOBILE_PLATE_BAND_D;
            struct MobileTab* entry;
            /* The mount is placed only while the drawer is open; whether the
             * cache HAS the tab is its own question now, and one the element
             * answers drawer open or shut. @see mobile_tab_present. */
            if( g_drawer_open )
                mobile_member(
                    ctx,
                    FRAME_SURFACE_SIDEBAR,
                    tab,
                    panel_x,
                    panel_y,
                    MOBILE_PANEL_W,
                    MOBILE_PANEL_H);
            if( !mobile_tab_present(ctx, tab) )
                continue;

            entry = &g_frame.tab[g_frame.tab_count++];
            entry->x = cell_x;
            entry->y = cell_y;
            entry->w = cell_w;
            entry->h = cell_h;
            entry->tabno = tab;
            /*
             * The icon comes from the set the LANE numbers its panels by, not
             * from the era this rail's rocks were cut in.
             *
             * A stone's icon names the panel behind it, and the two eras do
             * not agree: `sideicons.dat` has nothing at 7, friends at 8 and
             * ignore at 9, while rev-239 has chat-channel at 7, account
             * management at 8 and friends at 9. So a 2004 rail over an
             * OldSchool lane wearing the 2004 set left the live seventh rock
             * bare and put the frowning ignore face on Friends. The OldSchool
             * rail beside this one already reads the same table.
             */
            entry->icon = mobile_art_file(
                ctx, (mobile_lane_oldschool(ctx) ? IMG_O_SIDEICON_0 : IMG_SIDEICON_0) + tab);
            entry->lit = mobile_art(ctx, ART_STONE_0 + tab);
        }
    }
}

/*
 * The OldSchool rail: 601's two tab columns on one dark plate.
 *
 * Every cell is a stone, blitted here, and the open one wears the red stone
 * over it in the draw pass -- which is what 601 does (`tli_button01` 0 under
 * every tab, 2 on the selected). A tab this cache lacks keeps its cell and
 * shows the bare plate. @see MOBILE_O_COLUMN.
 */
static void
mobile_layout_rail_oldschool(
    struct MobileCall* ctx,
    int rail_x,
    int rail_y,
    int panel_x,
    int panel_y)
{
    assert(ctx);
    mobile_blit(ctx, "piece.rail.0", mobile_art(ctx, ART_O_RAIL), rail_x, rail_y);
    for( int col = 0; col < MOBILE_RAIL_COLS; col++ )
    {
        int const cell_x = rail_x + MOBILE_O_BORDER + col * MOBILE_O_STONE;

        for( int row = 0; row < MOBILE_RAIL_ROWS; row++ )
        {
            int const tab = MOBILE_O_COLUMN[col][row];
            int const cell_y = rail_y + MOBILE_O_BORDER + row * MOBILE_O_STRIDE;
            struct MobileTab* entry;

            if( g_drawer_open )
                mobile_member(
                    ctx,
                    FRAME_SURFACE_SIDEBAR,
                    tab,
                    panel_x,
                    panel_y,
                    MOBILE_PANEL_W,
                    MOBILE_PANEL_H);
            if( !mobile_tab_present(ctx, tab) )
                continue;

            entry = &g_frame.tab[g_frame.tab_count++];
            entry->x = cell_x;
            entry->y = cell_y;
            entry->w = MOBILE_O_STONE;
            entry->h = MOBILE_O_STONE;
            entry->tabno = tab;
            entry->icon = mobile_art_file(ctx, IMG_O_SIDEICON_0 + tab);
            entry->lit = mobile_art_file(ctx, IMG_O_STONE_LIT);
            /* The idle stone is the CELL's own face here rather than a blit of
             * its own: 601's lit stone is the same 40x40 box and covers it
             * exactly, so one described picture that swaps is the same picture
             * a stone under a stone made -- fourteen fewer described items, and
             * the classic rail already works this way, with the plate behind
             * the cell as its idle face. */
            entry->idle = mobile_art_file(ctx, IMG_O_STONE);
        }
    }
}
static void
mobile_layout(
    struct MobileCall* ctx,
    int canvas_w,
    int canvas_h)
{
    enum MobileFamily const family = mobile_family(ctx);
    int const oldschool = mobile_lane_oldschool(ctx);
    int const rail_w = family == FAMILY_OLDSCHOOL ? MOBILE_O_RAIL_W : MOBILE_RAIL_W;
    int const rail_h = family == FAMILY_OLDSCHOOL ? MOBILE_O_RAIL_H : MOBILE_RAIL_H;
    /* EVERY edge below is this box's rather than the window's, the left and
     * the top included: that is what a safe rect with a non-zero origin -- a
     * notch, a status bar -- means, and reading only its bottom is the defect
     * the ledger's safe-area row names. `area.x` and `area.y` are 0 on every
     * lane in this tree today, so this is arithmetic that costs nothing until
     * a platform states otherwise. @see mobile_lane_area. */
    struct MobileArea const area = mobile_lane_area(ctx, canvas_w, canvas_h);
    int const area_right = area.x + area.w;
    int const area_bottom = area.y + area.h;
    int const rail_x = area_right - MOBILE_MARGIN - rail_w;
    int const rail_y = area_bottom - MOBILE_MARGIN - rail_h;
    /* The drawer hangs off the rail's inner edge and shares its bottom margin,
     * so the two read as one assembly rather than two things that happen to be
     * in the same corner. */
    int const panel_x = rail_x - MOBILE_PANEL_W;
    int const panel_y = area_bottom - MOBILE_MARGIN - MOBILE_PANEL_H;
    struct MobileHousing const* housing = mobile_housing(ctx);
    int const map_x = area_right - MOBILE_MARGIN - g_map_w;
    int const map_y = area.y + MOBILE_MARGIN;
    int const safe_bottom = area_bottom;
    /*
     * The row the chat block hangs from, and the margin it was missing.
     *
     * `safe_bottom` is the last row this frame may draw on. It is not the row
     * anything SITS on: every other piece pinned to that edge -- the rail, the
     * drawer, and the two switches a few lines down -- is inset from it by
     * MOBILE_MARGIN, and the chat block was the one assembly that took the raw
     * edge instead. Flush with it, the parchment's torn bottom fringe is off
     * the screen entirely and the input line's last row IS the window's last
     * row, which on a frame whose whole point is that the sheet FLOATS reads
     * as a sheet that has slid off the bottom.
     *
     * A separate name rather than moving `safe_bottom` itself, because the
     * switch in the corner reads the raw edge when the sheet is down (@see
     * g_frame.toggle_y) and is already correctly inset from it: insetting
     * twice would put the switch four rows above the drawer beside it.
     */
    int const chat_bottom = safe_bottom - MOBILE_MARGIN;
    int strip_y;
    int chat_y;
    /* The surface's size is the LANE's, asked for once and then used
     * everywhere: the sheet is composed for it, the strip is that wide, the
     * four filter buttons are spread across it and the tap-blocker covers it.
     * @see mobile_chat_native. */
    int chat_w = 0;
    int chat_h = 0;
    int chat_visible;

    assert(ctx);
    mobile_chat_native(ctx, oldschool, &chat_w, &chat_h);
    /* The ART's width on a 2004 lane and the pack's own on an OldSchool one:
     * the parchment's torn fringe overhangs the surface on both sides, so a
     * canvas that fits the surface but not the sheet would slide the paper
     * under the drawer. @see MOBILE_PAPER_ART_W. */
    chat_visible =
        mobile_chat_visible(ctx, area.w, rail_w, oldschool ? chat_w : MOBILE_PAPER_ART_W(chat_w));

    /* The safe rect and the lane's strip are both in `area`, so every
     * bottom-anchored piece uses the same visible edge -- inset by the same
     * margin every other piece on that edge is inset by. @see chat_bottom. */
    strip_y = chat_bottom - MOBILE_STRIP_H;
    /* Through the macro rather than `strip_y - chat_h`, so the block is
     * still pinned by the ART's last inked row and not by the surface's -- the
     * safe bottom moved which edge it hangs from, not what hangs there.
     * @see MOBILE_CHAT_Y. The OldSchool pack is one 519x165 block sitting on
     * the bottom margin, its own stone bar included. */
    chat_y = oldschool ? chat_bottom - chat_h : MOBILE_CHAT_Y(chat_bottom, chat_h);

    /*
     * The scene is the WHOLE canvas, chrome included.
     *
     * That is what this frame is: every other piece floats on the world rather
     * than beside it, which is the one decision the whole layout follows from.
     */
    mobile_surface(ctx, FRAME_SURFACE_VIEWPORT, area.x, area.y, area.w, area.h);

    /* The housing is attached to the minimap rather than blitted globally, so
     * it paints immediately under that one live surface instead of over the
     * whole frame. */
    mobile_ui_node(
        ctx,
        "frame.minimap.housing",
        (struct ToriRS_Rect){ map_x, map_y, g_map_w, g_map_h },
        mobile_art_file(ctx, housing->art));
    /* Both surfaces go in the windows the RING says it has, at the boxes the
     * housing states. @see MobileHousing. */
    mobile_surface(
        ctx,
        FRAME_SURFACE_MINIMAP,
        map_x + g_hole_map.x,
        map_y + g_hole_map.y,
        g_hole_map.w,
        g_hole_map.h);
    mobile_surface(
        ctx,
        FRAME_SURFACE_COMPASS,
        map_x + g_hole_compass.x,
        map_y + g_hole_compass.y,
        g_hole_compass.w,
        g_hole_compass.h);
    /*
     * And the shape each of them is cut to.
     *
     * Both surfaces are LIVE -- the minimap is baked from the world and the
     * compass turns with the camera -- so neither can be blitted into a round
     * hole; a housing has to state where its holes are AND what shape they are,
     * and stating only the first leaves a square map in a round window. That is
     * what was on screen: the compass drew its four corners over the housing's
     * rounded one, and the map filled its box out to the edges.
     *
     * The compass keeps the LANE's art. It is the 2004 rose already, and the
     * only thing wrong with it was the shape it was cut to. An empty art half
     * leaves that half of the native picture alone -- the two halves of a skin
     * are independent, which is exactly the shape this wanted.
     */
    g_frame.skin_minimap = (struct MobileSkin){
        1, { NULL, { 0 } },
         mobile_mask(ctx, ART_MINIMAP_MASK)
    };
    /* The compass keeps the LANE's rose on a 2004 lane: it is this rose
     * already. On an OldSchool lane the cache's rose is OldSchool's, so the
     * classic family brings the 2004 one with it; the OldSchool family keeps
     * the cache's, which is the picture its ring was cut for. */
    g_frame.skin_compass = (struct MobileSkin){
        1,
        oldschool && family == FAMILY_CLASSIC ? mobile_art_file(ctx, IMG_COMPASS)
                                              : (struct MobileArt){ NULL, { 0 } },
        mobile_mask(ctx, ART_COMPASS_MASK)
    };
    /*
     * The orb block beside the map, where the OldSchool frames keep it. A
     * lane with no such block -- every 2004 one -- answers 0 and nothing
     * moves; the minimap-orbs plugin adds its own there.
     */
    mobile_surface(
        ctx,
        FRAME_SURFACE_ORBS,
        map_x + g_hole_map.x + MOBILE_O_ORBS_DX,
        map_y + g_hole_map.y + MOBILE_O_ORBS_DY,
        MOBILE_O_ORBS_W,
        MOBILE_O_ORBS_H);
    mobile_member(
        ctx,
        FRAME_SURFACE_ORBS,
        FRAME_ORBS_MEMBER_ACTIVITY_ADVISER,
        map_x + g_hole_map.x + MOBILE_O_ORBS_DX + MOBILE_O_ADVISER_DX,
        map_y + g_hole_map.y + MOBILE_O_ORBS_DY + MOBILE_O_ADVISER_DY,
        MOBILE_O_ADVISER_W,
        MOBILE_O_ADVISER_H);
    /*
     * And the XP counter, which travels with that block but is not IN it.
     *
     * Interface 122 is mounted on the TOPLEVEL, not on interface 160, so
     * moving the orb column leaves it behind -- at the lane's own right edge,
     * which on 548 is the fixed viewport's. With the Stone Drawer over 548 at
     * 1200x800 that stranded the counter at (395,4) while the XP orb it reads
     * for stood at (868,31).
     *
     * Pinned to the block's top-left corner, which is what puts the counter
     * just left of the XP toggle: the counter is anchored 2 columns in from
     * this slot's right edge and sits on its top row, and the toggle is the
     * first thing in the block at the block's own left edge. That is also the
     * relationship 548 authors natively -- its slot's right edge IS the orb
     * block's left -- so this states the cache's spot rather than a new one.
     */
    g_frame.xp_drops_placed = 1;
    g_frame.xp_drops_right = map_x + g_hole_map.x + MOBILE_O_ORBS_DX;
    g_frame.xp_drops_top = map_y + g_hole_map.y + MOBILE_O_ORBS_DY;

    /*
     * Behind the open PANEL rather than over the scene: this is that panel's
     * backing, and it is recorded only while the drawer is open -- which is
     * exactly what used to put it on top of a surface that had been raised
     * before it existed. @see MobileBlit::behind.
     *
     * The MOUNT and not the sidebar container, because a 2004 lane has no
     * container to name: `rs245_2lc_dat1_ui.ini` declares the fourteen mounts
     * and seats them under `fixed_shell`, and a depth target that does not
     * resolve leaves the piece with its own draw index -- the defect, back
     * again on the one lane this frame was written for. The mount is the node
     * the live tab's interface hangs off on both.
     */
    if( g_drawer_open )
        mobile_blit_behind(
            ctx,
            "piece.drawer",
            mobile_art_file(ctx, family == FAMILY_OLDSCHOOL ? IMG_O_DRAWER : IMG_INVBACK),
            panel_x,
            panel_y,
            mobile_panel_element(ctx),
            PORCELAIN_EL(NONE));

    /*
     * The sheet, and nothing under the filter buttons.
     *
     * They float on the scene instead. A bar behind them is what a DOCKED frame
     * needs -- something for the row to sit on where the surround stops -- and
     * this frame has no surround for it to continue, so the bar read as a slab
     * of stone lying on the grass under four labels.
     *
     * On an OldSchool lane there is no global sheet blit: a retained paper
     * layer replaces the pack backing and extends one text line past it.
     */
    if( chat_visible && !oldschool )
        mobile_blit_behind(
            ctx,
            "piece.sheet",
            mobile_paper_art(ctx, MOBILE_PAPER_ART_W(chat_w), MOBILE_PAPER_ART_H(chat_h)),
            area.x,
            chat_y - MOBILE_PAPER_FRINGE_T,
            PORCELAIN_EL(CHAT),
            PORCELAIN_EL(CHAT));

    /* The switch sits directly above whatever is in that corner: the sheet when
     * it is up, the safe bottom margin when it is not. Pinned to the thing it
     * operates rather than to a coordinate, so it never floats away from it --
     * nor under the keyboard, which the safe bottom is what keeps it out of. */
    g_frame.toggle_art = family == FAMILY_OLDSCHOOL ? mobile_art_file(ctx, IMG_O_STONE)
                                                    : mobile_art_file(ctx, IMG_SWITCH);
    g_frame.toggle_w = family == FAMILY_OLDSCHOOL ? MOBILE_O_TOGGLE_W : MOBILE_TOGGLE_W;
    g_frame.toggle_h = family == FAMILY_OLDSCHOOL ? MOBILE_O_TOGGLE_H : MOBILE_TOGGLE_H;
    /*
     * WHICH END of that strip: the NEAR one, and flush with the sheet rather
     * than with the frame's own margin.
     *
     * The pair operates the block underneath it -- one hides the sheet, the
     * other raises the keys for the input line on it -- so the edge they line
     * up with is that block's, and the block's left edge is not the same
     * number on the two lanes.
     *
     * On a 2004 lane the sheet is this frame's own parchment, blitted at
     * `area.x`, and the switch keeps the MOBILE_MARGIN inset every other piece
     * pinned to this edge has.
     *
     * On an OldSchool lane the sheet is drawn from the PACK's backing, which
     * is inset inside the pack: on a 765x503 stone601 capture the pack stands
     * at x=0 519 wide and `chat_backing` at x=58 461 wide, and `pack-sheet` is
     * placed at the backing's box. A switch at the frame's margin would then
     * float 54 px clear of the paper it operates, over the world. So the pair
     * starts at the backing's own left edge, which is the first column of
     * parchment on screen.
     *
     * That column is still readable with the sheet DOWN, which is what keeps
     * the switch that brings the chat back from sliding sideways under the
     * finger that just pressed it: a hide is not an unbinding, so the backing
     * keeps its watch and its box, and a box that reads zero -- a backing the
     * chat view has just re-created -- falls back to the last good one.
     * @see mobile_chat_backing.
     *
     * What this gives up, stated because it was the reason the pair used to
     * take the far end of the same strip: that lane parks a HUD in this
     * corner. `stat_boosts_hud` (708) is laid out by the cache's own script
     * 4130, which reads the pair the client reports as its identity:
     *
     *     if (~on_mobile = 0) { if_setposition(2, 2, abs_left,  abs_bottom, ..)
     *     } else              { if_setposition(2, 0, abs_right, abs_bottom, ..) }
     *
     * -- bottom-LEFT for a desktop client and bottom-RIGHT for a mobile one,
     * precisely because OldSchool Mobile's own chrome owns the bottom-left
     * corner. This frame IS that chrome, but the client it runs in is a
     * desktop client (CS2VM2_SetClientIdentity is one fact about the process,
     * resolved at boot from the platform, and flipping it would change the
     * login block, the toplevel the server opens and every other mobile branch
     * in the cache), so the lane takes the desktop branch and the readout is
     * one 35-row row growing rightwards from x=2 along the bottom. No frame
     * can move it -- the dodge insets are a per-toplevel table (enum_1135/1136,
     * 250x165 on all three OldSchool tops) and not a read of whatever chrome is
     * on screen.
     *
     * With the sheet UP the two do not meet: the switches sit a margin above
     * the block's first row, and the block is 165 rows tall, so the whole of
     * it stands between them and that band. With the sheet DOWN they share
     * the band, and a player with a boosted stat sees the readout behind the
     * two stones until the sheet comes back. That is the trade this makes:
     * the switch is on the paper it operates on every frame the paper is up,
     * at the cost of the corner it shares with the HUD on the frames it is
     * not.
     */
    g_frame.toggle_x = area.x + MOBILE_MARGIN;
    if( oldschool )
    {
        struct PorcelainElementState backing;

        if( mobile_chat_backing(ctx, &backing) && backing.box.x > g_frame.toggle_x )
            g_frame.toggle_x = backing.box.x;
    }
    g_frame.toggle_y =
        (chat_visible ? (oldschool ? chat_y : chat_y - MOBILE_PAPER_FRINGE_T) : safe_bottom) -
        MOBILE_MARGIN - g_frame.toggle_h;
    mobile_blit(ctx, "piece.switch.chat", g_frame.toggle_art, g_frame.toggle_x, g_frame.toggle_y);
    /*
     * And the keyboard beside it -- ONLY where there is a keyboard to raise.
     *
     * A frame the player reaches with a finger needs a way to ASK for the keys
     * -- the client's chat input was written for a machine that always had
     * them, so nothing in it ever raises a keyboard. Tapping the chat asks for
     * one too, but a switch that is always in the same place is what makes it
     * possible to put the keyboard AWAY again, which a tap on the chat can
     * never mean.
     *
     * None of that is true on a desk, and the switch used to be placed there
     * anyway. This frame is offered on every lane -- it is in the Display
     * panel like any other -- so a desktop player who picks it got a KEYS
     * button whose press calls input.text_input and raises nothing, because
     * PlatformWindow_SetTextInput has no keyboard to show on a desktop backend
     * and its own `off` arm declines for exactly that reason.
     *
     * The question is the DEVICE's and not the input policy's:
     * `input.screen_keyboard` is the platform's own answer, where `touch` is a
     * policy TORIRS_TOUCH_UI can switch on over a desk. Gating on touch would
     * have put the dead button back for anyone doing that.
     *
     * The chat switch is NOT gated with it. That one hides and shows the chat
     * sheet, which every device has.
     */
    if( mobile_has_screen_keyboard(ctx) )
    {
        g_frame.keys_x = g_frame.toggle_x + g_frame.toggle_w + MOBILE_TOGGLE_GAP;
        g_frame.keys_y = g_frame.toggle_y;
        mobile_blit(ctx, "piece.switch.keys", g_frame.toggle_art, g_frame.keys_x, g_frame.keys_y);
    }
    /*
     * And the PLUGINS switch at the end of the row.
     *
     * Third and not second: the two before it are about the chat, and the row
     * reads as "the sheet, its keys, and then a different thing". It hangs off
     * whichever of them is last, because the keyboard is not always there.
     *
     * This row is the only launcher this frame has -- the rail and the pop-out
     * column both stand down on a phone, @see mobile_has_plugin_window -- so
     * it is placed wherever the row is rather than being given a corner of its
     * own. A player who can find the chat switch has found this one.
     */
    if( mobile_has_plugin_window(ctx) )
    {
        int const last_x = mobile_has_screen_keyboard(ctx) ? g_frame.keys_x : g_frame.toggle_x;

        g_frame.plugins_x = last_x + g_frame.toggle_w + MOBILE_TOGGLE_GAP;
        g_frame.plugins_y = g_frame.toggle_y;
        mobile_blit(
            ctx, "piece.switch.plugins", g_frame.toggle_art, g_frame.plugins_x,
            g_frame.plugins_y);
    }
    mobile_ui_node(
        ctx,
        "chat-toggle",
        (struct ToriRS_Rect){
            g_frame.toggle_x, g_frame.toggle_y, g_frame.toggle_w, g_frame.toggle_h },
        (struct MobileArt){ NULL, { 0 } });
    if( mobile_has_screen_keyboard(ctx) )
        mobile_ui_node(
            ctx,
            "keyboard-toggle",
            (struct ToriRS_Rect){
                g_frame.keys_x, g_frame.keys_y, g_frame.toggle_w, g_frame.toggle_h },
            (struct MobileArt){ NULL, { 0 } });
    if( mobile_has_plugin_window(ctx) )
        mobile_ui_node(
            ctx,
            "plugins-toggle",
            (struct ToriRS_Rect){
                g_frame.plugins_x, g_frame.plugins_y, g_frame.toggle_w, g_frame.toggle_h },
            (struct MobileArt){ NULL, { 0 } });

    /*
     * The ROLE, and then its members.
     *
     * Both, because they answer different questions: the role is "the sidebar,
     * wherever it is" and is what the host places the open panel by, while a
     * member is one tab's mount and is the only call that can report whether
     * this cache HAS that tab. Placing only the members left the role unplaced
     * -- the panel had fourteen mounts and no box.
     */
    g_frame.panel_x = panel_x;
    g_frame.panel_y = panel_y;
    if( g_drawer_open )
        mobile_surface(
            ctx, FRAME_SURFACE_SIDEBAR, panel_x, panel_y, MOBILE_PANEL_W, MOBILE_PANEL_H);

    mobile_ui_node(
        ctx,
        "frame.sidebar.rail",
        (struct ToriRS_Rect){ rail_x, rail_y, rail_w, rail_h },
        (struct MobileArt){ NULL, { 0 } });

    if( family == FAMILY_OLDSCHOOL )
        mobile_layout_rail_oldschool(ctx, rail_x, rail_y, panel_x, panel_y);
    else
        mobile_layout_rail_classic(ctx, rail_x, rail_y, panel_x, panel_y);

    /*
     * The modal is CENTRED on the canvas, not pinned to the frame.
     *
     * It is the one region that is about where the player is looking rather
     * than about the chrome, and the cache authored its contents against a
     * 512x334 box -- so the placement is free and the size is not. Shrinking it
     * to fit a phone would clip a bank rather than reflow one.
     */
    mobile_surface(
        ctx,
        FRAME_SURFACE_MODAL,
        area.x + (area.w - MOBILE_MODAL_W) / 2,
        area.y + (area.h - MOBILE_MODAL_H) / 2,
        MOBILE_MODAL_W,
        MOBILE_MODAL_H);

    g_frame.canvas_w = area.x + area.w;
    g_frame.canvas_h = area.y + area.h;
    g_frame.chat_bottom = chat_bottom;
    g_frame.chat_placed = chat_visible;
    g_frame.chat_y = chat_y;
    g_frame.chat_pack = oldschool;
    g_frame.chat_w = chat_w;
    g_frame.chat_h = chat_h;
    if( !chat_visible )
        return;

    /* The OldSchool chat pack: placed whole, at its own size, flush with the
     * corner. It keeps its text, actions and scrollbar; the retained chat
     * decoration contributions replace only its furniture.
     * @see MOBILE_O_CHAT_W_DEFAULT. */
    if( oldschool )
    {
        struct PorcelainElementState bar;
        int band = 0;

        /*
         * The filter bar BELOW the chat, on the one root that puts it above.
         *
         * The mobile toplevel pins `controls` to the top of the pack with the
         * message lines underneath it, which is the layout OldSchool Mobile
         * wants and is not the layout this frame wants: every other piece of
         * the Stone Drawer's chat -- the torn sheet, the stone strip, the two
         * switches -- is built on the bar being the row along the BOTTOM, and
         * a bar on top puts the filters over the parchment's torn top edge
         * with nothing under the last message line.
         *
         * The BAR moves, not the subtree. The pack keeps its own children, its
         * own actions and its own eight filters; what changes is the row one
         * layer sits on and the row the block as a whole starts at, and the
         * two are one move each:
         *
         *   - the pack goes up by the BAND, so the message area -- which is
         *     bottom-anchored inside the pack -- lands exactly on `chat_y`,
         *     where the block's first row belongs;
         *   - the bar goes to the block's last rows, `bar.height` up from
         *     `chat_y + chat_h`.
         *
         * So the assembly occupies the same rows it did, chat_y through
         * chat_y + chat_h, and everything pinned to those two numbers -- the
         * tap blocker, the switches, the strip -- is untouched. The pack's own
         * box hangs `band` rows above its first visible row, which costs
         * nothing: the root is a bare layer that draws no pixel of its own,
         * and the parchment is positioned from the BACKING rather than from it.
         */
        if( mobile_chat_bar_on_top(ctx) && mobile_chat_bar_band(ctx, chat_h, &band) &&
            Porcelain_Element(ctx->state->porcelain, MOBILE_CHAT_CONTROLS, &bar) &&
            bar.box.height > 0 && bar.box.width > 0 && bar.box.height <= band )
        {
            mobile_surface(ctx, FRAME_SURFACE_CHAT, area.x, chat_y - band, chat_w, chat_h);
            g_frame.chat_bar.placed = 1;
            g_frame.chat_bar.rect = (struct ToriRS_Rect){
                bar.box.x, chat_y + chat_h - bar.box.height, bar.box.width, bar.box.height
            };
            return;
        }
        mobile_surface(ctx, FRAME_SURFACE_CHAT, area.x, chat_y, chat_w, chat_h);
        return;
    }
    mobile_surface(ctx, FRAME_SURFACE_CHAT, area.x + MOBILE_PAPER_FRINGE_L, chat_y, chat_w, chat_h);
    /*
     * A button UNDER each label, and nothing behind the row.
     *
     * The labels are the lane's own and it draws them itself; what it does not
     * draw is anything for them to sit on, because on the 2004 frame they sit
     * on the surround. Giving each one the interface button it would have worn
     * anywhere else puts the chrome back where it belongs -- on the four
     * controls -- without laying a slab across the corner behind them.
     */
    /* The plates are named UI nodes rather than ordinary blits. The registry
     * resolves one appearance provider per plate, so a wider replacement does
     * not expose this frame's art at its edges. */

    /*
     * The four filter buttons stay the LANE's.
     *
     * Their actions remain lane-owned: on a 2004 frame each one cycles its
     * filter through On/Friends/Off, and replacing that action with a
     * show/hide switch would remove three working states.
     * The sheet gets its own switch instead -- @see g_frame.toggle_x -- which
     * is a button this frame added rather than one it took over.
     */
    for( int i = 0; i < MOBILE_CHAT_BUTTON_COUNT; i++ )
    {
        static char const* const NAME[MOBILE_CHAT_BUTTON_COUNT] = {
            "frame.chat.button.public",
            "frame.chat.button.private",
            "frame.chat.button.trade",
            "frame.chat.button.report",
        };
        struct ToriRS_Rect const bounds = {
            area.x + mobile_chat_button_x(i, chat_w),
            strip_y + MOBILE_CHAT_BUTTON_LIFT,
            MOBILE_CHAT_BUTTON_W,
            MOBILE_CHAT_BUTTON_H,
        };

        mobile_member(
            ctx, FRAME_SURFACE_CHAT_BUTTONS, i, bounds.x, bounds.y, bounds.width, bounds.height);

        /* Publish the plate as a retained named node. The UI registry chooses
         * one provider per facet and the host paints only the resolved winner,
         * so a report-button contribution composes without double drawing.
         * One picture serves all four buttons; this strip has no hover art. */
        mobile_ui_node(ctx, NAME[i], bounds, mobile_art(ctx, ART_CHAT_BUTTON));
    }
}

/* ------------------------------------------------------ describing the plan */

/*
 * Everything below is the DESCRIPTION: the plan is stated to Porcelain by key
 * and the layer makes the tree match. Nothing here creates, moves or removes a
 * widget, keeps a handle, counts a member or asks whether a picture changed --
 * the reconcile does all of it, and the diff between one run's description and
 * the last is the only thing that undoes a move.
 *
 * What that deleted from this file:
 *
 *   - `struct MobileOwned` times eighty-eight -- a ref plus a live flag for
 *     every piece, cell, stone, icon, switch, glyph, plate and sheet -- and the
 *     `mobile_owned_drop` that had to be called on exactly the ones a smaller
 *     plan no longer wanted.
 *   - The anchor CHAIN: `last` threaded through four functions so each child
 *     could be anchored over the one before and the surfaces over the last of
 *     all. The layer creates a plugin's children in description order, and
 *     `raise` says "above everything I own" in one verb.
 *   - `lit_shown` / `icon_shown`, the two arrays that existed so a per-frame
 *     refresh wrote only what moved. An unchanged item's property hash matches
 *     and costs no engine call at all.
 *   - `mobile_reset_surfaces`, which reset all eight roles and up to sixteen
 *     members each before every plan, because the setters only ever added.
 *   - `mobile_clear` and `chat_dressed`: a whole second code path that undid
 *     the OldSchool pack's dressing, reached on a provider switch, which could
 *     and did leave dressing behind when it was not.
 *   - The thirty-two-hop parent walk for the clipping root.
 */

/*
 * The portable element for one of this frame's surfaces, and for a numbered
 * member of one.
 *
 * Six of the eight surfaces are first-class elements. The two that are not are
 * exactly the two whose MEMBERS this frame places, and there the vocabulary
 * runs out: `struct PorcelainElement` numbers members for three families --
 * ORB, CHAT_FILTER and LANE_CHROME -- and the sidebar's fourteen mounts and the
 * orb block's three children are in none of them. So those go through
 * PORCELAIN_EL_ROLE and the engine's own `<slot>:<member>` spelling, which
 * resolves to exactly the node `find_all` used to answer with -- and is also
 * precisely the hand-spelled role the element table exists to delete. @see the
 * port report's verb list.
 */
static struct PorcelainElement const FRAME_SURFACE_ELEMENT[FRAME_SURFACE_COUNT] = {
    { PORCELAIN_EL_VIEWPORT, 0, NULL },
    { PORCELAIN_EL_MINIMAP,  0, NULL },
    { PORCELAIN_EL_COMPASS,  0, NULL },
    { PORCELAIN_EL_CHAT,     0, NULL },
    { PORCELAIN_EL_CHAT_BAR, 0, NULL },
    { PORCELAIN_EL_SIDEBAR,  0, NULL },
    { PORCELAIN_EL_MODAL,    0, NULL },
    { PORCELAIN_EL_ORBS,     0, NULL },
};

/**
 * Member `m` of surface `s`, as an element.
 *
 * CHAT_FILTER is the one family the vocabulary already numbers, and it is
 * preferred over the spelled role for the reason the layer prefers it: the
 * element carries both of the lane's two spellings and picks whichever binds.
 * The rest are spelled, into a buffer the plugin owns, because the element a
 * description holds must outlive the describe that stated it.
 */
static struct PorcelainElement
mobile_member_element(
    struct MobileCall* ctx,
    int surface,
    int member)
{
    assert(ctx);
    assert(surface >= 0 && surface < FRAME_SURFACE_COUNT);
    assert(member >= 0 && member < FRAME_MEMBER_MAX);
    if( surface == FRAME_SURFACE_CHAT_BUTTONS )
        return PORCELAIN_CHAT_FILTER_EL(member);
    return PORCELAIN_ROLE_EL(ctx->state->member_role[surface][member]);
}

/*
 * The natural size of a picture this frame is about to describe.
 *
 * Asked here rather than left to the layer, and that is a behaviour decision
 * and not an optimisation: `PorcelainItem::w` of zero means "the picture's own
 * size", and a picture still crossing the IO queue has no size -- so a
 * zero-by-zero control would exist in the tree until the bytes landed. A piece
 * with no size yet is simply not described, and the describe runs again when
 * the asset lands.
 */
static bool
mobile_art_size(
    struct MobileCall* ctx,
    struct MobileArt art,
    int* out_w,
    int* out_h)
{
    assert(ctx);
    assert(out_w);
    assert(out_h);
    *out_w = 0;
    *out_h = 0;
    if( !art.name )
        return false;
    return Porcelain_ImageSize(ctx->state->porcelain, art.name, out_w, out_h) && *out_w > 0 &&
           *out_h > 0;
}

/**
 * One owned picture of this frame's chrome, at a canvas coordinate.
 *
 * The canvas placement parents to the clipping root, which is the only parent
 * that will hold a floating piece beside the scene rather than clipped inside
 * it. The depth target is what keeps it UNDER the lane's own surfaces: a child
 * of the root is otherwise after every subtree the lane mounted in it.
 *
 * Every piece sits over the SCENE and not over the piece before it. The old
 * apply pass chained them, and the layer cannot state that chain at all,
 * because a depth target is an ELEMENT and an owned control is not in the
 * vocabulary. It does not need to: they are all children of one parent and the
 * layer creates them in description order, which is the same relative order the
 * chain produced.
 */
static void
mobile_describe_piece_trans(
    struct ToriRS_PorcelainDescribe* describe,
    char const* key,
    struct MobileArt art,
    int x,
    int y,
    int w,
    int h,
    int trans,
    struct PorcelainElement depth)
{
    struct PorcelainItem item;

    assert(describe);
    assert(key);
    memset(&item, 0, sizeof(item));
    item.key = key;
    item.image = art.name;
    item.w = w;
    item.h = h;
    item.place.kind = PORCELAIN_AT_CANVAS;
    item.place.dx = x;
    item.place.dy = y;
    item.place.depth = depth;
    /*
     * `trans` is the client's own sense: 0 opaque, 255 invisible. Porcelain's
     * is the other way up AND reserves zero for "unstated, therefore opaque",
     * so the transparent end is named rather than numbered -- written as the
     * subtraction alone, the one piece that uses it (the tutorial blink, @see
     * the icon in mobile_describe_chrome) would never go out.
     *
     * The opaque end is stated as 255 and not left unset for the mirror-image
     * reason: an opacity that FALLS to the unset value is a fade the layer
     * records a finding against, and a blink comes back every ten cycles.
     */
    item.opacity = trans >= 255 ? PORCELAIN_OPACITY_INVISIBLE : 255 - trans;
    describe->piece(describe, &item);
}

/** The same picture, opaque -- which is every piece of this frame but one. */
static void
mobile_describe_piece(
    struct ToriRS_PorcelainDescribe* describe,
    char const* key,
    struct MobileArt art,
    int x,
    int y,
    int w,
    int h,
    struct PorcelainElement depth)
{
    mobile_describe_piece_trans(describe, key, art, x, y, w, h, 0, depth);
}

/*
 * A rock was tapped. A rock the server has not put a panel behind cannot be
 * tapped at all now -- the control is described inert -- but the check stays,
 * because the state it reads is a poll of the lane and the op could be
 * dispatched from a press queued against an earlier description. The tab you
 * are already looking at shuts the drawer; any other opens it on that panel.
 * One stone doing both is what makes the rail a drawer.
 */
static void
mobile_tab_pressed(
    struct ToriRS_Api* api,
    void* user,
    char const* key)
{
    struct MobileTabHandle const* handle = user;
    struct MobileState* state;

    assert(api);
    assert(handle);
    assert(key);
    (void)key;
    state = handle->state;
    if( !api->cache.tab_enabled(api, handle->tabno) )
        return;
    if( state->drawer_open && api->cache.tab_active(api) == handle->tabno )
        state->drawer_open = false;
    else
    {
        state->drawer_open = true;
        (void)api->cache.tab_select(api, handle->tabno);
    }
    /*
     * The HOST's invalidate and not the layer's.
     *
     * Porcelain_Invalidate re-runs the description, which is enough to move the
     * pixels -- but the drawer opening changes the frame the host believes this
     * plugin is providing, and the host learns that only through
     * frame.invalidate, which re-asks on_gameframe with the canvas.
     */
    api->frame.invalidate(api);
}

/* Tap blockers: the chat sheet asks for the keyboard, the rest swallow. */
static void
mobile_blocker_pressed(
    struct ToriRS_Api* api,
    void* user,
    char const* key)
{
    char const* op = user;

    assert(api);
    assert(key);
    (void)key;
    if( op && strcmp(op, "Type") == 0 )
        api->input.chat_focus(api, true);
}

static void
mobile_chat_toggle_pressed(
    struct ToriRS_Api* api,
    void* user,
    char const* key)
{
    struct MobileState* state = user;

    assert(api);
    assert(state);
    assert(key);
    (void)key;
    state->chat_open = !state->chat_open;
    /* Putting the sheet away takes the keyboard with it: there is nothing left
     * on screen to type into. Both sources are dropped, because either can be
     * holding it up -- the plugin's own latch and the chat line's focus. */
    if( !state->chat_open )
    {
        if( state->keyboard_on )
        {
            state->keyboard_on = false;
            api->input.text_input(api, false);
        }
        api->input.chat_focus(api, false);
    }
    api->frame.invalidate(api);
}

static void
mobile_keyboard_toggle_pressed(
    struct ToriRS_Api* api,
    void* user,
    char const* key)
{
    struct MobileState* state = user;

    assert(api);
    assert(state);
    assert(key);
    (void)key;
    state->keyboard_on = !state->keyboard_on;
    api->input.text_input(api, state->keyboard_on);
    /* Switching OFF also drops the chat line's focus, or the focus alone keeps
     * the keyboard up and the switch does nothing visible. */
    if( !state->keyboard_on )
        api->input.chat_focus(api, false);
}

/*
 * The plugins switch: open the client's plugin window, or put it away.
 *
 * No state of its own. The chat and keyboard switches latch in MobileState
 * because the thing they operate is this frame's; the plugin window is the
 * CLIENT's, and a copy of its state here would be a second answer that goes
 * stale the moment anything else opens or closes it -- the minimenu's "Manage
 * Plugins" row, a rail press on a lane that has one, or the window closing
 * itself. So it is read back every pass, which is also what the label reads.
 */
static void
mobile_plugins_toggle_pressed(
    struct ToriRS_Api* api,
    void* user,
    char const* key)
{
    struct MobileState* state = user;

    assert(api);
    assert(state);
    assert(key);
    (void)key;
    (void)state;
    assert(api->client);
    assert(api->client->plugin_window_open);
    assert(api->client->plugin_window_show);
    api->client->plugin_window_show(api, !api->client->plugin_window_open(api));
}

/** A switch and its glyph: an owned control wearing the plate, with the picture
 *  centred on it. @see mobile_describe_switches for why the plate is described
 *  even when its art has not landed. */
static void
mobile_describe_toggle(
    struct MobileCall* ctx,
    struct ToriRS_PorcelainDescribe* describe,
    struct MobileToggle const* toggle,
    char const* key,
    char const* glyph_key,
    char const* label,
    PorcelainOpFn on_op)
{
    struct MobileState* state = ctx->state;
    struct PorcelainItem item;
    struct MobileArt face = toggle->face;
    int w = 0;
    int h = 0;

    assert(ctx);
    assert(describe);
    assert(toggle);
    if( !toggle->placed )
        return;
    /*
     * A switch with no art is not a missing switch.
     *
     * The apply pass this replaces returned without describing either child
     * when image_size on the plate failed -- so a frame whose switch art had
     * not landed, or had failed outright, had no way to bring the chat back:
     * the chat became undismissable in the off state. The plate falls back to
     * the blank at the box the layout stated, which is drawn as nothing and
     * still carries the operation. That is the ledger row, fixed.
     */
    if( !mobile_art_size(ctx, face, &w, &h) )
    {
        face = mobile_blank(ctx);
        w = toggle->box.width;
        h = toggle->box.height;
    }
    memset(&item, 0, sizeof(item));
    item.key = key;
    item.image = face.name;
    item.w = w;
    item.h = h;
    item.place.kind = PORCELAIN_AT_CANVAS;
    item.place.dx = toggle->box.x;
    item.place.dy = toggle->box.y;
    item.place.depth = PORCELAIN_EL(VIEWPORT);
    item.op_label = label;
    item.on_op = on_op;
    item.user = state;
    item.hit = true;
    item.enabled = true;
    describe->control(describe, &item);

    if( mobile_art_size(ctx, toggle->glyph, &w, &h) )
        mobile_describe_piece(
            describe,
            glyph_key,
            toggle->glyph,
            toggle->box.x + (toggle->box.width - w) / 2,
            toggle->box.y + (toggle->box.height - h) / 2,
            w,
            h,
            PORCELAIN_EL(VIEWPORT));
}

/*
 * The chrome: the plates, the drawer, the sheet, the two switch plates, the tap
 * blockers, the housing, and the fourteen rock cells with their stones and
 * icons.
 *
 * One key per thing, stable across passes and across art families, so a plan
 * with fewer pieces than the last drops exactly the surplus and moves nothing
 * else.
 */
static void
mobile_describe_chrome(
    struct MobileCall* ctx,
    struct ToriRS_PorcelainDescribe* describe)
{
    struct MobileState* state = ctx->state;
    int const active = state->tab_active_shown;

    assert(ctx);
    assert(describe);
    for( int i = 0; i < g_frame.blit_count; i++ )
    {
        struct MobileBlit const* b = &g_frame.blit[i];
        int w = b->w;
        int h = b->h;

        if( b->op )
        {
            /*
             * A tap blocker, as a CONTROL wearing the 1x1 blank rather than a
             * Porcelain_Blocker.
             *
             * The verb exists and states `.image = NULL`, which is documented
             * as an invisible hit box -- and an owned image control takes its
             * SIZE through set_image, which the engine refuses for a zero
             * picture, so the size is never written and the blocker sits at
             * 0x0. Both frame providers ship a one-pixel transparent PNG for
             * exactly this. @see the port report's verb list.
             */
            struct PorcelainItem item;

            memset(&item, 0, sizeof(item));
            item.key = b->key;
            item.image = mobile_blank(ctx).name;
            item.w = w;
            item.h = h;
            item.place.kind = PORCELAIN_AT_CANVAS;
            item.place.dx = b->x;
            item.place.dy = b->y;
            item.place.depth =
                b->behind.kind == PORCELAIN_EL_NONE ? PORCELAIN_EL(VIEWPORT) : b->behind;
            item.place.behind = b->behind.kind != PORCELAIN_EL_NONE;
            item.visible_with = b->visible_with;
            item.op_label = b->op;
            item.on_op = mobile_blocker_pressed;
            item.user = (void*)(uintptr_t)b->op;
            item.hit = true;
            item.enabled = true;
            describe->control(describe, &item);
            continue;
        }
        if( !mobile_art_size(ctx, b->image, &w, &h) )
            continue;
        if( b->behind.kind != PORCELAIN_EL_NONE )
        {
            struct PorcelainItem item;

            memset(&item, 0, sizeof(item));
            item.key = b->key;
            item.image = b->image.name;
            item.w = w;
            item.h = h;
            item.place.kind = PORCELAIN_AT_CANVAS;
            item.place.dx = b->x;
            item.place.dy = b->y;
            item.place.depth = b->behind;
            item.place.behind = true;
            item.visible_with = b->visible_with;
            describe->piece(describe, &item);
            continue;
        }
        mobile_describe_piece(describe, b->key, b->image, b->x, b->y, w, h, PORCELAIN_EL(VIEWPORT));
    }

    if( g_frame.housing_placed )
    {
        int w = 0;
        int h = 0;
        struct PorcelainElementState compass;
        /*
         * Over the COMPASS where the compass PAINTS, and over the minimap
         * where it does not.
         *
         * The apply pass asked `find(compass)` and fell back to `find(minimap)`
         * -- resolution, not painting -- and the layer checks that a stated
         * depth target is PRESENTED before anchoring to it, falling back to the
         * placement's own element and recording a finding when it is not. The
         * three minimap states that suppress the compass are what that
         * distinction is about: a housing anchored to a node that emits nothing
         * keeps its own native draw index, the end of the tree, and paints over
         * the whole orb column.
         */
        if( mobile_art_size(ctx, g_frame.housing_image, &w, &h) )
            mobile_describe_piece(
                describe,
                "housing",
                g_frame.housing_image,
                g_frame.housing_rect.x,
                g_frame.housing_rect.y,
                w,
                h,
                Porcelain_Element(state->porcelain, PORCELAIN_EL(COMPASS), &compass) &&
                        compass.presented
                    ? PORCELAIN_EL(COMPASS)
                    : PORCELAIN_EL(MINIMAP));
    }

    /*
     * By TABNO and not by plan index, for the reason MobileBlit::key states.
     *
     * The plan holds only the tabs this cache HAS, so a lane that mounts its
     * sidebar after login -- or one whose seventh tab is absent, which every
     * 2004 cache's is -- renumbers every cell after the gap. Keyed by position
     * the stones would swap identities under the reconciler; keyed by the tab
     * they open, they cannot. @see mobile_tab_present.
     */
    for( int i = 0; i < g_frame.tab_count; i++ )
    {
        struct MobileTab const* t = &g_frame.tab[i];
        bool const given = (state->tab_given_shown >> t->tabno) & 1u;
        bool const lit = given && g_drawer_open && t->tabno == active;
        struct MobileArt face;
        struct PorcelainItem item;
        int w = 0;
        int h = 0;
        int fw = 0;
        int fh = 0;

        /*
         * The CELL is the rock's box wearing the blank: the hit area and the
         * Select operation, and nothing to stretch.
         *
         * A tab the SERVER has not handed over is drawn, INERT and carries no
         * menu row. The handler this replaces checked tab_enabled and returned,
         * which is a refusal inside the callback rather than a disarmed
         * control -- so the rock still offered "Select" and a tap on it did
         * nothing. That is the ledger's F14, and `enabled` is the verb the row
         * names for it.
         */
        memset(&item, 0, sizeof(item));
        item.key = state->cell_key[t->tabno];
        item.image = mobile_blank(ctx).name;
        item.w = t->w;
        item.h = t->h;
        item.place.kind = PORCELAIN_AT_CANVAS;
        item.place.dx = t->x;
        item.place.dy = t->y;
        item.place.depth = PORCELAIN_EL(VIEWPORT);
        item.op_label = "Select";
        item.on_op = mobile_tab_pressed;
        state->tab_handle[t->tabno] = (struct MobileTabHandle){ state, t->tabno };
        item.user = &state->tab_handle[t->tabno];
        item.hit = true;
        item.enabled = given;
        describe->control(describe, &item);

        /*
         * The STONE the cell wears: the redstone on the open tab and nothing on
         * every other, at the art's NATURAL size centred on the rock.
         *
         * Described whether or not it has a picture FOR THIS STATE, and that is
         * the faithful shape rather than the tidy one: the provider this
         * replaces created all fourteen and hid thirteen of them, and a key
         * that is described or is not would be a behaviour change with no row
         * behind it. The blank stands in for the hide, which is the same
         * picture -- nothing -- in the same box.
         *
         * The BOX is the LIT art's, because that is the size the control was
         * made at before anything was lit. On the OldSchool rail the idle stone
         * is the same 40x40 box, so the two agree there too.
         */
        face = lit ? t->lit : t->idle;
        /*
         * The BOX is asked of the HOST and not of the layer.
         *
         * Porcelain_ImageSize requests the picture, which takes one of the
         * handle's forty-eight image slots -- and a stone that is not the open
         * one is a name nothing describes. Asking through the layer for a box
         * alone would spend fourteen slots on pictures nobody is showing, and
         * the frame needs those slots for the icons and the parchment.
         */
        if( mobile_own_size(ctx, t->lit, &w, &h) )
        {
            if( !mobile_own_size(ctx, face, &fw, &fh) )
                face = mobile_blank(ctx);
            mobile_describe_piece(
                describe,
                state->lit_key[t->tabno],
                face,
                t->x + (t->w - w) / 2,
                t->y + (t->h - h) / 2,
                w,
                h,
                PORCELAIN_EL(VIEWPORT));
        }

        /*
         * And its ICON, centred the same way, on a tab the server has given
         * out. That used to be an array of last-written state and a refresh
         * pass; it is a key that is described or is not.
         *
         * The tutorial's BLINK is the same icon going out, and it is stated as
         * transparency rather than as a key that stops being described --
         * which the `given` line above could have been mistaken for. The clock
         * is what separates them: a tab is handed over once, so a create is
         * fair; a flash flips every ten cycles, and a control created and
         * destroyed twice a second would be remade at the END of this
         * plugin's draw order on every relight. @see MobileState::
         * tab_flash_dark_shown.
         */
        if( given && mobile_art_size(ctx, t->icon, &w, &h) )
            mobile_describe_piece_trans(
                describe,
                state->icon_key[t->tabno],
                t->icon,
                t->x + (t->w - w) / 2,
                t->y + (t->h - h) / 2,
                w,
                h,
                t->tabno == state->tab_flash_dark_shown ? 255 : 0,
                PORCELAIN_EL(VIEWPORT));
    }

    mobile_describe_toggle(
        ctx,
        describe,
        &g_frame.chat_toggle,
        "chat-toggle",
        "chat-glyph",
        g_chat_open ? "Hide chat" : "Show chat",
        mobile_chat_toggle_pressed);
    mobile_describe_toggle(
        ctx,
        describe,
        &g_frame.keyboard_toggle,
        "keyboard-toggle",
        "keyboard-glyph",
        "Keyboard",
        mobile_keyboard_toggle_pressed);
    mobile_describe_toggle(
        ctx,
        describe,
        &g_frame.plugins_toggle,
        "plugins-toggle",
        "plugins-glyph",
        mobile_has_plugin_window(ctx) && ctx->api->client->plugin_window_open(ctx->api)
            ? "Hide plugins"
            : "Plugins",
        mobile_plugins_toggle_pressed);
}

/*
 * The live surfaces, moved to the plan's rectangles.
 *
 * A role the plan did not place is hidden, the way an undeclared slot was: a
 * frame with no sidebar in it is how the drawer shuts, and a frame with no chat
 * in it is how the switch puts the sheet away. A role the lane does not have is
 * left alone, which is now the layer's answer rather than a `find` that failed.
 *
 * Porcelain_Move takes a PARENT-LOCAL box and the plan is in canvas
 * coordinates, so the translation is the element's own two boxes subtracted --
 * `box` is where the node draws and `local` where its parent thinks it is, and
 * the difference is the parent's origin. That is the same arithmetic
 * mobile_parent_origin did with a `parent` call and a `bounds` call per surface
 * per pass; here both numbers are already in the state the watch stamped.
 */
static void
mobile_describe_surfaces(
    struct MobileCall* ctx,
    struct ToriRS_PorcelainDescribe* describe)
{
    struct MobileState* state = ctx->state;

    assert(ctx);
    assert(describe);
    for( int s = 0; s < FRAME_SURFACE_COUNT; s++ )
    {
        struct PorcelainElementState native;
        bool has_members = false;
        bool surface_bound;
        /*
         * How far this surface is about to move, carried down to its members.
         *
         * A member's box is stated PARENT-LOCAL and the plan is in canvas
         * coordinates, so the conversion needs the parent's origin -- and the
         * only origin a watch carries is the one the tree has NOW, before the
         * container move this same description states has been applied. Every
         * member of a surface that moves therefore lands at the plan's box
         * plus the container's own displacement, and stays there until a later
         * describe reads the settled tree and corrects it.
         *
         * On this frame that is every time the drawer opens. Measured on
         * osrs239 at 765x503: the sidebar container went from the lane's
         * (472,208) to the rail's (481,238), the open panel was written
         * local (9,30) against the pre-move origin, and for two frames the
         * inventory's item icons painted at (490,268) -- off the drawer, over
         * the world -- before the third describe put them right. That is
         * the "the items appear somewhere else and then jump into the panel"
         * the drawer has always done.
         *
         * The displacement is known right here, so the members are converted
         * against the origin the container WILL have. Zero where the plan does
         * not move the surface.
         *
         * And carried only to a member that is INSIDE the surface. On rs289lc
         * SIDEBAR binds to the node at the 2004 inventory box, but the
         * fourteen mounts are seated BESIDE it under `fixed_shell`, so their
         * parent does not move at all. Carrying the region's (-72,+33) into
         * them cancelled their own move exactly: the tab the player opened
         * painted at the lane's (553,205) for two frames and then jumped into
         * the drawer. @see Porcelain_Inside.
         */
        int surface_dx = 0;
        int surface_dy = 0;

        for( int m = 0; m < FRAME_MEMBER_MAX; m++ )
            if( g_frame.member[s][m].placed )
                has_members = true;

        /* The chat FILTERS hang off the chat, not off the bar: a 2004 lane has
         * filters and no bar at all, so gating their placement on the bar is
         * gating it on the wrong container. */
        surface_bound = Porcelain_Element(
            state->porcelain,
            s == FRAME_SURFACE_CHAT_BUTTONS ? PORCELAIN_EL(CHAT) : FRAME_SURFACE_ELEMENT[s],
            &native);
        /* The chat buttons are placed only as members: the strip as a whole is
         * the frame's own art and the buttons on it are the player's. */
        if( s != FRAME_SURFACE_CHAT_BUTTONS && surface_bound )
        {
            /*
             * The sidebar's container is asked about whether or not the drawer
             * is open, so its watch is bound before the fence that needs it.
             * Asked for the first time on the fence the drawer OPENS, it was
             * pending there, that pass was held, and the pass after it
             * re-applied every edit as new. @see MOBILE_SIDEBAR_CONTAINER.
             */
            struct PorcelainElementState container;
            bool const container_bound =
                s == FRAME_SURFACE_SIDEBAR &&
                Porcelain_Element(state->porcelain, MOBILE_SIDEBAR_CONTAINER, &container);

            if( g_frame.surface[s].placed )
            {
                struct ToriRS_WidgetBounds box;
                box.x = g_frame.surface[s].rect.x - (native.box.x - native.local.x);
                box.y = g_frame.surface[s].rect.y - (native.box.y - native.local.y);
                box.width = g_frame.surface[s].rect.width;
                box.height = g_frame.surface[s].rect.height;
                /*
                 * The open panel takes the layer it stands in along with it.
                 *
                 * On 164 that layer is noClickThrough and sits after the rail
                 * in the input order, so left at the lane's (796,389) it ate
                 * every click on the rocks under it the moment a tab opened:
                 * the rail drew, lit on hover and did nothing. Moved onto the
                 * panel box it blocks only where the panel's own blocker
                 * already does.
                 *
                 * The sidebar is then at (0,0) inside it, which holds whatever
                 * origin the container has now: there is no displacement to
                 * carry, unlike the members below. @see surface_dx.
                 */
                if( container_bound )
                {
                    struct ToriRS_WidgetBounds held = box;
                    held.x = g_frame.surface[s].rect.x - (container.box.x - container.local.x);
                    held.y = g_frame.surface[s].rect.y - (container.box.y - container.local.y);
                    describe->move(describe, MOBILE_SIDEBAR_CONTAINER, held, 0);
                    box.x = 0;
                    box.y = 0;
                }
                describe->move(describe, FRAME_SURFACE_ELEMENT[s], box, 0);
                surface_dx = g_frame.surface[s].rect.x - native.box.x;
                surface_dy = g_frame.surface[s].rect.y - native.box.y;
                /*
                 * And OVER this frame's own chrome.
                 *
                 * The scene is the WHOLE canvas on this frame, so every piece
                 * of chrome is drawn at canvas coordinates under the clipping
                 * root, after the lane subtree the map, the chat, the panels
                 * and the modal all live in. Without this the rail paints over
                 * every one of them.
                 *
                 * The viewport is the exception and it is the same exception
                 * the apply pass made: it is what the chrome is drawn ON TOP
                 * OF, and raising it would put the world over the frame.
                 */
                if( s != FRAME_SURFACE_VIEWPORT )
                    describe->raise(describe, FRAME_SURFACE_ELEMENT[s], PORCELAIN_EL(NONE), false);
            }
            else if( !has_members )
                describe->hide(describe, FRAME_SURFACE_ELEMENT[s]);
        }
        /*
         * The lane's filter CAPTIONS go away with the sheet they stand on.
         *
         * Every other surface here follows the rule that a role the plan did
         * not place is hidden. The filters were exempted from it because they
         * are placed as members and their container is the chat, which the
         * plan does place -- and that is true only where the filters are
         * CHILDREN of the chat. On a 2004 frame they are not: the four
         * captions are siblings of the chat in the frame's own layer, so
         * hiding the chat leaves them behind. Every time this frame put the
         * sheet away -- which is what opening the drawer does -- "Public chat
         * / On" and three more went on painting on bare world, with the plates
         * gone from under them, because a plate is described only where the
         * plan placed one.
         *
         * Asked of the LANE's count rather than of this frame's four: a pack
         * with eight answers eight, and a lane with no filters at all is not
         * asked about one it does not have. @see Porcelain_Count.
         */
        if( s == FRAME_SURFACE_CHAT_BUTTONS && !g_frame.chat_placed )
        {
            int const filters = Porcelain_Count(state->porcelain, PORCELAIN_EL_CHAT_FILTER);

            for( int i = 0; i < filters; i++ )
                describe->hide(describe, PORCELAIN_CHAT_FILTER_EL(i));
            continue;
        }
        /*
         * A SHUT drawer puts every mount away BY NAME.
         *
         * Every other surface here rests on the rule the branch above states:
         * the plan did not place the role, so the role is hidden and its whole
         * subtree goes with it. That rule needs the container to be a declared
         * role AND an ancestor of the mounts, and on a 2004 dat1 lane it is
         * neither. `rs245_2lc_dat1_ui.ini` -- which rs289lc shares -- declares
         * only `panel_<name> = slot(sidebar, <n>)`, the fourteen MOUNTS, and
         * seats each of them directly under `fixed_shell` at the 2004
         * inventory box. There is no `sidebar` role for `surface_bound` to
         * find, and the one node standing at that box, `sidebar_region`, is
         * the invback plate BESIDE the mounts rather than over them: hiding it
         * would take the backing away and leave the panel painting.
         *
         * So the shut drawer says it to each mount, and says it before the
         * container gate below, which a shut drawer can never pass -- nothing
         * is placed, so `has_members` is false.
         *
         * What this looked like on rs289lc: with the drawer shut the
         * inventory's item icons went on painting over the world at (553,205),
         * and the grid under them ate taps meant for the rail.
         *
         * Free of the watch budget the member loop below is careful about: the
         * rail asked about all fourteen of these this same layout, to decide
         * which rocks to cut. @see mobile_tab_present.
         */
        if( s == FRAME_SURFACE_SIDEBAR && !g_drawer_open )
            for( int tab = 0; tab < MOBILE_TAB_COUNT; tab++ )
                if( g_tab_present[tab] )
                    describe->hide(
                        describe, mobile_member_element(ctx, FRAME_SURFACE_SIDEBAR, tab));
        /*
         * The CONTAINER before its members -- where there IS a container.
         *
         * A member is a child of the surface, and the server mounts the surface
         * first: asking about `orbs:1` before interface 160 exists gets it
         * called ABSENT, and an absence recorded for something that binds four
         * frames later is a finding nobody can act on. One ask answers for all
         * of them, and it is an ask this loop already made.
         *
         * The SIDEBAR is the one surface that reasoning is not about. Its
         * fourteen mounts are not children of its container on a 2004 lane --
         * `rs245_2lc_dat1_ui.ini` seats them under `fixed_shell` and declares
         * no `[role:sidebar]` at all -- so `surface_bound` there is not "the
         * mounts have not arrived yet", it is permanently false, and the gate
         * skipped the whole member loop on every frame of every lane rs289lc
         * shares that layout with. What that looked like was the drawer
         * OPENING on nothing: the tab the player picked went on painting at
         * the lane's own 2004 box, (553,205), instead of moving to the rail.
         *
         * The prerequisite the container was standing in for, this surface
         * already has per member and better: `mobile_tab_present` asks each
         * mount whether it is ABSENT, this same layout, and the member loop
         * below drops any that does not resolve. So there is nothing here for
         * a container to answer earlier, and no watch spent finding it out.
         */
        if( !has_members || (!surface_bound && s != FRAME_SURFACE_SIDEBAR) )
            continue;
        for( int m = 0; m < FRAME_MEMBER_MAX; m++ )
        {
            struct MobileRect const* at = &g_frame.member[s][m];
            struct PorcelainElement element;
            struct PorcelainElementState member;

            /*
             * Only a member this frame has something to SAY about.
             *
             * Asking is what registers a watch, and there are eight surfaces
             * times sixteen members against a watch table of forty-eight, so
             * asking about all of them is not a wasted call -- it is a budget
             * overrun that costs the frame the watches it actually needs.
             *
             * The orb block is the one place an UNPLACED member is still
             * described, because the plan seats only the adviser and the other
             * two have to be told to stay where the block put them.
             */
            if( !at->placed && !(s == FRAME_SURFACE_ORBS && m <= FRAME_ORBS_MEMBER_WIKI) )
                continue;
            element = mobile_member_element(ctx, s, m);
            if( !Porcelain_Element(state->porcelain, element, &member) )
                continue;
            if( at->placed )
            {
                struct ToriRS_WidgetBounds box;
                /* The parent's origin AFTER the container move stated
                 * above, not the one the tree still has -- where the
                 * container IS an ancestor. @see surface_dx. */
                bool const carried = (surface_dx || surface_dy) &&
                                     Porcelain_Inside(state->porcelain, &member, &native);
                box.x = at->rect.x - (member.box.x + (carried ? surface_dx : 0) - member.local.x);
                box.y = at->rect.y - (member.box.y + (carried ? surface_dy : 0) - member.local.y);
                box.width = at->rect.width;
                box.height = at->rect.height;
                /* No raise here: a member is a CHILD of the surface, and the
                 * surface was raised above the chrome a few lines up, which
                 * carries its whole subtree with it. */
                describe->move(describe, element, box, 0);
            }
            else if( s == FRAME_SURFACE_ORBS )
            {
                /*
                 * The globe and the wiki banner KEEP their place in the block.
                 *
                 * They used to fall into an "unplaced ORBS member" branch that
                 * hid them, so a frame that moved the block as one box lost two
                 * of its children -- and 601 shows both. Moving the block moves
                 * its members with it, which is what KEEP_RELATIVE states: the
                 * member's own offset inside the block, said out loud rather
                 * than fallen through. That is the ledger's F10.
                 */
                struct ToriRS_WidgetBounds const keep = {
                    PORCELAIN_KEEP_RELATIVE, PORCELAIN_KEEP_RELATIVE, 0, 0
                };
                describe->move(describe, element, keep, 0);
            }
        }
        /*
         * The 2004 plates under the four filter captions: owned images directly
         * BEHIND the lane's buttons, so the caption sits on a plate and nothing
         * lies behind the row.
         *
         * `visible_with` is the filter itself, because OVER and BEHIND inherit
         * nothing: a plate behind a button the lane has put away would go on
         * painting on its own.
         */
        if( s != FRAME_SURFACE_CHAT_BUTTONS )
            continue;
        for( int i = 0; i < MOBILE_CHAT_BUTTON_COUNT; i++ )
        {
            struct MobileRect const* at = &g_frame.plate[i];
            struct PorcelainElement const filter = PORCELAIN_CHAT_FILTER_EL(i);
            struct PorcelainElementState plate;
            struct PorcelainItem item;
            int w = 0;
            int h = 0;

            if( !at->placed || !Porcelain_Element(state->porcelain, filter, &plate) )
                continue;
            if( !mobile_art_size(ctx, g_frame.plate_art[i], &w, &h) )
                continue;
            memset(&item, 0, sizeof(item));
            item.key = state->plate_key[i];
            item.image = g_frame.plate_art[i].name;
            item.w = w;
            item.h = h;
            item.place.kind = PORCELAIN_AT_CANVAS;
            item.place.dx = at->rect.x;
            item.place.dy = at->rect.y;
            item.place.depth = filter;
            item.place.behind = true;
            item.visible_with = filter;
            describe->piece(describe, &item);
        }
    }
}

/*
 * The chat pack's filter bar, moved to the bottom of the block.
 *
 * ONE move of ONE layer. The pack is not re-parented, nothing of it is hidden
 * and no child of it is created: `controls` keeps its seven captions, their
 * mode lines, their plates and every action inside them, and this states the
 * row it stands on. The plan already worked out which row -- @see the
 * OldSchool branch of mobile_layout, which also moved the pack up by the band
 * so the two together leave the block on exactly the rows it occupied before.
 *
 * Parent-local, like every other move here, and the parent is the pack ROOT --
 * which this same description is moving. So the origin read back here is the
 * one from the last fence and the first application lands the bar `band` rows
 * high; the move changes an element, the layer notes it, the description runs
 * again and the second lands it. Two fences and then still, which is the same
 * convergence a member of a moved surface already relies on.
 */
static void
mobile_describe_chat_bar(
    struct MobileCall* ctx,
    struct ToriRS_PorcelainDescribe* describe)
{
    struct PorcelainElementState bar;
    struct ToriRS_WidgetBounds box;

    assert(ctx);
    assert(describe);
    if( !g_frame.chat_bar.placed )
        return;
    if( !Porcelain_Element(ctx->state->porcelain, MOBILE_CHAT_CONTROLS, &bar) )
        return;
    box.x = g_frame.chat_bar.rect.x - (bar.box.x - bar.local.x);
    box.y = g_frame.chat_bar.rect.y - (bar.box.y - bar.local.y);
    box.width = g_frame.chat_bar.rect.width;
    box.height = g_frame.chat_bar.rect.height;
    describe->move(describe, MOBILE_CHAT_CONTROLS, box, 0);
}

/*
 * The XP counter and the XP drops, moved to the orb column this frame drew.
 *
 * ONE move of ONE slot, and a move with NO SIZE in it -- which is the whole of
 * why this is three lines rather than a second layout. Interface 122 anchors
 * the counter 2 columns in from this slot's right edge and each of its seven
 * drop columns 3, and anchors nothing to its left edge or its bottom; the only
 * thing the slot's size decides is how far a drop falls, which is the cache's
 * number and not a frame's. So the plan pins the top-right CORNER and the
 * width is read back from the lane, unchanged: `x` is the corner less the box
 * the toplevel gave it. A width or height in the bounds would make Porcelain
 * call set_size as well (@see porcelain_apply_edit), and the drop travel would
 * then be this frame's invention on every toplevel.
 *
 * Parent-local like every other move here, and the parent is a toplevel layer
 * that nothing in this frame moves -- so unlike the chat bar's, this converges
 * on the first application.
 *
 * Not raised. Every surface this frame places is raised over its own chrome
 * because the chrome is drawn at canvas coordinates after the lane's subtree;
 * this slot is pinned to the left of the orb column, where this frame paints
 * nothing, and raising it would put a full-height transparent container over
 * the rail and the sheet for no picture at all.
 */
static void
mobile_describe_xp_drops(
    struct MobileCall* ctx,
    struct ToriRS_PorcelainDescribe* describe)
{
    struct PorcelainElementState slot;
    struct ToriRS_WidgetBounds box;

    assert(ctx);
    assert(describe);
    if( !g_frame.xp_drops_placed )
        return;
    if( !Porcelain_Element(ctx->state->porcelain, MOBILE_XP_DROPS, &slot) )
        return;
    box.x = (g_frame.xp_drops_right - slot.box.width) - (slot.box.x - slot.local.x);
    box.y = g_frame.xp_drops_top - (slot.box.y - slot.local.y);
    box.width = 0;
    box.height = 0;
    describe->move(describe, MOBILE_XP_DROPS, box, 0);
}

/* The two round windows' masks, and the compass rose the classic family brings
 * with it: re-skins stated as NAMES, because that is what a description
 * carries. An empty half leaves that half of the native picture alone. */
static void
mobile_describe_skins(
    struct MobileCall* ctx,
    struct ToriRS_PorcelainDescribe* describe)
{
    struct
    {
        int surface;
        struct MobileSkin const* skin;
    } const skins[] = {
        { FRAME_SURFACE_MINIMAP, &g_frame.skin_minimap },
        { FRAME_SURFACE_COMPASS, &g_frame.skin_compass },
    };

    assert(ctx);
    assert(describe);
    for( size_t i = 0; i < sizeof(skins) / sizeof(skins[0]); i++ )
    {
        struct MobileSkin const* skin = skins[i].skin;
        if( !skin->placed )
            continue;
        /*
         * A re-skin with NEITHER half is not a re-skin.
         *
         * An empty half means "leave that half of the native picture alone",
         * and the two halves are independent -- which is the whole reason the
         * compass can take a mask and keep the lane's rose. Both halves empty
         * says nothing at all, and Porcelain_Skin asserts on it, because a
         * describe that states nothing is a caller bug and not a no-op. It
         * became reachable the moment the masks stopped being named before
         * they were cut (@see mobile_mask): on a 2004 lane the compass keeps
         * the lane's rose, so its image half is empty by design, and until the
         * ring decodes its mask half is empty too.
         */
        if( !skin->art.name && !skin->mask.name )
            continue;
        describe->skin(
            describe, FRAME_SURFACE_ELEMENT[skins[i].surface], skin->art.name, skin->mask.name);
    }
}

/*
 * Dress the OldSchool chat pack in Stone Drawer furniture.
 *
 * The pack keeps its message text, its input line, its scrollbar, its eight
 * FILTERS and every action inside them. What changes is the picture: the torn
 * parchment behind it, a transparent backing so the sheet shows through, and
 * a 2004 stone on the bar for each of the eight -- at the boxes the filter
 * elements report, so a caption always lands on a stone, and CLEAR between
 * them so the sheet and the world show through. The plates themselves are
 * hidden: a 2004 chat filter is a caption on a stone with nothing between the
 * rock and the text, and the captions are the LANE's.
 *
 * The UNDRESSING is gone, and that is the port's clearest single win. There
 * used to be a `chat_dressed` flag and a branch that found the backing, the bar
 * and all eight plates and `reset` each one when the frame stopped being this
 * plugin's -- a whole second code path, reached on a provider switch, that
 * could and did leave dressing behind when it was not. Now the dressing is
 * simply not described, and the diff takes it off.
 */
static void
mobile_describe_chat_dress(
    struct MobileCall* ctx,
    struct ToriRS_PorcelainDescribe* describe)
{
    struct MobileState* state = ctx->state;
    struct PorcelainElementState pack;
    struct PorcelainElementState bar;
    struct PorcelainElementState row;
    struct PorcelainElementState backing;
    struct MobileChatCell cell[MOBILE_CHAT_CELL_MAX];
    int cell_count = 0;
    struct MobileArt rock;

    assert(ctx);
    assert(describe);
    if( !mobile_lane_oldschool(ctx) || !g_frame.chat_placed )
        return;
    /*
     * The PACK before its parts.
     *
     * The bar, the backing and the eight plates are all children of interface
     * 162, which the server mounts several frames after the toplevel. Asking
     * about a child before the pack exists gets it called ABSENT, and an
     * absence recorded for something that binds a frame later is a finding
     * nobody can act on. The container is one ask that answers for all ten.
     */
    if( !Porcelain_Element(state->porcelain, PORCELAIN_EL(CHAT), &pack) )
        return;
    /* The BAR's box: every stone is measured from its left edge, and a bar
     * with no box is a pack whose layout has not run yet. */
    if( !Porcelain_Element(state->porcelain, PORCELAIN_EL(CHAT_BAR), &bar) || bar.box.width <= 0 ||
        bar.box.height <= 0 )
        return;
    /*
     * The bar is WIDER THAN THE ROW IT LIVES IN, and the rock must not be.
     *
     * On 601 the bar node answers 519 wide at x=58 -- `controls_background_
     * graphic` carries the PACK's width rather than its own container's, which
     * is 461 (162|1, abs 58,471 461x28). So the bar as the lane states it runs
     * 58 px past the strip it is the backdrop for.
     *
     * That is the lane's own geometry, not something this provider did: the
     * boxes are byte-identical with no plugins loaded. It is invisible there
     * only because the lane paints that band translucent over the world. Paint
     * opaque 2004 rock on the same number and the overflow is a detached slab
     * of stone, buttons and all, standing on the floor past the last filter --
     * which is what it looked like. The band carries no rock of its own now,
     * so the overflow would be eight-stones-worth of nothing; the clamp stays
     * because the stones are measured from the bar's left edge and a bar that
     * starts 58 px off puts every one of them 58 px off with it.
     *
     * Clamp to `controls`, the bar's OWN container, and not to the pack that
     * container is in. The pack's right edge gave the same 461 on a settled
     * 601 tree by arithmetic accident -- the pack is 58 px to the LEFT and 58
     * px wider, so the two right edges coincide -- and stopped agreeing the
     * moment either number was read from a tree mid-layout. Measured, on a
     * boot fence of this lane: the pack answered `11,0 519x165` while the bar
     * answered `0,480 519x23`, the pack's right edge came out 530, the bar's
     * 519, the `> pack_right` test was false and a 519-wide slab was described
     * with no clamp at all. The row and the bar are parent and child of the
     * same layout pass, so they cannot disagree that way.
     *
     * And the row is REQUIRED, not consulted-if-present. `if( pack.width > 0 )`
     * was the same silent skip written as a guard: a fence that could not say
     * how wide the strip is painted the rock unclamped instead of not painting
     * it. A row with no box yet is an ordinary pending layout -- the guard
     * below `bar.box.width` already reads that way -- so it waits for the next
     * fence, which is one frame and not a slab.
     *
     * The stones are measured from the clamped bar.box.x, so a caption still
     * lands on its own stone.
     */
    if( !Porcelain_Element(state->porcelain, MOBILE_CHAT_CONTROLS, &row) )
        return;
    if( row.box.width <= 0 )
        return;
    {
        int const row_left = row.box.x;
        int const row_right = row.box.x + row.box.width;
        int left = bar.box.x < row_left ? row_left : bar.box.x;
        int right = bar.box.x + bar.box.width;

        if( right > row_right )
            right = row_right;
        bar.box.x = left;
        bar.box.width = right - left;
    }
    if( bar.box.width <= 0 )
        return;
    for( int i = 0; i < MOBILE_CHAT_CELL_MAX; i++ )
    {
        struct PorcelainElementState plate;
        if( !Porcelain_Element(state->porcelain, PORCELAIN_CHAT_FILTER_EL(i), &plate) ||
            plate.box.width <= 0 || plate.box.height <= 0 )
            continue;
        cell[cell_count++] = (struct MobileChatCell){
            plate.box.x - bar.box.x, plate.box.y - bar.box.y, plate.box.width, plate.box.height
        };
    }
    rock = mobile_bar_art(ctx, bar.box.width, bar.box.height, cell, cell_count);
    if( !rock.name )
        return;
    describe->skin(describe, PORCELAIN_EL(CHAT_BAR), rock.name, NULL);
    /*
     * And the same rock as a picture of this frame's OWN, because on the
     * mobile toplevel the re-skin above reaches nothing.
     *
     * A re-skin can only change a picture the lane DRAWS. 601 does not draw
     * one here: its bar is a TYPE_GRAPHIC held at full transparency -- the
     * toplevel paints the band with a translucent rect beside it and keeps
     * this node for its box -- and UITree_EmitFill drops a node at trans 255
     * before it ever looks at what a plugin put on it. So the engine took the
     * skin, answered OK, recorded it applied, and threw it away on every
     * frame; what reached the screen was seven captions and their green mode
     * lines standing on the world, with no socket, no plate and no affordance
     * of any kind. A control with nothing behind it does not read as a
     * control, which is the same defect as a plate on parchment said the
     * other way round.
     *
     * The picture is the same one either way -- one band carrying a 2004
     * stone at every filter the pack has and nothing elsewhere -- so this is
     * still dressing the bar and not replacing the subtree, and still one
     * picture rather than a plate per caption.
     *
     * BEHIND the whole pack, exactly as `pack-sheet` below is: the captions,
     * the mode lines and the bar's own translucent band are all inside the
     * pack, so behind it is under all three and over the world. Anchoring to
     * the BAR would have been the obvious depth and is refused by rule -- a
     * depth target that does not paint cannot be got above, which is the same
     * fact that made the re-skin useless.
     */
    {
        struct PorcelainItem item;

        memset(&item, 0, sizeof(item));
        item.key = "bar-rock";
        item.image = rock.name;
        item.w = bar.box.width;
        item.h = bar.box.height;
        item.place.kind = PORCELAIN_AT_CANVAS;
        item.place.dx = bar.box.x;
        item.place.dy = bar.box.y;
        item.place.depth = PORCELAIN_EL(CHAT);
        item.place.behind = true;
        /* The bar's own presented state and not the chat's: a script that puts
         * the filter row away must take its rock with it. */
        item.visible_with = PORCELAIN_EL(CHAT_BAR);
        describe->piece(describe, &item);
    }

    if( mobile_chat_backing(ctx, &backing) )
    {
        /*
         * The sheet stops ABOVE THE BAR, and the bar's own box says where.
         *
         * The backing runs the whole height of the pack, the stone bar
         * included, so a sheet sized to the backing puts its last rows -- the
         * nine-patch's torn BOTTOM fringe, the one edge that makes the
         * parchment read as paper -- underneath `bar-rock`, which is drawn
         * over them. The paper then met the rock at a straight horizontal cut
         * while its other three edges were torn. Measured on classic548: sheet
         * 0,320,519x170 ends at 489 and the bar starts at 476, so 14 rows went
         * under it; on stone601, 10 of the 12 inked rows.
         *
         * This is the same rule MOBILE_CHAT_Y already states for the 2004
         * branch -- pin the block by the ART's last inked row, not by the
         * surface's -- said here for the OldSchool one, which never got it.
         *
         * From the BAR'S OWN BOX and not from MOBILE_STRIP_H: the rock above
         * is placed at `bar.box`, and a sheet clipped against a constant while
         * the rock is drawn at the lane's number is the two-sources-of-truth
         * the straight seam came from. The lane's bar is 23 tall here, not 36.
         */
        int h = backing.box.height + MOBILE_O_PAPER_PAD_T + MOBILE_O_PAPER_PAD_B;
        int const sheet_top = backing.box.y - MOBILE_O_PAPER_PAD_T;
        struct MobileArt paper;

        if( bar.box.height > 0 && bar.box.y > sheet_top && bar.box.y - sheet_top < h )
            h = bar.box.y - sheet_top;
        paper = mobile_paper_art(ctx, backing.box.width, h);

        if( paper.name )
        {
            struct PorcelainItem item;

            /*
             * The sheet is positioned from the BACKING's box and sits behind
             * the PACK, and it is gated on the backing being presented: the two
             * are different questions, and it used to inherit the chat's
             * presented state -- so a script that hid the backing left the
             * sheet painting on its own. That is the ledger's F15.
             */
            memset(&item, 0, sizeof(item));
            item.key = "pack-sheet";
            item.image = paper.name;
            item.w = backing.box.width;
            item.h = h;
            item.place.kind = PORCELAIN_AT_CANVAS;
            item.place.dx = backing.box.x;
            item.place.dy = backing.box.y - MOBILE_O_PAPER_PAD_T;
            item.place.depth = PORCELAIN_EL(CHAT);
            item.place.behind = true;
            item.visible_with = PORCELAIN_EL(CHAT_BACKING);
            describe->piece(describe, &item);
            /*
             * The backing keeps its box and loses its PICTURE -- a transparent
             * re-skin rather than a hide -- so the sheet behind the pack shows
             * through it and every native part consumer still sees the block.
             *
             * And only where there is a picture to lose. `graphic_token` is the
             * lane saying whether this node carries art at all, and on the
             * mobile toplevel the backing is a plain LAYER: the re-skin was
             * refused there, silently, on every frame the apply pass ran --
             * which the layer reports and the apply pass swallowed. A node that
             * draws nothing already shows the sheet behind it.
             */
            if( backing.graphic_token != 0 )
                describe->skin(describe, PORCELAIN_EL(CHAT_BACKING), mobile_blank(ctx).name, NULL);
        }
    }
    /* The eight plates, hidden: the caption above each is the lane's own and
     * stays, mode line and all, straight on the rock. */
    for( int i = 0; i < MOBILE_CHAT_CELL_MAX; i++ )
    {
        struct PorcelainElementState plate;
        if( Porcelain_Element(state->porcelain, PORCELAIN_CHAT_FILTER_EL(i), &plate) )
            describe->hide(describe, PORCELAIN_CHAT_FILTER_EL(i));
    }
}

static void
mobile_call_init(
    struct MobileCall* call,
    struct ToriRS_Api* api,
    struct MobileState* state);

/*
 * The whole frame, as one description.
 *
 * Run by Porcelain_FrameEvent when the host asks for the frame, and again
 * whenever one of the layer's own inputs moved -- the canvas resized, a role
 * rebound, an asset landed, a config key changed, this plugin invalidated. The
 * layout is rebuilt from nothing every run and that is deliberate: every number
 * in it is derived from the canvas and from what the lane has bound, and a plan
 * carried over from a run whose inputs are gone is exactly the stale frame the
 * provider used to ship.
 *
 * PENDING comes out of this for free. A description that asked about an element
 * which has not resolved yet holds the frame back by rule, which is the
 * convergence the one-shot PENDING in the old provider got wrong.
 */
static void
mobile_describe(
    struct ToriRS_PorcelainDescribe* describe,
    void* user)
{
    struct MobileState* state = user;
    struct MobileCall call;
    struct MobileCall* ctx = &call;
    struct PorcelainElementState viewport;

    assert(describe);
    assert(state);
    mobile_call_init(&call, state->api, state);

    mobile_ensure_masks(ctx);
    memset(&g_frame, 0, sizeof(g_frame));
    mobile_layout(ctx, state->canvas_w, state->canvas_h);
    /*
     * The sheet and the drawer stop a tap reaching the world behind them.
     *
     * The scene is the WHOLE canvas on this frame, so every pixel of chrome has
     * world underneath it: a tap that misses a chat line or an inventory cell
     * used to fall straight through and walk the player somewhere. The blockers
     * are pieces -- owned controls under the live widgets -- so the chat's
     * scrollbar and the panel's items still take their own taps first, which
     * is what each blocker's `behind` surface is for rather than a hope about
     * the order they were created in. @see MobileBlit::behind. The
     * sheet's runs from the block's top to the block's BOTTOM, and the
     * SURFACE's columns rather than the torn parchment's: a blocker cut to
     * the picture would swallow taps on world the player can see.
     *
     * The bottom used to be the usable floor, which was the same number while
     * the block was hung flush to it. It is not any more: the block sits on
     * the bottom margin like everything else on that edge, and a blocker that
     * still ran to the floor would make those four rows of visible world a
     * dead band -- the defect this blocker exists to avoid, four pixels tall.
     */
    if( g_frame.chat_placed )
        mobile_blocker(
            ctx,
            "piece.blocker.chat",
            (struct ToriRS_Rect){ g_frame.surface[FRAME_SURFACE_CHAT].rect.x,
                                  g_frame.chat_y,
                                  g_frame.chat_w,
                                  g_frame.chat_bottom - g_frame.chat_y },
            "Type",
            PORCELAIN_EL(CHAT));
    if( g_drawer_open )
        mobile_blocker(
            ctx,
            "piece.blocker.panel",
            (struct ToriRS_Rect){
                g_frame.panel_x, g_frame.panel_y, MOBILE_PANEL_W, MOBILE_PANEL_H },
            "Panel",
            mobile_panel_element(ctx));

    /*
     * Nothing at all until the lane has a scene to arrange around.
     *
     * Asking is what holds the frame PENDING, so this is both the guard and the
     * answer: the viewport is the one element every layout needs, and a run
     * that got no further stated nothing, which the reconcile takes as "remove
     * what I own" -- correct on a remount, where the nodes those keys named are
     * gone anyway.
     */
    if( !Porcelain_Element(state->porcelain, PORCELAIN_EL(VIEWPORT), &viewport) )
        return;
    /* Noticed here and reported after the fence: invalidating from inside a
     * describe is the trap the layer documents -- the reconcile runs once per
     * fence on the LAST pass's scratch, so a run that invalidates itself has
     * its own description discarded. @see MobileState::remounted. */
    if( state->viewport_incarnation && state->viewport_incarnation != viewport.incarnation )
        state->remounted = true;
    state->viewport_incarnation = viewport.incarnation;

    mobile_describe_chrome(ctx, describe);
    mobile_describe_surfaces(ctx, describe);
    mobile_describe_chat_bar(ctx, describe);
    mobile_describe_xp_drops(ctx, describe);
    mobile_describe_skins(ctx, describe);
    mobile_describe_chat_dress(ctx, describe);
}

/* ---------------------------------------------------------------- events */

/*
 * The two lane reads this file cannot get from an element, in ONE place.
 *
 * Which tab is open and which tabs the server has given out are the only facts
 * the frame needs that the layer cannot answer: PorcelainElementState carries a
 * `facets` word for exactly this and every adapter reads ZERO into it today, so
 * SELECTED and GIVEN do not exist to be asked for. @see the port report's verb
 * list.
 *
 * So they are read here and nowhere else, and everything downstream -- the lit
 * stone, the icon, the arming of the cell -- reads the stash. Keyed by TABNO
 * and not by plan index, because this runs before the plan does on the fence a
 * frame is first asked for.
 *
 * Returns whether either answer MOVED, which is what the caller turns into an
 * invalidation.
 */
static bool
mobile_poll_tabs(
    struct ToriRS_Api* api,
    struct MobileState* state)
{
    int active;
    uint32_t given = 0;
    int flash_dark = -1;
    bool moved;

    assert(api);
    assert(state);
    active = api->cache.tab_active(api);
    for( int tabno = 0; tabno < MOBILE_TAB_COUNT; tabno++ )
    {
        if( api->cache.tab_enabled(api, tabno) )
            given |= 1u << tabno;
        /* At most one tab is ever flagged, so this records WHICH and stops
         * asking: the engine answers false for every tab that is not it. */
        if( flash_dark < 0 && api->cache.tab_flash_hidden(api, tabno) )
            flash_dark = tabno;
    }
    moved = active != state->tab_active_shown || given != state->tab_given_shown ||
            flash_dark != state->tab_flash_dark_shown;
    state->tab_active_shown = active;
    state->tab_given_shown = given;
    state->tab_flash_dark_shown = flash_dark;
    return moved;
}

static void
mobile_call_init(
    struct MobileCall* call,
    struct ToriRS_Api* api,
    struct MobileState* state)
{
    assert(call);
    assert(api);
    assert(state);
    memset(call, 0, sizeof(*call));
    call->api = api;
    call->state = state;
    state->api = api;
}

/*
 * The frame is asked for, or given back.
 *
 * One answer is this plugin's own and is given before the layer is reached,
 * because it is not a question about an element: the title screen has no frame
 * to dress. Everything after that is the description, and Porcelain_FrameEvent
 * runs it, fences it and answers READY, PENDING or UNSUPPORTED from what the
 * run touched.
 */
static enum ToriRS_FrameBuildResult
mobile_on_gameframe(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_GameframeEvent const* event)
{
    struct MobileState* state = state_ptr;
    struct MobileCall call;
    enum ToriRS_FrameBuildResult result;

    assert(api);
    assert(state);
    assert(event);
    mobile_call_init(&call, api, state);
    if( !event->active )
    {
        /*
         * A release is a description that stages nothing, run and fenced by the
         * layer: the moves, the hides, the skins and every owned control come
         * off in one pass and the claims go with them.
         */
        enum ToriRS_FrameBuildResult const released =
            (enum ToriRS_FrameBuildResult)Porcelain_FrameEvent(state->porcelain, event);
        state->provided = 0;
        /*
         * And then the half the diff cannot undo.
         *
         * The description's undo is the description; the IME and the chat
         * line's focus are not in the tree, so nothing in the layer takes them
         * back. A provider switch with the keyboard up used to leave it up with
         * nothing to type into, and the outgoing provider never gets another
         * frame start to notice -- which is the ledger's F9, and the gap the
         * frame round records as "a frame release gives you no hook for the
         * non-description side effects". Doing it here is that hook, by hand.
         */
        if( state->keyboard_on )
        {
            state->keyboard_on = false;
            api->input.text_input(api, false);
        }
        api->input.chat_focus(api, false);
        /* Porcelain_FrameEvent fences; a fence whose writes nobody committed is
         * a frame of stale layout and the layer says so. The per-frame fence
         * commits its own, and this one is not on that path. */
        Porcelain_Commit(api);
        return released;
    }
    /* A frame offer is meaningful only on the game screen. PENDING keeps the
     * lane-native title tree intact until live game surfaces exist. */
    if( api->core.screen(api) != TORIRS_SCREEN_GAME )
    {
        (void)snprintf(
            event->reason,
            event->reason_capacity,
            "%s",
            "Stone Drawer is waiting for the game screen.");
        return TORIRS_FRAME_PENDING;
    }
    /* Before the description, because the description READS the stash and the
     * host may ask for the frame before it has started one. @see
     * mobile_poll_tabs. */
    (void)mobile_poll_tabs(api, state);

    /*
     * The canvas and the safe rect, carried by hand from the event to the
     * description.
     *
     * Porcelain_FrameEvent already takes these numbers, compares them, stores
     * them and notes PORCELAIN_INPUT_CANVAS when they move -- and then hands
     * the describe function nothing but its own user pointer. A layout has no
     * other input, so every frame provider written against this layer will copy
     * exactly these lines. @see the port report.
     */
    state->canvas_w = event->width;
    state->canvas_h = event->height;
    state->safe.x = event->safe.x;
    state->safe.y = event->safe.y;
    state->safe.width = event->safe.width;
    state->safe.height = event->safe.height;

    result = (enum ToriRS_FrameBuildResult)Porcelain_FrameEvent(state->porcelain, event);
    Porcelain_Commit(api);
    state->provided = result == TORIRS_FRAME_READY;
    if( result == TORIRS_FRAME_READY || !state->logged_pending )
        api->core.log(
            api,
            "mobile stone drawer at %dx%d: %d chrome pieces, %d tabs, drawer %s, chat %s%s",
            event->width,
            event->height,
            state->frame.blit_count + state->frame.housing_placed,
            state->frame.tab_count,
            state->drawer_open ? "open" : "shut",
            state->frame.chat_placed ? "up" : "down",
            result == TORIRS_FRAME_READY ? "" : " (pending)");
    state->logged_pending = result != TORIRS_FRAME_READY;
    return result;
}

/*
 * The per-frame half.
 *
 * Two things move between descriptions without any of the layer's own inputs
 * moving, so this plugin says so itself rather than re-describing blind: which
 * stone is open and which icons the server has handed over.
 *
 * Everything else the frame used to do here is gone: the mask and art builders
 * that re-ran up to twenty image_size calls a frame until they latched, the tab
 * refresh that compared two arrays of last-written state against the live
 * answer, and the chat dressing that was re-applied and conditionally
 * un-applied every single frame whether or not the pack had moved.
 */
static void
mobile_on_frame(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_FrameEvent const* event)
{
    struct MobileState* state = state_ptr;

    (void)event;
    assert(api);
    assert(state);
    state->api = api;
    /* A lit stone changes what the description SAYS and not what the frame IS,
     * so it goes through the layer: the next fence re-describes and the
     * reconcile writes the setters that moved, in this same frame. */
    if( state->provided && api->core.screen(api) == TORIRS_SCREEN_GAME &&
        mobile_poll_tabs(api, state) )
        Porcelain_Invalidate(state->porcelain);
    Porcelain_Fence(state->porcelain);
    Porcelain_Commit(api);
    if( state->remounted )
    {
        state->remounted = false;
        api->frame.invalidate(api);
    }
}

/*
 * Leaving the game forgets the plan: its boxes were measured against a
 * gameframe that no longer exists, and the next session's tree is asked for a
 * fresh one.
 */
static void
mobile_on_screen(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_ScreenChangedEvent const* event)
{
    struct MobileState* state = state_ptr;
    assert(api);
    assert(state);
    assert(event);
    (void)api;
    if( event->screen != TORIRS_SCREEN_GAME )
    {
        memset(&state->frame, 0, sizeof(state->frame));
        state->provided = 0;
        state->viewport_incarnation = 0;
        /* Measured against a chatbox that no longer exists, and so is every
         * other number above. @see MobileState::chat_backing_box. */
        memset(&state->chat_backing_box, 0, sizeof(state->chat_backing_box));
    }
}

static void
mobile_on_start(
    struct ToriRS_Api* api,
    void* state_ptr)
{
    struct MobileState* state = state_ptr;

    assert(api);
    assert(state);
    memset(state, 0, sizeof(*state));
    state->api = api;
    state->hole_map = (struct MobileHole){ 42, 8, 146, 151, 0, 0 };
    state->hole_compass = (struct MobileHole){ 17, 3, 33, 33, 0, 0 };
    state->map_w = MOBILE_MAP_W;
    state->map_h = MOBILE_MAP_H;
    state->chat_open = true;
    state->tab_active_shown = -1;
    /* Nothing is flashing until the poll says so; a zeroed field would mean
     * "tab 0's icon is dark" and blank the combat rock for one fence. */
    state->tab_flash_dark_shown = -1;
    /* Present until the first description asks, so a rail described before the
     * lane has mounted its sidebar still wears its icons. @see
     * mobile_tab_present. */
    for( int i = 0; i < MOBILE_TAB_COUNT; i++ )
        state->tab_present[i] = true;

    state->porcelain = Porcelain_Open(api, &TORIRS_PLUGIN_MOBILE_GAMEFRAME, state);
    assert(state->porcelain);
    g_mobile_porcelain_for_testing = state->porcelain;

    /*
     * The chat pack's own size, declared unsupported on the lanes that do not
     * state one.
     *
     * The layout asks Porcelain_NativeSize(CHAT) for the pixel box the LANE
     * authored its chatbox at, and falls back to 479x96 (a 2004 builtin) or
     * 519x165 (every OldSchool pack) when the lane has no answer. A lane with
     * no stated size is a fact about the lane, not a failure, and declaring it
     * is what keeps the clean-findings gate meaningful.
     */
    Porcelain_ExpectUnsupported(
        state->porcelain,
        "the lane states no pixel size for this surface",
        "the chat falls back to the size its era's chatbox is built for");

    /*
     * A 2004 lane has no XP counter, and that is a fact about the lane.
     *
     * The counter and the drops are interface 122, mounted into a slot only
     * the OldSchool toplevels declare; a dat1 frame has neither the interface
     * nor a rung to name it by. Declared rather than left to report itself,
     * because the layout asks about this every fence on every lane -- @see
     * mobile_describe_xp_drops.
     */
    Porcelain_ExpectAbsent(
        state->porcelain, MOBILE_XP_DROPS, "a 2004 frame has no XP counter or XP drops");

    /*
     * The one offer, bound to the description.
     *
     * The OFFER itself is static data in ToriRS_PluginDef.frames and the host
     * resolves auto/native before this plugin starts, so registration order
     * cannot change whether it is asked for; what is bound here is the
     * DESCRIPTION of it.
     */
    Porcelain_Frame(
        state->porcelain,
        "stone-drawer",
        TORIRS_FRAME_CANVAS_WINDOW,
        MOBILE_MIN_W,
        MOBILE_MIN_H,
        mobile_describe,
        state);

    /*
     * The names, once. Every shipped picture is named from the table and its
     * handle arrives when something first asks for it; the gaps in the table
     * are slots no layout ships and stay NULL. @see mobile_art_file.
     */
    for( int i = 0; i < MOBILE_IMG_COUNT; i++ )
        state->image[i].name = MOBILE_IMAGE_FILE[i];
    /* And the names of the pictures this plugin composes for itself. A composed
     * name is published, described and re-read at every fence, so it has to
     * outlive the call that made it. @see mobile_art. */
    for( int i = 0; i < MOBILE_ART_COUNT; i++ )
    {
        char const* literal = NULL;
        switch( i )
        {
        case ART_MINIMAP_MASK:
            literal = "minimap_mask.png";
            break;
        case ART_COMPASS_MASK:
            literal = "compass_mask.png";
            break;
        case ART_ICON_CHAT:
            literal = "icon_chat_fit.png";
            break;
        case ART_ICON_KEYBOARD:
            literal = "icon_keyboard_fit.png";
            break;
        case ART_ICON_PLUGINS:
            literal = "icon_plugins_fit.png";
            break;
        case ART_PLATE_0:
            literal = "plate_l.png";
            break;
        case ART_PLATE_1:
            literal = "plate_r.png";
            break;
        case ART_O_RAIL:
            literal = "osrs_rail_plate.png";
            break;
        case ART_CHAT_BUTTON:
            literal = "chat_button_cut.png";
            break;
        default:
            break;
        }
        if( literal )
            (void)snprintf(state->art_name[i], sizeof(state->art_name[i]), "%s", literal);
        else
            (void)snprintf(
                state->art_name[i], sizeof(state->art_name[i]), "stone_%d.png", i - ART_STONE_0);
        state->art[i].name = state->art_name[i];
    }

    /* The keys the description names its own children by. Built once because a
     * key must outlive the describe that stated it, and because building
     * eighty-eight strings per fence is what the retained layer exists to
     * stop. */
    for( int i = 0; i < MOBILE_TAB_COUNT; i++ )
    {
        (void)snprintf(state->cell_key[i], sizeof(state->cell_key[i]), "tab.%02d", i);
        (void)snprintf(state->lit_key[i], sizeof(state->lit_key[i]), "lit.%02d", i);
        (void)snprintf(state->icon_key[i], sizeof(state->icon_key[i]), "icon.%02d", i);
    }
    for( int i = 0; i < MOBILE_CHAT_BUTTON_COUNT; i++ )
        (void)snprintf(state->plate_key[i], sizeof(state->plate_key[i]), "plate.%d", i);
    /* One `<slot>:<member>` per member this frame can place. @see
     * mobile_member_element for why these are spelled at all. */
    for( int s = 0; s < FRAME_SURFACE_COUNT; s++ )
        for( int m = 0; m < FRAME_MEMBER_MAX; m++ )
            (void)snprintf(
                state->member_role[s][m],
                sizeof(state->member_role[s][m]),
                "%s:%d",
                FRAME_SURFACE_ROLE[s],
                m);

    state->blank.name = "mobile_blank.png";
    {
        uint32_t const clear = 0;
        (void)api->assets.image_compose(api, state->blank.name, 1, 1, &clear, &state->blank.ref);
    }

    /*
     * No widget watches of this plugin's own, and that is forced.
     *
     * The provider used to keep four -- viewport, chat, sidebar, minimap -- and
     * call frame.invalidate from them, while the COMPASS it anchors the housing
     * to and the lane strip it subtracts were read and watched by nothing.
     * Porcelain watches every element the description names, which is all four
     * and thirty more, and the host keeps ONE watch slot per (plugin, role):
     * widget_subscribe finds an existing slot by role name and memsets it. So
     * the two registrations replace each other, last writer wins, and which one
     * that is depends on whether the describe ran before or after on_start. The
     * remount is noticed inside the description instead. @see mobile_describe.
     */
}

static void
mobile_on_asset(
    struct ToriRS_Api* api,
    void* state_ptr,
    struct ToriRS_AssetEvent const* event)
{
    struct MobileState* state = state_ptr;

    assert(api);
    assert(state);
    assert(event);
    (void)api;
    (void)event;
    /*
     * One note, and no table walk.
     *
     * This used to compare the arriving name against all sixty-eight and
     * re-request the one that matched, because a handle taken before the bytes
     * landed stayed empty for ever. The composers ask the host for a source
     * every time they read one, and Porcelain re-asks a PENDING picture by
     * itself at every fence, so the only thing left to say is that an input
     * moved.
     */
    if( state->porcelain )
        Porcelain_Note(state->porcelain, PORCELAIN_INPUT_ASSET);
}

static void
mobile_release_paper(
    struct ToriRS_Api* api,
    struct MobilePaper* paper)
{
    assert(api);
    assert(paper);
    if( paper->art.ref.value != 0 )
        api->assets.image_release(api, paper->art.ref);
    memset(paper, 0, sizeof(*paper));
}

/*
 * Every picture this plugin COMPOSED goes back, and so does every shipped one
 * it holds a handle to. The owned widgets and the retained edits are the
 * layer's, and Porcelain_Close takes both.
 */
static void
mobile_on_stop(
    struct ToriRS_Api* api,
    void* state_ptr)
{
    struct MobileState* state = state_ptr;

    assert(api);
    assert(state);
    /* Put the keyboard away with the frame that raised it: a disabled plugin
     * must not leave a phone with half its screen covered. */
    if( state->keyboard_on )
    {
        state->keyboard_on = 0;
        api->input.text_input(api, false);
    }
    Porcelain_Close(state->porcelain);
    g_mobile_porcelain_for_testing = NULL;
    for( int i = 0; i < MOBILE_IMG_COUNT; i++ )
        if( state->image[i].ref.value != 0 )
            api->assets.image_release(api, state->image[i].ref);
    for( int i = 0; i < MOBILE_ART_COUNT; i++ )
        if( state->art[i].ref.value != 0 )
            api->assets.image_release(api, state->art[i].ref);
    if( state->blank.ref.value != 0 )
        api->assets.image_release(api, state->blank.ref);
    mobile_release_paper(api, &state->paper);
    mobile_release_paper(api, &state->bar);
    memset(state, 0, sizeof(*state));
}

static void
mobile_on_config(
    struct ToriRS_Api* api,
    void* state_ptr,
    char const* key)
{
    struct MobileState* state = state_ptr;

    assert(api);
    assert(state);
    if( !key || (strcmp(key, "housing") != 0 && strcmp(key, "art") != 0) )
        return;
    /* The masks are cut from the housing, so a different housing is a different
     * pair of masks and a different set of window boxes. Dropping the latch is
     * what makes the next description read them again; the DESCRIPTION re-runs
     * by itself, because a config write is one of the layer's six inputs. */
    if( state->art[ART_MINIMAP_MASK].ref.value != 0 )
        api->assets.image_release(api, state->art[ART_MINIMAP_MASK].ref);
    if( state->art[ART_COMPASS_MASK].ref.value != 0 )
        api->assets.image_release(api, state->art[ART_COMPASS_MASK].ref);
    state->art[ART_MINIMAP_MASK].ref = (struct ToriRS_ImageRef){ 0 };
    state->art[ART_COMPASS_MASK].ref = (struct ToriRS_ImageRef){ 0 };
    state->masks_ready = false;
    /* The art family changes the frame's SHAPE -- a different rail is a
     * different width -- and the host owns the frame record. */
    api->frame.invalidate(api);
}
/*
 * The default is the LABEL and not "0", so that the value this ships with has
 * the same shape as the value the settings panel writes back. Two spellings of
 * one choice in a file is how a reader ends up believing one is special.
 */
static struct ToriRS_ConfigItem const MOBILE_CONFIG[] = {
    /* Auto is the 2004 pieces on every lane; OldSchool Mobile's own are a
     * choice. @see mobile_family. */
    { "art",     TORIRS_CONFIG_ENUM, "Art",         "Auto", 0, 2, "Auto|Classic|OldSchool",      0 },
    /* Auto is the family's own ring. Lizards and Ring keep the numbers they
     * were saved under. */
    { "housing",
     TORIRS_CONFIG_ENUM,             "Map housing",
     "Auto",                                                0,
     3,                                                           "Lizards|Ring|OldSchool|Auto",
     0                                                                                             },
    { NULL,      TORIRS_CONFIG_BOOL, NULL,          NULL,   0, 0, NULL,                          0 },
};

static struct ToriRS_ConfigSchema const MOBILE_SCHEMA = {
    .struct_size = sizeof(struct ToriRS_ConfigSchema),
    .items = MOBILE_CONFIG,
};

_Static_assert(
    sizeof(MOBILE_HOUSING_NAME) / sizeof(MOBILE_HOUSING_NAME[0]) == MOBILE_HOUSING_AUTO + 1,
    "the housing name table and the schema's choices= are the same list");
_Static_assert(
    sizeof(MOBILE_HOUSING) / sizeof(MOBILE_HOUSING[0]) == MOBILE_HOUSING_COUNT,
    "every housing choice needs a picture");

static struct ToriRS_FrameOffer const MOBILE_FRAME_OFFERS[] = {
    {
     .struct_size = sizeof(struct ToriRS_FrameOffer),
     .id = "stone-drawer",
     .title = "Stone Drawer",
     .canvas = TORIRS_FRAME_CANVAS_WINDOW,
     .min_width = MOBILE_MIN_W,
     .min_height = MOBILE_MIN_H,
     },
    { .struct_size = sizeof(struct ToriRS_FrameOffer) },
};

struct ToriRS_PluginDef const TORIRS_PLUGIN_MOBILE_GAMEFRAME = {
    .struct_size = sizeof(struct ToriRS_PluginDef),
    .id = "mobile-gameframe",
    .title = "Mobile Gameframe (Stone Drawer)",
    .version = "3.0.0",
    .state_size = sizeof(struct MobileState),
    .config = &MOBILE_SCHEMA,
    .flags = TORIRS_PLUGIN_DISABLED_BY_DEFAULT,
    .frames = MOBILE_FRAME_OFFERS,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = mobile_on_start,
        .on_stop = mobile_on_stop,
        .on_frame_start = mobile_on_frame,
        .on_screen_changed = mobile_on_screen,
        .on_config_changed = mobile_on_config,
        .on_asset = mobile_on_asset,
        .on_gameframe = mobile_on_gameframe,
    },
};
