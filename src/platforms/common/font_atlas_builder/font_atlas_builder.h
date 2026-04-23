#pragma once

#include <cstdint>
#include <vector>

/**
 * FontAtlasEntry — descriptor for a single font atlas page to be uploaded.
 *
 * The pixel pointer is NON-OWNED.  The caller must keep the pixel data
 * alive at least until the corresponding Submit call returns.
 */
struct FontAtlasEntry
{
    int font_id;                ///< GpuFontCache font id (0 .. GpuFontCache::kMaxFonts-1)
    const uint8_t* pixels_rgba; ///< RGBA8 rows, width*height*4 bytes
    int width;
    int height;
};

/**
 * FontAtlasBuilder — accumulates font atlas descriptors for bulk GPU upload.
 *
 * Usage:
 *   FontAtlasBuilder b;
 *   b.add_font(font_id, atlas->rgba_pixels, atlas->atlas_width, atlas->atlas_height);
 *   FontAtlasBuilderSubmitMetal(b, ctx);   // or WebGL1 variant
 *
 * Each Submit function creates a GPU texture from the RGBA data and registers
 * it with the renderer's GpuFontCache.  Entries whose font_id is already
 * cached are skipped silently.
 */
class FontAtlasBuilder
{
public:
    void
    reset();
    void
    add_font(
        int font_id,
        const uint8_t* pixels_rgba,
        int width,
        int height);

    const std::vector<FontAtlasEntry>&
    entries() const;
    bool
    empty() const;

private:
    std::vector<FontAtlasEntry> entries_;
};

// ---------------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------------

/** Discard all accumulated entries (keeps capacity). */
inline void
FontAtlasBuilder::reset()
{
    entries_.clear();
}

/** Enqueue a font atlas for upload. pixels_rgba must remain valid through Submit. */
inline void
FontAtlasBuilder::add_font(
    int font_id,
    const uint8_t* pixels_rgba,
    int width,
    int height)
{
    entries_.push_back(FontAtlasEntry{ font_id, pixels_rgba, width, height });
}

inline const std::vector<FontAtlasEntry>&
FontAtlasBuilder::entries() const
{
    return entries_;
}

inline bool
FontAtlasBuilder::empty() const
{
    return entries_.empty();
}
