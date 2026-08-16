#ifndef RSCACHE_DATATYPES_MODEL_H
#define RSCACHE_DATATYPES_MODEL_H

#include "../rscache_profile.h"

#include <stdint.h>

/**
 * face_infos seems to be doing a lot of things.
 * rs-map-viewer calls this faceRenderTypes
 *
 * In 2004Scape, face_infos is used in the following ways:
 * 1. During Rendering:
 *   - 0x00 or NULL: GOURAUD
 *   - 0x01: FLAT
 *   - 0x02: TEXTURED (PNM implied - the PNM face is stored in the top bits (skipping the first
 * two))
 *   - 0x03: TEXTURED_FLAT_SHADED (PNM implied)
 *
 * 2. During Lighting:
 *   - NULL: Vertex Lighting (lightness blend) "Blended"
 *   - 0x00, 0x02: Vertex Lighting with (127 - lightness) "Clamped"
 *
 * In RSMapViewer, faceRenderTypes is used
 * 1. Calculating normals
 *    - 0x00 or NULL: Calculate vertex normals
 *    - 0x01: Calculate face normals
 * 2. Lighting with texture, no alpha, or alpha >= 0
 *    - 0x00 or NULL: Vertex Lighting (lightness blend) "Blended"
 *    - 0x01: Face Lighting "Blended"
 *    - 0x02: No Lighting hidden face.
 *    - 0x03: Pretty sure this is draw texture no alpha    model.faceColors1[i] = 128;
 * model.faceColors3[i] = -1; HSL=128 is black.
 *
 * 3. Lighting with texture, alpha < 0
 *    - Does not matter.
 *      alpha == -1: No Lighting hidden face.
 *      alpha == -2: Unsure. Flat lighting?
 * 4. Lighting without texture, no alpha, or alpha >= 0
 *    - 0x00 or NULL: Vertex Lighting  "Clamped" (127 - lightness)
 *    - 0x01: Face Lighting "Clamped" (127 - lightness)
 *    - 0x02 or 0x03: No Lighting hidden face.
 * 5. Lighting without texture, alpha < 0
 *    - Does not matter.
 *      alpha == -1: No Lighting hidden face.
 *      alpha == -2: No Lighting hidden face.
 *
 *
 * PNM Mapping
 *
 *  faceCoords && != -1: PNM = PNM Face
 *  else: PNM = Model Face
 *
 * Face Drawing
 *
 * If faceColorC == -2 (Hidden Face)
 * If faceColorC == -1 (Flat Color Blend) (only use faceColorsA)
 * else Color Blend (faceColorsA, faceColorsB, faceColorsC)
 *
 * Face Draw Type:
 *   faceTextures && != -1: Textured
 *   faceColorC == -1: Flat triangle
 *   Gouraud
 *
 */
enum RSCache_FaceRenderKind
{
    FACE_GOURAUD = 0,
    FACE_FLAT = 1,
    FACE_TEXTURED = 2,
    FACE_TEXTURE_FLAT_SHADED = 3
};

struct RSCache_Model
{
    int vertex_count;
    int* vertices_x;
    int* vertices_y;
    int* vertices_z;
    // Each vertex can belong to 32 bone groups.
    //
    uint8_t* vertex_bone_map;

    // These are sometimes called "packed transparency vertex groups"
    // because the animation system uses them to apply alpha animations.
    // packed transparency vertex
    uint8_t* face_bone_map;

