#ifndef SRC_UI_TORIRS_CHROME_INKWELL_H
#define SRC_UI_TORIRS_CHROME_INKWELL_H

#include <stdint.h>

/*
 * The touch marker -- the "inkwell" -- authored here rather than baked.
 *
 * WHY THIS IS DRAWN AND NOT BAKED
 *
 * Every other piece of chrome art in this tree comes out of a cache through
 * 3rd/rscache/tools/spritebake (see engine/torirs_chrome_skin_baked.c, which
 * says "GENERATED -- do not edit" at the top). That works because those sprites
 * EXIST in a cache. This one does not: no revision ever shipped a touch marker,
 * because no revision this client reproduces ran on a phone. So it has to be
 * authored, and authoring it as code rather than as a pixel array is what makes
 * three styles and two colours cheap instead of six blobs of hex.
 *
 * WHAT IT IS FOR
 *
 * The cross (ui/uitree_cross.c) marks a click that RESULTED IN SOMETHING -- a
 * walk, an interaction. Plenty of taps produce neither: a tap on an interface
 * widget, a tap that missed, a tap during a modal. On a mouse that is fine,
 * because the pointer is visible and the user knows the machine saw it. On a
 * touchscreen there is no pointer, and a tap that draws nothing is
 * indistinguishable from a tap the device never received.
 *
 * So this fires on EVERY touch, and that is the whole difference from the
 * cross. It answers "did that register?", which is a question only touch has.
 */

/**
 * Which artwork. Three, because the right answer is a matter of taste and the
 * only way to pick is to look at them on the device.
 */
enum ToriRSInkwellStyle
{
    /** An expanding filled blob that fades as it grows. Closest to the tap
     *  feedback a modern touch client uses. */
    TORIRS_INKWELL_SPLASH = 0,
    /** A solid centre with a fixed ring around it, fading in place. Reads as a
     *  deliberate marker rather than a ripple. */
    TORIRS_INKWELL_BLOT,
    /** A thin ring that expands outward and fades, centre left empty -- the
     *  least obtrusive of the three over a busy scene. */
    TORIRS_INKWELL_RIPPLE,
    TORIRS_INKWELL_STYLE_COUNT
};

/** Colour, matching the cross's semantics: yellow walks, red interacts. */
enum ToriRSInkwellColour
{
    TORIRS_INKWELL_YELLOW = 0,
    TORIRS_INKWELL_RED,
    TORIRS_INKWELL_COLOUR_COUNT
};

/** Frames in a marker's animation, and how long each is shown. 8 frames at
 *  50 ms is 400 ms total -- the same life the cross has, so the two feel like
 *  the same client. */
#define TORIRS_INKWELL_FRAMES 8
#define TORIRS_INKWELL_FRAME_MS 50

/*
 * HOW BIG THE MARKER IS
 *
 * In POINTS, not pixels. A marker is sized against a FINGERTIP, and a fingertip
 * is the same size on every phone -- so the one number that can be authored
 * here is a physical one, and the pixel count is whatever that comes to on the
 * display in front of it. Stating it in pixels instead makes the marker shrink
 * as screens get sharper: the same 32 pixels that covered a fingertip on a
 * 160dpi display is a quarter of one at 640dpi, which is the wrong direction
 * for the one piece of art whose entire job is to be seen under a finger.
 *
 * Nothing is resampled to get there. The frames are drawn (INK_LEN16 scales
 * every radius), so a 2x marker is drawn at 2x and is exactly as sharp as a 1x
 * one -- the advantage of authoring artwork as code rather than as pixels, and
 * the reason this is a generator and not a bake.
 */

/** The authored size, in points -- 32 of them, which is a fingertip's width at
 *  arm's length and covers the cross it stands in for. */
#define TORIRS_INKWELL_POINTS 32

/** Densities the atlas is built for. Matches PlatformWindow_PixelDensity's own
 *  1..4 clamp; there is no display past 4x to build for. */
#define TORIRS_INKWELL_DENSITY_MAX 4

/**
 * Build the atlas for this display density (drawable pixels per point).
 *
 * Idempotent: calling it every frame with the same density costs a compare,
 * which is what lets the caller simply follow PlatformWindow_PixelDensity rather
 * than having to notice the one event that changes it -- a window dragged onto
 * a display of a different density raises none.
 */
void
ToriRSInkwell_SetDensity(int density);

/** The square every frame currently is, in CANVAS pixels: points x density.
 *  Whoever places or uploads the marker asks; nobody recomputes the product. */
int
ToriRSInkwell_Size(void);

/** `style` from a profile's `style=` key, or -1 when unrecognised. */
int
ToriRSInkwell_StyleFromName(char const* name);

char const*
ToriRSInkwell_StyleName(int style);

/**
 * One frame, as ARGB8888, `ToriRSInkwell_Size()` square.
 *
 * Returns a pointer into the atlas built by ToriRSInkwell_SetDensity, which
 * must have been called: the frames are the same every time and there are only
 * 48 of them (3 styles x 2 colours x 8 frames), so they are drawn once at the
 * display's density and kept rather than re-rendered per touch.
 */
uint32_t const*
ToriRSInkwell_Frame(int style, int colour, int frame);

/** How many frames a full upload holds: every style x colour x frame. */
#define TORIRS_INKWELL_ATLAS_COUNT \
    (TORIRS_INKWELL_STYLE_COUNT * TORIRS_INKWELL_COLOUR_COUNT * TORIRS_INKWELL_FRAMES)

/**
 * Where one frame sits in that upload.
 *
 * Every combination is uploaded, so choosing a style at boot -- or a colour
 * per touch -- is an index change and never a re-upload. The ordering is
 * spelled once, here, because the bridge that fills the atlas and the emit that
 * reads it must agree and neither should own the rule.
 */
static inline int
ToriRSInkwell_AtlasIndex(int style, int colour, int frame)
{
    return ((style * TORIRS_INKWELL_COLOUR_COUNT) + colour) * TORIRS_INKWELL_FRAMES + frame;
}

#endif
