#include "config_obj.h"

#include "../rsbuf.h"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct CacheDatConfigObj*
decode_obj(struct RSBuffer* buffer)
{
    struct CacheDatConfigObj* obj = malloc(sizeof(struct CacheDatConfigObj));
    memset(obj, 0, sizeof(struct CacheDatConfigObj));

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
            obj->model = g2(buffer);
            break;
        case 2:
            obj->name = gstringnewline(buffer);
            break;
        case 3:
            obj->desc = gstringnewline(buffer);
            break;
        case 4:
            obj->zoom2d = g2(buffer);
            break;
        case 5:
            obj->xan2d = g2(buffer);
            break;
        case 6:
            obj->yan2d = g2(buffer);
            break;
        case 7:
        {
            obj->xof2d = g2b(buffer);
            if( obj->xof2d > 32767 )
            {
                obj->xof2d -= 65536;
            }
            break;
        }
        case 8:
        {
            obj->yof2d = g2b(buffer);
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
            obj->code10 = g2(buffer);
            break;
        case 11:
            obj->stackable = true;
            break;
        case 12:
            obj->cost = g4(buffer);
            break;
        case 16:
            obj->members = true;
            break;
        case 23:
            obj->manwear = g2(buffer);
            obj->manwearOffsetY = g1b(buffer);
            break;
        case 24:
            obj->manwear2 = g2(buffer);
            break;
        case 25:
            obj->womanwear = g2(buffer);
            obj->womanwearOffsetY = g1b(buffer);
            break;
        case 26:
            obj->womanwear2 = g2(buffer);
            break;
        case 30 ... 34:
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
                    obj->op[opcode - 30] = NULL;
                }
                else
                {
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
            obj->iop[opcode - 35] = gstringnewline(buffer);
            break;
        }
        case 40:
        {
            int count = g1(buffer);
            obj->recol_s = malloc(count * sizeof(int));
            obj->recol_d = malloc(count * sizeof(int));
            obj->recol_count = count;
            for( int i = 0; i < count; i++ )
            {
                obj->recol_s[i] = g2(buffer);
                obj->recol_d[i] = g2(buffer);
            }
            break;
        }
        case 78:
            obj->manwear3 = g2(buffer);
            break;
        case 79:
            obj->womanwear3 = g2(buffer);
            break;
        case 90:
            obj->manhead = g2(buffer);
            break;
        case 91:
            obj->womanhead = g2(buffer);
            break;
        case 92:
            obj->manhead2 = g2(buffer);
            break;
        case 93:
            obj->womanhead2 = g2(buffer);
            break;
        case 95:
            obj->zan2d = g2(buffer);
            break;
        case 97:
            obj->certlink = g2(buffer);
            break;
        case 98:
            obj->certtemplate = g2(buffer);
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
            obj->countobj[opcode - 100] = g2(buffer);
            obj->countco[opcode - 100] = g2(buffer);
            break;
        }
        case 110:
            obj->resizex = g2(buffer);
            break;
        case 111:
            obj->resizey = g2(buffer);
            break;
        case 112:
            obj->resizez = g2(buffer);
            break;
        case 113:
            obj->ambient = g1b(buffer);
            break;
        case 114:
            obj->contrast = g1b(buffer) * 5;
            break;
        default:
            assert(false && "Unrecognized opcode");
            break;
        }
    }
    return obj;
}

struct CacheDatConfigObjList*
cache_dat_config_obj_list_new_decode(
    void* jagfile_objdat_data,
    int jagfile_objdat_data_size)
{
    struct CacheDatConfigObjList* obj_list = malloc(sizeof(struct CacheDatConfigObjList));
    memset(obj_list, 0, sizeof(struct CacheDatConfigObjList));

    struct RSBuffer buffer = { .data = jagfile_objdat_data,
                               .size = jagfile_objdat_data_size,
                               .position = 0 };

    int obj_count = g2(&buffer);
    obj_list->objs = malloc(obj_count * sizeof(struct CacheDatConfigObj));
    memset(obj_list->objs, 0, obj_count * sizeof(struct CacheDatConfigObj));

    obj_list->objs_count = obj_count;

    for( int i = 0; i < obj_count; i++ )
    {
        struct CacheDatConfigObj* decoded_obj = decode_obj(&buffer);
        if( decoded_obj == NULL )
        {
            assert(false && "Failed to decode obj");
            return NULL;
        }
        obj_list->objs[i] = *decoded_obj;
        free(decoded_obj);
    }

    return obj_list;
}
