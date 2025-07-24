#include "model.h"

#include "osrs/cache.h"

#include <assert.h>
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

struct CacheModelBones*
modelbones_new_decode(int* vertex_bone_map, int vertex_bone_map_count)
{
    struct CacheModelBones* bones = (struct CacheModelBones*)malloc(sizeof(struct CacheModelBones));
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

struct CacheModel*
decodeOldFormat(const unsigned char* inputData, int inputLength)
{
    struct CacheModel* model = (struct CacheModel*)malloc(sizeof(struct CacheModel));
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

struct CacheModel*
decodeType1(const unsigned char* inputData, int inputLength)
{
    struct CacheModel* model = (struct CacheModel*)malloc(sizeof(struct CacheModel));
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
            // Packed vertex groups
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
            int face_type = read_byte(inputData, &renderTypesOffset);
            assert(face_type <= 3);
            model->face_infos[i] = face_type;
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

        // if (var16 == 1)
        // {
        // 	def.faceTextures[var49] = (short) (var7.readUnsignedShort() - 1);
        // }

        // if (def.textureCoords != null && def.faceTextures[var49] != -1)
        // {
        // 	def.textureCoords[var49] = (byte) (var8.readUnsignedByte() - 1);
        // }

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

struct CacheModel*
decodeType2(const unsigned char* inputData, int inputLength)
{
    struct CacheModel* model = (struct CacheModel*)malloc(sizeof(struct CacheModel));
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
    if( textureCount > 0 )
    {
        model->textured_p_coordinate = (int*)malloc(textureCount * sizeof(int));
        memset(model->textured_p_coordinate, 0, textureCount * sizeof(int));
        model->textured_m_coordinate = (int*)malloc(textureCount * sizeof(int));
        memset(model->textured_m_coordinate, 0, textureCount * sizeof(int));
        model->textured_n_coordinate = (int*)malloc(textureCount * sizeof(int));
        memset(model->textured_n_coordinate, 0, textureCount * sizeof(int));
    }

    for( int i = 0; i < textureCount; i++ )
    {
        model->textured_p_coordinate[i] = read_unsigned_short(inputData, &offset);
        model->textured_m_coordinate[i] = read_unsigned_short(inputData, &offset);
        model->textured_n_coordinate[i] = read_unsigned_short(inputData, &offset);
    }

    // Set model priority
    model->model_priority = faceRenderPriority;

    return model;
}

// decodeV3(data: Int8Array): void {
//         this.version = 3;
//         const buf1 = new ByteBuffer(data);
//         const buf2 = new ByteBuffer(data);
//         const buf3 = new ByteBuffer(data);
//         const buf4 = new ByteBuffer(data);
//         const buf5 = new ByteBuffer(data);
//         const buf6 = new ByteBuffer(data);
//         const buf7 = new ByteBuffer(data);
//         buf1.offset = data.length - 26;
//         const vertexCount = buf1.readUnsignedShort();
//         const faceCount = buf1.readUnsignedShort();
//         const texTriangleCount = buf1.readUnsignedByte();
//         const var12 = buf1.readUnsignedByte();
//         const var13 = buf1.readUnsignedByte();
//         const var14 = buf1.readUnsignedByte();
//         const var15 = buf1.readUnsignedByte();
//         const var16 = buf1.readUnsignedByte();
//         const var17 = buf1.readUnsignedByte();
//         const hasMayaGroups = buf1.readUnsignedByte();
//         const var19 = buf1.readUnsignedShort();
//         const var20 = buf1.readUnsignedShort();
//         const var21 = buf1.readUnsignedShort();
//         const var22 = buf1.readUnsignedShort();
//         const var23 = buf1.readUnsignedShort();
//         const var24 = buf1.readUnsignedShort();
//         let simpleTextureFaceCount = 0;
//         let complexTextureFaceCount = 0;
//         let cubeTextureFaceCount = 0;
//         if (texTriangleCount > 0) {
//             this.textureRenderTypes = new Int8Array(texTriangleCount);
//             buf1.offset = 0;

//             for (let i = 0; i < texTriangleCount; i++) {
//                 const type = (this.textureRenderTypes[i] = buf1.readByte());
//                 if (type === 0) {
//                     simpleTextureFaceCount++;
//                 }

//                 if (type >= 1 && type <= 3) {
//                     complexTextureFaceCount++;
//                 }

//                 if (type === 2) {
//                     cubeTextureFaceCount++;
//                 }
//             }
//         }

//         let var28 = texTriangleCount + vertexCount;
//         const var30 = var28;
//         if (var12 === 1) {
//             var28 += faceCount;
//         }

//         const var31 = var28;
//         var28 += faceCount;
//         const var32 = var28;
//         if (var13 === 255) {
//             var28 += faceCount;
//         }

//         const var33 = var28;
//         if (var15 === 1) {
//             var28 += faceCount;
//         }

//         const var34 = var28;
//         var28 += var24;
//         const var35 = var28;
//         if (var14 === 1) {
//             var28 += faceCount;
//         }

//         const var36 = var28;
//         var28 += var22;
//         const var37 = var28;
//         if (var16 === 1) {
//             var28 += faceCount * 2;
//         }

//         const var38 = var28;
//         var28 += var23;
//         const var39 = var28;
//         var28 += faceCount * 2;
//         const var40 = var28;
//         var28 += var19;
//         const var41 = var28;
//         var28 += var20;
//         const var42 = var28;
//         var28 += var21;
//         const var43 = var28;
//         var28 += simpleTextureFaceCount * 6;
//         const var44 = var28;
//         var28 += complexTextureFaceCount * 6;
//         const var45 = var28;
//         var28 += complexTextureFaceCount * 6;
//         const var46 = var28;
//         var28 += complexTextureFaceCount * 2;
//         const var47 = var28;
//         var28 += complexTextureFaceCount;
//         const var48 = var28;
//         var28 += complexTextureFaceCount * 2 + cubeTextureFaceCount * 2;
//         this.verticesCount = vertexCount;
//         this.faceCount = faceCount;
//         this.textureFaceCount = texTriangleCount;
//         this.verticesX = new Int32Array(vertexCount);
//         this.verticesY = new Int32Array(vertexCount);
//         this.verticesZ = new Int32Array(vertexCount);
//         this.indices1 = new Int32Array(faceCount);
//         this.indices2 = new Int32Array(faceCount);
//         this.indices3 = new Int32Array(faceCount);
//         if (var17 === 1) {
//             this.vertexSkins = new Int32Array(vertexCount);
//         }

//         if (var12 === 1) {
//             this.faceRenderTypes = new Int8Array(faceCount);
//         }

//         if (var13 === 255) {
//             this.faceRenderPriorities = new Int8Array(faceCount);
//         } else {
//             this.priority = var13;
//         }

//         if (var14 === 1) {
//             this.faceAlphas = new Int8Array(faceCount);
//         }

//         if (var15 === 1) {
//             this.faceSkins = new Int32Array(faceCount);
//         }

//         if (var16 === 1) {
//             this.faceTextures = new Int16Array(faceCount);
//         }

//         if (var16 === 1 && texTriangleCount > 0) {
//             this.textureCoords = new Int8Array(faceCount);
//         }

//         if (hasMayaGroups === 1) {
//             this.animMayaGroups = new Array(vertexCount);
//             this.animMayaScales = new Array(vertexCount);
//         }

//         this.faceColors = new Uint16Array(faceCount);
//         if (texTriangleCount > 0) {
//             this.textureMappingP = new Int16Array(texTriangleCount);
//             this.textureMappingM = new Int16Array(texTriangleCount);
//             this.textureMappingN = new Int16Array(texTriangleCount);
//             if (complexTextureFaceCount > 0) {
//                 this.textureScaleX = new Int32Array(complexTextureFaceCount);
//                 this.textureScaleY = new Int32Array(complexTextureFaceCount);
//                 this.textureScaleZ = new Int32Array(complexTextureFaceCount);
//                 this.textureRotation = new Int8Array(complexTextureFaceCount);
//                 this.textureDirection = new Int8Array(complexTextureFaceCount);
//                 this.textureSpeed = new Int32Array(complexTextureFaceCount);
//             }
//             if (cubeTextureFaceCount > 0) {
//                 this.textureTransU = new Int32Array(cubeTextureFaceCount);
//                 this.textureTransV = new Int32Array(cubeTextureFaceCount);
//             }
//         }

//         buf1.offset = texTriangleCount;
//         buf2.offset = var40;
//         buf3.offset = var41;
//         buf4.offset = var42;
//         buf5.offset = var34;
//         let lastVertX = 0;
//         let lastVertY = 0;
//         let lastVertZ = 0;

//         for (let i = 0; i < vertexCount; i++) {
//             const flag = buf1.readUnsignedByte();
//             let deltaVertX = 0;
//             if ((flag & 1) !== 0) {
//                 deltaVertX = buf2.readSmart2();
//             }

//             let deltaVertY = 0;
//             if ((flag & 2) !== 0) {
//                 deltaVertY = buf3.readSmart2();
//             }

//             let deltaVertZ = 0;
//             if ((flag & 4) !== 0) {
//                 deltaVertZ = buf4.readSmart2();
//             }

//             this.verticesX[i] = lastVertX + deltaVertX;
//             this.verticesY[i] = lastVertY + deltaVertY;
//             this.verticesZ[i] = lastVertZ + deltaVertZ;
//             lastVertX = this.verticesX[i];
//             lastVertY = this.verticesY[i];
//             lastVertZ = this.verticesZ[i];
//             if (var17 === 1 && this.vertexSkins) {
//                 this.vertexSkins[i] = buf5.readUnsignedByte();
//             }
//         }

//         if (hasMayaGroups === 1) {
//             for (let i = 0; i < vertexCount; i++) {
//                 const var54 = buf5.readUnsignedByte();
//                 this.animMayaGroups[i] = new Int32Array(var54);
//                 this.animMayaScales[i] = new Int32Array(var54);

//                 for (let j = 0; j < var54; j++) {
//                     this.animMayaGroups[i][j] = buf5.readUnsignedByte();
//                     this.animMayaScales[i][j] = buf5.readUnsignedByte();
//                 }
//             }
//         }

//         buf1.offset = var39;
//         buf2.offset = var30;
//         buf3.offset = var32;
//         buf4.offset = var35;
//         buf5.offset = var33;
//         buf6.offset = var37;
//         buf7.offset = var38;

//         for (let i = 0; i < faceCount; i++) {
//             this.faceColors[i] = buf1.readUnsignedShort();
//             if (var12 === 1 && this.faceRenderTypes) {
//                 this.faceRenderTypes[i] = buf2.readByte();
//             }

//             if (var13 === 255) {
//                 this.faceRenderPriorities[i] = buf3.readByte();
//             }

//             if (var14 === 1) {
//                 this.faceAlphas[i] = buf4.readByte();
//             }

//             if (var15 === 1 && this.faceSkins) {
//                 this.faceSkins[i] = buf5.readUnsignedByte();
//             }

//             if (var16 === 1 && this.faceTextures) {
//                 this.faceTextures[i] = buf6.readUnsignedShort() - 1;
//             }

//             if (this.textureCoords && this.faceTextures && this.faceTextures[i] !== -1) {
//                 this.textureCoords[i] = buf7.readUnsignedByte() - 1;
//             }
//         }

//         buf1.offset = var36;
//         buf2.offset = var31;
//         let var53 = 0;
//         let var54 = 0;
//         let var55 = 0;
//         let var56 = 0;

//         for (let i = 0; i < faceCount; i++) {
//             const type = buf2.readUnsignedByte();
//             if (type === 1) {
//                 var53 = buf1.readSmart2() + var56;
//                 var54 = buf1.readSmart2() + var53;
//                 var55 = buf1.readSmart2() + var54;
//                 var56 = var55;
//                 this.indices1[i] = var53;
//                 this.indices2[i] = var54;
//                 this.indices3[i] = var55;
//             }

//             if (type === 2) {
//                 var54 = var55;
//                 var55 = buf1.readSmart2() + var56;
//                 var56 = var55;
//                 this.indices1[i] = var53;
//                 this.indices2[i] = var54;
//                 this.indices3[i] = var55;
//             }

//             if (type === 3) {
//                 var53 = var55;
//                 var55 = buf1.readSmart2() + var56;
//                 var56 = var55;
//                 this.indices1[i] = var53;
//                 this.indices2[i] = var54;
//                 this.indices3[i] = var55;
//             }

//             if (type === 4) {
//                 const var59 = var53;
//                 var53 = var54;
//                 var54 = var59;
//                 var55 = buf1.readSmart2() + var56;
//                 var56 = var55;
//                 this.indices1[i] = var53;
//                 this.indices2[i] = var59;
//                 this.indices3[i] = var55;
//             }
//         }

//         buf1.offset = var43;
//         buf2.offset = var44;
//         buf3.offset = var45;
//         buf4.offset = var46;
//         buf5.offset = var47;
//         buf6.offset = var48;

//         for (let i = 0; i < texTriangleCount; i++) {
//             const type = this.textureRenderTypes[i] & 255;
//             if (type === 0) {
//                 this.textureMappingP[i] = buf1.readUnsignedShort();
//                 this.textureMappingM[i] = buf1.readUnsignedShort();
//                 this.textureMappingN[i] = buf1.readUnsignedShort();
//             }
//         }

//         buf1.offset = var28;
//         const var57 = buf1.readUnsignedByte();
//         if (var57 !== 0) {
//             // new ModelData0();
//             buf1.readUnsignedShort();
//             buf1.readUnsignedShort();
//             buf1.readUnsignedShort();
//             buf1.readInt();
//         }
//     }

struct CacheModel*
decodeType3(const unsigned char* inputData, int inputLength)
{
    struct CacheModel* model = (struct CacheModel*)malloc(sizeof(struct CacheModel));
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
    model->vertex_bone_map = NULL;

    // Read header information from the end of the file
    int offset = inputLength - 26;
    int vertexCount = read_unsigned_short(inputData, &offset);
    int faceCount = read_unsigned_short(inputData, &offset);
    int texTriangleCount = read_byte(inputData, &offset);
    int hasFaceRenderTypes = read_byte(inputData, &offset);
    int faceRenderPriority = read_byte(inputData, &offset);
    int hasFaceTransparencies = read_byte(inputData, &offset);
    int hasFaceTextures = read_byte(inputData, &offset);
    int unknown = read_byte(inputData, &offset);
    int hasPackedVertexGroups = read_byte(inputData, &offset);
    int hasMayaGroups = read_byte(inputData, &offset);
    int vertexXDataByteCount = read_unsigned_short(inputData, &offset);
    int vertexYDataByteCount = read_unsigned_short(inputData, &offset);
    int vertexZDataByteCount = read_unsigned_short(inputData, &offset);
    int faceIndexDataByteCount = read_unsigned_short(inputData, &offset);
    int faceColorDataByteCount = read_unsigned_short(inputData, &offset);
    int faceTextureDataByteCount = read_unsigned_short(inputData, &offset);

    // Calculate texture face counts
    int simpleTextureFaceCount = 0;
    int complexTextureFaceCount = 0;
    int cubeTextureFaceCount = 0;

    // Read texture render types if texture triangles exist
    if( texTriangleCount > 0 )
    {
        offset = 0;
        for( int i = 0; i < texTriangleCount; i++ )
        {
            int type = read_byte(inputData, &offset);
            if( type == 0 )
            {
                simpleTextureFaceCount++;
            }
            if( type >= 1 && type <= 3 )
            {
                complexTextureFaceCount++;
            }
            if( type == 2 )
            {
                cubeTextureFaceCount++;
            }
        }
    }

    // Calculate offsets for different data sections
    int offsetOfTextureRenderTypes = 0;
    int dataOffset = vertexCount + texTriangleCount;
    int offsetOfFaceRenderTypes = dataOffset;
    if( hasFaceRenderTypes == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfFaceIndices = dataOffset;
    dataOffset += faceCount;
    int offsetOfFaceRenderPriorities = dataOffset;
    if( faceRenderPriority == 255 )
    {
        dataOffset += faceCount;
    }

    int offsetOfPackedTransparencyVertexGroups = dataOffset;
    if( hasFaceTextures == 1 )
    {
        dataOffset += faceCount;
    }

    int unkown3 = dataOffset;
    dataOffset += faceTextureDataByteCount;
    int offsetOfFaceTransparencies = dataOffset;
    if( hasFaceTransparencies == 1 )
    {
        dataOffset += faceCount;
    }

    int offsetOfPackedVertexGroups = dataOffset;
    dataOffset += faceIndexDataByteCount;
    int offsetOfFaceTextures = dataOffset;
    if( unknown == 1 )
    {
        dataOffset += faceCount * 2;
    }

    int offsetOfFaceColors = dataOffset;
    dataOffset += faceColorDataByteCount;
    int uknown2 = dataOffset;
    dataOffset += faceCount * 2;
    int offsetOfVertexXData = dataOffset;
    dataOffset += vertexXDataByteCount;
    int offsetOfVertexYData = dataOffset;
    dataOffset += vertexYDataByteCount;
    int offsetOfVertexZData = dataOffset;
    dataOffset += vertexZDataByteCount;
    int offsetOfSimpleTextureMapping = dataOffset;
    dataOffset += simpleTextureFaceCount * 6;
    int offsetOfComplexTextureMapping = dataOffset;
    dataOffset += complexTextureFaceCount * 6;
    int offsetOfComplexTextureScales = dataOffset;
    dataOffset += complexTextureFaceCount * 6;
    int offsetOfComplexTextureRotation = dataOffset;
    dataOffset += complexTextureFaceCount * 2;
    int offsetOfComplexTextureDirection = dataOffset;
    dataOffset += complexTextureFaceCount;
    int offsetOfComplexTextureSpeed = dataOffset;
    dataOffset += complexTextureFaceCount * 2 + cubeTextureFaceCount * 2;

    // Set model properties
    model->vertex_count = vertexCount;
    model->face_count = faceCount;
    model->textured_face_count = texTriangleCount;

    // Allocate memory for vertices
    model->vertices_x = (int*)malloc(vertexCount * sizeof(int));
    model->vertices_y = (int*)malloc(vertexCount * sizeof(int));
    model->vertices_z = (int*)malloc(vertexCount * sizeof(int));

    // Allocate memory for vertex bone mapping if needed
    if( hasPackedVertexGroups == 1 )
    {
        model->vertex_bone_map = (int*)malloc(vertexCount * sizeof(int));
    }

    // Allocate memory for faces
    model->face_indices_a = (int*)malloc(faceCount * sizeof(int));
    model->face_indices_b = (int*)malloc(faceCount * sizeof(int));
    model->face_indices_c = (int*)malloc(faceCount * sizeof(int));
    model->face_colors = (int*)malloc(faceCount * sizeof(int));
    model->face_priorities = (int*)malloc(faceCount * sizeof(int));
    model->face_alphas = (int*)malloc(faceCount * sizeof(int));
    model->face_infos = (int*)malloc(faceCount * sizeof(int));
    model->face_textures = (int*)malloc(faceCount * sizeof(int));
    model->face_texture_coords = (int*)malloc(faceCount * sizeof(int));

    // Allocate memory for texture coordinates if needed
    if( texTriangleCount > 0 )
    {
        model->textured_p_coordinate = (int*)malloc(texTriangleCount * sizeof(int));
        memset(model->textured_p_coordinate, 0, texTriangleCount * sizeof(int));
        model->textured_m_coordinate = (int*)malloc(texTriangleCount * sizeof(int));
        memset(model->textured_m_coordinate, 0, texTriangleCount * sizeof(int));
        model->textured_n_coordinate = (int*)malloc(texTriangleCount * sizeof(int));
        memset(model->textured_n_coordinate, 0, texTriangleCount * sizeof(int));
    }

    // Read vertex data
    int previousVertexX = 0;
    int previousVertexY = 0;
    int previousVertexZ = 0;
    offset = texTriangleCount; // Start after texture render types

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

        // Read vertex bone mapping if present
        if( hasPackedVertexGroups == 1 && model->vertex_bone_map )
        {
            model->vertex_bone_map[i] = read_byte(inputData, &offsetOfPackedVertexGroups);
        }
    }

    // Skip Maya groups data if present (not supported in current struct)
    if( hasMayaGroups == 1 )
    {
        for( int i = 0; i < vertexCount; i++ )
        {
            int groupCount = read_byte(inputData, &offsetOfPackedVertexGroups);
            for( int j = 0; j < groupCount; j++ )
            {
                read_byte(inputData, &offsetOfPackedVertexGroups); // Skip group
                read_byte(inputData, &offsetOfPackedVertexGroups); // Skip scale
            }
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
            int face_type = read_byte(inputData, &renderTypesOffset);
            assert(face_type <= 3);
            model->face_infos[i] = face_type;
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

        if( hasFaceTextures == 1 )
        {
            // read_byte(inputData, &transparencyGroupsOffset);
        }

        // Skip face textures and texture coordinates (not supported in current struct)
        if( hasFaceTextures == 1 )
        {
            model->face_alphas[i] =
                read_unsigned_short(inputData, &texturesOffset); // Skip texture data
        }
    }

    // Read face indices
    offset = offsetOfFaceTransparencies + (hasFaceTransparencies == 1 ? faceCount : 0);
    int compressionTypesOffset = offsetOfFaceIndices;
    int previousIndex1 = 0;
    int previousIndex2 = 0;
    int previousIndex3 = 0;
    int previousIndex3Copy = 0;

    for( int i = 0; i < faceCount; i++ )
    {
        int compressionType = read_byte(inputData, &compressionTypesOffset);

        if( compressionType == 1 )
        {
            previousIndex1 = read_short_smart(inputData, &offset) + previousIndex3;
            previousIndex2 = read_short_smart(inputData, &offset) + previousIndex1;
            previousIndex3 = read_short_smart(inputData, &offset) + previousIndex2;
            previousIndex3Copy = previousIndex3;
        }
        else if( compressionType == 2 )
        {
            previousIndex2 = previousIndex3;
            previousIndex3 = read_short_smart(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3;
        }
        else if( compressionType == 3 )
        {
            previousIndex1 = previousIndex3;
            previousIndex3 = read_short_smart(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3;
        }
        else if( compressionType == 4 )
        {
            int temp = previousIndex1;
            previousIndex1 = previousIndex2;
            previousIndex2 = temp;
            previousIndex3 = read_short_smart(inputData, &offset) + previousIndex3Copy;
            previousIndex3Copy = previousIndex3;
        }

        model->face_indices_a[i] = previousIndex1;
        model->face_indices_b[i] = previousIndex2;
        model->face_indices_c[i] = previousIndex3;
    }

    // Read texture mapping data for simple textures
    if( texTriangleCount > 0 && model->textured_p_coordinate && model->textured_m_coordinate &&
        model->textured_n_coordinate )
    {
        offset = offsetOfSimpleTextureMapping;
        for( int i = 0; i < texTriangleCount; i++ )
        {
            // Only read simple texture mapping (type 0)
            // Complex texture mapping is skipped as it's not supported in current struct
            if( i < simpleTextureFaceCount )
            {
                model->textured_p_coordinate[i] = read_unsigned_short(inputData, &offset);
                model->textured_m_coordinate[i] = read_unsigned_short(inputData, &offset);
                model->textured_n_coordinate[i] = read_unsigned_short(inputData, &offset);
            }
        }
    }

    // Skip complex texture data (not supported in current struct)
    // This includes texture scales, rotation, direction, speed, and cube texture data

    // Skip final data section (ModelData0)
    offset = offsetOfComplexTextureSpeed + complexTextureFaceCount * 2 + cubeTextureFaceCount * 2;
    int finalData = read_byte(inputData, &offset);
    if( finalData != 0 )
    {
        // Skip ModelData0 structure
        read_unsigned_short(inputData, &offset);
        read_unsigned_short(inputData, &offset);
        read_unsigned_short(inputData, &offset);
        // Skip int value (4 bytes)
        offset += 4;
    }

    // Set model priority
    model->model_priority = faceRenderPriority;

    return model;
}

struct CacheModel*
model_new_from_cache(struct Cache* cache, int model_id)
{
    struct CacheModel* model = NULL;
    struct CacheArchive* archive = cache_archive_new_load(cache, CACHE_MODELS, model_id);
    if( !archive )
    {
        printf("Failed to load model %d\n", model_id);
        return NULL;
    }

    if( model_id == 2251 )
    {
        int iiii = 0;
        // write_model_separate(model, "mode/l_1000");
    }

    model = model_new_decode(archive->data, archive->data_size);

    cache_archive_free(archive);
    if( model )
        model->_id = model_id;

    return model;
}

struct CacheModel*
model_new_decode(const unsigned char* inputData, int inputLength)
{
    // Check the last two bytes to determine model type
    if( inputLength >= 2 )
    {
        unsigned char lastByte = inputData[inputLength - 1];
        unsigned char secondLastByte = inputData[inputLength - 2];

        if( lastByte == 0xFD && secondLastByte == 0xFF )
        { // -3, -1
            return decodeType3(inputData, inputLength);
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
write_model_separate(const struct CacheModel* model, const char* filename)
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
modelbones_free(struct CacheModelBones* modelbones)
{
    for( int i = 0; i < modelbones->bones_count; i++ )
        free(modelbones->bones[i]);
    free(modelbones->bones);
    free(modelbones->bones_sizes);

    free(modelbones);
}

void
model_free(struct CacheModel* model)
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
    if( model->textured_p_coordinate )
        free(model->textured_p_coordinate);
    if( model->textured_m_coordinate )
        free(model->textured_m_coordinate);
    if( model->textured_n_coordinate )
        free(model->textured_n_coordinate);

    free(model);
}