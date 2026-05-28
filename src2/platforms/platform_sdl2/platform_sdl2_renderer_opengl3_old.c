#include "platform_sdl2_renderer_opengl3.h"

#include "../trspk_toridraw.h"
#include "games/model_viewer.h"
#include "libtorirs.h"
#include "libtorirs_internal.h"
#include "platforms/ToriRSPlatformKit/include/ToriRSPlatformKit/trspk_math.h"
#include "platforms/ToriRSPlatformKit/include/ToriRSPlatformKit/trspk_types.h"
#include "platforms/ToriRSPlatformKit/src/backends/webgl1/webgl1_vertex.h"
#include "platforms/ToriRSPlatformKit/src/tools/trspk_batch32.h"
#include "platforms/ToriRSPlatformKit/src/tools/trspk_facebuffer.h"
#include "platforms/ToriRSPlatformKit/src/tools/trspk_resource_cache.h"
#include "render/libtorirs_render.h"
#include "toridraw/toridraw.h"
#include "toridraw/toridraw_types.h"
#include "world/world_scene.h"
#include "world/world_scene_events.h"
#include <SDL_opengl.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#define OGL3_SHADER_VERSION "#version 150 core\n"
#else
#define OGL3_SHADER_VERSION "#version 330 core\n"
#endif

#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif

typedef struct OGL3WorldShaderLocs
{
    int32_t a_position;
    int32_t a_color;
    int32_t a_texcoord;
    int32_t a_tex_id;
    int32_t a_uv_mode;
    int32_t s_atlas;
} OGL3WorldShaderLocs;

typedef struct OGL3Uniforms
{
    float modelViewMatrix[16];
    float projectionMatrix[16];
    float uClock;
    float _pad[3];
} OGL3Uniforms;

#define OGL3_DYNAMIC_VBO_SIZE (2u * 1024u * 1024u)
#define OGL3_DYNAMIC_EBO_SIZE (512u * 1024u)
#define OGL3_MAX_PENDING_DRAWS 32u
#define OGL3_TEXTURE_RGBA_SCRATCH_BYTES (128u * 128u * 4u)

typedef struct OGL3_SortedDraw
{
    GLuint vbo;
    GLuint ring_ebo;
    uint32_t vbo_byte_offset;
    uint32_t sorted_ebo_byte_offset;
    uint32_t index_count;
} OGL3_SortedDraw;

typedef struct OGL3_DynamicRing
{
    GLuint vbo;
    GLuint ebo;
    GLuint vao;
    uint32_t vbo_offset;
    uint32_t ebo_offset;
    uint32_t vbo_capacity;
    uint32_t ebo_capacity;
} OGL3_DynamicRing;

struct LibToriPlatformSDL2_RendererGL3
{
    SDL_Window* window;
    SDL_GLContext gl_context;
    uint32_t prog_world3d;
    OGL3WorldShaderLocs world_locs;
    uint32_t ubo;
    uint32_t atlas_texture;
    TRSPK_ResourceCache* cache;
    TRSPK_Batch32* batch_staging;
    TRSPK_FaceBuffer32 facebuffer;
    TRSPK_BatchId current_batch_id;
    bool batch_active;
    int width;
    int height;
    int window_width;
    int window_height;
    bool ready;
    double frame_clock;
    bool atlas_dirty;
    OGL3_DynamicRing dynamic_ring;
};

