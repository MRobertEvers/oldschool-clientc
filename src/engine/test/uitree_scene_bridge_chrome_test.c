/*
 * The scene bridge registers the chrome at its rational scale (workbench step
 * 6.0b, the native half; docs/CHROME_SCALE.md).
 *
 * The chrome model lays out at quarter scale q and the bake
 * (ui/torirs_chrome_bake.h) rasterises the faces and the skin at any q. This
 * holds the bridge to handing the renderers exactly those pixels:
 *
 *   - TORIRS_CHROME_SCALE's text ("1.5") parsed to q = 6 and set on the
 *     bridge resolves every face to the bake's VECTOR atlas at q = 6 (the
 *     synthesized outline, ToriRSChromeGlyphs_Rasterise; not the 1x cells
 *     scaled), under its own id, with the coverage flag a GPU path reads;
 *   - its baseline is the face's row table's baseline row, and every glyph
 *     that stands on the 1x baseline ends on it;
 *   - a stem of a known glyph is even in the registered pixels: the same
 *     bytes on every middle row, and ink (the stem darkening);
 *   - "smooth always" (SetChromeTextSmooth) registers the vector atlas at a
 *     whole q too, once per q, and turning it off drops it again; at a
 *     fractional q it changes nothing;
 *   - the skin entry at q = 6 is ToriRSChromeBake_Skin's images, at their
 *     own sizes, and the parchment is ScalePx(6, 88) x ScalePx(6, 60);
 *   - at q = 4, 8, 12 the faces are the baked tiers under their historic ids
 *     and the skin is the baked tier's pixels, byte for byte;
 *   - leaving a fractional q drops its faces through FONT_UNLOAD events, and
 *     the skin is replaced under the same id;
 *   - the software blitter blends a coverage atlas (a 64 byte is a quarter
 *     tone, not solid ink), and still draws a mask face solid.
 *
 * Content-free: the faces and the skin are compiled in, the provider is
 * empty, and no cache is opened.
 */

#include "asyncio.h"
#include "mem/torirs_mem.h"
#include "engine/cache_provider.h"
#include "engine/torirs_chrome_font_tier.h"
#include "engine/torirs_chrome_skin_baked.h"
#include "engine/uitree_scene_bridge.h"
#include "toridraw_font.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"
#include "ui/torirs_chrome_bake.h"
#include "ui/torirs_chrome_scale_q.h"
#include "ui/torirs_chrome_skin.h"
#include "ui/uitree_debug_overlay.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define CHECK(cond, ...)                                                       \
    do                                                                         \
    {                                                                          \
        g_checks++;                                                            \
        if( !(cond) )                                                          \
        {                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                        \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
            g_failures++;                                                      \
        }                                                                      \
    } while( 0 )

static char const* const k_face_name[TORIRS_CHROME_FONT_SLOT_COUNT] = {
    [TORIRS_CHROME_FONT_SMALL] = "Small",
    [TORIRS_CHROME_FONT_MENU] = "Menu",
    [TORIRS_CHROME_FONT_BODY] = "Body",
};

struct BridgeFixture
{
    struct CacheProvider provider;
    struct UITreeSceneBridge bridge;
    struct ToriDraw_Scene* scene;
};

static void
fixture_open(struct BridgeFixture* f)
{
    memset(f, 0, sizeof(*f));
    CacheProvider_InitEngineCaches(&f->provider);
    f->scene = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(f->scene);
    UITreeSceneBridge_Init(&f->bridge, f->scene, &f->provider);
}

static void
fixture_close(struct BridgeFixture* f)
{
    UITreeSceneBridge_Free(&f->bridge);
    ToriDraw_SceneFree(f->scene);
    CacheProvider_FreeEngineCaches(&f->provider);
}

