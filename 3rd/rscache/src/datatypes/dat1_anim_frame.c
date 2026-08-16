#include "dat1_anim_frame.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct RSCache_Dat1AnimBaseFrames*
RSCache_Dat1AnimBaseFramesNewDecode(
    char* data,
    int data_size)
{
    assert(data != NULL);
    assert(data_size >= 8);

    struct RSCache_Dat1AnimBaseFrames* animbaseframes =
        malloc(sizeof(struct RSCache_Dat1AnimBaseFrames));
    if( !animbaseframes )
        return NULL;
    memset(animbaseframes, 0, sizeof(struct RSCache_Dat1AnimBaseFrames));

    struct RSCache_Buffer buffer = { .data = (uint8_t*)(data),
                                     .size = (uint32_t)(data_size),
                                     .position = 0 };
    struct RSCache_Buffer head_buffer = { .data = (uint8_t*)(data),
                                          .size = (uint32_t)(data_size),
                                          .position = 0 };
    struct RSCache_Buffer tran1_buffer = { .data = (uint8_t*)(data),
                                           .size = (uint32_t)(data_size),
                                           .position = 0 };
    struct RSCache_Buffer tran2_buffer = { .data = (uint8_t*)(data),
                                           .size = (uint32_t)(data_size),
                                           .position = 0 };
    struct RSCache_Buffer del_buffer = { .data = (uint8_t*)(data),
                                         .size = (uint32_t)(data_size),
                                         .position = 0 };

    buffer.position = data_size - 8;

    int head_length = g2(&buffer);
    int tran1_length = g2(&buffer);
    int tran2_length = g2(&buffer);
    int del_length = g2(&buffer);

    int pos = 0;
    head_buffer.position = pos;

    pos += head_length + 2;
    tran1_buffer.position = pos;

    pos += tran1_length;
    tran2_buffer.position = pos;

    pos += tran2_length;
    del_buffer.position = pos;

    pos += del_length;
    animbaseframes->base = RSCache_Dat1AnimBaseNewDecode(data + pos, data_size - pos);
    if( !animbaseframes->base )
    {
        free(animbaseframes);
        return NULL;
    }

    int total = g2(&head_buffer);
    static int labels[500];
    static int x[500];
    static int y[500];
    static int z[500];
    static bool synthesized_scratch[500];
    memset(labels, 0, 500 * sizeof(int));
    memset(x, 0, 500 * sizeof(int));
    memset(y, 0, 500 * sizeof(int));
    memset(z, 0, 500 * sizeof(int));
    memset(synthesized_scratch, 0, 500 * sizeof(bool));

    animbaseframes->frames = malloc((size_t)total * sizeof(struct RSCache_Dat1AnimFrame));
    if( !animbaseframes->frames )
    {
        RSCache_Dat1AnimBaseFramesFree(animbaseframes);
        return NULL;
    }
    memset(animbaseframes->frames, 0, (size_t)total * sizeof(struct RSCache_Dat1AnimFrame));
    animbaseframes->frame_count = total;

    for( int i = 0; i < total; i++ )
    {
        struct RSCache_Dat1AnimFrame* animframe = &animbaseframes->frames[i];
        int id = g2(&head_buffer);

        animframe->id = id;
        animframe->base = animbaseframes->base;
        animframe->delay = g1(&del_buffer);

        int group_count = g1(&head_buffer);
        animframe->flag_count = group_count;
        if( group_count > 0 )
        {
            animframe->bone_flags = malloc((size_t)group_count);
            if( !animframe->bone_flags )
            {
                RSCache_Dat1AnimBaseFramesFree(animbaseframes);
                return NULL;
            }
            memcpy(
                animframe->bone_flags,
                tran1_buffer.data + tran1_buffer.position,
                (size_t)group_count);
        }

        int last_group = -1;
        int current = 0;
        memset(synthesized_scratch, 0, sizeof(synthesized_scratch));

        for( int j = 0; j < group_count; j++ )
        {
            int flags = g1(&tran1_buffer);
            if( flags > 0 )
            {
                if( animframe->base->types[j] != 0 )
                {
                    for( int group = j - 1; group > last_group; group-- )
                    {
                        if( animframe->base->types[group] == 0 )
                        {
                            labels[current] = group;
                            x[current] = 0;
                            y[current] = 0;
                            z[current] = 0;
                            synthesized_scratch[current] = true;
                            current++;
                            break;
                        }
                    }
                }

                labels[current] = j;
                synthesized_scratch[current] = false;

                int default_value = 0;
                if( animframe->base->types[labels[current]] == 3 )
                {
                    default_value = 128;
                }

                if( (flags & 1) == 0 )
                {
                    x[current] = default_value;
                }
                else
                {
                    x[current] = gshortsmart(&tran2_buffer);
                }

                if( (flags & 2) == 0 )
                {
                    y[current] = default_value;
                }
                else
                {
                    y[current] = gshortsmart(&tran2_buffer);
                }

                if( (flags & 4) == 0 )
                {
                    z[current] = default_value;
                }
                else
                {
                    z[current] = gshortsmart(&tran2_buffer);
                }

                last_group = j;
                current++;
            }
        }

        animframe->length = current;
        if( current > 0 )
        {
            animframe->groups = malloc((size_t)current * sizeof(int16_t));
            animframe->x = malloc((size_t)current * sizeof(int16_t));
            animframe->y = malloc((size_t)current * sizeof(int16_t));
            animframe->z = malloc((size_t)current * sizeof(int16_t));
            animframe->synthesized = calloc((size_t)current, sizeof(bool));
            if( !animframe->groups || !animframe->x || !animframe->y || !animframe->z ||
                !animframe->synthesized )
            {
                RSCache_Dat1AnimBaseFramesFree(animbaseframes);
                return NULL;
            }
            for( int j = 0; j < current; j++ )
            {
                animframe->groups[j] = (int16_t)labels[j];
                animframe->x[j] = (int16_t)x[j];
                animframe->y[j] = (int16_t)y[j];
                animframe->z[j] = (int16_t)z[j];
                animframe->synthesized[j] = synthesized_scratch[j];
            }
        }
    }

    return animbaseframes;
}

