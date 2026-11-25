/*
 * Soft3D's outline/shadow cache (soft3d_outline_cache_get).
 *
 * A bank draws an outlined, shadowed icon for each of its items every frame.
 * The cache used to be 256 LRU slots, and a frame drawing more distinct icons
 * than that evicted each one just before it was asked for again. What is
 * pinned here is that the cache never changes a pixel, whatever it holds:
 * every frame a renderer that kept its cache draws is compared with the same
 * frame drawn by a renderer that has none, through
 *
 *   - a frame of 600 icons, repeated (the table grows past 256 and hits);
 *   - a different 600 (stale entries evicted for new ones), then the first;
 *   - more icons in one frame than the ceiling holds;
 *   - an icon whose pixels changed in place, after the SPRITE_UNLOAD the
 *     scene emits when it frees a sprite;
 *   - a second scene with the same ids.
 *
 * Half the icons are drawn IF3 (the shadow pads them past their nominal box,
 * so they take the general path and its second lookup).
 */
#include "platform/platform_renderer_soft3d.h"
#include "render/torirs_render.h"
#include "toridraw.h"
#include "toridraw_alloc.h"
#include "toridraw_scene.h"
#include "toridraw_sprite.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ICON 6
#define CELL 9
#define COLS 64
#define ICONS 2200
#define CANVAS_W (COLS * CELL)
#define CANVAS_H (((ICONS + COLS - 1) / COLS) * CELL)

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

static void
execute(struct ToriPlatform_Renderer_Soft3D* soft, enum ToriRS_RenderCommandKind kind)
{
    struct ToriRS_RenderCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.kind = kind;
    ToriPlatform_Renderer_Soft3D_Execute(soft, &cmd);
}

/* An icon unlike its neighbours: a diagonal plus a bar whose place and
 * colour follow the id, on transparent. */
static void
paint_icon(uint32_t* px, int id, uint32_t salt)
{
    memset(px, 0, ICON * ICON * sizeof(*px));
    for( int i = 0; i < ICON; i++ )
        px[i * ICON + i] = 0xFF000000u | ((((uint32_t)id * 2654435761u) ^ salt) & 0xFFFFFFu);
    for( int x = 0; x < ICON; x++ )
        px[(id % ICON) * ICON + x] = 0xFF000000u | (((uint32_t)id * 40503u) & 0xFFFFFFu);
}

static void
add_icons(struct ToriDraw_Scene* scene)
{
    for( int id = 1; id <= ICONS; id++ )
    {
        uint32_t* px = TORIDRAW_MALLOC(ICON * ICON * sizeof(uint32_t));
        struct ToriDraw_Sprite** sprites = TORIDRAW_MALLOC(sizeof(*sprites));
        assert(px);
        assert(sprites);
        paint_icon(px, id, 0);
        sprites[0] = ToriDraw_SpriteNewFromArgbOwned(px, ICON, ICON);
        assert(sprites[0]);
        ToriDraw_SceneSpriteAdd(scene, id, sprites, 1);
    }
}

/* One frame: icons [first, first + count), each outlined and shadowed. */
static void
frame(
    struct ToriPlatform_Renderer_Soft3D* soft,
    struct ToriDraw_Scene* scene,
    int* pixels,
    int first,
    int count)
{
    memset(pixels, 0, (size_t)CANVAS_W * CANVAS_H * sizeof(*pixels));
    ToriPlatform_Renderer_Soft3D_Init(soft, scene, pixels, CANVAS_W, CANVAS_H);
    execute(soft, TORIRSRC_BEGIN_2D);
    for( int k = 0; k < count; k++ )
    {
        int const id = first + k;
        int const cell = id - 1;
        struct ToriRS_RenderCommand cmd;

        memset(&cmd, 0, sizeof(cmd));
        cmd.kind = TORIRSRC_SPRITE;
        cmd.u.sprite.scene_id = id;
        cmd.u.sprite.x = (cell % COLS) * CELL;
        cmd.u.sprite.y = (cell / COLS) * CELL;
        cmd.u.sprite.scissor_w = CANVAS_W;
        cmd.u.sprite.scissor_h = CANVAS_H;
        cmd.u.sprite.outline = 1;
        cmd.u.sprite.graphic_shadow = 0x333333;
        cmd.u.sprite.if3 = (uint8_t)(id & 1);
        ToriPlatform_Renderer_Soft3D_Execute(soft, &cmd);
    }
    execute(soft, TORIRSRC_END_2D);
}

