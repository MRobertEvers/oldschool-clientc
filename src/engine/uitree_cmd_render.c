#include "uitree_cmd_render.h"
#include "mem/torirs_mem.h"

#include "platform/platform_renderer_soft3d.h"
#include "render/torirs_frame.h"
#include "ui/uitree_emit.h"

#include "bmp.h"

#include <assert.h>
#include <stdlib.h>

void
UITreeCmd_RenderToPixelsWith(
    struct ToriPlatform_Renderer_Soft3D* soft,
    struct ToriDraw_Scene* scene,
    struct UITreeEmitDesc const* cmds,
    int cmd_count,
    int* pixels,
    int width,
    int height)
{
    struct ToriRS_Frame frame;

    assert(soft);
    assert(scene);
    assert(pixels);
    assert(width > 0);
    assert(height > 0);
    if( cmd_count > 0 )
        assert(cmds);

    ToriRS_FrameInit(&frame);
    ToriRS_FrameSetScene(&frame, scene);
    ToriRS_FrameSetCanvas(&frame, width, height);
    ToriRS_FrameSetEmit(&frame, cmds, cmd_count);
    ToriPlatform_Renderer_Soft3D_Init(soft, scene, pixels, width, height);
    ToriPlatform_Renderer_Soft3D_RenderFrame(soft, &frame);
}

void
UITreeCmd_RenderToPixels(
    struct ToriDraw_Scene* scene,
    struct UITreeEmitDesc const* cmds,
    int cmd_count,
    int* pixels,
    int width,
    int height)
{
    struct ToriPlatform_Renderer_Soft3D* soft;

    /* One shot: this renderer's caches have nothing to carry to a next frame,
     * so it lives no longer than the picture it draws. */
    soft = ToriPlatform_Renderer_Soft3D_New();
    UITreeCmd_RenderToPixelsWith(soft, scene, cmds, cmd_count, pixels, width, height);
    ToriPlatform_Renderer_Soft3D_Free(soft);
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
    pixels = ToriMem_Calloc(ToriMem_Ui(), n, sizeof(int));
    assert(pixels);

    UITreeCmd_RenderToPixels(scene, cmds, cmd_count, pixels, width, height);
    bmp_write_file(path, pixels, width, height);
    ToriMem_Free(ToriMem_Ui(), pixels);
    return 0;
}
