#ifndef TORIRS_BOOT_BAR_H
#define TORIRS_BOOT_BAR_H

#include <stdint.h>

/*
 * The startup progress bar, before any cache asset exists.
 *
 * Client-TS's GameShell.messageBox and the deob's class510.method11179 draw
 * the same picture, and both hardcode it -- as this does, for the reason it
 * exists at all: it has to be on screen while the thing still loading IS the
 * asset pipeline, so it can own no sprite, no font and no layout row. It is
 * the one screen in this client that cannot be revconfig's. Its WORDS still
 * are: by the time a caption matters there is a font to draw one with.
 *
 * A leaf on purpose -- pixels in, pixels out, no App and no scene -- so the
 * geometry can be asserted rather than eyeballed. That matters more here than
 * elsewhere: the bar is on screen for a fraction of a second and a
 * re-rendered exit frame never contains it, so a screenshot cannot check it.
 */

/** Client-TS `rgb(140, 17, 17)`; the deob's `new Color(140, 17, 17)`. */
#define BOOT_BAR_COLOR 0x8C1111u

/* The reference's own measurements. The interior is 300 wide so that three
 * pixels per percent fills it exactly at 100. */
#define BOOT_BAR_W 304
#define BOOT_BAR_H 34
#define BOOT_BAR_INSET 2
#define BOOT_BAR_FILL_W 300
#define BOOT_BAR_FILL_H 30
#define BOOT_BAR_PX_PER_PERCENT 3
/** Caption baseline, measured from the track's top. */
#define BOOT_BAR_TEXT_BASELINE 22
/** The track's top sits this far above the canvas centre. */
#define BOOT_BAR_ABOVE_CENTRE 18

/** Where the track's top-left lands on a canvas of this size. */
int
BootBar_OriginX(int width);
int
BootBar_OriginY(int height);

/**
 * Clear to black and draw the bar at `percent` (clamped to 0..100).
 *
 * The whole canvas is cleared, which is what both references do on the frame
 * that first shows the bar.
 */
void
BootBar_Draw(
    uint32_t* pixels,
    int width,
    int height,
    int percent);

#endif /* TORIRS_BOOT_BAR_H */
