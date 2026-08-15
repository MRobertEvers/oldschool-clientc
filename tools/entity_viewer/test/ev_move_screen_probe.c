/*
 * Do the movement keys move the image the way the SCREEN says they should?
 *
 * The earlier unit test asserted a convention I had assumed — that yaw rotates
 * the camera — and passed while the keys felt wrong. This one asserts nothing
 * about the convention: it renders, steps, renders again, and measures where
 * the subject actually went. Whatever the projection does internally, these are
 * the properties a user judges the keys by:
 *
 *   D (right)   -> the subject moves LEFT on screen
 *   W (forward) -> the subject gets BIGGER (the viewpoint approaches)
 *   R (up)      -> the subject moves DOWN on screen
 *
 * and each must hold at EVERY yaw, because "correct at yaw 0 and inverted at
 * 180" is exactly the bug being chased.
 */
#include "ev_build.h"
#include "ev_render.h"
#include "ev_wire.h"
#include "asset_access.h"
#include "tool_profile.h"
#include "toridraw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 256
#define H 256

/* Centroid and area of the drawn pixels. */
static void
measure(const unsigned char* rgba, double* out_cx, double* out_cy, int* out_area)
{
    double sx = 0, sy = 0;
    int n = 0;
    for( int y = 0; y < H; y++ )
        for( int x = 0; x < W; x++ )
        {
            const unsigned char* p = rgba + (y * W + x) * 4;
            if( p[0] == 0x14 && p[1] == 0x18 && p[2] == 0x21 )
                continue; /* background */
            sx += x;
            sy += y;
            n++;
        }
    *out_area = n;
    *out_cx = n ? sx / n : 0;
    *out_cy = n ? sy / n : 0;
}

static int fails = 0;

static void
expect(const char* what, int yaw, double delta, int want_sign, double min_mag)
{
    int ok = (want_sign > 0 ? delta > min_mag : delta < -min_mag);
    if( !ok )
        fails++;
    printf("    yaw %-5d %-28s delta=%+8.2f  %s\n", yaw, what, delta, ok ? "ok" : "FAIL");
}

int
main(int argc, char** argv)
{
    struct RSCache profile;
    struct Tool_Dat2Cache cache;
    int npc_id = argc > 3 ? atoi(argv[3]) : 1;

    ToriDraw_Init();
    if( !tool_resolve_profile(argv[2], NULL, NULL, NULL, NULL, &profile) ) return 1;
    if( !tool_dat2_open(argv[1], &profile, &cache) ) return 1;

    struct ToriDraw_Model* model = ev_build_npc_model(&cache, npc_id);
    if( !model ) { printf("no model\n"); return 1; }

    struct EV_WireBuf wb = { 0 };
    ev_wire_write_model(&wb, model);
    ToriDraw_ModelFree(model);

    ev_init();
    ev_set_model(wb.data, (int)wb.len);
    int zoom = ev_model_height() * 3;
    if( zoom < 400 ) zoom = 400;
    const int pitch = 200;
    const int step = zoom / 8;

    static const int YAWS[] = { 0, 256, 512, 768, 1024, 1280, 1536, 1792 };

    unsigned char base[W * H * 4];

    for( unsigned k = 0; k < sizeof(YAWS) / sizeof(YAWS[0]); k++ )
    {
        int yaw = YAWS[k];
        double cx0, cy0, cx1, cy1;
        int a0, a1;

        printf("  --- yaw %d ---\n", yaw);

        /* D: step right. */
        ev_move_reset();
        memcpy(base, ev_render(W, H, yaw, pitch, zoom, 0), sizeof(base));
        measure(base, &cx0, &cy0, &a0);
        ev_move(0, step, 0);
        measure(ev_render(W, H, yaw, pitch, zoom, 0), &cx1, &cy1, &a1);
        expect("D -> subject moves left", yaw, cx1 - cx0, -1, 1.0);

        /* W: step forward. The subject should grow; the centroid is not the
         * measure here because approaching a centred subject barely moves it. */
        ev_move_reset();
        memcpy(base, ev_render(W, H, yaw, pitch, zoom, 0), sizeof(base));
        measure(base, &cx0, &cy0, &a0);
        ev_move(step, 0, 0);
        measure(ev_render(W, H, yaw, pitch, zoom, 0), &cx1, &cy1, &a1);
        expect("W -> subject grows", yaw, (double)(a1 - a0), +1, 1.0);

        /* R: step up. */
        ev_move_reset();
        memcpy(base, ev_render(W, H, yaw, pitch, zoom, 0), sizeof(base));
        measure(base, &cx0, &cy0, &a0);
        ev_move(0, 0, step);
        measure(ev_render(W, H, yaw, pitch, zoom, 0), &cx1, &cy1, &a1);
        expect("R -> subject moves down", yaw, cy1 - cy0, +1, 1.0);
    }

    printf("%s\n", fails ? "FAILURES" : "all screen-direction checks pass");
    return fails ? 1 : 0;
}
