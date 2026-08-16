#ifndef TORIRS_PROCTEX_GENERATOR_H
#define TORIRS_PROCTEX_GENERATOR_H

#include "datatypes/dat2_proctexture.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Procedural texture evaluator — turns a decoded 643 texture *program* into pixels.
 *
 * ## The model
 *
 * A 643 texture is a DAG of operations (see dat2_proctexture.h). Evaluation is a **pull** per
 * output scanline: the texture names a colour operation and an alpha operation, each is asked
 * for line N, and each asks its inputs for the lines it needs. Values are 12.4 fixed point
 * throughout — 4096 is "1.0" — which is why the reference is full of `>> 12` and `* 4096`.
 *
 * An operation is either **monochrome** (one channel) or **colour** (three). Feeding one into
 * the other is legal and the conversion is fixed: a monochrome output read as colour is
 * broadcast to all three channels; a colour output read as monochrome yields its red channel.
 * That asymmetry is the reference's, not a simplification.
 *
 * ## Caching
 *
 * The reference keeps a small LRU of scanlines per operation, sized by the record's
 * `cache_size` (0xFF meaning one slot per line). This port caches **every** line of every
 * operation instead. That is a deliberate trade: a texture is baked once at 128x128, so the
 * peak cost is bounded and small, and it removes the eviction logic — which exists only for
 * memory and can only change *performance*, never output. Neighbourhood operations (blur,
 * emboss, the edge detectors) read lines other than the current one, and with full caching
 * they simply hit.
 *
 * ## Partial coverage is explicit, not silent
 *
 * Not every operation is ported yet. An unported one yields a mid-grey constant and bumps
 * `unsupported_count`, and the caller can refuse the texture rather than publish a wrong
 * image. This matters because the failure mode we are trying to avoid — a texture that looks
 * plausible but is wrong — is worse than a texture that is visibly absent.
 */

struct ProcTexGenerator;

/** Resolve a sprite dependency to ARGB pixels. Returns false when unavailable. */
typedef bool (*ProcTexSpriteFn)(
    void* user,
    int sprite_id,
    const int32_t** out_argb,
    int* out_width,
    int* out_height);

/** Resolve a texture-source dependency (a nested procedural texture) to ARGB pixels. */
typedef bool (*ProcTexTextureFn)(
    void* user,
    int texture_id,
    int size,
    const int32_t** out_argb);

struct ProcTexGenerator*
ProcTexGenerator_New(
    ProcTexSpriteFn sprite_fn,
    ProcTexTextureFn texture_fn,
    void* user);

void
ProcTexGenerator_Free(struct ProcTexGenerator* gen);

/**
 * Render `texture` into `out_argb` (size*size ARGB8888).
 *
 * `brightness` is the gamma the caller wants applied (the reference passes 1.0 for raw and
 * 0.8-ish for the in-world look). Returns false when the texture cannot be evaluated.
 *
 * `out_unsupported` receives the number of operation *instances* that fell back, and
 * `out_transparent` whether any pixel came out non-opaque. Both may be NULL.
 */
bool
ProcTexGenerator_Render(
    struct ProcTexGenerator* gen,
    const struct RSCache_Dat2ProcTexture* texture,
    int size,
    double brightness,
    int32_t* out_argb,
    int* out_unsupported,
    bool* out_transparent);

/** True when every operation in `texture` has a ported evaluator. Lets a caller skip a
 *  texture before spending the render, and lets a harness report coverage. */
bool
ProcTexGenerator_IsFullySupported(
    const struct RSCache_Dat2ProcTexture* texture,
    int* out_first_unsupported_type);

/** True when `op_type` has a ported evaluator. */
bool
ProcTexGenerator_SupportsOp(int op_type);

#endif
