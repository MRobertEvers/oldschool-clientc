#include "uitree_cmd_render.h"

#include "platform/platform_sdl2_renderer_soft3d.h"
#include "render/torirs_frame.h"
#include "ui/uitree_emit.h"

#include "bmp.h"

#include <assert.h>
#include <stdlib.h>

void
UITreeCmd_RenderToPixels(
    struct ToriDraw_Scene* scene,
    struct UITreeEmitDesc const* cmds,
    int cmd_count,
    int* pixels,
    int width,
    int height)
{
    struct ToriRS_Frame frame;
    struct ToriRS_Soft3D* soft;

    assert(scene);
    assert(pixels);
    assert(width > 0 && height > 0);
    if( cmd_count > 0 )
        assert(cmds);

    ToriRS_FrameInit(&frame);
    ToriRS_FrameSetScene(&frame, scene);
    ToriRS_FrameSetCanvas(&frame, width, height);
    ToriRS_FrameSetEmit(&frame, cmds, cmd_count);

    /* One shot: this renderer's caches have nothing to carry to a next frame,
     * so it lives no longer than the picture it draws. */
    soft = ToriRS_Soft3D_New();
    ToriRS_Soft3D_Init(soft, scene, pixels, width, height);
    ToriRS_Soft3D_RenderFrame(soft, &frame);
    ToriRS_Soft3D_Free(soft);
}

int
UITreeCmd_WriteBmp(
    struct ToriDraw_Scene* scene,
    struct UITreeEmitDesc const* cmds,
    int cmd_count,
    char const* path,
    int width,
    int height)
{
    int* pixels;
    size_t n;

    assert(scene);
    assert(path);
    assert(width > 0 && height > 0);

    n = (size_t)width * (size_t)height;
    pixels = calloc(n, sizeof(int));
    assert(pixels);

    UITreeCmd_RenderToPixels(scene, cmds, cmd_count, pixels, width, height);
    bmp_write_file(path, pixels, width, height);
    free(pixels);
    return 0;
}
