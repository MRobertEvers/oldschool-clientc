#ifndef TORIRS_PLATFORM_KIT_TRSPK_TYPES_H
#define TORIRS_PLATFORM_KIT_TRSPK_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TRSPK_MAX_MODELS 32768u
#define TRSPK_MAX_TEXTURES 256u
#define TRSPK_MAX_BATCHES 10u
#define TRSPK_MAX_POSES_PER_MODEL 8u
#define TRSPK_MAX_WEBGL1_CHUNKS 64u
#define TRSPK_TEXTURE_DIMENSION 128u
#define TRSPK_ATLAS_DIMENSION 2048u
#define TRSPK_ATLAS_BYTES_PER_PIXEL 4u

#define TRSPK_GPU_ANIM_NONE_IDX 0u
#define TRSPK_GPU_ANIM_PRIMARY_IDX 1u
#define TRSPK_GPU_ANIM_SECONDARY_IDX 2u

#define TRSPK_SCENE_BATCH_NONE 0xFFu
#define TRSPK_INVALID_TEXTURE_ID 0xFFFFu
/** Same values as DASHHSL16_FLAT / DASHHSL16_HIDDEN in graphics/dash_hsl16.h */
#define TRSPK_HSL16_FLAT 0xFF7Fu
#define TRSPK_HSL16_HIDDEN 0xFFFFu

typedef uintptr_t TRSPK_GPUHandle;
typedef uint32_t TRSPK_DynamicSlotHandle;
typedef uint16_t TRSPK_ModelId;
typedef uint16_t TRSPK_TextureId;
typedef uint8_t TRSPK_BatchId;

#define TRSPK_DYNAMIC_SLOT_INVALID 0xFFFFFFFFu

typedef enum TRSPK_UsageClass
{
    TRSPK_USAGE_SCENERY = 0,
    TRSPK_USAGE_NPC = 1,
    TRSPK_USAGE_PLAYER = 2,
    TRSPK_USAGE_PROJECTILE = 3
} TRSPK_UsageClass;

typedef struct TRSPK_Rect
{
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} TRSPK_Rect;

typedef struct TRSPK_Vec3
{
    float x;
    float y;
    float z;
} TRSPK_Vec3;

typedef struct TRSPK_Mat4
{
    float m[16];
} TRSPK_Mat4;

typedef struct TRSPK_Vertex
{
    float position[4];
    float color[4];
    float texcoord[2];
    float tex_id;
    float uv_mode;
} TRSPK_Vertex;

typedef enum TRSPK_VertexBufferStatus
{
    TRSPK_VERTEX_BUFFER_STATUS_EMPTY = 0,
    TRSPK_VERTEX_BUFFER_STATUS_READY = 1,
    /** Vertex/index pointers reference batch storage; do not heap-free. */
    TRSPK_VERTEX_BUFFER_STATUS_BATCH_VIEW = 2,
} TRSPK_VertexBufferStatus;

typedef enum TRSPK_VertexFormat
{
    TRSPK_VERTEX_FORMAT_TRSPK = 0,
    TRSPK_VERTEX_FORMAT_WEBGL1,
    TRSPK_VERTEX_FORMAT_METAL,
    /** AoSoA: aligned blocks (see TRSPK_VertexWebGL1Soaos in webgl1_vertex.h). */
    TRSPK_VERTEX_FORMAT_WEBGL1_SOAOS,
    /** AoSoA: aligned blocks (see TRSPK_VertexMetalSoaos in metal_vertex.h). */
    TRSPK_VERTEX_FORMAT_METAL_SOAOS,
    /** Empty / no GPU vertex payload (see TRSPK_VertexBuffer). */
    TRSPK_VERTEX_FORMAT_NONE
} TRSPK_VertexFormat;

typedef enum TRSPK_IndexFormat
{
    TRSPK_INDEX_FORMAT_U16 = 0,
    TRSPK_INDEX_FORMAT_U32 = 1
} TRSPK_IndexFormat;

