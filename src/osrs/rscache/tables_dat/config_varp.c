#include "config_varp.h"

#include "../rsbuf.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static struct CacheDatConfigVarp*
decode_varp(struct RSBuffer* inb, int id, int** code3_ids, int* code3_count)
{
    struct CacheDatConfigVarp* varp = malloc(sizeof(struct CacheDatConfigVarp));
    memset(varp, 0, sizeof(struct CacheDatConfigVarp));
    varp->code4 = true; /* default per VarpType.ts */

    struct RSBuffer buffer = *inb;

    while (true)
    {
        if (buffer.position >= buffer.size)
        {
            assert(false && "Buffer position exceeded data size");
            return NULL;
        }

        int opcode = g1(&buffer);
        if (opcode == 0)
            break;

        switch (opcode)
        {
        case 1:
            varp->code1 = g1(&buffer);
            break;
        case 2:
            varp->code2 = g1(&buffer);
            break;
        case 3:
            varp->code3 = true;
            /* Append id to code3s - realloc to grow */
            {
                int n = *code3_count;
                *code3_ids = realloc(*code3_ids, (size_t)(n + 1) * sizeof(int));
                (*code3_ids)[n] = id;
                *code3_count = n + 1;
            }
            break;
        case 4:
            varp->code4 = false;
            break;
        case 5:
            varp->clientcode = g2(&buffer);
            break;
        case 6:
            varp->code6 = true;
            break;
        case 7:
            varp->code7 = g4(&buffer);
            break;
        case 8:
            varp->code8 = true;
            varp->code11 = true;
            break;
        case 10:
            varp->debugname = gstringnewline(&buffer);
            break;
        case 11:
            varp->code11 = true;
            break;
        default:
            assert(false && "Unrecognized config code");
            break;
        }
    }

    *inb = buffer;
    return varp;
}

struct CacheDatConfigVarpList*
cache_dat_config_varp_list_new_decode(
    void* jagfile_varpdat_data,
    int jagfile_varpdat_data_size)
{
    struct CacheDatConfigVarpList* list = malloc(sizeof(struct CacheDatConfigVarpList));
    memset(list, 0, sizeof(struct CacheDatConfigVarpList));

    struct RSBuffer buffer = { .data = (int8_t*)jagfile_varpdat_data,
                               .size = jagfile_varpdat_data_size,
                               .position = 0 };

    int varp_count = g2(&buffer);
    list->varps = malloc((size_t)varp_count * sizeof(struct CacheDatConfigVarp*));
    memset(list->varps, 0, (size_t)varp_count * sizeof(struct CacheDatConfigVarp*));
    list->varps_count = varp_count;

    int* code3_ids = NULL;
    int code3_count = 0;

    for (int i = 0; i < varp_count; i++)
    {
        list->varps[i] = decode_varp(&buffer, i, &code3_ids, &code3_count);
        if (!list->varps[i])
        {
            free(code3_ids);
            cache_dat_config_varp_list_free(list);
            return NULL;
        }
    }

    list->code3_ids = code3_ids;
    list->code3_count = code3_count;
    return list;
}

void
cache_dat_config_varp_list_free(struct CacheDatConfigVarpList* list)
{
    if (!list)
        return;

    if (list->varps)
    {
        for (int i = 0; i < list->varps_count; i++)
        {
            if (list->varps[i])
            {
                free(list->varps[i]->debugname);
                free(list->varps[i]);
            }
        }
        free(list->varps);
        list->varps = NULL;
    }
    list->varps_count = 0;
    free(list->code3_ids);
    list->code3_ids = NULL;
    list->code3_count = 0;
    free(list);
}