/* The kept renderer's frame against the same frame from a renderer with no
 * cache yet. */
static void
check(
    struct ToriPlatform_Renderer_Soft3D* kept,
    struct ToriDraw_Scene* scene,
    int* got,
    int* want,
    int first,
    int count,
    char const* what)
{
    struct ToriPlatform_Renderer_Soft3D* fresh = ToriPlatform_Renderer_Soft3D_New();
    int drawn = 0;

    frame(kept, scene, got, first, count);
    frame(fresh, scene, want, first, count);
    ToriPlatform_Renderer_Soft3D_Free(fresh);
    for( int i = 0; i < CANVAS_W * CANVAS_H; i++ )
        drawn |= want[i] != 0;
    CHECK(drawn, "the reference frame drew something");
    CHECK(memcmp(got, want, (size_t)CANVAS_W * CANVAS_H * sizeof(*got)) == 0, what);
}

int
main(void)
{
    static int got[CANVAS_W * CANVAS_H];
    static int want[CANVAS_W * CANVAS_H];
    struct ToriDraw_Scene* scene;
    struct ToriDraw_Scene* other;
    struct ToriPlatform_Renderer_Soft3D* soft;

    ToriDraw_InitSinTable();
    ToriDraw_InitCosTable();
    scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(scene);
    add_icons(scene);

    soft = ToriPlatform_Renderer_Soft3D_New();
    assert(soft);

    check(soft, scene, got, want, 1, 600, "600 icons, first frame");
    check(soft, scene, got, want, 1, 600, "600 icons, second frame");
    check(soft, scene, got, want, 1, 600, "600 icons, third frame");
    check(soft, scene, got, want, 601, 600, "a different 600 icons");
    check(soft, scene, got, want, 1, 600, "the first 600 again");
    check(soft, scene, got, want, 1, 40, "a frame of 40");
    check(soft, scene, got, want, 1, ICONS, "more icons than the ceiling, first frame");
    check(soft, scene, got, want, 1, ICONS, "more icons than the ceiling, second frame");
    check(soft, scene, got, want, 1, 600, "back under the ceiling");

    /* The scene frees a sprite and its id gets new pixels; the cache keyed the
     * old buffer. Changing the pixels in place is the case where the new
     * buffer lands at the old address -- only the unload can tell. */
    {
        int count = 0;
        struct ToriDraw_Sprite** sprites = ToriDraw_SceneSpriteGet(scene, 7, &count);
        struct ToriRS_RenderCommand cmd;

        assert(sprites);
        assert(count == 1);
        paint_icon(sprites[0]->pixels_argb, 7, 0x00A0A0A0u);
        memset(&cmd, 0, sizeof(cmd));
        cmd.kind = TORIRSRC_SPRITE_UNLOAD;
        cmd.u.sprite_load.element_id = 7;
        ToriPlatform_Renderer_Soft3D_Execute(soft, &cmd);
        check(soft, scene, got, want, 1, 600, "an icon changed under an unload");
    }

    /* Another scene: same ids, other pixels. */
    other = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_LOW_2K);
    assert(other);
    for( int id = 1; id <= 600; id++ )
    {
        uint32_t* px = TORIDRAW_MALLOC(ICON * ICON * sizeof(uint32_t));
        struct ToriDraw_Sprite** sprites = TORIDRAW_MALLOC(sizeof(*sprites));
        assert(px);
        assert(sprites);
        paint_icon(px, id, 0x00505050u);
        sprites[0] = ToriDraw_SpriteNewFromArgbOwned(px, ICON, ICON);
        assert(sprites[0]);
        ToriDraw_SceneSpriteAdd(other, id, sprites, 1);
    }
    check(soft, other, got, want, 1, 600, "a second scene with the same ids");
    check(soft, scene, got, want, 1, 600, "back to the first scene");

    ToriPlatform_Renderer_Soft3D_Free(soft);
    ToriDraw_SceneFree(other);
    ToriDraw_SceneFree(scene);

    if( failures )
    {
        fprintf(stderr, "soft3d_outline_cache_test: %d FAILED\n", failures);
        return 1;
    }
    printf("soft3d_outline_cache_test: OK\n");
    return 0;
}
