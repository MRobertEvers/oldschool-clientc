#include "platforms/platform_impl2_sdl2_renderer_webgl1.h"
#include "platforms/webgl1/ctx.h"

#ifdef __EMSCRIPTEN__

#    include <GLES2/gl2.h>

#    include <cstdio>
#    include <cstring>

static GLuint
compile_shader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    if( s == 0u )
        return 0u;
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char buf[512];
        glGetShaderInfoLog(s, (GLsizei)sizeof(buf), nullptr, buf);
        fprintf(stderr, "WebGL1 shader compile failed: %s\n", buf);
        glDeleteShader(s);
        return 0u;
    }
    return s;
}

static const char kWorldVs[] = R"(attribute vec4 a_position;
attribute vec4 a_color;
attribute vec2 a_texcoord;
attribute float a_tex_id;
attribute float a_uv_mode;
uniform mat4 u_modelViewMatrix;
uniform mat4 u_projectionMatrix;
varying vec4 v_color;
varying vec2 v_texcoord;
varying float v_tex_id;
varying float v_uv_pack;
void main() {
  vec4 wp = vec4(a_position.xyz, 1.0);
  gl_Position = u_projectionMatrix * u_modelViewMatrix * wp;
  v_color = a_color;
  v_texcoord = a_texcoord;
  v_tex_id = a_tex_id;
  v_uv_pack = a_uv_mode;
}
)";

static const char kWorldFs[] = R"(precision mediump float;
varying vec4 v_color;
varying vec2 v_texcoord;
varying float v_tex_id;
varying float v_uv_pack;
uniform float u_clock;
uniform sampler2D s_atlas;
void main() {
  int raw = int(floor(v_tex_id + 0.5));
  int atlas_id;
  bool cutout;
  if (raw >= 256) {
    atlas_id = raw - 256;
    cutout = true;
  } else {
    atlas_id = raw;
    cutout = false;
  }
  if (atlas_id < 0 || atlas_id >= 256) {
    gl_FragColor = vec4(v_color.rgb, v_color.a);
    return;
  }
  int enc = int(floor(v_uv_pack * 0.5 + 0.5));
  float anim_u = 0.0;
  float anim_v = 0.0;
  if (enc > 0) {
    if (enc < 257) {
      int mag_u = enc - 1;
      anim_u = float(mag_u) / 256.0;
    } else {
      int mag_v = enc - 257;
      anim_v = float(mag_v) / 256.0;
    }
  }
  const float TEX_DIM = 128.0;
  const float ATLAS_DIM = 2048.0;
  const float cols = 16.0;
  float du = TEX_DIM / ATLAS_DIM;
  float dv = du;
  float col = mod(float(atlas_id), cols);
  float row = floor(float(atlas_id) / cols);
  vec4 ta = vec4(col * du, row * dv, du, dv);
  vec2 local = v_texcoord;
  float clk = u_clock;
  if (anim_u > 0.0) local.x += clk * anim_u;
  if (anim_v > 0.0) local.y -= clk * anim_v;
  local.x = clamp(local.x, 0.008, 0.992);
  local.y = clamp(fract(local.y), 0.008, 0.992);
  vec2 uv_atlas = ta.xy + local * ta.zw;
  vec4 texColor = texture2D(s_atlas, uv_atlas);
  vec3 finalColor = mix(v_color.rgb, texColor.rgb * v_color.rgb, 1.0);
  float finalAlpha = v_color.a;
  if (cutout) {
    if (texColor.a < 0.5) discard;
  }
  gl_FragColor = vec4(finalColor, finalAlpha);
}
)";

static const char kClearVs[] = R"(attribute vec2 a_pos;
void main() {
  gl_Position = vec4(a_pos.xy, 1.0, 1.0);
}
)";

static const char kClearFs[] = R"(precision mediump float;
void main() {
  gl_FragColor = vec4(0.0);
}
)";