struct RSCache_Dat1AnimBase*
RSCache_Dat1AnimBaseNewDecode(
    char* data,
    int data_size)
{
    assert(data != NULL);
    (void)data_size;

    struct RSCache_Dat1AnimBase* animbase = malloc(sizeof(struct RSCache_Dat1AnimBase));
    if( !animbase )
        return NULL;
    memset(animbase, 0, sizeof(struct RSCache_Dat1AnimBase));

    struct RSCache_Buffer buffer = { .data = (uint8_t*)(data),
                                     .size = (uint32_t)(data_size),
                                     .position = 0 };

    int length = g1(&buffer);
    animbase->length = length;
    animbase->types = malloc((size_t)length * sizeof(uint8_t));
    animbase->labels = malloc((size_t)length * sizeof(uint8_t*));
    animbase->label_counts = malloc((size_t)length * sizeof(uint16_t));
    if( !animbase->types || !animbase->labels || !animbase->label_counts )
    {
        free(animbase->types);
        free(animbase->labels);
        free(animbase->label_counts);
        free(animbase);
        return NULL;
    }
    memset(animbase->types, 0, (size_t)length * sizeof(uint8_t));
    memset(animbase->labels, 0, (size_t)length * sizeof(uint8_t*));
    memset(animbase->label_counts, 0, (size_t)length * sizeof(uint16_t));

    for( int i = 0; i < length; i++ )
    {
        animbase->types[i] = (uint8_t)g1(&buffer);
    }

    for( int i = 0; i < length; i++ )
    {
        int count = g1(&buffer);
        animbase->labels[i] = malloc((size_t)count * sizeof(uint8_t));
        if( !animbase->labels[i] )
        {
            for( int k = 0; k < i; k++ )
                free(animbase->labels[k]);
            free(animbase->labels);
            free(animbase->label_counts);
            free(animbase->types);
            free(animbase);
            return NULL;
        }
        animbase->label_counts[i] = (uint16_t)count;
        for( int j = 0; j < count; j++ )
            animbase->labels[i][j] = (uint8_t)g1(&buffer);
    }
    return animbase;
}

static void
cache_dat_animbase_free(struct RSCache_Dat1AnimBase* base)
{
    if( !base )
        return;
    if( base->labels )
    {
        for( int i = 0; i < base->length; i++ )
            free(base->labels[i]);
        free(base->labels);
    }
    free(base->label_counts);
    free(base->types);
    free(base);
}

void
RSCache_Dat1AnimFrameFreeInplace(struct RSCache_Dat1AnimFrame* frame)
{
    if( !frame )
        return;
    free(frame->groups);
    free(frame->x);
    free(frame->y);
    free(frame->z);
    free(frame->bone_flags);
    free(frame->synthesized);
    memset(frame, 0, sizeof(*frame));
}

void
RSCache_Dat1AnimFrameFree(struct RSCache_Dat1AnimFrame* animframe)
{
    if( !animframe )
        return;
    RSCache_Dat1AnimFrameFreeInplace(animframe);
    free(animframe);
}

