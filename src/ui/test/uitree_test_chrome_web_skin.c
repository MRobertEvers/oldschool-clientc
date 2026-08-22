/*
 * The baked skin, as the web executor hands it to the page.
 *
 * WHAT THIS IS FOR. `chrome_web_sprite_b64` shuffles the bake's 0xAARRGGBB
 * words into the R,G,B,A byte stream an ImageData wants, and base64s the
 * result. Get that shuffle wrong and NOTHING fails: the page decodes the right
 * number of bytes, puts them on a canvas, and the window comes up wearing a
 * checkbox whose red and blue are swapped -- a blue tick and a cyan cross,
 * which reads as a theme rather than as a bug. The node test on the other side
 * of the wall cannot see it either, because it feeds itself made-up bytes.
 *
 * So this drives the REAL encoder over the REAL bake and decodes it back, and
 * asserts on what the pixels mean: the ON slot is a green tick, the OFF slot is
 * a red cross. Those two are the whole point of the byte order, and they are
 * the same property `visual_checkbox_skinned` asserts about the in-canvas path.
 *
 * The executor is #included rather than linked. It is a web-lane translation
 * unit whose every EM_JS body is dead code off that lane (the file stubs the
 * macro itself for exactly this), and the function under test is static -- so
 * including it is what makes it reachable at all, and it costs nothing else.
 *
 * Run: make -C src test-chrome-web-skin
 */

#include "ui/torirs_chrome_skin.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Both are provided by the executor's own non-emscripten fallback, and have to
 * be in scope before it is included. */
#include "ui/torirs_chrome_exec_web.c"

static int g_failures;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);                        \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/** base64 -> bytes. The page uses atob(); this is that, to check the round
 *  trip rather than to trust it. */
static int
unbase64(char const* in, unsigned char* out, int cap)
{
    static char const* const k_alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned acc = 0;
    int bits = 0;
    int n = 0;

    for( int i = 0; in[i]; i++ )
    {
        char const* at;
        if( in[i] == '=' )
            break;
        at = strchr(k_alphabet, in[i]);
        if( !at )
            return -1;
        acc = (acc << 6) | (unsigned)(at - k_alphabet);
        bits += 6;
        if( bits < 8 )
            continue;
        bits -= 8;
        if( n >= cap )
            return -1;
        out[n++] = (unsigned char)((acc >> bits) & 0xFF);
    }
    return n;
}

/**
 * Is this slot's art green-dominant, or red-dominant?
 *
 * Counted over the OPAQUE pixels only: both sprites are a mark inside a ring on
 * a transparent field, and the ring is near-black in both, so a test that
 * averaged the whole image would find the ring and not the mark.
 */
static void
mark_hues(int slot, int* green_out, int* red_out, int* opaque_out)
{
    struct ToriRSChromeSkin_Sprite const* spr = ToriRSChromeSkin_ForSlot(slot);
    unsigned char* bytes;
    char* b64;
    int decoded;

    *green_out = 0;
    *red_out = 0;
    *opaque_out = 0;
    if( !spr )
        return;

    b64 = chrome_web_sprite_b64(spr);
    bytes = malloc((size_t)spr->w * (size_t)spr->h * 4);
    assert(bytes);
    decoded = unbase64(b64, bytes, spr->w * spr->h * 4);
    CHECK(decoded == spr->w * spr->h * 4, "the encoding round-trips to the pixel count");

    for( int i = 0; i < spr->w * spr->h; i++ )
    {
        unsigned const r = bytes[i * 4 + 0];
        unsigned const g = bytes[i * 4 + 1];
        unsigned const b = bytes[i * 4 + 2];
        unsigned const a = bytes[i * 4 + 3];

        if( a < 200 )
            continue;
        (*opaque_out)++;
        if( g > r + 30 && g > b + 30 )
            (*green_out)++;
        if( r > g + 30 && r > b + 30 )
            (*red_out)++;
    }
    free(bytes);
    free(b64);
}

