#include "model.h"

#include <stdlib.h>
#include <string.h>

// Helper function to read a byte from the buffer
static unsigned char
read_byte(const unsigned char* buffer, int* offset)
{
    return buffer[(*offset)++] & 0xFF;
}

// Helper function to read an unsigned short from the buffer
static unsigned short
read_unsigned_short(const unsigned char* buffer, int* offset)
{
    unsigned short value = (buffer[*offset] << 8) | buffer[*offset + 1];
    *offset += 2;
    return value & 0xFFFF;
}

// Helper function to read a short from the buffer
static short
read_short(const unsigned char* buffer, int* offset)
{
    short value = (buffer[*offset] << 8) | buffer[*offset + 1];
    *offset += 2;
    return value;
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

// Helper function to read an int from the buffer
static int
read_int(const unsigned char* buffer, int* offset)
{
    int value = (buffer[*offset] << 24) | (buffer[*offset + 1] << 16) | (buffer[*offset + 2] << 8) |
                buffer[*offset + 3];
    *offset += 4;
    return value;
}

void
decodeType1(struct CacheModel* def, const unsigned char* var1, int var1_length)
{
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
    int var11 = read_byte(var1, &var2_offset);           // textureCount
    int var12 = read_byte(var1, &var2_offset);           // hasFaceRenderTypes
    int var13 = read_byte(var1, &var2_offset);           // faceRenderPriority
    int var14 = read_byte(var1, &var2_offset);           // hasFaceTransparencies
    int var15 = read_byte(var1, &var2_offset);           // hasPackedTransparencyVertexGroups
    int var16 = read_byte(var1, &var2_offset);           // hasFaceTextures
    int var17 = read_byte(var1, &var2_offset);           // hasPackedVertexGroups
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

    // Allocate packed transparency vertex groups if needed
    if( var15 == 1 )
    {
        // Note: We don't have this field in our struct, so we'll skip it
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
        var50 = read_byte(var1, &var3_offset); // vertexFlags
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
            def->vertex_bone_map[var49] = read_byte(var1, &var6_offset);
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
            // Skip packed transparency vertex groups
            read_byte(var1, &var7_offset);
        }

        if( var16 == 1 )
        {
            def->face_textures[var49] = (int)(read_unsigned_short(var1, &var8_offset) - 1);
        }

        if( def->face_texture_coords != NULL && def->face_textures[var49] != -1 )
        {
            def->face_texture_coords[var49] = (int)(read_byte(var1, &var9_offset) - 1);
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
        var54 = read_byte(var1, &var4_offset); // compressionType

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
}
