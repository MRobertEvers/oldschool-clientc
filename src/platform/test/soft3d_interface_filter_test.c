/*
 * Soft3D's Linear/Bicubic interface filter (soft3d_segment_begin/_end).
 *
 * A 2D segment on a scaled buffer draws 1:1 into a layout-sized layer and is
 * filtered into the buffer at END_2D. What is pinned here:
 *
 *   - Coverage is what the segment DREW, not what differs from the buffer
 *     underneath. The regression: a player standing in the world behind the
 *     character designer matched the panel's player texel for texel, those
 *     texels read as uncovered, and the filter speckled the model.
 *   - Output pixels whose taps all fall inside the drawn rect are exactly its
 *     colour; pixels whose taps cannot reach it are the world, untouched.
 *   - The per-segment cache: an identical second frame composites the same
 *     picture, and a changed colour is re-filtered.
 */
#include "platform/platform_sdl2_renderer_soft3d.h"
#include "render/torirs_render.h"
#include "toridraw.h"
#include "toridraw_scene.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define LAYOUT 10
#define SCALE 3
#define BUF (LAYOUT * SCALE)

static int failures;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s\n", msg);                                                    \
            failures++;                                                                            \
        }                                                                                          \
    } while( 0 )

/* The world: a checker at buffer resolution, one colour of which is the
 * interface's own. */
static void
fill_world(int* pixels, int same)
{
    for( int y = 0; y < BUF; y++ )
        for( int x = 0; x < BUF; x++ )
            pixels[y * BUF + x] = ((x + y) & 1) ? same : 0x00102030;
}

static void
execute(struct ToriRS_Soft3D* soft, enum ToriRS_RenderCommandKind kind)
{
    struct ToriRS_RenderCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = kind;
    ToriRS_Soft3D_Execute(soft, &cmd);
}

/* One frame: the world, then a segment filling layout [3,7) x [3,7). */
static void
frame(struct ToriRS_Soft3D* soft, struct ToriDraw_Scene* scene, int* pixels, int mode, int colour, int world_same)
{
    struct ToriRS_RenderCommand cmd;

    fill_world(pixels, world_same);
    ToriRS_Soft3D_Init(soft, scene, pixels, BUF, BUF);
    ToriRS_Soft3D_SetLayout(soft, LAYOUT, LAYOUT);
    ToriRS_Soft3D_SetInterfaceScaleMode(soft, mode);
    execute(soft, TORIRSRC_BEGIN_2D);
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = TORIRSRC_FILL_RECT;
    cmd.u.fill_rect.x = 3;
    cmd.u.fill_rect.y = 3;
    cmd.u.fill_rect.w = 4;
    cmd.u.fill_rect.h = 4;
    cmd.u.fill_rect.argb = (int)(0xFF000000u | (unsigned)colour);
    cmd.u.fill_rect.scissor_w = LAYOUT;
    cmd.u.fill_rect.scissor_h = LAYOUT;
    cmd.u.fill_rect.filled = 1;
    ToriRS_Soft3D_Execute(soft, &cmd);
    execute(soft, TORIRSRC_END_2D);
}

/* Output pixels 14..16 have every bicubic tap in layout [3,6]; 12..17 every
 * linear tap. Pixels 0..5 have none of either anywhere near the rect. */
static void
check_frame(int const* pixels, int mode, int colour, int world_same, char const* what)
{
    int const inner0 = mode == 2 ? 14 : 12;
    int const inner1 = mode == 2 ? 16 : 17;
    int inside_ok = 1;
    int outside_ok = 1;
    char msg[160];

    for( int y = inner0; y <= inner1; y++ )
        for( int x = inner0; x <= inner1; x++ )
            inside_ok &= (pixels[y * BUF + x] & 0xFFFFFF) == colour;
    for( int y = 0; y < 6; y++ )
        for( int x = 0; x < 6; x++ )
            outside_ok &= pixels[y * BUF + x] == (((x + y) & 1) ? world_same : 0x00102030);
    snprintf(msg, sizeof(msg), "mode %d %s: interior is exactly the interface colour", mode, what);
    CHECK(inside_ok, msg);
    snprintf(msg, sizeof(msg), "mode %d %s: the world far from the interface is untouched", mode, what);
    CHECK(outside_ok, msg);
}

int
main(void)
{
    struct ToriDraw_Scene* scene;
    struct ToriRS_Soft3D* soft;
    static int pixels[BUF * BUF];
    int const panel = 0x00785A30;
    int const changed = 0x00206040;

    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);

    for( int mode = 1; mode <= 2; mode++ )
    {
        soft = ToriRS_Soft3D_New();
        assert(soft);
        /* Half the world under the rect is the rect's own colour. */
        frame(soft, scene, pixels, mode, panel, panel);
        check_frame(pixels, mode, panel, panel, "world matching the interface");
        /* Same content again: composited from the cache. */
        frame(soft, scene, pixels, mode, panel, panel);
        check_frame(pixels, mode, panel, panel, "cached frame");
        /* A changed colour must be re-filtered, not served stale. */
        frame(soft, scene, pixels, mode, changed, panel);
        check_frame(pixels, mode, changed, panel, "changed colour");
        ToriRS_Soft3D_Free(soft);
    }

    /* Nearest writes straight into the buffer: every pixel of the rect, and
     * nothing is filtered past its edge. */
    soft = ToriRS_Soft3D_New();
    assert(soft);
    frame(soft, scene, pixels, 0, panel, panel);
    {
        int ok = 1;
        for( int y = 0; y < BUF; y++ )
            for( int x = 0; x < BUF; x++ )
            {
                int const in = x >= 9 && x < 21 && y >= 9 && y < 21;
                int const want = in ? panel : (((x + y) & 1) ? panel : 0x00102030);
                ok &= (pixels[y * BUF + x] & 0xFFFFFF) == want;
            }
        CHECK(ok, "mode 0: nearest fills exactly the scaled rect");
    }
    ToriRS_Soft3D_Free(soft);
    ToriDraw_SceneFree(scene);

    if( failures )
    {
        fprintf(stderr, "soft3d_interface_filter_test: %d FAILED\n", failures);
        return 1;
    }
    printf("soft3d_interface_filter_test: OK\n");
    return 0;
}
