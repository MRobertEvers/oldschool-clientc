#ifndef TORIRS_GPU_CLIPSPACE_H
#define TORIRS_GPU_CLIPSPACE_H

#ifdef __cplusplus
extern "C" {
#endif

/** Positive framebuffer extent for division; avoids divide-by-zero. */
static inline float
torirs_gpu_fb_extent(float w_or_h)
{
    return w_or_h > 0.0f ? w_or_h : 1.0f;
}

/**
 * Map a top-left-origin logical pixel (y downward) to normalized device coordinates:
 * x in [-1, 1], y in [-1, 1] with y increasing upward (matches Metal / GL clip space).
 */
static inline void
torirs_logical_pixel_to_ndc(
    float x,
    float y,
    float fbw,
    float fbh,
    float* out_ndc_x,
    float* out_ndc_y)
{
    const float fw = torirs_gpu_fb_extent(fbw);
    const float fh = torirs_gpu_fb_extent(fbh);
    *out_ndc_x = 2.0f * x / fw - 1.0f;
    *out_ndc_y = 1.0f - 2.0f * y / fh;
}

#ifdef __cplusplus
}
#endif

#endif
