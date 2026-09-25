/*
 * Android's OpenGL ES 2.0 renderer, on an EGL context.
 *
 * Every function here is one line: this lane's name for a core entry point.
 * That is the whole of the difference between this lane and its peer, and
 * having it in a file of its own is what keeps it from drifting back into
 * the core as a #if on the platform.
 *
 * @see platform_androidarmv7_renderer_opengles2.h for what this lane is, and
 *      3rd/trspk/es2/trspk_es2.h for what the core does.
 */

#include "platform/platform_androidarmv7_renderer_opengles2.h"

/*
 * The one cast in the lane, in the one file entitled to make it: the handle
 * this lane hands out is the core's, under this lane's name. Nothing
 * dereferences through struct ToriPlatformAndroid_Renderer_GLES2 -- it has no definition to
 * dereference -- so there is no aliasing question, only a name.
 */
struct ToriPlatformAndroid_Renderer_GLES2*
ToriPlatformAndroid_Renderer_GLES2_New(int width, int height)
{
    return (struct ToriPlatformAndroid_Renderer_GLES2*)TRSPK_Renderer_ES2_New(width, height, "GLES2");
}

void
ToriPlatformAndroid_Renderer_GLES2_Free(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer)
{
    TRSPK_Renderer_ES2_Free((struct TRSPK_Renderer_ES2*)renderer);
}

bool
ToriPlatformAndroid_Renderer_GLES2_PainterInit(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    return TRSPK_Renderer_ES2_PainterInit((struct TRSPK_Renderer_ES2*)renderer, window, scene);
}

bool
ToriPlatformAndroid_Renderer_GLES2_ZBufferInit(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    ToriPlatform_GLWindow* window,
    struct ToriDraw_Scene* scene)
{
    return TRSPK_Renderer_ES2_ZBufferInit((struct TRSPK_Renderer_ES2*)renderer, window, scene);
}

void
ToriPlatformAndroid_Renderer_GLES2_SetViewport(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    int width,
    int height)
{
    TRSPK_Renderer_ES2_SetViewport((struct TRSPK_Renderer_ES2*)renderer, width, height);
}

void
ToriPlatformAndroid_Renderer_GLES2_SetInterfaceScaleMode(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    int mode)
{
    TRSPK_Renderer_ES2_SetInterfaceScaleMode((struct TRSPK_Renderer_ES2*)renderer, mode);
}

void
ToriPlatformAndroid_Renderer_GLES2_SetClientScaling(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    struct ClientScaleSettings const* settings)
{
    TRSPK_Renderer_ES2_SetClientScaling((struct TRSPK_Renderer_ES2*)renderer, settings);
}

void
ToriPlatformAndroid_Renderer_GLES2_SetPick(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    int mouse_x,
    int mouse_y)
{
    TRSPK_Renderer_ES2_SetPick((struct TRSPK_Renderer_ES2*)renderer, mouse_x, mouse_y);
}

struct ToriRS_PickHits const*
ToriPlatformAndroid_Renderer_GLES2_PickHits(
    struct ToriPlatformAndroid_Renderer_GLES2 const* renderer)
{
    return TRSPK_Renderer_ES2_PickHits((struct TRSPK_Renderer_ES2 const*)renderer);
}

void
ToriPlatformAndroid_Renderer_GLES2_PainterExecute(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    struct ToriRS_RenderCommand const* command)
{
    TRSPK_Renderer_ES2_PainterExecute((struct TRSPK_Renderer_ES2*)renderer, command);
}

void
ToriPlatformAndroid_Renderer_GLES2_ZBufferExecute(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    struct ToriRS_RenderCommand const* command)
{
    TRSPK_Renderer_ES2_ZBufferExecute((struct TRSPK_Renderer_ES2*)renderer, command);
}

void
ToriPlatformAndroid_Renderer_GLES2_PainterDrawBootBar(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    TRSPK_Renderer_ES2_PainterDrawBootBar(
        (struct TRSPK_Renderer_ES2*)renderer, progress, caption_font_id, caption);
}

void
ToriPlatformAndroid_Renderer_GLES2_ZBufferDrawBootBar(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    TRSPK_Renderer_ES2_ZBufferDrawBootBar(
        (struct TRSPK_Renderer_ES2*)renderer, progress, caption_font_id, caption);
}

void
ToriPlatformAndroid_Renderer_GLES2_PainterRenderFrame(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    struct ToriRS_Frame* frame)
{
    TRSPK_Renderer_ES2_PainterRenderFrame((struct TRSPK_Renderer_ES2*)renderer, frame);
}

void
ToriPlatformAndroid_Renderer_GLES2_ZBufferRenderFrame(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    struct ToriRS_Frame* frame)
{
    TRSPK_Renderer_ES2_ZBufferRenderFrame((struct TRSPK_Renderer_ES2*)renderer, frame);
}

bool
ToriPlatformAndroid_Renderer_GLES2_ReadPixels(
    struct ToriPlatformAndroid_Renderer_GLES2* renderer,
    int* pixels,
    int width,
    int height)
{
    return TRSPK_Renderer_ES2_ReadPixels((struct TRSPK_Renderer_ES2*)renderer, pixels, width, height);
}

struct TRSPK_Renderer_ES2*
ToriPlatformAndroid_Renderer_GLES2_Core(struct ToriPlatformAndroid_Renderer_GLES2* renderer)
{
    return (struct TRSPK_Renderer_ES2*)renderer;
}

void
ToriPlatformAndroid_Renderer_GLES2_RotmaskSourceChanged(void)
{
    TRSPK_Renderer_ES2_RotmaskSourceChanged();
}