    int face_count;
    int* face_indices_a;
    int* face_indices_b;
    int* face_indices_c;
    uint8_t* face_alphas;
    // The bottom 2 bits are the face render kind.
    // The top bits are the face texture id.
    uint8_t* face_infos;
    uint8_t* face_priorities;
    uint16_t* face_colors;
    // If model priority is set, this is important for merged_models,
    // such as characters. For example, "arms" will have a model priority of 10,
    // but do not have face_priorities. When a model with model_priority is merged,
    // all of its faces will have the model_priority.
    uint8_t model_priority;
    int textured_face_count;
    // Used in type 2 >
    uint16_t* textured_p_coordinate;
    uint16_t* textured_m_coordinate;
    uint16_t* textured_n_coordinate;
    int16_t* face_textures;
    int16_t* face_texture_coords;
    // Texture render types for type 3 models; Can apply special
    // effects to a texture.
    // In older revisions, the texture holds these fields inherently
    // and ALL models with that texture will have the same params.
    // 0 = “this texture triangle is a fixed PMN projector.”
    // 1 / 3 = “this one has animated/scaled mapping params.”
    // 2 = “same as complex, plus cube UV translation.”
    //     Types 1–3 — Complex
    // Counted together (type >= 1 && type <= 3). They get extra mapping data beyond simple PMN:
    // Field	Role
    // (complex mapping block, 6 bytes): Alternate / extended mapping payload
    // textureScaleX/Y/Z: Scale along each axis
    // textureRotation: Rotation
    // textureDirection: Scroll / flow direction
    // textureSpeed: Animation speed
    uint8_t* texture_render_types;

    /*
     * Complex texture mapping parameters — render types 1 (cylinder), 2 (cube)
     * and 3 (sphere).
     *
     * All eight arrays are `textured_face_count` long and indexed by the
     * **textured face index**, the same index as `texture_render_types` and
     * `textured_p/m/n_coordinate`, so a face reaches them through
     * `face_texture_coords[face]`. They are allocated together and only when the
     * model has at least one complex face; a type-0 entry reads 0 throughout.
     *
     * The stream stores them in six independent sections, one cursor each, and a
     * face consumes from a section only if its render type calls for it — which
     * is why they cannot be read in the same pass as the type-0 p/m/n triples.
     * Section widths are `simple*6`, `complex*6`, `complex*scale_bytes`,
     * `complex`, `complex`, `complex + cube*2`, and `scale_bytes` is 6 below
     * format version 14, 7 at 14, and 9 at 15+.
     *
     * For a complex face `textured_p/m/n_coordinate` are **not vertex indices**.
     * They are a raw axis triple consumed as `m/32767` and `sqrt(p*p + n*n)`
     * when building the mapping's rotation matrix. Reading them as indices is
     * meaningless and will usually be out of range.
     *
     * Units, as the reference consumes them: scales are /1024 (cylinder and
     * sphere) or 64/scale (cube), rotation is in 1/128 turns, speed and the cube
     * translations are /256. See the uv generator for the exact rules.
     */
    int32_t* texture_scale_x;
    int32_t* texture_scale_y;
    int32_t* texture_scale_z;
    int8_t* texture_rotation;
    int8_t* texture_direction;
    int8_t* texture_speed;
    /** Type 2 (cube) only; 0 for every other render type. */
    int8_t* texture_trans_u;
    int8_t* texture_trans_v;

    int rotated;

    /*
     * The ob3 header's optional version byte (header-flags bit 3), or 1 when absent. NOT
     * cosmetic: rs-map-viewer's decodeV1 ends with `if (version >= 13) scaleDown(2)` —
     * version-13+ models (the 643 era) store their vertices at 4x precision and the
     * *reference* decode shifts them down. This decoder deliberately does not: the shift
     * drops two bits, and byte-exact round-trip is this library's validation bar. The
     * consumer that wants reference geometry applies the shift itself — the engine's
     * ToriRS adaptor does — and this field is how it knows to. Skipping it renders every
     * version-13+ model 4x too large, which blanketed whole 643 squares in giant gravel.
     */
    int format_version;

    /* Animaya (skeletal) per-vertex skin data.  Set only when the model was
     * decoded with hasAnimayaGroups == 1.  animaya_group_counts[i] is the
     * number of bone influences on vertex i; animaya_groups[i][j] and
     * animaya_scales[i][j] give bone index and weight for influence j. */
    int animaya_vertex_count;
    uint8_t* animaya_group_counts;
    uint8_t** animaya_groups;
    uint8_t** animaya_scales;

    /**
     * OSRS 232+ face Z offsets (V2/V3 only). Length is face_count when present;
     * NULL when the stream's hasOffsets gate was zero or the format lacks the
     * section (OB2/OB3).
     */
    int8_t* face_z_offsets;
};

