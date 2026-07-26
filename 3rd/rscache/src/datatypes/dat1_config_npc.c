#include "dat1_config_npc.h"

#include "../filelist.h"
#include "../rsbuffer.h"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// name: string | null = null;
// desc: string | null = null;
// size: number = 1;
// models: Uint16Array | null = null;
// heads: Uint16Array | null = null;
// readyanim: number = -1;
// walkanim: number = -1;
// walkanim_b: number = -1;
// walkanim_r: number = -1;
// walkanim_l: number = -1;
// animHasAlpha: boolean = false;
// recol_s: Uint16Array | null = null;
// recol_d: Uint16Array | null = null;
// op: (string | null)[] | null = null;
// resizex: number = -1;
// resizey: number = -1;
// resizez: number = -1;
// minimap: boolean = true;
// vislevel: number = -1;
// resizeh: number = 128;
// resizev: number = 128;
// alwaysontop: boolean = false;
// headicon: number = -1;
// static modelCache: LruCache | null = new LruCache(30);
// ambient: number = 0;
// contrast: number = 0;
static void
init_npc(struct RSCache_Dat1ConfigNpc* npc)
{
    memset(npc, 0, sizeof(struct RSCache_Dat1ConfigNpc));
    npc->name = NULL;
    npc->desc = NULL;
    npc->size = 1;
    npc->models = NULL;
    npc->models_count = 0;
    npc->heads = NULL;
    npc->heads_count = 0;
    npc->readyanim = -1;
    npc->walkanim = -1;
    npc->walkanim_b = -1;
    npc->walkanim_r = -1;
    npc->walkanim_l = -1;
    npc->animHasAlpha = false;
    npc->recol_s = NULL;
    npc->recol_d = NULL;
    npc->recol_count = 0;
    npc->resizex = -1;
    npc->resizey = -1;
    npc->resizez = -1;
    npc->minimap = true;
    npc->vislevel = -1;
    npc->resizeh = 128;
    npc->resizev = 128;
    npc->alwaysontop = false;
    npc->headicon = -1;
    npc->turnspeed = 32;
    npc->ambient = 0;
    npc->contrast = 0;
}

struct RSCache_Dat1ConfigNpc*
RSCache_Dat1ConfigNpcDecodeOne(struct RSCache_Buffer* buffer)
{
    struct RSCache_Dat1ConfigNpc* npc = malloc(sizeof(struct RSCache_Dat1ConfigNpc));
    memset(npc, 0, sizeof(struct RSCache_Dat1ConfigNpc));

    init_npc(npc);

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
    npc->turnspeed = 32;
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
            free(npc->name);
            npc->name = gstringnewline(buffer);
            break;
        }
        case 3:
        {
            free(npc->desc);
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
            int op_idx = opcode - 30;
            if( op_idx >= 5 )
            {
                free(action);
                break;
            }
            if( action != NULL )
            {
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
                    free(npc->op[op_idx]);
                    npc->op[op_idx] = NULL;
                }
                else
                {
                    free(npc->op[op_idx]);
                    npc->op[op_idx] = action;
                }
            }
            else
            {
                free(npc->op[op_idx]);
                npc->op[op_idx] = NULL;
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
        case 103:
        {
            npc->turnspeed = g2(buffer);
            break;
        }
        default:
            assert(false && "Unrecognized opcode");
            break;
        }
    }
    return npc;
}

