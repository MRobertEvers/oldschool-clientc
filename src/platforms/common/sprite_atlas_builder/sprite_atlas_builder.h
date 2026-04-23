#pragma once

#include <cstdint>
#include <vector>

/**
 * SpriteAtlasEntry — descriptor for a single sprite to be uploaded.
 *
 * The pixel pointer is NON-OWNED.  The caller must keep the pixel data
 * alive at least until the corresponding Submit call returns.
 */
struct SpriteAtlasEntry
{
    int element_id;              ///< GpuSpriteCache element dimension
    int atlas_index;             ///< GpuSpriteCache atlas dimension
    const uint32_t* pixels_argb; ///< packed 0xAARRGGBB rows, w*h pixels
    int width;
    int height;
};

/**
 * SpriteAtlasBuilder — accumulates sprite descriptors for bulk GPU upload.
 *
 * Usage:
 *   SpriteAtlasBuilder b;
 *   b.add_sprite(el, ai, sp->pixels_argb, sp->width, sp->height);
 *   SpriteAtlasBuilderSubmitMetal(b, ctx);   // or WebGL1 variant
 *
 * Each Submit function converts ARGB→RGBA, creates a GPU texture, and
 * registers the entry with the renderer's GpuSpriteCache.  Entries that
 * are already cached are skipped silently.
 */
class SpriteAtlasBuilder
{
public:
    void
    reset();
    void
    add_sprite(
        int element_id,
        int atlas_index,
        const uint32_t* pixels_argb,
        int width,
        int height);

    const std::vector<SpriteAtlasEntry>&
    entries() const;
    bool
    empty() const;

private:
    std::vector<SpriteAtlasEntry> entries_;
};

// ---------------------------------------------------------------------------
// Inline implementations
// ---------------------------------------------------------------------------

/** Discard all accumulated entries (keeps capacity). */
inline void
SpriteAtlasBuilder::reset()
{
    entries_.clear();
}

/** Enqueue a sprite for upload. pixels_argb must remain valid through Submit. */
inline void
SpriteAtlasBuilder::add_sprite(
    int element_id,
    int atlas_index,
    const uint32_t* pixels_argb,
    int width,
    int height)
{
    entries_.push_back(SpriteAtlasEntry{ element_id, atlas_index, pixels_argb, width, height });
}

inline const std::vector<SpriteAtlasEntry>&
SpriteAtlasBuilder::entries() const
{
    return entries_;
}

inline bool
SpriteAtlasBuilder::empty() const
{
    return entries_.empty();
}
