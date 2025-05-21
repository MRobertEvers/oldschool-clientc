#include <metal_stdlib>
using namespace metal;

// Vertex structure matching our Objective-C++ code
struct Vertex {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
};

// Uniform structure matching our Objective-C++ code
struct Uniforms {
    float4x4 modelViewMatrix;
    float4x4 projectionMatrix;
};

// Vertex shader output structure
struct VertexOut {
    float4 position [[position]];
    float4 color;
};

vertex VertexOut vertexShader(Vertex in [[stage_in]],
                              constant Uniforms& uniforms [[buffer(1)]]) {
    VertexOut out;
    float4 pos = float4(in.position, 1.0);
    out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * pos;
    out.color = in.color;
    return out;
}

fragment float4 fragmentShader(VertexOut in [[stage_in]]) {
    return in.color;
} 