void
RSCache_Dat1AnimBaseFramesFree(struct RSCache_Dat1AnimBaseFrames* abf)
{
    if( !abf )
        return;
    if( abf->frames )
    {
        for( int i = 0; i < abf->frame_count; i++ )
            RSCache_Dat1AnimFrameFreeInplace(&abf->frames[i]);
        free(abf->frames);
    }
    cache_dat_animbase_free(abf->base);
    free(abf);
}

uint32_t
RSCache_Dat1AnimBaseEncodeBound(const struct RSCache_Dat1AnimBase* base)
{
    if( !base )
        return 0;
    uint32_t bound = 1;
    bound += (uint32_t)base->length; /* types */
    for( int i = 0; i < base->length; i++ )
        bound += 1u + (uint32_t)base->label_counts[i];
    return bound;
}

uint32_t
RSCache_Dat1AnimBaseEncode(
    const struct RSCache_Dat1AnimBase* base,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(base != NULL);
    assert(out != NULL);

    if( out_capacity < RSCache_Dat1AnimBaseEncodeBound(base) )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    p1(&buffer, base->length);
    for( int i = 0; i < base->length; i++ )
        p1(&buffer, base->types[i]);
    for( int i = 0; i < base->length; i++ )
    {
        p1(&buffer, base->label_counts[i]);
        for( int j = 0; j < base->label_counts[i]; j++ )
            p1(&buffer, base->labels[i][j]);
    }
    return buffer.position;
}

static int
frame_group_count_for_encode(const struct RSCache_Dat1AnimFrame* frame)
{
    if( frame->flag_count > 0 && frame->bone_flags )
        return frame->flag_count;

    /* Fallback without provenance: span from 0 to highest group index+1. */
    int max_group = -1;
    for( int i = 0; i < frame->length; i++ )
    {
        if( frame->groups[i] > max_group )
            max_group = frame->groups[i];
    }
    return max_group + 1;
}

uint32_t
RSCache_Dat1AnimBaseFramesEncodeBound(const struct RSCache_Dat1AnimBaseFrames* abf)
{
    if( !abf || !abf->base )
        return 0;

    uint32_t head = 2; /* total */
    uint32_t tran1 = 0;
    uint32_t tran2 = 0;
    uint32_t del = (uint32_t)abf->frame_count;

    for( int i = 0; i < abf->frame_count; i++ )
    {
        const struct RSCache_Dat1AnimFrame* frame = &abf->frames[i];
        int gc = frame_group_count_for_encode(frame);
        head += 3; /* id + group_count */
        tran1 += (uint32_t)gc;
        /* Each real entry can contribute up to 3 shortsmarts (2 bytes each). */
        for( int j = 0; j < frame->length; j++ )
        {
            if( frame->synthesized && frame->synthesized[j] )
                continue;
            tran2 += 6;
        }
    }

    return head + tran1 + tran2 + del + RSCache_Dat1AnimBaseEncodeBound(abf->base) + 8;
}

