#ifndef UITREE_CMD_RENDER_H
#define UITREE_CMD_RENDER_H

struct ToriDraw_Scene;
struct UITreeEmitDesc;
struct ToriPlatform_Renderer_Soft3D;

/** Raster emit commands into a caller-owned ARGB pixel buffer. */
void
UITreeCmd_RenderToPixels(
    struct ToriDraw_Scene* scene,
    struct UITreeEmitDesc const* cmds,
    int cmd_count,
    int* pixels,
    int width,
    int height);

/** The same, through a caller-kept renderer, so a surface redrawn on every
 *  change does not allocate and free one each time. `soft` comes from
 *  ToriPlatform_Renderer_Soft3D_New and keeps only its caches between calls. */
void
UITreeCmd_RenderToPixelsWith(
    struct ToriPlatform_Renderer_Soft3D* soft,
    struct ToriDraw_Scene* scene,
    struct UITreeEmitDesc const* cmds,
    int cmd_count,
    int* pixels,
    int width,
    int height);

/** Raster emit commands using scene_id lookups into scene → BMP file. */
int
UITreeCmd_WriteBmp(
    struct ToriDraw_Scene* scene,
    struct UITreeEmitDesc const* cmds,
    int cmd_count,
    char const* path,
    int width,
    int height);

#endif
