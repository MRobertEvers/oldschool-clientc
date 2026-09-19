/*
 * The browser's OpenGL ES 3.0 renderer, on a WebGL2 context.
 *
 * Every function here is one line: this lane's name for a core entry point.
 * That is the whole of the difference between this lane and its peer, and
 * having it in a file of its own is what keeps it from drifting back into
 * the core as a #if on the platform.
 *
 * @see platform_renderer_webgl2.h for what this lane is, and
 *      platform_renderer_es3.h for what the core does.
 */

#include "platform/platform_renderer_webgl2.h"

/*
 * The one cast in the lane, in the one file entitled to make it: the handle
 * this lane hands out is the core's, under this lane's name. Nothing
 * dereferences through struct ToriRS_WebGL2 -- it has no definition to
 * dereference -- so there is no aliasing question, only a name.
 */
struct ToriRS_WebGL2*
ToriRS_WebGL2_New(int width, int height)
{
    return (struct ToriRS_WebGL2*)ToriRS_ES3_New(width, height, "WebGL2");
}

void
ToriRS_WebGL2_Free(
    struct ToriRS_WebGL2* renderer)
{
    ToriRS_ES3_Free((struct ToriRS_ES3*)renderer);
}

bool
ToriRS_WebGL2_Init(
    struct ToriRS_WebGL2* renderer,
    ToriRS_GLWindow* window,
    struct ToriDraw_Scene* scene,
    bool z_buffer)
{
    return ToriRS_ES3_Init((struct ToriRS_ES3*)renderer, window, scene, z_buffer);
}

void
ToriRS_WebGL2_SetViewport(
    struct ToriRS_WebGL2* renderer,
    int width,
    int height)
{
    ToriRS_ES3_SetViewport((struct ToriRS_ES3*)renderer, width, height);
}

void
ToriRS_WebGL2_SetInterfaceScaleMode(
    struct ToriRS_WebGL2* renderer,
    int mode)
{
    ToriRS_ES3_SetInterfaceScaleMode((struct ToriRS_ES3*)renderer, mode);
}

void
ToriRS_WebGL2_SetClientScaling(
    struct ToriRS_WebGL2* renderer,
    struct ClientScaleSettings const* settings)
{
    ToriRS_ES3_SetClientScaling((struct ToriRS_ES3*)renderer, settings);
}

void
ToriRS_WebGL2_SetPick(
    struct ToriRS_WebGL2* renderer,
    int mouse_x,
    int mouse_y)
{
    ToriRS_ES3_SetPick((struct ToriRS_ES3*)renderer, mouse_x, mouse_y);
}

struct ToriRS_PickHits const*
ToriRS_WebGL2_PickHits(
    struct ToriRS_WebGL2 const* renderer)
{
    return ToriRS_ES3_PickHits((struct ToriRS_ES3 const*)renderer);
}

void
ToriRS_WebGL2_Execute(
    struct ToriRS_WebGL2* renderer,
    struct ToriRS_RenderCommand const* command)
{
    ToriRS_ES3_Execute((struct ToriRS_ES3*)renderer, command);
}

void
ToriRS_WebGL2_DrawBootBar(
    struct ToriRS_WebGL2* renderer,
    int progress,
    int caption_font_id,
    char const* caption)
{
    ToriRS_ES3_DrawBootBar((struct ToriRS_ES3*)renderer, progress, caption_font_id, caption);
}

void
ToriRS_WebGL2_RenderFrame(
    struct ToriRS_WebGL2* renderer,
    struct ToriRS_Frame* frame)
{
    ToriRS_ES3_RenderFrame((struct ToriRS_ES3*)renderer, frame);
}

bool
ToriRS_WebGL2_ReadPixels(
    struct ToriRS_WebGL2* renderer,
    int* pixels,
    int width,
    int height)
{
    return ToriRS_ES3_ReadPixels((struct ToriRS_ES3*)renderer, pixels, width, height);
}

void
ToriRS_WebGL2_RotmaskSourceChanged(void)
{
    ToriRS_ES3_RotmaskSourceChanged();
}