typedef struct TRSPK_BakeTransform
{
    float cos_yaw;
    float sin_yaw;
    float tx;
    float ty;
    float tz;
    bool identity;
} TRSPK_BakeTransform;

/** Selects how PNM inputs map to per-corner UVs (CPU only; not sent to the GPU). */
typedef enum TRSPK_UVCalculationMode
{
    TRSPK_UV_CALC_TEXTURED_FACE_ARRAY = 0,
    TRSPK_UV_CALC_FIRST_FACE = 1
} TRSPK_UVCalculationMode;

typedef struct TRSPK_UVFaceCoords
{
    float u1;
    float v1;
    float u2;
    float v2;
    float u3;
    float v3;
} TRSPK_UVFaceCoords;

typedef struct TRSPK_AtlasUVRect
{
    float u0;
    float v0;
    float du;
    float dv;
} TRSPK_AtlasUVRect;

typedef struct TRSPK_AtlasTile
{
    float u0;
    float v0;
    float du;
    float dv;
    float anim_u;
    float anim_v;
    float opaque;
    float _pad;
} TRSPK_AtlasTile;

typedef struct TRSPK_ModelPose
{
    TRSPK_GPUHandle vbo;
    TRSPK_GPUHandle ebo;
    TRSPK_GPUHandle world_vao;
    uint32_t vbo_offset;
    uint32_t ebo_offset;
    uint32_t element_count;
    TRSPK_BatchId batch_id;
    uint8_t chunk_index;
    uint8_t usage_class;
    bool dynamic;
    bool valid;
} TRSPK_ModelPose;

typedef struct TRSPK_BatchResource
{
    TRSPK_GPUHandle vbo;
    TRSPK_GPUHandle ebo;
    TRSPK_GPUHandle world_vao;
    uint32_t chunk_count;
    TRSPK_GPUHandle chunk_vbos[TRSPK_MAX_WEBGL1_CHUNKS];
    TRSPK_GPUHandle chunk_ebos[TRSPK_MAX_WEBGL1_CHUNKS];
    bool valid;
} TRSPK_BatchResource;

typedef struct TRSPK_Batch16Entry
{
    TRSPK_ModelId model_id;
    uint8_t gpu_segment_slot;
    uint16_t frame_index;
    uint8_t chunk_index;
    uint32_t vbo_offset;
    uint32_t ebo_offset;
    uint32_t element_count;
} TRSPK_Batch16Entry;

typedef struct TRSPK_Batch32Entry
{
    TRSPK_ModelId model_id;
    uint8_t gpu_segment_slot;
    uint16_t frame_index;
    uint32_t vbo_offset;
    uint32_t ebo_offset;
    uint32_t element_count;
} TRSPK_Batch32Entry;

typedef struct TRSPK_TextureUpload
{
    TRSPK_TextureId texture_id;
    const uint8_t* rgba_pixels;
    uint32_t width;
    uint32_t height;
    float anim_u;
    float anim_v;
    bool opaque;
} TRSPK_TextureUpload;

typedef struct TRSPK_ModelArrays
{
    uint32_t vertex_count;
    const int16_t* vertices_x;
    const int16_t* vertices_y;
    const int16_t* vertices_z;
    uint32_t face_count;
    const uint16_t* faces_a;
    const uint16_t* faces_b;
    const uint16_t* faces_c;
    const uint16_t* faces_a_color_hsl16;
    const uint16_t* faces_b_color_hsl16;
    const uint16_t* faces_c_color_hsl16;
    const uint8_t* face_alphas;
    const int32_t* face_infos;
    const int16_t* faces_textures;
    const uint16_t* textured_faces;
    const uint16_t* textured_faces_a;
    const uint16_t* textured_faces_b;
    const uint16_t* textured_faces_c;
} TRSPK_ModelArrays;

#ifdef __cplusplus
}
#endif

#endif
