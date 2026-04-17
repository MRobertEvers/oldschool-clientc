#include <metal_stdlib>
using namespace metal;

// Must match MetalVertex / MetalUniforms / MetalInstanceUniform / MetalRunUniform in
// platform_impl2_sdl2_renderer_metal.mm
struct Vertex {
    float4 position [[attribute(0)]]; // xyz = model-local, w = 1
    float4 color [[attribute(1)]];
    float2 texcoord [[attribute(2)]];
    float texBlend [[attribute(3)]];
};

struct InstanceUniform {
    float4 t0; // cos_yaw, sin_yaw, world_x, world_y
    float4 t1; // world_z, pad
};

struct RunUniform {
    float texBlendOverride;
    float textureOpaque;
    float textureAnimSpeed;
    float pad;
};

struct Uniforms {
    float4x4 modelViewMatrix;
    float4x4 projectionMatrix;
    float4 uClockPad; // .x = animation time (seconds), matches pix3dgl uClock
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
    float2 texcoord;
    float texBlend [[flat]];
};

vertex VertexOut vertexShader(
    Vertex in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]],
    constant InstanceUniform& inst [[buffer(2)]])
{
    VertexOut out;
    float lx = in.position.x;
    float ly = in.position.y;
    float lz = in.position.z;
    float wx = inst.t0.x * lx + inst.t0.y * lz + inst.t0.z;
    float wy = ly + inst.t0.w;
    float wz = -inst.t0.y * lx + inst.t0.x * lz + inst.t1.x;
    float4 worldPos = float4(wx, wy, wz, 1.0);
    out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * worldPos;
    out.color = in.color;
    out.texcoord = in.texcoord;
    out.texBlend = in.texBlend;
    return out;
}

fragment float4 fragmentShader(
    VertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]],
    constant RunUniform& runU [[buffer(3)]],
    texture2d<float> tex [[texture(0)]],
    sampler samp [[sampler(0)]])
{
    float tb = min(in.texBlend, runU.texBlendOverride);
    float2 localUV = in.texcoord;
    if( tb > 0.5 )
    {
        if( runU.textureAnimSpeed > 0.0 )
            localUV.x = localUV.x + (uniforms.uClockPad.x * runU.textureAnimSpeed);
        else if( runU.textureAnimSpeed < 0.0 )
            localUV.y = localUV.y - (uniforms.uClockPad.x * (-runU.textureAnimSpeed));
        localUV.x = clamp(localUV.x, 0.008, 0.992);
        localUV.y = clamp(fract(localUV.y), 0.008, 0.992);
    }

    float4 texColor = tex.sample(samp, localUV);

    float3 finalColor = mix(in.color.rgb, texColor.rgb * in.color.rgb, tb);
    float finalAlpha = in.color.a;

    if( tb > 0.5 )
    {
        if( runU.textureOpaque < 0.5 )
        {
            if( texColor.a < 0.5 )
                discard_fragment();
        }
    }

    return float4(finalColor, finalAlpha);
}

// ---------------------------------------------------------------------------
// Simple textured quad for UI sprites (full-framebuffer clip space, no atlas)
// ---------------------------------------------------------------------------
struct UIVertIn {
    float2 p [[attribute(0)]];
    float2 uv [[attribute(1)]];
};

struct UIVertOut {
    float4 position [[position]];
    float2 uv;
};

vertex UIVertOut uiSpriteVert(UIVertIn in [[stage_in]])
{
    UIVertOut o;
    o.position = float4(in.p, 0.0, 1.0);
    o.uv       = in.uv;
    return o;
}

fragment float4 uiSpriteFrag(
    UIVertOut in [[stage_in]],
    texture2d<float> tex [[texture(0)]],
    sampler samp [[sampler(0)]])
{
    float4 c = tex.sample(samp, in.uv);
    if( c.a < 0.01 )
        discard_fragment();
    return c;
}

/** Solid transparent clear for TORIRS_GFX_CLEAR_RECT (no texture). */
fragment float4 torirsClearRectFrag(UIVertOut in [[stage_in]])
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

// ---------------------------------------------------------------------------
// Font atlas rendering: per-vertex color tinted by atlas alpha
// ---------------------------------------------------------------------------
struct UIFontVertIn {
    float2 p [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
};

struct UIFontVertOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

vertex UIFontVertOut uiFontVert(UIFontVertIn in [[stage_in]])
{
    UIFontVertOut o;
    o.position = float4(in.p, 0.0, 1.0);
    o.uv       = in.uv;
    o.color    = in.color;
    return o;
}

fragment float4 uiFontFrag(
    UIFontVertOut in [[stage_in]],
    texture2d<float> tex [[texture(0)]],
    sampler samp [[sampler(0)]])
{
    float a = tex.sample(samp, in.uv).a;
    if( a < 0.01 )
        discard_fragment();
    return float4(in.color.rgb, a * in.color.a);
}