/* --- GL proc pointers --- */
typedef void (*PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void (*PFNGLUNIFORM1IPROC)(
    GLint location,
    GLint v0);
typedef void (*PFNGLUNIFORMMATRIX4FVPROC)(
    GLint location,
    GLsizei count,
    GLboolean transpose,
    const GLfloat* value);
typedef void (*PFNGLGENVERTEXARRAYSPROC)(
    GLsizei n,
    GLuint* arrays);
typedef void (*PFNGLDELETEVERTEXARRAYSPROC)(
    GLsizei n,
    const GLuint* arrays);
typedef void (*PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (*PFNGLGENBUFFERSPROC)(
    GLsizei n,
    GLuint* buffers);
typedef void (*PFNGLDELETEBUFFERSPROC)(
    GLsizei n,
    const GLuint* buffers);
typedef void (*PFNGLBINDBUFFERPROC)(
    GLenum target,
    GLuint buffer);
typedef void (*PFNGLBUFFERDATAPROC)(
    GLenum target,
    GLsizeiptr size,
    const void* data,
    GLenum usage);
typedef void (*PFNGLBUFFERSUBDATAPROC)(
    GLenum target,
    GLintptr offset,
    GLsizeiptr size,
    const void* data);
typedef void (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (*PFNGLVERTEXATTRIBPOINTERPROC)(
    GLuint index,
    GLint size,
    GLenum type,
    GLboolean normalized,
    GLsizei stride,
    const void* pointer);
typedef void (*PFNGLDRAWELEMENTSPROC)(
    GLenum mode,
    GLsizei count,
    GLenum type,
    const void* indices);
typedef void (*PFNGLBINDTEXTUREPROC)(
    GLenum target,
    GLuint texture);
typedef void (*PFNGLGENTEXTURESPROC)(
    GLsizei n,
    GLuint* textures);
typedef void (*PFNGLTEXIMAGE2DPROC)(
    GLenum target,
    GLint level,
    GLint internalformat,
    GLsizei width,
    GLsizei height,
    GLint border,
    GLenum format,
    GLenum type,
    const void* pixels);
typedef void (*PFNGLTEXPARAMETERIPROC)(
    GLenum target,
    GLenum pname,
    GLint param);
typedef void (*PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef GLuint (*PFNGLCREATEPROGRAMPROC)(void);
typedef GLuint (*PFNGLCREATESHADERPROC)(GLenum type);
typedef void (*PFNGLSHADERSOURCEPROC)(
    GLuint shader,
    GLsizei count,
    const GLchar* const* string,
    const GLint* length);
typedef void (*PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (*PFNGLGETSHADERIVPROC)(
    GLuint shader,
    GLenum pname,
    GLint* params);
typedef void (*PFNGLGETSHADERINFOLOGPROC)(
    GLuint shader,
    GLsizei bufSize,
    GLsizei* length,
    GLchar* infoLog);
typedef void (*PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (*PFNGLATTACHSHADERPROC)(
    GLuint program,
    GLuint shader);
typedef void (*PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (*PFNGLGETPROGRAMIVPROC)(
    GLuint program,
    GLenum pname,
    GLint* params);
typedef void (*PFNGLGETPROGRAMINFOLOGPROC)(
    GLuint program,
    GLsizei bufSize,
    GLsizei* length,
    GLchar* infoLog);
typedef void (*PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef GLint (*O3_GetAttribLocationProc)(
    GLuint program,
    const GLchar* name);
typedef GLint (*O3_GetUniformLocationProc)(
    GLuint program,
    const GLchar* name);
typedef void (*PFNGLBINDATTRIBLOCATIONPROC)(
    GLuint program,
    GLuint index,
    const GLchar* name);
typedef GLuint (*PFNGLGETUNIFORMBLOCKINDEXPROC)(
    GLuint program,
    const GLchar* uniformBlockName);
typedef void (*PFNGLUNIFORMBLOCKBINDINGPROC)(
    GLuint program,
    GLuint uniformBlockIndex,
    GLuint uniformBlockBinding);
typedef void (*PFNGLBINDBUFFERRANGEPROC)(
    GLenum target,
    GLuint index,
    GLuint buffer,
    GLintptr offset,
    GLsizeiptr size);

static PFNGLUSEPROGRAMPROC ogl3_glUseProgram;
static PFNGLUNIFORM1IPROC ogl3_glUniform1i;
static PFNGLUNIFORMMATRIX4FVPROC ogl3_glUniformMatrix4fv;
static PFNGLGENVERTEXARRAYSPROC ogl3_glGenVertexArrays;
static PFNGLDELETEVERTEXARRAYSPROC ogl3_glDeleteVertexArrays;
static PFNGLBINDVERTEXARRAYPROC ogl3_glBindVertexArray;
static PFNGLGENBUFFERSPROC ogl3_glGenBuffers;
static PFNGLDELETEBUFFERSPROC ogl3_glDeleteBuffers;
static PFNGLBINDBUFFERPROC ogl3_glBindBuffer;
static PFNGLBUFFERDATAPROC ogl3_glBufferData;
static PFNGLBUFFERSUBDATAPROC ogl3_glBufferSubData;
static PFNGLENABLEVERTEXATTRIBARRAYPROC ogl3_glEnableVertexAttribArray;
static PFNGLVERTEXATTRIBPOINTERPROC ogl3_glVertexAttribPointer;
static PFNGLDRAWELEMENTSPROC ogl3_glDrawElements;
static PFNGLBINDTEXTUREPROC ogl3_glBindTexture;
static PFNGLGENTEXTURESPROC ogl3_glGenTextures;
static PFNGLTEXIMAGE2DPROC ogl3_glTexImage2D;
static PFNGLTEXPARAMETERIPROC ogl3_glTexParameteri;
static PFNGLACTIVETEXTUREPROC ogl3_glActiveTexture;
static PFNGLCREATEPROGRAMPROC ogl3_glCreateProgram;
static PFNGLCREATESHADERPROC ogl3_glCreateShader;
static PFNGLSHADERSOURCEPROC ogl3_glShaderSource;
static PFNGLCOMPILESHADERPROC ogl3_glCompileShader;
static PFNGLGETSHADERIVPROC ogl3_glGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC ogl3_glGetShaderInfoLog;
static PFNGLDELETESHADERPROC ogl3_glDeleteShader;
static PFNGLATTACHSHADERPROC ogl3_glAttachShader;
static PFNGLLINKPROGRAMPROC ogl3_glLinkProgram;
static PFNGLGETPROGRAMIVPROC ogl3_glGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC ogl3_glGetProgramInfoLog;
static PFNGLDELETEPROGRAMPROC ogl3_glDeleteProgram;
static O3_GetAttribLocationProc ogl3_glGetAttribLocation;
static O3_GetUniformLocationProc ogl3_glGetUniformLocation;
static PFNGLBINDATTRIBLOCATIONPROC ogl3_glBindAttribLocation;
static PFNGLGETUNIFORMBLOCKINDEXPROC ogl3_glGetUniformBlockIndex;
static PFNGLUNIFORMBLOCKBINDINGPROC ogl3_glUniformBlockBinding;
static PFNGLBINDBUFFERRANGEPROC ogl3_glBindBufferRange;

// clang-format off
#define O3L(name, Type, field)                                           \
    do                                                                   \
    {                                                                    \
        void* p = SDL_GL_GetProcAddress(#name);                          \
        if( !p )                                                         \
        {                                                                \
            fprintf(stderr, "OpenGL3: missing proc %s\n", #name);        \
            return false;                                                \
        }                                                                \
        field = (Type)p;                                                 \
    } while( 0 )
// clang-format on

static bool
ogl3_load_gl_procs(void)
{
    O3L(glUseProgram, PFNGLUSEPROGRAMPROC, ogl3_glUseProgram);
    O3L(glUniform1i, PFNGLUNIFORM1IPROC, ogl3_glUniform1i);
    O3L(glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC, ogl3_glUniformMatrix4fv);
    O3L(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC, ogl3_glGenVertexArrays);
    O3L(glDeleteVertexArrays, PFNGLDELETEVERTEXARRAYSPROC, ogl3_glDeleteVertexArrays);
    O3L(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC, ogl3_glBindVertexArray);
    O3L(glGenBuffers, PFNGLGENBUFFERSPROC, ogl3_glGenBuffers);
    O3L(glDeleteBuffers, PFNGLDELETEBUFFERSPROC, ogl3_glDeleteBuffers);
    O3L(glBindBuffer, PFNGLBINDBUFFERPROC, ogl3_glBindBuffer);
    O3L(glBufferData, PFNGLBUFFERDATAPROC, ogl3_glBufferData);
    O3L(glBufferSubData, PFNGLBUFFERSUBDATAPROC, ogl3_glBufferSubData);
    O3L(glEnableVertexAttribArray,
        PFNGLENABLEVERTEXATTRIBARRAYPROC,
        ogl3_glEnableVertexAttribArray);
    O3L(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC, ogl3_glVertexAttribPointer);
    O3L(glDrawElements, PFNGLDRAWELEMENTSPROC, ogl3_glDrawElements);
    O3L(glBindTexture, PFNGLBINDTEXTUREPROC, ogl3_glBindTexture);
    O3L(glGenTextures, PFNGLGENTEXTURESPROC, ogl3_glGenTextures);
    O3L(glTexImage2D, PFNGLTEXIMAGE2DPROC, ogl3_glTexImage2D);
    O3L(glTexParameteri, PFNGLTEXPARAMETERIPROC, ogl3_glTexParameteri);
    O3L(glActiveTexture, PFNGLACTIVETEXTUREPROC, ogl3_glActiveTexture);
    O3L(glCreateProgram, PFNGLCREATEPROGRAMPROC, ogl3_glCreateProgram);
    O3L(glCreateShader, PFNGLCREATESHADERPROC, ogl3_glCreateShader);
    O3L(glShaderSource, PFNGLSHADERSOURCEPROC, ogl3_glShaderSource);
    O3L(glCompileShader, PFNGLCOMPILESHADERPROC, ogl3_glCompileShader);
    O3L(glGetShaderiv, PFNGLGETSHADERIVPROC, ogl3_glGetShaderiv);
    O3L(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC, ogl3_glGetShaderInfoLog);
    O3L(glDeleteShader, PFNGLDELETESHADERPROC, ogl3_glDeleteShader);
    O3L(glAttachShader, PFNGLATTACHSHADERPROC, ogl3_glAttachShader);
    O3L(glLinkProgram, PFNGLLINKPROGRAMPROC, ogl3_glLinkProgram);
    O3L(glGetProgramiv, PFNGLGETPROGRAMIVPROC, ogl3_glGetProgramiv);
    O3L(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC, ogl3_glGetProgramInfoLog);
    O3L(glDeleteProgram, PFNGLDELETEPROGRAMPROC, ogl3_glDeleteProgram);
    O3L(glGetAttribLocation, O3_GetAttribLocationProc, ogl3_glGetAttribLocation);
    O3L(glGetUniformLocation, O3_GetUniformLocationProc, ogl3_glGetUniformLocation);
    O3L(glBindAttribLocation, PFNGLBINDATTRIBLOCATIONPROC, ogl3_glBindAttribLocation);
    O3L(glGetUniformBlockIndex, PFNGLGETUNIFORMBLOCKINDEXPROC, ogl3_glGetUniformBlockIndex);
    O3L(glUniformBlockBinding, PFNGLUNIFORMBLOCKBINDINGPROC, ogl3_glUniformBlockBinding);
    O3L(glBindBufferRange, PFNGLBINDBUFFERRANGEPROC, ogl3_glBindBufferRange);
    return true;
}

static GLuint
ogl3_compile_shader(
    GLenum type,
    const char* src)
{
    GLuint shader = ogl3_glCreateShader(type);
    if( shader == 0u )
        return 0u;
    ogl3_glShaderSource(shader, 1, &src, NULL);
    ogl3_glCompileShader(shader);
    GLint ok = 0;
    ogl3_glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char buf[512];
        ogl3_glGetShaderInfoLog(shader, (GLsizei)sizeof(buf), NULL, buf);
        fprintf(stderr, "OpenGL3 shader compile failed: %s\n", buf);
        ogl3_glDeleteShader(shader);
        return 0u;
    }
    return shader;
}

static uint32_t
ogl3_create_world_program(OGL3WorldShaderLocs* locs)
{
    if( !locs )
        return 0u;
    memset(locs, -1, sizeof(*locs));

    const char* vs =
        OGL3_SHADER_VERSION "layout(std140) uniform TRSPK_UboWorld {\n"
                            "  mat4 u_modelViewMatrix;\n"
                            "  mat4 u_projectionMatrix;\n"
                            "  vec4 u_clock_pad;\n"
                            "} ubo;\n"
                            "in vec4 a_position;\n"
                            "in vec4 a_color;\n"
                            "in vec2 a_texcoord;\n"
                            "in float a_tex_id;\n"
                            "in float a_uv_mode;\n"
                            "out vec4 v_color;\n"
                            "out vec2 v_texcoord;\n"
                            "out float v_tex_id;\n"
                            "out float v_uv_pack;\n"
                            "void main() {\n"
                            "  vec4 wp = vec4(a_position.xyz, 1.0);\n"
                            "  gl_Position = ubo.u_projectionMatrix * ubo.u_modelViewMatrix * wp;\n"
                            "  v_color = a_color;\n"
                            "  v_texcoord = a_texcoord;\n"
                            "  v_tex_id = a_tex_id;\n"
                            "  v_uv_pack = a_uv_mode;\n"
                            "}\n";

    const char* fs = OGL3_SHADER_VERSION
        "layout(std140) uniform TRSPK_UboWorld {\n"
        "  mat4 u_modelViewMatrix;\n"
        "  mat4 u_projectionMatrix;\n"
        "  vec4 u_clock_pad;\n"
        "} ubo;\n"
        "in vec4 v_color;\n"
        "in vec2 v_texcoord;\n"
        "in float v_tex_id;\n"
        "in float v_uv_pack;\n"
        "uniform sampler2D s_atlas;\n"
        "out vec4 frag_color;\n"
        "void main() {\n"
        "  int raw = int(floor(v_tex_id + 0.5));\n"
        "  int atlas_id;\n"
        "  bool cutout;\n"
        "  if (raw >= 256) {\n"
        "    atlas_id = raw - 256;\n"
        "    cutout = true;\n"
        "  } else {\n"
        "    atlas_id = raw;\n"
        "    cutout = false;\n"
        "  }\n"
        "  if (atlas_id < 0 || atlas_id >= 256) {\n"
        "    frag_color = vec4(v_color.rgb, v_color.a);\n"
        "    return;\n"
        "  }\n"
        "  int enc = int(floor(v_uv_pack * 0.5 + 0.5));\n"
        "  float anim_u = 0.0;\n"
        "  float anim_v = 0.0;\n"
        "  if (enc > 0) {\n"
        "    if (enc < 257) {\n"
        "      int mag_u = enc - 1;\n"
        "      anim_u = float(mag_u) / 256.0;\n"
        "    } else {\n"
        "      int mag_v = enc - 257;\n"
        "      anim_v = float(mag_v) / 256.0;\n"
        "    }\n"
        "  }\n"
        "  const float TEX_DIM = 128.0;\n"
        "  const float ATLAS_DIM = 2048.0;\n"
        "  const float cols = 16.0;\n"
        "  float du = TEX_DIM / ATLAS_DIM;\n"
        "  float dv = du;\n"
        "  float col = mod(float(atlas_id), cols);\n"
        "  float row = floor(float(atlas_id) / cols);\n"
        "  vec4 ta = vec4(col * du, row * dv, du, dv);\n"
        "  vec2 local = v_texcoord;\n"
        "  float clk = ubo.u_clock_pad.x;\n"
        "  if (anim_u > 0.0) local.x += clk * anim_u;\n"
        "  if (anim_v > 0.0) local.y -= clk * anim_v;\n"
        "  local.x = clamp(local.x, 0.008, 0.992);\n"
        "  local.y = clamp(fract(local.y), 0.008, 0.992);\n"
        "  vec2 uv_atlas = ta.xy + local * ta.zw;\n"
        "  vec4 texColor = texture(s_atlas, uv_atlas);\n"
        "  vec3 finalColor = mix(v_color.rgb, texColor.rgb * v_color.rgb, 1.0);\n"
        "  float finalAlpha = v_color.a;\n"
        "  if (cutout) {\n"
        "    if (texColor.a < 0.5) discard;\n"
        "  }\n"
        "  frag_color = vec4(finalColor, finalAlpha);\n"
        "}\n";

    GLuint p = ogl3_glCreateProgram();
    GLuint v = ogl3_compile_shader(GL_VERTEX_SHADER, vs);
    GLuint f = ogl3_compile_shader(GL_FRAGMENT_SHADER, fs);
    if( p == 0u || v == 0u || f == 0u )
    {
        if( v )
            ogl3_glDeleteShader(v);
        if( f )
            ogl3_glDeleteShader(f);
        if( p )
            ogl3_glDeleteProgram(p);
        return 0u;
    }
    ogl3_glAttachShader(p, v);
    ogl3_glAttachShader(p, f);
    ogl3_glBindAttribLocation(p, 0u, "a_position");
    ogl3_glBindAttribLocation(p, 1u, "a_color");
    ogl3_glBindAttribLocation(p, 2u, "a_texcoord");
    ogl3_glBindAttribLocation(p, 3u, "a_tex_id");
    ogl3_glBindAttribLocation(p, 4u, "a_uv_mode");
    ogl3_glLinkProgram(p);
    ogl3_glDeleteShader(v);
    ogl3_glDeleteShader(f);
    GLint ok = 0;
    ogl3_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if( !ok )
    {
        char buf[512];
        ogl3_glGetProgramInfoLog(p, (GLsizei)sizeof(buf), NULL, buf);
        fprintf(stderr, "OpenGL3 world program link failed: %s\n", buf);
        ogl3_glDeleteProgram(p);
        return 0u;
    }
    locs->a_position = ogl3_glGetAttribLocation(p, "a_position");
    locs->a_color = ogl3_glGetAttribLocation(p, "a_color");
    locs->a_texcoord = ogl3_glGetAttribLocation(p, "a_texcoord");
    locs->a_tex_id = ogl3_glGetAttribLocation(p, "a_tex_id");
    locs->a_uv_mode = ogl3_glGetAttribLocation(p, "a_uv_mode");
    locs->s_atlas = ogl3_glGetUniformLocation(p, "s_atlas");
    {
        const GLuint block_ix = ogl3_glGetUniformBlockIndex(p, "TRSPK_UboWorld");
        if( block_ix != GL_INVALID_INDEX )
            ogl3_glUniformBlockBinding(p, block_ix, 0u);
    }
    return (uint32_t)p;
}

static void
ogl3_bind_world_attribs(
    const OGL3WorldShaderLocs* locs,
    uint32_t mesh_vbo)
{
    if( !locs || mesh_vbo == 0u )
        return;
    const GLsizei stride = (GLsizei)sizeof(TRSPK_VertexWebGL1);
    ogl3_glBindBuffer(GL_ARRAY_BUFFER, mesh_vbo);
    if( locs->a_position >= 0 )
    {
        ogl3_glEnableVertexAttribArray((GLuint)locs->a_position);
        ogl3_glVertexAttribPointer(
            (GLuint)locs->a_position, 4, GL_FLOAT, GL_FALSE, stride, (const void*)0);
    }
    if( locs->a_color >= 0 )
    {
        ogl3_glEnableVertexAttribArray((GLuint)locs->a_color);
        ogl3_glVertexAttribPointer(
            (GLuint)locs->a_color, 4, GL_FLOAT, GL_FALSE, stride, (const void*)(uintptr_t)16u);
    }
    if( locs->a_texcoord >= 0 )
    {
        ogl3_glEnableVertexAttribArray((GLuint)locs->a_texcoord);
        ogl3_glVertexAttribPointer(
            (GLuint)locs->a_texcoord, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(uintptr_t)32u);
    }
    if( locs->a_tex_id >= 0 )
    {
        ogl3_glEnableVertexAttribArray((GLuint)locs->a_tex_id);
        ogl3_glVertexAttribPointer(
            (GLuint)locs->a_tex_id, 1, GL_FLOAT, GL_FALSE, stride, (const void*)(uintptr_t)40u);
    }
    if( locs->a_uv_mode >= 0 )
    {
        ogl3_glEnableVertexAttribArray((GLuint)locs->a_uv_mode);
        ogl3_glVertexAttribPointer(
            (GLuint)locs->a_uv_mode, 1, GL_FLOAT, GL_FALSE, stride, (const void*)(uintptr_t)44u);
    }
}

static void
ogl3_world_vao_setup(
    uint32_t vao,
    const OGL3WorldShaderLocs* locs,
    uint32_t mesh_vbo,
    uint32_t ibo)
{
    if( !locs || vao == 0u || mesh_vbo == 0u || ibo == 0u )
        return;
    ogl3_glBindVertexArray(vao);
    ogl3_bind_world_attribs(locs, mesh_vbo);
    ogl3_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
}

static bool
ogl3_ensure_atlas_initialized(struct LibToriPlatformSDL2_RendererGL3* r)
{
    if( !r || !r->cache )
        return false;
    if( trspk_resource_cache_get_atlas_pixels(r->cache) )
        return true;
    return trspk_resource_cache_init_atlas(r->cache, TRSPK_ATLAS_DIMENSION, TRSPK_ATLAS_DIMENSION);
}

static bool
ogl3_load_texture(
    struct LibToriPlatformSDL2_RendererGL3* r,
    int texture_id,
    struct ToriDraw_Texture* tex)
{
    if( !r || !r->cache || !tex || !tex->texels || texture_id < 0 || texture_id >= 256 )
        return false;

    if( !ogl3_ensure_atlas_initialized(r) )
        return false;

    static uint8_t rgba_scratch[OGL3_TEXTURE_RGBA_SCRATCH_BYTES];
    const uint8_t* rgba = NULL;
    uint32_t rgba_size = 0u;
    trspk_toridraw_fill_rgba128(
        tex, rgba_scratch, (uint32_t)sizeof(rgba_scratch), &rgba, &rgba_size);
    if( !rgba )
        return false;

    float anim_u = 0.0f;
    float anim_v = 0.0f;
    const float scroll =
        trspk_texture_animation_signed(tex->animation_direction, tex->animation_speed);
    if( scroll >= 0.0f )
        anim_u = scroll;
    else
        anim_v = -scroll;

    return trspk_resource_cache_load_texture_128(
        r->cache, (TRSPK_TextureId)texture_id, rgba, anim_u, anim_v, tex->opaque);
}

static bool
ogl3_unload_texture(
    struct LibToriPlatformSDL2_RendererGL3* r,
    int texture_id)
{
    if( !r || !r->cache || texture_id < 0 || texture_id >= 256 )
        return false;
    if( !trspk_resource_cache_unload_texture_128(r->cache, (TRSPK_TextureId)texture_id) )
        return false;
    if( trspk_resource_cache_loaded_texture_count(r->cache) == 0u )
    {
        trspk_resource_cache_unload_atlas(r->cache);
        if( r->atlas_texture != 0u )
        {
            GLuint tex = r->atlas_texture;
            glDeleteTextures(1, &tex);
            r->atlas_texture = 0u;
        }
    }
    return true;
}

static void
ogl3_refresh_atlas(struct LibToriPlatformSDL2_RendererGL3* r)
{
    if( !r || !r->cache )
        return;
    const uint8_t* pixels = trspk_resource_cache_get_atlas_pixels(r->cache);
    const uint32_t width = trspk_resource_cache_get_atlas_width(r->cache);
    const uint32_t height = trspk_resource_cache_get_atlas_height(r->cache);
    if( !pixels || width == 0u || height == 0u )
        return;

    GLuint tex = r->atlas_texture;
    if( tex == 0u )
        ogl3_glGenTextures(1, &tex);
    r->atlas_texture = tex;
    ogl3_glBindTexture(GL_TEXTURE_2D, tex);
    ogl3_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ogl3_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ogl3_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ogl3_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ogl3_glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        (GLsizei)width,
        (GLsizei)height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels);
}

static void
ogl3_batch_submit(
    struct LibToriPlatformSDL2_RendererGL3* r,
    TRSPK_BatchId batch_id)
{
    if( !r || !r->batch_staging || !r->cache )
        return;

    const void* vertices = NULL;
    const void* indices = NULL;
    uint32_t vertex_bytes = 0u;
    uint32_t index_bytes = 0u;
    trspk_batch32_get_data(r->batch_staging, &vertices, &vertex_bytes, &indices, &index_bytes);
    if( !vertices || !indices || vertex_bytes == 0u || index_bytes == 0u )
        return;

    GLuint vbo = 0u;
    GLuint ebo = 0u;
    ogl3_glGenBuffers(1, &vbo);
    ogl3_glGenBuffers(1, &ebo);
    ogl3_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    ogl3_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vertex_bytes, vertices, GL_STATIC_DRAW);
    ogl3_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    ogl3_glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)index_bytes, indices, GL_STATIC_DRAW);
    ogl3_glBindBuffer(GL_ARRAY_BUFFER, 0);
    ogl3_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    TRSPK_GPUHandle hvbo = (TRSPK_GPUHandle)(uintptr_t)vbo;
    TRSPK_GPUHandle hebo = (TRSPK_GPUHandle)(uintptr_t)ebo;
    trspk_resource_cache_batch_set_resource(r->cache, batch_id, hvbo, hebo);

    GLuint world_vao = 0u;
    ogl3_glGenVertexArrays(1, &world_vao);
    if( world_vao != 0u )
    {
        ogl3_world_vao_setup(world_vao, &r->world_locs, vbo, ebo);
        trspk_resource_cache_batch_set_world_vao(
            r->cache, batch_id, (TRSPK_GPUHandle)(uintptr_t)world_vao);
    }

    const uint32_t count = trspk_batch32_entry_count(r->batch_staging);
    for( uint32_t i = 0; i < count; ++i )
    {
        const TRSPK_Batch32Entry* e = trspk_batch32_get_entry(r->batch_staging, i);
        if( !e )
            continue;
        const uint32_t pose_idx = trspk_resource_cache_allocate_pose_slot(
            r->cache, e->model_id, e->gpu_segment_slot, (int)e->frame_index);
        TRSPK_ModelPose pose = { 0 };
        pose.vbo = hvbo;
        pose.ebo = hebo;
        pose.world_vao = (TRSPK_GPUHandle)(uintptr_t)world_vao;
        pose.vbo_offset = e->vbo_offset;
        pose.ebo_offset = e->ebo_offset;
        pose.element_count = e->element_count;
        pose.batch_id = batch_id;
        pose.valid = true;
        trspk_resource_cache_set_model_pose(r->cache, e->model_id, pose_idx, &pose);
    }
}

static void
ogl3_batch_clear(
    struct LibToriPlatformSDL2_RendererGL3* r,
    TRSPK_BatchId batch_id)
{
    if( !r || !r->cache )
        return;
    trspk_resource_cache_invalidate_poses_for_batch(r->cache, batch_id);
    TRSPK_BatchResource old = trspk_resource_cache_batch_clear(r->cache, batch_id);
    if( old.world_vao != 0u )
    {
        GLuint wv = (GLuint)(uintptr_t)old.world_vao;
        ogl3_glDeleteVertexArrays(1, &wv);
    }
    if( old.vbo != 0u )
    {
        GLuint v = (GLuint)(uintptr_t)old.vbo;
        ogl3_glDeleteBuffers(1, &v);
    }
    if( old.ebo != 0u )
    {
        GLuint e = (GLuint)(uintptr_t)old.ebo;
        ogl3_glDeleteBuffers(1, &e);
    }
}

static void
ogl3_ring_upload_model(
    struct LibToriPlatformSDL2_RendererGL3* r,
    const struct ToriDraw_Model* model,
    TRSPK_ModelId model_id,
    const TRSPK_BakeTransform* bake)
{
    if( !r || !model || !r->batch_staging || !r->cache || (uint32_t)model_id >= TRSPK_MAX_MODELS )
        return;

    OGL3_DynamicRing* ring = &r->dynamic_ring;
    if( ring->vbo == 0u || ring->ebo == 0u || ring->vao == 0u )
        return;

    trspk_batch32_clear(r->batch_staging);
    trspk_batch32_begin(r->batch_staging);
    trspk_toridraw_batch_add_model32(
        r->batch_staging, model, (uint16_t)model_id, TRSPK_GPU_ANIM_NONE_IDX, 0u, bake, r->cache);
    trspk_batch32_end(r->batch_staging);

    const void* vertices = NULL;
    const void* indices = NULL;
    uint32_t vertex_bytes = 0u;
    uint32_t index_bytes = 0u;
    trspk_batch32_get_data(r->batch_staging, &vertices, &vertex_bytes, &indices, &index_bytes);
    if( !vertices || !indices || vertex_bytes == 0u || index_bytes == 0u )
        return;

    const TRSPK_Batch32Entry* e = trspk_batch32_get_entry(r->batch_staging, 0u);
    if( !e )
        return;

    const uint32_t stride = (uint32_t)sizeof(TRSPK_VertexWebGL1);
    const uint32_t entry_vbo_bytes = e->vbo_offset * stride;
    const uint32_t entry_ebo_bytes = e->ebo_offset * (uint32_t)sizeof(uint32_t);

    if( ring->vbo_offset + vertex_bytes > ring->vbo_capacity )
        ring->vbo_offset = 0u;
    if( ring->ebo_offset + index_bytes > ring->ebo_capacity )
        ring->ebo_offset = 0u;

    const uint32_t vbo_byte = ring->vbo_offset;
    const uint32_t ebo_byte = ring->ebo_offset;

    ogl3_glBindBuffer(GL_ARRAY_BUFFER, ring->vbo);
    ogl3_glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)vbo_byte, (GLsizeiptr)vertex_bytes, vertices);
    ogl3_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ring->ebo);
    ogl3_glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER, (GLintptr)ebo_byte, (GLsizeiptr)index_bytes, indices);
    ogl3_glBindBuffer(GL_ARRAY_BUFFER, 0);
    ogl3_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    ring->vbo_offset += vertex_bytes;
    ring->ebo_offset += index_bytes;

    const uint32_t pose_idx =
        trspk_resource_cache_allocate_pose_slot(r->cache, model_id, TRSPK_GPU_ANIM_NONE_IDX, 0);
    TRSPK_ModelPose pose = { 0 };
    pose.vbo = (TRSPK_GPUHandle)(uintptr_t)ring->vbo;
    pose.ebo = (TRSPK_GPUHandle)(uintptr_t)ring->ebo;
    pose.world_vao = (TRSPK_GPUHandle)(uintptr_t)ring->vao;
    pose.vbo_offset = vbo_byte + entry_vbo_bytes;
    pose.ebo_offset = ebo_byte + entry_ebo_bytes;
    pose.element_count = e->element_count;
    pose.dynamic = true;
    pose.valid = true;
    trspk_resource_cache_set_model_pose(r->cache, model_id, pose_idx, &pose);
}

static float
ogl3_dash_yaw_to_radians(int dash_yaw)
{
    return ((float)dash_yaw * 2.0f * (float)M_PI) / 2048.0f;
}

static void
ogl3_compute_view_matrix(
    float* out_matrix,
    float camera_x,
    float camera_y,
    float camera_z,
    float pitch,
    float yaw)
{
    float cosPitch = cosf(-pitch);
    float sinPitch = sinf(-pitch);
    float cosYaw = cosf(-yaw);
    float sinYaw = sinf(-yaw);

    out_matrix[0] = cosYaw;
    out_matrix[1] = sinYaw * sinPitch;
    out_matrix[2] = sinYaw * cosPitch;
    out_matrix[3] = 0.0f;
    out_matrix[4] = 0.0f;
    out_matrix[5] = cosPitch;
    out_matrix[6] = -sinPitch;
    out_matrix[7] = 0.0f;
    out_matrix[8] = -sinYaw;
    out_matrix[9] = cosYaw * sinPitch;
    out_matrix[10] = cosYaw * cosPitch;
    out_matrix[11] = 0.0f;
    out_matrix[12] = -camera_x * cosYaw + camera_z * sinYaw;
    out_matrix[13] =
        -camera_x * sinYaw * sinPitch - camera_y * cosPitch - camera_z * cosYaw * sinPitch;
    out_matrix[14] =
        -camera_x * sinYaw * cosPitch + camera_y * sinPitch - camera_z * cosYaw * cosPitch;
    out_matrix[15] = 1.0f;
}

static void
ogl3_compute_projection_matrix(
    float* out_matrix,
    float fov,
    float screen_width,
    float screen_height)
{
    float y = 1.0f / tanf(fov * 0.5f);
    float x = y;
    out_matrix[0] = x * 512.0f / (screen_width / 2.0f);
    out_matrix[1] = 0.0f;
    out_matrix[2] = 0.0f;
    out_matrix[3] = 0.0f;
    out_matrix[4] = 0.0f;
    out_matrix[5] = -y * 512.0f / (screen_height / 2.0f);
    out_matrix[6] = 0.0f;
    out_matrix[7] = 0.0f;
    out_matrix[8] = 0.0f;
    out_matrix[9] = 0.0f;
    out_matrix[10] = 0.0f;
    out_matrix[11] = 1.0f;
    out_matrix[12] = 0.0f;
    out_matrix[13] = 0.0f;
    out_matrix[14] = -1.0f;
    out_matrix[15] = 0.0f;
}

static void
ogl3_bind_world_attribs_at_offset(
    const OGL3WorldShaderLocs* locs,
    uint32_t mesh_vbo,
    uint32_t vbo_byte_offset)
{
    if( !locs || mesh_vbo == 0u )
        return;
    const GLsizei stride = (GLsizei)sizeof(TRSPK_VertexWebGL1);
    const uintptr_t base = (uintptr_t)vbo_byte_offset;
    ogl3_glBindBuffer(GL_ARRAY_BUFFER, mesh_vbo);
    if( locs->a_position >= 0 )
    {
        ogl3_glEnableVertexAttribArray((GLuint)locs->a_position);
        ogl3_glVertexAttribPointer(
            (GLuint)locs->a_position, 4, GL_FLOAT, GL_FALSE, stride, (const void*)(base + 0u));
    }
    if( locs->a_color >= 0 )
    {
        ogl3_glEnableVertexAttribArray((GLuint)locs->a_color);
        ogl3_glVertexAttribPointer(
            (GLuint)locs->a_color, 4, GL_FLOAT, GL_FALSE, stride, (const void*)(base + 16u));
    }
    if( locs->a_texcoord >= 0 )
    {
        ogl3_glEnableVertexAttribArray((GLuint)locs->a_texcoord);
        ogl3_glVertexAttribPointer(
            (GLuint)locs->a_texcoord, 2, GL_FLOAT, GL_FALSE, stride, (const void*)(base + 32u));
    }
    if( locs->a_tex_id >= 0 )
    {
        ogl3_glEnableVertexAttribArray((GLuint)locs->a_tex_id);
        ogl3_glVertexAttribPointer(
            (GLuint)locs->a_tex_id, 1, GL_FLOAT, GL_FALSE, stride, (const void*)(base + 40u));
    }
    if( locs->a_uv_mode >= 0 )
    {
        ogl3_glEnableVertexAttribArray((GLuint)locs->a_uv_mode);
        ogl3_glVertexAttribPointer(
            (GLuint)locs->a_uv_mode, 1, GL_FLOAT, GL_FALSE, stride, (const void*)(base + 44u));
    }
}

static void
ogl3_upload_uniforms(
    struct LibToriPlatformSDL2_RendererGL3* r,
    const float* view,
    const float* proj)
{
    OGL3Uniforms u = { 0 };
    memcpy(u.modelViewMatrix, view, sizeof(float) * 16u);
    memcpy(u.projectionMatrix, proj, sizeof(float) * 16u);
    u.uClock = (float)r->frame_clock;
    ogl3_glBindBuffer(GL_UNIFORM_BUFFER, r->ubo);
    ogl3_glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)sizeof(OGL3Uniforms), &u);
    ogl3_glBindBuffer(GL_UNIFORM_BUFFER, 0);
    ogl3_glBindBufferRange(GL_UNIFORM_BUFFER, 0u, r->ubo, 0, (GLsizeiptr)sizeof(OGL3Uniforms));
}

