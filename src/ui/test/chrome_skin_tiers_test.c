/*
 * The chrome skin and font tiers: every slot at 1x, 2x and 3x, of the size the
 * tier promises and of the kind its class promises, and every font tier
 * reachable through the one accessor the draw path reads.
 *
 * WHAT THIS IS FOR. The 2x and 3x skin tiers are generated
 * (tools/chrome_skin_tiers.py) from the 1x bake, which spritebake regenerates
 * from a cache. Either can move without the other: a slot appended to the
 * bake, an image re-cropped, a class row forgotten. Each of those draws
 * something plausible -- the 1x image stretched, a glyph scaled as a texture --
 * so nothing on screen says the tiers are stale. This says it:
 *
 *   - every slot is classified GLYPH or TEXTURED (no hole in the table);
 *   - tier 1 IS the bake (the same sprite, so 1x stays pixel-identical);
 *   - tiers 2 and 3 exist for every slot, are exactly n times the 1x size,
 *     and were baked from THIS 1x image (the tier bake's FNV of its source);
 *   - a GLYPH tier holds at most three distinct colours (the plate and two
 *     inks: the mask route), and keeps ink wherever the 1x has ink;
 *   - a TEXTURED tier of an opaque 1x image is opaque (xBR blends inside the
 *     art, it does not fray its edge);
 *   - ToriRSChromeFont_Tier(face, tier) is the derived tier today (no tier is
 *     tuned) and agrees with the metrics header the layout measures with.
 *
 * Run: make -C src test-chrome-skin-tiers (and chrome-skin runs it).
 */

#include "engine/torirs_chrome_font_tier.h"
#include "engine/torirs_chrome_skin_tiers.h"
#include "ui/torirs_chrome_skin.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                   \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/** FNV-1a 32 over the words' little-endian bytes, as the tier bake hashes. */
static uint32_t
fnv1a(struct ToriRSChromeSkin_Sprite const* sprite)
{
    uint32_t h = 0x811C9DC5u;
    int const n = sprite->w * sprite->h;

    for( int i = 0; i < n; i++ )
    {
        uint32_t p = sprite->argb[i];
        if( (p >> 24) == 0 )
            p = 0;
        for( int b = 0; b < 4; b++ )
        {
            h ^= (p >> (8 * b)) & 0xFFu;
            h *= 0x01000193u;
        }
    }
    return h;
}

/** Distinct ARGB values in a sprite, counting up to `cap` + 1. */
static int
distinct_colours(struct ToriRSChromeSkin_Sprite const* sprite, int cap)
{
    uint32_t seen[16];
    int count = 0;
    int const n = sprite->w * sprite->h;

    for( int i = 0; i < n && count <= cap; i++ )
    {
        int known = 0;
        for( int k = 0; k < count; k++ )
            known |= seen[k] == sprite->argb[i];
        if( !known )
            seen[count++] = sprite->argb[i];
    }
    return count;
}

static void
test_classes(void)
{
    int glyph = 0;
    int textured = 0;

    for( int slot = 0; slot < TORIRS_CHROME_SKIN_SLOT_COUNT; slot++ )
    {
        int const cls = ToriRSChromeSkin_ClassOf(slot);
        CHECK(cls == ToriRSChromeSkin_CLASS_GLYPH || cls == ToriRSChromeSkin_CLASS_TEXTURED,
            "slot %d is not classified (%d)", slot, cls);
        glyph += cls == ToriRSChromeSkin_CLASS_GLYPH;
        textured += cls == ToriRSChromeSkin_CLASS_TEXTURED;
    }
    CHECK(glyph + textured == TORIRS_CHROME_SKIN_SLOT_COUNT, "every slot has one class");

    /* The design's two lists (docs/CHROME_SCALE.md), spot-checked at their
     * edges: a tool icon, the marks, the arrows and buttons are glyphs; the
     * bodies, frame, plates, track and grips, the wrench are textures. */
    CHECK(ToriRSChromeSkin_ClassOf(TORIRS_CHROME_SKIN_TOOL_RAISE) == ToriRSChromeSkin_CLASS_GLYPH,
        "a tool icon is a GLYPH");
    CHECK(ToriRSChromeSkin_ClassOf(TORIRS_CHROME_SKIN_CHECK_BOX_ON) == ToriRSChromeSkin_CLASS_GLYPH,
        "a check box is a GLYPH");
    CHECK(ToriRSChromeSkin_ClassOf(TORIRS_CHROME_SKIN_SCROLL_UP) == ToriRSChromeSkin_CLASS_GLYPH,
        "a scroll arrow is a GLYPH");
    CHECK(ToriRSChromeSkin_ClassOf(TORIRS_CHROME_SKIN_DOCK) == ToriRSChromeSkin_CLASS_GLYPH,
        "the dock button is a GLYPH");
    CHECK(ToriRSChromeSkin_ClassOf(TORIRS_CHROME_SKIN_SPLITTER_GRIP) == ToriRSChromeSkin_CLASS_GLYPH,
        "the splitter grip is a GLYPH");
    CHECK(ToriRSChromeSkin_ClassOf(TORIRS_CHROME_SKIN_PANEL_BODY) == ToriRSChromeSkin_CLASS_TEXTURED,
        "the panel body is TEXTURED");
    CHECK(ToriRSChromeSkin_ClassOf(TORIRS_CHROME_SKIN_FRAME_BOTTOM_RIGHT) ==
              ToriRSChromeSkin_CLASS_TEXTURED,
        "a frame corner is TEXTURED");
    CHECK(ToriRSChromeSkin_ClassOf(TORIRS_CHROME_SKIN_BUTTON_MID) == ToriRSChromeSkin_CLASS_TEXTURED,
        "a button plate is TEXTURED");
    CHECK(ToriRSChromeSkin_ClassOf(TORIRS_CHROME_SKIN_SCROLL_GRIP_MID) ==
              ToriRSChromeSkin_CLASS_TEXTURED,
        "the scroll grip is TEXTURED");
    CHECK(ToriRSChromeSkin_ClassOf(TORIRS_CHROME_SKIN_SLIDER_TRACK_RIGHT) ==
              ToriRSChromeSkin_CLASS_TEXTURED,
        "a slider track cap is TEXTURED");
    CHECK(ToriRSChromeSkin_ClassOf(TORIRS_CHROME_SKIN_PLUGIN_ICON) == ToriRSChromeSkin_CLASS_TEXTURED,
        "the plugin wrench is TEXTURED");
    CHECK(ToriRSChromeSkin_SlotWrap(ToriRSChromeSkin_SLOT_PanelBody) == ToriRSChromeSkin_WRAP_XY,
        "the panel body tiles both ways");
}

