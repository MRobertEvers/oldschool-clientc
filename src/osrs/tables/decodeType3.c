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
decodeType3(struct CacheModel* def, const unsigned char* var1, int var1_length)
{
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
    int texTriangleCount = read_byte(var1, &var2_offset);
    int var12 = read_byte(var1, &var2_offset);
    int var13 = read_byte(var1, &var2_offset);
    int var14 = read_byte(var1, &var2_offset);
    int hasPackedTransparencyVertexGroups = read_byte(var1, &var2_offset);
    int hasFaceTextures = read_byte(var1, &var2_offset);
    int hasPackedVertexGroups = read_byte(var1, &var2_offset);
    int var18 = read_byte(var1, &var2_offset);
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
    if( var12 == 1 )
    {
        var28 += faceCount;
    }

    int var30 = var28;
    var28 += faceCount;
    int var31 = var28;
    if( var13 == 255 )
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
    if( var14 == 1 )
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
    }

    // Allocate face render types if needed
    if( var12 == 1 )
    {
        def->face_infos = (int*)malloc(faceCount * sizeof(int));
    }

    // Allocate face render priorities if needed
    if( var13 == 255 )
    {
        def->face_priorities = (int*)malloc(faceCount * sizeof(int));
    }
    else
    {
        def->model_priority = (int)var13;
    }

    // Allocate face transparencies if needed
    if( var14 == 1 )
    {
        def->face_alphas = (int*)malloc(faceCount * sizeof(int));
    }

    // Allocate packed transparency vertex groups if needed
    if( hasPackedTransparencyVertexGroups == 1 )
    {
        // Note: This field is not in our struct, so we'll skip it
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
        var52 = read_byte(var1, &var3_offset);
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
            def->vertex_bone_map[var51] = read_byte(var1, &var6_offset);
        }
    }

    // Read animaya groups if needed
    if( var18 == 1 )
    {
        for( var51 = 0; var51 < vertexCount; ++var51 )
        {
            var52 = read_byte(var1, &var6_offset);
            // Note: We don't have animaya groups in our struct, so we'll skip the data
            for( var53 = 0; var53 < var52; ++var53 )
            {
                read_byte(var1, &var6_offset); // animayaGroups
                read_byte(var1, &var6_offset); // animayaScales
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

        if( var12 == 1 )
        {
            def->face_infos[var51] = (int)read_byte(var1, &var3_offset);
        }

        if( var13 == 255 )
        {
            def->face_priorities[var51] = (int)read_byte(var1, &var4_offset);
        }

        if( var14 == 1 )
        {
            def->face_alphas[var51] = (int)read_byte(var1, &var5_offset);
        }

        if( hasPackedTransparencyVertexGroups == 1 )
        {
            // Note: This field is not in our struct, so we'll skip it
            read_byte(var1, &var6_offset);
        }

        if( hasFaceTextures == 1 )
        {
            def->face_textures[var51] = (int)(read_unsigned_short(var1, &var7_offset) - 1);
        }

        if( def->face_texture_coords != NULL && def->face_textures[var51] != -1 )
        {
            def->face_texture_coords[var51] = (int)(read_byte(var1, &var8_offset) - 1);
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
        var56 = read_byte(var1, &var3_offset);
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
    var55 = read_byte(var1, &var2_offset);
    if( var55 != 0 )
    {
        read_unsigned_short(var1, &var2_offset);
        read_unsigned_short(var1, &var2_offset);
        read_unsigned_short(var1, &var2_offset);
        read_int(var1, &var2_offset);
    }

    return def;
}