typedef struct OGL3_FrameState
{
    bool world_gl_ready;
    bool has_3d;
    struct LibToriRS_RenderCommand_Begin3D cur_3d;
    float view[16];
    float proj[16];
    OGL3_SortedDraw pending[OGL3_MAX_PENDING_DRAWS];
    int pending_count;
} OGL3_FrameState;

static void
ogl3_frame_state_compute_pass_matrices(
    struct LibToriPlatformSDL2_RendererGL3* r,
    OGL3_FrameState* fs)
{
    if( !r || !fs || !fs->has_3d )
        return;

    const struct ToriDraw_Position* cam_pos = &fs->cur_3d.camera_position;
    const struct ToriDraw_Camera* cam = &fs->cur_3d.camera;
    const struct ToriDraw_ViewPort* vp = &fs->cur_3d.view_port;
    const int pass_w = vp->width > 0 ? vp->width : r->width;
    const int pass_h = vp->height > 0 ? vp->height : r->height;

    ogl3_compute_view_matrix(
        fs->view,
        -(float)cam_pos->x,
        -(float)cam_pos->y,
        -(float)cam_pos->z,
        ogl3_dash_yaw_to_radians(cam->pitch),
        ogl3_dash_yaw_to_radians(cam->yaw));
    ogl3_compute_projection_matrix(
        fs->proj, (90.0f * (float)M_PI) / 180.0f, (float)pass_w, (float)pass_h);
}