static void
test_tiers(void)
{
    CHECK(TORIRS_CHROME_SKIN_TIERS_BAKED == TORIRS_CHROME_SKIN_SLOT_COUNT,
        "the tiers were baked for %d slots, the bake has %d", TORIRS_CHROME_SKIN_TIERS_BAKED,
        TORIRS_CHROME_SKIN_SLOT_COUNT);
    CHECK(TORIRS_CHROME_SKIN_TIER_MAX == 3, "tiers up to 3x");

    for( int slot = 0; slot < TORIRS_CHROME_SKIN_SLOT_COUNT; slot++ )
    {
        struct ToriRSChromeSkin_Sprite const* base = ToriRSChromeSkin_ForSlot(slot);
        int const glyph = ToriRSChromeSkin_ClassOf(slot) == ToriRSChromeSkin_CLASS_GLYPH;
        int opaque_base = 1;

        CHECK(base != NULL, "slot %d is baked at 1x", slot);
        if( !base )
            continue;
        CHECK(ToriRSChromeSkin_SpriteTier(slot, 1) == base, "slot %d: tier 1 is the bake", slot);
        if( slot < TORIRS_CHROME_SKIN_TIERS_BAKED )
            CHECK(ToriRSChromeSkin_TierSourceHash(slot) == fnv1a(base),
                "slot %d: the tiers were baked from a different 1x image "
                "(rerun tools/chrome_skin_tiers.py)",
                slot);
        for( int i = 0; i < base->w * base->h; i++ )
            opaque_base &= (base->argb[i] >> 24) == 0xFF;

        for( int tier = 2; tier <= TORIRS_CHROME_SKIN_TIER_MAX; tier++ )
        {
            struct ToriRSChromeSkin_Sprite const* t = ToriRSChromeSkin_SpriteTier(slot, tier);
            struct ToriRSChromeSkin_Sprite const* raw = ToriRSChromeSkin_TierGet(slot, tier);

            CHECK(raw != NULL, "slot %d has a %dx tier", slot, tier);
            CHECK(t == raw, "slot %d at %dx is its tier, not the 1x fallback", slot, tier);
            if( !raw )
                continue;
            CHECK(raw->argb != NULL, "slot %d %dx has pixels", slot, tier);
            CHECK(raw->w == base->w * tier && raw->h == base->h * tier,
                "slot %d %dx is %dx%d, want %dx%d", slot, tier, raw->w, raw->h, base->w * tier,
                base->h * tier);
            if( !raw->argb || raw->w != base->w * tier || raw->h != base->h * tier )
                continue;
            if( glyph )
            {
                int lost = 0;
                CHECK(distinct_colours(raw, 3) <= 3,
                    "GLYPH slot %d %dx holds more than three colours", slot, tier);
                /* scale3x keeps the centre of every source pixel; scale2x
                 * rounds at most two opposite corners of its 2x2 block, so
                 * at least two of the four keep the source's mask. Either
                 * way no 1-pixel feature disappears. */
                for( int y = 0; y < base->h; y++ )
                    for( int x = 0; x < base->w; x++ )
                    {
                        int const src = (base->argb[y * base->w + x] >> 24) >= 128;
                        int kept = 0;
                        for( int sy = 0; sy < tier; sy++ )
                            for( int sx = 0; sx < tier; sx++ )
                                kept += src == ((raw->argb[(y * tier + sy) * raw->w + x * tier +
                                                           sx] >>
                                                    24) != 0);
                        if( tier == 3 )
                            lost += src != ((raw->argb[(y * 3 + 1) * raw->w + x * 3 + 1] >> 24) !=
                                               0);
                        else
                            lost += kept < 2;
                    }
                CHECK(lost == 0, "GLYPH slot %d %dx moved %d mask pixels", slot, tier, lost);
            }
            else if( opaque_base )
            {
                int frayed = 0;
                for( int i = 0; i < raw->w * raw->h; i++ )
                    frayed += (raw->argb[i] >> 24) != 0xFF;
                CHECK(frayed == 0, "TEXTURED slot %d %dx frayed %d pixels", slot, tier, frayed);
            }
        }
    }
}

