#ifndef ENGINE_TORIDRAW_ANIMATION_FROM_RSCACHE_H
#define ENGINE_TORIDRAW_ANIMATION_FROM_RSCACHE_H

struct ToriDraw_Animation;
struct ToriDraw_SkeletalAnim;
struct RSCache_Dat2Framemap;
struct RSCache_Dat2Frame;
struct RSCache_Dat1AnimBase;
struct RSCache_Dat1AnimFrame;
struct RSCache_Dat2AnimMaya;
struct RSCache_Dat2SkeletalBase;

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

/*
 * Same assembly from dat1 inputs. A dat1 animation archive already pairs its
 * frames with the base they were built against (dat2 splits those into a
 * separate framemap group), so the caller passes the base plus the frames the
 * sequence selected, in sequence order.
 */
struct ToriDraw_Animation*
ToriDraw_AnimationFromRSCacheDat1(
    struct RSCache_Dat1AnimBase const* base,
    struct RSCache_Dat1AnimFrame const* const* frames,
    int const* delays,
    int frame_count,
    int frame_step);

/*
 * Bake an Animaya (skeletal) sequence into a render-ready ToriDraw_SkeletalAnim:
 * one column-major 4x4 skinning matrix per (frame, bone). The maya curves supply
 * the animated channels, the skeletal base the bind pose everything else falls
 * back to. Owns its matrices; the RSCache inputs may be freed afterwards.
 * Returns NULL on bad input.
 */
struct ToriDraw_SkeletalAnim*
ToriDraw_SkeletalAnimFromRSCache(
    int seq_id,
    struct RSCache_Dat2AnimMaya const* maya,
    struct RSCache_Dat2SkeletalBase* base);

#endif