static void
ogl3_frame_state_init(OGL3_FrameState* fs)
{
    if( !fs )
        return;
    memset(fs, 0, sizeof(*fs));
}

static void
ogl3_ensure_world_gl(
    struct LibToriPlatformSDL2_RendererGL3* r,
    OGL3_FrameState* fs)
{
    if( !r || !fs || fs->world_gl_ready )
        return;

    fs->world_gl_ready = true;
    ogl3_glUseProgram((GLuint)r->prog_world3d);
    if( r->world_locs.s_atlas >= 0 )
        ogl3_glUniform1i(r->world_locs.s_atlas, 0);
    ogl3_glActiveTexture(GL_TEXTURE0);
    ogl3_glBindTexture(GL_TEXTURE_2D, r->atlas_texture);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_FALSE);
}

static void
ogl3_flush_pending_draws(
    struct LibToriPlatformSDL2_RendererGL3* r,
    const OGL3_FrameState* fs);

static void
ogl3_handle_render_command(
    struct LibToriPlatformSDL2_RendererGL3* r,
    struct LibToriRS_Instance* instance,
    const struct LibToriRS_RenderCommand* cmd,
    OGL3_FrameState* fs)
{
    if( !r || !cmd || !fs )
        return;

    switch( cmd->kind )
    {
    case TORIRSRC_MODEL_LOAD:
    {
        const struct ToriDraw_Model* model =
            trspk_toridraw_model_from_handle(&cmd->u.model_load.model);
        if( model && cmd->u.model_load.element_id >= 0 )
        {
            TRSPK_BakeTransform bake = trspk_bake_transform_identity();
            ogl3_ring_upload_model(r, model, (TRSPK_ModelId)cmd->u.model_load.element_id, &bake);
        }
        break;
    }
    case TORIRSRC_MODEL_UNLOAD:
        if( cmd->u.model_load.element_id >= 0 )
            trspk_resource_cache_clear_model(r->cache, (TRSPK_ModelId)cmd->u.model_load.element_id);
        break;
    case TORIRSRC_BATCH3D_BEGIN:
        r->current_batch_id = (TRSPK_BatchId)cmd->u.batch.batch_id;
        r->batch_active = true;
        trspk_batch32_begin(r->batch_staging);
        trspk_resource_cache_batch_begin(r->cache, r->current_batch_id);
        break;
    case TORIRSRC_BATCH3D_MODEL_ADD:
    {
        const struct ToriDraw_Model* model = trspk_toridraw_model_from_handle(&cmd->u.batch.model);
        TRSPK_BakeTransform bake = trspk_bake_transform_identity();
        if( r->batch_active && model && cmd->u.batch.element_id >= 0 )
        {
            trspk_toridraw_batch_add_model32(
                r->batch_staging,
                model,
                (uint16_t)cmd->u.batch.element_id,
                TRSPK_GPU_ANIM_NONE_IDX,
                0u,
                &bake,
                r->cache);
        }
        break;
    }
    case TORIRSRC_BATCH3D_END:
        if( r->batch_active )
        {
            trspk_batch32_end(r->batch_staging);
            ogl3_batch_submit(r, r->current_batch_id);
            r->batch_active = false;
        }
        break;
    case TORIRSRC_BATCH3D_CLEAR:
        if( cmd->u.batch.batch_id >= 0 )
            ogl3_batch_clear(r, (TRSPK_BatchId)cmd->u.batch.batch_id);
        break;
    case TORIRSRC_TEX_LOAD:
        if( ogl3_load_texture(r, cmd->u.tex_load.texture_id, cmd->u.tex_load.texture) )
            r->atlas_dirty = true;
        break;
    case TORIRSRC_TEX_UNLOAD:
        if( ogl3_unload_texture(r, cmd->u.tex_load.texture_id) )
            r->atlas_dirty = true;
        break;
    case TORIRSRC_BEGIN_3D:
        ogl3_ensure_world_gl(r, fs);
        fs->cur_3d = cmd->u.begin_3d;
        fs->has_3d = true;
        if( r->atlas_dirty )
        {
            ogl3_refresh_atlas(r);
            r->atlas_dirty = false;
        }
        ogl3_frame_state_compute_pass_matrices(r, fs);
        break;
    case TORIRSRC_END_3D:
        ogl3_flush_pending_draws(r, fs);
        fs->pending_count = 0;
        fs->has_3d = false;
        break;
    case TORIRSRC_DRAW_MODEL:
    {
        if( !fs->has_3d )
            break;

        struct ToriDraw_Context* ctx = NULL;
        if( instance && instance->model_viewer )
            ctx = instance->model_viewer->context;
        if( !ctx )
            break;

        const int sort_count = toridraw_face_order_count(ctx);
        if( sort_count <= 0 )
            break;

        TRSPK_ModelId draw_model_id = 0;
        if( cmd->u.model.element_id != WORLD_SCENE_INVALID_ELEMENT_ID )
            draw_model_id = (TRSPK_ModelId)cmd->u.model.element_id;

        const TRSPK_ModelPose* pose =
            trspk_resource_cache_get_pose_for_draw(r->cache, draw_model_id, false, 0, 0);
        if( !pose || !pose->valid )
            break;

        int max_faces = sort_count;
        if( max_faces > (int)TRSPK_FACEBUFFER_MAX_FACES )
            max_faces = (int)TRSPK_FACEBUFFER_MAX_FACES;

        int* face_order = toridraw_face_order(ctx);
        uint32_t* out_ix = r->facebuffer.indices;
        for( int fi = 0; fi < max_faces; fi++ )
        {
            const int face = face_order[fi];
            out_ix[(uint32_t)fi * 3u + 0u] = (uint32_t)(face * 3 + 0);
            out_ix[(uint32_t)fi * 3u + 1u] = (uint32_t)(face * 3 + 1);
            out_ix[(uint32_t)fi * 3u + 2u] = (uint32_t)(face * 3 + 2);
        }

        const uint32_t index_count = (uint32_t)max_faces * 3u;
        const uint32_t sorted_index_bytes = index_count * (uint32_t)sizeof(uint32_t);

        OGL3_DynamicRing* ring = &r->dynamic_ring;
        if( ring->ebo == 0u || ring->vbo == 0u )
            break;

        if( ring->ebo_offset + sorted_index_bytes > ring->ebo_capacity )
            ring->ebo_offset = 0u;

        const uint32_t sorted_ebo_byte = ring->ebo_offset;
        ogl3_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ring->ebo);
        ogl3_glBufferSubData(
            GL_ELEMENT_ARRAY_BUFFER,
            (GLintptr)sorted_ebo_byte,
            (GLsizeiptr)sorted_index_bytes,
            out_ix);
        ogl3_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        ring->ebo_offset += sorted_index_bytes;

        if( fs->pending_count >= (int)OGL3_MAX_PENDING_DRAWS )
            break;

        OGL3_SortedDraw* pd = &fs->pending[fs->pending_count++];
        pd->vbo = (GLuint)(uintptr_t)pose->vbo;
        pd->ring_ebo = ring->ebo;
        pd->vbo_byte_offset = pose->vbo_offset;
        pd->sorted_ebo_byte_offset = sorted_ebo_byte;
        pd->index_count = index_count;
        break;
    }
    default:
        break;
    }
}

