#version 150 core

layout(std140) uniform TRSPK_UboWorld {
    mat4 u_modelViewMatrix;
    mat4 u_projectionMatrix;
    vec4 u_clock_pad;
} ubo;

in vec4 v_color;
in vec2 v_texcoord;
in float v_tex_id;
in float v_uv_pack;

uniform sampler2D s_atlas;

out vec4 frag_color;

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
        frag_color = vec4(v_color.rgb, v_color.a);
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
    float clk = ubo.u_clock_pad.x;
    if (anim_u > 0.0) local.x += clk * anim_u;
    if (anim_v > 0.0) local.y -= clk * anim_v;
    local.x = clamp(local.x, 0.008, 0.992);
    local.y = clamp(fract(local.y), 0.008, 0.992);
    vec2 uv_atlas = ta.xy + local * ta.zw;
    vec4 texColor = texture(s_atlas, uv_atlas);
    vec3 finalColor = mix(v_color.rgb, texColor.rgb * v_color.rgb, 1.0);
    float finalAlpha = v_color.a;
    if (cutout) {
        if (texColor.a < 0.5) discard;
    }
    frag_color = vec4(finalColor, finalAlpha);
}
