#ifndef MODEL_H
#define MODEL_H

enum CacheModelFlags
{
    CMODEL_FLAG_SHARED = 1 << 0,
    CMODEL_FLAG_MERGED = 1 << 1,
    CMODEL_FLAG_TRANSFORMED = 1 << 2
};

enum LightingFlags
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
enum FaceRenderKind
{
    FACE_GOURAUD = 0,
    FACE_FLAT = 1,
    FACE_TEXTURED = 2,
    FACE_TEXTURE_FLAT_SHADED = 3
};

struct CacheModel
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
    // This is
    int* vertex_bone_map;

    int face_count;
    int* face_indices_a;
    int* face_indices_b;
    int* face_indices_c;
    int* face_alphas;
    int* face_infos;
    int* face_priorities;
    int* face_colors;
    int model_priority;
    int textured_face_count;
    // Used in type 2 >
    int* textured_p_coordinate;
    int* textured_m_coordinate;
    int* textured_n_coordinate;
    int* face_textures;
    int* face_texture_coords;
    // Texture render types for type 3 models
    unsigned char* textureRenderTypes;

    int rotated;
};

struct CacheModelBones
{
    int bones_count;
    // Array of arrays vertices... AKA arrays of bones.
    int** bones;
    int* bones_sizes;
};

struct CacheModelBones*
modelbones_new_decode(int* packed_bone_groups, int packed_bone_groups_count);

struct Cache;
struct CacheModel* model_new_from_cache(struct Cache* cache, int model_id);
struct CacheModel* model_new_decode(const unsigned char* inputData, int inputLength);
struct CacheModel* model_new_copy(struct CacheModel* model);
struct CacheModel* model_new_merge(struct CacheModel** models, int model_count);

void modelbones_free(struct CacheModelBones* modelbones);
void model_free(struct CacheModel* model);

void write_model_separate(const struct CacheModel* model, const char* filename);

#endif
