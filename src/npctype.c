
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/**
 * Sourced from Runelite!
 *
 */
struct NPCType
{
    int* models;
    int models_count;
    char* name;
    int size;
    int standing_animation;
    int walking_animation;
    int idle_rotate_left_animation;
    int idle_rotate_right_animation;
    int rotate180_animation;
    int rotate_left_animation;
    int rotate_right_animation;
    char* actions[5]; // Options 30-34
    short* recolor_to_find;
    short* recolor_to_replace;
    int recolor_count;
    short* retexture_to_find;
    short* retexture_to_replace;
    int retexture_count;
    int* chathead_models;
    int chathead_models_count;
    bool is_minimap_visible;
    int combat_level;
    int width_scale;
    int height_scale;
    bool has_render_priority;
    int ambient;
    int contrast;
    int* head_icon_archive_ids;
    short* head_icon_sprite_index;
    int head_icon_count;
    int rotation_speed;
    int varbit_id;
    int varp_index;
    int* configs;
    int configs_count;
    bool is_interactable;
    bool rotation_flag;
    bool is_pet;
    int run_animation;
    int run_rotate180_animation;
    int run_rotate_left_animation;
    int run_rotate_right_animation;
    int crawl_animation;
    int crawl_rotate180_animation;
    int crawl_rotate_left_animation;
    int crawl_rotate_right_animation;
    bool low_priority_follower_ops;
    int height;
    int category;
    int stats[6]; // Stats for opcodes 74-79
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

/**
 * @brief
 *
 * Runelite//Users/matthewevers/Documents/git_repos/runelite/cache/src/main/java/net/runelite/cache/definitions/loaders/NpcLoader.java
 *
 * @param npc
 * @param buffer
 */
static void
decode_npc_type(struct NPCType* npc, struct Buffer* buffer)
{
    if( !npc || !buffer || !buffer->data )
    {
        printf("decode_npc_type: Invalid input parameters\n");
        return;
    }

    while( 1 )
    {
        if( buffer->position >= buffer->data_size )
        {
            printf(
                "decode_npc_type: Buffer position %d exceeded data size %d\n",
                buffer->position,
                buffer->data_size);
            return;
        }

        int opcode = read_8(buffer) & 0xFF;
        if( opcode == 0 )
        {
            // printf("decode_npc_type: Reached end of data (opcode 0)\n");
            return;
        }

        switch( opcode )
        {
        case 1:
        {
            if( buffer->position >= buffer->data_size )
            {
                printf("decode_npc_type: Buffer overflow in case 1\n");
                return;
            }
            int length = read_8(buffer) & 0xFF;
            npc->models = malloc(length * sizeof(int));
            if( !npc->models )
            {
                printf("decode_npc_type: Failed to allocate models array of size %d\n", length);
                return;
            }
            npc->models_count = length;

            for( int idx = 0; idx < length; ++idx )
            {
                if( buffer->position + 1 >= buffer->data_size )
                {
                    printf(
                        "decode_npc_type: Buffer overflow while reading models at index %d\n", idx);
                    free(npc->models);
                    npc->models = NULL;
                    npc->models_count = 0;
                    return;
                }
                npc->models[idx] = read_16(buffer) & 0xFFFF;
            }
            break;
        }
        case 2:
        {
            // Read string length first
            int str_len = 0;
            while( buffer->position + str_len < buffer->data_size &&
                   buffer->data[buffer->position + str_len] != '\0' )
            {
                str_len++;
            }
            if( buffer->position + str_len >= buffer->data_size )
            {
                printf("decode_npc_type: Buffer overflow while reading name string\n");
                return;
            }
            npc->name = malloc(str_len + 1);
            if( !npc->name )
            {
                printf("decode_npc_type: Failed to allocate name string of length %d\n", str_len);
                return;
            }
            memset(npc->name, 0, str_len + 1);
            readto(npc->name, str_len + 1, str_len + 1, buffer);
            break;
        }
        case 12:
        {
            npc->size = read_8(buffer) & 0xFF;
            break;
        }
        case 13:
        {
            npc->standing_animation = read_16(buffer) & 0xFFFF;
            break;
        }
        case 14:
        {
            npc->walking_animation = read_16(buffer) & 0xFFFF;
            break;
        }
        case 15:
        {
            npc->idle_rotate_left_animation = read_16(buffer) & 0xFFFF;
            break;
        }
        case 16:
        {
            npc->idle_rotate_right_animation = read_16(buffer) & 0xFFFF;
            break;
        }
        case 17:
        {
            npc->walking_animation = read_16(buffer) & 0xFFFF;
            npc->rotate180_animation = read_16(buffer) & 0xFFFF;
            npc->rotate_left_animation = read_16(buffer) & 0xFFFF;
            npc->rotate_right_animation = read_16(buffer) & 0xFFFF;
            break;
        }
        case 30:
        case 31:
        case 32:
        case 33:
        case 34:
        {
            int idx = opcode - 30;
            // Read string length first
            int str_len = 0;
            while( buffer->position + str_len < buffer->data_size &&
                   buffer->data[buffer->position + str_len] != '\0' )
            {
                str_len++;
            }
            if( buffer->position + str_len >= buffer->data_size )
            {
                return;
            }
            npc->actions[idx] = malloc(str_len + 1);
            readto(npc->actions[idx], str_len + 1, str_len + 1, buffer);

            // Check if string is "Hidden"
            if( strcmp(npc->actions[idx], "Hidden") == 0 )
            {
                free(npc->actions[idx]);
                npc->actions[idx] = NULL;
            }
            break;
        }
        case 40:
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
            break;
        }
        case 41:
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
            break;
        }
        case 60:
        {
            int length = read_8(buffer) & 0xFF;
            npc->chathead_models = malloc(length * sizeof(int));
            npc->chathead_models_count = length;

            for( int idx = 0; idx < length; ++idx )
            {
                npc->chathead_models[idx] = read_16(buffer) & 0xFFFF;
            }
            break;
        }
        case 93:
        {
            npc->is_minimap_visible = false;
            break;
        }
        case 95:
        {
            npc->combat_level = read_16(buffer) & 0xFFFF;
            break;
        }
        case 97:
        {
            npc->width_scale = read_16(buffer) & 0xFFFF;
            break;
        }
        case 98:
        {
            npc->height_scale = read_16(buffer) & 0xFFFF;
            break;
        }
        case 99:
        {
            npc->has_render_priority = true;
            break;
        }
        case 100:
        {
            npc->ambient = read_8(buffer);
            break;
        }
        case 101:
        {
            npc->contrast = read_8(buffer);
            break;
        }
        case 102:
        {
            bool rev210_head_icons = true; // TODO: Make this configurable
            int default_head_icon_archive = -1;

            if( !rev210_head_icons )
            {
                npc->head_icon_archive_ids = malloc(sizeof(int));
                npc->head_icon_sprite_index = malloc(sizeof(short));
                npc->head_icon_count = 1;
                npc->head_icon_archive_ids[0] = default_head_icon_archive;
                npc->head_icon_sprite_index[0] = (short)(read_16(buffer) & 0xFFFF);
            }
            else
            {
                int bitfield = read_8(buffer) & 0xFF;
                int len = 0;
                for( int var5 = bitfield; var5 != 0; var5 >>= 1 )
                {
                    ++len;
                }

                npc->head_icon_archive_ids = malloc(len * sizeof(int));
                npc->head_icon_sprite_index = malloc(len * sizeof(short));
                npc->head_icon_count = len;

                for( int i = 0; i < len; i++ )
                {
                    if( (bitfield & (1 << i)) == 0 )
                    {
                        npc->head_icon_archive_ids[i] = -1;
                        npc->head_icon_sprite_index[i] = -1;
                    }
                    else
                    {
                        npc->head_icon_archive_ids[i] = get_smart_int(buffer);
                        npc->head_icon_sprite_index[i] = (short)(read_16(buffer) & 0xFFFF) - 1;
                    }
                }
            }
            break;
        }
        case 103:
        {
            npc->rotation_speed = read_16(buffer) & 0xFFFF;
            break;
        }
        case 106:
        {
            npc->varbit_id = read_16(buffer) & 0xFFFF;
            if( npc->varbit_id == 0xFFFF )
            {
                npc->varbit_id = -1;
            }

            npc->varp_index = read_16(buffer) & 0xFFFF;
            if( npc->varp_index == 0xFFFF )
            {
                npc->varp_index = -1;
            }

            int length = read_8(buffer) & 0xFF;
            npc->configs = malloc((length + 2) * sizeof(int));
            npc->configs_count = length + 2;

            for( int idx = 0; idx <= length; ++idx )
            {
                npc->configs[idx] = read_16(buffer) & 0xFFFF;
                if( npc->configs[idx] == 0xFFFF )
                {
                    npc->configs[idx] = -1;
                }
            }

            npc->configs[length + 1] = -1;
            break;
        }
        case 107:
        {
            npc->is_interactable = false;
            break;
        }
        case 109:
        {
            npc->rotation_flag = false;
            break;
        }
        case 111:
        {
            npc->is_pet = true;
            break;
        }
        case 114:
        {
            npc->run_animation = read_16(buffer) & 0xFFFF;
            break;
        }
        case 115:
        {
            npc->run_animation = read_16(buffer) & 0xFFFF;
            npc->run_rotate180_animation = read_16(buffer) & 0xFFFF;
            npc->run_rotate_left_animation = read_16(buffer) & 0xFFFF;
            npc->run_rotate_right_animation = read_16(buffer) & 0xFFFF;
            break;
        }
        case 116:
        {
            npc->crawl_animation = read_16(buffer) & 0xFFFF;
            break;
        }
        case 117:
        {
            npc->crawl_animation = read_16(buffer) & 0xFFFF;
            npc->crawl_rotate180_animation = read_16(buffer) & 0xFFFF;
            npc->crawl_rotate_left_animation = read_16(buffer) & 0xFFFF;
            npc->crawl_rotate_right_animation = read_16(buffer) & 0xFFFF;
            break;
        }
        case 118:
        {
            npc->varbit_id = read_16(buffer) & 0xFFFF;
            if( npc->varbit_id == 0xFFFF )
            {
                npc->varbit_id = -1;
            }

            npc->varp_index = read_16(buffer) & 0xFFFF;
            if( npc->varp_index == 0xFFFF )
            {
                npc->varp_index = -1;
            }

            int var = read_16(buffer) & 0xFFFF;
            if( var == 0xFFFF )
            {
                var = -1;
            }

            int length = read_8(buffer) & 0xFF;
            npc->configs = malloc((length + 2) * sizeof(int));
            npc->configs_count = length + 2;

            for( int idx = 0; idx <= length; ++idx )
            {
                npc->configs[idx] = read_16(buffer) & 0xFFFF;
                if( npc->configs[idx] == 0xFFFF )
                {
                    npc->configs[idx] = -1;
                }
            }

            npc->configs[length + 1] = var;
            break;
        }
        case 122:
        {
            npc->is_pet = true;
            break;
        }
        case 123:
        {
            npc->low_priority_follower_ops = true;
            break;
        }
        case 124:
        {
            npc->height = read_16(buffer) & 0xFFFF;
            break;
        }
        case 18:
        {
            npc->category = read_16(buffer) & 0xFFFF;
            break;
        }
        case 74:
        {
            npc->stats[0] = read_16(buffer) & 0xFFFF;
            break;
        }
        case 75:
        {
            npc->stats[1] = read_16(buffer) & 0xFFFF;
            break;
        }
        case 76:
        {
            npc->stats[2] = read_16(buffer) & 0xFFFF;
            break;
        }
        case 77:
        {
            npc->stats[3] = read_16(buffer) & 0xFFFF;
            break;
        }
        case 78:
        {
            npc->stats[4] = read_16(buffer) & 0xFFFF;
            break;
        }
        case 79:
        {
            npc->stats[5] = read_16(buffer) & 0xFFFF;
            break;
        }
        case 249:
        {
            if( buffer->position >= buffer->data_size )
            {
                printf("decode_npc_type: Buffer overflow in case 249\n");
                return;
            }
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

            if( !npc->params.keys || !npc->params.values || !npc->params.is_string )
            {
                printf(
                    "decode_npc_type: Failed to allocate params arrays of capacity %d\n", capacity);
                if( npc->params.keys )
                    free(npc->params.keys);
                if( npc->params.values )
                    free(npc->params.values);
                if( npc->params.is_string )
                    free(npc->params.is_string);
                return;
            }

            npc->params.count = 0;
            npc->params.capacity = capacity;

            for( int i = 0; i < length; i++ )
            {
                if( buffer->position >= buffer->data_size )
                {
                    printf(
                        "decode_npc_type: Buffer overflow while reading params at index %d\n", i);
                    // Cleanup on error
                    for( int j = 0; j < npc->params.count; j++ )
                    {
                        if( npc->params.values[j] )
                        {
                            free(npc->params.values[j]);
                        }
                    }
                    free(npc->params.keys);
                    free(npc->params.values);
                    free(npc->params.is_string);
                    npc->params.keys = NULL;
                    npc->params.values = NULL;
                    npc->params.is_string = NULL;
                    npc->params.count = 0;
                    npc->params.capacity = 0;
                    return;
                }

                bool is_string = (read_8(buffer) & 0xFF) == 1;
                int key = read_24(buffer);
                void* value;

                if( is_string )
                {
                    // Read string length first
                    int str_len = 0;
                    while( buffer->position + str_len < buffer->data_size &&
                           buffer->data[buffer->position + str_len] != '\0' )
                    {
                        str_len++;
                    }
                    if( buffer->position + str_len >= buffer->data_size )
                    {
                        printf(
                            "decode_npc_type: Buffer overflow while reading param string at index "
                            "%d\n",
                            i);
                        // Cleanup on error
                        for( int j = 0; j < npc->params.count; j++ )
                        {
                            if( npc->params.values[j] )
                            {
                                free(npc->params.values[j]);
                            }
                        }
                        free(npc->params.keys);
                        free(npc->params.values);
                        free(npc->params.is_string);
                        npc->params.keys = NULL;
                        npc->params.values = NULL;
                        npc->params.is_string = NULL;
                        npc->params.count = 0;
                        npc->params.capacity = 0;
                        return;
                    }
                    value = malloc(str_len + 1);
                    if( !value )
                    {
                        printf(
                            "decode_npc_type: Failed to allocate param string of length %d at "
                            "index %d\n",
                            str_len,
                            i);
                        // Cleanup on error
                        for( int j = 0; j < npc->params.count; j++ )
                        {
                            if( npc->params.values[j] )
                            {
                                free(npc->params.values[j]);
                            }
                        }
                        free(npc->params.keys);
                        free(npc->params.values);
                        free(npc->params.is_string);
                        npc->params.keys = NULL;
                        npc->params.values = NULL;
                        npc->params.is_string = NULL;
                        npc->params.count = 0;
                        npc->params.capacity = 0;
                        return;
                    }
                    readto(value, str_len + 1, str_len + 1, buffer);
                }
                else
                {
                    if( buffer->position + 3 >= buffer->data_size )
                    {
                        printf(
                            "decode_npc_type: Buffer overflow while reading param int at index "
                            "%d\n",
                            i);
                        // Cleanup on error
                        for( int j = 0; j < npc->params.count; j++ )
                        {
                            if( npc->params.values[j] )
                            {
                                free(npc->params.values[j]);
                            }
                        }
                        free(npc->params.keys);
                        free(npc->params.values);
                        free(npc->params.is_string);
                        npc->params.keys = NULL;
                        npc->params.values = NULL;
                        npc->params.is_string = NULL;
                        npc->params.count = 0;
                        npc->params.capacity = 0;
                        return;
                    }
                    value = malloc(sizeof(int));
                    if( !value )
                    {
                        printf("decode_npc_type: Failed to allocate param int at index %d\n", i);
                        // Cleanup on error
                        for( int j = 0; j < npc->params.count; j++ )
                        {
                            if( npc->params.values[j] )
                            {
                                free(npc->params.values[j]);
                            }
                        }
                        free(npc->params.keys);
                        free(npc->params.values);
                        free(npc->params.is_string);
                        npc->params.keys = NULL;
                        npc->params.values = NULL;
                        npc->params.is_string = NULL;
                        npc->params.count = 0;
                        npc->params.capacity = 0;
                        return;
                    }
                    *(int*)value = read_32(buffer);
                }

                npc->params.keys[npc->params.count] = key;
                npc->params.values[npc->params.count] = value;
                npc->params.is_string[npc->params.count] = is_string;
                npc->params.count++;
            }
            break;
        }
        default:
        {
            printf("decode_npc_type: Unknown opcode %d\n", opcode);
            break;
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

    // Print chathead_models
    printf("Chathead Models (%d): ", npc->chathead_models_count);
    if( npc->chathead_models )
    {
        for( int i = 0; i < npc->chathead_models_count; i++ )
        {
            printf("%d ", npc->chathead_models[i]);
        }
    }
    printf("\n");

    // Print animations
    printf("Animations:\n");
    printf("  Standing: %d\n", npc->standing_animation);
    printf("  Walking: %d\n", npc->walking_animation);
    printf("  Idle Rotate Left: %d\n", npc->idle_rotate_left_animation);
    printf("  Idle Rotate Right: %d\n", npc->idle_rotate_right_animation);
    printf("  Rotate 180: %d\n", npc->rotate180_animation);
    printf("  Rotate Left: %d\n", npc->rotate_left_animation);
    printf("  Rotate Right: %d\n", npc->rotate_right_animation);
    printf("  Run: %d\n", npc->run_animation);
    printf("  Run Rotate 180: %d\n", npc->run_rotate180_animation);
    printf("  Run Rotate Left: %d\n", npc->run_rotate_left_animation);
    printf("  Run Rotate Right: %d\n", npc->run_rotate_right_animation);
    printf("  Crawl: %d\n", npc->crawl_animation);
    printf("  Crawl Rotate 180: %d\n", npc->crawl_rotate180_animation);
    printf("  Crawl Rotate Left: %d\n", npc->crawl_rotate_left_animation);
    printf("  Crawl Rotate Right: %d\n", npc->crawl_rotate_right_animation);

    // Print actions
    printf("Actions:\n");
    for( int i = 0; i < 5; i++ )
    {
        printf("  Action %d: %s\n", i + 30, npc->actions[i] ? npc->actions[i] : "NULL");
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
    printf("  Size: %d\n", npc->size);
    printf("  Combat Level: %d\n", npc->combat_level);
    printf("  Width Scale: %d\n", npc->width_scale);
    printf("  Height Scale: %d\n", npc->height_scale);
    printf("  Ambient: %d\n", npc->ambient);
    printf("  Contrast: %d\n", npc->contrast);

    // Print flags
    printf("Flags:\n");
    printf("  Is Minimap Visible: %s\n", npc->is_minimap_visible ? "true" : "false");
    printf("  Has Render Priority: %s\n", npc->has_render_priority ? "true" : "false");
    printf("  Is Interactable: %s\n", npc->is_interactable ? "true" : "false");
    printf("  Rotation Flag: %s\n", npc->rotation_flag ? "true" : "false");
    printf("  Is Pet: %s\n", npc->is_pet ? "true" : "false");
    printf("  Low Priority Follower Ops: %s\n", npc->low_priority_follower_ops ? "true" : "false");

    // Print special integers
    printf("Special Integers:\n");
    printf("  Rotation Speed: %d\n", npc->rotation_speed);
    printf("  Height: %d\n", npc->height);
    printf("  Category: %d\n", npc->category);
    printf("  Stats:\n");
    printf("    Stat 0: %d\n", npc->stats[0]);
    printf("    Stat 1: %d\n", npc->stats[1]);
    printf("    Stat 2: %d\n", npc->stats[2]);
    printf("    Stat 3: %d\n", npc->stats[3]);
    printf("    Stat 4: %d\n", npc->stats[4]);
    printf("    Stat 5: %d\n", npc->stats[5]);

    // Print configs
    printf("Configs (%d elements): ", npc->configs_count);
    if( npc->configs )
    {
        for( int i = 0; i < npc->configs_count; i++ )
        {
            printf("%d ", npc->configs[i]);
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