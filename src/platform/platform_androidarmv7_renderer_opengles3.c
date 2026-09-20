/*
 * Android's OpenGL ES 3.0 renderer, on an EGL context.
 *
 * Every function here is one line: this lane's name for a core entry point.
 * That is the whole of the difference between this lane and its peer, and
 * having it in a file of its own is what keeps it from drifting back into
 * the core as a #if on the platform.
 *
 * @see platform_androidarmv7_renderer_opengles3.h for what this lane is, and
 *      3rd/trspk/es3/trspk_es3.h for what the core does.
 */

#include "platform/platform_androidarmv7_renderer_opengles3.h"

/*
 * The one cast in the lane, in the one file entitled to make it: the handle
 * this lane hands out is the core's, under this lane's name. Nothing
 * dereferences through struct ToriPlatformAndroid_Renderer_GLES3 -- it has no definition to
 * dereference -- so there is no aliasing question, only a name.
 */
struct ToriPlatformAndroid_Renderer_GLES3*
ToriPlatformAndroid_Renderer_GLES3_New(int width, int height)
{
    return (struct ToriPlatformAndroid_Renderer_GLES3*)TRSPK_Renderer_ES3_New(width, height, "GLES3");
}

void
ToriPlatformAndroid_Renderer_GLES3_Free(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer)
{
    TRSPK_Renderer_ES3_Free((struct TRSPK_Renderer_ES3*)renderer);
}

bool
ToriPlatformAndroid_Renderer_GLES3_PainterInit(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    return TRSPK_Renderer_ES3_PainterInit((struct TRSPK_Renderer_ES3*)renderer, window, scene);
}

bool
ToriPlatformAndroid_Renderer_GLES3_ZBufferInit(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    return TRSPK_Renderer_ES3_ZBufferInit((struct TRSPK_Renderer_ES3*)renderer, window, scene);
}

void
ToriPlatformAndroid_Renderer_GLES3_SetViewport(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    int width,
    int height)
{
    TRSPK_Renderer_ES3_SetViewport((struct TRSPK_Renderer_ES3*)renderer, width, height);
}

void
ToriPlatformAndroid_Renderer_GLES3_SetInterfaceScaleMode(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    int mode)
{
    TRSPK_Renderer_ES3_SetInterfaceScaleMode((struct TRSPK_Renderer_ES3*)renderer, mode);
}

void
ToriPlatformAndroid_Renderer_GLES3_SetClientScaling(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    struct ClientScaleSettings const* settings)
{
    TRSPK_Renderer_ES3_SetClientScaling((struct TRSPK_Renderer_ES3*)renderer, settings);
}

void
ToriPlatformAndroid_Renderer_GLES3_SetPick(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    int mouse_x,
    int mouse_y)
{
    TRSPK_Renderer_ES3_SetPick((struct TRSPK_Renderer_ES3*)renderer, mouse_x, mouse_y);
}

struct ToriRS_PickHits const*
ToriPlatformAndroid_Renderer_GLES3_PickHits(
    struct ToriPlatformAndroid_Renderer_GLES3 const* renderer)
{
    return TRSPK_Renderer_ES3_PickHits((struct TRSPK_Renderer_ES3 const*)renderer);
}

void
ToriPlatformAndroid_Renderer_GLES3_PainterExecute(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    struct ToriRS_RenderCommand const* command)
{
    TRSPK_Renderer_ES3_PainterExecute((struct TRSPK_Renderer_ES3*)renderer, command);
}

void
ToriPlatformAndroid_Renderer_GLES3_ZBufferExecute(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    struct ToriRS_RenderCommand const* command)
{
    TRSPK_Renderer_ES3_ZBufferExecute((struct TRSPK_Renderer_ES3*)renderer, command);
}

void
ToriPlatformAndroid_Renderer_GLES3_PainterDrawBootBar(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    TRSPK_Renderer_ES3_PainterDrawBootBar((struct TRSPK_Renderer_ES3*)renderer, progress, caption_font_id, caption);
}

void
ToriPlatformAndroid_Renderer_GLES3_ZBufferDrawBootBar(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    TRSPK_Renderer_ES3_ZBufferDrawBootBar((struct TRSPK_Renderer_ES3*)renderer, progress, caption_font_id, caption);
}

void
ToriPlatformAndroid_Renderer_GLES3_PainterRenderFrame(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    struct ToriRS_Frame* frame)
{
    TRSPK_Renderer_ES3_PainterRenderFrame((struct TRSPK_Renderer_ES3*)renderer, frame);
}

void
ToriPlatformAndroid_Renderer_GLES3_ZBufferRenderFrame(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    struct ToriRS_Frame* frame)
{
    TRSPK_Renderer_ES3_ZBufferRenderFrame((struct TRSPK_Renderer_ES3*)renderer, frame);
}

bool
ToriPlatformAndroid_Renderer_GLES3_ReadPixels(
    struct ToriPlatformAndroid_Renderer_GLES3* renderer,
    int* pixels,
    int width,
    int height)
{
    return TRSPK_Renderer_ES3_ReadPixels((struct TRSPK_Renderer_ES3*)renderer, pixels, width, height);
}

void
ToriPlatformAndroid_Renderer_GLES3_RotmaskSourceChanged(void)
{
    TRSPK_Renderer_ES3_RotmaskSourceChanged();
}
