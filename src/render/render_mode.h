#ifndef SRC_RENDER_RENDER_MODE_H
#define SRC_RENDER_RENDER_MODE_H

/*
 * How the world is drawn this session: what the renderer that started can
 * do, and what the server has asked for.
 *
 * Three facts belong to the renderer and are stated once it has initialized:
 * whether the world is collected for the painter or for a depth buffer, whether
 * the renderer scrolls animated textures itself (so the per-cycle CPU texel
 * rotate is skipped), and whether the software renderer was launched in its
 * OSRS z-buffer form. A software fallback states the painter mode again.
 *
 * The fourth is the server's: ZBUF names a depth-buffer choice for the
 * session, and -1 means it has made none. It is forgotten when the client
 * starts and when a session ends.
 *
 * The software renderer's frame combines them. It draws through its depth
 * buffer when the world was collected for one. It draws the OSRS z-buffer way
 * when the server's choice, once made, says so -- and until then when the
 * renderer the player launched says so -- but only while the world is
 * collected for the painter.
 *
 * This does not know which renderer is running, when a session starts or
 * ends, or what a packet carried. The caller states the renderer's facts,
 * passes the server's choice in, forgets it at the right moments, and hands
 * the answers to the renderer.
 */

#include <stdbool.h>

enum ToriRS_WorldRenderMode
{
    TORIRS_WORLD_PAINTER = 0,
    TORIRS_WORLD_DEPTH = 1,
};

struct RenderMode
{
    /** Selected only after the platform renderer has initialized successfully. */
    enum ToriRS_WorldRenderMode world;
    /** The active renderer scrolls animated textures itself (GPU lanes). */
    bool renderer_animates_textures;
    /** Soft3D draws the world the rev-241 client's way, through its own depth
     *  buffer in two scene passes. */
    bool software_rszbuffer;
    /** ZBUF: the server's depth-buffer choice for this session, -1 when it
     *  has made none. Kept as an int: the replay checkpoint stores it. */
    int server_zbuf;
};

void
RenderMode_SetWorld(
    struct RenderMode* rm,
    enum ToriRS_WorldRenderMode mode);

enum ToriRS_WorldRenderMode
RenderMode_World(struct RenderMode const* rm);

void
RenderMode_SetRendererAnimatesTextures(
    struct RenderMode* rm,
    bool animates);

bool
RenderMode_RendererAnimatesTextures(struct RenderMode const* rm);

void
RenderMode_SetSoftwareRsZBuffer(
    struct RenderMode* rm,
    bool rszbuffer);

/** ZBUF arrived: `enabled` as the packet carried it. */
void
RenderMode_SetServerZbuf(
    struct RenderMode* rm,
    int enabled);

/** The server's choice ends: back to -1, none made. */
void
RenderMode_ForgetServerZbuf(struct RenderMode* rm);

/** The software frame draws through its depth buffer: the world was
 *  collected for one. */
bool
RenderMode_SoftwareDepth(struct RenderMode const* rm);

/** The software frame draws the OSRS z-buffer way: the server's choice when
 *  it has made one, otherwise the launched renderer's, and only while the
 *  world is collected for the painter. */
bool
RenderMode_SoftwareRsZBuffer(struct RenderMode const* rm);

#endif