struct RSCache_Dat1ConfigNpcList*
RSCache_Dat1ConfigNpcListNewDecode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size)
{
    struct RSCache_Dat1ConfigNpcList* npc_list = malloc(sizeof(struct RSCache_Dat1ConfigNpcList));
    memset(npc_list, 0, sizeof(struct RSCache_Dat1ConfigNpcList));

    struct RSCache_FileListDatIndexed* filelist_indexed =
        RSCache_FileListDatIndexedNewFromDecode(index_data, index_data_size, data, data_size);

    npc_list->npcs = malloc(filelist_indexed->offset_count * sizeof(struct RSCache_Dat1ConfigNpc*));
    memset(npc_list->npcs, 0, filelist_indexed->offset_count * sizeof(struct RSCache_Dat1ConfigNpc));

    npc_list->npcs_count = filelist_indexed->offset_count;

    struct RSCache_Buffer buffer;
    for( int i = 0; i < filelist_indexed->offset_count; i++ )
    {
        RSCache_BufferInit(
            &buffer,
            (uint8_t*)(filelist_indexed->data + filelist_indexed->offsets[i]),
            (uint32_t)(filelist_indexed->data_size - filelist_indexed->offsets[i]));

        struct RSCache_Dat1ConfigNpc* npc = RSCache_Dat1ConfigNpcDecodeOne(&buffer);
        if( npc == NULL )
        {
            assert(false && "Failed to decode npc");
            return NULL;
        }
        npc_list->npcs[i] = npc;
    }

    RSCache_FileListDatIndexedFree(filelist_indexed);

    return npc_list;
}

void
RSCache_Dat1ConfigNpcFree(struct RSCache_Dat1ConfigNpc* npc)
{
    if( !npc )
        return;
    free(npc->name);
    free(npc->desc);
    free(npc->models);
    free(npc->heads);
    free(npc->recol_s);
    free(npc->recol_d);
    for( int i = 0; i < 5; i++ )
        free(npc->op[i]);
    free(npc);
}

void
RSCache_Dat1ConfigNpcListFree(struct RSCache_Dat1ConfigNpcList* list)
{
    if( !list )
        return;
    if( list->npcs )
    {
        for( int i = 0; i < list->npcs_count; i++ )
            RSCache_Dat1ConfigNpcFree(list->npcs[i]);
        free(list->npcs);
    }
    free(list);
}

uint32_t
RSCache_Dat1ConfigNpcEncodeBound(const struct RSCache_Dat1ConfigNpc* npc)
{
    if( !npc )
        return 0;

    /* Generous upper bound: opcode + payload for every field, terminator. */
    uint32_t bound = 1; /* terminator */
    if( npc->models_count > 0 )
        bound += 2u + (uint32_t)npc->models_count * 2u;
    if( npc->name )
        bound += 2u + (uint32_t)strlen(npc->name);
    if( npc->desc )
        bound += 2u + (uint32_t)strlen(npc->desc);
    bound += 2; /* size */
    bound += 3; /* readyanim */
    bound += 3; /* walkanim */
    bound += 9; /* walkanim set */
    bound += 1; /* animHasAlpha */
    for( int i = 0; i < 5; i++ )
    {
        if( npc->op[i] )
            bound += 2u + (uint32_t)strlen(npc->op[i]);
        else
            bound += 2u + 6u; /* "hidden" */
    }
    if( npc->recol_count > 0 )
        bound += 2u + (uint32_t)npc->recol_count * 4u;
    if( npc->heads_count > 0 )
        bound += 2u + (uint32_t)npc->heads_count * 2u;
    bound += 3 * 3; /* resizex/y/z */
    bound += 1;     /* minimap */
    bound += 3;     /* vislevel */
    bound += 3;     /* resizeh */
    bound += 3;     /* resizev */
    bound += 1;     /* alwaysontop */
    bound += 2;     /* ambient */
    bound += 2;     /* contrast */
    bound += 3;     /* headicon */
    bound += 3;     /* turnspeed */
    return bound;
}

