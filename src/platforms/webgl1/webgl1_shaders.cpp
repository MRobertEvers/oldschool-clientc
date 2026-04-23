#include "platforms/webgl1/webgl1_internal.h"

#include <cstdio>
#include <cstring>

static const char kWorldVs[] = R"(#version 100
precision highp float;
attribute vec4 aClipPos;
attribute vec4 aColor;
attribute vec2 aTex;
attribute float aTexId;
attribute float aUvMode;
varying vec4 vColor;
varying vec2 vTex;
varying float vTexId;
varying float vUvMode;
void main()
{
    gl_Position = aClipPos;
    vColor = aColor;
    vTex = aTex;
    vTexId = aTexId;
    vUvMode = aUvMode;
}
)";

static const char kWorldFs[] = R"(#version 100
precision mediump float;
varying vec4 vColor;
varying vec2 vTex;
varying float vTexId;
varying float vUvMode;
uniform sampler2D uAtlas;
uniform sampler2D uTileMeta;
uniform vec4 uClockPad;
void main()
{
    float tid = vTexId;
    if( tid >= 256.0 )
    {
        gl_FragColor = vec4(vColor.rgb, vColor.a);
        return;
    }
    vec4 t0 = texture2D(uTileMeta, vec2((tid + 0.5) / 256.0, 0.25));
    vec4 t1 = texture2D(uTileMeta, vec2((tid + 0.5) / 256.0, 0.75));
    float u0 = t0.r;
    float v0 = t0.g;
    float du = t0.b;
    float dv = t0.a;
    if( du <= 0.0 || dv <= 0.0 )
    {
        gl_FragColor = vec4(vColor.rgb, vColor.a);
        return;
    }
    float animUx = t1.r;
    float animVy = t1.g;
    float opaque = t1.b;
    vec2 local = vTex;
    float clk = uClockPad.x;
    if( vUvMode < 0.5 )
    {
        if( animUx > 0.0 )
            local.x += clk * animUx;
        if( animVy > 0.0 )
            local.y -= clk * animVy;
        local.x = clamp(local.x, 0.008, 0.992);
        local.y = clamp(fract(local.y), 0.008, 0.992);
    }
    else
    {
        if( animUx > 0.0 )
            local.x += clk * animUx;
        if( animVy > 0.0 )
            local.y += clk * animVy;
        local.x = clamp(fract(local.x), 0.008, 0.992);
        local.y = clamp(fract(local.y), 0.008, 0.992);
    }
    vec2 uv_atlas = vec2(u0, v0) + local * vec2(du, dv);
    vec4 texColor = texture2D(uAtlas, uv_atlas);
    vec3 finalColor = mix(vColor.rgb, texColor.rgb * vColor.rgb, 1.0);
    float finalAlpha = vColor.a;
    if( opaque < 0.5 )
    {
        if( texColor.a < 0.5 )
            discard;
    }
    gl_FragColor = vec4(finalColor, finalAlpha);
}
)";

static const char kUiVs[] = R"(#version 100
precision highp float;
attribute vec2 aPos;
attribute vec2 aUv;
varying vec2 vUv;
uniform mat4 uProjection;
void main()
{
    vUv = aUv;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)";

static const char kUiFs[] = R"(#version 100
precision mediump float;
varying vec2 vUv;
uniform sampler2D uTex;
void main()
{
    float a = texture2D(uTex, vUv).a;
    if( a < 0.01 )
        discard;
    gl_FragColor = texture2D(uTex, vUv);
}
)";

static const char kInvRotFs[] = R"(#version 100
precision mediump float;
varying vec2 vUv;
uniform sampler2D uTex;
uniform vec4 uParams[4];
void main()
{
    float dst_x_abs = floor(vUv.x);
    float dst_y_abs = floor(vUv.y);
    float dst_origin_x = uParams[0].x;
    float dst_origin_y = uParams[0].y;
    float dst_anchor_x = uParams[0].z;
    float dst_anchor_y = uParams[0].w;
    float src_anchor_x = uParams[1].x;
    float src_anchor_y = uParams[1].y;
    float src_crop_x = uParams[1].z;
    float src_crop_y = uParams[1].w;
    float src_size_x = uParams[2].x;
    float src_size_y = uParams[2].y;
    float cos_val = uParams[2].z;
    float sin_val = uParams[2].w;
    float tex_size_x = uParams[3].x;
    float tex_size_y = uParams[3].y;

    float rel_x = dst_x_abs - dst_origin_x - dst_anchor_x;
    float rel_y = dst_y_abs - dst_origin_y - dst_anchor_y;
    float src_rel_x = rel_x * cos_val + rel_y * sin_val;
    float src_rel_y = -rel_x * sin_val + rel_y * cos_val;
    float src_x = floor(src_anchor_x + src_rel_x);
    float src_y = floor(src_anchor_y + src_rel_y);
    if( src_x < 0.0 || src_x >= src_size_x || src_y < 0.0 || src_y >= src_size_y )
        discard;
    float bx = src_crop_x + src_x;
    float by = src_crop_y + src_y;
    vec2 uv = vec2(bx / tex_size_x, by / tex_size_y);
    gl_FragColor = texture2D(uTex, uv);
}
)";

