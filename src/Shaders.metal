#include <metal_stdlib>
using namespace metal;

// Vertex structure matching our Objective-C++ code
struct Vertex {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 texCoord [[attribute(2)]];  // UV coordinates
};

// Uniform structure matching our Objective-C++ code
struct Uniforms {
    float4x4 modelViewMatrix;
    float4x4 projectionMatrix;
    float4x4 textureMatrix;  // Optional matrix for texture coordinate transforms
};

// Vertex shader output structure
struct VertexOut {
    float4 position [[position]];
    float4 color;
    float2 texCoord;  // Pass UV coordinates to fragment shader
};

vertex VertexOut vertexShader(Vertex in [[stage_in]],
                            constant Uniforms& uniforms [[buffer(1)]]) {
    VertexOut out;
    float4 pos = float4(in.position, 1.0);
    out.position = uniforms.projectionMatrix * uniforms.modelViewMatrix * pos;
    out.color = in.color;
    
    // Transform texture coordinates if needed
    float4 texCoord = float4(in.texCoord, 0.0, 1.0);
    float4 transformedTexCoord = uniforms.textureMatrix * texCoord;
    out.texCoord = transformedTexCoord.xy / transformedTexCoord.w;
    
    return out;
}

fragment float4 fragmentShader(VertexOut in [[stage_in]],
                             texture2d<float> diffuseTexture [[texture(0)]],
                             sampler textureSampler [[sampler(0)]]) {
    // Sample texture and blend with vertex color
    float4 texColor = diffuseTexture.sample(textureSampler, in.texCoord);
    return texColor * in.color;
} 