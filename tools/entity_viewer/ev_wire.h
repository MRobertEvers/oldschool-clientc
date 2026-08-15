#ifndef EV_WIRE_H
#define EV_WIRE_H

/*
 * The byte format the entity viewer's two halves speak.
 *
 * The server links rscache and decodes the cache; the browser module links only
 * toridraw and renders. Between them travels an already-built model and an
 * already-built animation, not cache archives — which is why this format exists
 * rather than shipping the cache to the browser: cache.osrs239 is 216 MB, and a
 * viewer only ever needs the handful of records the user is looking at.
 *
 * Both halves compile this same file, so there is one definition of the format
 * and no way for a writer and a reader to drift apart.
 *
 * Everything is little-endian and every scalar is written as int32, including
 * fields toridraw stores in 16 or 8 bits. That costs a few hundred kilobytes
 * across a session and buys immunity to the typedef widths in
 * toridraw_types.h changing under either half independently.
 */

#include <stddef.h>
#include <stdint.h>

struct ToriDraw_Model;
struct ToriDraw_ModelHD;
struct ToriDraw_Animation;

#define EV_WIRE_MODEL_MAGIC 0x314D5645u /* "EVM1" */
/*
 * "EVH1" — an HD model: this magic, then a complete EVM1 blob, then the HD
 * tail. A distinct magic rather than a version field, so an older reader
 * rejects the blob outright instead of parsing the base and silently dropping
 * the mappings, which would render every mapped face through the wrong kernel
 * and look almost right.
 */
#define EV_WIRE_MODEL_HD_MAGIC 0x31485645u

/*
 * Words per texture mapping on the wire: centre (3) + matrix (9) + direction
 * (1) + speed, u/v offset, scale_z and the three axis scales (7).
 *
 * Named rather than inlined because the writer and the reader both need it and
 * a disagreement between them is silent — the first version of this format had
 * the reader indexing back from the end with a hand-counted 19, and every
 * mapping vanished with no error anywhere.
 */
#define EV_WIRE_HD_MAPPING_WORDS 20
#define EV_WIRE_ANIM_MAGIC  0x31415645u /* "EVA1" */

/* ---- writing (server side) ---------------------------------------------- */

/**
 * A growable byte sink. `data` is heap memory owned by the buffer; hand it off
 * with ev_wire_detach or release with ev_wire_free.
 */
struct EV_WireBuf
{
    uint8_t* data;
    size_t len;
    size_t cap;
};

void
ev_wire_free(struct EV_WireBuf* buf);

/** Serialise a built model. Returns 0 on allocation failure. */
int
ev_wire_write_model(
    struct EV_WireBuf* out,
    const struct ToriDraw_Model* model);

/**
 * Serialise an HD model: the base, plus the per-face-group texture mappings the
 * texcylinder / texcube / texsphere kernels need.
 *
 * The mappings carry floats, which every other field here avoids; they are
 * written as their IEEE bit patterns through int32, so the format stays
 * uniformly 32-bit and the value survives exactly.
 */
int
ev_wire_write_model_hd(
    struct EV_WireBuf* out,
    const struct ToriDraw_ModelHD* model);

/** Serialise a built animation (its rig and every frame). */
int
ev_wire_write_anim(
    struct EV_WireBuf* out,
    const struct ToriDraw_Animation* anim);

/* ---- reading (browser side) --------------------------------------------- */

/**
 * Rebuild a model from bytes. Returns NULL when the blob is truncated or its
 * magic is wrong — a short read here would otherwise surface as geometry that
 * renders but is subtly wrong, which is the hardest kind of bug to see.
 *
 * The result owns its arrays; free with ToriDraw_ModelFree.
 */
struct ToriDraw_Model*
ev_wire_read_model(
    const uint8_t* data,
    size_t len);

/**
 * Rebuild an HD model. Returns NULL unless the blob starts with
 * EV_WIRE_MODEL_HD_MAGIC. Free with ToriDraw_ModelHDFree.
 */
struct ToriDraw_ModelHD*
ev_wire_read_model_hd(
    const uint8_t* data,
    size_t len);

/** True when `data` is an HD blob. Cheap enough to call before deciding which
 *  reader to use. */
int
ev_wire_is_model_hd(
    const uint8_t* data,
    size_t len);

/** Rebuild an animation. Free with ev_wire_free_anim. */
struct ToriDraw_Animation*
ev_wire_read_anim(
    const uint8_t* data,
    size_t len);

/* An animation read back here is a plain ToriDraw_Animation, laid out exactly
 * as the library builds one, so ToriDraw_AnimationFree releases it. This file
 * deliberately owns no free of its own. */

#endif