/**
 * Which of the four stream layouts a model uses.
 *
 * The format is *not* a revision property — it is stamped on the file itself, in
 * the last two bytes, and a single cache holds a mix (osrs230 is ~89% V2 and ~11%
 * V3). So the decoder sniffs rather than consulting the profile, and
 * RSCache_ModelCodecVersion exists only to answer "what would this cache write?".
 */
enum RSCache_ModelFormat
{
    /** No magic. The 2004 layout: 18-byte trailer, texture ids packed into the
     *  per-face info byte. */
    RSCACHE_MODEL_FORMAT_OB2 = 0,
    /** Magic 0xFF 0xFF. Adds texture render types, a separate face-texture
     *  section, and the complex/cube texture mapping blocks. */
    RSCACHE_MODEL_FORMAT_OB3,
    /** Magic 0xFF 0xFE. OB2's section order plus skeletal (animaya) skin data. */
    RSCACHE_MODEL_FORMAT_V2,
    /** Magic 0xFF 0xFD. OB3's section order plus animaya. */
    RSCACHE_MODEL_FORMAT_V3
};

/** Most header flag bytes any format carries (V3). */
#define RSCACHE_MODEL_HEADER_FLAG_MAX 8

/**
 * What the model decode consumed but did not keep.
 *
 * Opt-in: the plain decode entry points never allocate this, so the client pays
 * nothing for it. Only a caller that intends to re-encode asks for it.
 *
 * Three things in the stream cannot be recovered from a decoded model:
 *
 *  1. **The per-face index type.** Each face picks one of four ways to reuse the
 *     previous face's indices. The decoder resolves it to absolute indices and
 *     drops the choice. More than one type can express the same triangle, so a
 *     re-encode without this is correct but not byte-identical.
 *  2. **The OB2/V2 face info byte.** It packs the shading mode, a "textured" bit
 *     and — in its top six bits — the texture coordinate index. When a face's
 *     coordinates match its texture triangle's p/m/n the decoder rewrites the
 *     index to -1, and if no face survives that it frees the array outright.
 *     Either way the original index is gone.
 *  3. **The trailing bytes.** Every format except OB2 ends with sections this
 *     library does not interpret: a gated per-face block, and OB3/V3's
 *     complex/cube texture mapping payloads. See the note on `tail`.
 *
 * Everything else the encoder recomputes, which measurement showed is safe:
 * across ~11,000 models in six caches the per-vertex flag bits are exactly
 * "this axis has a non-zero delta" (never set over a zero delta, never carrying
 * a bit above 0x4), and every section byte count in the trailer matches a
 * minimal shortsmart re-encode. So the deltas, their flag bits and all four
 * byte counts are derived rather than stored.
 */
struct RSCache_ModelProvenance
{
    /** enum RSCache_ModelFormat — which layout the source used. */
    int format;

    /** The header's flag bytes verbatim, in stream order, excluding the vertex
     *  and face counts and the byte counts (which are recomputed). Kept rather
     *  than inferred from which arrays are non-NULL because the decoder frees
     *  some of them on a "nothing here was used" check, which is not reversible:
     *  `has_face_info = 1` with zero faces is indistinguishable from 0. */
    uint8_t header_flags[RSCACHE_MODEL_HEADER_FLAG_MAX];
    int header_flag_count;

    /* Mirrors of the counts the arrays below are sized by, so a provenance handed
     * to the encoder alongside the wrong model is rejected instead of read out of
     * bounds. */
    int vertex_count;
    int face_count;
    int textured_face_count;

    /** Per face, the index-reuse type (0-4). `face_count` entries. */
    uint8_t* face_index_types;
    /** Per face, the raw info byte. OB2/V2 only; NULL for OB3/V3, which store
     *  the byte directly in the model's `face_infos`. */
    uint8_t* face_info_bytes;

    /**
     * Bytes between the last section this library decodes and the trailer,
     * preserved verbatim.
     *
     * Two distinct things live here and neither is decoded today:
     *
     *  - OB3/V3 only: the complex and cube texture mapping blocks. The decoder
     *    reads the *simple* (render type 0) p/m/n triples and skips the rest,
     *    sizing them as parallel column blocks of 19 bytes per complex triangle
     *    plus 2 per cube triangle.
     *  - V2/OB3/V3: a gate byte whose non-zero value introduces one further
     *    per-face block, then a second flag byte whose non-zero value introduces
     *    10 more bytes. The per-face block holds small values (0-3, in runs)
     *    and is set on roughly a fifth of V2 models; what it means is not
     *    established, so it is carried rather than interpreted.
     *
     * Empty for OB2, whose body ends exactly at its trailer on every model
     * measured.
     */
    uint8_t* tail;
    int tail_size;
};

