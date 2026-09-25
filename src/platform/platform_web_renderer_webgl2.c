/*
 * The browser's OpenGL ES 3.0 renderer, on a WebGL2 context.
 *
 * Every function here is one line: this lane's name for a core entry point.
 * That is the whole of the difference between this lane and its peer, and
 * having it in a file of its own is what keeps it from drifting back into
 * the core as a #if on the platform.
 *
 * @see platform_web_renderer_webgl2.h for what this lane is, and
 *      3rd/trspk/es3/trspk_es3.h for what the core does.
 */

#include "platform/platform_web_renderer_webgl2.h"

/*
 * The one cast in the lane, in the one file entitled to make it: the handle
 * this lane hands out is the core's, under this lane's name. Nothing
 * dereferences through struct ToriPlatformWeb_Renderer_WebGL2 -- it has no definition to
 * dereference -- so there is no aliasing question, only a name.
 */
struct ToriPlatformWeb_Renderer_WebGL2*
ToriPlatformWeb_Renderer_WebGL2_New(int width, int height)
{
    return (struct ToriPlatformWeb_Renderer_WebGL2*)TRSPK_Renderer_ES3_New(width, height, "WebGL2");
}

void
ToriPlatformWeb_Renderer_WebGL2_Free(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer)
{
    TRSPK_Renderer_ES3_Free((struct TRSPK_Renderer_ES3*)renderer);
}

bool
ToriPlatformWeb_Renderer_WebGL2_PainterInit(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    return TRSPK_Renderer_ES3_PainterInit((struct TRSPK_Renderer_ES3*)renderer, window, scene);
}

bool
ToriPlatformWeb_Renderer_WebGL2_ZBufferInit(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    return TRSPK_Renderer_ES3_ZBufferInit((struct TRSPK_Renderer_ES3*)renderer, window, scene);
}

void
ToriPlatformWeb_Renderer_WebGL2_SetViewport(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    int width,
    int height)
{
    TRSPK_Renderer_ES3_SetViewport((struct TRSPK_Renderer_ES3*)renderer, width, height);
}

void
ToriPlatformWeb_Renderer_WebGL2_SetInterfaceScaleMode(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    int mode)
{
    TRSPK_Renderer_ES3_SetInterfaceScaleMode((struct TRSPK_Renderer_ES3*)renderer, mode);
}

void
ToriPlatformWeb_Renderer_WebGL2_SetClientScaling(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    struct ClientScaleSettings const* settings)
{
    TRSPK_Renderer_ES3_SetClientScaling((struct TRSPK_Renderer_ES3*)renderer, settings);
}

void
ToriPlatformWeb_Renderer_WebGL2_SetPick(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    int mouse_x,
    int mouse_y)
{
    TRSPK_Renderer_ES3_SetPick((struct TRSPK_Renderer_ES3*)renderer, mouse_x, mouse_y);
}

struct ToriRS_PickHits const*
ToriPlatformWeb_Renderer_WebGL2_PickHits(
    struct ToriPlatformWeb_Renderer_WebGL2 const* renderer)
{
    return TRSPK_Renderer_ES3_PickHits((struct TRSPK_Renderer_ES3 const*)renderer);
}

void
ToriPlatformWeb_Renderer_WebGL2_PainterExecute(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    struct ToriRS_RenderCommand const* command)
{
    TRSPK_Renderer_ES3_PainterExecute((struct TRSPK_Renderer_ES3*)renderer, command);
}

void
ToriPlatformWeb_Renderer_WebGL2_ZBufferExecute(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    struct ToriRS_RenderCommand const* command)
{
    TRSPK_Renderer_ES3_ZBufferExecute((struct TRSPK_Renderer_ES3*)renderer, command);
}

void
ToriPlatformWeb_Renderer_WebGL2_PainterDrawBootBar(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    TRSPK_Renderer_ES3_PainterDrawBootBar(
        (struct TRSPK_Renderer_ES3*)renderer, progress, caption_font_id, caption);
}

void
ToriPlatformWeb_Renderer_WebGL2_ZBufferDrawBootBar(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    TRSPK_Renderer_ES3_ZBufferDrawBootBar(
        (struct TRSPK_Renderer_ES3*)renderer, progress, caption_font_id, caption);
}

void
ToriPlatformWeb_Renderer_WebGL2_PainterRenderFrame(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    struct ToriRS_Frame* frame)
{
    TRSPK_Renderer_ES3_PainterRenderFrame((struct TRSPK_Renderer_ES3*)renderer, frame);
}

void
ToriPlatformWeb_Renderer_WebGL2_ZBufferRenderFrame(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    struct ToriRS_Frame* frame)
{
    TRSPK_Renderer_ES3_ZBufferRenderFrame((struct TRSPK_Renderer_ES3*)renderer, frame);
}

bool
ToriPlatformWeb_Renderer_WebGL2_ReadPixels(
    struct ToriPlatformWeb_Renderer_WebGL2* renderer,
    int* pixels,
    int width,
    int height)
{
    return TRSPK_Renderer_ES3_ReadPixels((struct TRSPK_Renderer_ES3*)renderer, pixels, width, height);
}

void
ToriPlatformWeb_Renderer_WebGL2_RotmaskSourceChanged(void)
{
    TRSPK_Renderer_ES3_RotmaskSourceChanged();
}