static void
ogl3_flush_pending_draws(
    struct LibToriPlatformSDL2_RendererGL3* r,
    const OGL3_FrameState* fs)
{
    if( !r || !fs || fs->pending_count <= 0 )
        return;

    ogl3_upload_uniforms(r, fs->view, fs->proj);
    glViewport(0, 0, r->width, r->height);

    for( int p = 0; p < fs->pending_count; p++ )
    {
        const OGL3_SortedDraw* pd = &fs->pending[p];
        if( pd->index_count == 0u )
            continue;

        ogl3_bind_world_attribs_at_offset(&r->world_locs, pd->vbo, pd->vbo_byte_offset);
        ogl3_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pd->ring_ebo);
        ogl3_glDrawElements(
            GL_TRIANGLES,
            (GLsizei)pd->index_count,
            GL_UNSIGNED_INT,
            (const void*)(uintptr_t)pd->sorted_ebo_byte_offset);
    }
}

struct LibToriPlatformSDL2_RendererGL3*
LibToriPlatformSDL2_RendererGL3_New(
    int width,
    int height)
{
    struct LibToriPlatformSDL2_RendererGL3* r =
        calloc(1, sizeof(struct LibToriPlatformSDL2_RendererGL3));
    if( !r )
        return NULL;
    r->width = width;
    r->height = height;
    r->window_width = width;
    r->window_height = height;
    return r;
}

