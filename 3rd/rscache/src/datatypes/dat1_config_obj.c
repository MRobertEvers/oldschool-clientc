#include "dat1_config_obj.h"

#include "../filelist.h"
#include "../rsbuffer.h"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RSCache_Dat1ConfigObj*
RSCache_Dat1ConfigObjDecodeOne(void* data, int size)
{
    struct RSCache_Dat1ConfigObj* obj = malloc(sizeof(struct RSCache_Dat1ConfigObj));
    memset(obj, 0, sizeof(struct RSCache_Dat1ConfigObj));

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)(data), (uint32_t)(size));

    // Initialize default values
    obj->model = 0;
    obj->name = NULL;
    obj->desc = NULL;
    obj->recol_s = NULL;
    obj->recol_d = NULL;
    obj->recol_count = 0;
    obj->zoom2d = 2000;
    obj->xan2d = 0;
    obj->yan2d = 0;
    obj->zan2d = 0;
    obj->xof2d = 0;
    obj->yof2d = 0;
    obj->code9 = false;
    obj->code10 = -1;
    obj->stackable = false;
    obj->cost = 1;
    obj->members = false;
    obj->manwearOffsetY = 0;
    obj->womanwearOffsetY = 0;
    obj->manwear = -1;
    obj->manwear2 = -1;
    obj->womanwear = -1;
    obj->womanwear2 = -1;
    obj->manwear3 = -1;
    obj->womanwear3 = -1;
    obj->manhead = -1;
    obj->manhead2 = -1;
    obj->womanhead = -1;
    obj->womanhead2 = -1;
    obj->certlink = -1;
    obj->certtemplate = -1;
    obj->resizex = 128;
    obj->resizey = 128;
    obj->resizez = 128;
    obj->ambient = 0;
    obj->contrast = 0;
    obj->countobj = NULL;
    obj->countco = NULL;
    obj->countobj_count = 0;

    for( int i = 0; i < 5; i++ )
    {
        obj->op[i] = NULL;
        obj->iop[i] = NULL;
    }

    while( true )
    {
        if( buffer.position >= buffer.size )
        {
            assert(false && "Buffer position exceeded data size");
            return NULL;
        }

        int opcode = g1(&buffer);
        if( opcode == 0 )
        {
            break;
        }

        if( obj->opcode_count < (int)(sizeof(obj->opcodes) / sizeof(obj->opcodes[0])) )
            obj->opcodes[obj->opcode_count++] = opcode;

        switch( opcode )
        {
        case 1:
            obj->model = g2(&buffer);
            break;
        case 2:
            free(obj->name);
            obj->name = gstringnewline(&buffer);
            break;
        case 3:
            free(obj->desc);
            obj->desc = gstringnewline(&buffer);
            break;
        case 4:
            obj->zoom2d = g2(&buffer);
            break;
        case 5:
            obj->xan2d = g2(&buffer);
            break;
        case 6:
            obj->yan2d = g2(&buffer);
            break;
        case 7:
        {
            obj->xof2d = g2b(&buffer);
            if( obj->xof2d > 32767 )
            {
                obj->xof2d -= 65536;
            }
            break;
        }
        case 8:
        {
            obj->yof2d = g2b(&buffer);
            if( obj->yof2d > 32767 )
            {
                obj->yof2d -= 65536;
            }
            break;
        }
        case 9:
            obj->code9 = true;
            break;
        case 10:
            obj->code10 = g2(&buffer);
            break;
        case 11:
            obj->stackable = true;
            break;
        case 12:
            obj->cost = g4(&buffer);
            break;
        case 16:
            obj->members = true;
            break;
        case 23:
            obj->manwear = g2(&buffer);
            obj->manwearOffsetY = g1b(&buffer);
            break;
        case 24:
            obj->manwear2 = g2(&buffer);
            break;
        case 25:
            obj->womanwear = g2(&buffer);
            obj->womanwearOffsetY = g1b(&buffer);
            break;
        case 26:
            obj->womanwear2 = g2(&buffer);
            break;
        case 30 ... 34:
        {
            char* action = gstringnewline(&buffer);
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
                    free(obj->op[opcode - 30]);
                    obj->op[opcode - 30] = NULL;
                }
                else
                {
                    free(obj->op[opcode - 30]);
                    obj->op[opcode - 30] = action;
                }
            }
            else
            {
                obj->op[opcode - 30] = NULL;
            }
            break;
        }
        case 35 ... 39:
        {
            free(obj->iop[opcode - 35]);
            obj->iop[opcode - 35] = gstringnewline(&buffer);
            break;
        }
        case 40:
        {
            int count = g1(&buffer);
            obj->recol_s = malloc(count * sizeof(int));
            obj->recol_d = malloc(count * sizeof(int));
            obj->recol_count = count;
            for( int i = 0; i < count; i++ )
            {
                obj->recol_s[i] = g2(&buffer);
                obj->recol_d[i] = g2(&buffer);
            }
            break;
        }
        case 78:
            obj->manwear3 = g2(&buffer);
            break;
        case 79:
            obj->womanwear3 = g2(&buffer);
            break;
        case 90:
            obj->manhead = g2(&buffer);
            break;
        case 91:
            obj->womanhead = g2(&buffer);
            break;
        case 92:
            obj->manhead2 = g2(&buffer);
            break;
        case 93:
            obj->womanhead2 = g2(&buffer);
            break;
        case 95:
            obj->zan2d = g2(&buffer);
            break;
        case 97:
            obj->certlink = g2(&buffer);
            break;
        case 98:
            obj->certtemplate = g2(&buffer);
            break;
        case 100 ... 109:
        {
            if( obj->countobj == NULL || obj->countco == NULL )
            {
                obj->countobj = malloc(10 * sizeof(int));
                obj->countco = malloc(10 * sizeof(int));
                obj->countobj_count = 10;
                memset(obj->countobj, 0, 10 * sizeof(int));
                memset(obj->countco, 0, 10 * sizeof(int));
            }
            obj->countobj[opcode - 100] = g2(&buffer);
            obj->countco[opcode - 100] = g2(&buffer);
            break;
        }
        case 110:
            obj->resizex = g2(&buffer);
            break;
        case 111:
            obj->resizey = g2(&buffer);
            break;
        case 112:
            obj->resizez = g2(&buffer);
            break;
        case 113:
            obj->ambient = g1b(&buffer);
            break;
        case 114:
            obj->contrast = g1b(&buffer) * 5;
            break;
        default:
            assert(false && "Unrecognized opcode");
            break;
        }
    }
    return obj;
}

