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
uniform vec4 u_inst0;
uniform vec4 u_inst1;
uniform mat4 u_modelViewMatrix;
uniform mat4 u_projectionMatrix;
varying vec4 v_color;
varying vec2 v_texcoord;
varying float v_tex_id;
varying float v_uv_mode;
void main() {
  vec3 p = a_position.xyz;
  float c = u_inst0.x;
  float s = u_inst0.y;
  float xr = p.x * c + p.z * s;
  float zr = -p.x * s + p.z * c;
  vec4 wp = vec4(xr + u_inst0.z, p.y + u_inst0.w, zr + u_inst1.x, 1.0);
  gl_Position = u_projectionMatrix * u_modelViewMatrix * wp;
  v_color = a_color;
  v_texcoord = a_texcoord;
  v_tex_id = a_tex_id;
  v_uv_mode = a_uv_mode;
}
)";

static const char kWorldFs[] = R"(precision mediump float;
varying vec4 v_color;
varying vec2 v_texcoord;
varying float v_tex_id;
varying float v_uv_mode;
uniform float u_clock;
uniform sampler2D s_atlas;
uniform vec4 u_tileA[256];
uniform vec4 u_tileB[256];
void main() {
  int tid = int(floor(v_tex_id + 0.5));
  if (tid < 0 || tid >= 256) {
    gl_FragColor = vec4(v_color.rgb, v_color.a);
    return;
  }
  /* GLSL ES 1.00: uniform arrays may only be indexed by const or loop indices, not by `tid`. */
  vec4 ta = vec4(0.0);
  vec4 tb = vec4(0.0);
  for (int i = 0; i < 256; ++i) {
    if (i == tid) {
      ta = u_tileA[i];
      tb = u_tileB[i];
      break;
    }
  }
  if (ta.z <= 0.0 || ta.w <= 0.0) {
    gl_FragColor = vec4(v_color.rgb, v_color.a);
    return;
  }
  vec2 local = v_texcoord;
  float clk = u_clock;
  if (v_uv_mode < 0.5) {
    if (tb.x > 0.0) local.x += clk * tb.x;
    if (tb.y > 0.0) local.y -= clk * tb.y;
    local.x = clamp(local.x, 0.008, 0.992);
    local.y = clamp(fract(local.y), 0.008, 0.992);
  } else {
    if (tb.x > 0.0) local.x += clk * tb.x;
    if (tb.y > 0.0) local.y += clk * tb.y;
    local.x = clamp(fract(local.x), 0.008, 0.992);
    local.y = clamp(fract(local.y), 0.008, 0.992);
  }
  vec2 uv_atlas = ta.xy + local * ta.zw;
  vec4 texColor = texture2D(s_atlas, uv_atlas);
  vec3 finalColor = mix(v_color.rgb, texColor.rgb * v_color.rgb, 1.0);
  float finalAlpha = v_color.a;
  if (tb.z < 0.5) {
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
webgl1_gl_create_programs(struct Platform2_SDL2_Renderer_WebGL1* r)
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
    L.u_inst0 = glGetUniformLocation(wp, "u_inst0");
    L.u_inst1 = glGetUniformLocation(wp, "u_inst1");
    L.u_modelViewMatrix = glGetUniformLocation(wp, "u_modelViewMatrix");
    L.u_projectionMatrix = glGetUniformLocation(wp, "u_projectionMatrix");
    L.u_clock = glGetUniformLocation(wp, "u_clock");
    L.s_atlas = glGetUniformLocation(wp, "s_atlas");
    L.u_tileA = glGetUniformLocation(wp, "u_tileA");
    L.u_tileB = glGetUniformLocation(wp, "u_tileB");

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
webgl1_gl_destroy_programs(struct Platform2_SDL2_Renderer_WebGL1* r)
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
