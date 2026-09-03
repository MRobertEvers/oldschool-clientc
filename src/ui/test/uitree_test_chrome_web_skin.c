/*
 * The web executor's authored-icon RGBA encoder, checked against baked pixels.
 *
 * WHAT THIS IS FOR. `chrome_web_sprite_b64` shuffles the bake's 0xAARRGGBB
 * words into the R,G,B,A byte stream an ImageData wants, and base64s the
 * result. The immutable theme is packaged as PNG now, but plugin-authored rail
 * icons still cross through this encoder. Get the shuffle wrong and every icon
 * silently swaps red and blue. The node test on the other side cannot see that
 * because it feeds itself made-up bytes.
 *
 * So this drives the REAL encoder over the REAL bake and decodes it back, and
 * asserts on what the pixels mean: the ON slot is a green tick, the OFF slot is
 * a red cross. They are convenient known images for the same encoder an
 * arbitrary authored icon uses.
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
    printf("chrome web pixels: packaged bake and authored-icon encoder\n");

    if( !ToriRSChromeSkin_Available() )
    {
        /* Not a failure. A lane can be built with the skin stubbed out, and the
         * page's own fallback is the flat sheet -- there is simply nothing to
         * check here. Said out loud so a green run cannot be mistaken for
         * coverage that did not happen. */
        printf("  no skin baked into this build; nothing to check\n");
        return 0;
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
    }

    {
        struct ToriRSChromeExec exec = ToriRSChromeExec_Web();
        struct ToriRSChromeRailIntent intents[4];
        ToriRSChromeExecWeb_RequestSelect(5, 17);
        ToriRSChromeExecWeb_RequestSelect(-2, 17);
        ToriRSChromeExecWeb_RequestLayout(17, 320, 480, 2000, 1, 1, 0);
        /* rail_poll is intentionally independent of begin/end: collapsed is
         * exactly when there is no page executor to poll. */
        exec.end(exec.user);
        CHECK(exec.rail_poll(exec.user, intents, 4) == 3,
            "the persistent web rail drains while the page executor is down");
        CHECK(intents[0].plugin_index == 5 && intents[1].plugin_index == -2,
            "web rail events retain concrete plugin and Manage destinations");
        CHECK(intents[2].kind == TORIRS_CHROME_RAIL_INTENT_LAYOUT &&
                  intents[2].selection_generation == 17 && !intents[2].game_visible,
            "web allocation returns generation-fenced exclusive visibility");
    }

    {
        struct ChromeWeb batch;
        struct ToriRSChromeCmd cmd;

        memset(&batch, 0, sizeof(batch));
        memset(&cmd, 0, sizeof(cmd));
        cmd.kind = TORIRS_CHROME_CMD_WIDGET_OPTION;
        cmd.panel = 2;
        cmd.widget = 9;
        cmd.value = 1;
        cmd.x = 0;
        snprintf(cmd.text, sizeof(cmd.text), "%s", "missing/frame");
        snprintf(cmd.label, sizeof(cmd.label), "%s", "Same|label");
        snprintf(cmd.detail, sizeof(cmd.detail), "%s", "Provider is not installed");
        chrome_web_batch_begin(&batch);
        chrome_web_batch_command(&batch, &cmd);
        CHECK(
            batch.batch_json && strstr(batch.batch_json, "\"text\":\"missing/frame\"") &&
                strstr(batch.batch_json, "\"label\":\"Same|label\"") &&
                strstr(
                    batch.batch_json,
                    "\"detail\":\"Provider is not installed\"") &&
                strstr(batch.batch_json, "\"x\":0"),
            "the web executor serializes structured option fields separately");
        free(batch.batch_json);
    }

    {
        struct ChromeWeb web;
        struct ToriRSChromeCustomFrame frame;
        uint32_t pixel = 0xff112233u;

        memset(&web, 0, sizeof(web));
        chrome_web_reset_mounted(&web);
        web.open = 1;
        memset(&frame, 0, sizeof(frame));
        frame.panel = 2;
        frame.widget = 9;
        frame.selection_generation = 7;
        frame.widget_serial = 99;
        frame.scale_milli = 1000;
        frame.width = frame.height = frame.stride = 1;
        frame.argb = &pixel;
        /* Native EM_JS stubs reject the send, giving this test a deterministic
         * transport-loss injection without a browser. */
        chrome_web_custom_present(&web, &frame);
        CHECK(web.snapshot_needed && web.custom_panel[9] == -1 &&
                  web.custom_serial[9] == 0,
            "a rejected custom bitmap clears its fence and requests a page snapshot");
    }

    if( g_failures )
    {
        fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    printf("chrome web skin: ok\n");
    return 0;
}