/** Do two fonts carry the same glyph pixels and metrics? */
static int
fonts_equal(struct ToriDraw_Font const* a, struct ToriDraw_Font const* b)
{
    if( a->line_height != b->line_height )
        return 0;
    if( memcmp(a->draw_width, b->draw_width, sizeof(a->draw_width)) != 0 )
        return 0;
    if( memcmp(a->charcodeset, b->charcodeset, sizeof(a->charcodeset)) != 0 )
        return 0;
    for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
    {
        size_t bytes;
        if( a->glyph_width[gi] != b->glyph_width[gi] || a->glyph_height[gi] != b->glyph_height[gi] ||
            a->offset_x[gi] != b->offset_x[gi] || a->offset_y[gi] != b->offset_y[gi] ||
            a->advance[gi] != b->advance[gi] )
            return 0;
        if( !a->glyph_alpha[gi] != !b->glyph_alpha[gi] )
            return 0;
        if( !a->glyph_alpha[gi] )
            continue;
        bytes = (size_t)a->glyph_width[gi] * (size_t)a->glyph_height[gi];
        if( memcmp(a->glyph_alpha[gi], b->glyph_alpha[gi], bytes) != 0 )
            return 0;
    }
    return 1;
}

/** Does scene skin entry `scene_id` hold, frame for frame, `expect` (count frames)? */
static int
skin_frames_are(
    struct BridgeFixture* f,
    int scene_id,
    int q,
    int tier_only)
{
    int count = 0;
    struct ToriDraw_Sprite** sprites = ToriDraw_SceneSpriteGet(f->scene, scene_id, &count);
    int const expect = ToriRSChromeSkin_Count() < TORIRS_CHROME_SKIN_SLOT_COUNT
                           ? ToriRSChromeSkin_Count()
                           : TORIRS_CHROME_SKIN_SLOT_COUNT;

    if( !sprites || count != expect )
        return 0;
    for( int i = 0; i < count; i++ )
    {
        int w;
        int h;
        uint32_t const* argb;
        if( tier_only )
        {
            struct ToriRSChromeSkin_Sprite const* t = ToriRSChromeSkin_SpriteTier(i, q / 4);
            w = t->w;
            h = t->h;
            argb = t->argb;
        }
        else
        {
            struct ToriRSChromeBakeImage const* img = ToriRSChromeBake_Skin(i, q);
            w = img->w;
            h = img->h;
            argb = img->argb;
        }
        if( sprites[i]->width != w || sprites[i]->height != h || !sprites[i]->alpha_channel )
            return 0;
        if( memcmp(sprites[i]->pixels_argb, argb, sizeof(uint32_t) * (size_t)w * (size_t)h) != 0 )
            return 0;
    }
    return 1;
}

/** How many FONT_UNLOAD events for `font_id` the scene has queued. */
static int
unloads_of(struct ToriDraw_Scene* scene, int font_id)
{
    struct ToriDraw_EventQueue* queue = ToriDraw_SceneEvents(scene);
    int n = 0;
    for( int i = 0; i < queue->count; i++ )
        if( queue->events[i].kind == TORIDRAW_EVENT_FONT_UNLOAD &&
            queue->events[i].texture_id == font_id )
            n++;
    return n;
}

/** Free the masks of a face Rasterise wrote into a caller's struct. */
static void
font_masks_free(struct ToriDraw_Font* font)
{
    for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
    {
        free(font->glyph_alpha[gi]);
        font->glyph_alpha[gi] = NULL;
    }
}

/* TORIRS_CHROME_SCALE=1.5 through to the registered faces: the atlas the
 * renderers get at q = 6 is the bake's vector atlas, on the row table's
 * baseline. */
