/*
 * The browser's OpenGL ES 2.0 renderer, on a WebGL1 context.
 *
 * Every function here is one line: this lane's name for a core entry point.
 * That is the whole of the difference between this lane and its peer, and
 * having it in a file of its own is what keeps it from drifting back into
 * the core as a #if on the platform.
 *
 * @see platform_renderer_webgl1.h for what this lane is, and
 *      platform_renderer_es2.h for what the core does.
 */

#include "platform/platform_renderer_webgl1.h"

/*
 * The one cast in the lane, in the one file entitled to make it: the handle
 * this lane hands out is the core's, under this lane's name. Nothing
 * dereferences through struct ToriRS_WebGL1 -- it has no definition to
 * dereference -- so there is no aliasing question, only a name.
 */
struct ToriRS_WebGL1*
ToriRS_WebGL1_New(int width, int height)
{
    return (struct ToriRS_WebGL1*)ToriRS_ES2_New(width, height, "WebGL1");
}

void
ToriRS_WebGL1_Free(
    struct ToriRS_WebGL1* renderer)
{
    ToriRS_ES2_Free((struct ToriRS_ES2*)renderer);
}

bool
ToriRS_WebGL1_Init(
    struct ToriRS_WebGL1* renderer,
    ToriRS_GLWindow* window,
    struct ToriDraw_Scene* scene,
    bool z_buffer)
{
    return ToriRS_ES2_Init((struct ToriRS_ES2*)renderer, window, scene, z_buffer);
}

void
ToriRS_WebGL1_SetViewport(
    struct ToriRS_WebGL1* renderer,
    int width,
    int height)
{
    ToriRS_ES2_SetViewport((struct ToriRS_ES2*)renderer, width, height);
}

void
ToriRS_WebGL1_SetInterfaceScaleMode(
    struct ToriRS_WebGL1* renderer,
    int mode)
{
    ToriRS_ES2_SetInterfaceScaleMode((struct ToriRS_ES2*)renderer, mode);
}

void
ToriRS_WebGL1_SetClientScaling(
    struct ToriRS_WebGL1* renderer,
    struct ClientScaleSettings const* settings)
{
    ToriRS_ES2_SetClientScaling((struct ToriRS_ES2*)renderer, settings);
}

void
ToriRS_WebGL1_SetPick(
    struct ToriRS_WebGL1* renderer,
    int mouse_x,
    int mouse_y)
{
    ToriRS_ES2_SetPick((struct ToriRS_ES2*)renderer, mouse_x, mouse_y);
}

struct ToriRS_PickHits const*
ToriRS_WebGL1_PickHits(
    struct ToriRS_WebGL1 const* renderer)
{
    return ToriRS_ES2_PickHits((struct ToriRS_ES2 const*)renderer);
}

void
ToriRS_WebGL1_Execute(
    struct ToriRS_WebGL1* renderer,
    struct ToriRS_RenderCommand const* command)
{
    ToriRS_ES2_Execute((struct ToriRS_ES2*)renderer, command);
}

void
ToriRS_WebGL1_DrawBootBar(
    struct ToriRS_WebGL1* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    ToriRS_ES2_DrawBootBar((struct ToriRS_ES2*)renderer, progress, caption_font_id, caption);
}

void
ToriRS_WebGL1_RenderFrame(
    struct ToriRS_WebGL1* renderer,
    struct ToriRS_Frame* frame)
{
    ToriRS_ES2_RenderFrame((struct ToriRS_ES2*)renderer, frame);
}

bool
ToriRS_WebGL1_ReadPixels(
    struct ToriRS_WebGL1* renderer,
    int* pixels,
    int width,
    int height)
{
    return ToriRS_ES2_ReadPixels((struct ToriRS_ES2*)renderer, pixels, width, height);
}

void
ToriRS_WebGL1_RotmaskSourceChanged(void)
{
    ToriRS_ES2_RotmaskSourceChanged();
}
