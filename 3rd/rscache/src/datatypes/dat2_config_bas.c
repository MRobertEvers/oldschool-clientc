#include "dat2_config_bas.h"

#include "../rsbuffer.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Opcode widths match rs-map-viewer/src/rs/config/bastype/BasType.ts.
 * Opcode 1 is idle+walk (two u16s); opcode 6 is run (not idle).
 */
static void
decode_bas_type(
    struct RSCache_Dat2ConfigBas* bas,
    struct RSCache_Buffer* buffer)
{
    for( ;; )
    {
        int opcode = g1(buffer);
        if( opcode == 0 )
        {
            bas->_consumed = (int)buffer->position;
            return;
        }

        if( opcode == 1 )
        {
            bas->idle_seq_id = g2(buffer);
            bas->walk_seq_id = g2(buffer);
            if( bas->idle_seq_id == 0xFFFF )
                bas->idle_seq_id = -1;
            if( bas->walk_seq_id == 0xFFFF )
                bas->walk_seq_id = -1;
        }
        else if( opcode >= 2 && opcode <= 9 )
        {
            /* crawl/run variants — u16 each */
            g2(buffer);
        }
        else if( opcode == 26 )
        {
            g1(buffer);
            g1(buffer);
        }
        else if( opcode == 27 )
        {
            int body_part = g1(buffer);
            (void)body_part;
            for( int i = 0; i < 6; i++ )
                g2(buffer); /* signed short on the wire; value unused here */
        }
        else if( opcode == 29 )
        {
            g1(buffer);
        }
        else if( opcode == 30 )
        {
            g2(buffer);
        }
        else if( opcode == 31 )
        {
            g1(buffer);
        }
        else if( opcode == 32 )
        {
            g2(buffer);
        }
        else if( opcode == 33 )
        {
            g2(buffer);
        }
        else if( opcode == 34 )
        {
            g1(buffer);
        }
        else if( opcode == 35 )
        {
            g2(buffer);
        }
        else if( opcode == 36 )
        {
            g2(buffer);
        }
        else if( opcode == 37 )
        {
            g1(buffer);
        }
        else if( opcode == 38 || opcode == 39 )
        {
            g2(buffer); /* idle left/right */
        }
        else if( opcode == 40 )
        {
            bas->walk_back_seq_id = g2(buffer);
            if( bas->walk_back_seq_id == 0xFFFF )
                bas->walk_back_seq_id = -1;
        }
        else if( opcode == 41 )
        {
            bas->walk_left_seq_id = g2(buffer);
            if( bas->walk_left_seq_id == 0xFFFF )
                bas->walk_left_seq_id = -1;
        }
        else if( opcode == 42 )
        {
            bas->walk_right_seq_id = g2(buffer);
            if( bas->walk_right_seq_id == 0xFFFF )
                bas->walk_right_seq_id = -1;
        }
        else if( opcode >= 43 && opcode <= 51 )
        {
            g2(buffer);
        }
        else if( opcode == 52 )
        {
            int count = g1(buffer);
            for( int i = 0; i < count; i++ )
            {
                g2(buffer);
                g1(buffer);
            }
        }
        else if( opcode == 53 )
        {
            /* flag, no payload */
        }
        else if( opcode == 54 )
        {
            /* rs-map-viewer lists two shapes for 54; both start with two bytes we
             * can consume as u8s. The alternate 1+3×u16 form is not seen on the
             * 643 corpus under the two-u8 reading (exact consumption). */
            g1(buffer);
            g1(buffer);
        }
        else if( opcode == 55 )
        {
            g1(buffer);
            g2(buffer);
        }
        else
        {
            printf("BasType: Unknown opcode: %d\n", opcode);
            return;
        }
    }
}

struct RSCache_Dat2ConfigBas*
RSCache_Dat2ConfigBasNewDecode(
    char* data,
    int data_size)
{
    struct RSCache_Dat2ConfigBas* bas = calloc(1, sizeof(*bas));
    if( !bas )
        return NULL;

    bas->idle_seq_id = -1;
    bas->walk_seq_id = -1;
    bas->walk_back_seq_id = -1;
    bas->walk_left_seq_id = -1;
    bas->walk_right_seq_id = -1;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    decode_bas_type(bas, &buffer);
    return bas;
}

void
RSCache_Dat2ConfigBasFree(struct RSCache_Dat2ConfigBas* bas)
{
    free(bas);
}
