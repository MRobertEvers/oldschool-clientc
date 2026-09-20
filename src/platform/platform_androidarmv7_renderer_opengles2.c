/*
 * Android's OpenGL ES 2.0 renderer, on an EGL context.
 *
 * Every function here is one line: this lane's name for a core entry point.
 * That is the whole of the difference between this lane and its peer, and
 * having it in a file of its own is what keeps it from drifting back into
 * the core as a #if on the platform.
 *
 * @see platform_androidarmv7_renderer_opengles2.h for what this lane is, and
 *      platform_renderer_es2.h for what the core does.
 */

#include "platform/platform_androidarmv7_renderer_opengles2.h"

/*
 * The one cast in the lane, in the one file entitled to make it: the handle
 * this lane hands out is the core's, under this lane's name. Nothing
 * dereferences through struct ToriRS_GLES2 -- it has no definition to
 * dereference -- so there is no aliasing question, only a name.
 */
struct ToriRS_GLES2*
ToriRS_GLES2_New(int width, int height)
{
    return (struct ToriRS_GLES2*)ToriRS_ES2_New(width, height, "GLES2");
}

void
ToriRS_GLES2_Free(
    struct ToriRS_GLES2* renderer)
{
    ToriRS_ES2_Free((struct ToriRS_ES2*)renderer);
}

bool
ToriRS_GLES2_Init(
    struct ToriRS_GLES2* renderer,
    ToriRS_GLWindow* window,
    struct ToriDraw_Scene* scene,
    bool z_buffer)
{
    return ToriRS_ES2_Init((struct ToriRS_ES2*)renderer, window, scene, z_buffer);
}

void
ToriRS_GLES2_SetViewport(
    struct ToriRS_GLES2* renderer,
    int width,
    int height)
{
    ToriRS_ES2_SetViewport((struct ToriRS_ES2*)renderer, width, height);
}

void
ToriRS_GLES2_SetInterfaceScaleMode(
    struct ToriRS_GLES2* renderer,
    int mode)
{
    ToriRS_ES2_SetInterfaceScaleMode((struct ToriRS_ES2*)renderer, mode);
}

void
ToriRS_GLES2_SetClientScaling(
    struct ToriRS_GLES2* renderer,
    struct ClientScaleSettings const* settings)
{
    ToriRS_ES2_SetClientScaling((struct ToriRS_ES2*)renderer, settings);
}

void
ToriRS_GLES2_SetPick(
    struct ToriRS_GLES2* renderer,
    int mouse_x,
    int mouse_y)
{
    ToriRS_ES2_SetPick((struct ToriRS_ES2*)renderer, mouse_x, mouse_y);
}

struct ToriRS_PickHits const*
ToriRS_GLES2_PickHits(
    struct ToriRS_GLES2 const* renderer)
{
    return ToriRS_ES2_PickHits((struct ToriRS_ES2 const*)renderer);
}

void
ToriRS_GLES2_Execute(
    struct ToriRS_GLES2* renderer,
    struct ToriRS_RenderCommand const* command)
{
    ToriRS_ES2_Execute((struct ToriRS_ES2*)renderer, command);
}

void
ToriRS_GLES2_DrawBootBar(
    struct ToriRS_GLES2* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    ToriRS_ES2_DrawBootBar((struct ToriRS_ES2*)renderer, progress, caption_font_id, caption);
}

void
ToriRS_GLES2_RenderFrame(
    struct ToriRS_GLES2* renderer,
    struct ToriRS_Frame* frame)
{
    ToriRS_ES2_RenderFrame((struct ToriRS_ES2*)renderer, frame);
}

bool
ToriRS_GLES2_ReadPixels(
    struct ToriRS_GLES2* renderer,
    int* pixels,
    int width,
    int height)
{
    return ToriRS_ES2_ReadPixels((struct ToriRS_ES2*)renderer, pixels, width, height);
}

struct ToriRS_ES2*
ToriRS_GLES2_Core(struct ToriRS_GLES2* renderer)
{
    return (struct ToriRS_ES2*)renderer;
}

void
ToriRS_GLES2_RotmaskSourceChanged(void)
{
    ToriRS_ES2_RotmaskSourceChanged();
}
