#pragma once

#include <cstdint>
#include <vector>

struct FontDrawGroup
{
    int font_id = -1;
    void* atlas_tex = nullptr;
    uint32_t first_float = 0;
    uint32_t float_count = 0;
};

/**
 * Batches font glyph quads (48 floats per glyph: 6 verts × 8 floats) until flush.
 * One Metal draw per (font_id, atlas) group.
 */
class BufferedFont2D
{
public:
    void
    begin_pass()
    {
        verts_.clear();
        groups_.clear();
        cur_font_ = -1;
        cur_atlas_ = nullptr;
        segment_start_ = 0;
    }

    void
    set_font(int font_id, void* atlas_tex)
    {
        if( cur_font_ == font_id && cur_atlas_ == atlas_tex )
            return;
        if( cur_font_ >= 0 && verts_.size() > segment_start_ )
        {
            groups_.push_back(FontDrawGroup{
                cur_font_,
                cur_atlas_,
                segment_start_,
                (uint32_t)(verts_.size() - segment_start_),
            });
        }
        cur_font_ = font_id;
        cur_atlas_ = atlas_tex;
        segment_start_ = (uint32_t)verts_.size();
    }

    void
    append_glyph_quad(const float vq[48])
    {
        if( cur_font_ < 0 || !cur_atlas_ )
            return;
        verts_.insert(verts_.end(), vq, vq + 48);
    }

    /** Finalize the open segment into groups() (call before iterating groups to draw). */
    void
    close_open_segment()
    {
        if( cur_font_ >= 0 && verts_.size() > segment_start_ )
        {
            groups_.push_back(FontDrawGroup{
                cur_font_,
                cur_atlas_,
                segment_start_,
                (uint32_t)(verts_.size() - segment_start_),
            });
        }
        cur_font_ = -1;
        cur_atlas_ = nullptr;
        segment_start_ = (uint32_t)verts_.size();
    }

    const std::vector<FontDrawGroup>&
    groups() const
    {
        return groups_;
    }

    const std::vector<float>&
    verts() const
    {
        return verts_;
    }

private:
    std::vector<float> verts_;
    std::vector<FontDrawGroup> groups_;
    int cur_font_ = -1;
    void* cur_atlas_ = nullptr;
    uint32_t segment_start_ = 0;
};
