#include "painter_fuzz_diff.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool
terrain_has(const PainterFuzzDrawnSet* d, uint64_t key)
{
    for( int i = 0; i < d->terrain_n; i++ )
        if( d->terrain[i] == key )
            return true;
    return false;
}

static bool
element_has(const PainterFuzzDrawnSet* d, uint32_t key)
{
    for( int i = 0; i < d->element_n; i++ )
        if( d->elements[i] == key )
            return true;
    return false;
}

static void
terrain_add(PainterFuzzDrawnSet* d, int x, int z, int level)
{
    uint64_t key = ((uint64_t)(uint32_t)level << 32) | ((uint64_t)(uint32_t)z << 16) |
                   (uint64_t)(uint32_t)x;
    if( terrain_has(d, key) )
        return;
    if( d->terrain_n >= PAINTER_FUZZ_MAX_TERRAIN )
        return;
    d->terrain[d->terrain_n++] = key;
}

static void
element_add(PainterFuzzDrawnSet* d, uint32_t entity)
{
    if( element_has(d, entity) )
        return;
    if( d->element_n >= PAINTER_FUZZ_MAX_ELEMENT )
        return;
    d->elements[d->element_n++] = entity;
}

static int
classify_entity(uint32_t id)
{
    if( id >= 1000 && id < 2000 )
        return 1;
    if( id >= 2000 && id < 3000 )
        return 2;
    if( id >= 3000 && id < 4000 )
        return 3;
    if( id >= 4000 && id < 5000 )
        return 4;
    if( id >= 5000 && id < 6000 )
        return 5;
    return 0;
}

void
painter_fuzz_drawn_from_buffer(PainterFuzzDrawnSet* d, const struct PaintersBuffer* buf)
{
    memset(d, 0, sizeof(*d));
    for( int i = 0; i < buf->command_count; i++ )
    {
        const struct PaintersElementCommand* cmd = &buf->commands[i];
        if( cmd->_bf_kind == PNTR_CMD_TERRAIN )
        {
            terrain_add(
                d,
                (int)cmd->_terrain._bf_terrain_x,
                (int)cmd->_terrain._bf_terrain_z,
                (int)cmd->_terrain._bf_terrain_y);
        }
        else if( cmd->_bf_kind == PNTR_CMD_ELEMENT )
        {
            element_add(d, cmd->_entity._bf_entity);
        }
    }
}

int
painter_fuzz_compare_superset(
    const PainterFuzzDrawnSet* reference,
    const PainterFuzzDrawnSet* bucket,
    int* missing_terrain,
    int* missing_elements)
{
    int mt = 0;
    int me = 0;
    for( int i = 0; i < reference->terrain_n; i++ )
        if( !terrain_has(bucket, reference->terrain[i]) )
            mt++;
    for( int i = 0; i < reference->element_n; i++ )
        if( !element_has(bucket, reference->elements[i]) )
            me++;
    *missing_terrain = mt;
    *missing_elements = me;
    return (mt == 0 && me == 0) ? 0 : 1;
}

void
painter_fuzz_print_drawn_miss(
    const PainterFuzzDrawnSet* reference,
    const PainterFuzzDrawnSet* bucket,
    int max_print)
{
    int n = 0;
    for( int i = 0; i < reference->terrain_n && n < max_print; i++ )
    {
        uint64_t key = reference->terrain[i];
        if( !terrain_has(bucket, key) )
        {
            int x = (int)(key & 0xffffu);
            int z = (int)((key >> 16) & 0xffffu);
            int level = (int)(key >> 32);
            printf("  missing terrain (%d,%d,%d)\n", x, z, level);
            n++;
        }
    }
    n = 0;
    for( int i = 0; i < reference->element_n && n < max_print; i++ )
    {
        uint32_t e = reference->elements[i];
        if( !element_has(bucket, e) )
        {
            printf("  missing element %u\n", (unsigned)e);
            n++;
        }
    }
}

void
painter_fuzz_drawn_counts_from_set(const PainterFuzzDrawnSet* d, int out[5])
{
    for( int i = 0; i < 5; i++ )
        out[i] = 0;
    for( int i = 0; i < d->element_n; i++ )
    {
        int cat = classify_entity(d->elements[i]);
        if( cat >= 1 && cat <= 5 )
            out[cat - 1]++;
    }
}