static void
test_faces_at_one_and_a_half(void)
{
    struct BridgeFixture f;
    int const q = ToriRSChromeScaleQ_Parse("1.5");
    int any_coverage = 0;

    CHECK(q == 6, "TORIRS_CHROME_SCALE '1.5' parses to q 6, got %d", q);
    fixture_open(&f);
    CHECK(UITreeSceneBridge_ChromeScaleQ(&f.bridge) == 4, "a new bridge is at 1x");
    UITreeSceneBridge_SetChromeScaleQ(&f.bridge, q);
    CHECK(UITreeSceneBridge_ChromeScaleQ(&f.bridge) == 6, "the bridge holds q 6");
    CHECK(UITreeSceneBridge_ChromeScale(&f.bridge) == 2, "its tier is the one at or above, 2");
    CHECK(ToriRSChromeBake_Prepared(6), "setting the scale prepared the bake at q 6");

    for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
    {
        struct ToriDraw_Font const* src = ToriRSChromeFont_Tier(slot, 1);
        int const ascent = ToriRSChrome_FontLineHeight(slot, 1);
        int rows[33];
        int id = UITreeSceneBridge_EnsureDebugFont(&f.bridge, slot);
        struct ToriDraw_Font const* font = ToriDraw_SceneFontGet(f.scene, id);
        int atlas_h = 0;
        int glyph_rows_ok = 1;

        CHECK(id == UITREE_SCENE_DEBUG_FONT_Q_ID(slot, 6), "%s at q 6 is under its q 6 id, got 0x%x",
              k_face_name[slot], id);
        CHECK(UITreeSceneBridge_EnsureDebugFont(&f.bridge, slot) == id,
              "%s: asking again is the same entry", k_face_name[slot]);
        CHECK(font != NULL, "%s at q 6 is registered", k_face_name[slot]);
        if( !font )
            continue;
        CHECK(fonts_equal(font, ToriRSChromeBake_Face(slot, 6)),
              "%s: the registered pixels are the bake's q 6 atlas", k_face_name[slot]);
        CHECK(fonts_equal(font, ToriRSChromeBake_FaceSmooth(slot, 6)),
              "%s: at a fractional q that is the vector atlas", k_face_name[slot]);
        CHECK(font->coverage == 1, "%s at q 6 says its bytes are coverage", k_face_name[slot]);
        {
            struct ToriDraw_Font vector;
            struct ToriDraw_Font cells;
            ToriRSChromeGlyphs_Rasterise(slot, 6, 0, &vector);
            ToriRSChromeGlyphs_RasteriseCells(src, ascent, 6, &cells);
            CHECK(fonts_equal(font, &vector),
                  "%s: the registered atlas is Rasterise(q 6), bitmap mode: the vector face",
                  k_face_name[slot]);
            CHECK(!fonts_equal(font, &cells), "%s: and not the 1x cells scaled", k_face_name[slot]);
            font_masks_free(&vector);
            font_masks_free(&cells);
        }

        ToriRSChromeGlyphs_Rows(ascent, 6, rows, 33);
        CHECK(font->line_height == rows[ascent], "%s: line height %d is the table's baseline row %d",
              k_face_name[slot], font->line_height, rows[ascent]);
        for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
        {
            int const oy = src->offset_y[gi];
            int const h = src->glyph_height[gi];
            if( !src->glyph_alpha[gi] || h <= 0 || src->glyph_width[gi] <= 0 || !font->glyph_alpha[gi] )
                continue;
            atlas_h += font->glyph_height[gi];
            /* Standing on the 1x baseline: ends on the device baseline row. */
            if( oy + h == ascent && font->offset_y[gi] + font->glyph_height[gi] != rows[ascent] )
                glyph_rows_ok = 0;
            for( int p = 0; p < font->glyph_width[gi] * font->glyph_height[gi]; p++ )
                if( font->glyph_alpha[gi][p] != 0 && font->glyph_alpha[gi][p] != 255 )
                    any_coverage = 1;
        }
        CHECK(glyph_rows_ok, "%s: every glyph on the 1x baseline ends on the baseline row",
              k_face_name[slot]);
        CHECK(atlas_h > 0, "%s: the atlas has rows", k_face_name[slot]);
    }
    CHECK(any_coverage, "the q 6 atlases carry antialiased coverage");

    /* The 1x faces a component names stay 1x under any chrome scale. */
    CHECK(UITreeSceneBridge_EnsureDebugFont1x(&f.bridge, TORIRS_CHROME_FONT_BODY) ==
              UITREE_SCENE_DEBUG_FONT_BODY_ID,
          "EnsureDebugFont1x is the 1x Body whatever the scale");
    fixture_close(&f);
}

/* A stem is even: Body's 'l' and 'I' and Small's 'l' are one upright, and in
 * the registered q pixels every row of its middle third is the same bytes
 * (the vector face's vertical edges are straight, so the stem is one profile
 * all the way down), and it reads as ink: its peak is 250 or more where the
 * stem covers a device column whole (q 6), 200 or more where a 1.25- or
 * 1.75-pixel stem straddles two (q 5, 7; the stem darkening lifts it). */
