/*
 * The chrome's three faces at their three integer tiers, through one door.
 *
 * Every face (Small p11, Body p12, Menu b12) is baked at 1x, 2x and 3x
 * (engine/torirs_debug_font_baked.c). The 2x and 3x tiers are DERIVED: the 1x
 * masks multiplied, every metric with them. That reads well except where it
 * does not -- a 1-pixel counter doubled is a 2-pixel blot (p12's a, e, s; the
 * queue is in docs/CHROME_SCALE.md) -- and the fix there is a hand-tuned tier:
 * new pixel art at 2x or 3x, drawn by a person, kept beside the derived one.
 *
 * This is the hook for that. The metrics header carries, per face per tier,
 * `ToriRSChromeFont_<Face><N>x_TUNED` (fontbake writes it from the recipe's
 * `--tuned` arguments). When it is 1 this accessor hands out
 * `ToriRSChromeFont_<Face><N>xTuned()` -- a hand-kept file fontbake never
 * writes, src/engine/torirs_chrome_font_tuned.c, with the same advances and
 * line metrics as the derived tier (tools/chrome_font_tiers.py checks that) --
 * and when it is 0 the derived tier, which is every tier today.
 *
 * The draw path reads its font through ToriRSChromeFont_Tier and nothing else,
 * so turning a tuned tier on is a flag and a file, not a hunt for every place
 * that picked a font by scale.
 *
 * Header-only, for the reason ui/torirs_chrome_skin.h is: a few lines over
 * functions the bake already exports.
 */
#ifndef TORIRS_CHROME_FONT_TIER_H
#define TORIRS_CHROME_FONT_TIER_H

#include "engine/torirs_debug_font_baked.h"
#include "ui/uitree_debug_font_metrics.h"

#include <assert.h>

/* The faces, numbered as the chrome numbers its font slots
 * (TORIRS_CHROME_FONT_SMALL / _MENU / _BODY in ui/uitree_debug_overlay.h; a
 * translation unit that sees both pins them, as uitree_scene_bridge.c does).
 * Spelled here so this header does not need the widget model. */
#define ToriRSChromeFont_FACE_SMALL 0
#define ToriRSChromeFont_FACE_MENU 1
#define ToriRSChromeFont_FACE_BODY 2
#define ToriRSChromeFont_FACE_COUNT 3
/** The integer tiers baked: 1x, 2x, 3x. */
#define ToriRSChromeFont_TIER_MIN 1
#define ToriRSChromeFont_TIER_MAX 3

/* One selection per face per tier: the tuned tier when the metrics header
 * says there is one, the derived tier otherwise. A tuned tier's function is
 * declared only when it is selected, so a build without one links nothing
 * new. The 1x faces are the source of the tiers and are never tuned. */
#if ToriRSChromeFont_Small2x_TUNED
struct ToriDraw_Font* ToriRSChromeFont_Small2xTuned(void);
#define TORIRS_CHROME_FONT_TIER_SMALL2X_ ToriRSChromeFont_Small2xTuned
#else
#define TORIRS_CHROME_FONT_TIER_SMALL2X_ ToriRSChromeFont_Small2x
#endif
#if ToriRSChromeFont_Small3x_TUNED
struct ToriDraw_Font* ToriRSChromeFont_Small3xTuned(void);
#define TORIRS_CHROME_FONT_TIER_SMALL3X_ ToriRSChromeFont_Small3xTuned
#else
#define TORIRS_CHROME_FONT_TIER_SMALL3X_ ToriRSChromeFont_Small3x
#endif
#if ToriRSChromeFont_Menu2x_TUNED
struct ToriDraw_Font* ToriRSChromeFont_Menu2xTuned(void);
#define TORIRS_CHROME_FONT_TIER_MENU2X_ ToriRSChromeFont_Menu2xTuned
#else
#define TORIRS_CHROME_FONT_TIER_MENU2X_ ToriRSChromeFont_Menu2x
#endif
#if ToriRSChromeFont_Menu3x_TUNED
struct ToriDraw_Font* ToriRSChromeFont_Menu3xTuned(void);
#define TORIRS_CHROME_FONT_TIER_MENU3X_ ToriRSChromeFont_Menu3xTuned
#else
#define TORIRS_CHROME_FONT_TIER_MENU3X_ ToriRSChromeFont_Menu3x
#endif
#if ToriRSChromeFont_Body2x_TUNED
struct ToriDraw_Font* ToriRSChromeFont_Body2xTuned(void);
#define TORIRS_CHROME_FONT_TIER_BODY2X_ ToriRSChromeFont_Body2xTuned
#else
#define TORIRS_CHROME_FONT_TIER_BODY2X_ ToriRSChromeFont_Body2x
#endif
#if ToriRSChromeFont_Body3x_TUNED
struct ToriDraw_Font* ToriRSChromeFont_Body3xTuned(void);
#define TORIRS_CHROME_FONT_TIER_BODY3X_ ToriRSChromeFont_Body3xTuned
#else
#define TORIRS_CHROME_FONT_TIER_BODY3X_ ToriRSChromeFont_Body3x
#endif