void
LibToriPlatformSDL2_RendererGL3_Free(struct LibToriPlatformSDL2_RendererGL3* renderer)
{
    if( !renderer )
        return;
    if( renderer->window && renderer->gl_context )
    {
        SDL_GL_MakeCurrent(renderer->window, renderer->gl_context);
        if( renderer->prog_world3d )
            ogl3_glDeleteProgram((GLuint)renderer->prog_world3d);
        if( renderer->ubo )
        {
            GLuint u = renderer->ubo;
            ogl3_glDeleteBuffers(1, &u);
        }
        if( renderer->atlas_texture )
        {
            GLuint t = renderer->atlas_texture;
            glDeleteTextures(1, &t);
        }
        if( renderer->dynamic_ring.vao != 0u )
        {
            GLuint vao = renderer->dynamic_ring.vao;
            ogl3_glDeleteVertexArrays(1, &vao);
            renderer->dynamic_ring.vao = 0u;
        }
        if( renderer->dynamic_ring.vbo != 0u )
        {
            GLuint vbo = renderer->dynamic_ring.vbo;
            ogl3_glDeleteBuffers(1, &vbo);
            renderer->dynamic_ring.vbo = 0u;
        }
        if( renderer->dynamic_ring.ebo != 0u )
        {
            GLuint ebo = renderer->dynamic_ring.ebo;
            ogl3_glDeleteBuffers(1, &ebo);
            renderer->dynamic_ring.ebo = 0u;
        }
    }
    if( renderer->cache )
        trspk_resource_cache_destroy(renderer->cache);
    if( renderer->batch_staging )
        trspk_batch32_destroy(renderer->batch_staging);
    if( renderer->gl_context )
        SDL_GL_DeleteContext(renderer->gl_context);
    free(renderer);
}

