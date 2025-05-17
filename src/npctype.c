#include <stdbool.h>
#include <stdio.h>
#include <string.h>

struct NPCType
{
    int* models;
    int models_count;
    char* name;
    int tile_spaces_occupied;
    int stance_animation;
    int walk_animation;
    int anInt2165;
    int anInt2189;
    int rotate180_animation;
    int rotate90_right_animation;
    int rotate90_left_animation;
    char* options[5]; // Options 30-34
    short* recolor_to_find;
    short* recolor_to_replace;
    int recolor_count;
    short* retexture_to_find;
    short* retexture_to_replace;
    int retexture_count;
    int* models_2;
    int models_2_count;
    bool render_on_minimap;
    int combat_level;
    int resize_x;
    int resize_y;
    bool has_render_priority;
    int ambient;
    int contrast;
    int head_icon;
    int anInt2156;
    int anInt2174;
    int anInt2187;
    int* anIntArray2185;
    int anIntArray2185_count;
    bool is_clickable;
    bool aBool2170;
    bool aBool2190;
    // HashMap equivalent for params
    struct
    {
        int* keys;
        void** values;
        bool* is_string;
        int count;
        int capacity;
    } params;
};

static void
decode_npc_type(struct NPCType* npc, struct Buffer* buffer)
{
    while( 1 )
    {
        int opcode = read_8(buffer) & 0xFF;
        if( opcode == 0 )
            return;

        if( 1 == opcode )
        {
            int length = read_8(buffer) & 0xFF;
            npc->models = malloc(length * sizeof(int));
            npc->models_count = length;

            for( int idx = 0; idx < length; ++idx )
            {
                npc->models[idx] = read_16(buffer) & 0xFFFF;
            }
        }
        else if( 2 == opcode )
        {
            // Read string length first
            int str_len = 0;
            while( buffer->data[buffer->position + str_len] != '\0' )
            {
                str_len++;
            }
            npc->name = malloc(str_len + 1);
            readto(npc->name, str_len + 1, str_len + 1, buffer);
        }
        else if( 12 == opcode )
        {
            npc->tile_spaces_occupied = read_8(buffer) & 0xFF;
        }
        else if( opcode == 13 )
        {
            npc->stance_animation = read_16(buffer) & 0xFFFF;
        }
        else if( opcode == 14 )
        {
            npc->walk_animation = read_16(buffer) & 0xFFFF;
        }
        else if( 15 == opcode )
        {
            npc->anInt2165 = read_16(buffer) & 0xFFFF;
        }
        else if( opcode == 16 )
        {
            npc->anInt2189 = read_16(buffer) & 0xFFFF;
        }
        else if( 17 == opcode )
        {
            npc->walk_animation = read_16(buffer) & 0xFFFF;
            npc->rotate180_animation = read_16(buffer) & 0xFFFF;
            npc->rotate90_right_animation = read_16(buffer) & 0xFFFF;
            npc->rotate90_left_animation = read_16(buffer) & 0xFFFF;
        }
        else if( opcode >= 30 && opcode < 35 )
        {
            int idx = opcode - 30;
            // Read string length first
            int str_len = 0;
            while( buffer->data[buffer->position + str_len] != '\0' )
            {
                str_len++;
            }
            npc->options[idx] = malloc(str_len + 1);
            readto(npc->options[idx], str_len + 1, str_len + 1, buffer);

            // Check if string is "Hidden"
            if( strcmp(npc->options[idx], "Hidden") == 0 )
            {
                free(npc->options[idx]);
                npc->options[idx] = NULL;
            }
        }
        else if( opcode == 40 )
        {
            int length = read_8(buffer) & 0xFF;
            npc->recolor_to_find = malloc(length * sizeof(short));
            npc->recolor_to_replace = malloc(length * sizeof(short));
            npc->recolor_count = length;

            for( int idx = 0; idx < length; ++idx )
            {
                npc->recolor_to_find[idx] = (short)(read_16(buffer) & 0xFFFF);
                npc->recolor_to_replace[idx] = (short)(read_16(buffer) & 0xFFFF);
            }
        }
        else if( opcode == 41 )
        {
            int length = read_8(buffer) & 0xFF;
            npc->retexture_to_find = malloc(length * sizeof(short));
            npc->retexture_to_replace = malloc(length * sizeof(short));
            npc->retexture_count = length;

            for( int idx = 0; idx < length; ++idx )
            {
                npc->retexture_to_find[idx] = (short)(read_16(buffer) & 0xFFFF);
                npc->retexture_to_replace[idx] = (short)(read_16(buffer) & 0xFFFF);
            }
        }
        else if( 60 == opcode )
        {
            int length = read_8(buffer) & 0xFF;
            npc->models_2 = malloc(length * sizeof(int));
            npc->models_2_count = length;

            for( int idx = 0; idx < length; ++idx )
            {
                npc->models_2[idx] = read_16(buffer) & 0xFFFF;
            }
        }
        else if( opcode == 93 )
        {
            npc->render_on_minimap = false;
        }
        else if( 95 == opcode )
        {
            npc->combat_level = read_16(buffer) & 0xFFFF;
        }
        else if( 97 == opcode )
        {
            npc->resize_x = read_16(buffer) & 0xFFFF;
        }
        else if( 98 == opcode )
        {
            npc->resize_y = read_16(buffer) & 0xFFFF;
        }
        else if( opcode == 99 )
        {
            npc->has_render_priority = true;
        }
        else if( 100 == opcode )
        {
            npc->ambient = read_8(buffer);
        }
        else if( 101 == opcode )
        {
            npc->contrast = read_8(buffer);
        }
        else if( opcode == 102 )
        {
            npc->head_icon = read_16(buffer) & 0xFFFF;
        }
        else if( 103 == opcode )
        {
            npc->anInt2156 = read_16(buffer) & 0xFFFF;
        }
        else if( opcode == 106 )
        {
            npc->anInt2174 = read_16(buffer) & 0xFFFF;
            if( 0xFFFF == npc->anInt2174 )
            {
                npc->anInt2174 = -1;
            }

            npc->anInt2187 = read_16(buffer) & 0xFFFF;
            if( 0xFFFF == npc->anInt2187 )
            {
                npc->anInt2187 = -1;
            }

            int length = read_8(buffer) & 0xFF;
            npc->anIntArray2185 = malloc((length + 2) * sizeof(int));
            npc->anIntArray2185_count = length + 2;

            for( int idx = 0; idx <= length; ++idx )
            {
                npc->anIntArray2185[idx] = read_16(buffer) & 0xFFFF;
                if( npc->anIntArray2185[idx] == 0xFFFF )
                {
                    npc->anIntArray2185[idx] = -1;
                }
            }

            npc->anIntArray2185[length + 1] = -1;
        }
        else if( 107 == opcode )
        {
            npc->is_clickable = false;
        }
        else if( opcode == 109 )
        {
            npc->aBool2170 = false;
        }
        else if( opcode == 111 )
        {
            npc->aBool2190 = true;
        }
        else if( opcode == 118 )
        {
            npc->anInt2174 = read_16(buffer) & 0xFFFF;
            if( 0xFFFF == npc->anInt2174 )
            {
                npc->anInt2174 = -1;
            }

            npc->anInt2187 = read_16(buffer) & 0xFFFF;
            if( 0xFFFF == npc->anInt2187 )
            {
                npc->anInt2187 = -1;
            }

            int var = read_16(buffer) & 0xFFFF;
            if( var == 0xFFFF )
            {
                var = -1;
            }

            int length = read_8(buffer) & 0xFF;
            npc->anIntArray2185 = malloc((length + 2) * sizeof(int));
            npc->anIntArray2185_count = length + 2;

            for( int idx = 0; idx <= length; ++idx )
            {
                npc->anIntArray2185[idx] = read_16(buffer) & 0xFFFF;
                if( npc->anIntArray2185[idx] == 0xFFFF )
                {
                    npc->anIntArray2185[idx] = -1;
                }
            }

            npc->anIntArray2185[length + 1] = var;
        }
        else if( opcode == 249 )
        {
            int length = read_8(buffer) & 0xFF;

            // Initialize params with next power of 2 size
            int capacity = 1;
            while( capacity < length )
            {
                capacity <<= 1;
            }

            npc->params.keys = malloc(capacity * sizeof(int));
            npc->params.values = malloc(capacity * sizeof(void*));
            npc->params.is_string = malloc(capacity * sizeof(bool));
            npc->params.count = 0;
            npc->params.capacity = capacity;

            for( int i = 0; i < length; i++ )
            {
                bool is_string = (read_8(buffer) & 0xFF) == 1;
                int key = read_24(buffer);
                void* value;

                if( is_string )
                {
                    // Read string length first
                    int str_len = 0;
                    while( buffer->data[buffer->position + str_len] != '\0' )
                    {
                        str_len++;
                    }
                    value = malloc(str_len + 1);
                    readto(value, str_len + 1, str_len + 1, buffer);
                }
                else
                {
                    value = malloc(sizeof(int));
                    *(int*)value = read_32(buffer);
                }

                npc->params.keys[npc->params.count] = key;
                npc->params.values[npc->params.count] = value;
                npc->params.is_string[npc->params.count] = is_string;
                npc->params.count++;
            }
        }
    }
}

