#ifndef RSCACHE_RSCACHEDAT2A_MODEL_H
#define RSCACHE_RSCACHEDAT2A_MODEL_H

#include <stdint.h>

struct RSCacheDat2Disk_Archive;

enum RSCacheDat2A_ModelFlags
{
    CMODEL_FLAG_SHARED = 1 << 0,
    CMODEL_FLAG_MERGED = 1 << 1,
    CMODEL_FLAG_TRANSFORMED = 1 << 2
};

enum RSCacheDat2A_LightingFlags
{
    LF_LIGHTING_VERTEX_BLEND = 1 << 0,
    LF_LIGHTING_VERTEX_FLAT = 1 << 1,
    LF_TEXTURED_LIGHTING_UNKNOWN = 1 << 2,
    LF_LIGHTING_SKIP_FACE = 1 << 3,
};

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
enum RSCacheDat2A_FaceRenderKind
{
    FACE_GOURAUD = 0,
    FACE_FLAT = 1,
    FACE_TEXTURED = 2,
    FACE_TEXTURE_FLAT_SHADED = 3
};

struct RSCacheDat2A_Model
{
    // TODO: Should this be included or carried with.
    int _id;
    int _model_type;
    int _flags;

    int _ids[10];

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
    // Texture render types for type 3 models
    unsigned char* textureRenderTypes;

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

struct RSCacheDat2A_ModelBones
{
    int bones_count;
    // Array of arrays vertices... AKA arrays of bones.
    uint16_t** bones;
    uint16_t* bones_sizes;
};

struct RSCacheDat2A_ModelBones*
RSCacheDat2A_ModelBonesNewDecode(
    const uint8_t* packed_bone_groups,
    int packed_bone_groups_count);

struct RSCacheDat2Disk;
struct RSCacheDat2A_Model*
RSCacheDat2A_ModelNewFromCache(
    struct RSCacheDat2Disk* cache,
    int model_id);
struct RSCacheDat2A_Model*
RSCacheDat2A_ModelNewFromArchive(
    struct RSCacheDat2Disk_Archive* archive,
    int model_id);
struct RSCacheDat1Disk_Archive;
struct RSCacheDat2A_Model*
RSCacheDat2A_ModelNewFromDatArchive(
    struct RSCacheDat1Disk_Archive* archive,
    int model_id);
struct RSCacheDat2A_Model*
RSCacheDat2A_ModelNewDecode(
    const unsigned char* inputData,
    int inputLength);
struct RSCacheDat2A_Model*
RSCacheDat2A_ModelNewCopy(struct RSCacheDat2A_Model* model);
struct RSCacheDat2A_Model*
RSCacheDat2A_ModelNewMerge(
    struct RSCacheDat2A_Model** models,
    int model_count);

void
RSCacheDat2A_ModelBonesFree(struct RSCacheDat2A_ModelBones* modelbones);
void
RSCacheDat2A_ModelFree(struct RSCacheDat2A_Model* model);

#endif