struct RSCache_Dat1ConfigObjList*
RSCache_Dat1ConfigObjListNewDecode(
    char* index_data,
    int index_data_size,
    char* data,
    int data_size)
{
    struct RSCache_Dat1ConfigObjList* obj_list = malloc(sizeof(struct RSCache_Dat1ConfigObjList));
    memset(obj_list, 0, sizeof(struct RSCache_Dat1ConfigObjList));
    struct RSCache_FileListDatIndexed* filelist_indexed =
        RSCache_FileListDatIndexedNewFromDecode(index_data, index_data_size, data, data_size);

    obj_list->objs = malloc(filelist_indexed->offset_count * sizeof(struct RSCache_Dat1ConfigObj*));
    obj_list->objs_count = filelist_indexed->offset_count;

    for( int i = 0; i < filelist_indexed->offset_count; i++ )
    {
        struct RSCache_Dat1ConfigObj* obj = RSCache_Dat1ConfigObjDecodeOne(
            filelist_indexed->data + filelist_indexed->offsets[i],
            filelist_indexed->data_size - filelist_indexed->offsets[i]);
        if( obj == NULL )
        {
            assert(false && "Failed to decode obj");
            return NULL;
        }

        obj_list->objs[i] = obj;
    }

    RSCache_FileListDatIndexedFree(filelist_indexed);

    return obj_list;
}

void
RSCache_Dat1ConfigObjFree(struct RSCache_Dat1ConfigObj* obj)
{
    if( !obj )
        return;
    free(obj->name);
    free(obj->desc);
    free(obj->recol_s);
    free(obj->recol_d);
    free(obj->countobj);
    free(obj->countco);
    for( int i = 0; i < 5; i++ )
    {
        free(obj->op[i]);
        free(obj->iop[i]);
    }
    free(obj);
}

uint32_t
RSCache_Dat1ConfigObjEncodeBound(const struct RSCache_Dat1ConfigObj* obj)
{
    uint32_t need = 64u;
    int i;

    if( !obj )
        return need;
    if( obj->name )
        need += (uint32_t)strlen(obj->name) + 2u;
    if( obj->desc )
        need += (uint32_t)strlen(obj->desc) + 2u;
    for( i = 0; i < 5; i++ )
    {
        if( obj->op[i] )
            need += (uint32_t)strlen(obj->op[i]) + 2u;
        if( obj->iop[i] )
            need += (uint32_t)strlen(obj->iop[i]) + 2u;
    }
    need += (uint32_t)obj->recol_count * 4u + 2u;
    need += (uint32_t)obj->countobj_count * 4u + 2u;
    /* Every remaining opcode is at most a code byte plus four payload bytes. */
    need += (uint32_t)obj->opcode_count * 6u;
    return need;
}