int
main(void)
{
    printf("chrome web skin: the bake, as the page receives it\n");

    if( !ToriRSChromeSkin_Available() )
    {
        /* Not a failure. A lane can be built with the skin stubbed out, and the
         * page's own fallback is the flat sheet -- there is simply nothing to
         * check here. Said out loud so a green run cannot be mistaken for
         * coverage that did not happen. */
        printf("  no skin baked into this build; nothing to check\n");
        return 0;
    }

    /* Every slot the executor sends must actually be in the bake. A slot named
     * in that list and missing from the bake is a sprite the page waits for
     * forever, which shows up as a permanently flat window. */
    for( int i = 0; i < (int)(sizeof(k_web_skin_slots) / sizeof(k_web_skin_slots[0])); i++ )
    {
        struct ToriRSChromeSkin_Sprite const* spr =
            ToriRSChromeSkin_ForSlot(k_web_skin_slots[i]);
        CHECK(spr != NULL, "every slot the page is sent is in the bake");
        if( spr )
            CHECK(spr->w > 0 && spr->h > 0, "and every one of them has pixels");
    }

    /*
     * The window X is SENT, not merely named.
     *
     * The page has had a `SKIN.CLOSE` entry in its slot table for as long as
     * the enum has, and the client did not send it -- so the title bar wore a
     * system-font glyph beside a window of baked art, and nothing anywhere
     * failed. A slot the page knows and the client never sends is exactly the
     * shape of that: the enum-sync test sees the two tables agree, the loop
     * above sees every slot it IS sent, and neither can see the gap between.
     */
    {
        int sends_close = 0;
        int sends_over = 0;

        for( int i = 0; i < (int)(sizeof(k_web_skin_slots) / sizeof(k_web_skin_slots[0])); i++ )
        {
            if( k_web_skin_slots[i] == TORIRS_CHROME_SKIN_CLOSE )
                sends_close = 1;
            if( k_web_skin_slots[i] == TORIRS_CHROME_SKIN_CLOSE_OVER )
                sends_over = 1;
        }
        CHECK(sends_close, "the page is sent the window X the other presentations wear");
        CHECK(sends_over, "and the hover half of it, which is a second sprite not a filter");
    }

    /*
     * The SYNTHESIZED buttons: the same plate, a different mark.
     *
     * These four have no archive behind them -- spritebake stamps an arrow
     * into the close button's plate (`--stamp`), because the game has no
     * pop-out button to lift. That makes two things worth asserting, and
     * neither is visible from the page:
     *
     *  - the PLATE is untouched. If the stamp wandered outside the middle 8x8
     *    it would eat the bevel, and the button would still look like a
     *    button -- just not like the one beside it.
     *  - the MARK actually changed. A stamp that silently did nothing gives
     *    you a pop-out button wearing an X, which reads as a bug in the page.
     *
     * The rotation is checked the same way: `arrow_sw` is `arrow_ne` turned
     * around, so DockButton's mark must be PopoutButton's, backwards.
     */
    {
        struct ToriRSChromeSkin_Sprite const* plate =
            ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_CLOSE);
        struct ToriRSChromeSkin_Sprite const* pop =
            ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_POPOUT);
        struct ToriRSChromeSkin_Sprite const* dock =
            ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_DOCK);

        CHECK(pop && dock, "the synthesized buttons are in the bake");
        if( plate && pop && dock && plate->w == 16 && pop->w == 16 && dock->w == 16 )
        {
            int const inset = 4;
            int outside_same = 1;
            int inside_diff = 0;
            int rotated = 1;

            for( int y = 0; y < 16; y++ )
                for( int x = 0; x < 16; x++ )
                {
                    int const in_box = x >= inset && x < 16 - inset && y >= inset &&
                                       y < 16 - inset;
                    if( in_box )
                    {
                        if( pop->argb[y * 16 + x] != plate->argb[y * 16 + x] )
                            inside_diff = 1;
                        if( dock->argb[y * 16 + x] !=
                            pop->argb[(15 - y) * 16 + (15 - x)] )
                            rotated = 0;
                    }
                    else if( pop->argb[y * 16 + x] != plate->argb[y * 16 + x] )
                        outside_same = 0;
                }

            CHECK(outside_same, "the stamp left the plate -- frame, bevel and face -- alone");
            CHECK(inside_diff, "and actually replaced the mark in the middle of it");
            CHECK(rotated, "putting it back is the same arrow, turned around");
        }

        {
            int sends_pop = 0;
            int sends_dock = 0;
            for( int i = 0;
                 i < (int)(sizeof(k_web_skin_slots) / sizeof(k_web_skin_slots[0])); i++ )
            {
                if( k_web_skin_slots[i] == TORIRS_CHROME_SKIN_POPOUT )
                    sends_pop = 1;
                if( k_web_skin_slots[i] == TORIRS_CHROME_SKIN_DOCK )
                    sends_dock = 1;
            }
            CHECK(sends_pop, "and the page is sent the pop-out button");
            CHECK(sends_dock, "and the one that puts it back");
        }
    }

    /*
     * The byte order, stated as what the pixels MEAN.
     *
     * R and B swapped would turn the green tick blue and the red cross cyan --
     * and both would still decode, still blit, and still look deliberate. The
     * hue is the only assertion that catches it.
     */
    {
        int green;
        int red;
        int opaque;

        mark_hues(TORIRS_CHROME_SKIN_CHECK_ON, &green, &red, &opaque);
        CHECK(opaque > 100, "the ON slot has a mark on it");
        CHECK(green > 20, "the ON slot decodes to a GREEN tick");
        CHECK(green > red, "and the green outweighs the red");

        mark_hues(TORIRS_CHROME_SKIN_CHECK_OFF, &green, &red, &opaque);
        CHECK(opaque > 100, "the OFF slot has a mark on it");
        CHECK(red > 20, "the OFF slot decodes to a RED cross");
        CHECK(red > green, "and the red outweighs the green");

        /*
         * The other boolean the page can be told to wear.
         *
         * Its OFF is an EMPTY well rather than a second mark, so the pair is
         * asserted the other way round from the one above: what says the two
         * slots are not each other is that one has a green tick in it and the
         * other has no green at all. A bake with them swapped satisfies "both
         * are 18x18 wells" and nothing else here.
         */
        mark_hues(TORIRS_CHROME_SKIN_CHECK_BOX_ON, &green, &red, &opaque);
        CHECK(opaque > 100, "the boxed ON slot is a filled well");
        CHECK(green > 20, "with a GREEN tick in it");
        CHECK(green > red, "and the green outweighs the red");

        mark_hues(TORIRS_CHROME_SKIN_CHECK_BOX_OFF, &green, &red, &opaque);
        CHECK(opaque > 100, "the boxed OFF slot is a well of the same weight");
        CHECK(green < 5, "and it is EMPTY -- no tick, and no cross either");

        {
            int sends_on = 0;
            int sends_off = 0;
            for( int i = 0;
                 i < (int)(sizeof(k_web_skin_slots) / sizeof(k_web_skin_slots[0])); i++ )
            {
                if( k_web_skin_slots[i] == TORIRS_CHROME_SKIN_CHECK_BOX_ON )
                    sends_on = 1;
                if( k_web_skin_slots[i] == TORIRS_CHROME_SKIN_CHECK_BOX_OFF )
                    sends_off = 1;
            }
            /* BOTH pairs cross at open, always: the style command can arrive on
             * any frame and a page that had to wait for a base64 blob would
             * repaint the window in two steps. */
            CHECK(sends_on, "the page is sent the boxed tick");
            CHECK(sends_off, "and the empty well beside it");
        }
    }

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("chrome web skin: ok\n");
    return 0;
}
