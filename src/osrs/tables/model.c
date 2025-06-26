#include "model.h"

#include "osrs/cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper function to read a byte from the buffer
static unsigned char
read_byte(const unsigned char* buffer, int* offset)
{
    return buffer[(*offset)++] & 0xFF;
}

// Helper function to read a short from the buffer
static short
read_short(const unsigned char* buffer, int* offset)
{
    short value = (buffer[*offset] << 8) | buffer[*offset + 1];
    *offset += 2;
    return value;
}

// Helper function to read an unsigned short from the buffer
static unsigned short
read_unsigned_short(const unsigned char* buffer, int* offset)
{
    unsigned short value = (buffer[*offset] << 8) | buffer[*offset + 1];
    *offset += 2;
    return value & 0xFFFF;
}

// Helper function to read a smart short (variable length)
static int
read_short_smart(const unsigned char* buffer, int* offset)
{
    int value = buffer[*offset] & 0xFF;
    if( value < 128 )
    {
        (*offset)++;
        return value - 64;
    }
    else
    {
        unsigned short ushort_value = read_unsigned_short(buffer, offset);
        return (int)(ushort_value - 0xC000);
    }
}

struct ModelBones*
modelbones_new_decode(int* vertex_bone_map, int vertex_bone_map_count)
{
    struct ModelBones* bones = (struct ModelBones*)malloc(sizeof(struct ModelBones));
    if( !bones )
        return NULL;

    // int[] groupCounts = new int[256];
    // int numGroups = 0;
    // int var3, var4;

    // for( var3 = 0; var3 < this.vertexCount; ++var3 )
    // {
    //     var4 = this.packedVertexGroups[var3];
    //     ++groupCounts[var4];
    //     if( var4 > numGroups )
    //     {
    //         numGroups = var4;
    //     }
    // }

    // this.vertexGroups = new int[numGroups + 1][];

    // for( var3 = 0; var3 <= numGroups; ++var3 )
    // {
    //     this.vertexGroups[var3] = new int[groupCounts[var3]];
    //     groupCounts[var3] = 0;
    // }

    // for( var3 = 0; var3 < this.vertexCount; this.vertexGroups[var4][groupCounts[var4]++] = var3++
    // )
    // {
    //     var4 = this.packedVertexGroups[var3];
    // }

    // Initialize group counts array
    int bone_counts[256] = { 0 };
    int num_bones = 0;

    // Count occurrences of each group and find max group number
    for( int i = 0; i < vertex_bone_map_count; i++ )
    {
        int bone = vertex_bone_map[i];
        bone_counts[bone]++;
        if( bone > num_bones )
            num_bones = bone;
    }

    // Allocate arrays
    bones->bones_count = num_bones + 1;
    bones->bones = (int**)malloc((num_bones + 1) * sizeof(int*));
    bones->bones_sizes = (int*)malloc((num_bones + 1) * sizeof(int));

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
        bones->bones[i] = (int*)malloc(bone_counts[i] * sizeof(int));
        bones->bones_sizes[i] = 0;
    }

    // Fill the groups
    for( int i = 0; i < vertex_bone_map_count; i++ )
    {
        int bone = vertex_bone_map[i];
        bones->bones[bone][bones->bones_sizes[bone]++] = i;
    }

    return bones;
}