bool
LibToriPlatformSDL2_RendererGL3_Init(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    SDL_Window* window)
{
    if( !renderer || !window )
        return false;

    renderer->window = window;
    renderer->gl_context = SDL_GL_CreateContext(window);
    if( !renderer->gl_context )
    {
        fprintf(stderr, "OpenGL3: SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return false;
    }
    if( SDL_GL_MakeCurrent(window, renderer->gl_context) != 0 )
    {
        fprintf(stderr, "OpenGL3: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(renderer->gl_context);
        renderer->gl_context = NULL;
        return false;
    }

    SDL_GL_SetSwapInterval(0);

    if( !ogl3_load_gl_procs() )
        return false;

    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_GL_GetDrawableSize(window, &drawable_w, &drawable_h);
    renderer->width = drawable_w;
    renderer->height = drawable_h;
    SDL_GetWindowSize(window, &renderer->window_width, &renderer->window_height);

    renderer->cache = trspk_resource_cache_create(0u, TRSPK_VERTEX_FORMAT_NONE);
    renderer->batch_staging = trspk_batch32_create(4096u, 12288u, TRSPK_VERTEX_FORMAT_WEBGL1);
    if( !renderer->cache || !renderer->batch_staging )
        return false;

    renderer->prog_world3d = ogl3_create_world_program(&renderer->world_locs);
    if( renderer->prog_world3d == 0u )
        return false;

    ogl3_glGenBuffers(1, &renderer->ubo);
    ogl3_glBindBuffer(GL_UNIFORM_BUFFER, renderer->ubo);
    ogl3_glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)sizeof(OGL3Uniforms), NULL, GL_DYNAMIC_DRAW);
    ogl3_glBindBuffer(GL_UNIFORM_BUFFER, 0);

    renderer->atlas_dirty = false;

    OGL3_DynamicRing* ring = &renderer->dynamic_ring;
    ring->vbo_capacity = OGL3_DYNAMIC_VBO_SIZE;
    ring->ebo_capacity = OGL3_DYNAMIC_EBO_SIZE;
    ring->vbo_offset = 0u;
    ring->ebo_offset = 0u;
    ogl3_glGenBuffers(1, &ring->vbo);
    ogl3_glGenBuffers(1, &ring->ebo);
    ogl3_glBindBuffer(GL_ARRAY_BUFFER, ring->vbo);
    ogl3_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)ring->vbo_capacity, NULL, GL_DYNAMIC_DRAW);
    ogl3_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ring->ebo);
    ogl3_glBufferData(
        GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ring->ebo_capacity, NULL, GL_DYNAMIC_DRAW);
    ogl3_glBindBuffer(GL_ARRAY_BUFFER, 0);
    ogl3_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    ogl3_glGenVertexArrays(1, &ring->vao);
    if( ring->vao != 0u )
        ogl3_world_vao_setup(ring->vao, &renderer->world_locs, ring->vbo, ring->ebo);

    renderer->ready = true;
    return true;
}

