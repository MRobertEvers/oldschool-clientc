#include <metal_stdlib>
using namespace metal;

// Must match MetalVertex / MetalUniforms / MetalInstanceUniform in platforms/metal/metal_internal.h
struct Vertex {
    float4 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 texcoord [[attribute(2)]];
    ushort2 texIdPack [[attribute(3)]];
};

struct InstanceUniform {
    float4 t0;
    float4 t1;
};

struct Uniforms {
    float4x4 modelViewMatrix;
    float4x4 projectionMatrix;
    float4 uClockPad;
};

struct AtlasTile {
    float4 uvRect;
    float4 animOp;
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
    float2 texcoord;
    uint tex_id [[flat]];
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
    out.tex_id = (uint)in.texIdPack.x;
    return out;
}

fragment float4 fragmentShader(
    VertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]],
    texture2d<float> atlas [[texture(0)]],
    sampler samp [[sampler(0)]],
    constant AtlasTile* tiles [[buffer(4)]])
{
    if( in.tex_id >= 256u )
    {
        return float4(in.color.rgb, in.color.a);
    }
    AtlasTile t = tiles[in.tex_id];
    if( t.uvRect.z <= 0.0 || t.uvRect.w <= 0.0 )
        return float4(in.color.rgb, in.color.a);

    float2 local = in.texcoord;
    const float clk = uniforms.uClockPad.x;
    local.x = fract(local.x + clk * t.animOp.x);
    local.y = fract(local.y + clk * t.animOp.y);
    float2 uv_atlas = t.uvRect.xy + local * t.uvRect.zw;
    float4 texColor = atlas.sample(samp, uv_atlas);

    float3 finalColor = mix(in.color.rgb, texColor.rgb * in.color.rgb, 1.0);
    float finalAlpha = in.color.a;

    if( t.animOp.z < 0.5 )
    {
        if( texColor.a < 0.5 )
            discard_fragment();
    }

    return float4(finalColor, finalAlpha);
}

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
    o.uv = in.uv;
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

fragment float4 torirsClearRectFrag(UIVertOut in [[stage_in]])
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

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
    o.uv = in.uv;
    o.color = in.color;
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
