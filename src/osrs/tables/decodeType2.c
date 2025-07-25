#include "model.h"

#include <stdlib.h>
#include <string.h>

// Helper functions for reading data
static unsigned char
read_byte(const unsigned char* buffer, int* offset)
{
    return buffer[(*offset)++] & 0xFF;
}

static unsigned short
read_unsigned_short(const unsigned char* buffer, int* offset)
{
    unsigned short value = (buffer[*offset] << 8) | buffer[*offset + 1];
    *offset += 2;
    return value & 0xFFFF;
}

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

static char
read_signed_byte(const unsigned char* buffer, int* offset)
{
    return (char)buffer[(*offset)++];
}

struct CacheModel*
decodeType2(const unsigned char* var1, int var1_length)
{
    struct CacheModel* def = (struct CacheModel*)malloc(sizeof(struct CacheModel));
    if( !def )
        return NULL;
    memset(def, 0, sizeof(struct CacheModel));

    // Special case for model ID 637 (debugging)
    // if (def->_id == 637) {
    //     int i = 0;
    // }

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
    int var11 = read_byte(var1, &var4_offset);           // numTextureFaces
    int var12 = read_byte(var1, &var4_offset);           // hasFaceRenderTypes
    int var13 = read_byte(var1, &var4_offset);           // faceRenderPriority
    int var14 = read_byte(var1, &var4_offset);           // hasFaceTransparencies
    int var15 = read_byte(var1, &var4_offset);           // hasPackedTransparencyVertexGroups
    int var16 = read_byte(var1, &var4_offset);           // hasPackedVertexGroups
    int var17 = read_byte(var1, &var4_offset);           // hasAnimayaGroups
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
        def->face_priorities = (int*)malloc(var10 * sizeof(int));
        for( int i = 0; i < var10; i++ )
        {
            def->face_priorities[i] = var13;
        }
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
    for( int var40 = 0; var40 < var9; ++var40 )
    {
        int var41 = read_byte(var1, &var4_offset); // vertex flags
        int var42 = 0;                             // delta X
        if( (var41 & 1) != 0 )
        {
            var42 = read_short_smart(var1, &var5_offset);
        }

        int var43 = 0; // delta Y
        if( (var41 & 2) != 0 )
        {
            var43 = read_short_smart(var1, &var6_offset);
        }

        int var44 = 0; // delta Z
        if( (var41 & 4) != 0 )
        {
            var44 = read_short_smart(var1, &var7_offset);
        }

        def->vertices_x[var40] = var37 + var42;
        def->vertices_y[var40] = var38 + var43;
        def->vertices_z[var40] = var39 + var44;
        var37 = def->vertices_x[var40];
        var38 = def->vertices_y[var40];
        var39 = def->vertices_z[var40];

        if( var16 == 1 )
        {
            def->vertex_bone_map[var40] = read_byte(var1, &var8_offset);
        }
    }

    // Read animaya groups if present
    if( var17 == 1 )
    {
        for( int var40 = 0; var40 < var9; ++var40 )
        {
            int var41 = read_byte(var1, &var8_offset);
            // Skip animaya groups and scales as they're not in our struct
            for( int var42 = 0; var42 < var41; ++var42 )
            {
                read_byte(var1, &var8_offset); // animaya group
                read_byte(var1, &var8_offset); // animaya scale
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
    for( int var40 = 0; var40 < var10; ++var40 )
    {
        def->face_colors[var40] = (int)read_unsigned_short(var1, &var4_offset);

        if( var12 == 1 )
        {
            int var41 = read_byte(var1, &var5_offset);
            if( (var41 & 1) == 1 )
            {
                def->face_infos[var40] = 1;
                var2 = 1;
            }
            else
            {
                def->face_infos[var40] = 0;
            }

            if( (var41 & 2) == 2 )
            {
                def->face_texture_coords[var40] = (int)(var41 >> 2);
                def->face_textures[var40] = def->face_colors[var40];
                def->face_colors[var40] = 127;
                if( def->face_textures[var40] != -1 )
                {
                    var3 = 1;
                }
            }
            else
            {
                def->face_texture_coords[var40] = -1;
                def->face_textures[var40] = -1;
            }
        }

        if( var13 == 255 )
        {
            def->face_priorities[var40] = (int)read_signed_byte(var1, &var6_offset);
        }

        if( var14 == 1 )
        {
            def->face_alphas[var40] = (int)read_signed_byte(var1, &var7_offset);
        }

        if( var15 == 1 )
        {
            read_byte(var1, &var8_offset); // Skip packed transparency vertex groups
        }
    }

    // Set up stream offsets for face indices
    var4_offset = var31;
    var5_offset = var25;
    int var40 = 0; // previous index 1
    int var41 = 0; // previous index 2
    int var42 = 0; // previous index 3
    int var43 = 0; // previous index 3 copy

    // Read face indices
    for( int var44 = 0; var44 < var10; ++var44 )
    {
        int var45 = read_byte(var1, &var5_offset);
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
        def->textured_p_coordinate[var44] = (int)read_unsigned_short(var1, &var4_offset);
        def->textured_m_coordinate[var44] = (int)read_unsigned_short(var1, &var4_offset);
        def->textured_n_coordinate[var44] = (int)read_unsigned_short(var1, &var4_offset);
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

        if( !var47 )
        {
            free(def->face_texture_coords);
            def->face_texture_coords = NULL;
        }
    }

    // Clean up unused arrays
    if( !var3 )
    {
        free(def->face_textures);
        def->face_textures = NULL;
    }

    if( !var2 )
    {
        free(def->face_infos);
        def->face_infos = NULL;
    }

    return def;
}