static void
check_stem(struct ToriDraw_Font const* src, struct ToriDraw_Font const* font, int q, char ch, char const* face)
{
    int const gi = (unsigned char)src->charcodeset[(unsigned char)ch];
    int even_rows = 0;
    int peak = 0;
    int dw;
    int dh;
    uint8_t const* first;

    CHECK(gi < TORIDRAW_FONT_GLYPH_COUNT && font->glyph_alpha[gi], "%s '%c' is a glyph", face, ch);
    if( gi >= TORIDRAW_FONT_GLYPH_COUNT || !font->glyph_alpha[gi] )
        return;
    dw = font->glyph_width[gi];
    dh = font->glyph_height[gi];
    /* The middle third of the device rows: clear of serifs and the ends. */
    first = font->glyph_alpha[gi] + (size_t)(dh / 3) * (size_t)dw;
    for( int y = dh / 3; y < dh - dh / 3; y++ )
    {
        uint8_t const* row = font->glyph_alpha[gi] + (size_t)y * (size_t)dw;
        for( int x = 0; x < dw; x++ )
            if( row[x] > peak )
                peak = row[x];
        if( memcmp(row, first, (size_t)dw) == 0 )
            even_rows++;
        else
            printf("  %s '%c' q %d row %d differs from row %d\n", face, ch, q, y, dh / 3);
    }
    CHECK(even_rows == dh - 2 * (dh / 3), "%s '%c' at q %d: its stem is the same pixels on every middle row (%d of %d rows)",
          face, ch, q, even_rows, dh - 2 * (dh / 3));
    CHECK(peak >= (q % 2 == 0 ? 250 : 200), "%s '%c' at q %d: its stem reads as ink (peak %d)", face, ch, q,
          peak);
    (void)src;
}

static void
test_stems_are_even(void)
{
    struct BridgeFixture f;

    fixture_open(&f);
    for( int q = 5; q <= 7; q++ )
    {
        struct ToriDraw_Font const* body;
        struct ToriDraw_Font const* small;
        UITreeSceneBridge_SetChromeScaleQ(&f.bridge, q);
        body = ToriDraw_SceneFontGet(
            f.scene, UITreeSceneBridge_EnsureDebugFont(&f.bridge, TORIRS_CHROME_FONT_BODY));
        small = ToriDraw_SceneFontGet(
            f.scene, UITreeSceneBridge_EnsureDebugFont(&f.bridge, TORIRS_CHROME_FONT_SMALL));
        CHECK(body && small, "q %d: Body and Small registered", q);
        if( !body || !small )
            continue;
        check_stem(ToriRSChromeFont_Tier(TORIRS_CHROME_FONT_BODY, 1), body, q, 'l', "Body");
        check_stem(ToriRSChromeFont_Tier(TORIRS_CHROME_FONT_BODY, 1), body, q, 'I', "Body");
        check_stem(ToriRSChromeFont_Tier(TORIRS_CHROME_FONT_SMALL, 1), small, q, 'l', "Small");
    }
    fixture_close(&f);
}

/* The tiers: the very faces and skin the bake carries, under the ids a
 * renderer already knows. q 4 is the 1x draw it always was. */
static void
test_tiers_are_the_bake(void)
{
    struct BridgeFixture f;
    static int const k_tier_q[3] = { 4, 8, 12 };

    fixture_open(&f);
    for( int t = 0; t < 3; t++ )
    {
        int const q = k_tier_q[t];
        int skin;

        UITreeSceneBridge_SetChromeScaleQ(&f.bridge, q);
        /* A new bridge is already at 1x, so setting 4 moves nothing and
         * prepares nothing; the comparisons below read the bake at q. */
        ToriRSChromeBake_Prepare(q);
        for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
        {
            int const id = UITreeSceneBridge_EnsureDebugFont(&f.bridge, slot);
            int const want = q == 4 ? (slot == TORIRS_CHROME_FONT_SMALL  ? UITREE_SCENE_DEBUG_FONT_SMALL_ID
                                       : slot == TORIRS_CHROME_FONT_MENU ? UITREE_SCENE_DEBUG_FONT_MENU_ID
                                                                         : UITREE_SCENE_DEBUG_FONT_BODY_ID)
                                    : UITREE_SCENE_DEBUG_FONT_SCALED_ID(slot, q / 4);
            struct ToriDraw_Font const* font = ToriDraw_SceneFontGet(f.scene, id);
            CHECK(id == want, "q %d %s: the tier's historic id 0x%x, got 0x%x", q, k_face_name[slot],
                  want, id);
            CHECK(font && fonts_equal(font, ToriRSChromeFont_Tier(slot, q / 4)),
                  "q %d %s: the registered pixels are the baked tier's", q, k_face_name[slot]);
            CHECK(font && fonts_equal(font, ToriRSChromeBake_Face(slot, q)),
                  "q %d %s: and ToriRSChromeBake_Face's", q, k_face_name[slot]);
            CHECK(font && font->coverage == 0, "q %d %s: a tier is a mask", q, k_face_name[slot]);
        }
        skin = UITreeSceneBridge_EnsureChromeSkin(&f.bridge);
        CHECK(skin == UITREE_SCENE_CHROME_SKIN_ID, "q %d: the skin's id", q);
        CHECK(skin_frames_are(&f, skin, q, 1), "q %d: every skin frame is the baked tier %d's pixels", q,
              q / 4);
    }
    fixture_close(&f);
}

