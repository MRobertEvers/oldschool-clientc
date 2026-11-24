// The win64 OSRS renderers' world programs ("Direct3D 9 (Sailing OSRS)" and
// "(Post-TFA OSRS)", src/platform/platform_win64_renderer_d3d9sm3_*.c).
//
// Compiled offline, never at run time: tools/d3d9_shaderc (D3DCompile from the
// Windows SDK's d3dcompiler_47.dll) writes the bytecode into
// ../d3d9_rev241_shaders.h, which is checked in. Edit this, then on Windows:
//   tools\d3d9_shaderc\build.cmd
//
// The vertex the D3D9 core bakes (TRSPK_VertexD3D9, D3DFVF_XYZ | DIFFUSE |
// TEX2): position, colour, texcoord, and texdata = (tex id, twice the face's
// rev-241 depth bias).

row_major float4x4 u_view_proj : register(c0);
// x: 1 keys depth the client's way, 0 runs the biased standard mapping
// (a parallel camera); z, w: that mapping's A + B / depth.
float4 u_depth : register(c4);
// The bound texture's scroll (d3d9_bind_world_config writes it).
float4 u_scroll : register(c5);
// Post-TFA: a projected run's (s, t, q) as rows over (x, y, z, 1).
row_major float4x4 u_projector : register(c6);

sampler2D s_texture : register(s0);

struct VSIn
{
    float3 position : POSITION;
    float4 color : COLOR0;
    float2 texcoord : TEXCOORD0;
    float2 texdata : TEXCOORD1;
};

struct VSOut
{
    float4 position : POSITION;
    float4 color : COLOR0;
    float4 texcoord : TEXCOORD0;
};

// The rev-241 client's depth key at camera depth 76, one unit past its pole:
// the nearest key the pass maps (window depth 1).
static const float OSRS_KEY_MAX = 150.75377 / 1.0100503 + 1.0100503;

// Clip z for this corner. The client keys each corner at
//   150.75377 / ((z - 2*bias - 75) * 1.0100503) + 1.0100503
// and interpolates the key linearly across the screen, as the hardware does
// window z; the pass clears to 0 and tests GREATER, so window z = key /
// OSRS_KEY_MAX keeps the client's order and its tie rule. Corners inside the
// pole get its negative key and are clipped. texdata.y is already 2*bias.
float4 rev241_clip(float3 position, float bias2)
{
    float4 clip = mul(float4(position, 1.0), u_view_proj);
    float depth = clip.w;
    if( u_depth.x > 0.5 )
    {
        float scaled = (depth - bias2 - 75.0) * 1.0100503;
        if( abs(scaled) < 1.0e-4 )
            scaled = -1.0e-4;
        float key = 150.75377 / scaled + 1.0100503;
        clip.z = min(key / OSRS_KEY_MAX, 1.0) * clip.w;
    }
    else if( bias2 != 0.0 )
    {
        float biased = max(depth - bias2, 1.0);
        clip.z = saturate(u_depth.z + u_depth.w / biased) * clip.w;
    }
    return clip;
}

VSOut world_vs(VSIn i)
{
    VSOut o;
    o.position = rev241_clip(i.position, i.texdata.y);
    o.color = i.color;
    o.texcoord = float4(i.texcoord + u_scroll.xy, 0.0, 1.0);
    return o;
}

// The client's eye-ray texel [class171.method5476]: (s, t, q) linear in
// position, divided per pixel.
VSOut projected_vs(VSIn i)
{
    VSOut o;
    float4 stq = mul(float4(i.position, 1.0), u_projector);
    o.position = rev241_clip(i.position, i.texdata.y);
    o.color = i.color;
    o.texcoord = float4(stq.x, stq.y, 0.0, stq.z);
    return o;
}

float4 world_ps(float4 color : COLOR0, float4 texcoord : TEXCOORD0) : COLOR
{
    return tex2D(s_texture, texcoord.xy) * color;
}

float4 projected_ps(float4 color : COLOR0, float4 texcoord : TEXCOORD0) : COLOR
{
    return tex2Dproj(s_texture, texcoord) * color;
}
