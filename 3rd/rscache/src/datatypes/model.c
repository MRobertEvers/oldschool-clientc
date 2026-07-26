#include "rscache.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// face_texture_coords/face_textures must be NULL, or have at least one
// non-(-1) entry, to avoid false-positives in the has_textures check.
static void
model_assert_texture_invariant(const struct RSCache_Model* model)
{
    if( model->face_texture_coords != NULL )
    {
        bool found = false;
        for( int i = 0; i < model->face_count; i++ )
        {
            if( model->face_texture_coords[i] != -1 )
            {
                found = true;
                break;
            }
        }
        assert(found);
    }

    if( model->face_textures != NULL )
    {
        bool found = false;
        for( int i = 0; i < model->face_count; i++ )
        {
            if( model->face_textures[i] != -1 )
            {
                found = true;
                break;
            }
        }
        assert(found);
    }
}

// computeAnimationTables
//
struct RSCache_ModelBones*
RSCache_ModelBonesNewDecode(
    const uint8_t* vertex_bone_map,
    int vertex_bone_map_count)
{
    struct RSCache_ModelBones* bones =
        (struct RSCache_ModelBones*)malloc(sizeof(struct RSCache_ModelBones));
    if( !bones )
        return NULL;

    // Initialize group counts array
    int bone_counts[256] = { 0 };
    int num_bones = 0;

    // Count occurrences of each group and find max group number
    for( int i = 0; i < vertex_bone_map_count; i++ )
    {
        int bone = vertex_bone_map[i];
        if( bone >= 0 )
        {
            bone_counts[bone]++;
            if( bone > num_bones )
                num_bones = bone;
        }
    }

    // Allocate arrays
    bones->bones_count = num_bones + 1;
    bones->bones = (uint16_t**)malloc((num_bones + 1) * sizeof(uint16_t*));
    bones->bones_sizes = (uint16_t*)malloc((num_bones + 1) * sizeof(uint16_t));

    if( !bones->bones || !bones->bones_sizes )
    {
        free(bones->bones);
        free(bones->bones_sizes);
        free(bones);
        return NULL;
    }

    // Allocate each group array
    for( int i = 0; i <= num_bones; i++ )
    {
        bones->bones[i] = (uint16_t*)malloc((size_t)bone_counts[i] * sizeof(uint16_t));
        bones->bones_sizes[i] = 0;
    }

    // Fill the groups
    for( int i = 0; i < vertex_bone_map_count; i++ )
    {
        int bone = vertex_bone_map[i];
        if( bone >= 0 )
            bones->bones[bone][bones->bones_sizes[bone]++] = (uint16_t)i;
    }

    return bones;
}

/* ------------------------------------------------- provenance capture --- */
/*
 * Small helpers the four decoders use to record what they are about to throw
 * away. Every one is a no-op when `prov` is NULL, which is the client's path —
 * so the cost of being able to re-encode is paid only by callers that ask.
 *
 * A capture failure (out of memory) is deliberately silent: it leaves the field
 * NULL, and the encoder rejects a provenance whose arrays do not match its
 * counts. Losing the ability to re-encode is not a reason to fail a decode.
 */

static void
prov_header(
    struct RSCache_ModelProvenance* prov,
    int format,
    int vertex_count,
    int face_count,
    int textured_face_count,
    const int* flags,
    int flag_count)
{
    if( !prov )
        return;

    prov->format = format;
    prov->vertex_count = vertex_count;
    prov->face_count = face_count;
    prov->textured_face_count = textured_face_count;

    if( flag_count > RSCACHE_MODEL_HEADER_FLAG_MAX )
        flag_count = RSCACHE_MODEL_HEADER_FLAG_MAX;
    prov->header_flag_count = flag_count;
    for( int i = 0; i < flag_count; i++ )
        prov->header_flags[i] = (uint8_t)flags[i];
}

/** Drop anything captured so far, for the fall-through to another format. */
static void
prov_reset(struct RSCache_ModelProvenance* prov)
{
    if( !prov )
        return;

    free(prov->face_index_types);
    free(prov->face_info_bytes);
    free(prov->tail);
    memset(prov, 0, sizeof(*prov));
}

/** Copy `count` bytes out of the source stream into a fresh array. */
static void
prov_bytes(
    uint8_t** out,
    const uint8_t* data,
    int from,
    int count)
{
    if( !out || count <= 0 )
        return;

    uint8_t* copy = (uint8_t*)malloc((size_t)count);
    if( !copy )
        return;
    memcpy(copy, data + from, (size_t)count);
    *out = copy;
}

/**
 * Capture `[from, to)` as the opaque tail.
 *
 * `from` is where the last section this library decodes ends and `to` is where the
 * trailer begins. On every format measured the two coincide for OB2 and differ for
 * the rest; capturing the range unconditionally means a model carrying sections
 * nobody has identified still round-trips.
 */
static void
prov_tail(
    struct RSCache_ModelProvenance* prov,
    const uint8_t* data,
    int from,
    int to)
{
    if( !prov || to <= from )
        return;

    prov_bytes(&prov->tail, data, from, to - from);
    if( prov->tail )
        prov->tail_size = to - from;
}

static struct RSCache_Model*
decode_ob2(
    const uint8_t* inputData,
    int inputLength,
    struct RSCache_ModelProvenance* prov)
{
    if( !inputData || inputLength < 18 )
        return NULL;

    struct RSCache_Model* model = (struct RSCache_Model*)malloc(sizeof(struct RSCache_Model));
    memset(model, 0, sizeof(struct RSCache_Model));
    if( !model )
        return NULL;

    // Initialize all pointers to NULL
    model->vertices_x = NULL;
    model->vertices_y = NULL;
    model->vertices_z = NULL;
    model->face_indices_a = NULL;
    model->face_indices_b = NULL;
    model->face_indices_c = NULL;
    model->face_alphas = NULL;
    model->face_infos = NULL;
    model->face_priorities = NULL;
    model->face_colors = NULL;
    model->textured_p_coordinate = NULL;
    model->textured_m_coordinate = NULL;
    model->textured_n_coordinate = NULL;

    // Read header information from the end of the file
    bool has_textures = false;
    bool has_faceinfos = false;
    int offset = inputLength - 18;
    int vertex_count = RSCache_BufferG2At(inputData, &offset);
    int face_count = RSCache_BufferG2At(inputData, &offset);
    int textured_face_count = RSCache_BufferG1At(inputData, &offset);
    int has_face_info = RSCache_BufferG1At(inputData, &offset);
    int model_priority = RSCache_BufferG1At(inputData, &offset);
    int has_alpha = RSCache_BufferG1At(inputData, &offset);
    int has_face_labels = RSCache_BufferG1At(inputData, &offset);
    int has_vertex_labels = RSCache_BufferG1At(inputData, &offset);
    int vertexXDataByteCount = RSCache_BufferG2At(inputData, &offset);
    int vertexYDataByteCount = RSCache_BufferG2At(inputData, &offset);
    int vertexZDataByteCount = RSCache_BufferG2At(inputData, &offset);
    int faceIndexDataByteCount = RSCache_BufferG2At(inputData, &offset);

    // Calculate offsets for different data sections
    int offsetOfVertexFlags = 0;
    int dataOffset = offsetOfVertexFlags + vertex_count;
    int offsetOfFaceIndices = dataOffset;
    dataOffset += face_count;
    int offsetOfFaceRenderPriorities = dataOffset;
    if( model_priority == 255 )
    {
        dataOffset += face_count;
    }

    int offsetOfPackedTransparencyVertexGroups = dataOffset;
    if( has_face_labels == 1 )
    {
        dataOffset += face_count;
    }

    int face_infos_offset = dataOffset;
    if( has_face_info == 1 )
    {
        dataOffset += face_count;
    }

    int offsetOfPackedVertexGroups = dataOffset;
    if( has_vertex_labels == 1 )
    {
        dataOffset += vertex_count;
    }

    int offsetOfFaceTransparencies = dataOffset;
    if( has_alpha == 1 )
    {
        dataOffset += face_count;
    }

    int offsetOfFaceIndexData = dataOffset;
    dataOffset += faceIndexDataByteCount;
    int offsetOfFaceColorsOrFaceTextures = dataOffset;
    dataOffset += face_count * 2;
    int offsetOfTextureIndices = dataOffset;
    dataOffset += textured_face_count * 6;
    int offsetOfVertexXData = dataOffset;
    dataOffset += vertexXDataByteCount;
    int offsetOfVertexYData = dataOffset;
    dataOffset += vertexYDataByteCount;
    int offsetOfVertexZData = dataOffset;
    dataOffset += vertexZDataByteCount;

    {
        const int flags[] = { has_face_info,   model_priority,   has_alpha,
                              has_face_labels, has_vertex_labels };
        prov_header(
            prov, RSCACHE_MODEL_FORMAT_OB2, vertex_count, face_count, textured_face_count, flags,
            5);
        if( prov )
        {
            prov_bytes(&prov->face_index_types, inputData, offsetOfFaceIndices, face_count);
            if( has_face_info == 1 )
                prov_bytes(&prov->face_info_bytes, inputData, face_infos_offset, face_count);
            prov_tail(prov, inputData, dataOffset, inputLength - 18);
        }
    }

    // Set model properties
    model->vertex_count = vertex_count;
    model->face_count = face_count;
    model->textured_face_count = textured_face_count;

    // Allocate memory for vertices
    model->vertices_x = (int*)malloc(vertex_count * sizeof(int));
    model->vertices_y = (int*)malloc(vertex_count * sizeof(int));
    model->vertices_z = (int*)malloc(vertex_count * sizeof(int));
    memset(model->vertices_x, 0, vertex_count * sizeof(int));
    memset(model->vertices_y, 0, vertex_count * sizeof(int));
    memset(model->vertices_z, 0, vertex_count * sizeof(int));

    // Allocate memory for faces
    model->face_indices_a = (int*)malloc(face_count * sizeof(int));
    model->face_indices_b = (int*)malloc(face_count * sizeof(int));
    model->face_indices_c = (int*)malloc(face_count * sizeof(int));
    model->face_colors = (uint16_t*)malloc((size_t)face_count * sizeof(uint16_t));
    model->face_priorities = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
    if( has_alpha == 1 )
    {
        model->face_alphas = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
        memset(model->face_alphas, 0, (size_t)face_count * sizeof(uint8_t));
    }
    memset(model->face_indices_a, 0, face_count * sizeof(int));
    memset(model->face_indices_b, 0, face_count * sizeof(int));
    memset(model->face_indices_c, 0, face_count * sizeof(int));
    memset(model->face_colors, 0, (size_t)face_count * sizeof(uint16_t));
    memset(model->face_priorities, 0, (size_t)face_count * sizeof(uint8_t));

    model->face_infos = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
    model->face_textures = (int16_t*)malloc((size_t)face_count * sizeof(int16_t));
    model->face_texture_coords = (int16_t*)malloc((size_t)face_count * sizeof(int16_t));
    memset(model->face_infos, 0, (size_t)face_count * sizeof(uint8_t));
    memset(model->face_textures, 0, (size_t)face_count * sizeof(int16_t));
    memset(model->face_texture_coords, 0, (size_t)face_count * sizeof(int16_t));

    model->textured_p_coordinate =
        (uint16_t*)malloc((size_t)textured_face_count * sizeof(uint16_t));
    model->textured_m_coordinate =
        (uint16_t*)malloc((size_t)textured_face_count * sizeof(uint16_t));
    model->textured_n_coordinate =
        (uint16_t*)malloc((size_t)textured_face_count * sizeof(uint16_t));
    memset(model->textured_p_coordinate, 0, (size_t)textured_face_count * sizeof(uint16_t));
    memset(model->textured_m_coordinate, 0, (size_t)textured_face_count * sizeof(uint16_t));
    memset(model->textured_n_coordinate, 0, (size_t)textured_face_count * sizeof(uint16_t));

    if( has_vertex_labels == 1 )
    {
        model->vertex_bone_map = (uint8_t*)malloc((size_t)vertex_count * sizeof(uint8_t));
        memset(model->vertex_bone_map, 0, (size_t)vertex_count * sizeof(uint8_t));
    }

    if( has_face_labels == 1 )
    {
        model->face_bone_map = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
        memset(model->face_bone_map, 0, (size_t)face_count * sizeof(uint8_t));
    }

    // Read vertex data
    int previousVertexX = 0;
    int previousVertexY = 0;
    int previousVertexZ = 0;
    offset = offsetOfVertexFlags;

    for( int i = 0; i < vertex_count; i++ )
    {
        int vertexFlags = RSCache_BufferG1At(inputData, &offset);
        int deltaX = 0;
        int deltaY = 0;
        int deltaZ = 0;

        if( vertexFlags & 1 )
        {
            deltaX = RSCache_BufferReadShortSmartAt(inputData, &offsetOfVertexXData);
        }
        if( vertexFlags & 2 )
        {
            deltaY = RSCache_BufferReadShortSmartAt(inputData, &offsetOfVertexYData);
        }
        if( vertexFlags & 4 )
        {
            deltaZ = RSCache_BufferReadShortSmartAt(inputData, &offsetOfVertexZData);
        }

        model->vertices_x[i] = previousVertexX + deltaX;
        model->vertices_y[i] = previousVertexY + deltaY;
        model->vertices_z[i] = previousVertexZ + deltaZ;
        previousVertexX = model->vertices_x[i];
        previousVertexY = model->vertices_y[i];
        previousVertexZ = model->vertices_z[i];

        if( has_vertex_labels == 1 )
        {
            uint8_t vertexLabel = RSCache_BufferG1At(inputData, &offsetOfPackedVertexGroups);
            model->vertex_bone_map[i] = vertexLabel;
        }
    }

    // Read face data
    offset = offsetOfFaceColorsOrFaceTextures;
    int textureFlagsOffset = face_infos_offset;
    int prioritiesOffset = offsetOfFaceRenderPriorities;
    int transparenciesOffset = offsetOfFaceTransparencies;
    int transparencyGroupsOffset = offsetOfPackedTransparencyVertexGroups;

    for( int i = 0; i < face_count; i++ )
    {
        model->face_colors[i] = RSCache_BufferG2At(inputData, &offset);

        if( has_face_info == 1 )
        {
            has_faceinfos = true;
            int faceTextureFlags = RSCache_BufferG1At(inputData, &textureFlagsOffset);
            if( faceTextureFlags & 1 )
            {
                // Flat shading
                model->face_infos[i] = 1;
            }
            else
            {
                // Gouraud shading
                model->face_infos[i] = 0;
            }

            if( faceTextureFlags & 2 )
            {
                // Textured
                int textured_face = faceTextureFlags >> 2;
                model->face_texture_coords[i] = (int16_t)textured_face;
                model->face_textures[i] = (int16_t)model->face_colors[i];
                model->face_colors[i] = 127;
                has_textures = true;
            }
            else
            {
                // Not textured
                model->face_texture_coords[i] = -1;
                model->face_textures[i] = -1;
            }
        }

        if( model_priority == 255 )
        {
            model->face_priorities[i] = RSCache_BufferG1At(inputData, &prioritiesOffset);
        }
        else
        {
            model->face_priorities[i] = (uint8_t)model_priority;
        }

        if( has_alpha == 1 )
        {
            model->face_alphas[i] = RSCache_BufferG1At(inputData, &transparenciesOffset);
        }

        if( has_face_labels == 1 )
        {
            uint8_t faceLabel = RSCache_BufferG1At(
                inputData,
                &transparencyGroupsOffset); // Skip this data as it's not in our model struct
            model->face_bone_map[i] = faceLabel;
        }
    }

