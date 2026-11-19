#ifndef SRC_EDITOR_HOVER_FOOTPRINT_H
#define SRC_EDITOR_HOVER_FOOTPRINT_H

/*
 * The hover-footprint outline's state: the live mode -- 0 off, 1 the hovered
 * loc, >1 every instance of that loc id -- and the non-zero mode the toggle
 * restores.
 *
 * The outline itself is a side-app (app/app_tool_hover_footprint.c); this is
 * the half that needs no client. It decides what an env-chosen mode starts the
 * outline at, and what the toggle turns it to.
 *
 * Nothing here reads a world, input or the environment, or prints. The caller
 * parses the env var, routes the key and draws the outline.
 */

struct HoverFootprint
{
    /** The live mode: 0 off, 1 the hovered loc, >1 every instance of that
     *  loc id. */
    int live;
    /** The non-zero mode the toggle turns the outline back on at. 0 until
     *  seeded. */
    int mode;
};

/**
 * Start the outline at an env-chosen mode: a positive mode is on at that mode;
 * 0 or a negative mode is off, and the toggle then restores mode 1.
 */
void
HoverFootprint_Seed(
    struct HoverFootprint* footprint,
    int mode);

/** Turn the outline off, or back on at the mode it restores, and return the
 *  new live mode. */
int
HoverFootprint_Toggle(struct HoverFootprint* footprint);

#endif