/* The skin at 1.5x, and what moving the scale does to the scene. */
static void
test_skin_and_moves(void)
{
    struct BridgeFixture f;
    int skin;
    int count = 0;
    struct ToriDraw_Sprite** sprites;
    int one_x;

    fixture_open(&f);
    UITreeSceneBridge_SetChromeScaleQ(&f.bridge, 6);
    skin = UITreeSceneBridge_EnsureChromeSkin(&f.bridge);
    CHECK(skin == UITREE_SCENE_CHROME_SKIN_ID, "q 6: the skin's id is the one id");
    CHECK(skin_frames_are(&f, skin, 6, 0), "q 6: every skin frame is ToriRSChromeBake_Skin's image");
    sprites = ToriDraw_SceneSpriteGet(f.scene, skin, &count);
    CHECK(sprites && count > TORIRS_CHROME_SKIN_PANEL_BODY, "q 6: the parchment is a frame");
    if( sprites && count > TORIRS_CHROME_SKIN_PANEL_BODY )
        CHECK(sprites[TORIRS_CHROME_SKIN_PANEL_BODY]->width == ToriRSChrome_ScalePx(6, 88) &&
                  sprites[TORIRS_CHROME_SKIN_PANEL_BODY]->height == ToriRSChrome_ScalePx(6, 60),
              "q 6: the parchment is %dx%d, got %dx%d", ToriRSChrome_ScalePx(6, 88),
              ToriRSChrome_ScalePx(6, 60), sprites[TORIRS_CHROME_SKIN_PANEL_BODY]->width,
              sprites[TORIRS_CHROME_SKIN_PANEL_BODY]->height);

    /* A component's chrome art stays 1x. */
    one_x = UITreeSceneBridge_EnsureChromeSkin1x(&f.bridge);
    CHECK(one_x == UITREE_SCENE_CHROME_SKIN_1X_ID, "the 1x skin has its own id");
    CHECK(skin_frames_are(&f, one_x, 4, 1), "the 1x skin is the 1x bake at q 6");

    for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
        (void)UITreeSceneBridge_EnsureDebugFont(&f.bridge, slot);
    ToriDraw_SceneFrameEnd(f.scene);

    /* 1.5x -> 1.75x: q 6's faces go, by unload events; the skin follows. */
    UITreeSceneBridge_SetChromeScaleQ(&f.bridge, 7);
    for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
    {
        int const old_id = UITREE_SCENE_DEBUG_FONT_Q_ID(slot, 6);
        CHECK(!ToriDraw_SceneFontHas(f.scene, old_id), "q 6 -> 7: %s at q 6 is dropped",
              k_face_name[slot]);
        CHECK(unloads_of(f.scene, old_id) == 1, "q 6 -> 7: %s at q 6 is unloaded once, got %d",
              k_face_name[slot], unloads_of(f.scene, old_id));
    }
    CHECK(skin_frames_are(&f, UITREE_SCENE_CHROME_SKIN_ID, 7, 0),
          "q 6 -> 7: the skin entry now holds the q 7 images");
    CHECK(skin_frames_are(&f, one_x, 4, 1), "q 6 -> 7: the 1x skin is untouched");
    CHECK(UITreeSceneBridge_EnsureDebugFont(&f.bridge, TORIRS_CHROME_FONT_BODY) ==
              UITREE_SCENE_DEBUG_FONT_Q_ID(TORIRS_CHROME_FONT_BODY, 7),
          "q 7: Body resolves to its q 7 id");
    ToriDraw_SceneFrameEnd(f.scene);

    /* 1.75x -> 2x: the fraction's faces go, the tier's come. Setting the same
     * scale again moves nothing. */
    UITreeSceneBridge_SetChromeScaleQ(&f.bridge, 8);
    CHECK(!ToriDraw_SceneFontHas(f.scene, UITREE_SCENE_DEBUG_FONT_Q_ID(TORIRS_CHROME_FONT_BODY, 7)),
          "q 7 -> 8: Body at q 7 is dropped");
    CHECK(skin_frames_are(&f, UITREE_SCENE_CHROME_SKIN_ID, 8, 1), "q 7 -> 8: the skin is the 2x tier");
    ToriDraw_SceneFrameEnd(f.scene);
    UITreeSceneBridge_SetChromeScaleQ(&f.bridge, 8);
    CHECK(ToriDraw_SceneEvents(f.scene)->count == 0, "setting q 8 again queues no event, got %d",
          ToriDraw_SceneEvents(f.scene)->count);
    fixture_close(&f);
}