/**
 * Is (face, tier) drawn from hand-tuned masks? 0 for every tier today.
 * face: ToriRSChromeFont_FACE_*; tier: ToriRSChromeFont_TIER_MIN..MAX.
 */
static inline int
ToriRSChromeFont_TierTuned(int face, int tier)
{
    static const unsigned char tuned[ToriRSChromeFont_FACE_COUNT][ToriRSChromeFont_TIER_MAX] = {
        [ToriRSChromeFont_FACE_SMALL] = { ToriRSChromeFont_Small_TUNED,
                                          ToriRSChromeFont_Small2x_TUNED,
                                          ToriRSChromeFont_Small3x_TUNED },
        [ToriRSChromeFont_FACE_MENU] = { ToriRSChromeFont_Menu_TUNED,
                                         ToriRSChromeFont_Menu2x_TUNED,
                                         ToriRSChromeFont_Menu3x_TUNED },
        [ToriRSChromeFont_FACE_BODY] = { ToriRSChromeFont_Body_TUNED,
                                         ToriRSChromeFont_Body2x_TUNED,
                                         ToriRSChromeFont_Body3x_TUNED },
    };
    _Static_assert(
        ToriRSChromeFont_Small_TUNED == 0, "the 1x Small face is the source, it cannot be tuned");
    _Static_assert(
        ToriRSChromeFont_Menu_TUNED == 0, "the 1x Menu face is the source, it cannot be tuned");
    _Static_assert(
        ToriRSChromeFont_Body_TUNED == 0, "the 1x Body face is the source, it cannot be tuned");

    assert(face >= 0);
    assert(face < ToriRSChromeFont_FACE_COUNT);
    assert(tier >= ToriRSChromeFont_TIER_MIN);
    assert(tier <= ToriRSChromeFont_TIER_MAX);
    return tuned[face][tier - 1];
}

/**
 * The font the chrome draws `face` with at integer tier `tier`: the tuned
 * tier when there is one, else the derived. Static storage; never
 * ToriDraw_FontFree it (see torirs_debug_font_baked.h).
 */
static inline struct ToriDraw_Font*
ToriRSChromeFont_Tier(int face, int tier)
{
    assert(face >= 0);
    assert(face < ToriRSChromeFont_FACE_COUNT);
    assert(tier >= ToriRSChromeFont_TIER_MIN);
    assert(tier <= ToriRSChromeFont_TIER_MAX);
    switch( face )
    {
    case ToriRSChromeFont_FACE_SMALL:
        return tier == 3   ? TORIRS_CHROME_FONT_TIER_SMALL3X_()
               : tier == 2 ? TORIRS_CHROME_FONT_TIER_SMALL2X_()
                           : ToriRSChromeFont_Small();
    case ToriRSChromeFont_FACE_MENU:
        return tier == 3   ? TORIRS_CHROME_FONT_TIER_MENU3X_()
               : tier == 2 ? TORIRS_CHROME_FONT_TIER_MENU2X_()
                           : ToriRSChromeFont_Menu();
    default:
        return tier == 3   ? TORIRS_CHROME_FONT_TIER_BODY3X_()
               : tier == 2 ? TORIRS_CHROME_FONT_TIER_BODY2X_()
                           : ToriRSChromeFont_Body();
    }
}

#endif