void
LibToriPlatformSDL2_RendererGL3_Render(
    struct LibToriPlatformSDL2_RendererGL3* renderer,
    struct LibToriRS_Instance* instance)
{
    if( !renderer || !renderer->ready || !renderer->window || !instance )
        return;

    if( SDL_GL_MakeCurrent(renderer->window, renderer->gl_context) != 0 )
        return;

    int drawable_w = renderer->width;
    int drawable_h = renderer->height;
    SDL_GL_GetDrawableSize(renderer->window, &drawable_w, &drawable_h);
    renderer->width = drawable_w;
    renderer->height = drawable_h;
    SDL_GetWindowSize(renderer->window, &renderer->window_width, &renderer->window_height);

    renderer->frame_clock += 1.0 / 50.0;

    renderer->dynamic_ring.vbo_offset = 0u;
    renderer->dynamic_ring.ebo_offset = 0u;

    glViewport(0, 0, drawable_w, drawable_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    OGL3_FrameState frame_state;
    ogl3_frame_state_init(&frame_state);

    LibToriRS_FrameBegin(instance);
    struct LibToriRS_RenderCommand command;
    while( LibToriRS_FrameNextCommand(instance, &command) )
        ogl3_handle_render_command(renderer, instance, &command, &frame_state);
    LibToriRS_FrameEnd(instance);

    SDL_GL_SwapWindow(renderer->window);
}