/* "Smooth always": the vector atlas at a whole q too, registered once per q
 * and dropped again when the mode goes back to bitmap; a fractional q draws
 * the vector face either way. */
static void
test_smooth_always(void)
{
    struct BridgeFixture f;
    int builds;

    fixture_open(&f);
    CHECK(UITreeSceneBridge_ChromeTextSmooth(&f.bridge) == 0, "a new bridge draws bitmap tiers");
    CHECK(UITreeSceneBridge_EnsureDebugFont(&f.bridge, TORIRS_CHROME_FONT_BODY) ==
              UITREE_SCENE_DEBUG_FONT_BODY_ID,
          "q 4, bitmap: Body is the 1x tier");
    ToriDraw_SceneFrameEnd(f.scene);

    builds = ToriRSChromeBake_GlyphAtlasBuilds();
    UITreeSceneBridge_SetChromeTextSmooth(&f.bridge, 1);
    for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
    {
        int const id = UITreeSceneBridge_EnsureDebugFont(&f.bridge, slot);
        struct ToriDraw_Font const* font = ToriDraw_SceneFontGet(f.scene, id);
        CHECK(id == UITREE_SCENE_DEBUG_FONT_Q_ID(slot, 4), "q 4, smooth: %s under its q 4 id, got 0x%x",
              k_face_name[slot], id);
        CHECK(font && fonts_equal(font, ToriRSChromeBake_FaceSmooth(slot, 4)),
              "q 4, smooth: %s is the vector atlas at q 4", k_face_name[slot]);
        CHECK(font && font->coverage == 1, "q 4, smooth: %s says its bytes are coverage",
              k_face_name[slot]);
        CHECK(font && !fonts_equal(font, ToriRSChromeFont_Tier(slot, 1)),
              "q 4, smooth: %s is not the bitmap tier", k_face_name[slot]);
    }
    CHECK(ToriRSChromeBake_GlyphAtlasBuilds() - builds <= TORIRS_CHROME_FONT_SLOT_COUNT,
          "turning smooth on builds at most one atlas per face (%d)",
          ToriRSChromeBake_GlyphAtlasBuilds() - builds);
    ToriDraw_SceneFrameEnd(f.scene);

    /* Setting the same mode again moves nothing. */
    UITreeSceneBridge_SetChromeTextSmooth(&f.bridge, 1);
    CHECK(ToriDraw_SceneEvents(f.scene)->count == 0, "smooth again queues no event");

    /* Back to bitmap: q 4's vector faces go by unload events; the tiers answer. */
    UITreeSceneBridge_SetChromeTextSmooth(&f.bridge, 0);
    for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
    {
        int const old_id = UITREE_SCENE_DEBUG_FONT_Q_ID(slot, 4);
        CHECK(!ToriDraw_SceneFontHas(f.scene, old_id), "smooth -> bitmap: %s at q 4 is dropped",
              k_face_name[slot]);
        CHECK(unloads_of(f.scene, old_id) == 1, "smooth -> bitmap: %s at q 4 is unloaded once, got %d",
              k_face_name[slot], unloads_of(f.scene, old_id));
    }
    CHECK(UITreeSceneBridge_EnsureDebugFont(&f.bridge, TORIRS_CHROME_FONT_MENU) ==
              UITREE_SCENE_DEBUG_FONT_MENU_ID,
          "smooth -> bitmap: Menu is the 1x tier again");
    ToriDraw_SceneFrameEnd(f.scene);

    /* On again: the bake's raster is cached, so nothing is rebuilt. */
    builds = ToriRSChromeBake_GlyphAtlasBuilds();
    UITreeSceneBridge_SetChromeTextSmooth(&f.bridge, 1);
    for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
        (void)UITreeSceneBridge_EnsureDebugFont(&f.bridge, slot);
    CHECK(ToriRSChromeBake_GlyphAtlasBuilds() == builds, "smooth on again rebuilds no atlas");

    /* 2x under smooth: q 8's vector atlas; leaving q 8 drops it. */
    UITreeSceneBridge_SetChromeScaleQ(&f.bridge, 8);
    CHECK(!ToriDraw_SceneFontHas(f.scene, UITREE_SCENE_DEBUG_FONT_Q_ID(TORIRS_CHROME_FONT_BODY, 4)),
          "q 4 -> 8, smooth: q 4's Body is dropped");
    CHECK(UITreeSceneBridge_EnsureDebugFont(&f.bridge, TORIRS_CHROME_FONT_BODY) ==
              UITREE_SCENE_DEBUG_FONT_Q_ID(TORIRS_CHROME_FONT_BODY, 8),
          "q 8, smooth: Body under its q 8 id");
    CHECK(fonts_equal(
              ToriDraw_SceneFontGet(
                  f.scene, UITREE_SCENE_DEBUG_FONT_Q_ID(TORIRS_CHROME_FONT_BODY, 8)),
              ToriRSChromeBake_FaceSmooth(TORIRS_CHROME_FONT_BODY, 8)),
          "q 8, smooth: Body is the vector atlas at q 8");

    /* 1.5x: the vector face whatever the mode; a toggle moves nothing. */
    UITreeSceneBridge_SetChromeScaleQ(&f.bridge, 6);
    CHECK(UITreeSceneBridge_EnsureDebugFont(&f.bridge, TORIRS_CHROME_FONT_BODY) ==
              UITREE_SCENE_DEBUG_FONT_Q_ID(TORIRS_CHROME_FONT_BODY, 6),
          "q 6, smooth: Body under its q 6 id");
    ToriDraw_SceneFrameEnd(f.scene);
    UITreeSceneBridge_SetChromeTextSmooth(&f.bridge, 0);
    CHECK(ToriDraw_SceneEvents(f.scene)->count == 0, "q 6: smooth -> bitmap queues no event, got %d",
          ToriDraw_SceneEvents(f.scene)->count);
    CHECK(UITreeSceneBridge_EnsureDebugFont(&f.bridge, TORIRS_CHROME_FONT_BODY) ==
              UITREE_SCENE_DEBUG_FONT_Q_ID(TORIRS_CHROME_FONT_BODY, 6),
          "q 6, bitmap: Body is still the vector atlas under its q 6 id");
    fixture_close(&f);
}

