/*
 * The browser's OpenGL ES 2.0 renderer, on a WebGL1 context.
 *
 * Every function here is one line: this lane's name for a core entry point.
 * That is the whole of the difference between this lane and its peer, and
 * having it in a file of its own is what keeps it from drifting back into
 * the core as a #if on the platform.
 *
 * @see platform_web_renderer_webgl1.h for what this lane is, and
 *      3rd/trspk/es2/trspk_es2.h for what the core does.
 */

#include "platform/platform_web_renderer_webgl1.h"

/*
 * The one cast in the lane, in the one file entitled to make it: the handle
 * this lane hands out is the core's, under this lane's name. Nothing
 * dereferences through struct ToriPlatformWeb_Renderer_WebGL1 -- it has no definition to
 * dereference -- so there is no aliasing question, only a name.
 */
struct ToriPlatformWeb_Renderer_WebGL1*
ToriPlatformWeb_Renderer_WebGL1_New(int width, int height)
{
    return (struct ToriPlatformWeb_Renderer_WebGL1*)TRSPK_Renderer_ES2_New(width, height, "WebGL1");
}

void
ToriPlatformWeb_Renderer_WebGL1_Free(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer)
{
    TRSPK_Renderer_ES2_Free((struct TRSPK_Renderer_ES2*)renderer);
}

bool
ToriPlatformWeb_Renderer_WebGL1_PainterInit(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    return TRSPK_Renderer_ES2_PainterInit((struct TRSPK_Renderer_ES2*)renderer, window, scene);
}

bool
ToriPlatformWeb_Renderer_WebGL1_ZBufferInit(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    return TRSPK_Renderer_ES2_ZBufferInit((struct TRSPK_Renderer_ES2*)renderer, window, scene);
}

void
ToriPlatformWeb_Renderer_WebGL1_SetViewport(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    int width,
    int height)
{
    TRSPK_Renderer_ES2_SetViewport((struct TRSPK_Renderer_ES2*)renderer, width, height);
}

void
ToriPlatformWeb_Renderer_WebGL1_SetInterfaceScaleMode(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    int mode)
{
    TRSPK_Renderer_ES2_SetInterfaceScaleMode((struct TRSPK_Renderer_ES2*)renderer, mode);
}

void
ToriPlatformWeb_Renderer_WebGL1_SetClientScaling(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    struct ClientScaleSettings const* settings)
{
    TRSPK_Renderer_ES2_SetClientScaling((struct TRSPK_Renderer_ES2*)renderer, settings);
}

void
ToriPlatformWeb_Renderer_WebGL1_SetPick(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    int mouse_x,
    int mouse_y)
{
    TRSPK_Renderer_ES2_SetPick((struct TRSPK_Renderer_ES2*)renderer, mouse_x, mouse_y);
}

struct ToriRS_PickHits const*
ToriPlatformWeb_Renderer_WebGL1_PickHits(
    struct ToriPlatformWeb_Renderer_WebGL1 const* renderer)
{
    return TRSPK_Renderer_ES2_PickHits((struct TRSPK_Renderer_ES2 const*)renderer);
}

void
ToriPlatformWeb_Renderer_WebGL1_PainterExecute(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    struct ToriRS_RenderCommand const* command)
{
    TRSPK_Renderer_ES2_PainterExecute((struct TRSPK_Renderer_ES2*)renderer, command);
}

void
ToriPlatformWeb_Renderer_WebGL1_ZBufferExecute(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    struct ToriRS_RenderCommand const* command)
{
    TRSPK_Renderer_ES2_ZBufferExecute((struct TRSPK_Renderer_ES2*)renderer, command);
}

void
ToriPlatformWeb_Renderer_WebGL1_PainterDrawBootBar(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    TRSPK_Renderer_ES2_PainterDrawBootBar(
        (struct TRSPK_Renderer_ES2*)renderer, progress, caption_font_id, caption);
}

void
ToriPlatformWeb_Renderer_WebGL1_ZBufferDrawBootBar(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    TRSPK_Renderer_ES2_ZBufferDrawBootBar(
        (struct TRSPK_Renderer_ES2*)renderer, progress, caption_font_id, caption);
}

void
ToriPlatformWeb_Renderer_WebGL1_PainterRenderFrame(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    struct ToriRS_Frame* frame)
{
    TRSPK_Renderer_ES2_PainterRenderFrame((struct TRSPK_Renderer_ES2*)renderer, frame);
}

void
ToriPlatformWeb_Renderer_WebGL1_ZBufferRenderFrame(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    struct ToriRS_Frame* frame)
{
    TRSPK_Renderer_ES2_ZBufferRenderFrame((struct TRSPK_Renderer_ES2*)renderer, frame);
}

bool
ToriPlatformWeb_Renderer_WebGL1_ReadPixels(
    struct ToriPlatformWeb_Renderer_WebGL1* renderer,
    int* pixels,
    int width,
    int height)
{
    return TRSPK_Renderer_ES2_ReadPixels((struct TRSPK_Renderer_ES2*)renderer, pixels, width, height);
}

void
ToriPlatformWeb_Renderer_WebGL1_RotmaskSourceChanged(void)
{
    TRSPK_Renderer_ES2_RotmaskSourceChanged();
}