uint32_t
RSCache_Dat1ConfigNpcEncode(
    const struct RSCache_Dat1ConfigNpc* npc,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(npc != NULL);
    assert(out != NULL);

    if( out_capacity < RSCache_Dat1ConfigNpcEncodeBound(npc) )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    if( npc->models_count > 0 )
    {
        assert(npc->models != NULL);
        p1(&buffer, 1);
        p1(&buffer, npc->models_count);
        for( int i = 0; i < npc->models_count; i++ )
            p2(&buffer, npc->models[i]);
    }
    if( npc->name )
    {
        p1(&buffer, 2);
        pjstr(&buffer, npc->name, RSCACHE_JSTR_TERMINATOR_NEWLINE);
    }
    if( npc->desc )
    {
        p1(&buffer, 3);
        pjstr(&buffer, npc->desc, RSCACHE_JSTR_TERMINATOR_NEWLINE);
    }
    if( npc->size != 1 )
    {
        p1(&buffer, 12);
        p1b(&buffer, npc->size);
    }
    if( npc->readyanim != -1 )
    {
        p1(&buffer, 13);
        p2(&buffer, npc->readyanim);
    }

    /* Opcode 17 carries walk + turn variants; opcode 14 is walk alone. */
    if( npc->walkanim_b != -1 || npc->walkanim_r != -1 || npc->walkanim_l != -1 )
    {
        p1(&buffer, 17);
        p2(&buffer, npc->walkanim);
        p2(&buffer, npc->walkanim_b);
        p2(&buffer, npc->walkanim_r);
        p2(&buffer, npc->walkanim_l);
    }
    else if( npc->walkanim != -1 )
    {
        p1(&buffer, 14);
        p2(&buffer, npc->walkanim);
    }

    if( npc->animHasAlpha )
        p1(&buffer, 16);

    for( int i = 0; i < 5; i++ )
    {
        if( npc->op[i] )
        {
            p1(&buffer, 30 + i);
            pjstr(&buffer, npc->op[i], RSCACHE_JSTR_TERMINATOR_NEWLINE);
        }
    }

    if( npc->recol_count > 0 )
    {
        assert(npc->recol_s && npc->recol_d);
        p1(&buffer, 40);
        p1(&buffer, npc->recol_count);
        for( int i = 0; i < npc->recol_count; i++ )
        {
            p2(&buffer, npc->recol_s[i]);
            p2(&buffer, npc->recol_d[i]);
        }
    }

    if( npc->heads_count > 0 )
    {
        assert(npc->heads != NULL);
        p1(&buffer, 60);
        p1(&buffer, npc->heads_count);
        for( int i = 0; i < npc->heads_count; i++ )
            p2(&buffer, npc->heads[i]);
    }

    if( npc->resizex != -1 )
    {
        p1(&buffer, 90);
        p2(&buffer, npc->resizex);
    }
    if( npc->resizey != -1 )
    {
        p1(&buffer, 91);
        p2(&buffer, npc->resizey);
    }
    if( npc->resizez != -1 )
    {
        p1(&buffer, 92);
        p2(&buffer, npc->resizez);
    }
    if( !npc->minimap )
        p1(&buffer, 93);
    if( npc->vislevel != -1 )
    {
        p1(&buffer, 95);
        p2(&buffer, npc->vislevel);
    }
    if( npc->resizeh != 128 )
    {
        p1(&buffer, 97);
        p2(&buffer, npc->resizeh);
    }
    if( npc->resizev != 128 )
    {
        p1(&buffer, 98);
        p2(&buffer, npc->resizev);
    }
    if( npc->alwaysontop )
        p1(&buffer, 99);
    if( npc->ambient != 0 )
    {
        p1(&buffer, 100);
        p1b(&buffer, npc->ambient);
    }
    if( npc->contrast != 0 )
    {
        p1(&buffer, 101);
        p1b(&buffer, npc->contrast / 5);
    }
    if( npc->headicon != -1 )
    {
        p1(&buffer, 102);
        p2(&buffer, npc->headicon);
    }
    if( npc->turnspeed != 32 )
    {
        p1(&buffer, 103);
        p2(&buffer, npc->turnspeed);
    }

    p1(&buffer, 0);
    return buffer.position;
}