    // Read face indices
    offset = offsetOfFaceIndexData;
    int compressionTypesOffset = offsetOfFaceIndices;
    int previousIndex1 = 0;
    int previousIndex2 = 0;
    int previousIndex3 = 0;
    int previousIndex3Copy = 0;

    for( int i = 0; i < face_count; i++ )
    {
        int compressionType = RSCache_BufferG1At(inputData, &compressionTypesOffset);

        if( compressionType == 1 )
        {
            previousIndex1 = RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3;
            previousIndex2 = RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex1;
            previousIndex3 = RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex2;
            previousIndex3Copy = previousIndex3; // Store copy for next iteration
        }
        else if( compressionType == 2 )
        {
            previousIndex2 = previousIndex3;
            previousIndex3 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3; // Store copy for next iteration
        }
        else if( compressionType == 3 )
        {
            previousIndex1 = previousIndex3;
            previousIndex3 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3; // Store copy for next iteration
        }
        else if( compressionType == 4 )
        {
            int temp = previousIndex1;
            previousIndex1 = previousIndex2;
            previousIndex2 = temp;
            previousIndex3 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3; // Store copy for next iteration
        }

        model->face_indices_a[i] = previousIndex1;
        model->face_indices_b[i] = previousIndex2;
        model->face_indices_c[i] = previousIndex3;
    }

    offset = offsetOfTextureIndices;

    for( int i = 0; i < textured_face_count; i++ )
    {
        model->textured_p_coordinate[i] = RSCache_BufferG2At(inputData, &offset);
        model->textured_m_coordinate[i] = RSCache_BufferG2At(inputData, &offset);
        model->textured_n_coordinate[i] = RSCache_BufferG2At(inputData, &offset);
    }

    if( textured_face_count > 0 )
    {
        // let hasValidTexFace = false;

        //     for (let i = 0; i < faceCount; i++) {
        //         const index = this.textureCoords[i] & 255;
        //         if (index !== 255) {
        //             if (
        //                 this.indices1[i] === (this.textureMappingP[index] & 0xffff) &&
        //                 this.indices2[i] === (this.textureMappingM[index] & 0xffff) &&
        //                 this.indices3[i] === (this.textureMappingN[index] & 0xffff)
        //             ) {
        //                 this.textureCoords[i] = -1;
        //             } else {
        //                 hasValidTexFace = true;
        //             }
        //         }
        //     }

        //     if (!hasValidTexFace) {
        //         this.textureCoords = undefined;
        //     }
        int hasValidTexFace = false;
        for( int i = 0; i < face_count; i++ )
        {
            int index = model->face_texture_coords[i] & 255;
            if( index != 255 )
            {
                assert(index >= 0 && index < textured_face_count);
                if( model->face_indices_a[i] == (model->textured_p_coordinate[index] & 0xffff) &&
                    model->face_indices_b[i] == (model->textured_m_coordinate[index] & 0xffff) &&
                    model->face_indices_c[i] == (model->textured_n_coordinate[index] & 0xffff) )
                {
                    model->face_texture_coords[i] = -1;
                }
                else
                {
                    hasValidTexFace = true;
                }
            }
        }

        if( !hasValidTexFace )
        {
            free(model->face_texture_coords);
            model->face_texture_coords = NULL;
        }
    }

    // Set model priority
    model->model_priority = (uint8_t)model_priority;

    if( !has_faceinfos )
    {
        free(model->face_infos);
        model->face_infos = NULL;
    }

    if( !has_textures )
    {
        free(model->face_textures);
        model->face_textures = NULL;
        free(model->face_texture_coords);
        model->face_texture_coords = NULL;
    }

    model_assert_texture_invariant(model);
    return model;
}

static struct RSCache_Model*
decode_ob3(
    const uint8_t* inputData,
    int inputLength,
    struct RSCache_ModelProvenance* prov)

{
    struct RSCache_Model* model = (struct RSCache_Model*)malloc(sizeof(struct RSCache_Model));
    memset(model, 0, sizeof(struct RSCache_Model));

    int headerOffset = inputLength - 23;
    int offset = 0;

    int vertex_count = RSCache_BufferG2At(inputData, &headerOffset);
    int face_count = RSCache_BufferG2At(inputData, &headerOffset);
    int textured_face_count = RSCache_BufferG1At(inputData, &headerOffset);
    /*
     * A **bitmask**, not a boolean, and reading it as one is what broke every 643-era
     * model. Bit 0 is the face-render-types flag; the rest announce sections OSRS caches
     * never use, so `== 1` happened to work there and nowhere else.
     *
     *   0x1  face render types present
     *   0x2  particle effects present   (data lives past everything else; not decoded)
     *   0x4  billboards present         (likewise)
     *   0x8  a version byte precedes the header
     *
     * Per rs-map-viewer's ModelData.decodeV1, which is the only reference here that
     * handles the 643 branch.
     */
    int header_flags = RSCache_BufferG1At(inputData, &headerOffset);
    int has_face_render_types = header_flags & 0x1;

    /* The version byte sits immediately *before* the 23-byte header when bit 3 is set.
     * It selects the width of the per-complex-face texture scale block below. */
    int format_version = 1;
    if( (header_flags & 0x8) != 0 && inputLength >= 24 )
        format_version = inputData[inputLength - 24] & 0xff;
    /* Recorded for the consumer: version >= 13 vertices are stored at 4x and the
     * reference shifts them down after decode — see the field's comment in model.h. */
    model->format_version = format_version;

    int model_priority = RSCache_BufferG1At(inputData, &headerOffset);
    int has_face_transparencies = RSCache_BufferG1At(inputData, &headerOffset);
    int has_packed_transparency_vertex_groups = RSCache_BufferG1At(inputData, &headerOffset);
    int has_face_textures = RSCache_BufferG1At(inputData, &headerOffset);
    int has_packed_vertex_groups = RSCache_BufferG1At(inputData, &headerOffset);
    int vertexXDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);
    int vertexYDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);
    int vertexZDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);
    int faceIndexDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);
    int faceColorDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);

    int simpleTextureFaceCount = 0;
    int complexTextureFaceCount = 0;
    int cubeTextureFaceCount = 0;

    if( textured_face_count > 0 )
    {
        model->textured_face_count = textured_face_count;
        /* Keep the render types. The decoder used to read them into a local and
         * drop them, which left no way to tell a simple texture triangle from a
         * complex one after the fact — and the two are sized differently, so a
         * re-encode could not lay the mapping section out. `version3` already
         * stored them; this is the same field. */
        model->texture_render_types =
            (unsigned char*)malloc((size_t)textured_face_count * sizeof(unsigned char));

        for( int i = 0; i < textured_face_count; ++i )
        {
            unsigned char textureRenderType = RSCache_BufferG1At(inputData, &offset);
            if( model->texture_render_types )
                model->texture_render_types[i] = textureRenderType;
            if( textureRenderType == 0 )
            {
                ++simpleTextureFaceCount;
            }
            if( textureRenderType >= 1 && textureRenderType <= 3 )
            {
                ++complexTextureFaceCount;
            }
            if( textureRenderType == 2 )
            {
                ++cubeTextureFaceCount;
            }
        }
    }

    int dataOffset = textured_face_count + vertex_count;
    int offsetOfFaceRenderTypes = dataOffset;
    if( has_face_render_types == 1 )
    {
        dataOffset += face_count;
    }

    int offsetOfFaceRenderPriorities = dataOffset;
    dataOffset += face_count;
    int offsetOfFaceTransparencies = dataOffset;
    if( model_priority == 255 )
    {
        dataOffset += face_count;
    }

    int offsetOfPackedTransparencyVertexGroups = dataOffset;
    if( has_packed_transparency_vertex_groups == 1 )
    {
        dataOffset += face_count;
    }

    int offsetOfPackedVertexGroups = dataOffset;
    if( has_packed_vertex_groups == 1 )
    {
        dataOffset += vertex_count;
    }

    int offsetOfFaceAlphas = dataOffset;
    if( has_face_transparencies == 1 )
    {
        dataOffset += face_count;
    }

    int offsetOfFaceIndices = dataOffset;
    dataOffset += faceIndexDataByteCount;
    int offsetOfFaceColors = dataOffset;
    if( has_face_textures == 1 )
    {
        dataOffset += face_count * 2;
    }

    int offsetOfTextureCoords = dataOffset;
    dataOffset += faceColorDataByteCount;
    int offsetOfFaceIndexTypes = dataOffset;
    dataOffset += face_count * 2;
    int offsetOfVertexXData = dataOffset;
    dataOffset += vertexXDataByteCount;
    int offsetOfVertexYData = dataOffset;
    dataOffset += vertexYDataByteCount;
    int offsetOfVertexZData = dataOffset;
    dataOffset += vertexZDataByteCount;
    /*
     * Texture mapping. Per complex face: a 6-byte p/m/n triple, a scale block whose
     * width the format version selects, then one byte each of rotation, direction and
     * translation. Cube faces add two bytes to the translation block.
     *
     * This previously charged **19 bytes per complex face** rather than 15 (at the
     * default version), which overshot the file on 1795 of 3000 sampled 643 models.
     * Invisible on every OSRS cache in the corpus because they contain no complex
     * texture faces at all — `complex == 0` makes any per-complex figure correct.
     */
    int texture_scale_bytes = 6;
    if( format_version == 14 )
        texture_scale_bytes = 7;
    else if( format_version >= 15 )
        texture_scale_bytes = 9;

    int offsetOfSimpleTextureMapping = dataOffset;
    dataOffset += simpleTextureFaceCount * 6;
    dataOffset += complexTextureFaceCount * 6;
    dataOffset += complexTextureFaceCount * texture_scale_bytes;
    dataOffset += complexTextureFaceCount;     /* rotation */
    dataOffset += complexTextureFaceCount;     /* direction */
    dataOffset += complexTextureFaceCount;     /* translation */
    dataOffset += cubeTextureFaceCount * 2;

    {
        /* The raw bitmask, not the extracted bit: the trailer has to carry the byte the
         * source wrote, particle and billboard bits included. */
        const int flags[] = { header_flags,
                              model_priority,
                              has_face_transparencies,
                              has_packed_transparency_vertex_groups,
                              has_face_textures,
                              has_packed_vertex_groups };
        prov_header(
            prov, RSCACHE_MODEL_FORMAT_OB3, vertex_count, face_count, textured_face_count, flags,
            6);
        if( prov )
        {
            /* `offsetOfFaceRenderPriorities` is the face *index type* section — the
             * local names in this decoder are shifted one section against what they
             * hold, which the cursor assignments below make plain. */
            prov_bytes(
                &prov->face_index_types, inputData, offsetOfFaceRenderPriorities, face_count);
            /* The simple mapping triples are re-encoded from the model, so the tail
             * starts after them and carries the complex/cube blocks. */
            prov_tail(
                prov, inputData, offsetOfSimpleTextureMapping + (simpleTextureFaceCount * 6),
                inputLength - 23);
        }
    }

    model->vertex_count = vertex_count;
    model->face_count = face_count;
    model->textured_face_count = textured_face_count;

    model->vertices_x = (int*)malloc(vertex_count * sizeof(int));
    model->vertices_y = (int*)malloc(vertex_count * sizeof(int));
    model->vertices_z = (int*)malloc(vertex_count * sizeof(int));

    /* Zeroed, not just allocated: a face whose index type is 0 has none of the
     * three assigned, and reading back uninitialised heap is undefined where the
     * reference implementation reads a zeroed array. */
    model->face_indices_a = (int*)calloc((size_t)face_count, sizeof(int));
    model->face_indices_b = (int*)calloc((size_t)face_count, sizeof(int));
    model->face_indices_c = (int*)calloc((size_t)face_count, sizeof(int));

    if( has_packed_vertex_groups == 1 )
    {
        model->vertex_bone_map = (uint8_t*)malloc((size_t)vertex_count * sizeof(uint8_t));
    }

    if( has_face_render_types == 1 )
    {
        model->face_infos = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
    }

    if( model_priority == 255 )
    {
        model->face_priorities = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
    }
    else
    {
        model->model_priority = (uint8_t)model_priority;
    }

    if( has_face_transparencies == 1 )
    {
        model->face_alphas = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
    }

    if( has_packed_transparency_vertex_groups == 1 )
    {
        model->face_bone_map = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
    }

    if( has_face_textures == 1 )
    {
        model->face_textures = (int16_t*)malloc((size_t)face_count * sizeof(int16_t));
    }

    if( has_face_textures == 1 && textured_face_count > 0 )
    {
        /* Only faces that carry a texture get a coordinate byte, so the rest must
         * start at a defined value. Zero matches the reference, whose array is
         * zero-initialised; the entries are read only behind a
         * `face_textures[i] != -1` test either way. */
        model->face_texture_coords = (int16_t*)calloc((size_t)face_count, sizeof(int16_t));
    }

    model->face_colors = (uint16_t*)malloc((size_t)face_count * sizeof(uint16_t));

    if( textured_face_count > 0 )
    {
        /* Zeroed: only render-type-0 (simple) triangles have p/m/n in the stream. */
        model->textured_p_coordinate =
            (uint16_t*)calloc((size_t)textured_face_count, sizeof(uint16_t));
        model->textured_m_coordinate =
            (uint16_t*)calloc((size_t)textured_face_count, sizeof(uint16_t));
        model->textured_n_coordinate =
            (uint16_t*)calloc((size_t)textured_face_count, sizeof(uint16_t));
    }

    int previousVertexX = 0;
    int previousVertexY = 0;
    int previousVertexZ = 0;
    /*
     * The flag byte and the X delta come from *different* sections: flags from the
     * per-vertex flag block (which follows the texture render types), X from its own
     * delta stream. Reading both through one cursor — as this did — walked the flags
     * off into the X data and mangled every vertex. `version3` below has always had
     * this right; compare its cursor set-up.
     */
    offset = textured_face_count;
    int vertexXOffset = offsetOfVertexXData;

    for( int i = 0; i < vertex_count; ++i )
    {
        int vertexFlags = RSCache_BufferG1At(inputData, &offset);
        int deltaX = 0;
        if( (vertexFlags & 1) != 0 )
        {
            deltaX = RSCache_BufferReadShortSmartAt(inputData, &vertexXOffset);
        }

        int deltaY = 0;
        if( (vertexFlags & 2) != 0 )
        {
            deltaY = RSCache_BufferReadShortSmartAt(inputData, &offsetOfVertexYData);
        }

        int deltaZ = 0;
        if( (vertexFlags & 4) != 0 )
        {
            deltaZ = RSCache_BufferReadShortSmartAt(inputData, &offsetOfVertexZData);
        }

        model->vertices_x[i] = previousVertexX + deltaX;
        model->vertices_y[i] = previousVertexY + deltaY;
        model->vertices_z[i] = previousVertexZ + deltaZ;
        previousVertexX = model->vertices_x[i];
        previousVertexY = model->vertices_y[i];
        previousVertexZ = model->vertices_z[i];

        if( has_packed_vertex_groups == 1 )
        {
            model->vertex_bone_map[i] = RSCache_BufferG1At(inputData, &offsetOfPackedVertexGroups);
        }
    }

    offset = offsetOfFaceIndexTypes;
    int faceRenderTypesOffset = offsetOfFaceRenderTypes;
    int faceRenderPrioritiesOffset = offsetOfFaceTransparencies;
    int faceTransparenciesOffset = offsetOfFaceAlphas;
    int packedTransparencyVertexGroupsOffset = offsetOfPackedTransparencyVertexGroups;
    int faceTexturesOffset = offsetOfFaceColors;
    int textureCoordsOffset = offsetOfTextureCoords;

    for( int i = 0; i < face_count; ++i )
    {
        model->face_colors[i] = RSCache_BufferG2At(inputData, &offset);

        if( has_face_render_types == 1 )
        {
            model->face_infos[i] = RSCache_BufferG1At(inputData, &faceRenderTypesOffset);
        }

        if( model_priority == 255 )
        {
            model->face_priorities[i] = RSCache_BufferG1At(inputData, &faceRenderPrioritiesOffset);
        }

        if( has_face_transparencies == 1 )
        {
            model->face_alphas[i] = RSCache_BufferG1At(inputData, &faceTransparenciesOffset);
        }

        if( has_packed_transparency_vertex_groups == 1 )
        {
            model->face_bone_map[i] =
                RSCache_BufferG1At(inputData, &packedTransparencyVertexGroupsOffset);
        }

        if( has_face_textures == 1 )
        {
            model->face_textures[i] =
                (int16_t)((int)RSCache_BufferG2At(inputData, &faceTexturesOffset) - 1);
        }

        if( model->face_texture_coords != NULL && model->face_textures[i] != -1 )
        {
            model->face_texture_coords[i] =
                (int16_t)((int)RSCache_BufferG1At(inputData, &textureCoordsOffset) - 1);
        }
    }

    offset = offsetOfFaceIndices;
    int compressionTypesOffset = offsetOfFaceRenderPriorities;
    int previousIndex1 = 0;
    int previousIndex2 = 0;
    int previousIndex3 = 0;
    int previousIndex3Copy = 0;

    for( int i = 0; i < face_count; ++i )
    {
        int compressionType = RSCache_BufferG1At(inputData, &compressionTypesOffset);

        if( compressionType == 1 )
        {
            previousIndex1 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex2 = RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex1;
            previousIndex3 = RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex2;
            previousIndex3Copy = previousIndex3;
            model->face_indices_a[i] = previousIndex1;
            model->face_indices_b[i] = previousIndex2;
            model->face_indices_c[i] = previousIndex3;
        }

        if( compressionType == 2 )
        {
            previousIndex2 = previousIndex3;
            previousIndex3 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3;
            model->face_indices_a[i] = previousIndex1;
            model->face_indices_b[i] = previousIndex2;
            model->face_indices_c[i] = previousIndex3;
        }

        if( compressionType == 3 )
        {
            previousIndex1 = previousIndex3;
            previousIndex3 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3;
            model->face_indices_a[i] = previousIndex1;
            model->face_indices_b[i] = previousIndex2;
            model->face_indices_c[i] = previousIndex3;
        }

        if( compressionType == 4 )
        {
            int swappedIndex1 = previousIndex1;
            previousIndex1 = previousIndex2;
            previousIndex2 = swappedIndex1;
            previousIndex3 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3;
            model->face_indices_a[i] = previousIndex1;
            model->face_indices_b[i] = swappedIndex1;
            model->face_indices_c[i] = previousIndex3;
        }
    }

    offset = offsetOfSimpleTextureMapping;

    for( int i = 0; i < textured_face_count; ++i )
    {
        /*
         * The render type comes from the section at the head of the file, which was
         * already read above — not from this one. Reading a byte here consumed 7
         * bytes per simple triangle where the layout arithmetic 20 lines up allots
         * `simpleTextureFaceCount * 6`, so every triangle after the first took its
         * p/m/n from one byte too far along. `version3` reads the stored array.
         */
        int textureRenderType =
            model->texture_render_types ? (model->texture_render_types[i] & 255) : 0;
        if( textureRenderType == 0 )
        {
            model->textured_p_coordinate[i] = RSCache_BufferG2At(inputData, &offset);
            model->textured_m_coordinate[i] = RSCache_BufferG2At(inputData, &offset);
            model->textured_n_coordinate[i] = RSCache_BufferG2At(inputData, &offset);
        }
    }

    offset = dataOffset;
    int trailingFlag = RSCache_BufferG1At(inputData, &offset);
    if( trailingFlag != 0 )
    {
        RSCache_BufferG2At(inputData, &offset);
        RSCache_BufferG2At(inputData, &offset);
        RSCache_BufferG2At(inputData, &offset);
        RSCache_BufferG4At(inputData, &offset);
    }

    return model;
}

