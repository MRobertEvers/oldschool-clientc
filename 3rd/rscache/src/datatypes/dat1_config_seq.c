#include "dat1_config_seq.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SeqType.unpack (LostCity JavaClient):
// code 1: frameCount, then per frame: frame g2, iframe g2 (65535 -> -1), delay g2
// code 2: loops        code 3: walkmerge list (terminated with 9999999)
// code 4: stretches    code 5: priority
// code 6/7: replaceheldleft/right   code 8: maxloops
// code 9/10: preanim_move/postanim_move   code 11: duplicatebehavior
static void
init_seq(struct RSCache_Dat1ConfigSeq* seq)
{
    memset(seq, 0, sizeof(*seq));
    seq->loops = -1;
    seq->priority = 5;
    seq->replaceheldleft = -1;
    seq->replaceheldright = -1;
    seq->maxloops = 99;
    seq->preanim_move = -1;
    seq->postanim_move = -1;
    seq->duplicate_behavior = -1;
}

int
RSCache_Dat1ConfigSeqDecodeInplace(
    struct RSCache_Dat1ConfigSeq* seq,
    char* data,
    int data_size)
{
    struct RSCache_Buffer buffer;

    init_seq(seq);
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    while( true )
    {
        int opcode = g1(&buffer);
        if( opcode == 0 )
            break;

        switch( opcode )
        {
        case 1:
        {
            seq->frame_count = g1(&buffer);
            seq->frames = calloc((size_t)seq->frame_count, sizeof(int));
            seq->iframes = calloc((size_t)seq->frame_count, sizeof(int));
            seq->delay = calloc((size_t)seq->frame_count, sizeof(int));
            if( !seq->frames || !seq->iframes || !seq->delay )
            {
                seq->frame_count = 0;
                return (int)buffer.position;
            }

            for( int i = 0; i < seq->frame_count; i++ )
            {
                int iframe;
                seq->frames[i] = g2(&buffer);
                iframe = g2(&buffer);
                seq->iframes[i] = iframe == 65535 ? -1 : iframe;
                seq->delay[i] = g2(&buffer);
            }
            break;
        }
        case 2:
            seq->loops = g2(&buffer);
            break;
        case 3:
        {
            int count = g1(&buffer);
            seq->walkmerge = calloc((size_t)count + 1, sizeof(int));
            if( !seq->walkmerge )
                return (int)buffer.position;
            for( int i = 0; i < count; i++ )
                seq->walkmerge[i] = g1(&buffer);
            seq->walkmerge[count] = 9999999;
            break;
        }
        case 4:
            seq->stretches = true;
            break;
        case 5:
            seq->priority = g1(&buffer);
            break;
        case 6:
            seq->replaceheldleft = g2(&buffer);
            break;
        case 7:
            seq->replaceheldright = g2(&buffer);
            break;
        case 8:
            seq->maxloops = g1(&buffer);
            break;
        case 9:
            seq->preanim_move = g1(&buffer);
            break;
        case 10:
            seq->postanim_move = g1(&buffer);
            break;
        case 11:
            seq->duplicate_behavior = g1(&buffer);
            break;
        default:
            /* An unknown opcode desynchronises the rest of the table (entries
             * are only sequentially addressable), so stop here rather than
             * decode garbage into later ids. */
            printf("Unrecognized dat1 seq opcode %d\n", opcode);
            return (int)buffer.position;
        }
    }

    return (int)buffer.position;
}

void
RSCache_Dat1ConfigSeqFreeInplace(struct RSCache_Dat1ConfigSeq* seq)
{
    if( !seq )
        return;
    free(seq->frames);
    free(seq->iframes);
    free(seq->delay);
    free(seq->walkmerge);
    memset(seq, 0, sizeof(*seq));
}

struct RSCache_Dat1ConfigSeqList*
RSCache_Dat1ConfigSeqListNewDecode(
    char* data,
    int data_size)
{
    struct RSCache_Dat1ConfigSeqList* list;
    struct RSCache_Buffer buffer;
    int count;

    if( !data || data_size <= 0 )
        return NULL;

    list = malloc(sizeof(*list));
    if( !list )
        return NULL;
    memset(list, 0, sizeof(*list));

    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    count = g2(&buffer);
    if( count <= 0 )
        return list;

    list->seqs = calloc((size_t)count, sizeof(*list->seqs));
    if( !list->seqs )
    {
        free(list);
        return NULL;
    }
    list->seqs_count = count;

    for( int i = 0; i < count; i++ )
    {
        buffer.position += (uint32_t)RSCache_Dat1ConfigSeqDecodeInplace(
            &list->seqs[i],
            (char*)buffer.data + buffer.position,
            (int)(buffer.size - buffer.position));
    }

    return list;
}