uint32_t
RSCache_Dat1ConfigObjEncode(
    const struct RSCache_Dat1ConfigObj* obj,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Buffer buffer;
    int i;

    if( !obj || !out )
        return 0;
    if( out_capacity < RSCache_Dat1ConfigObjEncodeBound(obj) )
        return 0;
    RSCache_BufferInit(&buffer, out, out_capacity);

    for( i = 0; i < obj->opcode_count; i++ )
    {
        int opcode = obj->opcodes[i];

        p1(&buffer, opcode);
        switch( opcode )
        {
        case 1:
            p2(&buffer, obj->model);
            break;
        case 2:
            pjstr(&buffer, obj->name ? obj->name : "", RSCACHE_JSTR_TERMINATOR_NEWLINE);
            break;
        case 3:
            pjstr(&buffer, obj->desc ? obj->desc : "", RSCACHE_JSTR_TERMINATOR_NEWLINE);
            break;
        case 4:
            p2(&buffer, obj->zoom2d);
            break;
        case 5:
            p2(&buffer, obj->xan2d);
            break;
        case 6:
            p2(&buffer, obj->yan2d);
            break;
        case 7:
            p2b(&buffer, obj->xof2d);
            break;
        case 8:
            p2b(&buffer, obj->yof2d);
            break;
        case 9:
        case 11:
        case 16:
            /* Bare flags: the opcode byte is the whole payload. */
            break;
        case 10:
            p2(&buffer, obj->code10);
            break;
        case 12:
            p4(&buffer, obj->cost);
            break;
        case 23:
            p2(&buffer, obj->manwear);
            p1b(&buffer, obj->manwearOffsetY);
            break;
        case 24:
            p2(&buffer, obj->manwear2);
            break;
        case 25:
            p2(&buffer, obj->womanwear);
            p1b(&buffer, obj->womanwearOffsetY);
            break;
        case 26:
            p2(&buffer, obj->womanwear2);
            break;
        case 40:
        {
            int rec;

            p1(&buffer, obj->recol_count);
            for( rec = 0; rec < obj->recol_count; rec++ )
            {
                p2(&buffer, obj->recol_s[rec]);
                p2(&buffer, obj->recol_d[rec]);
            }
            break;
        }
        case 78:
            p2(&buffer, obj->manwear3);
            break;
        case 79:
            p2(&buffer, obj->womanwear3);
            break;
        case 90:
            p2(&buffer, obj->manhead);
            break;
        case 91:
            p2(&buffer, obj->womanhead);
            break;
        case 92:
            p2(&buffer, obj->manhead2);
            break;
        case 93:
            p2(&buffer, obj->womanhead2);
            break;
        case 95:
            p2(&buffer, obj->zan2d);
            break;
        case 97:
            p2(&buffer, obj->certlink);
            break;
        case 98:
            p2(&buffer, obj->certtemplate);
            break;
        case 110:
            p2(&buffer, obj->resizex);
            break;
        case 111:
            p2(&buffer, obj->resizey);
            break;
        case 112:
            p2(&buffer, obj->resizez);
            break;
        case 113:
            p1b(&buffer, obj->ambient);
            break;
        case 114:
            /* The decoder multiplies by five, so the wire value is the fifth. */
            p1b(&buffer, obj->contrast / 5);
            break;
        default:
            if( opcode >= 30 && opcode < 35 )
            {
                const char* act = obj->op[opcode - 30];

                pjstr(&buffer, act ? act : "", RSCACHE_JSTR_TERMINATOR_NEWLINE);
            }
            else if( opcode >= 35 && opcode < 40 )
            {
                const char* act = obj->iop[opcode - 35];

                pjstr(&buffer, act ? act : "", RSCACHE_JSTR_TERMINATOR_NEWLINE);
            }
            else if( opcode >= 100 && opcode < 110 )
            {
                p2(&buffer, obj->countobj[opcode - 100]);
                p2(&buffer, obj->countco[opcode - 100]);
            }
            else
            {
                return 0;
            }
            break;
        }
    }

    p1(&buffer, 0);
    return buffer.position;
}
