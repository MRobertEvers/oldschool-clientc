#pragma once

#include <cstdint>
#include <vector>

class Buffered2DOrder;

/** One sprite vertex: clip-space xy + UV (matches UI sprite pipeline). */
struct SpriteQuadVertex
{
    float cx, cy, u, v;
};

/** Fragment constants for inverse-map rotated blit; must match `SpriteInverseRotParams` in Shaders.metal. */
struct SpriteInverseRotParams
{
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

static_assert(sizeof(SpriteInverseRotParams) == 64, "SpriteInverseRotParams must match MSL");

/** Logical scissor in drawable pixels (same fields as Metal's MTLScissorRect). */
struct SpriteLogicalScissor
{
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct SpriteDrawGroup
{
    void* atlas_tex = nullptr;
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
    bool has_scissor = false;
    SpriteLogicalScissor scissor{};
    bool inverse_rot_blit = false;
    SpriteInverseRotParams inverse_rot_params{};
};

/**
 * Frame-scoped 2D sprite batch: merge consecutive draws that share the same atlas texture
 * and scissor into one draw call at flush time.
 */
class BufferedSprite2D
{
public:
    void
    begin_pass()
    {
        groups_.clear();
        verts_.clear();
    }

    void
    enqueue(
        void* atlas_tex,
        const SpriteQuadVertex six_verts[6],
        const SpriteLogicalScissor* opt_scissor,
        Buffered2DOrder* order = nullptr,
        bool* split_sprite_before_next_enqueue = nullptr,
        const SpriteInverseRotParams* inverse_rot_params = nullptr)
    {
        const bool force_split =
            split_sprite_before_next_enqueue && *split_sprite_before_next_enqueue;
        if( force_split )
            *split_sprite_before_next_enqueue = false;

        const bool has_sc = (opt_scissor != nullptr);
        const bool inverse_rot = (inverse_rot_params != nullptr);
        bool need_new = groups_.empty() || force_split || inverse_rot;
        if( !need_new )
        {
            const SpriteDrawGroup& g = groups_.back();
            if( g.atlas_tex != atlas_tex || g.has_scissor != has_sc )
                need_new = true;
            else if( has_sc )
            {
                const SpriteLogicalScissor& a = g.scissor;
                const SpriteLogicalScissor& b = *opt_scissor;
                if( a.x != b.x || a.y != b.y || a.width != b.width || a.height != b.height )
                    need_new = true;
            }
        }
        if( need_new )
        {
            SpriteDrawGroup ng{};
            ng.atlas_tex = atlas_tex;
            ng.first_vertex = (uint32_t)verts_.size();
            ng.vertex_count = 0;
            ng.has_scissor = has_sc;
            if( has_sc )
                ng.scissor = *opt_scissor;
            if( inverse_rot )
            {
                ng.inverse_rot_blit = true;
                ng.inverse_rot_params = *inverse_rot_params;
            }
            groups_.push_back(ng);
            if( order )
                order->push_sprite((uint32_t)(groups_.size() - 1u));
        }
        for( int i = 0; i < 6; ++i )
            verts_.push_back(six_verts[i]);
        groups_.back().vertex_count += 6u;
    }

    const std::vector<SpriteDrawGroup>&
    groups() const
    {
        return groups_;
    }

    const std::vector<SpriteQuadVertex>&
    verts() const
    {
        return verts_;
    }

private:
    std::vector<SpriteDrawGroup> groups_;
    std::vector<SpriteQuadVertex> verts_;
};
