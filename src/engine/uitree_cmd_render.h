#ifndef UITREE_CMD_RENDER_H
#define UITREE_CMD_RENDER_H

struct ToriDraw_Scene;
struct UITreeEmitDesc;

/** Raster emit commands into a caller-owned ARGB pixel buffer. */
void
UITreeCmd_RenderToPixels(
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