bool
webgl1_gl_create_programs(Platform2_SDL2_Renderer_WebGL1* r)
{
    if( !r )
        return false;

    GLuint wvs = compile_shader(GL_VERTEX_SHADER, kWorldVs);
    GLuint wfs = compile_shader(GL_FRAGMENT_SHADER, kWorldFs);
    if( !wvs || !wfs )
        return false;

    GLuint wp = glCreateProgram();
    glBindAttribLocation(wp, 0, "a_position");
    glBindAttribLocation(wp, 1, "a_color");
    glBindAttribLocation(wp, 2, "a_texcoord");
    glBindAttribLocation(wp, 3, "a_tex_id");
    glBindAttribLocation(wp, 4, "a_uv_mode");
    glAttachShader(wp, wvs);
    glAttachShader(wp, wfs);
    glLinkProgram(wp);
    glDeleteShader(wvs);
    glDeleteShader(wfs);
    GLint wok = 0;
    glGetProgramiv(wp, GL_LINK_STATUS, &wok);
    if( !wok )
    {
        char buf[512];
        glGetProgramInfoLog(wp, (GLsizei)sizeof(buf), nullptr, buf);
        fprintf(stderr, "WebGL1 world program link failed: %s\n", buf);
        glDeleteProgram(wp);
        return false;
    }
    r->prog_world3d = wp;

    WebGL1WorldShaderLocs& L = r->world_locs;
    L.a_position = glGetAttribLocation(wp, "a_position");
    L.a_color = glGetAttribLocation(wp, "a_color");
    L.a_texcoord = glGetAttribLocation(wp, "a_texcoord");
    L.a_tex_id = glGetAttribLocation(wp, "a_tex_id");
    L.a_uv_mode = glGetAttribLocation(wp, "a_uv_mode");
    L.u_modelViewMatrix = glGetUniformLocation(wp, "u_modelViewMatrix");
    L.u_projectionMatrix = glGetUniformLocation(wp, "u_projectionMatrix");
    L.u_clock = glGetUniformLocation(wp, "u_clock");
    L.s_atlas = glGetUniformLocation(wp, "s_atlas");

    GLuint cvs = compile_shader(GL_VERTEX_SHADER, kClearVs);
    GLuint cfs = compile_shader(GL_FRAGMENT_SHADER, kClearFs);
    if( !cvs || !cfs )
        return false;
    GLuint cp = glCreateProgram();
    glBindAttribLocation(cp, 0, "a_pos");
    glAttachShader(cp, cvs);
    glAttachShader(cp, cfs);
    glLinkProgram(cp);
    glDeleteShader(cvs);
    glDeleteShader(cfs);
    GLint cok = 0;
    glGetProgramiv(cp, GL_LINK_STATUS, &cok);
    if( !cok )
    {
        char buf[512];
        glGetProgramInfoLog(cp, (GLsizei)sizeof(buf), nullptr, buf);
        fprintf(stderr, "WebGL1 clear program link failed: %s\n", buf);
        glDeleteProgram(cp);
        return false;
    }
    r->prog_clear_depth = cp;
    r->clear_depth_a_pos = glGetAttribLocation(cp, "a_pos");

    glGenBuffers(1, &r->clear_quad_vbo);

    const size_t clear_vbo_bytes = (size_t)kWebGL1MaxClearRectsPerFrame * (size_t)kWebGL1SpriteSlotBytes;

    glBindBuffer(GL_ARRAY_BUFFER, r->clear_quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)clear_vbo_bytes, nullptr, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    /* Match Metal `metal_init.mm` world pipeline: depth test on, always pass, no depth write;
     * no face cull; alpha blend for textured/cutout faces. */
    webgl1_gl_bind_default_world_gl_state();

    return true;
}

void
webgl1_gl_bind_default_world_gl_state(void)
{
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void
webgl1_gl_destroy_programs(Platform2_SDL2_Renderer_WebGL1* r)
{
    if( !r )
        return;
    if( r->prog_world3d )
    {
        glDeleteProgram(r->prog_world3d);
        r->prog_world3d = 0;
    }
    if( r->prog_clear_depth )
    {
        glDeleteProgram(r->prog_clear_depth);
        r->prog_clear_depth = 0;
    }
    if( r->clear_quad_vbo )
    {
        glDeleteBuffers(1, &r->clear_quad_vbo);
        r->clear_quad_vbo = 0;
    }
    r->world_locs = WebGL1WorldShaderLocs{};
    r->clear_depth_a_pos = -1;
}

#endif
