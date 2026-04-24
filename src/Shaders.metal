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
        // VA/FA: uv_pnm can exceed 0-1; tile U and V with fract, then inset like mode 0 (atlas margin)
        if( t.animOp.x > 0.0 )
            local.x += clk * t.animOp.x;
        if( t.animOp.y > 0.0 )
            local.y += clk * t.animOp.y;
        local.x = clamp(fract(local.x), 0.008, 0.992);
        local.y = clamp(fract(local.y), 0.008, 0.992);
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

/** Must match `SpriteInverseRotParams` in `buffered_sprite_2d.h` (16 floats, 64 bytes). */
struct SpriteInverseRotParams {
    float dst_origin_x;
    float dst_origin_y;
    float dst_anchor_x;
    float dst_anchor_y;
    float src_anchor_x;
    float src_anchor_y;
    float src_crop_x;
    float src_crop_y;
    float src_size_x;
    float src_size_y;
    float cos_val;
    float sin_val;
    float tex_size_x;
    float tex_size_y;
    float _pad0;
    float _pad1;
};

/** Inverse-map blit: `in.uv` holds logical destination pixel (x,y); matches `dash2d_blit_rotated_ex`. */
fragment float4 uiSpriteInverseRotFrag(
    UIVertOut in [[stage_in]],
    constant SpriteInverseRotParams& params [[buffer(1)]],
    texture2d<float, access::sample> src_tex [[texture(0)]])
{
    float dst_x_abs = floor(in.uv.x);
    float dst_y_abs = floor(in.uv.y);

    float rel_x = dst_x_abs - params.dst_origin_x - params.dst_anchor_x;
    float rel_y = dst_y_abs - params.dst_origin_y - params.dst_anchor_y;

    float src_rel_x = (rel_x * params.cos_val) + (rel_y * params.sin_val);
    float src_rel_y = (-rel_x * params.sin_val) + (rel_y * params.cos_val);

    float src_x = floor(params.src_anchor_x + src_rel_x);
    float src_y = floor(params.src_anchor_y + src_rel_y);

    if( src_x < 0.0 || src_x >= params.src_size_x || src_y < 0.0 || src_y >= params.src_size_y )
        discard_fragment();

    float bx = params.src_crop_x + src_x;
    float by = params.src_crop_y + src_y;

    float2 uv = float2(bx / params.tex_size_x, by / params.tex_size_y);

    constexpr sampler point_sampler(
        coord::normalized,
        address::clamp_to_zero,
        filter::nearest);
    float4 src_pixel = src_tex.sample(point_sampler, uv);

    if( src_pixel.a == 0.0 ||
        (src_pixel.r == 0.0 && src_pixel.g == 0.0 && src_pixel.b == 0.0) )
        discard_fragment();

    return src_pixel;
}

fragment float4 torirsClearRectFrag(UIVertOut in [[stage_in]])
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

/** Same clip xy as UI clears; z/w chosen so NDC depth == 1.0 (match main pass clearDepth). */
vertex UIVertOut torirsClearRectDepthVert(UIVertIn in [[stage_in]])
{
    UIVertOut o;
    o.position = float4(in.p.x, in.p.y, 1.0, 1.0);
    o.uv = in.uv;
    return o;
}

fragment float4 torirsClearRectDepthFrag(UIVertOut in [[stage_in]])
{
    (void)in;
    return float4(0.0);
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

// ============================================================
// v2 3D pass: packed uint16_t vertices + integer instance data
// ============================================================
struct ModelVertexV2
{
    ushort x;
    ushort y;
    ushort z;
    ushort color_lo; // R | (G << 8)
    ushort color_hi; // B | (A << 8)
    ushort u;
    ushort v;
    ushort tex_id;
};

struct InstanceDataV2
{
    int rotation_r2pi2048;
    int x;
    int y;
    int z;
};

vertex VertexOut vertexShader3DV2(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    device const ModelVertexV2* verts [[buffer(0)]],
    device const InstanceDataV2* instances [[buffer(1)]],
    constant Uniforms& uniforms [[buffer(2)]])
{
    // drawIndexedPrimitives: vid is the resolved vertex index from the index buffer.
    ModelVertexV2 v = verts[vid];
    InstanceDataV2 inst = instances[iid];

    float vx = (float)as_type<short>(v.x);
    float vy = (float)as_type<short>(v.y);
    float vz = (float)as_type<short>(v.z);

    float cr = (float)(v.color_lo & 0xFFu) / 255.0f;
    float cg = (float)((v.color_lo >> 8u) & 0xFFu) / 255.0f;
    float cb = (float)(v.color_hi & 0xFFu) / 255.0f;
    float ca = (float)((v.color_hi >> 8u) & 0xFFu) / 255.0f;

    float angle = (float)inst.rotation_r2pi2048 * (2.0f * M_PI_F / 2048.0f);
    float cos_yaw = cos(angle);
    float sin_yaw = sin(angle);
    float xr = vx * cos_yaw + vz * sin_yaw;
    float zr = -vx * sin_yaw + vz * cos_yaw;

    float4 worldPos = float4(
        xr + (float)inst.x,
        vy + (float)inst.y,
        zr + (float)inst.z,
        1.0f);

    VertexOut out;
    out.position  = uniforms.projectionMatrix * uniforms.modelViewMatrix * worldPos;
    out.color     = float4(cr, cg, cb, ca);
    out.texcoord  = float2((float)v.u, (float)v.v) * (1.0f / 65535.0f);
    out.tex_id    = (uint)v.tex_id;
    out.uv_mode   = 0u;
    return out;
}