struct RSCache_ModelBones
{
    int bones_count;
    // Array of arrays vertices... AKA arrays of bones.
    uint16_t** bones;
    uint16_t* bones_sizes;
};

struct RSCache_ModelBones*
RSCache_ModelBonesNewDecode(
    const uint8_t* packed_bone_groups,
    int packed_bone_groups_count);

struct RSCache_Dat2Disk;
struct RSCache_Dat2DiskArchive;
struct RSCache_Model*
RSCache_ModelNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int model_id);

struct RSCache_Model*
RSCache_ModelNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int model_id);
struct RSCache_Model*
RSCache_ModelNewFromDatArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int model_id);
struct RSCache_Model*
RSCache_ModelNewDecode(
    uint8_t* data,
    int data_size);

/**
 * As RSCache_ModelNewDecode, but also records what the decode discards.
 *
 * `*out_provenance` receives a fresh provenance (free it with
 * RSCache_ModelProvenanceFree) on success. Pass NULL for `out_provenance` to get
 * the plain decode. On failure both the model and the provenance are NULL.
 */
struct RSCache_Model*
RSCache_ModelNewDecodeProvenance(
    uint8_t* data,
    int data_size,
    struct RSCache_ModelProvenance** out_provenance);

void
RSCache_ModelProvenanceFree(struct RSCache_ModelProvenance* provenance);

/**
 * Which format this cache would write.
 *
 * Only meaningful for authoring a new model — a *decode* must sniff the magic,
 * because the format travels with the file and a single cache mixes them.
 */
int
RSCache_ModelCodecVersion(const struct RSCache* cache);

/**
 * Encode a model.
 *
 * With `provenance` from RSCache_ModelNewDecodeProvenance the output is
 * byte-identical to the source. Without it the output is a valid model of the
 * requested format that decodes back to an equal struct, but differs in the
 * three places listed on struct RSCache_ModelProvenance — chiefly that every
 * face is written with index type 1 (a full triple), which is always legal but
 * larger than what a packer would choose.
 *
 * The four byte counts in the trailer are not taken from the provenance; they are
 * recomputed from the sections actually written, so a caller may modify vertex
 * positions, colours, alphas, priorities or bone maps and re-encode. Changing the
 * *counts* (vertex_count, face_count, textured_face_count) invalidates a
 * provenance and is rejected.
 *
 * Returns bytes written, or 0 on failure. Failure is reported rather than
 * approximated: a mismatched provenance, a missing array the header flags say is
 * present, a vertex delta outside shortsmart range, or an index type whose
 * reuse pattern the model's indices do not actually satisfy.
 */
uint32_t
RSCache_ModelEncode(
    const struct RSCache* cache,
    const struct RSCache_Model* model,
    const struct RSCache_ModelProvenance* provenance,
    uint8_t* out,
    uint32_t out_capacity);

/** As RSCache_ModelEncode, for a caller naming the format directly. */
uint32_t
RSCache_ModelEncodeFormat(
    const struct RSCache_Model* model,
    const struct RSCache_ModelProvenance* provenance,
    int format,
    uint8_t* out,
    uint32_t out_capacity);

/** Worst-case output size for RSCache_ModelEncode. */
uint32_t
RSCache_ModelEncodeBound(
    const struct RSCache_Model* model,
    const struct RSCache_ModelProvenance* provenance);
struct RSCache_Model*
RSCache_ModelNewCopy(struct RSCache_Model* model);
struct RSCache_Model*
RSCache_ModelNewMerge(
    struct RSCache_Model** models,
    int model_count);

void
RSCache_ModelBonesFree(struct RSCache_ModelBones* modelbones);
void
RSCache_ModelFree(struct RSCache_Model* model);

#endif