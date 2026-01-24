#include "config_npc.h"

#include "../rsbuf.h"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct CacheDatConfigNpc*
decode_npc(struct RSBuffer* buffer)
{
    struct CacheDatConfigNpc* npc = malloc(sizeof(struct CacheDatConfigNpc));
    memset(npc, 0, sizeof(struct CacheDatConfigNpc));

    // Initialize default values
    npc->size = 1;
    npc->readyanim = -1;
    npc->walkanim = -1;
    npc->walkanim_b = -1;
    npc->walkanim_r = -1;
    npc->walkanim_l = -1;
    npc->animHasAlpha = false;
    npc->resizex = -1;
    npc->resizey = -1;
    npc->resizez = -1;
    npc->minimap = true;
    npc->vislevel = -1;
    npc->resizeh = 128;
    npc->resizev = 128;
    npc->alwaysontop = false;
    npc->headicon = -1;
    npc->ambient = 0;
    npc->contrast = 0;

    for( int i = 0; i < 5; i++ )
    {
        npc->op[i] = NULL;
    }

    while( true )
    {
        if( buffer->position >= buffer->size )
        {
            assert(false && "Buffer position exceeded data size");
            return NULL;
        }

        int opcode = g1(buffer);
        if( opcode == 0 )
        {
            break;
        }

        switch( opcode )
        {
        case 1:
        {
            int count = g1(buffer);
            npc->models = malloc(count * sizeof(int));
            npc->models_count = count;
            for( int i = 0; i < count; i++ )
            {
                npc->models[i] = g2(buffer);
            }
            break;
        }
        case 2:
        {
            npc->name = gstringnewline(buffer);
            break;
        }
        case 3:
        {
            npc->desc = gstringnewline(buffer);
            break;
        }
        case 12:
        {
            npc->size = g1b(buffer);
            break;
        }
        case 13:
        {
            npc->readyanim = g2(buffer);
            break;
        }
        case 14:
        {
            npc->walkanim = g2(buffer);
            break;
        }
        case 16:
        {
            npc->animHasAlpha = true;
            break;
        }
        case 17:
        {
            npc->walkanim = g2(buffer);
            npc->walkanim_b = g2(buffer);
            npc->walkanim_r = g2(buffer);
            npc->walkanim_l = g2(buffer);
            break;
        }
        case 30 ... 39:
        {
            char* action = gstringnewline(buffer);
            if( action != NULL )
            {
                // Check if action is "hidden" (case-insensitive)
                char* hidden_str = "hidden";
                int is_hidden = 1;
                int len = strlen(action);
                if( len == 6 )
                {
                    for( int i = 0; i < len; i++ )
                    {
                        if( tolower(action[i]) != hidden_str[i] )
                        {
                            is_hidden = 0;
                            break;
                        }
                    }
                }
                else
                {
                    is_hidden = 0;
                }

                if( is_hidden )
                {
                    free(action);
                    npc->op[opcode - 30] = NULL;
                }
                else
                {
                    npc->op[opcode - 30] = action;
                }
            }
            else
            {
                npc->op[opcode - 30] = NULL;
            }
            break;
        }
        case 40:
        {
            int count = g1(buffer);
            npc->recol_s = malloc(count * sizeof(int));
            npc->recol_d = malloc(count * sizeof(int));
            npc->recol_count = count;
            for( int i = 0; i < count; i++ )
            {
                npc->recol_s[i] = g2(buffer);
                npc->recol_d[i] = g2(buffer);
            }
            break;
        }
        case 60:
        {
            int count = g1(buffer);
            npc->heads = malloc(count * sizeof(int));
            npc->heads_count = count;
            for( int i = 0; i < count; i++ )
            {
                npc->heads[i] = g2(buffer);
            }
            break;
        }
        case 90:
        {
            npc->resizex = g2(buffer);
            break;
        }
        case 91:
        {
            npc->resizey = g2(buffer);
            break;
        }
        case 92:
        {
            npc->resizez = g2(buffer);
            break;
        }
        case 93:
        {
            npc->minimap = false;
            break;
        }
        case 95:
        {
            npc->vislevel = g2(buffer);
            break;
        }
        case 97:
        {
            npc->resizeh = g2(buffer);
            break;
        }
        case 98:
        {
            npc->resizev = g2(buffer);
            break;
        }
        case 99:
        {
            npc->alwaysontop = true;
            break;
        }
        case 100:
        {
            npc->ambient = g1b(buffer);
            break;
        }
        case 101:
        {
            npc->contrast = g1b(buffer) * 5;
            break;
        }
        case 102:
        {
            npc->headicon = g2(buffer);
            break;
        }
        default:
            assert(false && "Unrecognized opcode");
            break;
        }
    }
    return npc;
}

struct CacheDatConfigNpcList*
cache_dat_config_npc_list_new_decode(
    void* jagfile_npcdat_data,
    int jagfile_npcdat_data_size)
{
    struct CacheDatConfigNpcList* npc_list = malloc(sizeof(struct CacheDatConfigNpcList));
    memset(npc_list, 0, sizeof(struct CacheDatConfigNpcList));

    struct RSBuffer buffer = { .data = jagfile_npcdat_data,
                               .size = jagfile_npcdat_data_size,
                               .position = 0 };

    int npc_count = g2(&buffer);
    npc_list->npcs = malloc(npc_count * sizeof(struct CacheDatConfigNpc));
    memset(npc_list->npcs, 0, npc_count * sizeof(struct CacheDatConfigNpc));

    npc_list->npcs_count = npc_count;

    for( int i = 0; i < npc_count; i++ )
    {
        struct CacheDatConfigNpc* decoded_npc = decode_npc(&buffer);
        if( decoded_npc == NULL )
        {
            assert(false && "Failed to decode npc");
            return NULL;
        }
        npc_list->npcs[i] = *decoded_npc;
        free(decoded_npc);
    }

    return npc_list;
}