static const char kFontVs[] = R"(#version 100
attribute vec2 aPos;
attribute vec2 aUv;
attribute vec4 aColor;
uniform mat4 uProjection;
varying vec2 vUv;
varying vec4 vColor;
void main()
{
    vUv = aUv;
    vColor = aColor;
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
}
)";

static const char kFontFs[] = R"(#version 100
precision mediump float;
varying vec2 vUv;
varying vec4 vColor;
uniform sampler2D uTex;
void main()
{
    float a = texture2D(uTex, vUv).a;
    if( a < 0.01 )
        discard;
    gl_FragColor = vec4(vColor.rgb, a * vColor.a);
}
)";

GLuint
wg1_compile_shader(GLenum type, const char* src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if( !ok )
    {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "[webgl1] shader compile error: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint
wg1_link_program(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "aClipPos");
    glBindAttribLocation(p, 1, "aColor");
    glBindAttribLocation(p, 2, "aTex");
    glBindAttribLocation(p, 3, "aTexId");
    glBindAttribLocation(p, 4, "aUvMode");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if( !ok )
    {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "[webgl1] program link error: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static GLuint
link_ui_program(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aUv");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if( !ok )
    {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "[webgl1] ui program link: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static GLuint
link_font_program(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aUv");
    glBindAttribLocation(p, 2, "aColor");
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if( !ok )
    {
        char log[512];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "[webgl1] font program link: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool
wg1_shaders_init(struct Platform2_SDL2_Renderer_WebGL1* r)
{
    if( !r )
        return false;
    GLuint wvs = wg1_compile_shader(GL_VERTEX_SHADER, kWorldVs);
    GLuint wfs = wg1_compile_shader(GL_FRAGMENT_SHADER, kWorldFs);
    if( !wvs || !wfs )
    {
        if( wvs )
            glDeleteShader(wvs);
        if( wfs )
            glDeleteShader(wfs);
        return false;
    }
    r->program_world = wg1_link_program(wvs, wfs);
    glDeleteShader(wvs);
    glDeleteShader(wfs);
    if( !r->program_world )
        return false;
    r->loc_world_uAtlas = glGetUniformLocation(r->program_world, "uAtlas");
    r->loc_world_uTileMeta = glGetUniformLocation(r->program_world, "uTileMeta");
    r->loc_world_uClockPad = glGetUniformLocation(r->program_world, "uClockPad");

    GLuint uiv = wg1_compile_shader(GL_VERTEX_SHADER, kUiVs);
    GLuint uif = wg1_compile_shader(GL_FRAGMENT_SHADER, kUiFs);
    if( uiv && uif )
    {
        r->program_ui_sprite = link_ui_program(uiv, uif);
        glDeleteShader(uiv);
        glDeleteShader(uif);
        if( r->program_ui_sprite )
        {
            r->loc_ui_uProj = glGetUniformLocation(r->program_ui_sprite, "uProjection");
            r->loc_ui_uTex = glGetUniformLocation(r->program_ui_sprite, "uTex");
        }
    }

    GLuint irv = wg1_compile_shader(GL_VERTEX_SHADER, kUiVs);
    GLuint irf = wg1_compile_shader(GL_FRAGMENT_SHADER, kInvRotFs);
    if( irv && irf )
    {
        r->program_ui_sprite_invrot = link_ui_program(irv, irf);
        glDeleteShader(irv);
        glDeleteShader(irf);
        if( r->program_ui_sprite_invrot )
        {
            r->loc_inv_uProj = glGetUniformLocation(r->program_ui_sprite_invrot, "uProjection");
            r->loc_inv_uTex = glGetUniformLocation(r->program_ui_sprite_invrot, "uTex");
            r->loc_inv_uParams = glGetUniformLocation(r->program_ui_sprite_invrot, "uParams");
        }
    }

    GLuint fvs = wg1_compile_shader(GL_VERTEX_SHADER, kFontVs);
    GLuint ffs = wg1_compile_shader(GL_FRAGMENT_SHADER, kFontFs);
    if( fvs && ffs )
    {
        r->font_program = link_font_program(fvs, ffs);
        glDeleteShader(fvs);
        glDeleteShader(ffs);
        if( r->font_program )
        {
            r->font_attrib_pos = glGetAttribLocation(r->font_program, "aPos");
            r->font_attrib_uv = glGetAttribLocation(r->font_program, "aUv");
            r->font_attrib_color = glGetAttribLocation(r->font_program, "aColor");
            r->font_uniform_projection = glGetUniformLocation(r->font_program, "uProjection");
            r->font_uniform_tex = glGetUniformLocation(r->font_program, "uTex");
        }
    }

    return r->program_world != 0;
}

void
wg1_shaders_shutdown(struct Platform2_SDL2_Renderer_WebGL1* r)
{
    if( !r )
        return;
    if( r->program_world )
        glDeleteProgram(r->program_world);
    r->program_world = 0;
    if( r->program_ui_sprite )
        glDeleteProgram(r->program_ui_sprite);
    r->program_ui_sprite = 0;
    if( r->program_ui_sprite_invrot )
        glDeleteProgram(r->program_ui_sprite_invrot);
    r->program_ui_sprite_invrot = 0;
    if( r->font_program )
        glDeleteProgram(r->font_program);
    r->font_program = 0;
}
