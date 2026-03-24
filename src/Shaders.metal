#include <metal_stdlib>
using namespace metal;

// Must match MetalVertex / MetalUniforms in platform_impl2_osx_sdl2_renderer_metal.mm
struct Vertex {
    float4 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 texcoord [[attribute(2)]];
    float texBlend [[attribute(3)]];
    float textureOpaque [[attribute(4)]];
    float textureAnimSpeed [[attribute(5)]];
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
    float textureOpaque [[flat]];
    float textureAnimSpeed [[flat]];
};

vertex VertexOut vertexShader(Vertex in [[stage_in]], constant Uniforms& uniforms [[buffer(1)]])
{
    VertexOut out;
    float4 pos = float4(in.position.xyz, 1.0);
    out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * pos;
    out.color = in.color;
    out.texcoord = in.texcoord;
    out.texBlend = in.texBlend;
    out.textureOpaque = in.textureOpaque;
    out.textureAnimSpeed = in.textureAnimSpeed;
    return out;
}

// Mirrors g_fragment_shader_core in pix3dgl.cpp: local UV clamp / V wrap, then tex * lighting.
fragment float4 fragmentShader(
    VertexOut in [[stage_in]],
    constant Uniforms& uniforms [[buffer(1)]],
    texture2d<float> tex [[texture(0)]],
    sampler samp [[sampler(0)]])
{
    float2 localUV = in.texcoord;
    if( in.texBlend > 0.5 )
    {
        if( in.textureAnimSpeed > 0.0 )
            localUV.x = localUV.x + (uniforms.uClockPad.x * in.textureAnimSpeed);
        else if( in.textureAnimSpeed < 0.0 )
            localUV.y = localUV.y - (uniforms.uClockPad.x * (-in.textureAnimSpeed));
        localUV.x = clamp(localUV.x, 0.008, 0.992);
        localUV.y = clamp(fract(localUV.y), 0.008, 0.992);
    }

    float4 texColor = tex.sample(samp, localUV);

    float3 finalColor = mix(in.color.rgb, texColor.rgb * in.color.rgb, in.texBlend);
    float finalAlpha = in.color.a;

    if( in.texBlend > 0.5 )
    {
        if( in.textureOpaque < 0.5 )
        {
            if( texColor.a < 0.5 )
                discard_fragment();
        }
    }

    return float4(finalColor, finalAlpha);
}
