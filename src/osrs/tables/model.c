#include "model.h"

#include "osrs/cache.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper function to read a byte from the buffer
static uint8_t
read_byte(const char* buffer, int* offset)
{
    return buffer[(*offset)++] & 0xFF;
}

static uint8_t
read_unsigned_byte(const unsigned char* buffer, int* offset)
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

static int
read_int(const unsigned char* buffer, int* offset)
{
    int value = (buffer[*offset] << 24) | (buffer[*offset + 1] << 16) | (buffer[*offset + 2] << 8) |
                buffer[*offset + 3];
    *offset += 4;
    return value;
}

// computeAnimationTables
//
struct ModelBones*
modelbones_new_decode(int* vertex_bone_map, int vertex_bone_map_count)
{
    struct ModelBones* bones = (struct ModelBones*)malloc(sizeof(struct ModelBones));
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
        if( bone >= 0 )
            bones->bones[bone][bones->bones_sizes[bone]++] = i;
    }

    return bones;
}

struct CacheModel*
decodeOldFormat(const unsigned char* inputData, int inputLength)
{
    struct CacheModel* model = (struct CacheModel*)malloc(sizeof(struct CacheModel));
    memset(model, 0, sizeof(struct CacheModel));
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
    int textureCount = read_unsigned_byte(inputData, &offset);
    int isTextured = read_unsigned_byte(inputData, &offset);
    int faceRenderPriority = read_unsigned_byte(inputData, &offset);
    int hasFaceTransparencies = read_unsigned_byte(inputData, &offset);
    int hasPackedTransparencyVertexGroups = read_unsigned_byte(inputData, &offset);
    int hasPackedVertexGroups = read_unsigned_byte(inputData, &offset);
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
    model->face_textures = (int*)malloc(faceCount * sizeof(int));
    model->face_texture_coords = (int*)malloc(faceCount * sizeof(int));

    model->textured_p_coordinate = (int*)malloc(textureCount * sizeof(int));
    model->textured_m_coordinate = (int*)malloc(textureCount * sizeof(int));
    model->textured_n_coordinate = (int*)malloc(textureCount * sizeof(int));

    // Read vertex data
    int previousVertexX = 0;
    int previousVertexY = 0;
    int previousVertexZ = 0;
    offset = offsetOfVertexFlags;

    for( int i = 0; i < vertexCount; i++ )
    {
        int vertexFlags = read_unsigned_byte(inputData, &offset);
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
            int faceTextureFlags = read_unsigned_byte(inputData, &textureFlagsOffset);
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
                model->face_textures[i] = model->face_colors[i];
                model->face_colors[i] = 127;
            }
            else
            {
                model->face_texture_coords[i] = -1;
                model->face_textures[i] = -1;
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
            read_unsigned_byte(
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
        int compressionType = read_unsigned_byte(inputData, &compressionTypesOffset);

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

    offset = offsetOfTextureIndices;

    for( int i = 0; i < textureCount; i++ )
    {
        model->textured_p_coordinate[i] = read_byte(inputData, &offset);
        model->textured_m_coordinate[i] = read_byte(inputData, &offset);
        model->textured_n_coordinate[i] = read_byte(inputData, &offset);
    }

    // Set model priority
    model->model_priority = faceRenderPriority;

    return model;
}

// void decodeType1(ModelDefinition def, byte[] var1)
// 	{
// 		InputStream var2 = new InputStream(var1);
// 		InputStream var3 = new InputStream(var1);
// 		InputStream var4 = new InputStream(var1);
// 		InputStream var5 = new InputStream(var1);
// 		InputStream var6 = new InputStream(var1);
// 		InputStream var7 = new InputStream(var1);
// 		InputStream var8 = new InputStream(var1);
// 		var2.setOffset(var1.length - 23);
// 		int var9 = var2.readUnsignedShort();
// 		int var10 = var2.readUnsignedShort();
// 		int var11 = var2.readUnsignedByte();
// 		int var12 = var2.readUnsignedByte();
// 		int var13 = var2.readUnsignedByte();
// 		int var14 = var2.readUnsignedByte();
// 		int var15 = var2.readUnsignedByte();
// 		int var16 = var2.readUnsignedByte();
// 		int var17 = var2.readUnsignedByte();
// 		int var18 = var2.readUnsignedShort();
// 		int var19 = var2.readUnsignedShort();
// 		int var20 = var2.readUnsignedShort();
// 		int var21 = var2.readUnsignedShort();
// 		int var22 = var2.readUnsignedShort();
// 		int var23 = 0;
// 		int var24 = 0;
// 		int var25 = 0;
// 		int var26;
// 		if (var11 > 0)
// 		{
// 			def.textureRenderTypes = new byte[var11];
// 			var2.setOffset(0);

// 			for (var26 = 0; var26 < var11; ++var26)
// 			{
// 				byte var27 = def.textureRenderTypes[var26] = var2.readByte();
// 				if (var27 == 0)
// 				{
// 					++var23;
// 				}

// 				if (var27 >= 1 && var27 <= 3)
// 				{
// 					++var24;
// 				}

// 				if (var27 == 2)
// 				{
// 					++var25;
// 				}
// 			}
// 		}

// 		var26 = var11 + var9;
// 		int var56 = var26;
// 		if (var12 == 1)
// 		{
// 			var26 += var10;
// 		}

// 		int var28 = var26;
// 		var26 += var10;
// 		int var29 = var26;
// 		if (var13 == 255)
// 		{
// 			var26 += var10;
// 		}

// 		int var30 = var26;
// 		if (var15 == 1)
// 		{
// 			var26 += var10;
// 		}

// 		int var31 = var26;
// 		if (var17 == 1)
// 		{
// 			var26 += var9;
// 		}

// 		int var32 = var26;
// 		if (var14 == 1)
// 		{
// 			var26 += var10;
// 		}

// 		int var33 = var26;
// 		var26 += var21;
// 		int var34 = var26;
// 		if (var16 == 1)
// 		{
// 			var26 += var10 * 2;
// 		}

// 		int var35 = var26;
// 		var26 += var22;
// 		int var36 = var26;
// 		var26 += var10 * 2;
// 		int var37 = var26;
// 		var26 += var18;
// 		int var38 = var26;
// 		var26 += var19;
// 		int var39 = var26;
// 		var26 += var20;
// 		int var40 = var26;
// 		var26 += var23 * 6;
// 		int var41 = var26;
// 		var26 += var24 * 6;
// 		int var42 = var26;
// 		var26 += var24 * 6;
// 		int var43 = var26;
// 		var26 += var24 * 2;
// 		int var44 = var26;
// 		var26 += var24;
// 		int var45 = var26;
// 		var26 = var26 + var24 * 2 + var25 * 2;
// 		def.vertexCount = var9;
// 		def.faceCount = var10;
// 		def.numTextureFaces = var11;
// 		def.vertexX = new int[var9];
// 		def.vertexY = new int[var9];
// 		def.vertexZ = new int[var9];
// 		def.faceIndices1 = new int[var10];
// 		def.faceIndices2 = new int[var10];
// 		def.faceIndices3 = new int[var10];
// 		if (var17 == 1)
// 		{
// 			def.packedVertexGroups = new int[var9];
// 		}

// 		if (var12 == 1)
// 		{
// 			def.faceRenderTypes = new byte[var10];
// 		}

// 		if (var13 == 255)
// 		{
// 			def.faceRenderPriorities = new byte[var10];
// 		}
// 		else
// 		{
// 			def.priority = (byte) var13;
// 		}

// 		if (var14 == 1)
// 		{
// 			def.faceTransparencies = new byte[var10];
// 		}

// 		if (var15 == 1)
// 		{
// 			def.packedTransparencyVertexGroups = new int[var10];
// 		}

// 		if (var16 == 1)
// 		{
// 			def.faceTextures = new short[var10];
// 		}

// 		if (var16 == 1 && var11 > 0)
// 		{
// 			def.textureCoords = new byte[var10];
// 		}

// 		def.faceColors = new short[var10];
// 		if (var11 > 0)
// 		{
// 			def.texIndices1 = new short[var11];
// 			def.texIndices2 = new short[var11];
// 			def.texIndices3 = new short[var11];
// 		}

// 		var2.setOffset(var11);
// 		var3.setOffset(var37);
// 		var4.setOffset(var38);
// 		var5.setOffset(var39);
// 		var6.setOffset(var31);
// 		int var46 = 0;
// 		int var47 = 0;
// 		int var48 = 0;

// 		int var49;
// 		int var50;
// 		int var51;
// 		int var52;
// 		int var53;
// 		for (var49 = 0; var49 < var9; ++var49)
// 		{
// 			var50 = var2.readUnsignedByte();
// 			var51 = 0;
// 			if ((var50 & 1) != 0)
// 			{
// 				var51 = var3.readShortSmart();
// 			}

// 			var52 = 0;
// 			if ((var50 & 2) != 0)
// 			{
// 				var52 = var4.readShortSmart();
// 			}

// 			var53 = 0;
// 			if ((var50 & 4) != 0)
// 			{
// 				var53 = var5.readShortSmart();
// 			}

// 			def.vertexX[var49] = var46 + var51;
// 			def.vertexY[var49] = var47 + var52;
// 			def.vertexZ[var49] = var48 + var53;
// 			var46 = def.vertexX[var49];
// 			var47 = def.vertexY[var49];
// 			var48 = def.vertexZ[var49];
// 			if (var17 == 1)
// 			{
// 				def.packedVertexGroups[var49] = var6.readUnsignedByte();
// 			}
// 		}

// 		var2.setOffset(var36);
// 		var3.setOffset(var56);
// 		var4.setOffset(var29);
// 		var5.setOffset(var32);
// 		var6.setOffset(var30);
// 		var7.setOffset(var34);
// 		var8.setOffset(var35);

// 		for (var49 = 0; var49 < var10; ++var49)
// 		{
// 			def.faceColors[var49] = (short) var2.readUnsignedShort();
// 			if (var12 == 1)
// 			{
// 				def.faceRenderTypes[var49] = var3.readByte();
// 			}

// 			if (var13 == 255)
// 			{
// 				def.faceRenderPriorities[var49] = var4.readByte();
// 			}

// 			if (var14 == 1)
// 			{
// 				def.faceTransparencies[var49] = var5.readByte();
// 			}

// 			if (var15 == 1)
// 			{
// 				def.packedTransparencyVertexGroups[var49] = var6.readUnsignedByte();
// 			}

// 			if (var16 == 1)
// 			{
// 				def.faceTextures[var49] = (short) (var7.readUnsignedShort() - 1);
// 			}

// 			if (def.textureCoords != null && def.faceTextures[var49] != -1)
// 			{
// 				def.textureCoords[var49] = (byte) (var8.readUnsignedByte() - 1);
// 			}
// 		}

// 		var2.setOffset(var33);
// 		var3.setOffset(var28);
// 		var49 = 0;
// 		var50 = 0;
// 		var51 = 0;
// 		var52 = 0;

// 		int var54;
// 		for (var53 = 0; var53 < var10; ++var53)
// 		{
// 			var54 = var3.readUnsignedByte();
// 			if (var54 == 1)
// 			{
// 				var49 = var2.readShortSmart() + var52;
// 				var50 = var2.readShortSmart() + var49;
// 				var51 = var2.readShortSmart() + var50;
// 				var52 = var51;
// 				def.faceIndices1[var53] = var49;
// 				def.faceIndices2[var53] = var50;
// 				def.faceIndices3[var53] = var51;
// 			}

// 			if (var54 == 2)
// 			{
// 				var50 = var51;
// 				var51 = var2.readShortSmart() + var52;
// 				var52 = var51;
// 				def.faceIndices1[var53] = var49;
// 				def.faceIndices2[var53] = var50;
// 				def.faceIndices3[var53] = var51;
// 			}

// 			if (var54 == 3)
// 			{
// 				var49 = var51;
// 				var51 = var2.readShortSmart() + var52;
// 				var52 = var51;
// 				def.faceIndices1[var53] = var49;
// 				def.faceIndices2[var53] = var50;
// 				def.faceIndices3[var53] = var51;
// 			}

// 			if (var54 == 4)
// 			{
// 				int var55 = var49;
// 				var49 = var50;
// 				var50 = var55;
// 				var51 = var2.readShortSmart() + var52;
// 				var52 = var51;
// 				def.faceIndices1[var53] = var49;
// 				def.faceIndices2[var53] = var55;
// 				def.faceIndices3[var53] = var51;
// 			}
// 		}

// 		var2.setOffset(var40);
// 		var3.setOffset(var41);
// 		var4.setOffset(var42);
// 		var5.setOffset(var43);
// 		var6.setOffset(var44);
// 		var7.setOffset(var45);

// 		for (var53 = 0; var53 < var11; ++var53)
// 		{
// 			var54 = def.textureRenderTypes[var53] & 255;
// 			if (var54 == 0)
// 			{
// 				def.texIndices1[var53] = (short) var2.readUnsignedShort();
// 				def.texIndices2[var53] = (short) var2.readUnsignedShort();
// 				def.texIndices3[var53] = (short) var2.readUnsignedShort();
// 			}
// 		}

// 		var2.setOffset(var26);
// 		var53 = var2.readUnsignedByte();
// 		if (var53 != 0)
// 		{
// 			var2.readUnsignedShort();
// 			var2.readUnsignedShort();
// 			var2.readUnsignedShort();
// 			var2.readInt();
// 		}

// 	}

struct CacheModel*
decodeType1(const unsigned char* var1, int var1_length)

{
    struct CacheModel* def = (struct CacheModel*)malloc(sizeof(struct CacheModel));
    memset(def, 0, sizeof(struct CacheModel));

    // Create multiple input streams (offsets) for different data sections
    int var2_offset = var1_length - 23;
    int var3_offset = 0;
    int var4_offset = 0;
    int var5_offset = 0;
    int var6_offset = 0;
    int var7_offset = 0;
    int var8_offset = 0;

    // Read header information
    int var9 = read_unsigned_short(var1, &var2_offset);  // vertexCount
    int var10 = read_unsigned_short(var1, &var2_offset); // faceCount
    int var11 = read_unsigned_byte(var1, &var2_offset);  // textureCount
    int var12 = read_unsigned_byte(var1, &var2_offset);  // hasFaceRenderTypes
    int var13 = read_unsigned_byte(var1, &var2_offset);  // faceRenderPriority
    int var14 = read_unsigned_byte(var1, &var2_offset);  // hasFaceTransparencies
    int var15 = read_unsigned_byte(var1, &var2_offset);  // hasPackedTransparencyVertexGroups
    int var16 = read_unsigned_byte(var1, &var2_offset);  // hasFaceTextures
    int var17 = read_unsigned_byte(var1, &var2_offset);  // hasPackedVertexGroups
    int var18 = read_unsigned_short(var1, &var2_offset); // vertexXDataByteCount
    int var19 = read_unsigned_short(var1, &var2_offset); // vertexYDataByteCount
    int var20 = read_unsigned_short(var1, &var2_offset); // vertexZDataByteCount
    int var21 = read_unsigned_short(var1, &var2_offset); // faceIndexDataByteCount
    int var22 = read_unsigned_short(var1, &var2_offset); // faceColorDataByteCount

    int var23 = 0; // simpleTextureFaceCount
    int var24 = 0; // complexTextureFaceCount
    int var25 = 0; // cubeTextureFaceCount

    // Read texture render types and count different types
    if( var11 > 0 )
    {
        def->textured_face_count = var11;
        // Note: We don't have textureRenderTypes in our struct, so we'll skip reading them
        // but still count the different types for offset calculations

        for( int var26 = 0; var26 < var11; ++var26 )
        {
            unsigned char var27 = read_byte(var1, &var3_offset);
            if( var27 == 0 )
            {
                ++var23;
            }
            if( var27 >= 1 && var27 <= 3 )
            {
                ++var24;
            }
            if( var27 == 2 )
            {
                ++var25;
            }
        }
    }

    // Calculate offsets for different data sections
    int var26 = var11 + var9; // offsetOfVertexFlags
    int var56 = var26;        // offsetOfFaceRenderTypes
    if( var12 == 1 )
    {
        var26 += var10;
    }

    int var28 = var26; // offsetOfFaceRenderPriorities
    var26 += var10;
    int var29 = var26; // offsetOfFaceTransparencies
    if( var13 == 255 )
    {
        var26 += var10;
    }

    int var30 = var26; // offsetOfPackedTransparencyVertexGroups
    if( var15 == 1 )
    {
        var26 += var10;
    }

    int var31 = var26; // offsetOfPackedVertexGroups
    if( var17 == 1 )
    {
        var26 += var9;
    }

    int var32 = var26; // offsetOfFaceTextures
    if( var14 == 1 )
    {
        var26 += var10;
    }

    int var33 = var26; // offsetOfFaceIndices
    var26 += var21;
    int var34 = var26; // offsetOfFaceColors
    if( var16 == 1 )
    {
        var26 += var10 * 2;
    }

    int var35 = var26; // offsetOfTextureCoords
    var26 += var22;
    int var36 = var26; // offsetOfVertexXData
    var26 += var10 * 2;
    int var37 = var26; // offsetOfVertexYData
    var26 += var18;
    int var38 = var26; // offsetOfVertexZData
    var26 += var19;
    int var39 = var26; // offsetOfSimpleTextureMapping
    var26 += var20;
    int var40 = var26; // offsetOfComplexTextureMapping
    var26 += var23 * 6;
    int var41 = var26; // offsetOfComplexTextureScales
    var26 += var24 * 6;
    int var42 = var26; // offsetOfComplexTextureRotation
    var26 += var24 * 6;
    int var43 = var26; // offsetOfComplexTextureDirection
    var26 += var24 * 2;
    int var44 = var26; // offsetOfComplexTextureSpeed
    var26 += var24;
    int var45 = var26; // offsetOfCubeTextureMapping
    var26 = var26 + var24 * 2 + var25 * 2;

    // Set model properties
    def->vertex_count = var9;
    def->face_count = var10;
    def->textured_face_count = var11;

    // Allocate vertex arrays
    def->vertices_x = (int*)malloc(var9 * sizeof(int));
    def->vertices_y = (int*)malloc(var9 * sizeof(int));
    def->vertices_z = (int*)malloc(var9 * sizeof(int));

    // Allocate face arrays
    def->face_indices_a = (int*)malloc(var10 * sizeof(int));
    def->face_indices_b = (int*)malloc(var10 * sizeof(int));
    def->face_indices_c = (int*)malloc(var10 * sizeof(int));

    // Allocate packed vertex groups if needed
    if( var17 == 1 )
    {
        def->vertex_bone_map = (int*)malloc(var9 * sizeof(int));
    }

    // Allocate face render types if needed
    if( var12 == 1 )
    {
        def->face_infos = (int*)malloc(var10 * sizeof(int));
    }

    // Allocate face render priorities if needed
    if( var13 == 255 )
    {
        def->face_priorities = (int*)malloc(var10 * sizeof(int));
    }
    else
    {
        def->model_priority = (int)var13;
    }

    // Allocate face transparencies if needed
    if( var14 == 1 )
    {
        def->face_alphas = (int*)malloc(var10 * sizeof(int));
    }

    // Allocate packed transparency  groups if needed
    if( var15 == 1 )
    {
        // Note: We don't have this field in our struct, so we'll skip it
        def->face_bone_map = (int*)malloc(var10 * sizeof(int));
    }

    // Allocate face textures if needed
    if( var16 == 1 )
    {
        def->face_textures = (int*)malloc(var10 * sizeof(int));
    }

    // Allocate texture coordinates if needed
    if( var16 == 1 && var11 > 0 )
    {
        def->face_texture_coords = (int*)malloc(var10 * sizeof(int));
    }

    // Allocate face colors
    def->face_colors = (int*)malloc(var10 * sizeof(int));

    // Allocate texture indices if needed
    if( var11 > 0 )
    {
        def->textured_p_coordinate = (int*)malloc(var11 * sizeof(int));
        def->textured_m_coordinate = (int*)malloc(var11 * sizeof(int));
        def->textured_n_coordinate = (int*)malloc(var11 * sizeof(int));
    }

    // Set up stream offsets for reading vertex data
    var3_offset = var37; // vertexYData
    var4_offset = var38; // vertexZData
    var5_offset = var39; // simpleTextureMapping
    var6_offset = var31; // packedVertexGroups

    int var46 = 0; // previousVertexX
    int var47 = 0; // previousVertexY
    int var48 = 0; // previousVertexZ

    int var49, var50, var51, var52, var53;

    // Read vertex data
    for( var49 = 0; var49 < var9; ++var49 )
    {
        var50 = read_unsigned_byte(var1, &var3_offset); // vertexFlags
        var51 = 0;
        if( (var50 & 1) != 0 )
        {
            var51 = read_short_smart(var1, &var3_offset);
        }

        var52 = 0;
        if( (var50 & 2) != 0 )
        {
            var52 = read_short_smart(var1, &var4_offset);
        }

        var53 = 0;
        if( (var50 & 4) != 0 )
        {
            var53 = read_short_smart(var1, &var5_offset);
        }

        def->vertices_x[var49] = var46 + var51;
        def->vertices_y[var49] = var47 + var52;
        def->vertices_z[var49] = var48 + var53;
        var46 = def->vertices_x[var49];
        var47 = def->vertices_y[var49];
        var48 = def->vertices_z[var49];

        if( var17 == 1 )
        {
            def->vertex_bone_map[var49] = read_unsigned_byte(var1, &var6_offset);
        }
    }

    // Set up stream offsets for reading face data
    var3_offset = var36;     // faceColors
    var4_offset = var56;     // faceRenderTypes
    var5_offset = var29;     // faceRenderPriorities
    var6_offset = var32;     // faceTransparencies
    var7_offset = var30;     // packedTransparencyVertexGroups
    var8_offset = var34;     // faceTextures
    int var9_offset = var35; // textureCoords

    // Read face data
    for( var49 = 0; var49 < var10; ++var49 )
    {
        def->face_colors[var49] = (int)read_unsigned_short(var1, &var3_offset);

        if( var12 == 1 )
        {
            def->face_infos[var49] = (int)read_byte(var1, &var4_offset);
        }

        if( var13 == 255 )
        {
            def->face_priorities[var49] = (int)read_byte(var1, &var5_offset);
        }

        if( var14 == 1 )
        {
            def->face_alphas[var49] = (int)read_byte(var1, &var6_offset);
        }

        if( var15 == 1 )
        {
            // read_unsigned_byte(var1, &var7_offset);
            def->face_bone_map[var49] = (int)read_unsigned_byte(var1, &var7_offset);
        }

        if( var16 == 1 )
        {
            def->face_textures[var49] = (int)(read_unsigned_short(var1, &var8_offset) - 1);
        }

        if( def->face_texture_coords != NULL && def->face_textures[var49] != -1 )
        {
            def->face_texture_coords[var49] = (int)(read_unsigned_byte(var1, &var9_offset) - 1);
        }
    }

    // Set up stream offsets for reading face indices
    var3_offset = var33; // faceIndices
    var4_offset = var28; // faceRenderTypes (for compression types)
    var49 = 0;           // previousIndex1
    var50 = 0;           // previousIndex2
    var51 = 0;           // previousIndex3
    var52 = 0;           // previousIndex3Copy

    int var54;
    // Read face indices
    for( var53 = 0; var53 < var10; ++var53 )
    {
        var54 = read_unsigned_byte(var1, &var4_offset); // compressionType

        if( var54 == 1 )
        {
            var49 = read_short_smart(var1, &var3_offset) + var52;
            var50 = read_short_smart(var1, &var3_offset) + var49;
            var51 = read_short_smart(var1, &var3_offset) + var50;
            var52 = var51;
            def->face_indices_a[var53] = var49;
            def->face_indices_b[var53] = var50;
            def->face_indices_c[var53] = var51;
        }

        if( var54 == 2 )
        {
            var50 = var51;
            var51 = read_short_smart(var1, &var3_offset) + var52;
            var52 = var51;
            def->face_indices_a[var53] = var49;
            def->face_indices_b[var53] = var50;
            def->face_indices_c[var53] = var51;
        }

        if( var54 == 3 )
        {
            var49 = var51;
            var51 = read_short_smart(var1, &var3_offset) + var52;
            var52 = var51;
            def->face_indices_a[var53] = var49;
            def->face_indices_b[var53] = var50;
            def->face_indices_c[var53] = var51;
        }

        if( var54 == 4 )
        {
            int var55 = var49;
            var49 = var50;
            var50 = var55;
            var51 = read_short_smart(var1, &var3_offset) + var52;
            var52 = var51;
            def->face_indices_a[var53] = var49;
            def->face_indices_b[var53] = var55;
            def->face_indices_c[var53] = var51;
        }
    }

    // Set up stream offsets for reading texture data
    var3_offset = var40; // simpleTextureMapping
    var4_offset = var41; // complexTextureScales
    var5_offset = var42; // complexTextureRotation
    var6_offset = var43; // complexTextureDirection
    var7_offset = var44; // complexTextureSpeed
    var8_offset = var45; // cubeTextureMapping

    // Read texture data (only simple texture mapping for now)
    for( var53 = 0; var53 < var11; ++var53 )
    {
        var54 = read_byte(var1, &var3_offset) & 255; // textureRenderType
        if( var54 == 0 )
        {
            def->textured_p_coordinate[var53] = (int)read_unsigned_short(var1, &var3_offset);
            def->textured_m_coordinate[var53] = (int)read_unsigned_short(var1, &var3_offset);
            def->textured_n_coordinate[var53] = (int)read_unsigned_short(var1, &var3_offset);
        }
        // Note: Complex and cube texture mapping are skipped as they're not in our struct
    }

    // Read final data section
    var3_offset = var26;
    var53 = read_byte(var1, &var3_offset);
    if( var53 != 0 )
    {
        read_unsigned_short(var1, &var3_offset);
        read_unsigned_short(var1, &var3_offset);
        read_unsigned_short(var1, &var3_offset);
        read_int(var1, &var3_offset);
    }

    return def;
}

struct CacheModel*
decodeType2(const unsigned char* var1, int var1_length)
{
    struct CacheModel* def = (struct CacheModel*)malloc(sizeof(struct CacheModel));
    if( !def )
        return NULL;
    memset(def, 0, sizeof(struct CacheModel));

    // Special case for model ID 637 (debugging)

    int var2 = 0; // hasFaceRenderTypes
    int var3 = 0; // hasFaceTextures

    // Create multiple input streams (offsets) for different data sections
    int var4_offset = var1_length - 23;
    int var5_offset = 0;
    int var6_offset = 0;
    int var7_offset = 0;
    int var8_offset = 0;

    // Read header information
    int var9 = read_unsigned_short(var1, &var4_offset);  // vertexCount
    int var10 = read_unsigned_short(var1, &var4_offset); // faceCount
    int var11 = read_unsigned_byte(var1, &var4_offset);  // numTextureFaces
    int var12 = read_unsigned_byte(var1, &var4_offset);  // hasFaceRenderTypes
    int var13 = read_unsigned_byte(var1, &var4_offset);  // faceRenderPriority
    int var14 = read_unsigned_byte(var1, &var4_offset);  // hasFaceTransparencies
    int var15 = read_unsigned_byte(var1, &var4_offset);  // hasPackedTransparencyVertexGroups
    int var16 = read_unsigned_byte(var1, &var4_offset);  // hasPackedVertexGroups
    int var17 = read_unsigned_byte(var1, &var4_offset);  // hasAnimayaGroups
    int var18 = read_unsigned_short(var1, &var4_offset); // vertexXDataByteCount
    int var19 = read_unsigned_short(var1, &var4_offset); // vertexYDataByteCount
    int var20 = read_unsigned_short(var1, &var4_offset); // vertexZDataByteCount
    int var21 = read_unsigned_short(var1, &var4_offset); // faceIndexDataByteCount
    int var22 = read_unsigned_short(var1, &var4_offset); // faceColorDataByteCount

    // Calculate offsets for different data sections
    int var23 = 0;            // start offset
    int var24 = var23 + var9; // vertex flags end
    int var25 = var24;        // face indices start
    var24 += var10;           // face indices end
    int var26 = var24;        // face render priorities start
    if( var13 == 255 )
    {
        var24 += var10;
    }

    int var27 = var24; // packed transparency vertex groups start
    if( var15 == 1 )
    {
        var24 += var10;
    }

    int var28 = var24; // face render types start
    if( var12 == 1 )
    {
        var24 += var10;
    }

    int var29 = var24; // packed vertex groups start
    var24 += var22;    // packed vertex groups end
    int var30 = var24; // face transparencies start
    if( var14 == 1 )
    {
        var24 += var10;
    }

    int var31 = var24;            // face index data start
    var24 += var21;               // face index data end
    int var32 = var24;            // face colors start
    var24 += var10 * 2;           // face colors end
    int var33 = var24;            // texture indices start
    var24 += var11 * 6;           // texture indices end
    int var34 = var24;            // vertex X data start
    var24 += var18;               // vertex X data end
    int var35 = var24;            // vertex Y data start
    var24 += var19;               // vertex Y data end
    int var10000 = var24 + var20; // vertex Z data end

    // Set model properties
    def->vertex_count = var9;
    def->face_count = var10;
    def->textured_face_count = var11;

    // Allocate vertex arrays
    def->vertices_x = (int*)malloc(var9 * sizeof(int));
    def->vertices_y = (int*)malloc(var9 * sizeof(int));
    def->vertices_z = (int*)malloc(var9 * sizeof(int));

    // Allocate face arrays
    def->face_indices_a = (int*)malloc(var10 * sizeof(int));
    def->face_indices_b = (int*)malloc(var10 * sizeof(int));
    def->face_indices_c = (int*)malloc(var10 * sizeof(int));

    // Allocate texture arrays if needed
    if( var11 > 0 )
    {
        def->textureRenderTypes = (unsigned char*)malloc(var11 * sizeof(unsigned char));
        def->textured_p_coordinate = (int*)malloc(var11 * sizeof(int));
        def->textured_m_coordinate = (int*)malloc(var11 * sizeof(int));
        def->textured_n_coordinate = (int*)malloc(var11 * sizeof(int));
    }

    // Allocate packed vertex groups if needed
    if( var16 == 1 )
    {
        def->vertex_bone_map = (int*)malloc(var9 * sizeof(int));
    }

    // Allocate face render types if needed
    if( var12 == 1 )
    {
        def->face_infos = (int*)malloc(var10 * sizeof(int));
        def->face_texture_coords = (int*)malloc(var10 * sizeof(int));
        def->face_textures = (int*)malloc(var10 * sizeof(int));
    }

    // Allocate face render priorities if needed
    if( var13 == 255 )
    {
        def->face_priorities = (int*)malloc(var10 * sizeof(int));
    }
    else
    {
        def->model_priority = (int)var13;
    }

    // Allocate face transparencies if needed
    if( var14 == 1 )
    {
        def->face_alphas = (int*)malloc(var10 * sizeof(int));
    }

    // Allocate packed transparency vertex groups if needed
    if( var15 == 1 )
    {
        // Note: This field is not in our struct, so we'll skip it
        def->face_bone_map = (int*)malloc(var10 * sizeof(int));
    }

    // Allocate animaya groups if needed
    if( var17 == 1 )
    {
        // Note: This field is not in our struct, so we'll skip it
    }

    // Allocate face colors
    def->face_colors = (int*)malloc(var10 * sizeof(int));

    // Set up stream offsets
    var4_offset = var23;
    var5_offset = var34;
    var6_offset = var35;
    var7_offset = var24;
    var8_offset = var29;

    int var37 = 0; // previous X
    int var38 = 0; // previous Y
    int var39 = 0; // previous Z

    // Read vertex data
    int var40, var41, var42, var43, var44;
    for( int i = 0; i < var9; ++i )
    {
        var41 = read_unsigned_byte(var1, &var4_offset); // vertex flags
        var42 = 0;                                      // delta X
        if( (var41 & 1) != 0 )
        {
            var42 = read_short_smart(var1, &var5_offset);
        }

        var43 = 0; // delta Y
        if( (var41 & 2) != 0 )
        {
            var43 = read_short_smart(var1, &var6_offset);
        }

        var44 = 0; // delta Z
        if( (var41 & 4) != 0 )
        {
            var44 = read_short_smart(var1, &var7_offset);
        }

        def->vertices_x[i] = var37 + var42;
        def->vertices_y[i] = var38 + var43;
        def->vertices_z[i] = var39 + var44;
        var37 = def->vertices_x[i];
        var38 = def->vertices_y[i];
        var39 = def->vertices_z[i];

        if( var16 == 1 )
        {
            def->vertex_bone_map[i] = read_unsigned_byte(var1, &var8_offset);
        }
    }

    // Read animaya groups if present
    if( var17 == 1 )
    {
        for( int i = 0; i < var9; ++i )
        {
            var41 = read_unsigned_byte(var1, &var8_offset);
            // Skip animaya groups and scales as they're not in our struct
            for( int j = 0; j < var41; ++j )
            {
                read_unsigned_byte(var1, &var8_offset); // animaya group
                read_unsigned_byte(var1, &var8_offset); // animaya scale
            }
        }
    }

    // Set up stream offsets for face data
    var4_offset = var32;
    var5_offset = var28;
    var6_offset = var26;
    var7_offset = var30;
    var8_offset = var27;

    // Read face data
    for( int i = 0; i < var10; ++i )
    {
        def->face_colors[i] = (int)read_unsigned_short(var1, &var4_offset);

        if( var12 == 1 )
        {
            var41 = read_unsigned_byte(var1, &var5_offset);
            if( (var41 & 1) == 1 )
            {
                def->face_infos[i] = 1;
                var2 = 1;
            }
            else
            {
                def->face_infos[i] = 0;
            }

            if( (var41 & 2) == 2 )
            {
                def->face_texture_coords[i] = (int)(var41 >> 2);
                def->face_textures[i] = def->face_colors[i];
                def->face_colors[i] = 127;
                if( def->face_textures[i] != -1 )
                {
                    var3 = 1;
                }
            }
            else
            {
                def->face_texture_coords[i] = -1;
                def->face_textures[i] = -1;
            }
        }

        if( var13 == 255 )
        {
            def->face_priorities[i] = (int)read_byte(var1, &var6_offset);
        }

        if( var14 == 1 )
        {
            def->face_alphas[i] = read_byte(var1, &var7_offset);
        }

        if( var15 == 1 )
        {
            // read_unsigned_byte(var1, &var8_offset);
            def->face_bone_map[i] = read_unsigned_byte(var1, &var8_offset);
        }
    }

    // Set up stream offsets for face indices
    var4_offset = var31;
    var5_offset = var25;
    var40 = 0;
    var41 = 0;
    var42 = 0;
    var43 = 0;

    // Read face indices
    for( int var44 = 0; var44 < var10; ++var44 )
    {
        int var45 = read_unsigned_byte(var1, &var5_offset);
        if( var45 == 1 )
        {
            var40 = read_short_smart(var1, &var4_offset) + var43;
            var41 = read_short_smart(var1, &var4_offset) + var40;
            var42 = read_short_smart(var1, &var4_offset) + var41;
            var43 = var42;
            def->face_indices_a[var44] = var40;
            def->face_indices_b[var44] = var41;
            def->face_indices_c[var44] = var42;
        }

        if( var45 == 2 )
        {
            var41 = var42;
            var42 = read_short_smart(var1, &var4_offset) + var43;
            var43 = var42;
            def->face_indices_a[var44] = var40;
            def->face_indices_b[var44] = var41;
            def->face_indices_c[var44] = var42;
        }

        if( var45 == 3 )
        {
            var40 = var42;
            var42 = read_short_smart(var1, &var4_offset) + var43;
            var43 = var42;
            def->face_indices_a[var44] = var40;
            def->face_indices_b[var44] = var41;
            def->face_indices_c[var44] = var42;
        }

        if( var45 == 4 )
        {
            int var46 = var40;
            var40 = var41;
            var41 = var46;
            var42 = read_short_smart(var1, &var4_offset) + var43;
            var43 = var42;
            def->face_indices_a[var44] = var40;
            def->face_indices_b[var44] = var46;
            def->face_indices_c[var44] = var42;
        }
    }

    // Read texture indices
    var4_offset = var33;
    for( int var44 = 0; var44 < var11; ++var44 )
    {
        def->textureRenderTypes[var44] = 0;
        int p_coordinate = (int)read_unsigned_short(var1, &var4_offset);
        int m_coordinate = (int)read_unsigned_short(var1, &var4_offset);
        int n_coordinate = (int)read_unsigned_short(var1, &var4_offset);
        def->textured_p_coordinate[var44] = p_coordinate;
        def->textured_m_coordinate[var44] = m_coordinate;
        def->textured_n_coordinate[var44] = n_coordinate;
    }

    // Validate texture coordinates
    if( def->face_texture_coords != NULL )
    {
        int var47 = 0;

        for( int var45 = 0; var45 < var10; ++var45 )
        {
            int var46 = def->face_texture_coords[var45] & 255;
            if( var46 != 255 )
            {
                if( def->face_indices_a[var45] == (def->textured_p_coordinate[var46] & 0xFFFF) &&
                    def->face_indices_b[var45] == (def->textured_m_coordinate[var46] & 0xFFFF) &&
                    def->face_indices_c[var45] == (def->textured_n_coordinate[var46] & 0xFFFF) )
                {
                    def->face_texture_coords[var45] = -1;
                }
                else
                {
                    var47 = 1;
                }
            }
        }

        // if( !var47 )
        // {
        //     free(def->face_texture_coords);
        //     def->face_texture_coords = NULL;
        // }
    }

    // Clean up unused arrays
    // if( !var3 )
    // {
    //     free(def->face_textures);
    //     def->face_textures = NULL;
    // }

    // if( !var2 )
    // {
    //     free(def->face_infos);
    //     def->face_infos = NULL;
    // }

    return def;
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
decodeType3(const unsigned char* var1, int var1_length)
{
    struct CacheModel* def = (struct CacheModel*)malloc(sizeof(struct CacheModel));
    memset(def, 0, sizeof(struct CacheModel));

    // Create multiple input streams (offsets) for different data sections
    int var2_offset = var1_length - 26;
    int var3_offset = 0;
    int var4_offset = 0;
    int var5_offset = 0;
    int var6_offset = 0;
    int var7_offset = 0;
    int var8_offset = 0;

    // Read header information
    int vertexCount = read_unsigned_short(var1, &var2_offset);
    int faceCount = read_unsigned_short(var1, &var2_offset);
    int texTriangleCount = read_unsigned_byte(var1, &var2_offset);
    int hasFaceInfos = read_unsigned_byte(var1, &var2_offset);
    int hasFacePriorities = read_unsigned_byte(var1, &var2_offset);
    int hasFaceAlphas = read_unsigned_byte(var1, &var2_offset);
    int hasPackedTransparencyVertexGroups = read_unsigned_byte(var1, &var2_offset);
    int hasFaceTextures = read_unsigned_byte(var1, &var2_offset);
    int hasPackedVertexGroups = read_unsigned_byte(var1, &var2_offset);
    int var18 = read_unsigned_byte(var1, &var2_offset);
    int var19 = read_unsigned_short(var1, &var2_offset);
    int var20 = read_unsigned_short(var1, &var2_offset);
    int var21 = read_unsigned_short(var1, &var2_offset);
    int var22 = read_unsigned_short(var1, &var2_offset);
    int var23 = read_unsigned_short(var1, &var2_offset);
    int var24 = read_unsigned_short(var1, &var2_offset);

    int var25 = 0; // simpleTextureFaceCount
    int var26 = 0; // complexTextureFaceCount
    int var27 = 0; // cubeTextureFaceCount

    // Read texture render types and count different types
    if( texTriangleCount > 0 )
    {
        def->textureRenderTypes = (unsigned char*)malloc(texTriangleCount * sizeof(unsigned char));
        var2_offset = 0;

        for( int var28 = 0; var28 < texTriangleCount; ++var28 )
        {
            unsigned char var29 = def->textureRenderTypes[var28] = read_byte(var1, &var2_offset);
            if( var29 == 0 )
            {
                ++var25;
            }

            if( var29 >= 1 && var29 <= 3 )
            {
                ++var26;
            }

            if( var29 == 2 )
            {
                ++var27;
            }
        }
    }

    // Calculate offsets for different data sections
    int var28 = texTriangleCount + vertexCount;
    int var58 = var28;
    if( hasFaceInfos == 1 )
    {
        var28 += faceCount;
    }

    int var30 = var28;
    var28 += faceCount;
    int var31 = var28;
    if( hasFacePriorities == 255 )
    {
        var28 += faceCount;
    }

    int var32 = var28;
    if( hasPackedTransparencyVertexGroups == 1 )
    {
        var28 += faceCount;
    }

    int var33 = var28;
    var28 += var24;
    int var34 = var28;
    if( hasFaceAlphas == 1 )
    {
        var28 += faceCount;
    }

    int var35 = var28;
    var28 += var22;
    int var36 = var28;
    if( hasFaceTextures == 1 )
    {
        var28 += faceCount * 2;
    }

    int var37 = var28;
    var28 += var23;
    int var38 = var28;
    var28 += faceCount * 2;
    int var39 = var28;
    var28 += var19;
    int var40 = var28;
    var28 += var20;
    int var41 = var28;
    var28 += var21;
    int var42 = var28;
    var28 += var25 * 6;
    int var43 = var28;
    var28 += var26 * 6;
    int var44 = var28;
    var28 += var26 * 6;
    int var45 = var28;
    var28 += var26 * 2;
    int var46 = var28;
    var28 += var26;
    int var47 = var28;
    var28 = var28 + var26 * 2 + var27 * 2;

    // Set basic model properties
    def->vertex_count = vertexCount;
    def->face_count = faceCount;
    def->textured_face_count = texTriangleCount;

    // Allocate vertex arrays
    def->vertices_x = (int*)malloc(vertexCount * sizeof(int));
    def->vertices_y = (int*)malloc(vertexCount * sizeof(int));
    def->vertices_z = (int*)malloc(vertexCount * sizeof(int));

    // Allocate face arrays
    def->face_indices_a = (int*)malloc(faceCount * sizeof(int));
    def->face_indices_b = (int*)malloc(faceCount * sizeof(int));
    def->face_indices_c = (int*)malloc(faceCount * sizeof(int));

    // Allocate packed vertex groups if needed
    if( hasPackedVertexGroups == 1 )
    {
        def->vertex_bone_map = (int*)malloc(vertexCount * sizeof(int));
        // memset(def->vertex_bone_map, 0, vertexCount * sizeof(int));
    }

    // Allocate face render types if needed
    if( hasFaceInfos == 1 )
    {
        def->face_infos = (int*)malloc(faceCount * sizeof(int));
    }

    // Allocate face render priorities if needed
    if( hasFacePriorities == 255 )
    {
        def->face_priorities = (int*)malloc(faceCount * sizeof(int));
    }
    else
    {
        def->model_priority = (int)hasFacePriorities;
    }

    // Allocate face transparencies if needed
    if( hasFaceAlphas == 1 )
    {
        def->face_alphas = (int*)malloc(faceCount * sizeof(int));
    }

    // Allocate packed transparency vertex groups if needed
    if( hasPackedTransparencyVertexGroups == 1 )
    {
        def->face_bone_map = (int*)malloc(faceCount * sizeof(int));
        memset(def->face_bone_map, 0, faceCount * sizeof(int));
    }

    // Allocate face textures if needed
    if( hasFaceTextures == 1 )
    {
        def->face_textures = (int*)malloc(faceCount * sizeof(int));
    }

    // Allocate texture coordinates if needed
    if( hasFaceTextures == 1 && texTriangleCount > 0 )
    {
        def->face_texture_coords = (int*)malloc(faceCount * sizeof(int));
    }

    // Allocate animaya groups if needed
    if( var18 == 1 )
    {
        // Note: This field is not in our struct, so we'll skip it
    }

    // Allocate face colors
    def->face_colors = (int*)malloc(faceCount * sizeof(int));

    // Allocate texture indices if needed
    if( texTriangleCount > 0 )
    {
        def->textured_p_coordinate = (int*)malloc(texTriangleCount * sizeof(int));
        def->textured_m_coordinate = (int*)malloc(texTriangleCount * sizeof(int));
        def->textured_n_coordinate = (int*)malloc(texTriangleCount * sizeof(int));
    }

    // Set up stream offsets for reading vertex data
    var2_offset = texTriangleCount;
    var3_offset = var39;
    var4_offset = var40;
    var5_offset = var41;
    var6_offset = var33;

    int var48 = 0; // previousVertexX
    int var49 = 0; // previousVertexY
    int var50 = 0; // previousVertexZ

    int var51, var52, var53, var54, var55;

    // Read vertex data
    for( var51 = 0; var51 < vertexCount; ++var51 )
    {
        var52 = read_unsigned_byte(var1, &var2_offset);
        var53 = 0;
        if( (var52 & 1) != 0 )
        {
            var53 = read_short_smart(var1, &var3_offset);
        }

        var54 = 0;
        if( (var52 & 2) != 0 )
        {
            var54 = read_short_smart(var1, &var4_offset);
        }

        var55 = 0;
        if( (var52 & 4) != 0 )
        {
            var55 = read_short_smart(var1, &var5_offset);
        }

        def->vertices_x[var51] = var48 + var53;
        def->vertices_y[var51] = var49 + var54;
        def->vertices_z[var51] = var50 + var55;
        var48 = def->vertices_x[var51];
        var49 = def->vertices_y[var51];
        var50 = def->vertices_z[var51];

        if( hasPackedVertexGroups == 1 )
        {
            def->vertex_bone_map[var51] = read_unsigned_byte(var1, &var6_offset);
        }
    }

    // Read animaya groups if needed
    if( var18 == 1 )
    {
        for( var51 = 0; var51 < vertexCount; ++var51 )
        {
            var52 = read_unsigned_byte(var1, &var6_offset);
            // Note: We don't have animaya groups in our struct, so we'll skip the data
            for( var53 = 0; var53 < var52; ++var53 )
            {
                read_unsigned_byte(var1, &var6_offset); // animayaGroups
                read_unsigned_byte(var1, &var6_offset); // animayaScales
            }
        }
    }

    // Set up stream offsets for reading face data
    var2_offset = var38;
    var3_offset = var58;
    var4_offset = var31;
    var5_offset = var34;
    var6_offset = var32;
    var7_offset = var36;
    var8_offset = var37;

    // Read face data
    for( var51 = 0; var51 < faceCount; ++var51 )
    {
        def->face_colors[var51] = (int)read_unsigned_short(var1, &var2_offset);

        if( hasFaceInfos == 1 )
        {
            int face_info = (int)read_byte(var1, &var3_offset);

            def->face_infos[var51] = face_info;
        }

        if( hasFacePriorities == 255 )
        {
            def->face_priorities[var51] = (int)read_byte(var1, &var4_offset);
        }

        if( hasFaceAlphas == 1 )
        {
            def->face_alphas[var51] = (int)read_byte(var1, &var5_offset);
        }

        if( hasPackedTransparencyVertexGroups == 1 )
        {
            // read_unsigned_byte(var1, &var6_offset);
            def->face_bone_map[var51] = read_unsigned_byte(var1, &var6_offset);
        }

        if( hasFaceTextures == 1 )
        {
            def->face_textures[var51] = (int)(read_unsigned_short(var1, &var7_offset) - 1);
        }

        if( def->face_texture_coords != NULL && def->face_textures[var51] != -1 )
        {
            def->face_texture_coords[var51] = (int)(read_unsigned_byte(var1, &var8_offset) - 1);
        }
    }

    // Set up stream offsets for reading face indices
    var2_offset = var35;
    var3_offset = var30;
    var51 = 0;
    var52 = 0;
    var53 = 0;
    var54 = 0;

    int var56;

    // Read face indices
    for( var55 = 0; var55 < faceCount; ++var55 )
    {
        var56 = read_unsigned_byte(var1, &var3_offset);
        if( var56 == 1 )
        {
            var51 = read_short_smart(var1, &var2_offset) + var54;
            var52 = read_short_smart(var1, &var2_offset) + var51;
            var53 = read_short_smart(var1, &var2_offset) + var52;
            var54 = var53;
            def->face_indices_a[var55] = var51;
            def->face_indices_b[var55] = var52;
            def->face_indices_c[var55] = var53;
        }

        if( var56 == 2 )
        {
            var52 = var53;
            var53 = read_short_smart(var1, &var2_offset) + var54;
            var54 = var53;
            def->face_indices_a[var55] = var51;
            def->face_indices_b[var55] = var52;
            def->face_indices_c[var55] = var53;
        }

        if( var56 == 3 )
        {
            var51 = var53;
            var53 = read_short_smart(var1, &var2_offset) + var54;
            var54 = var53;
            def->face_indices_a[var55] = var51;
            def->face_indices_b[var55] = var52;
            def->face_indices_c[var55] = var53;
        }

        if( var56 == 4 )
        {
            int var57 = var51;
            var51 = var52;
            var52 = var57;
            var53 = read_short_smart(var1, &var2_offset) + var54;
            var54 = var53;
            def->face_indices_a[var55] = var51;
            def->face_indices_b[var55] = var57;
            def->face_indices_c[var55] = var53;
        }
    }

    // Set up stream offsets for reading texture data
    var2_offset = var42;
    var3_offset = var43;
    var4_offset = var44;
    var5_offset = var45;
    var6_offset = var46;
    var7_offset = var47;

    // Read texture indices for simple textures
    for( var55 = 0; var55 < texTriangleCount; ++var55 )
    {
        var56 = def->textureRenderTypes[var55] & 255;
        if( var56 == 0 )
        {
            def->textured_p_coordinate[var55] = (int)read_unsigned_short(var1, &var2_offset);
            def->textured_m_coordinate[var55] = (int)read_unsigned_short(var1, &var2_offset);
            def->textured_n_coordinate[var55] = (int)read_unsigned_short(var1, &var2_offset);
        }
    }

    // Read final data section
    var2_offset = var28;
    var55 = read_unsigned_byte(var1, &var2_offset);
    if( var55 != 0 )
    {
        read_unsigned_short(var1, &var2_offset);
        read_unsigned_short(var1, &var2_offset);
        read_unsigned_short(var1, &var2_offset);
        read_int(var1, &var2_offset);
    }

    return def;
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

    model = model_new_decode(archive->data, archive->data_size);

    cache_archive_free(archive);
    if( model )
        model->_id = model_id;

    return model;
}

struct CacheModel*
model_new_from_archive(struct CacheArchive* archive, int model_id)
{
    struct CacheModel* model = model_new_decode(archive->data, archive->data_size);
    if( model )
        model->_id = model_id;
    return model;
}

struct CacheModel*
model_new_decode(const unsigned char* inputData, int inputLength)
{
    struct CacheModel* model = NULL;
    // Check the last two bytes to determine model type
    if( inputLength >= 2 )
    {
        unsigned char lastByte = inputData[inputLength - 1];
        unsigned char secondLastByte = inputData[inputLength - 2];

        if( lastByte == 0xFD && secondLastByte == 0xFF )
        { // -3, -1
            model = decodeType3(inputData, inputLength);
            assert(model != NULL);

            model->_model_type = 3;
        }
        else if( lastByte == 0xFE && secondLastByte == 0xFF )
        { // -2, -1
            model = decodeType2(inputData, inputLength);
            assert(model != NULL);
            model->_model_type = 2;
        }
        else if( lastByte == 0xFF && secondLastByte == 0xFF )
        { // -1, -1
            model = decodeType1(inputData, inputLength);
            assert(model != NULL);
            model->_model_type = 1;
        }
    }
    else
    {
        model = decodeOldFormat(inputData, inputLength);
        model->_model_type = 0;
    }

    assert(model != NULL);

    // This is a hack. I'm not sure where this is done in the deob,
    // but it appears that if a model has face bone map, but no face alphas,
    // then face alphas are all assumed to be "0" (opaque), this is so the animation can add
    // transparency.
    if( model->face_bone_map )
    {
        if( !model->face_alphas )
        {
            model->face_alphas = (int*)malloc(model->face_count * sizeof(int));
            memset(model->face_alphas, 0, model->face_count * sizeof(int));
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

    return model;
}

struct CacheModel*
model_new_copy(struct CacheModel* model)
{
    struct CacheModel* copy = (struct CacheModel*)malloc(sizeof(struct CacheModel));
    memset(copy, 0, sizeof(struct CacheModel));

    copy->_id = model->_id;
    copy->_model_type = model->_model_type;

    copy->vertex_count = model->vertex_count;
    copy->vertices_x = (int*)malloc(model->vertex_count * sizeof(int));
    copy->vertices_y = (int*)malloc(model->vertex_count * sizeof(int));
    copy->vertices_z = (int*)malloc(model->vertex_count * sizeof(int));

    memcpy(copy->vertices_x, model->vertices_x, model->vertex_count * sizeof(int));
    memcpy(copy->vertices_y, model->vertices_y, model->vertex_count * sizeof(int));
    memcpy(copy->vertices_z, model->vertices_z, model->vertex_count * sizeof(int));

    if( model->vertex_bone_map )
    {
        copy->vertex_bone_map = (int*)malloc(model->vertex_count * sizeof(int));
        memcpy(copy->vertex_bone_map, model->vertex_bone_map, model->vertex_count * sizeof(int));
    }
    if( model->face_bone_map )
    {
        copy->face_bone_map = (int*)malloc(model->face_count * sizeof(int));
        memcpy(copy->face_bone_map, model->face_bone_map, model->face_count * sizeof(int));
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
        copy->face_alphas = (int*)malloc(model->face_count * sizeof(int));
        memcpy(copy->face_alphas, model->face_alphas, model->face_count * sizeof(int));
    }

    if( model->face_infos )
    {
        copy->face_infos = (int*)malloc(model->face_count * sizeof(int));
        memcpy(copy->face_infos, model->face_infos, model->face_count * sizeof(int));
    }

    if( model->face_priorities )
    {
        copy->face_priorities = (int*)malloc(model->face_count * sizeof(int));
        memcpy(copy->face_priorities, model->face_priorities, model->face_count * sizeof(int));
    }

    if( model->face_colors )
    {
        copy->face_colors = (int*)malloc(model->face_count * sizeof(int));
        memcpy(copy->face_colors, model->face_colors, model->face_count * sizeof(int));
    }

    copy->model_priority = model->model_priority;
    copy->textured_face_count = model->textured_face_count;

    if( model->textured_p_coordinate )
    {
        copy->textured_p_coordinate = (int*)malloc(model->textured_face_count * sizeof(int));
        memcpy(
            copy->textured_p_coordinate,
            model->textured_p_coordinate,
            model->textured_face_count * sizeof(int));
    }

    if( model->textured_m_coordinate )
    {
        copy->textured_m_coordinate = (int*)malloc(model->textured_face_count * sizeof(int));
        memcpy(
            copy->textured_m_coordinate,
            model->textured_m_coordinate,
            model->textured_face_count * sizeof(int));
    }

    if( model->textured_n_coordinate )
    {
        copy->textured_n_coordinate = (int*)malloc(model->textured_face_count * sizeof(int));

        memcpy(
            copy->textured_n_coordinate,
            model->textured_n_coordinate,
            model->textured_face_count * sizeof(int));
    }

    if( model->face_textures )
    {
        copy->face_textures = (int*)malloc(model->face_count * sizeof(int));
        memcpy(copy->face_textures, model->face_textures, model->face_count * sizeof(int));
    }

    if( model->face_texture_coords )
    {
        copy->face_texture_coords = (int*)malloc(model->face_count * sizeof(int));
        memcpy(
            copy->face_texture_coords, model->face_texture_coords, model->face_count * sizeof(int));
    }

    if( model->textureRenderTypes )
    {
        copy->textureRenderTypes =
            (unsigned char*)malloc(model->textured_face_count * sizeof(unsigned char));
        memcpy(
            copy->textureRenderTypes,
            model->textureRenderTypes,
            model->textured_face_count * sizeof(unsigned char));
    }

    copy->rotated = model->rotated;

    return copy;
}

static int
copy_vertex(struct CacheModel* model, struct CacheModel* other, int vertex_index)
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

        // TODO: Vertex skins
        if( model->vertex_bone_map )
        {
            model->vertex_bone_map[new_vertex_count] = other->vertex_bone_map[vertex_index];
        }

        new_vertex_count = model->vertex_count++;
    }

    return new_vertex_count;
}

struct CacheModel*
model_new_merge(struct CacheModel** models, int model_count)
{
    struct CacheModel* model = (struct CacheModel*)malloc(sizeof(struct CacheModel));
    memset(model, 0, sizeof(struct CacheModel));

    model->_model_type = -1;
    model->_id = -1;

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

    for( int i = 0; i < model_count; i++ )
    {
        model->_flags |= models[i]->_flags;
        model->_ids[i] = models[i]->_id;

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

    int* face_alphas = NULL;
    if( has_face_render_alphas )
    {
        face_alphas = (int*)malloc(face_count * sizeof(int));
        memset(face_alphas, 0, face_count * sizeof(int));
    }

    int* face_infos = NULL;
    if( has_face_render_infos )
    {
        face_infos = (int*)malloc(face_count * sizeof(int));
        memset(face_infos, 0, face_count * sizeof(int));
    }

    int* face_priorities = NULL;
    if( has_face_render_prios )
    {
        face_priorities = (int*)malloc(face_count * sizeof(int));
        memset(face_priorities, 0, face_count * sizeof(int));
    }

    int* face_colors = NULL;
    if( has_face_render_colors )
        face_colors = (int*)malloc(face_count * sizeof(int));

    int* textured_p_coordinate = NULL;
    if( has_face_render_textures )
        textured_p_coordinate = (int*)malloc(textured_face_count * sizeof(int));

    int* textured_m_coordinate = NULL;
    if( has_face_render_textures )
        textured_m_coordinate = (int*)malloc(textured_face_count * sizeof(int));

    int* textured_n_coordinate = NULL;
    if( has_face_render_textures )
        textured_n_coordinate = (int*)malloc(textured_face_count * sizeof(int));

    int* vertex_bone_map = NULL;
    if( has_vertex_bones )
    {
        vertex_bone_map = (int*)malloc(vertex_count * sizeof(int));
        memset(vertex_bone_map, 0, vertex_count * sizeof(int));
    }

    int* face_bone_map = NULL;
    if( has_face_bones )
    {
        face_bone_map = (int*)malloc(face_count * sizeof(int));
        memset(face_bone_map, 0, face_count * sizeof(int));
    }

    int* face_textures = NULL;
    int* face_texture_coords = NULL;
    if( has_face_render_textures )
    {
        face_textures = (int*)malloc(face_count * sizeof(int));
        face_texture_coords = (int*)malloc(face_count * sizeof(int));
    }

    unsigned char* textureRenderTypes = NULL;
    if( has_face_render_textures )
        textureRenderTypes = (unsigned char*)malloc(textured_face_count * sizeof(unsigned char));

    // model->vertex_count = vertex_count;
    // model->face_count = face_count;
    // model->textured_face_count = textured_face_count;

    model->textureRenderTypes = textureRenderTypes;

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

    for( int i = 0; i < model_count; i++ )
    {
        for( int j = 0; j < models[i]->face_count; j++ )
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
                    model->face_bone_map[model->face_count] = -1;
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
                vertex_index_p = copy_vertex(model, models[i], models[i]->textured_p_coordinate[j]);

            if( textured_m_coordinate && models[i]->textured_m_coordinate )
                vertex_index_m = copy_vertex(model, models[i], models[i]->textured_m_coordinate[j]);

            if( textured_n_coordinate && models[i]->textured_n_coordinate )
                vertex_index_n = copy_vertex(model, models[i], models[i]->textured_n_coordinate[j]);

            assert(vertex_index_p > -1);
            assert(vertex_index_m > -1);
            assert(vertex_index_n > -1);

            model->textured_p_coordinate[model->textured_face_count] = vertex_index_p;
            model->textured_m_coordinate[model->textured_face_count] = vertex_index_m;
            model->textured_n_coordinate[model->textured_face_count] = vertex_index_n;

            model->face_textures[model->textured_face_count] = models[i]->face_textures[j];
            model->face_texture_coords[model->textured_face_count] =
                models[i]->face_texture_coords[j];

            model->textured_face_count++;
        }
    }

    model->_flags &= ~CMODEL_FLAG_SHARED;

    return model;
}

void
modelbones_free(struct ModelBones* modelbones)
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
model_free(struct CacheModel* model)
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
    if( model->textureRenderTypes )
        free(model->textureRenderTypes);
    if( model->face_textures )
        free(model->face_textures);
    free(model);
}