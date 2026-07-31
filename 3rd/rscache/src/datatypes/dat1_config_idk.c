#include "dat1_config_idk.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

void
RSCache_Dat1ConfigIdkInit(struct RSCache_Dat1ConfigIdk* idk)
{
    memset(idk, 0, sizeof(struct RSCache_Dat1ConfigIdk));
}

bool
RSCache_Dat1ConfigIdkDecodeOp(
    struct RSCache_Dat1ConfigIdk* idk,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    (void)flags; /* dat1 idk has no era-dependent opcode. */

    switch( opcode )
    {
    case 1:
        idk->type = g1(buffer);
        break;
    case 2:
        idk->models_count = g1(buffer);
        idk->models = malloc(idk->models_count * sizeof(int));
        memset(idk->models, 0, idk->models_count * sizeof(int));
        for( int i = 0; i < idk->models_count; i++ )
        {
            idk->models[i] = g2(buffer);
        }
        break;
    case 3:
        idk->disable = true;
        break;
    case 40 ... 49:
        idk->recol_s[opcode - 40] = g2(buffer);
        break;
    case 50 ... 59:
        idk->recol_d[opcode - 50] = g2(buffer);
        break;
    case 60 ... 69:
        idk->heads[opcode - 60] = g2(buffer);
        break;
    default:
        return false;
    }

    /*
     * Recorded *after* the payload, so an opcode this handler declined never
     * reaches the encoder — replaying one it cannot write returns 0 for the
     * whole record. The order itself is data, not derivable: cache254's first
     * idk runs 1, 60, 2 (see the field's note in the header).
     */
    if( idk->opcode_count < (int)(sizeof(idk->opcodes) / sizeof(idk->opcodes[0])) )
        idk->opcodes[idk->opcode_count++] = opcode;
    return true;
}

static struct RSCache_Dat1ConfigIdk*
decode_idk(struct RSCache_Buffer* inb)
{
    struct RSCache_Dat1ConfigIdk* idk = malloc(sizeof(struct RSCache_Dat1ConfigIdk));

    RSCache_Dat1ConfigIdkInit(idk);

    struct RSCache_Buffer buffer = *inb;

    while( true )
    {
        if( buffer.position >= buffer.size )
        {
            assert(false && "Buffer position exceeded data size");
            RSCache_Dat1ConfigIdkFree(idk);
            return NULL;
        }

        int opcode = g1(&buffer);
        if( opcode == 0 )
        {
            break;
        }

        if( !RSCache_Dat1ConfigIdkDecodeOp(idk, opcode, &buffer, 0) )
        {
            /* The records are one concatenated stream, so an unknown opcode
             * loses every id after this one, not just this one. */
            assert(false && "Unrecognized opcode");
            break;
        }
    }

    *inb = buffer;
    return idk;
}

struct RSCache_Dat1ConfigIdkList*
RSCache_Dat1ConfigIdkListNewDecode(
    void* jagfile_idkdat_data,
    int jagfile_idkdat_data_size)
{
    struct RSCache_Dat1ConfigIdkList* idk_list = malloc(sizeof(struct RSCache_Dat1ConfigIdkList));
    memset(idk_list, 0, sizeof(struct RSCache_Dat1ConfigIdkList));

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(
        &buffer,
        (uint8_t*)(jagfile_idkdat_data),
        (uint32_t)(jagfile_idkdat_data_size));

    int idk_count = g2(&buffer);
    idk_list->idks = malloc(idk_count * sizeof(struct RSCache_Dat1ConfigIdk*));
    memset(idk_list->idks, 0, idk_count * sizeof(struct RSCache_Dat1ConfigIdk*));

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

void
RSCache_Dat1ConfigIdkFreeInplace(struct RSCache_Dat1ConfigIdk* idk)
{
    if( !idk )
        return;
    free(idk->models);
    idk->models = NULL;
    idk->models_count = 0;
}

void
RSCache_Dat1ConfigIdkFree(struct RSCache_Dat1ConfigIdk* idk)
{
    if( !idk )
        return;
    RSCache_Dat1ConfigIdkFreeInplace(idk);
    free(idk);
}
uint32_t
RSCache_Dat1ConfigIdkEncodeBound(const struct RSCache_Dat1ConfigIdk* idk)
{
    /* type, disable, terminator, plus the three ten-slot tables and the models. */
    uint32_t need = 8u + 30u * 3u;

    if( !idk )
        return need;
    return need + (uint32_t)idk->models_count * 2u + 2u;
}

uint32_t
RSCache_Dat1ConfigIdkEncode(
    const struct RSCache_Dat1ConfigIdk* idk,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Buffer buffer;
    int i;

    if( !idk || !out )
        return 0;
    if( out_capacity < RSCache_Dat1ConfigIdkEncodeBound(idk) )
        return 0;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /*
     * The opcodes are replayed in the order the record carried them, not in
     * ascending order.
     *
     * These records are not sorted: cache254's first idk runs 1, 60, 2. An
     * ascending encoder produced the right length and the right values and still
     * did not match a single archive, because every record's bytes were permuted.
     * Only the decoded order reproduces the source.
     */
    for( i = 0; i < idk->opcode_count; i++ )
    {
        int opcode = idk->opcodes[i];

        p1(&buffer, opcode);
        if( opcode == 1 )
        {
            p1(&buffer, idk->type);
        }
        else if( opcode == 2 )
        {
            int m;

            p1(&buffer, idk->models_count);
            for( m = 0; m < idk->models_count; m++ )
                p2(&buffer, idk->models[m]);
        }
        else if( opcode == 3 )
        {
            /* A bare flag: the opcode byte is the whole payload. */
        }
        else if( opcode >= 40 && opcode < 50 )
        {
            p2(&buffer, idk->recol_s[opcode - 40]);
        }
        else if( opcode >= 50 && opcode < 60 )
        {
            p2(&buffer, idk->recol_d[opcode - 50]);
        }
        else if( opcode >= 60 && opcode < 70 )
        {
            p2(&buffer, idk->heads[opcode - 60]);
        }
        else
        {
            return 0;
        }
    }

    p1(&buffer, 0);
    return buffer.position;
}
