#ifndef SRC_UITREE_INK_H
#define SRC_UITREE_INK_H

#include <stdbool.h>

/*
 * The touch marker's STATE. Its artwork is ui/torirs_chrome_inkwell.h, or the
 * cache pack a profile binds with `sprite=` (OSRS: clickcircle_filled).
 *
 * Jagex's phone draws this where the desktop draws the cross: armed by the
 * same walks and world ops (app_cross_show), never by an interface tap. It is
 * separate state from UICross only because a lane draws one or the other
 * (`cross@mobile` -> inkwell) and each animates on its own frame count.
 */

#include "ui/torirs_chrome_inkwell.h"

/** How long the state lives: its longest piece, the op popup text's 40 client
 *  cycles (Android 241 FUN_0061714c: renderer +0x28 = 40). */
#define UI_INK_LIFE_MS 800

struct UIInk
{
    int x;
    int y;
    /** enum ToriRSInkwellColour: yellow walks, red interacts. */
    int colour;
    /** Milliseconds since it was shown; the state ends at UI_INK_LIFE_MS. The
     *  pieces end on their own clocks inside that (app_host_request.c). */
    int cycle;
    int active;
};

void
UIInk_Reset(struct UIInk* ink);

/** A walk or world op was armed here. Starts the animation from frame 0. */
void
UIInk_Show(struct UIInk* ink, int colour, int x, int y);

void
UIInk_Tick(struct UIInk* ink, int delta_ms);

bool
UIInk_IsActive(struct UIInk const* ink);

/** Frame within the generated style's pack, 0..TORIRS_INKWELL_FRAMES-1. */
int
UIInk_Frame(struct UIInk const* ink);

#endif