void
print_npc_type(const struct NPCType* npc)
{
    printf("NPC Type Information:\n");
    printf("====================\n");

    // Print name
    printf("Name: %s\n", npc->name ? npc->name : "NULL");

    // Print models
    printf("Models (%d): ", npc->models_count);
    if( npc->models )
    {
        for( int i = 0; i < npc->models_count; i++ )
        {
            printf("%d ", npc->models[i]);
        }
    }
    printf("\n");

    // Print models_2
    printf("Models 2 (%d): ", npc->models_2_count);
    if( npc->models_2 )
    {
        for( int i = 0; i < npc->models_2_count; i++ )
        {
            printf("%d ", npc->models_2[i]);
        }
    }
    printf("\n");

    // Print animations
    printf("Animations:\n");
    printf("  Stance: %d\n", npc->stance_animation);
    printf("  Walk: %d\n", npc->walk_animation);
    printf("  Rotate 180: %d\n", npc->rotate180_animation);
    printf("  Rotate 90 Right: %d\n", npc->rotate90_right_animation);
    printf("  Rotate 90 Left: %d\n", npc->rotate90_left_animation);

    // Print options
    printf("Options:\n");
    for( int i = 0; i < 5; i++ )
    {
        printf("  Option %d: %s\n", i + 30, npc->options[i] ? npc->options[i] : "NULL");
    }

    // Print recoloring info
    printf("Recoloring (%d pairs):\n", npc->recolor_count);
    if( npc->recolor_to_find && npc->recolor_to_replace )
    {
        for( int i = 0; i < npc->recolor_count; i++ )
        {
            printf("  %d -> %d\n", npc->recolor_to_find[i], npc->recolor_to_replace[i]);
        }
    }

    // Print retexturing info
    printf("Retexturing (%d pairs):\n", npc->retexture_count);
    if( npc->retexture_to_find && npc->retexture_to_replace )
    {
        for( int i = 0; i < npc->retexture_count; i++ )
        {
            printf("  %d -> %d\n", npc->retexture_to_find[i], npc->retexture_to_replace[i]);
        }
    }

    // Print other properties
    printf("Properties:\n");
    printf("  Tile Spaces Occupied: %d\n", npc->tile_spaces_occupied);
    printf("  Combat Level: %d\n", npc->combat_level);
    printf("  Resize X: %d\n", npc->resize_x);
    printf("  Resize Y: %d\n", npc->resize_y);
    printf("  Ambient: %d\n", npc->ambient);
    printf("  Contrast: %d\n", npc->contrast);
    printf("  Head Icon: %d\n", npc->head_icon);

    // Print flags
    printf("Flags:\n");
    printf("  Render On Minimap: %s\n", npc->render_on_minimap ? "true" : "false");
    printf("  Has Render Priority: %s\n", npc->has_render_priority ? "true" : "false");
    printf("  Is Clickable: %s\n", npc->is_clickable ? "true" : "false");
    printf("  aBool2170: %s\n", npc->aBool2170 ? "true" : "false");
    printf("  aBool2190: %s\n", npc->aBool2190 ? "true" : "false");

    // Print special integers
    printf("Special Integers:\n");
    printf("  anInt2165: %d\n", npc->anInt2165);
    printf("  anInt2189: %d\n", npc->anInt2189);
    printf("  anInt2156: %d\n", npc->anInt2156);
    printf("  anInt2174: %d\n", npc->anInt2174);
    printf("  anInt2187: %d\n", npc->anInt2187);

    // Print anIntArray2185
    printf("anIntArray2185 (%d elements): ", npc->anIntArray2185_count);
    if( npc->anIntArray2185 )
    {
        for( int i = 0; i < npc->anIntArray2185_count; i++ )
        {
            printf("%d ", npc->anIntArray2185[i]);
        }
    }
    printf("\n");

    // Print params
    printf("Params (%d entries):\n", npc->params.count);
    for( int i = 0; i < npc->params.count; i++ )
    {
        printf("  Key: %d, Value: ", npc->params.keys[i]);
        if( npc->params.is_string[i] )
        {
            printf("\"%s\" (string)\n", (char*)npc->params.values[i]);
        }
        else
        {
            printf("%d (integer)\n", *(int*)npc->params.values[i]);
        }
    }
    printf("\n");
}