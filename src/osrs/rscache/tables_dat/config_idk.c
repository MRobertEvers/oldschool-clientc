#include "config_idk.h"

#include "../rsbuf.h"
// if (code === 1) {
//     this.type = dat.g1();
// } else if (code === 2) {
//     const count: number = dat.g1();
//     this.models = new Int32Array(count);

//     for (let i: number = 0; i < count; i++) {
//         this.models[i] = dat.g2();
//     }
// } else if (code === 3) {
//     this.disable = true;
// } else if (code >= 40 && code < 50) {
//     this.recol_s[code - 40] = dat.g2();
// } else if (code >= 50 && code < 60) {
//     this.recol_d[code - 50] = dat.g2();
// } else if (code >= 60 && code < 70) {
//     this.heads[code - 60] = dat.g2();
// } else {
//     console.log('Error unrecognised config code: ', code);
// }

static struct CacheDatConfigIdk*
decode_idk(struct RSBuffer* buffer)
{
    struct CacheDatConfigIdk* idk = malloc(sizeof(struct CacheDatConfigIdk));
    memset(idk, 0, sizeof(struct CacheDatConfigIdk));

    while( true )
    {
        if( buffer->position >= buffer->size )
        {
            assert(false && "Buffer position exceeded data size");
            return NULL;
        }

        int opcode = g1(&buffer);
        if( opcode == 0 )
        {
            break;
        }

        switch( opcode )
        {
        case 1:
            idk->type = g1(&buffer);
            break;
        case 2:
            idk->models_count = g1(&buffer);
            idk->models = malloc(idk->models_count * sizeof(int));
            memset(idk->models, 0, idk->models_count * sizeof(int));
            for( int i = 0; i < idk->models_count; i++ )
            {
                idk->models[i] = g2(&buffer);
            }
            break;
        case 3:
            idk->disable = true;
            break;
        case 40 ... 49:
            idk->recol_s[opcode - 40] = g2(&buffer);
            break;
        case 50 ... 59:
            idk->recol_d[opcode - 50] = g2(&buffer);
            break;
        case 60 ... 69:
            idk->heads[opcode - 60] = g2(&buffer);
            break;
        default:
            assert(false && "Unrecognized opcode");
            break;
        }
    }
    return idk;
}

struct CacheDatConfigIdkList*
cache_dat_config_idk_list_new_decode(
    void* jagfile_idkdat_data,
    int jagfile_idkdat_data_size)
{
    struct CacheDatConfigIdkList* idk_list = malloc(sizeof(struct CacheDatConfigIdkList));
    memset(idk_list, 0, sizeof(struct CacheDatConfigIdkList));

    struct RSBuffer buffer = { .data = jagfile_idkdat_data,
                               .size = jagfile_idkdat_data_size,
                               .position = 0 };

    int idk_count = g2(&buffer);
    idk_list->idks = malloc(idk_count * sizeof(struct CacheDatConfigIdk));
    memset(idk_list->idks, 0, idk_count * sizeof(struct CacheDatConfigIdk));

    idk_list->idks_count = idk_count;

    for( int i = 0; i < idk_count; i++ )
    {
        idk_list->idks[i] = decode_idk(&buffer);
        if( idk_list->idks[i] == NULL )
        {
            assert(false && "Failed to decode idk");
            return NULL;
        }
    }

    return idk_list;
}