void
RSCache_Dat1ConfigSeqListFree(struct RSCache_Dat1ConfigSeqList* list)
{
    if( !list )
        return;
    for( int i = 0; i < list->seqs_count; i++ )
        RSCache_Dat1ConfigSeqFreeInplace(&list->seqs[i]);
    free(list->seqs);
    free(list);
}

static int
walkmerge_count(const struct RSCache_Dat1ConfigSeq* seq)
{
    if( !seq->walkmerge )
        return 0;
    int count = 0;
    while( seq->walkmerge[count] != 9999999 )
        count++;
    return count;
}

uint32_t
RSCache_Dat1ConfigSeqEncodeBound(const struct RSCache_Dat1ConfigSeq* seq)
{
    if( !seq )
        return 0;
    uint32_t bound = 1; /* terminator */
    if( seq->frame_count > 0 )
        bound += 2u + (uint32_t)seq->frame_count * 6u;
    bound += 3; /* loops */
    int wm = walkmerge_count(seq);
    if( wm > 0 )
        bound += 2u + (uint32_t)wm;
    bound += 1; /* stretches */
    bound += 2; /* priority */
    bound += 3; /* replaceheldleft */
    bound += 3; /* replaceheldright */
    bound += 2; /* maxloops */
    bound += 2; /* preanim_move */
    bound += 2; /* postanim_move */
    bound += 2; /* duplicate_behavior */
    return bound;
}

uint32_t
RSCache_Dat1ConfigSeqEncode(
    const struct RSCache_Dat1ConfigSeq* seq,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(seq != NULL);
    assert(out != NULL);

    if( out_capacity < RSCache_Dat1ConfigSeqEncodeBound(seq) )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    if( seq->frame_count > 0 )
    {
        assert(seq->frames && seq->iframes && seq->delay);
        p1(&buffer, 1);
        p1(&buffer, seq->frame_count);
        for( int i = 0; i < seq->frame_count; i++ )
        {
            p2(&buffer, seq->frames[i]);
            p2(&buffer, seq->iframes[i] == -1 ? 65535 : seq->iframes[i]);
            p2(&buffer, seq->delay[i]);
        }
    }
    if( seq->loops != -1 )
    {
        p1(&buffer, 2);
        p2(&buffer, seq->loops);
    }
    {
        int wm = walkmerge_count(seq);
        if( wm > 0 )
        {
            p1(&buffer, 3);
            p1(&buffer, wm);
            for( int i = 0; i < wm; i++ )
                p1(&buffer, seq->walkmerge[i]);
            /* Do not write the 9999999 sentinel the decoder appends. */
        }
    }
    if( seq->stretches )
        p1(&buffer, 4);
    if( seq->priority != 5 )
    {
        p1(&buffer, 5);
        p1(&buffer, seq->priority);
    }
    if( seq->replaceheldleft != -1 )
    {
        p1(&buffer, 6);
        p2(&buffer, seq->replaceheldleft);
    }
    if( seq->replaceheldright != -1 )
    {
        p1(&buffer, 7);
        p2(&buffer, seq->replaceheldright);
    }
    if( seq->maxloops != 99 )
    {
        p1(&buffer, 8);
        p1(&buffer, seq->maxloops);
    }
    if( seq->preanim_move != -1 )
    {
        p1(&buffer, 9);
        p1(&buffer, seq->preanim_move);
    }
    if( seq->postanim_move != -1 )
    {
        p1(&buffer, 10);
        p1(&buffer, seq->postanim_move);
    }
    if( seq->duplicate_behavior != -1 )
    {
        p1(&buffer, 11);
        p1(&buffer, seq->duplicate_behavior);
    }

    p1(&buffer, 0);
    return buffer.position;
}

uint32_t
RSCache_Dat1ConfigSeqListEncodeBound(const struct RSCache_Dat1ConfigSeqList* list)
{
    if( !list )
        return 0;
    uint32_t bound = 2;
    for( int i = 0; i < list->seqs_count; i++ )
        bound += RSCache_Dat1ConfigSeqEncodeBound(&list->seqs[i]);
    return bound;
}

uint32_t
RSCache_Dat1ConfigSeqListEncode(
    const struct RSCache_Dat1ConfigSeqList* list,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(list != NULL);
    assert(out != NULL);

    if( out_capacity < RSCache_Dat1ConfigSeqListEncodeBound(list) )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);
    p2(&buffer, list->seqs_count);
    for( int i = 0; i < list->seqs_count; i++ )
    {
        uint32_t written = RSCache_Dat1ConfigSeqEncode(
            &list->seqs[i],
            out + buffer.position,
            out_capacity - buffer.position);
        if( written == 0 && RSCache_Dat1ConfigSeqEncodeBound(&list->seqs[i]) != 0 )
            return 0;
        buffer.position += written;
    }
    return buffer.position;
}