static struct RSCache_Model*
decode_version2__osrs_extended(
    const uint8_t* inputData,
    int inputLength,
    struct RSCache_ModelProvenance* prov)
{
    struct RSCache_Model* model = (struct RSCache_Model*)malloc(sizeof(struct RSCache_Model));
    if( !model )
        return NULL;
    memset(model, 0, sizeof(struct RSCache_Model));

    int foundFaceInfos = 0;
    int foundFaceTextures = 0;

    int headerOffset = inputLength - 23;
    int offset = 0;

    int vertex_count = RSCache_BufferG2At(inputData, &headerOffset);
    int face_count = RSCache_BufferG2At(inputData, &headerOffset);
    int textured_face_count = RSCache_BufferG1At(inputData, &headerOffset);
    int has_face_render_types = RSCache_BufferG1At(inputData, &headerOffset);
    int model_priority = RSCache_BufferG1At(inputData, &headerOffset);
    int has_face_transparencies = RSCache_BufferG1At(inputData, &headerOffset);
    int has_packed_transparency_vertex_groups = RSCache_BufferG1At(inputData, &headerOffset);
    int has_packed_vertex_groups = RSCache_BufferG1At(inputData, &headerOffset);
    int hasAnimayaGroups = RSCache_BufferG1At(inputData, &headerOffset);
    int vertexXDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);
    int vertexYDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);
    int vertexZDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);
    int faceIndexDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);
    int faceColorDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);

    int dataOffset = 0;
    int offsetOfVertexFlags = dataOffset;
    dataOffset += vertex_count;
    int offsetOfFaceIndexTypes = dataOffset;
    dataOffset += face_count;
    int offsetOfFaceRenderPriorities = dataOffset;
    if( model_priority == 255 )
    {
        dataOffset += face_count;
    }

    int offsetOfPackedTransparencyVertexGroups = dataOffset;
    if( has_packed_transparency_vertex_groups == 1 )
    {
        dataOffset += face_count;
    }

    int offsetOfFaceRenderTypes = dataOffset;
    if( has_face_render_types == 1 )
    {
        dataOffset += face_count;
    }

    int offsetOfPackedVertexGroups = dataOffset;
    dataOffset += faceColorDataByteCount;
    int offsetOfFaceTransparencies = dataOffset;
    if( has_face_transparencies == 1 )
    {
        dataOffset += face_count;
    }

    int offsetOfFaceIndexData = dataOffset;
    dataOffset += faceIndexDataByteCount;
    int offsetOfFaceColors = dataOffset;
    dataOffset += face_count * 2;
    int offsetOfTextureIndices = dataOffset;
    dataOffset += textured_face_count * 6;
    int offsetOfVertexXData = dataOffset;
    dataOffset += vertexXDataByteCount;
    int offsetOfVertexYData = dataOffset;
    dataOffset += vertexYDataByteCount;

    {
        const int flags[] = { has_face_render_types,
                              model_priority,
                              has_face_transparencies,
                              has_packed_transparency_vertex_groups,
                              has_packed_vertex_groups,
                              hasAnimayaGroups };
        prov_header(
            prov, RSCACHE_MODEL_FORMAT_V2, vertex_count, face_count, textured_face_count, flags, 6);
        if( prov )
        {
            prov_bytes(&prov->face_index_types, inputData, offsetOfFaceIndexTypes, face_count);
            if( has_face_render_types == 1 )
                prov_bytes(&prov->face_info_bytes, inputData, offsetOfFaceRenderTypes, face_count);
            /* Tail is filled after faceZOffsets so that section is not double-stored. */
        }
    }

    model->vertex_count = vertex_count;
    model->face_count = face_count;
    model->textured_face_count = textured_face_count;

    model->vertices_x = (int*)malloc(vertex_count * sizeof(int));
    model->vertices_y = (int*)malloc(vertex_count * sizeof(int));
    model->vertices_z = (int*)malloc(vertex_count * sizeof(int));

    /* Zeroed for the index-type-0 case; see the note in decode_ob3. */
    model->face_indices_a = (int*)calloc((size_t)face_count, sizeof(int));
    model->face_indices_b = (int*)calloc((size_t)face_count, sizeof(int));
    model->face_indices_c = (int*)calloc((size_t)face_count, sizeof(int));

    if( textured_face_count > 0 )
    {
        model->texture_render_types =
            (unsigned char*)malloc(textured_face_count * sizeof(unsigned char));
        model->textured_p_coordinate =
            (uint16_t*)malloc((size_t)textured_face_count * sizeof(uint16_t));
        model->textured_m_coordinate =
            (uint16_t*)malloc((size_t)textured_face_count * sizeof(uint16_t));
        model->textured_n_coordinate =
            (uint16_t*)malloc((size_t)textured_face_count * sizeof(uint16_t));
    }

    if( has_packed_vertex_groups == 1 )
    {
        model->vertex_bone_map = (uint8_t*)malloc((size_t)vertex_count * sizeof(uint8_t));
    }

    if( has_face_render_types == 1 )
    {
        model->face_infos = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
        model->face_texture_coords = (int16_t*)malloc((size_t)face_count * sizeof(int16_t));
        model->face_textures = (int16_t*)malloc((size_t)face_count * sizeof(int16_t));
    }

    if( model_priority == 255 )
    {
        model->face_priorities = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
    }
    else
    {
        model->model_priority = (uint8_t)model_priority;
    }

    if( has_face_transparencies == 1 )
    {
        model->face_alphas = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
    }

    if( has_packed_transparency_vertex_groups == 1 )
    {
        model->face_bone_map = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
    }

    if( hasAnimayaGroups == 1 )
    {
        model->animaya_vertex_count = vertex_count;
        model->animaya_group_counts = calloc((size_t)vertex_count, sizeof(uint8_t));
        model->animaya_groups = calloc((size_t)vertex_count, sizeof(uint8_t*));
        model->animaya_scales = calloc((size_t)vertex_count, sizeof(uint8_t*));
    }

    model->face_colors = (uint16_t*)malloc((size_t)face_count * sizeof(uint16_t));

    offset = offsetOfVertexFlags;
    int vertexXOffset = offsetOfVertexXData;
    int vertexYOffset = offsetOfVertexYData;
    int vertexZOffset = dataOffset;
    int packedVertexGroupsOffset = offsetOfPackedVertexGroups;

    int previousVertexX = 0;
    int previousVertexY = 0;
    int previousVertexZ = 0;

    for( int i = 0; i < vertex_count; ++i )
    {
        int vertexFlags = RSCache_BufferG1At(inputData, &offset);
        int deltaX = 0;
        if( (vertexFlags & 1) != 0 )
        {
            deltaX = RSCache_BufferReadShortSmartAt(inputData, &vertexXOffset);
        }

        int deltaY = 0;
        if( (vertexFlags & 2) != 0 )
        {
            deltaY = RSCache_BufferReadShortSmartAt(inputData, &vertexYOffset);
        }

        int deltaZ = 0;
        if( (vertexFlags & 4) != 0 )
        {
            deltaZ = RSCache_BufferReadShortSmartAt(inputData, &vertexZOffset);
        }

        model->vertices_x[i] = previousVertexX + deltaX;
        model->vertices_y[i] = previousVertexY + deltaY;
        model->vertices_z[i] = previousVertexZ + deltaZ;
        previousVertexX = model->vertices_x[i];
        previousVertexY = model->vertices_y[i];
        previousVertexZ = model->vertices_z[i];

        if( has_packed_vertex_groups == 1 )
        {
            model->vertex_bone_map[i] = RSCache_BufferG1At(inputData, &packedVertexGroupsOffset);
        }
    }

    if( hasAnimayaGroups == 1 )
    {
        for( int i = 0; i < vertex_count; ++i )
        {
            int animayaGroupCount = RSCache_BufferG1At(inputData, &packedVertexGroupsOffset);
            if( model->animaya_group_counts )
                model->animaya_group_counts[i] = (uint8_t)animayaGroupCount;
            if( animayaGroupCount > 0 && model->animaya_groups && model->animaya_scales )
            {
                model->animaya_groups[i] = malloc((size_t)animayaGroupCount);
                model->animaya_scales[i] = malloc((size_t)animayaGroupCount);
            }
            for( int j = 0; j < animayaGroupCount; ++j )
            {
                uint8_t grp = (uint8_t)RSCache_BufferG1At(inputData, &packedVertexGroupsOffset);
                uint8_t scale = (uint8_t)RSCache_BufferG1At(inputData, &packedVertexGroupsOffset);
                if( model->animaya_groups && model->animaya_groups[i] )
                    model->animaya_groups[i][j] = grp;
                if( model->animaya_scales && model->animaya_scales[i] )
                    model->animaya_scales[i][j] = scale;
            }
        }
    }

    offset = offsetOfFaceColors;
    int faceRenderTypesOffset = offsetOfFaceRenderTypes;
    int faceRenderPrioritiesOffset = offsetOfFaceRenderPriorities;
    int faceTransparenciesOffset = offsetOfFaceTransparencies;
    int packedTransparencyVertexGroupsOffset = offsetOfPackedTransparencyVertexGroups;

    for( int i = 0; i < face_count; ++i )
    {
        model->face_colors[i] = RSCache_BufferG2At(inputData, &offset);

        if( has_face_render_types == 1 )
        {
            int faceTextureFlags = RSCache_BufferG1At(inputData, &faceRenderTypesOffset);
            if( (faceTextureFlags & 1) == 1 )
            {
                model->face_infos[i] = 1;
                foundFaceInfos = 1;
            }
            else
            {
                model->face_infos[i] = 0;
            }

            if( (faceTextureFlags & 2) == 2 )
            {
                model->face_texture_coords[i] = (int16_t)(faceTextureFlags >> 2);
                model->face_textures[i] = (int16_t)model->face_colors[i];
                model->face_colors[i] = 127;
                if( model->face_textures[i] != -1 )
                {
                    foundFaceTextures = 1;
                }
            }
            else
            {
                model->face_texture_coords[i] = -1;
                model->face_textures[i] = -1;
            }
        }

        if( model_priority == 255 )
        {
            model->face_priorities[i] = RSCache_BufferG1At(inputData, &faceRenderPrioritiesOffset);
        }

        if( has_face_transparencies == 1 )
        {
            model->face_alphas[i] = RSCache_BufferG1At(inputData, &faceTransparenciesOffset);
        }

        if( has_packed_transparency_vertex_groups == 1 )
        {
            model->face_bone_map[i] =
                RSCache_BufferG1At(inputData, &packedTransparencyVertexGroupsOffset);
        }
    }

    offset = offsetOfFaceIndexData;
    int faceIndexTypesOffset = offsetOfFaceIndexTypes;
    int previousIndex1 = 0;
    int previousIndex2 = 0;
    int previousIndex3 = 0;
    int previousIndex3Copy = 0;

    for( int i = 0; i < face_count; ++i )
    {
        int compressionType = RSCache_BufferG1At(inputData, &faceIndexTypesOffset);
        if( compressionType == 1 )
        {
            previousIndex1 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex2 = RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex1;
            previousIndex3 = RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex2;
            previousIndex3Copy = previousIndex3;
            model->face_indices_a[i] = previousIndex1;
            model->face_indices_b[i] = previousIndex2;
            model->face_indices_c[i] = previousIndex3;
        }

        if( compressionType == 2 )
        {
            previousIndex2 = previousIndex3;
            previousIndex3 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3;
            model->face_indices_a[i] = previousIndex1;
            model->face_indices_b[i] = previousIndex2;
            model->face_indices_c[i] = previousIndex3;
        }

        if( compressionType == 3 )
        {
            previousIndex1 = previousIndex3;
            previousIndex3 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3;
            model->face_indices_a[i] = previousIndex1;
            model->face_indices_b[i] = previousIndex2;
            model->face_indices_c[i] = previousIndex3;
        }

        if( compressionType == 4 )
        {
            int swappedIndex1 = previousIndex1;
            previousIndex1 = previousIndex2;
            previousIndex2 = swappedIndex1;
            previousIndex3 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3;
            model->face_indices_a[i] = previousIndex1;
            model->face_indices_b[i] = swappedIndex1;
            model->face_indices_c[i] = previousIndex3;
        }
    }

    offset = offsetOfTextureIndices;
    for( int i = 0; i < textured_face_count; ++i )
    {
        model->texture_render_types[i] = 0;
        model->textured_p_coordinate[i] = RSCache_BufferG2At(inputData, &offset);
        model->textured_m_coordinate[i] = RSCache_BufferG2At(inputData, &offset);
        model->textured_n_coordinate[i] = RSCache_BufferG2At(inputData, &offset);
    }

    /* OSRS 232+: faceZOffsets sits immediately after the vertex Z column. */
    offset = dataOffset + vertexZDataByteCount;
    if( offset < inputLength - 23 )
    {
        int hasOffsets = RSCache_BufferG1At(inputData, &offset);
        if( hasOffsets == 1 )
        {
            model->face_z_offsets = (int8_t*)malloc((size_t)face_count);
            for( int i = 0; i < face_count; ++i )
            {
                int8_t b = (int8_t)RSCache_BufferG1At(inputData, &offset);
                if( model->face_z_offsets )
                    model->face_z_offsets[i] = b;
            }
        }
    }
    if( prov )
        prov_tail(prov, inputData, offset, inputLength - 23);

    if( model->face_texture_coords != NULL )
    {
        int anyFaceUsesTextureCoords = 0;

        for( int i = 0; i < face_count; ++i )
        {
            int textureCoordIndex = model->face_texture_coords[i] & 255;
            if( textureCoordIndex != 255 )
            {
                if( model->face_indices_a[i] ==
                        (model->textured_p_coordinate[textureCoordIndex] & 0xFFFF) &&
                    model->face_indices_b[i] ==
                        (model->textured_m_coordinate[textureCoordIndex] & 0xFFFF) &&
                    model->face_indices_c[i] ==
                        (model->textured_n_coordinate[textureCoordIndex] & 0xFFFF) )
                {
                    model->face_texture_coords[i] = -1;
                }
                else
                {
                    anyFaceUsesTextureCoords = 1;
                }
            }
        }

        if( !anyFaceUsesTextureCoords )
        {
            free(model->face_texture_coords);
            model->face_texture_coords = NULL;
        }
    }

    if( !foundFaceTextures )
    {
        free(model->face_textures);
        model->face_textures = NULL;
    }

    model_assert_texture_invariant(model);
    (void)foundFaceInfos;
    (void)foundFaceTextures;
    return model;
}

