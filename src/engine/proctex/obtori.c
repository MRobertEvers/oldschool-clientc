#include "obtori.h"

#include <stdlib.h>
#include <string.h>

/* Little-endian accessors. Spelled out rather than memcpy'd through a struct so
 * the on-disk layout in obtori.h is the only description of the format. */
static uint32_t
rd32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t
rd16(const uint8_t* p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

static void
wr32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void
wr16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

bool
ObTori_IsObTori(const void* data, int size)
{
    return data && size >= OBTORI_HEADER_SIZE &&
           memcmp(data, OBTORI_MAGIC, sizeof(OBTORI_MAGIC)) == 0;
}

const char*
ObTori_KernelName(int kernel)
{
    switch( kernel )
    {
    case OBTORI_KERNEL_DEFAULT: return "default";
    case OBTORI_KERNEL_TRANSPARENT: return "transparent";
    case OBTORI_KERNEL_ALPHA: return "alpha";
    case OBTORI_KERNEL_MODULATE: return "modulate";
    case OBTORI_KERNEL_DETAIL: return "detail";
    default: return "?";
    }
}

struct ObToriModel*
ObTori_NewDecode(const void* data, int size, int face_count)
{
    const uint8_t* bytes = (const uint8_t*)data;
    struct ObToriModel* model;
    int section_count;
    int ob3_size;
    int offset;

    if( !ObTori_IsObTori(data, size) )
        return NULL;
    if( rd16(bytes + 8) != OBTORI_VERSION )
        return NULL;

    section_count = rd16(bytes + 10);
    ob3_size = (int)rd32(bytes + 12);
    if( ob3_size < 0 || ob3_size > size - OBTORI_HEADER_SIZE )
        return NULL;

    model = (struct ObToriModel*)calloc(1, sizeof(*model));
    if( !model )
        return NULL;
    model->face_count = face_count;
    model->ob3_size = ob3_size;
    model->ob3 = (uint8_t*)malloc((size_t)(ob3_size > 0 ? ob3_size : 1));
    if( !model->ob3 )
    {
        ObTori_Free(model);
        return NULL;
    }
    memcpy(model->ob3, bytes + OBTORI_HEADER_SIZE, (size_t)ob3_size);

    offset = OBTORI_HEADER_SIZE + ob3_size;
    for( int i = 0; i < section_count; i++ )
    {
        int kind, payload;
        const uint8_t* body;

        if( offset + 8 > size )
        {
            ObTori_Free(model);
            return NULL;
        }
        kind = rd16(bytes + offset);
        payload = (int)rd32(bytes + offset + 4);
        body = bytes + offset + 8;
        if( payload < 0 || offset + 8 + payload > size )
        {
            ObTori_Free(model);
            return NULL;
        }

        switch( kind )
        {
        case OBTORI_SECTION_FACE_KERNEL:
        case OBTORI_SECTION_FACE_DETAIL_STRENGTH:
        {
            uint8_t** slot = kind == OBTORI_SECTION_FACE_KERNEL
                                 ? &model->face_kernel
                                 : &model->face_detail_strength;
            /* A per-face section that does not have one entry per face is a
             * corrupt file: silently using a short array would read past the
             * end on the faces it does not cover. */
            if( payload != face_count || *slot )
            {
                ObTori_Free(model);
                return NULL;
            }
            *slot = (uint8_t*)malloc((size_t)(payload > 0 ? payload : 1));
            if( !*slot )
            {
                ObTori_Free(model);
                return NULL;
            }
            memcpy(*slot, body, (size_t)payload);
            break;
        }
        case OBTORI_SECTION_FACE_DETAIL_TEXTURE:
        {
            if( payload != face_count * 2 || model->face_detail_texture )
            {
                ObTori_Free(model);
                return NULL;
            }
            model->face_detail_texture =
                (int16_t*)malloc((size_t)(payload > 0 ? payload : 2));
            if( !model->face_detail_texture )
            {
                ObTori_Free(model);
                return NULL;
            }
            for( int f = 0; f < face_count; f++ )
                model->face_detail_texture[f] = (int16_t)rd16(body + f * 2);
            break;
        }
        default:
            /* Unknown kind: skip by size. A reader older than the writer keeps
             * working and just misses the feature. */
            break;
        }
        offset += 8 + payload;
    }
    return model;
}

void
ObTori_Free(struct ObToriModel* model)
{
    if( !model )
        return;
    free(model->ob3);
    free(model->face_kernel);
    free(model->face_detail_strength);
    free(model->face_detail_texture);
    free(model);
}

int
ObTori_EncodeBound(int ob3_size, int face_count, int section_count)
{
    return OBTORI_HEADER_SIZE + ob3_size + section_count * (8 + face_count * 2);
}

int
ObTori_Encode(
    const void* ob3,
    int ob3_size,
    int face_count,
    const uint8_t* face_kernel,
    const uint8_t* face_detail_strength,
    const int16_t* face_detail_texture,
    void* out,
    int out_capacity)
{
    uint8_t* bytes = (uint8_t*)out;
    int section_count = (face_kernel ? 1 : 0) + (face_detail_strength ? 1 : 0) +
                        (face_detail_texture ? 1 : 0);
    int offset;

    if( !out || ob3_size < 0 || face_count < 0 )
        return 0;
    if( out_capacity < ObTori_EncodeBound(ob3_size, face_count, section_count) )
        return 0;

    memcpy(bytes, OBTORI_MAGIC, sizeof(OBTORI_MAGIC));
    wr16(bytes + 8, OBTORI_VERSION);
    wr16(bytes + 10, (uint16_t)section_count);
    wr32(bytes + 12, (uint32_t)ob3_size);
    memcpy(bytes + OBTORI_HEADER_SIZE, ob3, (size_t)ob3_size);
    offset = OBTORI_HEADER_SIZE + ob3_size;

    if( face_kernel )
    {
        wr16(bytes + offset, OBTORI_SECTION_FACE_KERNEL);
        wr16(bytes + offset + 2, 0);
        wr32(bytes + offset + 4, (uint32_t)face_count);
        memcpy(bytes + offset + 8, face_kernel, (size_t)face_count);
        offset += 8 + face_count;
    }
    if( face_detail_strength )
    {
        wr16(bytes + offset, OBTORI_SECTION_FACE_DETAIL_STRENGTH);
        wr16(bytes + offset + 2, 0);
        wr32(bytes + offset + 4, (uint32_t)face_count);
        memcpy(bytes + offset + 8, face_detail_strength, (size_t)face_count);
        offset += 8 + face_count;
    }
    if( face_detail_texture )
    {
        wr16(bytes + offset, OBTORI_SECTION_FACE_DETAIL_TEXTURE);
        wr16(bytes + offset + 2, 0);
        wr32(bytes + offset + 4, (uint32_t)(face_count * 2));
        for( int f = 0; f < face_count; f++ )
            wr16(bytes + offset + 8 + f * 2, (uint16_t)face_detail_texture[f]);
        offset += 8 + face_count * 2;
    }
    return offset;
}
