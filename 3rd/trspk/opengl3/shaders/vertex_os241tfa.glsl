#version 150 core

// The OpenGL (Post-TFA OSRS) renderer's world vertex stage: vertex.glsl, plus
// the rev-241 client's depth key (as vertex_os235sailing.glsl) and its
// eye-ray texel lookup. Linked only for
// a handle made by ToriPlatformSDL2_Renderer_GL3_NewOs241Tfa, whose vertex
// (TRSPK_VertexOpenGl3Os241Tfa) carries the texture projector.

layout(std140) uniform TRSPK_UboWorld {
    mat4 u_modelViewMatrix;
    mat4 u_projectionMatrix;
    float uClock;
    float uAtlasDim;
    float uAtlasSlots;
    float uDepthBias;
    vec4 uEye;
} ubo;

in vec4 a_position;
in vec4 a_color;
in vec2 a_texcoord;
in float a_tex_id;
in float a_uv_mode;
// The face's texture projector: P less this corner, U = M - P, V = N - P,
// world space. Zero for a face with none (untextured, a tile, a widget).
in vec3 a_proj_p;
in vec3 a_proj_u;
in vec3 a_proj_v;

out vec4 v_color;
out vec2 v_texcoord;
out float v_tex_id;
out float v_uv_pack;
// The client's texel lookup [class171.method5476]: the texel is where the eye
// ray through the pixel meets the projector plane P + u*U + v*V. With
// r = corner - eye and Prel = P - corner, Cramer's rule gives
//   u = -dot(r, Prel x V) / dot(r, U x V),  v = -dot(r, U x Prel) / dot(r, U x V)
// and all three dots are linear in position, so perspective-correct
// interpolation gives each pixel exact values; the fragment stage divides.
// v_ray_on is 0 for a face with no projector: it keeps its baked texcoord.
out vec3 v_ray;
out float v_ray_on;

// The rev-241 client's depth key at camera depth 76, one unit past its pole
// at 75: the nearest key the OSRS pass maps (window depth 1).
const float OSRS_KEY_MAX = 150.75377 / 1.0100503 + 1.0100503;

void main() {
    vec4 wp = vec4(a_position.xyz, 1.0);
    gl_Position = ubo.u_projectionMatrix * ubo.u_modelViewMatrix * wp;
    // uDepthBias 2: the OSRS pass ("OpenGL (Post-TFA OSRS)"). The client keys
    // each corner at 150.75377 / ((z - 2*bias - 75) * 1.0100503) + 1.0100503
    // and interpolates the key linearly across the screen, as the hardware
    // does NDC z; the pass clears to 0 and tests GREATER, so window depth =
    // key / OSRS_KEY_MAX keeps the client's order and its tie rule. a_position.w
    // is already 2*bias. Corners inside the pole get the client's negative
    // key, below the clear, and are clipped. docs/toridraw_rszbuffer.md.
    if (ubo.uDepthBias > 1.5) {
        float osrs_depth = gl_Position.w - a_position.w;
        float scaled = (osrs_depth - 75.0) * 1.0100503;
        if (abs(scaled) < 1.0e-4) scaled = -1.0e-4;
        float key = 150.75377 / scaled + 1.0100503;
        gl_Position.z = min(2.0 * key / OSRS_KEY_MAX - 1.0, 1.0) * gl_Position.w;
    }
    // The face's depth bias (rev 241, class174.field2408 * 2, baked into w):
    // its camera depth less the bias decides the depth test, its position
    // does not move. The depth projection has w = camera depth, so clip z is
    // rebuilt from the biased depth, clamped to [-w, w] so a bias never
    // clips or unclips a corner.
    else if (ubo.uDepthBias > 0.5 && a_position.w != 0.0) {
        float depth = gl_Position.w;
        float biased = max(depth - a_position.w, 1.0);
        float ndc = ubo.u_projectionMatrix[2][2] + ubo.u_projectionMatrix[3][2] / biased;
        gl_Position.z = clamp(ndc, -1.0, 1.0) * depth;
    }
    v_color = a_color;
    v_texcoord = a_texcoord;
    v_tex_id = a_tex_id;
    v_uv_pack = a_uv_mode;
    vec3 r = a_position.xyz - ubo.uEye.xyz;
    v_ray = vec3(dot(r, cross(a_proj_p, a_proj_v)),
                 dot(r, cross(a_proj_u, a_proj_p)),
                 dot(r, cross(a_proj_u, a_proj_v)));
    v_ray_on = dot(a_proj_u, a_proj_u) > 0.0 ? 1.0 : 0.0;
}
