#ifndef SRC_RENDER_TORIRS_RENDERER_KIND_H
#define SRC_RENDER_TORIRS_RENDERER_KIND_H

/**
 * Which renderer draws the frame.
 *
 * One list for every lane, so the Client Settings page, the saved preference
 * and main.c's switch all name a renderer the same way. A lane offers the
 * subset its build and its window can start (RS_CS2Host_SetRendererStatus);
 * the rest are never offered there.
 *
 * The depth-buffered variants are separate entries, not a flag, because the
 * depth buffer is a context-creation choice: moving between them is a full
 * renderer restart exactly like moving between APIs.
 *
 * Appended only. The numbers are persisted (device option
 * RS_CS2_DEVICEOPTION_RENDERER stores kind + 1) and restated to plugins as
 * TORIRS_RENDERER_*.
 */
enum ToriRS_RendererKind
{
    TORIRS_RENDERER_KIND_SOFTWARE = 0,
    /** Desktop GL 3.2 core. */
    TORIRS_RENDERER_KIND_OPENGL3,
    TORIRS_RENDERER_KIND_OPENGL3_DEPTH,
    /** Android's OpenGL ES 2.0 renderer (platform_androidarmv7_renderer_opengles2.c). */
    TORIRS_RENDERER_KIND_GLES2,
    TORIRS_RENDERER_KIND_GLES2_DEPTH,
    /** Fixed-function Direct3D 9, the Windows XP lane. */
    TORIRS_RENDERER_KIND_D3D9,
    TORIRS_RENDERER_KIND_D3D9_DEPTH,
    /** The browser's OpenGL ES 3.0 renderer, on a WebGL2 context
     *  (platform_web_renderer_webgl2.c). */
    TORIRS_RENDERER_KIND_WEBGL2,
    TORIRS_RENDERER_KIND_WEBGL2_DEPTH,
    /** The browser's OpenGL ES 2.0 renderer, on a WebGL1 context
     *  (platform_web_renderer_webgl1.c). Its own kind rather than sharing the
     *  GLES2 one, so that every kind has exactly one true name: these two
     *  run the same core (platform_renderer_es2_*.c) but they are not the
     *  same renderer to a player choosing between WebGL 1 and WebGL 2. */
    TORIRS_RENDERER_KIND_WEBGL1,
    TORIRS_RENDERER_KIND_WEBGL1_DEPTH,
    /** Android's OpenGL ES 3.0 renderer (platform_androidarmv7_renderer_opengles3.c), which
     *  runs the same core the browser runs as WebGL2. */
    TORIRS_RENDERER_KIND_GLES3,
    TORIRS_RENDERER_KIND_GLES3_DEPTH,

    TORIRS_RENDERER_KIND_COUNT
};

#define TORIRS_RENDERER_KIND_BIT(kind) (1u << (unsigned)(kind))

#endif
