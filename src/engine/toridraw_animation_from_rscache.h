#ifndef ENGINE_TORIDRAW_ANIMATION_FROM_RSCACHE_H
#define ENGINE_TORIDRAW_ANIMATION_FROM_RSCACHE_H

struct ToriDraw_Animation;
struct RSCache_Dat2Framemap;
struct RSCache_Dat2Frame;

/*
 * Assemble a render-ready classic ToriDraw_Animation from a decoded framemap
 * (the rigging) and an ordered array of decoded frames (one per sequence frame),
 * with per-frame delays and the sequence loop-back offset (frame_step) from the
 * sequence config. Deep-copies into freshly owned ToriDraw arrays; the RSCache
 * inputs may be freed afterwards. Returns NULL on bad input.
 */
struct ToriDraw_Animation*
ToriDraw_AnimationFromRSCache(
    struct RSCache_Dat2Framemap const* framemap,
    struct RSCache_Dat2Frame const* const* frames,
    int const* delays,
    int frame_count,
    int frame_step);

#endif
