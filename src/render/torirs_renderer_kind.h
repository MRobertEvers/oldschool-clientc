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
    /** OpenGL ES 2.0: EGL on Android, WebGL1 in the browser. */
    TORIRS_RENDERER_KIND_GLES2,
    TORIRS_RENDERER_KIND_GLES2_DEPTH,
    /** Fixed-function Direct3D 9, the Windows XP lane. */
    TORIRS_RENDERER_KIND_D3D9,
    TORIRS_RENDERER_KIND_D3D9_DEPTH,
    /** OpenGL ES 3.0: WebGL2 in the browser. A separate renderer from the
     *  GLES2 one, not a mode of it. */
    TORIRS_RENDERER_KIND_WEBGL2,
    TORIRS_RENDERER_KIND_WEBGL2_DEPTH,

    TORIRS_RENDERER_KIND_COUNT
};

#define TORIRS_RENDERER_KIND_BIT(kind) (1u << (unsigned)(kind))

#endif
