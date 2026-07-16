#ifndef RSCACHE_DATATYPES_MODEL_H
#define RSCACHE_DATATYPES_MODEL_H

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

    int rotated;

    /* Animaya (skeletal) per-vertex skin data.  Set only when the model was
     * decoded with hasAnimayaGroups == 1.  animaya_group_counts[i] is the
     * number of bone influences on vertex i; animaya_groups[i][j] and
     * animaya_scales[i][j] give bone index and weight for influence j. */
    int animaya_vertex_count;
    uint8_t* animaya_group_counts;
    uint8_t** animaya_groups;
    uint8_t** animaya_scales;
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

struct RSCache_Disk;
struct RSCache_DiskArchive;
struct RSCache_Model*
RSCache_ModelNewFromCache(
    struct RSCache_Disk* cache,
    int model_id);

struct RSCache_Model*
RSCache_ModelNewFromArchive(
    struct RSCache_DiskArchive* archive,
    int model_id);
struct RSCache_Model*
RSCache_ModelNewFromDatArchive(
    struct RSCache_DiskArchive* archive,
    int model_id);
struct RSCache_Model*
RSCache_ModelNewDecode(
    uint8_t* data,
    int data_size);
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