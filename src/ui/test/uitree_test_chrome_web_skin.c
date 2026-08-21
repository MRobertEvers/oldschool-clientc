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
    }

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("chrome web skin: ok\n");
    return 0;
}