/* The software blitter draws a coverage atlas by its coverage: drawn white on
 * black, each pixel of a q 6 glyph is the grey of its byte (within the
 * blend's rounding) -- a 64 a quarter tone, not solid ink -- and a mask face
 * still draws pure white. */
static int
draw_glyph_matches(struct ToriDraw_Font* font, char ch, int* out_partial)
{
    enum
    {
        W = 64,
        H = 64
    };
    static toripixel_t pixels[W * H];
    struct ToriDraw_ViewPort vp;
    char text[2] = { ch, '\0' };
    int const gi = (unsigned char)font->charcodeset[(unsigned char)ch];
    int min_x = W;
    int min_y = H;
    int ok = 1;

    memset(pixels, 0, sizeof(pixels));
    memset(&vp, 0, sizeof(vp));
    vp.width = W;
    vp.height = H;
    vp.stride = W;
    vp.clip_right = W;
    vp.clip_bottom = H;
    (void)ToriDraw2D_DrawString(font, &vp, 8, 40, text, 0xFFFFFF, false, 0, pixels);

    for( int y = 0; y < H; y++ )
        for( int x = 0; x < W; x++ )
            if( toripixel_to_argb8888(pixels[y * W + x]) & 0xFFFFFFu )
            {
                if( x < min_x )
                    min_x = x;
                if( y < min_y )
                    min_y = y;
            }
    *out_partial = 0;
    if( min_x == W )
        return 0;
    /* The glyph's first inked column and row are where its mask's are; walk
     * the mask from there. */
    {
        int gx0 = font->glyph_width[gi];
        int gy0 = font->glyph_height[gi];
        for( int y = 0; y < font->glyph_height[gi]; y++ )
            for( int x = 0; x < font->glyph_width[gi]; x++ )
                if( font->glyph_alpha[gi][y * font->glyph_width[gi] + x] )
                {
                    if( x < gx0 )
                        gx0 = x;
                    if( y < gy0 )
                        gy0 = y;
                }
        for( int y = 0; y < font->glyph_height[gi]; y++ )
            for( int x = 0; x < font->glyph_width[gi]; x++ )
            {
                int const px = min_x + x - gx0;
                int const py = min_y + y - gy0;
                int const a = font->glyph_alpha[gi][y * font->glyph_width[gi] + x];
                int grey;
                if( px < 0 || py < 0 || px >= W || py >= H )
                {
                    if( a )
                        ok = 0;
                    continue;
                }
                grey = (int)(toripixel_to_argb8888(pixels[py * W + px]) & 0xFF);
                if( a > 0 && a < 255 && grey > 0 && grey < 255 )
                    (*out_partial)++;
                if( grey < a - 2 || grey > a + 2 )
                    ok = 0;
            }
    }
    return ok;
}