static struct RSCache_Model*
decode_version3__osrs_material(
    const uint8_t* inputData,
    int inputLength,
    struct RSCache_ModelProvenance* prov)
{
    struct RSCache_Model* model = (struct RSCache_Model*)malloc(sizeof(struct RSCache_Model));
    memset(model, 0, sizeof(struct RSCache_Model));

    int headerOffset = inputLength - 26;
    int offset = 0;

    int vertexCount = RSCache_BufferG2At(inputData, &headerOffset);
    int faceCount = RSCache_BufferG2At(inputData, &headerOffset);
    int texTriangleCount = RSCache_BufferG1At(inputData, &headerOffset);
    int hasFaceInfos = RSCache_BufferG1At(inputData, &headerOffset);
    int hasFacePriorities = RSCache_BufferG1At(inputData, &headerOffset);
    int hasFaceAlphas = RSCache_BufferG1At(inputData, &headerOffset);
    int hasPackedTransparencyVertexGroups = RSCache_BufferG1At(inputData, &headerOffset);
    int hasFaceTextures = RSCache_BufferG1At(inputData, &headerOffset);
    int hasPackedVertexGroups = RSCache_BufferG1At(inputData, &headerOffset);
    int hasAnimayaGroups = RSCache_BufferG1At(inputData, &headerOffset);
    int vertexXDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);
    int vertexYDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);
    int vertexZDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);
    int faceIndexDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);
    int textureCoordDataByteCount = RSCache_BufferG2At(inputData, &headerOffset);
    int animayaSectionByteCount = RSCache_BufferG2At(inputData, &headerOffset);

    int simpleTextureFaceCount = 0;
    int complexTextureFaceCount = 0;
    int cubeTextureFaceCount = 0;

    if( texTriangleCount > 0 )
    {
        model->texture_render_types =
            (unsigned char*)malloc(texTriangleCount * sizeof(unsigned char));
        headerOffset = 0;

        for( int i = 0; i < texTriangleCount; ++i )
        {
            unsigned char textureRenderType = model->texture_render_types[i] =
                RSCache_BufferG1At(inputData, &headerOffset);
            if( textureRenderType == 0 )
            {
                ++simpleTextureFaceCount;
            }

            if( textureRenderType >= 1 && textureRenderType <= 3 )
            {
                ++complexTextureFaceCount;
            }

            if( textureRenderType == 2 )
            {
                ++cubeTextureFaceCount;
            }
        }
    }

    int dataOffset = texTriangleCount + vertexCount;
    int offsetOfFaceInfos = dataOffset;
    if( hasFaceInfos == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfFaceIndexTypes = dataOffset;
    dataOffset += faceCount;
    int offsetOfFaceRenderPriorities = dataOffset;
    if( hasFacePriorities == 255 )
    {
        dataOffset += faceCount;
    }

    int offsetOfPackedTransparencyVertexGroups = dataOffset;
    if( hasPackedTransparencyVertexGroups == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfPackedVertexGroups = dataOffset;
    dataOffset += animayaSectionByteCount;
    int offsetOfFaceTransparencies = dataOffset;
    if( hasFaceAlphas == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfFaceIndexData = dataOffset;
    dataOffset += faceIndexDataByteCount;
    int offsetOfFaceTextures = dataOffset;
    if( hasFaceTextures == 1 )
    {
        dataOffset += faceCount * 2;
    }

    int offsetOfTextureCoords = dataOffset;
    dataOffset += textureCoordDataByteCount;
    int offsetOfFaceColors = dataOffset;
    dataOffset += faceCount * 2;
    int offsetOfVertexXData = dataOffset;
    dataOffset += vertexXDataByteCount;
    int offsetOfVertexYData = dataOffset;
    dataOffset += vertexYDataByteCount;
    int offsetOfVertexZData = dataOffset;
    dataOffset += vertexZDataByteCount;
    int offsetOfSimpleTextureMapping = dataOffset;
    dataOffset += simpleTextureFaceCount * 6;
    dataOffset += complexTextureFaceCount * 6;
    dataOffset += complexTextureFaceCount * 6;
    dataOffset += complexTextureFaceCount * 2;
    dataOffset += complexTextureFaceCount;
    dataOffset += complexTextureFaceCount * 2;
    dataOffset = dataOffset + complexTextureFaceCount * 2 + cubeTextureFaceCount * 2;

    {
        const int flags[] = { hasFaceInfos,
                              hasFacePriorities,
                              hasFaceAlphas,
                              hasPackedTransparencyVertexGroups,
                              hasFaceTextures,
                              hasPackedVertexGroups,
                              hasAnimayaGroups };
        prov_header(
            prov, RSCACHE_MODEL_FORMAT_V3, vertexCount, faceCount, texTriangleCount, flags, 7);
        if( prov )
        {
            prov_bytes(&prov->face_index_types, inputData, offsetOfFaceIndexTypes, faceCount);
            prov_tail(
                prov, inputData, offsetOfSimpleTextureMapping + (simpleTextureFaceCount * 6),
                inputLength - 26);
        }
    }

    model->vertex_count = vertexCount;
    model->face_count = faceCount;
    model->textured_face_count = texTriangleCount;

    model->vertices_x = (int*)malloc(vertexCount * sizeof(int));
    model->vertices_y = (int*)malloc(vertexCount * sizeof(int));
    model->vertices_z = (int*)malloc(vertexCount * sizeof(int));

    /* Zeroed for the index-type-0 case; see the note in decode_ob3. */
    model->face_indices_a = (int*)calloc((size_t)faceCount, sizeof(int));
    model->face_indices_b = (int*)calloc((size_t)faceCount, sizeof(int));
    model->face_indices_c = (int*)calloc((size_t)faceCount, sizeof(int));

    if( hasPackedVertexGroups == 1 )
    {
        model->vertex_bone_map = (uint8_t*)malloc((size_t)vertexCount * sizeof(uint8_t));
    }

    if( hasFaceInfos == 1 )
    {
        model->face_infos = (uint8_t*)malloc((size_t)faceCount * sizeof(uint8_t));
    }

    if( hasFacePriorities == 255 )
    {
        model->face_priorities = (uint8_t*)malloc((size_t)faceCount * sizeof(uint8_t));
    }
    else
    {
        model->model_priority = (uint8_t)hasFacePriorities;
    }

    if( hasFaceAlphas == 1 )
    {
        model->face_alphas = (uint8_t*)malloc((size_t)faceCount * sizeof(uint8_t));
    }

    if( hasPackedTransparencyVertexGroups == 1 )
    {
        model->face_bone_map = (uint8_t*)malloc((size_t)faceCount * sizeof(uint8_t));
        memset(model->face_bone_map, 0, (size_t)faceCount * sizeof(uint8_t));
    }

    if( hasFaceTextures == 1 )
    {
        model->face_textures = (int16_t*)malloc((size_t)faceCount * sizeof(int16_t));
    }

    if( hasFaceTextures == 1 && texTriangleCount > 0 )
    {
        /* Zeroed; see the matching note in decode_ob3. */
        model->face_texture_coords = (int16_t*)calloc((size_t)faceCount, sizeof(int16_t));
    }

    if( hasAnimayaGroups == 1 )
    {
        model->animaya_vertex_count = vertexCount;
        model->animaya_group_counts = calloc((size_t)vertexCount, sizeof(uint8_t));
        model->animaya_groups = calloc((size_t)vertexCount, sizeof(uint8_t*));
        model->animaya_scales = calloc((size_t)vertexCount, sizeof(uint8_t*));
    }

    model->face_colors = (uint16_t*)malloc((size_t)faceCount * sizeof(uint16_t));

    if( texTriangleCount > 0 )
    {
        /* Zeroed: only render-type-0 triangles have p/m/n in the stream. */
        model->textured_p_coordinate =
            (uint16_t*)calloc((size_t)texTriangleCount, sizeof(uint16_t));
        model->textured_m_coordinate =
            (uint16_t*)calloc((size_t)texTriangleCount, sizeof(uint16_t));
        model->textured_n_coordinate =
            (uint16_t*)calloc((size_t)texTriangleCount, sizeof(uint16_t));
    }

    offset = texTriangleCount;
    int vertexXOffset = offsetOfVertexXData;
    int vertexYOffset = offsetOfVertexYData;
    int vertexZOffset = offsetOfVertexZData;
    int packedVertexGroupsOffset = offsetOfPackedVertexGroups;

    int previousVertexX = 0;
    int previousVertexY = 0;
    int previousVertexZ = 0;

    for( int i = 0; i < vertexCount; ++i )
    {
        int vertexFlags = RSCache_BufferG1At(inputData, &offset);
        int deltaX = 0;
        if( (vertexFlags & 1) != 0 )
        {
            deltaX = RSCache_BufferReadShortSmartAt(inputData, &vertexXOffset);
        }

        int deltaY = 0;
        if( (vertexFlags & 2) != 0 )
        {
            deltaY = RSCache_BufferReadShortSmartAt(inputData, &vertexYOffset);
        }

        int deltaZ = 0;
        if( (vertexFlags & 4) != 0 )
        {
            deltaZ = RSCache_BufferReadShortSmartAt(inputData, &vertexZOffset);
        }

        model->vertices_x[i] = previousVertexX + deltaX;
        model->vertices_y[i] = previousVertexY + deltaY;
        model->vertices_z[i] = previousVertexZ + deltaZ;
        previousVertexX = model->vertices_x[i];
        previousVertexY = model->vertices_y[i];
        previousVertexZ = model->vertices_z[i];

        if( hasPackedVertexGroups == 1 )
        {
            model->vertex_bone_map[i] = RSCache_BufferG1At(inputData, &packedVertexGroupsOffset);
        }
    }

    if( hasAnimayaGroups == 1 )
    {
        for( int i = 0; i < vertexCount; ++i )
        {
            int animayaGroupCount = RSCache_BufferG1At(inputData, &packedVertexGroupsOffset);
            if( model->animaya_group_counts )
                model->animaya_group_counts[i] = (uint8_t)animayaGroupCount;
            if( animayaGroupCount > 0 && model->animaya_groups && model->animaya_scales )
            {
                model->animaya_groups[i] = malloc((size_t)animayaGroupCount);
                model->animaya_scales[i] = malloc((size_t)animayaGroupCount);
            }
            for( int j = 0; j < animayaGroupCount; ++j )
            {
                uint8_t grp = (uint8_t)RSCache_BufferG1At(inputData, &packedVertexGroupsOffset);
                uint8_t scale = (uint8_t)RSCache_BufferG1At(inputData, &packedVertexGroupsOffset);
                if( model->animaya_groups && model->animaya_groups[i] )
                    model->animaya_groups[i][j] = grp;
                if( model->animaya_scales && model->animaya_scales[i] )
                    model->animaya_scales[i][j] = scale;
            }
        }
    }

    offset = offsetOfFaceColors;
    int faceInfosOffset = offsetOfFaceInfos;
    int faceRenderPrioritiesOffset = offsetOfFaceRenderPriorities;
    int faceTransparenciesOffset = offsetOfFaceTransparencies;
    int packedTransparencyVertexGroupsOffset = offsetOfPackedTransparencyVertexGroups;
    int faceTexturesOffset = offsetOfFaceTextures;
    int textureCoordsOffset = offsetOfTextureCoords;

    for( int i = 0; i < faceCount; ++i )
    {
        model->face_colors[i] = RSCache_BufferG2At(inputData, &offset);

        if( hasFaceInfos == 1 )
        {
            model->face_infos[i] = RSCache_BufferG1At(inputData, &faceInfosOffset);
        }

        if( hasFacePriorities == 255 )
        {
            model->face_priorities[i] = RSCache_BufferG1At(inputData, &faceRenderPrioritiesOffset);
        }

        if( hasFaceAlphas == 1 )
        {
            model->face_alphas[i] = RSCache_BufferG1At(inputData, &faceTransparenciesOffset);
        }

        if( hasPackedTransparencyVertexGroups == 1 )
        {
            model->face_bone_map[i] =
                RSCache_BufferG1At(inputData, &packedTransparencyVertexGroupsOffset);
        }

        if( hasFaceTextures == 1 )
        {
            model->face_textures[i] =
                (int16_t)((int)RSCache_BufferG2At(inputData, &faceTexturesOffset) - 1);
        }

        if( model->face_texture_coords != NULL && model->face_textures[i] != -1 )
        {
            model->face_texture_coords[i] =
                (int16_t)((int)RSCache_BufferG1At(inputData, &textureCoordsOffset) - 1);
        }
    }

    offset = offsetOfFaceIndexData;
    int faceIndexTypesOffset = offsetOfFaceIndexTypes;
    int previousIndex1 = 0;
    int previousIndex2 = 0;
    int previousIndex3 = 0;
    int previousIndex3Copy = 0;

    for( int i = 0; i < faceCount; ++i )
    {
        int compressionType = RSCache_BufferG1At(inputData, &faceIndexTypesOffset);
        if( compressionType == 1 )
        {
            previousIndex1 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex2 = RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex1;
            previousIndex3 = RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex2;
            previousIndex3Copy = previousIndex3;
            model->face_indices_a[i] = previousIndex1;
            model->face_indices_b[i] = previousIndex2;
            model->face_indices_c[i] = previousIndex3;
        }

        if( compressionType == 2 )
        {
            previousIndex2 = previousIndex3;
            previousIndex3 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3;
            model->face_indices_a[i] = previousIndex1;
            model->face_indices_b[i] = previousIndex2;
            model->face_indices_c[i] = previousIndex3;
        }

        if( compressionType == 3 )
        {
            previousIndex1 = previousIndex3;
            previousIndex3 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3;
            model->face_indices_a[i] = previousIndex1;
            model->face_indices_b[i] = previousIndex2;
            model->face_indices_c[i] = previousIndex3;
        }

        if( compressionType == 4 )
        {
            int swappedIndex1 = previousIndex1;
            previousIndex1 = previousIndex2;
            previousIndex2 = swappedIndex1;
            previousIndex3 =
                RSCache_BufferReadShortSmartAt(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3;
            model->face_indices_a[i] = previousIndex1;
            model->face_indices_b[i] = swappedIndex1;
            model->face_indices_c[i] = previousIndex3;
        }
    }

    offset = offsetOfSimpleTextureMapping;

    for( int i = 0; i < texTriangleCount; ++i )
    {
        int textureRenderType = model->texture_render_types[i] & 255;
        if( textureRenderType == 0 )
        {
            model->textured_p_coordinate[i] = RSCache_BufferG2At(inputData, &offset);
            model->textured_m_coordinate[i] = RSCache_BufferG2At(inputData, &offset);
            model->textured_n_coordinate[i] = RSCache_BufferG2At(inputData, &offset);
        }
    }

    offset = dataOffset;
    int trailingFlag = RSCache_BufferG1At(inputData, &offset);
    if( trailingFlag != 0 )
    {
        RSCache_BufferG2At(inputData, &offset);
        RSCache_BufferG2At(inputData, &offset);
        RSCache_BufferG2At(inputData, &offset);
        RSCache_BufferG4At(inputData, &offset);
    }

    /* OSRS 232+: faceZOffsets after the trailingFlag block. */
    if( offset < inputLength - 26 )
    {
        int hasOffsets = RSCache_BufferG1At(inputData, &offset);
        if( hasOffsets == 1 )
        {
            model->face_z_offsets = (int8_t*)malloc((size_t)faceCount);
            for( int i = 0; i < faceCount; ++i )
            {
                int8_t b = (int8_t)RSCache_BufferG1At(inputData, &offset);
                if( model->face_z_offsets )
                    model->face_z_offsets[i] = b;
            }
        }
        /* Strip faceZOffsets from the provenance tail so encode does not double-emit. */
        if( prov && prov->tail_size > 0 )
        {
            int face_z_bytes = (hasOffsets == 1) ? (1 + faceCount) : 1;
            if( prov->tail_size >= face_z_bytes )
                prov->tail_size -= face_z_bytes;
        }
    }

    model_assert_texture_invariant(model);
    return model;
}

struct RSCache_Model*
RSCache_ModelNewFromCache(
    struct RSCache_Dat2Disk* cache,
    int model_id)
{
    struct RSCache_Model* model = NULL;
    struct RSCache_Dat2DiskArchive* archive = RSCache_Dat2DiskArchiveNewLoad(
        cache, RSCache_Dat2DiskTableId(cache, RSCACHE_DAT2_TABLE_MODELS), model_id);
    if( !archive )
    {
        printf("Failed to load model %d\n", model_id);
        return NULL;
    }

    model = RSCache_ModelNewDecode((uint8_t*)archive->data, archive->data_size);

    RSCache_Dat2DiskArchiveFree(archive);

    return model;
}

struct RSCache_Model*
RSCache_ModelNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int model_id_nullable)
{
    (void)model_id_nullable;
    return RSCache_ModelNewDecode((uint8_t*)archive->data, archive->data_size);
}

struct RSCache_Model*
RSCache_ModelNewFromDatArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int model_id_nullable)
{
    (void)model_id_nullable;
    return RSCache_ModelNewDecode((uint8_t*)archive->data, archive->data_size);
}

// From Gemini
// Type 0 (ob2): This is the "Legacy" format from the 2004 launch of RuneScape 2. It has no footer
// marker because it was the only format at the time. The loader simply tries to parse it if no
// other markers are found.
//
// Type 1 (ob3): Introduced around 2006. It added a 2-byte footer (0xFFFF)
// to explicitly tell the client "I am the new format." This added support for vertex skinning
// (boneweights for smoother animation).
//
// Type 2 (0xFEFF): This was an OSRS-specific upgrade. It
// expanded the model limits, allowing for more than 65,535 vertices/faces in a single model and
// better support for high-definition textures used in the Steam and mobile clients.
//
// Type 3 (0xFDFF): This is the current "Modern" format. It includes support for GPU-based vertex
// colors, enhanced alpha transparency, and "Material" IDs that map to specific shaders (like the
// shiny effect on a Crystal Body or the glow on a Shadow Silk Robe).
struct RSCache_Model*
RSCache_ModelNewDecode(
    uint8_t* data,
    int data_size)
{
    return RSCache_ModelNewDecodeProvenance(data, data_size, NULL);
}

void
RSCache_ModelProvenanceFree(struct RSCache_ModelProvenance* provenance)
{
    if( !provenance )
        return;

    free(provenance->face_index_types);
    free(provenance->face_info_bytes);
    free(provenance->tail);
    free(provenance);
}

struct RSCache_Model*
RSCache_ModelNewDecodeProvenance(
    uint8_t* data,
    int data_size,
    struct RSCache_ModelProvenance** out_provenance)
{
    struct RSCache_ModelProvenance* prov = NULL;
    if( out_provenance )
    {
        *out_provenance = NULL;
        prov = (struct RSCache_ModelProvenance*)calloc(1, sizeof(*prov));
        if( !prov )
            return NULL;
    }

    struct RSCache_Model* model = NULL;
    // Check the last two bytes to determine model type
    if( data_size >= 2 )
    {
        uint8_t lastByte = data[data_size - 1];
        uint8_t secondLastByte = data[data_size - 2];

        if( lastByte == 0xFD && secondLastByte == 0xFF )
        { // -3, -1
            model = decode_version3__osrs_material(data, data_size, prov);
        }
        else if( lastByte == 0xFE && secondLastByte == 0xFF )
        { // -2, -1
            model = decode_version2__osrs_extended(data, data_size, prov);
        }
        else if( lastByte == 0xFF && secondLastByte == 0xFF )
        { // -1, -1
            model = decode_ob3(data, data_size, prov);
        }
    }

    if( !model )
    {
        // Used by many early 2004scape models.
        prov_reset(prov);
        model = decode_ob2(data, data_size, prov);
    }

    if( !model )
    {
        RSCache_ModelProvenanceFree(prov);
        return NULL;
    }

    // This is a hack. I'm not sure where this is done in the deob,
    // but it appears that if a model has face bone map, but no face alphas,
    // then face alphas are all assumed to be "0" (opaque), this is so the animation can add
    // transparency.
    if( model->face_bone_map )
    {
        if( !model->face_alphas )
        {
            model->face_alphas = (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
            memset(model->face_alphas, 0, (size_t)model->face_count * sizeof(uint8_t));
        }
    }

    // if( model->face_infos )
    // {
    //     for( int i = 0; i < model->face_count; i++ )
    //     {
    //         int face_info = model->face_infos[i];
    //         assert(face_info == 0 || face_info == 1 || face_info == 2);
    //     }
    // }

    if( out_provenance )
        *out_provenance = prov;
    return model;
}

/* ==================================================================== encode === */
/*
 * The four layouts are the same set of sections in two different orders, with two
 * different trailers. So rather than four hand-rolled writers, each section is
 * built into its own growable buffer and then concatenated in the order the format
 * wants. That falls out of the shape of the problem: the trailer carries the byte
 * counts of four of the sections, and building them separately means those counts
 * are simply the buffers' lengths — no backfilling, no second pass.
 */

enum model_section
{
    SEC_RENDER_TYPES = 0,
    SEC_VERTEX_FLAGS,
    SEC_FACE_INDEX_TYPES,
    SEC_FACE_PRIORITIES,
    SEC_FACE_SKINS,
    SEC_FACE_INFOS,
    SEC_VERTEX_SKINS,
    SEC_FACE_ALPHAS,
    SEC_FACE_INDEX_DATA,
    SEC_FACE_TEXTURES,
    SEC_TEXTURE_COORDS,
    SEC_FACE_COLORS,
    SEC_TEXTURE_MAPPING,
    SEC_VERTEX_X,
    SEC_VERTEX_Y,
    SEC_VERTEX_Z,
    SEC_TAIL,
    SEC_COUNT
};

/** Section order, terminated by -1. OB2 and V2 share one; OB3 and V3 the other. */
static const int MODEL_ORDER_OB2_V2[] = {
    SEC_VERTEX_FLAGS,    SEC_FACE_INDEX_TYPES, SEC_FACE_PRIORITIES, SEC_FACE_SKINS,
    SEC_FACE_INFOS,      SEC_VERTEX_SKINS,     SEC_FACE_ALPHAS,     SEC_FACE_INDEX_DATA,
    SEC_FACE_COLORS,     SEC_TEXTURE_MAPPING,  SEC_VERTEX_X,        SEC_VERTEX_Y,
    SEC_VERTEX_Z,        SEC_TAIL,             -1
};

static const int MODEL_ORDER_OB3_V3[] = {
    SEC_RENDER_TYPES,   SEC_VERTEX_FLAGS,  SEC_FACE_INFOS,      SEC_FACE_INDEX_TYPES,
    SEC_FACE_PRIORITIES, SEC_FACE_SKINS,   SEC_VERTEX_SKINS,    SEC_FACE_ALPHAS,
    SEC_FACE_INDEX_DATA, SEC_FACE_TEXTURES, SEC_TEXTURE_COORDS, SEC_FACE_COLORS,
    SEC_VERTEX_X,        SEC_VERTEX_Y,      SEC_VERTEX_Z,       SEC_TEXTURE_MAPPING,
    SEC_TAIL,            -1
};

/** The header flags, normalised across formats so the writers read one shape. */
struct model_header
{
    int format;
    int vertex_count;
    int face_count;
    int textured_face_count;
    int has_face_info;
    /** 255 means "a priority per face"; anything else is the whole model's. */
    int model_priority;
    int has_alpha;
    int has_face_labels;
    /** OB3/V3 only — OB2/V2 pack texture ids into the per-face info byte. */
    int has_face_textures;
    /**
     * OB3/V3: the raw header byte that `has_face_info` is bit 0 of. Written to the
     * trailer verbatim, because its other bits announce particle and billboard sections
     * this library carries in the tail without interpreting. For OB2/V2 it equals
     * `has_face_info`, which really is a boolean there.
     */
    int face_info_flags;
    int has_vertex_labels;
    /** V2/V3 only. */
    int has_animaya;
};

static int
model_header_flag_count(int format)
{
    switch( format )
    {
    case RSCACHE_MODEL_FORMAT_OB2:
        return 5;
    case RSCACHE_MODEL_FORMAT_OB3:
    case RSCACHE_MODEL_FORMAT_V2:
        return 6;
    case RSCACHE_MODEL_FORMAT_V3:
        return 7;
    default:
        return 0;
    }
}

/** Read the header out of a provenance. Returns false if it does not describe
 *  `format`, which is how a zeroed or mismatched provenance is caught. */
static bool
model_header_from_provenance(
    struct model_header* header,
    const struct RSCache_ModelProvenance* prov,
    int format)
{
    if( !prov || prov->format != format )
        return false;
    if( prov->header_flag_count != model_header_flag_count(format) )
        return false;

    const uint8_t* f = prov->header_flags;
    memset(header, 0, sizeof(*header));
    header->format = format;
    header->vertex_count = prov->vertex_count;
    header->face_count = prov->face_count;
    header->textured_face_count = prov->textured_face_count;
    header->face_info_flags = f[0];
    /* Bit 0 for the OB3/V3 family, where the byte is a bitmask; the whole value for
     * OB2/V2, where it is a plain flag. */
    header->has_face_info =
        (format == RSCACHE_MODEL_FORMAT_OB3 || format == RSCACHE_MODEL_FORMAT_V3) ? (f[0] & 1)
                                                                                 : f[0];
    header->model_priority = f[1];
    header->has_alpha = f[2];
    header->has_face_labels = f[3];

    switch( format )
    {
    case RSCACHE_MODEL_FORMAT_OB2:
        header->has_vertex_labels = f[4];
        break;
    case RSCACHE_MODEL_FORMAT_OB3:
        header->has_face_textures = f[4];
        header->has_vertex_labels = f[5];
        break;
    case RSCACHE_MODEL_FORMAT_V2:
        header->has_vertex_labels = f[4];
        header->has_animaya = f[5];
        break;
    default: /* V3 */
        header->has_face_textures = f[4];
        header->has_vertex_labels = f[5];
        header->has_animaya = f[6];
        break;
    }
    return true;
}

/** Do all `count` entries of `values` equal `probe`? True for an empty range. */
static bool
model_all_equal(
    const uint8_t* values,
    int count,
    int probe)
{
    for( int i = 0; i < count; i++ )
        if( values[i] != probe )
            return false;
    return true;
}

/**
 * Reconstruct the header from the model alone, for a caller encoding a model it
 * built rather than decoded.
 *
 * Mostly this is "is the array present", but two flags need more care, because two
 * of the decoders normalise on the way in and a naive reading would re-encode
 * something the source never said:
 *
 *  - **Priority.** OB2 always allocates per-face priorities, filling them from the
 *    whole-model priority when the header named one, so presence says nothing. It
 *    does keep the header byte in `model_priority` though, so "every face already
 *    agrees with it" distinguishes the two cases. The other three formats only
 *    allocate the array for the per-face case, where presence is the answer.
 *  - **Alpha.** The decode entry point invents an all-zero alpha array for any
 *    model with face bone weights, so presence over-reports. An all-zero array
 *    alongside bone weights is exactly what that invention produces, so it is
 *    dropped: the decoder puts it back.
 */
static void
model_header_from_model(
    struct model_header* header,
    const struct RSCache_Model* model,
    int format)
{
    memset(header, 0, sizeof(*header));
    header->format = format;
    header->vertex_count = model->vertex_count;
    header->face_count = model->face_count;
    header->textured_face_count = model->textured_face_count;
    header->has_face_info = model->face_infos != NULL;
    /* Derived: no particle or billboard bits, since nothing here can know about them. */
    header->face_info_flags = header->has_face_info;
    header->has_face_labels = model->face_bone_map != NULL;
    header->has_vertex_labels = model->vertex_bone_map != NULL;

    if( !model->face_priorities )
        header->model_priority = model->model_priority;
    else if( format == RSCACHE_MODEL_FORMAT_OB2 &&
             model_all_equal(model->face_priorities, model->face_count, model->model_priority) )
        header->model_priority = model->model_priority;
    else
        header->model_priority = 255;

    header->has_alpha = model->face_alphas != NULL;
    if( header->has_alpha && model->face_bone_map &&
        model_all_equal(model->face_alphas, model->face_count, 0) )
        header->has_alpha = 0;

    if( format == RSCACHE_MODEL_FORMAT_OB3 || format == RSCACHE_MODEL_FORMAT_V3 )
        header->has_face_textures = model->face_textures != NULL;
    if( format == RSCACHE_MODEL_FORMAT_V2 || format == RSCACHE_MODEL_FORMAT_V3 )
        header->has_animaya = model->animaya_group_counts != NULL;
}

/** Can `value` be written as a shortsmart at all? */
static bool
model_smart_fits(int value)
{
    return value >= -16384 && value <= 16383;
}

/**
 * The texture coordinate index for a face whose stored index was erased.
 *
 * OB2 and V2 rewrite a face's coordinate index to -1 when the face's own vertex
 * indices already equal its texture triangle's p/m/n, and drop the whole array if
 * that leaves nothing. Searching the texture triangles for the match inverts it
 * exactly, so a model can be re-encoded without its provenance.
 *
 * Returns the index, or -1 when no triangle matches.
 */
static int
model_recover_texture_coord(
    const struct RSCache_Model* model,
    int face)
{
    for( int k = 0; k < model->textured_face_count; k++ )
    {
        if( model->face_indices_a[face] == (model->textured_p_coordinate[k] & 0xFFFF) &&
            model->face_indices_b[face] == (model->textured_m_coordinate[k] & 0xFFFF) &&
            model->face_indices_c[face] == (model->textured_n_coordinate[k] & 0xFFFF) )
            return k;
    }
    return -1;
}

/**
 * The per-face info byte for OB2/V2: shading in bit 0, "textured" in bit 1, and
 * the texture coordinate index in the top six.
 *
 * Uses the recorded byte when there is one. Without it the byte is rebuilt, which
 * is exact as long as every textured face's coordinate index can be established —
 * either it survived decode or model_recover_texture_coord finds it.
 */
static bool
model_face_info_byte(
    const struct RSCache_Model* model,
    const struct RSCache_ModelProvenance* prov,
    int face,
    uint8_t* out)
{
    if( prov && prov->face_info_bytes && prov->face_count == model->face_count )
    {
        *out = prov->face_info_bytes[face];
        return true;
    }

    int byte = model->face_infos ? (model->face_infos[face] & 1) : 0;
    bool textured = model->face_textures && model->face_textures[face] != -1;

    if( textured )
    {
        int coord = model->face_texture_coords ? model->face_texture_coords[face] : -1;
        if( coord < 0 )
            coord = model_recover_texture_coord(model, face);
        if( coord < 0 || coord > 63 )
            return false;
        byte |= 2 | (coord << 2);
    }

    *out = (uint8_t)byte;
    return true;
}

/** The 16-bit colour as it appeared in the stream. OB2/V2 move a textured face's
 *  colour into face_textures and leave 127 behind, so it has to come back. */
static bool
model_wire_face_colour(
    const struct RSCache_Model* model,
    int face,
    bool textured,
    uint16_t* out)
{
    if( !textured )
    {
        *out = model->face_colors[face];
        return true;
    }
    if( !model->face_textures )
        return false;
    *out = (uint16_t)model->face_textures[face];
    return true;
}

/**
 * Write the per-face index types and their delta stream.
 *
 * This is the inverse of the decoders' four-way reuse machine. Types 2, 3 and 4
 * each assert that two of the face's indices carry over from the previous face in
 * a particular arrangement; the encoder checks that the model's indices actually
 * satisfy the recorded type and fails if they do not, rather than emitting a
 * triangle that would decode to something else.
 */
static bool
model_write_face_indices(
    struct RSCache_Buffer* types_out,
    struct RSCache_Buffer* data_out,
    const struct RSCache_Model* model,
    const struct RSCache_ModelProvenance* prov,
    int format)
{
    const uint8_t* recorded =
        (prov && prov->face_index_types && prov->face_count == model->face_count)
            ? prov->face_index_types
            : NULL;

    int prev1 = 0;
    int prev2 = 0;
    int prev3 = 0;
    int prev3_copy = 0;

    for( int i = 0; i < model->face_count; i++ )
    {
        /* Type 1 spells out all three indices, so it is always legal; it is the
         * fallback when no type was recorded. */
        int type = recorded ? recorded[i] : 1;
        int a = model->face_indices_a[i];
        int b = model->face_indices_b[i];
        int c = model->face_indices_c[i];

        p1(types_out, type);

        /* OB2 bases the first delta on prev3 and the others on prev3_copy. The two
         * are equal after every branch here, but keep the distinction so this stays
         * a mirror of the decoders rather than a claim about them. */
        int base = (format == RSCACHE_MODEL_FORMAT_OB2) ? prev3 : prev3_copy;

        switch( type )
        {
        case 0:
            /* Nothing in the stream and no state change. Never seen in any cache
             * measured, but the decoders accept it, so this does too. */
            break;

        case 1:
        {
            int d1 = a - base;
            int d2 = b - a;
            int d3 = c - b;
            if( !model_smart_fits(d1) || !model_smart_fits(d2) || !model_smart_fits(d3) )
                return false;
            pshortsmart(data_out, d1);
            pshortsmart(data_out, d2);
            pshortsmart(data_out, d3);
            prev1 = a;
            prev2 = b;
            prev3 = c;
            prev3_copy = c;
            break;
        }

        case 2:
        {
            if( a != prev1 || b != prev3 )
                return false;
            int d = c - prev3_copy;
            if( !model_smart_fits(d) )
                return false;
            pshortsmart(data_out, d);
            prev2 = prev3;
            prev3 = c;
            prev3_copy = c;
            break;
        }

        case 3:
        {
            if( a != prev3 || b != prev2 )
                return false;
            int d = c - prev3_copy;
            if( !model_smart_fits(d) )
                return false;
            pshortsmart(data_out, d);
            prev1 = prev3;
            prev3 = c;
            prev3_copy = c;
            break;
        }

        case 4:
        {
            if( a != prev2 || b != prev1 )
                return false;
            int d = c - prev3_copy;
            if( !model_smart_fits(d) )
                return false;
            pshortsmart(data_out, d);
            int swapped = prev1;
            prev1 = prev2;
            prev2 = swapped;
            prev3 = c;
            prev3_copy = c;
            break;
        }

        default:
            return false;
        }
    }

    return true;
}

/** Vertex flag bits and the three delta streams. */
static bool
model_write_vertices(
    struct RSCache_Buffer* flags_out,
    struct RSCache_Buffer* x_out,
    struct RSCache_Buffer* y_out,
    struct RSCache_Buffer* z_out,
    const struct RSCache_Model* model)
{
    int prev_x = 0;
    int prev_y = 0;
    int prev_z = 0;

    for( int i = 0; i < model->vertex_count; i++ )
    {
        int dx = model->vertices_x[i] - prev_x;
        int dy = model->vertices_y[i] - prev_y;
        int dz = model->vertices_z[i] - prev_z;

        if( !model_smart_fits(dx) || !model_smart_fits(dy) || !model_smart_fits(dz) )
            return false;

        /* "A bit per axis with a non-zero delta" reproduces the flag byte exactly:
         * across ~11,000 models in six caches no flag was ever set over a zero
         * delta, and no flag byte ever carried a bit above 0x4. */
        p1(flags_out, (dx ? 1 : 0) | (dy ? 2 : 0) | (dz ? 4 : 0));

        if( dx )
            pshortsmart(x_out, dx);
        if( dy )
            pshortsmart(y_out, dy);
        if( dz )
            pshortsmart(z_out, dz);

        prev_x = model->vertices_x[i];
        prev_y = model->vertices_y[i];
        prev_z = model->vertices_z[i];
    }

    return true;
}

uint32_t
RSCache_ModelEncodeBound(
    const struct RSCache_Model* model,
    const struct RSCache_ModelProvenance* provenance)
{
    if( !model )
        return 0;

    /* Per vertex: a flag byte plus up to three 2-byte deltas, plus a bone byte and
     * an animaya group byte. Per face: an index type, up to three 2-byte index
     * deltas, an info byte, a priority, an alpha, a bone byte, two colour bytes,
     * two texture bytes and a coordinate byte. Per texture triangle: a render type
     * and six mapping bytes. */
    uint32_t bound = 64u;
    bound += (uint32_t)model->vertex_count * 9u;
    bound += (uint32_t)model->face_count * 18u;
    bound += (uint32_t)model->textured_face_count * 8u;

    if( model->animaya_group_counts )
    {
        for( int i = 0; i < model->animaya_vertex_count; i++ )
            bound += 1u + 2u * (uint32_t)model->animaya_group_counts[i];
    }

    if( provenance )
        bound += (uint32_t)provenance->tail_size;

    return bound;
}

uint32_t
RSCache_ModelEncodeFormat(
    const struct RSCache_Model* model,
    const struct RSCache_ModelProvenance* provenance,
    int format,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !model || !out )
        return 0;
    if( format < RSCACHE_MODEL_FORMAT_OB2 || format > RSCACHE_MODEL_FORMAT_V3 )
        return 0;
    if( model->vertex_count < 0 || model->face_count < 0 || model->textured_face_count < 0 )
        return 0;
    if( model->face_count > 0 &&
        (!model->face_indices_a || !model->face_indices_b || !model->face_indices_c ||
         !model->face_colors) )
        return 0;
    if( model->vertex_count > 0 && (!model->vertices_x || !model->vertices_y || !model->vertices_z) )
        return 0;

    /*
     * A provenance is only used when it describes *this* model. Otherwise it is
     * ignored rather than trusted, and the encode falls back to deriving the header
     * — which costs byte-exactness but cannot read past an array.
     */
    const struct RSCache_ModelProvenance* prov = provenance;
    if( prov && (prov->vertex_count != model->vertex_count ||
                 prov->face_count != model->face_count ||
                 prov->textured_face_count != model->textured_face_count) )
        prov = NULL;

    struct model_header header;
    if( !model_header_from_provenance(&header, prov, format) )
    {
        model_header_from_model(&header, model, format);
        /* The header is derived, so the recorded per-face arrays no longer
         * necessarily agree with it. */
        prov = NULL;
    }

    /* Everything the header claims is present has to actually be there. */
    if( header.model_priority == 255 && model->face_count > 0 && !model->face_priorities )
        return 0;
    if( header.has_alpha == 1 && model->face_count > 0 && !model->face_alphas )
        return 0;
    if( header.has_face_labels == 1 && model->face_count > 0 && !model->face_bone_map )
        return 0;
    if( header.has_vertex_labels == 1 && model->vertex_count > 0 && !model->vertex_bone_map )
        return 0;
    if( header.has_animaya == 1 && model->vertex_count > 0 && !model->animaya_group_counts )
        return 0;
    if( header.textured_face_count > 0 &&
        (!model->textured_p_coordinate || !model->textured_m_coordinate ||
         !model->textured_n_coordinate) )
        return 0;

    bool ob3_family = (format == RSCACHE_MODEL_FORMAT_OB3 || format == RSCACHE_MODEL_FORMAT_V3);

    if( ob3_family )
    {
        if( header.has_face_info == 1 && model->face_count > 0 && !model->face_infos )
            return 0;
        if( header.has_face_textures == 1 && model->face_count > 0 && !model->face_textures )
            return 0;
        /* The mapping section is laid out by render type, so it has to be known. */
        if( header.textured_face_count > 0 && !model->texture_render_types )
            return 0;
    }

    struct RSCache_Buffer sec[SEC_COUNT];
    memset(sec, 0, sizeof(sec));
    bool ok = true;
    for( int i = 0; i < SEC_COUNT && ok; i++ )
        ok = RSCache_BufferInitAlloc(&sec[i], 64);

    if( ok )
        ok = model_write_vertices(
            &sec[SEC_VERTEX_FLAGS], &sec[SEC_VERTEX_X], &sec[SEC_VERTEX_Y], &sec[SEC_VERTEX_Z],
            model);

    if( ok )
        ok = model_write_face_indices(
            &sec[SEC_FACE_INDEX_TYPES], &sec[SEC_FACE_INDEX_DATA], model, prov, format);

    /* Texture render types lead the OB3/V3 body. */
    if( ok && ob3_family )
    {
        for( int i = 0; i < header.textured_face_count; i++ )
            p1(&sec[SEC_RENDER_TYPES], model->texture_render_types[i]);
    }

    /* The per-face columns. */
    for( int i = 0; ok && i < model->face_count; i++ )
    {
        bool textured = false;

        if( ob3_family )
        {
            if( header.has_face_info == 1 )
                p1(&sec[SEC_FACE_INFOS], model->face_infos[i]);

            p2(&sec[SEC_FACE_COLORS], model->face_colors[i]);

            if( header.has_face_textures == 1 )
                p2(&sec[SEC_FACE_TEXTURES], (model->face_textures[i] + 1) & 0xFFFF);

            /* Mirror the decoder's gate exactly: a coordinate byte exists only when
             * the array does *and* the face carries a texture. */
            if( model->face_texture_coords && model->face_textures &&
                model->face_textures[i] != -1 )
                p1(&sec[SEC_TEXTURE_COORDS], (model->face_texture_coords[i] + 1) & 0xFF);
        }
        else
        {
            if( header.has_face_info == 1 )
            {
                uint8_t info = 0;
                if( !model_face_info_byte(model, prov, i, &info) )
                {
                    ok = false;
                    break;
                }
                p1(&sec[SEC_FACE_INFOS], info);
                textured = (info & 2) != 0;
            }

            uint16_t colour = 0;
            if( !model_wire_face_colour(model, i, textured, &colour) )
            {
                ok = false;
                break;
            }
            p2(&sec[SEC_FACE_COLORS], colour);
        }

        if( header.model_priority == 255 )
            p1(&sec[SEC_FACE_PRIORITIES], model->face_priorities[i]);
        if( header.has_alpha == 1 )
            p1(&sec[SEC_FACE_ALPHAS], model->face_alphas[i]);
        if( header.has_face_labels == 1 )
            p1(&sec[SEC_FACE_SKINS], model->face_bone_map[i]);
    }

    /* Vertex bone weights, and after them the animaya skin block — one section,
     * because the decoders read both through a single cursor. */
    if( ok && header.has_vertex_labels == 1 )
    {
        for( int i = 0; i < model->vertex_count; i++ )
            p1(&sec[SEC_VERTEX_SKINS], model->vertex_bone_map[i]);
    }
    if( ok && header.has_animaya == 1 )
    {
        for( int i = 0; i < model->animaya_vertex_count; i++ )
        {
            int count = model->animaya_group_counts[i];
            p1(&sec[SEC_VERTEX_SKINS], count);
            for( int j = 0; j < count; j++ )
            {
                if( !model->animaya_groups || !model->animaya_groups[i] || !model->animaya_scales ||
                    !model->animaya_scales[i] )
                {
                    ok = false;
                    break;
                }
                p1(&sec[SEC_VERTEX_SKINS], model->animaya_groups[i][j]);
                p1(&sec[SEC_VERTEX_SKINS], model->animaya_scales[i][j]);
            }
        }
    }

    /* Texture triangle mapping. OB2/V2 store a p/m/n triple for every triangle;
     * OB3/V3 store one only for the simple (render type 0) triangles, and keep the
     * complex and cube payloads in the tail. */
    if( ok )
    {
        for( int i = 0; i < header.textured_face_count; i++ )
        {
            if( ob3_family && model->texture_render_types[i] != 0 )
                continue;
            p2(&sec[SEC_TEXTURE_MAPPING], model->textured_p_coordinate[i]);
            p2(&sec[SEC_TEXTURE_MAPPING], model->textured_m_coordinate[i]);
            p2(&sec[SEC_TEXTURE_MAPPING], model->textured_n_coordinate[i]);
        }
    }

    if( ok && (format == RSCACHE_MODEL_FORMAT_V2) )
    {
        /* V2: faceZOffsets first, then any unexplained bytes after it. */
        if( model->face_z_offsets )
        {
            p1(&sec[SEC_TAIL], 1);
            for( int i = 0; i < model->face_count; i++ )
                p1(&sec[SEC_TAIL], (uint8_t)model->face_z_offsets[i]);
        }
        else
        {
            p1(&sec[SEC_TAIL], 0);
        }
        if( prov && prov->tail && prov->tail_size > 0 )
        {
            for( int i = 0; i < prov->tail_size; i++ )
                p1(&sec[SEC_TAIL], prov->tail[i]);
        }
    }
    else if( ok && format == RSCACHE_MODEL_FORMAT_V3 )
    {
        /* V3: provenance/complex+trailingFlag, then faceZOffsets. */
        if( prov && prov->tail && prov->tail_size > 0 )
        {
            for( int i = 0; i < prov->tail_size; i++ )
                p1(&sec[SEC_TAIL], prov->tail[i]);
        }
        else if( !prov )
        {
            p1(&sec[SEC_TAIL], 0); /* trailingFlag */
        }
        if( model->face_z_offsets )
        {
            p1(&sec[SEC_TAIL], 1);
            for( int i = 0; i < model->face_count; i++ )
                p1(&sec[SEC_TAIL], (uint8_t)model->face_z_offsets[i]);
        }
        else
        {
            p1(&sec[SEC_TAIL], 0);
        }
    }
    else if( ok && prov && prov->tail && prov->tail_size > 0 )
    {
        for( int i = 0; i < prov->tail_size; i++ )
            p1(&sec[SEC_TAIL], prov->tail[i]);
    }
    else if( ok && !prov && format == RSCACHE_MODEL_FORMAT_OB3 )
    {
        /*
         * Not cosmetic: decode_ob3 reads a byte here without checking it is still
         * inside the file, so a body that stopped at the trailer would have it
         * read the vertex count as a flag and, if non-zero, ten bytes past the end.
         */
        p1(&sec[SEC_TAIL], 0);
        p1(&sec[SEC_TAIL], 0);
    }

    /* Assemble. */
    uint32_t written = 0;
    if( ok )
    {
        const int* order = ob3_family ? MODEL_ORDER_OB3_V3 : MODEL_ORDER_OB2_V2;

        uint32_t body = 0;
        for( int i = 0; order[i] >= 0; i++ )
            body += sec[order[i]].position;

        uint32_t trailer = 5u + (uint32_t)model_header_flag_count(format);
        trailer += (format == RSCACHE_MODEL_FORMAT_OB2) ? 8u : 10u;
        if( format == RSCACHE_MODEL_FORMAT_V3 )
            trailer += 2u; /* the sixth byte count */
        if( format != RSCACHE_MODEL_FORMAT_OB2 )
            trailer += 2u; /* magic */

        if( body + trailer > out_capacity )
        {
            ok = false;
        }
        else
        {
            struct RSCache_Buffer buffer;
            RSCache_BufferInit(&buffer, out, out_capacity);

            for( int i = 0; order[i] >= 0; i++ )
            {
                struct RSCache_Buffer* s = &sec[order[i]];
                for( uint32_t k = 0; k < s->position; k++ )
                    p1(&buffer, s->data[k]);
            }

            p2(&buffer, header.vertex_count);
            p2(&buffer, header.face_count);
            p1(&buffer, header.textured_face_count);
            p1(&buffer, header.face_info_flags);
            p1(&buffer, header.model_priority);
            p1(&buffer, header.has_alpha);
            p1(&buffer, header.has_face_labels);
            if( ob3_family )
                p1(&buffer, header.has_face_textures);
            p1(&buffer, header.has_vertex_labels);
            if( format == RSCACHE_MODEL_FORMAT_V2 || format == RSCACHE_MODEL_FORMAT_V3 )
                p1(&buffer, header.has_animaya);

            p2(&buffer, (int)sec[SEC_VERTEX_X].position);
            p2(&buffer, (int)sec[SEC_VERTEX_Y].position);
            p2(&buffer, (int)sec[SEC_VERTEX_Z].position);
            p2(&buffer, (int)sec[SEC_FACE_INDEX_DATA].position);

            /* The fifth byte count names a different section per format: the
             * texture coordinate block for OB3/V3, the skin block for V2. V3 has
             * both, coordinates first. */
            if( ob3_family )
                p2(&buffer, (int)sec[SEC_TEXTURE_COORDS].position);
            if( format == RSCACHE_MODEL_FORMAT_V2 || format == RSCACHE_MODEL_FORMAT_V3 )
                p2(&buffer, (int)sec[SEC_VERTEX_SKINS].position);

            switch( format )
            {
            case RSCACHE_MODEL_FORMAT_OB3:
                p1(&buffer, 0xFF);
                p1(&buffer, 0xFF);
                break;
            case RSCACHE_MODEL_FORMAT_V2:
                p1(&buffer, 0xFF);
                p1(&buffer, 0xFE);
                break;
            case RSCACHE_MODEL_FORMAT_V3:
                p1(&buffer, 0xFF);
                p1(&buffer, 0xFD);
                break;
            default:
                break;
            }

            written = buffer.position;
        }
    }

    for( int i = 0; i < SEC_COUNT; i++ )
        RSCache_BufferRelease(&sec[i]);

    return ok ? written : 0;
}

int
RSCache_ModelCodecVersion(const struct RSCache* cache)
{
    if( !cache )
        return RSCACHE_MODEL_FORMAT_OB2;

    /* Stored as format + 1 so that slot 0 keeps meaning RSCACHE_CODEC_AUTO. */
    if( cache->codec[RSCACHE_TYPE_MODEL] != RSCACHE_CODEC_AUTO )
        return cache->codec[RSCACHE_TYPE_MODEL] - 1;

    if( RSCache_IsDat1(cache) )
        return RSCACHE_MODEL_FORMAT_OB2;
    if( RSCache_IsRs2Dat2(cache) )
        return RSCACHE_MODEL_FORMAT_OB3;

    /*
     * Only a default for *authoring*, and a weak one: the format is stamped on each
     * file, and real caches mix them. osrs184 and kronos are 98% OB2 with a handful
     * of OB3; osrs230 and osrs239 are 89% V2 and 11% V3. So the choice below is the
     * dominant format of the era, not a rule the cache obeys.
     */
    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_MODEL, 220, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        return RSCACHE_MODEL_FORMAT_V2;

    return RSCACHE_MODEL_FORMAT_OB2;
}

uint32_t
RSCache_ModelEncode(
    const struct RSCache* cache,
    const struct RSCache_Model* model,
    const struct RSCache_ModelProvenance* provenance,
    uint8_t* out,
    uint32_t out_capacity)
{
    /* A provenance names the format the source used, which beats any guess from the
     * profile — the format travels with the file, not with the revision. */
    int format = provenance && provenance->header_flag_count > 0
                     ? provenance->format
                     : RSCache_ModelCodecVersion(cache);

    return RSCache_ModelEncodeFormat(model, provenance, format, out, out_capacity);
}

struct RSCache_Model*
RSCache_ModelNewCopy(struct RSCache_Model* model)
{
    struct RSCache_Model* copy = (struct RSCache_Model*)malloc(sizeof(struct RSCache_Model));
    memset(copy, 0, sizeof(struct RSCache_Model));

    /* Not geometry, but load-bearing on it: the ToriRS adaptor keys the reference's
     * version-13+ scaleDown on this, and the dat2 model task adapts a COPY — dropping it
     * here silently un-scales every 643 model. */
    copy->format_version = model->format_version;

    copy->vertex_count = model->vertex_count;
    copy->vertices_x = (int*)malloc(model->vertex_count * sizeof(int));
    copy->vertices_y = (int*)malloc(model->vertex_count * sizeof(int));
    copy->vertices_z = (int*)malloc(model->vertex_count * sizeof(int));

    memcpy(copy->vertices_x, model->vertices_x, model->vertex_count * sizeof(int));
    memcpy(copy->vertices_y, model->vertices_y, model->vertex_count * sizeof(int));
    memcpy(copy->vertices_z, model->vertices_z, model->vertex_count * sizeof(int));

    if( model->vertex_bone_map )
    {
        copy->vertex_bone_map = (uint8_t*)malloc((size_t)model->vertex_count * sizeof(uint8_t));
        memcpy(
            copy->vertex_bone_map,
            model->vertex_bone_map,
            (size_t)model->vertex_count * sizeof(uint8_t));
    }
    if( model->face_bone_map )
    {
        copy->face_bone_map = (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
        memcpy(
            copy->face_bone_map, model->face_bone_map, (size_t)model->face_count * sizeof(uint8_t));
    }

    copy->face_count = model->face_count;
    if( model->face_indices_a )
    {
        copy->face_indices_a = (int*)malloc(model->face_count * sizeof(int));

        memcpy(copy->face_indices_a, model->face_indices_a, model->face_count * sizeof(int));
    }

    if( model->face_indices_b )
    {
        copy->face_indices_b = (int*)malloc(model->face_count * sizeof(int));
        memcpy(copy->face_indices_b, model->face_indices_b, model->face_count * sizeof(int));
    }

    if( model->face_indices_c )
    {
        copy->face_indices_c = (int*)malloc(model->face_count * sizeof(int));
        memcpy(copy->face_indices_c, model->face_indices_c, model->face_count * sizeof(int));
    }

    if( model->face_alphas )
    {
        copy->face_alphas = (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
        memcpy(copy->face_alphas, model->face_alphas, (size_t)model->face_count * sizeof(uint8_t));
    }

    if( model->face_infos )
    {
        copy->face_infos = (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
        memcpy(copy->face_infos, model->face_infos, (size_t)model->face_count * sizeof(uint8_t));
    }

    if( model->face_priorities )
    {
        copy->face_priorities = (uint8_t*)malloc((size_t)model->face_count * sizeof(uint8_t));
        memcpy(
            copy->face_priorities,
            model->face_priorities,
            (size_t)model->face_count * sizeof(uint8_t));
    }

    if( model->face_colors )
    {
        copy->face_colors = (uint16_t*)malloc((size_t)model->face_count * sizeof(uint16_t));
        memcpy(copy->face_colors, model->face_colors, (size_t)model->face_count * sizeof(uint16_t));
    }

    copy->model_priority = model->model_priority;
    copy->textured_face_count = model->textured_face_count;

    if( model->textured_p_coordinate )
    {
        copy->textured_p_coordinate =
            (uint16_t*)malloc((size_t)model->textured_face_count * sizeof(uint16_t));
        memcpy(
            copy->textured_p_coordinate,
            model->textured_p_coordinate,
            (size_t)model->textured_face_count * sizeof(uint16_t));
    }

    if( model->textured_m_coordinate )
    {
        copy->textured_m_coordinate =
            (uint16_t*)malloc((size_t)model->textured_face_count * sizeof(uint16_t));
        memcpy(
            copy->textured_m_coordinate,
            model->textured_m_coordinate,
            (size_t)model->textured_face_count * sizeof(uint16_t));
    }

    if( model->textured_n_coordinate )
    {
        copy->textured_n_coordinate =
            (uint16_t*)malloc((size_t)model->textured_face_count * sizeof(uint16_t));

        memcpy(
            copy->textured_n_coordinate,
            model->textured_n_coordinate,
            (size_t)model->textured_face_count * sizeof(uint16_t));
    }

    if( model->face_textures )
    {
        copy->face_textures = (int16_t*)malloc((size_t)model->face_count * sizeof(int16_t));
        memcpy(
            copy->face_textures, model->face_textures, (size_t)model->face_count * sizeof(int16_t));
    }

    if( model->face_texture_coords )
    {
        copy->face_texture_coords = (int16_t*)malloc((size_t)model->face_count * sizeof(int16_t));
        memcpy(
            copy->face_texture_coords,
            model->face_texture_coords,
            (size_t)model->face_count * sizeof(int16_t));
    }

    if( model->texture_render_types )
    {
        copy->texture_render_types =
            (unsigned char*)malloc(model->textured_face_count * sizeof(unsigned char));
        memcpy(
            copy->texture_render_types,
            model->texture_render_types,
            model->textured_face_count * sizeof(unsigned char));
    }

    if( model->face_z_offsets )
    {
        copy->face_z_offsets = (int8_t*)malloc((size_t)model->face_count);
        if( copy->face_z_offsets )
            memcpy(copy->face_z_offsets, model->face_z_offsets, (size_t)model->face_count);
    }

    copy->rotated = model->rotated;

    if( model->animaya_vertex_count > 0 && model->animaya_group_counts && model->animaya_groups &&
        model->animaya_scales )
    {
        int vc = model->animaya_vertex_count;
        copy->animaya_vertex_count = vc;
        copy->animaya_group_counts = (uint8_t*)malloc((size_t)vc * sizeof(uint8_t));
        copy->animaya_groups = (uint8_t**)calloc((size_t)vc, sizeof(uint8_t*));
        copy->animaya_scales = (uint8_t**)calloc((size_t)vc, sizeof(uint8_t*));
        if( copy->animaya_group_counts && copy->animaya_groups && copy->animaya_scales )
        {
            memcpy(copy->animaya_group_counts, model->animaya_group_counts, (size_t)vc);
            for( int i = 0; i < vc; i++ )
            {
                int cnt = (int)model->animaya_group_counts[i];
                if( cnt <= 0 )
                    continue;
                copy->animaya_groups[i] = (uint8_t*)malloc((size_t)cnt);
                copy->animaya_scales[i] = (uint8_t*)malloc((size_t)cnt);
                if( copy->animaya_groups[i] && model->animaya_groups[i] )
                    memcpy(copy->animaya_groups[i], model->animaya_groups[i], (size_t)cnt);
                if( copy->animaya_scales[i] && model->animaya_scales[i] )
                    memcpy(copy->animaya_scales[i], model->animaya_scales[i], (size_t)cnt);
            }
        }
    }

    return copy;
}

static int
copy_vertex(
    struct RSCache_Model* model,
    struct RSCache_Model* other,
    int vertex_index)
{
    int new_vertex_count = -1;
    int vert_x = other->vertices_x[vertex_index];
    int vert_y = other->vertices_y[vertex_index];
    int vert_z = other->vertices_z[vertex_index];

    for( int i = 0; i < model->vertex_count; i++ )
    {
        if( vert_x == model->vertices_x[i] && vert_y == model->vertices_y[i] &&
            vert_z == model->vertices_z[i] )
        {
            new_vertex_count = i;
            break;
        }
    }

    if( new_vertex_count == -1 )
    {
        new_vertex_count = model->vertex_count;
        model->vertices_x[new_vertex_count] = vert_x;
        model->vertices_y[new_vertex_count] = vert_y;
        model->vertices_z[new_vertex_count] = vert_z;

        // Vertex skins (animaya)
        if( model->animaya_group_counts && other->animaya_group_counts && other->animaya_groups &&
            other->animaya_scales )
        {
            int cnt = (int)other->animaya_group_counts[vertex_index];
            model->animaya_group_counts[new_vertex_count] = (uint8_t)cnt;
            if( cnt > 0 )
            {
                model->animaya_groups[new_vertex_count] = (uint8_t*)malloc((size_t)cnt);
                model->animaya_scales[new_vertex_count] = (uint8_t*)malloc((size_t)cnt);
                if( model->animaya_groups[new_vertex_count] && other->animaya_groups[vertex_index] )
                    memcpy(
                        model->animaya_groups[new_vertex_count],
                        other->animaya_groups[vertex_index],
                        (size_t)cnt);
                if( model->animaya_scales[new_vertex_count] && other->animaya_scales[vertex_index] )
                    memcpy(
                        model->animaya_scales[new_vertex_count],
                        other->animaya_scales[vertex_index],
                        (size_t)cnt);
            }
        }

        if( model->vertex_bone_map && other->vertex_bone_map )
        {
            model->vertex_bone_map[new_vertex_count] = other->vertex_bone_map[vertex_index];
        }

        new_vertex_count = model->vertex_count++;
    }

    return new_vertex_count;
}

struct RSCache_Model*
RSCache_ModelNewMerge(
    struct RSCache_Model** models,
    int model_count)
{
    struct RSCache_Model* model = (struct RSCache_Model*)malloc(sizeof(struct RSCache_Model));
    memset(model, 0, sizeof(struct RSCache_Model));

    /* All parts of a merge come from one era; carry the version so a later adaptation
     * still applies the version-13+ scaleDown (see RSCache_ModelNewCopy). */
    if( model_count > 0 && models[0] )
        model->format_version = models[0]->format_version;

    int vertex_count = 0;
    int face_count = 0;
    int textured_face_count = 0;

    bool has_face_render_prios = false;
    bool has_face_render_infos = false;
    bool has_face_render_colors = false;
    bool has_face_render_alphas = false;
    bool has_face_render_textures = false;
    bool has_vertex_bones = false;
    bool has_face_bones = false;
    bool has_animaya = false;

    for( int i = 0; i < model_count; i++ )
    {
        vertex_count += models[i]->vertex_count;
        face_count += models[i]->face_count;
        textured_face_count += models[i]->textured_face_count;

        if( models[i]->face_priorities || models[i]->model_priority )
            has_face_render_prios = true;

        if( models[i]->face_infos )
            has_face_render_infos = true;

        if( models[i]->face_colors )
            has_face_render_colors = true;

        if( models[i]->face_alphas )
            has_face_render_alphas = true;

        if( models[i]->face_textures )
            has_face_render_textures = true;

        if( models[i]->vertex_bone_map )
            has_vertex_bones = true;

        if( models[i]->face_bone_map )
            has_face_bones = true;

        if( models[i]->animaya_group_counts && models[i]->animaya_groups &&
            models[i]->animaya_scales )
            has_animaya = true;
    }

    int* vertices_x = (int*)malloc(vertex_count * sizeof(int));
    int* vertices_y = (int*)malloc(vertex_count * sizeof(int));
    int* vertices_z = (int*)malloc(vertex_count * sizeof(int));
    memset(vertices_x, 0, vertex_count * sizeof(int));
    memset(vertices_y, 0, vertex_count * sizeof(int));
    memset(vertices_z, 0, vertex_count * sizeof(int));

    int* face_indices_a = (int*)malloc(face_count * sizeof(int));
    int* face_indices_b = (int*)malloc(face_count * sizeof(int));
    int* face_indices_c = (int*)malloc(face_count * sizeof(int));
    memset(face_indices_a, 0, face_count * sizeof(int));
    memset(face_indices_b, 0, face_count * sizeof(int));
    memset(face_indices_c, 0, face_count * sizeof(int));

    uint8_t* face_alphas = NULL;
    if( has_face_render_alphas )
    {
        face_alphas = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
        memset(face_alphas, 0, (size_t)face_count * sizeof(uint8_t));
    }

    uint8_t* face_infos = NULL;
    if( has_face_render_infos )
    {
        face_infos = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
        memset(face_infos, 0, (size_t)face_count * sizeof(uint8_t));
    }

    uint8_t* face_priorities = NULL;
    if( has_face_render_prios )
    {
        face_priorities = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
        memset(face_priorities, 0, (size_t)face_count * sizeof(uint8_t));
    }

    uint16_t* face_colors = NULL;
    if( has_face_render_colors )
    {
        face_colors = (uint16_t*)malloc((size_t)face_count * sizeof(uint16_t));
        memset(face_colors, 0, (size_t)face_count * sizeof(uint16_t));
    }

    uint16_t* textured_p_coordinate = NULL;
    if( has_face_render_textures )
    {
        textured_p_coordinate = (uint16_t*)malloc((size_t)textured_face_count * sizeof(uint16_t));
        memset(textured_p_coordinate, 0, (size_t)textured_face_count * sizeof(uint16_t));
    }

    uint16_t* textured_m_coordinate = NULL;
    if( has_face_render_textures )
    {
        textured_m_coordinate = (uint16_t*)malloc((size_t)textured_face_count * sizeof(uint16_t));
        memset(textured_m_coordinate, 0, (size_t)textured_face_count * sizeof(uint16_t));
    }

    uint16_t* textured_n_coordinate = NULL;
    if( has_face_render_textures )
    {
        textured_n_coordinate = (uint16_t*)malloc((size_t)textured_face_count * sizeof(uint16_t));
        memset(textured_n_coordinate, 0, (size_t)textured_face_count * sizeof(uint16_t));
    }

    uint8_t* vertex_bone_map = NULL;
    if( has_vertex_bones )
    {
        vertex_bone_map = (uint8_t*)malloc((size_t)vertex_count * sizeof(uint8_t));
        memset(vertex_bone_map, 0, (size_t)vertex_count * sizeof(uint8_t));
    }

    uint8_t* face_bone_map = NULL;
    if( has_face_bones )
    {
        face_bone_map = (uint8_t*)malloc((size_t)face_count * sizeof(uint8_t));
        memset(face_bone_map, 0, (size_t)face_count * sizeof(uint8_t));
    }

    uint8_t* animaya_group_counts = NULL;
    uint8_t** animaya_groups = NULL;
    uint8_t** animaya_scales = NULL;
    if( has_animaya )
    {
        animaya_group_counts = (uint8_t*)calloc((size_t)vertex_count, sizeof(uint8_t));
        animaya_groups = (uint8_t**)calloc((size_t)vertex_count, sizeof(uint8_t*));
        animaya_scales = (uint8_t**)calloc((size_t)vertex_count, sizeof(uint8_t*));
    }

    int16_t* face_textures = NULL;
    int16_t* face_texture_coords = NULL;
    if( has_face_render_textures )
    {
        face_textures = (int16_t*)malloc((size_t)face_count * sizeof(int16_t));
        face_texture_coords = (int16_t*)malloc((size_t)face_count * sizeof(int16_t));

        memset(face_textures, 0, (size_t)face_count * sizeof(int16_t));
        memset(face_texture_coords, 0, (size_t)face_count * sizeof(int16_t));
    }

    unsigned char* texture_render_types = NULL;
    if( has_face_render_textures )
    {
        texture_render_types = (unsigned char*)malloc(textured_face_count * sizeof(unsigned char));
        memset(texture_render_types, 0, textured_face_count * sizeof(unsigned char));
    }

    // model->vertex_count = vertex_count;
    // model->face_count = face_count;
    // model->textured_face_count = textured_face_count;

    model->texture_render_types = texture_render_types;

    model->vertices_x = vertices_x;
    model->vertices_y = vertices_y;
    model->vertices_z = vertices_z;

    model->face_indices_a = face_indices_a;
    model->face_indices_b = face_indices_b;
    model->face_indices_c = face_indices_c;

    model->face_alphas = face_alphas;
    model->face_infos = face_infos;
    model->face_priorities = face_priorities;

    model->face_colors = face_colors;

    model->textured_p_coordinate = textured_p_coordinate;
    model->textured_m_coordinate = textured_m_coordinate;
    model->textured_n_coordinate = textured_n_coordinate;

    model->face_textures = face_textures;
    model->face_texture_coords = face_texture_coords;

    model->vertex_bone_map = vertex_bone_map;
    model->face_bone_map = face_bone_map;

    if( has_animaya )
    {
        model->animaya_vertex_count = vertex_count;
        model->animaya_group_counts = animaya_group_counts;
        model->animaya_groups = animaya_groups;
        model->animaya_scales = animaya_scales;
    }

    for( int i = 0; i < model_count; i++ )
    {
        int face_count = models[i]->face_count;
        for( int j = 0; j < face_count; j++ )
        {
            if( face_alphas && models[i]->face_alphas )
                model->face_alphas[model->face_count] = models[i]->face_alphas[j];

            if( face_infos && models[i]->face_infos )
                model->face_infos[model->face_count] = models[i]->face_infos[j];

            if( face_priorities )
            {
                if( models[i]->face_priorities )
                    model->face_priorities[model->face_count] = models[i]->face_priorities[j];
                else if( models[i]->model_priority )
                    model->face_priorities[model->face_count] = models[i]->model_priority;
            }

            if( face_colors && models[i]->face_colors )
                model->face_colors[model->face_count] = models[i]->face_colors[j];

            if( face_alphas && models[i]->face_alphas )
                model->face_alphas[model->face_count] = models[i]->face_alphas[j];

            if( face_bone_map )
            {
                assert(models[i]->face_count > j);
                if( models[i]->face_bone_map )
                    model->face_bone_map[model->face_count] = models[i]->face_bone_map[j];
                else
                    model->face_bone_map[model->face_count] = (uint8_t)255;
            }

            int index_a = copy_vertex(model, models[i], models[i]->face_indices_a[j]);
            int index_b = copy_vertex(model, models[i], models[i]->face_indices_b[j]);
            int index_c = copy_vertex(model, models[i], models[i]->face_indices_c[j]);

            model->face_indices_a[model->face_count] = index_a;
            model->face_indices_b[model->face_count] = index_b;
            model->face_indices_c[model->face_count] = index_c;

            if( has_face_render_textures )
            {
                if( models[i]->face_texture_coords )
                {
                    model->face_texture_coords[model->face_count] =
                        models[i]->face_texture_coords[j];
                }
                else
                {
                    model->face_texture_coords[model->face_count] = -1;
                }

                if( models[i]->face_textures )
                {
                    model->face_textures[model->face_count] = models[i]->face_textures[j];
                }
                else
                {
                    model->face_textures[model->face_count] = -1;
                }
            }

            model->face_count++;
        }

        for( int j = 0; j < models[i]->textured_face_count; j++ )
        {
            int vertex_index_p = 0;
            int vertex_index_m = 0;
            int vertex_index_n = 0;

            if( textured_p_coordinate && models[i]->textured_p_coordinate )
                vertex_index_p =
                    copy_vertex(model, models[i], (int)models[i]->textured_p_coordinate[j]);

            if( textured_m_coordinate && models[i]->textured_m_coordinate )
                vertex_index_m =
                    copy_vertex(model, models[i], (int)models[i]->textured_m_coordinate[j]);

            if( textured_n_coordinate && models[i]->textured_n_coordinate )
                vertex_index_n =
                    copy_vertex(model, models[i], (int)models[i]->textured_n_coordinate[j]);

            assert(vertex_index_p > -1);
            assert(vertex_index_m > -1);
            assert(vertex_index_n > -1);

            model->textured_p_coordinate[model->textured_face_count] = vertex_index_p;
            model->textured_m_coordinate[model->textured_face_count] = vertex_index_m;
            model->textured_n_coordinate[model->textured_face_count] = vertex_index_n;

            model->face_texture_coords[model->textured_face_count] =
                models[i]->face_texture_coords[j];

            model->textured_face_count++;
        }
    }

    model_assert_texture_invariant(model);
    return model;
}

void
RSCache_ModelBonesFree(struct RSCache_ModelBones* modelbones)
{
    if( !modelbones )
        return;

    for( int i = 0; i < modelbones->bones_count; i++ )
        free(modelbones->bones[i]);
    free(modelbones->bones);
    free(modelbones->bones_sizes);

    free(modelbones);
}

void
RSCache_ModelFree(struct RSCache_Model* model)
{
    if( !model )
        return;

    if( model->face_bone_map )
        free(model->face_bone_map);

    if( model->vertices_x )
        free(model->vertices_x);
    if( model->vertices_y )
        free(model->vertices_y);
    if( model->vertices_z )
        free(model->vertices_z);
    if( model->vertex_bone_map )
        free(model->vertex_bone_map);
    if( model->face_indices_a )
        free(model->face_indices_a);
    if( model->face_indices_b )
        free(model->face_indices_b);
    if( model->face_indices_c )
        free(model->face_indices_c);
    if( model->face_colors )
        free(model->face_colors);
    if( model->face_priorities )
        free(model->face_priorities);
    if( model->face_alphas )
        free(model->face_alphas);
    if( model->face_infos )
        free(model->face_infos);
    if( model->textured_p_coordinate )
        free(model->textured_p_coordinate);
    if( model->textured_m_coordinate )
        free(model->textured_m_coordinate);
    if( model->textured_n_coordinate )
        free(model->textured_n_coordinate);
    if( model->face_texture_coords )
        free(model->face_texture_coords);
    if( model->texture_render_types )
        free(model->texture_render_types);
    if( model->face_textures )
        free(model->face_textures);

    if( model->face_z_offsets )
        free(model->face_z_offsets);

    if( model->animaya_groups )
    {
        for( int i = 0; i < model->animaya_vertex_count; i++ )
            free(model->animaya_groups[i]);
        free(model->animaya_groups);
    }
    if( model->animaya_scales )
    {
        for( int i = 0; i < model->animaya_vertex_count; i++ )
            free(model->animaya_scales[i]);
        free(model->animaya_scales);
    }
    free(model->animaya_group_counts);

    free(model);
}