uint32_t
RSCache_Dat1AnimBaseFramesEncode(
    const struct RSCache_Dat1AnimBaseFrames* abf,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(abf != NULL);
    assert(abf->base != NULL);
    assert(out != NULL);

    uint32_t bound = RSCache_Dat1AnimBaseFramesEncodeBound(abf);
    if( out_capacity < bound )
        return 0;

    /* Build sections into temporary buffers, then assemble with trailer. */
    uint32_t head_cap = 2u + (uint32_t)abf->frame_count * 3u;
    uint32_t tran1_cap = 0;
    uint32_t tran2_cap = 0;
    for( int i = 0; i < abf->frame_count; i++ )
    {
        tran1_cap += (uint32_t)frame_group_count_for_encode(&abf->frames[i]);
        tran2_cap += (uint32_t)abf->frames[i].length * 6u;
    }
    uint32_t del_cap = (uint32_t)abf->frame_count;

    uint8_t* head = malloc(head_cap ? head_cap : 1);
    uint8_t* tran1 = malloc(tran1_cap ? tran1_cap : 1);
    uint8_t* tran2 = malloc(tran2_cap ? tran2_cap : 1);
    uint8_t* del = malloc(del_cap ? del_cap : 1);
    if( !head || !tran1 || !tran2 || !del )
    {
        free(head);
        free(tran1);
        free(tran2);
        free(del);
        return 0;
    }

    struct RSCache_Buffer head_b, tran1_b, tran2_b, del_b;
    RSCache_BufferInit(&head_b, head, head_cap);
    RSCache_BufferInit(&tran1_b, tran1, tran1_cap ? tran1_cap : 1);
    RSCache_BufferInit(&tran2_b, tran2, tran2_cap ? tran2_cap : 1);
    RSCache_BufferInit(&del_b, del, del_cap ? del_cap : 1);

    p2(&head_b, abf->frame_count);

    for( int i = 0; i < abf->frame_count; i++ )
    {
        const struct RSCache_Dat1AnimFrame* frame = &abf->frames[i];
        int group_count = frame_group_count_for_encode(frame);

        p2(&head_b, frame->id);
        p1(&head_b, group_count);
        p1(&del_b, frame->delay);

        if( frame->flag_count > 0 && frame->bone_flags )
        {
            for( int j = 0; j < frame->flag_count; j++ )
                p1(&tran1_b, frame->bone_flags[j]);

            for( int j = 0; j < frame->length; j++ )
            {
                if( frame->synthesized && frame->synthesized[j] )
                    continue;
                int bone = frame->groups[j];
                if( bone < 0 || bone >= frame->flag_count )
                {
                    free(head);
                    free(tran1);
                    free(tran2);
                    free(del);
                    return 0;
                }
                int flags = frame->bone_flags[bone];
                if( flags & 1 )
                    pshortsmart(&tran2_b, frame->x[j]);
                if( flags & 2 )
                    pshortsmart(&tran2_b, frame->y[j]);
                if( flags & 4 )
                    pshortsmart(&tran2_b, frame->z[j]);
            }
        }
        else
        {
            /* Semantic fallback: one non-zero flag byte per real group index. */
            uint8_t* flags_tmp = calloc((size_t)group_count, 1);
            if( group_count > 0 && !flags_tmp )
            {
                free(head);
                free(tran1);
                free(tran2);
                free(del);
                return 0;
            }
            for( int j = 0; j < frame->length; j++ )
            {
                int bone = frame->groups[j];
                if( bone < 0 || bone >= group_count )
                {
                    free(flags_tmp);
                    free(head);
                    free(tran1);
                    free(tran2);
                    free(del);
                    return 0;
                }
                int flags = 0;
                int default_value = (abf->base->types[bone] == 3) ? 128 : 0;
                if( frame->x[j] != default_value )
                    flags |= 1;
                if( frame->y[j] != default_value )
                    flags |= 2;
                if( frame->z[j] != default_value )
                    flags |= 4;
                if( flags == 0 )
                    flags = 7; /* force presence so the entry is not dropped */
                flags_tmp[bone] = (uint8_t)flags;
            }
            for( int j = 0; j < group_count; j++ )
                p1(&tran1_b, flags_tmp[j]);
            for( int j = 0; j < frame->length; j++ )
            {
                int bone = frame->groups[j];
                int flags = flags_tmp[bone];
                if( flags & 1 )
                    pshortsmart(&tran2_b, frame->x[j]);
                if( flags & 2 )
                    pshortsmart(&tran2_b, frame->y[j]);
                if( flags & 4 )
                    pshortsmart(&tran2_b, frame->z[j]);
            }
            free(flags_tmp);
        }
    }

    /* head_length excludes the leading u16 total. */
    uint32_t head_length = head_b.position >= 2 ? head_b.position - 2 : 0;
    uint32_t tran1_length = tran1_b.position;
    uint32_t tran2_length = tran2_b.position;
    uint32_t del_length = del_b.position;

    /* Trailer stores each section length as g2/p2. Oversized archives (e.g.
     * hundreds of related frames in one file) silently truncated and decoded
     * with a zeroed AnimBase — refuse instead. */
    if( head_length > 0xFFFFu || tran1_length > 0xFFFFu || tran2_length > 0xFFFFu ||
        del_length > 0xFFFFu )
    {
        free(head);
        free(tran1);
        free(tran2);
        free(del);
        return 0;
    }

    uint32_t base_bound = RSCache_Dat1AnimBaseEncodeBound(abf->base);
    uint8_t* base_bytes = malloc(base_bound ? base_bound : 1);
    if( !base_bytes )
    {
        free(head);
        free(tran1);
        free(tran2);
        free(del);
        return 0;
    }
    uint32_t base_size = RSCache_Dat1AnimBaseEncode(abf->base, base_bytes, base_bound);
    if( base_size == 0 )
    {
        free(base_bytes);
        free(head);
        free(tran1);
        free(tran2);
        free(del);
        return 0;
    }

    struct RSCache_Buffer out_b;
    RSCache_BufferInit(&out_b, out, out_capacity);
    pbuf(&out_b, head, (int)head_b.position);
    pbuf(&out_b, tran1, (int)tran1_length);
    pbuf(&out_b, tran2, (int)tran2_length);
    pbuf(&out_b, del, (int)del_length);
    pbuf(&out_b, base_bytes, (int)base_size);
    p2(&out_b, (int)head_length);
    p2(&out_b, (int)tran1_length);
    p2(&out_b, (int)tran2_length);
    p2(&out_b, (int)del_length);

    free(base_bytes);
    free(head);
    free(tran1);
    free(tran2);
    free(del);
    return out_b.position;
}