static void
test_soft_blend(void)
{
    struct BridgeFixture f;
    struct ToriDraw_Font* body;
    int partial = 0;
    int matched;

    fixture_open(&f);
    UITreeSceneBridge_SetChromeScaleQ(&f.bridge, 6);
    body = ToriDraw_SceneFontGet(
        f.scene, UITreeSceneBridge_EnsureDebugFont(&f.bridge, TORIRS_CHROME_FONT_BODY));
    CHECK(body != NULL, "q 6 Body registered");
    if( body )
    {
        /* Every letter and digit, and the ones whose atlas carries a partial
         * byte (a bar's end, a diagonal's step) must show it as a tone. */
        int glyphs = 0;
        int toned = 0;
        int all_matched = 1;
        for( int c = '0'; c <= 'z'; c++ )
        {
            int const gi = (unsigned char)body->charcodeset[c];
            if( !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) )
                continue;
            if( gi >= TORIDRAW_FONT_GLYPH_COUNT || !body->glyph_alpha[gi] )
                continue;
            matched = draw_glyph_matches(body, (char)c, &partial);
            if( !matched )
            {
                printf("  q 6 Body '%c' does not draw its coverage\n", c);
                all_matched = 0;
            }
            glyphs++;
            if( partial > 0 )
                toned++;
        }
        CHECK(glyphs > 50, "q 6 Body: %d letters and digits drawn", glyphs);
        CHECK(all_matched, "q 6 Body: every glyph draws the grey of every coverage byte");
        CHECK(toned > 0, "q 6 Body: %d glyphs draw blended edge pixels", toned);
        printf("  q 6 Body: %d of %d letters and digits draw coverage tones\n", toned, glyphs);
    }
    UITreeSceneBridge_SetChromeScaleQ(&f.bridge, 4);
    body = ToriDraw_SceneFontGet(
        f.scene, UITreeSceneBridge_EnsureDebugFont(&f.bridge, TORIRS_CHROME_FONT_BODY));
    CHECK(body != NULL, "1x Body registered");
    if( body )
    {
        matched = draw_glyph_matches(body, 'v', &partial);
        CHECK(matched && partial == 0, "1x Body 'v' still draws solid (a mask), %d partial", partial);
    }
    fixture_close(&f);
}

int
main(void)
{
    printf("uitree_scene_bridge_chrome_test\n");
    test_faces_at_one_and_a_half();
    test_stems_are_even();
    test_smooth_always();
    test_tiers_are_the_bake();
    test_skin_and_moves();
    test_soft_blend();

    if( g_failures )
    {
        printf("FAILED (%d of %d checks)\n", g_failures, g_checks);
        return 1;
    }
    printf("PASS (%d checks)\n", g_checks);
    return 0;
}
