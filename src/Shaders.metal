#include <metal_stdlib>
using namespace metal;

// Must match MetalVertex / MetalUniforms / InstanceXform / DrawStreamEntry in metal_internal /
// buffered_face_order (C++). C packs float[4] at 4-byte alignment (44 bytes total); use packed_* so
// indexing matches CPU sizeof(MetalVertex).
struct MetalVertexPacked {
    packed_float4 position;
    packed_float4 color;
    packed_float2 texcoord;
    ushort tex_id;
    ushort uv_mode; // 0: OpenGL-style clamp U / fract+inset V. 1: VA dual-fract tile.
};

/* 32 bytes; must match InstanceXform in buffered_face_order.h.
 *  cos_yaw/sin_yaw: cosf/sinf(metal_dash_yaw_to_radians(Dash yaw)) on CPU (Metal renderer). */
struct InstanceXform {
    float cos_yaw;
    float sin_yaw;
    float x;
    float y;
    float z;
    uint angle_encoding;
    int _pad0;
    int _pad1;
};

struct DrawEntry {
    uint vertex_index;
    uint instance_id;
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
    uint uv_mode [[flat]];
};

vertex VertexOut vertexShader(
    uint vid [[vertex_id]],
    constant Uniforms& uniforms [[buffer(1)]],
    device const InstanceXform* instances [[buffer(2)]],
    device const DrawEntry* stream [[buffer(3)]],
    device const MetalVertexPacked* verts [[buffer(0)]])
{
    DrawEntry e = stream[vid];
    MetalVertexPacked v = verts[e.vertex_index];
    InstanceXform t = instances[e.instance_id];

    float3 p = float4(v.position).xyz;
    /* projection.u.c / D3D11 xz yaw; trig was prebaked on CPU into cos_yaw/sin_yaw. */
    float xr = p.x * t.cos_yaw + p.z * t.sin_yaw;
    float zr = -p.x * t.sin_yaw + p.z * t.cos_yaw;
    float4 worldPos = float4(xr + t.x, p.y + t.y, zr + t.z, 1.0);

    VertexOut out;
    out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * worldPos;
    out.color = float4(v.color);
    out.texcoord = float2(v.texcoord);
    out.tex_id = (uint)v.tex_id;
    out.uv_mode = (uint)v.uv_mode;
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
    if( in.uv_mode == 0u ) {
        // OpenGL: clamp U, fract+inset V; V scroll with clock minus
        if( t.animOp.x > 0.0 )
            local.x += clk * t.animOp.x;
        if( t.animOp.y > 0.0 )
            local.y -= clk * t.animOp.y;
        local.x = clamp(local.x, 0.008, 0.992);
        local.y = clamp(fract(local.y), 0.008, 0.992);
    } else {
        // VA/FA: uv_pnm can exceed 0-1; tile U and V like pre-clamp Metal (dual fract + scroll)
        if( t.animOp.x > 0.0 )
            local.x += clk * t.animOp.x;
        if( t.animOp.y > 0.0 )
            local.y += clk * t.animOp.y;
        local.x = fract(local.x);
        local.y = fract(local.y);
    }
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