static void
test_fonts(void)
{
    static int const line_height[ToriRSChromeFont_FACE_COUNT][ToriRSChromeFont_TIER_MAX] = {
        [ToriRSChromeFont_FACE_SMALL] = { ToriRSChromeFont_Small_LINE_HEIGHT,
                                          ToriRSChromeFont_Small2x_LINE_HEIGHT,
                                          ToriRSChromeFont_Small3x_LINE_HEIGHT },
        [ToriRSChromeFont_FACE_MENU] = { ToriRSChromeFont_Menu_LINE_HEIGHT,
                                         ToriRSChromeFont_Menu2x_LINE_HEIGHT,
                                         ToriRSChromeFont_Menu3x_LINE_HEIGHT },
        [ToriRSChromeFont_FACE_BODY] = { ToriRSChromeFont_Body_LINE_HEIGHT,
                                         ToriRSChromeFont_Body2x_LINE_HEIGHT,
                                         ToriRSChromeFont_Body3x_LINE_HEIGHT },
    };
    static int const* const advance[ToriRSChromeFont_FACE_COUNT][ToriRSChromeFont_TIER_MAX] = {
        [ToriRSChromeFont_FACE_SMALL] = { ToriRSChromeFont_Small_advance_px,
                                          ToriRSChromeFont_Small2x_advance_px,
                                          ToriRSChromeFont_Small3x_advance_px },
        [ToriRSChromeFont_FACE_MENU] = { ToriRSChromeFont_Menu_advance_px,
                                         ToriRSChromeFont_Menu2x_advance_px,
                                         ToriRSChromeFont_Menu3x_advance_px },
        [ToriRSChromeFont_FACE_BODY] = { ToriRSChromeFont_Body_advance_px,
                                         ToriRSChromeFont_Body2x_advance_px,
                                         ToriRSChromeFont_Body3x_advance_px },
    };
    struct ToriDraw_Font* (*const derived[ToriRSChromeFont_FACE_COUNT][ToriRSChromeFont_TIER_MAX])(
        void) = {
        [ToriRSChromeFont_FACE_SMALL] = { ToriRSChromeFont_Small, ToriRSChromeFont_Small2x,
                                          ToriRSChromeFont_Small3x },
        [ToriRSChromeFont_FACE_MENU] = { ToriRSChromeFont_Menu, ToriRSChromeFont_Menu2x,
                                         ToriRSChromeFont_Menu3x },
        [ToriRSChromeFont_FACE_BODY] = { ToriRSChromeFont_Body, ToriRSChromeFont_Body2x,
                                         ToriRSChromeFont_Body3x },
    };

    for( int face = 0; face < ToriRSChromeFont_FACE_COUNT; face++ )
        for( int tier = ToriRSChromeFont_TIER_MIN; tier <= ToriRSChromeFont_TIER_MAX; tier++ )
        {
            struct ToriDraw_Font const* font = ToriRSChromeFont_Tier(face, tier);
            int const tuned = ToriRSChromeFont_TierTuned(face, tier);
            int advances_agree = 1;

            CHECK(font != NULL, "face %d %dx has a font", face, tier);
            if( !font )
                continue;
            /* Today no tier is tuned: each is the derived bake. */
            CHECK(tuned == 0, "face %d %dx is not tuned yet", face, tier);
            if( !tuned )
                CHECK(font == derived[face][tier - 1](), "face %d %dx is the derived tier", face,
                    tier);
            CHECK(font->line_height == line_height[face][tier - 1],
                "face %d %dx: ascent %d, the metrics header says %d", face, tier,
                font->line_height, line_height[face][tier - 1]);
            for( int c = 0; c < 256; c++ )
            {
                int adv = font->draw_width[c];
                if( adv <= 0 )
                    adv = 4;
                advances_agree &= adv == advance[face][tier - 1][c];
            }
            CHECK(advances_agree, "face %d %dx: glyph advances are the ones the layout measures",
                face, tier);
        }
}

int
main(void)
{
    test_classes();
    test_tiers();
    test_fonts();
    if( g_failures )
    {
        fprintf(stderr, "chrome_skin_tiers_test: %d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("chrome_skin_tiers_test: %d checks passed\n", g_checks);
    return 0;
}
