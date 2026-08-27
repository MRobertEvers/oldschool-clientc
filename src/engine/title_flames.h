#ifndef TORIRS_TITLE_FLAMES_H
#define TORIRS_TITLE_FLAMES_H

#include <stdint.h>

struct ToriRS_Sprite;

/*
 * The two burning braziers on the title screen.
 *
 * A procedural fire, not an animation: a 128x256 heat field that is seeded at
 * the bottom, blurred, cooled by a scrolling noise map and finally coloured
 * through a 256-entry palette. Both references run the same simulation
 * (Client-TS updateFlames/drawFlames, deob class77), and the shape of the fire
 * comes almost entirely from the cooling map -- which is why a rune glyph
 * stamped into it as ZEROES appears as a cold silhouette drifting up through
 * the flame. That is the whole trick, and it is the reason the runes are cache
 * sprites rather than decoration.
 *
 * Deliberately free of the scene, the tree and the cache: it takes pixels and
 * masks in and gives pixels back, so a golden-frame test needs no client.
 */

#define TORIRS_FLAME_W 128
#define TORIRS_FLAME_H 256
#define TORIRS_FLAME_CELLS (TORIRS_FLAME_W * TORIRS_FLAME_H)
/*
 * The strip the fire is composited into, which is TALLER than the fire.
 *
 * Client-TS's surface is 128x265 (33920 pixels) holding a 128x256 heat
 * field drawn nine rows down, so the flame's base sits in the brazier bowl
 * instead of on the bottom edge of the strip.
 */
#define TORIRS_FLAME_COLUMN_H 265
/** Rune glyphs the cooling map draws from (Client-TS: `runes`, 12 frames). */
#define TORIRS_FLAME_RUNES 12
/** Palettes, and the two the base one cross-fades to. */
#define TORIRS_FLAME_PALETTES 3
#define TORIRS_FLAME_PALETTE_ENTRIES 256
/** Colour stops per palette; each is expanded across four bands of 64. */
#define TORIRS_FLAME_PALETTE_STOPS 5

/*
 * Where one brazier's fire sits inside the column it burns in.
 *
 * The column is a copy of the wall behind it, blitted back over that same
 * wall -- so moving the COLUMN drags the wall with it and duplicates a
 * strip of brickwork. Both references move the fire within the column
 * instead, which is what these four numbers do.
 *
 *   bias  destination column the run begins at, signed
 *   sway  which way the per-row wobble pushes this side: +1 or -1
 *   run   how many source columns are drawn
 *   row   destination row the fire starts on
 *
 * Client-TS draws its left brazier from source column `offset + 22` onward
 * into destination column 0, and its right one into column `24 + offset`
 * for 103 columns, both starting at row 9. The deob leans both by 22 and
 * starts a row higher. The numbers are the revision's, so they arrive from
 * its profile rather than living here.
 */
struct TitleFlameGeometry
{
    int bias;
    int sway;
    int run;
    int row;
};

enum TitleFlameSide
{
    TORIRS_FLAME_LEFT = 0,
    TORIRS_FLAME_RIGHT,
    TORIRS_FLAME_SIDES
};

/** One rune glyph as a coverage mask; nonzero pixels are stamped cold. */
struct TitleFlameRune
{
    uint8_t* mask;
    int width;
    int height;
    int offset_x;
    int offset_y;
};

struct TitleFlames
{
    /* The heat field and its scratch. int32 rather than uint8 because the blur
     * and the cooling subtraction both run wide before clamping. */
    int32_t heat[TORIRS_FLAME_CELLS];
    int32_t blur[TORIRS_FLAME_CELLS];
    /* The cooling map, and the ping-pong buffer its 20 smoothing passes use. */
    int32_t cooling[TORIRS_FLAME_CELLS];
    int32_t cooling_scratch[TORIRS_FLAME_CELLS];

    uint32_t palette[TORIRS_FLAME_PALETTES][TORIRS_FLAME_PALETTE_ENTRIES];
    uint32_t active_palette[TORIRS_FLAME_PALETTE_ENTRIES];
    /** Countdown for each alternate palette; 1024 on trigger, 0 when idle. */
    int fade[TORIRS_FLAME_PALETTES - 1];

    /** Per-row horizontal wobble, one entry per heat row. */
    int line_offset[TORIRS_FLAME_H];
    /** Phase the three wobble sines are sampled at. */
    int wobble_phase;
    /** Scroll offset into the cooling map; wraps, restamping a new rune. */
    int cooling_scroll;

    struct TitleFlameRune runes[TORIRS_FLAME_RUNES];
    int rune_count;

    /* The pristine backdrop behind each column, and the composite handed out.
     * The background is restored every frame before blending, because the fire
     * is translucent and compositing onto last frame's result would smear. */
    uint32_t* background[TORIRS_FLAME_SIDES];
    uint32_t* out[TORIRS_FLAME_SIDES];
    int width;
    int height;

    /* Per-side placement, and whether it was ever stated. */
    struct TitleFlameGeometry geometry[TORIRS_FLAME_SIDES];
    int has_geometry[TORIRS_FLAME_SIDES];

    /** Milliseconds not yet consumed by a fixed step. */
    int accum_ms;
    /** Private, seeded, and never rand(): a fire that differs run to run
     *  cannot be golden-tested, and this one is. */
    uint32_t rng;
    int inited;
};

/**
 * Ready the simulation.
 *
 * `background` supplies the two columns the fire burns in front of, each
 * `width` x `height` ARGB; both are copied. `runes` may be NULL, in which case
 * the cooling map carries no glyphs and the fire is a plain one -- which is
 * what a revision with no rune pack honestly has.
 */
void
TitleFlames_Init(
    struct TitleFlames* flames,
    uint32_t const* const background[TORIRS_FLAME_SIDES],
    int width,
    int height,
    struct ToriRS_Sprite const* runes);

void
TitleFlames_Free(struct TitleFlames* flames);

/**
 * Place one side's fire within its column.
 *
 * Per side because the two braziers are not mirror images: the reference's
 * right-hand fire is narrower than its left and starts further in. A side
 * never given a geometry draws no fire at all, rather than drawing it in
 * the wrong place -- an undeclared brazier is an absent one.
 */
void
TitleFlames_SetGeometry(
    struct TitleFlames* flames,
    enum TitleFlameSide side,
    struct TitleFlameGeometry const* geometry);


/**
 * Set the three palettes from five 24-bit stops each, expanded across four
 * bands of 64 entries (deob method7884).
 *
 * The old lane states its stops in the profile; the modern one can read them
 * from the cache's own flame record. Either way the colours are the
 * revision's, not the client's.
 */
void
TitleFlames_SetPalettes(
    struct TitleFlames* flames,
    int const stops[TORIRS_FLAME_PALETTES][TORIRS_FLAME_PALETTE_STOPS]);

/**
 * Advance by `elapsed_ms` and recomposite.
 *
 * Fixed-step internally: the reference ticks on a 35 ms timer and runs the
 * update twice per tick, and doing it by wall-clock instead would make the
 * flame's character depend on the frame rate. Returns nonzero when a step ran
 * and the output changed.
 */
int
TitleFlames_Advance(
    struct TitleFlames* flames,
    int elapsed_ms);

/** The composited column for one side; `width` x `height` ARGB, owned by the
 *  simulation and rewritten by the next Advance. */
uint32_t const*
TitleFlames_Pixels(
    struct TitleFlames const* flames,
    enum TitleFlameSide side);

#endif /* TORIRS_TITLE_FLAMES_H */