struct Model*
decodeOldFormat(const unsigned char* inputData, int inputLength)
{
    struct Model* model = (struct Model*)malloc(sizeof(struct Model));
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
    int offset = inputLength - 18;
    int vertexCount = read_unsigned_short(inputData, &offset);
    int faceCount = read_unsigned_short(inputData, &offset);
    int textureCount = read_byte(inputData, &offset);
    int isTextured = read_byte(inputData, &offset);
    int faceRenderPriority = read_byte(inputData, &offset);
    int hasFaceTransparencies = read_byte(inputData, &offset);
    int hasPackedTransparencyVertexGroups = read_byte(inputData, &offset);
    int hasPackedVertexGroups = read_byte(inputData, &offset);
    int vertexXDataByteCount = read_unsigned_short(inputData, &offset);
    int vertexYDataByteCount = read_unsigned_short(inputData, &offset);
    int vertexZDataByteCount = read_unsigned_short(inputData, &offset);
    int faceIndexDataByteCount = read_unsigned_short(inputData, &offset);

    // Calculate offsets for different data sections
    int offsetOfVertexFlags = 0;
    int dataOffset = offsetOfVertexFlags + vertexCount;
    int offsetOfFaceIndices = dataOffset;
    dataOffset += faceCount;
    int offsetOfFaceRenderPriorities = dataOffset;
    if( faceRenderPriority == 255 )
    {
        dataOffset += faceCount;
    }

    int offsetOfPackedTransparencyVertexGroups = dataOffset;
    if( hasPackedTransparencyVertexGroups == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfFaceTextureFlags = dataOffset;
    if( isTextured == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfPackedVertexGroups = dataOffset;
    if( hasPackedVertexGroups == 1 )
    {
        dataOffset += vertexCount;
    }

    int offsetOfFaceTransparencies = dataOffset;
    if( hasFaceTransparencies == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfFaceIndexData = dataOffset;
    dataOffset += faceIndexDataByteCount;
    int offsetOfFaceColorsOrFaceTextures = dataOffset;
    dataOffset += faceCount * 2;
    int offsetOfTextureIndices = dataOffset;
    dataOffset += textureCount * 6;
    int offsetOfVertexXData = dataOffset;
    dataOffset += vertexXDataByteCount;
    int offsetOfVertexYData = dataOffset;
    dataOffset += vertexYDataByteCount;
    int offsetOfVertexZData = dataOffset;

    // Set model properties
    model->vertex_count = vertexCount;
    model->face_count = faceCount;
    model->textured_face_count = textureCount;

    // Allocate memory for vertices
    model->vertices_x = (int*)malloc(vertexCount * sizeof(int));
    model->vertices_y = (int*)malloc(vertexCount * sizeof(int));
    model->vertices_z = (int*)malloc(vertexCount * sizeof(int));

    // Allocate memory for faces
    model->face_indices_a = (int*)malloc(faceCount * sizeof(int));
    model->face_indices_b = (int*)malloc(faceCount * sizeof(int));
    model->face_indices_c = (int*)malloc(faceCount * sizeof(int));
    model->face_colors = (int*)malloc(faceCount * sizeof(int));
    model->face_priorities = (int*)malloc(faceCount * sizeof(int));
    model->face_alphas = (int*)malloc(faceCount * sizeof(int));
    model->face_infos = (int*)malloc(faceCount * sizeof(int));

    // Read vertex data
    int previousVertexX = 0;
    int previousVertexY = 0;
    int previousVertexZ = 0;
    offset = offsetOfVertexFlags;

    for( int i = 0; i < vertexCount; i++ )
    {
        int vertexFlags = read_byte(inputData, &offset);
        int deltaX = 0;
        int deltaY = 0;
        int deltaZ = 0;

        if( vertexFlags & 1 )
        {
            deltaX = read_short_smart(inputData, &offsetOfVertexXData);
        }
        if( vertexFlags & 2 )
        {
            deltaY = read_short_smart(inputData, &offsetOfVertexYData);
        }
        if( vertexFlags & 4 )
        {
            deltaZ = read_short_smart(inputData, &offsetOfVertexZData);
        }

        model->vertices_x[i] = previousVertexX + deltaX;
        model->vertices_y[i] = previousVertexY + deltaY;
        model->vertices_z[i] = previousVertexZ + deltaZ;
        previousVertexX = model->vertices_x[i];
        previousVertexY = model->vertices_y[i];
        previousVertexZ = model->vertices_z[i];
    }

    // Read face data
    offset = offsetOfFaceColorsOrFaceTextures;
    int textureFlagsOffset = offsetOfFaceTextureFlags;
    int prioritiesOffset = offsetOfFaceRenderPriorities;
    int transparenciesOffset = offsetOfFaceTransparencies;
    int transparencyGroupsOffset = offsetOfPackedTransparencyVertexGroups;

    for( int i = 0; i < faceCount; i++ )
    {
        model->face_colors[i] = read_unsigned_short(inputData, &offset);

        if( isTextured == 1 )
        {
            int faceTextureFlags = read_byte(inputData, &textureFlagsOffset);
            if( faceTextureFlags & 1 )
            {
                model->face_infos[i] = 1;
            }
            else
            {
                model->face_infos[i] = 0;
            }

            if( faceTextureFlags & 2 )
            {
                model->face_alphas[i] = faceTextureFlags >> 2;
                model->face_colors[i] = 127;
            }
            else
            {
                model->face_alphas[i] = -1;
            }
        }

        if( faceRenderPriority == 255 )
        {
            model->face_priorities[i] = read_byte(inputData, &prioritiesOffset);
        }
        else
        {
            model->face_priorities[i] = faceRenderPriority;
        }

        if( hasFaceTransparencies == 1 )
        {
            model->face_alphas[i] = read_byte(inputData, &transparenciesOffset);
        }

        if( hasPackedTransparencyVertexGroups == 1 )
        {
            read_byte(
                inputData,
                &transparencyGroupsOffset); // Skip this data as it's not in our model struct
        }
    }

    // Read face indices
    offset = offsetOfFaceIndexData;
    int compressionTypesOffset = offsetOfFaceIndices;
    int previousIndex1 = 0;
    int previousIndex2 = 0;
    int previousIndex3 = 0;
    int previousIndex3Copy = 0; // This matches var43 in Java code

    for( int i = 0; i < faceCount; i++ )
    {
        int compressionType = read_byte(inputData, &compressionTypesOffset);

        if( compressionType == 1 )
        {
            previousIndex1 = read_short_smart(inputData, &offset) + previousIndex3;
            previousIndex2 = read_short_smart(inputData, &offset) + previousIndex1;
            previousIndex3 = read_short_smart(inputData, &offset) + previousIndex2;
            previousIndex3Copy = previousIndex3; // Store copy for next iteration
        }
        else if( compressionType == 2 )
        {
            previousIndex2 = previousIndex3;
            previousIndex3 = read_short_smart(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3; // Store copy for next iteration
        }
        else if( compressionType == 3 )
        {
            previousIndex1 = previousIndex3;
            previousIndex3 = read_short_smart(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3; // Store copy for next iteration
        }
        else if( compressionType == 4 )
        {
            int temp = previousIndex1;
            previousIndex1 = previousIndex2;
            previousIndex2 = temp;
            previousIndex3 = read_short_smart(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3; // Store copy for next iteration
        }

        model->face_indices_a[i] = previousIndex1;
        model->face_indices_b[i] = previousIndex2;
        model->face_indices_c[i] = previousIndex3;
    }

    // Set model priority
    model->model_priority = faceRenderPriority;

    return model;
}

struct Model*
decodeType1(const unsigned char* inputData, int inputLength)
{
    struct Model* model = (struct Model*)malloc(sizeof(struct Model));
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
    int offset = inputLength - 23;
    int vertexCount = read_unsigned_short(inputData, &offset);
    int faceCount = read_unsigned_short(inputData, &offset);
    int textureCount = read_byte(inputData, &offset);
    int hasFaceRenderTypes = read_byte(inputData, &offset);
    int faceRenderPriority = read_byte(inputData, &offset);
    int hasFaceTransparencies = read_byte(inputData, &offset);
    int hasPackedTransparencyVertexGroups = read_byte(inputData, &offset);
    int hasFaceTextures = read_byte(inputData, &offset);
    int hasPackedVertexGroups = read_byte(inputData, &offset);
    int vertexXDataByteCount = read_unsigned_short(inputData, &offset);
    int vertexYDataByteCount = read_unsigned_short(inputData, &offset);
    int vertexZDataByteCount = read_unsigned_short(inputData, &offset);
    int faceIndexDataByteCount = read_unsigned_short(inputData, &offset);
    int faceColorDataByteCount = read_unsigned_short(inputData, &offset);

    // Calculate offsets for different data sections
    int offsetOfTextureRenderTypes = 0;
    int dataOffset = offsetOfTextureRenderTypes + textureCount;
    int offsetOfFaceRenderTypes = dataOffset;
    if( hasFaceRenderTypes == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfFaceRenderPriorities = dataOffset;
    if( faceRenderPriority == 255 )
    {
        dataOffset += faceCount;
    }

    int offsetOfPackedVertexGroups = dataOffset;
    if( hasPackedVertexGroups == 1 )
    {
        dataOffset += vertexCount;
    }

    int offsetOfFaceTransparencies = dataOffset;
    if( hasFaceTransparencies == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfPackedTransparencyVertexGroups = dataOffset;
    if( hasPackedTransparencyVertexGroups == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfFaceTextures = dataOffset;
    if( hasFaceTextures == 1 )
    {
        dataOffset += faceCount * 2;
    }

    int offsetOfFaceColors = dataOffset;
    dataOffset += faceColorDataByteCount;
    int offsetOfVertexXData = dataOffset;
    dataOffset += vertexXDataByteCount;
    int offsetOfVertexYData = dataOffset;
    dataOffset += vertexYDataByteCount;
    int offsetOfVertexZData = dataOffset;

    // Set model properties
    model->vertex_count = vertexCount;
    model->face_count = faceCount;
    model->textured_face_count = textureCount;

    // Allocate memory for vertices
    model->vertices_x = (int*)malloc(vertexCount * sizeof(int));
    model->vertices_y = (int*)malloc(vertexCount * sizeof(int));
    model->vertices_z = (int*)malloc(vertexCount * sizeof(int));

    // Allocate memory for faces
    model->face_indices_a = (int*)malloc(faceCount * sizeof(int));
    model->face_indices_b = (int*)malloc(faceCount * sizeof(int));
    model->face_indices_c = (int*)malloc(faceCount * sizeof(int));
    model->face_colors = (int*)malloc(faceCount * sizeof(int));
    model->face_priorities = (int*)malloc(faceCount * sizeof(int));
    model->face_alphas = (int*)malloc(faceCount * sizeof(int));
    model->face_infos = (int*)malloc(faceCount * sizeof(int));

    // Read vertex data
    int previousVertexX = 0;
    int previousVertexY = 0;
    int previousVertexZ = 0;
    offset = offsetOfTextureRenderTypes + textureCount;

    for( int i = 0; i < vertexCount; i++ )
    {
        int vertexFlags = read_byte(inputData, &offset);
        int deltaX = 0;
        int deltaY = 0;
        int deltaZ = 0;

        if( vertexFlags & 1 )
        {
            deltaX = read_short_smart(inputData, &offsetOfVertexXData);
        }
        if( vertexFlags & 2 )
        {
            deltaY = read_short_smart(inputData, &offsetOfVertexYData);
        }
        if( vertexFlags & 4 )
        {
            deltaZ = read_short_smart(inputData, &offsetOfVertexZData);
        }

        model->vertices_x[i] = previousVertexX + deltaX;
        model->vertices_y[i] = previousVertexY + deltaY;
        model->vertices_z[i] = previousVertexZ + deltaZ;
        previousVertexX = model->vertices_x[i];
        previousVertexY = model->vertices_y[i];
        previousVertexZ = model->vertices_z[i];

        if( hasPackedVertexGroups == 1 )
        {
            read_byte(
                inputData,
                &offsetOfPackedVertexGroups); // Skip this data as it's not in our model struct
        }
    }

    // Read face data
    offset = offsetOfFaceColors;
    int renderTypesOffset = offsetOfFaceRenderTypes;
    int prioritiesOffset = offsetOfFaceRenderPriorities;
    int transparenciesOffset = offsetOfFaceTransparencies;
    int transparencyGroupsOffset = offsetOfPackedTransparencyVertexGroups;
    int texturesOffset = offsetOfFaceTextures;

    for( int i = 0; i < faceCount; i++ )
    {
        model->face_colors[i] = read_unsigned_short(inputData, &offset);

        if( hasFaceRenderTypes == 1 )
        {
            model->face_infos[i] = read_byte(inputData, &renderTypesOffset);
        }

        if( faceRenderPriority == 255 )
        {
            model->face_priorities[i] = read_byte(inputData, &prioritiesOffset);
        }
        else
        {
            model->face_priorities[i] = faceRenderPriority;
        }

        if( hasFaceTransparencies == 1 )
        {
            model->face_alphas[i] = read_byte(inputData, &transparenciesOffset);
        }

        if( hasPackedTransparencyVertexGroups == 1 )
        {
            read_byte(
                inputData,
                &transparencyGroupsOffset); // Skip this data as it's not in our model struct
        }

        if( hasFaceTextures == 1 )
        {
            read_unsigned_short(
                inputData, &texturesOffset); // Skip texture data as it's not in our model struct
        }
    }

    // Read face indices
    offset = offsetOfFaceTextures + (hasFaceTextures == 1 ? faceCount * 2 : 0);
    int previousIndex1 = 0;
    int previousIndex2 = 0;
    int previousIndex3 = 0;

    for( int i = 0; i < faceCount; i++ )
    {
        previousIndex1 = read_short_smart(inputData, &offset) + previousIndex3;
        previousIndex2 = read_short_smart(inputData, &offset) + previousIndex1;
        previousIndex3 = read_short_smart(inputData, &offset) + previousIndex2;

        model->face_indices_a[i] = previousIndex1;
        model->face_indices_b[i] = previousIndex2;
        model->face_indices_c[i] = previousIndex3;
    }

    // Set model priority
    model->model_priority = faceRenderPriority;

    return model;
}

struct Model*
decodeType2(const unsigned char* inputData, int inputLength)
{
    struct Model* model = (struct Model*)malloc(sizeof(struct Model));
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
    int offset = inputLength - 23;
    int vertexCount = read_unsigned_short(inputData, &offset);
    int faceCount = read_unsigned_short(inputData, &offset);
    int textureCount = read_byte(inputData, &offset);
    int hasFaceRenderTypes = read_byte(inputData, &offset);
    int faceRenderPriority = read_byte(inputData, &offset);
    int hasFaceTransparencies = read_byte(inputData, &offset);
    int hasPackedTransparencyVertexGroups = read_byte(inputData, &offset);
    int hasPackedVertexGroups = read_byte(inputData, &offset);
    int hasAnimayaGroups = read_byte(inputData, &offset);
    int vertexXDataByteCount = read_unsigned_short(inputData, &offset);
    int vertexYDataByteCount = read_unsigned_short(inputData, &offset);
    int vertexZDataByteCount = read_unsigned_short(inputData, &offset);
    int faceIndexDataByteCount = read_unsigned_short(inputData, &offset);
    int faceColorDataByteCount = read_unsigned_short(inputData, &offset);

    // Calculate offsets for different data sections
    int offsetOfVertexFlags = 0;
    int dataOffset = offsetOfVertexFlags + vertexCount;
    int offsetOfFaceIndices = dataOffset;
    dataOffset += faceCount;
    int offsetOfFaceRenderPriorities = dataOffset;
    if( faceRenderPriority == 255 )
    {
        dataOffset += faceCount;
    }

    int offsetOfPackedTransparencyVertexGroups = dataOffset;
    if( hasPackedTransparencyVertexGroups == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfFaceRenderTypes = dataOffset;
    if( hasFaceRenderTypes == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfPackedBoneGroups = dataOffset;
    dataOffset += faceColorDataByteCount;
    int offsetOfFaceTransparencies = dataOffset;
    if( hasFaceTransparencies == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfFaceIndexData = dataOffset;
    dataOffset += faceIndexDataByteCount;
    int offsetOfFaceColorsOrFaceTextures = dataOffset;
    dataOffset += faceCount * 2;
    int offsetOfTextureIndices = dataOffset;
    dataOffset += textureCount * 6;
    int offsetOfVertexXData = dataOffset;
    dataOffset += vertexXDataByteCount;
    int offsetOfVertexYData = dataOffset;
    dataOffset += vertexYDataByteCount;
    int offsetOfVertexZData = dataOffset;

    // Set model properties
    model->vertex_count = vertexCount;
    model->face_count = faceCount;
    model->textured_face_count = textureCount;

    // Allocate memory for vertices
    model->vertices_x = (int*)malloc(vertexCount * sizeof(int));
    model->vertices_y = (int*)malloc(vertexCount * sizeof(int));
    model->vertices_z = (int*)malloc(vertexCount * sizeof(int));
    model->vertex_bone_map = (int*)malloc(vertexCount * sizeof(int));

    // Allocate memory for faces
    model->face_indices_a = (int*)malloc(faceCount * sizeof(int));
    model->face_indices_b = (int*)malloc(faceCount * sizeof(int));
    model->face_indices_c = (int*)malloc(faceCount * sizeof(int));
    model->face_colors = (int*)malloc(faceCount * sizeof(int));
    model->face_priorities = (int*)malloc(faceCount * sizeof(int));
    model->face_alphas = (int*)malloc(faceCount * sizeof(int));
    model->face_infos = (int*)malloc(faceCount * sizeof(int));

    // Read vertex data
    int previousVertexX = 0;
    int previousVertexY = 0;
    int previousVertexZ = 0;
    int previousBoneGroup = 0;
    offset = offsetOfVertexFlags;

    for( int i = 0; i < vertexCount; i++ )
    {
        int vertexFlags = read_byte(inputData, &offset);
        int deltaX = 0;
        int deltaY = 0;
        int deltaZ = 0;

        if( vertexFlags & 1 )
        {
            deltaX = read_short_smart(inputData, &offsetOfVertexXData);
        }
        if( vertexFlags & 2 )
        {
            deltaY = read_short_smart(inputData, &offsetOfVertexYData);
        }
        if( vertexFlags & 4 )
        {
            deltaZ = read_short_smart(inputData, &offsetOfVertexZData);
        }

        model->vertices_x[i] = previousVertexX + deltaX;
        model->vertices_y[i] = previousVertexY + deltaY;
        model->vertices_z[i] = previousVertexZ + deltaZ;
        previousVertexX = model->vertices_x[i];
        previousVertexY = model->vertices_y[i];
        previousVertexZ = model->vertices_z[i];

        if( hasPackedVertexGroups == 1 )
        {
            model->vertex_bone_map[i] = read_byte(inputData, &offsetOfPackedBoneGroups);
        }
    }

    // Read face data
    offset = offsetOfFaceColorsOrFaceTextures;
    int textureFlagsOffset = offsetOfFaceRenderTypes;
    int prioritiesOffset = offsetOfFaceRenderPriorities;
    int transparenciesOffset = offsetOfFaceTransparencies;
    int transparencyGroupsOffset = offsetOfPackedTransparencyVertexGroups;

    for( int i = 0; i < faceCount; i++ )
    {
        model->face_colors[i] = read_unsigned_short(inputData, &offset);

        if( hasFaceRenderTypes == 1 )
        {
            int faceTextureFlags = read_byte(inputData, &textureFlagsOffset);
            if( faceTextureFlags & 1 )
            {
                model->face_infos[i] = 1;
            }
            else
            {
                model->face_infos[i] = 0;
            }

            if( faceTextureFlags & 2 )
            {
                model->face_alphas[i] = faceTextureFlags >> 2;
                model->face_colors[i] = 127;
            }
            else
            {
                model->face_alphas[i] = -1;
            }
        }

        if( faceRenderPriority == 255 )
        {
            model->face_priorities[i] = read_byte(inputData, &prioritiesOffset);
        }
        else
        {
            model->face_priorities[i] = faceRenderPriority;
        }

        if( hasFaceTransparencies == 1 )
        {
            model->face_alphas[i] = read_byte(inputData, &transparenciesOffset);
        }

        if( hasPackedTransparencyVertexGroups == 1 )
        {
            read_byte(
                inputData,
                &transparencyGroupsOffset); // Skip this data as it's not in our model struct
        }
    }

    // Read face indices
    offset = offsetOfFaceIndexData;
    int compressionTypesOffset = offsetOfFaceIndices;
    int previousIndex1 = 0;
    int previousIndex2 = 0;
    int previousIndex3 = 0;
    int previousIndex3Copy = 0; // This matches var43 in Java code

    for( int i = 0; i < faceCount; i++ )
    {
        int compressionType = read_byte(inputData, &compressionTypesOffset);

        if( compressionType == 1 )
        {
            previousIndex1 = read_short_smart(inputData, &offset) + previousIndex3;
            previousIndex2 = read_short_smart(inputData, &offset) + previousIndex1;
            previousIndex3 = read_short_smart(inputData, &offset) + previousIndex2;
            previousIndex3Copy = previousIndex3; // Store copy for next iteration
        }
        else if( compressionType == 2 )
        {
            previousIndex2 = previousIndex3;
            previousIndex3 = read_short_smart(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3; // Store copy for next iteration
        }
        else if( compressionType == 3 )
        {
            previousIndex1 = previousIndex3;
            previousIndex3 = read_short_smart(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3; // Store copy for next iteration
        }
        else if( compressionType == 4 )
        {
            int temp = previousIndex1;
            previousIndex1 = previousIndex2;
            previousIndex2 = temp;
            previousIndex3 = read_short_smart(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3; // Store copy for next iteration
        }

        model->face_indices_a[i] = previousIndex1;
        model->face_indices_b[i] = previousIndex2;
        model->face_indices_c[i] = previousIndex3;
    }

    // Read texture indices
    offset = offsetOfTextureIndices;
    for( int i = 0; i < textureCount; i++ )
    {
        // Skip texture data as it's not in our model struct
        read_unsigned_short(inputData, &offset);
        read_unsigned_short(inputData, &offset);
        read_unsigned_short(inputData, &offset);
    }

    // Set model priority
    model->model_priority = faceRenderPriority;

    return model;
}

struct Model*
model_new_from_cache(struct Cache* cache, int model_id)
{
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_MODELS, model_id);
    if( !archive )
    {
        printf("Failed to load model %d\n", model_id);
        return NULL;
    }

    struct Model* model = model_new_decode(archive->data, archive->data_size);

    cache_archive_free(archive);

    return model;
}

struct Model*
model_new_decode(const unsigned char* inputData, int inputLength)
{
    // Check the last two bytes to determine model type
    if( inputLength >= 2 )
    {
        unsigned char lastByte = inputData[inputLength - 1];
        unsigned char secondLastByte = inputData[inputLength - 2];

        if( lastByte == 0xFD && secondLastByte == 0xFF )
        { // -3, -1
            return NULL;
            // return decodeType3(inputData, inputLength);
        }
        else if( lastByte == 0xFE && secondLastByte == 0xFF )
        { // -2, -1
            return decodeType2(inputData, inputLength);
        }
        else if( lastByte == 0xFF && secondLastByte == 0xFF )
        { // -1, -1
            return decodeType1(inputData, inputLength);
        }
    }

    // If no type matches, use old format
    return decodeOldFormat(inputData, inputLength);
}

#include <stdio.h>

void
write_model_separate(const struct Model* model, const char* filename)
{
    char name_buffer[256];
    FILE* file = NULL;

    // Write vertices file
    snprintf(name_buffer, sizeof(name_buffer), "%s.vertices", filename);
    file = fopen(name_buffer, "wb");
    if( !file )
    {
        printf("Failed to open vertices file for writing\n");
        return;
    }

    // Write vertex count and vertex data
    fwrite(&model->vertex_count, sizeof(int), 1, file);
    fwrite(model->vertices_x, sizeof(int), model->vertex_count, file);
    fwrite(model->vertices_y, sizeof(int), model->vertex_count, file);
    fwrite(model->vertices_z, sizeof(int), model->vertex_count, file);
    if( model->vertex_bone_map )
    {
        fwrite(model->vertex_bone_map, sizeof(int), model->vertex_count, file);
    }
    fclose(file);

    // Write faces file
    snprintf(name_buffer, sizeof(name_buffer), "%s.faces", filename);
    file = fopen(name_buffer, "wb");
    if( !file )
    {
        printf("Failed to open faces file for writing\n");
        return;
    }

    // Write face count and face data
    fwrite(&model->face_count, sizeof(int), 1, file);
    fwrite(model->face_indices_a, sizeof(int), model->face_count, file);
    fwrite(model->face_indices_b, sizeof(int), model->face_count, file);
    fwrite(model->face_indices_c, sizeof(int), model->face_count, file);
    fclose(file);

    // Write colors file
    snprintf(name_buffer, sizeof(name_buffer), "%s.colors", filename);
    file = fopen(name_buffer, "wb");
    if( !file )
    {
        printf("Failed to open colors file for writing\n");
        return;
    }

    // Write face count and color data
    fwrite(&model->face_count, sizeof(int), 1, file);
    fwrite(model->face_colors, sizeof(int), model->face_count, file);
    fclose(file);

    // Write priorities file
    snprintf(name_buffer, sizeof(name_buffer), "%s.priorities", filename);
    file = fopen(name_buffer, "wb");
    if( !file )
    {
        printf("Failed to open priorities file for writing\n");
        return;
    }

    // Write face count and priority data
    fwrite(&model->face_count, sizeof(int), 1, file);
    fwrite(model->face_priorities, sizeof(int), model->face_count, file);
    fclose(file);
}

void
modelbones_free(struct ModelBones* modelbones)
{
    for( int i = 0; i < modelbones->bones_count; i++ )
        free(modelbones->bones[i]);
    free(modelbones->bones);
    free(modelbones->bones_sizes);

    free(modelbones);
}

void
model_free(struct Model* model)
{
    if( !model )
        return;

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

    free(model);
}