#ifndef PLATFORMS_WEBGL1_WEBGL1_VERTEX_H
#define PLATFORMS_WEBGL1_WEBGL1_VERTEX_H

#include <cstdint>

/** Same layout as `MetalVertex` / Shaders.metal `MetalVertexPacked`. */
struct WgVertex
{
    float position[4];
    float color[4];
    float texcoord[2];
    uint16_t tex_id;
    uint16_t uv_mode;
};

enum WgVertexUvMode : uint16_t
{
    kWgVertexUvMode_Standard = 0,
    kWgVertexUvMode_VaFractTile = 1
};

#endif
