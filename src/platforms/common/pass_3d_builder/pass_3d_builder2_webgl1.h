#ifndef PASS_3D_BUILDER2_WEBGL1_H
#define PASS_3D_BUILDER2_WEBGL1_H

#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"

#include <cstdint>
#include <vector>

#ifdef __EMSCRIPTEN__
#    include <GLES2/gl2.h>
#endif

struct WebGL1RendererCore;

inline constexpr uint32_t kPass3DBuilder2WebGL1DynamicIndexCapacity = 1u << 21; // 2_097_152

/** Uniform / attribute locations for world 3D program (stride-48 interleaved mesh). */
struct WebGL1WorldShaderLocs
{
    GLint a_position = -1;
    GLint a_color = -1;
    GLint a_texcoord = -1;
    GLint a_tex_id = -1;
    GLint a_uv_mode = -1;
    GLint u_modelViewMatrix = -1;
    GLint u_projectionMatrix = -1;
    GLint u_clock = -1;
    GLint s_atlas = -1;
    /** `glUniform4fv(loc, 256, ...)` packed `WebGL1AtlasTile`: u0,v0,du,dv per slot. */
    GLint u_tileA = -1;
    /** anim_u, anim_v, opaque, pad per slot. */
    GLint u_tileB = -1;
};

/** One contiguous range in the dynamic IBO for draws sharing one mesh VBO. */
struct Pass3DWebGL1Slice
{
    GPUResourceHandle vbo = 0;
    uint32_t index_offset = 0;
    uint32_t index_count = 0;
};

/** Scenery-only: uint16 indices; one `glDrawElements` per slice (per mesh VBO). */
class Pass3DBuilder2WebGL1
{
private:
    bool is_building_ = false;
    std::vector<uint16_t> indices_pool_;
    std::vector<Pass3DWebGL1Slice> slices_;

public:
    Pass3DBuilder2WebGL1()
    {
        indices_pool_.reserve(4096u * 16u);
        slices_.reserve(8u);
    }

    void
    Begin();

    void
    End()
    {
        is_building_ = false;
    }

    bool
    IsBuilding() const
    {
        return is_building_;
    }

    bool
    HasCommands() const
    {
        return !indices_pool_.empty() && !slices_.empty();
    }

    void
    ClearAfterSubmit();

    const std::vector<uint16_t>&
    Indices() const
    {
        return indices_pool_;
    }

    const std::vector<uint16_t>&
    GetDynamicIndices() const
    {
        return indices_pool_;
    }

    bool
    HasDynamicIndices() const
    {
        return !indices_pool_.empty();
    }

    const std::vector<Pass3DWebGL1Slice>&
    Slices() const
    {
        return slices_;
    }

    /** Drops the draw if any biased index would exceed 0xFFFF (uint16 ceiling). */
    void
    AppendSortedDraw(
        GPUResourceHandle vbo,
        uint32_t pose_vbo_vertex_offset,
        const uint16_t* sorted_indices,
        uint32_t index_count);
};

#ifdef __EMSCRIPTEN__

void
Pass3DBuilder2SubmitWebGL1(
    Pass3DBuilder2WebGL1& builder,
    struct WebGL1RendererCore* webgl_renderer,
    GLuint program,
    const WebGL1WorldShaderLocs& locs,
    GLuint fragment_atlas_texture,
    const float modelViewMatrix[16],
    const float projectionMatrix[16],
    float u_clock);

#endif

#endif // PASS_3D_BUILDER2_WEBGL1_H
